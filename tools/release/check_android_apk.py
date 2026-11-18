#!/usr/bin/env python3
"""FASE 121: l'APK deve portare i dati di gioco (l'APK v0.10.1 no: 'Failed to load strings!').
  check_android_apk.py <apk>
"""
import sys
import zipfile

D = "assets/defaults/"


def problemi(apk):
    with zipfile.ZipFile(apk) as z:
        info = {i.filename: i for i in z.infolist()}
        out = [f"manca {D}{f}" for f in ("config/strings.conf", "config/configs.json") if D + f not in info]
        if not any(n.startswith(D + "expansions/") and n.endswith(".cdb") for n in info):
            out.append(f"nessun .cdb in {D}expansions/")
        if not any(n.startswith(D + "script/") and not n.endswith("/") for n in info):
            out.append(f"{D}script/ vuoto o assente")
        for f in ("assets/index.txt", "assets/filelist.txt"):
            if f not in info or info[f].file_size == 0:
                out.append(f"{f} assente o vuoto")
        # Senza DEFAULT_PIC_URL compilato l'app non scarica le immagini (configs.json dice "default").
        so = [n for n in info if n.endswith("/libEDOProClient.so")]
        if not so:
            out.append("libEDOProClient.so assente")
        for n in so:
            if b"pics.projectignis.org" not in z.read(n):
                out.append(f"{n}: URL immagini non compilato (DEFAULT_PIC_URL)")
    return out


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    p = problemi(sys.argv[1])
    if p:
        print("ROSSO: APK incompleto:", *p, sep="\n  ")
        sys.exit(1)
    print("verde")
