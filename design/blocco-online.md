# Blocco dell'online per client o core vecchio — forma (D237)

**Scritto il 2026-10-02.** Richiesta dell'utente, testuale:

> Il core troppo vecchio deve proprio bloccare l'online, è pericoloso per noi
> quanto il player, quindi va obbligato all'update (va dato un periodo di
> grazia di 60 minuti per chiunque stia in game, ma già premere il tasto
> dell'online non deve essere possibile e deve essere messo l'avviso quando si
> prova). Poi quando finisci la partita e decidi cosa vuoi fare con il replay,
> dopo vieni cacciato dall'online e portato al menù principale.

Questo documento fissa la **forma**. Il codice lo scrive chi implementa,
seguendo il brief di fase (FASE 59; per Windows anche FASE 60). Le quattro
scelte che restavano aperte le ha fatte l'utente il 2026-10-02: §8.

---

## 1. Il core non è lo stesso oggetto sulle due piattaforme

Va capito prima di tutto il resto, perché decide cosa si può misurare.

| | Windows (`ygopro.exe`) | Linux (`ygoprodll`) |
|---|---|---|
| Dove sta il core | **compilato dentro** l'eseguibile, dal sottomodulo `ocgcore/` | file separato, caricato a runtime |
| Da dove arriva | dal commit del sottomodulo **al momento della build** | prima dalla cartella dei dati (`LoadCore`), poi **sostituito** con quello del repository di Project Ignis (`LoadCoreFromRepos`) |
| Quando invecchia | quando Project Ignis aggiorna script e core e noi non abbiamo ancora ricompilato | quando la sostituzione col core del repository **non riesce** |
| Chi lo aggiorna | noi, con una release | il meccanismo dei repository, da solo |

<!-- verifica: grep -q "core e il runtime collegati staticamente" EdoproForkGSY/edopro_custom/.github/workflows/release.yml -->
<!-- verifica: grep -q -- "--no-core=true" EdoproForkGSY/edopro_custom/.github/workflows/release.yml -->

Il caso di Windows è quello che si è già visto: le build `v0.0.1-alpha` e
`v0.0.2-alpha` avevano dentro il core di aprile 2025, gli script del
repository chiamavano `Duel.GetReasonEffect`, e il sottomodulo è stato portato
avanti il 2026-09-25 (commit `b2a22a1c0`). Da `v0.0.3-alpha` in poi tutte le
release portano lo stesso core (`efc21aa`).

**Deciso il 2026-10-02 (D239): anche Windows passera' al core caricato dal
repository**, come Linux. **Non e' ancora fatto** (FASE 60): fino ad allora
vale la tabella qui sopra, e su Windows l'unico segnale resta R1.

## 2. Come si riconosce "troppo vecchio" — e come NO

**Non con la versione del core.** `OCG_GetVersion` risponde `11.0` sia per il
core di aprile 2025 sia per quello di settembre 2026 (misurato il 2026-10-01
caricando entrambi). Il client accetta solo una corrispondenza esatta di
quella coppia, quindi non distingue niente.
<!-- verifica: grep -qF "(max == OCG_VERSION_MAJOR) && (min == OCG_VERSION_MINOR)" EdoproForkGSY/edopro_custom/gframe/dllinterface.cpp -->

Le due ragioni che chiudono, e solo queste:

- **R1 — build sotto soglia.** Il manifesto firmato dichiara `min_supported`
  e la build che gira è sotto. Esiste già (`IsClientSupported`). Su Windows è
  **l'unico** segnale possibile: il core è parte della build, quindi un core
  vecchio *è* una build vecchia. Funziona solo se qualcuno alza la soglia
  quando Project Ignis aggiorna il core (§8, D-58).
- **R2 — il core in uso non è quello del repository** (solo build a core
  separato: Linux oggi, Windows dopo FASE 60). Una regola sola, che copre tre
  casi (D240):
  1. un repository che dichiara un core si è sincronizzato, ma lo scambio
     **non è riuscito**: script nuovi del repository su core vecchio della
     cartella dei dati. È il caso degli errori Lua. Oggi `LoadCoreFromRepos`
     fa `continue` e non lo dice a nessuno;
  2. quel repository **non si è sincronizzato**: il client ne scarta sia gli
     script sia il core e gioca con quelli dell'installazione, vecchi ma
     coerenti fra loro;
  3. **nessun** repository dichiara un core. Con la configurazione che
     installiamo non succede.

Quello che **non** chiude:

- **Manifesto irraggiungibile o non valido**: regola già in vigore, l'assenza
  di un'opinione firmata non chiude mai la porta.
Il caso 2 di R2 **chiude** per scelta, non per necessità (D240): una regola
che si dice in una frase, *online solo col core del repository*, invece di
due. Il costo, accettato: chi avvia il client mentre GitHub non risponde resta
fuori dall'online per quella sessione, e aggiornare non lo aiuta.
<!-- verifica: grep -q "if(repo->has_core)" EdoproForkGSY/edopro_custom/gframe/game.cpp -->

R2 è sempre noto **prima** che si possa entrare online: il cancello dei dati
pronti (FASE 38) non lascia ospitare né entrare finché le sincronizzazioni e
lo scambio del core non sono finiti.

## 3. Cosa protegge davvero, detto onestamente

In una stanza online **il duello lo esegue il server**, non il core del
client: nel codice di rete del client non c'è nessuna chiamata al core. Il
core del client esegue un duello solo quando è il client a ospitare
(`NetServer::StartServer` crea un `GenericDuel`: host in LAN o per IP, e i
duelli contro l'IA, che passano dallo stesso host), nei puzzle e nei replay di
vecchio formato.
<!-- verifica(NON): grep -q "OCG_" EdoproForkGSY/edopro_custom/gframe/duelclient.cpp -->

Quindi il blocco dell'online è **la leva che obbliga ad aggiornare**, ed è
quello che è stato chiesto. Non protegge l'avversario in una stanza online,
perché lì non è in pericolo. La porta dove un core vecchio rompe il duello
**anche all'avversario** è l'host per IP: per questo il cancello chiude anche
quella (D240). Con il cancello chiuso **si gioca solo da soli**: IA, replay,
puzzle. Tutto ciò che mette davanti un'altra persona si chiude.

## 4. Il cancello: un solo predicato

Un predicato puro, in un header senza dipendenze da irrlicht, come
`game_data_ready.h`: riceve gli ingressi come parametri e restituisce
*aperto* oppure *chiuso con ragione* (R1, R2). Tutti i punti che oggi
leggono `OnlineDisabled()` passano da lì; nessuno ricalcola la condizione per
conto suo.

**Monotono nella sessione:** una volta chiuso resta chiuso fino al riavvio.
Un manifesto successivo che abbassasse la soglia non lo riapre a sessione in
corso: un cancello che si apre e si chiude da solo insegna a non fidarsene.

## 5. Chi NON è in partita

- **Il bottone "Online" del menu principale** resta visibile e premibile, ma
  con il cancello chiuso **non apre** la lista delle stanze: mostra l'avviso.
  Visibile e non disabilitato, perché la richiesta è che *premendolo* si
  legga il perché; un bottone spento non spiega niente.
- **L'avviso** dice la ragione (R1 o R2, con i numeri), cosa resta possibile
  (locale, IA, replay) e cosa fare. Se un aggiornamento è disponibile, lo
  propone lì. **Un solo testo**, costruito in un solo punto e usato anche
  dalla guardia già esistente in `JoinServer`: due testi per lo stesso divieto
  finiscono per dire due cose diverse.
- `JoinServer` resta la seconda linea di difesa, com'è oggi.
- **Entrare per IP** nella stanza di qualcun altro (finestra LAN) mostra lo
  stesso avviso e non si connette.
- **Ospitare in LAN resta possibile, ma solo per sé**: la stanza accetta
  connessioni soltanto dal proprio computer, così l'IA (che si collega in
  locale) continua a funzionare e nessun altro può entrare. Non si annuncia
  sulla rete locale. **Trappola:** ascoltare solo su `::1` sembra la stessa
  cosa e non lo è, perché il client e l'IA si collegano a `127.0.0.1`. La
  proprietà da garantire è *chi si collega*, non *dove si ascolta*.
<!-- verifica: grep -q "0x100007F; //127.0.0.1" EdoproForkGSY/edopro_custom/gframe/menu_handler.cpp -->

## 6. Chi È in partita quando il cancello si chiude

"In partita" vuol dire: duello iniziato, oppure side deck fra due duelli
dello stesso match. Una stanza **non ancora iniziata** non ha niente da
finire: si esce subito, con l'avviso.

- **Grazia: 60 minuti** dal momento in cui *questo client* scopre la
  chiusura, misurati con un orologio monotono (non l'ora di sistema, che si
  può spostare).
- **Durante la grazia**, un avviso **non bloccante** e una sola volta, con
  l'ora di scadenza. Mai una finestra modale che si mette davanti a una
  scelta di gioco.
- **A fine partita.** Il server manda `STOC_REPLAY` alla fine di **ogni**
  duello (lì compare la domanda sul replay), poi `STOC_DUEL_END` se il match
  è finito oppure `STOC_CHANGE_SIDE` se continua. Quindi `STOC_DUEL_END`
  arriva sempre **dopo** l'ultima decisione sul replay: è il punto in cui,
  invece di tornare alla lista delle stanze, si chiude la connessione, si va
  al **menu principale**, si mostra l'avviso e si propone l'aggiornamento.
<!-- verifica: grep -q "NetServer::SendPacketToPlayer(nullptr, STOC_REPLAY);" EdoproForkGSY/edopro_custom/gframe/generic_duel.cpp -->
- **Se la grazia scade a partita in corso**, la connessione si chiude. Il
  percorso di disconnessione esistente propone già il salvataggio del replay
  se il duello era iniziato; dopo, menu principale invece di lista stanze. Da
  sapere: per il server e per l'avversario è un **abbandono**.
- **Rivincita**: con il cancello chiuso non si offre.
- **Se si sta ospitando** una stanza con dentro un'altra persona, la grazia
  vale uguale: la partita in corso finisce (entro i 60 minuti), ma nessun
  altro può più entrare da fuori.

## 7. Il controllo a intervalli

Senza, la grazia non si applicherebbe mai: il controllo di oggi gira una
volta all'avvio, cioè prima che chiunque sia in partita. Il cancello deve
poter scattare a sessione aperta, quando viene pubblicato un manifesto con
una soglia più alta.

- Ogni **15 minuti** (D240) il client rilegge il manifesto **con la stessa
  verifica** di avvio: firma prima di interpretare, anti-rollback. È il
  ritardo massimo fra la pubblicazione di una soglia nuova e il momento in
  cui un client acceso se ne accorge: chi è in partita esce al più tardi 75
  minuti dopo la pubblicazione.
- Il risultato alimenta **solo** il cancello (R1) e l'eventuale proposta da
  mostrare all'uscita. Non apre finestre da solo.
- Un controllo fallito (rete, firma) non apre e non chiude niente.

## 8. Le quattro scelte, fatte dall'utente il 2026-10-02

- **Core di Windows** (D239): si carica dal repository, come su Linux. Il
  core segue Project Ignis da solo e R2 vale anche lì. Da implementare in
  FASE 60; la DLL del repository è a 32 bit come il nostro eseguibile.
- **Intervallo di controllo** (D240): 15 minuti.
- **Host per IP** (D240): chiuso agli altri, aperto solo all'IA (§5).
- **Repository non sincronizzato** (D240): chiude (§2, R2 caso 2).

## 9. Prerequisito: la versione installata

Il blocco non deve diventare una trappola. Fino al 2026-10-02 un aggiornamento
che falliva lasciava scritta la versione nuova (`client-update.md`
§6quinquies), e da quel momento l'aggiornamento non veniva più proposto: con
l'online chiuso, la persona sarebbe rimasta fuori senza nessun rimedio
offerto. **Corretto (D238), FASE 59 punto 1**: la versione installata è
`CLIENT_UPDATE_VERSION` compilato nel binario, non un file — vedi
`client-update.md` §6quinquies per il dettaglio.
<!-- verifica: grep -q "return ygo::update::CLIENT_UPDATE_VERSION;" EdoproForkGSY/edopro_custom/gframe/client_updater.cpp -->

## 11. Stato dell'implementazione

**Scritto il 2026-10-02, insieme al primo lavoro di FASE 59.**

Fatto:

- **D238** (§9): la versione installata è `CLIENT_UPDATE_VERSION`, non un
  file. Commit `72142ca2d`.
- **Il predicato del cancello (R1/R2), puro e testato**:
  `gframe/online_gate.h` (`EvaluateOnlineGate`, `OnlineGateLatch` per la
  monotonia di §4). Non ancora chiamato da nessun punto del client reale.
<!-- verifica: grep -q "EvaluateOnlineGate" EdoproForkGSY/edopro_custom/gframe/online_gate.h -->
- **Il filtro "chi si collega" (§5)**: `gframe/local_connection.h`
  (`IsLocalCallerAddress`), puro e testato su 127.0.0.1/::1/::ffff:127.0.0.1
  contro LAN e indirizzi pubblici. **Non collegato a `NetServer`**: oggi
  `NetServer` accetta ancora qualunque chiamante, a cancello aperto o chiuso.
<!-- verifica: grep -q "IsLocalCallerAddress" EdoproForkGSY/edopro_custom/gframe/local_connection.h -->
- **La macchina a stati della grazia (§6)**: `gframe/grace_period.h`
  (`EvaluateGrace`, `GraceWarningLatch`). **Non collegata a nessun timer né
  a `duelclient.cpp`**: oggi non esiste ancora un orologio che la interroghi
  durante una partita reale.
<!-- verifica: grep -q "EvaluateGrace" EdoproForkGSY/edopro_custom/gframe/grace_period.h -->

**Non fatto — resta da cablare**, nell'ordine del brief di fase (PHASES.md,
FASE 59):

- Il bottone "Online" (`menu_handler.cpp`, `BUTTON_ONLINE_MULTIPLAYER`) non
  consulta ancora il cancello: apre sempre `wRoomListPlaceholder`.
- `BUTTON_JOIN_HOST` (ingresso per IP) non consulta il cancello.
- Nessun testo unico d'avviso: `ServerLobby::JoinServer` ha ancora il proprio
  testo per `OnlineDisabled()` (FASE 36), scritto prima di D237/D240 e non
  ancora unificato con R2 né con la proposta di aggiornamento a intervalli.
- Nessun rilettura del manifesto ogni 15 minuti: il controllo resta quello
  di un colpo solo all'avvio (`client-update.md` §8).
- `R2` non è calcolato da nessuna parte: `Game::LoadCoreFromRepos` (game.cpp)
  non distingue ancora i tre casi di §2 e non scrive niente in `error.log`
  per nessuno di essi.
- `NetServer::StartServer` non filtra i chiamanti con
  `IsLocalCallerAddress`: ospitare in LAN a cancello chiuso accetta ancora
  chiunque sulla rete locale, non solo sé stessi.
- Il ramo `STOC_DUEL_END` (`duelclient.cpp`) non controlla il cancello: non
  porta ancora al menu principale invece che alla lista delle stanze.
- Nessuna rivincita viene ancora soppressa a cancello chiuso.

In breve: **la forma e la logica pura ci sono e sono verificate (34
controlli in `tests/online_gate_tests.cpp`), il cablaggio nel client vero
non c'è**. Un giocatore che gioca oggi con questo codice non vede alcun
comportamento diverso da prima di questo documento.

## 10. Cosa resta invariato

- L'eseguibile non si sostituisce senza consenso (`client-update.md` §6):
  "obbligato all'aggiornamento" vuol dire che l'online resta chiuso finché non
  si aggiorna, non che l'aggiornamento parte da solo.
- IA, replay, puzzle e deck editor restano disponibili. Tutto il resto, cioè
  ogni partita con un'altra persona, si chiude.
- Il launcher (D236) non entra in questo meccanismo: gira **prima** della
  sincronizzazione dei repository, quindi non può sapere quale core il
  repository consegnerà. R2 si può valutare solo dentro il simulatore.
