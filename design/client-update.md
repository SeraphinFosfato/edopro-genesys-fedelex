# Aggiornamento del client — contratto

Compagno di `banlist-distribution.md`, e ne segue deliberatamente la forma.
Questa è **l'unica copia** del contratto: se una modifica sembra richiedere di
aggiornare anche un'altra copia, è un errore, non un passo da fare.

## Perché esiste

Il 2026-09-25 un tester ha preso in duello:

```
proc_workaround.lua:29: attempt to call a nil value (field 'GetReasonEffect')
```

Gli script delle carte **si aggiornano da soli** dai repository di Project
Ignis. Il **motore** no: viaggia dentro il nostro binario. Il nostro `ocgcore`
era fermo a v11.0 (aprile 2025), 96 commit dietro upstream, e
`Duel.GetReasonEffect` è stata aggiunta in mezzo.

Quel disallineamento è stato riparato a mano. Il punto è che **si
riprodurrà**, perché nulla lo impedisce: ogni volta che gli script superano il
motore, servono una release a mano e un tester che reinstalla. Questo
documento descrive il meccanismo che toglie di mezzo la categoria, non
l'istanza.

## Due cose diverse, che non vanno confuse

| Cosa | Di chi è | Chi la aggiorna oggi |
|---|---|---|
| Dati di gioco: script, `cards.cdb`, immagini | Project Ignis | `repo_manager.cpp`, **già funziona** |
| Il nostro binario, motore incluso | nostro | **nessuno** |

L'aggiornatore descritto qui copre **solo la seconda riga**. Il primo
meccanismo esiste, è a posto, e non va toccato.

## Lo stato di partenza, e perché non si accende com'è

`gframe/client_updater.cpp` è un aggiornatore **completo e funzionante** —
sa scaricare, mostrare il progresso, e sostituire l'eseguibile in uso anche
su Windows. È interamente dentro `#if defined(UPDATE_URL)`, e noi
`UPDATE_URL` non lo definiamo mai: per questo è inerte.

**Non va acceso così com'è.** Verifica un **MD5** per file, contro un
manifesto **non firmato**. Sono due difetti distinti:

- un MD5 protegge dalla corruzione durante il trasferimento, **non da un
  avversario**: chi può modificare il file può ricalcolarne l'MD5, e MD5 è
  rotto rispetto alle collisioni da vent'anni;
- il manifesto non è autenticato, quindi chiunque controlli quell'URL — o lo
  comprometta, o si metta in mezzo — decide **quale eseguibile** installiamo
  su ogni macchina.

Accendere `UPDATE_URL` senza firma trasforma l'aggiornatore in un canale di
esecuzione di codice da remoto su tutti i giocatori. Il `CLAUDE.md` di questo
repo lo dice già in forma generale: *«un artefatto nuovo che autorizza
qualcosa segue la forma banlist»*.

## Il contratto

### 1. Si firma il manifesto, sui byte grezzi

Ed25519 sui **byte grezzi** del documento, come `banlist.json` e **non** come
i titoli. La ragione è quella già annotata in `title_verify.h`: firmare i byte
permette di **verificare prima di interpretare**, ed è la posizione più
rigida. Un aggiornatore interpreta un documento che gli dice quali file
eseguibili scaricare: è esattamente il caso in cui non si vuole che il parser
veda dati non autenticati.

Etichetta di dominio propria: **`fedelex-update-v1`**. Come per gli altri
domini, serve il **test di dominio incrociato su entrambi i lati** — senza,
gli stessi byte firmati per un altro scopo si rileggono come un manifesto di
aggiornamento.

### 2. Chiave separata da quella della banlist

Non è la stessa chiave con un dominio diverso, ed è una scelta deliberata
contro la comodità.

La separazione di dominio impedisce di **riusare una firma** fra i due scopi,
e basterebbe se il danno fosse simmetrico. Non lo è: una chiave della banlist
che trapela permette di pubblicare una point list sbagliata — si revoca e si
ripubblica. Una chiave di aggiornamento che trapela permette di **eseguire
codice** sulla macchina di ogni giocatore. Ordini di grandezza diversi
meritano custodia e rotazione indipendenti: ruotare la chiave della banlist
non deve costringere a ritoccare gli aggiornamenti, e viceversa.

Quindi: `gframe/update_keys.h` accanto a `banlist_keys.h` e `title_keys.h`,
pubblica compilata dentro, **privata mai in questo repo, per nessun motivo**.

### 3. SHA-256 per file, e l'autorità è il manifesto

Ogni voce porta la **SHA-256** del file. L'MD5 del codice upstream può
restare dove serve alla sua logica, ma **non è più una verifica**: l'unica
cosa che autorizza l'installazione di un file è la sua SHA-256 **dentro il
manifesto firmato**.

### 4. Anti-rollback

Il manifesto porta una `version`. Il client **rifiuta** un manifesto con
versione inferiore a quella installata, esattamente come la banlist. Senza,
chi può servire un documento vecchio ma validamente firmato riporta tutti a
una versione con una vulnerabilità nota.

### 5. Comportamento richiesto

| Caso | Comportamento |
|---|---|
| Firma valida, versione > installata | Propone l'aggiornamento **dicendo da quale versione a quale** |
| Firma non valida o assente | **Rifiuta**, tiene il client attuale, **lo dice** |
| Versione ≤ installata | Non fa niente, senza allarmi |
| SHA-256 di un file non corrisponde | **Rifiuta quel file e l'intero aggiornamento**, lo dice |
| Endpoint irraggiungibile | Continua col client attuale, **lo dice una volta**, senza bloccare |

### 6. Non sostituisce l'eseguibile in silenzio

È la regola di tutta la sessione del 2026-09-25: **il silenzio è il bug**. Un
programma che si riscrive da solo senza dirlo è la cosa che fa disinstallare
un client. L'utente vede cosa sta per succedere e conferma.

## Onestà sui deterrenti

Vale quanto scritto nel `CLAUDE.md`: ogni controllo compilato in un binario
distribuito è aggirabile con un patch. La firma qui **non** serve a impedire
che un giocatore modifichi il proprio client — quello può farlo comunque e non
ci riguarda. Serve a impedire che **qualcun altro** decida cosa gira sulla sua
macchina. È una garanzia sul canale, non sull'endpoint.

## Le tre scelte che restavano aperte — chiuse il 2026-09-25

### 7. Dove vive il manifesto, e perché l'host dei file non conta

Il manifesto sta su **`SeraphinFosfato/Banlist-dist`**, a un URL stabile,
accanto all'artefatto firmato della banlist: è già pubblico, serve già
qualcosa di firmato, e non aggiunge infrastruttura da tenere accesa.

I **file** invece restano allegati alle Release di
`SeraphinFosfato/edopro-genesys-fedelex`, e il manifesto li indirizza per URL
assoluto. Non è una scorciatoia: poiché ogni file è inchiodato dalla sua
SHA-256 **dentro un documento firmato**, chi ospita il file **non deve essere
fidato**. Può servirci qualunque cosa: se non è il byte per byte previsto,
l'aggiornamento si rifiuta. Il solo punto di fiducia è la chiave, e quella
non sta su nessuno dei due host.

Ne discende una regola operativa: **l'URL del manifesto è compilato nel
binario** insieme alla chiave. Un URL configurabile da file sposterebbe la
fiducia su un file modificabile a mano, che è il difetto da cui siamo partiti.

**I due URL, fissati il 2026-09-26** (prima erano descritti e mai scritti):

```
https://seraphinfosfato.github.io/Banlist-dist/update.json
https://seraphinfosfato.github.io/Banlist-dist/update.json.sig
```

Stanno accanto a `banlist.json` e `banlist.json.sig` nella radice dello stesso
Pages, con lo stesso schema firma-a-fianco. Nessuna infrastruttura nuova:
il workflow della banlist pubblica già in quella radice.

### 7bis. I file del manifesto devono essere ZIP — e oggi non lo sono

Verificato leggendo il codice il 2026-09-26, non assunto: ogni voce di
`files[]` viene scaricata in `./updates/<name>` e poi passata a
`Utils::UnzipArchive` (`client_updater.cpp`, `ClientUpdater::Unzip`), che
apre l'archivio con irrlicht in modalità **`EFAT_ZIP`** (`utils.cpp:747`).
Solo ZIP: né `.tar.gz`, né un binario nudo.

Gli allegati della Release `v0.0.4-alpha` sono invece
`edopro-custom-linux-x64.tar.gz`, `ygopro.exe` e `ygoprodll` — **nessuno dei
tre è installabile dall'aggiornatore**. Un manifesto che li indirizzasse
verificherebbe la firma, verificherebbe le SHA-256, e poi fallirebbe a
scompattare: cioè fallirebbe **dopo** aver spostato l'eseguibile in `.old`,
che è il momento peggiore in cui fallire.

Quindi: prima del primo manifesto, la pipeline di release deve produrre
**uno ZIP per piattaforma**, con dentro i file ai percorsi relativi alla
cartella d'installazione (l'eseguibile in radice; su Windows anche il core,
che `Unzip` sposta e ripristina a parte). I `.tar.gz` e i binari nudi possono
restare come allegati per chi installa a mano: il manifesto semplicemente non
li nomina.

### 8. Un controllo per avvio, su thread separato

Stessa forma della banlist, e per la stessa ragione (`banlist-distribution.md`,
"Un controllo per avvio"): un binario nuovo diventa attivo comunque solo al
riavvio, quindi controllare più spesso non anticipa niente e aggiunge solo
traffico e modi di fallire. Nessun polling in sottofondo durante la sessione.

Il controllo **non blocca l'avvio**: se l'endpoint non risponde, il client
parte com'è e lo dice una volta.

### 9. Obbligatorio no, ma il motore vecchio non gioca online

La tentazione era rendere obbligatorio l'aggiornamento quando il motore è più
vecchio degli script — cioè il caso che ha originato tutto. **No**: sarebbe
sostituire l'eseguibile senza consenso, che la regola 6 vieta, e quella regola
vale più di questa comodità.

La via che risolve lo stesso problema senza violarla: il manifesto può portare
un campo **`min_supported`**, e un client sotto quella versione **si rifiuta
di ospitare e di entrare in stanze online**, spiegando perché e offrendo
l'aggiornamento lì. Gioco in locale, contro l'IA e replay restano disponibili.

Il ragionamento: il danno del motore disallineato non è sul giocatore che non
aggiorna, è sull'**avversario** che si trova il duello rotto a metà. Chiudere
la porta online è la misura che colpisce il danno vero; riscrivere il binario
di qualcuno a sua insaputa no. Il valore di `min_supported` lo decide chi
pubblica la release, una per una: non è una soglia cablata.

## Cosa resta davvero aperto

- **Le due chiavi di aggiornamento non esistono ancora.** Finché
  `update_keys.h` ha i segnaposto a zero, l'aggiornatore è spento per
  costruzione e `UPDATE_URL` non va definito in nessuna build.
