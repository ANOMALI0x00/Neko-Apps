#include "Proc.h"
#include <glib.h>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>

bool Proc::exists(const std::string &binary) {
    gchar *path = g_find_program_in_path(binary.c_str());
    if (path) {
        g_free(path);
        return true;
    }
    return false;
}

std::string Proc::capture(const std::string &cmd, const std::vector<std::string> &args) {
    std::vector<const gchar*> argv;
    argv.push_back(cmd.c_str());
    for (const auto &a : args) {
        argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);

    gchar *standard_output = nullptr;
    gchar *standard_error = nullptr;
    gint exit_status = 0;
    GError *error = nullptr;

    gboolean ok = g_spawn_sync(
        nullptr,
        const_cast<gchar**>(argv.data()),
        nullptr,
        G_SPAWN_SEARCH_PATH,
        nullptr,
        nullptr,
        &standard_output,
        &standard_error,
        &exit_status,
        &error
    );

    std::string result;
    if (ok && standard_output) {
        result = standard_output;
    }

    if (standard_output) g_free(standard_output);
    if (standard_error) g_free(standard_error);
    if (error) g_error_free(error);

    return result;
}

namespace {
struct IdleContext {
    std::function<void()> fn;
};

gboolean onMainIdle(gpointer data) {
    auto *ctx = static_cast<IdleContext*>(data);
    if (ctx && ctx->fn) {
        ctx->fn();
    }
    delete ctx;
    return G_SOURCE_REMOVE;
}
} // namespace

void Proc::runOnMainThread(std::function<void()> fn) {
    auto *ctx = new IdleContext{std::move(fn)};
    g_idle_add(onMainIdle, ctx);
}

int Proc::executeStreaming(const std::string &cmd,
                           const std::vector<std::string> &args,
                           std::function<void(const std::string &line, bool isStderr)> onLine) {
    std::vector<const gchar*> argv;
    argv.push_back(cmd.c_str());
    for (const auto &a : args) {
        argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);

    GPid pid = 0;
    gint fdOut = -1;
    gint fdErr = -1;
    GError *error = nullptr;

    gboolean ok = g_spawn_async_with_pipes(
        nullptr,
        const_cast<gchar**>(argv.data()),
        nullptr,
        static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
        +[](gpointer) {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
        },
        nullptr,
        &pid,
        nullptr,
        &fdOut,
        &fdErr,
        &error
    );

    if (!ok) {
        if (error) {
            if (onLine) onLine(std::string("Error al iniciar proceso: ") + error->message, true);
            g_error_free(error);
        }
        return -1;
    }

    auto setNonBlocking = [](int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    };
    if (fdOut >= 0) setNonBlocking(fdOut);
    if (fdErr >= 0) setNonBlocking(fdErr);

    std::string outBuf;
    std::string errBuf;

    auto processBuffer = [&onLine](std::string &buf, bool isErr) {
        size_t pos = 0;
        while (pos < buf.size()) {
            size_t nextEnd = buf.find_first_of("\r\n", pos);
            if (nextEnd == std::string::npos) {
                break;
            }
            std::string line = buf.substr(pos, nextEnd - pos);
            // Trim trailing spaces/carriage returns
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty() && onLine) {
                onLine(line, isErr);
            }
            if (buf[nextEnd] == '\r' && nextEnd + 1 < buf.size() && buf[nextEnd + 1] == '\n') {
                pos = nextEnd + 2;
            } else {
                pos = nextEnd + 1;
            }
        }
        if (pos > 0) {
            buf.erase(0, pos);
        }
    };

    char chunk[4096];
    bool outOpen = (fdOut >= 0);
    bool errOpen = (fdErr >= 0);

    while (outOpen || errOpen) {
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

        if (outIdx >= 0) {
            if (pfd[outIdx].revents & POLLIN) {
                ssize_t n = read(fdOut, chunk, sizeof(chunk) - 1);
                if (n > 0) {
                    chunk[n] = '\0';
                    outBuf.append(chunk, n);
                    processBuffer(outBuf, false);
                } else if (n == 0) {
                    outOpen = false;
                }
            } else if (pfd[outIdx].revents & (POLLHUP | POLLERR)) {
                ssize_t n;
                while ((n = read(fdOut, chunk, sizeof(chunk) - 1)) > 0) {
                    chunk[n] = '\0';
                    outBuf.append(chunk, n);
                    processBuffer(outBuf, false);
                }
                outOpen = false;
            }
        }

        if (errIdx >= 0) {
            if (pfd[errIdx].revents & POLLIN) {
                ssize_t n = read(fdErr, chunk, sizeof(chunk) - 1);
                if (n > 0) {
                    chunk[n] = '\0';
                    errBuf.append(chunk, n);
                    processBuffer(errBuf, true);
                } else if (n == 0) {
                    errOpen = false;
                }
            } else if (pfd[errIdx].revents & (POLLHUP | POLLERR)) {
                ssize_t n;
                while ((n = read(fdErr, chunk, sizeof(chunk) - 1)) > 0) {
                    chunk[n] = '\0';
                    errBuf.append(chunk, n);
                    processBuffer(errBuf, true);
                }
                errOpen = false;
            }
        }
    }

    if (!outBuf.empty() && onLine) onLine(outBuf, false);
    if (!errBuf.empty() && onLine) onLine(errBuf, true);

    if (fdOut >= 0) close(fdOut);
    if (fdErr >= 0) close(fdErr);

    int status = 0;
    waitpid(pid, &status, 0);
    g_spawn_close_pid(pid);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
