#include "NekoAppCard.h"
#include "Transaction.h"
#include <unordered_map>

struct CardCallbacks {
    Package pkg;
    std::function<void(const Package&)> onClicked;
    std::function<void(const Package&)> onAction;
    GtkWidget *btnAction = nullptr;
    GtkWidget *lblAction = nullptr;
    GtkWidget *spinner = nullptr;
};

static void onCardButtonRelease(GtkWidget*, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_RELEASE && event->button == 1) {
        auto *cb = static_cast<CardCallbacks*>(user_data);
        if (cb && cb->onClicked) {
            cb->onClicked(cb->pkg);
        }
    }
}

static void onActionButtonClicked(GtkButton *btn, gpointer user_data) {
    auto *cb = static_cast<CardCallbacks*>(user_data);
    if (!cb) return;

    if (Transaction::isBusy()) {
        return;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
    if (cb->spinner) {
        gtk_widget_set_no_show_all(cb->spinner, FALSE);
        gtk_widget_show(cb->spinner);
        gtk_spinner_start(GTK_SPINNER(cb->spinner));
    }
    if (cb->lblAction) {
        if (cb->pkg.isInstalled()) {
            gtk_label_set_text(GTK_LABEL(cb->lblAction), "Eliminando...");
        } else if (cb->pkg.source == PkgSource::Source) {
            gtk_label_set_text(GTK_LABEL(cb->lblAction), "Compilando...");
        } else if (!cb->pkg.downloadSize.empty()) {
            std::string t = "Descargando (" + cb->pkg.downloadSize + ")...";
            gtk_label_set_text(GTK_LABEL(cb->lblAction), t.c_str());
        } else {
            gtk_label_set_text(GTK_LABEL(cb->lblAction), "Descargando...");
        }
    }

    if (cb->onAction) {
        cb->onAction(cb->pkg);
    }
}

static void onCardDestroy(gpointer data, GClosure*) {
    auto *cb = static_cast<CardCallbacks*>(data);
    delete cb;
}

GtkWidget* NekoAppCard::create(const Package &pkg,
                              std::function<void(const Package&)> onClicked,
                              std::function<void(const Package&)> onAction) {
    auto *cb = new CardCallbacks{pkg, onClicked, onAction};

    GtkWidget *eventBox = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eventBox), FALSE);
    gtk_widget_set_can_focus(eventBox, TRUE);
    g_signal_connect_data(eventBox, "button-release-event", G_CALLBACK(onCardButtonRelease),
                          cb, onCardDestroy, (GConnectFlags)0);

    GtkWidget *cardBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(cardBox), "neko-card");
    gtk_container_add(GTK_CONTAINER(eventBox), cardBox);

    // Row: Icon + Title/Summary
    GtkWidget *topRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(cardBox), topRow, FALSE, FALSE, 0);

    // Icon with fast cached theme lookup
    static std::unordered_map<std::string, bool> s_iconCache;
    GtkWidget *iconImage = nullptr;
    GtkIconTheme *theme = gtk_icon_theme_get_default();

    auto hasIcon = [theme](const std::string &name) -> bool {
        if (name.empty()) return false;
        auto it = s_iconCache.find(name);
        if (it != s_iconCache.end()) return it->second;
        bool ex = gtk_icon_theme_has_icon(theme, name.c_str());
        s_iconCache[name] = ex;
        return ex;
    };

    if (!pkg.iconPath.empty() && pkg.iconPath[0] == '/') {
        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(pkg.iconPath.c_str(), 48, 48, TRUE, nullptr);
        if (pix) {
            iconImage = gtk_image_new_from_pixbuf(pix);
            g_object_unref(pix);
        }
    }
    if (!iconImage) {
        if (hasIcon(pkg.iconPath)) {
            iconImage = gtk_image_new_from_icon_name(pkg.iconPath.c_str(), GTK_ICON_SIZE_DIALOG);
        } else if (hasIcon(pkg.id)) {
            iconImage = gtk_image_new_from_icon_name(pkg.id.c_str(), GTK_ICON_SIZE_DIALOG);
        } else {
            iconImage = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
        }
    }
    gtk_widget_set_size_request(iconImage, 48, 48);
    gtk_box_pack_start(GTK_BOX(topRow), iconImage, FALSE, FALSE, 0);

    // Info Column
    GtkWidget *infoCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(topRow), infoCol, TRUE, TRUE, 0);

    // Title
    std::string displayName = pkg.name.empty() ? pkg.id : pkg.name;
    if (!displayName.empty() && displayName[0] >= 'a' && displayName[0] <= 'z') {
        displayName[0] = displayName[0] - 'a' + 'A';
    }
    GtkWidget *lblTitle = gtk_label_new(displayName.c_str());
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(lblTitle), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(lblTitle), 22);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(infoCol), lblTitle, FALSE, FALSE, 0);

    // Summary
    std::string summary = pkg.summary.empty() ? "Paquete de software para Linux" : pkg.summary;
    GtkWidget *lblSummary = gtk_label_new(summary.c_str());
    gtk_widget_set_halign(lblSummary, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(lblSummary), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(lblSummary), 30);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSummary), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(infoCol), lblSummary, FALSE, FALSE, 0);

    // Bottom Action Row
    GtkWidget *bottomRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(cardBox), bottomRow, FALSE, FALSE, 0);

    // Version label
    std::string verStr = pkg.version.empty() ? "" : "v" + pkg.version;
    GtkWidget *lblVer = gtk_label_new(verStr.c_str());
    gtk_widget_set_halign(lblVer, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblVer), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(bottomRow), lblVer, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(bottomRow), spacer, TRUE, TRUE, 0);

    // Action button with working spinner
    const char *actionText = pkg.isInstalled() ? "Desinstalar" : (pkg.source == PkgSource::Source ? "Compilar" : "Instalar");
    const char *btnClass = pkg.isInstalled() ? "neko-btn-danger" : "neko-btn-primary";
    GtkWidget *btnAction = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btnAction), btnClass);

    GtkWidget *btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(btnBox, GTK_ALIGN_CENTER);

    GtkWidget *spinner = gtk_spinner_new();
    gtk_widget_set_no_show_all(spinner, TRUE);
    gtk_box_pack_start(GTK_BOX(btnBox), spinner, FALSE, FALSE, 0);

    GtkWidget *lblAction = gtk_label_new(actionText);
    gtk_box_pack_start(GTK_BOX(btnBox), lblAction, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(btnAction), btnBox);

    cb->btnAction = btnAction;
    cb->lblAction = lblAction;
    cb->spinner = spinner;

    g_signal_connect(btnAction, "clicked", G_CALLBACK(onActionButtonClicked), cb);
    gtk_box_pack_start(GTK_BOX(bottomRow), btnAction, FALSE, FALSE, 0);

    gtk_widget_show_all(eventBox);
    return eventBox;
}
