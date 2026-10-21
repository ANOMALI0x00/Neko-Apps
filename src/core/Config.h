#pragma once

#include <string>
#include <map>
#include <vector>

class Config {
public:
    static Config& instance();

    bool xbpsEnabled() const;
    bool flatpakEnabled() const;
    bool appimageEnabled() const;
    bool sourceEnabled() const;

    bool backendEnabled(const std::string &key) const;
    void setBackendEnabled(const std::string &key, bool enabled);

    bool darkMode() const;
    void setDarkMode(bool dark);

    std::vector<std::string> customSourceRepos() const;
    void setCustomSourceRepos(const std::vector<std::string> &repos);
    void addCustomSourceRepo(const std::string &repo);
    void removeCustomSourceRepo(const std::string &repo);

    std::string voidPackagesDir() const;
    std::string vouruSourcesFile() const;

    void save();
    void load();

private:
    Config();
    std::string configPath() const;

    std::map<std::string, bool> m_backends;
    std::vector<std::string> m_customSourceRepos;
    bool m_darkMode = true;
    bool m_sourceUserSet = false;
};
