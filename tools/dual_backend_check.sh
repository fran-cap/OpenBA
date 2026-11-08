#!/usr/bin/env bash
# tools/dual_backend_check.sh -- the dual exe's renderer choice, unattended (src/ext/render_backend/)
#
# usage: tools/dual_backend_check.sh <boot> [switch-spec] [quit-frame]
#   <boot>         dx11 | vulkan: the renderer the run starts (settings.cfg ext.render_backend=1 and
#                  ext.render_api=0 / 1 in the user dir)
#   [switch-spec]  BANISHED_RENDER_SWITCH (src/diag/render_switch_drive.h), e.g.
#                  "900:vulkan,1800:dx11" (the full video restart at those game frames) or
#                  "failcreate=vulkan" (the boot's Vulkan CreateDevice fails once: the fallback);
#                  empty: no switch
#   [quit-frame]   the game frame the pause menu's Quit Game is clicked on (BANISHED_PAUSEMENU);
#                  default 600 after the last switch
#
# Takes BANISHED_DIR / BANISHED_USER_DIR from tools/game_dirs.sh (lock first; eval its output as its own
# command), deploys build/cmake-debug-dx11+vulkan/OpenBA.exe (built with
# `BANISHED_RENDER_BACKEND=dx11+vulkan tools/build.sh build`), starts a new game (BANISHED_AUTOSTART, no
# /profile: the extensions follow settings.cfg), waits for the run to end and prints its verdict and the
# [RenderBackend] / [RenderSwitch] / [Vulkan] / [DeviceLost] lines of standalone_error.log. The caller
# backs up and restores settings.cfg: this script rewrites two keys in it.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOOT="${1:-}"
SPEC="${2:-}"
QUIT="${3:-}"
case "$BOOT" in
  dx11) API=0;;
  vulkan) API=1;;
  *) echo "usage: $0 dx11|vulkan [switch-spec] [quit-frame]"; exit 2;;
esac
[ -n "${BANISHED_DIR:-}" ] || { echo "BANISHED_DIR is not set: lock a game dir first (tools/game_dirs.sh env ...)"; exit 2; }
USER_DIR="${BANISHED_USER_DIR:-${USERPROFILE}/Documents/Banished}"
CFG="$USER_DIR/settings.cfg"
EXE="$ROOT/build/cmake-debug-dx11+vulkan/OpenBA.exe"
[ -f "$EXE" ] || { echo "no dual exe at $EXE"; exit 2; }
[ -f "$CFG" ] || { echo "no settings.cfg at $CFG"; exit 2; }

# The last switch's frame + 600, unless given.
if [ -z "$QUIT" ]; then
  LAST=0
  for tok in ${SPEC//,/ }; do
    f="${tok%%:*}"
    case "$f" in ''|*[!0-9]*) continue;; esac
    [ "$f" -gt "$LAST" ] && LAST="$f"
  done
  QUIT=$((LAST + 600))
fi

set_key() {
  if grep -q "^$1=" "$CFG"; then
    sed -i "s|^$1=.*|$1=$2|" "$CFG"
  else
    printf '%s=%s\n' "$1" "$2" >> "$CFG"
  fi
}
set_key ext.render_backend 1
set_key ext.render_api "$API"

cp -f "$EXE" "$BANISHED_DIR/OpenBA.exe" || { echo "deploy: exe busy, is a game running?"; exit 1; }
rm -f "$BANISHED_DIR/standalone_debug.log" "$BANISHED_DIR/standalone_error.log" "$BANISHED_DIR/standalone_crash.log" \
      "$BANISHED_DIR/standalone_hang.log"
echo "== dual check: boot $BOOT, switches '${SPEC}', quit at frame $QUIT ($BANISHED_DIR)"
(
  cd "$BANISHED_DIR" || exit 1
  BANISHED_AUTOSTART=1 BANISHED_RENDER_SWITCH="$SPEC" BANISHED_PAUSEMENU="$QUIT" ./OpenBA.exe
  echo "== exit code $?"
)
grep -E "^\[(RenderBackend|RenderSwitch|DeviceLost)\]|^\[Vulkan\] (not available|instance|device|shutdown)|AutoStart\] sequence fired" \
  "$BANISHED_DIR/standalone_error.log" | head -80
[ -s "$BANISHED_DIR/standalone_crash.log" ] && { echo "== CRASH: $BANISHED_DIR/standalone_crash.log"; head -40 "$BANISHED_DIR/standalone_crash.log"; }
[ -s "$BANISHED_DIR/standalone_hang.log" ] && echo "== HANG: $BANISHED_DIR/standalone_hang.log"
exit 0
