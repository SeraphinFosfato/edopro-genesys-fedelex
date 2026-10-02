# Il launcher — forma (D236)

## Stato dell'implementazione (FASE 64)

**Aggiornato il 2026-10-02.** Non e' un'intenzione: dice cosa esiste DAVVERO
nel sorgente a questo commit (§1.9 del CLAUDE.md del vault — qui la fonte e'
ancora il sorgente, nessuna release lo porta).

- **Fatto, con test verdi** (`./premake5 --file=tests/premake5.lua gmake2 &&
  make -C tests/build config=release && ./bin/banlist_tests`):
  - Cancello 1: `gframe/update_verify.h/.cpp` legge `launcher_files[]`
    (name/url/sha256/os/role) e filtra per piattaforma + role riconosciuto
    con `SelectLauncherFilesForPlatform()`. Unicita' per (nome, os), non per
    nome solo — `fedelex.conf` e' un upload che installa su entrambe le
    piattaforme (D244 punto 7/8).
  - Cancello 2: nuovo `gframe/launcher_logic.h/.cpp`, funzioni pure senza
    I/O: guardia del ciclo simulatore->launcher, anti-rollback sul proprio
    stato, sostituisci/tieni per hash, decisione della sequenza di verifica
    (hash -> bit eseguibile -> avvio -> installa/torna a `.old`).
  - Cancello 7: `banlist/scripts/build_update_manifest.py` emette
    `launcher_files` da una tabella esplicita allegato -> (os, role); un
    allegato assente dalla release non e' un errore (release precedenti a
    questo cablaggio), un role fuori enumerazione lo e'.
- **NON fatto**: nessun eseguibile del launcher esiste ancora (cancelli 3-5:
  progetto premake `fedelex-launcher`, download/scarica/sostituisci reale
  con curl, guardia `--from-launcher` nel punto di avvio del simulatore),
  nessuna modifica a `.github/workflows/release.yml`, `build_windows.ps1`,
  `tools/release/artifacts.json` o `tools/release/installer/install.sh`
  (cancello 6), nessuna build Linux del launcher provata (cancello 3),
  nessun test in HOME finto con server locale (cancello 4). Build Windows:
  fuori portata per definizione fino al primo tag (vedi PHASES.md).
- Non e' un punto di risalita (§6.6): nessuna scelta di forma e' rimasta
  aperta, solo lavoro non ancora scritto — vedi PHASES.md, coda del brief
  FASE 64, per cosa riprendere e da dove.

**Scritto il 2026-10-02.** Decide la forma del componente deciso in D-56 del
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
