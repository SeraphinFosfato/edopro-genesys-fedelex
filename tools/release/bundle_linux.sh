#!/usr/bin/env bash
# Impacchetta il client Linux con la propria chiusura di dipendenze accanto,
# in modo che parta su una distribuzione diversa da quella del runner.
#
# Uso: tools/release/bundle_linux.sh [binario] [cartella-di-uscita]
#   [binario]             default bin/x64/release/ygoprodll
#   [cartella-di-uscita]  default tools/release/out
#
# Produce <out>/edopro-custom-linux-x64.tar.gz, che estratto da:
#   edopro-custom-linux-x64/
#     ygoprodll        <- byte per byte lo stesso binario nudo della release
#     lib/*.so.*       <- la chiusura filtrata, con il SONAME come nome file
#     LEGGIMI.txt
#     notices/         <- licenze delle librerie che ridistribuiamo
#
# ── Perche' esiste ────────────────────────────────────────────────────────
# Il binario prodotto da release.yml/ci.yml e' compilato su ubuntu-latest e
# linka dinamicamente le librerie DI QUEL sistema. Sceso su Arch/CachyOS il
# 2026-09-25 non partiva affatto:
#
#   ./ygoprodll: error while loading shared libraries: libFLAC.so.12:
#   cannot open shared object file
#
# e `ldd` ne riportava tre irrisolte (libFLAC.so.12, libgit2.so.1.7,
# libfmt.so.9). Tre e' quante ne mancavano su UNA distribuzione quel giorno:
# il difetto non e' "tre librerie da aggiungere", e' che il pacchetto non si
# porta dietro niente. Per questo qui la lista non si scrive a mano — si
# calcola con ldd sul binario appena compilato e si SOTTRAE cio' che deve
# per forza venire dal sistema dell'utente (vedi MAI_IMPACCHETTARE sotto).
#
# Il precedente in casa e' tools/release/package_linux_release.sh, che ha una
# lista di SONAME scritti a mano (libfmt.so.12, libgit2.so.1.9, ...): sono i
# nomi visti su UNA macchina, e a ogni bump di SONAME vanno riscritti. Quello
# script resta perche' fa un lavoro diverso (impacchetta un'installazione
# EDOPro completa presa da runtime/); questo qui parte dal solo binario ed e'
# quello che gira in CI.
#
# ── Cosa NON si impacchetta, e perche' ────────────────────────────────────
# Una libGL impacchettata da noi non parla col driver video dell'utente: il
# client smetterebbe di partire proprio sulle macchine dove oggi parte. Lo
# stesso vale per il loader, per il nucleo glibc e per lo stack audio, che
# deve parlare col demone audio in esecuzione sulla macchina dell'utente.
# Nel dubbio su una libreria che non e' in quella lista si impacchetta: il
# rischio sta tutto dall'altra parte.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

BIN="${1:-$REPO_ROOT/bin/x64/release/ygoprodll}"
OUT_DIR="${2:-$REPO_ROOT/tools/release/out}"

PKG_NAME="edopro-custom-linux-x64"
STAGE_ROOT="$REPO_ROOT/tools/release/.stage-bundle"
STAGE="$STAGE_ROOT/$PKG_NAME"

# ── La lista "mai impacchettare" ──────────────────────────────────────────
# Formato: "<glob sul SONAME>|<categoria>". La categoria serve solo a
# stampare il perche': uno script che esclude in silenzio e' indistinguibile
# da uno rotto.
MAI_IMPACCHETTARE=(
	# Il loader: deve essere quello del sistema, e' lui che carica tutto il
	# resto. Impacchettarlo significa impacchettare anche la glibc intera.
	"ld-linux*.so*|loader"
	"ld.so*|loader"
	"ld64.so*|loader"

	# Nucleo glibc. libmvec/libanl/libutil/libnsl/libnss_* non erano
	# nell'elenco dettato, ma sono glibc quanto libc.so.6: sono costruite
	# dallo stesso albero, dipendono da simboli GLIBC_PRIVATE che NON sono
	# stabili fra una versione e l'altra, e una copia di Ubuntu accanto a una
	# libc di Arch e' il modo piu' rapido di far esplodere il loader. Sono
	# qui come estensione dichiarata della categoria "nucleo glibc", non come
	# aggiunte silenziose: lo script le stampa fra le escluse come tutte le
	# altre.
	"libc.so*|glibc"
	"libm.so*|glibc"
	"libmvec.so*|glibc"
	"libdl.so*|glibc"
	"libpthread.so*|glibc"
	"librt.so*|glibc"
	"libresolv.so*|glibc"
	"libanl.so*|glibc"
	"libutil.so*|glibc"
	"libnsl.so*|glibc"
	"libnss_*.so*|glibc"
	"libthread_db.so*|glibc"

	# Stack grafico e di display. Questa e' la riga che impedisce al pacchetto
	# di diventare peggio del problema che risolve: libGL e compagnia sono
	# l'interfaccia verso il driver installato sulla macchina dell'utente
	# (mesa, nvidia proprietario, ...). Una nostra copia non lo conosce.
	"libGL*|grafica"
	"libEGL*|grafica"
	"libGLX*|grafica"
	"libGLdispatch*|grafica"
	"libOpenGL*|grafica"
	"libglapi*|grafica"
	"libX11*|grafica"
	"libxcb*|grafica"
	"libXau*|grafica"
	"libXdmcp*|grafica"
	"libXext*|grafica"
	"libXrandr*|grafica"
	"libXrender*|grafica"
	"libXi*|grafica"
	"libXfixes*|grafica"
	"libXcursor*|grafica"
	"libXxf86vm*|grafica"
	"libwayland*|grafica"
	"libdrm*|grafica"
	"libgbm*|grafica"

	# Stack audio di sistema: parla col demone audio in esecuzione (pipewire,
	# pulse, jack). Una copia nostra parlerebbe col demone sbagliato o con
	# nessuno.
	"libasound*|audio"
	"libpulse*|audio"
	"libpipewire*|audio"
	"libjack*|audio"
)

motivo_categoria() {
	case "$1" in
	loader) echo "il loader dinamico: carica tutto il resto, deve essere quello del sistema" ;;
	glibc) echo "nucleo glibc: legata al loader da simboli GLIBC_PRIVATE non stabili fra versioni" ;;
	grafica) echo "stack grafico/display: una copia nostra non parla col driver video dell'utente" ;;
	audio) echo "stack audio di sistema: deve parlare col demone audio in esecuzione sulla macchina" ;;
	vdso) echo "fornita dal kernel, non e' un file su disco" ;;
	*) echo "$1" ;;
	esac
}

# Librerie che non sono "di sistema ordinarie" e la cui ridistribuzione
# dentro un nostro tarball ha conseguenze di licenza (questo repo e' AGPLv3).
# NON le esclude: le segnala, perche' la scelta di ridistribuirle non spetta
# a uno script. notices/ contiene gia' LICENSE.openssl e COPYING.libgit2.
LICENZA_DA_GUARDARE=(
	"libssl.so*"
	"libcrypto.so*"
	"libidn2.so*"
	"libunistring.so*"
	"libgmp.so*"
	"libgnutls.so*"
	"libgcrypt.so*"
	"libreadline.so*"
)

# Normalizza l'output di ldd (da stdin) a righe "<soname>\t<percorso o ->".
# Righe possibili:
#   "\tnome => /percorso (0x...)"    dipendenza risolta
#   "\tnome => not found"            chiusura bucata, gestita a parte
#   "\t/percorso (0x...)"            il loader, su alcune distro
#   "\tlinux-vdso.so.1 (0x...)"      fornita dal kernel, nessun file
# Si taglia SOLO l'indirizzo finale e si separa su " => ": il percorso puo'
# contenere spazi. Questo repo vive sotto "Formato GSY CustomIgnis/", e un
# `awk '{print $3}'` qui restituiva "/home/.../Documenti/Obisidian/Formato" —
# cioe' un controllo che falliva su un pacchetto perfettamente buono.
normalizza_ldd() {
	awk '
		{
			sub(/[[:space:]]*\(0x[0-9a-f]+\)[[:space:]]*$/, "");
			gsub(/^[[:space:]]+|[[:space:]]+$/, "");
			if ($0 == "") next;
			i = index($0, " => ");
			if (i > 0) {
				nome = substr($0, 1, i - 1);
				perc = substr($0, i + 4);
			} else {
				nome = $0;
				perc = ($0 ~ /^\//) ? $0 : "-";
			}
			sub(/.*\//, "", nome);
			print nome "\t" perc;
		}
	'
}

combacia() {
	# combacia <nome> <glob...>
	local nome="$1"
	shift
	local g
	for g in "$@"; do
		# shellcheck disable=SC2254 # il glob e' voluto
		case "$nome" in
		$g) return 0 ;;
		esac
	done
	return 1
}

# ── 0. controlli d'ingresso ───────────────────────────────────────────────
[[ -f "$BIN" ]] || {
	echo "ERRORE: binario non trovato: $BIN" >&2
	echo "        compila prima con tools/release/build_linux.sh release" >&2
	exit 1
}

echo "== bundle_linux.sh =="
echo "binario:  $BIN"
echo "uscita:   $OUT_DIR"
echo

# Il pacchetto funziona solo se il binario cerca davvero in lib/ accanto a se'
# stesso. Se il rpath non c'e' lo si dice e si fallisce: un tarball che
# sembra giusto e non parte e' peggio di nessun tarball (e' la stessa
# categoria di difetto del silenzio di partenza).
RPATH_RAW="$(readelf -d "$BIN" | grep -E 'RPATH|RUNPATH' || true)"
echo "-- rpath del binario --"
if [[ -z "$RPATH_RAW" ]]; then
	echo "  (nessuno)"
else
	echo "$RPATH_RAW" | sed 's/^/  /'
fi
if ! grep -q 'ORIGIN/lib' <<<"$RPATH_RAW"; then
	echo
	echo "ERRORE: il binario non ha \$ORIGIN/lib nel suo rpath: le librerie" >&2
	echo "        impacchettate in lib/ non verrebbero mai cercate li'." >&2
	echo "        Ricompila dopo la modifica a premake5.lua (filtro" >&2
	echo "        system:linux + configurations:Release)." >&2
	exit 1
fi
# DT_RUNPATH vale solo per le dipendenze DIRETTE dell'oggetto che lo porta:
# libcurl.so.4 presa da lib/ cercherebbe le PROPRIE dipendenze (libnghttp2,
# libidn2, ...) nei percorsi di sistema, non in lib/. DT_RPATH invece si
# eredita lungo tutta la catena di caricamento. Per questo premake linka con
# --disable-new-dtags; se qui esce RUNPATH, quella parte e' saltata.
if grep -q 'RUNPATH' <<<"$RPATH_RAW"; then
	echo
	echo "ERRORE: il binario porta DT_RUNPATH, non DT_RPATH. RUNPATH non si" >&2
	echo "        eredita: le dipendenze delle librerie impacchettate" >&2
	echo "        verrebbero comunque cercate nel sistema. Serve" >&2
	echo "        -Wl,--disable-new-dtags (vedi premake5.lua)." >&2
	exit 1
fi
echo

# ── 1. chiusura di dipendenze, calcolata sul binario appena compilato ─────
LDD_OUT="$(ldd "$BIN")"

MANCANTI="$(awk '/=> not found/{print $1}' <<<"$LDD_OUT")"
if [[ -n "$MANCANTI" ]]; then
	echo "ERRORE: la chiusura e' incompleta gia' su questa macchina." >&2
	echo "        Non risolte:" >&2
	sed 's/^/          /' >&2 <<<"$MANCANTI"
	echo "        Impacchettare una chiusura bucata produce un tarball che" >&2
	echo "        non parte da nessuna parte. Installa le dipendenze mancanti." >&2
	exit 1
fi

CHIUSURA="$(normalizza_ldd <<<"$LDD_OUT" | sort -u)"

TOT=$(wc -l <<<"$CHIUSURA")
echo "-- chiusura di dipendenze: $TOT voci --"
echo

# ── 2. stage ──────────────────────────────────────────────────────────────
rm -rf "$STAGE_ROOT"
mkdir -p "$STAGE/lib"

# Copia, non strip: il binario dentro il tarball deve restare byte per byte
# lo stesso che la release pubblica nudo, altrimenti "sono lo stesso file"
# smette di essere vero e un bug si riproduce solo in uno dei due.
cp "$BIN" "$STAGE/$(basename "$BIN")"
chmod +x "$STAGE/$(basename "$BIN")"

ESCLUSE=()
INCLUSE=()
DA_GUARDARE=()

while IFS=$'\t' read -r soname percorso; do
	[[ -n "$soname" ]] || continue

	if [[ "$soname" == linux-vdso.so* || "$soname" == linux-gate.so* ]]; then
		ESCLUSE+=("$soname|vdso")
		continue
	fi

	escluso=""
	for voce in "${MAI_IMPACCHETTARE[@]}"; do
		glob="${voce%%|*}"
		cat="${voce##*|}"
		if combacia "$soname" "$glob"; then
			escluso="$cat"
			break
		fi
	done
	if [[ -n "$escluso" ]]; then
		ESCLUSE+=("$soname|$escluso")
		continue
	fi

	if [[ "$percorso" == "-" || ! -f "$percorso" ]]; then
		echo "ERRORE: $soname risolve a '$percorso', che non e' un file." >&2
		exit 1
	fi

	# -L segue il symlink (di solito libfoo.so.1 -> libfoo.so.1.2.3) e la
	# destinazione prende il SONAME come nome: e' il nome con cui il loader
	# la cerca, e senza quello il file in lib/ non lo troverebbe nessuno.
	cp -L "$percorso" "$STAGE/lib/$soname"
	chmod 0644 "$STAGE/lib/$soname"
	INCLUSE+=("$soname|$percorso")

	if combacia "$soname" "${LICENZA_DA_GUARDARE[@]}"; then
		DA_GUARDARE+=("$soname")
	fi
done <<<"$CHIUSURA"

# ── 3. il resoconto: lo script dichiara il proprio effetto ────────────────
echo "-- ESCLUSE, devono venire dal sistema dell'utente (${#ESCLUSE[@]}) --"
for voce in "${ESCLUSE[@]}"; do
	printf '  %-34s %s\n' "${voce%%|*}" "$(motivo_categoria "${voce##*|}")"
done
echo

echo "-- IMPACCHETTATE in lib/ (${#INCLUSE[@]}) --"
for voce in "${INCLUSE[@]}"; do
	soname="${voce%%|*}"
	origine="${voce##*|}"
	printf '  %-34s %8s  <- %s\n' "$soname" \
		"$(du -h "$STAGE/lib/$soname" | cut -f1)" "$origine"
done
echo

if ((${#DA_GUARDARE[@]} > 0)); then
	echo "-- ATTENZIONE licenza: ridistribuite dentro il tarball (${#DA_GUARDARE[@]}) --"
	echo "   Questo repo e' AGPLv3. Queste non sono librerie di sistema"
	echo "   ordinarie e ridistribuirle ha conseguenze: servono le loro"
	echo "   licenze in notices/ (o vanno tolte dalla chiusura)."
	for soname in "${DA_GUARDARE[@]}"; do
		echo "     $soname"
	done
	echo
fi

# ── 4. autoverifica del filtro ────────────────────────────────────────────
# Non "a occhio": si ricontrolla l'albero prodotto contro la stessa lista.
VIOLAZIONI=0
for f in "$STAGE"/lib/*; do
	[[ -e "$f" ]] || continue
	n="$(basename "$f")"
	for voce in "${MAI_IMPACCHETTARE[@]}"; do
		if combacia "$n" "${voce%%|*}"; then
			echo "ERRORE: $n e' finita in lib/ ma e' nella lista 'mai impacchettare'" >&2
			VIOLAZIONI=$((VIOLAZIONI + 1))
		fi
	done
done
((VIOLAZIONI == 0)) || exit 1

# Il binario, dentro lo stage, deve risolvere in lib/ tutto cio' che abbiamo
# impacchettato — altrimenti abbiamo copiato file che nessuno usera'.
NON_DAL_BUNDLE=0
VERIFICA="$(ldd "$STAGE/$(basename "$BIN")" | normalizza_ldd)"
for voce in "${INCLUSE[@]}"; do
	soname="${voce%%|*}"
	risolto="$(awk -F'\t' -v s="$soname" '$1==s{print $2}' <<<"$VERIFICA")"
	if [[ "$risolto" != "$STAGE/lib/$soname" ]]; then
		echo "ATTENZIONE: $soname risolve a '$risolto', non alla copia in lib/" >&2
		NON_DAL_BUNDLE=$((NON_DAL_BUNDLE + 1))
	fi
done
if ((NON_DAL_BUNDLE > 0)); then
	echo "ERRORE: $NON_DAL_BUNDLE librerie impacchettate non vengono prese da lib/." >&2
	echo "        Il rpath non sta funzionando: il tarball sarebbe inutile." >&2
	exit 1
fi
echo "-- verifica: il filtro e' pulito e tutte le ${#INCLUSE[@]} librerie in lib/ vengono prese da li' --"
echo

# ── 5. istruzioni e licenze ───────────────────────────────────────────────
cp -r "$REPO_ROOT/notices" "$STAGE/notices"
cp "$REPO_ROOT/LICENSE" "$STAGE/LICENSE"

cat >"$STAGE/LEGGIMI.txt" <<EOF
EDOPro - client con supporto alla point list (build Linux x64)

Questo pacchetto NON e' un'installazione completa di EDOPro: e' solo il
client. Ha bisogno dei dati di un'installazione EDOPro gia' presente
(cards.cdb, script/, config/, textures/, fonts).

Installazione
  1. Apri la cartella dove sta il tuo eseguibile EDOPro.
  2. Copia dentro, accanto a quell'eseguibile:
       ygoprodll
       lib/          (la cartella intera)
  3. Avvia ./ygoprodll da quella cartella.

La cartella lib/ deve restare accanto a ygoprodll: il binario ci cerca
dentro le librerie che si porta appresso. Se la sposti o la rinomini, il
client non parte piu'.

Se all'avvio compare "error while loading shared libraries", esegui
  ldd ./ygoprodll
e segnala quali righe dicono "not found": vuol dire che manca una libreria
di sistema che questo pacchetto da' per scontata (driver grafici, audio,
glibc) e va installata dal gestore pacchetti della tua distribuzione.

Licenza: AGPLv3, vedi LICENSE. Il sorgente completo di questa build sta su
https://github.com/SeraphinFosfato/edopro-genesys-fedelex
Le licenze delle librerie ridistribuite in lib/ stanno in notices/.
EOF

# ── 6. tarball ────────────────────────────────────────────────────────────
mkdir -p "$OUT_DIR"
TARBALL="$OUT_DIR/${PKG_NAME}.tar.gz"
rm -f "$TARBALL"
tar -C "$STAGE_ROOT" -czf "$TARBALL" "$PKG_NAME"
rm -rf "$STAGE_ROOT"

echo "Pacchetto pronto: $TARBALL ($(du -h "$TARBALL" | cut -f1))"
echo "  ${#INCLUSE[@]} librerie impacchettate, ${#ESCLUSE[@]} lasciate al sistema."
