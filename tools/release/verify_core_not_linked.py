#!/usr/bin/env python3
"""Cancello 2 di FASE 60 (PHASES.md, D239): il core NON e' dentro l'eseguibile
Windows.

Uso: verify_core_not_linked.py <percorso/ygoprodll.exe>

Perche' esiste, e perche' guarda l'ARTEFATTO (CLAUDE.md §1.9). La stessa
sessione che ha scritto FASE 60 aveva gia' sbagliato una volta, in una fase
precedente, esattamente cosi': ha letto premake5.lua, dedotto cosa sarebbe
stato compilato, e ha costruito una decisione su quella deduzione invece che
sul binario. Qui l'unica cosa che conta e' verificabile con `strings` — anzi,
con esattamente quello che `strings` fa, riscritto in Python perche' un
runner Windows non ha `strings` e perche' dev'essere lo stesso controllo su
entrambi i sistemi operativi su cui gira questo repo.

Cosa controlla, e perche' sono le due facce della stessa domanda (misurato a
mano il 2026-10-02 su `v0.1.1-alpha`, `design/blocco-online.md` §1):

1. **Il binario non contiene i nomi delle funzioni Lua del core.** Ogni
   `LUA_STATIC_FUNCTION(nome)` in `ocgcore/` (vedi
   `ocgcore/function_array_helper.h`) registra la funzione con
   `luaL_Reg{"nome", ...}` — quella stringa letterale finisce leggibile nel
   binario SOLO se `ocgcore/` e' stato compilato ed collegato dentro
   l'eseguibile (progetto premake "ygopro", non "ygoprodll"). Il 2026-10-02
   la build statica di `v0.1.1-alpha` conteneva tutti i sei nomi sotto;
   `ygoprodll` (Linux, core separato) nessuno dei sei.
2. **Il binario cita `ocgcore.dll`.** `gframe/dllinterface.cpp` — il modulo
   che apre il core a runtime con `LoadLibrary`/`dlopen` — e' compilato SOLO
   sotto `#ifdef YGOPRO_BUILD_DLL` (vedi l'inizio del file): la build
   statica non lo compila affatto, quindi non cita mai `ocgcore.dll` da
   nessuna parte. La build `ygoprodll` cita sempre il nome del core della
   propria piattaforma (`CORENAME` in quel file).

Le due condizioni sono ridondanti per disegno, non per sciatteria: la build
statica del 2026-10-02 fallisce ENTRAMBE (nomi presenti, nessuna menzione di
ocgcore.dll), e una build corretta deve passarle entrambe. Se mai una delle
due si trovasse sola a fallire, e' un segnale che la build e' a meta' strada
fra le due forme — da guardare, non da far passare.

Le stringhe si cercano sia in ASCII sia in UTF-16LE (little-endian a due
byte per carattere, un carattere nullo di separazione), perche'
`EPRO_TEXT(...)` espande a `L"..."` quando `UNICODE`/`_UNICODE` sono definiti
(`premake5.lua`, sistema Windows) — la stessa build che genera CORENAME come
stringa Lua puo' quindi comparire codificata in uno dei due modi a seconda
di dove la stringa e' usata.
"""

import sys
from pathlib import Path

# Nomi di funzioni Lua di ocgcore/libduel.cpp: scelti perche' specifici del
# core (non compaiono altrove nel sorgente di gframe) e verificati
# individualmente il 2026-10-02 con `strings` sul binario statico di
# v0.1.1-alpha (tutti e sei presenti li', nessuno nel ygoprodll Linux della
# stessa release).
NOMI_FUNZIONI_CORE = [
    "GetLocationCount",
    "GetReasonEffect",
    "GetMatchingGroupCount",
    "EnableReviveLimit",
    "RegisterEffect",
    "GetOperationInfo",
]

CORENAME_ASCII = "ocgcore.dll"


def _is_token_byte(b: int) -> bool:
    return (48 <= b <= 57) or (65 <= b <= 90) or (97 <= b <= 122) or b == 95  # 0-9 A-Z a-z _


def _trova_token_esatto(dati: bytes, ascii_bytes: bytes) -> bool:
    """True se `ascii_bytes` compare nei dati (in ASCII o in UTF-16LE), non
    come sottostringa di un identificatore piu' lungo: il byte prima e dopo
    l'occorrenza non deve essere un carattere di token (lettera/cifra/'_').
    Questo e' esattamente cio' che rende una funzione Lua REGISTRATA (il
    letterale passato a luaL_Reg) distinguibile da un nome che compare per
    caso dentro una stringa piu' lunga.
    """
    varianti = [ascii_bytes, ascii_bytes.decode("ascii").encode("utf-16-le")]
    for pattern in varianti:
        start = 0
        while True:
            i = dati.find(pattern, start)
            if i == -1:
                break
            prima_ok = i == 0 or not _is_token_byte(dati[i - 1])
            dopo = i + len(pattern)
            dopo_ok = dopo >= len(dati) or not _is_token_byte(dati[dopo])
            if prima_ok and dopo_ok:
                return True
            start = i + 1
    return False


def _contiene_sottostringa(dati: bytes, ascii_bytes: bytes) -> bool:
    """Per ocgcore.dll non serve il controllo di confine: e' gia' un nome di
    file con un punto, non un identificatore che potrebbe essere il prefisso
    di qualcos'altro."""
    varianti = [ascii_bytes, ascii_bytes.decode("ascii").encode("utf-16-le")]
    return any(v in dati for v in varianti)


def verifica(percorso: Path) -> int:
    if not percorso.is_file():
        print(f"ERRORE: '{percorso}' non esiste.", file=sys.stderr)
        return 1

    dati = percorso.read_bytes()

    nomi_trovati = [
        nome for nome in NOMI_FUNZIONI_CORE
        if _trova_token_esatto(dati, nome.encode("ascii"))
    ]
    cita_corename = _contiene_sottostringa(dati, CORENAME_ASCII.encode("ascii"))

    errori = []
    if nomi_trovati:
        errori.append(
            "il binario contiene nomi di funzioni Lua del core "
            "(sembra una build con ocgcore collegato DENTRO, non caricato "
            f"dal repository): {', '.join(nomi_trovati)}"
        )
    if not cita_corename:
        errori.append(
            f"il binario non cita mai '{CORENAME_ASCII}': il modulo che "
            "carica il core a runtime (gframe/dllinterface.cpp) non sembra "
            "compilato dentro questo eseguibile"
        )

    if errori:
        print(f"ERRORE: '{percorso}' non e' una build a core separato.", file=sys.stderr)
        for e in errori:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"Verificato: {percorso}")
    print("  nessun nome di funzione Lua del core trovato")
    print(f"  cita '{CORENAME_ASCII}': il core si carica a runtime, non e' collegato dentro")
    return 0


def main(argv):
    if len(argv) != 2:
        print(f"uso: {Path(argv[0]).name} <percorso/ygoprodll.exe>", file=sys.stderr)
        return 2
    return verifica(Path(argv[1]))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
