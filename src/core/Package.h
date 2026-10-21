#pragma once

#include <string>
#include <vector>

enum class PkgSource {
    XBPS = 0,
    Flatpak = 1,
    AppImage = 2,
    Source = 3
};

inline std::string sourceLabel(PkgSource s) {
    switch (s) {
    case PkgSource::XBPS: return "XBPS";
    case PkgSource::Flatpak: return "Flatpak";
    case PkgSource::AppImage: return "AppImage";
    case PkgSource::Source: return "Vouru";
    }
    return "";
}

inline std::string sourceKey(PkgSource s) {
    switch (s) {
    case PkgSource::XBPS: return "xbps";
    case PkgSource::Flatpak: return "flatpak";
    case PkgSource::AppImage: return "appimage";
    case PkgSource::Source: return "source";
    }
    return "";
}

enum class PkgState {
    NotInstalled = 0,
    Installed,
    Updatable
};

struct Package {
    PkgSource source = PkgSource::XBPS;
    PkgState state = PkgState::NotInstalled;

    std::string id;
    std::string name;
    std::string version;
    std::string installedVersion;
    std::string newVersion;
    std::string summary;
    std::string description;
    std::string repository;
    std::string homepage;
    std::string license;
    std::string maintainer;
    std::string iconPath;
    std::string downloadSize;
    std::string installedSize;
    std::vector<std::string> categories;
    std::vector<std::string> dependencies;
    bool detailsLoaded = false;

    std::string key() const {
        return sourceKey(source) + ":" + id;
    }

    bool isInstalled() const {
        return state != PkgState::NotInstalled;
    }
};

using PackageList = std::vector<Package>;
