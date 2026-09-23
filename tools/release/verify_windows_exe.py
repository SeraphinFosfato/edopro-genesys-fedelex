#!/usr/bin/env python3
"""Verifica che la build Windows abbia prodotto davvero un eseguibile (D178).

Uso: verify_windows_exe.py <percorso/ygopro.exe>

Perche' esiste. Il difetto originale di questa build non e' che il percorso
dell'eseguibile fosse sbagliato: e' che per mesi nessuno se n'e' accorto,
perche' niente lo verificava. `make` usciva con 0 e quello veniva preso per
una prova che il binario ci fosse. Non lo e' mai stata. E' la quarta istanza
della classe di bug di path che questo repo gia' conosce (vedi il commento in
build_linux.sh): le prime tre sono state curate con un path assoluto, questa
si cura con una VERIFICA, che e' l'altra meta' del rimedio — non basta
calcolare giusto, bisogna accorgersi quando si calcola sbagliato.

Perche' in Python e non in shell. La prima versione usava `file`, che su un
runner Windows non esiste. Qui l'intestazione PE si legge a mano: sono venti
righe, funzionano identiche su Linux e su Windows, e soprattutto si possono
PROVARE sul sistema di chi le scrive — un controllo che non si puo' far
fallire a comando non e' un controllo.
"""

import struct
import sys
from pathlib import Path

# IMAGE_FILE_MACHINE_*
MACCHINE = {0x014C: "i386 (32 bit)", 0x8664: "x86-64 (64 bit)", 0xAA64: "arm64"}
MACCHINA_ATTESA = 0x014C  # il formato distribuisce l'eseguibile x86
IMAGE_FILE_DLL = 0x2000


def errore(messaggio, *dettagli):
    print(f"ERRORE: {messaggio}", file=sys.stderr)
    for riga in dettagli:
        print(f"  {riga}", file=sys.stderr)
    return 1


def verifica(percorso: Path) -> int:
    if not percorso.is_file():
        return errore(
            f"'{percorso}' non esiste.",
            "Se il compilatore e' uscito con 0, un'uscita con 0 non e' una prova",
            "che il binario ci sia: se il generatore ha cambiato dove scrive, e'",
            "qui che si scopre, non il giorno della release.",
        )

    dati = percorso.read_bytes()

    if len(dati) < 0x40 or dati[:2] != b"MZ":
        return errore(
            f"'{percorso}' esiste ma non e' un eseguibile Windows.",
            "Manca la firma 'MZ' in testa al file.",
        )

    (offset_pe,) = struct.unpack_from("<I", dati, 0x3C)
    if offset_pe + 24 > len(dati) or dati[offset_pe : offset_pe + 4] != b"PE\0\0":
        return errore(
            f"'{percorso}' esiste ma non e' un eseguibile Windows.",
            "Ha la firma 'MZ' ma non l'intestazione 'PE'.",
        )

    macchina, = struct.unpack_from("<H", dati, offset_pe + 4)
    caratteristiche, = struct.unpack_from("<H", dati, offset_pe + 22)
    nome_macchina = MACCHINE.get(macchina, f"sconosciuta (0x{macchina:04X})")

    if caratteristiche & IMAGE_FILE_DLL:
        return errore(
            f"'{percorso}' e' una DLL, non un eseguibile.",
            f"Macchina: {nome_macchina}",
        )

    if macchina != MACCHINA_ATTESA:
        return errore(
            f"'{percorso}' e' un PE, ma per la macchina sbagliata.",
            f"Attesa: {MACCHINE[MACCHINA_ATTESA]} — trovata: {nome_macchina}",
            "Il formato distribuisce l'eseguibile a 32 bit, che gira sia su",
            "Windows a 32 che a 64 bit. Passare a 64 e' una decisione a parte.",
        )

    print(f"Verificato: {percorso}")
    print(f"  PE eseguibile, {nome_macchina}, {len(dati)} byte")
    return 0


def main(argv):
    if len(argv) != 2:
        print(f"uso: {Path(argv[0]).name} <percorso/ygopro.exe>", file=sys.stderr)
        return 2
    return verifica(Path(argv[1]))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
