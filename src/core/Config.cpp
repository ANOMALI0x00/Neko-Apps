#include "Config.h"
#include <glib.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

Config::Config() {
    // Defaults: XBPS is enabled, others disabled by default
    m_backends["xbps"] = true;
    m_backends["flatpak"] = false;
    m_backends["appimage"] = false;
    m_backends["source"] = false;
    m_darkMode = true; // Tokyo Night theme default

    load();
}

std::string Config::configPath() const {
    const char *cfgDir = g_get_user_config_dir();
    std::string dir = std::string(cfgDir) + "/neko-apps";
    std::string path = dir + "/config.ini";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return path;
}

bool Config::xbpsEnabled() const { return true; }
bool Config::flatpakEnabled() const { return backendEnabled("flatpak"); }
bool Config::appimageEnabled() const { return backendEnabled("appimage"); }
bool Config::sourceEnabled() const { return backendEnabled("source"); }

bool Config::backendEnabled(const std::string &key) const {
    if (key == "xbps") return true;
    auto it = m_backends.find(key);
    if (it != m_backends.end()) {
        return it->second;
    }
    return false;
}

void Config::setBackendEnabled(const std::string &key, bool enabled) {
    if (key == "xbps") return; // XBPS es el gestor por defecto del sistema y siempre está activo
    if (key == "source") m_sourceUserSet = true;
    auto it = m_backends.find(key);
    if (it != m_backends.end() && it->second == enabled) {
        save();
        return;
    }
    m_backends[key] = enabled;
    save();
}

bool Config::darkMode() const {
    return m_darkMode;
}

void Config::setDarkMode(bool dark) {
    if (m_darkMode == dark) return;
    m_darkMode = dark;
    save();
}

std::vector<std::string> Config::customSourceRepos() const {
    return m_customSourceRepos;
}

void Config::setCustomSourceRepos(const std::vector<std::string> &repos) {
    std::vector<std::string> uniqueRepos;
    for (std::string r : repos) {
        while (!r.empty() && (r.back() == ' ' || r.back() == '\t' || r.back() == '\r' || r.back() == '/')) r.pop_back();
        while (!r.empty() && (r.front() == ' ' || r.front() == '\t')) r.erase(0, 1);
        if (!r.empty() && std::find(uniqueRepos.begin(), uniqueRepos.end(), r) == uniqueRepos.end()) {
            uniqueRepos.push_back(r);
        }
    }
    if (!uniqueRepos.empty()) {
        m_backends["source"] = true;
        m_sourceUserSet = true;
    }
    m_customSourceRepos = std::move(uniqueRepos);

    // Prune any removed repositories from ~/.config/vouru/template-sources.tsv and srcpkgs
    std::string tsvPath = vouruSourcesFile();
    std::ifstream tsvIn(tsvPath);
    if (tsvIn.is_open()) {
        std::vector<std::string> keptLines;
        std::string line;
        std::error_code ec;
        while (std::getline(tsvIn, line)) {
            if (line.empty()) continue;
            auto t1 = line.find('\t');
            auto t2 = (t1 != std::string::npos) ? line.find('\t', t1 + 1) : std::string::npos;
            if (t1 == std::string::npos || t2 == std::string::npos) continue;
            std::string rDir = line.substr(0, t1);
            std::string pName = line.substr(t1 + 1, t2 - (t1 + 1));
            std::string rUrl = line.substr(t2 + 1);
            while (!rUrl.empty() && (rUrl.back() == ' ' || rUrl.back() == '\t' || rUrl.back() == '\r' || rUrl.back() == '/')) rUrl.pop_back();
            if (std::find(m_customSourceRepos.begin(), m_customSourceRepos.end(), rUrl) != m_customSourceRepos.end()) {
                keptLines.push_back(rDir + "\t" + pName + "\t" + rUrl);
            } else if (!rDir.empty() && !pName.empty()) {
                std::filesystem::remove_all(std::filesystem::path(rDir) / "srcpkgs" / pName, ec);
            }
        }
        tsvIn.close();
        std::ofstream tsvOut(tsvPath, std::ios::trunc);
        if (tsvOut.is_open()) {
            for (const auto &kl : keptLines) tsvOut << kl << "\n";
        }
    }

    save();
}

void Config::addCustomSourceRepo(const std::string &repo) {
    if (repo.empty()) return;
    auto list = m_customSourceRepos;
    list.push_back(repo);
    setCustomSourceRepos(list);
}

void Config::removeCustomSourceRepo(const std::string &repo) {
    auto list = m_customSourceRepos;
    list.erase(std::remove(list.begin(), list.end(), repo), list.end());
    setCustomSourceRepos(list);
}

std::string Config::voidPackagesDir() const {
    const char *home = g_get_home_dir();
    std::string homeStr = (home && *home) ? std::string(home) : "/tmp";
    std::string defaultDir = homeStr + "/.local/share/pkgs/void-packages";

    // Check ~/.voururc for custom VURU_DIR just like vouru
    std::ifstream vrc(homeStr + "/.voururc");
    if (vrc.is_open()) {
        std::string line;
        while (std::getline(vrc, line)) {
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(0, 1);
            if (line.rfind("VURU_DIR=", 0) == 0) {
                std::string val = line.substr(9);
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r' || val.back() == '"' || val.back() == '\'')) val.pop_back();
                while (!val.empty() && (val.front() == ' ' || val.front() == '\t' || val.front() == '"' || val.front() == '\'')) val.erase(0, 1);
                if (val.rfind("~/", 0) == 0) {
                    val = homeStr + val.substr(1);
                } else if (val.rfind("$HOME/", 0) == 0) {
                    val = homeStr + val.substr(5);
                }
                if (!val.empty()) {
                    return val;
                }
            }
        }
    }
    return defaultDir;
}

std::string Config::vouruSourcesFile() const {
    const char *home = g_get_home_dir();
    std::string homeStr = (home && *home) ? std::string(home) : "/tmp";
    return homeStr + "/.config/vouru/template-sources.tsv";
}

void Config::load() {
    std::string path = configPath();
    std::ifstream file(path);

    m_customSourceRepos.clear();
    m_sourceUserSet = false;
    auto pushUniqueRepo = [this](std::string r) {
        while (!r.empty() && (r.back() == ' ' || r.back() == '\t' || r.back() == '\r' || r.back() == '/')) r.pop_back();
        while (!r.empty() && (r.front() == ' ' || r.front() == '\t')) r.erase(0, 1);
        if (!r.empty() && std::find(m_customSourceRepos.begin(), m_customSourceRepos.end(), r) == m_customSourceRepos.end()) {
            m_customSourceRepos.push_back(r);
        }
    };

    if (file.is_open()) {
        std::string line;
        std::string currentSection;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            // Section header
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            // Trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
            while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(0, 1);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);

            if (key == "backend.xbps") m_backends["xbps"] = (val == "true" || val == "1");
            else if (key == "backend.flatpak") m_backends["flatpak"] = (val == "true" || val == "1");
            else if (key == "backend.appimage") m_backends["appimage"] = (val == "true" || val == "1");
            else if (key == "backend.source") m_backends["source"] = (val == "true" || val == "1");
            else if (key == "backend.source.user_set") m_sourceUserSet = (val == "true" || val == "1");
            else if (key == "theme.dark") m_darkMode = (val == "true" || val == "1");
            else if (key == "source.repos" || key == "repos") {
                // Semicolon-separated URLs
                std::stringstream ss(val);
                std::string item;
                while (std::getline(ss, item, ';')) {
                    pushUniqueRepo(item);
                }
            } else if (key == "repo" || key.find("repo.") == 0 || (currentSection == "SourceRepos" && key.find("repo") != std::string::npos)) {
                pushUniqueRepo(val);
            }
        }
    }

    // Also import any repositories previously registered in vouru's ~/.config/vouru/template-sources.tsv
    std::ifstream vtsv(vouruSourcesFile());
    if (vtsv.is_open()) {
        std::string line;
        while (std::getline(vtsv, line)) {
            if (line.empty()) continue;
            auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            auto t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            std::string url = line.substr(t2 + 1);
            pushUniqueRepo(url);
        }
    }

    if (!m_customSourceRepos.empty() && !m_sourceUserSet) {
        m_backends["source"] = true;
    }
}

void Config::save() {
    std::string path = configPath();
    std::ofstream file(path);
    if (!file.is_open()) return;

    file << "[Backends]\n";
    file << "backend.xbps=" << (m_backends["xbps"] ? "true" : "false") << "\n";
    file << "backend.flatpak=" << (m_backends["flatpak"] ? "true" : "false") << "\n";
    file << "backend.appimage=" << (m_backends["appimage"] ? "true" : "false") << "\n";
    file << "backend.source=" << (m_backends["source"] ? "true" : "false") << "\n";
    file << "backend.source.user_set=" << (m_sourceUserSet ? "true" : "false") << "\n\n";

    file << "[Theme]\n";
    file << "theme.dark=" << (m_darkMode ? "true" : "false") << "\n\n";

    file << "[SourceRepos]\n";
    for (const auto &repo : m_customSourceRepos) {
        file << "repo=" << repo << "\n";
    }
}
