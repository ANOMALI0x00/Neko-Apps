#pragma once

#include "Backend.h"

class XbpsBackend : public Backend {
public:
    XbpsBackend() = default;
    ~XbpsBackend() override = default;

    PkgSource source() const override { return PkgSource::XBPS; }
    bool isAvailable() const override;

    void loadCatalog(std::function<void(PkgSource, const PackageList&)> onReady) override;
    void checkUpdates(std::function<void(PkgSource, const PackageList&)> onReady) override;
    void getDetails(const std::string &pkgId, std::function<void(const Package&)> onReady) override;

    static std::string pkgverName(const std::string &pkgver);
    static std::string pkgverVersion(const std::string &pkgver);
    static std::vector<std::string> guessCategories(const std::string &name);
};
