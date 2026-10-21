#pragma once

#include <gtk/gtk.h>
#include "Package.h"
#include <functional>
#include <vector>

class ManageView {
public:
    static GtkWidget* create(std::function<void(const Package&)> onOpenDetails,
                             std::function<void(const Package&)> onUninstall);

    static void setInstalledPackages(GtkWidget *manageWidget, const PackageList &pkgs);
};
