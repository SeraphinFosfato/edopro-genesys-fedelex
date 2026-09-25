# Distribuzione della banlist firmata — spec del client

> Questo file vive in un repo pubblico (AGPLv3). Copre fetch, verifica,
> applicazione e presentazione della banlist firmata. Non descrive meccanismi
> di accesso, revoca o controllo account: quelli restano privati, e non si
> riassumono qui (vedi `CLAUDE.md`).

## Dove vive cosa

Questo documento è **l'unica copia** del contratto del client per la banlist
firmata. Non esiste una versione "completa" da tenere allineata altrove: se
una modifica sembra richiedere di aggiornare anche un'altra copia, non c'è
un'altra copia — c'è un errore da fermare.

Tre cose, di proposito, non sono scritte qui ma nei file che le contengono:

- **le chiavi pubbliche** → `gframe/banlist_keys.h`;
- **i casi di test della verifica** → `tests/banlist_tests.cpp`, fixture
  descritti in `tests/fixtures/README.md`;
- **cartelle, nomi file, firme delle funzioni** → `gframe/banlist_updater.h`
  e `gframe/banlist_verify.h`, i cui commenti sono requisiti.

Un dato ricopiato in un documento diventa un secondo dato. Qui si scrive il
*perché* e il comportamento; il *valore* sta nel codice.

## Cosa scorre da dove

```
publisher (privato) → firma Ed25519 → canale statico pubblico (banlist.json + .sig)
                                                  │
                                       HTTPS GET, il client verifica qui
                                                  ▼
                                          fork-edopro (questo repo)
```

Il client non genera né possiede mai una chiave privata. Possiede solo le due
chiavi **pubbliche**, compilate nel binario: sono pubbliche per definizione
(vedi "Onestà sui deterrenti" in `CLAUDE.md`).

## Cosa viaggia

**Due file**, su HTTPS, dall'endpoint pubblico:

```
banlist.json        l'artefatto
banlist.json.sig    firma Ed25519 detached, 64 byte grezzi, sui byte esatti di banlist.json
```

La firma è **staccata**, e non per comodità: è ciò che permette di
**verificare prima di parsare**. Con la firma dentro il JSON bisognerebbe
parsare per estrarla, cioè dare in pasto a `nlohmann/json` un input non ancora
autenticato per decidere se autenticarlo. Una vecchia stesura di questo
documento mostrava proprio quella forma: era sbagliata.

Niente git sul client. La cronologia delle release sta nel repo pubblico
dell'artefatto, ma il client non ha bisogno di clonarlo per scaricare due
file, e git non aggiungerebbe sicurezza: **autentica il server via TLS, non
il contenuto**. Ciò che protegge l'autorità sul formato è la firma, identica
con o senza git.

Ogni `entry` porta `id`, `limit` (0..3, sempre presente, mai sottinteso),
`points`, `name`, `macro`, `source`, e `reason` **solo quando esiste** —
assente, non `null` e non stringa vuota. `limit` e `points` sono due assi
indipendenti: una carta può valere 100 punti ed essere comunque bannata, e a
leggerli **vince `limit`**.

## Accettazione: firma, versione, schema

Il client accetta una firma valida per **qualunque** chiave di
`TRUSTED_KEYS` (due: operativa e riserva). Senza la seconda, perdere o
compromettere la chiave privata significherebbe far reinstallare il client a
tutti; con la seconda, ruotare è una release della lista, non del client.

`format_version` è un **intero monotono**. Il confronto con la lista attiva
avviene **solo dopo** che la firma è valida: prima, un payload non firmato
potrebbe sondare la versione locale.

| Caso | Comportamento |
|---|---|
| Firma valida, versione **maggiore** | Accetta, mette in staging (si applica al riavvio) |
| Firma valida, versione **uguale** | Nessuna azione, nessuna notifica: non è un aggiornamento |
| Firma non valida o assente | Rifiuta, tiene l'attiva, logga. **Non scrive nulla su disco** |
| Versione **minore** | Rifiuta come tentativo di downgrade, logga |
| Firma valida, schema violato | Rifiuta come una firma non valida |
| Endpoint irraggiungibile | Tiene l'attiva, nessun allarme, l'avvio non aspetta la rete |
| Attiva scaduta ed endpoint giù | Avviso discreto e persistente, si continua a giocare |

Schema violato, cioè payload firmato ma non quello contrattato:
`format_version` non intero, `macro` fuori dall'enumerazione chiusa dei dieci
valori, `id` duplicato, `limit` fuori da 0..3. `reason` presente ma vuota non
equivale a `reason` assente.

**Nessun confronto a segmenti.** Se un giorno servisse uno schema
incompatibile, la risposta è un **secondo endpoint** (`banlist-v2.json`): un
client vecchio non lo chiede mai, quindi non può nemmeno provare a leggerlo
male.

## Ciclo di vita: staging, non sovrascrittura

Il punto delicato è **quando** una lista nuova diventa attiva. Non durante la
sessione: cambiare le regole a chi sta costruendo un mazzo — o peggio, a chi
sta duellando — non è accettabile.

```
avvio N    ─►  promuovi l'eventuale staged  ─►  carica l'attiva  ─►  DeckManager::LoadLFList()
   │
   └─ (dopo l'avvio, thread separato)  fetch → verifica → anti-rollback → staging
                                                                            │
avvio N+1  ─►  promuovi staged  ──────────────────────────────────────────◄─┘
```

**Un controllo per avvio**, su thread separato. Nessun controllo periodico
durante la sessione: la lista si applica comunque al riavvio, e scaricarla
prima non anticipa di un minuto il momento in cui diventa attiva.

Quattro regole che discendono da qui:

1. **La lista in chiaro non viene mai scritta su disco.** Il client costruisce
   la `LFList` in memoria dal JSON verificato. Un `.conf` su disco è
   modificabile a mano e il client se lo mangerebbe senza accorgersene: chi si
   ritocca i punti si ritroverebbe con un hash che non ha nessun altro e un
   messaggio d'errore incomprensibile. Su disco resta solo il payload
   **firmato**, che alterato di un byte diventa inservibile alla verifica.
2. **La promozione è sicura per ordine, non per speranza.** Attiva e staged
   sono due file ciascuna, e nessun `rename` ne sposta due insieme: un crash
   fra i due lascerebbe un JSON nuovo con una firma vecchia. Per questo lo
   staged si cancella **solo dopo** che la coppia promossa ha verificato a sua
   volta. Un crash in qualunque punto lascia lo staged intatto e la promozione
   si ripete al riavvio: l'operazione è idempotente.
3. **Nessuna sostituzione passa da `Utils::FileMove`.** Su Windows è un
   `MoveFile` nudo, che fallisce quando la destinazione esiste — cioè sempre,
   dalla seconda promozione in poi.
4. **Eccezione alla regola "si applica al riavvio": la prima installazione.**
   Se non c'è nessuna lista attiva, lo staged si promuove subito — non c'è
   nessuna sessione da disturbare, e l'alternativa è un client appena
   installato che gioca la sua prima sessione senza formato.

## L'hash della lista e la sincronizzazione

Fra due client viaggia **solo l'hash** della lista. Il formato cambia quasi
sempre **solo i punteggi**: se i punti non entrassero nell'hash, due giocatori
con versioni diverse si considererebbero compatibili ed entrerebbero nella
stessa stanza senza essere d'accordo su quali mazzi siano legali — un
disaccordo silenzioso che salta fuori come lite a metà torneo. I punti
**entrano** nell'hash, nella forma XOR che rende il risultato indipendente
dall'ordine di lettura.

**L'hash si calcola in un posto solo**: `FoldLFListEntry` in
`gframe/lflist_hash.h`. Una lista può nascere da un `.conf` o dal JSON
firmato; se le due strade piegassero le voci in modo diverso, la stessa lista
avrebbe due hash a seconda della provenienza, e due giocatori con la stessa
identica banlist si rifiuterebbero a vicenda. Il clamp di `limit` a 0..3 sta
dentro quella funzione, dove avviene lo shift che altrimenti sarebbe
undefined behavior, così nessun chiamante può dimenticarlo.

### L'hash non è nostro: è il nome che tutta la rete dà a una lista

**Regola, e non è negoziabile: una lista senza punti deve produrre
esattamente l'hash che produce EDOPro upstream.** I punti entrano nella
piegatura **solo quando ci sono**, cioè solo per le voci con `points != 0`.

Il motivo è che quel numero non è un dettaglio interno: è l'**unico
identificatore** con cui una lista viaggia fra host, server di stanze e
client, nostri e non. Cambiarlo per tutte le liste non ci ha dato una lista
nostra distinta — ci ha tolto il nome di **tutte le altre**.

Misurato il 2026-09-25 sulle stanze vive di EU Central Competitive, con la
piegatura che mescolava i punti sempre:

| Cosa | Esito |
|---|---|
| Stanze di cui il nostro client sa dire la lista | **0 su 78** |
| `OCG.lflist.conf`, il nostro file, piegato all'upstream | `0x857713b8` — **lo stesso numero che il server usa** |
| Lo stesso file, piegato mescolando sempre i punti | numero diverso, che non esiste per nessun altro |

Da lì discendevano tre guasti che sembravano scollegati: ospitando sul server
pubblico la lista scelta veniva **sostituita con "nessuna lista"** (l'host
vede `N/A`, e **nessun mazzo viene più controllato, a nessuno**); la colonna
della lista mostrava `???` su ogni stanza; e il filtro per banlist nella
lobby non trovava **mai** niente, perché confrontava un nostro numero con i
numeri della rete.

La proprietà che la mescolatura difendeva resta intera: due client che
concordano su limiti e id ma non sui punti continuano ad avere hash diversi,
perché basta **una** voce con punti diversi da zero a separarli — e una lista
a punti che non ne ha nemmeno uno non è una lista a punti. Quello che si
perdeva era solo la compatibilità con chi i punti non li ha mai avuti.

**Conseguenza da non sottovalutare: correggere la piegatura cambia anche
l'hash della nostra lista.** Client vecchi e nuovi non si riconosceranno fra
loro. È un cambio che si fa mentre i giocatori sono pochi, e va accompagnato
dall'aggiornatore di `client-update.md`, non prima.

### Quando qualcun altro sostituisce la lista, si dice

Chi esegue il lato server di una stanza cerca l'hash fra **le proprie** liste
e, se non lo trova, lo **sostituisce con una sua senza dirlo**
(`netserver.cpp`, ramo `hash == 1`; i server pubblici di Project Ignis
sostituiscono con zero, cioè con nessuna lista). L'host riceve indietro un
hash diverso da quello che ha mandato e lo mostra come un nome qualsiasi.

**Il client deve accorgersene e dirlo.** Se l'hash che torna nella
`STOC_JOIN_GAME` è diverso da quello mandato nella `CTOS_CREATE_GAME`, la
lista scelta non è quella in vigore: si avvisa nominando entrambe, e si dice
che in quella stanza il formato **non è applicato**. Un `N/A` silenzioso in
un angolo non è un avviso — è il bug di §6.5 nella sua forma esatta.

**Il requisito è hash uguale fra i due giocatori, non "tutti all'ultima
versione".** Chi non aggiorna non trova nessuno con cui giocare e si allinea
da sé: la rete converge sull'ultima versione senza che nessuna infrastruttura
debba restare in piedi perché il formato funzioni. Pretendere l'ultima
versione richiederebbe un fetch riuscito a ogni avvio, cioè un endpoint il cui
guasto ferma il formato per tutti.

**Hash che non combacia entrando in una stanza: rifiuto con spiegazione, mai
aggiornamento a caldo.** Aggiornarsi sul momento reintrodurrebbe le regole che
cambiano sotto un giocatore, proprio sulla porta di una partita. Il messaggio
dice il *perché* (la tua lista è diversa da quella dell'altro) e il *cosa
fare* (un riavvio la allinea, se l'aggiornamento è già stato scaricato), non
un codice d'errore.

**La lista firmata ha un nome fisso: `Fedelex della Luce`**, senza numero di
versione (il nome non entra nell'hash, quindi non cambia la compatibilità).
La versione resta visibile dove serve: nel titolo della finestra "Novità" e,
quando una stanza rifiuta per hash diverso, nel messaggio che lo spiega.

## Modalità degradata

`expires_at` è **sempre e solo un avviso**. Una lista scaduta con endpoint
irraggiungibile produce un avviso persistente ma discreto — il giocatore sa di
avere una lista vecchia — e il client continua a funzionare. Il fallimento da
evitare è il client che si rifiuta di partire perché l'hosting è giù.

## Il diff

Calcolato **sul client**, fra la lista attiva e quella in staging, per `id`.
Non viene scaricato né mantenuto a mano da nessuna parte.

Il motivo per cui è client-side e non un changelog pubblicato: se un giocatore
salta tre release, il diff calcolato localmente è **esattamente giusto**,
mentre concatenare tre changelog gli mostrerebbe cambiamenti che si sono
annullati a vicenda. Gli serve il *netto*.

Quattro gruppi, in quest'ordine — l'ordine in cui interessano a chi gioca:

| Gruppo | Significato per chi gioca |
|---|---|
| **Più care** | `points` salito. Il mazzo che gioco potrebbe non stare più nel budget |
| **Più economiche** | `points` sceso. Si apre uno slot |
| **Nuove in lista** | Prima erano gratis, ora costano |
| **Uscite dalla lista** | Non costano più niente |

Un cambio di `limit` è un cambiamento anche a punti invariati, e va mostrato:
passare da 3 copie a bannata conta più di qualunque swing di punti.

Dentro ogni gruppo, ordinamento per **entità del cambiamento**, decrescente:
uno swing di 50 punti conta più di uno da 3. Lista troncata a ~15 voci con
"e altre N": una release grossa muove centinaia di carte, e una lista
infinita non la legge nessuno.

Per ogni voce: nome, `vecchio → nuovo`, e la riga di `reason` **se il JSON la
porta**. Se manca, si mostrano solo i numeri: il client non inventa mai un
perché. Le voci di "Nuove in lista" portano l'etichetta `(new)`.

Il diff resta consultabile **anche dopo il riavvio che applica la lista**: la
promozione conserva la coppia attiva uscente in `./lflists/.previous/`
(firmata e riverificata prima dell'uso, come le altre). La finestra "Novità"
confronta attiva → staging se c'è un aggiornamento pronto, altrimenti
precedente → attiva. Senza una precedente (prima installazione) lo dice, e non
confronta contro il vuoto.

## La notifica

Mostrata **quando lo staging è pronto**, non all'avvio successivo: il
giocatore deve poterla leggere quando arriva, non ritrovarsela dopo un riavvio
che magari fa fra tre giorni.

- Dice esplicitamente che **si applica al prossimo avvio**, e offre di
  riavviare subito.
- Ha uno stato "già vista" persistente per `format_version`, così non
  ricompare a ogni lancio.
- Il diff completo resta consultabile dopo averla chiusa: una voce nel menù,
  non solo un popup che se lo perdi è perso.

## Punti di innesto nel codice

Pattern da riusare, non da reinventare:

| Cosa | Dove | Nota |
|---|---|---|
| Fetch HTTP | `gframe/curl.h` | wrapper libcurl già presente, con i fix di compatibilità versione per versione. Nessuna nuova dipendenza HTTP |
| Forma del componente | `gframe/repo_manager.cpp` | fetch asincrono, fallimento di rete che non blocca la UI. Lì il trasporto è libgit2, ma la forma è quella |
| Parsing JSON | `nlohmann/json` | già dipendenza. *Non* rapidjson, che una vecchia stesura citava per errore |
| Hash | `gframe/lflist_hash.h` | l'unico punto in cui una voce entra nell'hash |
| Lettura `.conf` | `DeckManager::LoadLFListSingle` | non si tocca, salvo passare da `FoldLFListEntry` |
| Innesto all'avvio | `data_handler.cpp`, subito prima di `LoadLFList()` | vedi ordine sotto |

L'ordine all'avvio non è negoziabile: **promozione dello staged → caricamento
dell'attiva in memoria → `LoadLFList()` → controllo in background**. Il
caricamento dell'attiva riempie la versione contro cui l'anti-rollback
confronta quella scaricata: se il controllo partisse prima, il confronto
avverrebbe contro zero e qualunque payload firmato passerebbe come "più
recente".

## Crypto

Ed25519 **vendorato** (TweetNaCl, pubblico dominio), non libsodium: il
progetto compila per Windows, Linux, macOS, Android e iOS, e il percorso di
build Linux evita deliberatamente vcpkg. Due file vendorati compilano ovunque
senza chiedere niente a nessuno, e la licenza è compatibile con AGPL. Si
verifica una firma per avvio: le prestazioni non sono un criterio.

La verifica (`banlist_verify.{h,cpp}`) **non include niente di `gframe`**:
niente irrlicht, niente curl, niente globali. È ciò che la rende linkabile in
un binario di test senza rete, finestra né gioco. Il workspace di test
(`tests/premake5.lua`) è separato dal premake di root perché quel file è
condiviso con l'upstream, e ogni riga aggiunta lì è un conflitto al rebase.

## Test

- **Verifica, anti-rollback, staging, hash**: i casi sono dichiarati in
  `tests/banlist_tests.cpp` e non si ricopiano qui. Fra i fixture c'è
  l'artefatto **vero** pubblicato, firmato dalla chiave operativa: è l'unico
  test che si accorge di una divergenza fra ciò che si firma e ciò che il
  client accetta.
- **Diff e notifica**, da aggiungere quando si implementano:
  - salto di più versioni → il diff mostra il netto, non la somma;
  - carta bannata a punti invariati → compare nel diff;
  - release che muove più voci del tetto → troncatura con "e altre N";
  - voce senza `reason` → solo i numeri, nessun testo inventato;
  - notifica già vista per quel `format_version` → non ricompare.

## Dove il formato è davvero imposto — e dove non lo sarà mai

Il controllo dei mazzi non lo fa il client di chi gioca: lo fa **chi esegue
il lato server della stanza**, in `GenericDuel::PlayerReady`, e lo fa con la
lista che corrisponde all'hash della stanza. Da questa sola frase discende
tutto il resto, e va tenuto presente prima di progettare qualunque
"controllo lato host".

| Dove si gioca | Chi controlla | La nostra lista è applicata? |
|---|---|---|
| Stanza ospitata dal nostro client (host diretto) | il nostro binario | **Sì**, anche contro un EDOPro normale che si collega |
| Server pubblico di Project Ignis | il loro server | **No**, e non potrà mai: il file della lista lì non c'è |
| Un server di stanze nostro | il nostro server | Sì, ma è infrastruttura da tenere accesa — decisione aperta |

Due conseguenze pratiche che non vanno riscoperte ogni volta:

**Il client host non vede i mazzi altrui.** Chi entra manda il proprio mazzo
al server con `CTOS_UPDATE_DECK`; l'host riceve solo nome, posizione e la
spunta "pronto" (`STOC_HS_PLAYER_ENTER` / `STOC_HS_PLAYER_CHANGE`). Un
autokick "chi non è in formato" **non è implementabile** lato host: il
comando per cacciare esiste (`CTOS_HS_KICK`) ma non c'è niente su cui
giudicare. Chi ci ripensa fra sei mesi si fermi qui.

**Quello che si può fare, e cosa vale.** Il nostro client può controllare il
**proprio** mazzo contro la lista attiva prima di dichiararsi pronto, e
rifiutarsi di farlo se è fuori formato; e l'host può cacciare chi non
dimostra di avere il nostro client. Ferma il caso reale — l'esterno che il
formato non ce l'ha — e non ferma chi si patcha il binario, esattamente come
tutto il resto qui sotto. Va scritto come deterrente, mai come garanzia.

## Onestà sui limiti

Tutto questo alza il costo dell'attacco casuale. Non ferma chi ricompila il
client togliendo il controllo — ed è giusto che stia scritto qui, e non in un
commento che promette una sicurezza che non c'è. Quello che regge davvero è
che i giocatori usano questo client contro questo endpoint, e che l'hash della
lista rende visibile chi si è tirato fuori dal formato.

## Cosa resta aperto

- Dove stanno a schermo l'avviso di modalità degradata e la voce di menù del
  diff: si sceglie implementando la notifica.
- Porting Android del modulo (fase a sé).
