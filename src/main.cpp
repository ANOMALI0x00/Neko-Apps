#include "MainWindow.h"
#include "ThemeManager.h"
#include "CustomRepoDialog.h"
#include "WishListDialog.h"
#include "LocalPackageDialog.h"
#include <gtk/gtk.h>
#include <iostream>
#include <cstring>

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::cout << "Neko-Apps 2.0.0 (GTK3)\n"
                      << "Centro gráfico de aplicaciones y gestor de paquetes para Neko Void Linux.\n";
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "Uso: neko-apps [OPCIONES] [ARCHIVO.xbps | template]\n\n"
                      << "Opciones:\n"
                      << "  -v, --version         Muestra la versión del programa y sale\n"
                      << "  -h, --help            Muestra esta ayuda de línea de comandos y sale\n"
                      << "      --drivers         Abre la vista de Controladores y Drivers de hardware\n"
                      << "      --repos           Abre la vista de Repositorios XBPS\n"
                      << "      --post-install    Alias para abrir la vista de drivers/post-instalación\n\n"
                      << "Argumentos de archivo:\n"
                      << "  paquete.xbps          Abre el instalador gráfico para un paquete binario XBPS local\n"
                      << "  template              Abre el instalador y compilador gráfico para una plantilla xbps-src\n";
            return 0;
        }
    }

    g_set_prgname("neko-apps");
    g_set_application_name("Neko-Apps");

    // Suppress broken system GTK module warnings (e.g. appmenu-gtk-module / libwindow-decorations ABI mismatch on Void Linux)
    g_log_set_writer_func([](GLogLevelFlags log_level, const GLogField *fields, gsize n_fields, gpointer user_data) -> GLogWriterOutput {
        for (gsize i = 0; i < n_fields; ++i) {
            if (g_strcmp0(fields[i].key, "MESSAGE") == 0 && fields[i].value) {
                const char *msg = static_cast<const char*>(fields[i].value);
                if (std::strstr(msg, "appmenu-gtk-module") || std::strstr(msg, "Failed to load module")) {
                    return G_LOG_WRITER_HANDLED;
                }
            }
        }
        return g_log_writer_default(log_level, fields, n_fields, user_data);
    }, nullptr, nullptr);

    gtk_init(&argc, &argv);

    // Initialize Tokyo Night GTK theme
    ThemeManager::init();

    bool postInstallMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--drivers" || arg == "-drivers" || arg == "--test-drivers" ||
            arg == "--repos" || arg == "-repos" || arg == "--test-repos" ||
            arg == "--post-install" || arg == "-post-install" || arg == "--test-post-install") {
            postInstallMode = true;
            break;
        }
    }

    MainWindow window(postInstallMode);
    window.show();

    std::string localPackagePath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (LocalPackageDialog::canHandle(arg)) {
            localPackagePath = arg;
            break;
        }
    }

    if (!localPackagePath.empty()) {
        g_timeout_add(250, +[](gpointer data) -> gboolean {
            auto *p = static_cast<std::pair<MainWindow*, std::string>*>(data);
            LocalPackageDialog::show(GTK_WINDOW(p->first->window()), p->second, [win = p->first](bool success) {
                if (success) {
                    win->loadAllCatalogs();
                }
            });
            delete p;
            return G_SOURCE_REMOVE;
        }, new std::pair<MainWindow*, std::string>(&window, localPackagePath));
    }

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--test-terminal") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowTerminal();
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--test-terminal-open") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowTerminal(true);
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--test-details") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowDetails();
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--test-updates") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowUpdates();
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--test-manage") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowManage();
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--test-settings") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowSettings();
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--drivers" || arg == "-drivers" || arg == "--test-drivers" ||
                   arg == "--test-post-install" || arg == "--post-install") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowPostInstall();
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--repos" || arg == "-repos" || arg == "--test-repos") {
            g_timeout_add(200, +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->testShowRepos();
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--test-custom-repo-dialog") {
            g_timeout_add(300, +[](gpointer data) -> gboolean {
                auto *win = static_cast<MainWindow*>(data);
                win->testShowSettings();
                CustomRepoDialog::show(GTK_WINDOW(win->window()));
                return G_SOURCE_REMOVE;
            }, &window);
        } else if (arg == "--test-wishlist") {
            g_timeout_add(300, +[](gpointer data) -> gboolean {
                auto *win = static_cast<MainWindow*>(data);
                WishListDialog::show(GTK_WINDOW(win->window()));
                return G_SOURCE_REMOVE;
            }, &window);
        }
    }

    gtk_main();
    return 0;
}
