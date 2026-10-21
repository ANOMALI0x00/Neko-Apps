#include "UpdatesView.h"
#include "Transaction.h"
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <cstdint>
#include <cctype>

namespace {

static uint64_t parseSizeToBytes(const std::string &sizeStr) {
    if (sizeStr.empty() || sizeStr == "N/D") return 0;
    std::string s = sizeStr;
    for (auto &c : s) {
        if (c == ',') c = '.';
    }
    std::istringstream iss(s);
    double val = 0.0;
    std::string unit;
    if (!(iss >> val)) return 0;
    iss >> unit;
    for (auto &c : unit) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (unit.find("GB") != std::string::npos || unit.find("GIO") != std::string::npos || unit.find("G") == 0) {
        return static_cast<uint64_t>(val * 1024.0 * 1024.0 * 1024.0);
    }
    if (unit.find("MB") != std::string::npos || unit.find("MIO") != std::string::npos || unit.find("M") == 0) {
        return static_cast<uint64_t>(val * 1024.0 * 1024.0);
    }
    if (unit.find("KB") != std::string::npos || unit.find("KIO") != std::string::npos || unit.find("K") == 0) {
        return static_cast<uint64_t>(val * 1024.0);
    }
    return static_cast<uint64_t>(val);
}

static std::string formatBytes(uint64_t bytes) {
    if (bytes == 0) return "";
    char buf[64];
    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return std::string(buf);
}

} // namespace

struct UpdateRowItem {
    std::string pkgId;
    PkgSource source;
    GtkWidget *btn = nullptr;
    GtkWidget *spinner = nullptr;
    GtkWidget *label = nullptr;
};

struct UpdatesWidgets {
    GtkWidget *lblHeader = nullptr;
    GtkWidget *lblSub = nullptr;
    GtkWidget *btnCheckUpdates = nullptr;
    GtkWidget *spinnerCheck = nullptr;
    GtkWidget *lblCheck = nullptr;
    GtkWidget *btnUpdateAll = nullptr;
    GtkWidget *lblUpdateAll = nullptr;
    GtkWidget *spinnerAll = nullptr;
    GtkWidget *listContainer = nullptr;

    PackageList updates;
    std::vector<UpdateRowItem> rowItems;
    bool isChecking = false;
    std::function<void(const PackageList&)> onUpdateAll;
    std::function<void(const Package&)> onUpdateSingle;
    std::function<void()> onCheckUpdates;
};

static GtkWidget* createPackageIcon(const Package &pkg) {
    static std::unordered_map<std::string, bool> s_iconCache;
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    auto hasIcon = [theme](const std::string &name) -> bool {
        if (name.empty()) return false;
        auto it = s_iconCache.find(name);
        if (it != s_iconCache.end()) return it->second;
        bool ex = gtk_icon_theme_has_icon(theme, name.c_str());
        s_iconCache[name] = ex;
        return ex;
    };

    GtkWidget *iconImage = nullptr;
    if (!pkg.iconPath.empty() && pkg.iconPath[0] == '/') {
        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(pkg.iconPath.c_str(), 42, 42, TRUE, nullptr);
        if (pix) {
            iconImage = gtk_image_new_from_pixbuf(pix);
            g_object_unref(pix);
        }
    }
    if (!iconImage) {
        if (hasIcon(pkg.iconPath)) {
            iconImage = gtk_image_new_from_icon_name(pkg.iconPath.c_str(), GTK_ICON_SIZE_DND);
        } else if (hasIcon(pkg.id)) {
            iconImage = gtk_image_new_from_icon_name(pkg.id.c_str(), GTK_ICON_SIZE_DND);
        } else {
            iconImage = gtk_image_new_from_icon_name("package-x-generic", GTK_ICON_SIZE_DND);
        }
    }
    gtk_widget_set_size_request(iconImage, 42, 42);
    return iconImage;
}

static void onUpdateAllClicked(GtkButton *btn, gpointer user_data) {
    if (Transaction::isBusy()) return;
    auto *uw = static_cast<UpdatesWidgets*>(user_data);
    if (uw && uw->onUpdateAll && !uw->updates.empty()) {
        gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
        if (uw->btnCheckUpdates) {
            gtk_widget_set_sensitive(uw->btnCheckUpdates, FALSE);
        }
        if (uw->spinnerAll) {
            gtk_widget_set_no_show_all(uw->spinnerAll, FALSE);
            gtk_widget_show(uw->spinnerAll);
            gtk_spinner_start(GTK_SPINNER(uw->spinnerAll));
        }
        if (uw->lblUpdateAll) {
            gtk_label_set_text(GTK_LABEL(uw->lblUpdateAll), "Actualizando...");
        }

        // Disable all individual app update buttons and show their updating status
        for (const auto &item : uw->rowItems) {
            if (item.btn) {
                gtk_widget_set_sensitive(item.btn, FALSE);
            }
            if (item.spinner) {
                gtk_widget_set_no_show_all(item.spinner, FALSE);
                gtk_widget_show(item.spinner);
                gtk_spinner_start(GTK_SPINNER(item.spinner));
            }
            if (item.label) {
                gtk_label_set_text(GTK_LABEL(item.label), "Actualizando...");
            }
        }

        uw->onUpdateAll(uw->updates);
    }
}

static void onUpdatesDestroy(gpointer data) {
    auto *uw = static_cast<UpdatesWidgets*>(data);
    delete uw;
}

GtkWidget* UpdatesView::create(std::function<void(const PackageList&)> onUpdateAll,
                              std::function<void(const Package&)> onUpdateSingle,
                              std::function<void()> onCheckUpdates) {
    auto *uw = new UpdatesWidgets();
    uw->onUpdateAll = onUpdateAll;
    uw->onUpdateSingle = onUpdateSingle;
    uw->onCheckUpdates = onCheckUpdates;

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 24);
    gtk_container_add(GTK_CONTAINER(scroll), rootBox);

    // Header Panel
    GtkWidget *headPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(headPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), headPanel, FALSE, FALSE, 0);

    GtkWidget *topRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(headPanel), topRow, FALSE, FALSE, 0);

    GtkWidget *vTitle = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(topRow), vTitle, TRUE, TRUE, 0);

    uw->lblHeader = gtk_label_new("Actualizaciones del Sistema");
    gtk_widget_set_halign(uw->lblHeader, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(uw->lblHeader), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(vTitle), uw->lblHeader, FALSE, FALSE, 0);

    uw->lblSub = gtk_label_new("Mantén seguros y al día los paquetes de tu sistema.");
    gtk_widget_set_halign(uw->lblSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(uw->lblSub), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(vTitle), uw->lblSub, FALSE, FALSE, 0);

    // Actions Box (Right side)
    GtkWidget *actBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_valign(actBox, GTK_ALIGN_CENTER);

    // Check updates button
    uw->btnCheckUpdates = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(uw->btnCheckUpdates), "neko-btn-secondary");

    GtkWidget *cBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    uw->spinnerCheck = gtk_spinner_new();
    gtk_widget_set_no_show_all(uw->spinnerCheck, TRUE);
    gtk_box_pack_start(GTK_BOX(cBox), uw->spinnerCheck, FALSE, FALSE, 0);

    GtkWidget *cIcon = gtk_image_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(cBox), cIcon, FALSE, FALSE, 0);

    uw->lblCheck = gtk_label_new("Comprobar");
    gtk_box_pack_start(GTK_BOX(cBox), uw->lblCheck, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(uw->btnCheckUpdates), cBox);
    g_signal_connect_swapped(uw->btnCheckUpdates, "clicked", G_CALLBACK(+[](UpdatesWidgets *w) {
        if (Transaction::isBusy()) return;
        if (w->onCheckUpdates) {
            if (w->spinnerCheck) {
                gtk_widget_set_no_show_all(w->spinnerCheck, FALSE);
                gtk_widget_show(w->spinnerCheck);
                gtk_spinner_start(GTK_SPINNER(w->spinnerCheck));
            }
            if (w->lblCheck) {
                gtk_label_set_text(GTK_LABEL(w->lblCheck), "Buscando...");
            }
            gtk_widget_set_sensitive(w->btnCheckUpdates, FALSE);
            w->onCheckUpdates();
        }
    }), uw);
    gtk_box_pack_start(GTK_BOX(actBox), uw->btnCheckUpdates, FALSE, FALSE, 0);

    // Update All button (hidden when 0 updates)
    uw->btnUpdateAll = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(uw->btnUpdateAll), "neko-btn-primary");
    gtk_widget_set_no_show_all(uw->btnUpdateAll, TRUE);

    GtkWidget *allBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    uw->spinnerAll = gtk_spinner_new();
    gtk_widget_set_no_show_all(uw->spinnerAll, TRUE);
    gtk_box_pack_start(GTK_BOX(allBox), uw->spinnerAll, FALSE, FALSE, 0);

    GtkWidget *allIcon = gtk_image_new_from_icon_name("software-update-available-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(allBox), allIcon, FALSE, FALSE, 0);

    uw->lblUpdateAll = gtk_label_new("Actualizar Todo");
    gtk_box_pack_start(GTK_BOX(allBox), uw->lblUpdateAll, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(uw->btnUpdateAll), allBox);
    g_signal_connect(uw->btnUpdateAll, "clicked", G_CALLBACK(onUpdateAllClicked), uw);
    gtk_box_pack_start(GTK_BOX(actBox), uw->btnUpdateAll, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(topRow), actBox, FALSE, FALSE, 0);

    // List container
    uw->listContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(rootBox), uw->listContainer, TRUE, TRUE, 0);

    g_object_set_data_full(G_OBJECT(scroll), "updates_widgets", uw, onUpdatesDestroy);
    return scroll;
}

void UpdatesView::setUpdates(GtkWidget *viewWidget, const PackageList &pkgs) {
    auto *uw = static_cast<UpdatesWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "updates_widgets"));
    if (!uw) return;

    uw->updates = pkgs;
    uw->isChecking = false;
    uw->rowItems.clear();

    // Reset check button state
    if (uw->spinnerCheck) {
        gtk_spinner_stop(GTK_SPINNER(uw->spinnerCheck));
        gtk_widget_hide(uw->spinnerCheck);
        gtk_widget_set_no_show_all(uw->spinnerCheck, TRUE);
    }
    if (uw->lblCheck) {
        gtk_label_set_text(GTK_LABEL(uw->lblCheck), "Comprobar");
    }
    if (uw->btnCheckUpdates) {
        gtk_widget_set_sensitive(uw->btnCheckUpdates, TRUE);
    }

    // Clear old rows
    GList *children = gtk_container_get_children(GTK_CONTAINER(uw->listContainer));
    for (GList *iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    if (pkgs.empty()) {
        gtk_label_set_text(GTK_LABEL(uw->lblHeader), "Actualizaciones del Sistema");
        if (uw->lblSub) {
            gtk_label_set_text(GTK_LABEL(uw->lblSub), "Tu sistema se encuentra protegido y sincronizado con los repositorios.");
        }

        // Hide "Actualizar Todo" button when 0 updates
        gtk_widget_hide(uw->btnUpdateAll);
        gtk_widget_set_no_show_all(uw->btnUpdateAll, TRUE);

        // Modern Hero Empty State Card
        GtkWidget *heroCard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_style_context_add_class(gtk_widget_get_style_context(heroCard), "neko-empty-card");
        gtk_widget_set_halign(heroCard, GTK_ALIGN_FILL);
        gtk_widget_set_valign(heroCard, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_top(heroCard, 24);
        gtk_widget_set_margin_bottom(heroCard, 24);

        // Glowing circle icon
        GtkWidget *circleWrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_halign(circleWrap, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(circleWrap), "neko-empty-icon-circle");

        GtkWidget *icon = gtk_image_new_from_icon_name("object-select-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
        gtk_box_pack_start(GTK_BOX(circleWrap), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(heroCard), circleWrap, FALSE, FALSE, 0);

        // Title
        GtkWidget *lblHeroTitle = gtk_label_new("¡Tu sistema está completamente al día!");
        gtk_widget_set_halign(lblHeroTitle, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblHeroTitle), "neko-empty-title");
        gtk_box_pack_start(GTK_BOX(heroCard), lblHeroTitle, FALSE, FALSE, 0);

        // Description
        GtkWidget *lblHeroDesc = gtk_label_new("No hay actualizaciones pendientes para tus paquetes ni aplicaciones instaladas.");
        gtk_widget_set_halign(lblHeroDesc, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblHeroDesc), "neko-empty-subtitle");
        gtk_box_pack_start(GTK_BOX(heroCard), lblHeroDesc, FALSE, FALSE, 0);

        // Status Badge
        GtkWidget *pillBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_halign(pillBox, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(pillBox), "neko-hero-badge");
        GtkWidget *checkIcon = gtk_image_new_from_icon_name("object-select-symbolic", GTK_ICON_SIZE_MENU);
        gtk_box_pack_start(GTK_BOX(pillBox), checkIcon, FALSE, FALSE, 0);
        GtkWidget *lblPill = gtk_label_new("Todos los orígenes de software habilitados están al día");
        gtk_box_pack_start(GTK_BOX(pillBox), lblPill, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(heroCard), pillBox, FALSE, FALSE, 4);

        gtk_box_pack_start(GTK_BOX(uw->listContainer), heroCard, FALSE, FALSE, 0);
    } else {
        uint64_t totalBytes = 0;
        for (const auto &p : pkgs) {
            totalBytes += parseSizeToBytes(p.downloadSize);
        }
        std::string totalSizeStr = formatBytes(totalBytes);

        std::string title = "Actualizaciones del Sistema (" + std::to_string(pkgs.size()) + " disponibles)";
        gtk_label_set_text(GTK_LABEL(uw->lblHeader), title.c_str());
        if (uw->lblSub) {
            std::string subTxt = "Se encontraron " + std::to_string(pkgs.size()) + " paquetes listos para actualizar.";
            if (!totalSizeStr.empty()) {
                subTxt += " Descarga estimada: " + totalSizeStr + ".";
            }
            gtk_label_set_text(GTK_LABEL(uw->lblSub), subTxt.c_str());
        }

        // Show and enable "Actualizar Todo"
        gtk_widget_set_no_show_all(uw->btnUpdateAll, FALSE);
        gtk_widget_show_all(uw->btnUpdateAll);
        gtk_widget_set_sensitive(uw->btnUpdateAll, TRUE);
        std::string allTxt = "Actualizar Todo (" + std::to_string(pkgs.size()) + ")";
        if (!totalSizeStr.empty()) {
            allTxt += " • " + totalSizeStr;
        }
        gtk_label_set_text(GTK_LABEL(uw->lblUpdateAll), allTxt.c_str());

        for (const auto &p : pkgs) {
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
            gtk_style_context_add_class(gtk_widget_get_style_context(row), "neko-item-row");

            // App / Package Icon
            GtkWidget *icon = createPackageIcon(p);
            gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

            // Info Column
            GtkWidget *infoCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_box_pack_start(GTK_BOX(row), infoCol, TRUE, TRUE, 0);

            // Top row: App Name + Origin Pill
            GtkWidget *nameRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            std::string dispName = p.name.empty() ? p.id : p.name;
            if (!dispName.empty() && dispName[0] >= 'a' && dispName[0] <= 'z') {
                dispName[0] = toupper(dispName[0]);
            }
            GtkWidget *lblName = gtk_label_new(dispName.c_str());
            gtk_widget_set_halign(lblName, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(lblName), "neko-card-title");
            gtk_box_pack_start(GTK_BOX(nameRow), lblName, FALSE, FALSE, 0);

            GtkWidget *srcPill = gtk_label_new(sourceLabel(p.source).c_str());
            gtk_style_context_add_class(gtk_widget_get_style_context(srcPill), "neko-filter-pill");
            gtk_box_pack_start(GTK_BOX(nameRow), srcPill, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(infoCol), nameRow, FALSE, FALSE, 0);

            // Version Comparison Row
            GtkWidget *verRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            std::string oldVer = !p.installedVersion.empty()
                                     ? p.installedVersion
                                     : (!p.version.empty() && p.version != p.newVersion ? p.version : "");
            std::string newVer = !p.newVersion.empty() ? p.newVersion : p.version;

            std::string verDiff;
            bool isPatchRebuild = false;
            if (!oldVer.empty() && !newVer.empty()) {
                if (oldVer != newVer) {
                    verDiff = oldVer + "  ➔  " + newVer;
                } else {
                    isPatchRebuild = true;
                    verDiff = "v" + newVer + "  (Parche / Nueva compilación)";
                }
            } else if (!newVer.empty()) {
                verDiff = "v" + newVer + "  (Actualización disponible)";
            } else if (!oldVer.empty()) {
                verDiff = "v" + oldVer + "  (Actualización disponible)";
            } else {
                verDiff = "Actualización disponible";
            }

            GtkWidget *lblVer = gtk_label_new(verDiff.c_str());
            gtk_widget_set_halign(lblVer, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(lblVer), "neko-version-badge");
            if (isPatchRebuild) {
                gtk_widget_set_tooltip_text(lblVer, "El desarrollador publicó una nueva compilación en el repositorio con parches de seguridad, librerías o dependencias actualizadas para esta versión.");
            }
            gtk_box_pack_start(GTK_BOX(verRow), lblVer, FALSE, FALSE, 0);

            if (!p.downloadSize.empty() && p.downloadSize != "N/D") {
                GtkWidget *lblSize = gtk_label_new(("⬇ " + p.downloadSize).c_str());
                gtk_style_context_add_class(gtk_widget_get_style_context(lblSize), "neko-card-summary");
                gtk_box_pack_start(GTK_BOX(verRow), lblSize, FALSE, FALSE, 0);
            }
            gtk_box_pack_start(GTK_BOX(infoCol), verRow, FALSE, FALSE, 0);

            // Single Update Button
            GtkWidget *btnSingle = gtk_button_new();
            gtk_style_context_add_class(gtk_widget_get_style_context(btnSingle), "neko-btn-primary");
            gtk_widget_set_valign(btnSingle, GTK_ALIGN_CENTER);

            GtkWidget *sBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_set_halign(sBox, GTK_ALIGN_CENTER);

            GtkWidget *rowSpinner = gtk_spinner_new();
            gtk_widget_set_no_show_all(rowSpinner, TRUE);
            gtk_box_pack_start(GTK_BOX(sBox), rowSpinner, FALSE, FALSE, 0);

            GtkWidget *lblRow = gtk_label_new("Actualizar");
            gtk_box_pack_start(GTK_BOX(sBox), lblRow, FALSE, FALSE, 0);

            gtk_container_add(GTK_CONTAINER(btnSingle), sBox);

            struct SingleCb {
                UpdatesWidgets *uw;
                Package pkg;
                GtkWidget *btn;
                GtkWidget *spinner;
                GtkWidget *label;
            };
            uw->rowItems.push_back({p.id, p.source, btnSingle, rowSpinner, lblRow});

            auto *scb = new SingleCb{uw, p, btnSingle, rowSpinner, lblRow};
            g_signal_connect_data(btnSingle, "clicked", G_CALLBACK(+[](GtkButton *b, gpointer data) {
                if (Transaction::isBusy()) return;
                auto *cb = static_cast<SingleCb*>(data);

                // Disable bulk update and check buttons
                if (cb->uw->btnUpdateAll) {
                    gtk_widget_set_sensitive(cb->uw->btnUpdateAll, FALSE);
                }
                if (cb->uw->btnCheckUpdates) {
                    gtk_widget_set_sensitive(cb->uw->btnCheckUpdates, FALSE);
                }

                // Disable all row buttons during individual update
                for (const auto &item : cb->uw->rowItems) {
                    if (item.btn) {
                        gtk_widget_set_sensitive(item.btn, FALSE);
                    }
                    if (item.btn != GTK_WIDGET(b) && item.label) {
                        gtk_label_set_text(GTK_LABEL(item.label), "En espera...");
                    }
                }

                if (cb->spinner) {
                    gtk_widget_set_no_show_all(cb->spinner, FALSE);
                    gtk_widget_show(cb->spinner);
                    gtk_spinner_start(GTK_SPINNER(cb->spinner));
                }
                if (cb->label) {
                    gtk_label_set_text(GTK_LABEL(cb->label), "Actualizando...");
                }
                if (cb->uw->onUpdateSingle) cb->uw->onUpdateSingle(cb->pkg);
            }), scb, [](gpointer d, GClosure*) { delete static_cast<SingleCb*>(d); }, (GConnectFlags)0);

            gtk_box_pack_start(GTK_BOX(row), btnSingle, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(uw->listContainer), row, FALSE, FALSE, 0);
        }
    }

    gtk_widget_show_all(uw->listContainer);
}

void UpdatesView::setChecking(GtkWidget *viewWidget, bool checking) {
    if (!viewWidget) return;
    auto *uw = static_cast<UpdatesWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "updates_widgets"));
    if (!uw) return;

    uw->isChecking = checking;
    if (checking) {
        if (uw->spinnerCheck) {
            gtk_widget_set_no_show_all(uw->spinnerCheck, FALSE);
            gtk_widget_show(uw->spinnerCheck);
            gtk_spinner_start(GTK_SPINNER(uw->spinnerCheck));
        }
        if (uw->lblCheck) {
            gtk_label_set_text(GTK_LABEL(uw->lblCheck), "Buscando...");
        }
        if (uw->btnCheckUpdates) {
            gtk_widget_set_sensitive(uw->btnCheckUpdates, FALSE);
        }
    } else {
        if (uw->spinnerCheck) {
            gtk_spinner_stop(GTK_SPINNER(uw->spinnerCheck));
            gtk_widget_hide(uw->spinnerCheck);
            gtk_widget_set_no_show_all(uw->spinnerCheck, TRUE);
        }
        if (uw->lblCheck) {
            gtk_label_set_text(GTK_LABEL(uw->lblCheck), "Comprobar");
        }
        if (uw->btnCheckUpdates) {
            gtk_widget_set_sensitive(uw->btnCheckUpdates, TRUE);
        }
    }
}

void UpdatesView::resetActionState(GtkWidget *viewWidget) {
    if (!viewWidget) return;
    auto *uw = static_cast<UpdatesWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "updates_widgets"));
    if (!uw) return;
    if (uw->spinnerAll) {
        gtk_spinner_stop(GTK_SPINNER(uw->spinnerAll));
        gtk_widget_hide(uw->spinnerAll);
        gtk_widget_set_no_show_all(uw->spinnerAll, TRUE);
    }
    if (uw->spinnerCheck) {
        gtk_spinner_stop(GTK_SPINNER(uw->spinnerCheck));
        gtk_widget_hide(uw->spinnerCheck);
        gtk_widget_set_no_show_all(uw->spinnerCheck, TRUE);
    }
    if (uw->lblCheck) {
        gtk_label_set_text(GTK_LABEL(uw->lblCheck), "Comprobar");
    }
    if (uw->btnCheckUpdates) {
        gtk_widget_set_sensitive(uw->btnCheckUpdates, TRUE);
    }
    gtk_widget_set_sensitive(uw->btnUpdateAll, !uw->updates.empty());
    if (uw->lblUpdateAll) {
        uint64_t totalBytes = 0;
        for (const auto &p : uw->updates) {
            totalBytes += parseSizeToBytes(p.downloadSize);
        }
        std::string totalSizeStr = formatBytes(totalBytes);
        std::string txt = uw->updates.empty() ? "Actualizar Todo" : ("Actualizar Todo (" + std::to_string(uw->updates.size()) + ")");
        if (!uw->updates.empty() && !totalSizeStr.empty()) {
            txt += " • " + totalSizeStr;
        }
        gtk_label_set_text(GTK_LABEL(uw->lblUpdateAll), txt.c_str());
    }

    // Reset all row buttons and their spinners/labels
    for (const auto &item : uw->rowItems) {
        if (item.btn) {
            gtk_widget_set_sensitive(item.btn, TRUE);
        }
        if (item.spinner) {
            gtk_spinner_stop(GTK_SPINNER(item.spinner));
            gtk_widget_hide(item.spinner);
            gtk_widget_set_no_show_all(item.spinner, TRUE);
        }
        if (item.label) {
            gtk_label_set_text(GTK_LABEL(item.label), "Actualizar");
        }
    }
}
