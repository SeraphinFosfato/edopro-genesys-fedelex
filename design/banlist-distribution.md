# Distribuzione della banlist firmata — spec

> Questo file vive in un repo pubblico (AGPLv3): copre solo fetch/verifica
> della firma della banlist. Non descrive meccanismi di accesso, revoca o
> controllo account — quelli restano nel vault privato, mai qui.

Stato: **solo spec, non implementata**. Fissa il contratto prima di scrivere
codice, perché tocca la superficie di sicurezza del client (vedi "Modello di
fiducia" in `CLAUDE.md`). Formalizza la tabella già presente lì.

## Perché non è ancora codice

Verificare una firma Ed25519 richiede una libreria crypto, e questo repo
oggi non ne linka nessuna (verificato: nessun riferimento a `sodium`,
`ed25519`, `openssl` in `gframe/` o nei `premake5.lua`). La scelta della
libreria è una dipendenza nuova in un progetto AGPLv3 distribuito su tre
piattaforme — non è una decisione da prendere di corsa dentro un batch che
doveva solo mettere in sicurezza un fix di build. Candidato naturale:
**libsodium** (API Ed25519 diretta, licenza ISC, pacchettizzata in vcpkg
— che il repo già usa per Windows/macOS). Da confermare in un batch dedicato
prima di aggiungere la dipendenza.

## Cosa scorre da dove (riferimento a `PROJECT-MAP.md`)

```
vault-banlist (privato) → GitHub Action firma Ed25519 → banlist-dist (pubblico, JSON + chiave pubblica)
                                                                │
                                                     HTTPS GET, client verifica qui
                                                                ▼
                                                        fork-edopro (questo repo)
```

Il client non genera né possiede mai la chiave privata. Possiede solo la
chiave **pubblica**, compilata nel binario (va bene: è pubblica per
definizione — vedi "Onestà sui deterrenti" nel `CLAUDE.md`).

## Payload atteso

Un file JSON (`banlist.json`, generato dal vault, mai editato a mano) con
almeno:

```jsonc
{
  "format_version": 12,        // intero monotono, usato per l'anti-rollback
  "generated_at": "2026-09-10T00:00:00Z",
  "entries": [ { "id": 12345678, "limit": 3, "points": 20 }, ... ],
  "signature": "<base64, Ed25519 su (format_version + entries serializzati canonicamente)>"
}
```

Il formato esatto della serializzazione canonica (cosa esattamente viene
firmato) va deciso insieme al lato vault che genera la firma — i due lati
devono essere sviluppati in coppia, non uno indovinando il formato
dell'altro.

## I cinque casi (dalla tabella nel `CLAUDE.md`, qui con cosa fare)

| Caso | Comportamento | Dove |
|---|---|---|
| Firma valida, `format_version` ≥ locale | Accetta, sovrascrive la cache locale | verifica prima di toccare il file su disco |
| Firma non valida o assente | Rifiuta, tiene la copia locale valida, logga (senza bloccare l'avvio) | mai scrivere un payload non verificato su disco |
| `format_version` < locale | Rifiuta come tentativo di downgrade, stesso log di cui sopra | confronto fatto solo *dopo* che la firma è valida — non prima, altrimenti un payload non firmato potrebbe sondare la versione locale |
| Endpoint irraggiungibile | Usa la cache locale valida, avvisa l'utente in UI | non bloccare l'avvio del client in attesa della rete |
| Cache locale scaduta e endpoint giù | Avvisa esplicitamente, modalità degradata (l'utente sa di giocare con una banlist non fresca) | serve una nozione di "scadenza" della cache, non solo presenza/assenza |

## Punti di innesto nel codice esistente

Pattern da riusare, non da reinventare:

- **Fetch HTTP**: `gframe/curl.h` incapsula già libcurl con i fix di
  compatibilità versione-per-versione del progetto. Il fetch della banlist
  passa da lì, non da una nuova dipendenza HTTP.
- **Schema fetch-con-progresso**: `gframe/repo_manager.cpp` mostra già il
  pattern (fetch asincrono, percentuali, gestione fallimento di rete senza
  bloccare la UI) anche se lì il trasporto è libgit2, non curl — la forma
  del componente (stato, retry, fallback) è quella da copiare.
- **Parsing JSON**: rapidjson è già linkato (vedi `data_manager.cpp`,
  `repo_manager.cpp`, `client_updater.cpp`) — nessuna nuova dipendenza per
  il parsing, solo per la verifica della firma.
- **Consumo della banlist**: `DeckManager::LoadLFList()` (chiamato da
  `data_handler.cpp:164`) è il punto che oggi legge il file `.lflist.conf`
  già presente su disco. Il fetch/verifica va eseguito *prima* di questa
  chiamata, scrivendo (solo se la verifica passa) il file che poi
  `LoadLFList()` legge normalmente — non serve toccare la logica di lettura
  esistente, solo anticiparla con uno step di aggiornamento sicuro.

## Cosa resta aperto

- Libreria Ed25519 da confermare (libsodium, vedi sopra).
- Formato esatto della serializzazione canonica da firmare (va concordato
  col lato vault, non deciso qui unilateralmente).
- UI per lo stato "modalità degradata" (dove/come avvisare l'utente) — non
  disegnata, serve un giro con chi cura l'interfaccia del client.
