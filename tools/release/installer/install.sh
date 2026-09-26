#!/usr/bin/env bash
# Installatore/disinstallatore Linux per il tarball portabile di
# tools/release/bundle_linux.sh (FASE 35, PHASES.md).
#
# Vincolo centrale, non negoziabile: NIENTE sudo/pkexec, NIENTE scritture
# fuori da $HOME. Tutto quello che questo script tocca sta sotto $HOME.
#
# Cosa risolve: il tarball nudo contiene solo l'eseguibile e le librerie.
# Scompattato in una cartella qualsiasi, il client parte ma non mostra
# NESSUNA stanza, senza nessun messaggio d'errore, perche' senza
# config/configs.json l'elenco server e' vuoto (Game::LoadServers in
# gframe/game.cpp). Questo script installa il programma sotto ~/.local e
# lo fa partire sempre dentro una "cartella dati" separata che quel file
# ce l'ha dentro — riusando un'installazione EDOPro gia' presente se
# l'utente la conferma, o creandone una nuova altrimenti.
#
# Uso:
#   ./install.sh                 installa (o aggiorna un'installazione gia'
#                                 fatta da questo stesso script)
#   ./install.sh --yes           come sopra, senza chiedere conferma sulla
#                                 cartella dati esistente trovata (usa la
#                                 prima che trova)
#   ./install.sh --data-dir DIR  forza la cartella dati, senza cercare ne'
#                                 chiedere (utile per test o setup non standard)
#   ./install.sh --uninstall     toglie programma, avviatore, voce di menu
#                                 e icona. NON tocca mai la cartella dati:
#                                 mazzi e replay sono dell'utente.

set -euo pipefail

# ─────────────────────────── percorsi, tutti sotto $HOME ──────────────────
APP_ID="fedelex-edopro"
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_BIN_HOME="${XDG_BIN_HOME:-$HOME/.local/bin}"

PROGRAM_DIR="$XDG_DATA_HOME/$APP_ID"
LAUNCHER="$XDG_BIN_HOME/$APP_ID"
DESKTOP_DIR="$XDG_DATA_HOME/applications"
DESKTOP_FILE="$DESKTOP_DIR/$APP_ID.desktop"
ICON_DIR="$XDG_DATA_HOME/icons/hicolor/256x256/apps"
ICON_FILE="$ICON_DIR/$APP_ID.png"
DATA_DIR_MARKER="$PROGRAM_DIR/.data-dir"
DEFAULT_NEW_DATA_DIR="$XDG_DATA_HOME/${APP_ID}-data"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_NAME="ygoprodll"

# Elenco dei posti dove si cerca un'installazione EDOPro gia' presente,
# PRIMA di crearne una nuova. Deliberatamente ristretto a $HOME (un
# /opt/edopro di sistema e' fuori dal vincolo centrale: non potremmo
# scriverci repositories/deck/replay senza sudo, quindi non e' un
# candidato utilizzabile anche se lo trovassimo). Punto di risalita aperto
# in PHASES.md FASE 35: l'elenco esatto va confermato/ampliato da Opus,
# questo e' un primo elenco ragionevole, non l'ultima parola.
SEARCH_CANDIDATES=(
	"$DEFAULT_NEW_DATA_DIR"
	"$HOME/EDOPro"
	"$HOME/edopro"
	"$HOME/Games/EDOPro"
	"$HOME/Giochi/EDOPro"
	"$XDG_DATA_HOME/EDOPro"
)

# ─────────────────────────────── utilita' ──────────────────────────────────
# Tutti e tre su stderr, mai su stdout: resolve_data_dir() e ensure_configs_json()
# vengono chiamate dentro "$(...)"/come valore di ritorno altrove, e qualunque
# log su stdout ci finirebbe dentro mischiato al valore vero (bug trovato in
# test: il path restituito conteneva anche le righe di log). stdout resta
# riservato ai soli "return value" di quelle funzioni.
log()  { printf '%s\n' "$*" >&2; }
warn() { printf 'ATTENZIONE: %s\n' "$*" >&2; }
err()  { printf 'ERRORE: %s\n' "$*" >&2; }

# Una cartella "sembra" un'installazione dati EDOPro se ha sia
# config/configs.json (il file che manca nel tarball nudo) sia script/
# (il core Lua): il primo da solo potrebbe essere un residuo vuoto.
is_data_dir() {
	local dir="$1"
	[[ -f "$dir/config/configs.json" && -d "$dir/script" ]]
}

confirm() {
	# confirm <domanda>  ->  0 (si') o 1 (no/eof). Default no: un invio a
	# vuoto non deve mai far usare una cartella per sbaglio.
	local domanda="$1" risposta
	if [[ "$ASSUME_YES" == "1" ]]; then
		return 0
	fi
	if [[ -r /dev/tty && -w /dev/tty ]] && read -r -p "$domanda [s/N] " risposta </dev/tty 2>/dev/null; then
		:
	elif ! read -r risposta 2>/dev/null; then
		# niente tty e niente stdin leggibile: nessuna risposta possibile,
		# si assume no (mai usare una cartella dati senza conferma esplicita).
		risposta="n"
	fi
	case "$risposta" in
	[sSyY]*) return 0 ;;
	*) return 1 ;;
	esac
}

# ────────────────────────── risoluzione cartella dati ──────────────────────
resolve_data_dir() {
	if [[ -n "${FORCE_DATA_DIR:-}" ]]; then
		echo "$FORCE_DATA_DIR"
		return 0
	fi

	# Reinstallazione: se una cartella dati era gia' associata e sembra
	# ancora valida, la si riusa senza richiedere niente (idempotenza,
	# cancello 5). Se il marker punta altrove e non e' piu' valido, si
	# ricade sulla ricerca sotto invece di fallire.
	if [[ -f "$DATA_DIR_MARKER" ]]; then
		local marcata
		marcata="$(<"$DATA_DIR_MARKER")"
		if [[ -n "$marcata" && -d "$marcata" ]]; then
			log "Cartella dati gia' associata da un'installazione precedente: $marcata"
			echo "$marcata"
			return 0
		fi
	fi

	local trovate=()
	local c
	for c in "${SEARCH_CANDIDATES[@]}"; do
		if is_data_dir "$c"; then
			trovate+=("$c")
		fi
	done

	if ((${#trovate[@]} > 0)); then
		log "Trovata un'installazione EDOPro esistente:"
		local i=1
		for c in "${trovate[@]}"; do
			log "  $i) $c"
			i=$((i + 1))
		done
		local scelta="${trovate[0]}"
		if confirm "Usare '$scelta' come cartella dati (mazzi, replay, config)?"; then
			echo "$scelta"
			return 0
		fi
		log "Rifiutata. Verra' creata una cartella dati nuova in $DEFAULT_NEW_DATA_DIR."
	fi

	echo "$DEFAULT_NEW_DATA_DIR"
}

# ───────────────────── config/configs.json: il punto scoperto ─────────────
# Senza questo file l'elenco server resta vuoto e il client non lo dice:
# e' esattamente il guasto che questo installatore esiste per impedire
# (vedi intestazione). Se la cartella dati e' una gia' esistente, il file
# c'e' gia' (is_data_dir lo richiede). Se invece la cartella e' nuova, oggi
# NON abbiamo una sorgente da cui prenderlo: il tarball non lo contiene
# (non e' dato nostro da ridistribuire, vedi .gitignore su /runtime/) e
# scaricarlo violerebbe "niente rete durante l'installazione". E' un punto
# di risalita dichiarato nel brief (PHASES.md FASE 35): qui ci si ferma con
# un messaggio esplicito invece di installare in silenzio una cartella
# dati rotta — che sarebbe la stessa trappola P-15 in un'altra forma.
ensure_configs_json() {
	local data_dir="$1"
	if [[ -f "$data_dir/config/configs.json" ]]; then
		return 0
	fi

	mkdir -p "$data_dir/config"

	local sorgente="$SCRIPT_DIR/installer-data/configs.json"
	if [[ -f "$sorgente" ]]; then
		cp "$sorgente" "$data_dir/config/configs.json"
		log "config/configs.json scritto da $sorgente"
		return 0
	fi

	err "manca config/configs.json e nessuna sorgente e' disponibile in questo pacchetto."
	err "Senza questo file l'elenco delle stanze resta vuoto, senza nessun errore visibile."
	err "Questo e' un punto aperto (non ancora deciso) su come questo installatore debba"
	err "procurarselo: vedi PHASES.md, FASE 35, punti di risalita."
	err "La cartella dati e' stata comunque creata in: $data_dir"
	err "Programma e avviatore verranno installati lo stesso, ma il client non mostrera'"
	err "stanze finche' non copi tu un config/configs.json valido dentro quella cartella"
	err "(ad es. da un'installazione EDOPro completa che hai gia'):"
	err "  cp <installazione-completa>/config/configs.json '$data_dir/config/configs.json'"
	CONFIGS_JSON_MISSING=1
	return 1
}

# ───────────────────────────── verifica librerie ───────────────────────────
check_libraries() {
	local binario="$1"
	if ! command -v ldd >/dev/null 2>&1; then
		warn "ldd non e' disponibile su questo sistema: salto la verifica delle librerie."
		return 0
	fi
	local output mancanti
	output="$(ldd "$binario" 2>&1 || true)"
	mancanti="$(awk '/=> not found/{print $1}' <<<"$output")"
	if [[ -n "$mancanti" ]]; then
		warn "il client non trovera' queste librerie di sistema all'avvio:"
		while IFS= read -r nome; do
			[[ -n "$nome" ]] || continue
			printf '    %s\n' "$nome" >&2
		done <<<"$mancanti"
		warn "installa il pacchetto che le fornisce con il gestore pacchetti della tua"
		warn "distribuzione (es. il pacchetto che contiene quel .so) prima di avviare"
		warn "$APP_ID, altrimenti l'avvio fallira' con un errore del caricatore."
		return 1
	fi
	log "Verifica librerie: tutte risolte (ldd non riporta 'not found')."
	return 0
}

# ──────────────────────────────── installazione ────────────────────────────
do_install() {
	CONFIGS_JSON_MISSING=0

	if [[ ! -f "$SCRIPT_DIR/$BINARY_NAME" ]]; then
		err "$SCRIPT_DIR/$BINARY_NAME non trovato: questo script va eseguito dalla"
		err "cartella estratta dal tarball, non spostato da solo."
		exit 1
	fi

	local data_dir
	data_dir="$(resolve_data_dir)"
	log "Cartella dati: $data_dir"
	mkdir -p "$data_dir"

	ensure_configs_json "$data_dir" || true # non bloccante: vedi funzione

	log "Installo il programma in: $PROGRAM_DIR"
	mkdir -p "$PROGRAM_DIR"
	# rsync --delete: una reinstallazione da una versione con meno file di
	# libreria non lascia .so orfani dietro (idempotenza, cancello 5).
	rsync -a --delete \
		--exclude 'install.sh' \
		--exclude 'installer-data' \
		--exclude '*.desktop.in' \
		--exclude 'icon.png' \
		--exclude 'LEGGIMI.txt' \
		"$SCRIPT_DIR"/ "$PROGRAM_DIR"/
	chmod +x "$PROGRAM_DIR/$BINARY_NAME"

	echo "$data_dir" >"$DATA_DIR_MARKER"

	local librerie_ok=1
	check_libraries "$PROGRAM_DIR/$BINARY_NAME" || librerie_ok=0

	log "Scrivo l'avviatore: $LAUNCHER"
	mkdir -p "$XDG_BIN_HOME"
	cat >"$LAUNCHER" <<LAUNCHER_EOF
#!/usr/bin/env bash
# Generato da $APP_ID install.sh — non modificare a mano, verra' riscritto
# alla prossima installazione.
set -euo pipefail
PROGRAM_DIR="$PROGRAM_DIR"
DATA_DIR_MARKER="$DATA_DIR_MARKER"
if [[ -f "\$DATA_DIR_MARKER" ]]; then
	DATA_DIR="\$(<"\$DATA_DIR_MARKER")"
else
	DATA_DIR="$DEFAULT_NEW_DATA_DIR"
fi
cd "\$DATA_DIR" || {
	echo "Cartella dati non trovata: \$DATA_DIR" >&2
	exit 1
}
exec "\$PROGRAM_DIR/$BINARY_NAME" "\$@"
LAUNCHER_EOF
	chmod +x "$LAUNCHER"

	log "Installo l'icona: $ICON_FILE"
	mkdir -p "$ICON_DIR"
	cp "$SCRIPT_DIR/icon.png" "$ICON_FILE"

	log "Registro la voce di menu: $DESKTOP_FILE"
	mkdir -p "$DESKTOP_DIR"
	sed -e "s|{EXEC}|$LAUNCHER|" -e "s|{ICON}|$ICON_FILE|" \
		"$SCRIPT_DIR/$APP_ID.desktop.in" >"$DESKTOP_FILE"
	chmod +x "$DESKTOP_FILE"
	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
	fi

	log ""
	log "Installazione completata."
	log "  Programma:    $PROGRAM_DIR"
	log "  Cartella dati: $data_dir"
	log "  Avviatore:    $LAUNCHER"
	log "  Voce di menu: $DESKTOP_FILE"
	if ((CONFIGS_JSON_MISSING)); then
		warn "manca ancora config/configs.json nella cartella dati (vedi sopra): la lista"
		warn "stanze restera' vuota finche' non lo aggiungi tu."
	fi
	if ((!librerie_ok)); then
		warn "alcune librerie di sistema mancano (vedi sopra): risolvile prima di avviare."
	fi
}

# ─────────────────────────────── disinstallazione ──────────────────────────
do_uninstall() {
	local data_dir=""
	if [[ -f "$DATA_DIR_MARKER" ]]; then
		data_dir="$(<"$DATA_DIR_MARKER")"
	fi

	log "Rimuovo il programma: $PROGRAM_DIR"
	rm -rf "$PROGRAM_DIR"

	log "Rimuovo l'avviatore: $LAUNCHER"
	rm -f "$LAUNCHER"

	log "Rimuovo la voce di menu: $DESKTOP_FILE"
	rm -f "$DESKTOP_FILE"

	log "Rimuovo l'icona: $ICON_FILE"
	rm -f "$ICON_FILE"

	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
	fi

	log ""
	log "Disinstallazione completata."
	if [[ -n "$data_dir" ]]; then
		log "La cartella dati NON e' stata toccata (mazzi, replay, config): $data_dir"
	else
		log "Nessuna cartella dati era associata a questa installazione: niente da lasciare intatto."
	fi
}

# ──────────────────────────────── argomenti ────────────────────────────────
ASSUME_YES=0
FORCE_DATA_DIR=""
MODE="install"

while (($#)); do
	case "$1" in
	--uninstall)
		MODE="uninstall"
		shift
		;;
	--yes)
		ASSUME_YES=1
		shift
		;;
	--data-dir)
		FORCE_DATA_DIR="${2:?--data-dir richiede un percorso}"
		shift 2
		;;
	--data-dir=*)
		FORCE_DATA_DIR="${1#--data-dir=}"
		shift
		;;
	-h | --help)
		sed -n '1,25p' "${BASH_SOURCE[0]}" | grep '^#' | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		err "argomento sconosciuto: $1"
		exit 1
		;;
	esac
done

case "$MODE" in
install) do_install ;;
uninstall) do_uninstall ;;
esac
