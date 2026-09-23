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

# Modello di thread: serve POSIX. Su Debian/Ubuntu il nome senza suffisso
# punta alla variante win32, e con quella il link di gframe finisce con ~900
# riferimenti non risolti (pthread_mutex_lock, std::__get_once_call...):
# premake mette -lpthread nella riga di link e la cache vcpkg e' costruita
# contro winpthreads. Le due varianti stanno nello stesso pacchetto, quindi
# si sceglie, non si installa. Su Arch il nome unico e' gia' posix.
if [[ -z "${MINGW_CXX:-}" ]]; then
    if command -v i686-w64-mingw32-g++-posix >/dev/null; then
        MINGW_CXX=i686-w64-mingw32-g++-posix
    else
        MINGW_CXX=i686-w64-mingw32-g++
    fi
fi
if [[ -z "${MINGW_CC:-}" ]]; then
    if command -v i686-w64-mingw32-gcc-posix >/dev/null; then
        MINGW_CC=i686-w64-mingw32-gcc-posix
    else
        MINGW_CC=i686-w64-mingw32-gcc
    fi
fi
export MINGW_CXX MINGW_CC

if ! "$MINGW_CXX" -v 2>&1 | grep -q 'Thread model: posix'; then
    echo "ERRORE: '$MINGW_CXX' non usa il modello di thread POSIX." >&2
    echo "  Modello: $("$MINGW_CXX" -v 2>&1 | grep 'Thread model:')" >&2
    echo "  Con il modello win32 il link di gframe fallisce su centinaia di" >&2
    echo "  simboli pthread. Installa la variante -posix del toolchain." >&2
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

# D176 — maiuscole risolte al confine e non nei sorgenti, su entrambe le
# superfici: gli #include (<Windows.h> contro windows.h) e i nomi delle
# librerie che premake mette nella riga di link (-lIphlpapi contro
# libiphlpapi.a). Lo shim si rigenera a ogni build — e' scandito, non scritto
# a mano — e va in TESTA a include path e library path. Non ombreggia niente:
# genera solo nomi che la toolchain non espone affatto con quella
# capitalizzazione.
# Gira DOPO premake perche' i nomi delle librerie si leggono dai makefile che
# premake ha appena generato.
SHIM_DIR="$REPO_ROOT/build/mingw-case-shim"
rm -rf "$SHIM_DIR"
"$SCRIPT_DIR/mingw_case_shim.sh" "$SHIM_DIR" \
    --sources "$REPO_ROOT/gframe" "$REPO_ROOT/sfAudio" "$REPO_ROOT/ocgcore" \
              "$REPO_ROOT/overwrites" "$REPO_ROOT/overwrites-mingw" "$REPO_ROOT/irrlicht" \
    --makefiles "$REPO_ROOT/build/ygopro.make" \
    --lib-dirs "$VCPKG_ROOT/installed/x86-mingw-static/lib"
export CPATH="$SHIM_DIR/include${CPATH:+:$CPATH}"
# Per le librerie NON si usa LIBRARY_PATH: gcc lo onora solo quando e'
# configurato come compilatore nativo, e in cross-build lo ignora in silenzio
# (provato: gli alias c'erano, il linker continuava a non trovarli). Si passa
# un -L a make, che premake mette in testa ad ALL_LDFLAGS.
SHIM_LDFLAGS="-L$SHIM_DIR/lib"

# D177 — stesso confine, stesso rimedio, stesso posto. premake cabla il nome
# `windres` nella recipe delle risorse (build/ygopro.make), non `$(RESCOMP)`:
# in cross-build esiste solo `i686-w64-mingw32-windres`, e senza questo la
# compilazione di gframe/ygopro.rc esce con "Error 127" (comando non trovato).
# NON si patcha premake5.lua per emettere $(RESCOMP), che a monte sarebbe la
# correzione piu' pulita: quel generatore produce anche il job Linux, che e'
# verde ed e' l'unico binario che stiamo distribuendo.
#
# Non e' un semplice collegamento: windres, per espandere gli #include di un
# .rc, lancia il preprocessore del compilatore *di sistema* (`gcc -E`), cioe'
# quello che compila per Linux. Con quello, gframe/ygopro.rc trova l'alias
# Winuser.h generato sopra ma non il winuser.h vero, che sta nel sysroot
# MinGW e non in quello di sistema. Quindi il PATH espone un involucro che
# gli passa il preprocessore giusto.
TOOL_SHIM_DIR="$REPO_ROOT/build/mingw-tool-shim"
mkdir -p "$TOOL_SHIM_DIR"
# rm -f prima della scrittura, non per pulizia: se qui e' rimasto un
# collegamento simbolico da una versione precedente di questo script, `cat >`
# scrive ATTRAVERSO il collegamento e sovrascrive il binario di sistema a cui
# punta. E' successo davvero mentre si scriveva questo passo.
rm -f "$TOOL_SHIM_DIR/windres"
cat > "$TOOL_SHIM_DIR/windres" <<'EOF'
#!/bin/sh
# involucro generato da tools/release/build_windows.sh (D177)
exec i686-w64-mingw32-windres \
    --preprocessor=i686-w64-mingw32-gcc \
    --preprocessor-arg=-E \
    --preprocessor-arg=-xc \
    --preprocessor-arg=-DRC_INVOKED \
    "$@"
EOF
chmod +x "$TOOL_SHIM_DIR/windres"
export PATH="$TOOL_SHIM_DIR:$PATH"

# D178 — ci si allinea a cio' che premake genera DAVVERO. Con --os=windows
# --architecture=x86 la configurazione si chiama `release_win32` (non
# `release_x86`, che qui c'e' stato per mesi e faceva abortire make alla prima
# invocazione) e l'eseguibile esce in bin/<config>/, non in bin/x86/<config>/.
MAKE_CONFIG="${CONFIG}_win32"
EXE="$REPO_ROOT/bin/${CONFIG}/ygopro.exe"

echo "Compilo (config=${MAKE_CONFIG}, cross gcc mingw)..."
make -Cbuild -j"$(nproc)" config="$MAKE_CONFIG" \
    CC="$MINGW_CC" CXX="$MINGW_CXX" \
    AR=i686-w64-mingw32-ar \
    LDFLAGS="$SHIM_LDFLAGS" \
    ygopro

# D178, la meta' che conta: un `make` uscito con 0 non e' una prova che il
# binario esista. La verifica sta in uno script a parte apposta per poterla
# far fallire a comando — il perche' e' scritto li' dentro. E' la stessa che
# usa il percorso Windows (build_windows.ps1): una sola, in Python, perche' su
# un runner Windows `file` non esiste e due controlli diversi per la stessa
# cosa sono due cose che possono divergere.
python3 "$SCRIPT_DIR/verify_windows_exe.py" "$EXE"

echo "Fatto: bin/${CONFIG}/ygopro.exe"
