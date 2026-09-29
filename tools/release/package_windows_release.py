#!/usr/bin/env python3
"""
package_windows_release.py — pacchetto Windows PER LE PERSONE (D218 punto
A, FASE 48).

Distinto dallo ZIP per l'aggiornatore (`edopro-custom-windows-x86.zip`,
prodotto dallo step PowerShell dentro release.yml, che NON si tocca: un
manifesto firmato lo nomina — design/client-update.md). Questo si AGGIUNGE:
`ygopro.exe` da solo non porta `strings/fedelex.conf` (le etichette del
filtro punti restano "???", D202) ne' le licenze delle librerie collegate
staticamente (D219).

Scritto in Python invece che in PowerShell puro perche' e' l'unico modo di
testarne la logica di staging in QUESTO ambiente (sviluppo su Linux, senza
pwsh) prima che giri per la prima volta su un runner windows-latest, che
ha comunque Python 3 preinstallato (come CI, azioni/setup non necessaria).
release.yml lo richiama da uno step `shell: pwsh` con `python`.

Contenuto del pacchetto, alla radice dello zip:
  ygopro.exe
  strings/fedelex.conf
  LEGGIMI.txt
  notices/<porta>.copyright.txt   (le 19 licenze di D219, copiate da
                                    notices-windows/ del repo)

Uso:
  tools/release/package_windows_release.py \
      --exe bin/release/ygopro.exe \
      --out tools/release/out/edopro-custom-windows-x86-package.zip
"""
from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FEDELEX_CONF = REPO_ROOT / "strings" / "fedelex.conf"
NOTICES_WINDOWS_DIR = REPO_ROOT / "notices-windows"

LEGGIMI_TESTO = """\
EDOPro - client con supporto alla point list (build Windows x86)

Questo pacchetto NON e' un'installazione completa di EDOPro: e' solo il
client. Ha bisogno dei dati di un'installazione EDOPro gia' presente
(cards.cdb, script/, config/, textures/, fonts).

Installazione
  1. Estrai questo ZIP in una cartella dove hai il permesso di scrivere
     (la tua cartella utente, i Documenti, il Desktop). NON estrarlo
     dentro "C:\\Program Files" o dentro una cartella sincronizzata da
     OneDrive: entrambe possono impedire al client di creare la cartella
     crashdumps/ per permessi, ed e' una causa nota di crash silenziosi
     su Windows.
  2. Apri la cartella dove sta il tuo eseguibile EDOPro esistente.
  3. Copia dentro, accanto a quell'eseguibile:
       ygopro.exe
       strings/      (la cartella intera)
  4. Avvia ygopro.exe da quella cartella.

La cartella strings/ contiene le etichette che questo fork aggiunge
("Points:"/"Stats:" nel filtro per costo della point list). Senza di
essa il client parte comunque, ma quelle due etichette restano "???" in
ogni lingua.

Questa build e' compilata su Windows con MSVC, un file solo, nessun
redistribuibile da installare (runtime e core collegati staticamente
dentro ygopro.exe). E' pero' MAI stata eseguita su hardware Windows
reale finora: trattala come beta e segnala qualunque problema.

Licenza: AGPLv3, vedi LICENSE nel repository sorgente. Il sorgente
completo di questa build sta su
https://github.com/SeraphinFosfato/edopro-genesys-fedelex
Le licenze delle librerie collegate staticamente in ygopro.exe stanno in
notices/ (elenco e motivazione in design/decisioni.md, decisione D219 di
quel repository).
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", required=True, help="path a ygopro.exe compilato")
    ap.add_argument("--out", required=True, help="path dello zip da produrre")
    ap.add_argument(
        "--notices-dir",
        default=str(NOTICES_WINDOWS_DIR),
        help="cartella con le licenze generate (default notices-windows/)",
    )
    args = ap.parse_args()

    exe_path = Path(args.exe)
    out_path = Path(args.out)
    notices_dir = Path(args.notices_dir)

    errori = []
    if not exe_path.is_file():
        errori.append(f"eseguibile non trovato: {exe_path}")
    if not FEDELEX_CONF.is_file():
        errori.append(f"strings/fedelex.conf non trovato: {FEDELEX_CONF}")
    if not notices_dir.is_dir():
        errori.append(
            f"cartella licenze non trovata: {notices_dir} "
            "(rilancia tools/release/gen_windows_notices.py se e' la prima volta)"
        )
    else:
        file_notices = sorted(notices_dir.glob("*.copyright.txt"))
        if not file_notices:
            errori.append(f"{notices_dir} esiste ma non contiene licenze (*.copyright.txt)")

    if errori:
        print("ERRORE:", file=sys.stderr)
        for e in errori:
            print(f"  - {e}", file=sys.stderr)
        return 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    if out_path.exists():
        out_path.unlink()

    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(exe_path, "ygopro.exe")
        z.write(FEDELEX_CONF, "strings/fedelex.conf")
        z.writestr("LEGGIMI.txt", LEGGIMI_TESTO)
        for f in sorted(notices_dir.glob("*.copyright.txt")):
            z.write(f, f"notices/{f.name}")

    print(f"Pacchetto pronto: {out_path} ({out_path.stat().st_size} byte)")
    print("-- contenuto --")
    with zipfile.ZipFile(out_path) as z:
        for info in z.infolist():
            print(f"  {info.filename}\t{info.file_size} byte")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
