#pragma once

#include <gtk/gtk.h>
#include <functional>

class SettingsView {
public:
    static GtkWidget* create(std::function<void()> onBackendsChanged);
};
