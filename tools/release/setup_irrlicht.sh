#!/usr/bin/env bash
# Clones and builds edo9300's patched Irrlicht fork (branch 1.9-custom), which
# this project's gframe code requires on Linux instead of a distro's vanilla
# Irrlicht package. Idempotent: skips work that's already done.
#
# Result: a sibling directory ../irrlicht-custom containing lib/Linux/libIrrlicht.a
# and include/, which premake5.lua picks up automatically (or point at it
# explicitly with --irrlicht-root=<path>).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IRRLICHT_DIR="${1:-$REPO_ROOT/../irrlicht-custom}"

if [[ -f "$IRRLICHT_DIR/lib/Linux/libIrrlicht.a" ]]; then
    echo "Irrlicht gia' compilato in $IRRLICHT_DIR, nulla da fare."
    exit 0
fi

if [[ ! -d "$IRRLICHT_DIR" ]]; then
    echo "Clono edo9300/irrlicht1-8-4 (branch 1.9-custom) in $IRRLICHT_DIR..."
    # Fissato a uno SHA (Audit 2026-10-11 #19): il ramo 1.9-custom di edo9300/irrlicht1-8-4 e' mobile e di terzi.
    # Aggiornarlo qui, in build_windows.sh/setup_irrlicht.sh e nel passo "Commit di Irrlicht fissato" di release.yml.
    git init -q "$IRRLICHT_DIR"
    git -C "$IRRLICHT_DIR" fetch -q --depth 1 https://github.com/edo9300/irrlicht1-8-4.git 7071bec37b1aa1a663f662dc1372f9e476aae009
    git -C "$IRRLICHT_DIR" checkout -q FETCH_HEAD
fi

echo "Compilo Irrlicht (libreria statica)..."
make -C "$IRRLICHT_DIR/source/Irrlicht" -j"$(nproc)"

echo "Irrlicht pronto: $IRRLICHT_DIR/lib/Linux/libIrrlicht.a"
