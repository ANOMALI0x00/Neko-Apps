#include "FloatingTerminal.h"
#include "ThemeManager.h"

FloatingTerminal::FloatingTerminal(GtkWindow *parentWindow) {
    m_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(m_window), "Terminal de Operaciones - Neko-Apps");
    gtk_window_set_default_size(GTK_WINDOW(m_window), 720, 440);
    if (parentWindow) {
        gtk_window_set_transient_for(GTK_WINDOW(m_window), parentWindow);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(m_window), TRUE);
    }
    gtk_window_set_type_hint(GTK_WINDOW(m_window), GDK_WINDOW_TYPE_HINT_NORMAL);

    gtk_style_context_add_class(gtk_widget_get_style_context(m_window), "neko-floating-terminal-window");

    // Intercept delete-event to hide rather than destroy
    g_signal_connect(m_window, "delete-event", G_CALLBACK(+[](GtkWidget *w, GdkEvent*, gpointer) -> gboolean {
        gtk_widget_hide(w);
        return TRUE;
    }), nullptr);

    // Press Escape to hide
    g_signal_connect(m_window, "key-press-event", G_CALLBACK(+[](GtkWidget *w, GdkEventKey *ev, gpointer) -> gboolean {
        if (ev->keyval == GDK_KEY_Escape) {
            gtk_widget_hide(w);
            return TRUE;
        }
        return FALSE;
    }), nullptr);

    GtkWidget *mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(m_window), mainBox);

    // Top Header / Action Bar
    GtkWidget *termHeader = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(termHeader), "neko-terminal-header");
    gtk_container_set_border_width(GTK_CONTAINER(termHeader), 6);
    gtk_box_pack_start(GTK_BOX(mainBox), termHeader, FALSE, FALSE, 0);

    GtkWidget *termIcon = gtk_image_new_from_icon_name("utilities-terminal-symbolic", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(termHeader), termIcon, FALSE, FALSE, 4);

    GtkWidget *titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_box_pack_start(GTK_BOX(termHeader), titleBox, FALSE, FALSE, 0);

    GtkWidget *lblTermHeader = gtk_label_new("Terminal de Operaciones");
    gtk_widget_set_halign(lblTermHeader, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTermHeader), "neko-terminal-title");
    gtk_box_pack_start(GTK_BOX(titleBox), lblTermHeader, FALSE, FALSE, 0);

    GtkWidget *lblTermSub = gtk_label_new("Registro en vivo de instalación y compilación");
    gtk_widget_set_halign(lblTermSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTermSub), "neko-terminal-sub");
    gtk_box_pack_start(GTK_BOX(titleBox), lblTermSub, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(termHeader), spacer, TRUE, TRUE, 0);

    // Copy Button
    GtkWidget *btnCopy = gtk_button_new_with_label("Copiar");
    gtk_style_context_add_class(gtk_widget_get_style_context(btnCopy), "neko-btn-secondary");
    gtk_widget_set_tooltip_text(btnCopy, "Copiar todo el registro al portapapeles");
    g_signal_connect(btnCopy, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto *self = static_cast<FloatingTerminal*>(user_data);
        if (!self || !self->m_buffer) return;
        GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        GtkTextIter start;
        GtkTextIter end;
        gtk_text_buffer_get_bounds(self->m_buffer, &start, &end);
        gchar *text = gtk_text_buffer_get_text(self->m_buffer, &start, &end, FALSE);
        if (text) {
            gtk_clipboard_set_text(cb, text, -1);
            g_free(text);
        }
    }), this);
    gtk_box_pack_start(GTK_BOX(termHeader), btnCopy, FALSE, FALSE, 4);

    // Clear Button
    GtkWidget *btnClear = gtk_button_new_with_label("Limpiar");
    gtk_style_context_add_class(gtk_widget_get_style_context(btnClear), "neko-btn-secondary");
    gtk_widget_set_tooltip_text(btnClear, "Limpiar salida de la terminal");
    g_signal_connect(btnClear, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto *self = static_cast<FloatingTerminal*>(user_data);
        if (self) self->clearLog();
    }), this);
    gtk_box_pack_start(GTK_BOX(termHeader), btnClear, FALSE, FALSE, 4);

    // Close Button
    GtkWidget *btnClose = gtk_button_new_with_label("Cerrar");
    gtk_style_context_add_class(gtk_widget_get_style_context(btnClose), "neko-btn-secondary");
    gtk_widget_set_tooltip_text(btnClose, "Ocultar terminal flotante");
    g_signal_connect(btnClose, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto *self = static_cast<FloatingTerminal*>(user_data);
        if (self) self->hide();
    }), this);
    gtk_box_pack_start(GTK_BOX(termHeader), btnClose, FALSE, FALSE, 4);

    // Scrolled window for terminal view
    GtkWidget *scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(mainBox), scrolled, TRUE, TRUE, 0);

    m_textView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(m_textView), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(m_textView), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m_textView), GTK_WRAP_CHAR);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_textView), "neko-terminal-view");
    gtk_container_add(GTK_CONTAINER(scrolled), m_textView);

    m_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m_textView));
    const auto &pal = ThemeManager::palette();
    gtk_text_buffer_create_tag(m_buffer, "error_tag", "foreground", pal.terminalError.c_str(), nullptr);
    gtk_text_buffer_create_tag(m_buffer, "info_tag", "foreground", pal.terminalInfo.c_str(), nullptr);
    gtk_text_buffer_create_tag(m_buffer, "dim_tag", "foreground", pal.terminalDim.c_str(), nullptr);

    GtkTextIter endIter;
    gtk_text_buffer_get_end_iter(m_buffer, &endIter);
    m_endMark = gtk_text_buffer_create_mark(m_buffer, "end_mark", &endIter, FALSE);
}

void FloatingTerminal::show() {
    if (!m_window) return;
    gtk_widget_show_all(m_window);
    gtk_window_present(GTK_WINDOW(m_window));
}

void FloatingTerminal::hide() {
    if (!m_window) return;
    gtk_widget_hide(m_window);
}

void FloatingTerminal::toggle(bool showWindow) {
    if (showWindow) {
        show();
    } else {
        hide();
    }
}

void FloatingTerminal::toggleVisibility() {
    toggle(!isVisible());
}

bool FloatingTerminal::isVisible() const {
    return m_window && gtk_widget_get_visible(m_window);
}

void FloatingTerminal::appendLog(const std::string &line, bool isStderr) {
    if (!m_buffer || !m_textView) return;

    GtkTextIter endIter;
    gtk_text_buffer_get_end_iter(m_buffer, &endIter);

    std::string text = line + "\n";
    const char *tag = nullptr;
    if (isStderr) {
        tag = "error_tag";
    } else if (line.find(">>>") != std::string::npos) {
        tag = "info_tag";
    }

    if (tag) {
        gtk_text_buffer_insert_with_tags_by_name(m_buffer, &endIter, text.c_str(), -1, tag, nullptr);
    } else {
        gtk_text_buffer_insert(m_buffer, &endIter, text.c_str(), -1);
    }

    gtk_text_buffer_get_end_iter(m_buffer, &endIter);
    gtk_text_buffer_move_mark(m_buffer, m_endMark, &endIter);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(m_textView), m_endMark);
}

void FloatingTerminal::clearLog() {
    if (m_buffer) {
        gtk_text_buffer_set_text(m_buffer, "", 0);
    }
}
