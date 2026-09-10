# Project Map

Documento di orientamento per chi legge questo repo per la prima volta, umano
o agente.

## I tre repo

Il progetto è diviso in tre repository **indipendenti**. La separazione non è
organizzativa: è un vincolo di licenza, applicato rigidamente.

| Repo | Contenuto | Visibilità |
|---|---|---|
| Questo repo (`fork-edopro`) | Fork del client EDOPro con supporto al formato custom | Pubblico, AGPLv3 |
| `vault-banlist` | Analisi di bilanciamento, dati sorgente della point list | Privato |
| `bot-telegram` | Bot per segnalazioni e gestione tornei | Privato |

Nessun repo importa codice da un altro. L'unico artefatto condiviso è
`banlist.json`, pubblicato firmato su un canale di distribuzione statico
separato: questo repo lo scarica e ne verifica la firma (vedi
`design/banlist-distribution.md` per lo schema). Non contiene e non riceve
dati dagli altri due repo.

## Perché la separazione è rigida

- **Licenza.** Questo repo deriva da codice AGPLv3 e va distribuito col
  sorgente. Gli altri due sono lavoro originale e restano proprietari:
  mescolarli contaminerebbe la licenza.
- **Superficie.** Questo repo è distribuito a chiunque per definizione; gli
  altri due no.

## Modello di fiducia

**L'host della partita è untrusted per design.** Chiunque può ospitare
stanze, come nell'EDOPro upstream. Nessuna logica di autorità sta sull'host.
La banlist arriva dall'endpoint firmato, non dall'host — è così che due
giocatori si sincronizzano sul formato senza bisogno di un server nostro
nella partita.

## Cosa NON è protetto (e va accettato)

- Il `banlist.json` distribuito è leggibile da chiunque lo scarichi. La
  firma impedisce di **modificarlo**, non di leggerlo.
- Questo client, essendo distribuito, è ispezionabile e patchabile. Ogni
  controllo compilato al suo interno è un deterrente, non una barriera —
  vedi "Onestà sui deterrenti" in `CLAUDE.md`.

## Vedi anche

`CLAUDE.md` · `design/banlist-distribution.md` · `design/licensing.md`
