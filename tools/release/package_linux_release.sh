#!/usr/bin/env bash
# Builds the client and packages a self-contained, distro-portable Linux
# release tarball, ready to hand to someone who has never touched this repo.
#
# Usage: tools/release/package_linux_release.sh <runtime-dir> [version]
#   <runtime-dir>  an existing full EDOPro data install (e.g. a copy of
#                   edopro-bin's /opt/edopro) with your custom lflists/
#                   and config/ already in place.
#   [version]      tag for the output filename, default: date + short hash
#
# What makes it portable across distros (not just this machine):
#  - the client links against a few libraries whose SONAME churns a lot
#    between distros (fmt in particular) or that aren't always preinstalled
#    on a minimal desktop (git2, ssh2, libevent, the FLAC/vorbis/ogg/openal
#    sound stack). Those .so files are copied next to the binary, and the
#    binary carries an rpath of '$ORIGIN' (see premake5.lua) so it prefers
#    its own bundled copies over whatever (if anything) is on the target
#    system. Everything else linked (libc, libstdc++, curl, sqlite3,
#    freetype, X11/GL) is treated as a safe assumption on any Linux desktop.
#  - repositories/ (the git-cloned auto-update repos) is left out: EDOPro
#    reclones those itself on first launch, so shipping them would just be
#    dead weight (and the recipient gets a fresh pull instead of a stale one).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

RUNTIME_DIR="${1:?Usage: $0 <runtime-dir> [version]}"
VERSION="${2:-$(date +%Y%m%d)-$(git -C "$REPO_ROOT" rev-parse --short HEAD)}"
OUT_NAME="edopro-custom-linux-x64-${VERSION}"
STAGE="$REPO_ROOT/tools/release/.stage/${OUT_NAME}"
OUT_DIR="$REPO_ROOT/tools/release/out"

BUNDLE_LIBS=(
    libfmt.so.12
    libgit2.so.1.9
    libssh2.so.1
    libevent-2.1.so.7
    libevent_pthreads-2.1.so.7
    libFLAC.so.14
    libvorbisfile.so.3
    libvorbis.so.0
    libogg.so.0
    libopenal.so.1
)

echo "== 1/4: build =="
"$SCRIPT_DIR/build_linux.sh" release

echo "== 2/4: stage runtime =="
rm -rf "$STAGE"
mkdir -p "$STAGE"
rsync -a --exclude 'repositories' --exclude 'EDOPro.orig' --exclude 'EDOPro' \
    "$RUNTIME_DIR"/ "$STAGE"/

echo "== 3/4: strip and bundle libraries =="
cp "$REPO_ROOT/bin/x64/release/ygoprodll" "$STAGE/EDOPro"
strip --strip-unneeded "$STAGE/EDOPro"
chmod +x "$STAGE/EDOPro"

mkdir -p "$STAGE/lib"
for soname in "${BUNDLE_LIBS[@]}"; do
    real="$(ldd "$REPO_ROOT/bin/x64/release/ygoprodll" | awk -v s="$soname" '$1==s{print $3}')"
    if [[ -z "$real" || ! -f "$real" ]]; then
        echo "ATTENZIONE: impossibile risolvere $soname sul sistema, salto (il binario potrebbe non partire su un'altra macchina)" >&2
        continue
    fi
    cp -L "$real" "$STAGE/lib/$soname"
done
# put the bundled libs where the '$ORIGIN' rpath expects them: next to the binary
mv "$STAGE"/lib/* "$STAGE"/
rmdir "$STAGE/lib"

echo "== 4/4: tar it up =="
mkdir -p "$OUT_DIR"
tar -C "$(dirname "$STAGE")" -czf "$OUT_DIR/${OUT_NAME}.tar.gz" "$(basename "$STAGE")"
rm -rf "$REPO_ROOT/tools/release/.stage"

echo "Release pronta: $OUT_DIR/${OUT_NAME}.tar.gz ($(du -h "$OUT_DIR/${OUT_NAME}.tar.gz" | cut -f1))"
