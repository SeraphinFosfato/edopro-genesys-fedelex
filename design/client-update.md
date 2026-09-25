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

## Cosa resta da decidere (non deciso qui)

- **Dove vive il manifesto.** Il candidato naturale è `Banlist-dist`, che è
  già pubblico e serve già un artefatto firmato; i file possono restare come
  allegati delle Release di GitHub.
- **Ogni quanto si controlla**, e se all'avvio o in sottofondo.
- **Se un aggiornamento può essere obbligatorio** quando il motore è più
  vecchio degli script — cioè esattamente il caso che ha originato tutto.
