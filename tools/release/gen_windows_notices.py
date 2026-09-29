#!/usr/bin/env python3
"""
gen_windows_notices.py — genera notices-windows/ dai copyright della cache
vcpkg (D219, FASE 48).

Il pacchetto Windows per le persone (D218 punto A) linka staticamente le
librerie sotto, invece di portarle a fianco come .so dinamiche (Linux). Le
loro licenze vanno comunque ridistribuite: questo repo e' AGPLv3 e
CLAUDE.md mette la licenza come prima regola in assoluto.

**Come sono state trovate (D219).** La build Windows compila contro
`x86-windows-static`, dalla cache precompilata di terzi
`edo9300/edopro-vcpkg-cache` (build_windows.ps1). Quella cache e' uno ZIP
scaricabile, e vcpkg ci registra dentro sia i port installati sia il file
`copyright` di ognuno (`installed/x86-windows-static/share/<port>/copyright`)
— esattamente per questo scopo. Non serve una build Windows reale ne'
`dumpbin`: basta aprire lo zip. Verificato il 2026-09-29, elenco e
motivazione completi in design/decisioni.md D219.

**Per eccesso, non per precisione (D219).** L'elenco sotto e' quello dei
port CONTRO CUI la build e' configurata, non necessariamente quello che il
linker ha davvero tirato dentro l'eseguibile (es. discord-rpc-payload
potrebbe non essere collegata). Si generano comunque tutti e 19: una
licenza mancante e' una violazione, una in piu' e' un file di testo
inutile — le due cose non si pesano uguale, quindi questo script non prova
a essere piu' preciso di cosi'.

**Cosa NON entra:** `vcpkg-cmake`, `vcpkg-cmake-config`,
`vcpkg-cmake-get-vars` — sono strumenti di build per l'host x64, non
finiscono nel binario Windows x86 distribuito.

**Perche' l'output si versiona invece di scaricare la cache in CI.** La
cache e' 128 MB e cambia solo quando vcpkg-cache di terzi viene
rigenerata (non a ogni release nostra) — farla scaricare a ogni build
sarebbe un costo di rete ricorrente per un dato che non cambia quasi mai.
notices-windows/ si versiona come notices/ (Linux) gia' fa, e questo
script si rilancia A MANO quando la versione vcpkg cambia (o quando il
CLAUDE.md del fork nota una nuova libreria in premake5.lua), non da CI.

Uso:
  tools/release/gen_windows_notices.py --vcpkg-cache <path-allo-zip> [--out DIR]

  <path-allo-zip>  lo ZIP scaricato da
                    https://github.com/edo9300/edopro-vcpkg-cache/releases/latest/download/installed_x86-windows-static-vs2022.zip
                    (lo stesso URL di build_windows.ps1, $VcpkgCacheUrl)
  --out DIR         default: notices-windows/ (accanto a questo script, nella
                     radice del repo)
"""
from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = REPO_ROOT / "notices-windows"

# Trovato ispezionando la cache vcpkg il 2026-09-29 (D219). Elenco FISSO,
# non ricavato scansionando lo zip: uno zip futuro con un port in meno
# deve fare FALLIRE lo script (una libreria sparita e' un cambio di build
# da notare, non un'assenza silenziosa), non restringere l'elenco da solo.
LIBRERIE_STATICHE_WINDOWS = [
    "bzip2",
    "curl",
    "discord-rpc-payload",
    "fmt",
    "freetype",
    "libevent",
    "libflac",
    "libgit2",
    "libjpeg-turbo",
    "libogg",
    "libpng",
    "libssh2",
    "libvorbis",
    "nlohmann-json",
    "openal-soft",
    "openssl",
    "rapidjson",
    "sqlite3",
    "zlib",
]

# Non vanno in notices-windows/: strumenti di build per l'host, non finiscono
# nel binario distribuito. Elencati qui solo perche' se uno di loro scompare
# dallo zip non deve far fallire lo script per errore.
STRUMENTI_BUILD_ESCLUSI = [
    "vcpkg-cmake",
    "vcpkg-cmake-config",
    "vcpkg-cmake-get-vars",
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--vcpkg-cache", required=True, help="path allo ZIP della cache vcpkg")
    ap.add_argument("--out", default=str(DEFAULT_OUT), help="cartella di uscita (default notices-windows/)")
    args = ap.parse_args()

    zip_path = Path(args.vcpkg_cache)
    if not zip_path.is_file():
        print(f"ERRORE: cache non trovata: {zip_path}", file=sys.stderr)
        return 1

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    mancanti = []
    scritti = []

    with zipfile.ZipFile(zip_path) as z:
        nomi_zip = set(z.namelist())
        for port in LIBRERIE_STATICHE_WINDOWS:
            entry = f"installed/x86-windows-static/share/{port}/copyright"
            if entry not in nomi_zip:
                mancanti.append(port)
                continue
            testo = z.read(entry)
            dest = out_dir / f"{port}.copyright.txt"
            dest.write_bytes(testo)
            scritti.append((port, len(testo)))

    if mancanti:
        print("ERRORE: port attesi ma assenti dalla cache (build vcpkg cambiata?):", file=sys.stderr)
        for p in mancanti:
            print(f"  {p}", file=sys.stderr)
        print(
            "Se il port e' stato rimosso davvero dalla build, aggiorna "
            "LIBRERIE_STATICHE_WINDOWS qui E design/decisioni.md D219 — "
            "non silenziare l'elenco.",
            file=sys.stderr,
        )
        return 1

    print(f"-- notices-windows/: {len(scritti)} licenze scritte (elenco fisso, D219) --")
    for port, dim in scritti:
        print(f"  {port:24s} {dim:7d} byte")
    print()
    print("Esclusi (strumenti di build, non nel binario distribuito):")
    for p in STRUMENTI_BUILD_ESCLUSI:
        print(f"  {p}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
