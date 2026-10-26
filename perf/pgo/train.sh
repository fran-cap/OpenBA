#!/usr/bin/env bash
# perf/pgo/train.sh -- retrain the release profile set (profile-guided optimisation), Windows.
#
# usage: perf/pgo/train.sh [--build] [--keep]
#   1. builds the release flavour INSTRUMENTED (BANISHED_PGO=generate) into build/cmake-release-pgogen
#   2. empties perf/pgo/profile/ and runs the two training towns on the instrumented exe
#      (tools/stress_run.sh, ~9x slower than the plain exe, ~2 + 4 minutes):
#        10x, 3000 frames, a storage barn's panel held open from frame 1200
#        50x, 3000 frames, a market's panel held open from frame 1500
#      both end through the pause menu (Quit Game), a clean exit, which is when GCC writes
#      the counters. The mix is what makes the profile general: trained on 50x alone it gave
#      -4% at 10x; with this mix -10% at 10x and -12% at 50x (perf/pgo/README.md).
#   3. --build: `tools/build.sh build release` on the new set (BANISHED_PGO=auto picks it up)
#   --keep leaves the old profile set in place and ADDS the new runs to it (GCC sums counters).
# Needs the game directory (BANISHED_DIR), the mods parked, and nothing else running on the
# machine (perf measurements are not taken here, but a loaded machine makes the training town
# diverge). Commit perf/pgo/profile/ afterwards: the release workflow builds on it.
# Exit 1 = a step failed; the profile set is then whatever the failed step left.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
BUILD=0; KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --build) BUILD=1; shift;;
        --keep)  KEEP=1; shift;;
        *) echo "unknown argument $1" >&2; exit 2;;
    esac
done
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) echo "train.sh is the Windows recipe (the other platforms: perf/pgo/README.md)" >&2; exit 2;; esac
GAME_DIR="${BANISHED_DIR:-C:/GOG Games/Banished}"
PROFILE="$ROOT/perf/pgo/profile"
GEN="build/cmake-release-pgogen"

if tasklist 2>/dev/null | grep -qi 'OpenBA.exe'; then
    echo "OpenBA.exe is already running -- stop it first" >&2
    exit 1
fi

echo "== 1. instrumented release build ($GEN)"
# Its own build dir, configured once with BANISHED_PGO=generate; tools/build.sh's configure
# would not pass the flag to an existing dir, so the configure is done here.
if [ ! -f "$GEN/build.ninja" ] || ! grep -q '^BANISHED_PGO:STRING=generate$' "$GEN/CMakeCache.txt"; then
    rm -rf "$GEN"
    GCC_DIR="${BANISHED_GCC_DIR:-C:/winlibs-gcc16/mingw64/bin}"
    export PATH="$GCC_DIR:$PATH"
    cmake -S . -B "$GEN" -G Ninja -DBANISHED_FLAVOUR=release -DBANISHED_PGO=generate \
          -DBANISHED_GAME_DIR="$GAME_DIR" -DBANISHED_USE_CCACHE=OFF \
          -DCMAKE_C_COMPILER="$GCC_DIR/gcc.exe" -DCMAKE_CXX_COMPILER="$GCC_DIR/g++.exe" \
          -DCMAKE_RC_COMPILER="$GCC_DIR/windres.exe" || exit 1
fi
ninja -C "$GEN" -j"${BUILD_JOBS:-8}" OpenBA || exit 1

echo "== 2. training runs"
mkdir -p "$PROFILE"
if [ "$KEEP" = 0 ]; then
    rm -f "$PROFILE"/*.gcda
fi
cp -f "$GEN/OpenBA.exe" "$GAME_DIR/OpenBA.exe" || { echo "deploy failed (exe busy?)" >&2; exit 1; }
BANISHED_PANEL_HOLD=StorageComponent BANISHED_PANEL_HOLD_FRAME=1200 \
    tools/stress_run.sh --no-build --label pgo_train_10x --frames 3000 --timeout 580 --baseline none || exit 1
BANISHED_PANEL_HOLD=MarketComponent BANISHED_PANEL_HOLD_FRAME=1500 \
    tools/stress_run.sh --no-build --label pgo_train_50x --speed 50 --frames 3000 --timeout 580 --baseline none || exit 1
n="$(ls "$PROFILE"/*.gcda 2>/dev/null | wc -l)"
echo "== profile set: $n .gcda files in perf/pgo/profile"
[ "$n" -gt 0 ] || { echo "no profile written: did the runs exit cleanly?" >&2; exit 1; }

if [ "$BUILD" = 1 ]; then
    echo "== 3. release build on the new profile"
    # The release dir may hold objects compiled on the previous set; the profile is a compile
    # input ninja does not track, so the objects are rebuilt from scratch.
    rm -rf build/cmake-release
    tools/build.sh build release || exit 1
    echo "== release exe: build/cmake-release/OpenBA.exe"
fi
echo "== done; commit perf/pgo/profile/ with the release"
