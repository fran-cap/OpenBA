#!/usr/bin/env bash
# tools/toolchain/install_winlibs.sh -- the pinned Windows compiler (2026-10-26).
#
# OpenBA on Windows is built with ONE compiler build, pinned here: WinLibs GCC 16.2.0
# (mingw-w64 14.0.0, UCRT, posix threads, SEH), release 3. The pin is a measured choice
# (perf/pgo/README.md: GCC 16 is ~3% faster on the sim than the MSYS2 GCC 13.1 it replaced, and
# PGO profiles only load into the exact compiler version that wrote them), so the version is
# fixed, not "latest". Moving it = change the three constants below, rebuild, retrain the
# profile set (perf/pgo/train.sh), re-run the gate and the resave check, update
# docs/reference/environment.md.
#
# usage: tools/toolchain/install_winlibs.sh [<dir>]      default C:/winlibs-gcc16
#   Downloads the zip (GitHub release), verifies its sha256 against the pin, unpacks it so
#   <dir>/mingw64/bin/g++.exe exists. Idempotent: an existing install of the pinned version
#   is left alone. tools/build.sh reads BANISHED_GCC_DIR (default <dir>/mingw64/bin).
#   CI (.github/workflows/ci.yml, release.yml) runs this on the Windows runner.
set -euo pipefail

WINLIBS_TAG="16.2.0posix-23.1.3-14.0.0-ucrt-r3"
WINLIBS_ZIP="winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.zip"
WINLIBS_SHA256="3c69305081e4a647144339a936244fcf7d3d9e558319e1ad7be2cd2886d4b6a1"
WINLIBS_GCC_VERSION="16.2.0"

DIR="${1:-C:/winlibs-gcc16}"
URL="https://github.com/brechtsanders/winlibs_mingw/releases/download/${WINLIBS_TAG}/${WINLIBS_ZIP}"

if [ -x "$DIR/mingw64/bin/g++.exe" ]; then
    have="$("$DIR/mingw64/bin/g++.exe" -dumpfullversion 2>/dev/null || true)"
    if [ "$have" = "$WINLIBS_GCC_VERSION" ]; then
        echo "winlibs GCC $have already at $DIR/mingw64/bin"
        exit 0
    fi
    echo "!! $DIR/mingw64/bin holds GCC '$have', the pin is $WINLIBS_GCC_VERSION: remove it or pass another dir" >&2
    exit 1
fi

mkdir -p "$DIR"
cd "$DIR"
echo "== downloading $WINLIBS_ZIP"
curl -fsSL --retry 3 -o "$WINLIBS_ZIP" "$URL"
echo "$WINLIBS_SHA256  $WINLIBS_ZIP" | sha256sum -c -
echo "== unpacking into $DIR"
unzip -q "$WINLIBS_ZIP"
rm -f "$WINLIBS_ZIP"
"$DIR/mingw64/bin/g++.exe" --version | head -1
