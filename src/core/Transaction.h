#pragma once

#include "Package.h"
#include <functional>
#include <string>
#include <atomic>

struct DownloadProgress {
    std::string status;        // e.g. "Descargando 3proxy (248 KB)..."
    std::string speed;         // e.g. "2.4 MB/s"
    std::string downloadedStr; // e.g. "120 KB"
    std::string totalStr;      // e.g. "248 KB"
    double fraction = -1.0;    // 0.0 to 1.0 (-1.0 for pulsing/indeterminate)
    int stage = 0;             // 0: connecting, 1: downloading, 2: installing, 3: completed
};

class Transaction {
public:
    using LogCallback = std::function<void(const std::string &line, bool isStderr)>;
    using GlobalProgressCallback = std::function<void(const std::string &actionTitle, const DownloadProgress &prog)>;
    using GlobalFinishCallback = std::function<void(bool success, const std::string &msg)>;

    static void setGlobalLogCallback(LogCallback cb);
    static void setGlobalProgressCallback(GlobalProgressCallback cb);
    static void setGlobalFinishCallback(GlobalFinishCallback cb);

    static bool isBusy();
    static bool tryLock();
    static void unlock();

    static void install(
        const Package &pkg,
        std::function<void(bool success, const std::string &msg)> onFinished,
        std::function<void(const DownloadProgress &prog)> onProgress = nullptr
    );

    static void uninstall(
        const Package &pkg,
        std::function<void(bool success, const std::string &msg)> onFinished,
        std::function<void(const DownloadProgress &prog)> onProgress = nullptr
    );

    static void updateAll(
        const PackageList &pkgs,
        std::function<void(bool success, const std::string &msg)> onFinished,
        std::function<void(const DownloadProgress &prog)> onProgress = nullptr
    );

    static void cleanXbpsCache(std::function<void(bool success, const std::string &msg)> onFinished);
    static void removeXbpsOrphans(std::function<void(bool success, const std::string &msg)> onFinished);
    static void reconfigureXbps(std::function<void(bool success, const std::string &msg)> onFinished);

    static void setMirror(
        const std::string &targetUrl,
        std::function<void(bool success, const std::string &msg)> onFinished
    );

    static void installDriver(
        const std::string &driverId,
        const std::string &title,
        std::function<void(bool success, const std::string &msg)> onFinished
    );

    static void installLocalPackageFile(
        const std::string &filePath,
        const std::string &pkgName,
        std::function<void(bool success, const std::string &msg)> onFinished
    );

    static void executeScript(
        const std::string &actionTitle,
        const std::string &shellScript,
        std::function<void(bool success, const std::string &msg)> onFinished
    );

    static std::string elevateCmd();

private:
    static std::atomic<bool> s_busy;
};
