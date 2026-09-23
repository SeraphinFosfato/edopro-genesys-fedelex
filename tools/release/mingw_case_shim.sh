#!/usr/bin/env bash
# Genera gli alias di capitalizzazione fra cio' che il progetto scrive e cio'
# che la toolchain MinGW espone (D176). Due superfici, un meccanismo solo:
#
#   header   `#include <Windows.h>`  ->  il sysroot ha solo `windows.h`
#   libreria `-lIphlpapi`            ->  il sysroot ha solo `libiphlpapi.a`
#
# Uso:
#   mingw_case_shim.sh <cartella_shim> \
#       --sources <dir>... [--makefiles <file>...] [--lib-dirs <dir>...]
#
# Produce <cartella_shim>/include e <cartella_shim>/lib, da mettere in testa
# rispettivamente all'include path e al library path.
#
# Il problema: i sorgenti e le direttive di link di questo progetto sono nati
# su Windows, dove il filesystem ignora le maiuscole, e li' sono CORRETTI —
# si compilano tuttora con MSVC. Su Linux, dove il filesystem le distingue,
# gli stessi nomi non si risolvono.
#
# Il difetto non vive nel codice: vive nel CONFINE fra un filesystem che
# ignora le maiuscole e uno che no. Si ripara li', non nei sorgenti — che in
# buona parte sono codice di terzi incluso nel repo (sfAudio, Irrlicht) e ogni
# riga cambiata li' diventa un conflitto a ogni allineamento con l'origine.
#
# Il come: si scandisce, non si elenca. Una lista scritta a mano sarebbe il
# cerotto per istanza (CLAUDE.md §6.5) e si romperebbe alla prossima maiuscola
# diversa — cosa gia' successa una volta durante la scrittura di questo passo,
# quando gli header sembravano finiti e sono saltate fuori tre librerie.
#
# Criterio d'accettazione, non rifinitura: questo script STAMPA cosa ha
# generato e fallisce se non genera nessun alias di header. Uno shim
# silenzioso e' il difetto di domani — se un giorno la scansione si rompe, il
# fallimento dev'essere leggibile subito, non un errore di compilazione che
# sembra altro. (E' gia' servito: con `find -type f` l'indice del sysroot
# restava vuoto, perche' su Debian/Ubuntu quegli header sono simbolici.)

set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "uso: $(basename "$0") <cartella_shim> --sources <dir>... [--makefiles <file>...] [--lib-dirs <dir>...]" >&2
    exit 2
fi

SHIM_DIR="$1"
shift

SOURCES=()
MAKEFILES=()
EXTRA_LIB_DIRS=()
mode=""
for arg in "$@"; do
    case "$arg" in
        --sources)   mode=sources ;;
        --makefiles) mode=makefiles ;;
        --lib-dirs)  mode=libdirs ;;
        *)
            case "$mode" in
                sources)   SOURCES+=("$arg") ;;
                makefiles) MAKEFILES+=("$arg") ;;
                libdirs)   EXTRA_LIB_DIRS+=("$arg") ;;
                *) echo "argomento senza sezione: $arg" >&2; exit 2 ;;
            esac
            ;;
    esac
done

if [[ ${#SOURCES[@]} -eq 0 ]]; then
    echo "shim MinGW: nessuna radice di sorgenti (--sources)." >&2
    exit 2
fi

CXX="${MINGW_CXX:-i686-w64-mingw32-g++}"
CC="${MINGW_CC:-i686-w64-mingw32-gcc}"

if ! command -v "$CXX" >/dev/null; then
    echo "shim MinGW: manca il compilatore '$CXX'." >&2
    exit 1
fi

INC_SHIM="$SHIM_DIR/include"
LIB_SHIM="$SHIM_DIR/lib"
mkdir -p "$INC_SHIM" "$LIB_SHIM"

# ---------------------------------------------------------------- header ---

# Le cartelle di include del cross-compilatore, chieste al compilatore stesso
# invece di cablare /usr/i686-w64-mingw32/include: cambia fra distribuzioni e
# fra versioni del pacchetto.
mapfile -t INC_DIRS < <(
    echo | "$CXX" -x c++ -E -Wp,-v - 2>&1 >/dev/null \
        | sed -n 's|^ \(/.*\)$|\1|p' \
        | while IFS= read -r d; do [[ -d "$d" ]] && (cd "$d" && pwd); done
)

if [[ ${#INC_DIRS[@]} -eq 0 ]]; then
    echo "shim MinGW: nessuna cartella di include trovata per '$CXX'." >&2
    exit 1
fi

# Indice: nome-relativo-in-minuscolo -> nome relativo vero.
declare -A HDR_BY_LOWER=()
for d in "${INC_DIRS[@]}"; do
    while IFS= read -r rel; do
        key="${rel,,}"
        [[ -n "${HDR_BY_LOWER[$key]+x}" ]] || HDR_BY_LOWER["$key"]="$rel"
        # `! -type d` e non `-type f`: su Debian/Ubuntu gli header di
        # /usr/i686-w64-mingw32/include sono SIMBOLICI verso
        # /usr/share/mingw-w64/include, e con -type f ne sparivano tutti
        # tranne sei — cioe' l'indice restava vuoto e lo shim generava zero
        # alias. E' precisamente il caso che il fallimento rumoroso esiste
        # per intercettare.
    done < <(cd "$d" && find . \( -name '*.h' -o -name '*.hpp' \) ! -type d | sed 's|^\./||')
done

# Gli `#include` scritti nei sorgenti. Anche i .rc e i .inl: la compilazione
# delle risorse passa dal preprocessore e ygopro.rc include <Winuser.h>.
mapfile -t INCLUDED < <(
    grep -rhoE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]+[">]' \
        --include='*.c' --include='*.cpp' --include='*.cc' \
        --include='*.h' --include='*.hpp' --include='*.inl' --include='*.rc' \
        "${SOURCES[@]}" 2>/dev/null \
        | sed -E 's|^[^<"]*[<"]||; s|[">]$||' \
        | sort -u
)

exists_in_sysroot() {
    local name="$1" d
    for d in "${INC_DIRS[@]}"; do
        [[ -e "$d/$name" ]] && return 0
    done
    return 1
}

# Un header del progetto non va mai ombreggiato da uno shim: se esiste un file
# con quel nome dentro le radici scandite, quell'include e' roba nostra.
exists_in_project() {
    local name="$1" r
    for r in "${SOURCES[@]}"; do
        [[ -n "$(find "$r" -name "$(basename "$name")" -print -quit 2>/dev/null)" ]] && return 0
    done
    return 1
}

hdr_alias=()
for name in "${INCLUDED[@]}"; do
    [[ -n "$name" ]] || continue
    # Solo i nomi che il sysroot NON espone con questa capitalizzazione...
    exists_in_sysroot "$name" && continue
    key="${name,,}"
    real=""
    [[ -n "${HDR_BY_LOWER[$key]+x}" ]] && real="${HDR_BY_LOWER[$key]}"
    # ...ma che esistono ignorando le maiuscole.
    [[ -n "$real" ]] || continue
    exists_in_project "$name" && continue

    mkdir -p "$INC_SHIM/$(dirname "$name")"
    printf '/* alias generato da tools/release/mingw_case_shim.sh (D176) */\n#include <%s>\n' \
        "$real" > "$INC_SHIM/$name"
    hdr_alias+=("$name -> $real")
done

if [[ ${#hdr_alias[@]} -eq 0 ]]; then
    echo "shim MinGW: ZERO alias di header generati." >&2
    echo "  Non e' una build pulita: i sorgenti Windows di questo progetto usano" >&2
    echo "  sicuramente <Windows.h>. Zero alias vuol dire che la scansione si e'" >&2
    echo "  rotta (radici sbagliate, sysroot non trovato), non che non servivano." >&2
    exit 1
fi

echo "shim MinGW: ${#hdr_alias[@]} alias di header generati in $INC_SHIM"
printf '  %s\n' "${hdr_alias[@]}"

# ------------------------------------------------------------- librerie ---

lib_alias=()
if [[ ${#MAKEFILES[@]} -gt 0 ]]; then
    # Le cartelle di ricerca del linker, piu' quelle passate a mano (vcpkg).
    mapfile -t LIB_DIRS < <(
        {
            "$CC" -print-search-dirs 2>/dev/null \
                | sed -n 's/^libraries: =*//p' | tr ':' '\n'
            printf '%s\n' "${EXTRA_LIB_DIRS[@]+"${EXTRA_LIB_DIRS[@]}"}"
        } | while IFS= read -r d; do [[ -n "$d" && -d "$d" ]] && (cd "$d" && pwd); done | sort -u
    )

    # Indice: nome-di-link in minuscolo -> percorso del file vero.
    declare -A LIB_BY_LOWER=()
    declare -A LIB_EXACT=()
    for d in "${LIB_DIRS[@]}"; do
        while IFS= read -r f; do
            base="$(basename "$f")"
            link="${base#lib}"
            link="${link%.dll.a}"; link="${link%.a}"
            [[ -n "${LIB_EXACT[$link]+x}" ]] || LIB_EXACT["$link"]="$f"
            key="${link,,}"
            [[ -n "${LIB_BY_LOWER[$key]+x}" ]] || LIB_BY_LOWER["$key"]="$f"
        done < <(find "$d" -maxdepth 1 \( -name 'lib*.a' -o -name 'lib*.dll.a' \) ! -type d 2>/dev/null)
    done

    mapfile -t LINKED < <(
        grep -ohE '(^|[[:space:]])-l[A-Za-z0-9_.+-]+' "${MAKEFILES[@]}" 2>/dev/null \
            | sed -E 's|^[[:space:]]*-l||' | sort -u
    )

    for name in "${LINKED[@]}"; do
        [[ -n "$name" ]] || continue
        # Il linker lo trova gia' con questo nome esatto: niente da fare.
        [[ -n "${LIB_EXACT[$name]+x}" ]] && continue
        key="${name,,}"
        real=""
        [[ -n "${LIB_BY_LOWER[$key]+x}" ]] && real="${LIB_BY_LOWER[$key]}"
        [[ -n "$real" ]] || continue

        ln -sf "$real" "$LIB_SHIM/lib${name}.a"
        lib_alias+=("-l$name -> $(basename "$real")")
    done

    if [[ ${#lib_alias[@]} -eq 0 ]]; then
        echo "shim MinGW: nessun alias di libreria necessario"
    else
        echo "shim MinGW: ${#lib_alias[@]} alias di libreria generati in $LIB_SHIM"
        printf '  %s\n' "${lib_alias[@]}"
    fi
fi
