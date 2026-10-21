#!/bin/sh
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "=== Compilando Neko-Apps ==="
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "=== Empaquetando archivos para .xbps ==="
STAGING="/tmp/neko-apps-pkg"
rm -rf "$STAGING"
mkdir -p "$STAGING"
rm -f build/install_manifest.txt
cmake --install build --prefix "$STAGING/usr"

echo "=== Generando paquete binario XBPS ==="
xbps-create -A x86_64 -n "neko-apps-2.0.0_1" \
    -s "Gestor gráfico nativo de paquetes para Neko Void Linux (C++/GTK3)" \
    -H "https://github.com/Neko-Void-Linux/neko-apps" \
    -l "GPL-3.0-or-later" \
    -m "Neko Void Linux <neko@nekovoid.org>" \
    -D "gtk+3>=3.24 polkit xtools git curl" \
    "$STAGING"

echo "=== Registrando índice local del repositorio ==="
xbps-rindex -a neko-apps-2.0.0_1.x86_64.xbps

rm -rf "$STAGING"
echo "=== ¡Listo! Paquete creado: neko-apps-2.0.0_1.x86_64.xbps ==="
