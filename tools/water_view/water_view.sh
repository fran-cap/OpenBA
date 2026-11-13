#!/usr/bin/env bash
# tools/water_view/water_view.sh -- dump a map's water mesh and heights for water_view.py.
#
# Starts a new game (or loads a save) unattended with BANISHED_WATER_DUMP set, so ext.better_water's
# water-copy rebuild writes <outdir>/water_dump.bin (src/diag/water_dump.h), then quits through the
# pause menu. The run uses the settings.cfg profile, not /profile:faithful: the dump is made where the
# extension makes its copy, so ext.better_water must be on (its default).
#
# usage: tools/water_view/water_view.sh <outdir> [seed] [small|medium|large]
#        WATER_VIEW_SAVE=<save name> tools/water_view/water_view.sh <outdir>
#   WATER_VIEW_EXE   the exe (default build/cmake-debug/OpenBA.exe; build it first)
#   WATER_VIEW_EXT   <id>[,<id>...]: extensions turned on for this run (ext.<id>=1 in the functional user
#                    dir's settings.cfg, put back afterwards), e.g. WATER_VIEW_EXT=river_channel
#   WATER_VIEW_LANE  the game-dir lane (default water-view)
# Then: tools/water_view/water_view.py <outdir>/water_dump.bin --thin   (README.md)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${1:?usage: water_view.sh <outdir> [seed] [small|medium|large]}"
SEED="${2:-12345}"
SIZE="${3:-medium}"
EXE="${WATER_VIEW_EXE:-$REPO/build/cmake-debug/OpenBA.exe}"
LANE="${WATER_VIEW_LANE:-water-view}"
SECS=420

[ -f "$EXE" ] || { echo "[water_view] no exe: $EXE (tools/build.sh build debug)" >&2; exit 2; }
mkdir -p "$OUT"
OUT_ABS="$(cd "$OUT" && pwd)"
OUT_WIN="$OUT_ABS"
command -v cygpath >/dev/null 2>&1 && OUT_WIN="$(cygpath -m "$OUT_ABS")"
rm -f "$OUT_ABS/water_dump.bin" "$OUT_ABS"/standalone_*.log

LOCKED=0
release_lock() {
    if [ "$LOCKED" = 1 ]; then
        "$REPO/tools/game_dirs.sh" release "$LANE"
        LOCKED=0
    fi
}
trap release_lock EXIT
ENV_LINES="$("$REPO/tools/game_dirs.sh" env "$LANE" functional 1800)" || { echo "[water_view] lock not granted" >&2; exit 2; }
eval "$ENV_LINES"
LOCKED=1
G="$BANISHED_DIR"
command -v cygpath >/dev/null 2>&1 && G="$(cygpath -u "$G")"

CFG=""
CFG_SAVED=""
restore_cfg() {
    if [ -n "$CFG_SAVED" ]; then
        cp -f "$CFG_SAVED" "$CFG"
        rm -f "$CFG_SAVED"
        CFG_SAVED=""
    fi
}
if [ -n "${WATER_VIEW_EXT:-}" ]; then
    U="${BANISHED_USER_DIR:-}"
    [ -n "$U" ] || { echo "[water_view] WATER_VIEW_EXT needs the functional user dir (BANISHED_USER_DIR)" >&2; exit 2; }
    command -v cygpath >/dev/null 2>&1 && U="$(cygpath -u "$U")"
    mkdir -p "$U"
    CFG="$U/settings.cfg"
    CFG_SAVED="$(mktemp)"
    if [ -f "$CFG" ]; then cp -f "$CFG" "$CFG_SAVED"; else : > "$CFG_SAVED"; touch "$CFG"; fi
    trap 'restore_cfg; release_lock' EXIT
    for id in $(echo "$WATER_VIEW_EXT" | tr ',' ' '); do
        sed -i "/^ext\.$id=/d" "$CFG"
        printf 'ext.%s=1\n' "$id" >> "$CFG"
    done
    echo "[water_view] extensions on for this run: $WATER_VIEW_EXT"
fi

cp -f "$EXE" "$G/OpenBA.exe" || { echo "[water_view] exe busy, is a game running?" >&2; exit 2; }
rm -f "$G"/standalone_{debug,error,crash,hang}.log "$G/standalone_boot_state.txt"

if [ -n "${WATER_VIEW_SAVE:-}" ]; then
    START_ENV=(BANISHED_AUTOLOAD="$WATER_VIEW_SAVE")
    echo "[water_view] save $WATER_VIEW_SAVE"
else
    START_ENV=(BANISHED_AUTOSTART=1 BANISHED_SEED="$SEED" BANISHED_MAP_SIZE="$SIZE")
    echo "[water_view] new game, seed $SEED, $SIZE"
fi
START=$(date +%s)
(
    cd "$G" || exit 2
    exec env "${START_ENV[@]}" BANISHED_WATER_DUMP="$OUT_WIN" \
        BANISHED_PAUSEMENU=300 BANISHED_PAUSEMENU_QUIT=120 ./OpenBA.exe
) &
PID=$!
"$REPO/tools/boot_state.sh" --wait 300
echo "[water_view] boot_state rc=$?"
while kill -0 "$PID" 2>/dev/null; do
    [ $(( $(date +%s) - START )) -ge "$SECS" ] && break
    sleep 2
done
if kill -0 "$PID" 2>/dev/null; then
    echo "[water_view] alive after ${SECS}s, killing this run"
    bash "$REPO/tools/kill_game.sh" "$G"
    kill -9 "$PID" 2>/dev/null
fi
wait "$PID" 2>/dev/null
echo "[water_view] exit status: $?"
for f in standalone_error.log standalone_crash.log standalone_hang.log; do
    [ -f "$G/$f" ] && cp -f "$G/$f" "$OUT_ABS/"
done
restore_cfg
release_lock

grep -ah "\[WaterDump\]\|\[RiverChannel\]\|\[BetterWater\] river" "$OUT_ABS/standalone_error.log" 2>/dev/null | tail -4
[ -s "$OUT_ABS/standalone_crash.log" ] && { echo "[water_view] CRASH -- read $OUT_ABS/standalone_crash.log"; exit 1; }
[ -f "$OUT_ABS/water_dump.bin" ] || { echo "[water_view] FAIL: no dump (is ext.better_water on in settings.cfg?)"; exit 1; }
echo "[water_view] done: $OUT_ABS/water_dump.bin"
