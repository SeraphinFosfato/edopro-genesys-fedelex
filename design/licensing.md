# Licenza — verifica

Verificato leggendo `LICENSE` alla radice del repo (2026-09-10).

## Cosa dice `LICENSE`

Il progetto (Project Ignis: EDOPro, da cui questo fork deriva) è **AGPLv3**,
con due eccezioni interne già dichiarate dal file stesso:

- I moduli client indipendenti e lo script engine: **AGPLv3**.
- I moduli client modificati ereditati da Fluorohydride/ygopro: **GPLv3**
  (originariamente GPLv2 or later, poi rilicenziati).
- Lo script engine (`ocgcore`) ha le proprie note di licenza in
  `ocgcore/LICENSE`, da controllare separatamente se si tocca quell'area.
- Risorse di supporto (icone, asset) possono avere licenze proprie — l'icona
  del client è esplicitamente "All Rights Reserved, may only be used with an
  official build", quindi va sostituita o rimossa in una build distribuita
  con branding diverso dall'ufficiale.

Non c'è dual-licensing generale: il codice nostro, se scritto dentro questo
repo, eredita AGPLv3 (o GPLv3 se in un file già marcato come tale) per
costruzione — non è una scelta che possiamo fare file per file.

## Conseguenze pratiche (già in `CLAUDE.md`, qui solo tracciate)

- Il sorgente modificato va reso disponibile a chi riceve il binario.
  Distribuire su Windows/Linux/Android è distribuzione ai fini AGPL/GPL.
- Non possiamo cambiare licenza: è codice altrui con sopra il nostro lavoro.
- Non possiamo tenere privato questo repo mentre distribuiamo build.
- Niente in questo repo può essere codice che deve restare proprietario. Se
  una funzionalità richiede segretezza (es. la chiave privata di firma della
  banlist), non appartiene qui — vedi `banlist-distribution.md`, dove la
  chiave pubblica (compilata nel client, quindi in un binario AGPL) è
  l'unica cosa che *può* starci.

## `irrlicht-custom` è anch'essa AGPLv3, non zlib

Controllato in `irrlicht-custom/LICENSE`: non è l'Irrlicht upstream
(zlib-style), è il fork di edo9300 con modifiche proprie, **anch'esse
AGPLv3**. Compilare linkandola staticamente (`libIrrlicht.a`, come fa il
fix di build Linux in questo commit) non introduce un vincolo di licenza
diverso da quello che il repo ha già: resta tutto AGPLv3, coerente col resto.
Se in futuro si tornasse all'Irrlicht upstream di sistema (zlib-style), la
situazione cambierebbe e andrebbe riverificata.

## Cosa NON è coperto da questo file

- Il repo vault-banlist e bot-telegram: licenza proprietaria, indipendente,
  vedi i loro `CLAUDE.md`.

## `config/configs.json`: lo scriviamo noi, non lo ridistribuiamo

**Deciso il 2026-09-26**, dopo che l'installatore Linux (FASE 35) si è
fermato senza saperlo produrre.

Quel file dichiara i server pubblici e i **repository da cui il client
scarica i dati di gioco** — script, immagini, liste, e su Linux **il motore
stesso** (`has_core: true` su DeltaBagooska). Senza, il client parte e non
mostra nessun server, in silenzio.

**Non copiamo il file di Project Ignis**: ne scriviamo uno nostro, e ciò che
contiene sono indirizzi, porte e percorsi — dati di fatto e configurazione,
non espressione creativa. Siamo un fork AGPL del loro client: puntare ai
loro servizi pubblici è il comportamento normale di quel client, non
un'appropriazione. Resta il dovere di tenerlo aggiornato se i loro indirizzi
cambiano: è configurazione **nostra**, con la manutenzione che comporta.

**Scaricarlo al primo avvio è escluso, e non per pigrizia.** Un file che
decide *da dove il client prende del codice eseguibile* non può arrivare non
autenticato dalla rete: chi controlla quell'URL controllerebbe gli script e
il motore caricati su ogni macchina. È la stessa identica obiezione per cui
`client-update.md` rifiuta un manifesto non firmato — non la si può fare
valere per gli aggiornamenti e dimenticarla qui. Viaggia dentro la
distribuzione, coperto dalla stessa fiducia del binario.

**Onestà su cosa comporta il default.** Quel file punta ai server pubblici
di Project Ignis, dove la nostra lista **viene scartata e il formato non è
applicato** (vedi `banlist-distribution.md`). È accettabile solo perché il
client adesso **lo dice** quando la lista gli viene sostituita. Non è
un'approvazione di quel percorso: è il riconoscimento che l'alternativa —
un client che non può giocare online con nessuno — è peggio.
