#!/usr/bin/env bash
# Build dell'APK Fedelex dalla scatola android/ (FASE 119, design/fork-edopro/android.md).
# Uso: tools/release/build_android.sh [debug|release]
#
# debug   : APK non firmato dalla nostra chiave (firma di debug di Gradle). Gira
#           su ogni push, non vede nessun segreto.
# release : APK firmato. Legge ANDROID_KEYSTORE_FILE (percorso di un .jks gia'
#           decodificato) e ANDROID_KEYSTORE_PASS dall'ambiente: li fornisce SOLO
#           il job di release.yml con environment android-release.
#
# Prerequisiti (runner ubuntu): ANDROID_HOME (SDK con cmdline-tools), JDK 17+,
# lua5.3, curl, sottomodulo ocgcore gia' inizializzato (checkout con submodules).
# Risultato: tools/release/out/fedelex-android[-debug].apk
set -euo pipefail

CONFIG="${1:-debug}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LIBWINDBOT_URL="https://github.com/ProjectIgnis/windbot/releases/download/20260711/libWindbot.aar"

[[ "$CONFIG" == debug || "$CONFIG" == release ]] || { echo "uso: $0 [debug|release]" >&2; exit 2; }
[[ -f "$REPO_ROOT/ocgcore/Android.mk" ]] || { echo "ERRORE: ocgcore non inizializzato (git submodule update --init --recursive)" >&2; exit 1; }
: "${ANDROID_HOME:?ANDROID_HOME non impostato}"

cd "$REPO_ROOT/android"

mkdir -p libs
[[ -f libs/libWindbot.aar ]] || curl --retry 5 --connect-timeout 30 --location -o libs/libWindbot.aar "$LIBWINDBOT_URL"

{
  echo "sdk.dir=$ANDROID_HOME"
  if [[ "$CONFIG" == release ]]; then
    : "${ANDROID_KEYSTORE_FILE:?}" "${ANDROID_KEYSTORE_PASS:?}"
    echo "keystore=$ANDROID_KEYSTORE_FILE"
    echo "keystore.password=$ANDROID_KEYSTORE_PASS"
    echo "key=fedelex"
    echo "key.password=$ANDROID_KEYSTORE_PASS"
  fi
} > local.properties
echo "org.gradle.jvmargs=-Xmx8192M" >> gradle.properties

"$SCRIPT_DIR/android_assets.sh"
./ci/touch-assets.sh
# Gli stessi URL di "default" che premake5.lua da' al desktop: senza, Android.mk non
# definisce DEFAULT_PIC_URL e l'app non scarica nessuna immagine (prova POCO 2026-11-18).
export PICS_URL="https://pics.projectignis.org:2096/pics/{}.jpg"
export FIELDS_URL="https://pics.projectignis.org:2096/field/{}.png"
export COVERS_URL="https://pics.projectignis.org:2096/pics/cover/{}.jpg"
OS_NAME=linux USE_BUNDLED_FONT=1 ./ci/build-android.sh
./ci/finalize-assets.sh

if [[ "$CONFIG" == release ]]; then
  ./gradlew assembleRelease
  src="$(ls build/outputs/apk/release/*.apk | head -n1)"
  dst="fedelex-android.apk"
else
  ./gradlew assembleDebug
  src="$(ls build/outputs/apk/debug/*.apk | head -n1)"
  dst="fedelex-android-debug.apk"
fi
mkdir -p "$SCRIPT_DIR/out"
cp "$src" "$SCRIPT_DIR/out/$dst"
echo "Prodotto: tools/release/out/$dst"
