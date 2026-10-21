#include "AppImageBackend.h"
#include "Proc.h"
#include <glib.h>
#include <filesystem>
#include <thread>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <unistd.h>

namespace {

std::string trimStr(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Curated high-quality Soar / PkgForge portable & AppImage catalog with approximate sizes
struct SoarCurated {
    const char *id;
    const char *name;
    const char *summary;
    const char *category;
    const char *icon;
    const char *defaultSize;
};

struct SoarCatalogEntry {
    std::string id;
    std::string name;
    std::string summary;
    std::string category;
    std::string icon;
    std::string defaultSize;
};

static const SoarCurated kDefaultSoarCatalog[] = {
    {"obsidian",        "Obsidian",          "Base de conocimiento personal y notas en Markdown (Soar / AppImage)", "Oficina",     "obsidian",                 "115 MB"},
    {"krita",           "Krita",             "Estudio de pintura digital e ilustración profesional (Soar / AppImage)", "Gráficos", "krita",                    "240 MB"},
    {"kdenlive",        "Kdenlive",          "Editor de vídeo no lineal libre y multipista (Soar / AppImage)", "Multimedia",       "kdenlive",                 "165 MB"},
    {"blender",         "Blender",           "Suite de creación y animación 3D completa (Soar / AppImage)", "Gráficos",            "blender",                  "315 MB"},
    {"inkscape",        "Inkscape",          "Editor profesional de gráficos vectoriales (Soar / AppImage)", "Gráficos",           "inkscape",                 "140 MB"},
    {"audacity",        "Audacity",          "Editor y grabador de audio multipista (Soar / AppImage)", "Multimedia",              "audacity",                 "85 MB"},
    {"vlc",             "VLC Media Player",  "Reproductor multimedia universal portable (Soar / AppImage)", "Multimedia",          "vlc",                      "95 MB"},
    {"neovim",          "Neovim",            "Editor de texto hiperextensible basado en Vim (Soar / Portable)", "Desarrollo",      "nvim",                     "35 MB"},
    {"helix",           "Helix",             "Editor de texto modal moderno en terminal (Soar / Portable)", "Desarrollo",          "utilities-terminal",       "22 MB"},
    {"zellij",          "Zellij",            "Multiplexor de espacio de trabajo para terminal (Soar / Portable)", "Desarrollo",    "utilities-terminal",       "18 MB"},
    {"yazi",            "Yazi",              "Gestor de archivos ultrarrápido en terminal escrito en Rust (Soar)", "Utilidades",   "system-file-manager",      "15 MB"},
    {"lazygit",         "Lazygit",           "Interfaz de terminal simple y rápida para repositorios Git (Soar)", "Desarrollo",    "git",                      "16 MB"},
    {"btop",            "Btop++",            "Monitor de recursos e interfaz de sistema interactiva (Soar)", "Sistema",            "utilities-system-monitor", "12 MB"},
    {"fastfetch",       "Fastfetch",        "Herramienta rápida de información del sistema (Soar)", "Sistema",                    "utilities-terminal",       "8 MB"},
    {"ripgrep",         "Ripgrep",           "Herramienta de búsqueda recursiva ultrarrápida (Soar)", "Desarrollo",                "system-search",            "10 MB"},
    {"fd",              "fd",                "Alternativa rápida y amigable al comando find (Soar)", "Utilidades",                 "system-search",            "6 MB"},
    {"bat",             "Bat",               "Clon de cat con resaltado de sintaxis e integración Git (Soar)", "Desarrollo",       "text-x-generic",           "9 MB"},
    {"fzf",             "fzf",               "Buscador difuso interactivo de línea de comandos (Soar)", "Utilidades",              "system-search",            "5 MB"},
    {"jq",              "jq",                "Procesador de JSON ligero y flexible de línea de comandos (Soar)", "Desarrollo",     "text-x-script",            "4 MB"},
    {"upx",             "UPX",               "Empaquetador de ejecutables de alto rendimiento (Soar)", "Desarrollo",               "package-x-generic",        "2 MB"},
    {"localsend",       "LocalSend",         "Comparte archivos de forma segura en tu red local (Soar / AppImage)", "Internet",    "network-transmit-receive", "55 MB"},
    {"freetube",        "FreeTube",          "Cliente de escritorio privado para YouTube (Soar / AppImage)", "Multimedia",         "video-x-generic",          "95 MB"},
    {"librewolf",       "LibreWolf",         "Navegador web enfocado en privacidad y seguridad (Soar / AppImage)", "Internet",     "web-browser",              "125 MB"},
    {"appimagelauncher","AppImageLauncher", "Integrador de paquetes AppImage en el entorno de escritorio (Soar)", "Sistema",      "application-x-executable", "15 MB"}
};

static std::vector<SoarCatalogEntry> getCuratedCatalog() {
    std::vector<SoarCatalogEntry> results;

    const char *home = g_get_home_dir();
    std::vector<std::string> candidates;
    if (home && *home) {
        candidates.push_back(std::string(home) + "/.config/neko-apps/soar-catalog.tsv");
    }
    candidates.push_back("/usr/share/neko-apps/soar-catalog.tsv");
    candidates.push_back("/usr/local/share/neko-apps/soar-catalog.tsv");
    candidates.push_back("data/soar-catalog.tsv");
    candidates.push_back("../data/soar-catalog.tsv");

    for (const auto &p : candidates) {
        if (!std::filesystem::exists(p)) continue;
        std::ifstream f(p);
        if (!f.is_open()) continue;

        std::string line;
        while (std::getline(f, line)) {
            line = trimStr(line);
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string id, name, summary, cat, icon, sz;
            if (std::getline(ss, id, '\t') &&
                std::getline(ss, name, '\t') &&
                std::getline(ss, summary, '\t') &&
                std::getline(ss, cat, '\t') &&
                std::getline(ss, icon, '\t') &&
                std::getline(ss, sz, '\t')) {
                results.push_back({trimStr(id), trimStr(name), trimStr(summary), trimStr(cat), trimStr(icon), trimStr(sz)});
            }
        }
        if (!results.empty()) {
            return results;
        }
    }

    // Fallback to embedded default list if no external catalog is found
    for (const auto &item : kDefaultSoarCatalog) {
        results.push_back({item.id, item.name, item.summary, item.category, item.icon, item.defaultSize});
    }
    return results;
}
}

std::string AppImageBackend::findSoarBinary() {
    if (Proc::exists("soar")) {
        return "soar";
    }
    const char *home = g_get_home_dir();
    if (home && *home) {
        std::vector<std::string> fallbacks = {
            std::string(home) + "/.local/bin/soar",
            std::string(home) + "/.local/share/soar/bin/soar",
            std::string(home) + "/bin/soar"
        };
        for (const auto &p : fallbacks) {
            if (access(p.c_str(), X_OK) == 0) {
                return p;
            }
        }
    }
    return "";
}

bool AppImageBackend::hasSoar() {
    return !findSoarBinary().empty();
}

bool AppImageBackend::isSafeSoarId(const std::string &id) {
    if (id.empty() || id.size() > 128 || id[0] == '-' || id[0] == '/') {
        return false;
    }
    for (unsigned char c : id) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+' || c == '#' || c == ':' || c == '/')) {
            return false;
        }
    }
    return true;
}

void AppImageBackend::loadCatalog(std::function<void(PkgSource, const PackageList&)> onReady) {
    std::thread([onReady]() {
        PackageList list;
        std::unordered_set<std::string> seenIds;

        std::string soarBin = findSoarBinary();
        std::unordered_map<std::string, std::string> soarInstalledVersions;

        // 1. Query installed Soar packages if Soar is available
        if (!soarBin.empty()) {
            std::string infoOut = Proc::capture(soarBin, {"--no-color", "info"});
            std::istringstream iss(infoOut);
            std::string line;
            while (std::getline(iss, line)) {
                line = trimStr(line);
                if (line.empty() || line[0] == '[' || line.find("Total") == 0) continue;
                // Format typically: pkgname [version] (...) or pkgname#repo - version
                std::istringstream ls(line);
                std::string token, ver;
                if (ls >> token) {
                    if (ls >> ver) {
                        if (!ver.empty() && (ver.front() == '(' || ver.front() == '[')) {
                            ver = ver.substr(1, ver.size() > 2 ? ver.size() - 2 : 0);
                        }
                    }
                    std::string baseName = token;
                    auto hashPos = baseName.find('#');
                    if (hashPos != std::string::npos) {
                        baseName = baseName.substr(0, hashPos);
                    }
                    if (isSafeSoarId(baseName)) {
                        soarInstalledVersions[baseName] = ver;
                    }
                }
            }
        }

        // Add curated high-quality Soar / PkgForge portable & AppImage catalog
        auto curatedCatalog = getCuratedCatalog();
        for (const auto &item : curatedCatalog) {
            Package p;
            p.source = PkgSource::AppImage;
            p.id = item.id;
            p.name = item.name;
            p.summary = item.summary;
            p.iconPath = item.icon;
            p.categories = std::vector<std::string>{item.category};
            p.license = "Open Source / Portable (Soar)";
            p.downloadSize = item.defaultSize;
            p.installedSize = item.defaultSize;

            auto it = soarInstalledVersions.find(item.id);
            if (it != soarInstalledVersions.end()) {
                p.state = PkgState::Installed;
                p.version = it->second;
                p.installedVersion = it->second;
                soarInstalledVersions.erase(it);
            } else {
                p.state = PkgState::NotInstalled;
            }
            seenIds.insert(p.id);
            list.push_back(std::move(p));
        }

        // Include any additional user-installed Soar packages not in the curated list
        for (const auto &kv : soarInstalledVersions) {
            if (seenIds.count(kv.first)) continue;
            Package p;
            p.source = PkgSource::AppImage;
            p.id = kv.first;
            p.name = kv.first;
            if (!p.name.empty()) p.name[0] = std::toupper(static_cast<unsigned char>(p.name[0]));
            p.summary = "Paquete portable instalado mediante Soar (" + kv.first + ")";
            p.state = PkgState::Installed;
            p.version = kv.second;
            p.installedVersion = kv.second;
            p.iconPath = kv.first;
            p.categories = {"Utilidades"};
            seenIds.insert(p.id);
            list.push_back(std::move(p));
        }

        // 2. Scan local AppImage directories (~/Applications, ~/.local/bin, /Applications)
        const char *home = g_get_home_dir();
        std::vector<std::string> dirs;
        if (home && *home) {
            dirs.push_back(std::string(home) + "/Applications");
            dirs.push_back(std::string(home) + "/.local/bin");
        }
        dirs.push_back("/Applications");

        for (const auto &d : dirs) {
            std::error_code ec;
            if (!std::filesystem::exists(d, ec)) continue;
            try {
                for (const auto &entry : std::filesystem::directory_iterator(d, ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    std::string filename = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();
                    std::string lowExt = ext;
                    for (auto &c : lowExt) c = std::tolower(static_cast<unsigned char>(c));

                    if (lowExt == ".appimage") {
                        Package p;
                        p.source = PkgSource::AppImage;
                        p.id = entry.path().string();
                        p.name = entry.path().stem().string();
                        p.summary = "Aplicación portable AppImage (" + filename + ")";
                        p.state = PkgState::Installed;
                        p.iconPath = p.name;
                        p.categories = {"Utilidades"};

                        std::error_code szEc;
                        auto sz = std::filesystem::file_size(entry.path(), szEc);
                        if (!szEc) {
                            char buf[32];
                            if (sz >= 1024 * 1024) {
                                snprintf(buf, sizeof(buf), "%.1f MB", sz / (1024.0 * 1024.0));
                            } else {
                                snprintf(buf, sizeof(buf), "%.0f KB", sz / 1024.0);
                            }
                            p.downloadSize = buf;
                            p.installedSize = buf;
                        }

                        list.push_back(std::move(p));
                    }
                }
            } catch (...) {}
        }

        Proc::runOnMainThread([onReady, list]() {
            if (onReady) onReady(PkgSource::AppImage, list);
        });
    }).detach();
}

void AppImageBackend::checkUpdates(std::function<void(PkgSource, const PackageList&)> onReady) {
    if (onReady) onReady(PkgSource::AppImage, {});
}

void AppImageBackend::getDetails(const std::string &pkgId, std::function<void(const Package&)> onReady) {
    std::thread([pkgId, onReady]() {
        Package p;
        p.source = PkgSource::AppImage;
        p.id = pkgId;

        if (!pkgId.empty() && pkgId[0] == '/') {
            p.name = std::filesystem::path(pkgId).stem().string();
            p.iconPath = p.name;
            p.summary = "Paquete portable AppImage local";
            p.description = "Aplicación autónoma ejecutable en el espacio del usuario sin modificar el sistema raíz.";
            p.license = "Portable / AppImage";
            p.categories = {"Utilidades"};

            std::error_code ec;
            p.state = std::filesystem::exists(pkgId, ec) ? PkgState::Installed : PkgState::NotInstalled;
            auto sz = std::filesystem::file_size(pkgId, ec);
            if (!ec) {
                char buf[32];
                if (sz >= 1024 * 1024) {
                    snprintf(buf, sizeof(buf), "%.1f MB", sz / (1024.0 * 1024.0));
                } else {
                    snprintf(buf, sizeof(buf), "%.0f KB", sz / 1024.0);
                }
                p.downloadSize = buf;
                p.installedSize = buf;
            }
        } else {
            p.name = pkgId;
            if (!p.name.empty()) p.name[0] = std::toupper(static_cast<unsigned char>(p.name[0]));
            p.iconPath = pkgId;
            p.summary = "Paquete portable gestionado con Soar (PkgForge)";
            p.description = "Paquete portable / AppImage distribuido a través de los repositorios de PkgForge (Soar) e instalado de forma aislada en el perfil del usuario.";
            p.license = "Open Source / Portable";
            p.homepage = "https://pkgs.pkgforge.dev";
            p.maintainer = "PkgForge / Soar";
            p.categories = {"Utilidades"};

            std::string cleanPkgId = pkgId;
            if (cleanPkgId.size() > 8 && cleanPkgId.substr(cleanPkgId.size() - 8) == ".desktop") {
                cleanPkgId = cleanPkgId.substr(0, cleanPkgId.size() - 8);
            }
            std::string lowId = cleanPkgId;
            for (auto &c : lowId) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            for (const auto &item : getCuratedCatalog()) {
                if (cleanPkgId == item.id || lowId == item.id) {
                    p.name = item.name;
                    p.summary = item.summary;
                    p.description = item.summary;
                    p.iconPath = item.icon;
                    p.categories = std::vector<std::string>{item.category};
                    p.downloadSize = item.defaultSize;
                    p.installedSize = item.defaultSize;
                    break;
                }
            }

            std::string soarBin = findSoarBinary();
            if (!soarBin.empty() && isSafeSoarId(pkgId)) {
                std::string infoOut = Proc::capture(soarBin, {"--no-color", "info", pkgId});
                p.state = (infoOut.find(pkgId) != std::string::npos &&
                           infoOut.find("not installed") == std::string::npos &&
                           infoOut.find("No packages") == std::string::npos &&
                           !trimStr(infoOut).empty())
                          ? PkgState::Installed : PkgState::NotInstalled;

                std::string queryOut = Proc::capture(soarBin, {"--no-color", "query", pkgId});
                std::istringstream iss(queryOut);
                std::string line;
                while (std::getline(iss, line)) {
                    std::string t = trimStr(line);
                    if (t.find("Description:") == 0) {
                        std::string desc = trimStr(t.substr(12));
                        if (!desc.empty()) {
                            p.summary = desc;
                            p.description = desc;
                        }
                    } else if (t.find("Version:") == 0 && p.version.empty()) {
                        p.version = trimStr(t.substr(8));
                    } else if (t.find("Size:") == 0 || t.find("size:") == 0) {
                        std::string sz = trimStr(t.substr(5));
                        if (!sz.empty()) {
                            p.downloadSize = sz;
                            p.installedSize = sz;
                        }
                    } else if (t.find("License:") == 0) {
                        p.license = trimStr(t.substr(8));
                    } else if (t.find("Homepage:") == 0 || t.find("Website:") == 0) {
                        auto colon = t.find(':');
                        if (colon != std::string::npos) {
                            p.homepage = trimStr(t.substr(colon + 1));
                        }
                    }
                }
            }
        }

        p.detailsLoaded = true;
        Proc::runOnMainThread([onReady, p]() {
            if (onReady) onReady(p);
        });
    }).detach();
}

