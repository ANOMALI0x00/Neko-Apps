#include "ThemeManager.h"
#include "Config.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

GtkCssProvider *ThemeManager::s_baseTheme = nullptr;
GtkCssProvider *ThemeManager::s_customTheme = nullptr;
bool ThemeManager::s_isDark = true;
std::string ThemeManager::s_themeName = "Matecito-Dark";
ThemePalette ThemeManager::s_palette;

void ThemeManager::init() {
    s_isDark = true;
    s_baseTheme = gtk_css_provider_new();
    s_customTheme = gtk_css_provider_new();

    // Forzar modo oscuro en GtkSettings y bloquear eventos externos de cambio de tema
    // (ej. cuando el usuario cambia de tema en mangowc, xsettingsd o gsettings)
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

        static bool s_inThemeNotify = false;
        g_signal_connect_after(settings, "notify::gtk-application-prefer-dark-theme",
            G_CALLBACK(+[](GtkSettings *s, GParamSpec *, gpointer) {
                if (s_inThemeNotify) return;
                gboolean pref = FALSE;
                g_object_get(s, "gtk-application-prefer-dark-theme", &pref, NULL);
                if (!pref) {
                    s_inThemeNotify = true;
                    g_object_set(s, "gtk-application-prefer-dark-theme", TRUE, NULL);
                    s_inThemeNotify = false;
                }
            }), nullptr);

        static guint s_themeIdleId = 0;
        auto onExternalThemeChanged = +[](GtkSettings *s, GParamSpec *, gpointer) {
            if (s_inThemeNotify) return;
            s_inThemeNotify = true;
            g_object_set(s, "gtk-application-prefer-dark-theme", TRUE, NULL);
            s_inThemeNotify = false;

            if (s_themeIdleId == 0) {
                s_themeIdleId = g_idle_add(+[](gpointer) -> gboolean {
                    s_themeIdleId = 0;
                    ThemeManager::applyTheme(true);
                    return G_SOURCE_REMOVE;
                }, nullptr);
            }
        };

        g_signal_connect_after(settings, "notify::gtk-theme-name", G_CALLBACK(onExternalThemeChanged), nullptr);
        g_signal_connect_after(settings, "notify::gtk-color-scheme", G_CALLBACK(onExternalThemeChanged), nullptr);
    }

    GdkScreen *screen = gdk_screen_get_default();
    // Prioridad USER + 10 para el tema base (Matecito-Dark) y USER + 20 para neko-apps.css.
    // Esto garantiza precedencia absoluta sobre ~/.config/gtk-3.0/gtk.css (prioridad 800)
    // y sobre cualquier cambio de temas inyectado en tiempo de ejecución por mangowc u otros entornos.
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(s_baseTheme),
        GTK_STYLE_PROVIDER_PRIORITY_USER + 10
    );
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(s_customTheme),
        GTK_STYLE_PROVIDER_PRIORITY_USER + 20
    );

    applyTheme(true);
}

std::string ThemeManager::resolveThemeDir(const std::string &themeName) {
    std::vector<std::string> candidates = {
        "data/theme/" + themeName,
        "../data/theme/" + themeName,
        "/usr/share/neko-apps/theme/" + themeName,
        "/usr/local/share/neko-apps/theme/" + themeName
    };

    std::error_code ecProc;
    auto exePath = std::filesystem::read_symlink("/proc/self/exe", ecProc);
    if (!ecProc && !exePath.empty()) {
        auto exeDir = exePath.parent_path();
        candidates.insert(candidates.begin(), (exeDir / "data/theme" / themeName).string());
        candidates.insert(candidates.begin() + 1, (exeDir / "../data/theme" / themeName).string());
        candidates.insert(candidates.begin() + 2, (exeDir / "../share/neko-apps/theme" / themeName).string());
    }

    for (const auto &c : candidates) {
        if (std::filesystem::exists(c)) {
            std::error_code ec;
            auto canon = std::filesystem::canonical(c, ec);
            return ec ? c : canon.string();
        }
    }
    return "/usr/share/neko-apps/theme/" + themeName;
}

void ThemeManager::loadPaletteFile(const std::string &palettePath) {
    s_palette = ThemePalette{};
    std::ifstream in(palettePath);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(0, 1);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);

        if (key == "terminal.error" && !val.empty()) s_palette.terminalError = val;
        else if (key == "terminal.info" && !val.empty()) s_palette.terminalInfo = val;
        else if (key == "terminal.dim" && !val.empty()) s_palette.terminalDim = val;
    }
}

const ThemePalette& ThemeManager::palette() {
    return s_palette;
}

void ThemeManager::applyTheme(bool dark) {
    (void)dark;
    loadThemeByName(s_themeName, true);
}

void ThemeManager::loadThemeByName(const std::string &themeName, bool dark) {
    (void)dark;
    s_themeName = themeName.empty() ? "Matecito-Dark" : themeName;
    s_isDark = true;
    Config::instance().setDarkMode(true);

    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
    }

    std::string themeDir = resolveThemeDir(s_themeName);

    // 1. Load Semantic Palette from theme folder
    loadPaletteFile(themeDir + "/palette.conf");

    // 2. Load Base GTK3 CSS from theme folder (forzado a gtk-dark.css si existe)
    std::string gtkCssFile = themeDir + "/gtk-3.0/gtk-dark.css";
    if (!std::filesystem::exists(gtkCssFile)) {
        gtkCssFile = themeDir + "/gtk-3.0/gtk.css";
    }

    GError *error = nullptr;
    if (std::filesystem::exists(gtkCssFile)) {
        gtk_css_provider_load_from_path(s_baseTheme, gtkCssFile.c_str(), &error);
        if (error) {
            g_error_free(error);
            error = nullptr;
        }
    }

    // 3. Load Custom App Stylesheet (neko-apps.css) from theme folder
    std::string appCssPath = themeDir + "/neko-apps.css";
    std::string pixelDir = themeDir + "/assets/pixel";

    std::ifstream cssIn(appCssPath);
    if (cssIn.is_open()) {
        std::ostringstream ss;
        ss << cssIn.rdbuf();
        std::string cssStr = ss.str();

        size_t pos = 0;
        while ((pos = cssStr.find("{PIXEL_DIR}", pos)) != std::string::npos) {
            cssStr.replace(pos, 11, pixelDir);
            pos += pixelDir.length();
        }

        gtk_css_provider_load_from_data(s_customTheme, cssStr.c_str(), -1, &error);
        if (error) {
            g_printerr("ThemeManager CSS error (%s): %s\n", appCssPath.c_str(), error->message);
            g_error_free(error);
            error = nullptr;
        }
    }
}
