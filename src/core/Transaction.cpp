#include "Transaction.h"
#include "Proc.h"
#include "Config.h"
#include "AppImageBackend.h"
#include <glib.h>
#include <thread>
#include <mutex>
#include <filesystem>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
void runOnMainThread(std::function<void()> fn) {
    Proc::runOnMainThread(std::move(fn));
}

static Transaction::LogCallback s_globalLogCb = nullptr;
static Transaction::GlobalProgressCallback s_globalProgressCb = nullptr;
static Transaction::GlobalFinishCallback s_globalFinishCb = nullptr;
static std::mutex s_callbackMutex;

void broadcastLog(const std::string &line, bool isStderr) {
    std::string copy = line;
    runOnMainThread([copy, isStderr]() {
        std::lock_guard<std::mutex> lock(s_callbackMutex);
        if (s_globalLogCb) s_globalLogCb(copy, isStderr);
    });
}

void broadcastProgress(const std::string &actionTitle, const DownloadProgress &prog) {
    DownloadProgress copy = prog;
    std::string title = actionTitle;
    runOnMainThread([title, copy]() {
        std::lock_guard<std::mutex> lock(s_callbackMutex);
        if (s_globalProgressCb) s_globalProgressCb(title, copy);
    });
}

void broadcastFinish(bool success, const std::string &msg) {
    std::string copy = msg;
    runOnMainThread([success, copy]() {
        std::lock_guard<std::mutex> lock(s_callbackMutex);
        if (s_globalFinishCb) s_globalFinishCb(success, copy);
    });
}

std::string getSourceDisplayName(PkgSource src) {
    switch (src) {
        case PkgSource::XBPS: return "XBPS";
        case PkgSource::Flatpak: return "Flatpak";
        case PkgSource::AppImage: return "AppImage / Soar";
        case PkgSource::Source: return "Vouru";
    }
    return "Sistema";
}

static bool writeAll(int fd, const std::string &data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

// Persistent root session so the user only enters their password ONCE per Neko-Apps session
struct RootSession {
    std::mutex mtx;
    GPid pid = 0;
    int fdIn = -1;
    int fdOut = -1;
    int fdErr = -1;
    std::string outBuf;
    std::string errBuf;
    uint64_t seq = 0;
    bool ready = false;

    ~RootSession() {
        std::lock_guard<std::mutex> lock(mtx);
        shutdownLocked();
    }

    void shutdownLocked() {
        if (fdIn >= 0) {
            write(fdIn, "QUIT\n", 5);
            close(fdIn);
            fdIn = -1;
        }
        if (fdOut >= 0) {
            close(fdOut);
            fdOut = -1;
        }
        if (fdErr >= 0) {
            close(fdErr);
            fdErr = -1;
        }
        if (pid > 0) {
            int st = 0;
            for (int i = 0; i < 10; ++i) {
                pid_t r = waitpid(pid, &st, WNOHANG);
                if (r != 0) break;
                usleep(10000);
            }
            kill(pid, SIGTERM);
            waitpid(pid, &st, WNOHANG);
            g_spawn_close_pid(pid);
            pid = 0;
        }
        outBuf.clear();
        errBuf.clear();
        ready = false;
    }

    bool isAliveLocked() {
        if (!ready || pid <= 0 || fdIn < 0 || fdOut < 0 || fdErr < 0) return false;
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == 0) return true;
        shutdownLocked();
        return false;
    }

    bool ensureStartedLocked(std::function<void(const std::string &line, bool isStderr)> onLine) {
        if (isAliveLocked()) return true;

        std::string elev = Transaction::elevateCmd();
        if (elev.empty() && geteuid() != 0) return false;

        std::string helperPath = "/usr/libexec/neko-apps/neko-apps-helper";
        if (access(helperPath.c_str(), X_OK) != 0) {
            if (access("helper/neko-apps-helper", X_OK) == 0) helperPath = "helper/neko-apps-helper";
            else if (access("../helper/neko-apps-helper", X_OK) == 0) helperPath = "../helper/neko-apps-helper";
        }

        std::vector<const gchar*> argv;
        if (geteuid() != 0) {
            argv.push_back(elev.c_str());
        }
        argv.push_back(helperPath.c_str());
        argv.push_back("worker");
        argv.push_back(nullptr);

        GError *error = nullptr;
        gboolean ok = g_spawn_async_with_pipes(
            nullptr,
            const_cast<gchar**>(argv.data()),
            nullptr,
            static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
            nullptr,
            nullptr,
            &pid,
            &fdIn,
            &fdOut,
            &fdErr,
            &error
        );

        if (!ok) {
            if (error) {
                if (onLine) onLine(std::string("Error al iniciar el asistente de privilegios: ") + error->message, true);
                g_error_free(error);
            }
            shutdownLocked();
            return false;
        }

        auto setNonBlocking = [](int fd) {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        };
        setNonBlocking(fdOut);
        setNonBlocking(fdErr);

        char chunk[4096];
        bool outOpen = true;
        bool errOpen = true;
        bool gotReady = false;

        while (!gotReady && (outOpen || errOpen)) {
            struct pollfd pfd[2];
            nfds_t nfds = 0;
            int outIdx = -1, errIdx = -1;

            if (outOpen) {
                outIdx = nfds;
                pfd[nfds].fd = fdOut;
                pfd[nfds].events = POLLIN | POLLHUP | POLLERR;
                pfd[nfds].revents = 0;
                nfds++;
            }
            if (errOpen) {
                errIdx = nfds;
                pfd[nfds].fd = fdErr;
                pfd[nfds].events = POLLIN | POLLHUP | POLLERR;
                pfd[nfds].revents = 0;
                nfds++;
            }

            int ret = poll(pfd, nfds, 100);
            if (ret < 0 && errno == EINTR) continue;

            if (outIdx >= 0 && (pfd[outIdx].revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = read(fdOut, chunk, sizeof(chunk) - 1);
                if (n > 0) {
                    chunk[n] = '\0';
                    outBuf.append(chunk, static_cast<size_t>(n));
                    size_t pos = 0;
                    while (pos < outBuf.size()) {
                        size_t nextEnd = outBuf.find_first_of("\r\n", pos);
                        if (nextEnd == std::string::npos) break;
                        std::string line = outBuf.substr(pos, nextEnd - pos);
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                        if (outBuf[nextEnd] == '\r' && nextEnd + 1 < outBuf.size() && outBuf[nextEnd + 1] == '\n') pos = nextEnd + 2;
                        else pos = nextEnd + 1;
                        if (line == "__NEKO_WORKER_READY__") {
                            gotReady = true;
                            break;
                        } else if (!line.empty() && onLine) {
                            onLine(line, false);
                        }
                    }
                    if (pos > 0) outBuf.erase(0, pos);
                } else if (n == 0 || (pfd[outIdx].revents & (POLLHUP | POLLERR))) {
                    outOpen = false;
                }
            }

            if (errIdx >= 0 && (pfd[errIdx].revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = read(fdErr, chunk, sizeof(chunk) - 1);
                if (n > 0) {
                    chunk[n] = '\0';
                    errBuf.append(chunk, static_cast<size_t>(n));
                    size_t pos = 0;
                    while (pos < errBuf.size()) {
                        size_t nextEnd = errBuf.find_first_of("\r\n", pos);
                        if (nextEnd == std::string::npos) break;
                        std::string line = errBuf.substr(pos, nextEnd - pos);
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                        if (errBuf[nextEnd] == '\r' && nextEnd + 1 < errBuf.size() && errBuf[nextEnd + 1] == '\n') pos = nextEnd + 2;
                        else pos = nextEnd + 1;
                        if (!line.empty() && onLine) onLine(line, true);
                    }
                    if (pos > 0) errBuf.erase(0, pos);
                } else if (n == 0 || (pfd[errIdx].revents & (POLLHUP | POLLERR))) {
                    errOpen = false;
                }
            }
        }

        if (!gotReady) {
            shutdownLocked();
            return false;
        }

        ready = true;
        return true;
    }

    int runWorkerCommand(const std::vector<std::string> &args,
                         std::function<void(const std::string &line, bool isStderr)> onLine) {
        if (args.empty()) return 0;

        std::lock_guard<std::mutex> lock(mtx);
        if (!ensureStartedLocked(onLine)) {
            return 126;
        }

        ++seq;
        std::string outMarker = "__NEKO_DONE_" + std::to_string(seq) + "__:";
        std::string errMarker = "__NEKO_ERR_DONE_" + std::to_string(seq) + "__";

        std::string packet = "CMD " + std::to_string(seq) + " " + std::to_string(args.size()) + "\n";
        for (const auto &a : args) {
            packet += a + "\n";
        }

        if (!writeAll(fdIn, packet)) {
            shutdownLocked();
            return -1;
        }

        int exitCode = -1;
        bool outDone = false;
        bool errDone = false;
        bool outOpen = true;
        bool errOpen = true;
        char chunk[4096];

        auto drainOut = [&]() {
            size_t pos = 0;
            while (pos < outBuf.size()) {
                size_t nextEnd = outBuf.find_first_of("\r\n", pos);
                if (nextEnd == std::string::npos) break;
                std::string line = outBuf.substr(pos, nextEnd - pos);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (outBuf[nextEnd] == '\r' && nextEnd + 1 < outBuf.size() && outBuf[nextEnd + 1] == '\n') pos = nextEnd + 2;
                else pos = nextEnd + 1;

                if (line.rfind(outMarker, 0) == 0) {
                    try {
                        exitCode = std::stoi(line.substr(outMarker.size()));
                    } catch (...) {
                        exitCode = 1;
                    }
                    outDone = true;
                    break;
                } else if (!line.empty() && onLine) {
                    onLine(line, false);
                }
            }
            if (pos > 0) outBuf.erase(0, pos);
        };

        auto drainErr = [&]() {
            size_t pos = 0;
            while (pos < errBuf.size()) {
                size_t nextEnd = errBuf.find_first_of("\r\n", pos);
                if (nextEnd == std::string::npos) break;
                std::string line = errBuf.substr(pos, nextEnd - pos);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (errBuf[nextEnd] == '\r' && nextEnd + 1 < errBuf.size() && errBuf[nextEnd + 1] == '\n') pos = nextEnd + 2;
                else pos = nextEnd + 1;

                if (line == errMarker) {
                    errDone = true;
                    break;
                } else if (!line.empty() && onLine) {
                    onLine(line, true);
                }
            }
            if (pos > 0) errBuf.erase(0, pos);
        };

        drainOut();
        drainErr();

        while ((!outDone || !errDone) && (outOpen || errOpen)) {
            struct pollfd pfd[2];
            nfds_t nfds = 0;
            int outIdx = -1, errIdx = -1;

            if (outOpen && !outDone) {
                outIdx = nfds;
                pfd[nfds].fd = fdOut;
                pfd[nfds].events = POLLIN | POLLHUP | POLLERR;
                pfd[nfds].revents = 0;
                nfds++;
            }
            if (errOpen && !errDone) {
                errIdx = nfds;
                pfd[nfds].fd = fdErr;
                pfd[nfds].events = POLLIN | POLLHUP | POLLERR;
                pfd[nfds].revents = 0;
                nfds++;
            }
            if (nfds == 0) break;

            int ret = poll(pfd, nfds, 100);
            if (ret < 0 && errno == EINTR) continue;

            if (outIdx >= 0 && (pfd[outIdx].revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = read(fdOut, chunk, sizeof(chunk) - 1);
                if (n > 0) {
                    chunk[n] = '\0';
                    outBuf.append(chunk, static_cast<size_t>(n));
                    drainOut();
                } else if (n == 0 || (pfd[outIdx].revents & (POLLHUP | POLLERR))) {
                    outOpen = false;
                }
            }

            if (errIdx >= 0 && (pfd[errIdx].revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = read(fdErr, chunk, sizeof(chunk) - 1);
                if (n > 0) {
                    chunk[n] = '\0';
                    errBuf.append(chunk, static_cast<size_t>(n));
                    drainErr();
                } else if (n == 0 || (pfd[errIdx].revents & (POLLHUP | POLLERR))) {
                    errOpen = false;
                }
            }
        }

        if (!outDone) {
            shutdownLocked();
            return -1;
        }
        return exitCode;
    }

    bool ensureAuthenticated(std::function<void(const std::string &line, bool isStderr)> onLine = nullptr) {
        if (geteuid() == 0) return true;
        std::lock_guard<std::mutex> lock(mtx);
        return ensureStartedLocked(onLine);
    }
};

static RootSession s_rootSession;

int runElevatedScript(const std::string &shellCmd,
                      std::function<void(const std::string &line, bool isStderr)> onLine = nullptr) {
    if (shellCmd.find("masterdir") != std::string::npos) {
        std::string vpDir = Config::instance().voidPackagesDir();
        return s_rootSession.runWorkerCommand({"masterdir-cleanup", vpDir}, onLine);
    }

    auto mirrorPos = shellCmd.find("http");
    if (shellCmd.find("/etc/xbps.d/00-repository-main.conf") != std::string::npos && mirrorPos != std::string::npos) {
        auto endPos = shellCmd.find_first_of(" \"\'\n", mirrorPos);
        std::string url = (endPos != std::string::npos) ? shellCmd.substr(mirrorPos, endPos - mirrorPos) : shellCmd.substr(mirrorPos);
        auto currentPos = url.find("/current");
        if (currentPos != std::string::npos) url = url.substr(0, currentPos);
        return s_rootSession.runWorkerCommand({"set-mirror", url}, onLine);
    }

    static const std::vector<std::string> drivers = {
        "bluetooth", "printer", "amd", "intel", "nvidia-open", "nvidia-latest",
        "nvidia-580", "nvidia-470", "nvidia-390"
    };
    for (const auto &d : drivers) {
        if (shellCmd.find(d) != std::string::npos) {
            return s_rootSession.runWorkerCommand({"driver", d}, onLine);
        }
    }

    if (onLine) onLine("Comando no permitido por la política de seguridad estricta.", true);
    return 1;
}

// Helper to run commands inside the persistent root session (authenticating at most once per app run)
int runElevated(const std::string &binary, const std::vector<std::string> &args,
                std::function<void(const std::string &line, bool isStderr)> onLine = nullptr) {
    std::vector<std::string> workerArgs;
    if (binary == "xbps-install") {
        bool isUpgrade = false;
        bool isSync = false;
        std::string repoPath;
        std::vector<std::string> pkgs;

        for (size_t i = 0; i < args.size(); ++i) {
            const auto &a = args[i];
            if (a == "-Syu" || a == "-Su") {
                isUpgrade = true;
            } else if (a == "-S") {
                isSync = true;
            } else if (a.rfind("--repository=", 0) == 0) {
                if (repoPath.empty()) repoPath = a.substr(13);
            } else if (a == "-y" || a == "-f" || a == "--") {
                continue;
            } else if (!a.empty() && a[0] != '-') {
                pkgs.push_back(a);
            }
        }

        if (isUpgrade) {
            workerArgs.push_back("upgrade");
        } else if (isSync && pkgs.empty()) {
            workerArgs.push_back("sync");
        } else if (!repoPath.empty()) {
            workerArgs.push_back("install-local");
            workerArgs.push_back(repoPath);
            for (const auto &p : pkgs) workerArgs.push_back(p);
        } else {
            workerArgs.push_back("install");
            for (const auto &p : pkgs) workerArgs.push_back(p);
        }
    } else if (binary == "xbps-remove") {
        bool isClean = false;
        bool isOrphans = false;
        std::vector<std::string> pkgs;

        for (const auto &a : args) {
            if (a == "-O") isClean = true;
            else if (a == "-o") isOrphans = true;
            else if (a == "-y" || a == "-R" || a == "--") continue;
            else if (!a.empty() && a[0] != '-') pkgs.push_back(a);
        }

        if (isClean) {
            workerArgs.push_back("clean-cache");
        } else if (isOrphans) {
            workerArgs.push_back("remove-orphans");
        } else {
            workerArgs.push_back("remove");
            for (const auto &p : pkgs) workerArgs.push_back(p);
        }
    } else if (binary == "xbps-reconfigure") {
        workerArgs.push_back("reconfigure");
    } else {
        workerArgs.push_back(binary);
        for (const auto &a : args) workerArgs.push_back(a);
    }

    return s_rootSession.runWorkerCommand(workerArgs, onLine);
}

// Parses progress lines from xbps-install, flatpak, etc.
void parseProgressFromLine(const std::string &rawLine, DownloadProgress &prog, const std::string &fallbackTotal) {
    std::string line = rawLine;

    // Check for speed e.g. "3.2 MB/s" or "500 KB/s" or "1.2MB/s"
    auto speedPos = line.find("/s");
    if (speedPos == std::string::npos) speedPos = line.find("/sec");
    if (speedPos != std::string::npos && speedPos > 0) {
        size_t start = speedPos;
        while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '[' && line[start - 1] != '(') {
            start--;
        }
        if (start < speedPos) {
            prog.speed = line.substr(start, (speedPos + (line[speedPos + 1] == 's' && speedPos + 2 < line.size() && line[speedPos + 2] == 'e' ? 4 : 2)) - start);
        }
    }

    // Check for percentage e.g. "35%" or "(35%)"
    auto pctPos = line.find('%');
    if (pctPos != std::string::npos && pctPos > 0) {
        size_t start = pctPos - 1;
        while (start > 0 && (isdigit(static_cast<unsigned char>(line[start])) || line[start] == '.')) {
            start--;
        }
        if (!isdigit(static_cast<unsigned char>(line[start])) && line[start] != '.') start++;
        if (start < pctPos) {
            try {
                double pct = std::stod(line.substr(start, pctPos - start));
                prog.fraction = std::clamp(pct / 100.0, 0.0, 1.0);
            } catch (...) {}
        }
    }

    // Check for sizes e.g. "12.4 MB / 45.6 MB" or "[12MB/45MB]"
    auto slashPos = line.find('/');
    if (slashPos != std::string::npos && slashPos > 0 && slashPos + 1 < line.size() &&
        (slashPos == speedPos || slashPos + 1 != speedPos)) { // avoid matching /s in speed
        // Search left for downloaded size
        size_t leftStart = slashPos - 1;
        while (leftStart > 0 && line[leftStart] == ' ') leftStart--;
        while (leftStart > 0 && (isalnum(static_cast<unsigned char>(line[leftStart])) || line[leftStart] == '.' || line[leftStart] == ' ')) {
            if (line[leftStart] == '[' || line[leftStart] == '(' || line[leftStart] == ':') break;
            leftStart--;
        }
        if (leftStart < slashPos) {
            std::string leftStr = line.substr(leftStart + 1, slashPos - leftStart - 1);
            while (!leftStr.empty() && leftStr.front() == ' ') leftStr.erase(0, 1);
            while (!leftStr.empty() && leftStr.back() == ' ') leftStr.pop_back();

            // Search right for total size
            size_t rightEnd = slashPos + 1;
            while (rightEnd < line.size() && line[rightEnd] == ' ') rightEnd++;
            while (rightEnd < line.size() && (isalnum(static_cast<unsigned char>(line[rightEnd])) || line[rightEnd] == '.')) {
                rightEnd++;
            }
            std::string rightStr = line.substr(slashPos + 1, rightEnd - (slashPos + 1));
            while (!rightStr.empty() && rightStr.front() == ' ') rightStr.erase(0, 1);
            while (!rightStr.empty() && rightStr.back() == ' ') rightStr.pop_back();

            if (!leftStr.empty() && !rightStr.empty()) {
                prog.downloadedStr = leftStr;
                prog.totalStr = rightStr;
            }
        }
    }

    if (prog.totalStr.empty() && !fallbackTotal.empty()) {
        prog.totalStr = fallbackTotal;
    }

    // Stage detection
    std::string lowerLine = line;
    for (auto &c : lowerLine) c = tolower(static_cast<unsigned char>(c));

    if (lowerLine.find("downloading") != std::string::npos ||
        lowerLine.find("descargando") != std::string::npos ||
        lowerLine.find(".xbps") != std::string::npos) {
        prog.stage = 1;
        if (prog.status.empty() || prog.status.find("Descargando") == std::string::npos) {
            prog.status = "Descargando paquetes...";
        }
    } else if (lowerLine.find("unpacking") != std::string::npos ||
               lowerLine.find("desempaquetando") != std::string::npos) {
        prog.stage = 2;
        prog.status = "Desempaquetando archivos en el sistema...";
        if (prog.fraction < 0.75) prog.fraction = 0.75;
    } else if (lowerLine.find("configuring") != std::string::npos ||
               lowerLine.find("configurando") != std::string::npos ||
               lowerLine.find("reconfigure") != std::string::npos) {
        prog.stage = 2;
        prog.status = "Configurando dependencias y ejecutables...";
        if (prog.fraction < 0.90) prog.fraction = 0.90;
    } else if (lowerLine.find("installed successfully") != std::string::npos ||
               lowerLine.find("instalado con éxito") != std::string::npos) {
        prog.stage = 3;
        prog.status = "Instalación completada exitosamente.";
        prog.fraction = 1.0;
    }
}
}

std::atomic<bool> Transaction::s_busy{false};

void Transaction::setGlobalLogCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(s_callbackMutex);
    s_globalLogCb = cb;
}

void Transaction::setGlobalProgressCallback(GlobalProgressCallback cb) {
    std::lock_guard<std::mutex> lock(s_callbackMutex);
    s_globalProgressCb = cb;
}

void Transaction::setGlobalFinishCallback(GlobalFinishCallback cb) {
    std::lock_guard<std::mutex> lock(s_callbackMutex);
    s_globalFinishCb = cb;
}

bool Transaction::isBusy() {
    return s_busy.load();
}

bool Transaction::tryLock() {
    bool expected = false;
    return s_busy.compare_exchange_strong(expected, true);
}

void Transaction::unlock() {
    s_busy.store(false);
}

struct TransactionLock {
    bool acquired = false;
    TransactionLock() {
        acquired = Transaction::tryLock();
    }
    ~TransactionLock() {
        if (acquired) {
            Transaction::unlock();
        }
    }
};

std::string Transaction::elevateCmd() {
    if (Proc::exists("pkexec")) return "pkexec";
    if (Proc::exists("doas")) return "doas";
    if (Proc::exists("sudo")) return "sudo";
    return "";
}

void Transaction::install(const Package &pkg,
                          std::function<void(bool success, const std::string &msg)> onFinished,
                          std::function<void(const DownloadProgress &prog)> onProgress) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() {
                onFinished(false, "Ya hay otra operación o instalación en curso. Por favor espera a que termine.");
            });
        }
        return;
    }

    std::thread([pkg, onFinished, onProgress]() {
        TransactionLock lock;
        if (!lock.acquired) {
            if (onFinished) {
                runOnMainThread([onFinished]() {
                    onFinished(false, "Ya hay otra operación en progreso.");
                });
            }
            return;
        }

        bool ok = false;
        std::string out;
        std::string pkgDisplayName = pkg.name.empty() ? pkg.id : pkg.name;
        bool isUpdate = (pkg.state == PkgState::Updatable);
        std::string actionTitle = (isUpdate ? "Actualizando " : "Instalando ") + pkgDisplayName + " (" + getSourceDisplayName(pkg.source) + ")";

        DownloadProgress prog;
        prog.totalStr = pkg.downloadSize.empty() ? "" : pkg.downloadSize;
        prog.status = (isUpdate ? "Iniciando actualización de " : "Iniciando instalación de ") + pkgDisplayName + "...";
        prog.fraction = 0.05;
        prog.stage = 0;

        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle, false);
        broadcastProgress(actionTitle, prog);

        if (onProgress) {
            runOnMainThread([onProgress, prog]() { onProgress(prog); });
        }

        if (pkg.source == PkgSource::XBPS) {
            std::string elev = elevateCmd();
            if (elev.empty()) {
                out = "No se encontró herramienta de privilegios (pkexec, doas o sudo).";
                broadcastLog("Error: " + out, true);
            } else {
                prog.status = isUpdate ? "Actualizando con xbps-install..." : "Descargando e instalando con xbps-install...";
                prog.stage = 1;
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }

                std::vector<std::string> xbpsArgs = isUpdate
                    ? std::vector<std::string>{"-Su", "-y", pkg.id}
                    : std::vector<std::string>{"-y", pkg.id};
                std::string cmdDisplay = isUpdate ? ("xbps-install -Su -y " + pkg.id) : ("xbps-install -y " + pkg.id);

                broadcastLog(">>> Ejecutando: " + elev + " " + cmdDisplay, false);
                int code = runElevated("xbps-install", xbpsArgs, [&prog, onProgress, pkg, actionTitle](const std::string &line, bool isErr) {
                    broadcastLog(line, isErr);
                    parseProgressFromLine(line, prog, pkg.downloadSize);
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }
                });

                ok = (code == 0);
                out = ok ? (isUpdate ? "Actualizado con éxito vía XBPS." : "Instalado con éxito vía XBPS.")
                         : (isUpdate ? "Error al actualizar paquete XBPS." : "Error al instalar paquete XBPS.");
                if (ok) {
                    prog.stage = 3;
                    prog.fraction = 1.0;
                    prog.status = "Completado exitosamente.";
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }
                }
            }
        } else if (pkg.source == PkgSource::Flatpak) {
            prog.status = isUpdate ? "Actualizando Flatpak desde Flathub..." : "Descargando Flatpak desde Flathub...";
            prog.stage = 1;
            broadcastProgress(actionTitle, prog);
            if (onProgress) {
                runOnMainThread([onProgress, prog]() { onProgress(prog); });
            }

            std::string cleanId = pkg.id;
            if (cleanId.size() > 8 && cleanId.substr(cleanId.size() - 8) == ".desktop") {
                cleanId = cleanId.substr(0, cleanId.size() - 8);
            }

            std::vector<std::string> fpArgs = isUpdate
                ? std::vector<std::string>{"update", "-y", cleanId}
                : std::vector<std::string>{"install", "-y", "flathub", cleanId};
            std::string fpDisplay = isUpdate ? ("flatpak update -y " + cleanId) : ("flatpak install -y flathub " + cleanId);

            broadcastLog(">>> Ejecutando: " + fpDisplay, false);
            int code = Proc::executeStreaming("flatpak", fpArgs,
                [&prog, onProgress, pkg, actionTitle](const std::string &line, bool isErr) {
                    broadcastLog(line, isErr);
                    parseProgressFromLine(line, prog, pkg.downloadSize);
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }
                });

            ok = (code == 0);
            out = ok ? (isUpdate ? "Actualizado con éxito desde Flathub." : "Instalado con éxito desde Flathub.")
                     : (isUpdate ? "Error al actualizar Flatpak." : "Error al instalar Flatpak.");
            if (ok) {
                prog.stage = 3;
                prog.fraction = 1.0;
                prog.status = "Completado exitosamente.";
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }
            }
        } else if (pkg.source == PkgSource::Source) {
            std::string vpDir = Config::instance().voidPackagesDir();
            const char *cacheBase = g_get_user_cache_dir();
            std::string reposCacheDir = (cacheBase && *cacheBase ? std::string(cacheBase) : "/tmp") + "/neko-apps/custom_repos";

            if (!s_rootSession.ensureAuthenticated([](const std::string &line, bool isErr) {
                broadcastLog(line, isErr);
            })) {
                ok = false;
                out = "Autenticación cancelada o fallida.";
            } else {
                // Clean up any stale/orphaned processes inside the masterdir chroot holding database locks
                runElevatedScript(
                    "for p in /proc/[0-9]*; do "
                    "  r=\"$(readlink \"$p/root\" 2>/dev/null)\"; "
                    "  if [ \"$r\" = '" + vpDir + "/masterdir' ]; then "
                    "    kill -9 \"${p#/proc/}\" 2>/dev/null || true; "
                    "  fi; "
                    "done; "
                    "pkill -TERM -f '" + vpDir + "/masterdir' 2>/dev/null || true; "
                    "pkill -TERM -f 'xbps-src' 2>/dev/null || true; "
                    "sleep 0.2; "
                    "pkill -KILL -f '" + vpDir + "/masterdir' 2>/dev/null || true; "
                    "pkill -KILL -f 'xbps-src' 2>/dev/null || true"
                );

                std::string script =
                    "echo '=== Neko-Apps (Vouru): Preparando " + vpDir + " ==='; "
                    "mkdir -p \"$(dirname '" + vpDir + "')\"; "
                    "if [ ! -f '" + vpDir + "/xbps-src' ]; then "
                    "echo '>>> Clonando void-packages en " + vpDir + "...'; "
                    "rm -rf '" + vpDir + ".tmp_clone'; "
                    "GIT_TERMINAL_PROMPT=0 git clone --depth=1 https://github.com/void-linux/void-packages.git '" + vpDir + ".tmp_clone' || exit 1; "
                    "if [ -d '" + vpDir + "/srcpkgs' ]; then "
                    "cp -rfL '" + vpDir + "/srcpkgs/'* '" + vpDir + ".tmp_clone/srcpkgs/' 2>/dev/null || true; "
                    "fi; "
                    "rm -rf '" + vpDir + "'; "
                    "mv '" + vpDir + ".tmp_clone' '" + vpDir + "'; "
                    "fi; "
                    "mkdir -p '" + vpDir + "/etc'; "
                    "touch '" + vpDir + "/etc/conf'; "
                    "grep -q '^XBPS_ALLOW_RESTRICTED=yes' '" + vpDir + "/etc/conf' 2>/dev/null || echo 'XBPS_ALLOW_RESTRICTED=yes' >> '" + vpDir + "/etc/conf'; "
                    "if [ -f /etc/xbps.d/00-repository-main.conf ]; then "
                    "  _sysm=\"$(grep -m1 '^repository=' /etc/xbps.d/00-repository-main.conf 2>/dev/null | cut -d= -f2- | tr -d ' \\t\\r\\n')\"; "
                    "fi; "
                    "case \"$_sysm\" in "
                    "  *repo-default.voidlinux.org*) "
                    "    _sysm=\"$(echo \"$_sysm\" | sed 's|repo-default.voidlinux.org|repo-fastly.voidlinux.org|g')\"; "
                    "    ;; "
                    "esac; "
                    "if [ -z \"$_sysm\" ]; then _sysm='https://repo-fastly.voidlinux.org/current'; fi; "
                    "sed -i '/^XBPS_MIRROR=/d' '" + vpDir + "/etc/conf' 2>/dev/null || true; "
                    "echo \"XBPS_MIRROR=\\\"$_sysm\\\"\" >> '" + vpDir + "/etc/conf'; "
                    "export XBPS_MIRROR=\"$_sysm\"; "
                    "if [ -d '" + vpDir + "/masterdir/etc/xbps.d' ]; then "
                    "  for _c in '" + vpDir + "/masterdir/etc/xbps.d/'*.conf; do "
                    "    [ -f \"$_c\" ] || continue; "
                    "    sed -i \"s|https://repo-default.voidlinux.org|https://repo-fastly.voidlinux.org|g\" \"$_c\" 2>/dev/null || true; "
                    "    if [ \"$_sysm\" != 'https://repo-fastly.voidlinux.org/current' ]; then "
                    "      sed -i \"s|https://repo-fastly.voidlinux.org/current|$_sysm|g\" \"$_c\" 2>/dev/null || true; "
                    "    fi; "
                    "  done; "
                    "fi; "
                    "rm -f '" + vpDir + "/masterdir/var/cache/xbps/'*.part 2>/dev/null || true; "
                    "rm -f '" + vpDir + "/masterdir/var/db/xbps/'*repodata* 2>/dev/null || true; "
                    "if [ ! -f '" + vpDir + "/masterdir/.xbps_chroot_init' ] || { [ ! -x '" + vpDir + "/masterdir/bin/sh' ] && [ ! -x '" + vpDir + "/masterdir/usr/bin/sh' ]; } || { [ ! -x '" + vpDir + "/masterdir/bin/xbps-install' ] && [ ! -x '" + vpDir + "/masterdir/usr/bin/xbps-install' ]; }; then "
                    "echo '>>> Inicializando chroot (binary-bootstrap) en " + vpDir + "...'; "
                    "cd '" + vpDir + "' && ./xbps-src zap 2>/dev/null || true; "
                    "rm -f '" + vpDir + "/masterdir/.xbps_chroot_init' 2>/dev/null || true; "
                    "rm -rf '" + vpDir + "/masterdir' 2>/dev/null || true; "
                    "mkdir -p '" + vpDir + "/masterdir'; "
                    "cd '" + vpDir + "' && XBPS_MIRROR=\"$_sysm\" ./xbps-src binary-bootstrap || exit 1; "
                    "fi; ";

                // Sync custom template repositories into ~/.local/share/pkgs/void-packages/srcpkgs/ (Vouru add_template logic)
                auto customRepos = Config::instance().customSourceRepos();
                for (std::string repoUrl : customRepos) {
                    while (!repoUrl.empty() && (repoUrl.back() == ' ' || repoUrl.back() == '\t' || repoUrl.back() == '\r' || repoUrl.back() == '/')) {
                        repoUrl.pop_back();
                    }
                    while (!repoUrl.empty() && (repoUrl.front() == ' ' || repoUrl.front() == '\t')) {
                        repoUrl.erase(0, 1);
                    }
                    if (repoUrl.empty()) continue;

                    std::error_code ec;
                    std::string targetDir;
                    if (std::filesystem::is_directory(repoUrl, ec)) {
                        targetDir = repoUrl;
                    } else {
                        std::string safeName;
                        for (char c : repoUrl) {
                            if (std::isalnum(static_cast<unsigned char>(c))) safeName += c;
                            else safeName += '_';
                        }
                        targetDir = reposCacheDir + "/" + safeName;
                        script += "echo '>>> Sincronizando repositorio de plantillas: " + repoUrl + "'; ";
                        script += "mkdir -p '" + reposCacheDir + "'; ";
                        script += "if [ ! -d '" + targetDir + "/.git' ]; then ";
                        script += "rm -rf '" + targetDir + "'; ";
                        script += "GIT_TERMINAL_PROMPT=0 git clone --depth=1 '" + repoUrl + "' '" + targetDir + "' 2>&1; ";
                        script += "else ";
                        script += "GIT_TERMINAL_PROMPT=0 git -C '" + targetDir + "' pull --ff-only 2>&1 || true; ";
                        script += "fi; ";
                    }

                    // Install templates into vpDir/srcpkgs/<pkg_name> as real files (deleting any old broken symlinks first)
                    script += "if [ -d '" + targetDir + "' ]; then ";
                    script += "find '" + targetDir + "' -type f -name 'template' -not -path '*/.git/*' | sort -u | while IFS= read -r tfile; do ";
                    script += "tdir=\"$(dirname \"$tfile\")\"; ";
                    script += "if [ \"$tdir\" = '" + targetDir + "' ]; then ";
                    script += "pkgname=\"$(grep -m1 '^pkgname=' \"$tfile\" | cut -d= -f2- | tr -d '\"\\x27')\"; ";
                    script += "else ";
                    script += "pkgname=\"$(basename \"$tdir\")\"; ";
                    script += "fi; ";
                    script += "if [ -n \"$pkgname\" ] && [ \"$pkgname\" != 'srcpkgs' ] && [ \"$pkgname\" != 'common' ]; then ";
                    script += "rm -rf '" + vpDir + "/srcpkgs/'\"$pkgname\"; ";
                    script += "mkdir -p '" + vpDir + "/srcpkgs/'\"$pkgname\"; ";
                    script += "cp -rfL \"$tdir\"/. '" + vpDir + "/srcpkgs/'\"$pkgname\"/; ";
                    script += "rm -rf '" + vpDir + "/srcpkgs/'\"$pkgname\"/.git 2>/dev/null || true; ";
                    script += "fi; ";
                    script += "done; ";
                    script += "for sdir in '" + targetDir + "'/* '" + targetDir + "'/srcpkgs/*; do ";
                    script += "if [ -L \"$sdir\" ]; then ";
                    script += "sname=\"$(basename \"$sdir\")\"; ";
                    script += "linktarget=\"$(readlink \"$sdir\")\"; ";
                    script += "case \"$linktarget\" in /*) ;; *) rm -rf '" + vpDir + "/srcpkgs/'\"$sname\"; ln -sf \"$linktarget\" '" + vpDir + "/srcpkgs/'\"$sname\" ;; esac; ";
                    script += "fi; ";
                    script += "done; ";
                    script += "fi; ";

                    // Ensure Neko-Void-Linux/cnr required shlibs entries exist in common/shlibs
                    script += "if [ -f '" + vpDir + "/common/shlibs' ]; then ";
                    script += "grep -q '^libscenefx-0.5.so ' '" + vpDir + "/common/shlibs' 2>/dev/null || echo 'libscenefx-0.5.so scenefx-0.5_1' >> '" + vpDir + "/common/shlibs'; ";
                    script += "grep -q '^libQt6Pas.so.6 ' '" + vpDir + "/common/shlibs' 2>/dev/null || echo 'libQt6Pas.so.6 qt6pas-6.2.10_1' >> '" + vpDir + "/common/shlibs'; ";
                    script += "grep -q '^libcrypto.so.1.1 ' '" + vpDir + "/common/shlibs' 2>/dev/null || echo 'libcrypto.so.1.1 libssl1.1-1.1.1w_1' >> '" + vpDir + "/common/shlibs'; ";
                    script += "fi; ";
                }

                script += "if [ ! -f '" + vpDir + "/srcpkgs/" + pkg.id + "/template' ]; then "
                          "echo 'ERROR: No se encontró la plantilla en " + vpDir + "/srcpkgs/" + pkg.id + "/template' >&2; exit 1; fi; "
                          "cd '" + vpDir + "' && ( XBPS_MIRROR=\"$_sysm\" ./xbps-src pkg " + pkg.id + " || { rm -f '" + vpDir + "/masterdir/var/cache/xbps/'*.part 2>/dev/null; rm -f '" + vpDir + "/masterdir/var/db/xbps/'*repodata* 2>/dev/null; sleep 1; XBPS_MIRROR=\"$_sysm\" ./xbps-src pkg " + pkg.id + "; } );";

                prog.status = "Compilando con Vouru: " + pkg.id + "...";
                prog.stage = 1;
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }

                broadcastLog(">>> Iniciando proceso de compilación Vouru para " + pkg.id, false);
                int code = Proc::executeStreaming("sh", {"-c", script}, [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                    broadcastLog(line, isErr);
                    parseProgressFromLine(line, prog, "");
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }
                });

                if (code == 0) {
                    prog.status = "Instalando paquete compilado: " + pkg.id + "...";
                    prog.stage = 2;
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }
                    broadcastLog(">>> Instalando binario compilado desde hostdir/binpkgs...", false);
                    code = runElevated(
                        "xbps-install",
                        {
                            "--repository=" + vpDir + "/hostdir/binpkgs",
                            "--repository=" + vpDir + "/hostdir/binpkgs/nonfree",
                            "-y",
                            "-f",
                            pkg.id
                        },
                        [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                            broadcastLog(line, isErr);
                            parseProgressFromLine(line, prog, "");
                            broadcastProgress(actionTitle, prog);
                            if (onProgress) {
                                runOnMainThread([onProgress, prog]() { onProgress(prog); });
                            }
                        }
                    );
                }

                ok = (code == 0);
                out = ok ? "Compilado e instalado con éxito." : "Error al compilar con Vouru.";
            }
            if (ok) {
                prog.stage = 3;
                prog.fraction = 1.0;
                prog.status = "Compilado e instalado.";
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }
            }
        } else if (pkg.source == PkgSource::AppImage) {
            if (!pkg.id.empty() && pkg.id[0] == '/') {
                broadcastLog(">>> Verificando permisos de ejecución de AppImage local...", false);
                std::error_code ec;
                if (std::filesystem::exists(pkg.id, ec)) {
                    chmod(pkg.id.c_str(), 0755);
                    ok = true;
                    out = "Permisos de ejecución concedidos al AppImage.";
                } else {
                    ok = false;
                    out = "No se localizó el archivo AppImage en el sistema de archivos.";
                }
            } else {
                std::string soarBin = AppImageBackend::findSoarBinary();
                if (soarBin.empty()) {
                    ok = false;
                    out = "Soar no está instalado. Instálalo en tu usuario para descargar paquetes de PkgForge.";
                    broadcastLog("Error: " + out, true);
                } else if (!AppImageBackend::isSafeSoarId(pkg.id)) {
                    ok = false;
                    out = "Identificador de paquete Soar no válido.";
                    broadcastLog("Error: " + out, true);
                } else {
                    std::string subcmd = isUpdate ? "update" : "install";
                    prog.status = (isUpdate ? "Actualizando con Soar: " : "Instalando con Soar: ") + pkgDisplayName + "...";
                    prog.stage = 1;
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }

                    broadcastLog(">>> Ejecutando: " + soarBin + " --no-color " + subcmd + " " + pkg.id, false);
                    int code = Proc::executeStreaming(soarBin, {"--no-color", subcmd, pkg.id},
                        [&prog, onProgress, pkg, actionTitle](const std::string &line, bool isErr) {
                            broadcastLog(line, isErr);
                            parseProgressFromLine(line, prog, pkg.downloadSize);
                            broadcastProgress(actionTitle, prog);
                            if (onProgress) {
                                runOnMainThread([onProgress, prog]() { onProgress(prog); });
                            }
                        });
                    ok = (code == 0);
                    out = ok ? (isUpdate ? "Actualizado con éxito mediante Soar." : "Instalado con éxito mediante Soar.")
                             : (isUpdate ? "Error al actualizar paquete con Soar." : "Error al instalar paquete con Soar.");
                }
            }
            prog.stage = 3;
            prog.fraction = ok ? 1.0 : 0.0;
            prog.status = out;
            broadcastProgress(actionTitle, prog);
            if (onProgress) {
                runOnMainThread([onProgress, prog]() { onProgress(prog); });
            }
        }

        broadcastLog(ok ? ">>> [Neko-Apps] Proceso completado exitosamente." : (">>> [Neko-Apps] " + out), !ok);
        broadcastFinish(ok, out);

        runOnMainThread([onFinished, ok, out]() {
            if (onFinished) onFinished(ok, out);
        });
    }).detach();
}

void Transaction::uninstall(const Package &pkg,
                            std::function<void(bool success, const std::string &msg)> onFinished,
                            std::function<void(const DownloadProgress &prog)> onProgress) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() {
                onFinished(false, "Ya hay otra operación en curso. Por favor espera a que termine.");
            });
        }
        return;
    }

    std::thread([pkg, onFinished, onProgress]() {
        TransactionLock lock;
        if (!lock.acquired) {
            if (onFinished) {
                runOnMainThread([onFinished]() {
                    onFinished(false, "Ya hay otra operación en progreso.");
                });
            }
            return;
        }

        bool ok = false;
        std::string out;
        std::string pkgDisplayName = pkg.name.empty() ? pkg.id : pkg.name;
        std::string actionTitle = "Desinstalando " + pkgDisplayName + " (" + getSourceDisplayName(pkg.source) + ")";

        DownloadProgress prog;
        prog.status = "Iniciando desinstalación de " + pkgDisplayName + "...";
        prog.stage = 2;
        prog.fraction = 0.2;

        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle, false);
        broadcastProgress(actionTitle, prog);

        if (onProgress) {
            runOnMainThread([onProgress, prog]() { onProgress(prog); });
        }

        if (pkg.source == PkgSource::XBPS || pkg.source == PkgSource::Source) {
            std::string elev = elevateCmd();
            if (elev.empty()) {
                out = "No se encontró herramienta de privilegios.";
                broadcastLog("Error: " + out, true);
            } else {
                broadcastLog(">>> Ejecutando: " + elev + " xbps-remove -y " + pkg.id, false);
                int code = runElevated("xbps-remove", {"-y", pkg.id}, [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                    broadcastLog(line, isErr);
                    parseProgressFromLine(line, prog, "");
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }
                });
                ok = (code == 0);
                out = ok ? "Desinstalado con éxito vía XBPS." : "Error al desinstalar.";
            }
        } else if (pkg.source == PkgSource::Flatpak) {
            std::string cleanId = pkg.id;
            if (cleanId.size() > 8 && cleanId.substr(cleanId.size() - 8) == ".desktop") {
                cleanId = cleanId.substr(0, cleanId.size() - 8);
            }
            broadcastLog(">>> Ejecutando: flatpak uninstall -y " + cleanId, false);
            int code = Proc::executeStreaming("flatpak", {"uninstall", "-y", cleanId}, [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                broadcastLog(line, isErr);
                parseProgressFromLine(line, prog, "");
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }
            });
            ok = (code == 0);
            out = ok ? "Desinstalado con éxito desde Flatpak." : "Error al desinstalar Flatpak.";
        } else if (pkg.source == PkgSource::AppImage) {
            if (!pkg.id.empty() && pkg.id[0] == '/') {
                broadcastLog(">>> Eliminando archivo AppImage local...", false);
                std::error_code ec;
                if (std::filesystem::exists(pkg.id, ec)) {
                    std::filesystem::remove(pkg.id, ec);
                }
                ok = !ec;
                out = ok ? "AppImage eliminado con éxito." : ("Error al eliminar AppImage: " + ec.message());
            } else {
                std::string soarBin = AppImageBackend::findSoarBinary();
                if (soarBin.empty()) {
                    ok = false;
                    out = "Soar no está disponible para desinstalar el paquete.";
                } else if (!AppImageBackend::isSafeSoarId(pkg.id)) {
                    ok = false;
                    out = "Identificador de paquete Soar no válido.";
                } else {
                    broadcastLog(">>> Ejecutando: " + soarBin + " --no-color remove " + pkg.id, false);
                    int code = Proc::executeStreaming(soarBin, {"--no-color", "remove", pkg.id},
                        [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                            broadcastLog(line, isErr);
                            parseProgressFromLine(line, prog, "");
                            broadcastProgress(actionTitle, prog);
                            if (onProgress) {
                                runOnMainThread([onProgress, prog]() { onProgress(prog); });
                            }
                        });
                    ok = (code == 0);
                    out = ok ? "Desinstalado con éxito mediante Soar." : "Error al desinstalar con Soar.";
                }
            }
        }

        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = ok ? "Desinstalado exitosamente." : "Fallo al desinstalar.";
        broadcastProgress(actionTitle, prog);
        if (onProgress) {
            runOnMainThread([onProgress, prog]() { onProgress(prog); });
        }

        broadcastLog(ok ? ">>> [Neko-Apps] Desinstalación completada con éxito." : (">>> [Neko-Apps] " + out), !ok);
        broadcastFinish(ok, out);

        runOnMainThread([onFinished, ok, out]() {
            if (onFinished) onFinished(ok, out);
        });
    }).detach();
}

void Transaction::updateAll(const PackageList &pkgs,
                            std::function<void(bool success, const std::string &msg)> onFinished,
                            std::function<void(const DownloadProgress &prog)> onProgress) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() {
                onFinished(false, "Ya hay otra operación en curso. Por favor espera a que termine.");
            });
        }
        return;
    }

    std::thread([pkgs, onFinished, onProgress]() {
        TransactionLock lock;
        if (!lock.acquired) {
            if (onFinished) {
                runOnMainThread([onFinished]() {
                    onFinished(false, "Ya hay otra operación en progreso.");
                });
            }
            return;
        }

        bool hasXbps = pkgs.empty();
        bool hasFlatpak = pkgs.empty() && Config::instance().flatpakEnabled() && Proc::exists("flatpak");
        bool hasSoar = pkgs.empty() && Config::instance().appimageEnabled() && AppImageBackend::hasSoar();

        for (const auto &p : pkgs) {
            if (p.source == PkgSource::XBPS || p.source == PkgSource::Source) hasXbps = true;
            else if (p.source == PkgSource::Flatpak) hasFlatpak = true;
            else if (p.source == PkgSource::AppImage) hasSoar = true;
        }

        std::string elev = elevateCmd();
        bool ok = true;
        std::string out;
        std::string actionTitle = "Actualizando el sistema...";

        DownloadProgress prog;
        prog.status = "Sincronizando repositorios y actualizando el sistema...";
        prog.stage = 0;
        prog.fraction = 0.05;

        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle, false);
        broadcastProgress(actionTitle, prog);

        if (onProgress) {
            runOnMainThread([onProgress, prog]() { onProgress(prog); });
        }

        if (hasXbps) {
            if (elev.empty()) {
                ok = false;
                out = "No se encontró herramienta de privilegios.";
                broadcastLog("Error: " + out, true);
            } else {
                prog.status = "Descargando actualizaciones del sistema (XBPS)...";
                prog.stage = 1;
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }

                broadcastLog(">>> Ejecutando: " + elev + " xbps-install -Su -y", false);
                int code = runElevated("xbps-install", {"-Su", "-y"}, [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                    broadcastLog(line, isErr);
                    parseProgressFromLine(line, prog, "");
                    broadcastProgress(actionTitle, prog);
                    if (onProgress) {
                        runOnMainThread([onProgress, prog]() { onProgress(prog); });
                    }
                });
                if (code != 0) ok = false;
            }
        }

        if (ok && hasFlatpak && Config::instance().flatpakEnabled() && Proc::exists("flatpak")) {
            prog.status = "Actualizando paquetes Flatpak...";
            broadcastProgress(actionTitle, prog);
            if (onProgress) {
                runOnMainThread([onProgress, prog]() { onProgress(prog); });
            }
            broadcastLog(">>> Ejecutando: flatpak update -y", false);
            int fpCode = Proc::executeStreaming("flatpak", {"update", "-y"}, [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                broadcastLog(line, isErr);
                parseProgressFromLine(line, prog, "");
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }
            });
            if (fpCode != 0) ok = false;
        }

        std::string soarBin = AppImageBackend::findSoarBinary();
        if (ok && hasSoar && Config::instance().appimageEnabled() && !soarBin.empty()) {
            prog.status = "Actualizando paquetes portables con Soar...";
            broadcastProgress(actionTitle, prog);
            if (onProgress) {
                runOnMainThread([onProgress, prog]() { onProgress(prog); });
            }
            broadcastLog(">>> Ejecutando: " + soarBin + " --no-color update", false);
            int soarCode = Proc::executeStreaming(soarBin, {"--no-color", "update"}, [&prog, onProgress, actionTitle](const std::string &line, bool isErr) {
                broadcastLog(line, isErr);
                parseProgressFromLine(line, prog, "");
                broadcastProgress(actionTitle, prog);
                if (onProgress) {
                    runOnMainThread([onProgress, prog]() { onProgress(prog); });
                }
            });
            if (soarCode != 0) ok = false;
        }

        if (out.empty()) {
            out = ok ? "Sistema actualizado con éxito." : "Error al actualizar paquetes.";
        }
        if (ok) {
            prog.stage = 3;
            prog.fraction = 1.0;
            prog.status = "Actualización completada exitosamente.";
            broadcastProgress(actionTitle, prog);
            if (onProgress) {
                runOnMainThread([onProgress, prog]() { onProgress(prog); });
            }
        }

        broadcastLog(ok ? ">>> [Neko-Apps] Actualización completada exitosamente." : (">>> [Neko-Apps] " + out), !ok);
        broadcastFinish(ok, out);

        runOnMainThread([onFinished, ok, out]() {
            if (onFinished) onFinished(ok, out);
        });
    }).detach();
}

void Transaction::cleanXbpsCache(std::function<void(bool success, const std::string &msg)> onFinished) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() { onFinished(false, "Hay otra operación en curso."); });
        }
        return;
    }
    std::thread([onFinished]() {
        TransactionLock lock;
        if (!lock.acquired) {
            runOnMainThread([onFinished]() {
                if (onFinished) onFinished(false, "Otra operación se inició simultáneamente.");
            });
            return;
        }
        if (elevateCmd().empty()) {
            std::string err = "No se encontró pkexec ni sudo para elevar privilegios.";
            broadcastLog(">>> [Neko-Apps] Error: " + err, true);
            broadcastFinish(false, err);
            runOnMainThread([onFinished, err]() {
                if (onFinished) onFinished(false, err);
            });
            return;
        }
        std::string actionTitle = "Limpieza de caché XBPS";
        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle + " (xbps-remove -O)", false);
        DownloadProgress prog;
        prog.status = "Limpiando paquetes obsoletos en caché...";
        prog.stage = 2;
        prog.fraction = 0.5;
        broadcastProgress(actionTitle, prog);

        int code = runElevated("xbps-remove", {"-O"}, [](const std::string &line, bool isErr) {
            broadcastLog(line, isErr);
        });
        bool ok = (code == 0);
        std::string msg = ok ? "Caché de paquetes XBPS limpiada con éxito." : "Error al limpiar caché.";
        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = msg;
        broadcastProgress(actionTitle, prog);
        broadcastLog(ok ? ">>> [Neko-Apps] " + msg : (">>> [Neko-Apps] Error: " + msg), !ok);
        broadcastFinish(ok, msg);

        runOnMainThread([onFinished, ok, msg]() {
            if (onFinished) onFinished(ok, msg);
        });
    }).detach();
}


void Transaction::removeXbpsOrphans(std::function<void(bool success, const std::string &msg)> onFinished) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() { onFinished(false, "Hay otra operación en curso."); });
        }
        return;
    }
    std::thread([onFinished]() {
        TransactionLock lock;
        if (!lock.acquired) {
            runOnMainThread([onFinished]() {
                if (onFinished) onFinished(false, "Otra operación se inició simultáneamente.");
            });
            return;
        }
        if (elevateCmd().empty()) {
            std::string err = "No se encontró pkexec ni sudo para elevar privilegios.";
            broadcastLog(">>> [Neko-Apps] Error: " + err, true);
            broadcastFinish(false, err);
            runOnMainThread([onFinished, err]() {
                if (onFinished) onFinished(false, err);
            });
            return;
        }
        std::string actionTitle = "Eliminación de paquetes huérfanos";
        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle + " (xbps-remove -o -y)", false);
        DownloadProgress prog;
        prog.status = "Buscando y eliminando paquetes huérfanos...";
        prog.stage = 2;
        prog.fraction = 0.5;
        broadcastProgress(actionTitle, prog);

        int code = runElevated("xbps-remove", {"-o", "-y"}, [](const std::string &line, bool isErr) {
            broadcastLog(line, isErr);
        });
        bool ok = (code == 0);
        std::string msg = ok ? "Paquetes huérfanos eliminados con éxito." : "No se encontraron huérfanos o hubo un error.";
        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = msg;
        broadcastProgress(actionTitle, prog);
        broadcastLog(">>> [Neko-Apps] " + msg, !ok);
        broadcastFinish(ok, msg);

        runOnMainThread([onFinished, ok, msg]() {
            if (onFinished) onFinished(ok, msg);
        });
    }).detach();
}


void Transaction::reconfigureXbps(std::function<void(bool success, const std::string &msg)> onFinished) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() { onFinished(false, "Hay otra operación en curso."); });
        }
        return;
    }
    std::thread([onFinished]() {
        TransactionLock lock;
        if (!lock.acquired) {
            runOnMainThread([onFinished]() {
                if (onFinished) onFinished(false, "Otra operación se inició simultáneamente.");
            });
            return;
        }
        if (elevateCmd().empty()) {
            std::string err = "No se encontró pkexec ni sudo para elevar privilegios.";
            broadcastLog(">>> [Neko-Apps] Error: " + err, true);
            broadcastFinish(false, err);
            runOnMainThread([onFinished, err]() {
                if (onFinished) onFinished(false, err);
            });
            return;
        }
        std::string actionTitle = "Reconfiguración de paquetes XBPS";
        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle + " (xbps-reconfigure -a)", false);
        DownloadProgress prog;
        prog.status = "Reconfigurando paquetes en el sistema...";
        prog.stage = 2;
        prog.fraction = 0.5;
        broadcastProgress(actionTitle, prog);

        int code = runElevated("xbps-reconfigure", {"-a"}, [](const std::string &line, bool isErr) {
            broadcastLog(line, isErr);
        });
        bool ok = (code == 0);
        std::string msg = ok ? "Todos los paquetes fueron reconfigurados con éxito." : "Error al reconfigurar paquetes.";
        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = msg;
        broadcastProgress(actionTitle, prog);
        broadcastLog(">>> [Neko-Apps] " + msg, !ok);
        broadcastFinish(ok, msg);

        runOnMainThread([onFinished, ok, msg]() {
            if (onFinished) onFinished(ok, msg);
        });
    }).detach();
}


void Transaction::executeScript(const std::string &actionTitle,
                                const std::string &shellScript,
                                std::function<void(bool success, const std::string &msg)> onFinished) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() { onFinished(false, "Hay otra operación o instalación en curso."); });
        }
        return;
    }

    std::thread([actionTitle, shellScript, onFinished]() {
        TransactionLock lock;
        if (!lock.acquired) {
            runOnMainThread([onFinished]() {
                if (onFinished) onFinished(false, "Otra operación se inició simultáneamente.");
            });
            return;
        }

        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle, false);
        DownloadProgress prog;
        prog.status = "Ejecutando: " + actionTitle + "...";
        prog.stage = 2;
        prog.fraction = -1.0; // Indeterminate pulsing until download percentages are parsed
        broadcastProgress(actionTitle, prog);

        int code = runElevatedScript(shellScript, [&prog, actionTitle](const std::string &line, bool isErr) {
            broadcastLog(line, isErr);
            parseProgressFromLine(line, prog, "");
            broadcastProgress(actionTitle, prog);
        });

        bool ok = (code == 0);
        std::string msg = ok ? (actionTitle + " completado con éxito.")
                             : ("Error durante la operación de " + actionTitle + " (código " + std::to_string(code) + ").");
        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = msg;
        broadcastProgress(actionTitle, prog);
        broadcastLog(">>> [Neko-Apps] " + msg, !ok);
        broadcastFinish(ok, msg);

        runOnMainThread([onFinished, ok, msg]() {
            if (onFinished) onFinished(ok, msg);
        });
    }).detach();
}


void Transaction::setMirror(const std::string &targetUrl,
                            std::function<void(bool success, const std::string &msg)> onFinished) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() { onFinished(false, "Hay otra operación en curso."); });
        }
        return;
    }

    std::thread([targetUrl, onFinished]() {
        TransactionLock lock;
        if (!lock.acquired) {
            runOnMainThread([onFinished]() {
                if (onFinished) onFinished(false, "Otra operación se inició simultáneamente.");
            });
            return;
        }

        std::string actionTitle = "Configurando espejo oficial";
        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle + " (" + targetUrl + ")", false);
        DownloadProgress prog;
        prog.status = "Configurando espejo y actualizando índices...";
        prog.stage = 2;
        prog.fraction = -1.0;
        broadcastProgress(actionTitle, prog);

        int code = s_rootSession.runWorkerCommand({"set-mirror", targetUrl}, [&prog, actionTitle](const std::string &line, bool isErr) {
            broadcastLog(line, isErr);
            parseProgressFromLine(line, prog, "");
            broadcastProgress(actionTitle, prog);
        });

        bool ok = (code == 0);
        std::string msg = ok ? "Espejo configurado con éxito." : ("Error al configurar el espejo (código " + std::to_string(code) + ").");
        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = msg;
        broadcastProgress(actionTitle, prog);
        broadcastLog(">>> [Neko-Apps] " + msg, !ok);
        broadcastFinish(ok, msg);

        runOnMainThread([onFinished, ok, msg]() {
            if (onFinished) onFinished(ok, msg);
        });
    }).detach();
}


void Transaction::installDriver(const std::string &driverId,
                                const std::string &title,
                                std::function<void(bool success, const std::string &msg)> onFinished) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() { onFinished(false, "Hay otra operación en curso."); });
        }
        return;
    }

    std::thread([driverId, title, onFinished]() {
        TransactionLock lock;
        if (!lock.acquired) {
            runOnMainThread([onFinished]() {
                if (onFinished) onFinished(false, "Otra operación se inició simultáneamente.");
            });
            return;
        }

        std::string actionTitle = "Instalando " + title;
        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle, false);
        DownloadProgress prog;
        prog.status = "Instalando " + title + "...";
        prog.stage = 2;
        prog.fraction = -1.0;
        broadcastProgress(actionTitle, prog);

        int code = s_rootSession.runWorkerCommand({"driver", driverId}, [&prog, actionTitle](const std::string &line, bool isErr) {
            broadcastLog(line, isErr);
            parseProgressFromLine(line, prog, "");
            broadcastProgress(actionTitle, prog);
        });

        bool ok = (code == 0);
        std::string msg = ok ? (title + " completado con éxito.") : ("Error durante la instalación de " + title + " (código " + std::to_string(code) + ").");
        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = msg;
        broadcastProgress(actionTitle, prog);
        broadcastLog(">>> [Neko-Apps] " + msg, !ok);
        broadcastFinish(ok, msg);

        runOnMainThread([onFinished, ok, msg]() {
            if (onFinished) onFinished(ok, msg);
        });
    }).detach();
}

void Transaction::installLocalPackageFile(
    const std::string &filePath,
    const std::string &pkgName,
    std::function<void(bool success, const std::string &msg)> onFinished
) {
    if (isBusy()) {
        if (onFinished) {
            runOnMainThread([onFinished]() { onFinished(false, "Hay otra operación en curso."); });
        }
        return;
    }

    std::thread([filePath, pkgName, onFinished]() {
        TransactionLock lock;
        if (!lock.acquired) {
            runOnMainThread([onFinished]() {
                if (onFinished) onFinished(false, "Otra operación se inició simultáneamente.");
            });
            return;
        }

        std::string actionTitle = "Instalando paquete local: " + pkgName;
        broadcastLog(">>> [Neko-Apps] Iniciando: " + actionTitle, false);
        DownloadProgress prog;
        prog.status = "Instalando " + pkgName + "...";
        prog.stage = 2;
        prog.fraction = -1.0;
        broadcastProgress(actionTitle, prog);

        int code = s_rootSession.runWorkerCommand({"install-file", filePath}, [&prog, actionTitle](const std::string &line, bool isErr) {
            broadcastLog(line, isErr);
            parseProgressFromLine(line, prog, "");
            broadcastProgress(actionTitle, prog);
        });

        bool ok = (code == 0);
        std::string msg = ok ? (pkgName + " instalado con éxito.") : ("Error instalando " + pkgName + " (código " + std::to_string(code) + ").");
        prog.stage = 3;
        prog.fraction = ok ? 1.0 : 0.0;
        prog.status = msg;
        broadcastProgress(actionTitle, prog);
        broadcastLog(">>> [Neko-Apps] " + msg, !ok);
        broadcastFinish(ok, msg);

        runOnMainThread([onFinished, ok, msg]() {
            if (onFinished) onFinished(ok, msg);
        });
    }).detach();
}


