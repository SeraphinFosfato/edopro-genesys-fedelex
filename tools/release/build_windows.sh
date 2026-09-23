#!/usr/bin/env bash
# Cross-compiles the ygopro.exe client for Windows (x86, core statically
# linked in - no separate ocgcore.dll needed) from Linux, using mingw-w64
# and edo9300's prebuilt vcpkg cache (avoids building every dependency from
# source, which is what makes cross-compiling vcpkg ports painful).
#
# Prerequisites (Arch package name): mingw-w64-gcc
#
# NOTE: this produces a .exe that has never been run on real Windows - it's
# built the same way upstream's CI does it, but you'll need to test it
# yourself (a real machine, or Wine) before trusting it.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IRRLICHT_SRC="${IRRLICHT_SRC:-$REPO_ROOT/../irrlicht-custom}"
VCPKG_ROOT="${VCPKG_ROOT:-$REPO_ROOT/../vcpkg-cache}"
CONFIG="${1:-release}"

cd "$REPO_ROOT"

if ! command -v i686-w64-mingw32-g++ >/dev/null; then
    echo "Manca il toolchain MinGW: installa il pacchetto 'mingw-w64-gcc'." >&2
    exit 1
fi

if [[ ! -x ./premake5 ]]; then
    echo "Scarico premake5 v5.0.0-beta2..."
    ./travis/install-premake5.sh linux
    chmod +x ./premake5
fi

if [[ ! -d "$IRRLICHT_SRC" ]]; then
    echo "Clono edo9300/irrlicht1-8-4 (1.9-custom)..."
    git clone --branch 1.9-custom --depth 1 https://github.com/edo9300/irrlicht1-8-4.git "$IRRLICHT_SRC"
fi
if [[ ! -d irrlicht/include || ! -d irrlicht/src ]]; then
    echo "Popolo irrlicht/include e irrlicht/src per il subproject Windows..."
    rm -rf irrlicht/include irrlicht/src
    cp -r "$IRRLICHT_SRC/include" irrlicht/include
    cp -r "$IRRLICHT_SRC/source/Irrlicht" irrlicht/src
    # built against vcpkg-provided versions of these instead (same as upstream CI)
    rm -rf irrlicht/src/bzip2 irrlicht/src/jpeglib irrlicht/src/libpng irrlicht/src/zlib
fi

if [[ ! -d "$VCPKG_ROOT/installed/x86-mingw-static" ]]; then
    echo "Scarico la cache vcpkg precompilata di edo9300 (x86-windows-mingw-static, ~40MB)..."
    mkdir -p "$VCPKG_ROOT"
    curl -sL "https://github.com/edo9300/edopro-vcpkg-cache/releases/latest/download/installed_x86-windows-mingw-static.zip" -o /tmp/vcpkg_mingw_cache.zip
    unzip -oq /tmp/vcpkg_mingw_cache.zip -d "$VCPKG_ROOT"
    rm -f /tmp/vcpkg_mingw_cache.zip
fi

rm -rf "$REPO_ROOT/build" "$REPO_ROOT/obj" "$REPO_ROOT/bin"

# Nota per chi ritocca questo script dopo aver visto la correzione in
# build_linux.sh (--irrlicht-root assoluto, contro un path relativo che
# sbaglia su un checkout poco profondo): verificato in gframe/premake5.lua
# che tutto il blocco che legge --irrlicht-root e' dentro
# `if not os.istarget("windows")` (righe 221-235) — per il target Windows
# quell'opzione non viene letta affatto, Irrlicht qui arriva da sorgente
# copiato in irrlicht/include e irrlicht/src (sopra), non da --irrlicht-root.
# Il bug non si applica a questo script: non serve la stessa toppa.
echo "Genero i Makefile (target Windows x86, MinGW)..."
./premake5 gmake2 --os=windows --architecture=x86 --no-direct3d \
    --vcpkg-root="$VCPKG_ROOT" --sound=sfml --no-joystick=true

echo "Compilo (config=${CONFIG}_x86, cross gcc mingw)..."
make -Cbuild -j"$(nproc)" config="${CONFIG}_x86" \
    CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++ \
    AR=i686-w64-mingw32-ar \
    ygopro

echo "Fatto: bin/x86/${CONFIG}/ygopro.exe"
