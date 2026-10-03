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
- **Cancello 7 — verde.** Suite C++ completa (`banlist_tests`: 291 check
  totali prima di questa fase, 299 ora, 0 fallimenti), build Linux intera
  (`ygoprodll` + `fedelex-launcher`, nessun warning nuovo —
  `warnings "Extra"` + pedantic attivi), `controlla_documenti.py` dalla
  radice del vault verde (71 asserzioni, incluse due corrette in questa
  sessione: `CLIENT_UPDATE_VERSION` era rimasto a 12 nel documento mentre il
  sorgente era gia' a 13 — drift preesistente, non introdotto da questa
  fase, corretto perche' trovato mentre si verificava questo cancello).
- Non e' un punto di risalita (§6.6): nessuna scelta di forma e' rimasta
  aperta. Lavoro futuro, non bloccante: agganciare la finestra Win32 al
  ciclo di installazione Windows quando quel ciclo verra' scritto; la
  pipeline di release non e' mai girata su un runner vero (niente
  push/tag/dispatch in questa sessione, per vincolo).

**Scritto il 2026-10-02, sezione FASE 66 il 2026-10-03.** Decide la forma del componente deciso in D-56 del
registro dei sospesi (vault privato), dopo che ogni correzione
dell'aggiornatore ha richiesto una reinstallazione a mano.

Nessun codice qui: questo documento fissa il contratto. L'implementazione è
doppia (Linux e Windows) e viene dopo.

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
