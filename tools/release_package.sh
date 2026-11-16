#!/usr/bin/env bash
# tools/release_package.sh -- assemble the OpenBA release package (KITFREE RSCFONT).
#
# usage: tools/release_package.sh [--fonts <dir>] [--no-build] [flavour]
#
#   flavour      release (default) | perf | og | debug: the OpenBA.exe to package, built with
#                tools/build.sh build <flavour> first (--no-build takes the exe already there)
#   --fonts DIR  the directory holding FrancophilSans.ttf and FrancophilSans-Bold.ttf (the
#                user's copy; default C:/Users/USPC/Downloads/francophil_sans, or $FRANCOPHIL_DIR).
#                The typeface (its terms: docs/extensions/KITFREE.md §1) is kept under
#                tools/release/fonts/ (THIRD_PARTY_NOTICES.md) and ships beside the exe, where
#                src/platform/win/font_raster_win.cpp loads it privately when the face is not
#                installed.
#
# OUTPUT  build/release/
#   OpenBA.exe                    the executable (standalone: loads no retail binary)
#   FrancophilSans.ttf            the typeface, Regular
#   FrancophilSans-Bold.ttf       the typeface, Bold
#   FrancophilSans_README.txt     Manfred Klein's credit and terms (tools/release/, ours)
#   LICENSE, README.md            OpenBA's own, when present at the repo root
# and build/release/OpenBA-<version>.zip of the same (version = `git describe --tags`, or the
# short hash), when `zip` or python is available. The package holds NO game content and no
# compiled resource: the fonts are rasterised in the exe at run time, the dialogs are built from
# the embedded text (KITFREE).
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

FLAV=release; BUILD=1
FONTS="${FRANCOPHIL_DIR:-C:/Users/USPC/Downloads/francophil_sans}"
while [ $# -gt 0 ]; do
  case "$1" in
    --fonts) FONTS="$2"; shift 2;;
    --no-build) BUILD=0; shift;;
    debug|og|perf|release) FLAV="$1"; shift;;
    *) echo "release_package: unknown argument $1"; exit 2;;
  esac
done

for F in FrancophilSans.ttf FrancophilSans-Bold.ttf; do
  [ -f "$FONTS/$F" ] || { echo "release_package: FAILED -- $FONTS/$F missing (--fonts <dir> with the two TTFs)"; exit 1; }
done
[ -f tools/release/FrancophilSans_README.txt ] || { echo "release_package: FAILED -- tools/release/FrancophilSans_README.txt missing"; exit 1; }

if [ "$BUILD" = 1 ]; then
  tools/build.sh build "$FLAV" || { echo "release_package: FAILED -- build $FLAV"; exit 1; }
fi
EXE="build/cmake-$FLAV/OpenBA.exe"
[ -f "$EXE" ] || { echo "release_package: FAILED -- $EXE not built"; exit 1; }

OUT="build/release"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -f "$EXE" "$OUT/OpenBA.exe"
cp -f "$FONTS/FrancophilSans.ttf" "$FONTS/FrancophilSans-Bold.ttf" "$OUT/"
cp -f tools/release/FrancophilSans_README.txt "$OUT/"
[ -f LICENSE ] && cp -f LICENSE "$OUT/"
[ -f README.md ] && cp -f README.md "$OUT/"

# No game content may ride along: refuse a package with a compiled resource or a glyph sheet.
if ls "$OUT"/*.crs "$OUT"/*.pkg "$OUT"/*.pkm > /dev/null 2>&1; then
  echo "release_package: FAILED -- a compiled resource or package found in $OUT"; exit 1
fi

# `git describe` may name a tag with a slash: keep the version a plain file-name token.
VERSION="$(git describe --tags --always 2>/dev/null | tr '/\\' '--' || echo dev)"
[ -n "$VERSION" ] || VERSION=dev
ZIP="$OUT/OpenBA-$VERSION.zip"
if command -v zip > /dev/null 2>&1; then
  (cd "$OUT" && zip -q "OpenBA-$VERSION.zip" OpenBA.exe FrancophilSans.ttf FrancophilSans-Bold.ttf FrancophilSans_README.txt $( [ -f LICENSE ] && echo LICENSE ) $( [ -f README.md ] && echo README.md ))
else
  # python's zipfile (the build already needs python for cmake / ninja)
  python -I - "$OUT" "OpenBA-$VERSION.zip" <<'PY' || ZIP="(no zip tool; files only)"
import os, sys, zipfile
out, name = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(os.path.join(out, name), "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(out)):
        if f != name:
            z.write(os.path.join(out, f), f)
PY
fi
echo "release_package: $OUT ($(ls "$OUT" | tr '\n' ' '))"
echo "release_package: $ZIP"
