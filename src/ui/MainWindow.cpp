#include "MainWindow.h"
#include "ThemeManager.h"
#include "NekoAppCard.h"
#include "DetailsView.h"
#include "ManageView.h"
#include "UpdatesView.h"
#include "SettingsView.h"
#include "WishListDialog.h"
#include "PostInstallView.h"
#include "ReposView.h"

#include "Config.h"
#include "XbpsBackend.h"
#include "FlatpakBackend.h"
#include "AppImageBackend.h"
#include "Transaction.h"
#include "Proc.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <filesystem>
#include <algorithm>
#include <unordered_set>

namespace {
struct NavData {
    MainWindow *win;
    int pageIndex;
};

static std::string resolveAppIconPath() {
    static const char *candidates[] = {
        "data/neko-apps.png",
        "../data/neko-apps.png",
        "data/neko-apps.svg",
        "../data/neko-apps.svg",
        "/usr/share/pixmaps/neko-apps.png",
        "/usr/share/icons/hicolor/scalable/apps/neko-apps.svg"
    };
    for (const char *c : candidates) {
        if (std::filesystem::exists(c)) {
            return std::filesystem::absolute(c).string();
        }
    }
    return "neko-apps";
}

static void sendDesktopNotification(const std::string &summary, const std::string &body, bool isError) {
    std::string icon = isError ? "dialog-error" : resolveAppIconPath();
    const char *urgency = isError ? "critical" : "normal";

    std::string cleanBody = body;
    while (!cleanBody.empty() && (cleanBody.back() == '\n' || cleanBody.back() == '\r' || cleanBody.back() == ' ')) {
        cleanBody.pop_back();
    }
    if (cleanBody.size() > 250) {
        cleanBody = cleanBody.substr(0, 247) + "...";
    }

    const char *argv[] = {
        "notify-send",
        "-a", "Neko-Apps",
        "-u", urgency,
        "-i", icon.c_str(),
        summary.c_str(),
        cleanBody.c_str(),
        nullptr
    };

    GError *err = nullptr;
    if (!g_spawn_async(nullptr, const_cast<char**>(argv), nullptr,
                       G_SPAWN_SEARCH_PATH,
                       nullptr, nullptr, nullptr, &err)) {
        if (err) {
            g_error_free(err);
        }
    }
}
}

MainWindow::MainWindow(bool postInstallMode)
    : m_postInstallMode(postInstallMode) {
    m_backends.push_back(std::make_unique<XbpsBackend>());
    m_backends.push_back(std::make_unique<FlatpakBackend>());
    m_backends.push_back(std::make_unique<AppImageBackend>());

    initUi();

    Transaction::setGlobalLogCallback([this](const std::string &line, bool isStderr) {
        if (m_floatingTerminal) {
            m_floatingTerminal->appendLog(line, isStderr);
        }
    });

    Transaction::setGlobalProgressCallback([this](const std::string &title, const DownloadProgress &prog) {
        if (m_bottomBar) {
            m_bottomBar->updateProgress(title, prog);
        }
    });

    Transaction::setGlobalFinishCallback([this](bool ok, const std::string &msg) {
        if (m_bottomBar) {
            m_bottomBar->setFinished(ok, msg);
        }
        if (!ok) {
            DetailsView::setActionError(m_pageDetails);
        }
        std::string title = ok ? "Operación completada" : "Error en la operación";
        std::string body = msg.empty() ? (ok ? "La tarea ha finalizado con éxito." : "Ocurrió un problema durante la operación.") : msg;
        sendDesktopNotification(title, body, !ok);
    });

    loadAllCatalogs();
}

MainWindow::~MainWindow() {
    if (m_alive) *m_alive = false;
    if (m_searchDebounceId > 0) {
        g_source_remove(m_searchDebounceId);
        m_searchDebounceId = 0;
    }
    Transaction::setGlobalLogCallback(nullptr);
    Transaction::setGlobalProgressCallback(nullptr);
    Transaction::setGlobalFinishCallback(nullptr);

    if (m_window) {
        gtk_widget_destroy(m_window);
        m_window = nullptr;
    }
}

void MainWindow::show() {
    gtk_widget_show_all(m_window);
    gtk_widget_hide(m_btnBack);
}

void MainWindow::initUi() {
    m_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(m_window), "Neko-Apps");
    gtk_window_set_default_size(GTK_WINDOW(m_window), 1200, 780);
    gtk_widget_set_size_request(m_window, 960, 620);

    std::string iconFile = resolveAppIconPath();

    if (!iconFile.empty()) {
        GList *iconList = nullptr;
        int sizes[] = {16, 24, 32, 48, 64, 128, 256, 512};
        for (int sz : sizes) {
            GdkPixbuf *p = gdk_pixbuf_new_from_file_at_scale(iconFile.c_str(), sz, sz, TRUE, nullptr);
            if (p) {
                iconList = g_list_append(iconList, p);
            }
        }
        if (iconList) {
            gtk_window_set_icon_list(GTK_WINDOW(m_window), iconList);
            gtk_window_set_default_icon_list(iconList);
            g_list_free_full(iconList, g_object_unref);
        } else {
            gtk_window_set_icon_from_file(GTK_WINDOW(m_window), iconFile.c_str(), nullptr);
            gtk_window_set_default_icon_from_file(iconFile.c_str(), nullptr);
        }
    }

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_window_set_wmclass(GTK_WINDOW(m_window), "neko-apps", "neko-apps");
    G_GNUC_END_IGNORE_DEPRECATIONS

    g_signal_connect(m_window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto *self = static_cast<MainWindow*>(data);
        self->m_window = nullptr;
        if (self->m_alive) *self->m_alive = false;
        gtk_main_quit();
    }), this);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(m_window), rootBox);

    // 1. Sidebar
    initSidebar();
    gtk_box_pack_start(GTK_BOX(rootBox), m_sidebarBox, FALSE, FALSE, 0);

    // 2. Right Content Area (TopBar + Stack + BottomProgressBar)
    GtkWidget *rightArea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), rightArea, TRUE, TRUE, 0);

    initTopBar();
    gtk_box_pack_start(GTK_BOX(rightArea), m_topBar, FALSE, FALSE, 0);

    m_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(m_stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_set_transition_duration(GTK_STACK(m_stack), 0);

    // Page 0: Catálogo
    initCatalogPage();
    gtk_stack_add_named(GTK_STACK(m_stack), m_pageCatalog, "catalog");

    // Page 1: Gestionar
    m_pageManage = ManageView::create(
        [this](const Package &pkg) { onAppCardClicked(pkg); },
        [this](const Package &pkg) { onAppCardAction(pkg); }
    );
    gtk_stack_add_named(GTK_STACK(m_stack), m_pageManage, "manage");

    // Page 2: Actualizaciones
    m_pageUpdates = UpdatesView::create(
        [this](const PackageList &pkgs) {
            Transaction::updateAll(pkgs,
                [this](bool ok, const std::string &msg) {
                    UpdatesView::resetActionState(m_pageUpdates);
                    loadAllCatalogs();
                    if (!ok) {
                        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(m_window),
                            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                            GTK_BUTTONS_OK, "Error al actualizar:\n\n%s", msg.c_str());
                        g_signal_connect(dlg, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
                        gtk_widget_show_all(dlg);
                    }
                }
            );
        },
        [this](const Package &pkg) {
            Transaction::install(pkg,
                [this, pkg](bool ok, const std::string &msg) {
                    UpdatesView::resetActionState(m_pageUpdates);
                    loadAllCatalogs();
                    if (!ok) {
                        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(m_window),
                            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                            GTK_BUTTONS_OK, "Error al actualizar %s:\n\n%s", pkg.name.c_str(), msg.c_str());
                        g_signal_connect(dlg, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
                        gtk_widget_show_all(dlg);
                    }
                }
            );
        },
        [this]() {
            loadAllCatalogs();
        }
    );
    gtk_stack_add_named(GTK_STACK(m_stack), m_pageUpdates, "updates");

    // Page 3: Ajustes
    m_pageSettings = SettingsView::create([this]() {
        onBackendsChanged();
    });
    gtk_stack_add_named(GTK_STACK(m_stack), m_pageSettings, "settings");

    // Page 4: Detalles
    m_pageDetails = DetailsView::create(
        [this]() { goBack(); },
        [this](const Package &pkg) { onAppCardAction(pkg); }
    );
    gtk_stack_add_named(GTK_STACK(m_stack), m_pageDetails, "details");

    // Page 5: Drivers & Page 6: Repos
    m_pagePostInstall = PostInstallView::create();
    gtk_stack_add_named(GTK_STACK(m_stack), m_pagePostInstall, "postinstall");

    m_pageRepos = ReposView::create([this]() {
        loadAllCatalogs();
    });
    gtk_stack_add_named(GTK_STACK(m_stack), m_pageRepos, "repos");

    gtk_box_pack_start(GTK_BOX(rightArea), m_stack, TRUE, TRUE, 0);

    // Initialize decoupled FloatingTerminal and BottomProgressBar components
    m_floatingTerminal = std::make_unique<FloatingTerminal>(GTK_WINDOW(m_window));
    m_bottomBar = std::make_unique<BottomProgressBar>(
        [this]() {
            DetailsView::resetActionState(m_pageDetails);
        }
    );
    gtk_box_pack_end(GTK_BOX(rightArea), m_bottomBar->widget(), FALSE, FALSE, 0);

    switchPage(m_postInstallMode ? 5 : 0);
}

void MainWindow::initSidebar() {
    m_sidebarBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(m_sidebarBox, 150, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_sidebarBox), "neko-sidebar");

    // Profile Box
    GtkWidget *profBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(profBox), "neko-user-box");
    gtk_box_pack_start(GTK_BOX(m_sidebarBox), profBox, FALSE, FALSE, 0);

    GtkWidget *avatar = nullptr;
    std::string storeIconFile = resolveAppIconPath();

    if (!storeIconFile.empty()) {
        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(storeIconFile.c_str(), 32, 32, TRUE, nullptr);
        if (pix) {
            avatar = gtk_image_new_from_pixbuf(pix);
            g_object_unref(pix);
        }
    }
    if (!avatar) {
        avatar = gtk_image_new_from_icon_name("neko-apps", GTK_ICON_SIZE_DND);
        gtk_widget_set_size_request(avatar, 32, 32);
    }
    gtk_box_pack_start(GTK_BOX(profBox), avatar, FALSE, FALSE, 0);

    GtkWidget *vProf = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_box_pack_start(GTK_BOX(profBox), vProf, TRUE, TRUE, 0);

    GtkWidget *lblName = gtk_label_new("Neko-Apps");
    gtk_widget_set_halign(lblName, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblName), "neko-user-title");
    gtk_box_pack_start(GTK_BOX(vProf), lblName, FALSE, FALSE, 0);

    GtkWidget *lblSub = gtk_label_new("Neko Void Linux");
    gtk_widget_set_halign(lblSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblSub), "neko-user-sub");
    gtk_box_pack_start(GTK_BOX(vProf), lblSub, FALSE, FALSE, 0);

    auto createNavBtn = [this](const char *iconName, const char *label, int pageIdx) -> GtkWidget* {
        GtkWidget *btn = gtk_toggle_button_new();
        gtk_widget_set_halign(btn, GTK_ALIGN_FILL);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "neko-side-btn");

        GtkWidget *btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(btnBox), 0);

        GtkWidget *icon = gtk_image_new_from_icon_name(iconName, GTK_ICON_SIZE_BUTTON);
        gtk_box_pack_start(GTK_BOX(btnBox), icon, FALSE, FALSE, 0);

        GtkWidget *lbl = gtk_label_new(label);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(btnBox), lbl, TRUE, TRUE, 0);

        gtk_container_add(GTK_CONTAINER(btn), btnBox);

        auto *data = new NavData{this, pageIdx};
        g_signal_connect_data(btn, "toggled", G_CALLBACK(+[](GtkToggleButton *tb, gpointer d) {
            if (gtk_toggle_button_get_active(tb)) {
                auto *nd = static_cast<NavData*>(d);
                nd->win->switchPage(nd->pageIndex);
            }
        }), data, [](gpointer d, GClosure*) { delete static_cast<NavData*>(d); }, (GConnectFlags)0);

        return btn;
    };

    m_navBtnCatalog = createNavBtn("view-app-grid-symbolic", "Catálogo", 0);
    m_navBtnManage = createNavBtn("system-software-install-symbolic", "Gestionar", 1);
    m_navBtnUpdates = createNavBtn("software-update-available-symbolic", "Actualizaciones", 2);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_navBtnCatalog), !m_postInstallMode);

    gtk_box_pack_start(GTK_BOX(m_sidebarBox), m_navBtnCatalog, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(m_sidebarBox), m_navBtnManage, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(m_sidebarBox), m_navBtnUpdates, FALSE, FALSE, 2);

    if (m_postInstallMode) {
        m_navBtnPostInstall = createNavBtn("preferences-system-symbolic", "Drivers", 5);
        gtk_box_pack_start(GTK_BOX(m_sidebarBox), m_navBtnPostInstall, FALSE, FALSE, 2);

        m_navBtnRepos = createNavBtn("network-server-symbolic", "Repos", 6);
        gtk_box_pack_start(GTK_BOX(m_sidebarBox), m_navBtnRepos, FALSE, FALSE, 2);

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_navBtnPostInstall), TRUE);
    }
}

void MainWindow::initTopBar() {
    m_topBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_topBar), "neko-topbar");

    // 1. Left Section (Back button)
    GtkWidget *leftBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    m_btnBack = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(m_btnBack), "neko-btn-secondary");
    GtkWidget *bbBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *bbIcon = gtk_image_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *bbLbl = gtk_label_new("Volver");
    gtk_box_pack_start(GTK_BOX(bbBox), bbIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bbBox), bbLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(m_btnBack), bbBox);
    g_signal_connect_swapped(m_btnBack, "clicked", G_CALLBACK(+[](MainWindow *w) { w->goBack(); }), this);
    gtk_box_pack_start(GTK_BOX(leftBox), m_btnBack, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_topBar), leftBox, TRUE, TRUE, 0);

    // 2. Center Section (Search Bar)
    GtkWidget *centerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    m_searchEntry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(m_searchEntry), "Buscar paquetes, aplicaciones, herramientas...");
    gtk_widget_set_size_request(m_searchEntry, 520, 36);
    g_signal_connect_swapped(m_searchEntry, "search-changed", G_CALLBACK(+[](MainWindow *w) {
        const char *txt = gtk_entry_get_text(GTK_ENTRY(w->m_searchEntry));
        w->onSearchInput(txt ? txt : "");
    }), this);
    gtk_box_pack_start(GTK_BOX(centerBox), m_searchEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(m_topBar), centerBox, FALSE, FALSE, 0);

    // 3. Right Section (Hamburger Menu)
    GtkWidget *rightBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(m_topBar), rightBox, TRUE, TRUE, 0);

    GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    gtk_size_group_add_widget(sg, leftBox);
    gtk_size_group_add_widget(sg, rightBox);
    g_object_unref(sg);

    m_btnMenu = gtk_menu_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(m_btnMenu), "neko-btn-secondary");
    GtkWidget *menuIcon = gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(m_btnMenu), menuIcon);
    gtk_widget_set_tooltip_text(m_btnMenu, "Menú principal");

    GtkWidget *popover = gtk_popover_new(m_btnMenu);
    GtkWidget *popBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(popBox), 6);
    gtk_container_add(GTK_CONTAINER(popover), popBox);

    auto addMenuItem = [popover, popBox](const char *iconName, const char *text, std::function<void()> cb) {
        GtkWidget *itemBtn = gtk_button_new();
        gtk_widget_set_halign(itemBtn, GTK_ALIGN_FILL);
        gtk_style_context_add_class(gtk_widget_get_style_context(itemBtn), "neko-menu-item");

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(row), 4);

        GtkWidget *ic = gtk_image_new_from_icon_name(iconName, GTK_ICON_SIZE_MENU);
        gtk_box_pack_start(GTK_BOX(row), ic, FALSE, FALSE, 0);

        GtkWidget *lbl = gtk_label_new(text);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);

        gtk_container_add(GTK_CONTAINER(itemBtn), row);

        struct CbData {
            std::function<void()> fn;
            GtkWidget *pop;
        };
        auto *cbd = new CbData{cb, popover};
        g_signal_connect_data(itemBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
            auto *c = static_cast<CbData*>(d);
            gtk_popover_popdown(GTK_POPOVER(c->pop));
            if (c->fn) c->fn();
        }), cbd, [](gpointer d, GClosure*) { delete static_cast<CbData*>(d); }, (GConnectFlags)0);

        gtk_box_pack_start(GTK_BOX(popBox), itemBtn, FALSE, FALSE, 0);
    };

    // 1. Ajustes
    addMenuItem("preferences-system-symbolic", "Ajustes", [this]() {
        switchPage(3);
    });

    // 2. Terminal flotante
    addMenuItem("utilities-terminal-symbolic", "Terminal de operaciones", [this]() {
        if (m_floatingTerminal) m_floatingTerminal->toggleVisibility();
    });

    gtk_box_pack_start(GTK_BOX(popBox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    // 3. Acerca de Neko-Apps
    addMenuItem("help-about-symbolic", "Acerca de Neko-Apps", [this]() {
        GtkWidget *aboutDlg = gtk_about_dialog_new();
        gtk_window_set_transient_for(GTK_WINDOW(aboutDlg), GTK_WINDOW(m_window));
        gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(aboutDlg), "Neko-Apps");
        gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(aboutDlg), "2.0.0 (GTK)");
        gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(aboutDlg), "Centro gráfico de aplicaciones y gestor de paquetes para Neko Void Linux.");
        gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(aboutDlg), "https://github.com/Neko-Void-Linux/neko-apps");
        std::string iconPath = resolveAppIconPath();
        if (!iconPath.empty()) {
            GdkPixbuf *logo = gdk_pixbuf_new_from_file_at_scale(iconPath.c_str(), 64, 64, TRUE, nullptr);
            if (logo) {
                gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(aboutDlg), logo);
                g_object_unref(logo);
            }
        }
        g_signal_connect(aboutDlg, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
        gtk_widget_show_all(aboutDlg);
    });

    gtk_widget_show_all(popBox);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_btnMenu), popover);

    GtkWidget *btnTopTerm = gtk_button_new_from_icon_name("utilities-terminal-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(btnTopTerm), "neko-btn-secondary");
    gtk_widget_set_tooltip_text(btnTopTerm, "Terminal de operaciones");
    g_signal_connect_swapped(btnTopTerm, "clicked", G_CALLBACK(+[](MainWindow *w) {
        if (w->m_floatingTerminal) w->m_floatingTerminal->toggleVisibility();
    }), this);

    gtk_box_pack_end(GTK_BOX(rightBox), m_btnMenu, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(rightBox), btnTopTerm, FALSE, FALSE, 0);
}

void MainWindow::initCatalogPage() {
    m_pageCatalog = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_pageCatalog), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(rootBox), 24);
    gtk_container_add(GTK_CONTAINER(m_pageCatalog), rootBox);

    // 1. Welcome Hero Banner
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(hero), "neko-hero-banner");
    gtk_box_pack_start(GTK_BOX(rootBox), hero, FALSE, FALSE, 0);

    GtkWidget *badgeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *badgeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(badgeBox), "neko-hero-badge");
    GtkWidget *badgeIcon = gtk_image_new_from_icon_name("starred-symbolic", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(badgeBox), badgeIcon, FALSE, FALSE, 0);
    GtkWidget *badge = gtk_label_new("Neko Void Linux");
    gtk_box_pack_start(GTK_BOX(badgeBox), badge, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(badgeRow), badgeBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hero), badgeRow, FALSE, FALSE, 0);

    GtkWidget *lblHeroTitle = gtk_label_new("Bienvenido a Neko-Apps");
    gtk_widget_set_halign(lblHeroTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblHeroTitle), "neko-hero-title");
    gtk_box_pack_start(GTK_BOX(hero), lblHeroTitle, FALSE, FALSE, 0);

    GtkWidget *lblHeroSub = gtk_label_new("Explora, instala y mantén actualizado todo tu software favorito en un solo lugar.");
    gtk_widget_set_halign(lblHeroSub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblHeroSub), "neko-hero-sub");
    gtk_box_pack_start(GTK_BOX(hero), lblHeroSub, FALSE, FALSE, 0);

    // 2. Collapsible Categories Box
    GtkWidget *catBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(catBox), "neko-box-panel");
    gtk_box_pack_start(GTK_BOX(rootBox), catBox, FALSE, FALSE, 0);

    GtkWidget *catHeader = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(catBox), catHeader, FALSE, FALSE, 0);

    GtkWidget *lblCatTitle = gtk_label_new("Categorías");
    gtk_widget_set_halign(lblCatTitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblCatTitle), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(catHeader), lblCatTitle, TRUE, TRUE, 0);

    m_btnToggleCat = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(m_btnToggleCat), "neko-btn-secondary");
    GtkWidget *tcBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *tcIcon = gtk_image_new_from_icon_name("pan-up-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *tcLbl = gtk_label_new("Contraer");
    gtk_box_pack_start(GTK_BOX(tcBox), tcIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tcBox), tcLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(m_btnToggleCat), tcBox);

    g_signal_connect_swapped(m_btnToggleCat, "clicked", G_CALLBACK(+[](MainWindow *w) {
        w->m_categoriesExpanded = !w->m_categoriesExpanded;
        GList *ch = gtk_container_get_children(GTK_CONTAINER(w->m_btnToggleCat));
        if (ch) {
            GtkWidget *box = GTK_WIDGET(ch->data);
            GList *bch = gtk_container_get_children(GTK_CONTAINER(box));
            if (bch && bch->next) {
                GtkWidget *icon = GTK_WIDGET(bch->data);
                GtkWidget *lbl = GTK_WIDGET(bch->next->data);
                if (w->m_categoriesExpanded) {
                    gtk_widget_show(w->m_categoriesGridBox);
                    gtk_image_set_from_icon_name(GTK_IMAGE(icon), "pan-up-symbolic", GTK_ICON_SIZE_BUTTON);
                    gtk_label_set_text(GTK_LABEL(lbl), "Contraer");
                } else {
                    gtk_widget_hide(w->m_categoriesGridBox);
                    gtk_image_set_from_icon_name(GTK_IMAGE(icon), "pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
                    gtk_label_set_text(GTK_LABEL(lbl), "Expandir");
                }
            }
            g_list_free(bch);
            g_list_free(ch);
        }
    }), this);
    gtk_box_pack_start(GTK_BOX(catHeader), m_btnToggleCat, FALSE, FALSE, 0);

    // Categories Grid
    m_categoriesGridBox = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(m_categoriesGridBox), 8);
    gtk_grid_set_column_spacing(GTK_GRID(m_categoriesGridBox), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(m_categoriesGridBox), TRUE);
    gtk_box_pack_start(GTK_BOX(catBox), m_categoriesGridBox, FALSE, FALSE, 0);

    struct CatItem {
        const char *name;
        std::vector<const char*> iconFallbacks;
    };
    std::vector<CatItem> cats = {
        {"Desarrollo", {"applications-development-symbolic", "utilities-terminal-symbolic", "applications-development"}},
        {"Internet",   {"applications-internet-symbolic", "web-browser-symbolic", "applications-internet"}},
        {"Juegos",     {"applications-games-symbolic", "applications-games", "input-gaming-symbolic"}},
        {"Multimedia", {"applications-multimedia-symbolic", "applications-multimedia", "audio-x-generic"}},
        {"Oficina",    {"applications-office-symbolic", "x-office-document-symbolic", "applications-office"}},
        {"Sistema",    {"applications-system-symbolic", "applications-system", "preferences-system-symbolic"}},
        {"Gráficos",   {"applications-graphics-symbolic", "applications-graphics", "image-x-generic"}},
        {"Utilidades", {"applications-utilities-symbolic", "applications-utilities", "applications-other"}},
        {"Educación",  {"applications-science-symbolic", "applications-education-symbolic", "applications-science"}}
    };

    GtkIconTheme *theme = gtk_icon_theme_get_default();
    int col = 0, row = 0;
    for (const auto &c : cats) {
        GtkWidget *btn = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "neko-btn-secondary");

        GtkWidget *btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_halign(btnBox, GTK_ALIGN_CENTER);

        const char *chosenIcon = "applications-other";
        for (const char *icName : c.iconFallbacks) {
            if (gtk_icon_theme_has_icon(theme, icName)) {
                chosenIcon = icName;
                break;
            }
        }
        GtkWidget *icon = gtk_image_new_from_icon_name(chosenIcon, GTK_ICON_SIZE_BUTTON);
        gtk_box_pack_start(GTK_BOX(btnBox), icon, FALSE, FALSE, 0);

        GtkWidget *lbl = gtk_label_new(c.name);
        gtk_box_pack_start(GTK_BOX(btnBox), lbl, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(btn), btnBox);

        struct CatCb { MainWindow *win; std::string name; };
        auto *ccb = new CatCb{this, c.name};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
            auto *cb = static_cast<CatCb*>(d);
            cb->win->onCategoryClicked(cb->name);
        }), ccb, [](gpointer d, GClosure*) { delete static_cast<CatCb*>(d); }, (GConnectFlags)0);

        m_categoryButtons[c.name] = btn;
        gtk_grid_attach(GTK_GRID(m_categoriesGridBox), btn, col, row, 1, 1);
        col++;
        if (col >= 3) {
            col = 0;
            row++;
        }
    }

    // 3. Search results header + Wish list button
    GtkWidget *searchRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(rootBox), searchRow, FALSE, FALSE, 0);

    m_lblSearchHeader = gtk_label_new("Catálogo de Aplicaciones");
    gtk_widget_set_halign(m_lblSearchHeader, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblSearchHeader), "neko-card-title");
    gtk_box_pack_start(GTK_BOX(searchRow), m_lblSearchHeader, TRUE, TRUE, 0);

    GtkWidget *btnWishList = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(btnWishList), "neko-btn-secondary");
    GtkWidget *wlBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *wlIcon = gtk_image_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(wlBox), wlIcon, FALSE, FALSE, 0);
    GtkWidget *wlLbl = gtk_label_new("¿No encuentras la aplicación? Lista de deseos");
    gtk_box_pack_start(GTK_BOX(wlBox), wlLbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btnWishList), wlBox);

    g_signal_connect_swapped(btnWishList, "clicked", G_CALLBACK(+[](MainWindow *w) {
        WishListDialog::show(GTK_WINDOW(w->m_window));
    }), this);
    gtk_box_pack_start(GTK_BOX(searchRow), btnWishList, FALSE, FALSE, 0);

    // 4. Status row with loading spinner
    GtkWidget *statusRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    m_spinnerCatalog = gtk_spinner_new();
    gtk_box_pack_start(GTK_BOX(statusRow), m_spinnerCatalog, FALSE, FALSE, 0);

    m_lblCatalogStatus = gtk_label_new("Sincronizando repositorios de software...");
    gtk_widget_set_halign(m_lblCatalogStatus, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblCatalogStatus), "neko-card-summary");
    gtk_box_pack_start(GTK_BOX(statusRow), m_lblCatalogStatus, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), statusRow, FALSE, FALSE, 0);

    // 5. Responsive FlowBox for App Cards
    m_flowBoxCards = gtk_flow_box_new();
    gtk_widget_set_valign(m_flowBoxCards, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(m_flowBoxCards), 3);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(m_flowBoxCards), 2);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(m_flowBoxCards), 14);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(m_flowBoxCards), 14);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(m_flowBoxCards), TRUE);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(m_flowBoxCards), GTK_SELECTION_NONE);
    gtk_box_pack_start(GTK_BOX(rootBox), m_flowBoxCards, FALSE, FALSE, 0);

    // 6. Load More Button
    m_btnLoadMoreCatalog = gtk_button_new_with_label("Cargar más aplicaciones...");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_btnLoadMoreCatalog), "neko-btn-secondary");
    gtk_widget_set_no_show_all(m_btnLoadMoreCatalog, TRUE);
    g_signal_connect_swapped(m_btnLoadMoreCatalog, "clicked", G_CALLBACK(+[](MainWindow *w) {
        w->m_catalogLimit += 60;
        w->renderCatalogCards(true);
    }), this);
    gtk_box_pack_start(GTK_BOX(rootBox), m_btnLoadMoreCatalog, FALSE, FALSE, 16);
}

void MainWindow::switchPage(int pageIndex) {
    static bool inSwitch = false;
    if (inSwitch) return;
    inSwitch = true;

    if (pageIndex >= 3 && m_currentPage < 3) {
        m_previousPage = m_currentPage;
    }
    m_currentPage = pageIndex;

    if (m_stack) {
        const char *names[] = {"catalog", "manage", "updates", "settings", "details", "postinstall", "repos"};
        if (pageIndex >= 0 && pageIndex < 7) {
            gtk_stack_set_visible_child_name(GTK_STACK(m_stack), names[pageIndex]);
        }
    }
    if (m_btnBack) {
        gtk_widget_set_visible(m_btnBack, pageIndex >= 3);
    }

    GtkWidget *btns[] = {m_navBtnCatalog, m_navBtnManage, m_navBtnUpdates, nullptr, nullptr, m_navBtnPostInstall, m_navBtnRepos};
    for (int i = 0; i < 7; ++i) {
        if (btns[i]) {
            bool active = (i == pageIndex);
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btns[i])) != active) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btns[i]), active);
            }
        }
    }
    inSwitch = false;
}

void MainWindow::goBack() {
    switchPage(m_previousPage >= 3 ? 0 : m_previousPage);
}

static std::string normalizeAppName(const std::string &s) {
    std::string norm;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            norm.push_back(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return norm;
}

static bool isSameApplication(const Package &a, const Package &b) {
    if (a.source == b.source && a.id == b.id) return true;

    std::string normA = normalizeAppName(a.name);
    std::string normB = normalizeAppName(b.name);
    if (!normA.empty() && normA == normB) return true;

    auto getLastSegment = [](const std::string &id) -> std::string {
        auto lastDot = id.rfind('.');
        if (lastDot != std::string::npos && lastDot + 1 < id.size()) {
            return id.substr(lastDot + 1);
        }
        return id;
    };

    std::string segA = normalizeAppName(getLastSegment(a.id));
    std::string segB = normalizeAppName(getLastSegment(b.id));

    if (!segA.empty() && (segA == segB || segA == normB || segB == normA)) return true;

    std::string lowIdA = a.id; for (auto &c : lowIdA) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string lowIdB = b.id; for (auto &c : lowIdB) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowIdA == lowIdB || lowIdA == segB || lowIdB == segA) return true;

    return false;
}

void MainWindow::loadAllCatalogs() {
    const uint64_t gen = ++m_loadGeneration;
    m_catalogMap.clear();
    m_fullCatalog.clear();
    m_installedList.clear();
    m_updatesList.clear();
    m_pendingCatalogBackends = 0;
    m_pendingUpdateBackends = 0;

    if (m_pageUpdates) {
        UpdatesView::setChecking(m_pageUpdates, true);
    }

    if (m_spinnerCatalog) {
        gtk_widget_show(m_spinnerCatalog);
        gtk_spinner_start(GTK_SPINNER(m_spinnerCatalog));
    }
    gtk_label_set_text(GTK_LABEL(m_lblCatalogStatus), "Sincronizando repositorios de software...");

    Config &cfg = Config::instance();
    std::vector<Backend*> activeBackends;
    for (auto &b : m_backends) {
        bool enabled = false;
        if (b->source() == PkgSource::XBPS) enabled = cfg.xbpsEnabled();
        else if (b->source() == PkgSource::Flatpak) enabled = cfg.flatpakEnabled();
        else if (b->source() == PkgSource::AppImage) enabled = cfg.appimageEnabled();

        if (enabled && b->isAvailable()) {
            activeBackends.push_back(b.get());
        }
    }

    m_pendingCatalogBackends = static_cast<int>(activeBackends.size());
    m_pendingUpdateBackends = static_cast<int>(activeBackends.size());

    auto alive = m_alive;
    for (Backend *b : activeBackends) {
        b->loadCatalog([this, gen, alive](PkgSource, const PackageList &pkgs) {
            if (!alive || !*alive || gen != m_loadGeneration) return;

            for (const auto &p : pkgs) {
                std::string key = sourceKey(p.source) + ":" + p.id;
                m_catalogMap[key] = p;
            }

            PackageList allEntries;
            allEntries.reserve(m_catalogMap.size());
            for (const auto &pair : m_catalogMap) {
                allEntries.push_back(pair.second);
            }
            std::sort(allEntries.begin(), allEntries.end(), [](const Package &a, const Package &b) {
                if (a.source != b.source) return static_cast<int>(a.source) < static_cast<int>(b.source);
                return a.id < b.id;
            });

            m_installedList.clear();
            m_fullCatalog.clear();
            std::unordered_set<std::string> seenAppNames;

            for (const auto &p : allEntries) {
                if (p.isInstalled()) {
                    if (p.source == PkgSource::Source && m_catalogMap.find("xbps:" + p.id) != m_catalogMap.end()) {
                        // Already represented by XBPS installed entry
                    } else {
                        m_installedList.push_back(p);
                    }
                }
                std::string norm = normalizeAppName(p.name.empty() ? p.id : p.name);
                if (!norm.empty()) {
                    if (seenAppNames.find(norm) != seenAppNames.end()) {
                        continue;
                    }
                    seenAppNames.insert(norm);
                }
                m_fullCatalog.push_back(p);
            }

            if (m_pendingCatalogBackends > 0) {
                m_pendingCatalogBackends--;
            }

            ManageView::setInstalledPackages(m_pageManage, m_installedList);
            filterCatalog();
        });

        b->checkUpdates([this, gen, alive](PkgSource, const PackageList &upd) {
            if (!alive || !*alive || gen != m_loadGeneration) return;

            for (const auto &u : upd) {
                m_updatesList.push_back(u);
            }
            if (m_pendingUpdateBackends > 0) {
                m_pendingUpdateBackends--;
            }
            UpdatesView::setUpdates(m_pageUpdates, m_updatesList);
            if (m_pendingUpdateBackends > 0) {
                UpdatesView::setChecking(m_pageUpdates, true);
            }
        });
    }

    if (activeBackends.empty()) {
        if (m_spinnerCatalog) {
            gtk_spinner_stop(GTK_SPINNER(m_spinnerCatalog));
            gtk_widget_hide(m_spinnerCatalog);
        }
        ManageView::setInstalledPackages(m_pageManage, m_installedList);
        UpdatesView::setUpdates(m_pageUpdates, m_updatesList);
        filterCatalog();
        gtk_label_set_text(GTK_LABEL(m_lblCatalogStatus), "No hay orígenes de software habilitados.");
    }
}

void MainWindow::filterCatalog() {
    m_filteredList.clear();
    m_catalogLimit = 60;

    std::string lowerQuery = m_searchQuery;
    for (auto &c : lowerQuery) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto &pkg : m_fullCatalog) {
        if (!lowerQuery.empty()) {
            std::string n = pkg.name;
            std::string id = pkg.id;
            std::string s = pkg.summary;
            for (auto &c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto &c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (n.find(lowerQuery) == std::string::npos &&
                id.find(lowerQuery) == std::string::npos &&
                s.find(lowerQuery) == std::string::npos) {
                continue;
            }
        }

        if (!m_selectedCategory.empty()) {
            bool hasCat = false;
            for (const auto &cat : pkg.categories) {
                if (cat == m_selectedCategory) {
                    hasCat = true;
                    break;
                }
            }
            if (!hasCat) continue;
        }

        m_filteredList.push_back(pkg);
    }

    if (lowerQuery.empty()) {
        static const std::unordered_set<std::string> popular = {
            "firefox", "vlc", "gimp", "libreoffice", "steam", "obs-studio", "blender",
            "vscode", "kdenlive", "audacity", "telegram-desktop", "discord", "inkscape",
            "krita", "neovim", "mpv", "lutris", "htop", "fastfetch", "alacritty", "kitty",
            "thunderbird", "chromium", "godot", "qbittorrent", "keepassxc", "filezilla"
        };

        std::stable_sort(m_filteredList.begin(), m_filteredList.end(), [](const Package &a, const Package &b) {
            bool customA = (a.source == PkgSource::Source && !a.repository.empty());
            bool customB = (b.source == PkgSource::Source && !b.repository.empty());
            bool popA = popular.find(a.id) != popular.end() || customA;
            bool popB = popular.find(b.id) != popular.end() || customB;
            if (popA != popB) return popA > popB;

            bool devA = a.id.size() > 6 && a.id.substr(a.id.size() - 6) == "-devel";
            bool devB = b.id.size() > 6 && b.id.substr(b.id.size() - 6) == "-devel";
            if (devA != devB) return devA < devB;

            bool libA = a.id.rfind("lib", 0) == 0;
            bool libB = b.id.rfind("lib", 0) == 0;
            if (libA != libB) return libA < libB;

            return false;
        });
    }

    renderCatalogCards(false);
}

void MainWindow::renderCatalogCards(bool appendOnly) {
    int currentRendered = 0;
    if (!appendOnly) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(m_flowBoxCards));
        for (GList *iter = children; iter != nullptr; iter = g_list_next(iter)) {
            gtk_widget_destroy(GTK_WIDGET(iter->data));
        }
        g_list_free(children);
    } else {
        GList *children = gtk_container_get_children(GTK_CONTAINER(m_flowBoxCards));
        currentRendered = g_list_length(children);
        g_list_free(children);
    }

    int total = static_cast<int>(m_filteredList.size());
    int target = std::min(total, m_catalogLimit);

    for (int i = currentRendered; i < target; ++i) {
        const auto &pkg = m_filteredList[i];
        GtkWidget *card = NekoAppCard::create(
            pkg,
            [this](const Package &p) { onAppCardClicked(p); },
            [this](const Package &p) { onAppCardAction(p); }
        );
        gtk_flow_box_insert(GTK_FLOW_BOX(m_flowBoxCards), card, -1);
    }

    if (total > target) {
        std::string moreText = "Cargar más aplicaciones... (Mostrando " + std::to_string(target) + " de " + std::to_string(total) + ")";
        gtk_button_set_label(GTK_BUTTON(m_btnLoadMoreCatalog), moreText.c_str());
        gtk_widget_show(m_btnLoadMoreCatalog);
    } else {
        gtk_widget_hide(m_btnLoadMoreCatalog);
    }

    std::string status = "Mostrando " + std::to_string(target) + " de " + std::to_string(total) + " aplicaciones disponibles";
    if (!m_selectedCategory.empty()) status += " en " + m_selectedCategory;
    if (m_pendingCatalogBackends > 0) status += " (Sincronizando...)";
    gtk_label_set_text(GTK_LABEL(m_lblCatalogStatus), status.c_str());

    std::string header;
    if (!m_searchQuery.empty()) {
        header = "Resultados de búsqueda: '" + m_searchQuery + "' (" + std::to_string(total) + ")";
    } else if (!m_selectedCategory.empty()) {
        header = "Categoría: " + m_selectedCategory + " (" + std::to_string(total) + ")";
    } else {
        header = "Catálogo de Aplicaciones";
    }
    if (m_lblSearchHeader) {
        gtk_label_set_text(GTK_LABEL(m_lblSearchHeader), header.c_str());
    }

    if (m_spinnerCatalog && m_pendingCatalogBackends <= 0) {
        gtk_spinner_stop(GTK_SPINNER(m_spinnerCatalog));
        gtk_widget_hide(m_spinnerCatalog);
    }

    gtk_widget_show_all(m_flowBoxCards);
}

void MainWindow::onSearchInput(const std::string &query) {
    if (m_searchDebounceId > 0) {
        g_source_remove(m_searchDebounceId);
        m_searchDebounceId = 0;
    }

    m_searchQuery = query;

    // Empty search updates immediately without delay
    if (query.empty()) {
        onSearchChanged(query);
        return;
    }

    // Debounce fast typing by 150 ms
    m_searchDebounceId = g_timeout_add(150, +[](gpointer data) -> gboolean {
        auto *self = static_cast<MainWindow*>(data);
        self->m_searchDebounceId = 0;
        self->onSearchChanged(self->m_searchQuery);
        return G_SOURCE_REMOVE;
    }, this);
}

void MainWindow::onSearchChanged(const std::string &query) {
    m_searchQuery = query;
    if (!query.empty() && gtk_stack_get_visible_child(GTK_STACK(m_stack)) != m_pageCatalog) {
        switchPage(0);
    }
    filterCatalog();
}

void MainWindow::onCategoryClicked(const std::string &category) {
    if (m_selectedCategory == category) {
        m_selectedCategory.clear();
    } else {
        m_selectedCategory = category;
    }

    for (const auto &pair : m_categoryButtons) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(pair.second);
        if (pair.first == m_selectedCategory) {
            gtk_style_context_remove_class(ctx, "neko-btn-secondary");
            gtk_style_context_add_class(ctx, "neko-btn-primary");
        } else {
            gtk_style_context_remove_class(ctx, "neko-btn-primary");
            gtk_style_context_add_class(ctx, "neko-btn-secondary");
        }
    }

    filterCatalog();
}

void MainWindow::onAppCardClicked(const Package &pkg) {
    std::vector<Package> variants;
    std::unordered_set<std::string> seenKeys;
    seenKeys.insert(sourceKey(pkg.source) + ":" + pkg.id);

    Package activePkg = pkg;
    std::string activeKey = sourceKey(pkg.source) + ":" + pkg.id;
    auto itActive = m_catalogMap.find(activeKey);
    if (itActive != m_catalogMap.end()) {
        activePkg = itActive->second;
    }

    Package xbpsVariant;
    bool hasXbps = (activePkg.source == PkgSource::XBPS);
    if (hasXbps) xbpsVariant = activePkg;

    for (const auto &pair : m_catalogMap) {
        const auto &other = pair.second;
        if (other.source == activePkg.source) continue;

        if (isSameApplication(activePkg, other)) {
            std::string k = sourceKey(other.source) + ":" + other.id;
            if (seenKeys.find(k) == seenKeys.end()) {
                seenKeys.insert(k);
                variants.push_back(other);
                if (other.source == PkgSource::XBPS) {
                    hasXbps = true;
                    xbpsVariant = other;
                }
            }
        }
    }

    if (Config::instance().sourceEnabled() && activePkg.source != PkgSource::Source && hasXbps) {
        bool alreadyHasSource = false;
        for (const auto &v : variants) {
            if (v.source == PkgSource::Source) {
                alreadyHasSource = true;
                break;
            }
        }
        if (!alreadyHasSource) {
            Package srcVariant = xbpsVariant;
            srcVariant.source = PkgSource::Source;
            srcVariant.downloadSize.clear();
            srcVariant.installedSize.clear();
            srcVariant.maintainer.clear();
            srcVariant.repository.clear();
            std::string k = sourceKey(srcVariant.source) + ":" + srcVariant.id;
            if (seenKeys.find(k) == seenKeys.end()) {
                seenKeys.insert(k);
                variants.push_back(srcVariant);
            }
        }
    }

    DetailsView::setPackage(m_pageDetails, activePkg, variants);
    switchPage(4);

    auto alive = m_alive;
    auto requestDetails = [this, alive](const Package &target) {
        if (target.source == PkgSource::Source) return;
        for (auto &b : m_backends) {
            if (b->source() == target.source) {
                b->getDetails(target.id, [this, alive, target](const Package &detailed) {
                    if (!alive || !*alive) return;
                    Package merged = detailed;
                    if (merged.name.empty() || merged.name == merged.id) merged.name = target.name;
                    if (merged.summary.empty() || merged.summary == "Aplicación Flatpak") {
                        merged.summary = target.summary.empty() ? "Aplicación Flatpak" : target.summary;
                    }
                    if (merged.description.empty() || merged.description == "Aplicación empaquetada con Flatpak en formato sandbox desde Flathub.") {
                        merged.description = target.description.empty() ? "Aplicación empaquetada con Flatpak en formato sandbox desde Flathub." : target.description;
                    }
                    if (merged.iconPath.empty()) merged.iconPath = target.iconPath;
                    if (merged.categories.empty()) merged.categories = target.categories;
                    if (merged.license.empty()) merged.license = target.license;
                    if (merged.homepage.empty()) merged.homepage = target.homepage;
                    if (merged.maintainer.empty()) merged.maintainer = target.maintainer;
                    if (merged.downloadSize.empty()) merged.downloadSize = target.downloadSize;
                    if (merged.installedSize.empty()) merged.installedSize = target.installedSize;
                    if (merged.version.empty()) merged.version = target.version;

                    std::string key = sourceKey(merged.source) + ":" + merged.id;
                    m_catalogMap[key] = merged;
                    DetailsView::updatePackageDetails(m_pageDetails, merged);
                });
                break;
            }
        }
    };

    requestDetails(activePkg);
    for (const auto &v : variants) {
        requestDetails(v);
    }
}

void MainWindow::proceedUninstall(const Package &pkg) {
    Transaction::uninstall(pkg,
        [this, pkg](bool ok, const std::string &/*msg*/) {
            if (ok) {
                DetailsView::resetActionState(m_pageDetails);
                DetailsView::updatePackageState(m_pageDetails, pkg.id, pkg.source, false);
                std::string key = sourceKey(pkg.source) + ":" + pkg.id;
                if (m_catalogMap.find(key) != m_catalogMap.end()) {
                    m_catalogMap[key].state = PkgState::NotInstalled;
                }
                if (pkg.source == PkgSource::XBPS) {
                    DetailsView::updatePackageState(m_pageDetails, pkg.id, PkgSource::Source, false);
                    std::string srcKey = sourceKey(PkgSource::Source) + ":" + pkg.id;
                    if (m_catalogMap.find(srcKey) != m_catalogMap.end()) {
                        m_catalogMap[srcKey].state = PkgState::NotInstalled;
                    }
                } else if (pkg.source == PkgSource::Source) {
                    DetailsView::updatePackageState(m_pageDetails, pkg.id, PkgSource::XBPS, false);
                    std::string xbpsKey = sourceKey(PkgSource::XBPS) + ":" + pkg.id;
                    if (m_catalogMap.find(xbpsKey) != m_catalogMap.end()) {
                        m_catalogMap[xbpsKey].state = PkgState::NotInstalled;
                    }
                }
                loadAllCatalogs();
            } else {
                DetailsView::setActionError(m_pageDetails);
                loadAllCatalogs();
            }
        }
    );
}

void MainWindow::onAppCardAction(const Package &pkg) {
    if (Transaction::isBusy()) {
        return;
    }

    if (pkg.isInstalled()) {
        std::string displayName = pkg.name.empty() ? pkg.id : pkg.name;
        if (!displayName.empty() && displayName[0] >= 'a' && displayName[0] <= 'z') {
            displayName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(displayName[0])));
        }

        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(m_window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_NONE,
            "¿Deseas desinstalar %s?",
            displayName.c_str()
        );

        std::string secText = "El paquete '" + pkg.id + "' (" + sourceLabel(pkg.source) + ") y sus dependencias huérfanas serán eliminados del sistema.";
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", secText.c_str());

        GtkWidget *btnCancel = gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancelar", GTK_RESPONSE_CANCEL);
        GtkWidget *btnUninst = gtk_dialog_add_button(GTK_DIALOG(dlg), "Desinstalar", GTK_RESPONSE_ACCEPT);
        if (btnCancel) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btnCancel), "neko-btn-secondary");
        }
        if (btnUninst) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btnUninst), "neko-btn-danger");
        }

        struct ConfirmData {
            MainWindow *win;
            Package p;
        };
        auto *cd = new ConfirmData{this, pkg};

        g_signal_connect_data(dlg, "response", G_CALLBACK(+[](GtkDialog *d, gint response, gpointer data) {
            auto *c = static_cast<ConfirmData*>(data);
            gtk_widget_destroy(GTK_WIDGET(d));
            if (response == GTK_RESPONSE_ACCEPT) {
                c->win->proceedUninstall(c->p);
            } else {
                DetailsView::resetActionState(c->win->m_pageDetails);
                ManageView::setInstalledPackages(c->win->m_pageManage, c->win->m_installedList);
            }
        }), cd, [](gpointer d, GClosure*) { delete static_cast<ConfirmData*>(d); }, (GConnectFlags)0);

        gtk_widget_show_all(dlg);
        return;
    } else {
        Transaction::install(pkg,
            [this, pkg](bool ok, const std::string &/*msg*/) {
                if (ok) {
                    DetailsView::resetActionState(m_pageDetails);
                    DetailsView::updatePackageState(m_pageDetails, pkg.id, pkg.source, true);
                    std::string key = sourceKey(pkg.source) + ":" + pkg.id;
                    if (m_catalogMap.find(key) != m_catalogMap.end()) {
                        m_catalogMap[key].state = PkgState::Installed;
                    }
                    if (pkg.source == PkgSource::XBPS) {
                        DetailsView::updatePackageState(m_pageDetails, pkg.id, PkgSource::Source, true);
                        std::string srcKey = sourceKey(PkgSource::Source) + ":" + pkg.id;
                        if (m_catalogMap.find(srcKey) != m_catalogMap.end()) {
                            m_catalogMap[srcKey].state = PkgState::Installed;
                        }
                    } else if (pkg.source == PkgSource::Source) {
                        DetailsView::updatePackageState(m_pageDetails, pkg.id, PkgSource::XBPS, true);
                        std::string xbpsKey = sourceKey(PkgSource::XBPS) + ":" + pkg.id;
                        if (m_catalogMap.find(xbpsKey) != m_catalogMap.end()) {
                            m_catalogMap[xbpsKey].state = PkgState::Installed;
                        }
                    }
                    loadAllCatalogs();
                } else {
                    DetailsView::setActionError(m_pageDetails);
                    loadAllCatalogs();
                }
            }
        );
    }
}

void MainWindow::onBackendsChanged() {
    loadAllCatalogs();
}

void MainWindow::testShowTerminal(bool openDrawer) {
    if (m_bottomBar) {
        m_bottomBar->showTestState();
    }
    if (m_floatingTerminal) {
        m_floatingTerminal->toggle(openDrawer);
        m_floatingTerminal->appendLog(">>> [Neko-Apps] Iniciando: Instalando Firefox (XBPS)", false);
        m_floatingTerminal->appendLog(">>> Ejecutando: xbps-install -y firefox", false);
        m_floatingTerminal->appendLog("[*] Updating repository `https://repo-fastly.voidlinux.org/current/x86_64-repodata' ...", false);
        m_floatingTerminal->appendLog("firefox-128.3.0_1: verifying status ...", false);
        m_floatingTerminal->appendLog("firefox-128.3.0_1: installed successfully.", false);
    }
}

void MainWindow::testShowDetails() {
    Package pkg;
    pkg.id = "firefox";
    pkg.name = "Firefox";
    pkg.summary = "Navegador web libre y de código abierto desarrollado por Mozilla.";
    pkg.description = "Mozilla Firefox es un navegador web libre y de código abierto desarrollado para Linux, Android, iOS, macOS y Microsoft Windows.\n\nIncluye navegación por pestañas, corrector ortográfico, búsqueda progresiva, marcadores dinámicos, un administrador de descargas y navegación privada.";
    pkg.version = "128.3.0_1";
    pkg.license = "MPL-2.0";
    pkg.source = PkgSource::XBPS;
    pkg.state = PkgState::NotInstalled;
    pkg.installedVersion = "";
    onAppCardClicked(pkg);
}

void MainWindow::testShowUpdates() {
    switchPage(2);
}

void MainWindow::testShowManage() {
    switchPage(1);
}

void MainWindow::testShowSettings() {
    switchPage(3);
}

void MainWindow::testShowPostInstall() {
    switchPage(5);
}

void MainWindow::testShowRepos() {
    switchPage(6);
}
