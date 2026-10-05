# Il launcher — forma (D236)

## Stato dell'implementazione (FASE 64)

**Aggiornato il 2026-10-02.** Non e' un'intenzione: dice cosa esiste DAVVERO
nel sorgente a questo commit (§1.9 del CLAUDE.md del vault — qui la fonte e'
ancora il sorgente, nessuna release lo porta).

- **Fatto, con test verdi** (`./premake5 --file=tests/premake5.lua gmake2 &&
  make -C tests/build config=release && ./bin/banlist_tests` — 291 check,
  0 fallimenti):
  - Cancello 1: `gframe/update_verify.h/.cpp` legge `launcher_files[]`
    (name/url/sha256/os/role) e filtra per piattaforma + role riconosciuto
    con `SelectLauncherFilesForPlatform()`. Unicita' per (nome, os), non per
    nome solo — `fedelex.conf` e' un upload che installa su entrambe le
    piattaforme (D244 punto 7/8).
  - Cancello 2: nuovo `gframe/launcher_logic.h/.cpp`, funzioni pure senza
    I/O: guardia del ciclo simulatore->launcher, anti-rollback sul proprio
    stato, sostituisci/tieni per hash, decisione della sequenza di verifica
    (hash -> bit eseguibile -> avvio -> installa/torna a `.old`).
  - Cancello 3: `premake5.lua` ha ora il progetto separato
    `fedelex-launcher` (niente irrlicht ne' core), e `launcher/main.cpp` e'
    un eseguibile reale: individua PROGRAM_DIR/DATA_DIR, verifica il
    manifesto firmato, installa i `launcher_files` selezionati e avvia il
    simulatore con `-from-launcher`. Compilato e provato **solo su
    Linux** (`./premake5 gmake2 --no-core=true --sound=sfml
    --no-joystick=true --irrlicht-root=../irrlicht-custom && make -C build
    config=release_x64 fedelex-launcher`): parte e avvia il simulatore in
    <1s senza rete raggiungibile. I rami Windows (`#if _WIN32`, incluso il
    nome `ygopro.exe` di D244.7) seguono lo stesso contratto ma non sono
    MAI stati compilati — nessun toolchain Windows in questo ambiente.
  - Cancello 4: provato a mano in un HOME finto (`program/`, `data/`),
    `python3 -m http.server` locale, manifesto firmato con una **chiave di
    prova generata ad hoc** (mai quella vera — vive solo in
    `/tmp/.../scratchpad`, mai nel repo). Tre scenari reali: sostituzione
    riuscita (hash ok, bit eseguibile impostato, prova di avvio superata,
    `.old` tenuto, processo gia' avviato dalla prova usato come lancio
    vero, `launcher-state.json` scritto per l'anti-rollback), binario che
    non parte (prova di avvio fallita entro 1500ms — numero scelto
    nell'implementazione, non deciso da nessuno prima — rollback senza
    toccare il disco, loggato) e manifesto con SHA-256 malformata
    (rifiutato dallo schema, nessuna modifica). Trovato e corretto nello
    stesso giro: `zenity`/`kdialog`/`notify-send` possono bloccarsi a
    tempo indefinito senza una sessione grafica raggiungibile — ogni
    chiamata esterna ora ha un `timeout`, mai piu' un blocco silenzioso.
  - Cancello 5: `gframe/cli_args.h` + `edopro_main.cpp` hanno il flag
    `-from-launcher`; `gframe/gframe.cpp` chiama
    `RelaunchLauncherIfNeeded()` come primissimo passo di `edopro_main()` —
    usa la guardia pura del cancello 2, cerca il launcher accanto a
    PROGRAM_DIR e lo rilancia se il segno di provenienza manca; se non lo
    trova (build nuda, sviluppo) prosegue senza, mai un rifiuto ad
    avviarsi. `gframe/client_updater.cpp`: `StartUpdate()` ritorna sempre
    `false` (D244 punto 6) — il simulatore non installa piu' niente da
    solo; `CheckUpdates()`/`CheckOnlineGateThreshold()` restano (D237,
    sola lettura). `menu_handler.cpp`: il popup di avanzamento si apre
    solo se l'aggiornamento e' davvero partito.
  - Cancello 7: `banlist/scripts/build_update_manifest.py` emette
    `launcher_files` da una tabella esplicita allegato -> (os, role); un
    allegato assente dalla release non e' un errore (release precedenti a
    questo cablaggio), un role fuori enumerazione lo e'.
  - Cancello 8 (parziale): suite C++ completa verde, `ygoprodll` e
    `fedelex-launcher` compilano ed linkano puliti su Linux,
    `controlla_documenti.py` verde (68 asserzioni). La build Linux
    **del launcher** e' provata (cancello 3); quella del client con le
    modifiche del cancello 5 anche (sopra). Manca ancora la build Windows
    (fuori portata per definizione) e tutto cio' che cancello 6 avrebbe
    dovuto esercitare.
- **Cancello 6 — fatto su Linux, aperto su Windows per costruzione**
  (2026-10-02, terza sessione, fork `87d2ff10c`..`fc3d12560`, vault
  commit `python3 -m unittest` verde su `build_update_manifest.py`):
  - `premake5.lua`: rpath `$ORIGIN/../lib` aggiunto (in piu', non in
    sostituzione) per il simulatore sotto `bin/`. Misurato con `readelf -d`
    sul binario ricompilato: `RPATH = $ORIGIN:$ORIGIN/lib:$ORIGIN/../lib`.
  - `tools/release/bundle_linux.sh`: nuovo layout del tarball —
    `fedelex-launcher` alla radice, `bin/ygoprodll`, `lib/` accanto al
    launcher. Provato: lo script gira pulito, il tarball estratto ha la
    disposizione attesa.
  - `launcher/main.cpp`: le tre chiamate che eseguono il simulatore ora
    passano `-C <data_dir>` (bug trovato testando l'installazione end to
    end: senza, il simulatore chdir sulla propria cartella invece che
    sulla cartella dati, sbagliato ogni volta che le due differiscono —
    il caso comune).
  - `tools/release/installer/install.sh`: dispone il launcher alla radice
    di `PROGRAM_DIR` e il simulatore sotto `bin/` (D244.7); l'avviatore in
    `XDG_BIN_HOME` e' ora un **symlink** al binario del launcher (non un
    wrapper bash generato, non una copia — una copia cercherebbe
    `bin/ygoprodll` nella cartella sbagliata); `cleanup_data_dir_remnants`
    estesa a `bin/ygoprodll`/`bin/ygopro.exe` orfani con impronta fedelex
    nella cartella dati (D244.8). **Provato in HOME finti** (vincolo di
    sessione, mai sul sistema vero): installazione pulita (layout atteso,
    il launcher installato esegue davvero `bin/ygoprodll`, verificato in
    `launcher.log`); conversione di un'installazione FASE 62 preesistente
    (wrapper bash + `ygoprodll` alla radice) — il binario orfano viene
    rimosso, il mazzo dell'utente nella cartella dati resta intatto,
    l'avviatore punta al nuovo launcher; `--uninstall` lascia la cartella
    dati intatta.
  - `.github/workflows/release.yml`, `tools/release/build_linux.sh`: il
    job Linux compila e allega `fedelex-launcher` nudo e `fedelex.conf`
    nudo; il job publish torna ad allegare `ygoprodll.exe` nudo (tolto il
    2026-09-29, riallegato con un ruolo diverso). Questi tre file sono
    per `ALLEGATI_LAUNCHER` nel vault (D244.3): il launcher scarica file
    singoli, non zip, cercati fra gli asset della release per nome
    esatto. `tools/release/artifacts.json` + README aggiornati,
    `check_artifacts_manifest.py` verde.
  - `tools/release/build_windows.ps1`, `package_windows_release.py`:
    aggiornati per compilare e disporre il launcher secondo D244.7
    (`ygopro.exe` = launcher alla radice, `bin/ygoprodll.exe` = simulatore)
    — **mai compilati ne' eseguiti su un runner Windows vero**, nessun
    toolchain in questo ambiente. La logica di staging di
    `package_windows_release.py` e' provata con due binari ELF al posto
    di due PE (stesso codice, stesso `zipfile`): `unzip -l` conferma
    `ygopro.exe` e `bin/ygoprodll.exe` nella disposizione attesa, ma
    questa non e' una prova che compili o che un `.exe` reale si comporti
    cosi'. Fuori portata per definizione fino al primo tag (PHASES.md).
  - `banlist/scripts/build_update_manifest.py` (vault, cancello 7, gia'
    fatto prima di questa sessione): `ALLEGATI_LAUNCHER` usa esattamente
    i tre nomi allegati sopra (`ygoprodll`, `ygoprodll.exe`,
    `fedelex.conf`) — verificato con
    `python3 -m unittest banlist.scripts.tests.test_build_update_manifest`.
  - **Non ancora in una release pubblicata**: tutto questo esiste nel
    sorgente a questo commit, non in un tag (§1.9 CLAUDE.md del vault).
    Il divieto "non pubblicare da questo sorgente" sotto resta, finche'
    non si fa almeno un dispatch/tag di prova che eserciti davvero
    `release.yml` su un runner reale — mai fatto in questa sessione
    (vincolo: niente push/tag/release/dispatch).
- **Il cancello 6 e' passato secondo i suoi stessi criteri** (PHASES.md:
  `check_artifacts_manifest.py` verde, l'installatore in HOME finto produce
  la disposizione D244.7, la pulizia D244.8 verificata con un residuo
  fedelex finto in `bin/`). **Resta da provare prima di un tag reale:**
  la build Windows non e' mai stata compilata (nessun toolchain in questo
  ambiente — fuori portata per costruzione, non un difetto di questo
  cancello) e la pipeline intera non e' mai girata su un runner vero (niente
  dispatch in questa sessione, per vincolo). Il primo tag o
  `workflow_dispatch` su questo sorgente e' la prima prova reale sia della
  build Windows sia della pipeline end-to-end: se qualcosa non regge li', e'
  li' che si scopre, non prima.
- Non e' un punto di risalita (§6.6): nessuna scelta di forma e' rimasta
  aperta, solo lavoro non ancora scritto — vedi PHASES.md, coda del brief
  FASE 64, per cosa riprendere e da dove.

## Stato dell'implementazione (FASE 66)

**Aggiornato il 2026-10-03.** Chiude D247: il launcher ora mostra una
finestra durante un aggiornamento (prima non mostrava niente — "sembra che
non si stia aprendo" era il sintomo riportato) e il simulatore Linux
distribuito scende da ~67 a ~10 MB.

- **Cancello 1 — fatto, con test verdi.** `gframe/launcher_logic.h/.cpp`:
  `FormatDownloadProgress`/`DownloadProgressPercent`, pure, producono il
  testo ("12,3 / 66,1 MB") e la percentuale 0-100 per la finestra. 8 nuovi
  check in `launcher_logic_tests.cpp` (22 totali, 0 fallimenti).
- **Cancelli 2-4 — fatti e provati end-to-end su Linux**, non solo a
  compilazione: `launcher/progress.h/.cpp` (nuovo modulo, pura I/O come
  `main.cpp`). `zenity --progress` se c'e' una sessione grafica raggiungibile
  (`DISPLAY`/`WAYLAND_DISPLAY`) e zenity e' su PATH; altrimenti due
  `notify-send` (inizio/fine), mai una libreria grafica (D244.1). Compare
  solo se almeno un `launcher_files[]` ha davvero bisogno di sostituzione
  (calcolato PRIMA del ciclo di installazione): nessuna finestra "giusto per
  controllare". `Fetch()` accetta ora un callback di avanzamento
  (`CURLOPT_XFERINFOFUNCTION`).

  Provato con un vero binario `fedelex-launcher` ricompilato con una chiave
  Ed25519 di PROVA (generata ad hoc per questa sola sessione, mai quella
  vera — vive solo in `/tmp/.../scratchpad`, `gframe/update_keys.h`
  ripristinato con `git checkout` subito dopo ogni giro), uno zenity FINTO
  messo in testa al `PATH` (mai il binario vero, mai con un `DISPLAY`
  reale) che registra argv e stdin, un server HTTP locale che serve un
  "simulatore" finto (uno script shell) a velocita' ridotta per osservare
  piu' tick di avanzamento, e un `HOME` finto. Quattro scenari osservati
  davvero: percentuali crescenti 0→100 con testo MB corretto e chiusura a
  EOF (stdin chiuso -> il fake zenity esce da solo, come il vero farebbe
  con `--auto-close`); hash gia' allineato -> nessuna invocazione di
  zenity; nessun `DISPLAY`/`WAYLAND_DISPLAY` -> solo due `notify-send`;
  zenity che muore subito dopo l'avvio -> l'aggiornamento finisce lo
  stesso (richiede `signal(SIGPIPE, SIG_IGN)` in `main()`, altrimenti un
  pipe rotto termina il processo).

  **Due difetti trovati testando, corretti nello stesso giro** (non erano
  nel brief, sono emersi dal test):
  1. `NotifyUser()` lanciava il notificatore con `system("timeout 5 ...")`:
     bloccante (il launcher aspettava la finestra) E con un'uccisione a 5
     secondi che chiudeva il messaggio da solo (D247 punto 5 lo segnalava
     come sintomo). Corretto spawnando staccato (`fork`+`execvp`, mai
     un'attesa).
  2. **La sonda di presenza era rotta dalla sua introduzione in FASE
     64/D244**: `timeout N command -v NOME` fallisce sempre, perche'
     `timeout` esegue il suo argomento direttamente (non tramite shell) e
     `command` e' un builtin di shell senza file su `PATH` — verificato con
     `type command` (nessun eseguibile) e riproducendo l'esatta chiamata.
     Significa che **nessuna notifica e' mai stata mostrata in produzione**
     da quando D244 l'ha scritta: il probe diceva sempre "assente". Corretto
     avvolgendo in `sh -c 'command -v NOME'` in entrambi i punti
     (`main.cpp`, `progress.cpp`).
- **Cancello 5 — scritto con cura, compila, MAI eseguito.** Finestra Win32
  nativa (`progress.cpp`, ramo `#if defined(_WIN32)`): `comctl32`
  (`PROGRESS_CLASSW`, `InitCommonControlsEx`), un pompaggio messaggi non
  bloccante (`PeekMessageW`), nessuna libreria nuova. **Non e' agganciata**
  al ramo Windows di `main.cpp`: quel ramo non implementa ancora il ciclo
  di installazione dei `launcher_files[]` (gap aperto da FASE 64, non
  chiuso da questa fase — non c'e' un momento "si sta scaricando un file"
  a cui attaccare la finestra). Non e' un punto di risalita: nessuna forma
  e' rimasta aperta, e' lavoro futuro che aspetta che quel ciclo esista.
  Compila come parte dello stesso progetto `fedelex-launcher` (nessun
  toolchain Windows in questo ambiente — la prima prova vera e' la CI).
  **Chiuso da FASE 67** (sotto): quel ciclo ora esiste e la finestra e'
  agganciata.
- **Cancello 6 — fatto e misurato su una build reale di questa sessione.**
  `tools/release/build_linux.sh`: dopo la build Release,
  `objcopy --only-keep-debug` + `--strip-unneeded` +
  `--add-gnu-debuglink` — misurato: 66 072 880 byte prima, 10 311 808 dopo
  (~10 MB), debug separato 57 731 440 byte. `ldd` riporta la stessa,
  identica chiusura di dipendenze prima e dopo (lo strip non tocca i
  simboli dinamici necessari al linking). `gdb` con `ygoprodll.debug`
  accanto risolve file e riga di `main` senza bisogno di
  `set debug-file-directory`. Il binario stripped parte (provato con
  `DISPLAY` fittizio — mai quello reale — fino al banner di versione e
  all'errore atteso sulla cartella dati di prova assente). Nuovo allegato
  `ygoprodll.debug` in `tools/release/artifacts.json` (destinatario
  persona, mai nel manifesto, mai scaricato dal launcher) e in
  `release.yml` (job `build-linux` e `publish`); `README.md` rigenerato,
  `check_artifacts_manifest.py` verde (9 allegati).

  **Windows (`ygoprodll.exe`): valutato, nessun allegato aggiunto.** Il
  brief (PHASES.md) chiedeva di valutare se allegare anche un `.pdb`, se
  MSVC ne produce uno. Letto `premake5.lua` (§1.9: qui la fonte e' il
  sorgente, non un binario — **nessuna build Windows e' mai girata in
  questo progetto**, quindi non esiste un artefatto da controllare):
  `symbols "On"` e' nel filtro `{"configurations:Release*",
  "action:not vs*"}` (righe 337-338), cioe' **esclude** esplicitamente
  `vs*` — l'azione che `build_windows.ps1` usa (`premake5.exe vs2022 ...`). Senza
  quella riga MSVC non emette `<DebugInformationFormat>` per la
  configurazione Release, quindi (dedotto, non misurato su un binario
  reale che non esiste) oggi non nasce un `.pdb` con informazioni utili.
  Nessun cambio ai file di impacchettamento: non c'e' niente da allegare
  finche' quella riga non cambia. Non e' un punto di risalita (la forma
  degli allegati non cambia, "resta com'e'" e' la risposta alla
  valutazione chiesta, non un disegno nuovo).
- **Cancello 7 — verde.** Suite C++ completa (`banlist_tests`: 291 check
  totali prima di questa fase, 299 ora, 0 fallimenti), build Linux intera
  (`ygoprodll` + `fedelex-launcher`, nessun warning nuovo —
  `warnings "Extra"` + pedantic attivi), `controlla_documenti.py` dalla
  radice del vault verde (71 asserzioni, incluse due corrette in questa
  sessione: `CLIENT_UPDATE_VERSION` era rimasto a 12 nel documento mentre il
  sorgente era gia' a 13 — drift preesistente, non introdotto da questa
  fase, corretto perche' trovato mentre si verificava questo cancello).
- Non e' un punto di risalita (§6.6): nessuna scelta di forma e' rimasta
  aperta. Lavoro futuro, non bloccante (**chiuso da FASE 67, sotto**):
  agganciare la finestra Win32 al ciclo di installazione Windows quando
  quel ciclo verra' scritto; la pipeline di release non e' mai girata su
  un runner vero (niente push/tag/dispatch in questa sessione, per
  vincolo — resta cosi' anche dopo FASE 67).

**Scritto il 2026-10-02, sezione FASE 66 il 2026-10-03.** Decide la forma del componente deciso in D-56 del
registro dei sospesi (vault privato), dopo che ogni correzione
dell'aggiornatore ha richiesto una reinstallazione a mano.

## Stato dell'implementazione (FASE 67)

**Aggiornato il 2026-10-03.** Chiude D249: il ramo Windows di `main.cpp`
implementa ora lo stesso ciclo del passo 4 (`launcher_files`) di Linux —
prima di questa fase quel ramo si limitava ad avviare il simulatore
com'era, senza mai aggiornarlo (il buco che D249 ha misurato: dalla
v0.2.5-alpha i client Windows non si aggiornavano piu').

- **Cancello 1 — non serviva una funzione pura nuova.** `DecideReplace` e
  `DecideSwap` (`gframe/launcher_logic.h/.cpp`) bastano anche per l'ordine
  Windows: l'ordine delle operazioni (D249 punto 2 — su Windows si scambia
  PRIMA e si prova DOPO, il contrario di Linux) e' un fatto di I/O
  sequenziale in `launcher/main.cpp`, non una decisione nuova da
  codificare — le tre stesse domande (hash corretto? bit eseguibile?
  parte?) restano identiche, cambia solo in quale ordine i fatti vengono
  misurati e cosa succede sul disco in caso di esito negativo. Suite
  invariata: `./bin/banlist_tests` verde, 299 check (0 nuovi, nessuno
  perso).
- **Cancello 2 — build Linux pulita, comportamento Linux invariato.**
  `./premake5 gmake2 --no-core=true --sound=sfml --no-joystick=true
  --irrlicht-root=../irrlicht-custom && make -C build config=release_x64
  ygoprodll fedelex-launcher`: nessun warning nuovo (quello preesistente
  su `CURLOPT_PROGRESSFUNCTION` deprecato in `client_updater.cpp` c'era
  gia' prima di questa fase, verificato col vecchio sorgente). Il diff del
  ramo `#if !FEDELEX_WINDOWS`/`#else` e' solo riorganizzazione dei blocchi
  `#if`/commenti — la logica POSIX di `InstallSimulatorFile` e dello step
  5 non e' cambiata una riga. Provato in un HOME finto (scratchpad, mai
  `~/.local/...`): offline (`manifesto irraggiungibile`) avvia il
  simulatore come prima.

  **Incidente di sessione, non del codice**: il primo giro di prova ha
  usato il comando di build esatto del brief, che (D249 lo aveva gia'
  misurato) punta di default all'endpoint reale
  (`seraphinfosfato.github.io/Banlist-dist/update.json`, `newoption` in
  `premake5.lua`). Con `DISPLAY` reale e zenity vero sul `PATH` di questa
  sessione, il launcher ha scaricato il manifesto e il simulatore reali e
  **ha davvero aperto una finestra zenity sul desktop reale** (scomparsa
  da sola, nessuna conferma manuale osservata). Nessun dato scritto,
  nessuna chiave toccata — solo richieste HTTP in lettura verso
  l'endpoint pubblico e una notifica momentanea. Rifatto subito dopo con
  `--update-url` puntato a un indirizzo locale inesistente e con
  `DISPLAY`/`WAYLAND_DISPLAY`/`PATH` svuotati, come le sessioni precedenti
  di FASE 64/66 avevano gia' imposto per questa stessa ragione.
- **Cancello 3 — ramo Windows scritto, MAI compilato.** Nessun
  toolchain Windows in questo ambiente (confermato: nessun
  `i686-w64-mingw32-g++`/`x86_64-w64-mingw32-g++` installato) — la sintassi
  non e' stata verificata da nessun compilatore, solo da lettura attenta.
  `grep -n "not implemented" launcher/main.cpp` e' vuoto. Ogni percorso di
  errore di `InstallSimulatorFile` (Windows) lascia `dest.final_path` un
  file presente su disco — commentato come invariante a ogni
  `RenameFile`.

  **Rinomina preventiva, non un guasto**: l'helper di questo file si
  chiamava `MoveFile`, che `<windows.h>` ridefinisce come macro a
  `MoveFileW` (build UNICODE). Diventava quindi un sovraccarico della
  funzione Win32, che compilava solo perche' i tipi dei parametri sono
  diversi: le build Windows di v0.2.5-alpha e v0.2.6-alpha lo contenevano
  e sono passate. Rinominato in `RenameFile` per togliere la trappola.
  (Una prima versione di questa nota diceva che nessuna build Windows era
  mai arrivata al compilatore: falso, corretto prima del push.)
- **Cancello 4 — certificati.** `gframe/curl.h`: nuovo helper
  `ApplyCurlCertificateConfig()`, usato dai 5 siti che impostavano
  `CURLOPT_CAINFO` condizionatamente (`image_downloader.cpp`,
  `title_checkin.cpp`, `banlist_updater.cpp`, `server_lobby.cpp`,
  `client_updater.cpp`) — `grep -rn "CURLSSLOPT_NATIVE_CA"` li trova tutti
  piu' il launcher (`launcher/main.cpp`'s `Fetch()` non ha un
  `ssl_certificate_path` proprio: imposta l'opzione direttamente nel suo
  unico handle curl). Mai toccato `CURLOPT_SSL_VERIFYPEER`. Guardia di
  versione (`LIBCURL_VERSION_NUM >= 7.71.0`) coerente con lo stile
  esistente del file (`#if` su `CURLOPT_XFERINFOFUNCTION` in
  `client_updater.cpp`), anche se sia il curl di sistema (8.22) sia quello
  vendorizzato per Windows (8.7.0, vcpkg) lo superano ampiamente.
- **Cancello 5 — documenti.** `python3 banlist/scripts/controlla_documenti.py`
  dalla radice del vault: 0 NO (verificato dopo gli aggiornamenti di
  questa sezione e di `launcher/main.cpp`'s commento di testa).
- **Cancello 6 — commit per nome, nessun push.** Vedi PHASES.md FASE 67
  per gli hash.

**Nessuna prova di comportamento Windows**: questa fase scrive e fa
compilare (si spera — nessun toolchain qui) il ramo Windows, non lo
esegue. La prima prova reale e' il job `build-windows` della CI dopo il
push (compilazione), poi un `launcher.log` di un tester Windows
(comportamento). Non e' un punto di risalita: nessuna forma e' rimasta
aperta — vedi i punti di risalita elencati in PHASES.md FASE 67, nessuno
incontrato in questa sessione.

Nessun codice qui: questo documento fissa il contratto. L'implementazione è
doppia (Linux e Windows) e viene dopo.

---

## Stato dell'implementazione (FASE 75)

**Aggiornato il 2026-10-05.** Link `fedelex://tavolo?host=&port=&pass=&nome=`
per entrare in una stanza di torneo con un clic, e "modalita' torneo"
(indicatori di attivazione opzionale nascosti) per le stanze aperte cosi'.

- **Cancello 1 — parsing e validazione del link, testati.**
  `gframe/deep_link.h/.cpp` (nuovo, zero dipendenze oltre
  `BufferIO::DecodeUTF8`): interpreta la query string, rifiuta porta fuori
  range, host vuoto, campi oltre 19 code unit (limite reale imposto da
  `BufferIO::EncodeUTF16` su un buffer `[20]`, non 20 — troncare in
  silenzio avrebbe fatto entrare un nome diverso da quello del link).
  `tests/deep_link_tests.cpp`: 14 funzioni, 21 verifiche. Verificato con
  `./premake5 --file=tests/premake5.lua gmake2 && make -C tests/build
  config=release && ./bin/banlist_tests` → verde, incluso
  `deep_link_tests: 21 checks, 0 failures`.
- **Cancello 2 — ingresso alla stanza, server sconosciuti rifiutati.**
  `DuelClient::JoinFromDeepLink` (`gframe/duelclient.cpp`, accanto a
  `JoinFromDiscord` di cui riusa il pattern) chiama
  `ServerLobby::IsKnownHost` prima di connettersi e mostra un popup invece
  di procedere in silenzio se l'host non e' elencato. **Non verificato con
  un server reale**: non esiste un Multirole locale ne' remoto gia'
  pronto con una lista di host nota da falsificare in questa sessione, e
  costruirne uno per una sola prova avrebbe superato lo scope. Verificato
  invece: (a) le 21 verifiche di `deep_link_tests.cpp` sulla sola analisi
  del link; (b) lettura del punto di chiamata — `IsKnownHost` e' lo stesso
  controllo gia' in produzione per `JoinFromDiscord`, non una funzione
  nuova scritta per l'occasione; (c) build completa (sotto). Il
  round-trip vero resta da provare a mano, in `SOSPESI.md` del vault.
- **Cancello 3 — registrazione dello schema, END-TO-END su Linux.**
  `tools/release/installer/fedelex-edopro.desktop.in` dichiara
  `MimeType=x-scheme-handler/fedelex` e passa `%u` a `Exec`; `install.sh`
  registra lo schema con `xdg-mime default` dopo l'`update-desktop-database`
  gia' esistente. Provato per intero in una HOME finta
  (scratchpad, mai `~/.local/share/fedelex-edopro` ne' `~/.local/opt/edopro`):
  installazione → `xdg-mime query default x-scheme-handler/fedelex` risolve
  il `.desktop` → `xdg-open 'fedelex://tavolo?...'` con un simulatore finto
  (script che registra i suoi argv) al posto del binario vero → il finto
  simulatore riceve `-from-launcher -C <data_dir> -deep-link
  fedelex://tavolo?host=...&port=...&pass=...&nome=...`, lo stesso URI
  passato in ingresso. Primo tentativo con `DISPLAY` svuotato ha dato
  `xdg-open: no method available`: diagnosticato leggendo
  `/usr/bin/xdg-open` — `open_generic_xdg_x_scheme_handler` (la ricerca
  per `x-scheme-handler/<schema>`) gira solo dentro `if has_display`, e
  `has_display()` e' un controllo di stringa su `$DISPLAY`/`$WAYLAND_DISPLAY`,
  mai una connessione vera al server grafico. Rifatto con `DISPLAY=:99`
  (valore finto, nessun display reale dietro, nessuna finestra apribile):
  la ricerca per schema si attiva e il comando riesce. Nessuna finestra
  reale aperta in nessun tentativo. Ramo Windows (`HKCU\Software\Classes\
  fedelex`) scritto e letto con attenzione ma **mai compilato ne' eseguito**:
  nessun toolchain Windows qui, stesso limite gia' registrato per FASE 67.
- **Cancello 4 — niente simulatore singolo, lock provato.**
  `SingleInstanceLock` (flock POSIX / `CreateFileW` esclusivo Windows,
  `gframe/single_instance_lock.h/.cpp`): provato in sandbox tenendo il lock
  aperto a mano mentre il launcher tentava un secondo avvio con un link —
  il launcher ha rifiutato di spawnare, scritto
  `deep-link: un simulatore e' gia' aperto...` nel suo log e non ha aperto
  nessun processo nuovo (verificato sul log delle invocazioni del
  simulatore finto: una sola riga, non due). Rilasciato il lock e ripetuto:
  il launcher ha spawnato normalmente, inoltrando `-deep-link`.
  **Scelta deliberata**: e' una sonda puntuale (apre, controlla, chiude),
  non un servizio in ascolto — nessun socket, nessuna porta, nessun
  processo che resta vivo oltre la vita della sonda. Non e' il punto di
  risalita "serve qualcosa in ascolto sulla macchina": non serve niente di
  nuovo che resti attivo.
- **Cancello 5 — modalita' torneo, verificata a codice e a compilazione,
  non a duello vero.** In `MSG_SELECT_CHAIN`
  (`gframe/duelclient.cpp`), quando `dInfo.isTournamentRoom` e' vero e la
  chain non e' forzata: le carte attivabili perdono `is_selectable` (il
  solo indicatore visivo, `gframe/drawing.cpp`) ma **non** il `cmdFlag` che
  pilota il menu contestuale (`event_handler.cpp`'s `ShowMenu()`) — il
  click per attivare resta identico a una stanza normale. La domanda
  opzionale (popup `wQuery` Si/No) e' sostituita dal pulsante
  Annulla/Fine gia' esistente (`ShowCancelOrFinishButton`), mai nascosta
  senza alternativa. Build completa verificata: `./premake5 gmake2
  --no-core=true --sound=sfml --no-joystick=true
  --irrlicht-root=../irrlicht-custom && make -C build config=release_x64
  ygoprodll fedelex-launcher -j$(nproc)` → nessun warning nuovo (solo il
  deprecato preesistente di `CURLOPT_PROGRESSFUNCTION`, gia' noto da FASE
  67). **Non verificato con un duello reale** (due client, una mano con
  carte attivabili, confronto visivo stanza-link vs stanza-normale): serve
  un server Multirole raggiungibile e due istanze grafiche del simulatore,
  fuori scope sicuro per questa sessione (niente DISPLAY reale, niente
  Multirole gia' pronto con una lista host adatta). In
  `SOSPESI.md` del vault, come prova a mano.
  **Scope deliberatamente non coperto**: `MSG_SELECT_EFFECTYN` (nessuna
  alternativa di click, solo il Si/No del popup) e il `panelmode`
  (attivazioni da mazzo/cimitero/bandito/materiale Xyz: nessun click sul
  campo possibile per queste zone). Nasconderne l'indicatore avrebbe
  bloccato un'attivazione voluta — la stessa guardia del punto di risalita
  del brief, applicata qui come scelta di scope invece che come problema
  da risolvere.
- **Cancello 6 — documenti.** `python3 banlist/scripts/controlla_documenti.py`
  dalla radice del vault: 0 NO.
- **Cancello 7 — commit per nome, nessun push.** Un file o un gruppo
  coerente per commit, mai `git add -A`. Nessun push, nessun tag, nessuna
  release.

**Punti di risalita del brief: nessuno incontrato come blocco.** Il rischio
"nascondere le domande facoltative impedisce un'attivazione voluta" e'
stato evitato per costruzione (si nasconde solo `is_selectable`, mai
`cmdFlag`) invece di essere incontrato e aggirato. Il rischio "serve
qualcosa in ascolto sulla macchina" non si e' posto: `SingleInstanceLock`
e' una sonda, non un servizio.

**Resta non verificato, honestamente**: il round-trip reale di ingresso a
una stanza (cancello 2), la differenza visiva osservata in un duello vero
fra stanza-link e stanza-normale (cancello 4 del brief, qui numerato 5), e
l'intero ramo Windows (compilazione e registro). Tutti e tre richiedono
infrastruttura (un server Multirole con lista host nota, due client grafici,
un toolchain Windows) non disponibile o non sicura da allestire in questa
sessione — non sono stati aggirati ne' dati per scontati.

---

## 1. Il problema, in una riga

L'aggiornatore attuale vive **dentro** il client: il codice che esegue
l'aggiornamento è il codice che viene sostituito. Un suo difetto si può
correggere solo con un aggiornamento eseguito dall'aggiornatore difettoso.
Nessun fix dentro il client scioglie questo nodo — solo separare chi aggiorna
da chi viene aggiornato.

## 2. Il verso del flusso, e perché NON è quello che viene in mente

> **Il launcher è ciò che il giocatore avvia.** Controlla, se serve aggiorna,
> poi esegue il simulatore.

La forma intuitiva è l'opposto — il simulatore parte e chiede all'aggiornatore
se c'è qualcosa — ed è sbagliata per una ragione sola: così l'aggiornamento
dipende dal fatto che il simulatore **riesca a partire**. È esattamente ciò
che non è riuscito il 2026-09-30 (eseguibile senza bit di esecuzione) e il
2026-09-25 (core troppo vecchio per gli script). La via di riparazione non
deve passare per il pezzo che si rompe.

**La stretta di mano simulatore → launcher resta**, ma per un altro scopo: un
aggiornamento che compare *mentre si gioca*. Allora il simulatore avvisa,
ottiene il via libera, **termina il proprio processo** e il launcher
sostituisce e riapre. È un comodo, non la rete di sicurezza, e non va
confuso con essa.

## 3. Cosa fa il launcher, in ordine

1. Verifica di avere un simulatore **presente ed eseguibile** al percorso atteso.
2. Verifica la compatibilità fra **core e script** (§6).
3. Se c'è un aggiornamento da applicare, lo applica — e **ne verifica l'esito**:
   il file c'è, è eseguibile, ed è quello che si aspettava.
4. Se l'esito non regge, **torna indietro** alla copia precedente (§5).
5. Esegue il simulatore, passandogli il segno di provenienza (§4).

## 4. Nomi e posti: rendere difficile sbagliare

Il nome da solo non basta, perché si clicca ciò che si vede.

| Chi | Nome | Dove | Icona / collegamento |
|---|---|---|---|
| launcher | quello che il giocatore già conosce (`Name=EDOPro (Fedelex custom)` in `fedelex-edopro.desktop.in`) | accanto all'icona | **sì, è l'unico** |
| simulatore | `ygoprodll`, nome da interno | in una **sottocartella** (`bin/`) | **mai** |

Per il giocatore non cambia nulla: la voce di menu ha lo stesso nome di oggi,
e l'installatore fa puntare `{EXEC}` al launcher invece che al binario.

**Se il simulatore viene avviato direttamente deve comunque funzionare** —
qualcuno lo farà. Rilancia il launcher, **ma solo se manca il segno di
provenienza** (un argomento o una variabile d'ambiente che il launcher passa
sempre). Senza quella guardia: launcher → simulatore → launcher, ciclo
infinito. È la trappola principale di questo disegno e va coperta da un test,
non da attenzione.

## 5. Il ritorno indietro, al posto di un secondo aggiornatore

Era stata proposta una simmetria: il launcher aggiorna il simulatore, e un
modulo dentro il simulatore aggiorna il launcher — "fallisce solo se entrambi
sono compromessi".

**Scartata**, perché quel presupposto da noi non vale: i due pezzi non
falliscono in modo indipendente. Stessa pipeline, stessa chiave, stesso
canale, stessa cartella scritta dallo stesso codice. I guasti reali — archivio
sbagliato, percorso sbagliato, bit di esecuzione perso, scrittura a metà — li
colpiscono **insieme**, quindi lo scenario "entrambi compromessi" è la regola,
non l'eccezione. E un secondo percorso di aggiornamento gira quasi mai: un
percorso di riparazione non esercitato è un percorso che non funziona.

Al suo posto, due proprietà che costano meno e coprono di più:

- **Rollback locale.** Si conserva la copia precedente che funzionava (il
  `.old` che già produciamo) e ci si torna se la nuova non parte. Non ha
  bisogno della rete né del canale che ha appena fallito.
- **Asimmetria.** Il launcher fa così poco da non avere quasi difetti da
  correggere, e **non sovrascrive sé stesso** durante un aggiornamento
  normale. Un launcher nuovo arriva solo dall'installatore, cioè quasi mai.

## 6. Il core, e la regola che vale più del meccanismo

~~Il launcher gira prima del gioco, quindi può verificare che core e script
siano compatibili.~~ **Ritirato il 2026-10-02**, dopo averlo misurato:

- Il launcher gira **prima** della sincronizzazione dei repository, e la
  sincronizzazione avviene dentro il simulatore. Quindi il launcher non sa
  quale core il repository consegnerà, né se lo scambio riuscirà.
- Non esiste un dato di versione confrontabile: `OCG_GetVersion` risponde
  `11.0` sia per il core di aprile 2025 sia per quello di settembre 2026.
- Su Windows il core è compilato dentro l'eseguibile: non c'è un file da
  controllare. (Vero fino a FASE 60: con D239 anche Windows lo caricherà dal
  repository, e il ragionamento del primo punto varrà anche lì.)

Il controllo si fa **dentro il simulatore**, dopo la sincronizzazione, e
chiude l'online invece di bloccare l'avvio: forma in
[blocco-online.md](blocco-online.md) (D237). Il launcher non ci entra.

> **Un solo scrittore per file.** Il meccanismo di Project Ignis
> (`configs.json`, `has_core: true`) scarica già un core. Il launcher
> **verifica e blocca**, e NON diventa un secondo distributore di core: due
> meccanismi che si contendono lo stesso file sono il guasto di partenza di
> tutta questa storia, in forma più difficile da vedere.

## 7. Cosa il launcher non deve fare MAI

- Sovrascrivere sé stesso durante un aggiornamento ordinario.
- Scaricare o sostituire un core (§6).
- Chiedere privilegi di amministratore, o scrivere fuori dalla cartella utente.
- Proseguire dopo un passo non verificato: ogni sostituzione si controlla, e
  un controllo che non si può fare vale come fallimento.
- Uscire in silenzio. Qualunque esito va **detto**, perché il guasto del
  2026-09-30 era muto e nessuno poteva accorgersene.
- Crescere. Ogni funzione aggiunta erode l'unica promessa che giustifica
  questo componente: *non dovrai più reinstallare a mano.*

## 8. Cancelli di qualità, in ordine

| # | Passo | Si passa oltre solo se |
|---|---|---|
| 1 | Il launcher esegue il simulatore quando non c'è niente da fare | parte, e il ritardo aggiunto è impercettibile |
| 2 | Simulatore avviato direttamente | rilancia il launcher **una volta sola**: nessun ciclo, dimostrato da un test, non dall'osservazione |
| 3 | Aggiornamento applicato bene | il simulatore nuovo parte, e il segno di provenienza arriva |
| 4 | Aggiornamento che produce un eseguibile **senza bit di esecuzione** (il guasto reale: lo si riproduce togliendolo a mano) | il launcher lo rileva, lo corregge o torna indietro, e **lo dice** |
| 5 | Aggiornamento che non contiene affatto l'eseguibile | torna alla copia precedente, che parte |
| 6 | Core più vecchio di quanto gli script richiedono | il launcher lo dice con una frase comprensibile **prima** del gioco, mai un errore Lua a metà partita |
| 7 | Aggiornamento comparso mentre si gioca | il simulatore termina, il launcher sostituisce e riapre; nessun dato di gioco perso |
| 8 | Launcher stesso danneggiato | reinstallarlo (wizard su Windows, script su Linux) ripristina tutto, senza toccare dati o configurazione del giocatore |

## 9. Cosa questo documento NON decide

- ~~Se un core troppo vecchio blocca l'avvio o solo avvisa~~ **Deciso
  dall'utente il 2026-10-02 (D237):** non blocca l'avvio, **chiude
  l'online**, con 60 minuti di grazia per chi è in partita. Non è più compito
  del launcher: [blocco-online.md](blocco-online.md).
- ~~Come si riconosce "il core è troppo vecchio"~~ **Misurato il
  2026-10-01:** non con la versione del core (identica fra un core di aprile
  2025 e uno di settembre 2026). I segnali veri sono in
  [blocco-online.md](blocco-online.md) §2.
- **Il formato con cui il launcher scopre cosa c'è da aggiornare.** Oggi il
  manifesto è firmato e non distingue i sistemi operativi (D-54): chi
  implementa non deve inventare uno schema.
- **Le due forme d'installazione** (wizard su Windows, script su Linux) sono
  decise; il loro contenuto no.

## 10. Punti di risalita per chi implementa

Oltre a quelli permanenti (§6.6 del `CLAUDE.md` del vault):

- Il cancello 2 non si può scrivere come test → la guardia contro il ciclo è
  mal disegnata, non si aggira il test.
- Serve un numero che nessuno ha deciso: quante copie precedenti conservare,
  quanto attendere una risposta, un tetto di dimensione.
- Serve toccare lo schema del manifesto firmato (§9) o il meccanismo dei
  repository di Project Ignis (§6).
- Il launcher avrebbe bisogno di privilegi, o di scrivere fuori dalla
  cartella utente: fermarsi, è un errore di disegno.
