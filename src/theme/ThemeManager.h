#pragma once

#include <gtk/gtk.h>
#include <string>

struct ThemePalette {
    std::string terminalError = "#c27979";
    std::string terminalInfo  = "#8da383";
    std::string terminalDim   = "#7a847d";
};

class ThemeManager {
public:
    static void init();
    static void applyTheme(bool dark);
    static void loadThemeByName(const std::string &themeName, bool dark = true);
    static const ThemePalette& palette();
    static std::string resolveThemeDir(const std::string &themeName);

private:
    static void loadPaletteFile(const std::string &palettePath);

    static GtkCssProvider *s_baseTheme;
    static GtkCssProvider *s_customTheme;
    static bool s_isDark;
    static std::string s_themeName;
    static ThemePalette s_palette;
};
