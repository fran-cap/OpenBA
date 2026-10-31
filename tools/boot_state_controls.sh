#!/usr/bin/env bash
# tools/boot_state_controls.sh -- the controls for boot_state.sh's freshness check.
#
# WHY THIS FILE EXISTS
# The Wave 20 fix to boot_state.sh adds a STALE-on-frozen-file verdict. A verdict
# that fires is worthless without proof that (a) it fires where the old script
# said RUNNING, and (b) it does NOT simply fire always. Both halves are here, and
# each case prints its own PASS/FAIL with the exit code it actually got.
#
# SUBJECT: it runs the boot_state.sh sitting NEXT TO IT (printed below), against a
# PRIVATE fixture directory -- never the shared C:/GOG Games/Banished. No game-dir
# lock is needed and no real run is disturbed.
#
# PROVENANCE OF THE FIXTURES -- state it, do not imply a real crash:
# every state file here is SYNTHESISED. Each is byte-identical in FORMAT to what
# BootState::WriteStateFile emits (same fprintf layout), carries the pid of a REAL
# live process spawned for the test, and is aged by setting its mtime backwards
# with `touch -d`. What is real: the pid liveness, the file format, the mtime
# arithmetic. What is simulated: the crash itself. This harness therefore proves
# the DECISION RULE, not that any particular crash produces a frozen file --
# that part is inherited from BootState::WatchdogThread being the only writer.
#
# USAGE:  tools/boot_state_controls.sh
# EXIT :  0 all applicable cases passed, 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NEW="$SCRIPT_DIR/boot_state.sh"
FIX="${TMPDIR:-/tmp}/bs_ctl_$$"
OLD="$FIX/boot_state_OLD.sh"

mkdir -p "$FIX" || exit 1
echo "SUBJECT (new): $NEW"
echo "FIXTURE DIR  : $FIX   (private; the shared game dir is NOT touched)"

# The BEFORE script, straight out of git, so "it used to say RUNNING" is a
# measurement and not a recollection.
#
# IT IS FOUND BY CONTENT, NOT PINNED TO HEAD. The first version of this file
# used `git show HEAD:tools/boot_state.sh`, which was correct for exactly as
# long as the fix was uncommitted: the moment it landed, HEAD became the NEW
# script, "old" and "new" were the same file, and P1a/P2a flipped from PASS to
# FAIL. A baseline that MOVES WITH THE THING IT IS THE BASELINE FOR is not a
# baseline. So: walk this file's history and take the newest revision that does
# NOT contain the freshness check, identified by the helper the fix introduced.
BASE_SHA=""
for sha in $(git -C "$SCRIPT_DIR/.." log --format=%H -- tools/boot_state.sh); do
    if ! git -C "$SCRIPT_DIR/.." show "$sha:tools/boot_state.sh" 2>/dev/null \
         | grep -q 'file_age()'; then
        BASE_SHA="$sha"
        break
    fi
done
if [ -z "$BASE_SHA" ]; then
    echo "FATAL: no revision of tools/boot_state.sh WITHOUT file_age() -- nothing to"
    echo "       compare against. If the pre-fix history is gone, P1a/P2a cannot run."
    exit 1
fi
git -C "$SCRIPT_DIR/.." show "$BASE_SHA:tools/boot_state.sh" > "$OLD" 2>/dev/null
if [ ! -s "$OLD" ]; then
    echo "FATAL: could not extract $BASE_SHA:tools/boot_state.sh"
    exit 1
fi
echo "SUBJECT (old): ${BASE_SHA} (newest revision with no file_age) -> $OLD"

# ---------------------------------------------------------------------------
# A REAL live process, so the pid-liveness half of the tool is genuinely
# satisfied and only FRESHNESS distinguishes the cases.
# ---------------------------------------------------------------------------
powershell -NoProfile -Command \
  '(Start-Process -FilePath ping -ArgumentList "-n","900","127.0.0.1" -WindowStyle Hidden -PassThru).Id' \
  > "$FIX/pid.txt" 2>/dev/null
LIVE_PID="$(tr -dc '0-9' < "$FIX/pid.txt")"
if [ -z "$LIVE_PID" ]; then
    echo "FATAL: could not spawn a live helper process -- every case here needs one"
    exit 1
fi
if ! tasklist 2>/dev/null | grep -q "[^0-9]$LIVE_PID[^0-9]"; then
    echo "FATAL: helper pid $LIVE_PID is not visible to tasklist -- fixture invalid"
    kill -9 "$LIVE_PID" 2>/dev/null
    exit 1
fi
echo "LIVE HELPER PID: $LIVE_PID (visible to tasklist -- pid liveness is REAL)"
cleanup() { taskkill //F //PID "$LIVE_PID" >/dev/null 2>&1; rm -rf "$FIX"; }
trap cleanup EXIT

# Write a state file in BootState::WriteStateFile's exact format, then age it.
# age_secs=0 means leave it fresh.
make_state() {   # $1=STATE  $2=pid  $3=age_secs
    printf 'STATE=%s pid=%s uptimeSec=42 detail=-\n' "$1" "$2" > "$FIX/standalone_boot_state.txt"
    if [ "$3" -gt 0 ]; then
        touch -d "@$(( $(date +%s) - $3 ))" "$FIX/standalone_boot_state.txt"
    fi
}

PASS=0; FAIL=0; TOTAL=0
# Never read an exit code through a pipe -- capture, then test. Three people hit
# that on one tool in one afternoon (WAVE19).
run_tool() {   # $1=script ; echoes rc, output to $LAST_OUT
    LAST_OUT="$(BANISHED_DIR="$FIX" bash "$1" 2>&1)"
    return $?
}

check() {   # $1=label $2=script $3=expected_rc
    TOTAL=$(( TOTAL + 1 ))
    run_tool "$2"; local rc=$?
    if [ "$rc" = "$3" ]; then
        PASS=$(( PASS + 1 )); echo "  PASS  $1  (rc=$rc, expected $3)"
    else
        FAIL=$(( FAIL + 1 )); echo "  FAIL  $1  (rc=$rc, EXPECTED $3)"
    fi
    echo "$LAST_OUT" | sed 's/^/        | /'
}

echo
echo "=== POSITIVE CONTROL -- it must read non-zero, and the old tool must read 0 ==="
echo "--- P1: RUNNING, pid ALIVE, file frozen 600s (the 24-minute-outage shape)"
make_state RUNNING "$LIVE_PID" 600
check "P1a OLD script says RUNNING (this is the defect)" "$OLD" 0
check "P1b NEW script says STALE" "$NEW" 4

echo "--- P2: BOOTING, pid ALIVE, file frozen 600s (crash before Startup returned)"
make_state BOOTING "$LIVE_PID" 600
check "P2a OLD script says BOOTING" "$OLD" 3
check "P2b NEW script says STALE" "$NEW" 4

echo
echo "=== NEGATIVE CONTROLS -- the fix must not simply report STALE always ==="
echo "--- N1: RUNNING, pid ALIVE, file FRESH"
make_state RUNNING "$LIVE_PID" 0
check "N1a OLD script says RUNNING" "$OLD" 0
check "N1b NEW script STILL says RUNNING" "$NEW" 0

echo "--- N2: RUNNING, pid ALIVE, aged 5s -- inside the threshold, must stay green"
make_state RUNNING "$LIVE_PID" 5
check "N2 NEW script says RUNNING at age 5s" "$NEW" 0

echo "--- N3: BOOTING, pid ALIVE, file FRESH -- a real slow boot must stay BOOTING"
make_state BOOTING "$LIVE_PID" 0
check "N3 NEW script says BOOTING (not STALE)" "$NEW" 3

echo "--- N4: EXITED, frozen 600s -- a clean shutdown is SUPPOSED to sit frozen."
echo "        Every platform's Platform::App::Shutdown calls BootState::Stop(), so a"
echo "        clean exit writes EXITED (its pid then gone: see N4b)."
make_state EXITED "$LIVE_PID" 600
check "N4 NEW script says NOT_RUNNING, not STALE" "$NEW" 1
make_state EXITED 999999 0
check "N4b NEW script says NOT_RUNNING for EXITED with its pid gone (a clean exit)" "$NEW" 1

echo "--- N4c: NO_GAME_DATA (issue #72) -- written once before a deliberate exit, so neither"
echo "         its frozen age nor its gone pid makes it STALE."
make_state NO_GAME_DATA "$LIVE_PID" 600
check "N4c NEW script says NO_GAME_DATA, frozen 600s, pid alive" "$NEW" 6
make_state NO_GAME_DATA 999999 0
check "N4d NEW script says NO_GAME_DATA with its pid gone" "$NEW" 6

echo "--- N5: pid GONE -- the pre-existing STALE(4) must be unchanged"
make_state RUNNING 999999 0
check "N5a OLD script says STALE" "$OLD" 4
check "N5b NEW script says STALE (same code, same reason)" "$NEW" 4

echo "--- N6: no state file, no OpenBA.exe -- unchanged NOT_RUNNING"
rm -f "$FIX/standalone_boot_state.txt"
check "N6 NEW script says NOT_RUNNING" "$NEW" 1

echo
echo "--- N7: a LIVE publisher that keeps republishing ACROSS the threshold."
echo "        This is the case a naive mtime check breaks: 20s of real 1s ticks,"
echo "        probed after the file is older than one interval but never stale."
make_state RUNNING "$LIVE_PID" 0
( for i in $(seq 1 25); do
      printf 'STATE=RUNNING pid=%s uptimeSec=%s detail=-\n' "$LIVE_PID" "$i" \
          > "$FIX/standalone_boot_state.txt"
      sleep 1
  done ) &
PUBPID=$!
sleep 12
check "N7 NEW script RUNNING after 12s of live 1s ticks" "$NEW" 0
kill "$PUBPID" 2>/dev/null; wait "$PUBPID" 2>/dev/null

echo
echo "=== DERIVATION CONTROLS -- is the threshold really derived, or just a number? ==="
echo "    Zero free parameters buys unaimability, not soundness. A hard-coded 10s"
echo "    would pass every case above and go stale the moment the watchdog changed."
echo "    These two make the derivation FALSIFIABLE."

# D1: move the publisher's interval and the threshold must move with it.
DERIV="$FIX/deriv"
mkdir -p "$DERIV/tools" "$DERIV/src/diag/win"
cp "$NEW" "$DERIV/tools/boot_state.sh"
sed 's/Sleep(1000);/Sleep(4000);/' "$SCRIPT_DIR/../src/diag/win/boot_state_watchdog.cpp" \
    > "$DERIV/src/diag/win/boot_state_watchdog.cpp"
make_state RUNNING "$LIVE_PID" 20     # 20s: stale at a 1s interval, FRESH at 4s
TOTAL=$(( TOTAL + 1 ))
run_tool "$DERIV/tools/boot_state.sh"; rc=$?
if [ "$rc" = "0" ] && printf '%s' "$LAST_OUT" | grep -q '40s'; then
    PASS=$(( PASS + 1 ))
    echo "  PASS  D1 threshold TRACKED the publisher: Sleep(4000) -> 40s, age 20s reads RUNNING (rc=$rc)"
else
    FAIL=$(( FAIL + 1 ))
    echo "  FAIL  D1 threshold did NOT track Sleep(4000) (rc=$rc, expected 0 and a 40s threshold)"
fi
echo "$LAST_OUT" | sed 's/^/        | /'
# and the SAME 20s file against the REAL 1s interval must still be STALE,
# so D1's green is a difference between the two, not a property of the file.
check "D1b same 20s file, real Sleep(1000) tree -> STALE" "$NEW" 4

# D2: no publisher source next to the script -- the fallback must ANNOUNCE itself.
mkdir -p "$FIX/orphan/tools"
cp "$NEW" "$FIX/orphan/tools/boot_state.sh"
make_state RUNNING "$LIVE_PID" 600
TOTAL=$(( TOTAL + 1 ))
run_tool "$FIX/orphan/tools/boot_state.sh"; rc=$?
if [ "$rc" = "4" ] && printf '%s' "$LAST_OUT" | grep -q 'FALLBACK'; then
    PASS=$(( PASS + 1 ))
    echo "  PASS  D2 no watchdog source -> still STALE, and the fallback SAYS it is a fallback (rc=$rc)"
else
    FAIL=$(( FAIL + 1 ))
    echo "  FAIL  D2 fallback did not announce itself (rc=$rc)"
fi
echo "$LAST_OUT" | sed 's/^/        | /'

echo
echo "======================================================================"
echo "APPLICABLE CASES: $TOTAL   PASS: $PASS   FAIL: $FAIL"
echo "  positive (must be non-zero on the new tool): P1b, P2b"
echo "  negative (must stay green / unchanged)     : N1b, N2, N3, N4, N5b, N6, N7"
echo "  BEFORE-state evidence                      : P1a, P2a, N1a, N5a"
echo "  derivation is falsifiable                  : D1, D1b, D2"
echo "======================================================================"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
