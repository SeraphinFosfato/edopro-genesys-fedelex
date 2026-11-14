#!/usr/bin/env python3
"""
check_android_sources.py — ogni .cpp/.c/.cc che il desktop compila e' anche
nella scatola Android? (FASE 119, design/fork-edopro/android.md §4.)

Il desktop compila gframe con un glob (gframe/premake5.lua), la scatola
Android con una lista esplicita (android/jni/Android.mk): un file nuovo
entrerebbe da una parte sola e l'APK partirebbe senza una feature, in
silenzio. Due controlli:
  1. ogni sorgente di gframe (stessi `excludes` del desktop) compare in
     Android.mk oppure in tools/release/solo_desktop.txt;
  2. ogni voce di solo_desktop.txt esiste davvero e non e' anche in Android.mk
     (niente eccezioni morte).

solo_desktop.txt: righe `percorso<TAB>motivo` relative a gframe/; `#` commenti.

Uso: tools/release/check_android_sources.py [--root RADICE]
"""
from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from pathlib import Path

ESTENSIONI = (".cpp", ".c", ".cc")
# Specchio di `excludes` in gframe/premake5.lua (righe 10 e 68): lzma, i backend
# audio e la cartella Android hanno un loro modulo/voce, il font incluso e'
# solo di Android.
ESCLUSI = ("lzma/*", "SoundBackends/*", "sfAudio/*", "Android/*", "CGUITTFont/bundled_font.cpp")


def sorgenti_desktop(gframe: Path) -> set[str]:
    out = set()
    for p in gframe.rglob("*"):
        if p.suffix not in ESTENSIONI or not p.is_file():
            continue
        rel = p.relative_to(gframe).as_posix()
        if not any(fnmatch.fnmatch(rel, e) for e in ESCLUSI):
            out.add(rel)
    return out


def voci_android_mk(mk: Path) -> set[str]:
    return set(re.findall(r"\$\(GFRAME_PATH\)/(\S+?\.(?:cpp|c|cc))\b", mk.read_text(encoding="utf-8")))


def solo_desktop(txt: Path) -> dict[str, str]:
    voci = {}
    for n, riga in enumerate(txt.read_text(encoding="utf-8").splitlines(), 1):
        if not riga.strip() or riga.lstrip().startswith("#"):
            continue
        percorso, sep, motivo = riga.partition("\t")
        if not sep or not motivo.strip():
            raise SystemExit(f"ERRORE: {txt}:{n}: serve `percorso<TAB>motivo`")
        voci[percorso.strip()] = motivo.strip()
    return voci


def controlla(root: Path) -> list[str]:
    gframe = root / "gframe"
    mk = voci_android_mk(root / "android" / "jni" / "Android.mk")
    eccezioni = solo_desktop(root / "tools" / "release" / "solo_desktop.txt")
    desktop = sorgenti_desktop(gframe)
    errori = [f"manca da Android.mk e da solo_desktop.txt: gframe/{f}" for f in sorted(desktop - mk - set(eccezioni))]
    for f in sorted(eccezioni):
        if f not in desktop:
            errori.append(f"solo_desktop.txt: voce morta (il file non esiste o non e' compilato dal desktop): {f}")
        if f in mk:
            errori.append(f"solo_desktop.txt: {f} e' gia' in Android.mk, la voce e' contraddittoria")
    return errori


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    errori = controlla(ap.parse_args().root)
    for e in errori:
        print("ERRORE:", e)
    if not errori:
        print("OK: gframe e android/jni/Android.mk sono allineati")
    return 1 if errori else 0


if __name__ == "__main__":
    sys.exit(main())
