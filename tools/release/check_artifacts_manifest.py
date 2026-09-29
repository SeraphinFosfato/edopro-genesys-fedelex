#!/usr/bin/env python3
"""
check_artifacts_manifest.py — cio' che release.yml allega coincide con
artifacts.json? (D218, FASE 48, cancello 5).

Due controlli:
  1. L'elenco di file nel comando `gh release create` dentro
     .github/workflows/release.yml e' esattamente lo stesso insieme di file
     elencati in tools/release/artifacts.json (stesso conteggio, stessi
     nomi, nessuno in piu' o in meno).
  2. Il testo generato da gen_release_text.py e' allineato a README.md
     (stesso controllo di --check, richiamato qui cosi' un solo comando
     verifica tutta la catena).

E' un controllo **statico**: legge i due file di testo, non esegue la CI e
non scarica niente. Va lanciato da CI (job leggero, nessuna build) cosi'
togliere un file da uno dei due posti si vede subito, non il giorno della
release.

Uso:
  tools/release/check_artifacts_manifest.py
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS_JSON = REPO_ROOT / "tools" / "release" / "artifacts.json"
RELEASE_YML = REPO_ROOT / ".github" / "workflows" / "release.yml"
README = REPO_ROOT / "README.md"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_release_text  # noqa: E402


def file_del_manifesto() -> set[str]:
    dati = json.loads(ARTIFACTS_JSON.read_text(encoding="utf-8"))
    return {a["file"] for a in dati["artifacts"]}


def file_allegati_da_release_yml() -> set[str]:
    """Estrae i nomi di file dalla riga/blocco `gh release create` del
    workflow. I percorsi sono scritti come `dist/<nome>`: si toglie il
    prefisso di cartella, che non fa parte del nome dell'artefatto."""
    testo = RELEASE_YML.read_text(encoding="utf-8")

    # Ancorato all'inizio riga (^) con re.MULTILINE per non prendere la
    # menzione dentro il commento in cima al file ("... chiama mai `gh
    # release create`."), che non e' seguita da --title e farebbe fallire
    # il match, o peggio da un match sbagliato se lo fosse per caso.
    m = re.search(r"^\s*gh release create.*?--title", testo, re.DOTALL | re.MULTILINE)
    if not m:
        raise SystemExit(
            "ERRORE: non trovo il blocco 'gh release create ... --title' "
            f"in {RELEASE_YML}. Il workflow e' cambiato: aggiorna questo "
            "script o il pattern non protegge piu' niente."
        )
    blocco = m.group(0)

    nomi = set()
    for riga in blocco.splitlines():
        riga = riga.strip().rstrip("\\").strip()
        for token in riga.split():
            if token.startswith("dist/"):
                nomi.add(token[len("dist/"):])
    return nomi


def main() -> int:
    errori = []

    manifesto = file_del_manifesto()
    allegati = file_allegati_da_release_yml()

    solo_in_manifesto = manifesto - allegati
    solo_in_release_yml = allegati - manifesto

    if solo_in_manifesto:
        errori.append(
            "In artifacts.json ma MAI allegati da release.yml: "
            + ", ".join(sorted(solo_in_manifesto))
        )
    if solo_in_release_yml:
        errori.append(
            "Allegati da release.yml ma assenti da artifacts.json: "
            + ", ".join(sorted(solo_in_release_yml))
        )

    print(f"-- artifacts.json: {len(manifesto)} voci --")
    for n in sorted(manifesto):
        print(f"  {n}")
    print(f"-- release.yml (gh release create): {len(allegati)} file --")
    for n in sorted(allegati):
        print(f"  {n}")
    print()

    blocco_generato = gen_release_text.genera_testo()
    testo_readme = README.read_text(encoding="utf-8")
    try:
        atteso = gen_release_text._sostituisci_blocco(testo_readme, blocco_generato)
    except SystemExit as exc:
        errori.append(str(exc))
        atteso = None

    if atteso is not None and atteso != testo_readme:
        errori.append(
            "README.md non riflette artifacts.json: rilancia "
            "'tools/release/gen_release_text.py --write README.md'."
        )

    if errori:
        print("FALLITO:", file=sys.stderr)
        for e in errori:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print("OK: release.yml, artifacts.json e README.md sono allineati.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
