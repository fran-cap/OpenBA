#!/usr/bin/env bash
# tools/boot_state.sh -- one-shot boot-state probe for the standalone exe.
#
# WHY: a modal Win32 dialog (usually the launcher, armed when the previous run did not
# exit cleanly) leaves the process ALIVE and pumping messages while standalone_error.log
# stops mid-boot. To a log tail that is indistinguishable from a slow load, so an agent
# polling for `[HuntScan]` sits there until its timeout expires. Call this instead of
# assuming a quiet log means "still working".
#
# AND THE OTHER HALF: a process whose MAIN THREAD is wedged (teardown after the
# pause-menu Quit, an unanswered in-game modal) used to read as RUNNING, because the
# watchdog only proved its own thread was alive. The state line now carries the main
# loop's FRAME COUNTER and how long it has been stuck (`frame=` / `stale=`), and the
# watchdog publishes STATE=HUNG once that exceeds the phase budget. On an unattended run
# the exe then writes standalone_hang.log (all-thread stacks) and terminates itself with
# EXIT CODE 3, so a hang costs ~30s instead of the caller's whole timeout.
#
# USAGE
#   tools/boot_state.sh              # print one status line
#   tools/boot_state.sh --run-dir DIR # poll a launcher --run-dir DIR test run
#   tools/boot_state.sh --wait 300   # block until RUNNING, or fail fast if blocked/hung
#   tools/boot_state.sh --android [--wait N]  # the app on the attached Android device, over adb
#                                    # (docs/android/README.md; tools/android.sh boot-state)
#
# EXIT CODES (so a script can branch without parsing)
#   0  RUNNING         normal operation
#   1  NOT_RUNNING     no process, and no state file
#   2  BLOCKED_DIALOG  modal dialog open -- boot will NEVER proceed unattended
#   3  BOOTING         started, Engine::Startup has not returned yet
#   4  STALE           the state file does not describe a live, publishing process.
#                     TWO ways to earn it:
#                       (a) its pid is GONE          -- a previous run's leftovers
#                       (b) its pid is ALIVE but the FILE HAS STOPPED ADVANCING
#                           -- the watchdog thread is dead while the process lingers
#   5  HUNG            the watchdog IS publishing, and it is reporting that the main
#                     loop has not completed a frame for longer than this phase's
#                     budget. Read standalone_hang.log -- it has every thread's stack,
#                     with the main thread marked. An unattended run kills itself
#                     (exit 4) moments after publishing this, so seeing HUNG at all
#                     usually means an ATTENDED run, where a human may simply be
#                     sitting in a dialog.
#   6  NO_GAME_DATA    the game directory's WinData/ is missing, holds no *.pkg, or has
#                     no data0.pkg (src/app/game_data_check.h, issue #72). The exe showed
#                     one box naming the folder (none on an unattended run) and exits with
#                     code 6. detail= is "<verdict> <folder>". Never clears by waiting.
#
# (b) IS THE ONE THIS SCRIPT USED TO MISS, AND IT COST A 24-MINUTE OUTAGE.
# The old script checked pid liveness and NEVER file freshness, so a crashed run
# that had not exited -- still holding the exe and both logs -- read as RUNNING
# and a lane reported BOOTSTATE_RC=0 for a process that was already dead.
# (WAVE20_GUIDE.md 0b. Note the corrected attribution there: this tool was a
# CONTRIBUTING cause, not the proximate one. A correct answer here would have
# shortened that outage; it would NOT have released the lock or reaped the
# orphan, which is what actually prolonged it.)
#
# The state file is rewritten on a fixed interval by BootState::Start
# (src/diag/win/boot_state_watchdog.cpp), so its MTIME is a heartbeat. The
# staleness threshold is DERIVED from that publisher's own Sleep() below rather
# than picked, and is printed beside every verdict -- a hard-coded number here
# would go stale the moment the watchdog's interval changed, with nothing to
# notice (reference_a_check_is_unaimable_only_without_a_free_parameter.md).

set -u

GAME_DIR="${BANISHED_DIR:-C:/GOG Games/Banished}"
if [ "$(uname -s)" = Darwin ]; then
    STATE_FILE="${HOME}/Library/Application Support/OpenBA/Run/standalone_boot_state.txt"
else
    STATE_FILE="$GAME_DIR/standalone_boot_state.txt"
fi

# ---------------------------------------------------------------------------
# DERIVE the publish interval from the publisher, do not restate it.
#
# BootState::WatchdogThread ends its loop with `Sleep(<ms>)`. Read that number
# out of the source so this check tracks the watchdog automatically. If the
# source is not next to us (this script copied elsewhere), fall back and SAY SO
# -- an undeclared fallback is how a derived number quietly becomes a guess.
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WATCHDOG_SRC="$SCRIPT_DIR/../src/diag/win/boot_state_watchdog.cpp"

PUBLISH_MS=""
INTERVAL_SRC=""
if [ -f "$WATCHDOG_SRC" ]; then
    PUBLISH_MS="$(sed -n 's/^ *Sleep(\([0-9][0-9]*\));.*/\1/p' "$WATCHDOG_SRC" | head -1)"
fi
if [ -n "$PUBLISH_MS" ]; then
    INTERVAL_SRC="derived from $(basename "$WATCHDOG_SRC") Sleep(${PUBLISH_MS})"
else
    PUBLISH_MS=1000
    INTERVAL_SRC="FALLBACK 1000ms -- could NOT read Sleep() from $WATCHDOG_SRC"
fi
PUBLISH_SECS=$(( (PUBLISH_MS + 999) / 1000 ))
[ "$PUBLISH_SECS" -lt 1 ] && PUBLISH_SECS=1

# ---------------------------------------------------------------------------
# DERIVE the hang budgets from the same publisher, for the same reason. The
# watchdog declares them as
#     const unsigned kHungRunningSec  = <n>;
#     const unsigned kHungQuittingSec = <n>;
# and they are printed beside every verdict so a HUNG (or a green) is quotable
# with the number that produced it.
# ---------------------------------------------------------------------------
HUNG_RUN_SECS=""
HUNG_QUIT_SECS=""
if [ -f "$WATCHDOG_SRC" ]; then
    HUNG_RUN_SECS="$(sed -n 's/^ *const unsigned kHungRunningSec *= *\([0-9][0-9]*\);.*/\1/p' "$WATCHDOG_SRC" | head -1)"
    HUNG_QUIT_SECS="$(sed -n 's/^ *const unsigned kHungQuittingSec *= *\([0-9][0-9]*\);.*/\1/p' "$WATCHDOG_SRC" | head -1)"
fi
if [ -n "$HUNG_RUN_SECS" ] && [ -n "$HUNG_QUIT_SECS" ]; then
    HUNG_SRC="derived: running ${HUNG_RUN_SECS}s / quitting ${HUNG_QUIT_SECS}s"
else
    HUNG_RUN_SECS="?"; HUNG_QUIT_SECS="?"
    HUNG_SRC="UNKNOWN -- could NOT read kHung*Sec from $WATCHDOG_SRC"
fi

# How many missed heartbeats before we call it dead. A publish is Sleep(interval)
# PLUS an EnumWindows sweep and a file rewrite, and this machine runs several
# lanes and a game at once, so a couple of late ticks are normal and must not
# read as a crash. Ten consecutive misses is not lateness.
STALE_FACTOR="${BANISHED_BOOTSTATE_STALE_FACTOR:-10}"
STALE_SECS=$(( PUBLISH_SECS * STALE_FACTOR ))

# --android: the state file is the app's, in its data folder on the attached device
# (src/diag/android/boot_state_watchdog_android.cpp writes it there); every probe goes over adb.
ANDROID=0
ANDROID_PKG=""
ADB_BIN=""
adb_sh() {
    MSYS_NO_PATHCONV=1 "$ADB_BIN" shell "$@" 2>/dev/null | tr -d '\r'
}

state_exists() {
    if [ "$ANDROID" = 1 ]; then
        [ "$(adb_sh "[ -f '$STATE_FILE' ] && echo yes")" = yes ]
    else
        [ -f "$STATE_FILE" ]
    fi
}

state_read() {
    if [ "$ANDROID" = 1 ]; then
        adb_sh cat "$STATE_FILE"
    else
        cat "$STATE_FILE" 2>/dev/null
    fi
}

# Age of the state file in seconds, or "" if it cannot be determined.
file_age() {
    local mtime now
    if [ "$ANDROID" = 1 ]; then
        mtime="$(adb_sh stat -c %Y "$STATE_FILE")"
        now="$(adb_sh date +%s)"
        [ -n "$mtime" ] && [ -n "$now" ] || return 1
        if [ "$mtime" -gt "$now" ]; then echo 0; else echo $(( now - mtime )); fi
        return 0
    fi
    if [ "$(uname -s)" = Darwin ]; then
        mtime="$(stat -f %m "$STATE_FILE" 2>/dev/null)" || return 1
    else
        mtime="$(stat -c %Y "$STATE_FILE" 2>/dev/null)" || return 1
    fi
    [ -n "$mtime" ] || return 1
    now="$(date +%s)"
    # Clock skew (or a file stamped in the future) must never manufacture a
    # STALE. Clamp at 0 and let it read fresh.
    if [ "$mtime" -gt "$now" ]; then echo 0; else echo $(( now - mtime )); fi
}

WAIT_SECS=0
if [ -n "${BANISHED_STATE_DIR:-}" ]; then
    STATE_FILE="$BANISHED_STATE_DIR/standalone_boot_state.txt"
fi
while [ $# -gt 0 ]; do
    case "$1" in
        --wait)
            WAIT_SECS="${2:-300}"
            shift
            [ $# -eq 0 ] || shift
            ;;
        --android)
            ANDROID=1
            shift
            ;;
        --run-dir)
            if [ $# -lt 2 ] || [ -z "$2" ]; then
                echo "--run-dir requires a directory" >&2
                exit 2
            fi
            STATE_FILE="$2/support/Run/standalone_boot_state.txt"
            shift 2
            ;;
        *)
            echo "unknown argument $1" >&2
            exit 2
            ;;
    esac
done

if [ "$ANDROID" = 1 ]; then
    ANDROID_PKG="$(sed -n 's/^openba.applicationId=//p' "$SCRIPT_DIR/../android/gradle.properties" | tr -d '\r' | head -1)"
    ADB_BIN="${ADB:-}"
    if [ -z "$ADB_BIN" ]; then
        for cand in "${ANDROID_HOME:-/nonexistent}/platform-tools/adb.exe" "${ANDROID_HOME:-/nonexistent}/platform-tools/adb" \
                    "C:/code/platform-tools/adb.exe"; do
            if [ -x "$cand" ]; then ADB_BIN="$cand"; break; fi
        done
        ADB_BIN="${ADB_BIN:-adb}"
    fi
    STATE_FILE="/sdcard/Android/data/$ANDROID_PKG/files/standalone_boot_state.txt"
fi

# Is an OpenBA.exe alive at all? tasklist is authoritative here; the state file is not,
# because a killed process leaves its last line behind. On Android: the package's process.
proc_alive() {
    if [ "$ANDROID" = 1 ]; then
        [ -n "$(adb_sh pidof "$ANDROID_PKG")" ]
        return
    fi
    if [ "$(uname -s)" = Darwin ]; then
        pgrep -x 'OpenBA(-engine)?' >/dev/null
    else
        tasklist 2>/dev/null | grep -qi 'OpenBA\.exe'
    fi
}

state_pid_alive() {
    if [ "$ANDROID" = 1 ]; then
        adb_sh pidof "$ANDROID_PKG" | tr ' ' '\n' | grep -qx "$1"
        return
    fi
    if [ "$(uname -s)" = Darwin ]; then
        kill -0 "$1" 2>/dev/null
    else
        tasklist 2>/dev/null | grep -q "[^0-9]$1[^0-9]"
    fi
}

probe_once() {
    if ! state_exists; then
        if proc_alive; then
            # Running but nothing published yet: either a pre-watchdog build or the
            # first second of boot. Report BOOTING rather than inventing a failure.
            echo "BOOTING  (no state file yet -- process alive, watchdog not up)"
            return 3
        fi
        echo "NOT_RUNNING  (no process, no state file at $STATE_FILE)"
        return 1
    fi

    local line state pid detail age fresh frame stale beat hb
    line="$(state_read)"
    state="$(printf '%s' "$line" | sed -n 's/.*STATE=\([A-Z_]*\).*/\1/p')"
    pid="$(printf '%s' "$line" | sed -n 's/.*pid=\([0-9]*\).*/\1/p')"
    detail="$(printf '%s' "$line" | sed -n 's/.*detail=\(.*\)$/\1/p')"
    # frame=/stale= are the MAIN-LOOP heartbeat, not the file's mtime: the
    # watchdog thread can be perfectly healthy while the game is frozen, which
    # is the whole point of them. Absent on a pre-heartbeat build -> "?".
    frame="$(printf '%s' "$line" | sed -n 's/.*[ =]frame=\([0-9]*\).*/\1/p')"
    stale="$(printf '%s' "$line" | sed -n 's/.* stale=\([0-9]*\).*/\1/p')"

    # A file whose pid is gone describes a previous run. Never report its state as live.
    # EXITED is the one state written by a process on its way out (Stop()): its pid is
    # gone by design, so it goes on to the NOT_RUNNING arm below.
    # NO_GAME_DATA is the other one: written once, before the exe exits on purpose.
    if [ -n "$pid" ] && [ "$state" != "EXITED" ] && [ "$state" != "NO_GAME_DATA" ] && ! state_pid_alive "$pid"; then
        echo "STALE  (state file says $state from pid $pid, which is no longer running)"
        return 4
    fi

    # ---- FRESHNESS -- the check this script was missing -------------------
    # APPLICABLE ONLY to the three states a LIVE watchdog republishes every
    # tick. EXITED is written once by Stop() and is SUPPOSED to sit frozen, so
    # ageing it would turn every clean shutdown into a false STALE.
    #
    # Every platform's Platform::App::Shutdown calls `BootState::Stop()` before
    # SDL_Quit (Windows and Linux since P8, 2026-10-25; Darwin and Android before),
    # so a clean exit writes STATE=EXITED. A crash, a kill or a hang leaves the
    # last live tick behind with a dead pid: the pid check above catches that, and
    # freshness catches it even if the pid were recycled.
    case "$state" in
        BOOTING|RUNNING|QUITTING|HUNG|BLOCKED_DIALOG)
            age="$(file_age)"
            if [ -n "$age" ] && [ "$age" -gt "$STALE_SECS" ]; then
                echo "STALE  (file says $state from pid $pid, and that pid is still alive,"
                echo "        but the file has not advanced for ${age}s > ${STALE_SECS}s"
                echo "        [freshness: publish interval ${PUBLISH_SECS}s x factor ${STALE_FACTOR}; ${INTERVAL_SRC}])"
                echo "        The watchdog thread is dead while the process lingers -- typically a"
                echo "        crashed run that never exited. It is STILL HOLDING the exe and both"
                echo "        logs. Read standalone_crash.log, then reap the orphan; do not wait."
                return 4
            fi
            ;;
    esac

    # Print the freshness that was actually applied beside the verdict, so a
    # green is quotable with the thing that made it green.
    fresh="age=${age:-?}s/${STALE_SECS}s"
    # The heartbeat, printed on EVERY status line: `age` says the watchdog is
    # alive, `frame`/`stale` say the GAME is. A green with a climbing stale is
    # the interesting case and must not need a second command to see.
    beat="frame=${frame:-?} stale=${stale:-?}s/${HUNG_RUN_SECS}s"

    case "$state" in
        RUNNING)        echo "RUNNING  [$fresh $beat] ($line)"; return 0 ;;
        QUITTING)       echo "QUITTING  [$fresh frame=${frame:-?} stale=${stale:-?}s/${HUNG_QUIT_SECS}s] ($line)"
                        echo "          MainLoop::Run returned; teardown in progress."
                        return 0 ;;
        HUNG)           # detail= carries the phase it froze in, which is what picks
                        # the budget: QUITTING gets the teardown one.
                        hb="$HUNG_RUN_SECS"
                        [ "$detail" = "QUITTING" ] && hb="$HUNG_QUIT_SECS"
                        echo "HUNG  the main loop has not completed a frame for ${stale:-?}s."
                        echo "      phase: $detail   [frame=${frame:-?} stale=${stale:-?}s/${hb}s; hang budgets $HUNG_SRC]"
                        echo "      Read standalone_hang.log -- all-thread stacks, main thread marked."
                        echo "      An UNATTENDED run self-terminates with exit code 4 right after"
                        echo "      publishing this; seeing it persist means an attended run."
                        return 5 ;;
        BLOCKED_DIALOG) echo "BLOCKED_DIALOG  a modal dialog is open and boot cannot proceed."
                        echo "                title: $detail"
                        echo "                Click it, or relaunch -- an unattended run will never clear it."
                        return 2 ;;
        BOOTING)        echo "BOOTING  [$fresh $beat] ($line)"; return 3 ;;
        NO_GAME_DATA)   echo "NO_GAME_DATA  the game data is not where the exe looked; it exits with code 6."
                        echo "              verdict + folder: $detail"
                        echo "              Copy the retail WinData/*.pkg there (src/app/game_data_check.h)."
                        return 6 ;;
        EXITED)         echo "NOT_RUNNING  (clean shutdown: $line)"; return 1 ;;
        *)              echo "NOT_RUNNING  (unrecognised state file: $line)"; return 1 ;;
    esac
}

if [ "$WAIT_SECS" -le 0 ]; then
    probe_once
    exit $?
fi

# --wait: poll until RUNNING, but bail immediately on BLOCKED_DIALOG, STALE or HUNG.
# Waiting out a modal dialog -- or a frozen main loop -- is exactly the wasted timeout
# this script exists to prevent. HUNG never clears on its own either: an unattended run
# is already killing itself, and an attended one needs a human.
deadline=$(( $(date +%s) + WAIT_SECS ))
while :; do
    out="$(probe_once)"; rc=$?
    case $rc in
        0) echo "$out"; exit 0 ;;
        2) echo "$out"; exit 2 ;;
        4) echo "$out"; exit 4 ;;
        5) echo "$out"; exit 5 ;;
        6) echo "$out"; exit 6 ;;
    esac
    if [ "$(date +%s)" -ge "$deadline" ]; then
        echo "TIMEOUT after ${WAIT_SECS}s -- last: $out"
        exit 3
    fi
    sleep 3
done
