#!/usr/bin/env python3
"""
gen_release_text.py — genera il testo "cosa scaricare" da artifacts.json.

D218 (FASE 48): artifacts.json e' la fonte unica di cosa una release allega.
Questo script produce il testo Markdown per le persone (usato dentro
README.md) SOLO da quel file: non c'e' niente qui dentro che descriva un
artefatto a mano, quindi non puo' disallinearsi da artifacts.json come le
sette superfici che questa fase ha trovato divergenti.

Uso:
  tools/release/gen_release_text.py               stampa su stdout
  tools/release/gen_release_text.py --check FILE   verifica che il blocco
                                                    generato dentro FILE, fra
                                                    i marcatori, sia uguale a
                                                    quello che si genera ora
  tools/release/gen_release_text.py --write FILE   sostituisce il blocco fra
                                                    i marcatori dentro FILE

I marcatori sono commenti HTML, cosi' il resto del file (installazione,
compilazione, ecc.) resta scritto a mano intorno al blocco generato:
  <!-- artifacts:start -->
  ...
  <!-- artifacts:end -->

Deve essere IDEMPOTENTE: generare due volte produce lo stesso byte (cancello
4 di FASE 48). Non dipende da niente che cambi fra due run (nessuna data,
nessun hash del commit): solo il contenuto di artifacts.json.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS_JSON = REPO_ROOT / "tools" / "release" / "artifacts.json"

START_MARKER = "<!-- artifacts:start -->"
END_MARKER = "<!-- artifacts:end -->"

DESTINATARIO_LABEL = {
    "persona": "da scaricare a mano",
    "aggiornatore": "SOLO per l'aggiornatore integrato — non scaricarlo a mano",
}

SISTEMA_LABEL = {
    "linux": "Linux",
    "windows": "Windows",
}


def carica_artifacts() -> list[dict]:
    dati = json.loads(ARTIFACTS_JSON.read_text(encoding="utf-8"))
    return dati["artifacts"]


def genera_testo() -> str:
    artifacts = carica_artifacts()
    righe = [
        "Ogni release allega questi file (generato da",
        "`tools/release/artifacts.json`, non modificare a mano qui sotto):",
        "",
    ]
    for a in artifacts:
        sistema = SISTEMA_LABEL.get(a["sistema"], a["sistema"])
        dest = DESTINATARIO_LABEL.get(a["destinatario"], a["destinatario"])
        righe.append(f"- **`{a['file']}`** ({sistema}, {dest})")
        righe.append(f"  {a['contenuto']}")
    return "\n".join(righe) + "\n"


def _sostituisci_blocco(testo_file: str, blocco_nuovo: str) -> str:
    if START_MARKER not in testo_file or END_MARKER not in testo_file:
        raise SystemExit(
            f"ERRORE: marcatori {START_MARKER} / {END_MARKER} non trovati nel file."
        )
    inizio = testo_file.index(START_MARKER) + len(START_MARKER)
    fine = testo_file.index(END_MARKER)
    if inizio > fine:
        raise SystemExit("ERRORE: marcatore di fine prima di quello di inizio.")
    return testo_file[:inizio] + "\n" + blocco_nuovo + testo_file[fine:]


def main() -> int:
    args = sys.argv[1:]
    blocco = genera_testo()

    if not args:
        sys.stdout.write(blocco)
        return 0

    modo, path_str = args[0], args[1]
    path = Path(path_str)
    testo_attuale = path.read_text(encoding="utf-8")
    testo_nuovo = _sostituisci_blocco(testo_attuale, blocco)

    if modo == "--check":
        if testo_nuovo != testo_attuale:
            print(f"DISALLINEATO: {path} non riflette artifacts.json.", file=sys.stderr)
            print("Rilancia con --write per rigenerare.", file=sys.stderr)
            return 1
        print(f"OK: {path} e' allineato ad artifacts.json.")
        return 0
    elif modo == "--write":
        path.write_text(testo_nuovo, encoding="utf-8")
        print(f"Scritto: {path}")
        return 0
    else:
        print(f"ERRORE: modo sconosciuto '{modo}' (usa --check o --write)", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
