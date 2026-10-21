#pragma once

#include <gtk/gtk.h>
#include "Package.h"
#include "Backend.h"
#include "FloatingTerminal.h"
#include "BottomProgressBar.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

class MainWindow {
public:
    explicit MainWindow(bool postInstallMode = false);
    ~MainWindow();

    void show();
    GtkWidget* window() const { return m_window; }
    void loadAllCatalogs();
    void testShowTerminal(bool openDrawer = false);
    void testShowDetails();
    void testShowUpdates();
    void testShowManage();
    void testShowSettings();
    void testShowPostInstall();
    void testShowRepos();

private:
    void initUi();
    void initSidebar();
    void initTopBar();
    void initCatalogPage();
    void filterCatalog();
    void switchPage(int pageIndex);
    void goBack();

    void onSearchChanged(const std::string &query);
    void onCategoryClicked(const std::string &category);
    void onAppCardClicked(const Package &pkg);
    void onAppCardAction(const Package &pkg);
    void onBackendsChanged();

    // GTK Widgets
    GtkWidget *m_window = nullptr;
    GtkWidget *m_sidebarBox = nullptr;
    GtkWidget *m_navBtnCatalog = nullptr;
    GtkWidget *m_navBtnManage = nullptr;
    GtkWidget *m_navBtnUpdates = nullptr;
    GtkWidget *m_navBtnPostInstall = nullptr;
    GtkWidget *m_navBtnRepos = nullptr;

    GtkWidget *m_topBar = nullptr;
    GtkWidget *m_btnBack = nullptr;
    GtkWidget *m_searchEntry = nullptr;
    GtkWidget *m_btnMenu = nullptr;
    GtkWidget *m_stack = nullptr;

    // Catalog page widgets
    GtkWidget *m_categoriesGridBox = nullptr;
    GtkWidget *m_btnToggleCat = nullptr;
    bool m_categoriesExpanded = true;
    std::unordered_map<std::string, GtkWidget*> m_categoryButtons;
    GtkWidget *m_lblSearchHeader = nullptr;
    GtkWidget *m_lblCatalogStatus = nullptr;
    GtkWidget *m_spinnerCatalog = nullptr;
    GtkWidget *m_flowBoxCards = nullptr;
    GtkWidget *m_btnLoadMoreCatalog = nullptr;

    int m_catalogLimit = 60;
    PackageList m_filteredList;
    void renderCatalogCards(bool appendOnly = false);

    // Pages
    GtkWidget *m_pageCatalog = nullptr;
    GtkWidget *m_pageManage = nullptr;
    GtkWidget *m_pageUpdates = nullptr;
    GtkWidget *m_pageSettings = nullptr;
    GtkWidget *m_pageDetails = nullptr;
    GtkWidget *m_pagePostInstall = nullptr;
    GtkWidget *m_pageRepos = nullptr;

    bool m_postInstallMode = false;

    // Decoupled UI Components
    std::unique_ptr<FloatingTerminal> m_floatingTerminal;
    std::unique_ptr<BottomProgressBar> m_bottomBar;

    // State
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    int m_currentPage = 0;
    int m_previousPage = 0;
    uint64_t m_loadGeneration = 0;
    int m_pendingCatalogBackends = 0;
    int m_pendingUpdateBackends = 0;
    std::string m_selectedCategory;
    std::string m_searchQuery;
    guint m_searchDebounceId = 0;

    void onSearchInput(const std::string &query);
    void proceedUninstall(const Package &pkg);

    std::unordered_map<std::string, Package> m_catalogMap;
    PackageList m_fullCatalog;
    PackageList m_installedList;
    PackageList m_updatesList;

    std::vector<std::unique_ptr<Backend>> m_backends;
};
