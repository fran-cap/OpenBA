#!/usr/bin/env bash
# One run of the deployed exe for the TREE_SHADOW blocks (a copy of tools/foliage_aa/run_arm.sh). The caller holds the game-dir lock as $LANE (default tsfix).
#   run_arm.sh <tag> <captureFrame|0> <seconds-in-game> [ENV=V ...] [-- exe args]
# captureFrame > 0: launch under renderdoccmd, wait for the .rdc, keep it as rdoc_tree/<tag>.rdc
# 0: plain launch, wait RUNNING, stay <seconds-in-game>, kill BY PID.
set -u
GAME="${BANISHED_DIR:-C:/GOG Games/Banished}"   # the game directory (env BANISHED_DIR, as tools/build.sh)
WT="$(cd "$(dirname "$0")/../.." && pwd)"
LANE="${LANE:-tsfix}"
RDCMD="${RENDERDOCCMD:-C:/Program Files/RenderDoc/renderdoccmd.exe}"
OUT="$GAME/rdoc_tree"
TAG="$1"; CAP="$2"; SECS="$3"; shift 3
ENVS=(); ARGS=()
while [ $# -gt 0 ]; do
  if [ "$1" = "--" ]; then shift; ARGS=("$@"); break; fi
  ENVS+=("$1"); shift
done
mkdir -p "$OUT"
cd "$GAME" || exit 9
"$WT/tools/game_dir_lock.sh" assert-held "$LANE" "$WT/build/cmake-debug/OpenBA.exe" || exit 8
rm -f "$GAME"/reimpl_capture*.rdc standalone_debug.log standalone_error.log standalone_crash.log standalone_hang.log
{ echo "tag=$TAG cap=$CAP secs=$SECS"; echo "exe_md5=$(md5sum OpenBA.exe | cut -d' ' -f1)";
  for e in "${ENVS[@]:-}"; do echo "env=$e"; done; echo "args=${ARGS[*]:-}"; date -u; } > "$OUT/$TAG.arm.txt"

pidof_game() { tasklist //FI "IMAGENAME eq OpenBA.exe" //FO CSV //NH 2>/dev/null | grep -i OpenBA | cut -d'"' -f4 | head -1; }

if [ "$CAP" -gt 0 ]; then
  env "${ENVS[@]}" BANISHED_AUTOSTART=1 BANISHED_CAPTURE_INGAME="$CAP" MSYS2_ARG_CONV_EXCL="*" \
    "$RDCMD" capture --opt-ref-all-resources -d "$GAME" -c "$GAME/reimpl_capture" \
    "$GAME/OpenBA.exe" "${ARGS[@]}" > "$OUT/$TAG.rdcmd.log" 2>&1 &
  rdc=""
  for t in $(seq 1 400); do
    sleep 2
    # the menu loop captures its 10th frame whenever RenderDoc is attached (menudialogloopt_stubs.cpp):
    # the IN-GAME capture is the SECOND file
    n=$(ls "$GAME"/reimpl_capture*.rdc 2>/dev/null | wc -l)
    cand=""
    [ "$n" -ge 2 ] && cand=$(ls -t "$GAME"/reimpl_capture*.rdc 2>/dev/null | head -1)
    if [ -n "$cand" ]; then
      s1=$(stat -c%s "$cand"); sleep 4; s2=$(stat -c%s "$cand")
      if [ "$s1" = "$s2" ] && [ "$s1" != "0" ]; then rdc="$cand"; break; fi
    fi
    if [ $t -gt 10 ] && [ -z "$(pidof_game)" ]; then echo "process gone before a capture"; break; fi
  done
  sleep "$SECS"
  pid=$(pidof_game); [ -n "$pid" ] && taskkill //PID "$pid" //F > /dev/null 2>&1
  sleep 2
  menu=$(ls -tr "$GAME"/reimpl_capture*.rdc 2>/dev/null | head -1)
  [ -n "$menu" ] && [ "$menu" != "$rdc" ] && mv -f "$menu" "$OUT/${TAG}_menu.rdc"
  if [ -n "$rdc" ]; then mv -f "$rdc" "$OUT/$TAG.rdc"; echo "capture -> $OUT/$TAG.rdc ($(( $(stat -c%s "$OUT/$TAG.rdc") / 1048576 )) MB)"; else echo "NO CAPTURE"; fi
else
  ( env "${ENVS[@]}" BANISHED_AUTOSTART=1 MSYS2_ARG_CONV_EXCL="*" "$GAME/OpenBA.exe" "${ARGS[@]}" > /dev/null 2>&1 & )
  sleep 3
  "$WT/tools/boot_state.sh" --wait 300; bs=$?
  echo "boot_state rc=$bs"
  sleep "$SECS"
  "$WT/tools/boot_state.sh"
  pid=$(pidof_game); [ -n "$pid" ] && taskkill //PID "$pid" //F > /dev/null 2>&1
  sleep 2
fi
cp -f standalone_error.log "$OUT/$TAG.error.log" 2>/dev/null
cp -f standalone_debug.log "$OUT/$TAG.debug.log" 2>/dev/null
[ -s standalone_crash.log ] && { echo "CRASH LOG:"; head -30 standalone_crash.log; cp -f standalone_crash.log "$OUT/$TAG.crash.log"; }
[ -s standalone_hang.log ] && { echo "HANG LOG present"; cp -f standalone_hang.log "$OUT/$TAG.hang.log"; }
grep -c "BillboardShadow" "$OUT/$TAG.error.log"
exit 0
