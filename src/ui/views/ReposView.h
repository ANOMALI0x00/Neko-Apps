#pragma once

#include <gtk/gtk.h>
#include <functional>

class ReposView {
public:
    static GtkWidget* create(std::function<void()> onRepoChanged = nullptr);
};
