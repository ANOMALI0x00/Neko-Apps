#include "CustomRepoDialog.h"
#include "Config.h"
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <memory>

bool CustomRepoDialog::show(GtkWindow *parent) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Repositorios de Templates (Vouru)",
        parent,
        GTK_DIALOG_MODAL,
        "Cancelar", GTK_RESPONSE_CANCEL,
        "Guardar", GTK_RESPONSE_ACCEPT,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 460);

    // Style the Guardar button
    GtkWidget *btnSave = gtk_dialog_get_widget_for_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    if (btnSave) {
        gtk_style_context_add_class(gtk_widget_get_style_context(btnSave), "neko-btn-primary");
    }
    GtkWidget *btnCancel = gtk_dialog_get_widget_for_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);
    if (btnCancel) {
        gtk_style_context_add_class(gtk_widget_get_style_context(btnCancel), "neko-btn-secondary");
    }

    GtkWidget *contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 18);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_pack_start(GTK_BOX(contentArea), vbox, TRUE, TRUE, 0);

    // 1. Header Information
    GtkWidget *headBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), headBox, FALSE, FALSE, 0);

    GtkWidget *lblTitle = gtk_label_new("Repositorios Personalizados para Vouru");
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(headBox), lblTitle, FALSE, FALSE, 0);

    GtkWidget *lblSub = gtk_label_new("Agrega repositorios de Git o rutas que contengan plantillas (templates) para compilar paquetes adicionales con Vouru.");
    gtk_label_set_line_wrap(GTK_LABEL(lblSub), TRUE);
    gtk_widget_set_halign(lblSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSub), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(headBox), lblSub, FALSE, FALSE, 0);

    // Working list of repos
    auto workingRepos = std::make_shared<std::vector<std::string>>(Config::instance().customSourceRepos());

    // 2. Scrolled List of Repositories
    GtkWidget *lblListTitle = gtk_label_new("Repositorios de plantillas configurados:");
    gtk_widget_set_halign(lblListTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblListTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(vbox), lblListTitle, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 160);
    gtk_style_context_add_class(gtk_widget_get_style_context(scroll), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    GtkWidget *listContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(listContainer), 8);
    gtk_container_add(GTK_CONTAINER(scroll), listContainer);

    auto refreshList = std::make_shared<std::function<void()>>();
    *refreshList = [listContainer, workingRepos, refreshList]() {
        // Clear children
        GList *children = gtk_container_get_children(GTK_CONTAINER(listContainer));
        for (GList *iter = children; iter != nullptr; iter = g_list_next(iter)) {
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        }
        g_list_free(children);

        if (workingRepos->empty()) {
            GtkWidget *emptyLbl = gtk_label_new("No hay repositorios adicionales configurados. Utiliza el campo inferior para agregar uno.");
            gtk_widget_set_halign(emptyLbl, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(emptyLbl), "neko-card-summary");
            gtk_box_pack_start(GTK_BOX(listContainer), emptyLbl, FALSE, FALSE, 12);
        } else {
            for (size_t i = 0; i < workingRepos->size(); ++i) {
                const std::string &repo = (*workingRepos)[i];
                GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_style_context_add_class(gtk_widget_get_style_context(row), "neko-item-row");

                GtkWidget *icon = gtk_image_new_from_icon_name("folder-remote-symbolic", GTK_ICON_SIZE_MENU);
                gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

                GtkWidget *lbl = gtk_label_new(repo.c_str());
                gtk_widget_set_halign(lbl, GTK_ALIGN_START);
                gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
                gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "neko-card-title");
                gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);

                GtkWidget *btnDel = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
                gtk_style_context_add_class(gtk_widget_get_style_context(btnDel), "neko-btn-danger");
                gtk_widget_set_tooltip_text(btnDel, "Eliminar repositorio");

                struct DelData {
                    std::shared_ptr<std::vector<std::string>> repos;
                    std::string target;
                    std::shared_ptr<std::function<void()>> ref;
                };
                auto *dd = new DelData{workingRepos, repo, refreshList};
                g_signal_connect_data(btnDel, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                    auto *data = static_cast<DelData*>(d);
                    auto it = std::remove(data->repos->begin(), data->repos->end(), data->target);
                    if (it != data->repos->end()) {
                        data->repos->erase(it, data->repos->end());
                    }
                    if (*data->ref) (*data->ref)();
                }), dd, [](gpointer d, GClosure*) { delete static_cast<DelData*>(d); }, (GConnectFlags)0);

                gtk_box_pack_start(GTK_BOX(row), btnDel, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(listContainer), row, FALSE, FALSE, 0);
            }
        }
        gtk_widget_show_all(listContainer);
    };

    (*refreshList)();

    // 3. Add Repository Section
    GtkWidget *lblAddTitle = gtk_label_new("Dirección del repositorio (Git URL o ruta local):");
    gtk_widget_set_halign(lblAddTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblAddTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(vbox), lblAddTitle, FALSE, FALSE, 0);

    GtkWidget *addBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), addBox, FALSE, FALSE, 0);

    GtkWidget *entryUrl = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entryUrl), "ej. https://github.com/Neko-Void-Linux/cnr");
    gtk_box_pack_start(GTK_BOX(addBox), entryUrl, TRUE, TRUE, 0);

    GtkWidget *btnAdd = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btnAdd), "neko-btn-primary");
    GtkWidget *addInner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *addIcon = gtk_image_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *addLbl = gtk_label_new("Añadir");
    gtk_box_pack_start(GTK_BOX(addInner), addIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(addInner), addLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btnAdd), addInner);
    gtk_box_pack_start(GTK_BOX(addBox), btnAdd, FALSE, FALSE, 0);

    auto onAddTriggered = [entryUrl, workingRepos, refreshList]() {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entryUrl));
        if (!text) return;
        std::string s = text;
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '/')) s.pop_back();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
        if (s.empty()) return;

        // Check duplicates
        for (const auto &r : *workingRepos) {
            if (r == s) return;
        }
        workingRepos->push_back(s);
        gtk_entry_set_text(GTK_ENTRY(entryUrl), "");
        (*refreshList)();
    };

    struct AddCbData {
        std::function<void()> fn;
    };
    auto *ad = new AddCbData{onAddTriggered};
    g_signal_connect_data(btnAdd, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
        auto *data = static_cast<AddCbData*>(d);
        if (data->fn) data->fn();
    }), ad, [](gpointer d, GClosure*) { delete static_cast<AddCbData*>(d); }, (GConnectFlags)0);

    auto *ed = new AddCbData{onAddTriggered};
    g_signal_connect_data(entryUrl, "activate", G_CALLBACK(+[](GtkEntry*, gpointer d) {
        auto *data = static_cast<AddCbData*>(d);
        if (data->fn) data->fn();
    }), ed, [](gpointer d, GClosure*) { delete static_cast<AddCbData*>(d); }, (GConnectFlags)0);

    gtk_widget_show_all(dlg);

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    bool accepted = (res == GTK_RESPONSE_ACCEPT);
    if (accepted) {
        onAddTriggered();
        Config::instance().setCustomSourceRepos(*workingRepos);
    }

    gtk_widget_destroy(dlg);
    return accepted;
}
