#include "FlatpakBackend.h"
#include "Proc.h"
#include <glib.h>
#include <thread>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <algorithm>

namespace {

static std::string decodeXmlEntities(std::string text) {
    const std::vector<std::pair<std::string, std::string>> entities = {
        {"&amp;", "&"},
        {"&quot;", "\""},
        {"&apos;", "'"},
        {"&#39;", "'"},
        {"&lt;", "<"},
        {"&gt;", ">"}
    };
    for (const auto &e : entities) {
        size_t pos = 0;
        while ((pos = text.find(e.first, pos)) != std::string::npos) {
            text.replace(pos, e.first.length(), e.second);
            pos += e.second.length();
        }
    }
    return text;
}

static std::string stripTags(std::string_view html) {
    std::string text;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            text.push_back(c);
        }
    }
    std::string trimmed;
    bool prevSpace = false;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (!prevSpace && !trimmed.empty()) {
                trimmed.push_back(' ');
                prevSpace = true;
            }
        } else {
            trimmed.push_back(c);
            prevSpace = false;
        }
    }
    return decodeXmlEntities(trimmed);
}

static std::vector<std::string> mapCategories(std::string_view comp) {
    std::vector<std::string> cats;
    size_t catPos = comp.find("<categories>");
    if (catPos != std::string_view::npos) {
        size_t endCatPos = comp.find("</categories>", catPos);
        if (endCatPos != std::string_view::npos) {
            std::string_view catBlock = comp.substr(catPos, endCatPos - catPos);
            size_t cPos = 0;
            while ((cPos = catBlock.find("<category>", cPos)) != std::string_view::npos) {
                cPos += 10;
                size_t cEnd = catBlock.find("</category>", cPos);
                if (cEnd == std::string_view::npos) break;
                std::string c(catBlock.substr(cPos, cEnd - cPos));
                if (c == "Development") cats.push_back("Desarrollo");
                else if (c == "AudioVideo" || c == "Audio" || c == "Video") cats.push_back("Multimedia");
                else if (c == "Graphics") cats.push_back("Gráficos");
                else if (c == "Network" || c == "WebBrowser") cats.push_back("Internet");
                else if (c == "Game") cats.push_back("Juegos");
                else if (c == "Office") cats.push_back("Oficina");
                else if (c == "Education" || c == "Science") cats.push_back("Educación");
                else if (c == "Utility") cats.push_back("Utilidades");
                else if (c == "System" || c == "Settings") cats.push_back("Sistema");
                cPos = cEnd + 11;
            }
        }
    }
    if (cats.empty()) {
        cats.push_back("Utilidades");
    }
    return cats;
}

static std::string trimStr(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

static std::string cleanFlatpakId(std::string id) {
    id = trimStr(id);
    if (id.size() > 8 && id.substr(id.size() - 8) == ".desktop") {
        id = id.substr(0, id.size() - 8);
    }
    return id;
}

static std::string cleanSpaces(const std::string &s) {
    std::string res;
    for (size_t i = 0; i < s.size(); ++i) {
        if (static_cast<unsigned char>(s[i]) == 0xc2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xa0) {
            res.push_back(' ');
            i++;
        } else {
            res.push_back(s[i]);
        }
    }
    return trimStr(res);
}

static std::unordered_map<std::string, FlatMeta> fetchRemoteMeta() {
    std::unordered_map<std::string, FlatMeta> metaMap;
    // 1. Try --cached across all remotes
    std::string metaOut = Proc::capture("flatpak", {"remote-ls", "--cached", "--app", "--columns=application,version,download-size,installed-size"});
    // 2. Try explicitly with flathub
    if (metaOut.empty()) {
        metaOut = Proc::capture("flatpak", {"remote-ls", "flathub", "--cached", "--app", "--columns=application,version,download-size,installed-size"});
    }
    // 3. Try with --system
    if (metaOut.empty()) {
        metaOut = Proc::capture("flatpak", {"remote-ls", "--system", "--cached", "--app", "--columns=application,version,download-size,installed-size"});
    }
    // 4. Try without --cached (live summary)
    if (metaOut.empty()) {
        metaOut = Proc::capture("flatpak", {"remote-ls", "--app", "--columns=application,version,download-size,installed-size"});
    }

    std::istringstream metaIss(metaOut);
    std::string line;
    while (std::getline(metaIss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string appid, ver, dl, inst;
        if (std::getline(ls, appid, '\t')) {
            std::getline(ls, ver, '\t');
            std::getline(ls, dl, '\t');
            std::getline(ls, inst, '\t');
            appid = cleanFlatpakId(appid);
            if (!appid.empty()) {
                std::string cdl = cleanSpaces(dl);
                std::string cinst = cleanSpaces(inst);
                metaMap[appid] = {trimStr(ver), cdl, cinst};
            }
        }
    }
    return metaMap;
}
}

bool FlatpakBackend::isAvailable() const {
    return Proc::exists("flatpak");
}

void FlatpakBackend::loadCatalog(std::function<void(PkgSource, const PackageList&)> onReady) {
    if (!isAvailable()) {
        if (onReady) onReady(PkgSource::Flatpak, {});
        return;
    }

    std::thread([this, onReady]() {
        PackageList list;

        // 1. Check installed Flatpaks
        std::unordered_map<std::string, std::string> installedApps;
        std::string instOut = Proc::capture("flatpak", {"list", "--app", "--columns=application,version"});
        std::istringstream instIss(instOut);
        std::string line;
        while (std::getline(instIss, line)) {
            if (line.empty()) continue;
            std::istringstream ls(line);
            std::string appid, ver;
            if (ls >> appid) {
                appid = cleanFlatpakId(appid);
                ls >> ver;
                installedApps[appid] = ver;
            }
        }

        // 2. Fetch cached remote metadata (download size, installed size, versions)
        auto remoteMeta = fetchRemoteMeta();
        {
            std::lock_guard<std::mutex> lock(m_metaMutex);
            m_remoteMeta = remoteMeta;
        }

        // 3. Discover AppStream XML paths
        std::string xmlPath;
        std::string iconsDir;

        const std::string sysXml = "/var/lib/flatpak/appstream/flathub/x86_64/active/appstream.xml";
        const std::string sysIcons = "/var/lib/flatpak/appstream/flathub/x86_64/active/icons/64x64/";
        const std::string sysIcons128 = "/var/lib/flatpak/appstream/flathub/x86_64/active/icons/128x128/";

        const char *userDir = g_get_user_data_dir();
        std::string usrXml = userDir ? (std::string(userDir) + "/flatpak/appstream/flathub/x86_64/active/appstream.xml") : "";
        std::string usrIcons = userDir ? (std::string(userDir) + "/flatpak/appstream/flathub/x86_64/active/icons/64x64/") : "";

        if (std::filesystem::exists(sysXml)) {
            xmlPath = sysXml;
            iconsDir = sysIcons;
        } else if (!usrXml.empty() && std::filesystem::exists(usrXml)) {
            xmlPath = usrXml;
            iconsDir = usrIcons;
        }

        bool parsedXml = false;
        if (!xmlPath.empty()) {
            std::ifstream file(xmlPath, std::ios::binary);
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::string_view sv(content);

                size_t pos = 0;
                while ((pos = sv.find("<component", pos)) != std::string_view::npos) {
                    size_t endComp = sv.find("</component>", pos);
                    if (endComp == std::string_view::npos) break;

                    std::string_view comp = sv.substr(pos, endComp - pos);
                    size_t tagEnd = comp.find('>');
                    std::string_view compHeader = comp.substr(0, tagEnd + 1);

                    if (compHeader.find("type=\"desktop-application\"") != std::string_view::npos ||
                        compHeader.find("type=\"desktop\"") != std::string_view::npos) {

                        auto getTag = [&](std::string_view tag) -> std::string {
                            std::string openTag = "<" + std::string(tag) + ">";
                            std::string closeTag = "</" + std::string(tag) + ">";
                            size_t start = comp.find(openTag);
                            if (start != std::string_view::npos) {
                                start += openTag.size();
                                size_t end = comp.find(closeTag, start);
                                if (end != std::string_view::npos) {
                                    return std::string(comp.substr(start, end - start));
                                }
                            }
                            return "";
                        };

                        Package p;
                        p.source = PkgSource::Flatpak;
                        p.id = getTag("id");
                        if (p.id.size() > 8 && p.id.substr(p.id.size() - 8) == ".desktop") {
                            p.id = p.id.substr(0, p.id.size() - 8);
                        }
                        p.name = decodeXmlEntities(getTag("name"));
                        p.summary = decodeXmlEntities(getTag("summary"));
                        p.license = getTag("project_license");
                        p.repository = "flathub";

                        std::string devName = getTag("developer_name");
                        if (!devName.empty()) {
                            p.maintainer = decodeXmlEntities(devName);
                        } else {
                            p.maintainer = "Flathub";
                        }

                        // Homepage
                        size_t hpPos = comp.find("<url type=\"homepage\">");
                        if (hpPos != std::string_view::npos) {
                            hpPos += 21;
                            size_t hpEnd = comp.find("</url>", hpPos);
                            if (hpEnd != std::string_view::npos) {
                                p.homepage = decodeXmlEntities(std::string(comp.substr(hpPos, hpEnd - hpPos)));
                            }
                        }

                        // Version from latest release tag (handles any attribute order)
                        size_t relPos = comp.find("<release ");
                        if (relPos != std::string_view::npos) {
                            size_t rTagEnd = comp.find('>', relPos);
                            if (rTagEnd != std::string_view::npos) {
                                std::string_view relTag = comp.substr(relPos, rTagEnd - relPos);
                                size_t verAttr = relTag.find("version=\"");
                                if (verAttr != std::string_view::npos) {
                                    verAttr += 9;
                                    size_t verEnd = relTag.find('"', verAttr);
                                    if (verEnd != std::string_view::npos) {
                                        p.version = std::string(relTag.substr(verAttr, verEnd - verAttr));
                                    }
                                }
                            }
                        }

                        // Enrich from remote-ls metadata (sizes + version fallback)
                        auto mIt = remoteMeta.find(p.id);
                        if (mIt != remoteMeta.end()) {
                            if (p.version.empty() && !mIt->second.version.empty()) {
                                p.version = mIt->second.version;
                            }
                            if (!mIt->second.dlSize.empty()) {
                                p.downloadSize = mIt->second.dlSize;
                            }
                            if (!mIt->second.instSize.empty()) {
                                p.installedSize = mIt->second.instSize;
                            }
                        }

                        // Description
                        size_t descPos = comp.find("<description>");
                        if (descPos != std::string_view::npos) {
                            descPos += 13;
                            size_t descEnd = comp.find("</description>", descPos);
                            if (descEnd != std::string_view::npos) {
                                p.description = stripTags(comp.substr(descPos, descEnd - descPos));
                            }
                        }
                        if (p.description.empty()) {
                            p.description = p.summary;
                        }

                        // Categories
                        p.categories = mapCategories(comp);

                        // Icon resolution
                        size_t iconPos = comp.find("<icon ");
                        std::string iconFile;
                        if (iconPos != std::string_view::npos) {
                            size_t iconTagEnd = comp.find('>', iconPos);
                            size_t iconEnd = comp.find("</icon>", iconTagEnd);
                            if (iconTagEnd != std::string_view::npos && iconEnd != std::string_view::npos) {
                                iconFile = std::string(comp.substr(iconTagEnd + 1, iconEnd - iconTagEnd - 1));
                            }
                        }

                        if (!iconFile.empty()) {
                            std::string candidate64 = iconsDir + iconFile;
                            std::string candidate128 = sysIcons128 + iconFile;
                            if (std::filesystem::exists(candidate64)) {
                                p.iconPath = candidate64;
                            } else if (std::filesystem::exists(candidate128)) {
                                p.iconPath = candidate128;
                            } else {
                                p.iconPath = p.id;
                            }
                        } else {
                            p.iconPath = p.id;
                        }

                        // Installed check
                        auto instIt = installedApps.find(p.id);
                        if (instIt != installedApps.end()) {
                            p.state = PkgState::Installed;
                            p.installedVersion = instIt->second;
                            if (p.version.empty()) p.version = instIt->second;
                        } else {
                            p.state = PkgState::NotInstalled;
                        }

                        p.detailsLoaded = true;

                        if (!p.id.empty()) {
                            list.push_back(std::move(p));
                        }
                    }
                    pos = endComp + 12;
                }
                parsedXml = true;
            }
        }

        // 4. Fallback: query flatpak remote-ls if XML unavailable
        if (!parsedXml || list.empty()) {
            std::string out = Proc::capture("flatpak", {"remote-ls", "flathub", "--cached", "--app", "--columns=application,name,version,description"});
            std::istringstream iss(out);
            while (std::getline(iss, line)) {
                if (line.empty()) continue;
                std::istringstream ls(line);
                std::string appid, name, ver, desc;
                if (std::getline(ls, appid, '\t') && std::getline(ls, name, '\t')) {
                    std::getline(ls, ver, '\t');
                    std::getline(ls, desc, '\t');
                    Package p;
                    p.source = PkgSource::Flatpak;
                    p.id = cleanFlatpakId(appid);
                    p.name = name.empty() ? p.id : name;
                    p.version = ver;
                    p.summary = desc.empty() ? ("Aplicación Flathub " + p.name) : desc;
                    p.description = p.summary;
                    p.iconPath = p.id;
                    p.categories = {"Utilidades"};
                    p.repository = "flathub";
                    if (installedApps.find(p.id) != installedApps.end()) {
                        p.state = PkgState::Installed;
                        p.installedVersion = installedApps[p.id];
                    }
                    auto mIt = remoteMeta.find(p.id);
                    if (mIt != remoteMeta.end()) {
                        if (p.version.empty()) p.version = mIt->second.version;
                        p.downloadSize = mIt->second.dlSize;
                        p.installedSize = mIt->second.instSize;
                    }
                    list.push_back(p);
                }
            }
        }

        Proc::runOnMainThread([onReady, list]() {
            if (onReady) onReady(PkgSource::Flatpak, list);
        });
    }).detach();
}

void FlatpakBackend::checkUpdates(std::function<void(PkgSource, const PackageList&)> onReady) {
    if (!isAvailable()) {
        if (onReady) onReady(PkgSource::Flatpak, {});
        return;
    }

    std::thread([this, onReady]() {
        std::unordered_map<std::string, std::string> installedVers;
        std::string instOut = Proc::capture("flatpak", {"list", "--app", "--columns=application,version"});
        std::istringstream instIss(instOut);
        std::string line;
        while (std::getline(instIss, line)) {
            if (line.empty()) continue;
            std::istringstream ls(line);
            std::string appid, ver;
            if (ls >> appid) {
                ls >> ver;
                installedVers[cleanFlatpakId(appid)] = ver;
            }
        }

        PackageList updates;
        std::string out = Proc::capture("flatpak", {"remote-ls", "--cached", "--updates", "--columns=application,name,version,download-size"});
        if (out.empty() || out.find("error:") != std::string::npos) {
            out = Proc::capture("flatpak", {"remote-ls", "--updates", "--columns=application,name,version,download-size"});
        }
        std::istringstream iss(out);
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            std::istringstream ls(line);
            std::string appid, name, ver, dl;
            if (std::getline(ls, appid, '\t')) {
                std::getline(ls, name, '\t');
                std::getline(ls, ver, '\t');
                std::getline(ls, dl, '\t');
                Package p;
                p.source = PkgSource::Flatpak;
                p.id = cleanFlatpakId(appid);
                p.name = name.empty() ? p.id : name;
                p.newVersion = ver;
                auto it = installedVers.find(p.id);
                if (it != installedVers.end()) {
                    p.installedVersion = it->second;
                    p.version = it->second;
                }
                p.downloadSize = cleanSpaces(dl);
                p.iconPath = p.id;
                p.state = PkgState::Updatable;
                updates.push_back(std::move(p));
            }
        }

        Proc::runOnMainThread([onReady, updates = std::move(updates)]() {
            if (onReady) onReady(PkgSource::Flatpak, updates);
        });
    }).detach();
}

void FlatpakBackend::getDetails(const std::string &pkgId, std::function<void(const Package&)> onReady) {
    std::string cleanId = cleanFlatpakId(pkgId);

    std::thread([this, cleanId, onReady]() {
        Package p;
        p.source = PkgSource::Flatpak;
        p.id = cleanId;
        p.name = cleanId;

        // 1. Check in-memory metadata cache first (instant response)
        {
            std::lock_guard<std::mutex> lock(m_metaMutex);
            auto it = m_remoteMeta.find(cleanId);
            if (it != m_remoteMeta.end()) {
                p.version = it->second.version;
                p.downloadSize = it->second.dlSize;
                p.installedSize = it->second.instSize;
            }
        }

        // 2. Fallback: if downloadSize is still empty, query remote-ls
        if (p.downloadSize.empty()) {
            auto meta = fetchRemoteMeta();
            auto it = meta.find(cleanId);
            if (it != meta.end()) {
                p.version = it->second.version;
                p.downloadSize = it->second.dlSize;
                p.installedSize = it->second.instSize;
                std::lock_guard<std::mutex> lock(m_metaMutex);
                m_remoteMeta[cleanId] = it->second;
            }
        }

        // 3. Inspect installed details if installed
        std::string infoOut = Proc::capture("flatpak", {"info", cleanId});
        if (!infoOut.empty()) {
            p.state = PkgState::Installed;
            std::istringstream iss(infoOut);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string k = line.substr(0, colon);
                std::string v = line.substr(colon + 1);
                v = trimStr(v);
                if ((k.find("Version") != std::string::npos || k.find("Versi") != std::string::npos) && p.version.empty()) {
                    p.version = v;
                    p.installedVersion = v;
                }
                else if (k.find("Installed") != std::string::npos && p.installedSize.empty()) p.installedSize = v;
                else if (k.find("License") != std::string::npos || k.find("Licencia") != std::string::npos) p.license = v;
            }
        }
        p.detailsLoaded = true;

        Proc::runOnMainThread([onReady, p]() {
            if (onReady) onReady(p);
        });
    }).detach();
}
