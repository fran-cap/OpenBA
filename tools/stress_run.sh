#!/usr/bin/env bash
# tools/stress_run.sh -- run the BANISHED_STRESS town to completion and collect the numbers.
#
# WHAT IT DOES
#   1. (unless --no-build) builds the perf exe (tools/build.sh build perf) and deploys it
#   2. clears the previous run's logs/CSV, launches OpenBA.exe with BANISHED_STRESS=1
#      plus the knobs below, supervises boot with tools/boot_state.sh (bails instantly on
#      a blocked modal dialog instead of burning the timeout), then waits for the process
#      to exit (the harness quits itself through the in-game pause menu after --frames)
#   3. copies standalone_stress.csv, standalone_stress_compprof.txt and the error log into
#      perf/results/<timestamp>_<label>/ and prints tools/stress_report.py's summary,
#      diffed against --baseline when given (default: perf/baselines/current.csv if present).
#      The report's PATHFINDING section (PFBENCH: the pf_* / ph_pathdistance / ph_fillzones
#      columns, docs/extensions/handins/PFBENCH.md) prints "n/a" against a baseline CSV
#      that predates those columns
#
# WHAT ELSE THE RESULTS DIRECTORY NOW CONTAINS, AND WHY (Wave 26)
#
#   machine_load.csv / machine_load.txt
#     For much of Wave 25, up to SEVEN lanes were compiling and running Python on
#     this machine at once. A perf number taken under unknown contention is not
#     comparable to one taken under different unknown contention -- and nothing
#     recorded the contention, so the comparison could neither be made nor
#     refused. It was simply unknown, and unknown reads like fine.
#
#     So the poll loop now samples CPU load, the process table, and free memory
#     for the life of the run, and writes a per-run summary. THIS DOES NOT MAKE
#     TWO RUNS COMPARABLE. It makes them ARGUABLE: a later reader can see that
#     run A had four cc1plus processes alive throughout and run B had none, and
#     decide what to do about it. That is the whole claim.
#
#   exe_identity.txt
#     Wave 25 lane ALLOC md5-verified the deployed exe against its own build
#     before every launch. That per-run check is the only reason "was my
#     measurement contaminated?" was answerable rather than arguable, and it was
#     per-lane discipline. Recorded here automatically, before and after the run.
#
# USAGE
#   tools/stress_run.sh [--label NAME] [--speed N] [--citizens N] [--sets N] [--frames N]
#                       [--interval N] [--seed N] [--map small|medium|large]
#                       [--baseline CSV|none] [--no-build] [--debug] [--timeout SECS]
#                       [--lock LANE] [--no-load] [--spread]
#   --spread      the PFBENCH spread layout: workplaces and storage a quarter map west of
#                 the centre, houses a quarter map east (BANISHED_STRESS_SETS=-N, N = --sets
#                 or 3), so path requests dominate the run
#   --lock LANE   deploy and run under tools/game_dir_lock.sh, held across the whole
#                 run (OPT-IN, not the default -- see WAVE26_LANE_LOCK.md section 6)
#   --no-load     skip machine-load sampling
#   STRESS_CPU_LIMIT=<mask>  the leg's sys.cpu_limit (0x55 / 0xf / 0 on this box; CPULIMIT)
#   Any BANISHED_* variable already in the environment is passed through untouched and
#   wins over the harness defaults (StressHarness_Init only fills in UNSET variables).
#
# EXIT CODES
#   0 run completed and report printed      1 bad usage / build failed
#   2 crash (standalone_crash.log non-empty) 3 boot blocked or timed out
#   4 run did not finish within --timeout    5 no CSV produced (harness never armed)
#   6 run completed but the process crashed during teardown (numbers are valid)

# The macOS stress harness is not part of this repository.
if [ "$(uname -s)" = Darwin ]; then
    echo "macOS stress harness is not part of this repository; run the stress town directly with BANISHED_STRESS=1 (see perf/README.md)" >&2
    exit 2
fi

set -u
cd "$(dirname "$0")/.." || exit 1
REPO="$(pwd)"
GAME_DIR="${BANISHED_DIR:-C:/GOG Games/Banished}"
BUILDER="${BUILDER:-tools/build.sh}"   # the CMake build (2026-10-11; was mingw32-make standalone-perf)

ORIG_ARGS=("$@")
LABEL="run"; SPEED=""; CITIZENS=""; SETS=""; FRAMES=""; INTERVAL=""; SEED=""; MAP=""
BASELINE=""; BUILD=1; DEBUG=0; TIMEOUT=""; LOCK_LANE=""; LOAD=1; SPREAD=0
while [ $# -gt 0 ]; do
    case "$1" in
        --label)    LABEL="$2"; shift 2 ;;
        --speed)    SPEED="$2"; shift 2 ;;
        --citizens) CITIZENS="$2"; shift 2 ;;
        --sets)     SETS="$2"; shift 2 ;;
        --frames)   FRAMES="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --seed)     SEED="$2"; shift 2 ;;
        --map)      MAP="$2"; shift 2 ;;
        --baseline) BASELINE="$2"; shift 2 ;;
        --timeout)  TIMEOUT="$2"; shift 2 ;;
        --lock)     LOCK_LANE="$2"; shift 2 ;;
        --no-load)  LOAD=0; shift ;;
        --spread)   SPREAD=1; shift ;;
        --no-build) BUILD=0; shift ;;
        --debug)    DEBUG=1; BUILD=0; shift ;;
        -h|--help)  sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

# ---- MACHINE LOAD ------------------------------------------------------------
#
# Deliberately CHEAP, because the sampler must not be the contention. Measured
# on this machine: `tasklist` 0.26s, `wmic OS get FreePhysicalMemory` 0.09s,
# `wmic cpu get loadpercentage` 1.09s. The poll loop runs every 5s, so the
# tasklist/memory pair is taken every poll and the expensive CPU reading every
# LOAD_CPU_EVERY-th poll (CPU column is empty on the others rather than
# carried forward -- a repeated value would read as a measurement).
LOAD_CPU_EVERY=6

cpu_load() { wmic cpu get loadpercentage 2>/dev/null | tr -d '\r' | sed -n '2p' | tr -d ' '; }
free_mb()  { local k; k=$(wmic OS get FreePhysicalMemory 2>/dev/null | tr -d '\r' | sed -n '2p' | tr -d ' '); case "$k" in ''|*[!0-9]*) printf '' ;; *) printf '%s' $(( k / 1024 )) ;; esac; }

# One tasklist call, every count derived from it. Counting separately would be
# several calls AND several different instants.
LOAD_HDR='t_s,cpu_pct,free_mb,procs,make,compiler,python,reimpl,other_game'
machine_sample() {   # $1 = seconds since start, $2 = 1 to take the CPU reading
    local t="$1" want_cpu="$2" tl cpu procs mk cc py re og
    tl=$(tasklist 2>/dev/null)
    procs=$(printf '%s\n' "$tl" | grep -c -iE '\.exe')
    mk=$(printf '%s\n'   "$tl" | grep -c -iE '^(mingw32-)?make\.exe')
    cc=$(printf '%s\n'   "$tl" | grep -c -iE '^(cc1plus|cc1|g\+\+|gcc|ld|as|collect2)\.exe')
    py=$(printf '%s\n'   "$tl" | grep -c -iE '^python')
    re=$(printf '%s\n'   "$tl" | grep -c -i  'OpenBA\.exe')
    og=$(printf '%s\n'   "$tl" | grep -c -iE '^(Application-x64|Tools-x64)')
    cpu=""
    [ "$want_cpu" = 1 ] && cpu=$(cpu_load)
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$t" "$cpu" "$(free_mb)" "$procs" "$mk" "$cc" "$py" "$re" "$og"
}

# ---- OPT-IN LOCK -------------------------------------------------------------
# This script DEPLOYS and LAUNCHES -- exactly what tools/game_dir_lock.sh
# protects -- and until Wave 26 it neither acquired nor asserted. Making it
# mandatory now would turn a harness six lanes depend on into one that can
# refuse to run, so it is opt-in and the default is unchanged. ADDRESSEE: the
# Wave 26/27 integrator decides whether this becomes the default.
#
# Re-exec rather than acquire-here, so the acquire's exit status is checked
# inside game_dir_lock.sh's own `run` where no caller's pipe can discard it.
if [ -n "$LOCK_LANE" ] && [ "${STRESS_RUN_UNDER_LOCK:-0}" != "1" ]; then
    export STRESS_RUN_UNDER_LOCK=1
    exec "$REPO/tools/game_dir_lock.sh" run "$LOCK_LANE" 1800 -- \
        "$REPO/tools/stress_run.sh" ${ORIG_ARGS[@]+"${ORIG_ARGS[@]}"}
fi

# Exercise the sampler itself, in THIS file, without running anything. A copy of
# these functions in a test script would be a second implementation agreeing
# with itself -- the shape where two checks share one hidden premise and read as
# independent. This runs the real code.
if [ "${STRESS_LOAD_PROBE:-0}" = "1" ]; then
    echo "$LOAD_HDR"
    machine_sample -1 1
    machine_sample 0 0
    exit 0
fi

if [ "$BUILD" = 1 ]; then
    echo "== building perf exe"
    "$BUILDER" build perf || { echo "build failed" >&2; exit 1; }
    cp build/cmake-perf/OpenBA.exe "$GAME_DIR/OpenBA.exe" || exit 1
fi

# Frames the harness will run before quitting: default from the harness (6000) unless
# overridden. Needed here only to size the wait timeout.
FRAMES_EFF="${FRAMES:-${BANISHED_STRESS_FRAMES:-6000}}"
if [ -z "$TIMEOUT" ]; then
    # 60 sim frames per wall second when the sim keeps up; allow 3x for a slow build
    # plus 5 minutes for world build + boot.
    TIMEOUT=$(( FRAMES_EFF / 60 * 3 + 300 ))
    [ "$FRAMES_EFF" -le 0 ] && TIMEOUT=0
fi

# --spread: a negative set count selects the spread layout (stress_harness.h).
if [ "$SPREAD" = 1 ]; then
    n="${SETS:-${BANISHED_STRESS_SETS:-3}}"; n="${n#-}"
    SETS="-$n"
fi

export BANISHED_STRESS=1
[ -n "$SPEED" ]    && export BANISHED_STRESS_SPEED="$SPEED"
[ -n "$CITIZENS" ] && export BANISHED_STRESS_CITIZENS="$CITIZENS"
[ -n "$SETS" ]     && export BANISHED_STRESS_SETS="$SETS"
[ -n "$FRAMES" ]   && export BANISHED_STRESS_FRAMES="$FRAMES"
[ -n "$INTERVAL" ] && export BANISHED_STRESS_INTERVAL="$INTERVAL"
[ -n "$SEED" ]     && export BANISHED_SEED="$SEED"
[ -n "$MAP" ]      && export BANISHED_MAP_SIZE="$MAP"

if tasklist 2>/dev/null | grep -qi 'OpenBA.exe'; then
    echo "OpenBA.exe is already running -- stop it first" >&2
    exit 1
fi

rm -f "$GAME_DIR/standalone_stress.csv" "$GAME_DIR/standalone_stress_compprof.txt" \
      "$GAME_DIR/standalone_crash.log" "$GAME_DIR/standalone_error.log" \
      "$GAME_DIR/standalone_debug.log" "$GAME_DIR/standalone_boot_state.txt" \
      "$GAME_DIR/standalone_stress_hitch.csv" "$GAME_DIR/standalone_samples.txt"

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="$REPO/perf/results/${STAMP}_${LABEL}"
mkdir -p "$OUT"

# ---- EXE IDENTITY, BEFORE THE LAUNCH ----------------------------------------
# ALLOC's discipline, recorded rather than remembered. `built` is empty when
# --no-build/--debug was used, which is itself the fact worth recording: the run
# is against whatever was already deployed, and nothing here can say whose.
BUILT_EXE="build/cmake-perf/OpenBA.exe"
[ "$DEBUG" = 1 ] && BUILT_EXE="build/cmake-debug/OpenBA.exe"
{
    echo "deployed_before=$(md5sum "$GAME_DIR/OpenBA.exe" 2>/dev/null | cut -c1-32)"
    echo "built=$(md5sum "$BUILT_EXE" 2>/dev/null | cut -c1-32)   ($BUILT_EXE)"
    echo "built_this_run=$BUILD"
    echo "lock=${LOCK_LANE:-<none held>}"
} > "$OUT/exe_identity.txt"
if [ "$BUILD" = 1 ]; then
    a=$(md5sum "$GAME_DIR/OpenBA.exe" 2>/dev/null | cut -c1-32)
    b=$(md5sum "$BUILT_EXE" 2>/dev/null | cut -c1-32)
    if [ -n "$a" ] && [ "$a" != "$b" ]; then
        echo "!! the deployed exe is NOT the one just built -- somebody deployed over it." >&2
        echo "   deployed=$a built=$b" >&2
        exit 1
    fi
fi

# The header file is written even with --no-load. A results directory with NO
# machine_load.csv is silently absent, and a silently absent load record reads
# exactly like a quiet machine -- which is the failure lane ATTEND reported in
# another form on the same day (a stale cutover_manifest.inc survived a whole
# wave because its count MATCHED the prediction). An always-present file whose
# reporter says UNEXAMINED makes "nothing was measured" a statement rather than
# a gap the reader has to notice.
echo "$LOAD_HDR" > "$OUT/machine_load.csv"
if [ "$LOAD" = 1 ]; then
    machine_sample -1 1 >> "$OUT/machine_load.csv"      # before the launch
else
    echo "# --no-load was passed: NOTHING WAS SAMPLED. This is not a quiet machine." >> "$OUT/machine_load.csv"
fi

# The stress town must simulate unattended: GameAutoPause (the retail option, settings.cfg under
# Documents/Banished) pauses the application whenever the window loses focus, and
# PlayerToolManager::Update then opens the pause menu (retail 0x1401026a0: IsPaused + autoPause),
# which pauses the sim for the rest of the run. 2026-10-12: two stress runs at 0.00 sim iterations
# per frame because the window came up unfocused (the user was typing elsewhere). As
# tools/linux_run.sh does: switch it off for the run and restore the file at exit.
CFG="${BANISHED_USER_DIR:-${USERPROFILE:-$HOME}/Documents/Banished}/settings.cfg"; CFG_SAVED=""   # tools/game_dirs.sh
if [ -f "$CFG" ] && { grep -q '^GameAutoPause=' "$CFG" || [ -n "${STRESS_EXT:-}" ] || [ -n "${STRESS_CPU_LIMIT:-}" ]; }; then
    CFG_SAVED="$(mktemp)"; cp -f "$CFG" "$CFG_SAVED"
    sed -i 's/^GameAutoPause=.*/GameAutoPause=false/' "$CFG"
    trap '[ -n "$CFG_SAVED" ] && cp -f "$CFG_SAVED" "$CFG" && rm -f "$CFG_SAVED"' EXIT
    echo "== GameAutoPause=false for this run (settings.cfg restored at exit)"
fi
# STRESS_EXT=<id>[,<id>...] (PATHRESEARCH section 5 "The A/B pair"): the single-extension A/B.
# Every `ext.<id>=` line in settings.cfg is set to 0 except the listed ones (set or added as 1),
# every `fix.<id>=` line is set to 0 (2026-10-12: the fixes were left as the file had them --
# all ON -- while /profile:faithful forces them OFF, so every extension A/B before this date
# compared "fixes on + extension" against "fixes off": the extension-on towns held ~3,700
# fewer entities; STRESS_FIXES=keep restores the old behaviour), and the exe runs WITHOUT
# /profile (per-entry), so exactly the listed extensions are on. The file is restored at exit
# (the trap above).
PROFILE_ARG="/profile:${STRESS_PROFILE:-faithful}"
if [ -n "${STRESS_EXT:-}" ]; then
    [ -n "$CFG_SAVED" ] || { echo "STRESS_EXT needs $CFG" >&2; exit 1; }
    sed -i 's/^\(ext\.[a-z0-9_]*\)=.*/\1=0/' "$CFG"
    # The registry's VALUED keys (src/ext/registry.cpp kValueKeys: ext.ao_quality 1..3,
    # ext.ao_opacity 0..30) are settings, not toggles: put back what the file held, so a leg
    # can carry e.g. ext.ao_quality=3 (zeroed, it clamped to 1; found by AOCORE 2026-10-14).
    # (foliage_aa_mode and max_shadow_size added 2026-10-20: zeroed, a leg ran the 4096 map and
    # mode 0 while its label claimed the user's 8192 / mode 2 -- found by the GPU-frame legs.)
    for vk in ao_quality ao_opacity foliage_aa_mode max_shadow_size; do
        v=$(grep "^ext\.$vk=" "$CFG_SAVED" | tail -1 | cut -d= -f2 | tr -d '\r')
        if [ -n "$v" ]; then sed -i "s/^ext\.$vk=.*/ext.$vk=$v/" "$CFG"; fi
    done
    # STRESS_EXT_VALUES="k=v,k=v": per-leg overrides of the valued keys (e.g. max_shadow_size=4096),
    # written when the file has no line. Also the way to pin a DEFAULT-ON row off: a class A row
    # with no line in settings.cfg defaults ON, and the zeroing above only reaches existing lines
    # (an "off" leg of a new row ran with it on, 2026-10-20) -- pass `<id>=0`; an id listed in
    # STRESS_EXT is set to 1 below, after this.
    for kv in $(echo "${STRESS_EXT_VALUES:-}" | tr ',' ' '); do
        if grep -q "^ext\.${kv%%=*}=" "$CFG"; then sed -i "s/^ext\.${kv%%=*}=.*/ext.${kv%%=*}=${kv#*=}/" "$CFG"
        else printf 'ext.%s\n' "$kv" >> "$CFG"; fi
    done
    if [ "${STRESS_FIXES:-off}" != "keep" ]; then
        sed -i 's/^\(fix\.[a-z0-9_]*\)=.*/\1=0/' "$CFG"
        # A fix with no line in the file defaults ON (fixes are default-on): write the line.
        # The ids are the registry's (src/ext/registry.cpp, Tier::Fix rows).
        for fx in icon_slide residence_goto tavern_counter start_barn_height ui_scale_layout; do
            grep -q "^fix\.$fx=" "$CFG" || printf 'fix.%s=0\n' "$fx" >> "$CFG"
        done
        # STRESS_FIXES=<id>[,<id>...]: exactly these fixes ON (the rest off), for bisecting a
        # fix's effect on the stress town.
        if [ "${STRESS_FIXES:-off}" != "off" ]; then
            for fx in $(echo "$STRESS_FIXES" | tr ',' ' '); do sed -i "s/^fix\.$fx=.*/fix.$fx=1/" "$CFG"; done
        fi
        echo "== STRESS_EXT: fix.* in settings.cfg = $(grep '^fix\.' "$CFG" | tr '\n' ' ')(matches /profile:faithful)"
    fi
    # STRESS_EXT=none: the per-entry profile with every extension off (a control run for the
    # profile mode itself; 2026-10-12: the spread town held 3,700 fewer entities under per-entry).
    [ "$STRESS_EXT" = "none" ] && STRESS_EXT=""
    for id in $(echo "$STRESS_EXT" | tr ',' ' '); do
        if grep -q "^ext\.$id=" "$CFG"; then sed -i "s/^ext\.$id=.*/ext.$id=1/" "$CFG"
        else printf 'ext.%s=1\n' "$id" >> "$CFG"; fi
    done
    PROFILE_ARG=""
    echo "== STRESS_EXT=$STRESS_EXT: ext.* in settings.cfg = $(grep '^ext\.' "$CFG" | tr '\n' ' ')(per-entry profile)"
fi

# STRESS_CPU_LIMIT=<mask> (CPULIMIT, THREADPLAN section 4): the leg's `sys.cpu_limit` in settings.cfg
# (0x55 = four physical cores on this box, 0xf = two cores with both SMT threads, 0 = unrestricted),
# applied by the exe at boot before any thread; the mask is printed in the CSV's `# cpu` line and
# the report header, so each leg proves it. The file is restored at exit (the trap above).
if [ -n "${STRESS_CPU_LIMIT:-}" ]; then
    [ -n "$CFG_SAVED" ] || { echo "STRESS_CPU_LIMIT needs $CFG" >&2; exit 1; }
    if grep -q '^sys\.cpu_limit=' "$CFG"; then sed -i "s/^sys\.cpu_limit=.*/sys.cpu_limit=$STRESS_CPU_LIMIT/" "$CFG"
    else printf 'sys.cpu_limit=%s\n' "$STRESS_CPU_LIMIT" >> "$CFG"; fi
    echo "== STRESS_CPU_LIMIT=$STRESS_CPU_LIMIT: sys.cpu_limit set for this run (settings.cfg restored at exit)"
fi

echo "== launching  label=$LABEL speed=${SPEED:-default} citizens=${CITIZENS:-default} sets=${SETS:-default} frames=$FRAMES_EFF debug=$DEBUG"
echo "   results -> $OUT"
# STRESS_PROFILE (default faithful): the exe's /profile:<name> switch (docs/extensions/DESIGN.md);
# `all` turns every fix and extension on for the everything-on comparison.
# exec: $LAUNCH_PID then lives exactly as long as this run's game, so the loop below waits on THIS run
# only (another OpenBA.exe, the user's own game in the other game dir, is never waited on or killed).
( cd "$GAME_DIR" && exec env MSYS2_ARG_CONV_EXCL="/profile:" ./OpenBA.exe $PROFILE_ARG ) &
LAUNCH_PID=$!

echo "== waiting for boot"
tools/boot_state.sh --wait 600
rc=$?
if [ $rc -ne 0 ]; then
    echo "boot did not reach RUNNING (boot_state rc=$rc)" >&2
    exit 3
fi

echo "== running (timeout ${TIMEOUT}s; Ctrl-C leaves the game up)"
start=$(date +%s)
last_row=""
poll=0
while kill -0 "$LAUNCH_PID" 2>/dev/null; do
    if [ "$LOAD" = 1 ]; then
        want_cpu=0
        [ $(( poll % LOAD_CPU_EVERY )) -eq 0 ] && want_cpu=1
        machine_sample "$(( $(date +%s) - start ))" "$want_cpu" >> "$OUT/machine_load.csv"
    fi
    poll=$(( poll + 1 ))
    if [ -s "$GAME_DIR/standalone_crash.log" ]; then
        echo "!! crash log appeared"; break
    fi
    if [ -f "$GAME_DIR/standalone_stress.csv" ]; then
        row="$(tail -n 1 "$GAME_DIR/standalone_stress.csv" | tr -d '\r')"
        if [ "$row" != "$last_row" ] && [ "${row:0:1}" != "#" ] && [ "${row:0:6}" != "wall_s" ]; then
            echo "   $row"; last_row="$row"
        fi
    fi
    if [ "$TIMEOUT" -gt 0 ] && [ $(( $(date +%s) - start )) -ge "$TIMEOUT" ]; then
        echo "!! timeout after ${TIMEOUT}s -- killing"
        bash tools/kill_game.sh "$GAME_DIR"      # this game dir's exe only, never by image name
        kill -9 "$LAUNCH_PID" 2>/dev/null
        sleep 2
        cp -f "$GAME_DIR"/standalone_stress*.* "$GAME_DIR/standalone_error.log" "$OUT/" 2>/dev/null
        exit 4
    fi
    sleep 5
done
wait $LAUNCH_PID 2>/dev/null

if [ "$LOAD" = 1 ]; then
    machine_sample "$(( $(date +%s) - start ))" 1 >> "$OUT/machine_load.csv"
fi
# The identity question, closed at the other end: if the deployed exe is not the
# one that was there when this run started, something landed on it mid-run and
# the numbers are about an unknown mixture. This is ALLOC's per-launch md5,
# taken twice so it can answer for the INTERVAL and not just the instant.
{
    echo "deployed_after=$(md5sum "$GAME_DIR/OpenBA.exe" 2>/dev/null | cut -c1-32)"
} >> "$OUT/exe_identity.txt"
IDENT_BEFORE=$(sed -n 's/^deployed_before=//p' "$OUT/exe_identity.txt" | head -n 1)
IDENT_AFTER=$(sed -n 's/^deployed_after=//p' "$OUT/exe_identity.txt" | head -n 1)
if [ -n "$IDENT_BEFORE" ] && [ "$IDENT_BEFORE" != "$IDENT_AFTER" ]; then
    echo "!! EXE CHANGED DURING THIS RUN: $IDENT_BEFORE -> $IDENT_AFTER" | tee -a "$OUT/exe_identity.txt"
    echo "!! These numbers are NOT from one binary. Discard them." | tee -a "$OUT/exe_identity.txt"
fi

cp -f "$GAME_DIR"/standalone_stress*.* "$OUT/" 2>/dev/null
cp -f "$GAME_DIR/standalone_error.log" "$OUT/" 2>/dev/null
# HITCH: a sampled run (BANISHED_SAMPLE) leaves standalone_samples.txt; hitch_report.py joins it
# with the hitch rows by the [LoadTime] clock.
[ -n "${BANISHED_SAMPLE:-}" ] && [ -s "$GAME_DIR/standalone_samples.txt" ] && cp -f "$GAME_DIR/standalone_samples.txt" "$OUT/"
[ -s "$GAME_DIR/standalone_crash.log" ] && cp -f "$GAME_DIR/standalone_crash.log" "$OUT/"

TEARDOWN_CRASH=0
if [ -s "$GAME_DIR/standalone_crash.log" ]; then
    # A crash AFTER the harness clicked Quit is a teardown bug, not a lost run: every CSV
    # row was written first. Report it loudly but still produce the numbers (exit 6).
    last_frame=$(grep -v '^#' "$OUT/standalone_stress.csv" 2>/dev/null | tail -n 1 | cut -d, -f3)
    if [ "$FRAMES_EFF" -gt 0 ] && [ -n "$last_frame" ] && [ "${last_frame%.*}" -ge "$FRAMES_EFF" ] 2>/dev/null        && grep -q 'PauseMenu\] dispatching' "$OUT/standalone_error.log" 2>/dev/null; then
        echo "== CRASHED DURING TEARDOWN (after the quit click at frame $last_frame) -- numbers are complete:"
        head -n 12 "$GAME_DIR/standalone_crash.log"
        TEARDOWN_CRASH=1
    else
        echo "== CRASHED -- standalone_crash.log:"
        head -n 40 "$GAME_DIR/standalone_crash.log"
        exit 2
    fi
fi
if [ ! -s "$OUT/standalone_stress.csv" ]; then
    echo "== no standalone_stress.csv was written: the harness never produced a row." >&2
    echo "   grep '\[Stress' standalone_error.log for why." >&2
    grep -m 20 '\[Stress' "$GAME_DIR/standalone_error.log" 2>/dev/null
    exit 5
fi

if [ -z "$BASELINE" ] && [ -f perf/baselines/current.csv ]; then BASELINE=perf/baselines/current.csv; fi
[ "$BASELINE" = "none" ] && BASELINE=""
echo "== report"
if [ -n "$BASELINE" ]; then
    python tools/stress_report.py "$OUT/standalone_stress.csv" --compprof "$OUT/standalone_stress_compprof.txt" --baseline "$BASELINE" | tee "$OUT/report.txt"
else
    python tools/stress_report.py "$OUT/standalone_stress.csv" --compprof "$OUT/standalone_stress_compprof.txt" | tee "$OUT/report.txt"
fi
# HITCH (perf/README.md "Hitches"): the per-window frame_ms_max / hitch_n columns and the hitch
# rows' attribution, appended to report.txt. Prints PASS when no period exceeded the threshold.
echo "== hitches"
python tools/hitch_report.py "$OUT" --exe build/cmake-perf/OpenBA.exe | tee -a "$OUT/report.txt"
echo "== machine load"
# Appended to report.txt, not printed beside it: a contention line in a separate
# file is a line nobody reads next to the number it qualifies. Emitted
# UNCONDITIONALLY -- with --no-load the file holds only a header and the
# reporter says UNEXAMINED, which is the point: the reader is told that nothing
# was measured instead of being told nothing.
[ "$LOAD" = 1 ] || echo "(--no-load was passed)" | tee -a "$OUT/report.txt"
python tools/machine_load_report.py "$OUT/machine_load.csv" | tee -a "$OUT/report.txt"
echo "== exe identity"
cat "$OUT/exe_identity.txt"
echo "== done: $OUT"
[ "$TEARDOWN_CRASH" = 1 ] && exit 6
exit 0
