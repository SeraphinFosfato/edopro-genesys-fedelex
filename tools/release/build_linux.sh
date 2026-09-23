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

# --irrlicht-root esplicito, con path assoluto gia' risolto (niente "..").
# gframe/premake5.lua, senza questa opzione, ricava il path da solo come
# INVOCATION_CWD .. "/../irrlicht-custom" (stringa con ".." non normalizzata)
# e lo passa cosi' com'e' al calcolo del path relativo di premake5 verso
# build/. Quando REPO_ROOT e' vicino alla radice del filesystem (un runner
# CI che fa checkout in un path corto tipo /home/runner/work/.../repo, o un
# container) quel calcolo sbaglia silenziosamente: genera un include tipo
# "../gframe/irrlicht-custom/include" invece di "../../irrlicht-custom/include",
# la libreria non si trova e la build fallisce su IrrCompileConfig.h — ma
# solo li', non su path piu' profondi come una checkout locale annidata in
# tanti livelli di cartelle, dove lo stesso calcolo per caso riesce. E' la
# terza voce della stessa categoria gia' citata nel commento sopra (path
# irrlicht, poi rpath): niente qui dipende davvero dalla posizione di
# INVOCATION_CWD, quindi si passa un path assoluto gia' risolto e si toglie
# il problema alla radice invece di sperare che il calcolo relativo funzioni.
IRRLICHT_ROOT="$(cd "$REPO_ROOT/../irrlicht-custom" && pwd)"

echo "Genero i Makefile..."
./premake5 gmake2 --no-core=true --sound=sfml --no-joystick=true --irrlicht-root="$IRRLICHT_ROOT"

echo "Compilo (config=${CONFIG}_x64)..."
make -Cbuild -j"$(nproc)" config="${CONFIG}_x64" ygoprodll

echo "Fatto: bin/x64/${CONFIG}/ygoprodll"
