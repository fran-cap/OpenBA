#!/bin/bash
# Usage: make_release_zip.sh [APP [OUTPUT_DIR]]
# OUTPUT_DIR defaults to dist; use a temporary directory for a packaging dry run.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="${1:-$ROOT/dist/OpenBA.app}"
OUT="${2:-$ROOT/dist}"
[[ -d "$APP/Contents" ]] || { echo "Missing app: $APP" >&2; exit 1; }
# Follow links too: a bundled WinData link must never smuggle in retail data.
BAD="$(find -L "$APP" \( -iname '*.pkg' -o -iname '*.pkm' -o -iname 'WinData' -o -iname '*.ttf' \) -print)"
[[ -z "$BAD" ]] || { printf 'Forbidden package content:\n%s\n' "$BAD" >&2; exit 1; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
ditto -c -k --sequesterRsrc --keepParent "$APP" "$TMP/OpenBA-mac-arm64.zip"
mv "$TMP/OpenBA-mac-arm64.zip" "$OUT/"
ls -l "$OUT/OpenBA-mac-arm64.zip"
