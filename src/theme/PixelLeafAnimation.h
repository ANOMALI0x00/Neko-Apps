#pragma once

#include <gtk/gtk.h>
#include <functional>

enum class PixelAnimMode {
    Install,      // Códigos ensamblando la hoja de papel
    Uninstall,    // Hoja de papel desintegrándose en códigos
    Error         // Hoja de papel explotando en fragmentos y códigos
};

class PixelLeafAnimation {
public:
    static GtkWidget* create(int size = 96);
    static void start(GtkWidget *widget, PixelAnimMode mode = PixelAnimMode::Install, std::function<void()> onFinished = nullptr);
    static void stop(GtkWidget *widget);
    static void triggerExplosion(GtkWidget *widget, std::function<void()> onFinished = nullptr);
};
