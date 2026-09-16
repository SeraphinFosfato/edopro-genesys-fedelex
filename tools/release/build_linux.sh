#!/usr/bin/env bash
# Reproducible Linux build of the ygoprodll client, from a clean checkout.
# Usage: tools/release/build_linux.sh [release|debug]
#
# Prerequisites (Arch package names): premake headers/libs come bundled via
# the pinned premake5 binary this script downloads itself; system deps needed:
# mesa glu freetype2 sqlite curl libevent libgit2 libssh2 fmt flac libvorbis
# libogg openal nlohmann-json zlib-ng-compat base-devel git

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CONFIG="${1:-release}"

cd "$REPO_ROOT"

if [[ ! -x ./premake5 ]]; then
    echo "Scarico premake5 v5.0.0-beta2 (pinnato, richiesto da questo progetto)..."
    ./travis/install-premake5.sh linux
    chmod +x ./premake5
fi

"$SCRIPT_DIR/setup_irrlicht.sh"

# Always start clean: premake's gmake2 output doesn't track compiler-flag
# changes as a relink/recompile trigger, only file mtimes - a stale obj/bin
# from a previous invocation can silently ship without flag changes applied
# (bit us twice already: the irrlicht include path, then the rpath flag).
rm -rf "$REPO_ROOT/build" "$REPO_ROOT/obj" "$REPO_ROOT/bin"

echo "Genero i Makefile..."
./premake5 gmake2 --no-core=true --sound=sfml --no-joystick=true

echo "Compilo (config=${CONFIG}_x64)..."
make -Cbuild -j"$(nproc)" config="${CONFIG}_x64" ygoprodll

echo "Fatto: bin/x64/${CONFIG}/ygoprodll"
