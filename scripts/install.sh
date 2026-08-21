#!/usr/bin/env bash
# Install Capicola module to a Schwung device over SSH
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

MOVE_HOST="${MOVE_HOST:-ableton@move.local}"
DEST="/data/UserData/schwung/modules/audio_fxs/capicola"

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
