#!/usr/bin/env bash
# Copy the pristine vendored lib into the build tree and apply our patches.
# src/dsp/lib is NEVER modified in place — see VENDOR.md.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
OUT="${1:-$REPO_ROOT/build/lib}"

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$REPO_ROOT"/src/dsp/lib/*.h "$OUT"/

shopt -s nullglob
for p in "$REPO_ROOT"/patches/*.patch; do
    echo "applying $(basename "$p")"
    patch -d "$OUT" -p1 --forward --silent < "$p"
done
echo "patched lib -> $OUT"
