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

**Dove sta la privata operativa, deciso il 2026-09-26 (D196):** in una
GitHub Secret, dentro un **Environment con revisore obbligatorio**, nel
repository **privato del vault** — non qui. Questo repo è pubblico per
obbligo di licenza, e un segreto di firma in un repository pubblico è a un
trigger malconfigurato di distanza dall'esposizione. Il lavoro che tiene la
chiave non compila niente e non si innesca da solo a una release: si lancia
a mano, e nessuna esecuzione raggiunge la chiave senza che una persona
approvi.

La **riserva** non entra in nessuna Secret, in nessun Environment, in nessun
CI, mai: due chiavi nello stesso posto non proteggono più da niente.

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

### 6bis. Lo scompattamento deve avvenire ACCANTO all'eseguibile, non nella CWD

Incidente reale del 2026-09-30: un aggiornamento ha rinominato l'eseguibile in
uso in `ygoprodll.old` (`ClientUpdater::Unzip`, `gframe/client_updater.cpp`),
poi ha scompattato lo ZIP scaricato con `Utils::UnzipArchive` **senza passare
`dest`**, che di default vale `"./"` — la cartella di lavoro corrente. Nel
client vanilla CWD coincide sempre con la cartella dell'eseguibile, ma
**l'installer di questo fork non lo garantisce**: `install.sh` lancia il
client con `-C <cartella-dati>` separata dalla cartella del programma (per
il motivo opposto e altrettanto valido: la cartella dati deve sopravvivere
agli aggiornamenti del programma). Risultato: il nuovo `ygoprodll` finiva
scompattato nella cartella dati, mentre la cartella del programma restava
con solo `ygoprodll.old` — nessun eseguibile funzionante. `Utils::Reboot()`
tentava comunque di rilanciare il vecchio percorso (ormai inesistente),
falliva silenziosamente nel figlio e usciva (`exit(0)`) comunque nel padre:
il client si chiudeva "con successo" lasciando l'utente bloccato, sintomo
identico alla regola 6 sopra ma per una causa diversa (non un consenso
mancante, un percorso sbagliato).

Corretto passando `Utils::GetExeFolder()` come `dest`, e aggiungendo un
controllo del valore di ritorno di `UnzipArchive`: se anche un solo file
fallisce, l'intero aggiornamento si annulla, il vecchio eseguibile (se non
già sostituito da un file scritto correttamente) viene ripristinato da
`.old`, e **non si chiama `Reboot()`** — il client attuale resta in
esecuzione, esattamente come nel caso "endpoint irraggiungibile" di §5.
Stessa lezione di 6bis applicata a un livello diverso: un binario che si
riscrive da solo deve poter fallire senza sparire.

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

> **Nota FASE 48 (D218) — perché il nome dello ZIP resta scritto qui, a
> differenza delle guide del vault.** D218 toglie i nomi dei file scaricabili
> dalle guide che *descrivono* una release (`RILASCIO.md`,
> `Contesto/Come si pubblica il client` nel vault), perché un nome ripetuto
> in un documento prosa invecchia in silenzio a ogni cambio di
> pacchettizzazione. Questo documento è l'eccezione dichiarata: qui il nome
> del file (`edopro-custom-linux-x64.zip` / `edopro-custom-windows-x86.zip`,
> vedi `tests/fixtures/update_manifest.json`) non è una descrizione, è il
> **contratto stesso** — il campo `files[].name` del manifesto firmato, che
> il client confronta byte per byte con quello che scarica prima di
> verificarne lo SHA-256. Toglierlo da qui non lo disaccoppierebbe da
> `artifacts.json`, lo renderebbe solo implicito e più facile da disallineare
> per davvero. Una regola che non ammette eccezioni dichiarate viene aggirata
> invece che seguita (CLAUDE.md del vault, FASE 48 tabella E) — quindi
> l'eccezione sta scritta qui, non taciuta.

### 8. Un controllo per avvio, su thread separato

Stessa forma della banlist, e per la stessa ragione (`banlist-distribution.md`,
"Un controllo per avvio"): un binario nuovo diventa attivo comunque solo al
riavvio, quindi controllare più spesso non anticipa niente e aggiunge solo
traffico e modi di fallire. Nessun polling in sottofondo durante la sessione.

**Rivisto il 2026-10-02 (D237).** Per la *proposta* di aggiornamento il
ragionamento regge ancora. Per la *soglia online* no: il blocco deve poter
scattare a sessione aperta, con un periodo di grazia per chi sta giocando,
quindi la lettura di `min_supported` torna a guardare il manifesto a
intervalli. Forma in [blocco-online.md](blocco-online.md).

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

**Corretto il 2026-10-02: il ragionamento qui sopra è sbagliato in un punto,
e la regola è stata rivista (D237, [blocco-online.md](blocco-online.md)).**
In una stanza online il duello **non** lo esegue il core del client: lo
esegue il server. Nel codice di rete del client non c'è nessuna chiamata al
core. Il core del client gira solo quando è il client a ospitare
(`NetServer::StartServer` crea un `GenericDuel`: host in LAN o per IP, e i
duelli contro l'IA, che passano dallo stesso host locale), nei puzzle e nella
riproduzione dei replay di vecchio formato. Quindi chiudere ospitare ed
entrare **online** non protegge l'avversario da un duello rotto, e la porta
lasciata aperta, l'host per IP, è proprio quella dove un core vecchio lo
rompe anche all'avversario. La soglia resta utile, ma per un'altra ragione: è
la leva che obbliga ad aggiornare.
<!-- verifica(NON): grep -q "OCG_" EdoproForkGSY/edopro_custom/gframe/duelclient.cpp -->
<!-- verifica: grep -q "new GenericDuel" EdoproForkGSY/edopro_custom/gframe/netserver.cpp -->

### 6ter. Riuscire a scompattare non è riuscire ad aggiornare

**Scritto il 2026-10-01**, e completa §6bis, che copriva **solo** il caso in
cui `UnzipArchive` *ritorna falso*. Il caso peggiore restava scoperto: lo
scompattamento riesce, ma l'eseguibile **non finisce dove `Reboot()` lo
cercherà** — perché l'archivio non lo contiene, o lo contiene sotto un altro
nome. Allora si chiama `Reboot()` su un file che non c'è, e il suo ramo Linux
chiama `exit(0)` a prescindere dall'esito dell'`exec`: il processo muore e
nella cartella del programma resta **solo `<exe>.old`**.

Non è un'ipotesi, ma la prima stesura di questo paragrafo (e il messaggio
del commit `024c67cc8`) la raccontava **sbagliata** — corretto il 2026-10-02.
Il `ygoprodll` senza bit di esecuzione e il `ygopro.exe` di Windows trovati il
2026-10-01 in un'installazione Linux reale stavano nella **cartella dei dati**
(quella passata con `-C`), **non** in quella del programma: ce li aveva
scompattati un aggiornatore precedente a §6bis, che estraeva nella CWD. Il
client che la persona avvia stava nella cartella del programma ed era integro
ed eseguibile. Quei due file sono spazzatura: si cancellano, non si riparano.

E il bit di esecuzione mancante **non** viene da `stat()` (punto 2 sotto):
`UnzipArchive` non applica i permessi registrati nell'archivio, quindi ogni
file estratto nasce senza bit di esecuzione, e solo `Reboot()` glielo rimette
— sul percorso dell'eseguibile, non nella CWD. Il punto 2 resta un difetto
vero, ma non è la causa di questo incidente.
<!-- verifica: grep -q 'if(!Utils::FileExists(path))' EdoproForkGSY/edopro_custom/gframe/client_updater.cpp -->

Due buchi chiusi insieme, perché sono lo stesso guasto visto da due lati:

1. **Si verifica che l'eseguibile esista** al percorso atteso dopo uno
   scompattamento riuscito. Se non c'è, si ripristina il vecchio da `.old` e
   **non si riavvia** — stessa riga di condotta dell'endpoint irraggiungibile
   (§5): il client attuale resta disponibile.
2. **`Utils::Reboot()` non controllava il ritorno di `stat()`.** Se `stat()`
   fallisce, `fileStat.st_mode` è memoria di stack non inizializzata e il
   `chmod` subito dopo applica al file un **modo casuale**. Ora il `chmod`
   avviene solo se `stat()` è riuscita.

### 6quater. Il manifesto è filtrato per sistema operativo — CORRETTO il 2026-10-02 (D243, FASE 63)

**Era aperto fino al 2026-10-02**: `CheckUpdate` accodava **ogni** voce di
`manifest.files` senza guardare la piattaforma — un client Linux scaricava e
scompattava quindi **anche** il pacchetto Windows, ed era la ragione per cui
un `ygopro.exe` si trovava in un'installazione Linux.

**Corretto (D243, scelta (a)):** ogni voce di `files[]` porta un campo `os`
stringa (`"linux"` o `"windows"`), letto da `update_verify.cpp` come
**facoltativo a livello di schema** — un manifesto senza `os` resta un
manifesto valido (i client vecchi, da prima di questa build, continuano a
ignorarlo come fanno con qualunque campo sconosciuto; `os` non string invece
è una violazione di schema, stesso trattamento di `sha256`/`md5`). Il filtro
vero e proprio è una funzione pura separata,
`ygo::update::SelectFilesForPlatform(files, platform, &discarded_count)`
(`gframe/update_verify.h/.cpp`): tiene solo le voci il cui `os` combacia
**esattamente** col proprio, e scarta tutto il resto — `os` assente, vuoto, o
un valore non riconosciuto (es. `"macos"`) — mai un fallback "installa
comunque". `ClientUpdater::CheckUpdate()` (`gframe/client_updater.cpp`) la
chiama con `kThisClientPlatform`, fissato a compilazione dalle macro
`EDOPRO_LINUX`/`EDOPRO_WINDOWS` di `compiler_features.h`: se dopo il filtro
non resta niente, non accoda nessuna voce, lo scrive nel log e non dichiara
mai un aggiornamento disponibile.

**La firma resta sui byte grezzi**: `os` non apre nessun dominio nuovo, è
solo un campo in più nel JSON già firmato — l'autorità resta la SHA-256 per
file, come prima.

**Chi pubblica** (`banlist/scripts/build_update_manifest.py`, nel vault)
tiene una tabella esplicita `ALLEGATO_OS` (allegato → os) e la build
**fallisce** se un allegato installabile non vi compare — un file senza `os`
pubblicato non lo installerebbe nessun client, quindi è un errore di
pubblicazione, non un file "morto" ma innocuo.

**Compatibilità, verificata sul sorgente** ai tag v0.0.5/v0.1.1/v0.2.1: il
parser ignora i campi sconosciuti, quindi quei client (fino alla build 9)
continuano a scaricare tutto da un manifesto senza `os` — nessun
peggioramento. Il beneficio parte dalla prima build che contiene questo
filtro (non ancora pubblicata in una release alla scrittura di questa nota).
<!-- verifica: grep -q "SelectFilesForPlatform" EdoproForkGSY/edopro_custom/gframe/update_verify.h -->
<!-- verifica: grep -q "kThisClientPlatform" EdoproForkGSY/edopro_custom/gframe/client_updater.cpp -->
<!-- verifica: grep -q "ALLEGATO_OS" banlist/scripts/build_update_manifest.py -->

### 6quinquies. La versione installata si registra prima di installare — CORRETTO il 2026-10-02 (D238)

**Trovato il 2026-10-02.** `DownloadUpdate` scriveva `.edopro_update_version`
appena i file scaricati superavano la verifica, **prima** di `Unzip()` e
`Reboot()`. Se l'applicazione falliva dopo (§6bis, §6ter), il file diceva già
la versione nuova mentre girava ancora la vecchia: al controllo successivo
`CompareVersion` rispondeva *già aggiornato* e l'aggiornamento **non veniva
più proposto, mai**. Il file stava per di più nella CWD, cioè nella cartella
dei dati, non accanto all'eseguibile che avrebbe dovuto descrivere.

Col blocco dell'online (D237) sarebbe diventata una trappola: il client resta
sotto soglia, l'online è chiuso, e l'unico rimedio che gli si indica, cioè
l'aggiornamento, non gli viene più offerto.

**Corretto (D238, FASE 59 punto 1):** la versione installata **è**
`CLIENT_UPDATE_VERSION` del binario in esecuzione, non un file.
`GetInstalledVersion()` restituisce quella costante direttamente.
`SetInstalledVersion()` e la macro `UPDATE_VERSION_FILE` sono stati rimossi;
un `.edopro_update_version` lasciato da un binario precedente non viene letto
né cancellato — ripulire la cartella dati non è compito di questa funzione.
L'anti-rollback (§4) resta identico: un manifesto con versione inferiore al
binario che sta girando è rifiutato da `CompareVersion`, che non è cambiato.
<!-- verifica(NON): grep -q "UPDATE_VERSION_FILE" EdoproForkGSY/edopro_custom/gframe/client_updater.cpp -->
<!-- verifica: grep -q "return ygo::update::CLIENT_UPDATE_VERSION;" EdoproForkGSY/edopro_custom/gframe/client_updater.cpp -->

### 6sexies. L'installatore Linux ripulisce da solo i residui che porta la sua firma — FASE 61 (D241)

**Scritto il 2026-10-02.** §6quinquies diceva che ripulire la cartella dati
non era compito di `GetInstalledVersion()`/`SetInstalledVersion()`: resta
vero, ma qualcuno doveva comunque farlo, perché un vecchio aggiornatore
difettoso (§6bis/§6ter, prima delle correzioni) aveva scompattato
`ygoprodll`, `ygopro.exe` e `.edopro_update_version` dentro la cartella
**dati** (misurato su un'installazione reale il 2026-10-01/02) invece che
in quella del programma, e nessuno glieli toglieva.

`tools/release/installer/install.sh` ora lo fa da solo a ogni installazione,
in una funzione dedicata (`cleanup_data_dir_remnants`, chiamata da
`do_install` dopo `ensure_configs_json`):

- `strings/fedelex.conf` nella cartella dati si **sovrascrive sempre**: il
  client lo legge da lì (`gframe/data_handler.cpp`), non dalla cartella
  programma, quindi senza questo passo un'installazione che riusa una
  cartella dati esistente non vedeva mai le stringhe aggiornate — un
  secondo difetto scoperto misurando per D241, distinto dai residui sopra.
- `ygoprodll`/`ygopro.exe` nella cartella dati si tolgono **solo se portano
  l'impronta `fedelex`** (la stringa compare in ogni binario del fork, per
  esempio `UPDATE_DOMAIN` in `gframe/update_verify.h`; non compare
  nell'EDOPro ufficiale). Un file con quel nome ma senza l'impronta non si
  tocca: non è detto che sia nostro.
- `.edopro_update_version` si toglie sempre: esiste solo nel nostro fork e
  dopo D238 non lo legge più nessuno.
- Se la cartella dati coincide con quella del programma, la pulizia di
  `ygoprodll`/`ygopro.exe` non si applica: sarebbe il binario appena
  installato, non un residuo.
- Niente altro della cartella dati viene toccato (`script/`, i `.cdb`,
  `ocgcore`, mazzi, replay, `config/`).

Ogni azione si stampa file per file; una reinstallazione su una cartella già
pulita dice esplicitamente "Niente da ripulire nella cartella dati".
<!-- verifica: grep -q "cleanup_data_dir_remnants" EdoproForkGSY/edopro_custom/tools/release/installer/install.sh -->
<!-- verifica: grep -q "grep -qa fedelex" EdoproForkGSY/edopro_custom/tools/release/installer/install.sh -->

### 6septies. L'installatore non cancella mai un file che non ha installato lui — FASE 62 (D242)

**Scritto il 2026-10-02, dopo che un mazzo di un utente è andato perso.**
Fino a `164c6049a` (28/09, prima di `v0.1.0-alpha`) il client girava con la
cartella del **programma** come cartella di lavoro, e ci salvava dentro
`deck/`, `replay/` eccetera — prima che esistesse la cartella dati separata
di cui parla il resto di questo documento. `install.sh` reinstallava il
programma con `rsync -a --delete`, che cancella dalla destinazione tutto ciò
che non sta nella sorgente del pacchetto. Le reinstallazioni del 29/09 e del
02/10 hanno cancellato così i mazzi e i replay lasciati in quella cartella,
senza che nessuno lo vedesse: `--delete` non stampa cosa toglie.

Da FASE 62 `install.sh` non usa più `--delete`. Tiene invece un elenco di
cosa ha installato lui (`.installed-files`, dentro la cartella programma) e,
prima di scrivere, confronta ogni file già presente nella cartella programma
con due insiemi: il pacchetto che sta per installare, e quell'elenco.

- Se il file è nel pacchetto nuovo: resta, verrà sovrascritto normalmente.
- Se il file **era** nell'elenco di una nostra installazione precedente ma
  il pacchetto nuovo non lo spedisce più (es. una libreria tolta): si
  toglie. Non è un dato dell'utente, è un residuo nostro.
- In ogni altro caso — non è nel pacchetto, non è nell'elenco — non si sa
  di chi sia, quindi non si cancella mai: si **sposta** nella cartella dati,
  con un suffisso se il nome è già occupato lì.

Se l'elenco manca del tutto (un'installazione fatta da una versione di
`install.sh` precedente a questa regola, o la primissima installazione),
l'insieme "nostro" è vuoto: ogni file trovato nella cartella programma
finisce quindi spostato, mai cancellato — la stessa garanzia, nel caso in
cui si ha meno informazione.

`--uninstall` segue la stessa regola: toglie solo i file dell'elenco: se
nella cartella programma resta altro (un file che l'installatore non ha
mai messo lì), lo lascia e lo dice esplicitamente, invece di fare `rm -rf`
sull'intera cartella come prima.
<!-- verifica: grep -q "migrate_and_cleanup_program_dir" EdoproForkGSY/edopro_custom/tools/release/installer/install.sh -->
<!-- verifica(NON): grep -q -- "rsync -a --delete" EdoproForkGSY/edopro_custom/tools/release/installer/install.sh -->

## Cosa resta davvero aperto

- ~~**Le due chiavi di aggiornamento non esistono ancora.**~~ **FALSO,
  corretto il 2026-10-01.** `update_keys.h` contiene chiavi **reali**, non
  segnaposto a zero, e l'aggiornatore **è attivo**: i client aggiornano
  davvero. Era la stessa affermazione falsa già ritirata una volta (commit
  `dfa1191d2`, *"l'aggiornatore NON era spento, documento falso"*),
  sopravvissuta in un secondo punto del medesimo documento — cioè esattamente
  il difetto che §1.8 del `CLAUDE.md` del vault descrive: un cambio non è
  finito finché **ogni** posto che descrive la forma vecchia non è aggiornato.
<!-- verifica: grep -q "0x[1-9a-fA-F]" EdoproForkGSY/edopro_custom/gframe/update_keys.h -->
- ~~**Il manifesto non distingue i sistemi operativi**~~ **corretto (D243,
  FASE 63, §6quater)**: non ancora in una release pubblicata.
- ~~**La versione installata registrata prima di installare**~~ **corretto
  (D238, §6quinquies)**, non ancora in una release pubblicata.
- ~~**I residui del vecchio aggiornatore restano nella cartella dati**~~
  **corretto (D241, FASE 61, §6sexies)**: l'installatore Linux li toglie da
  solo a ogni installazione. Non ancora in una release pubblicata — il
  tarball `v0.1.1-alpha` porta ancora l'`install.sh` precedente.
- ~~**`install.sh` poteva cancellare dati dell'utente nella cartella
  programma (`rsync --delete`)**~~ **corretto (D242, FASE 62, §6septies)**:
  non usa più `--delete`, sposta nella cartella dati qualunque file che non
  è certo essere suo. Non ancora in una release pubblicata.
