# Neko-Apps

Centro de aplicaciones y gestor de paquetes para Neko Void Linux. Desarrollado en C++17 y GTK 3 con arquitectura modular y soporte para temas externos.

## Estructura del proyecto

```text
Neko-Apps/
├── CMakeLists.txt
├── template
├── package-xbps.sh
├── data/
│   ├── theme/Matecito-Dark/
│   │   ├── neko-apps.css
│   │   ├── palette.conf
│   │   ├── gtk-3.0/
│   │   └── assets/pixel/
│   ├── icons/drivers/
│   ├── neko-apps.png
│   ├── neko-apps.svg
│   ├── neko-apps.desktop
│   └── org.nekovoid.NekoApps.policy
├── helper/
│   └── neko-apps-helper
└── src/
    ├── main.cpp
    ├── core/
    │   ├── Package.h
    │   ├── Config.h / Config.cpp
    │   ├── Proc.h / Proc.cpp
    │   └── Transaction.h / Transaction.cpp
    ├── backends/
    │   ├── Backend.h
    │   ├── XbpsBackend.h / .cpp
    │   ├── FlatpakBackend.h / .cpp
    │   └── AppImageBackend.h / .cpp
    ├── theme/
    │   ├── ThemeManager.h / .cpp
    │   └── PixelLeafAnimation.h / .cpp
    └── ui/
        ├── MainWindow.h / MainWindow.cpp
        ├── components/
        │   ├── NekoAppCard.h / .cpp
        │   ├── FloatingTerminal.h / .cpp
        │   └── BottomProgressBar.h / .cpp
        ├── views/
        │   ├── DetailsView.h / .cpp
        │   ├── ManageView.h / .cpp
        │   ├── UpdatesView.h / .cpp
        │   ├── SettingsView.h / .cpp
        │   ├── PostInstallView.h / .cpp
        │   └── ReposView.h / .cpp
        └── dialogs/
            ├── CustomRepoDialog.h / .cpp
            └── WishListDialog.h / .cpp
```

## Temas

La configuracion visual se encuentra en `data/theme/Matecito-Dark/` (ruta de instalacion `/usr/share/neko-apps/theme/Matecito-Dark/`):

- `neko-apps.css`: Definicion de estilos GTK y variables de color (`@define-color`).
- `palette.conf`: Configuracion de colores para la terminal integrada.
- `assets/pixel/`: Recursos graficos e imagenes de interfaz.

## Compilacion e instalacion

### Requisitos

- `cmake`
- `pkg-config`
- `gtk+3-devel`
- `polkit`
- `xtools`
- `git`
- `curl`

### Compilacion manual

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Generacion del paquete XBPS

```bash
./package-xbps.sh
sudo xbps-install --repository=$PWD -f -y neko-apps
```
