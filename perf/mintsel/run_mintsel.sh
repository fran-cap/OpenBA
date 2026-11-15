#!/usr/bin/env bash
# usage: run_mintsel.sh <label> [extra env assignments...]
set -u
WT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"   # the repo this script lives in (perf/mintsel/)
GAME="${BANISHED_DIR:-C:/GOG Games/Banished}"            # the game directory (env BANISHED_DIR, as tools/build.sh)
OUT="$WT/perf/mintsel/$1"
shift
mkdir -p "$OUT"
cd "$WT" || exit 1

md5_src=$(md5sum build/debug/OpenBA.exe | cut -d' ' -f1)
cp build/debug/OpenBA.exe "$GAME/OpenBA.exe" || exit 1
md5_dst=$(md5sum "$GAME/OpenBA.exe" | cut -d' ' -f1)
echo "[md5] build  = $md5_src"
echo "[md5] deployed = $md5_dst"
[ "$md5_src" = "$md5_dst" ] || { echo "MD5 MISMATCH"; exit 1; }

tools/game_dir_lock.sh verify lane-MINTSEL || exit 1

rm -f "$GAME/standalone_debug.log" "$GAME/standalone_error.log" "$GAME/standalone_crash.log" "$GAME/standalone_boot_state.txt"

(
  cd "$GAME" || exit 1
  export BANISHED_AUTOSTART=1
  export BANISHED_RT_MINT_SHADOW=1
  export BANISHED_PAUSEMENU=420
  export BANISHED_PAUSEMENU_QUIT=120
  for kv in "$@"; do export "$kv"; done
  ./OpenBA.exe
) &
PID=$!

tools/boot_state.sh --wait 300
echo "[boot_state] exit=$?"

# give it up to 4 minutes to self-quit
for i in $(seq 1 48); do
  kill -0 $PID 2>/dev/null || break
  sleep 5
done
kill -0 $PID 2>/dev/null && { echo "[run] STILL ALIVE after 240s -- killing"; kill $PID; sleep 3; }
wait $PID 2>/dev/null
echo "[run] process gone"

cp "$GAME"/standalone_debug.log "$OUT"/ 2>/dev/null
cp "$GAME"/standalone_error.log "$OUT"/ 2>/dev/null
cp "$GAME"/standalone_crash.log "$OUT"/ 2>/dev/null
echo "[run] logs in $OUT"
ls -la "$OUT"
