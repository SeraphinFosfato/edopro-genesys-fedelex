#!/usr/bin/env bash
# FASE 121: riempie android/src/main/assets/{defaults,update} con i dati di gioco.
# Riproduce la logica di android/ci/download-assets.sh (subtree a monte, non si
# modifica) senza DEPLOY_TOKEN: ProjectIgnis/Distribution e' pubblico.
# Va lanciato da android/, prima di ci/touch-assets.sh e ci/finalize-assets.sh.
set -euo pipefail

# Ref FISSATO (build riproducibile): HEAD di master al 2026-11-18.
DISTRIBUTION_REF=54a6e2395c532648ff762540e9615319fac4f51b
WINDBOT_RESOURCES_URL="https://github.com/ProjectIgnis/windbot/releases/download/20260711/WindBotIgnite-Resources.7z"

ASSETS=src/main/assets/defaults
UPDATES=src/main/assets/update
mkdir -p "$ASSETS" "$UPDATES"

rm -rf Distribution
git clone -q https://github.com/ProjectIgnis/Distribution.git
git -C Distribution checkout -qf "$DISTRIBUTION_REF"
git -C Distribution submodule update --init --recursive --force

rsync -ar --exclude='.*' --exclude='*.sh' --exclude='script/.*' \
	--exclude='expansions/.*' --exclude='expansions/ci' --exclude='expansions/*rerelease*' --exclude='expansions/README.md' \
	--exclude='expansions/unofficial-fossil.cdb' --exclude='expansions/release.cdb' --exclude='script/pre-release' \
	--exclude=textures/Backup --exclude=puzzles \
	Distribution/ "$ASSETS"
# update/: solo gli .cdb (come l'originale; il resto del diff a monte e' rispetto a un BASE_REF che qui non esiste)
rsync -ar --exclude='.*' --exclude='*.sh' --exclude='ci' --exclude='*rerelease*' --exclude='README.md' \
	--exclude='unofficial-fossil.cdb' --exclude='release.cdb' Distribution/expansions/ "$UPDATES/expansions"

curl --retry 5 --connect-timeout 30 --location -o WindBotIgnite-Resources.7z "$WINDBOT_RESOURCES_URL"
7z x -y WindBotIgnite-Resources.7z -o"$ASSETS" >/dev/null
7z x -y WindBotIgnite-Resources.7z -o"$UPDATES" >/dev/null

find "$ASSETS" \( -name '*.md' -o -name 'COPYING*' -o -name 'LICENSE*' \) -exec mv {} {}.txt \;
mkdir -p "$ASSETS/deck"
rm -rf "$ASSETS"/config/*.json "$ASSETS"/config/languages/Thai
cp Distribution/config/configs.prod.json "$ASSETS/config/configs.json"
rm -rf "$UPDATES"/config/*.json "$UPDATES"/config/languages/Thai
mkdir -p "$UPDATES/config"
cp Distribution/config/configs.prod.json "$UPDATES/config/configs.json"
sed -i 's/dpi_scale = .*/dpi_scale = 2.000000/' "$ASSETS/config/system.conf"
rm -rf Distribution WindBotIgnite-Resources.7z
