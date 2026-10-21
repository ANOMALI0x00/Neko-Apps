#include "XbpsBackend.h"
#include "Proc.h"
#include "Config.h"
#include <glib.h>
#include <thread>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <cctype>

namespace {

std::string resolveBaseIconName(const std::string &pkgId) {
    std::string base = pkgId;
    const std::vector<std::string> suffixes = {"-bin", "-git", "-latest", "-nk", "-app"};
    for (const auto &suf : suffixes) {
        if (base.size() > suf.size() && base.compare(base.size() - suf.size(), suf.size(), suf) == 0) {
            base = base.substr(0, base.size() - suf.size());
            break;
        }
    }
    if (base == "vscode") return "visual-studio-code";
    if (base == "heroic-games") return "heroic";
    if (base == "onlyoffice") return "onlyoffice-desktopeditors";
    return base;
}

std::string expandTemplateVars(const std::string &input, const std::unordered_map<std::string, std::string> &vars) {
    std::string out = input;
    for (int pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (const auto &kv : vars) {
            if (kv.first.empty()) continue;
            std::string b1 = "${" + kv.first + "}";
            size_t pos = 0;
            while ((pos = out.find(b1, pos)) != std::string::npos) {
                out.replace(pos, b1.size(), kv.second);
                pos += kv.second.size();
                changed = true;
            }
            std::string b2 = "$" + kv.first;
            pos = 0;
            while ((pos = out.find(b2, pos)) != std::string::npos) {
                size_t after = pos + b2.size();
                if (after == out.size() || (!std::isalnum(static_cast<unsigned char>(out[after])) && out[after] != '_')) {
                    out.replace(pos, b2.size(), kv.second);
                    pos += kv.second.size();
                    changed = true;
                } else {
                    pos = after;
                }
            }
        }
        if (!changed) break;
    }
    return out;
}

bool parseVoidTemplate(const std::filesystem::path &tplPath,
                       const std::string &dirName,
                       const std::string &repoUrl,
                       Package &outPkg) {
    std::ifstream f(tplPath);
    if (!f.is_open()) return false;

    std::unordered_map<std::string, std::string> vars;
    vars["pkgname"] = dirName;

    std::string line;
    while (std::getline(f, line)) {
        // Trim leading spaces
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;

        // Only parse simple top-level variable assignments: key=value
        size_t eq = line.find('=', start);
        if (eq == std::string::npos || eq == start) continue;

        std::string key = line.substr(start, eq - start);
        bool validKey = true;
        for (char c : key) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                validKey = false;
                break;
            }
        }
        if (!validKey) continue;

        std::string val = line.substr(eq + 1);
        while (!val.empty() && (val.back() == '\r' || val.back() == ' ' || val.back() == '\t')) val.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);

        // Handle multi-line quoted string if closing quote is on a subsequent line
        if (!val.empty() && (val.front() == '"' || val.front() == '\'')) {
            char q = val.front();
            if (val.size() == 1 || val.back() != q) {
                std::string cont;
                while (std::getline(f, cont)) {
                    while (!cont.empty() && cont.back() == '\r') cont.pop_back();
                    val += " " + cont;
                    if (!cont.empty() && cont.find(q) != std::string::npos) break;
                }
            }
            if (val.size() >= 2 && val.front() == q && val.back() == q) {
                val = val.substr(1, val.size() - 2);
            }
        } else {
            // Strip unquoted inline comment
            size_t hashPos = val.find(" #");
            if (hashPos == std::string::npos) hashPos = val.find("\t#");
            if (hashPos != std::string::npos) {
                val = val.substr(0, hashPos);
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
            }
        }

        vars[key] = val;
    }

    std::string pkgname = expandTemplateVars(vars["pkgname"], vars);
    if (pkgname.empty()) pkgname = dirName;

    std::string version = expandTemplateVars(vars["version"], vars);
    std::string revision = expandTemplateVars(vars["revision"], vars);
    std::string shortDesc = expandTemplateVars(vars["short_desc"], vars);
    std::string maintainer = expandTemplateVars(vars["maintainer"], vars);
    std::string license = expandTemplateVars(vars["license"], vars);
    std::string homepage = expandTemplateVars(vars["homepage"], vars);

    outPkg.source = PkgSource::Source;
    outPkg.id = pkgname;
    outPkg.name = pkgname;
    outPkg.version = version + (!version.empty() && !revision.empty() ? ("_" + revision) : "");
    outPkg.summary = !shortDesc.empty() ? shortDesc : ("Plantilla Vouru (" + pkgname + ")");
    outPkg.description = outPkg.summary;
    outPkg.maintainer = maintainer;
    outPkg.license = license;
    outPkg.homepage = homepage;
    outPkg.repository = repoUrl;
    outPkg.iconPath = resolveBaseIconName(pkgname);
    outPkg.categories = XbpsBackend::guessCategories(pkgname);
    outPkg.detailsLoaded = true;
    return true;
}
}

bool XbpsBackend::isAvailable() const {
    return Proc::exists("xbps-query");
}

std::string XbpsBackend::pkgverName(const std::string &pkgver) {
    auto dash = pkgver.rfind('-');
    return (dash != std::string::npos && dash > 0) ? pkgver.substr(0, dash) : pkgver;
}

std::string XbpsBackend::pkgverVersion(const std::string &pkgver) {
    auto dash = pkgver.rfind('-');
    return (dash != std::string::npos && dash > 0) ? pkgver.substr(dash + 1) : "";
}

std::vector<std::string> XbpsBackend::guessCategories(const std::string &rawName) {
    std::string n = rawName;
    for (auto &ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    std::vector<std::string> c;
    if (n.find("-devel") != std::string::npos || n == "gcc" || n == "clang" || n == "cmake" ||
        n == "git" || n == "rust" || n == "cargo" || n == "go" || n == "nodejs" ||
        n == "python3" || n == "neovim" || n == "vim" || n == "emacs" ||
        n.find("vscode") != std::string::npos || n.find("antigravity") != std::string::npos ||
        n.find("codex") != std::string::npos || n.find("opencode") != std::string::npos ||
        n.find("unityhub") != std::string::npos || n.find("zig") == 0 || n == "3utools" ||
        n == "code" || n == "gdb" || n == "lldb" || n == "meson" || n == "ninja" ||
        n == "make" || n == "pkg-config" || n.find("qt5-") == 0 || n.find("qt6-") == 0 ||
        n.find("gtk+") == 0 || n.find("gtk4") == 0) {
        c.push_back("Desarrollo");
    } else if (n.find("firefox") != std::string::npos || n.find("chromium") != std::string::npos ||
               n.find("brave") != std::string::npos || n.find("waterfox") != std::string::npos ||
               n.find("mullvad") != std::string::npos || n.find("helium") != std::string::npos ||
               n.find("windscribe") != std::string::npos || n.find("zerotier") != std::string::npos ||
               n.find("thunderbird") != std::string::npos || n.find("telegram") != std::string::npos ||
               n.find("discord") != std::string::npos || n.find("vesktop") != std::string::npos ||
               n.find("qbittorrent") != std::string::npos ||
               n.find("transmission") != std::string::npos || n.find("filezilla") != std::string::npos ||
               n.find("curl") != std::string::npos || n.find("wget") != std::string::npos ||
               n.find("weechat") != std::string::npos || n.find("hexchat") != std::string::npos ||
               n.find("networkmanager") != std::string::npos || n.find("wpa_") != std::string::npos) {
        c.push_back("Internet");
    } else if (n.find("steam") != std::string::npos || n.find("lutris") != std::string::npos ||
               n.find("heroic") != std::string::npos || n.find("hydra") != std::string::npos ||
               n.find("faugus") != std::string::npos || n == "itch" ||
               n.find("portproton") != std::string::npos || n.find("protonup") != std::string::npos ||
               n.find("hytale") != std::string::npos || n.find("pineconemc") != std::string::npos ||
               n.find("trinity-launcher") != std::string::npos || n.find("goverlay") != std::string::npos ||
               n.find("lsfg-vk") != std::string::npos || n.find("falcond") != std::string::npos ||
               n.find("retroarch") != std::string::npos || n.find("game") != std::string::npos ||
               n.find("minetest") != std::string::npos || n.find("supertux") != std::string::npos ||
               n.find("0ad") != std::string::npos || n.find("wesnoth") != std::string::npos ||
               n.find("wine") != std::string::npos || n.find("prism") != std::string::npos ||
               n.find("mangohud") != std::string::npos || n.find("dolphin-emu") != std::string::npos) {
        c.push_back("Juegos");
    } else if (n.find("vlc") != std::string::npos || n.find("mpv") != std::string::npos ||
               n.find("obs") != std::string::npos || n.find("audacity") != std::string::npos ||
               n.find("kdenlive") != std::string::npos || n.find("ffmpeg") != std::string::npos ||
               n.find("pipewire") != std::string::npos || n.find("pulseaudio") != std::string::npos ||
               n.find("alsa") != std::string::npos || n.find("ardour") != std::string::npos ||
               n.find("lmms") != std::string::npos || n.find("handbrake") != std::string::npos ||
               n.find("spotify") != std::string::npos || n.find("spicetify") != std::string::npos ||
               n.find("stremio") != std::string::npos || n.find("rhythmbox") != std::string::npos) {
        c.push_back("Multimedia");
    } else if (n.find("libreoffice") != std::string::npos || n.find("onlyoffice") != std::string::npos ||
               n.find("evince") != std::string::npos ||
               n.find("okular") != std::string::npos || n.find("zathura") != std::string::npos ||
               n.find("calibre") != std::string::npos || n.find("abiword") != std::string::npos ||
               n.find("gnumeric") != std::string::npos || n.find("ghostscript") != std::string::npos ||
               n.find("foliate") != std::string::npos) {
        c.push_back("Oficina");
    } else if (n.find("gimp") != std::string::npos || n.find("inkscape") != std::string::npos ||
               n.find("aseprite") != std::string::npos || n.find("orcaslicer") != std::string::npos ||
               n.find("krita") != std::string::npos || n.find("blender") != std::string::npos ||
               n.find("darktable") != std::string::npos || n.find("rawtherapee") != std::string::npos ||
               n.find("imagemagick") != std::string::npos || n.find("gwenview") != std::string::npos ||
               n.find("feh") != std::string::npos || n.find("ristretto") != std::string::npos ||
               n.find("flameshot") != std::string::npos || n.find("godot") != std::string::npos) {
        c.push_back("Gráficos");
    } else if (n.find("stellarium") != std::string::npos || n.find("geogebra") != std::string::npos ||
               n.find("octave") != std::string::npos || n.find("maxima") != std::string::npos ||
               n.find("kiwix") != std::string::npos || n.find("anki") != std::string::npos ||
               n.find("kalzium") != std::string::npos || n.find("science") != std::string::npos) {
        c.push_back("Educación");
    } else if (n.rfind("font-", 0) == 0 || n.rfind("lib", 0) == 0 || n.rfind("linux", 0) == 0 ||
               n.rfind("neko-", 0) == 0 || n.rfind("xfce", 0) == 0 || n.rfind("xbps", 0) == 0 ||
               n.rfind("xorg", 0) == 0 || n.rfind("mesa", 0) == 0 || n.rfind("vulkan", 0) == 0 ||
               n.find("noctalia") != std::string::npos || n.find("labwc") != std::string::npos ||
               n.find("mangowc") != std::string::npos || n.find("scenefx") != std::string::npos ||
               n.find("niri") != std::string::npos || n.find("soar") != std::string::npos ||
               n == "htop" || n == "btop" || n == "fastfetch" || n == "neofetch" || n == "gparted") {
        c.push_back("Sistema");
    } else {
        c.push_back("Utilidades");
    }
    return c;
}

void XbpsBackend::loadCatalog(std::function<void(PkgSource, const PackageList&)> onReady) {
    std::thread([onReady]() {
        // 1. Get installed packages
        std::unordered_map<std::string, std::pair<std::string, std::string>> installed;
        std::string localOut = Proc::capture("xbps-query", {"-l"});
        std::istringstream localIss(localOut);
        std::string line;
        while (std::getline(localIss, line)) {
            if (line.size() < 5) continue;
            // e.g. "ii firefox-128.0_1 Mozilla Firefox web browser"
            std::istringstream ls(line);
            std::string state, pkgver;
            if (ls >> state >> pkgver) {
                std::string summary;
                std::getline(ls, summary);
                while (!summary.empty() && summary[0] == ' ') summary.erase(0, 1);
                std::string name = pkgverName(pkgver);
                std::string ver = pkgverVersion(pkgver);
                installed[name] = {ver, summary};
            }
        }

        // 2. Query remote repository catalog
        std::string remoteOut = Proc::capture("xbps-query", {"-Rs", ""});
        std::istringstream remoteIss(remoteOut);
        PackageList list;
        list.reserve(installed.size() + 1024);
        std::unordered_set<std::string> seen;

        while (std::getline(remoteIss, line)) {
            if (line.size() < 5) continue;
            // e.g. "[-] firefox-128.0_1 Mozilla Firefox web browser"
            // or   "[*] bash-5.2.037_1 GNU Bourne-Again Shell"
            if (line[0] != '[') continue;
            auto bracket = line.find(']');
            if (bracket == std::string::npos) continue;

            std::string rest = line.substr(bracket + 1);
            while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

            auto sp = rest.find(' ');
            std::string pkgver = (sp != std::string::npos) ? rest.substr(0, sp) : rest;
            std::string summary = (sp != std::string::npos) ? rest.substr(sp + 1) : "";
            while (!summary.empty() && summary[0] == ' ') summary.erase(0, 1);

            std::string name = pkgverName(pkgver);
            if (!seen.insert(name).second) continue;

            Package p;
            p.source = PkgSource::XBPS;
            p.id = name;
            p.name = name;
            p.version = pkgverVersion(pkgver);
            p.summary = summary;
            p.iconPath = resolveBaseIconName(name);
            p.categories = guessCategories(name);

            auto itInst = installed.find(name);
            if (itInst != installed.end()) {
                p.state = PkgState::Installed;
                p.installedVersion = itInst->second.first;
            } else {
                p.state = PkgState::NotInstalled;
            }

            list.push_back(std::move(p));
        }

        // Also add installed packages that might not be in the remote list
        for (const auto &pair : installed) {
            if (seen.find(pair.first) == seen.end()) {
                Package p;
                p.source = PkgSource::XBPS;
                p.id = pair.first;
                p.name = pair.first;
                p.version = pair.second.first;
                p.installedVersion = pair.second.first;
                p.summary = pair.second.second;
                p.state = PkgState::Installed;
                p.iconPath = resolveBaseIconName(pair.first);
                p.categories = guessCategories(pair.first);
                list.push_back(std::move(p));
            }
        }

        // 3. Sync custom xbps-src template repositories into ~/.local/share/pkgs/void-packages/srcpkgs/ (Vouru integration)
        if (Config::instance().sourceEnabled()) {
            auto customRepos = Config::instance().customSourceRepos();
            std::string vpDir = Config::instance().voidPackagesDir();
            std::filesystem::path vpSrcpkgs = std::filesystem::path(vpDir) / "srcpkgs";
            const char *cacheBase = g_get_user_cache_dir();
            std::string reposCacheDir = (cacheBase && *cacheBase ? std::string(cacheBase) : "/tmp") + "/neko-apps/custom_repos";
            std::error_code ec;
            std::filesystem::create_directories(reposCacheDir, ec);
            std::filesystem::create_directories(vpSrcpkgs, ec);

            // Load existing vouru template-sources.tsv index: (repoDir, pkgName) -> repoUrl
            std::string vouruTsvPath = Config::instance().vouruSourcesFile();
            std::map<std::pair<std::string, std::string>, std::string> vouruIndex;
            {
                std::ifstream tsvIn(vouruTsvPath);
                if (tsvIn.is_open()) {
                    std::string tline;
                    while (std::getline(tsvIn, tline)) {
                        if (tline.empty()) continue;
                        auto t1 = tline.find('\t');
                        if (t1 == std::string::npos) continue;
                        auto t2 = tline.find('\t', t1 + 1);
                        if (t2 == std::string::npos) continue;
                        std::string rDir = tline.substr(0, t1);
                        std::string pName = tline.substr(t1 + 1, t2 - (t1 + 1));
                        std::string rUrl = tline.substr(t2 + 1);
                        while (!rUrl.empty() && (rUrl.back() == '\r' || rUrl.back() == '\n' || rUrl.back() == ' ')) rUrl.pop_back();
                        if (!rDir.empty() && !pName.empty() && !rUrl.empty()) {
                            vouruIndex[{rDir, pName}] = rUrl;
                        }
                    }
                }
            }

            std::unordered_set<std::string> seenSource;
            bool indexModified = false;

            for (std::string repoUrl : customRepos) {
                while (!repoUrl.empty() && (repoUrl.back() == ' ' || repoUrl.back() == '\t' || repoUrl.back() == '\r' || repoUrl.back() == '/')) {
                    repoUrl.pop_back();
                }
                while (!repoUrl.empty() && (repoUrl.front() == ' ' || repoUrl.front() == '\t')) {
                    repoUrl.erase(0, 1);
                }
                if (repoUrl.empty()) continue;

                std::string repoDir;
                if (std::filesystem::is_directory(repoUrl, ec)) {
                    repoDir = repoUrl;
                } else {
                    std::string safeName;
                    for (char c : repoUrl) {
                        if (std::isalnum(static_cast<unsigned char>(c))) safeName += c;
                        else safeName += '_';
                    }
                    repoDir = reposCacheDir + "/" + safeName;

                    if (!std::filesystem::exists(repoDir + "/.git", ec)) {
                        std::filesystem::remove_all(repoDir, ec);
                        std::string cloneCmd = "GIT_TERMINAL_PROMPT=0 git clone --depth=1 '" + repoUrl + "' '" + repoDir + "' >/dev/null 2>&1";
                        Proc::capture("sh", {"-c", cloneCmd});
                    } else {
                        std::string pullCmd = "GIT_TERMINAL_PROMPT=0 git -C '" + repoDir + "' pull --ff-only >/dev/null 2>&1 || true";
                        Proc::capture("sh", {"-c", pullCmd});
                    }
                }

                if (repoDir.empty() || !std::filesystem::is_directory(repoDir, ec)) continue;

                // Scan both repoDir/<pkg>/template (e.g. Neko-Void-Linux/cnr) and repoDir/srcpkgs/<pkg>/template
                std::vector<std::filesystem::path> scanRoots = {
                    std::filesystem::path(repoDir),
                    std::filesystem::path(repoDir) / "srcpkgs"
                };

                // First pass: copy real template directories into ~/.local/share/pkgs/void-packages/srcpkgs/<pkg>
                for (const auto &root : scanRoots) {
                    if (!std::filesystem::is_directory(root, ec)) continue;
                    for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
                        std::string dName = entry.path().filename().string();
                        if (dName.empty() || dName[0] == '.' || dName == "srcpkgs" || dName == "common") continue;
                        if (entry.is_symlink(ec)) continue;

                        std::filesystem::path tplPath = entry.path() / "template";
                        if (!std::filesystem::is_regular_file(tplPath, ec)) continue;

                        std::filesystem::path dstPkgDir = vpSrcpkgs / dName;
                        std::filesystem::remove_all(dstPkgDir, ec);
                        std::filesystem::copy(
                            entry.path(),
                            dstPkgDir,
                            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                            ec
                        );
                        std::filesystem::remove_all(dstPkgDir / ".git", ec);
                        vouruIndex[{vpDir, dName}] = repoUrl;
                        indexModified = true;
                    }
                }

                // Second pass: copy relative subpackage symlinks (e.g. scx-common -> scx) into srcpkgs/
                for (const auto &root : scanRoots) {
                    if (!std::filesystem::is_directory(root, ec)) continue;
                    for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
                        std::string dName = entry.path().filename().string();
                        if (dName.empty() || dName[0] == '.' || dName == "srcpkgs" || dName == "common") continue;
                        if (!entry.is_symlink(ec)) continue;

                        auto symTarget = std::filesystem::read_symlink(entry.path(), ec);
                        if (!ec && !symTarget.empty() && symTarget.is_relative()) {
                            std::filesystem::path dstSym = vpSrcpkgs / dName;
                            std::filesystem::remove_all(dstSym, ec);
                            std::filesystem::create_directory_symlink(symTarget, dstSym, ec);
                            vouruIndex[{vpDir, dName}] = repoUrl;
                            indexModified = true;
                        }
                    }
                }
            }

            if (indexModified) {
                std::filesystem::create_directories(std::filesystem::path(vouruTsvPath).parent_path(), ec);
                std::ofstream tsvOut(vouruTsvPath, std::ios::trunc);
                if (tsvOut.is_open()) {
                    for (const auto &kv : vouruIndex) {
                        tsvOut << kv.first.first << "\t" << kv.first.second << "\t" << kv.second << "\n";
                    }
                }
            }

            // Parse all registered custom templates from ~/.local/share/pkgs/void-packages/srcpkgs/<pkg>/template
            for (const auto &kv : vouruIndex) {
                const std::string &rDir = kv.first.first;
                const std::string &dName = kv.first.second;
                const std::string &repoUrl = kv.second;

                std::filesystem::path pkgDirPath = std::filesystem::path(rDir) / "srcpkgs" / dName;
                std::filesystem::path tplPath = pkgDirPath / "template";
                if (!std::filesystem::is_regular_file(tplPath, ec)) {
                    tplPath = vpSrcpkgs / dName / "template";
                    pkgDirPath = vpSrcpkgs / dName;
                }
                if (!std::filesystem::is_regular_file(tplPath, ec)) continue;

                Package sp;
                if (parseVoidTemplate(tplPath, dName, repoUrl, sp)) {
                    if (std::filesystem::is_symlink(pkgDirPath, ec) && sp.id != dName) {
                        sp.id = dName;
                        sp.name = dName;
                        sp.iconPath = resolveBaseIconName(dName);
                        sp.categories = guessCategories(dName);
                    }
                    if (!seenSource.insert(sp.id).second) continue;

                    auto itInst = installed.find(sp.id);
                    if (itInst != installed.end()) {
                        sp.state = PkgState::Installed;
                        sp.installedVersion = itInst->second.first;
                    } else {
                        sp.state = PkgState::NotInstalled;
                    }

                    list.push_back(std::move(sp));
                }
            }
        }

        Proc::runOnMainThread([onReady, list = std::move(list)]() {
            if (onReady) {
                onReady(PkgSource::XBPS, list);
            }
        });
    }).detach();
}

void XbpsBackend::checkUpdates(std::function<void(PkgSource, const PackageList&)> onReady) {
    std::thread([onReady]() {
        // Map installed versions for accurate "installed -> new" comparison
        std::unordered_map<std::string, std::string> installedVers;
        std::string localOut = Proc::capture("xbps-query", {"-l"});
        std::istringstream localIss(localOut);
        std::string line;
        while (std::getline(localIss, line)) {
            if (line.size() < 5) continue;
            std::istringstream ls(line);
            std::string state, pkgver;
            if (ls >> state >> pkgver) {
                installedVers[pkgverName(pkgver)] = pkgverVersion(pkgver);
            }
        }

        std::string out = Proc::capture("xbps-install", {"-un"});
        std::istringstream iss(out);
        PackageList updates;

        while (std::getline(iss, line)) {
            // e.g. "firefox-130.0_1 update x86_64 https://... 240000000 68000000"
            std::istringstream ls(line);
            std::string pkgver, action, arch, repo;
            uint64_t instBytes = 0, dlBytes = 0;
            if (ls >> pkgver >> action) {
                if (action != "update") continue;
                Package p;
                p.source = PkgSource::XBPS;
                p.state = PkgState::Updatable;
                p.id = pkgverName(pkgver);
                p.name = p.id;
                p.newVersion = pkgverVersion(pkgver);
                auto it = installedVers.find(p.id);
                if (it != installedVers.end()) {
                    p.installedVersion = it->second;
                    p.version = it->second;
                } else {
                    p.version = p.newVersion;
                }
                if (ls >> arch >> repo >> instBytes >> dlBytes && dlBytes > 0) {
                    char buf[32];
                    if (dlBytes >= 1024 * 1024) {
                        snprintf(buf, sizeof(buf), "%.1f MB", dlBytes / (1024.0 * 1024.0));
                    } else {
                        snprintf(buf, sizeof(buf), "%.0f KB", dlBytes / 1024.0);
                    }
                    p.downloadSize = buf;
                }
                p.iconPath = p.id;
                updates.push_back(std::move(p));
            }
        }

        Proc::runOnMainThread([onReady, updates = std::move(updates)]() {
            if (onReady) {
                onReady(PkgSource::XBPS, updates);
            }
        });
    }).detach();
}

void XbpsBackend::getDetails(const std::string &pkgId, std::function<void(const Package&)> onReady) {
    std::thread([pkgId, onReady]() {
        Package p;
        p.source = PkgSource::XBPS;
        p.id = pkgId;
        p.name = pkgId;
        p.iconPath = pkgId;
        p.categories = guessCategories(pkgId);

        std::string localOut = Proc::capture("xbps-query", {"-S", pkgId});
        bool isLocal = !localOut.empty();
        std::string out = isLocal ? localOut : Proc::capture("xbps-query", {"-R", "-S", pkgId});

        p.state = isLocal ? PkgState::Installed : PkgState::NotInstalled;

        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(0, 1);

            if (key == "pkgver") {
                p.version = XbpsBackend::pkgverVersion(val);
                if (isLocal) p.installedVersion = p.version;
            } else if (key == "short_desc" || key == "short-desc") {
                p.summary = val;
            } else if (key == "homepage") {
                p.homepage = val;
            } else if (key == "license") {
                p.license = val;
            } else if (key == "maintainer") {
                p.maintainer = val;
            } else if (key == "installed_size" || key == "installed-size") {
                p.installedSize = val;
            } else if (key == "filename_size" || key == "filename-size") {
                p.downloadSize = val;
            } else if (key == "repository") {
                p.repository = val;
            }
        }

        // If installed locally, filename-size may not be in local DB; query remote DB for download size
        if (p.downloadSize.empty()) {
            std::string remOut = isLocal ? Proc::capture("xbps-query", {"-R", "-S", pkgId}) : "";
            if (!remOut.empty()) {
                std::istringstream riss(remOut);
                while (std::getline(riss, line)) {
                    auto colon = line.find(':');
                    if (colon == std::string::npos) continue;
                    std::string key = line.substr(0, colon);
                    std::string val = line.substr(colon + 1);
                    while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(0, 1);
                    if (key == "filename_size" || key == "filename-size") {
                        p.downloadSize = val;
                    } else if (key == "repository" && p.repository.empty()) {
                        p.repository = val;
                    }
                }
            }
        }

        if (p.downloadSize.empty() && !isLocal) {
            std::string dry = Proc::capture("xbps-install", {"-n", pkgId});
            std::istringstream diss(dry);
            std::string pver, act, arch, repo;
            uint64_t instBytes = 0, dlBytes = 0;
            if (diss >> pver >> act >> arch >> repo >> instBytes >> dlBytes) {
                if (dlBytes > 0) {
                    char buf[32];
                    if (dlBytes >= 1024 * 1024) {
                        snprintf(buf, sizeof(buf), "%.1f MB", dlBytes / (1024.0 * 1024.0));
                    } else {
                        snprintf(buf, sizeof(buf), "%.0f KB", dlBytes / 1024.0);
                    }
                    p.downloadSize = buf;
                }
            }
        }

        if (p.downloadSize.empty() && !p.installedSize.empty()) {
            p.downloadSize = p.installedSize + " (instalado)";
        }

        p.description = p.summary;
        p.detailsLoaded = true;

        Proc::runOnMainThread([onReady, p]() {
            if (onReady) {
                onReady(p);
            }
        });
    }).detach();
}
