# Blocco dell'online per client o core vecchio — forma (D237)

**Scritto il 2026-10-02.** Richiesta dell'utente, testuale:

> Il core troppo vecchio deve proprio bloccare l'online, è pericoloso per noi
> quanto il player, quindi va obbligato all'update (va dato un periodo di
> grazia di 60 minuti per chiunque stia in game, ma già premere il tasto
> dell'online non deve essere possibile e deve essere messo l'avviso quando si
> prova). Poi quando finisci la partita e decidi cosa vuoi fare con il replay,
> dopo vieni cacciato dall'online e portato al menù principale.

Questo documento fissa la **forma**. Il codice lo scrive chi implementa,
seguendo il brief di fase. I punti ancora da decidere sono in §8 e non vanno
inventati.

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
- **R2 — core non allineato** (solo build a core separato, cioè Linux oggi).
  Un repository che dichiara un core ha finito di sincronizzarsi, il suo core
  è stato passato allo scambio, e lo scambio **non è riuscito**: il client
  usa gli script nuovi del repository con il core vecchio della cartella dei
  dati. Oggi `LoadCoreFromRepos` in quel caso fa `continue` e non lo dice a
  nessuno: è questo silenzio che R2 trasforma in un segnale.

Quello che **non** chiude:

- **Manifesto irraggiungibile o non valido**: regola già in vigore, l'assenza
  di un'opinione firmata non chiude mai la porta.
- **Repository che non si è sincronizzato.** Il codice in quel caso scarta sia
  gli script sia il core del repository (ritorno anticipato prima di
  `script_dirs` e `cores_to_load`), quindi il client gira con script e core
  **della stessa installazione**: vecchi, ma coerenti. Se chiudere anche
  questo caso è una decisione aperta (§8, D-61).
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
**anche all'avversario** è l'host per IP, che oggi resta aperta (§8, D-60).

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

## 7. Il controllo a intervalli

Senza, la grazia non si applicherebbe mai: il controllo di oggi gira una
volta all'avvio, cioè prima che chiunque sia in partita. Il cancello deve
poter scattare a sessione aperta, quando viene pubblicato un manifesto con
una soglia più alta.

- Ogni **P minuti** (§8, D-59) il client rilegge il manifesto **con la stessa
  verifica** di avvio: firma prima di interpretare, anti-rollback.
- Il risultato alimenta **solo** il cancello (R1) e l'eventuale proposta da
  mostrare all'uscita. Non apre finestre da solo.
- Un controllo fallito (rete, firma) non apre e non chiude niente.

## 8. Cosa questo documento NON decide

Ognuna è nel registro dei sospesi del progetto; chi implementa non le sceglie.

- **D-58 — il core di Windows**: resta compilato dentro (e allora R1 funziona
  solo se qualcuno si accorge degli aggiornamenti di Project Ignis e alza la
  soglia), oppure Windows passa al core separato come Linux (e allora R2 vale
  anche lì, e il core segue il repository da solo). La DLL del repository è
  a 32 bit, come il nostro eseguibile: la seconda strada è praticabile.
- **D-59 — ogni quanti minuti** si ricontrolla il manifesto.
- **D-60 — se il cancello chiude anche l'host per IP**, che è la porta dove
  un core vecchio rompe il duello all'avversario (§3).
- **D-61 — se un repository non sincronizzato chiude l'online** (§2).

## 9. Prerequisito: la versione installata

Il blocco non deve diventare una trappola. Oggi un aggiornamento che fallisce
lascia scritta la versione nuova (`client-update.md` §6quinquies), e da quel
momento l'aggiornamento non viene più proposto: con l'online chiuso, la
persona resterebbe fuori senza nessun rimedio offerto. La correzione (D238:
la versione installata è quella compilata nel binario) va fatta **prima** o
**insieme** al blocco, mai dopo.

## 10. Cosa resta invariato

- L'eseguibile non si sostituisce senza consenso (`client-update.md` §6):
  "obbligato all'aggiornamento" vuol dire che l'online resta chiuso finché non
  si aggiorna, non che l'aggiornamento parte da solo.
- IA, replay, puzzle e deck editor restano disponibili.
- Il launcher (D236) non entra in questo meccanismo: gira **prima** della
  sincronizzazione dei repository, quindi non può sapere quale core il
  repository consegnerà. R2 si può valutare solo dentro il simulatore.
