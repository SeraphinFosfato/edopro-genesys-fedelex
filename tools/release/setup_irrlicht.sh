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
    git clone --branch 1.9-custom --depth 1 https://github.com/edo9300/irrlicht1-8-4.git "$IRRLICHT_DIR"
fi

echo "Compilo Irrlicht (libreria statica)..."
make -C "$IRRLICHT_DIR/source/Irrlicht" -j"$(nproc)"

echo "Irrlicht pronto: $IRRLICHT_DIR/lib/Linux/libIrrlicht.a"
