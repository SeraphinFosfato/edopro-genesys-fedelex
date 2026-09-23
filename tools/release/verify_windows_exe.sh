#!/usr/bin/env bash
# Verifica che la build Windows abbia prodotto davvero un eseguibile (D178).
#
# Uso: verify_windows_exe.sh <percorso/ygopro.exe>
#
# Perche' esiste. Il difetto originale di questa build non e' che il percorso
# dell'eseguibile fosse sbagliato: e' che per mesi nessuno se n'e' accorto,
# perche' niente lo verificava. `make` usciva con 0 e quello veniva preso per
# una prova che il binario ci fosse. Non lo e' mai stata.
#
# E' la quarta istanza della classe di bug di path che questo repo gia'
# conosce (vedi il commento in build_linux.sh). Le prime tre sono state curate
# con un path assoluto; questa si cura con una VERIFICA, che e' l'altra meta'
# del rimedio: non basta calcolare giusto, bisogna accorgersi quando si
# calcola sbagliato.
#
# Sta in un file suo, e non dentro build_windows.sh, per una ragione precisa:
# un controllo che non si puo' far fallire a comando non e' un controllo. Cosi'
# lo si lancia da solo, su un percorso inesistente o su un file qualunque, e si
# vede che dice di no.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "uso: $(basename "$0") <percorso/ygopro.exe>" >&2
    exit 2
fi

EXE="$1"

if ! command -v file >/dev/null; then
    echo "ERRORE: manca il comando 'file', che serve a verificare il binario." >&2
    exit 1
fi

if [[ ! -f "$EXE" ]]; then
    echo "ERRORE: '$EXE' non esiste." >&2
    echo "  Se make e' uscito con 0, un'uscita con 0 non e' una prova che il" >&2
    echo "  binario ci sia: se premake ha cambiato dove scrive, e' qui che si" >&2
    echo "  scopre, non il giorno della release." >&2
    exit 1
fi

descrizione="$(file -b "$EXE")"
if [[ "$descrizione" != *"PE32"* || "$descrizione" != *"MS Windows"* ]]; then
    echo "ERRORE: '$EXE' esiste ma non e' un eseguibile Windows." >&2
    echo "  file dice: $descrizione" >&2
    exit 1
fi

echo "Verificato: $EXE"
echo "  $descrizione"
