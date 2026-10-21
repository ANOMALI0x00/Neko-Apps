#pragma once

#include "Backend.h"

class AppImageBackend : public Backend {
public:
    AppImageBackend() = default;
    ~AppImageBackend() override = default;

    PkgSource source() const override { return PkgSource::AppImage; }
    bool isAvailable() const override { return true; }

    static std::string findSoarBinary();
    static bool hasSoar();
    static bool isSafeSoarId(const std::string &id);

    void loadCatalog(std::function<void(PkgSource, const PackageList&)> onReady) override;
    void checkUpdates(std::function<void(PkgSource, const PackageList&)> onReady) override;
    void getDetails(const std::string &pkgId, std::function<void(const Package&)> onReady) override;
};
