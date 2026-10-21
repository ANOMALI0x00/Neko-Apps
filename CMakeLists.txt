cmake_minimum_required(VERSION 3.16)
project(neko-apps VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# ==============================================================================
# Cabeceras e includes compartidos entre módulos
# ==============================================================================
add_library(neko_headers INTERFACE)
target_include_directories(neko_headers INTERFACE
    ${GTK3_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/views
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/dialogs
)
target_compile_options(neko_headers INTERFACE ${GTK3_CFLAGS_OTHER})

# ==============================================================================
# SECCIÓN 1: Core (Configuración, Subprocesos y Transacciones Root)
# ==============================================================================
add_library(neko_core STATIC
    src/core/Package.h
    src/core/Proc.h
    src/core/Proc.cpp
    src/core/Config.h
    src/core/Config.cpp
    src/core/Transaction.h
    src/core/Transaction.cpp
)
target_link_libraries(neko_core PUBLIC neko_headers ${GTK3_LIBRARIES} pthread)

# ==============================================================================
# SECCIÓN 2: Backends (XBPS/xbps-src, Flatpak, AppImage/Soar)
# ==============================================================================
add_library(neko_backends STATIC
    src/backends/Backend.h
    src/backends/XbpsBackend.h
    src/backends/XbpsBackend.cpp
    src/backends/FlatpakBackend.h
    src/backends/FlatpakBackend.cpp
    src/backends/AppImageBackend.h
    src/backends/AppImageBackend.cpp
)
target_link_libraries(neko_backends PUBLIC neko_core)

# ==============================================================================
# SECCIÓN 3: Theme (Cargador dinámico de temas CSS y animaciones Cairo)
# ==============================================================================
add_library(neko_theme STATIC
    src/theme/ThemeManager.h
    src/theme/ThemeManager.cpp
    src/theme/PixelLeafAnimation.h
    src/theme/PixelLeafAnimation.cpp
)
target_link_libraries(neko_theme PUBLIC neko_core)

# ==============================================================================
# SECCIÓN 4: UI Components (Tarjetas, Terminal Flotante y Barra de Progreso)
# ==============================================================================
add_library(neko_ui_components STATIC
    src/ui/components/NekoAppCard.h
    src/ui/components/NekoAppCard.cpp
    src/ui/components/FloatingTerminal.h
    src/ui/components/FloatingTerminal.cpp
    src/ui/components/BottomProgressBar.h
    src/ui/components/BottomProgressBar.cpp
)
target_link_libraries(neko_ui_components PUBLIC neko_core neko_theme)

# ==============================================================================
# SECCIÓN 5: UI Dialogs (Repositorios personalizados y Lista de Deseos)
# ==============================================================================
add_library(neko_ui_dialogs STATIC
    src/ui/dialogs/CustomRepoDialog.h
    src/ui/dialogs/CustomRepoDialog.cpp
    src/ui/dialogs/WishListDialog.h
    src/ui/dialogs/WishListDialog.cpp
    src/ui/dialogs/LocalPackageDialog.h
    src/ui/dialogs/LocalPackageDialog.cpp
)
target_link_libraries(neko_ui_dialogs PUBLIC neko_core neko_theme)

# ==============================================================================
# SECCIÓN 6: UI Views (Pestañas independientes de la aplicación)
# ==============================================================================
add_library(neko_ui_views STATIC
    src/ui/views/DetailsView.h
    src/ui/views/DetailsView.cpp
    src/ui/views/ManageView.h
    src/ui/views/ManageView.cpp
    src/ui/views/UpdatesView.h
    src/ui/views/UpdatesView.cpp
    src/ui/views/SettingsView.h
    src/ui/views/SettingsView.cpp
    src/ui/views/PostInstallView.h
    src/ui/views/PostInstallView.cpp
    src/ui/views/ReposView.h
    src/ui/views/ReposView.cpp
)
target_link_libraries(neko_ui_views PUBLIC
    neko_core
    neko_backends
    neko_theme
    neko_ui_components
    neko_ui_dialogs
)

# ==============================================================================
# EJECUTABLE PRINCIPAL: Neko-Apps
# ==============================================================================
add_executable(neko-apps
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_link_libraries(neko-apps PRIVATE
    neko_ui_views
    neko_ui_components
    neko_ui_dialogs
    neko_theme
    neko_backends
    neko_core
    ${GTK3_LIBRARIES}
    pthread
)

# ==============================================================================
# Instalación de Binarios, Temas y Recursos del Sistema
# ==============================================================================
install(TARGETS neko-apps DESTINATION bin)
install(PROGRAMS helper/neko-apps-helper DESTINATION libexec/neko-apps)
install(FILES data/neko-apps.desktop DESTINATION share/applications)
install(FILES data/org.nekovoid.NekoApps.policy DESTINATION share/polkit-1/actions)
install(FILES data/neko-apps.png DESTINATION share/pixmaps)
install(FILES data/neko-apps.svg DESTINATION share/icons/hicolor/scalable/apps)
install(DIRECTORY data/theme/Matecito-Dark DESTINATION share/neko-apps/theme)
install(DIRECTORY data/icons DESTINATION share/neko-apps)
install(FILES data/soar-catalog.tsv DESTINATION share/neko-apps)
