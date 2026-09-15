# Distribuzione della banlist firmata — spec

> Questo file vive in un repo pubblico (AGPLv3): copre solo fetch/verifica
> della firma della banlist. Non descrive meccanismi di accesso, revoca o
> controllo account — quelli restano nel vault privato, mai qui.

Fissa il contratto prima del codice, perché tocca la superficie di sicurezza
del client (vedi "Modello di fiducia" in `CLAUDE.md`). Formalizza la tabella
già presente lì.

## Stato

Il contratto è chiuso. La verifica usa **Ed25519 vendorato** (TweetNaCl), non
libsodium: una vecchia stesura di questo documento proponeva libsodium via
vcpkg, ma il percorso di build Linux evita vcpkg di proposito e la dipendenza
andrebbe fatta arrivare su cinque configurazioni diverse. Due file di pubblico
dominio compilano ovunque.

## Cosa scorre da dove (riferimento a `PROJECT-MAP.md`)

```
vault-banlist (privato) → GitHub Action firma Ed25519 → banlist-dist (pubblico, JSON + chiave pubblica)
                                                                │
                                                     HTTPS GET, client verifica qui
                                                                ▼
                                                        fork-edopro (questo repo)
```

Il client non genera né possiede mai una chiave privata. Possiede solo le due
chiavi **pubbliche**, compilate nel binario (va bene: sono pubbliche per
definizione — vedi "Onestà sui deterrenti" nel `CLAUDE.md`).

## Cosa viaggia

**Due file**, su HTTPS, non uno:

```
banlist.json        l'artefatto
banlist.json.sig    firma Ed25519 detached, 64 byte grezzi, sui byte esatti di banlist.json
```

La firma è **staccata**, e non è un dettaglio di comodo: è ciò che permette di
**verificare prima di parsare**. Con la firma dentro il JSON bisognerebbe
parsare per estrarla — cioè dare in pasto a `nlohmann/json` un input non
ancora autenticato per poter decidere se autenticarlo. Una vecchia stesura di
questo documento mostrava proprio quella forma: era sbagliata.

L'artefatto è generato dal vault e mai editato a mano. Ogni `entry` porta
`id`, `limit` (0..3, sempre presente, mai sottinteso), `points`, `name`,
`macro`, `source`, e `reason` **solo quando esiste** — assente, non `null` e
non stringa vuota, così il client non ha bisogno di nessuna convenzione
implicita per distinguere "nessuna motivazione" da "motivazione vuota".
`format_version` è un **intero monotono**; un valore che non è un intero è
payload malformato e si rifiuta come una firma non valida.

`limit` e `points` sono due assi indipendenti: una carta può valere 100 punti
ed essere comunque bannata. A leggerli, vince `limit`.

## I cinque casi (dalla tabella nel `CLAUDE.md`, qui con cosa fare)

| Caso | Comportamento | Dove |
|---|---|---|
| Firma valida, `format_version` **maggiore** | Accetta, mette in staging (si applica al riavvio) | verifica prima di toccare qualunque file su disco |
| Firma valida, `format_version` **uguale** | Nessuna azione, nessuna notifica | non è un aggiornamento, non va annunciato come tale |
| Firma non valida o assente | Rifiuta, tiene la lista attiva, logga (senza bloccare l'avvio) | mai scrivere un payload non verificato su disco |
| `format_version` < locale | Rifiuta come tentativo di downgrade, logga | confronto fatto solo *dopo* che la firma è valida — non prima, altrimenti un payload non firmato potrebbe sondare la versione locale |
| Endpoint irraggiungibile | Usa la lista attiva, nessun allarme al primo fallimento | non bloccare l'avvio del client in attesa della rete |
| Lista attiva scaduta ed endpoint giù | Avvisa in modo discreto e persistente, si continua a giocare | `expires_at` è **sempre e solo** un avviso: non impedisce mai una partita |

## Il ciclo di vita: staging, non sovrascrittura

Il punto delicato è **quando** una lista nuova diventa attiva. Non durante la
sessione: cambiare le regole a chi sta costruendo un mazzo — o peggio, a chi
sta duellando — non è accettabile.

```
avvio N    ─►  promuovi l'eventuale staged  ─►  DeckManager::LoadLFList()
   │
   └─ (dopo l'avvio, thread separato)  fetch → verifica → anti-rollback → staging
                                                                            │
avvio N+1  ─►  promuovi staged  ──────────────────────────────────────────◄─┘
```

Tre regole che discendono da qui:

1. **La lista in chiaro non viene mai scritta su disco.** Il client costruisce
   la `LFList` in memoria dal JSON verificato. Un `.conf` su disco è
   modificabile a mano e oggi il client se lo mangerebbe senza accorgersene:
   chi si ritocca i punti si ritroverebbe con un hash che non ha nessun altro
   e un messaggio d'errore incomprensibile. Su disco resta solo il payload
   **firmato**, che alterato di un byte diventa inservibile alla verifica.
2. **La promozione è ordinata, non sperata.** Lo staged si cancella **solo
   dopo** che la coppia promossa ha verificato a sua volta. Un crash a metà
   lascia lo staged intatto e la promozione si ripete al riavvio successivo:
   non esiste una finestra in cui l'attiva è un JSON nuovo con una firma
   vecchia.
3. **Eccezione alla regola "si applica al riavvio": la prima installazione.**
   Se non c'è nessuna lista attiva, lo staged si promuove subito — non c'è
   nessuna sessione da disturbare, e l'alternativa è un client appena
   installato che gioca la sua prima sessione senza formato.

## Punti di innesto nel codice esistente

Pattern da riusare, non da reinventare:

- **Fetch HTTP**: `gframe/curl.h` incapsula già libcurl con i fix di
  compatibilità versione-per-versione del progetto. Il fetch della banlist
  passa da lì, non da una nuova dipendenza HTTP.
- **Schema fetch-con-progresso**: `gframe/repo_manager.cpp` mostra già il
  pattern (fetch asincrono, fallimento di rete che non blocca la UI) anche se
  lì il trasporto è libgit2, non curl — la forma del componente è quella.
- **Parsing JSON**: **`nlohmann/json`**, già dipendenza (`repo_manager.h`,
  `game_config.h`). *Non* rapidjson, che una vecchia stesura di questo
  documento citava per errore.
- **Consumo della banlist**: `DeckManager::LoadLFList()` (chiamato da
  `data_handler.cpp:164`). La promozione avviene **subito prima** di questa
  chiamata; la lista viene poi costruita in memoria, non rileggendo un file.
- **Hash della lista**: `gframe/lflist_hash.h`. Esiste in un posto solo perché
  una lista può arrivare da un `.conf` o dal JSON firmato, e se i due percorsi
  piegassero le voci in modo diverso la stessa lista avrebbe due hash a
  seconda della provenienza.

## Crypto

Ed25519 **vendorato** (TweetNaCl, pubblico dominio), non libsodium: il
progetto compila per Windows, Linux, macOS, Android e iOS, e il percorso Linux
evita deliberatamente vcpkg. Due file vendorati compilano ovunque senza
chiedere niente a nessuno, e la licenza è compatibile con AGPL. Si verifica
una firma per avvio: le prestazioni non sono un criterio.

Le chiavi fidate sono **due** (`gframe/banlist_keys.h`), operativa e di
riserva, e una firma valida per **qualunque** delle due è accettata. Senza la
seconda, perdere o compromettere la chiave privata significherebbe far
reinstallare il client a tutti; con la seconda, ruotare è una release del
vault e non del client.

## Onestà sui limiti

Tutto questo alza il costo dell'attacco casuale. Non ferma chi ricompila il
client togliendo il controllo — ed è giusto che stia scritto qui, e non in un
commento che promette una sicurezza che non c'è. Quello che regge davvero è
che i giocatori usano questo client contro questo endpoint, e che l'hash della
lista rende visibile chi si è tirato fuori dal formato.

## Cosa resta aperto

- UI per lo stato "modalità degradata" e per la notifica di aggiornamento
  (dove e come avvisare) — non disegnata.
- Porting Android del modulo (fase a sé).
