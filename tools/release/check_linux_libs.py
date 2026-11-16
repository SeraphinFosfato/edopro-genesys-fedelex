#!/usr/bin/env python3
"""FASE 120 guardia A: ogni NEEDED di ygoprodll deve stare nella lib/ delle installazioni esistenti.

Il launcher aggiorna solo l'eseguibile, mai lib/ (D-120B): un soname nuovo = client che non parte.
  check_linux_libs.py <ygoprodll>            controlla
  check_linux_libs.py --genera <tar.gz>      riscrive linux_lib_base.txt dalla lib/ dell'installatore
"""
import subprocess
import sys
import tarfile
from pathlib import Path

BASE = Path(__file__).with_name("linux_lib_base.txt")
SISTEMA = {"libc.so.6", "libm.so.6", "ld-linux-x86-64.so.2", "libpthread.so.0", "libdl.so.2", "librt.so.1"}


def needed(elf):
    out = subprocess.run(["readelf", "-d", str(elf)], capture_output=True, text=True, check=True).stdout
    return [l.split("[")[1].split("]")[0] for l in out.splitlines() if "(NEEDED)" in l]


def mancanti(elf, base):
    return [n for n in needed(elf) if n not in base and n not in SISTEMA]


def genera(tgz):
    with tarfile.open(tgz) as t:
        nomi = sorted({Path(m.name).name for m in t.getmembers() if "/lib/" in "/" + m.name and not m.isdir()})
    BASE.write_text("".join(n + "\n" for n in nomi))
    print(f"{len(nomi)} file scritti in {BASE}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--genera":
        genera(sys.argv[2])
    elif len(sys.argv) == 2:
        m = mancanti(sys.argv[1], set(BASE.read_text().split()))
        if m:
            print("ROSSO: soname non presenti nella lib/ delle installazioni esistenti:", *m, sep="\n  ")
            sys.exit(1)
        print("verde")
    else:
        sys.exit(__doc__)
