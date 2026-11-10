#!/usr/bin/env bash
# =============================================================================
# tools/device_sim.sh -- run OpenBA.exe on Windows as if on an Android phone or tablet
# =============================================================================
#
# WHY. The phone / tablet layout (ext.phone_menu "Phone screen fit", ext.touch_mode "Touch
# controls", ext.touch_pointer "Touch pointer") is a function of the screen's pixel size and the UI
# scale, and checking it on a device costs a build, an install and a hand on the screen. This
# script gives the same screen on the desktop in one command and writes a full-resolution
# screenshot of it. Facts, limits and what is NOT the device: docs/reference/device_sim.md.
#
# USAGE
#   tools/device_sim.sh <phone|tablet> [--screenshot PATH] [--ui N] [--view game|panels|options]
#                       [--panels LIST] [--tap FX,FY] [--building TEMPLATE] [--set KEY=VALUE]...
#                       [--lane NAME] [--jobs N] [--no-build]
#
#   phone    Galaxy S25 class, 2340 x 1080 (docs/android/TOUCH_MODE.md, the reference device)
#   tablet   OnePlus Pad class, 3392 x 2400 (issue #96)
#   --screenshot PATH  where the PNG goes (default: device_sim_<profile>.png in the current dir)
#   --ui N             GameUIScale (default 50: what the Android first run picks, see the doc)
#   --view panels      (default) the Professions, Town and Event Log panels open, a building armed
#                      in the placement tool and one finger tap on the world, so ext.touch_pointer's
#                      action bar and the ghost show
#   --view game        the plain game screen (no harness dialogs, no tap)
#   --view options     the Options dialog on its Enhanced tab, opened from the pause menu
#                      (GameLastOption=4; the run is ended once the shot is written)
#   --panels LIST      the utility panels --view panels opens, in order, comma separated, from
#                      professions, town, eventlog, limits, map, help (default professions,town,eventlog)
#   --tap FX,FY[,DELAY[,HOLD]]  where the finger lands, as fractions of the window (default 0.62,0.42);
#                      DELAY frames after the harness (default 30), held HOLD frames (default 1: a tap; a
#                      longer hold presses a held control such as a camera button)
#   --building T       the armed template (default Template\WoodHouse.rsc)
#   --set KEY=VALUE    one more settings.cfg key, written after the profile's (repeatable; e.g.
#                      --set ext.touch_mode_pad_scale=0). The saved settings.cfg is put back after.
#   --no-build         deploy the exe already built (build/cmake-debug/OpenBA.exe)
#
# WHAT IT DOES, in order: builds the debug exe (tools/build.sh build debug), takes the FUNCTIONAL
# game dir (tools/game_dirs.sh env <lane> functional), saves that dir's settings.cfg, writes the
# profile's keys into it, deploys the exe there (tools/build.sh deploy with BANISHED_DIR set by the
# lock), launches with BANISHED_AUTOSTART=1, waits with tools/boot_state.sh --wait, lets the harness
# take the game's own with-interface screenshot (BANISHED_SCREENSHOT_AT=<frame>,ui: the back buffer,
# so a window larger than the monitor is captured whole), quits through the pause menu, converts the
# .bmp to PNG, puts settings.cfg back and releases the lane. Never the perf dir, never the user's
# own C:/GOG Games/Banished settings.
#
# EXIT: 0 = screenshot written; 1 = the run failed (crash / hang / no screenshot; the log paths are
# printed); 2 = usage or setup error.
# =============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

die() {
    echo "[device_sim] $*" >&2
    exit 2
}

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) ;;
    *) die "Windows only (the DX11 exe and the Win32 window)" ;;
esac

PROFILE="${1:-}"
[ -n "$PROFILE" ] || die "usage: tools/device_sim.sh <phone|tablet> [--screenshot PATH] [--ui N] [--view game|panels] [--tap FX,FY] [--building T] [--lane NAME] [--jobs N] [--no-build]"
shift
case "$PROFILE" in
    phone)
        WIN_W=2340
        WIN_H=1080
        ;;
    tablet)
        WIN_W=3392
        WIN_H=2400
        ;;
    *) die "unknown profile '$PROFILE' (phone|tablet)" ;;
esac

SHOT="$PWD/device_sim_$PROFILE.png"
UI=50
VIEW=panels
TAP="0.62,0.42"
BUILDING='Template\WoodHouse.rsc'
PANELS="professions,town,eventlog"
LANE="device-sim"
JOBS="${BUILD_JOBS:-8}"
BUILD=1
EXTRA_KEYS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --screenshot) SHOT="${2:?--screenshot needs a path}"; shift 2 ;;
        --ui)         UI="${2:?--ui needs a percent}"; shift 2 ;;
        --view)       VIEW="${2:?--view needs game|panels}"; shift 2 ;;
        --tap)        TAP="${2:?--tap needs FX,FY}"; shift 2 ;;
        --building)   BUILDING="${2:?--building needs a template}"; shift 2 ;;
        --panels)     PANELS="${2:?--panels needs a list}"; shift 2 ;;
        --lane)       LANE="${2:?--lane needs a name}"; shift 2 ;;
        --jobs)       JOBS="${2:?--jobs needs a number}"; shift 2 ;;
        --set)
            case "${2:-}" in
                ?*=*) EXTRA_KEYS+=("$2") ;;
                *) die "--set needs KEY=VALUE" ;;
            esac
            shift 2
            ;;
        --no-build)   BUILD=0; shift ;;
        *) die "unknown option '$1'" ;;
    esac
done
case "$VIEW" in game | panels | options) ;; *) die "--view is game, panels or options" ;; esac
case "$UI" in '' | *[!0-9]*) die "--ui is a whole percent" ;; esac
# The toolbar's utility templates; "town" is the town status window (UtilityStatusBar).
TEMPLATES=""
for panel in ${PANELS//,/ }; do
    case "$panel" in
        professions) t='Template\UtilityProfessions.rsc' ;;
        town)        t='Template\UtilityStatusBar.rsc' ;;
        eventlog)    t='Template\UtilityEventLog.rsc' ;;
        limits)      t='Template\UtilityLimits.rsc' ;;
        map)         t='Template\UtilityMap.rsc' ;;
        help)        t='Template\UtilityHelp.rsc' ;;
        *) die "unknown panel '$panel' (professions, town, eventlog, limits, map, help)" ;;
    esac
    TEMPLATES="${TEMPLATES:+$TEMPLATES,}$t"
done

# The frames (GameUpdateLoop::Update calls, at the profile's 60 fps cap): the harness fires at 60,
# the finger 30 later, the shot at SHOT_FRAME, the pause menu opens at QUIT_FRAME (clamped up to
# 600 by the pause-menu harness) and clicks Quit Game 120 frames after.
SHOT_FRAME=420
QUIT_FRAME=600
DEADLINE_SECS=420

cd "$REPO" || die "no repo at $REPO"
if [ "$BUILD" = 1 ]; then
    tools/build.sh build debug --jobs "$JOBS" || die "build failed"
fi
EXE="$REPO/$(tools/build.sh exe debug)"
[ -f "$EXE" ] || die "no exe at $EXE (run without --no-build)"

# ---------------------------------------------------------------- the lane
LOCKED=0
CFG=""
CFG_SAVED=""
restore_cfg() {
    if [ -n "$CFG_SAVED" ]; then
        cp -f "$CFG_SAVED" "$CFG"
        rm -f "$CFG_SAVED"
        CFG_SAVED=""
        echo "[device_sim] settings.cfg put back"
    fi
}
cleanup() {
    restore_cfg
    if [ "$LOCKED" = 1 ]; then
        "$HERE/game_dirs.sh" release "$LANE"
        LOCKED=0
    fi
}
trap cleanup EXIT

ENV_LINES="$("$HERE/game_dirs.sh" env "$LANE" functional 1800)" || die "functional game dir lock not granted"
eval "$ENV_LINES"
LOCKED=1
[ -n "${BANISHED_USER_DIR:-}" ] || die "the functional dir came without BANISHED_USER_DIR"
G="$(cygpath -u "$BANISHED_DIR")"
U="$(cygpath -u "$BANISHED_USER_DIR")"

# ---------------------------------------------------------------- settings.cfg
# The keys the Android first run seeds (src/platform/android/app_android.cpp kPhoneSettings), the
# window at the device's pixel size, and the UI scale. VideoRenderScale is written for parity but
# Windows ignores it (Platform::Window::RenderScale is 1.0 there; docs/reference/device_sim.md).
mkdir -p "$U"
CFG="$U/settings.cfg"
CFG_SAVED="$(mktemp)"
if [ -f "$CFG" ]; then
    cp -f "$CFG" "$CFG_SAVED"
else
    : > "$CFG_SAVED"
    : > "$CFG"
fi
set_key() {
    local key="$1"
    local value="$2"
    if grep -q "^$key=" "$CFG"; then
        sed -i "s|^$key=.*|$key=$value|" "$CFG"
    else
        printf '%s=%s\n' "$key" "$value" >> "$CFG"
    fi
}
set_key VideoFullscreen false
set_key VideoWindowFullscreen 0
set_key VideoWindowWidth "$WIN_W"
set_key VideoWindowHeight "$WIN_H"
set_key VideoWindowX 0
set_key VideoWindowY 0
set_key GameUIScale "$UI"
set_key GameEdgeScroll false
set_key VideoRenderScale 50
set_key VideoMultisample 0
set_key VideoShadowScalar 2
set_key ext.ambient_occlusion 0
set_key ext.better_trees 0
set_key ext.foliage_aa 0
set_key ext.billboard_shadow 0
set_key ext.max_shadow_res 0
set_key ext.phone_menu 1
set_key ext.fps_cap 1
set_key ext.fps_cap_rate 60
set_key ext.touch_mode 1
set_key ext.touch_mode_toolbar_scale 200
set_key ext.touch_pointer 1
if [ "$VIEW" = options ]; then
    set_key GameLastOption 4   # the Options dialog opens on its fifth tab, Enhanced (optionsdialog.cpp)
fi
for kv in ${EXTRA_KEYS[@]+"${EXTRA_KEYS[@]}"}; do
    set_key "${kv%%=*}" "${kv#*=}"
    echo "[device_sim] --set ${kv%%=*}=${kv#*=}"
done
echo "[device_sim] $PROFILE: window ${WIN_W}x${WIN_H}, GameUIScale=$UI, phone_menu / touch_mode / touch_pointer on ($CFG)"

# ---------------------------------------------------------------- deploy and run
BANISHED_DIR="$BANISHED_DIR" tools/build.sh deploy debug --jobs "$JOBS" || die "deploy failed"
rm -f "$G"/standalone_{debug,error,crash,hang}.log "$G/standalone_boot_state.txt"
SHOTS="$U/Screenshots"
mkdir -p "$SHOTS"
BEFORE="$(ls -1 "$SHOTS" 2>/dev/null | sort)"

HARNESS=()
if [ "$VIEW" = panels ]; then
    HARNESS=(BANISHED_TOOL_HARNESS=utility
             "BANISHED_UTILITY_TEMPLATE=$TEMPLATES"
             "BANISHED_PLACEMENT_TEMPLATE=$BUILDING"
             "BANISHED_FINGER_TAP=$TAP")
fi
# --view options: the pause menu opens at QUIT_FRAME and its Options button (MainMenuDialog id 4) is
# clicked 60 frames later; the shot is taken 120 frames after that and the run is ended once it is
# written (the Options dialog has no harness route back to Quit Game).
PAUSE_BUTTON=()
if [ "$VIEW" = options ]; then
    PAUSE_BUTTON=(BANISHED_PAUSEMENU_BUTTON=4)
    PAUSE_DELAY=60
    SHOT_FRAME=$((QUIT_FRAME + PAUSE_DELAY + 120))
else
    PAUSE_DELAY=120
fi
echo "[device_sim] launching (view $VIEW; shot at frame $SHOT_FRAME, quit at $QUIT_FRAME)"
START=$(date +%s)
# BANISHED_AUTOSTART=<frames>: the menu settles MENU_FRAMES before New Game, the time the window
# check below needs to put the client size back before the game's dialogs are laid out.
MENU_FRAMES=300
(
    cd "$G" || exit 2
    exec env BANISHED_AUTOSTART="$MENU_FRAMES" BANISHED_SEED=12345 BANISHED_WINDOW_ANY_SIZE=1 \
        BANISHED_SCREENSHOT_AT="$SHOT_FRAME,ui" \
        BANISHED_PAUSEMENU="$QUIT_FRAME" BANISHED_PAUSEMENU_QUIT="$PAUSE_DELAY" \
        ${PAUSE_BUTTON[@]+"${PAUSE_BUTTON[@]}"} ${HARNESS[@]+"${HARNESS[@]}"} ./OpenBA.exe
) &
PID=$!
"$HERE/boot_state.sh" --wait 300
BOOT_RC=$?
echo "[device_sim] boot_state rc=$BOOT_RC"
# A window manager may resize the window when it is shown (tools/device_sim_window.ps1): the client
# area is put back to the device's size while the menu settles.
SIZE_OK=no
for _ in 1 2 3 4 5; do
    if powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/device_sim_window.ps1")" \
        -Width "$WIN_W" -Height "$WIN_H"; then
        SIZE_OK=yes
        break
    fi
    sleep 1
done
[ "$SIZE_OK" = yes ] || echo "[device_sim] WARNING: the client area is not ${WIN_W}x${WIN_H}; the shot is at another size"
# The run quits itself through the pause menu; the deadline catches one that does not.
while kill -0 "$PID" 2>/dev/null; do
    [ $(( $(date +%s) - START )) -ge "$DEADLINE_SECS" ] && break
    if [ "$VIEW" = options ] && ls -1 "$SHOTS" 2>/dev/null | sort | comm -13 <(printf '%s\n' "$BEFORE") - | grep -qi '\.bmp$'; then
        sleep 3   # the .bmp is written in one go by TakeScreenshot; leave it time to close
        break
    fi
    sleep 2
done
KILLED=no
ENDED=no
if [ "$VIEW" = options ] && kill -0 "$PID" 2>/dev/null; then
    ENDED=yes   # by design: the shot is in, nothing quits the Options dialog
    # This run's process only (the lane's other users may have their own OpenBA.exe running).
    WINPID="$(cat "/proc/$PID/winpid" 2> /dev/null)"
    [ -n "$WINPID" ] && taskkill //F //PID "$WINPID" > /dev/null 2>&1
    bash "$HERE/kill_game.sh" "$G"    # winpid can be the launch wrapper's: end the exe of this game dir
    kill -9 "$PID" 2> /dev/null
fi
if [ "$ENDED" = no ] && kill -0 "$PID" 2>/dev/null; then
    KILLED=yes
    bash "$HERE/kill_game.sh" "$G"
    kill -9 "$PID" 2> /dev/null
fi
wait "$PID" 2> /dev/null
RC=$?
restore_cfg

# ---------------------------------------------------------------- the screenshot
NEW="$(ls -1 "$SHOTS" 2>/dev/null | sort | comm -13 <(printf '%s\n' "$BEFORE") - | grep -i '\.bmp$' | tail -1)"
FAIL=0
if [ -s "$G/standalone_crash.log" ]; then
    echo "[device_sim] CRASH -- read $BANISHED_DIR/standalone_crash.log"
    head -20 "$G/standalone_crash.log"
    FAIL=1
fi
[ -f "$G/standalone_hang.log" ] && { echo "[device_sim] HANG -- read $BANISHED_DIR/standalone_hang.log"; FAIL=1; }
[ "$ENDED" = yes ] && echo "[device_sim] note: --view options ended the run after the shot"
[ "$KILLED" = yes ] && echo "[device_sim] note: killed at the ${DEADLINE_SECS}s deadline (no clean quit)"
if [ -z "$NEW" ]; then
    echo "[device_sim] no new screenshot in $SHOTS ([Screenshot] lines in $BANISHED_DIR/standalone_error.log)"
    FAIL=1
else
    mkdir -p "$(dirname "$SHOT")"
    case "$SHOT" in
        *.bmp | *.BMP) cp -f "$SHOTS/$NEW" "$SHOT" ;;
        *)
            powershell -NoProfile -ExecutionPolicy Bypass -Command \
                "Add-Type -AssemblyName System.Drawing; \$i = [System.Drawing.Image]::FromFile('$(cygpath -w "$SHOTS/$NEW")'); \$i.Save('$(cygpath -w "$SHOT")', [System.Drawing.Imaging.ImageFormat]::Png); \$i.Dispose()" ||
                { echo "[device_sim] PNG conversion failed; the .bmp is $SHOTS/$NEW"; FAIL=1; }
            ;;
    esac
    rm -f "$SHOTS/$NEW"
    [ "$FAIL" = 0 ] && echo "[device_sim] screenshot: $SHOT"
fi
echo "[device_sim] $PROFILE: exit $RC killed=$KILLED ($(( $(date +%s) - START ))s); logs in $BANISHED_DIR"
exit "$FAIL"
