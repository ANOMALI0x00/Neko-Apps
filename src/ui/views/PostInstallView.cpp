#include "PostInstallView.h"
#include "Transaction.h"
#include "Proc.h"
#include <vector>
#include <string>
#include <filesystem>

namespace {

struct DriverItem {
    std::string id;
    std::string iconFile;
    std::string fallbackIcon;
    std::string title;
    std::string description;
    std::string command;
    std::function<bool()> isInstalled;
};

struct DriverCategory {
    std::string categoryTitle;
    std::vector<DriverItem> items;
};

static bool isPkgInstalled(const std::string &pkg) {
    std::string out = Proc::capture("xbps-query", {"-p", "pkgver", pkg});
    return !out.empty() && out.find(pkg) != std::string::npos;
}

static GtkWidget* createDriverIcon(const std::string &iconFile, const std::string &fallbackThemedIcon) {
    const std::vector<std::string> candidates = {
        "data/icons/drivers/" + iconFile,
        "../data/icons/drivers/" + iconFile,
        "/usr/share/neko-apps/icons/drivers/" + iconFile,
        "/usr/local/share/neko-apps/icons/drivers/" + iconFile
    };
    for (const auto &c : candidates) {
        if (std::filesystem::exists(c)) {
            GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(c.c_str(), 36, 36, TRUE, nullptr);
            if (pix) {
                GtkWidget *img = gtk_image_new_from_pixbuf(pix);
                g_object_unref(pix);
                return img;
            }
        }
    }
    GtkWidget *img = gtk_image_new_from_icon_name(fallbackThemedIcon.c_str(), GTK_ICON_SIZE_DND);
    gtk_widget_set_size_request(img, 36, 36);
    return img;
}

static std::vector<DriverCategory> getDriverCategories() {
    return {
        {
            "Periféricos y Servicios Esenciales",
            {
                {
                    "bluetooth",
                    "bluetooth.png",
                    "bluetooth-active-symbolic",
                    "Soporte Bluetooth (BlueZ)",
                    "Controladores y servicio bluetoothd para auriculares, mandos, teclados y transferencia inalámbrica.",
                    "https://raw.githubusercontent.com/Neko-Void-Linux/bluetooth-enabler/refs/heads/main/install.sh",
                    []() {
                        return std::filesystem::exists("/var/service/bluetoothd") ||
                               isPkgInstalled("bluez");
                    }
                },
                {
                    "printer",
                    "print.png",
                    "printer-symbolic",
                    "Soporte para Impresoras (CUPS)",
                    "Servidor de impresión CUPS, utilidades administrativas y controladores de impresión comunes.",
                    "https://raw.githubusercontent.com/Neko-Void-Linux/printer-enable/refs/heads/main/enable.sh",
                    []() {
                        return std::filesystem::exists("/var/service/cupsd") ||
                               isPkgInstalled("cups");
                    }
                }
            }
        },
        {
            "Controladores Gráficos y Aceleración 3D (GPU)",
            {
                {
                    "amd",
                    "amd.png",
                    "video-display-symbolic",
                    "Controladores Gráficos AMD (Radeon)",
                    "Mesa DRI, controladores Vulkan Radeon (64 y 32 bits) y firmware oficial linux-firmware-amd.",
                    "xbps-install -Sy void-repo-multilib void-repo-nonfree && xbps-install -Sy mesa-dri mesa-dri-32bit mesa-vulkan-radeon mesa-vulkan-radeon-32bit linux-firmware-amd",
                    []() {
                        return isPkgInstalled("mesa-vulkan-radeon");
                    }
                },
                {
                    "intel",
                    "intel.png",
                    "video-display-symbolic",
                    "Controladores Gráficos Intel",
                    "Mesa DRI, Vulkan Intel (64 y 32 bits), aceleración de video por hardware e intel-media-driver.",
                    "xbps-install -Sy void-repo-multilib void-repo-nonfree && mesa-vulkan-intel intel-media-driver",
                    []() {
                        return isPkgInstalled("mesa-vulkan-intel");
                    }
                },
                {
                    "nvidia-open",
                    "nvidia.png",
                    "video-display-symbolic",
                    "NVIDIA (Módulos Open Kernel)",
                    "Controlador oficial NVIDIA con módulos abiertos de kernel (Turing / GTX 16xx, RTX 20xx o superior).",
                    "https://github.com/Neko-Void-Linux/nvidia-support (open)",
                    []() {
                        return isPkgInstalled("nvidia-open") || isPkgInstalled("nvidia-open-dkms");
                    }
                },
                {
                    "nvidia-latest",
                    "nvidia.png",
                    "video-display-symbolic",
                    "NVIDIA (Propietario - Última Versión)",
                    "Controlador privativo oficial más reciente de NVIDIA con aceleración gráfica 3D y soporte CUDA.",
                    "https://github.com/Neko-Void-Linux/nvidia-support (latest)",
                    []() {
                        return isPkgInstalled("nvidia") || isPkgInstalled("nvidia-dkms");
                    }
                },
                {
                    "nvidia-580",
                    "nvidia.png",
                    "video-display-symbolic",
                    "NVIDIA (Rama 580.xx)",
                    "Controladores propietarios de NVIDIA correspondientes a la serie de soporte 580.",
                    "https://github.com/Neko-Void-Linux/nvidia-support (580)",
                    []() {
                        return isPkgInstalled("nvidia580") || isPkgInstalled("nvidia-580");
                    }
                },
                {
                    "nvidia-470",
                    "nvidia.png",
                    "video-display-symbolic",
                    "NVIDIA (Serie 470 Legacy)",
                    "Controlador propietario para tarjetas de arquitectura Kepler (GeForce GTX 600 y 700 series).",
                    "https://github.com/Neko-Void-Linux/nvidia-support (470)",
                    []() {
                        return isPkgInstalled("nvidia470");
                    }
                },
                {
                    "nvidia-390",
                    "nvidia.png",
                    "video-display-symbolic",
                    "NVIDIA (Serie 390 Legacy)",
                    "Controlador propietario para tarjetas de arquitectura Fermi (GeForce GTX 400 y 500 series).",
                    "https://github.com/Neko-Void-Linux/nvidia-support (390)",
                    []() {
                        return isPkgInstalled("nvidia390");
                    }
                }
            }
        }
    };
}

} // namespace

GtkWidget* PostInstallView::create() {
    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 24);
    gtk_container_add(GTK_CONTAINER(scroll), rootBox);

    // 1. Hero Header
    GtkWidget *headPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(headPanel), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), headPanel, FALSE, FALSE, 0);

    GtkWidget *lblTitle = gtk_label_new("Controladores y Drivers de Hardware");
    gtk_widget_set_halign(lblTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblTitle), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(headPanel), lblTitle, FALSE, FALSE, 0);

    GtkWidget *lblSub = gtk_label_new("Instala y configura fácilmente controladores para GPUs AMD, Intel, NVIDIA, Bluetooth e impresoras en Void Linux.");
    gtk_widget_set_halign(lblSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSub), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(headPanel), lblSub, FALSE, FALSE, 0);

    auto categories = getDriverCategories();

    for (const auto &cat : categories) {
        GtkWidget *catPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_style_context_add_class(gtk_widget_get_style_context(catPanel), "neko-box-panel");
        gtk_box_pack_start(GTK_BOX(rootBox), catPanel, FALSE, FALSE, 0);

        GtkWidget *lblCatTitle = gtk_label_new(cat.categoryTitle.c_str());
        gtk_widget_set_halign(lblCatTitle, GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(lblCatTitle), "neko-card-title");
        gtk_box_pack_start(GTK_BOX(catPanel), lblCatTitle, FALSE, FALSE, 0);

        for (const auto &item : cat.items) {
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
            gtk_style_context_add_class(gtk_widget_get_style_context(row), "neko-item-row");

            // Custom Icon
            GtkWidget *icon = createDriverIcon(item.iconFile, item.fallbackIcon);
            gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

            // Text Info
            GtkWidget *infoBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_box_pack_start(GTK_BOX(row), infoBox, TRUE, TRUE, 0);

            GtkWidget *lblItemName = gtk_label_new(item.title.c_str());
            gtk_widget_set_halign(lblItemName, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(lblItemName), "neko-card-title");
            gtk_box_pack_start(GTK_BOX(infoBox), lblItemName, FALSE, FALSE, 0);

            GtkWidget *lblItemDesc = gtk_label_new(item.description.c_str());
            gtk_widget_set_halign(lblItemDesc, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(lblItemDesc), TRUE);
            gtk_style_context_add_class(gtk_widget_get_style_context(lblItemDesc), "neko-card-summary");
            gtk_box_pack_start(GTK_BOX(infoBox), lblItemDesc, FALSE, FALSE, 0);

            bool alreadyInstalled = item.isInstalled ? item.isInstalled() : false;

            // Status label if installed
            GtkWidget *lblStatus = gtk_label_new(nullptr);
            if (alreadyInstalled) {
                gtk_label_set_markup(GTK_LABEL(lblStatus), "<span foreground=\"#8da383\">✓ Instalado</span>");
            }
            gtk_widget_set_valign(lblStatus, GTK_ALIGN_CENTER);
            gtk_box_pack_start(GTK_BOX(row), lblStatus, FALSE, FALSE, 4);

            // Action Button
            GtkWidget *btnAction = gtk_button_new();
            gtk_widget_set_valign(btnAction, GTK_ALIGN_CENTER);
            if (alreadyInstalled) {
                gtk_style_context_add_class(gtk_widget_get_style_context(btnAction), "neko-btn-secondary");
            } else {
                gtk_style_context_add_class(gtk_widget_get_style_context(btnAction), "neko-btn-primary");
            }

            GtkWidget *btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            GtkWidget *btnIcon = gtk_image_new_from_icon_name(
                alreadyInstalled ? "view-refresh-symbolic" : "system-software-install-symbolic",
                GTK_ICON_SIZE_BUTTON
            );
            GtkWidget *btnLbl = gtk_label_new(alreadyInstalled ? "Reinstalar" : "Instalar");
            gtk_box_pack_start(GTK_BOX(btnBox), btnIcon, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(btnBox), btnLbl, FALSE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(btnAction), btnBox);

            struct ActionContext {
                DriverItem item;
                GtkWidget *button;
                GtkWidget *label;
                GtkWidget *icon;
                GtkWidget *statusLbl;
            };

            auto *ctx = new ActionContext{item, btnAction, btnLbl, btnIcon, lblStatus};

            g_signal_connect_data(btnAction, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                if (Transaction::isBusy()) return;
                auto *c = static_cast<ActionContext*>(d);
                gtk_widget_set_sensitive(c->button, FALSE);
                gtk_label_set_text(GTK_LABEL(c->label), "Instalando...");

                std::string actionTitle = "Instalando " + c->item.title;

                Transaction::installDriver(c->item.id, c->item.title, [c](bool success, const std::string &msg) {
                    gtk_widget_set_sensitive(c->button, TRUE);
                    bool installedNow = c->item.isInstalled ? c->item.isInstalled() : success;

                    if (installedNow) {
                        gtk_label_set_markup(GTK_LABEL(c->statusLbl), "<span foreground=\"#8da383\">✓ Instalado</span>");
                        gtk_label_set_text(GTK_LABEL(c->label), "Reinstalar");
                        gtk_image_set_from_icon_name(GTK_IMAGE(c->icon), "view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
                        GtkStyleContext *sc = gtk_widget_get_style_context(c->button);
                        gtk_style_context_remove_class(sc, "neko-btn-primary");
                        gtk_style_context_add_class(sc, "neko-btn-secondary");
                    } else {
                        gtk_label_set_text(GTK_LABEL(c->label), "Instalar");
                        gtk_image_set_from_icon_name(GTK_IMAGE(c->icon), "system-software-install-symbolic", GTK_ICON_SIZE_BUTTON);
                    }

                    if (!success) {
                        GtkWidget *toplevel = gtk_widget_get_toplevel(c->button);
                        GtkWidget *dlg = gtk_message_dialog_new(
                            GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
                            GTK_DIALOG_MODAL,
                            GTK_MESSAGE_ERROR,
                            GTK_BUTTONS_OK,
                            "Error en la instalación de %s:\n\n%s",
                            c->item.title.c_str(),
                            msg.c_str()
                        );
                        g_signal_connect(dlg, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
                        gtk_widget_show_all(dlg);
                    }
                });
            }), ctx, [](gpointer d, GClosure*) {
                delete static_cast<ActionContext*>(d);
            }, (GConnectFlags)0);

            gtk_box_pack_start(GTK_BOX(row), btnAction, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(catPanel), row, FALSE, FALSE, 0);
        }
    }

    return scroll;
}
