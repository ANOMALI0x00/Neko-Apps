#pragma once

#include <gtk/gtk.h>
#include "Package.h"
#include <functional>

class NekoAppCard {
public:
    static GtkWidget* create(const Package &pkg,
                             std::function<void(const Package&)> onClicked,
                             std::function<void(const Package&)> onAction);
};
