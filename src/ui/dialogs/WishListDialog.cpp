#include "WishListDialog.h"
#include <glib.h>
#include <gio/gio.h>
#include <string>

namespace {

void sendIssueToGithub([[maybe_unused]] GtkWindow *parent, const std::string &name, const std::string &notes) {
    std::string title = "[Solicitud]: " + name;
    std::string body = "### Solicitud de Paquete para Neko Void (CNR)\n\n"
                       "- **Nombre de la aplicación**: " + name + "\n"
                       "- **Sistema operativo**: Void Linux (Neko Void)\n";
    if (!notes.empty()) {
        body += "- **Nota para los desarrolladores**: " + notes + "\n";
    }
    body += "- **Origen**: Solicitud enviada a través de Neko-Apps\n\n"
            "---\n"
            "_Esta solicitud fue enviada desde Neko-Apps para su incorporación en el repositorio [Neko-Void-Linux/cnr](https://github.com/Neko-Void-Linux/cnr)._";

    gchar *escTitle = g_uri_escape_string(title.c_str(), nullptr, FALSE);
    gchar *escBody = g_uri_escape_string(body.c_str(), nullptr, FALSE);

    std::string url = "https://github.com/Neko-Void-Linux/cnr/issues/new?title=";
    url += escTitle;
    url += "&body=";
    url += escBody;

    g_free(escTitle);
    g_free(escBody);

    GError *err = nullptr;
    if (!g_app_info_launch_default_for_uri(url.c_str(), nullptr, &err)) {
        if (err) g_error_free(err);
        gchar *argv[] = {const_cast<gchar*>("xdg-open"), const_cast<gchar*>(url.c_str()), nullptr};
        g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr);
    }
}

} // namespace

void WishListDialog::show(GtkWindow *parent) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Solicitar Paquete a Neko Void",
        parent,
        GTK_DIALOG_MODAL,
        "Cancelar", GTK_RESPONSE_CANCEL,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dlg), 540, 340);

    GtkWidget *btnCancel = gtk_dialog_get_widget_for_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);
    if (btnCancel) {
        gtk_style_context_add_class(gtk_widget_get_style_context(btnCancel), "neko-btn-secondary");
    }

    GtkWidget *contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 18);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_pack_start(GTK_BOX(contentArea), vbox, TRUE, TRUE, 0);

    // Header Panel
    GtkWidget *heroBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(heroBox), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(vbox), heroBox, FALSE, FALSE, 0);

    GtkWidget *lblTitle = gtk_label_new("Solicitud de Paquetes (CNR)");
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(heroBox), lblTitle, FALSE, FALSE, 0);

    GtkWidget *lblInfo = gtk_label_new(
        "¿No encuentras una aplicación en los repositorios de Void Linux? Escribe el nombre del programa "
        "y notas para los desarrolladores. Al pulsar Enviar Solicitud se abrirá el reporte pre-completado en GitHub."
    );
    gtk_label_set_line_wrap(GTK_LABEL(lblInfo), TRUE);
    gtk_widget_set_halign(lblInfo, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblInfo), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(heroBox), lblInfo, FALSE, FALSE, 0);

    // Form Panel
    GtkWidget *formBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(formBox), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(vbox), formBox, FALSE, FALSE, 0);

    GtkWidget *lblName = gtk_label_new("Nombre de la aplicación o herramienta:");
    gtk_widget_set_halign(lblName, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblName), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(formBox), lblName, FALSE, FALSE, 0);

    GtkWidget *entryName = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entryName), "ej. blender, kdenlive, lutris, vesktop...");
    gtk_box_pack_start(GTK_BOX(formBox), entryName, FALSE, FALSE, 0);

    GtkWidget *lblNotes = gtk_label_new("Nota para los desarrolladores (opcional):");
    gtk_widget_set_halign(lblNotes, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblNotes), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(formBox), lblNotes, FALSE, FALSE, 0);

    GtkWidget *entryNotes = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entryNotes), "ej. versión específica, librerías, enlace oficial o motivo...");
    gtk_box_pack_start(GTK_BOX(formBox), entryNotes, FALSE, FALSE, 0);

    // Action button inside form
    GtkWidget *btnSend = gtk_button_new();
    GtkWidget *bBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *bIcon = gtk_image_new_from_icon_name("mail-send-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *bLbl = gtk_label_new("Enviar Solicitud");
    gtk_box_pack_start(GTK_BOX(bBox), bIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bBox), bLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btnSend), bBox);
    gtk_style_context_add_class(gtk_widget_get_style_context(btnSend), "neko-btn-primary");
    gtk_widget_set_halign(btnSend, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(formBox), btnSend, FALSE, FALSE, 4);

    struct SendData {
        GtkWindow *parent;
        GtkWidget *dlg;
        GtkWidget *entryName;
        GtkWidget *entryNotes;
    };
    auto *sd = new SendData{parent, dlg, entryName, entryNotes};

    auto onSend = +[](GtkWidget*, gpointer d) {
        auto *data = static_cast<SendData*>(d);
        const char *name = gtk_entry_get_text(GTK_ENTRY(data->entryName));
        const char *notes = gtk_entry_get_text(GTK_ENTRY(data->entryNotes));

        std::string sName = name ? name : "";
        while (!sName.empty() && (sName.back() == ' ' || sName.back() == '\t')) sName.pop_back();
        while (!sName.empty() && (sName.front() == ' ' || sName.front() == '\t')) sName.erase(0, 1);

        if (sName.empty()) {
            gtk_widget_grab_focus(data->entryName);
            return;
        }

        std::string sNotes = notes ? notes : "";
        while (!sNotes.empty() && (sNotes.back() == ' ' || sNotes.back() == '\t')) sNotes.pop_back();
        while (!sNotes.empty() && (sNotes.front() == ' ' || sNotes.front() == '\t')) sNotes.erase(0, 1);

        sendIssueToGithub(data->parent, sName, sNotes);
        gtk_dialog_response(GTK_DIALOG(data->dlg), GTK_RESPONSE_ACCEPT);
    };

    g_object_set_data_full(G_OBJECT(dlg), "wishlist_data", sd, [](gpointer d) {
        delete static_cast<SendData*>(d);
    });

    g_signal_connect(btnSend, "clicked", G_CALLBACK(onSend), sd);
    g_signal_connect(entryName, "activate", G_CALLBACK(onSend), sd);
    g_signal_connect(entryNotes, "activate", G_CALLBACK(onSend), sd);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}
