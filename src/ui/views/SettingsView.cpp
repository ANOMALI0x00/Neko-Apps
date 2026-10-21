#include "SettingsView.h"
#include "Config.h"
#include "Proc.h"
#include "AppImageBackend.h"
#include "CustomRepoDialog.h"

struct SettingsWidgets {
    std::function<void()> onBackendsChanged;
    GtkWidget *sourceSwitch = nullptr;
    bool updatingSwitch = false;
};

#include <vector>
#include <string>

struct InstructionStep {
    std::string description;
    std::string command;
};

static void copyCommandToClipboard(const std::string &cmd, GtkWidget *btnLabel) {
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (clip) {
        gtk_clipboard_set_text(clip, cmd.c_str(), -1);
    }
    GtkClipboard *primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (primary) {
        gtk_clipboard_set_text(primary, cmd.c_str(), -1);
    }
    if (btnLabel && GTK_IS_LABEL(btnLabel)) {
        gtk_label_set_text(GTK_LABEL(btnLabel), "Copiado");
        g_object_ref(btnLabel);
        g_timeout_add(1500, +[](gpointer data) -> gboolean {
            GtkWidget *lbl = GTK_WIDGET(data);
            if (GTK_IS_LABEL(lbl)) {
                gtk_label_set_text(GTK_LABEL(lbl), "Copiar");
            }
            g_object_unref(lbl);
            return G_SOURCE_REMOVE;
        }, btnLabel);
    }
}

static void showInstructionsDialog(GtkWindow *parent,
                                   const char *windowTitle,
                                   const char *heading,
                                   const char *introText,
                                   const std::vector<InstructionStep> &steps,
                                   const char *footerText = nullptr) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        windowTitle,
        parent,
        GTK_DIALOG_MODAL,
        "Cerrar", GTK_RESPONSE_OK,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dlg), 620, -1);

    GtkWidget *btnOk = gtk_dialog_get_widget_for_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    if (btnOk) {
        gtk_style_context_add_class(gtk_widget_get_style_context(btnOk), "neko-btn-primary");
    }

    GtkWidget *contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 18);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_pack_start(GTK_BOX(contentArea), vbox, TRUE, TRUE, 0);

    GtkWidget *lblTitle = gtk_label_new(heading);
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(vbox), lblTitle, FALSE, FALSE, 0);

    GtkWidget *lblIntro = gtk_label_new(introText);
    gtk_label_set_line_wrap(GTK_LABEL(lblIntro), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lblIntro), 72);
    gtk_label_set_selectable(GTK_LABEL(lblIntro), TRUE);
    gtk_widget_set_can_focus(lblIntro, FALSE);
    gtk_widget_set_halign(lblIntro, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblIntro), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(vbox), lblIntro, FALSE, FALSE, 0);

    for (const auto &step : steps) {
        GtkWidget *stepBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_pack_start(GTK_BOX(vbox), stepBox, FALSE, FALSE, 0);

        GtkWidget *lblStep = gtk_label_new(step.description.c_str());
        gtk_widget_set_halign(lblStep, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblStep), "neko-card-title");
        gtk_box_pack_start(GTK_BOX(stepBox), lblStep, FALSE, FALSE, 0);

        GtkWidget *cmdRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_style_context_add_class(gtk_widget_get_style_context(cmdRow), "neko-item-row");
        gtk_box_pack_start(GTK_BOX(stepBox), cmdRow, FALSE, FALSE, 0);

        GtkWidget *termIcon = gtk_image_new_from_icon_name("utilities-terminal-symbolic", GTK_ICON_SIZE_MENU);
        gtk_box_pack_start(GTK_BOX(cmdRow), termIcon, FALSE, FALSE, 0);

        GtkWidget *lblCmd = gtk_label_new(nullptr);
        gchar *escaped = g_markup_escape_text(step.command.c_str(), -1);
        gchar *markup = g_strdup_printf("<tt>%s</tt>", escaped);
        gtk_label_set_markup(GTK_LABEL(lblCmd), markup);
        g_free(markup);
        g_free(escaped);
        gtk_label_set_selectable(GTK_LABEL(lblCmd), TRUE);
        gtk_widget_set_can_focus(lblCmd, FALSE);
        gtk_label_set_line_wrap(GTK_LABEL(lblCmd), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(lblCmd), PANGO_WRAP_WORD_CHAR);
        gtk_widget_set_halign(lblCmd, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(cmdRow), lblCmd, TRUE, TRUE, 0);

        GtkWidget *btnCopy = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btnCopy), "neko-btn-secondary");
        gtk_widget_set_valign(btnCopy, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(btnCopy, "Copiar comando al portapapeles");

        GtkWidget *cpBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *cpIcon = gtk_image_new_from_icon_name("edit-copy-symbolic", GTK_ICON_SIZE_BUTTON);
        GtkWidget *cpLbl = gtk_label_new("Copiar");
        gtk_box_pack_start(GTK_BOX(cpBox), cpIcon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(cpBox), cpLbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(btnCopy), cpBox);

        struct CopyData {
            std::string cmd;
            GtkWidget *lbl;
        };
        auto *cd = new CopyData{step.command, cpLbl};
        g_signal_connect_data(btnCopy, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
            auto *data = static_cast<CopyData*>(d);
            copyCommandToClipboard(data->cmd, data->lbl);
        }), cd, [](gpointer d, GClosure*) { delete static_cast<CopyData*>(d); }, (GConnectFlags)0);

        gtk_box_pack_start(GTK_BOX(cmdRow), btnCopy, FALSE, FALSE, 0);
    }

    if (footerText && footerText[0] != '\0') {
        GtkWidget *lblFooter = gtk_label_new(footerText);
        gtk_label_set_line_wrap(GTK_LABEL(lblFooter), TRUE);
        gtk_widget_set_halign(lblFooter, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblFooter), "neko-card-summary");
        gtk_box_pack_start(GTK_BOX(vbox), lblFooter, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void showFlatpakNotice(GtkWindow *parent) {
    showInstructionsDialog(
        parent,
        "Instrucciones de Flatpak",
        "Instalación y configuración de Flatpak",
        "Para utilizar el origen de aplicaciones Flatpak en Void Linux, instala el paquete 'flatpak' y añade el repositorio oficial de Flathub:",
        {
            {"1. Instala Flatpak en una terminal:", "sudo xbps-install -S flatpak"},
            {"2. Añade el repositorio oficial de Flathub:", "flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo"}
        },
        "3. Reinicia Neko-Apps para habilitar e integrar el catálogo de Flatpak."
    );
}

static void showSoarNotice(GtkWindow *parent) {
    showInstructionsDialog(
        parent,
        "Instrucciones de Soar y AppImage",
        "Gestor de AppImage y Binarios Portables (Soar)",
        "Neko-Apps detecta automáticamente archivos .AppImage en ~/Applications y ~/.local/bin, e integra Soar (PkgForge) para descargar y actualizar aplicaciones portables sin permisos root.",
        {
            {"1. Instala Soar en tu sesión de usuario:", "curl -fsSL https://raw.githubusercontent.com/pkgforge/soar/main/install.sh | sh"},
            {"2. Sincroniza el catálogo de paquetes:", "~/.local/bin/soar sync"}
        },
        "Los paquetes instalados con Soar y tus archivos .AppImage locales aparecerán automáticamente en el catálogo."
    );
}

GtkWidget* SettingsView::create(std::function<void()> onBackendsChanged) {
    auto *sw = new SettingsWidgets{onBackendsChanged};

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 24);
    gtk_container_add(GTK_CONTAINER(scroll), rootBox);

    // 1. Header
    GtkWidget *headPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(headPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), headPanel, FALSE, FALSE, 0);

    GtkWidget *lblTitle = gtk_label_new("Ajustes de Neko-Apps");
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(headPanel), lblTitle, FALSE, FALSE, 0);

    GtkWidget *lblSub = gtk_label_new("Configura los orígenes de software y la integración de paquetes.");
    gtk_widget_set_halign(lblSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSub), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(headPanel), lblSub, FALSE, FALSE, 0);

    // 2. Backends Section
    GtkWidget *bPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(bPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), bPanel, FALSE, FALSE, 0);

    GtkWidget *lblBTitle = gtk_label_new("Orígenes y Proveedores de Software");
    gtk_widget_set_halign(lblBTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblBTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(bPanel), lblBTitle, FALSE, FALSE, 0);

    Config &cfg = Config::instance();

    // Helper to add a switch row
    auto addSwitchRow = [&](const char *name, const char *desc, bool active, bool sensitive,
                            std::function<void(bool)> onToggle,
                            std::function<void()> onDisabledClick = nullptr,
                            std::function<void()> onGearClick = nullptr) -> GtkWidget* {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_style_context_add_class(gtk_widget_get_style_context(row), "neko-item-row");

        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(row), v, TRUE, TRUE, 0);

        GtkWidget *lblN = gtk_label_new(name);
        gtk_widget_set_halign(lblN, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblN), "neko-card-title");
        gtk_box_pack_start(GTK_BOX(v), lblN, FALSE, FALSE, 0);

        GtkWidget *lblD = gtk_label_new(desc);
        gtk_widget_set_halign(lblD, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblD), "neko-card-summary");
        gtk_box_pack_start(GTK_BOX(v), lblD, FALSE, FALSE, 0);

        if (onDisabledClick) {
            GtkWidget *btnNotice = gtk_button_new();
            gtk_style_context_add_class(gtk_widget_get_style_context(btnNotice), "neko-btn-secondary");
            GtkWidget *nbBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            GtkWidget *nbIcon = gtk_image_new_from_icon_name("dialog-information-symbolic", GTK_ICON_SIZE_BUTTON);
            gtk_box_pack_start(GTK_BOX(nbBox), nbIcon, FALSE, FALSE, 0);
            GtkWidget *nbLbl = gtk_label_new("Instrucciones");
            gtk_box_pack_start(GTK_BOX(nbBox), nbLbl, FALSE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(btnNotice), nbBox);
            g_signal_connect_data(btnNotice, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                auto *fn = static_cast<std::function<void()>*>(d);
                if (*fn) (*fn)();
            }), new std::function<void()>(onDisabledClick), [](gpointer d, GClosure*) {
                delete static_cast<std::function<void()>*>(d);
            }, (GConnectFlags)0);
            gtk_box_pack_start(GTK_BOX(row), btnNotice, FALSE, FALSE, 0);
        }

        if (onGearClick) {
            GtkWidget *btnGear = gtk_button_new();
            gtk_button_set_relief(GTK_BUTTON(btnGear), GTK_RELIEF_NONE);
            gtk_widget_set_focus_on_click(btnGear, FALSE);
            gtk_style_context_add_class(gtk_widget_get_style_context(btnGear), "neko-gear-btn");
            GtkWidget *gIcon = gtk_image_new_from_icon_name("applications-system-symbolic", GTK_ICON_SIZE_BUTTON);
            gtk_container_add(GTK_CONTAINER(btnGear), gIcon);
            gtk_widget_set_tooltip_text(btnGear, "Configurar repositorios de plantillas (templates)");
            gtk_widget_set_valign(btnGear, GTK_ALIGN_CENTER);
            g_signal_connect_data(btnGear, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                auto *fn = static_cast<std::function<void()>*>(d);
                if (*fn) (*fn)();
            }), new std::function<void()>(onGearClick), [](gpointer d, GClosure*) {
                delete static_cast<std::function<void()>*>(d);
            }, (GConnectFlags)0);
            gtk_box_pack_start(GTK_BOX(row), btnGear, FALSE, FALSE, 0);
        }

        GtkWidget *switchWidget = gtk_switch_new();
        gtk_widget_set_valign(switchWidget, GTK_ALIGN_CENTER);
        gtk_switch_set_active(GTK_SWITCH(switchWidget), active);
        gtk_widget_set_sensitive(switchWidget, sensitive);

        struct SwCb {
            std::function<void(bool)> fn;
        };
        auto *scb = new SwCb{onToggle};
        g_signal_connect_data(switchWidget, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer d) -> gboolean {
            auto *cb = static_cast<SwCb*>(d);
            if (cb->fn) cb->fn(state);
            return FALSE;
        }), scb, [](gpointer d, GClosure*) { delete static_cast<SwCb*>(d); }, (GConnectFlags)0);

        gtk_box_pack_start(GTK_BOX(row), switchWidget, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bPanel), row, FALSE, FALSE, 0);
        return switchWidget;
    };

    // Flatpak Row (Disabled and showing instructions if flatpak is not installed)
    bool hasFlatpak = Proc::exists("flatpak");
    addSwitchRow(
        "Flatpak (Flathub)",
        hasFlatpak ? "Instalar aplicaciones universales desde Flathub."
                   : "No disponible: Flatpak no está instalado en este sistema.",
        hasFlatpak ? cfg.flatpakEnabled() : false,
        hasFlatpak,
        [sw](bool state) {
            Config::instance().setBackendEnabled("flatpak", state);
            if (sw->onBackendsChanged) sw->onBackendsChanged();
        },
        hasFlatpak ? std::function<void()>(nullptr) : [rootBox]() {
            showFlatpakNotice(GTK_WINDOW(gtk_widget_get_toplevel(rootBox)));
        }
    );

    // AppImage / Soar Row (Disabled and showing instructions if Soar is not installed)
    bool soarInstalled = AppImageBackend::hasSoar();
    addSwitchRow(
        "Integración de AppImage y Soar (PkgForge)",
        soarInstalled
            ? "Gestionar paquetes portables con Soar y archivos .AppImage en ~/Applications."
            : "No disponible: Soar no está instalado en este sistema.",
        soarInstalled ? cfg.appimageEnabled() : false,
        soarInstalled,
        [sw](bool state) {
            Config::instance().setBackendEnabled("appimage", state);
            if (sw->onBackendsChanged) sw->onBackendsChanged();
        },
        soarInstalled ? std::function<void()>(nullptr) : [rootBox]() {
            showSoarNotice(GTK_WINDOW(gtk_widget_get_toplevel(rootBox)));
        }
    );

    // Source Row (Disabled by default, with Gear button to configure template repositories)
    sw->sourceSwitch = addSwitchRow(
        "Compilación desde fuentes (Vouru)",
        "Compilar paquetes no distribuidos en binario mediante Void-Packages.",
        cfg.sourceEnabled(),
        true,
        [sw](bool state) {
            if (sw->updatingSwitch) return;
            Config::instance().setBackendEnabled("source", state);
            if (sw->onBackendsChanged) sw->onBackendsChanged();
        },
        nullptr,
        [rootBox, sw]() {
            if (CustomRepoDialog::show(GTK_WINDOW(gtk_widget_get_toplevel(rootBox)))) {
                if (sw->sourceSwitch) {
                    sw->updatingSwitch = true;
                    gtk_switch_set_active(GTK_SWITCH(sw->sourceSwitch), Config::instance().sourceEnabled());
                    sw->updatingSwitch = false;
                }
                if (sw->onBackendsChanged) sw->onBackendsChanged();
            }
        }
    );

    g_object_set_data_full(G_OBJECT(scroll), "settings_widgets", sw, [](gpointer d) {
        delete static_cast<SettingsWidgets*>(d);
    });

    return scroll;
}
