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

## Stato dell'implementazione (FASE 75b)

**Aggiornato il 2026-10-06.** D252 (`design/decisioni.md`,
`design/server-duelli.md` §13.5): FASE 75 nascondeva solo l'evidenziazione
delle carte attivabili (`is_selectable`); l'utente ha chiarito che anche il
fatto stesso che una domanda compaia o non compaia, e i pannelli che
elencano solo le carte attivabili, sono un indizio. Ramo `fase-75b`, creato
da `fase-75` (rimasto a `9d9299288`, invariato), 7 commit, pushato su
`origin/fase-75b`. Nessuna PR, nessun merge: questa sezione descrive uno
stato non ancora unito a `master`.

- **Cancello 1 — funzione pura e test.** `gframe/tournament_mode.h/.cpp`
  (nuovo, zero dipendenze gframe/irrlicht/rete, stesso schema di
  `deep_link.h`): quattro funzioni, una per decisione — `ChainAutoPasses`
  (punto 1: mai la risposta automatica a una catena opzionale in torneo),
  `AutoChainOrderApplies` (punto 5: l'ordine automatico di catena non si
  applica mai in torneo), `ZoneMenuFlags` (punti 2/3: una zona non vuota
  offre solo "Guarda", mai "Attiva"/"Evoca speciale" a livello di zona),
  `EffectYNIsConcealed` (punto 4: `MSG_SELECT_EFFECTYN` e' nascosto fuori
  dalla risoluzione di un'altra catena, non durante). `tests/
  tournament_mode_tests.cpp`: 23 verifiche, ognuna o fissa
  `isTournamentRoom == false` e controlla che il risultato sia la formula
  esatta di prima di FASE 75b, o fissa `== true` e controlla la regola di
  D252. Verde: `./bin/banlist_tests` → `tournament_mode_tests: 23 checks,
  0 failures`.
- **Cancello 2 — testo unico, verificato con `grep`.** Ogni punto dove il
  client scrive il testo del popup di catena o di `MSG_SELECT_EFFECTYN`
  e' stato letto a mano:
  - `duelclient.cpp`, `MSG_SELECT_CHAIN`: in torneo e non forzata,
    `stQMessage` prende sempre e solo la sysstring 1395 ("Vuoi attivare un
    effetto?"), mai `event_string` ne' il testo che varia per
    count/select_trigger/effetto scatenante — lo stesso ramo vale anche
    per `panelmode` (materiali Xyz), che FASE 75 escludeva esplicitamente
    (vedi sopra, cancello 5: "scope deliberatamente non coperto").
  - `duelclient.cpp`, `MSG_SELECT_EFFECTYN`: `concealed` e' calcolato
    PRIMA di costruire qualunque stringa — se vero, il ramo che chiama
    `GetName(code)`/`FormatLocation(...)` non viene eseguito affatto (non
    e' nascosto a schermo: non viene costruito), e si usa la stessa
    sysstring 1395; `is_highlighting`/`highlighting_card` restano
    entrambi intatti (mai assegnati).
  - `event_handler.cpp`: `ShowCancelOrFinishButton` ha un nuovo `case 3`
    con la sysstring 1396 ("Non attivo niente"), condiviso dalla pausa di
    catena e da quella di `EFFECTYN` — nessun testo diverso fra le due.
  - Le uniche stringhe non toccate in quel momento sono `stHintMsg`
    (sysstring 550/556, "Select the effect you want to activate"/"...to
    resolve"): preesistenti, identiche in torneo e fuori, mai
    card-specific — non sono un indizio nuovo.
  - Le due sysstring nuove (1395/1396) sono in `strings/fedelex.conf`
    (non in `runtime/`, che e' gitignored e non versionato — vedi il
    commento nel file stesso), codici 1393-1399 verificati vuoti
    nell'intervallo upstream 1392→1400 prima di scegliere i numeri.
- **Cancello 3 — build.** Linux locale: `./premake5 gmake2 --no-core=true
  --sound=sfml --no-joystick=true --irrlicht-root=../irrlicht-custom &&
  make -C build config=release_x64 ygoprodll` → verde, nessun warning
  nuovo. Suite dei test: `make -C tests/build config=debug banlist_tests`
  da zero → verde, tutte le suite (`banlist_tests`, `tournament_mode_tests`
  incluso). CI del fork sul ramo `fase-75b`, **due run, entrambe verdi
  dopo il push finale**: `build-windows` success, `Test (banlist_tests)`
  success (fa girare anche `tournament_mode_tests` su una macchina pulita,
  non solo in locale), `Manifesto artefatti (D218)` success, `Build Linux
  (client)` skipped — stesso comportamento gia' osservato su `fase-75`,
  non una regressione di questo ramo.
- **Cancello 4 — nessuna attivazione persa, percorso di clic per ogni
  zona.** In torneo, durante la pausa (dopo "Si'" alla domanda generica,
  o per `MSG_SELECT_EFFECTYN` fuori dalla risoluzione):

  | Zona | Percorso di clic |
  |---|---|
  | Mano | Clic diretto sulla carta (`LOCATION_HAND`) → `ShowMenu(cmdFlag)` apre il menu con "Attiva" se il bit c'e' — identico a fuori torneo, mai filtrato prima |
  | Campo (mostri/magie-trappole) | Clic diretto sulla carta (`LOCATION_MZONE`/`LOCATION_SZONE`) → stesso `ShowMenu(cmdFlag)` |
  | Cimitero | Clic sulla zona → `ZoneMenuFlags` forza il menu a solo "Guarda" (`COMMAND_LIST`) → "Guarda" apre la lista **completa e non filtrata** del cimitero → clic sulla carta nella lista: se ha un `cmdFlag` si apre lo stesso `ShowMenu(cmdFlag)` di un clic diretto sul campo; se non ne ha, nessuna reazione (silenzioso, non un indizio: ogni altra carta della stessa lista e' altrettanto silenziosa se non ha niente) |
  | Banditi (removed) | Stesso percorso del cimitero, `LOCATION_REMOVED` |
  | Deck | Stesso percorso, `LOCATION_DECK` (gia' limitato a "Guarda" anche in `isSingleMode`, qui lo e' sempre in torneo) |
  | Extra deck (fase principale, es. evocazione speciale) | Stesso percorso, `LOCATION_EXTRA`; il clic su una carta della lista con `cmdFlag` apre `ShowMenu`, da cui "Evoca speciale" segue lo stesso `BUTTON_CMD_SPSUMMON` di un clic diretto sul campo |
  | Materiali Xyz (catena) | "Si'" alla domanda generica, quando i candidati erano materiali (`panelmode`): si apre un pannello "Guarda" con **tutti** i materiali di ogni mostro Xyz candidato (non solo quello attivabile — stessa regola delle zone sopra), `ShowChainCard()`. Clic su un materiale che e' davvero nella lista attivabile: risolve subito (una sola opzione) o apre la scelta delle descrizioni (`ShowSelectOption`, piu' opzioni). Clic su un materiale presente nel pannello ma non attivabile: nessuna reazione (corretto il 2026-10-06 un vettore vuoto non controllato in questo ramo, vedi sotto) |

  Nessuna delle sette non ha un percorso: la tabella e' la prova richiesta
  dal brief, non solo la sua descrizione.
- **Un difetto trovato rileggendo il codice per questo cancello, non
  durante la scrittura.** Il pannello materiali Xyz in torneo mostra il
  set COMPLETO di materiali (`all_materials`), non il sottinsieme
  attivabile come fa `selectable_cards` fuori torneo (dove coincide sempre
  con `activatable_cards`). Cliccare un materiale presente nel pannello ma
  non nella lista attivabile lasciava `select_options` vuoto, e il ramo
  `else` di quel blocco in `event_handler.cpp` leggeva `select_options[0]`
  senza controllare la dimensione — un vettore vuoto letto in lettura,
  mai esercitato prima perche' irraggiungibile fuori da questo percorso
  nuovo. Commit `bed066bc6`, stessa famiglia del controllo gia' messo in
  `BUTTON_CMD_ACTIVATE`/`BUTTON_CMD_RESET` per un caso analogo (clic da
  "Guarda" su una carta senza niente da offrire).

**Punti di risalita del brief: nessuno incontrato come blocco.** In
particolare "la vista 'Guarda' di una zona non puo' offrire comandi sulla
singola carta senza riscrivere il meccanismo dei menu" e' stato evitato per
costruzione: il clic su una carta nella lista richiama lo stesso
`ShowMenu(cmdFlag)` di un clic diretto sul campo (`clicked_card`,
`list_command = 0`), non un meccanismo parallelo. "Distinguere 'durante la
risoluzione' richiede uno stato che il client non ha in modo affidabile" e'
stato risolto leggendo `ocgcore/processor.cpp` (`Processors::SolveChain`):
`core.chain_solving` e' vero esattamente fra l'invio di `MSG_CHAIN_SOLVING`
e quello di `MSG_CHAIN_SOLVED`, quindi uno specchio booleano lato client
(`ClientField::in_chain_resolution`, impostato in quei due punti) e' una
misura, non un'ipotesi.

**Resta non verificato, honestamente**: nessun duello reale giocato in una
stanza di torneo (niente DISPLAY, niente Multirole con mano di prova in
questa sessione) — la tabella del cancello 4 e' letta dal codice, non vista
a schermo. Il ritmo della domanda ripetuta a ogni finestra (se risponde
"No" decine di volte per turno sia vivibile) non e' misurabile da codice
per definizione: resta in P-40 di `SOSPESI.md` del vault, aggiornata con lo
stato di questa fase. Il ramo Windows e' verificato solo dalla CI
(`build-windows` success): nessuna esecuzione su un Windows vero, stesso
limite gia' registrato per FASE 75 (P-39).

## Stato dell'implementazione (FASE 76b)

**Scritto il 2026-10-07 (Sonnet).** D251, design/server-duelli.md §13.6
punto 5. Worktree separato `~/Progetti/edopro-fase-76b`, ramo `fase-76b`
creato da `fase-75b` (invariato), pushato su `origin/fase-76b`. Il
worktree principale (`EdoproForkGSY/edopro_custom`, restato su `fase-75b`)
non e' stato toccato, come richiesto. Il server della FASE 76a era ancora
in lavorazione da un'altra sessione (worktree
`~/Progetti/multirole-fedelex-76`, ramo `fase-76`) mentre questa fase
procedeva: letto, mai modificato — vedi sotto per cosa quella lettura ha
confermato.

**Cosa implementato, tutto dietro `dInfo.isTournamentRoom`** (fuori torneo
nessun ramo nuovo entra mai, byte per byte come oggi):

- `gframe/tournament_mode.h/.cpp`: quattro funzioni pure nuove —
  `ReconnectShouldActivate` (una caduta apre l'overlay invece della
  pulizia di oggi solo se `isTournamentRoom && isInDuel`),
  `ReconnectAttemptDue` (cadenza dei tentativi, 2000ms — scelta
  dell'implementazione, non un numero che D251 ha deciso),
  `ReconnectGivesUp` (qualunque `JOINERROR` esplicito su un tentativo di
  rientro significa arrendersi: la stessa identica `CTOS_JOIN_GAME` e'
  gia' stata rifiutata una volta), `ReconnectBlocksInput` (il punto unico
  da cui dipende il blocco di tastiera e mouse). 10 nuovi test in
  `tests/tournament_mode_tests.cpp` (33 totali nel file, 0 fallimenti).
- `gframe/duelclient.cpp`, `HandleSTOCPacketLanAsync`'s
  `INTERNAL_HANDLE_CONNECTION_END`: una caduta mid-duello/mid-match in
  torneo non tocca piu' `dField`, `lp`, `isInDuel` o il ricevitore eventi
  — apre solo l'overlay bloccante (`wReconnecting`, nuova finestra in
  `game.h`/`game.cpp`, stesso stampo di `wQuery` ma senza bottoni) e arma
  `DuelClient::last_reconnect_attempt`.
- `DuelClient::TournamentReconnectTick()`, chiamata ogni frame da
  `Game::MainLoop` mentre `isAwaitingReconnect` e' vero (stesso schema
  gia' in uso per `DuelClient::try_needed`, poche righe sopra nello
  stesso loop): ritenta `StartClient()` con host/porta/password/nome
  **mai scritti su disco** — restano in memoria in `dInfo.secret`/
  `ebNickName` dal `JoinFromDeepLink` originale (FASE 75), perche' il
  processo non si e' mai fermato: e' solo la rete che e' caduta. Stesso
  meccanismo che `JoinFromDiscord` usa gia' per lo stesso scopo.
- Il segnale di rientro riuscito e' `STOC_CATCHUP(false)`: **e' lo stesso
  meccanismo gia' usato per uno spettatore che si unisce a meta' duello**
  (`isCatchingUp`, usato in tutto `duelclient.cpp` da prima di questa
  fase), non uno nuovo — verificato leggendo
  `src/Multirole/Room/State/Dueling.cpp` della FASE 76a
  (`~/Progetti/multirole-fedelex-76`, commit `5f7cd93`): il server
  incornicia il replay della cache per-posto in
  `MakeCatchUp(true)/(false)` esattamente come gia' fa per il percorso
  spettatore nella stessa funzione. La posizione del giocatore
  (`selftype`/`dInfo.player_type`) non ha bisogno di un nuovo
  `STOC_TYPE_CHANGE` dal server (che infatti il codice letto non manda
  mai durante un `Event::Reconnect`): non viene mai azzerata da una
  caduta ne' da `StartClient()`, quindi al rientro vale ancora quella di
  prima del drop — **punto di risalita del brief risolto per
  costruzione**, non aggirato (stesso stile di FASE 75 coi suoi due
  rischi).
- `ReplayPrompt()`/`ERROR_TYPE::JOINERROR`: un `JOINERROR` esplicito
  ricevuto mentre si attende un rientro chiude per davvero il duello
  (sysstring 1398, "Non e' stato possibile rientrare: il tavolo non e'
  piu' disponibile per te"), eseguendo solo ora la pulizia rimandata alla
  caduta — `ReplayPrompt()` prende `gMutex` da solo, quindi nel nuovo ramo
  gira PRIMA del `lock_guard`, mai dentro (un errore di questo tipo
  sarebbe stato un deadlock silenzioso al primo utilizzo, trovato leggendo
  il codice prima di scriverlo, non in fase di test).
- `ConnectTimeout`: un tentativo di rientro il cui connect scade (5s,
  nessuna risposta) non deve piu' mostrare la pulizia da lobby (sysstring
  1400) ne' abbandonare il match in corso — resta in attesa del prossimo
  tick. Trovato leggendo il codice per capire ogni percorso che un
  tentativo di `StartClient()` puo' imboccare, non durante la scrittura:
  senza questa guardia, un semplice timeout di rete durante UN tentativo
  di rientro avrebbe chiuso la partita al posto di aspettare il
  prossimo giro.
- `gframe/event_handler.cpp`, `ClientField::OnEvent`: un'unica guardia in
  testa (`tournament_mode::ReconnectBlocksInput`) inghiotte ogni evento di
  tastiera e mouse mentre si rientra — e' il ricevitore eventi unico per
  tutta la durata di un duello (`STOC_DUEL_START` lo imposta), quindi una
  guardia sola copre click sulle carte, bottoni e scorciatoie.
- `strings/fedelex.conf`: due sysstring nuove (1397, 1398), in **italiano**
  a differenza di 1395/1396 (inglese, FASE 75b) — questa volta il testo
  e' quotato alla lettera nel brief e nel documento di design, non una
  scelta libera di traduzione, e coerente con le stringhe di chat che il
  server della FASE 76a manda gia' in italiano per lo stesso evento
  (`I18N.cpp`, `CLIENT_ROOM_FEDELEX_DISCONNECTED`/`RECONNECTED`).

**Build e test.** `./premake5 --file=tests/premake5.lua gmake2 && make -C
tests/build config=release && ./bin/banlist_tests` → verde,
`tournament_mode_tests: 33 checks, 0 failures` (gli altri 2 fallimenti
preesistenti di `banlist_tests` sono una fixture mancante,
`runtime/repositories/lflists/OCG.lflist.conf`, non versionata — non
toccati da questa fase). Build Linux completa pulita da zero (rimosso
`obj/x64/Release/ygoprodll` e ricompilato):
`./premake5 gmake2 --no-core=true --sound=sfml --no-joystick=true
--irrlicht-root=../irrlicht-custom && make -C build config=release_x64
ygoprodll fedelex-launcher` → nessun warning nuovo (confrontati i file
toccati uno per uno: ogni warning rimasto e' la stessa deprecazione
preesistente di `fmt::sprintf` gia' nota dalle fasi precedenti, nessuno
sulle righe aggiunte qui). `python3 banlist/scripts/controlla_documenti.py`
dalla radice del vault: verde (75 asserzioni) — verifica solo lo stato del
worktree principale (`fase-75b`), non ancora di questo ramo, perche'
nessuna asserzione nuova e' stata aggiunta in questa fase.

**CI del fork sul ramo `fase-76b`**: verde, run
[37610570993](https://github.com/SeraphinFosfato/edopro-genesys-fedelex/actions/runs/37610570993)
— `Test (banlist_tests)` success, `Manifesto artefatti (D218)` success,
`build-windows` success, `Build Linux (client)` skipped (stesso pattern
delle fasi precedenti, non una regressione).

**Resta non verificato, honestamente**: nessun rientro reale contro il
server della FASE 76a (worktree `~/Progetti/multirole-fedelex-76`,
ancora senza un cancello dal vivo passato quando questa fase e' arrivata
qui — nessun container di prova gia' acceso, costruirne uno avrebbe
superato lo scope di una fase client, niente `DISPLAY` reale consentito
per un client grafico comunque). Verificato invece, con la stessa onesta'
di FASE 75/75b: lettura diretta del sorgente server (non supposizione) per
capire cosa manda davvero al rientro, lettura del meccanismo client
esistente (`isCatchingUp`) che gia' fa lo stesso lavoro per uno spettatore,
compilazione pulita, suite di test verde. **Un caso limite dichiarato, non
risolto**: se il tentativo di rientro arriva mentre la finestra lato
server e' GIA' scaduta per una corsa (il server allora fa cadere il
client su `SetupAsSpectator`, stesso `STOC_CATCHUP` usato per un successo
vero — letto in `Dueling.cpp`), il client qui smette silenziosamente di
aspettare e mostra il duello come spettatore, senza un messaggio dedicato
che distingua questo caso da un rientro riuscito. E' un caso limite di una
corsa, non il percorso principale: P-42 in `SOSPESI.md` lo tiene aperto.

---

## Stato dell'implementazione (FASE 83)

**Scritto il 2026-10-09 (Sonnet).** Worktree separato
`EdoproForkGSY/edopro-fase-83`, ramo `fase-83` creato da `master`
(`1a262c4bc`), pushato su `origin/fase-83`. Il worktree principale non e'
stato toccato; niente merge, niente tag.

La connessione di gioco ai server che lo dichiarano e' **cifrata (TLS)**.
Questo cambia due cose dette sopra: il controllo "server noto" del link
`fedelex://` (FASE 75, cancello 2) e il modo in cui `DuelClient` apre il
socket.

- **Campo `"tls": true`** nelle voci di `servers[]` di `configs.json`
  (`gframe/server_tls.h`, `ParseTlsField`). Assente o `false` = comportamento
  di prima, byte per byte (le voci di Project Ignis non sono cambiate). Un
  valore che non sia un booleano **scarta la voce** con una riga nel log:
  leggere `"tls": "true"` come "in chiaro" manderebbe in chiaro le password
  dei posti a un server che l'operatore voleva cifrato. Una voce TLS non
  richiede `roomaddress`/`roomlistport` e **non compare nell'elenco stanze**
  (si entra solo dal link): non e' nel menu a tendina, ma occupa comunque il
  suo posto in `ServerLobby::serversVector`, e il menu porta l'indice vero
  come dato della voce (`getItemData`) invece di contare le posizioni.
- **L'identita' di un server TLS e' il suo nome.** `ServerLobby::FindTlsServer`
  confronta nome e porta del link con le voci TLS (senza distinguere maiuscole,
  ignorando un solo punto finale; `TlsServerMatches`). `JoinFromDeepLink`
  connette poi **col nome della nostra lista**, mai con quello scritto nel
  link. `ServerLobby::IsKnownHost` (indirizzo risolto) resta il confronto per
  tutti gli altri e ora **salta** le voci TLS: un indirizzo non prova niente per
  un server cifrato, e un invito Discord verso l'IP di un server TLS resta
  "sconosciuto" (sys string 1468) invece di passare.
- **Come si cifra: un filtro libevent con OpenSSL** (`gframe/tls_client.h/.cpp`,
  `bufferevent_filter_new` + memory BIO), non `bufferevent_openssl_socket_new`.
  Motivo verificato: la cache vcpkg MinGW di edo9300 contiene `libevent`,
  `libevent_core`, `libevent_extra` e nessun `libevent_openssl` ne'
  `event2/bufferevent_ssl.h` (`ls .../installed/x86-mingw-static/lib`); ricompilare
  libevent sarebbe stata una dipendenza nuova. La cache VS2022 che usa la CI
  non e' stata scaricata a mano: il filtro non dipende da quella scelta. Un
  unico percorso per Linux e Windows. OpenSSL c'era gia' (curl); su Linux a
  sistema `gframe/premake5.lua` ora linka `ssl` e `crypto` (prima solo il ramo
  vcpkg).
- **Verifica obbligatoria, senza interruttore**: `SSL_VERIFY_PEER`, SNI = nome,
  nome controllato sul certificato (`SSL_set1_host`; per un indirizzo letterale
  `set1_ip_asc`, senza SNI), TLS 1.2 minimo. Nessun `verify=none`, nemmeno come
  opzione. **Archivio dei certificati**: se esiste il `cacert.pem` esplicito
  (`ssl_certificate_path`) si fida **solo** di quello, la stessa priorita' dei
  curl handle (`gframe/curl.h`); su Windows legge l'archivio di sistema "ROOT"
  (`CertOpenSystemStoreW`, D249 punto 1, quello che curl raggiunge con
  `CURLSSLOPT_NATIVE_CA`); altrove i percorsi predefiniti di OpenSSL (che
  rispettano `SSL_CERT_FILE`/`SSL_CERT_DIR`) piu' le posizioni delle
  distribuzioni piu' comuni, perche' un `libssl` impacchettato porta i percorsi
  della distribuzione che l'ha compilato.
- **`CONNECTED` arriva solo a handshake finito e certificato verificato**; ogni
  guasto (rete, handshake, certificato, nome) e' `BEV_EVENT_ERROR` e il motivo e'
  gia' nel log, una riga ("certificato non accettato per '<nome>':
  self-signed certificate", "hostname mismatch", "unable to get local issuer
  certificate", "connessione di rete non riuscita: Connection refused",
  "wrong version number" se la porta non parla TLS). L'utente vede «Connessione
  cifrata al server non riuscita. Il motivo e' nel file di log.»
  (`DuelClient::ConnectFailedMessage`), per tutte le cause.
- **Un solo lucchetto.** Il filtro e' creato **senza** `BEV_OPT_THREADSAFE`:
  con due lucchetti libevent li prenderebbe in ordine opposto (il ciclo di
  eventi: socket poi filtro; chi scrive: filtro poi socket). Tutto si
  serializza sul lucchetto del socket grezzo, e ogni scrittura verso il server
  passa da `DuelClient::WriteToServer` (anche le tre `SendPacketToServer`).
- **Rientro e nuovo tentativo di versione.** `StartClient(..., tls_name)`
  ricorda il nome in `DuelClient::temp_tls_name`, accanto a `temp_ip`/
  `temp_port`: `TournamentReconnectTick` (FASE 76b, passa da `StartClient`, non
  serve un altro percorso) e il nuovo tentativo `try_needed` di `Game::MainLoop`
  riaprono la connessione allo stesso modo. Lo spettatore entra dal link come
  ogni altro giocatore. Invariati: ospitare in LAN, ingresso per IP, replay, IA,
  Discord.
- **Voce nostra**: `Fedelex`, `fedelex-duelli.quoll-ruffe.ts.net`, porta 443, `tls: true`.
  Scritta in `installer-data/configs.json` dalla 83; **dalla 83c vive nell'eseguibile**
  (vedi "83c" sotto), perche' l'aggiornamento non consegna `configs.json`.

**Cancelli.**

- **Cancello 1 (funzioni pure)**: `tests/server_tls_tests.cpp`, 7 funzioni, 20
  verifiche (campo assente/vero/falso/non booleano, nome senza maiuscole e
  punto finale, nomi "quasi uguali" rifiutati, vuoto mai uguale, nome+porta,
  indirizzo letterale mai uguale a un nome). Verde in locale e nel job
  "Test (banlist_tests)" della CI sul ramo.
- **Cancello 2 (build)**: `tools/release/build_linux.sh release` pulita da zero,
  nessun avviso nuovo; `ygoprodll` linka `libssl`/`libcrypto`.
  Windows: job `build-windows` della CI sul ramo (run 37985433053, commit
  `2c868568b`), verde in tutti i passi: MSVC compila e collega
  `tls_client.cpp` con l'OpenSSL della cache vcpkg.
- **Cancello 3 (client contro server TLS di prova)**: `tools/tls_probe/tls_probe.cpp`
  usa `tls::NewClient`, la stessa funzione che `DuelClient::StartClient` chiama
  per un server TLS (non un `DuelClient` intero: legge `mainGame` e irrlicht).
  Contro `openssl s_server` su 127.0.0.1, con `HOME` finto e `env -i`:
  certificato autofirmato -> **rifiutato** ("self-signed certificate");
  certificato di una CA di prova offerta solo a quel processo (`SSL_CERT_FILE` o
  `--ca`, nessun archivio di sistema toccato) -> **accettato**, e un'andata e
  ritorno cifrato riesce; certificato valido per un altro nome -> **rifiutato**
  ("hostname mismatch"); stessa CA non offerta -> **rifiutato**; server in chiaro
  sulla porta -> rifiutato ("wrong version number"); porta chiusa -> errore di
  rete nel log. Gli stessi casi sotto AddressSanitizer + UBSan: puliti.
- **Cancello 4 (nome pubblico)**: vedi la nota in PHASES.md FASE 83, perche' la
  rotta provata non e' una rotta pubblica.

**Non verificato.** Windows e' stato solo compilato, mai eseguito (nessuna
macchina Windows qui): l'archivio "ROOT" non e' mai stato letto davvero. Nessun
duello completo con il client grafico (niente `DISPLAY`): `DuelClient` intero
non e' mai stato istanziato con un server TLS. Il timeout di connessione di 5 s
(`ConnectTimeout`) copre ora anche l'handshake: non e' stato cambiato (sarebbe
un numero nuovo da decidere) e la FASE 72b ne aveva visto uno scadere dopo 8 s
al primo uso.

---


### 83b: correzioni dalla revisione di sicurezza (2026-10-09)

Una revisione in sola lettura (sorgente di libevent 2.1.12) ha chiesto sette correzioni, tutte fatte, ciascuna in un commit suo.
- **Eventi differiti.** `gframe/tls_client.cpp` non usa piu' `bufferevent_trigger_event(..., BEV_TRIG_DEFER_CALLBACKS)` sul filtro:
  CONNECTED/ERROR/EOF si accodano con `event_base_once()` e la callback prende `bufferevent_lock(raw)` prima dell'eventcb dell'utente
  (un registro dei contesti vivi evita un puntatore a un filtro gia' liberato). L'invariante in `tls_client.h` (tutto sotto il
  lucchetto del socket grezzo) ora e' completo, riferimenti del filtro compresi. Fine connessione consegnata una volta sola.
- **Identita'.** `X509_VERIFY_PARAM_set1_ip_asc` controllato (se fallisce, nessuna connessione); `SSL_OP_NO_RENEGOTIATION` sul contesto.
- **Ancore.** Senza `ca_file`, su ogni piattaforma, oltre all'archivio di sistema si fidano ISRG Root X1 e X2 (`gframe/tls_roots.cpp`, PEM
  da letsencrypt.org, impronta SHA-256 nel commento e nel test; verificata sul rapporto CCADB di Mozilla, perche' letsencrypt.org non
  stampa le impronte). Motivo: Windows scarica le radici su richiesta solo con CryptoAPI, non quando OpenSSL legge l'archivio.
- **Ingresso manuale.** L'indirizzo digitato a mano (`BUTTON_JOIN_HOST`, doppio clic su `LISTBOX_LAN_HOST`) passa da `FindTlsServer(host, porta)`:
  un server `tls: true` si raggiunge cifrato anche cosi'. NON coperto: l'IP numerico di un server TLS digitato al posto del nome non
  corrisponde per nome e resta in chiaro (vedi SOSPESI se Opus decide di chiuderlo).
- **Chiusura.** `WriteToServer` si conta in `client_writers` e controlla `client_open`; `ClientThread` chiude il cancello e aspetta i writer
  prima di liberare `client_bev`.

### 83c: il server Fedelex arriva con l'eseguibile, non con `configs.json` (2026-10-10)

**Il difetto.** L'aggiornamento automatico consegna soltanto l'eseguibile (`ygoprodll`, role `simulator`) e
`fedelex.conf` (role `strings`): sono i soli role che il client conosce (`gframe/update_verify.cpp`). La voce
"Fedelex" stava solo in `installer-data/configs.json`, che l'aggiornamento non porta mai. Chi si aggiornava
aveva quindi l'eseguibile capace di TLS ma nessun server TLS in lista: un link `fedelex://` verso il nome pubblico
non trovava `FindTlsServer` e partiva **in chiaro** verso un indirizzo che parla solo TLS, che lo scarta. Giocare
voleva dire reinstallare a mano. (Letto sul manifesto pubblicato il 2026-10-10: `launcher_files` ha solo
`ygoprodll`/`ygoprodll.exe` con role `simulator` e `fedelex.conf` con role `strings`.)

**La forma.** Il server e' **compilato nell'eseguibile**, che e' l'unica cosa che l'aggiornamento porta a tutti.
- `gframe/server_tls.cpp`: `kBuiltinTlsServers`, una lista costante, oggi una voce (nome `Fedelex`, indirizzo
  `fedelex-duelli.quoll-ruffe.ts.net`, porta 443).
  <!-- verifica: grep -qF '{ "Fedelex", "fedelex-duelli.quoll-ruffe.ts.net", 443 }' EdoproForkGSY/edopro_custom/gframe/server_tls.cpp -->
- `ShouldAddBuiltinTlsServer(voce, gia_caricati)`: funzione pura (nessun irrlicht), la decisione "aggiungi / non aggiungere".
  Non aggiunge solo se esiste gia' una voce **TLS** che corrisponde per nome e porta (`TlsServerMatches`: maiuscole e punto
  finale ignorati). Una voce in chiaro con lo stesso nome non conta; lo stesso nome su un'altra porta e' un altro servizio.
- `Game::LoadServers()` (`gframe/game.cpp`) la chiama **dopo** aver letto `user_configs` e `configs`, quindi una voce
  scritta a mano per lo stesso nome e porta vince e non si ritrova un doppione. Come ogni server TLS, non entra nel menu a
  tendina della lobby.
  <!-- verifica: grep -qF 'server_tls::ShouldAddBuiltinTlsServer(builtin, configured)' EdoproForkGSY/edopro_custom/gframe/game.cpp -->
- La voce **esce** da `installer-data/configs.json`: una fonte sola, il codice. Il meccanismo generico `"tls": true` in
  `configs.json` resta com'e' per chi vuole aggiungere altri server cifrati.
  <!-- verifica(NON): grep -q 'fedelex-duelli' EdoproForkGSY/edopro_custom/tools/release/installer/installer-data/configs.json -->

**`LoadServers()` e' chiamata una volta sola** (`Game::Initialize`). Se lo fosse piu' di una volta, la voce incorporata
della prima chiamata sarebbe gia' in `ServerLobby::serversVector` e la seconda non la raddoppierebbe: il controllo guarda
il vettore, non solo i file di configurazione.

**Cancelli.**
- **1 (funzioni pure)**: 4 funzioni di test nuove in `tests/server_tls_tests.cpp` (la lista contiene esattamente la voce
  sopra; aggiunge su lista vuota, senza TLS, con una voce in chiaro dello stesso nome, con un altro server TLS; non
  aggiunge se c'e' gia' la stessa voce TLS, anche con maiuscole diverse o punto finale; aggiunge se lo stesso nome e' su
  un'altra porta). Rosso prima (stub che non aggiunge nulla: `server_tls_tests: 47 checks, 6 failures`), verde dopo
  (`50 checks, 0 failures`). I due FAIL su `OCG.lflist.conf` assente in locale non contano.
- **2 (build Linux)**: `tools/release/build_linux.sh release` pulita da zero, compila e linka, nessun avviso nuovo.
- **3 (prova sull'artefatto)**: `strings bin/x64/release/ygoprodll | grep fedelex-duelli` stampa
  `fedelex-duelli.quoll-ruffe.ts.net`; lo stesso comando sul `ygoprodll` costruito dal ramo `fase-83` (prima della 83c)
  non stampa nulla, quindi la prova distingue. Non e' una `<!-- verifica -->`: il binario non sta nel repo.
- **4 (CI)**: vedi lo Stato della FASE 83c in PHASES.md (id della run e job per job).

**Non verificato.** `Game::LoadServers()` e' stata solo compilata, mai eseguita (niente `DISPLAY`, irrlicht): la decisione
e' provata dalla funzione pura, il collegamento in `LoadServers` dalla compilazione. Nessun client aggiornato davvero da
una release vera: la 83c non e' ancora in nessuna release.

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
