#include "DetailsView.h"
#include "PixelLeafAnimation.h"
#include "Proc.h"
#include <map>
#include <algorithm>

struct DetailsWidgets {
    GtkWidget *iconImage = nullptr;
    GtkWidget *lblTitle = nullptr;
    GtkWidget *lblSummary = nullptr;
    GtkWidget *lblVersion = nullptr;
    GtkWidget *lblLicense = nullptr;
    GtkWidget *lblSize = nullptr;
    GtkWidget *lblMaintainer = nullptr;
    GtkWidget *lblHomepage = nullptr;
    GtkWidget *lblDesc = nullptr;
    GtkWidget *btnAction = nullptr;
    GtkWidget *btnLabel = nullptr;
    GtkWidget *animWidget = nullptr;
    GtkWidget *animEventBox = nullptr;
    GtkWidget *sourcePillsBox = nullptr;

    Package currentPkg;
    std::vector<Package> variants;
    std::function<void()> onBack;
    std::function<void(const Package&)> onAction;

    std::vector<GtkWidget*> pillButtons;
    bool updatingPills = false;
    bool isActionRunning = false;
    bool isErrorState = false;
};

static void resetActionStateInternal(DetailsWidgets *dw) {
    if (!dw) return;
    dw->isActionRunning = false;
    dw->isErrorState = false;

    if (dw->animWidget) {
        PixelLeafAnimation::stop(dw->animWidget);
    }
    if (dw->animEventBox) {
        gtk_widget_hide(dw->animEventBox);
        gtk_widget_set_no_show_all(dw->animEventBox, TRUE);
    }
    if (dw->btnAction) {
        gtk_widget_set_no_show_all(dw->btnAction, FALSE);
        gtk_widget_show(dw->btnAction);
        gtk_widget_set_sensitive(dw->btnAction, TRUE);
    }
    for (GtkWidget *pill : dw->pillButtons) {
        gtk_widget_set_sensitive(pill, TRUE);
    }

    std::string actionText;
    if (dw->currentPkg.isInstalled()) {
        actionText = "Desinstalar";
    } else if (dw->currentPkg.source == PkgSource::Source) {
        actionText = "Compilar e Instalar";
    } else {
        actionText = "Instalar";
    }
    const char *btnClass = dw->currentPkg.isInstalled() ? "neko-btn-danger" : "neko-btn-primary";
    if (dw->btnLabel) {
        gtk_label_set_text(GTK_LABEL(dw->btnLabel), actionText.c_str());
    }
    gtk_style_context_remove_class(gtk_widget_get_style_context(dw->btnAction), "neko-btn-primary");
    gtk_style_context_remove_class(gtk_widget_get_style_context(dw->btnAction), "neko-btn-secondary");
    gtk_style_context_remove_class(gtk_widget_get_style_context(dw->btnAction), "neko-btn-danger");
    gtk_style_context_add_class(gtk_widget_get_style_context(dw->btnAction), btnClass);
}

static void setActionErrorInternal(DetailsWidgets *dw) {
    if (!dw) return;
    dw->isActionRunning = false;
    dw->isErrorState = true;

    for (GtkWidget *pill : dw->pillButtons) {
        gtk_widget_set_sensitive(pill, TRUE);
    }

    // Hide the button completely - the animation expresses the error state
    if (dw->btnAction) {
        gtk_widget_hide(dw->btnAction);
        gtk_widget_set_no_show_all(dw->btnAction, TRUE);
    }

    // Trigger paper explosion animation and hold on shattered pieces
    if (dw->animEventBox) {
        gtk_widget_set_no_show_all(dw->animEventBox, FALSE);
        gtk_widget_show_all(dw->animEventBox);
    }
    if (dw->animWidget) {
        PixelLeafAnimation::start(dw->animWidget, PixelAnimMode::Error);
    }
}

static void onActionClicked(GtkButton*, gpointer user_data) {
    auto *dw = static_cast<DetailsWidgets*>(user_data);
    if (!dw) return;

    if (dw->isActionRunning || Transaction::isBusy()) {
        return;
    }

    dw->isActionRunning = true;
    dw->isErrorState = false;
    for (GtkWidget *pill : dw->pillButtons) {
        gtk_widget_set_sensitive(pill, FALSE);
    }

    // Hide button completely while animation runs in its place
    if (dw->btnAction) {
        gtk_widget_hide(dw->btnAction);
        gtk_widget_set_no_show_all(dw->btnAction, TRUE);
    }

    bool isUninstall = dw->currentPkg.isInstalled();
    PixelAnimMode mode = isUninstall ? PixelAnimMode::Uninstall : PixelAnimMode::Install;

    if (dw->animEventBox) {
        gtk_widget_set_no_show_all(dw->animEventBox, FALSE);
        gtk_widget_show_all(dw->animEventBox);
    }
    if (dw->animWidget) {
        PixelLeafAnimation::start(dw->animWidget, mode);
    }

    if (dw->onAction) {
        dw->onAction(dw->currentPkg);
    }
}

static void onDetailsDestroy(gpointer data) {
    auto *dw = static_cast<DetailsWidgets*>(data);
    delete dw;
}

GtkWidget* DetailsView::create(std::function<void()> onBack,
                              std::function<void(const Package&)> onAction) {
    auto *dw = new DetailsWidgets();
    dw->onBack = onBack;
    dw->onAction = onAction;

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 24);
    gtk_container_add(GTK_CONTAINER(scroll), rootBox);

    // 1. Header Panel
    GtkWidget *headerPanel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_style_context_add_class(gtk_widget_get_style_context(headerPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), headerPanel, FALSE, FALSE, 0);

    dw->iconImage = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(dw->iconImage, 64, 64);
    gtk_box_pack_start(GTK_BOX(headerPanel), dw->iconImage, FALSE, FALSE, 0);

    GtkWidget *infoCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(headerPanel), infoCol, TRUE, TRUE, 0);

    // 1. Title Row with Title and Badges (Version & License)
    GtkWidget *titleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(infoCol), titleRow, FALSE, FALSE, 0);

    dw->lblTitle = gtk_label_new("Aplicación");
    gtk_widget_set_halign(dw->lblTitle, GTK_ALIGN_START);
    gtk_widget_set_valign(dw->lblTitle, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(dw->lblTitle), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(titleRow), dw->lblTitle, FALSE, FALSE, 0);

    dw->lblVersion = gtk_label_new("v1.0");
    gtk_widget_set_valign(dw->lblVersion, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(dw->lblVersion), "neko-hero-badge");
    gtk_box_pack_start(GTK_BOX(titleRow), dw->lblVersion, FALSE, FALSE, 0);

    dw->lblLicense = gtk_label_new("");
    gtk_widget_set_valign(dw->lblLicense, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(dw->lblLicense), "neko-filter-pill");
    gtk_label_set_max_width_chars(GTK_LABEL(dw->lblLicense), 26);
    gtk_label_set_ellipsize(GTK_LABEL(dw->lblLicense), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(titleRow), dw->lblLicense, FALSE, FALSE, 0);

    // 2. Summary Row
    dw->lblSummary = gtk_label_new("");
    gtk_widget_set_halign(dw->lblSummary, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(dw->lblSummary), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(dw->lblSummary), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(infoCol), dw->lblSummary, FALSE, FALSE, 0);

    // 3. Source Selection Row (Pills placed below summary where badges were previously)
    dw->sourcePillsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(infoCol), dw->sourcePillsBox, FALSE, FALSE, 4);

    // Action Column (Button & Pixel Art Paper Animation)
    GtkWidget *actionCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(actionCol, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(actionCol, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(actionCol, 150, -1);
    gtk_widget_set_margin_end(actionCol, 24);
    gtk_widget_set_margin_start(actionCol, 16);
    gtk_box_pack_end(GTK_BOX(headerPanel), actionCol, FALSE, FALSE, 0);

    dw->btnAction = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(dw->btnAction), "neko-btn-primary");
    gtk_widget_set_size_request(dw->btnAction, 130, 40);
    gtk_widget_set_halign(dw->btnAction, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dw->btnAction, GTK_ALIGN_CENTER);

    dw->btnLabel = gtk_label_new("Instalar");
    gtk_container_add(GTK_CONTAINER(dw->btnAction), dw->btnLabel);
    g_signal_connect(dw->btnAction, "clicked", G_CALLBACK(onActionClicked), dw);
    gtk_box_pack_start(GTK_BOX(actionCol), dw->btnAction, FALSE, FALSE, 0);

    dw->animEventBox = gtk_event_box_new();
    gtk_widget_set_halign(dw->animEventBox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dw->animEventBox, GTK_ALIGN_CENTER);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(dw->animEventBox), FALSE);
    gtk_widget_set_no_show_all(dw->animEventBox, TRUE);
    gtk_widget_hide(dw->animEventBox);

    dw->animWidget = PixelLeafAnimation::create(72);
    gtk_widget_set_size_request(dw->animWidget, 72, 72);
    gtk_widget_set_halign(dw->animWidget, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dw->animWidget, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(dw->animEventBox), dw->animWidget);

    gtk_widget_add_events(dw->animEventBox, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(dw->animEventBox, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer user_data) -> gboolean {
        auto *dw = static_cast<DetailsWidgets*>(user_data);
        if (dw && dw->isErrorState) {
            resetActionStateInternal(dw);
            return TRUE;
        }
        return FALSE;
    }), dw);
    gtk_box_pack_start(GTK_BOX(actionCol), dw->animEventBox, FALSE, FALSE, 0);



    // 2. Description Panel
    GtkWidget *descPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(descPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), descPanel, FALSE, FALSE, 0);

    GtkWidget *lblDescHead = gtk_label_new("Descripción");
    gtk_widget_set_halign(lblDescHead, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblDescHead), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(descPanel), lblDescHead, FALSE, FALSE, 0);

    dw->lblDesc = gtk_label_new("");
    gtk_widget_set_halign(dw->lblDesc, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(dw->lblDesc), TRUE);
    gtk_label_set_selectable(GTK_LABEL(dw->lblDesc), TRUE);
    gtk_box_pack_start(GTK_BOX(descPanel), dw->lblDesc, FALSE, FALSE, 0);

    // 3. Details / Metadata Grid
    GtkWidget *metaPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(metaPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), metaPanel, FALSE, FALSE, 0);

    GtkWidget *lblMetaHead = gtk_label_new("Información del Paquete");
    gtk_widget_set_halign(lblMetaHead, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblMetaHead), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(metaPanel), lblMetaHead, FALSE, FALSE, 0);

    GtkWidget *metaGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(metaGrid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(metaGrid), 24);
    gtk_box_pack_start(GTK_BOX(metaPanel), metaGrid, FALSE, FALSE, 0);

    auto addRow = [&](int row, const char *title, GtkWidget **targetLabel) {
        GtkWidget *t = gtk_label_new(title);
        gtk_widget_set_halign(t, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(t), "neko-card-summary");
        gtk_grid_attach(GTK_GRID(metaGrid), t, 0, row, 1, 1);

        *targetLabel = gtk_label_new("-");
        gtk_widget_set_halign(*targetLabel, GTK_ALIGN_START);
        gtk_label_set_selectable(GTK_LABEL(*targetLabel), TRUE);
        gtk_grid_attach(GTK_GRID(metaGrid), *targetLabel, 1, row, 1, 1);
    };

    addRow(0, "Tamaño de Descarga:", &dw->lblSize);
    addRow(1, "Mantenedor:", &dw->lblMaintainer);
    addRow(2, "Sitio Web:", &dw->lblHomepage);
    gtk_label_set_track_visited_links(GTK_LABEL(dw->lblHomepage), FALSE);
    static auto onActivateHomepageLink = +[](GtkLabel*, gchar *uri, gpointer) -> gboolean {
        if (!uri || !*uri) return TRUE;
        GError *err = nullptr;
        if (!g_app_info_launch_default_for_uri(uri, nullptr, &err)) {
            if (err) g_error_free(err);
            gchar *argv[] = {const_cast<gchar*>("xdg-open"), uri, nullptr};
            g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr);
        }
        return TRUE;
    };
    g_signal_connect(dw->lblHomepage, "activate-link", G_CALLBACK(onActivateHomepageLink), nullptr);

    g_object_set_data_full(G_OBJECT(scroll), "details_widgets", dw, onDetailsDestroy);
    return scroll;
}

static void applyPackageInfo(DetailsWidgets *dw, const Package &pkg) {
    dw->currentPkg = pkg;

    // Title
    std::string displayName = pkg.name.empty() ? pkg.id : pkg.name;
    if (!displayName.empty() && displayName[0] >= 'a' && displayName[0] <= 'z') {
        displayName[0] = displayName[0] - 'a' + 'A';
    }
    gtk_label_set_text(GTK_LABEL(dw->lblTitle), displayName.c_str());

    // Summary & Version
    std::string summaryText = pkg.summary.empty() ? "Paquete de software para Void Linux" : pkg.summary;
    gtk_label_set_text(GTK_LABEL(dw->lblSummary), summaryText.c_str());

    // Version badge (clean formatting, hide if missing)
    std::string ver = pkg.version;
    if (ver.empty() && !pkg.installedVersion.empty()) ver = pkg.installedVersion;
    if (!ver.empty()) {
        if (ver.front() != 'v' && ver.front() != 'V') ver = "v" + ver;
        gtk_label_set_text(GTK_LABEL(dw->lblVersion), ver.c_str());
        gtk_widget_show(dw->lblVersion);
    } else {
        gtk_widget_hide(dw->lblVersion);
    }

    // License badge (compact and clean, full info in tooltip)
    std::string lic = pkg.license;
    if (lic.empty()) {
        lic = "GPL / Open Source";
        gtk_widget_set_tooltip_text(dw->lblLicense, "Licencia de código abierto");
    } else {
        gtk_widget_set_tooltip_text(dw->lblLicense, lic.c_str());
        if (lic.find(" and ") != std::string::npos || lic.find(" or ") != std::string::npos || lic.size() > 24) {
            auto posAnd = lic.find(" and ");
            auto posOr = lic.find(" or ");
            size_t cut = std::min(posAnd, posOr);
            if (cut != std::string::npos && cut > 0 && cut <= 20) {
                lic = lic.substr(0, cut) + " (+ Varios)";
            } else if (lic.size() > 24) {
                lic = "Open Source / Mixta";
            }
        }
    }
    gtk_label_set_text(GTK_LABEL(dw->lblLicense), lic.c_str());

    // Action button
    if (!dw->isActionRunning) {
        if (!dw->isErrorState) {
            if (dw->animWidget) {
                PixelLeafAnimation::stop(dw->animWidget);
            }
            if (dw->animEventBox) {
                gtk_widget_hide(dw->animEventBox);
                gtk_widget_set_no_show_all(dw->animEventBox, TRUE);
            }
            if (dw->btnAction) {
                gtk_widget_set_no_show_all(dw->btnAction, FALSE);
                gtk_widget_show(dw->btnAction);
                gtk_widget_set_sensitive(dw->btnAction, TRUE);
            }

            std::string actionText;
            if (pkg.isInstalled()) {
                actionText = "Desinstalar";
            } else if (pkg.source == PkgSource::Source) {
                actionText = "Compilar e Instalar";
            } else {
                actionText = "Instalar";
            }
            const char *btnClass = pkg.isInstalled() ? "neko-btn-danger" : "neko-btn-primary";
            gtk_label_set_text(GTK_LABEL(dw->btnLabel), actionText.c_str());
            gtk_style_context_remove_class(gtk_widget_get_style_context(dw->btnAction), "neko-btn-primary");
            gtk_style_context_remove_class(gtk_widget_get_style_context(dw->btnAction), "neko-btn-danger");
            gtk_style_context_add_class(gtk_widget_get_style_context(dw->btnAction), btnClass);
        }
    }

    // Description & Metadata
    std::string desc = pkg.description.empty() ? pkg.summary : pkg.description;
    if (pkg.source == PkgSource::Source) {
        std::string repoOrigin = pkg.repository.empty() ? "void-packages" : pkg.repository;
        if (!desc.empty()) {
            desc += "\n\n(Compilación local mediante Vouru a partir de la plantilla en " + repoOrigin + ").";
        } else {
            desc = "Compilación local directamente desde el código fuente utilizando la plantilla de Vouru (" + repoOrigin + ").";
        }
    } else if (desc.empty()) {
        desc = "No hay descripción disponible para este paquete.";
    }
    gtk_label_set_text(GTK_LABEL(dw->lblDesc), desc.c_str());

    std::string displaySize;
    if (pkg.source == PkgSource::Source) {
        displaySize = "Código fuente (Compilación local)";
    } else if (!pkg.downloadSize.empty()) {
        displaySize = pkg.downloadSize;
    } else if (!pkg.installedSize.empty()) {
        displaySize = pkg.installedSize + " (Instalado)";
    } else {
        displaySize = "N/D";
    }
    gtk_label_set_text(GTK_LABEL(dw->lblSize), displaySize.c_str());

    std::string maintainer = pkg.maintainer;
    if (pkg.source == PkgSource::Source) {
        if (!maintainer.empty() && maintainer.find("orphan") == std::string::npos && maintainer != "Void Linux Team") {
            maintainer += " (Plantilla Vouru)";
        } else if (!pkg.repository.empty()) {
            maintainer = "Repositorio Vouru (" + pkg.repository + ")";
        } else {
            maintainer = "Plantilla void-packages / Usuario";
        }
    } else if (pkg.source == PkgSource::Flatpak) {
        if (maintainer.empty()) maintainer = "Flathub / Desarrollador";
    } else if (pkg.source == PkgSource::AppImage) {
        if (maintainer.empty()) maintainer = "AppImage / Desarrollador";
    } else {
        // XBPS
        if (maintainer.empty() || maintainer == "Void Linux Team") {
            maintainer = "Equipo de Void Linux";
        } else if (maintainer.find("orphan") != std::string::npos) {
            maintainer = "Paquete huérfano (Comunidad Void)";
        }
    }
    gtk_label_set_text(GTK_LABEL(dw->lblMaintainer), maintainer.c_str());

    std::string url = pkg.homepage;
    if (url.empty()) {
        if (pkg.source == PkgSource::Flatpak) url = "https://flathub.org";
        else if (pkg.source == PkgSource::AppImage) url = "https://appimage.org";
        else if (pkg.source == PkgSource::Source && !pkg.repository.empty()) url = pkg.repository;
        else url = "https://voidlinux.org";
    }
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        gchar *escapedUrl = g_markup_escape_text(url.c_str(), -1);
        gchar *markup = g_strdup_printf("<a href=\"%s\">%s</a>", escapedUrl, escapedUrl);
        gtk_label_set_markup(GTK_LABEL(dw->lblHomepage), markup);
        g_free(markup);
        g_free(escapedUrl);
    } else {
        gtk_label_set_text(GTK_LABEL(dw->lblHomepage), url.c_str());
    }

    // Icon
    bool iconLoaded = false;
    if (!pkg.iconPath.empty() && pkg.iconPath[0] == '/') {
        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(pkg.iconPath.c_str(), 64, 64, TRUE, nullptr);
        if (pix) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(dw->iconImage), pix);
            g_object_unref(pix);
            iconLoaded = true;
        }
    }
    if (!iconLoaded) {
        GtkIconTheme *theme = gtk_icon_theme_get_default();
        if (gtk_icon_theme_has_icon(theme, pkg.iconPath.c_str())) {
            gtk_image_set_from_icon_name(GTK_IMAGE(dw->iconImage), pkg.iconPath.c_str(), GTK_ICON_SIZE_DIALOG);
        } else if (gtk_icon_theme_has_icon(theme, pkg.id.c_str())) {
            gtk_image_set_from_icon_name(GTK_IMAGE(dw->iconImage), pkg.id.c_str(), GTK_ICON_SIZE_DIALOG);
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(dw->iconImage), "application-x-executable", GTK_ICON_SIZE_DIALOG);
        }
    }
}

static void rebuildPills(DetailsWidgets *dw) {
    if (!dw || !dw->sourcePillsBox) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(dw->sourcePillsBox));
    for (GList *iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
    dw->pillButtons.clear();

    // Canonical order (0: XBPS, 1: Flatpak, 2: AppImage, 3: Source)
    std::map<int, Package> sourceMap;
    sourceMap[static_cast<int>(dw->currentPkg.source)] = dw->currentPkg;
    for (const auto &v : dw->variants) {
        if (sourceMap.find(static_cast<int>(v.source)) == sourceMap.end()) {
            sourceMap[static_cast<int>(v.source)] = v;
        }
    }

    std::vector<Package> sortedVariants;
    for (const auto &pair : sourceMap) {
        sortedVariants.push_back(pair.second);
    }

    for (const auto &v : sortedVariants) {
        std::string label = sourceLabel(v.source);
        if (v.isInstalled()) label += " (Instalado)";

        GtkWidget *btn = gtk_toggle_button_new_with_label(label.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "neko-filter-pill");

        if (v.source == dw->currentPkg.source) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);
        }

        if (dw->isActionRunning) {
            gtk_widget_set_sensitive(btn, FALSE);
        }

        dw->pillButtons.push_back(btn);

        struct VariantCb {
            DetailsWidgets *dw;
            PkgSource targetSource;
            std::string targetId;
            GtkWidget *btn;
        };
        auto *vcb = new VariantCb{dw, v.source, v.id, btn};

        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto *cb = static_cast<VariantCb*>(data);
            if (!cb || !cb->dw || cb->dw->updatingPills || cb->dw->isActionRunning) return;

            // 1. Save currently displayed package details into dw->variants before switching
            bool saved = false;
            for (auto &existing : cb->dw->variants) {
                if (existing.source == cb->dw->currentPkg.source) {
                    existing = cb->dw->currentPkg;
                    saved = true;
                    break;
                }
            }
            if (!saved) {
                cb->dw->variants.push_back(cb->dw->currentPkg);
            }

            cb->dw->updatingPills = true;
            for (GtkWidget *otherBtn : cb->dw->pillButtons) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(otherBtn), otherBtn == cb->btn);
            }
            cb->dw->updatingPills = false;

            // 2. Find target variant in dw->variants
            Package targetPkg;
            bool found = false;
            for (const auto &existing : cb->dw->variants) {
                if (existing.source == cb->targetSource) {
                    targetPkg = existing;
                    found = true;
                    break;
                }
            }
            if (!found) {
                targetPkg = cb->dw->currentPkg;
                targetPkg.source = cb->targetSource;
                targetPkg.id = cb->targetId;
            }

            // 3. Fallback propagation from XBPS variant if target is Source
            if (targetPkg.source == PkgSource::Source) {
                for (const auto &existing : cb->dw->variants) {
                    if (existing.source == PkgSource::XBPS) {
                        if (targetPkg.version.empty() && !existing.version.empty()) targetPkg.version = existing.version;
                        if (targetPkg.license.empty() && !existing.license.empty()) targetPkg.license = existing.license;
                        if (targetPkg.homepage.empty() && !existing.homepage.empty()) targetPkg.homepage = existing.homepage;
                        if (targetPkg.summary.empty() && !existing.summary.empty()) targetPkg.summary = existing.summary;
                        break;
                    }
                }
            }

            cb->dw->isErrorState = false;
            applyPackageInfo(cb->dw, targetPkg);
        }), vcb, [](gpointer d, GClosure*) { delete static_cast<VariantCb*>(d); }, (GConnectFlags)0);

        gtk_box_pack_start(GTK_BOX(dw->sourcePillsBox), btn, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(dw->sourcePillsBox);
}

void DetailsView::setPackage(GtkWidget *viewWidget, const Package &pkg, const std::vector<Package> &variants) {
    auto *dw = static_cast<DetailsWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "details_widgets"));
    if (!dw) return;

    dw->isErrorState = false;
    dw->isActionRunning = false;
    dw->variants = variants;
    bool hasCur = false;
    for (auto &v : dw->variants) {
        if (v.source == pkg.source) {
            v = pkg;
            hasCur = true;
            break;
        }
    }
    if (!hasCur) {
        dw->variants.push_back(pkg);
    }

    applyPackageInfo(dw, pkg);
    rebuildPills(dw);
    gtk_widget_show_all(viewWidget);
}

void DetailsView::updatePackageDetails(GtkWidget *viewWidget, const Package &pkg) {
    if (!viewWidget) return;
    auto *dw = static_cast<DetailsWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "details_widgets"));
    if (!dw) return;

    bool stateChanged = false;
    bool foundInVariants = false;
    for (auto &v : dw->variants) {
        if (v.source == pkg.source) {
            if (v.state != pkg.state) stateChanged = true;
            v = pkg;
            foundInVariants = true;
            break;
        }
    }
    if (!foundInVariants) {
        dw->variants.push_back(pkg);
    }

    // Propagate common metadata (version, license, homepage) to Source variant if XBPS details arrived
    if (pkg.source == PkgSource::XBPS) {
        for (auto &v : dw->variants) {
            if (v.source == PkgSource::Source) {
                if (v.version.empty() && !pkg.version.empty()) v.version = pkg.version;
                if (v.license.empty() && !pkg.license.empty()) v.license = pkg.license;
                if (v.homepage.empty() && !pkg.homepage.empty()) v.homepage = pkg.homepage;
                if (v.summary.empty() && !pkg.summary.empty()) v.summary = pkg.summary;
            }
        }
    }

    if (dw->currentPkg.source == pkg.source) {
        if (dw->currentPkg.state != pkg.state) stateChanged = true;
        dw->currentPkg = pkg;
        applyPackageInfo(dw, pkg);
    } else if (dw->currentPkg.source == PkgSource::Source && pkg.source == PkgSource::XBPS) {
        if (dw->currentPkg.version.empty() && !pkg.version.empty()) dw->currentPkg.version = pkg.version;
        if (dw->currentPkg.license.empty() && !pkg.license.empty()) dw->currentPkg.license = pkg.license;
        if (dw->currentPkg.homepage.empty() && !pkg.homepage.empty()) dw->currentPkg.homepage = pkg.homepage;
        if (dw->currentPkg.summary.empty() && !pkg.summary.empty()) dw->currentPkg.summary = pkg.summary;
        applyPackageInfo(dw, dw->currentPkg);
    }

    if (stateChanged) {
        rebuildPills(dw);
    }
}

void DetailsView::updatePackageState(GtkWidget *viewWidget, const std::string &pkgId, PkgSource source, bool installed) {
    if (!viewWidget) return;
    auto *dw = static_cast<DetailsWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "details_widgets"));
    if (!dw) return;

    std::string cleanId = pkgId;
    if (cleanId.size() > 8 && cleanId.substr(cleanId.size() - 8) == ".desktop") {
        cleanId = cleanId.substr(0, cleanId.size() - 8);
    }

    auto matchId = [&](const std::string &id, PkgSource s) {
        if (s != source) return false;
        std::string c = id;
        if (c.size() > 8 && c.substr(c.size() - 8) == ".desktop") {
            c = c.substr(0, c.size() - 8);
        }
        return c == cleanId;
    };

    if (matchId(dw->currentPkg.id, dw->currentPkg.source)) {
        dw->currentPkg.state = installed ? PkgState::Installed : PkgState::NotInstalled;
        applyPackageInfo(dw, dw->currentPkg);
    }

    for (auto &v : dw->variants) {
        if (matchId(v.id, v.source)) {
            v.state = installed ? PkgState::Installed : PkgState::NotInstalled;
        }
    }

    rebuildPills(dw);
}

void DetailsView::resetActionState(GtkWidget *viewWidget) {
    if (!viewWidget) return;
    auto *dw = static_cast<DetailsWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "details_widgets"));
    if (!dw) return;
    resetActionStateInternal(dw);
}

void DetailsView::setActionError(GtkWidget *viewWidget) {
    if (!viewWidget) return;
    auto *dw = static_cast<DetailsWidgets*>(g_object_get_data(G_OBJECT(viewWidget), "details_widgets"));
    if (!dw) return;
    setActionErrorInternal(dw);
}
