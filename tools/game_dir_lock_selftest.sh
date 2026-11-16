#!/usr/bin/env bash
# =============================================================================
# tools/game_dir_lock_selftest.sh -- reproduces and verifies the defects fixed
# in tools/game_dir_lock.sh.
#
# Wave 11 follow-ups (2026-09-10):
#   1. empty-ticket starvation in ticket_head()
#   2. a timeout+requeue restamping the ticket epoch, losing FIFO priority
#   3. a caller reading a refusal as a grant (the "exit-code trap")
#
# Wave 26, from Wave 25 lane ALLOC (2026-09-12):
#   4. a LIVE holder expired on the wall clock alone (ALLOC: broken at 1802s
#      with its process alive and a write 1s earlier)
#   5. the holder was never told its lock had been broken
#   6. exe identity across a run was per-lane discipline, not tooling
#
# CONTROLS. Each of 4/5/6 is a screen, and every screen here is handed BOTH a
# shape it must catch and a shape it must not:
#
#   * 4 is scored on a DEAD holder (must break -- otherwise the lock deadlocks
#     forever) and on four separately-evidenced LIVE holders (must not), all at
#     the SAME age. It also asserts the legacy age-only predicate WOULD have
#     broken each live fixture, so a pass cannot come from a fixture that never
#     reached the changed code.
#   * 5 is scored on a breach naming the lane (must shout), on no breach at all
#     (must not -- a banner printed unconditionally is a dead check), and on a
#     breach naming a DIFFERENT lane (must not).
#   * 6 is scored on an exe changed WHILE A RUN IS LIVE (must flag), on an exe
#     changed while idle -- an ordinary deploy (must not), and on an unchanged
#     exe (must not). The corrupted value is the exe bytes, which is exactly
#     what the check compares.
#
# Each defect gets its OWN pass/fail, reported separately -- never an
# aggregate. An aggregate "3/3" cannot distinguish "all three fixed" from "one
# test carrying the others".
#
# Runs entirely against a SCRATCH directory via BANISHED_GAME_DIR. Never runs
# against the real game directory. Uses `GAMELOCK_NO_MAIN=1` to source the
# subject file for its internal functions (ticket_head, ticket_take, ...)
# without triggering its command dispatch.
#
# `acquire`'s exit status is always checked directly ( `if cmd; then` / `cmd;
# rc=$?` ), never through a pipe -- piping the very command whose exit-code
# handling is under test would be its own punchline.
# =============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The subject is overridable so tools/game_dir_lock_mutations.sh can point this
# same suite at DELIBERATELY BROKEN copies. A suite that has only ever been run
# against a correct subject cannot tell a passing screen from a dead one.
LOCK_SH="${GAMELOCK_SELFTEST_SUBJECT:-$HERE/game_dir_lock.sh}"

PASS=0
FAIL=0
report() {
    local label="$1" ok="$2" detail="$3"
    if [ "$ok" = "0" ]; then
        echo "PASS -- $label"
        PASS=$((PASS + 1))
    else
        echo "FAIL -- $label"
        echo "       $detail"
        FAIL=$((FAIL + 1))
    fi
}

new_scratch() {
    local d
    d="$(mktemp -d "${TMPDIR:-/tmp}/gdl_selftest.XXXXXX")"
    printf '%s' "$d"
}

# =============================================================================
# DEFECT 1 -- empty-ticket starvation
# =============================================================================
# An empty ticket file (asked="") must not make ticket_head() blind to that
# waiter. Reproduce by hand: create a queue dir with two tickets, one of them
# empty and the other with a LATER (larger) asked epoch. The empty one is the
# true earliest waiter (it has been sitting there, we just never recorded
# when) -- what we actually assert is narrower and unambiguous: ticket_head
# must not throw and must not silently skip the file to the point of always
# preferring the other ticket. We assert the two required properties:
#   (a) ticket_head does not error out / abort the loop on an empty ticket
#   (b) an empty ticket IS eligible to be chosen as head (asked coerces to 0,
#       the lowest possible epoch, which is also the conservative choice: an
#       unreadable "when did I ask" defaults to "assume you were first" rather
#       than "assume you don't exist")
test_defect1() {
    local scratch queue rc out
    scratch="$(new_scratch)"
    queue="$scratch/.gamedir_queue"
    mkdir -p "$queue"

    # laneB: a normal ticket, asked far in the future relative to epoch 0, so
    # if the empty ticket is correctly treated as asked=0 it MUST win.
    printf '%s' "9999999999" > "$queue/laneB"

    # laneA: an EMPTY ticket (the reproduction of the defect) with a fresh
    # mtime so it is not filtered out as stale.
    : > "$queue/laneA"
    touch "$queue/laneA"
    touch "$queue/laneB"

    out=$(
        GAMELOCK_NO_MAIN=1 BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash -c '
            source "'"$LOCK_SH"'"
            ticket_head
        ' 2>&1
    )
    rc=$?

    rm -rf "$scratch"

    if [ "$rc" -ne 0 ]; then
        report "defect 1: empty ticket (ticket_head must not error)" 1 \
            "ticket_head exited $rc, output: $out"
        return
    fi
    if [ "$out" != "laneA" ]; then
        report "defect 1: empty ticket (ticket_head must select it, asked coerced to 0)" 1 \
            "expected head='laneA' (asked coerced to 0, earliest), got '$out'"
        return
    fi
    report "defect 1: empty ticket (ticket_head selects it instead of going blind)" 0 ""
}

# =============================================================================
# DEFECT 2 -- timeout+requeue must preserve the original asked epoch
# =============================================================================
# Simulate: laneA takes a ticket at an old epoch (as if it had been queued a
# long time), then "times out" (cmd_acquire's timeout path, which must no
# longer ticket_drop), then calls ticket_take() again (as a retrying caller
# would). Assert the epoch recorded in the ticket file is UNCHANGED -- i.e.
# ticket_take() did not restamp now() because the file still existed.
test_defect2() {
    local scratch queue old_epoch rc out kept

    scratch="$(new_scratch)"
    queue="$scratch/.gamedir_queue"
    mkdir -p "$queue"

    old_epoch=1000000000   # a fixed, obviously-old epoch
    printf '%s' "$old_epoch" > "$queue/laneA"

    # Reproduce the OLD, buggy behaviour first, to prove the test actually
    # discriminates: if the timeout path called ticket_drop and then
    # ticket_take() ran, the epoch would change. We check that emulating the
    # OLD sequence (explicit drop + retake) DOES change the epoch, and that
    # the NEW code path (retake WITHOUT a drop, as cmd_acquire now does on
    # timeout) does NOT.
    old_behaviour_epoch=$(
        GAMELOCK_NO_MAIN=1 BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash -c '
            source "'"$LOCK_SH"'"
            ticket_drop laneA      # what the OLD cmd_acquire did on timeout
            ticket_take laneA      # what a requeuing acquire call does
            cat "$QUEUE/laneA"
        ' 2>&1
    )

    # restore the original ticket for the real (new-code) check
    printf '%s' "$old_epoch" > "$queue/laneA"

    kept=$(
        GAMELOCK_NO_MAIN=1 BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash -c '
            source "'"$LOCK_SH"'"
            # NEW behaviour: cmd_acquire timeout no longer drops the ticket,
            # so a requeuing acquire just calls ticket_take() against the
            # still-present file.
            ticket_take laneA
            cat "$QUEUE/laneA"
        ' 2>&1
    )

    rm -rf "$scratch"

    if [ "$old_behaviour_epoch" = "$old_epoch" ]; then
        report "defect 2: control (old drop-then-retake must lose the epoch)" 1 \
            "expected the OLD sequence to restamp away from $old_epoch, but it stayed $old_behaviour_epoch -- test does not discriminate"
        return
    fi
    if [ "$kept" != "$old_epoch" ]; then
        report "defect 2: timeout+requeue preserves original asked epoch" 1 \
            "expected epoch to stay $old_epoch, got '$kept' (old buggy sequence gave '$old_behaviour_epoch')"
        return
    fi
    report "defect 2: timeout+requeue preserves original asked epoch (old sequence gave $old_behaviour_epoch, new gives $kept)" 0 ""
}

# Companion check: cmd_acquire's TIMEOUT branches must not call ticket_drop
# any more (source-level check, since driving a real 900s timeout is not
# practical in a selftest). This directly checks the fix landed, not just a
# hand-simulation of its effect.
test_defect2_source() {
    local timeout_block rc
    # Extract the two TIMEOUT branches of cmd_acquire and confirm neither
    # calls ticket_drop.
    timeout_block=$(awk '
        /DENIED -- TIMEOUT after/{grab=1}
        grab{print}
        grab && /return 1/{grab=0}
    ' "$LOCK_SH")
    # Strip comments (this file's own explanation of the fix legitimately
    # mentions ticket_drop by name) before looking for a live call.
    if printf '%s' "$timeout_block" | grep -v '^[[:space:]]*#' | grep -q 'ticket_drop'; then
        report "defect 2: source check (TIMEOUT branches must not ticket_drop)" 1 \
            "found ticket_drop still present in a TIMEOUT branch:
$timeout_block"
        return
    fi
    report "defect 2: source check (TIMEOUT branches no longer ticket_drop)" 0 ""
}

# =============================================================================
# DEFECT 3 -- the exit-code trap: a caller must have an unmissable verb
# =============================================================================
# `assert-held <lane>` (alias for `verify`) must exit non-zero unless that
# exact lane currently owns the lock, and 0 when it does. This is the "point
# of use" gate the brief asks for -- checked directly, never through a pipe.
test_defect3() {
    local scratch rc

    scratch="$(new_scratch)"

    # Case A: nobody holds the lock at all.
    BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" assert-held laneA >/dev/null 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        rm -rf "$scratch"
        report "defect 3: assert-held with no lock held must fail" 1 "exit code was 0"
        return
    fi

    # Case B: laneA legitimately holds the lock -- must succeed.
    BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" acquire laneA 5 >/dev/null 2>&1
    if ! BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" assert-held laneA >/dev/null 2>&1; then
        rc=$?
        rm -rf "$scratch"
        report "defect 3: assert-held for the true holder must succeed" 1 "exit code was $rc"
        return
    fi

    # Case C: laneB is not the holder -- must fail (this is the exact shape
    # of the exit-code trap: a caller who assumes ownership without checking).
    BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" assert-held laneB >/dev/null 2>&1
    rc=$?
    BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" release laneA >/dev/null 2>&1
    rm -rf "$scratch"
    if [ "$rc" -eq 0 ]; then
        report "defect 3: assert-held for a non-holder must fail" 1 "exit code was 0"
        return
    fi

    report "defect 3: assert-held gates correctly (fail/succeed/fail across no-lock, true holder, wrong lane)" 0 ""
}

# =============================================================================
# DEFECT 4 -- a LIVE holder must not be expired on the wall clock alone
# =============================================================================
# Wave 25 lane ALLOC: lock broken as stale at 1802s while its process was alive
# and had written to the protected directory 1 second before the check.
#
# The dead-holder cell and the four live-holder cells use the SAME lock age
# (7200s). Only the liveness evidence differs between them, so nothing but the
# evidence can explain a difference in their verdicts.
#
# `stale_cell <label> <age> <expect BREAK|KEEP> <legacy BREAK|KEEP> <env...>`
# builds a scratch game dir, plants a lock of the given age owned by 'laneA',
# runs break_if_stale in a sourced shell, and reports whether the lock
# directory survived.
#
# `legacy` is the DISCRIMINATION CONTROL and it is asserted in BOTH directions:
# the cell states what the old age-only rule would have done, and the test
# fails if it did something else. A KEEP cell whose legacy verdict is also KEEP
# never reached the changed code, so its pass would mean nothing; a BREAK cell
# whose legacy verdict is KEEP would mean the fixture is not what it claims.
# Stating it per-cell is what stops the control from being applied where it
# does not belong -- the "under the floor" cell legitimately agrees with the
# old rule and says so.
stale_cell() {
    local label="$1" age="$2" expect="$3" legacy_expect="$4"; shift 4
    local scratch out legacy survived rc
    scratch="$(new_scratch)"
    mkdir -p "$scratch/.lane_lock"
    printf '%s' "laneA" > "$scratch/.lane_lock/owner"
    printf '%s' "$(( $(date +%s) - age ))" > "$scratch/.lane_lock/started"
    # optional extras the caller asked for, as KEY=VALUE pairs handled here
    local heartbeat=0 e
    for e in "$@"; do
        case "$e" in
            HEARTBEAT=1) heartbeat=1 ;;
        esac
    done
    [ "$heartbeat" = 1 ] && touch "$scratch/.lane_lock/heartbeat"

    out=$(
        env "$@" GAMELOCK_NO_MAIN=1 BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash -c '
            source "'"$LOCK_SH"'"
            a=$(lock_age)
            if [ "$a" -gt "$STALE_AFTER" ]; then echo "LEGACY=BREAK"; else echo "LEGACY=KEEP"; fi
            if break_if_stale selftest-breaker; then echo "BROKE=yes"; else echo "BROKE=no"; fi
        ' 2>/dev/null
    )
    legacy=$(printf '%s' "$out" | sed -n 's/^LEGACY=//p')
    if [ -d "$scratch/.lane_lock" ]; then survived=KEEP; else survived=BREAK; fi
    rm -rf "$scratch"

    if [ "$survived" != "$expect" ]; then
        report "defect 4/$label: expected $expect, got $survived" 1 \
            "age=${age}s env='$*' subject output: $(printf '%s' "$out" | tr '\n' ' ')"
        return 1
    fi
    if [ "$legacy" != "$legacy_expect" ]; then
        report "defect 4/$label: control -- the legacy age-only rule should have said $legacy_expect" 1 \
            "it said '$legacy' at age=${age}s; the fixture is not the one this cell claims to be"
        return 1
    fi
    report "defect 4/$label: $expect (legacy age-only rule: $legacy)" 0 ""
    return 0
}

test_defect4() {
    # THE POSITIVE CASE, handed to a screen that otherwise only looks for the
    # ABSENCE of a break. A genuinely dead holder MUST still be broken or every
    # lane deadlocks behind a corpse.
    stale_cell "dead holder must still break" 7200 BREAK BREAK \
        GAMELOCK_FAKE_PIDS= GAMELOCK_FAKE_IDLE=99999

    # ALLOC's case, both halves, separately.
    stale_cell "live exe pid (ALLOC, process alive)" 7200 KEEP BREAK \
        GAMELOCK_FAKE_PIDS=12345 GAMELOCK_FAKE_IDLE=99999
    stale_cell "recent write (ALLOC, wrote 1s before the check)" 7200 KEEP BREAK \
        GAMELOCK_FAKE_PIDS= GAMELOCK_FAKE_IDLE=1

    # The other two liveness signals, each alone.
    stale_cell "handles held open" 7200 KEEP BREAK \
        GAMELOCK_FAKE_PIDS= GAMELOCK_FAKE_IDLE=99999 GAMELOCK_FAKE_RUN=1
    stale_cell "fresh holder heartbeat" 7200 KEEP BREAK \
        GAMELOCK_FAKE_PIDS= GAMELOCK_FAKE_IDLE=99999 HEARTBEAT=1

    # Young: nothing is stale under the floor, whatever the evidence says.
    # This cell AGREES with the old rule and says so -- it is not a
    # fix-exercising cell and must not be scored as one.
    stale_cell "under the floor" 60 KEEP KEEP \
        GAMELOCK_FAKE_PIDS= GAMELOCK_FAKE_IDLE=99999

    # The deadlock bound. Evidence says ALIVE on every axis and it breaks
    # anyway, because "refuse to break a live-looking lock" must not mean
    # "never break". This cell is the reason the conservative direction is
    # safe to take.
    stale_cell "hard ceiling breaks despite every live signal" 20000 BREAK BREAK \
        GAMELOCK_FAKE_PIDS=12345 GAMELOCK_FAKE_IDLE=1 GAMELOCK_FAKE_RUN=1 HEARTBEAT=1
}

# =============================================================================
# DEFECT 5 -- the holder must be TOLD its lock was broken
# =============================================================================
# ALLOC: "the lock was broken as stale WHILE MY RUN WAS LIVE and I got no
# signal at all -- I only learned from your message."
BANNER='YOUR LOCK WAS BROKEN AS STALE'

test_defect5() {
    local scratch out rc

    # --- negative control FIRST: no breach record anywhere. assert-held must
    # still fail (no lock is held) but must NOT print the banner. A banner that
    # appears unconditionally would make every positive cell below vacuous.
    scratch="$(new_scratch)"
    out=$(BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" assert-held laneA 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        rm -rf "$scratch"
        report "defect 5: control -- assert-held with no lock must fail" 1 "exit 0"
        return
    fi
    if printf '%s' "$out" | grep -q "$BANNER"; then
        rm -rf "$scratch"
        report "defect 5: control -- no breach record must print NO banner" 1 \
            "banner printed with no breach record; the check is unconditional and proves nothing:
$out"
        return
    fi
    rm -rf "$scratch"

    # --- the positive case: laneA holds a lock, it is broken as stale, laneA
    # asks the three questions a wronged lane actually asks.
    scratch="$(new_scratch)"
    mkdir -p "$scratch/.lane_lock"
    printf '%s' "laneA" > "$scratch/.lane_lock/owner"
    printf '%s' "$(( $(date +%s) - 7200 ))" > "$scratch/.lane_lock/started"
    GAMELOCK_NO_MAIN=1 GAMELOCK_FAKE_PIDS= GAMELOCK_FAKE_IDLE=99999 \
        BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash -c '
            source "'"$LOCK_SH"'"
            break_if_stale laneB
        ' >/dev/null 2>&1
    if [ -d "$scratch/.lane_lock" ]; then
        rm -rf "$scratch"
        report "defect 5: fixture -- the lock should have been broken" 1 "it survived"
        return
    fi

    out=$(BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" assert-held laneA 2>&1)
    if ! printf '%s' "$out" | grep -q "$BANNER"; then
        rm -rf "$scratch"
        report "defect 5: assert-held must tell the broken holder" 1 "no banner:
$out"
        return
    fi
    if ! printf '%s' "$out" | grep -q "laneB"; then
        rm -rf "$scratch"
        report "defect 5: the notice must name the breaker" 1 "breaker 'laneB' not named:
$out"
        return
    fi

    # release must FAIL, not print "nothing to release" and exit 0 -- that
    # exact true-but-useless sentence is what ALLOC would have seen.
    out=$(BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" release laneA 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        rm -rf "$scratch"
        report "defect 5: release after a breach must fail" 1 "exit 0, output:
$out"
        return
    fi
    if ! printf '%s' "$out" | grep -q "$BANNER"; then
        rm -rf "$scratch"
        report "defect 5: release after a breach must shout" 1 "no banner:
$out"
        return
    fi

    # --- negative control: the breach names laneA, so laneB must not be told
    # it was breached. (A notice that fires for every lane is the same dead
    # check as one that fires with no record at all, in the other direction.)
    out=$(BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" assert-held laneB 2>&1)
    if printf '%s' "$out" | grep -q "$BANNER"; then
        rm -rf "$scratch"
        report "defect 5: control -- a breach naming laneA must not shout at laneB" 1 \
            "banner shown to the wrong lane:
$out"
        return
    fi

    # --- ack: the wrong lane must not be able to clear it; the right one must.
    if BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" ack-breach laneB >/dev/null 2>&1; then
        rm -rf "$scratch"
        report "defect 5: ack-breach by the wrong lane must be refused" 1 "it succeeded"
        return
    fi
    if ! BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" ack-breach laneA >/dev/null 2>&1; then
        rm -rf "$scratch"
        report "defect 5: ack-breach by the victim must succeed" 1 "it failed"
        return
    fi
    out=$(BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" bash "$LOCK_SH" assert-held laneA 2>&1)
    rm -rf "$scratch"
    if printf '%s' "$out" | grep -q "$BANNER"; then
        report "defect 5: an acknowledged breach must stop shouting" 1 "still shouting:
$out"
        return
    fi

    report "defect 5: the broken holder is told (acquire/assert-held/release), only the victim, and only until acked" 0 ""
}

# =============================================================================
# DEFECT 6 -- exe identity across a run, as tooling rather than lane discipline
# =============================================================================
# The discriminator under test is NOT "did the exe change" -- it changes on
# every deploy, by design. It is "did it change WHILE A RUN WAS LIVE".
#
# identity_cell <label> <live: 0|1> <change: 0|1> <expect FLAG|CLEAN>
identity_cell() {
    local label="$1" live="$2" change="$3" expect="$4"
    local scratch out got rc
    scratch="$(new_scratch)"
    printf 'ORIGINAL-BUILD' > "$scratch/OpenBA.exe"

    # The wrapped command stands in for `tools/build.sh run`: optionally mark a run as
    # live, optionally overwrite the deployed exe, take long enough for the
    # 1s-interval watcher to sample either side of the change.
    cat > "$scratch/cmd.sh" <<CMDEOF
sleep 2
[ "$live" = 1 ] && : > "$scratch/live_marker"
sleep 2
[ "$change" = 1 ] && printf 'SOMEBODY-ELSES-BUILD' > "$scratch/OpenBA.exe"
sleep 3
rm -f "$scratch/live_marker"
exit 0
CMDEOF

    out=$(
        GAMELOCK_WATCH_INTERVAL=1 \
        GAMELOCK_FAKE_RUN_FILE="$scratch/live_marker" \
        BANISHED_DIR="$scratch" BANISHED_GAME_DIR="$scratch" \
        bash "$LOCK_SH" run laneA 30 -- bash "$scratch/cmd.sh" 2>&1
    )
    rc=$?
    if printf '%s' "$out" | grep -q 'exe identity: FLAGGED'; then got=FLAG; else got=CLEAN; fi
    rm -rf "$scratch"

    if [ "$got" != "$expect" ]; then
        report "defect 6/$label: expected $expect, got $got" 1 "rc=$rc, output:
$out"
        return 1
    fi
    if [ "$expect" = "FLAG" ] && [ "$rc" -ne 7 ]; then
        report "defect 6/$label: a flagged run must not exit 0" 1 "rc=$rc (expected 7)"
        return 1
    fi
    if [ "$expect" = "CLEAN" ] && [ "$rc" -ne 0 ]; then
        report "defect 6/$label: a clean run must pass the command's status through" 1 "rc=$rc (expected 0)"
        return 1
    fi
    report "defect 6/$label: $expect (rc=$rc)" 0 ""
    return 0
}

test_defect6() {
    # POSITIVE: the clobber. The corrupted value is the exe's bytes, which is
    # exactly what the check compares -- corrupting anything else would report
    # NOT CAUGHT and indict a sound instrument.
    identity_cell "exe replaced WHILE the run is live" 1 1 FLAG
    # NEGATIVE, and the one that matters: an ordinary deploy also changes the
    # exe. A screen that flags every change is useless and fails here.
    identity_cell "exe replaced while idle (an ordinary deploy)" 0 1 CLEAN
    # NEGATIVE: a live run with no change at all.
    identity_cell "live run, exe untouched" 1 0 CLEAN
}

echo "== game_dir_lock.sh selftest =="
echo "subject: $LOCK_SH"
echo

test_defect1
test_defect2
test_defect2_source
test_defect3
test_defect4
test_defect5
test_defect6

echo
echo "== summary (per-defect, not aggregate) =="
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
