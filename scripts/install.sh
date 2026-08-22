#!/usr/bin/env bash
# Install Capicola module to a Schwung device over SSH
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

MOVE_HOST="${MOVE_HOST:-ableton@move.local}"
# NOT "audio_fxs". Schwung's docs describe the store extracting to
# modules/<component_type>s/<id>/, but the directory on a real device is
# modules/audio_fx/ (alongside midi_fx, tools, overtake — only
# sound_generators is pluralised). Verified against the nine audio FX
# installed on hardware. Get this wrong and the module is simply never
# discovered, with no error anywhere.
DEST="/data/UserData/schwung/modules/audio_fx/capicola"

if [ ! -f "dist/capicola/capicola.so" ]; then
    echo "Error: dist/capicola/capicola.so not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing Capicola Module ==="
echo "Target: $MOVE_HOST:$DEST"

ssh "$MOVE_HOST" "mkdir -p $DEST"
scp -r dist/capicola/* "$MOVE_HOST:$DEST/"

echo ""
echo "=== Install Complete ==="
echo "Module installed to: $DEST"
echo ""
echo "Restart Schwung to load the new module."
