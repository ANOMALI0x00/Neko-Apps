#pragma once

#include <gtk/gtk.h>
#include "Package.h"
#include "Transaction.h"
#include <functional>
#include <vector>

class UpdatesView {
public:
    static GtkWidget* create(std::function<void(const PackageList&)> onUpdateAll,
                             std::function<void(const Package&)> onUpdateSingle,
                             std::function<void()> onCheckUpdates = nullptr);

    static void setUpdates(GtkWidget *viewWidget, const PackageList &pkgs);
    static void setChecking(GtkWidget *viewWidget, bool checking);
    static void resetActionState(GtkWidget *viewWidget);
};
