# CLAUDE.md — fork-edopro

Fork di EDOPro con supporto al formato custom. Client distribuito a giocatori su
Linux, Windows e (in futuro) Android.

## PRIMA DI QUALSIASI COSA: la licenza

Il codice base deriva dalla linea ygopro / ocgcore ed è **probabilmente AGPLv3**.

Se non l'hai ancora fatto in questa sessione: apri il `LICENSE` e verifica.
Tutto ciò che segue assume AGPLv3. Se non lo è, fermati e segnalalo — il quadro
cambia.

Conseguenze se è AGPLv3:
- Il sorgente modificato va reso disponibile a chi riceve il binario. Distribuire
  su Windows/Linux/Android **è** distribuzione.
- Non si può cambiare licenza. È codice di altri con sopra il mio lavoro.
- Non si può tenere privato questo repo mentre si distribuiscono build.
- **Non introdurre in questo repo codice che deve restare proprietario.**
  Se una funzione va protetta, non appartiene qui.

## Cosa NON sta in questo repo

- Le analisi di bilanciamento → stanno in `vault-banlist` (privato)
- Il bot Telegram → repo separato, nessuna dipendenza
- La chiave privata di firma → mai qui, per nessun motivo
- **Descrizioni in chiaro dei meccanismi di controllo/accesso del client**
  (oltre alla verifica firma/anti-rollback già documentata sotto) → restano
  nel vault privato. Questo repo è pubblico per obbligo di licenza: quello
  che ci finisce dentro non si può più ritirare.

Se ti viene chiesto di aggiungere una di queste cose, la risposta è no: è nel repo
sbagliato.

## Modello di fiducia

**L'host della partita è untrusted per design.** Chiunque può ospitare stanze, come
nell'EDOPro upstream. Non aggiungere logica di autorità sull'host: un host ostile
può barare nella propria partita, e questo è accettato.

Ciò che invece è protetto:
- La banlist arriva da un endpoint firmato, non dall'host
- Il client **rifiuta** una banlist con firma non valida
- Il client **rifiuta** una banlist con `format_version` inferiore a quella già
  installata (anti-rollback)

## Verifica della banlist — comportamento richiesto

| Caso | Comportamento |
|---|---|
| Firma valida, versione ≥ locale | Accetta, aggiorna cache |
| Firma non valida o assente | Rifiuta, mantieni la copia locale valida |
| Versione < locale | Rifiuta (tentativo di downgrade) |
| Endpoint irraggiungibile | Usa copia locale valida, avvisa l'utente |
| Copia locale scaduta e endpoint giù | Avvisa, modalità degradata |

La chiave **pubblica** è compilata nel client. Va bene: è pubblica per definizione.

## Dove vive il design

`design/banlist-distribution.md` è **l'unica copia** del contratto del client
sulla banlist firmata. Non esiste una versione "completa" altrove da tenere
allineata: se una modifica sembra richiedere di aggiornare anche un'altra
copia, è un errore, non un passo da fare. I valori non si ricopiano nel
documento: le chiavi stanno in `gframe/banlist_keys.h`, i casi di test in
`tests/banlist_tests.cpp`.

## Onestà sui deterrenti

Ogni controllo compilato in un binario distribuito è aggirabile con un patch. I
controlli qui presenti alzano il costo dell'attacco casuale — non fermano un
avversario competente, e non è quello il loro scopo. Non scrivere codice o commenti
che suggeriscano una garanzia di sicurezza che non c'è.

## Vedi anche

`PROJECT-MAP.md` · `design/banlist-distribution.md` · `design/licensing.md`
