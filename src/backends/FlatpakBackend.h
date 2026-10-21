#pragma once

#include "Backend.h"
#include <unordered_map>
#include <mutex>
#include <string>

struct FlatMeta {
    std::string version;
    std::string dlSize;
    std::string instSize;
};

class FlatpakBackend : public Backend {
public:
    FlatpakBackend() = default;
    ~FlatpakBackend() override = default;

    PkgSource source() const override { return PkgSource::Flatpak; }
    bool isAvailable() const override;

    void loadCatalog(std::function<void(PkgSource, const PackageList&)> onReady) override;
    void checkUpdates(std::function<void(PkgSource, const PackageList&)> onReady) override;
    void getDetails(const std::string &pkgId, std::function<void(const Package&)> onReady) override;

private:
    std::unordered_map<std::string, FlatMeta> m_remoteMeta;
    std::mutex m_metaMutex;
};
