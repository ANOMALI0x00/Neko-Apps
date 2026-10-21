# Neko-Apps 🐾
### Centro de aplicaciones y gestor de paquetes nativo para Neko Void Linux

**Neko-Apps** es el centro de aplicaciones y gestor gráfico de software para **Neko Void Linux**. Desarrollado nativamente en **C++17** con **GTK 3** bajo una **arquitectura modular desacoplada** y con el tema externo **Matecito-Dark** de estética pixel-art 16-bit.

---

## 🧩 Arquitectura Modular (Seccionada)

El proyecto está dividido en **6 módulos independientes** compilados como librerías estáticas separadas en `CMakeLists.txt`, más la capa de temas completamente externa al código C++:

```text
Neko-Apps/
├── CMakeLists.txt                           # Compilación modular por librerías estáticas
├── template                                 # Plantilla oficial xbps-src (build_style=cmake)
├── package-xbps.sh                          # Empaquetador directo a binario .xbps
├── data/
│   ├── theme/Matecito-Dark/                 # 🎨 SECCIÓN DE TEMA (100% separada del C++)
│   │   ├── neko-apps.css                    # Hoja de estilos y variables @define-color de la tienda
│   │   ├── palette.conf                     # Colores semánticos de la terminal (sin recompilar)
│   │   ├── gtk-3.0/                         # Tema base GTK 3
│   │   └── assets/pixel/                    # Bordes e imágenes pixel-art 16-bit
│   ├── icons/drivers/                       # Iconos de hardware y controladores
│   ├── neko-apps.png / neko-apps.svg        # Iconos oficiales de la aplicación
│   ├── neko-apps.desktop                    # Lanzador del sistema
│   └── org.nekovoid.NekoApps.policy         # Política Polkit
├── helper/
│   └── neko-apps-helper                     # 🔐 SECCIÓN HELPER ADMINISTRATIVO (Polkit)
└── src/
    ├── main.cpp                             # Punto de entrada y modos de prueba
    ├── core/                                # ⚙️ SECCIÓN 1: Motor Base (neko_core)
    │   ├── Package.h                        # Modelo de datos unificado y progreso
    │   ├── Config.h / Config.cpp            # Configuración persistente (config.ini y vouru)
    │   ├── Proc.h / Proc.cpp                # Subprocesos POSIX y despacho al hilo principal
    │   └── Transaction.h / Transaction.cpp  # Sesión root única persistente y cola de tareas
    ├── backends/                            # 📦 SECCIÓN 2: Orígenes de Software (neko_backends)
    │   ├── Backend.h                        # Interfaz común de backends
    │   ├── XbpsBackend.h / .cpp             # Backend nativo XBPS + compilador Vouru (xbps-src)
    │   ├── FlatpakBackend.h / .cpp          # Backend Flatpak + parser AppStream XML
    │   └── AppImageBackend.h / .cpp         # Backend de paquetes portables Soar / AppImage
    ├── theme/                               # 🖌️ SECCIÓN 3: Motor de Temas (neko_theme)
    │   ├── ThemeManager.h / .cpp            # Cargador dinámico de neko-apps.css y palette.conf
    │   └── PixelLeafAnimation.h / .cpp      # Animaciones pixel-art en Cairo
    └── ui/                                  # 🖥️ SECCIÓN 4-6: Interfaz Gráfica Desacoplada
        ├── MainWindow.h / MainWindow.cpp    # Coordinador principal de navegación y catálogo
        ├── components/                      # (neko_ui_components) Piezas reutilizables aisladas
        │   ├── NekoAppCard.h / .cpp         # Tarjeta de aplicación en el catálogo
        │   ├── FloatingTerminal.h / .cpp    # Ventana de Terminal de Operaciones flotante
        │   └── BottomProgressBar.h / .cpp   # Barra inferior interactiva de progreso
        ├── views/                           # (neko_ui_views) Pestañas independientes
        │   ├── DetailsView.h / .cpp         # Vista de detalles y selector de variantes
        │   ├── ManageView.h / .cpp          # Gestión de instalados y mantenimiento XBPS
        │   ├── UpdatesView.h / .cpp         # Centro de actualizaciones del sistema
        │   ├── SettingsView.h / .cpp        # Ajustes de orígenes y repositorios
        │   ├── PostInstallView.h / .cpp     # Detección de GPU e instalación de drivers
        │   └── ReposView.h / .cpp           # Selector de mirrors y test de latencia
        └── dialogs/                         # (neko_ui_dialogs) Ventanas emergentes
            ├── CustomRepoDialog.h / .cpp    # Gestor de repositorios Git para xbps-src
            └── WishListDialog.h / .cpp      # Solicitud de paquetes para Neko Void (CNR)
```

---

## 🎨 Cómo modificar o crear un Tema sin tocar el código

Toda la apariencia visual de **Neko-Apps** reside fuera del ejecutable C++ en `data/theme/Matecito-Dark/` (instalado en `/usr/share/neko-apps/theme/Matecito-Dark/`):
1. **`neko-apps.css`**: Contiene al inicio todas las variables `@define-color` (`@neko_bg_main`, `@neko_accent`, `@neko_danger`, etc.) y las reglas de cada componente. Cualquier cambio en este archivo se aplica al abrir la aplicación **sin necesidad de recompilar**.
2. **`palette.conf`**: Define los colores semánticos de la terminal flotante (`terminal.error`, `terminal.info`, `terminal.dim`).
3. **`assets/pixel/*.png`**: Contiene los bordes escalonados pixel-art de tarjetas, paneles y botones.

---

## 🚀 Compilación y Empaquetado

### Generar paquete `.xbps` listo para instalar:
```bash
./package-xbps.sh
sudo xbps-install --repository=$PWD -f -y neko-apps
```
