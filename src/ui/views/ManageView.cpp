#include "ManageView.h"
#include "Transaction.h"
#include <unordered_map>

struct ManageWidgets {
    GtkWidget *searchEntry = nullptr;
    GtkWidget *listContainer = nullptr;
    GtkWidget *lblCount = nullptr;
    GtkWidget *lblStatus = nullptr;
    GtkWidget *spinnerOpt = nullptr;

    int displayedLimit = 50;
    std::string currentFilter;
    PackageList allInstalled;
    std::function<void(const Package&)> onOpenDetails;
    std::function<void(const Package&)> onUninstall;
};

static void populateList(ManageWidgets *mw, const std::string &filter, bool resetLimit = true) {
    if (resetLimit) {
        mw->displayedLimit = 50;
    }
    mw->currentFilter = filter;

    // Clear old rows
    GList *children = gtk_container_get_children(GTK_CONTAINER(mw->listContainer));
    for (GList *iter = children; iter != nullptr; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

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

    std::string lowerFilter = filter;
    for (auto &c : lowerFilter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    int totalMatches = 0;
    int rendered = 0;

    for (const auto &pkg : mw->allInstalled) {
        if (!lowerFilter.empty()) {
            std::string lowerName = pkg.name;
            std::string lowerId = pkg.id;
            std::string lowerSummary = pkg.summary;
            for (auto &c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto &c : lowerId) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto &c : lowerSummary) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (lowerName.find(lowerFilter) == std::string::npos &&
                lowerId.find(lowerFilter) == std::string::npos &&
                lowerSummary.find(lowerFilter) == std::string::npos) {
                continue;
            }
        }

        totalMatches++;
        if (rendered >= mw->displayedLimit) {
            continue;
        }
        rendered++;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_style_context_add_class(gtk_widget_get_style_context(row), "neko-item-row");

        // Icon
        GtkWidget *icon = nullptr;
        if (!pkg.iconPath.empty() && pkg.iconPath[0] == '/') {
            GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(pkg.iconPath.c_str(), 36, 36, TRUE, nullptr);
            if (pix) {
                icon = gtk_image_new_from_pixbuf(pix);
                g_object_unref(pix);
            }
        }
        if (!icon) {
            if (hasIcon(pkg.iconPath)) {
                icon = gtk_image_new_from_icon_name(pkg.iconPath.c_str(), GTK_ICON_SIZE_DND);
            } else if (hasIcon(pkg.id)) {
                icon = gtk_image_new_from_icon_name(pkg.id.c_str(), GTK_ICON_SIZE_DND);
            } else {
                icon = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DND);
            }
        }
        gtk_widget_set_size_request(icon, 36, 36);
        gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

        // Info
        GtkWidget *infoCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(row), infoCol, TRUE, TRUE, 0);

        GtkWidget *lblName = gtk_label_new(pkg.name.c_str());
        gtk_widget_set_halign(lblName, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblName), "neko-card-title");
        gtk_box_pack_start(GTK_BOX(infoCol), lblName, FALSE, FALSE, 0);

        std::string ver = !pkg.version.empty() ? pkg.version : pkg.installedVersion;
        std::string sub;
        if (!ver.empty()) {
            sub += (ver.front() == 'v' || ver.front() == 'V') ? ver : ("v" + ver);
            sub += " • ";
        }
        sub += sourceLabel(pkg.source);
        if (!pkg.summary.empty()) {
            sub += " • " + pkg.summary;
        }
        GtkWidget *lblSub = gtk_label_new(sub.c_str());
        gtk_widget_set_halign(lblSub, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(lblSub), PANGO_ELLIPSIZE_END);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblSub), "neko-card-summary");
        gtk_box_pack_start(GTK_BOX(infoCol), lblSub, FALSE, FALSE, 0);

        // Details button
        GtkWidget *btnDetails = gtk_button_new_with_label("Detalles");
        gtk_style_context_add_class(gtk_widget_get_style_context(btnDetails), "neko-btn-secondary");
        struct RowCb {
            ManageWidgets *mw;
            Package p;
        };
        auto *rcb1 = new RowCb{mw, pkg};
        g_signal_connect_data(btnDetails, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto *cb = static_cast<RowCb*>(data);
            if (cb->mw->onOpenDetails) cb->mw->onOpenDetails(cb->p);
        }), rcb1, [](gpointer d, GClosure*) { delete static_cast<RowCb*>(d); }, (GConnectFlags)0);
        gtk_box_pack_start(GTK_BOX(row), btnDetails, FALSE, FALSE, 0);

        // Uninstall button with working spinner
        GtkWidget *btnUninstall = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btnUninstall), "neko-btn-danger");

        GtkWidget *uBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_halign(uBox, GTK_ALIGN_CENTER);

        GtkWidget *uSpinner = gtk_spinner_new();
        gtk_widget_set_no_show_all(uSpinner, TRUE);
        gtk_box_pack_start(GTK_BOX(uBox), uSpinner, FALSE, FALSE, 0);

        GtkWidget *uLabel = gtk_label_new("Desinstalar");
        gtk_box_pack_start(GTK_BOX(uBox), uLabel, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(btnUninstall), uBox);

        struct UninstCb {
            ManageWidgets *mw;
            Package p;
            GtkWidget *btn;
            GtkWidget *spinner;
            GtkWidget *label;
        };
        auto *rcb2 = new UninstCb{mw, pkg, btnUninstall, uSpinner, uLabel};
        g_signal_connect_data(btnUninstall, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto *cb = static_cast<UninstCb*>(data);
            if (Transaction::isBusy()) {
                return;
            }
            if (cb->mw->onUninstall) cb->mw->onUninstall(cb->p);
        }), rcb2, [](gpointer d, GClosure*) { delete static_cast<UninstCb*>(d); }, (GConnectFlags)0);
        gtk_box_pack_start(GTK_BOX(row), btnUninstall, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(mw->listContainer), row, FALSE, FALSE, 0);
    }

    if (totalMatches > rendered) {
        GtkWidget *btnMore = gtk_button_new_with_label(
            ("Cargar más paquetes instalados... (Mostrando " + std::to_string(rendered) + " de " + std::to_string(totalMatches) + ")").c_str()
        );
        gtk_style_context_add_class(gtk_widget_get_style_context(btnMore), "neko-btn-secondary");
        g_signal_connect_swapped(btnMore, "clicked", G_CALLBACK(+[](ManageWidgets *w) {
            w->displayedLimit += 50;
            populateList(w, w->currentFilter, false);
        }), mw);
        gtk_box_pack_start(GTK_BOX(mw->listContainer), btnMore, FALSE, FALSE, 12);
    }

    std::string countStr = "Mostrando " + std::to_string(rendered) + " de " + std::to_string(totalMatches) + " paquetes instalados";
    gtk_label_set_text(GTK_LABEL(mw->lblCount), countStr.c_str());
    gtk_widget_show_all(mw->listContainer);
}

static void onSearchChanged(GtkSearchEntry *entry, gpointer user_data) {
    auto *mw = static_cast<ManageWidgets*>(user_data);
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    populateList(mw, text ? text : "");
}

static void onManageDestroy(gpointer data) {
    auto *mw = static_cast<ManageWidgets*>(data);
    delete mw;
}

static void setOptStatus(ManageWidgets *w, const std::string &msg) {
    if (!w || !w->lblStatus) return;
    if (msg.empty()) {
        gtk_widget_set_no_show_all(w->lblStatus, TRUE);
        gtk_widget_hide(w->lblStatus);
    } else {
        gtk_label_set_text(GTK_LABEL(w->lblStatus), msg.c_str());
        gtk_widget_set_no_show_all(w->lblStatus, FALSE);
        gtk_widget_show(w->lblStatus);
    }
}

GtkWidget* ManageView::create(std::function<void(const Package&)> onOpenDetails,
                             std::function<void(const Package&)> onUninstall) {
    auto *mw = new ManageWidgets();
    mw->onOpenDetails = onOpenDetails;
    mw->onUninstall = onUninstall;

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 24);
    gtk_container_add(GTK_CONTAINER(scroll), rootBox);

    // 1. Header Box
    GtkWidget *headPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(headPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), headPanel, FALSE, FALSE, 0);

    GtkWidget *lblTitle = gtk_label_new("Gestión del Sistema y Aplicaciones");
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(headPanel), lblTitle, FALSE, FALSE, 0);

    GtkWidget *lblSub = gtk_label_new("Desinstala paquetes instalados y ejecuta tareas de optimización de Void Linux.");
    gtk_widget_set_halign(lblSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSub), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(headPanel), lblSub, FALSE, FALSE, 0);

    // 2. Acceleration / Optimization Box
    GtkWidget *optPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(optPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), optPanel, FALSE, FALSE, 0);

    GtkWidget *lblOptTitle = gtk_label_new("Aceleración y Limpieza de Void Linux");
    gtk_widget_set_halign(lblOptTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblOptTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(optPanel), lblOptTitle, FALSE, FALSE, 0);

    GtkWidget *btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(optPanel), btnRow, FALSE, FALSE, 0);

    GtkWidget *btnClean = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btnClean), "neko-btn-secondary");
    GtkWidget *cBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *cIcon = gtk_image_new_from_icon_name("edit-clear-all-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *cLbl = gtk_label_new("Limpiar Caché (xbps-remove -O)");
    gtk_box_pack_start(GTK_BOX(cBox), cIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cBox), cLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btnClean), cBox);
    g_signal_connect(btnClean, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        if (Transaction::isBusy()) return;
        auto *w = static_cast<ManageWidgets*>(data);
        gtk_widget_set_no_show_all(w->spinnerOpt, FALSE);
        gtk_widget_show(w->spinnerOpt);
        gtk_spinner_start(GTK_SPINNER(w->spinnerOpt));
        setOptStatus(w, "Limpiando caché de paquetes...");
        Transaction::cleanXbpsCache([w](bool, const std::string &msg) {
            gtk_spinner_stop(GTK_SPINNER(w->spinnerOpt));
            gtk_widget_hide(w->spinnerOpt);
            setOptStatus(w, msg);
        });
    }), mw);
    gtk_box_pack_start(GTK_BOX(btnRow), btnClean, FALSE, FALSE, 0);

    GtkWidget *btnOrphans = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btnOrphans), "neko-btn-secondary");
    GtkWidget *oBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *oIcon = gtk_image_new_from_icon_name("user-trash-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *oLbl = gtk_label_new("Eliminar Huérfanos (xbps-remove -o)");
    gtk_box_pack_start(GTK_BOX(oBox), oIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(oBox), oLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btnOrphans), oBox);
    g_signal_connect(btnOrphans, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        if (Transaction::isBusy()) return;
        auto *w = static_cast<ManageWidgets*>(data);
        gtk_widget_set_no_show_all(w->spinnerOpt, FALSE);
        gtk_widget_show(w->spinnerOpt);
        gtk_spinner_start(GTK_SPINNER(w->spinnerOpt));
        setOptStatus(w, "Buscando paquetes huérfanos...");
        Transaction::removeXbpsOrphans([w](bool, const std::string &msg) {
            gtk_spinner_stop(GTK_SPINNER(w->spinnerOpt));
            gtk_widget_hide(w->spinnerOpt);
            setOptStatus(w, msg);
        });
    }), mw);
    gtk_box_pack_start(GTK_BOX(btnRow), btnOrphans, FALSE, FALSE, 0);

    GtkWidget *btnReconfig = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btnReconfig), "neko-btn-secondary");
    GtkWidget *rBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *rIcon = gtk_image_new_from_icon_name("system-run-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *rLbl = gtk_label_new("Reconfigurar Paquetes (xbps-reconfigure -a)");
    gtk_box_pack_start(GTK_BOX(rBox), rIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rBox), rLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btnReconfig), rBox);
    g_signal_connect(btnReconfig, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        if (Transaction::isBusy()) return;
        auto *w = static_cast<ManageWidgets*>(data);
        gtk_widget_set_no_show_all(w->spinnerOpt, FALSE);
        gtk_widget_show(w->spinnerOpt);
        gtk_spinner_start(GTK_SPINNER(w->spinnerOpt));
        setOptStatus(w, "Reconfigurando paquetes...");
        Transaction::reconfigureXbps([w](bool, const std::string &msg) {
            gtk_spinner_stop(GTK_SPINNER(w->spinnerOpt));
            gtk_widget_hide(w->spinnerOpt);
            setOptStatus(w, msg);
        });
    }), mw);
    gtk_box_pack_start(GTK_BOX(btnRow), btnReconfig, FALSE, FALSE, 0);

    GtkWidget *optStatusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    mw->spinnerOpt = gtk_spinner_new();
    gtk_widget_set_no_show_all(mw->spinnerOpt, TRUE);
    gtk_box_pack_start(GTK_BOX(optStatusBox), mw->spinnerOpt, FALSE, FALSE, 0);

    mw->lblStatus = gtk_label_new("");
    gtk_widget_set_halign(mw->lblStatus, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(mw->lblStatus), "neko-hero-badge");
    gtk_widget_set_no_show_all(mw->lblStatus, TRUE);
    gtk_box_pack_start(GTK_BOX(optStatusBox), mw->lblStatus, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(optPanel), optStatusBox, FALSE, FALSE, 0);

    // 3. Installed Packages Section
    GtkWidget *instPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_pack_start(GTK_BOX(rootBox), instPanel, TRUE, TRUE, 0);

    GtkWidget *filterRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(instPanel), filterRow, FALSE, FALSE, 0);

    mw->lblCount = gtk_label_new("Cargando aplicaciones instaladas...");
    gtk_widget_set_halign(mw->lblCount, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(mw->lblCount), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(filterRow), mw->lblCount, TRUE, TRUE, 0);

    mw->searchEntry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(mw->searchEntry), "Filtrar instalados...");
    g_signal_connect(mw->searchEntry, "search-changed", G_CALLBACK(onSearchChanged), mw);
    gtk_box_pack_start(GTK_BOX(filterRow), mw->searchEntry, FALSE, FALSE, 0);

    mw->listContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(instPanel), mw->listContainer, TRUE, TRUE, 0);

    g_object_set_data_full(G_OBJECT(scroll), "manage_widgets", mw, onManageDestroy);
    return scroll;
}

void ManageView::setInstalledPackages(GtkWidget *manageWidget, const PackageList &pkgs) {
    auto *mw = static_cast<ManageWidgets*>(g_object_get_data(G_OBJECT(manageWidget), "manage_widgets"));
    if (!mw) return;
    mw->allInstalled = pkgs;
    const char *text = gtk_entry_get_text(GTK_ENTRY(mw->searchEntry));
    populateList(mw, text ? text : "");
}
