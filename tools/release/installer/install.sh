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
# La provenienza di config/configs.json (deciso il 2026-09-26,
# design/licensing.md "config/configs.json: lo scriviamo noi, non lo
# ridistribuiamo"): non lo copiamo dal file di Project Ignis e non lo
# scarichiamo al primo avvio (deciderebbe da dove il client prende codice
# eseguibile, senza autenticazione — la stessa obiezione di
# design/client-update.md sul manifesto non firmato). Lo scriviamo noi
# (indirizzi/porte/percorsi pubblici, dati di fatto) in
# installer-data/configs.json, incluso nel tarball da bundle_linux.sh. Se
# pero' una copia dell'utente e' raggiungibile in lettura (la sua e' piu'
# aggiornata della nostra), si preferisce quella: vedi
# find_configs_json_source().
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
#
# Regola sulla cartella PROGRAMMA (non quella dati): questo script non
# cancella mai un file che non ha installato lui. Tiene l'elenco di cio' che
# ha messo li' (.installed-files) e lo confronta con cio' che trova: un file
# che non e' nel pacchetto nuovo ne' in quell'elenco non e' nostro — si
# sposta nella cartella dati invece di sparire. Un vecchio aggiornatore
# scriveva mazzi e replay proprio li' (prima che il client avesse una
# cartella dati separata), e una reinstallazione con `rsync --delete` li ha
# cancellati senza che nessuno se ne accorgesse: un mazzo dell'utente e'
# andato perso cosi'. Dettagli sotto, in migrate_and_cleanup_program_dir().

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
# FASE 64 cancello 6 (D244.7): il launcher e' quello che il giocatore
# avvia, alla radice di PROGRAM_DIR. Il simulatore (BINARY_NAME, invariato)
# vive sotto bin/, nome da interno, mai collegato dall'avviatore o dalla
# voce di menu direttamente.
BINARY_NAME="ygoprodll"
LAUNCHER_BINARY_NAME="fedelex-launcher"

# Elenco dei posti dove si cerca un'installazione EDOPro gia' presente da
# USARE COME CARTELLA DATI (ci si scrive dentro repositories/deck/replay),
# PRIMA di crearne una nuova. Confermato da Opus il 2026-09-26: resta
# ristretto a $HOME, perche' un /opt/edopro di sistema e' fuori dal vincolo
# centrale — non potremmo scriverci senza sudo, quindi non e' un candidato
# utilizzabile come cartella dati anche se lo trovassimo (resta pero' valido
# come SOLA SORGENTE di configs.json, ricerca separata: vedi
# CONFIG_SOURCE_CANDIDATES sotto).
SEARCH_CANDIDATES=(
	"$DEFAULT_NEW_DATA_DIR"
	"$HOME/EDOPro"
	"$HOME/edopro"
	"$HOME/Games/EDOPro"
	"$HOME/Giochi/EDOPro"
	"$XDG_DATA_HOME/EDOPro"
	# layout di un'installazione EDOPro manuale/portabile sotto $HOME
	# (non un pacchetto di sistema: quella resta esclusa, vedi sopra).
	"$HOME/.local/opt/edopro/app"
)

# Elenco dei posti dove cercare un config/configs.json da COPIARE IN SOLA
# LETTURA quando la cartella dati e' nuova (non un'installazione esistente
# gia' completa di config/). Deliberatamente piu' largo della ricerca sopra:
# qui non scriviamo niente nella sorgente, quindi anche un /opt/edopro
# root-owned va bene da leggere anche se non potremmo mai scriverci dentro.
# La copia dell'utente e' preferita alla nostra bundlata: e' piu' aggiornata
# (design/licensing.md, 2026-09-26).
CONFIG_SOURCE_CANDIDATES=(
	"${SEARCH_CANDIDATES[@]}"
	"/opt/edopro"
	"/usr/share/edopro"
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

# ───────────────────── config/configs.json ─────────────────────────────────
# Senza questo file l'elenco server resta vuoto e il client non lo dice:
# e' esattamente il guasto che questo installatore esiste per impedire (vedi
# intestazione). Deciso il 2026-09-26 (design/licensing.md): si preferisce
# sempre una copia dell'utente, gia' raggiungibile in lettura da qualche
# parte (e' piu' aggiornata della nostra); solo se non se ne trova nessuna
# si usa quella che portiamo nel tarball.
find_configs_json_source() {
	local c
	for c in "${CONFIG_SOURCE_CANDIDATES[@]}"; do
		# -s, non solo -r: un file leggibile ma vuoto (un mount strano, una
		# copia troncata a meta') non e' una sorgente valida, e' peggio di
		# niente — verrebbe copiato com'e' e romperebbe la cartella dati
		# invece di lasciarla senza (trovato testando questa stessa funzione).
		if [[ -s "$c/config/configs.json" && -r "$c/config/configs.json" ]]; then
			echo "$c/config/configs.json"
			return 0
		fi
	done
	return 1
}

ensure_configs_json() {
	local data_dir="$1"
	if [[ -f "$data_dir/config/configs.json" ]]; then
		return 0
	fi

	mkdir -p "$data_dir/config"

	local trovato
	if trovato="$(find_configs_json_source)"; then
		cp "$trovato" "$data_dir/config/configs.json"
		log "config/configs.json copiato da un'installazione trovata: $trovato"
		return 0
	fi

	local bundlato="$SCRIPT_DIR/installer-data/configs.json"
	if [[ -f "$bundlato" ]]; then
		cp "$bundlato" "$data_dir/config/configs.json"
		log "config/configs.json scritto dalla copia inclusa nel pacchetto (nessuna installazione trovata da cui copiarlo)"
		return 0
	fi

	# Non dovrebbe succedere: bundle_linux.sh include sempre installer-data/
	# nel tarball. Se manca e' un difetto di confezionamento, non un normale
	# "niente trovato" — si dice chiaramente invece di installare in silenzio
	# una cartella dati rotta (la stessa trappola P-15 in un'altra forma).
	err "manca config/configs.json e nessuna sorgente e' disponibile: ne' un'installazione"
	err "esistente da copiare, ne' la copia che questo pacchetto dovrebbe includere in"
	err "installer-data/configs.json. Il tarball e' probabilmente confezionato male."
	err "La cartella dati e' stata comunque creata in: $data_dir"
	err "Programma e avviatore verranno installati lo stesso, ma il client non mostrera'"
	err "stanze finche' non copi tu un config/configs.json valido dentro quella cartella:"
	err "  cp <installazione-completa>/config/configs.json '$data_dir/config/configs.json'"
	CONFIGS_JSON_MISSING=1
	return 1
}

# ───────────── pulizia della cartella dati (FASE 61, D241) ─────────────────
# Un vecchio aggiornatore difettoso scompattava lo zip di aggiornamento nella
# CWD invece che accanto all'eseguibile (client-update.md §6bis/§6ter nel
# fork): su un'installazione reale questo ha lasciato `ygoprodll` e
# `ygopro.exe` dentro la cartella DATI (quella passata con -C), non in quella
# del programma. Quel difetto e' corretto a monte, ma i residui restano sul
# disco di chi ha gia' installato e l'utente non deve toglierli a mano.
#
# Regola (D241): si tocca solo cio' che e' chiaramente nostro.
#   1. strings/fedelex.conf si SOVRASCRIVE sempre nella cartella dati: il
#      client lo legge da li' (gframe/data_handler.cpp), non dalla cartella
#      programma, quindi senza questo passo un'installazione che riusa una
#      cartella dati esistente non vede mai le stringhe aggiornate.
#   2. ygoprodll/ygopro.exe nella cartella dati si tolgono SOLO se portano
#      l'impronta "fedelex" (compilata in ogni nostra build, assente
#      nell'EDOPro originale). Un file senza impronta non si tocca: non e'
#      detto che sia nostro.
#   3. .edopro_update_version si toglie sempre: esiste solo nel nostro fork
#      e dopo D238 (CLIENT_UPDATE_VERSION letto dal binario) non lo legge
#      piu' nessuno.
#   4. Nient'altro della cartella dati si tocca (script/, cdb, ocgcore,
#      mazzi, replay, config/).
#   5. Se la cartella dati coincide con la cartella programma, il punto 2
#      non si applica: sarebbe il programma appena installato, non un
#      residuo.
#
# Un file ha "l'impronta fedelex" se la stringa compare al suo interno —
# e' compilata in ogni binario del fork (es. UPDATE_DOMAIN in
# gframe/update_verify.h) e non compare nell'EDOPro ufficiale.
has_fedelex_fingerprint() {
	local file="$1"
	[[ -f "$file" ]] || return 1
	grep -qa fedelex "$file" 2>/dev/null
}

cleanup_data_dir_remnants() {
	local data_dir="$1"
	local program_dir="$2"

	# Punto 1: strings/fedelex.conf si sovrascrive sempre.
	local conf_src="$SCRIPT_DIR/strings/fedelex.conf"
	if [[ -f "$conf_src" ]]; then
		mkdir -p "$data_dir/strings"
		cp "$conf_src" "$data_dir/strings/fedelex.conf"
		log "strings/fedelex.conf aggiornato nella cartella dati: $data_dir/strings/fedelex.conf"
	else
		warn "strings/fedelex.conf non trovato nel pacchetto ($conf_src): la cartella dati non e' stata aggiornata."
	fi

	# Tiene traccia se questa chiamata ha tolto davvero qualcosa, per poter
	# dire esplicitamente "niente da ripulire" quando non c'e' nulla da
	# togliere (reinstallazione su una cartella dati gia' pulita, cancello 3).
	local rimosso_qualcosa=0

	# Punto 5: su cartella dati = cartella programma il binario appena
	# installato ricadrebbe nel punto 2 e verrebbe cancellato per sbaglio.
	if [[ "$data_dir" == "$program_dir" ]]; then
		log "Cartella dati e cartella programma coincidono: salto la pulizia di ygoprodll/ygopro.exe."
	else
		# Punto 2: solo con impronta fedelex, altrimenti non si tocca.
		# D244.8 (FASE 64 cancello 6): estende lo stesso controllo a bin/,
		# perche' un bin/ygoprodll orfano nella cartella dati e' lo stesso
		# residuo del vecchio aggiornatore rotto, solo nel nuovo layout dove
		# il simulatore sta sotto bin/ invece che alla radice.
		local nome
		for nome in ygoprodll ygopro.exe bin/ygoprodll bin/ygopro.exe; do
			local candidato="$data_dir/$nome"
			if [[ -e "$candidato" ]]; then
				if has_fedelex_fingerprint "$candidato"; then
					rm -f "$candidato"
					log "Rimosso residuo con impronta fedelex dalla cartella dati: $candidato"
					rimosso_qualcosa=1
					# bin/ vuota dopo la rimozione non e' nostra: non la si crea mai,
					# ma se e' rimasta vuota a causa di questo rm la si toglie.
					rmdir "$data_dir/bin" 2>/dev/null || true
				else
					log "$candidato presente ma senza impronta fedelex: lasciato intatto."
				fi
			fi
		done
	fi

	# Punto 3: file di versione del vecchio aggiornatore, esiste solo da noi.
	local version_file="$data_dir/.edopro_update_version"
	if [[ -e "$version_file" ]]; then
		rm -f "$version_file"
		log "Rimosso .edopro_update_version dalla cartella dati (non piu' letto da nessuno, D238): $version_file"
		rimosso_qualcosa=1
	fi

	if ((!rimosso_qualcosa)); then
		log "Niente da ripulire nella cartella dati (nessun residuo fedelex trovato)."
	fi
}

# ───────── protezione dei dati dell'utente nella cartella programma ────────
# L'elenco dei file installati da questo script in $PROGRAM_DIR, uno per
# riga, percorso relativo a $PROGRAM_DIR. E' la fonte di verita' su "cosa ci
# ha messo install.sh": qualunque cosa nella cartella programma che non sia
# ne' in questo elenco ne' nel pacchetto nuovo e' un dato dell'utente, per
# definizione, e non si cancella mai.
INSTALLED_FILES_MARKER=".installed-files"

# Elenco dei file che il pacchetto corrente installerebbe in $PROGRAM_DIR,
# percorsi relativi a $SCRIPT_DIR, uno per riga, ordinati. Rispecchia gli
# --exclude passati a rsync piu' sotto: deve restare identico a quella lista
# o il confronto in migrate_and_cleanup_program_dir() sbaglia.
list_package_files() {
	(
		cd "$SCRIPT_DIR" && find . -mindepth 1 \
			\( -name 'install.sh' -o -name 'installer-data' -o -name '*.desktop.in' \
			-o -name 'icon.png' -o -name 'LEGGIMI.txt' \) -prune \
			-o -type f -print
	) | sed 's|^\./||' | sort
}

# Prima di (re)installare il programma: sposta nella cartella dati qualunque
# file nella cartella programma che non e' ne' un file del pacchetto nuovo
# ne' nell'elenco di cio' che un'installazione precedente di QUESTO script
# ci ha messo (D242). Se l'elenco manca (installazione fatta prima di questa
# regola, o prima installazione in assoluto) si comporta allo stesso modo:
# l'insieme "gia' installato da noi" e' vuoto, quindi ogni file estraneo
# finisce spostato, mai cancellato — e' esattamente la garanzia voluta.
#
# Toglie invece (senza spostare) i file che ERANO nell'elenco della vecchia
# installazione ma non sono piu' nel pacchetto nuovo: sono vecchi file
# nostri (es. una libreria non piu' spedita), non dati dell'utente.
migrate_and_cleanup_program_dir() {
	local data_dir="$1"
	[[ -d "$PROGRAM_DIR" ]] || return 0

	local tmp
	tmp="$(mktemp -d)"

	list_package_files >"$tmp/new.txt"

	if [[ -f "$PROGRAM_DIR/$INSTALLED_FILES_MARKER" ]]; then
		sort "$PROGRAM_DIR/$INSTALLED_FILES_MARKER" >"$tmp/old.txt"
	else
		: >"$tmp/old.txt"
	fi

	(
		cd "$PROGRAM_DIR" && find . -mindepth 1 -type f \
			-not -name "$INSTALLED_FILES_MARKER" -not -name "$(basename "$DATA_DIR_MARKER")" -print
	) | sed 's|^\./||' | sort >"$tmp/existing.txt"

	local rel dest base suffix
	while IFS= read -r rel; do
		[[ -n "$rel" ]] || continue

		if grep -qxF "$rel" "$tmp/new.txt"; then
			continue # file del pacchetto: rsync lo sovrascrive subito dopo
		fi

		if grep -qxF "$rel" "$tmp/old.txt"; then
			# Era un file nostro (installazione precedente) ma il pacchetto
			# nuovo non lo spedisce piu': si toglie, non e' dati dell'utente.
			rm -f "$PROGRAM_DIR/$rel"
			log "Rimosso file di una versione precedente non piu' nel pacchetto: $rel"
			continue
		fi

		# Non e' nostro: ne' nel pacchetto nuovo, ne' installato da noi
		# prima. Si sposta nella cartella dati, mai sovrascrivendo.
		dest="$data_dir/$rel"
		mkdir -p "$(dirname "$dest")"
		if [[ -e "$dest" ]]; then
			base="$dest"
			suffix=1
			while [[ -e "${base}.recuperato-${suffix}" ]]; do
				suffix=$((suffix + 1))
			done
			dest="${base}.recuperato-${suffix}"
		fi
		mv "$PROGRAM_DIR/$rel" "$dest"
		log "File non installato da questo script trovato nella cartella programma, spostato nella cartella dati: $rel -> $dest"
	done <"$tmp/existing.txt"

	rm -rf "$tmp"

	# Cartelle rimaste vuote dopo gli spostamenti/cancellazioni sopra
	# (es. deck/, replay/ create solo dal vecchio aggiornatore).
	find "$PROGRAM_DIR" -mindepth 1 -type d -empty -delete 2>/dev/null || true
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

	if [[ ! -f "$SCRIPT_DIR/$LAUNCHER_BINARY_NAME" ]]; then
		err "$SCRIPT_DIR/$LAUNCHER_BINARY_NAME non trovato: questo script va eseguito"
		err "dalla cartella estratta dal tarball, non spostato da solo."
		exit 1
	fi
	if [[ ! -f "$SCRIPT_DIR/bin/$BINARY_NAME" ]]; then
		err "$SCRIPT_DIR/bin/$BINARY_NAME non trovato: questo script va eseguito dalla"
		err "cartella estratta dal tarball, non spostato da solo."
		exit 1
	fi

	local data_dir
	data_dir="$(resolve_data_dir)"
	log "Cartella dati: $data_dir"
	mkdir -p "$data_dir"

	ensure_configs_json "$data_dir" || true # non bloccante: vedi funzione

	cleanup_data_dir_remnants "$data_dir" "$PROGRAM_DIR"

	log "Installo il programma in: $PROGRAM_DIR"
	mkdir -p "$PROGRAM_DIR"

	# D242: prima di scrivere, sposta via qualunque dato dell'utente che si
	# trova nella cartella programma (un vecchio aggiornatore ci salvava
	# mazzi e replay) e toglie solo i file di una nostra installazione
	# precedente che il pacchetto nuovo non spedisce piu'. Niente in questa
	# cartella viene mai cancellato se non e' certo che sia nostro.
	migrate_and_cleanup_program_dir "$data_dir"

	# NIENTE --delete: cancellare per differenza con la sorgente e' proprio
	# il meccanismo che ha perso dati dell'utente (D242) quando la cartella
	# programma era anche, in passato, la cartella di lavoro del client. La
	# pulizia dei file nostri ormai spariti dal pacchetto la fa
	# migrate_and_cleanup_program_dir() sopra, confrontando l'elenco di cio'
	# che abbiamo installato noi, non "tutto cio' che non sta nella sorgente".
	rsync -a \
		--exclude 'install.sh' \
		--exclude 'installer-data' \
		--exclude '*.desktop.in' \
		--exclude 'icon.png' \
		--exclude 'LEGGIMI.txt' \
		"$SCRIPT_DIR"/ "$PROGRAM_DIR"/
	chmod +x "$PROGRAM_DIR/$LAUNCHER_BINARY_NAME"
	chmod +x "$PROGRAM_DIR/bin/$BINARY_NAME"

	list_package_files >"$PROGRAM_DIR/$INSTALLED_FILES_MARKER"
	log "Elenco file installati aggiornato: $PROGRAM_DIR/$INSTALLED_FILES_MARKER"

	echo "$data_dir" >"$DATA_DIR_MARKER"

	local librerie_ok=1
	check_libraries "$PROGRAM_DIR/bin/$BINARY_NAME" || librerie_ok=0

	# D244.7: l'avviatore punta al launcher, non piu' a un wrapper bash
	# generato. E' un SYMLINK (non una copia) al binario appena installato
	# in PROGRAM_DIR, non una copia: fedelex-launcher trova PROGRAM_DIR
	# risolvendo la propria posizione (/proc/self/exe su Linux), che il
	# kernel risolve gia' attraverso il symlink fino al file vero — una
	# copia invece "vivrebbe" in XDG_BIN_HOME e cercherebbe bin/ygoprodll
	# li' dentro, cosa che non esiste. Il launcher stesso legge DATA_DIR da
	# PROGRAM_DIR/.data-dir (scritto sotto) e passa -C al simulatore: questo
	# avviatore non ha piu' bisogno di saperlo.
	log "Collego l'avviatore al launcher: $LAUNCHER -> $PROGRAM_DIR/$LAUNCHER_BINARY_NAME"
	mkdir -p "$XDG_BIN_HOME"
	ln -sf "$PROGRAM_DIR/$LAUNCHER_BINARY_NAME" "$LAUNCHER"

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

	# D242: la cartella programma non si cancella piu' con rm -rf. Si
	# toglie solo cio' che e' nell'elenco di cio' che questo script ci ha
	# messo; qualunque altra cosa trovata li' dentro resta, e lo si dice.
	if [[ -d "$PROGRAM_DIR" ]]; then
		rm -f "$DATA_DIR_MARKER"
		if [[ -f "$PROGRAM_DIR/$INSTALLED_FILES_MARKER" ]]; then
			log "Rimuovo i file installati da: $PROGRAM_DIR"
			local rel
			while IFS= read -r rel; do
				[[ -n "$rel" ]] || continue
				rm -f "$PROGRAM_DIR/$rel"
			done <"$PROGRAM_DIR/$INSTALLED_FILES_MARKER"
			rm -f "$PROGRAM_DIR/$INSTALLED_FILES_MARKER"
			find "$PROGRAM_DIR" -mindepth 1 -type d -empty -delete 2>/dev/null || true

			local rimasti
			rimasti="$(find "$PROGRAM_DIR" -mindepth 1 2>/dev/null || true)"
			if [[ -z "$rimasti" ]]; then
				rmdir "$PROGRAM_DIR" 2>/dev/null || true
				log "Cartella programma vuota, rimossa: $PROGRAM_DIR"
			else
				warn "nella cartella programma restano file che questo script non ha installato:"
				while IFS= read -r rel; do
					[[ -n "$rel" ]] || continue
					warn "  $rel"
				done <<<"$rimasti"
				warn "lasciati intatti: $PROGRAM_DIR"
			fi
		else
			warn "manca l'elenco dei file installati ($INSTALLED_FILES_MARKER, installazione"
			warn "precedente a questa regola): per non rischiare di cancellare dati tuoi non"
			warn "tocco niente dentro $PROGRAM_DIR. Rimuovila a mano se sei sicuro che non ci"
			warn "sia niente tuo li' dentro."
		fi
	else
		log "Cartella programma non trovata: $PROGRAM_DIR"
	fi

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
