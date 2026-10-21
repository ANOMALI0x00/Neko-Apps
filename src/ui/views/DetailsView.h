#pragma once

#include <gtk/gtk.h>
#include "Package.h"
#include "Transaction.h"
#include <functional>
#include <vector>

class DetailsView {
public:
    static GtkWidget* create(std::function<void()> onBack,
                             std::function<void(const Package&)> onAction);

    static void setPackage(GtkWidget *viewWidget, const Package &pkg, const std::vector<Package> &variants);
    static void updatePackageDetails(GtkWidget *viewWidget, const Package &pkg);
    static void updatePackageState(GtkWidget *viewWidget, const std::string &pkgId, PkgSource source, bool installed);
    static void resetActionState(GtkWidget *viewWidget);
    static void setActionError(GtkWidget *viewWidget);
};

