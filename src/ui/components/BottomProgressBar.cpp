#include "BottomProgressBar.h"

BottomProgressBar::BottomProgressBar(std::function<void()> onDismiss)
    : m_onDismiss(std::move(onDismiss)) {

    m_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_container), "neko-bottom-container");

    m_actionBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_actionBar), "neko-bottom-bar");
    gtk_box_pack_start(GTK_BOX(m_container), m_actionBar, TRUE, TRUE, 0);

    // Labels Column
    GtkWidget *lblBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(m_actionBar), lblBox, FALSE, FALSE, 0);

    m_lblTitle = gtk_label_new("Listo");
    gtk_widget_set_halign(m_lblTitle, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(m_lblTitle), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(m_lblTitle), 35);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblTitle), "neko-bottom-title");
    gtk_box_pack_start(GTK_BOX(lblBox), m_lblTitle, FALSE, FALSE, 0);

    m_lblStatus = gtk_label_new("Sin operaciones activas");
    gtk_widget_set_halign(m_lblStatus, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(m_lblStatus), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(m_lblStatus), 45);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblStatus), "neko-bottom-status");
    gtk_box_pack_start(GTK_BOX(lblBox), m_lblStatus, FALSE, FALSE, 0);

    // Progress Bar
    m_progressBar = gtk_progress_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(m_progressBar), "neko-progress-bar");
    gtk_widget_set_size_request(m_progressBar, 240, 8);
    gtk_widget_set_valign(m_progressBar, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(m_actionBar), m_progressBar, TRUE, TRUE, 0);

    // Close / Dismiss Button
    m_btnClose = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_btnClose), "neko-btn-secondary");
    gtk_widget_set_tooltip_text(m_btnClose, "Ocultar barra");
    g_signal_connect(m_btnClose, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto *self = static_cast<BottomProgressBar*>(user_data);
        if (!self) return;
        self->hide();
        if (self->m_onDismiss) self->m_onDismiss();
    }), this);
    gtk_box_pack_start(GTK_BOX(m_actionBar), m_btnClose, FALSE, FALSE, 0);

    gtk_widget_show_all(m_actionBar);
    gtk_widget_set_no_show_all(m_container, TRUE);
    gtk_widget_hide(m_container);
}

void BottomProgressBar::updateProgress(const std::string &title, const DownloadProgress &prog) {
    if (!m_container) return;
    gtk_widget_show(m_container);
    gtk_widget_show(m_actionBar);

    if (!title.empty()) {
        gtk_label_set_text(GTK_LABEL(m_lblTitle), title.c_str());
    }

    std::string statusStr = prog.status;
    if (!prog.speed.empty()) {
        statusStr += "  (" + prog.speed + ")";
    }
    if (!prog.downloadedStr.empty() && !prog.totalStr.empty()) {
        statusStr += "  [" + prog.downloadedStr + " / " + prog.totalStr + "]";
    }
    gtk_label_set_text(GTK_LABEL(m_lblStatus), statusStr.c_str());

    if (prog.fraction >= 0.0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_progressBar), prog.fraction);
    } else {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(m_progressBar));
    }

    if (prog.stage < 3) {
        gtk_widget_hide(m_btnClose);
    } else {
        gtk_widget_show(m_btnClose);
    }
}

void BottomProgressBar::setFinished(bool ok, const std::string &msg) {
    if (!m_container) return;
    gtk_widget_show(m_btnClose);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_progressBar), ok ? 1.0 : 0.0);
    gtk_label_set_text(GTK_LABEL(m_lblTitle), ok ? "Operación completada" : "Error en la operación");
    gtk_label_set_text(GTK_LABEL(m_lblStatus), msg.c_str());
}

void BottomProgressBar::showTestState() {
    if (!m_container) return;
    gtk_widget_show(m_container);
    gtk_widget_show(m_actionBar);
    gtk_label_set_text(GTK_LABEL(m_lblTitle), "Instalando: Firefox (XBPS)");
    gtk_label_set_text(GTK_LABEL(m_lblStatus), "Descargando paquetes... (3.8 MB/s) [28.2 MB / 56.4 MB]");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_progressBar), 0.50);
}

void BottomProgressBar::hide() {
    if (m_container) {
        gtk_widget_hide(m_container);
    }
}
