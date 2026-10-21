#pragma once

#include "Package.h"
#include <functional>
#include <memory>

class Backend {
public:
    virtual ~Backend() = default;

    virtual PkgSource source() const = 0;
    virtual bool isAvailable() const = 0;

    virtual void loadCatalog(std::function<void(PkgSource, const PackageList&)> onReady) = 0;
    virtual void checkUpdates(std::function<void(PkgSource, const PackageList&)> onReady) = 0;
    virtual void getDetails(const std::string &pkgId, std::function<void(const Package&)> onReady) = 0;
};
