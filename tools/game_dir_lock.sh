#!/usr/bin/env bash
# =============================================================================
# tools/game_dir_lock.sh -- a mutex over the ONE shared game directory
# =============================================================================
#
# TWO DIRECTORIES: tools/game_dirs.sh pools this one (perf) with a second copy
# (functional) that has a user dir of its own (BANISHED_USER_DIR); take a dir
# from it rather than calling this script with BANISHED_DIR by hand.
#
# WHY THIS EXISTS
#
# Lanes work in isolated git worktrees, so their SOURCE never collides. But every
# lane deploys the same `OpenBA.exe` into `C:/GOG Games/Banished` and every
# lane's run writes the same `standalone_{debug,error,crash}.log` there. The
# worktree isolation makes that easy to forget, because everything else about a
# lane is private.
#
# During Wave 9's eight-lane session this cost real work:
#
#   * three consecutive deploy+run attempts by one lane were clobbered by another
#     lane overwriting the exe in the seconds between `cp` and launch,
#   * a `cp` failed outright with "Device or resource busy" because another lane
#     had the exe running,
#   * one lane's first measurement was silently taken against a DIFFERENT lane's
#     binary and had to be discovered and redone.
#
# The last one is the dangerous class. A clobbered run does not announce itself:
# you get a plausible log, from a plausible exe, that is measuring somebody
# else's code. That is the same shape as every other defect this wave has paid
# for -- an instrument reporting a confident number about the wrong thing.
#
# WHAT IT DOES NOT DO
#
# This is advisory. It cannot stop a lane that does not call it, and it does not
# protect the game data files (`WinData/`, the `.pkm` set). A lane that mounts or
# unmounts mods changes the INPUT of every other lane's run, and no lock over the
# exe will catch that -- announce those changes to the integrator instead.
#
# USAGE
#
#   tools/game_dir_lock.sh acquire <lane-name> [timeout-seconds]   # default 900
#   tools/game_dir_lock.sh assert-held <lane-name> [your-build.exe]
#                                    # gate a step on ownership; see below.
#                                    # With a path, ALSO fails unless the exe
#                                    # deployed in the game dir IS that build.
#   tools/game_dir_lock.sh release <lane-name>
#   tools/game_dir_lock.sh heartbeat <lane-name>   # "still here" during a long hold
#   tools/game_dir_lock.sh status [lane-name]
#   tools/game_dir_lock.sh ack-breach <lane-name>  # clear a breach record you have read
#   tools/game_dir_lock.sh orphans   # is the dir usable? names an UNLOCKED run
#
# WAVE 26: STALENESS IS NO LONGER A WALL CLOCK, AND A BREAK IS NO LONGER SILENT
#
# Wave 25 lane ALLOC's lock was broken as stale at 1802s while its process was
# ALIVE and had written to the protected directory 1s earlier, and ALLOC WAS
# NEVER TOLD -- it learned from the integrator, after losing a measurement
# pair. Two changes, both aimed at that:
#
#   * `STALE_AFTER` is a FLOOR, not a verdict. Past it the lock is examined
#     against the liveness evidence `status` was already computing one function
#     away -- holder heartbeat, live OpenBA.exe pids, held handles, last
#     write to the game dir. A lock with any of those is NOT stale at any age
#     below `STALE_HARD`. See the block above `STALE_AFTER` for what this costs
#     and how `STALE_HARD` bounds it.
#   * Breaking a lock leaves a BREACH RECORD naming the victim. `acquire`,
#     `release`, `assert-held`, `heartbeat` and `status <lane>` all shout it,
#     and `release` FAILS instead of printing "nothing to release".
#
# LONG HOLDS: if you hold the lock across a long analysis gap, call
# `heartbeat <lane>` occasionally -- it is the only liveness signal
# attributable to YOU rather than to the machine. `run` does it for you.
#
# IDENTITY, NOT JUST OWNERSHIP: `run` samples the deployed exe's md5 for the
# life of the wrapped command and reports whether it changed WHILE A RUN WAS
# LIVE (a clobber) as opposed to between idle samples (an ordinary deploy). It
# exits 7 if the command succeeded but the identity check did not, and drops
# `exe_identity.tsv` into `--snapshot`. That is ALLOC's per-launch md5 check,
# which is the only reason its contamination question was answerable, turned
# from per-lane discipline into part of the tool.
#
# "IS THE LOCK FREE?" AND "CAN I USE THE DIRECTORY?" ARE DIFFERENT QUESTIONS.
# A process that outlives its run keeps OpenBA.exe and standalone_*.log
# open, so deploys fail with "Device or resource busy" while the lock correctly
# reads free. If a deploy fails that way, do NOT wait for the lock -- run
# `orphans`. See the UNLOCKED RUNS AND ORPHANS block below.
#
#   # the whole point -- deploy and run under ONE lock, so nothing can land
#   # between the copy and the launch:
#   tools/game_dir_lock.sh acquire lane-FOO 900 || exit 1
#
# PREFER, and the reason it exists:
#
#   tools/game_dir_lock.sh run lane-FOO 900 -- tools/build.sh run
#
# READ YOUR LOGS FROM A SNAPSHOT, NOT FROM THE GAME DIR:
#
#   tools/game_dir_lock.sh run lane-FOO 900 --snapshot ./mylogs -- tools/build.sh run
#
# The lock covers deploy+run. It does NOT cover the window in which you sit and
# ANALYSE, and standalone_*.log is overwritten by whoever runs next. On
# 2026-09-09 three consecutive reads of "the" debug log were three different
# processes, and another lane's verification-mode banner was read as a
# default-on regression in the merged tree. `--snapshot` copies the logs while
# the lock is still held, so what you analyse is what you ran.
#
# DEFEATED BY A PIPE -- this cost a real clobber on 2026-09-08. `acquire`
# returns 1 on timeout correctly, but in a pipeline the exit status is the LAST
# command's:
#
#   tools/game_dir_lock.sh acquire lane-FOO 540 2>&1 | tail -3   # <-- ALWAYS 0
#
# The lane read "exit code 0", deployed over another lane's exe mid-run, and
# rewrote its logs -- while the piped text it did not read said TIMEOUT in plain
# English. NEVER pipe `acquire` into head/tail/grep. Use `run` above, or call
# `acquire` bare, or `set -o pipefail`, or:
#
#   if ! tools/game_dir_lock.sh acquire lane-FOO 900; then exit 1; fi
#
# SECOND COSTUME, found 2026-09-10 by Wave 18 lane VDATA, cost ~15 min: it is
# not only pipes. ANY compound command discards the refusal, because the caller
# (or the agent harness reporting the step) sees the status of the LAST element:
#
#   tools/game_dir_lock.sh acquire lane-FOO 540; echo "ACQUIRE_EXIT=$?"   # <-- reports 0
#
# Note what makes this one nastier than the pipe: the echoed text is CORRECT --
# `$?` there really is acquire's status -- so the human-readable output says
# ACQUIRE_EXIT=1 while the step's own exit status says 0. Two fields disagree
# and the authoritative-looking one is wrong. A wrapper added to make the status
# more visible is what hid it.
#
# So the rule is not "do not pipe". It is: DO NOT PUT `acquire` IN A COMPOUND
# COMMAND OF ANY KIND -- no pipe, no `;`, no trailing echo, no `&&` tail. Give
# it a statement of its own, and put `assert-held` in front of the dangerous
# action regardless (below). In that lane's case assert-held is what caught it
# before it deployed over another lane.
#
# A granted acquire now also prints a line matching `^\[gamelock\] GRANT `, so a
# caller that must pipe can test for the POSITIVE rather than trusting a status
# that a pipe can swallow. And `verify <lane>` (alias: `assert-held`)
# re-checks ownership immediately before launch.
#
# THE STRONGEST GUARD DOES NOT TRUST `acquire`'S EXIT CODE AT ALL -- a caller
# that forgets the `||` entirely (not even a pipe, just a bare call with no
# check) defeats every mitigation above, and that happened in Wave 11. Put
# `assert-held` directly in front of the dangerous action instead, as its own
# statement that fails the step on its own if ownership is not current:
#
#   tools/game_dir_lock.sh acquire lane-FOO 900
#   tools/game_dir_lock.sh assert-held lane-FOO || exit 1     # <-- copy this line
#   cp build/cmake-debug/OpenBA.exe "$GAME/OpenBA.exe"
#
# This is what `run` already does internally, and it is why `run` is PREFERRED
# over hand-rolling acquire/deploy/release: there is no exit code to discard
# because there is no gap between the check and the action for a bad read of
# it to land in.
#
#   trap 'tools/game_dir_lock.sh release lane-FOO' EXIT
#   cp build/cmake-debug/OpenBA.exe "$GAME/OpenBA.exe"
#   ( cd "$GAME" && BANISHED_AUTOSTART=1 ./OpenBA.exe )
#   cp "$GAME"/standalone_*.log ./my-evidence/     # copy out BEFORE releasing
#
# Releasing before copying the logs out reintroduces the race you just avoided.
# =============================================================================

set -u

GAME="${BANISHED_DIR:-${BANISHED_GAME_DIR:-C:/GOG Games/Banished}}"
LOCK="$GAME/.lane_lock"          # a DIRECTORY: mkdir is atomic on every fs here

# ---- STALENESS: A WALL CLOCK IS NOT EVIDENCE OF DEATH ------------------------
#
# WAVE 25, LANE ALLOC. Its lock was broken as stale at **1802s** while its
# process was ALIVE and had written to the protected directory **one second
# before the staleness check ran**. During the unlocked window another lane
# legitimately acquired the lock this script granted it and deployed over
# ALLOC's exe. That lane did nothing wrong; the instrument did. And ALLOC WAS
# NEVER TOLD -- its own words: "the lock was broken as stale WHILE MY RUN WAS
# LIVE and I got no signal at all -- I only learned from your message." A
# measurement pair was lost.
#
# THE FIX IS NOT A BIGGER NUMBER. `status` already computed, and printed, every
# fact needed to know that holder was alive: live OpenBA.exe pids, held
# handles, and the age of the last write to the game dir. The information
# required NOT to expire a live lock was already inside the tool that expired
# it, one function away, and was never consulted. So:
#
#   * STALE_AFTER is a FLOOR, not a verdict. Past it, the lock is EXAMINED.
#   * A lock is stale only when the wall clock has passed AND every liveness
#     signal is negative (see `lock_staleness`).
#   * A breach is RECORDED where the wronged lane will see it (`record_breach`
#     / `breach_notice`), so a broken lock can never again be silent to its
#     holder.
#
# THE COST OF THE CONSERVATIVE DIRECTION, STATED PLAINLY: the liveness signals
# are NOT ATTRIBUTABLE to the holder, except the heartbeat. A live
# OpenBA.exe started by an UNRELATED lane running unlocked reads here as
# "the holder is alive" and would keep a genuinely dead lock alive. Refusing to
# break is the safe error -- it costs waiting, where the other error costs a
# measurement -- but "safe" is not "forever", so STALE_HARD bounds it. Past the
# hard ceiling the lock is broken on the wall clock alone, as loudly as this
# script can manage, and the breach is still recorded.
STALE_AFTER=3600                 # 60 min: the FLOOR past which evidence is consulted
STALE_HARD=10800                 # 3 h: broken regardless of evidence (deadlock bound)
STALE_IDLE_REQUIRED=600          # the game dir must have been QUIET this long
HEARTBEAT_STALE=900              # a holder heartbeat newer than this means ALIVE, full stop
BREACH_FILE="$GAME/.lane_lock_broken"
BREACH_KEEP=86400                # forget a breach record after a day

usage() { sed -n '/^# USAGE/,/^# ===/p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

now() { date +%s; }

lock_owner()  { cat "$LOCK/owner" 2>/dev/null || echo "(unknown)"; }
lock_started(){ cat "$LOCK/started" 2>/dev/null || echo 0; }

lock_age() {
    local started
    started=$(lock_started)
    case "$started" in ''|*[!0-9]*) started=$(now) ;; esac
    [ "$started" -eq 0 ] && started=$(now)
    printf '%s' "$(( $(now) - started ))"
}

# Age of the holder's heartbeat in seconds, or -1 if it never wrote one. This
# is the ONLY liveness signal attributable to the holder rather than to the
# machine: `run` touches it every watch interval, so a lane using the preferred
# path is protected even while it sits between launches analysing. A lane that
# hand-rolls acquire/deploy/release writes no heartbeat and falls back to the
# unattributable signals below, which is the pre-existing situation.
heartbeat_age() {
    local m
    [ -f "$LOCK/heartbeat" ] || { printf '%s' -1; return; }
    m=$(date -r "$LOCK/heartbeat" +%s 2>/dev/null) || { printf '%s' -1; return; }
    case "$m" in ''|*[!0-9]*) printf '%s' -1; return ;; esac
    printf '%s' "$(( $(now) - m ))"
}

# THE WHOLE POINT OF THIS FUNCTION: it is the evidence `status` already prints,
# consulted by the code that expires the lock. Echoes one word on stdout --
# YOUNG | ALIVE | STALE | STALE_HARD -- and the human-readable evidence on
# stderr, so a caller can branch on the verdict while a person reads the facts.
lock_staleness() {
    local age hb pids idle
    age=$(lock_age)
    hb=$(heartbeat_age)
    pids=$(live_exe_pids)
    idle=$(game_dir_idle)

    echo "[gamelock]   evidence: age=${age}s heartbeat=${hb}s exe_pids='${pids:-<none>}' last_write=${idle}s" >&2

    if [ "$age" -le "$STALE_AFTER" ]; then
        echo "[gamelock]   verdict: YOUNG (under the ${STALE_AFTER}s floor)" >&2
        printf '%s' YOUNG; return
    fi
    if [ "$age" -gt "$STALE_HARD" ]; then
        echo "[gamelock]   verdict: STALE_HARD (past the ${STALE_HARD}s ceiling; evidence NOT consulted)" >&2
        printf '%s' STALE_HARD; return
    fi
    if [ "$hb" -ge 0 ] && [ "$hb" -le "$HEARTBEAT_STALE" ]; then
        echo "[gamelock]   verdict: ALIVE -- holder heartbeat is ${hb}s old (limit ${HEARTBEAT_STALE}s)" >&2
        printf '%s' ALIVE; return
    fi
    if [ -n "$pids" ]; then
        echo "[gamelock]   verdict: ALIVE -- OpenBA.exe is running (pids: $pids)" >&2
        printf '%s' ALIVE; return
    fi
    if run_in_progress; then
        echo "[gamelock]   verdict: ALIVE -- game-dir handles are held open" >&2
        printf '%s' ALIVE; return
    fi
    if [ "$idle" -ge 0 ] && [ "$idle" -lt "$STALE_IDLE_REQUIRED" ]; then
        echo "[gamelock]   verdict: ALIVE -- the game dir was written ${idle}s ago (quiet period ${STALE_IDLE_REQUIRED}s)" >&2
        printf '%s' ALIVE; return
    fi
    echo "[gamelock]   verdict: STALE -- past the floor and every liveness signal is negative" >&2
    printf '%s' STALE
}

# ---- TELLING THE HOLDER -----------------------------------------------------
#
# The second half of the Wave 25 defect, and the half a bigger STALE_AFTER
# would not have touched: ALLOC got NO SIGNAL. A broken lock left nothing
# behind, so the wronged lane's next `release` printed "not held; nothing to
# release" and exited 0, and its `verify` said "no lock is held at all" as if
# it had simply never acquired one. Both statements were true and neither
# carried the fact that mattered.
#
# A breach record lives OUTSIDE $LOCK (which is what gets removed) and names
# the victim, so every later command that mentions that lane can shout. It is
# not cleared automatically -- a lane must `ack-breach` -- because the whole
# failure was a signal nobody had to acknowledge.
record_breach() {
    local owner="$1" age="$2" breaker="$3" verdict="$4"
    {
        echo "owner=$owner"
        echo "at=$(now)"
        echo "at_human=$(date '+%Y-%m-%d %H:%M:%S')"
        echo "held_for=$age"
        echo "breaker=$breaker"
        echo "verdict=$verdict"
        echo "exe_md5=$(exe_md5)"
    } > "$BREACH_FILE" 2>/dev/null
}

breach_field() { sed -n "s/^$1=//p" "$BREACH_FILE" 2>/dev/null | head -n 1; }

# Loud, unmissable, and returns 0 only when there IS a breach naming $1. Called
# from acquire (on grant), verify/assert-held, release, heartbeat and status --
# i.e. from every verb a wronged lane can plausibly run next.
breach_notice() {
    local who="$1" owner at age
    [ -f "$BREACH_FILE" ] || return 1
    at=$(breach_field at); case "$at" in ''|*[!0-9]*) at=0 ;; esac
    if [ "$at" -gt 0 ] && [ $(( $(now) - at )) -gt "$BREACH_KEEP" ]; then
        rm -f "$BREACH_FILE" 2>/dev/null; return 1
    fi
    owner=$(breach_field owner)
    [ "$owner" = "$who" ] || return 1
    age=$(breach_field held_for)
    echo "[gamelock] ****************************************************************" >&2
    echo "[gamelock] ** YOUR LOCK WAS BROKEN AS STALE -- lane '$who'" >&2
    echo "[gamelock] **   when    : $(breach_field at_human) (held ${age}s)" >&2
    echo "[gamelock] **   verdict : $(breach_field verdict)" >&2
    echo "[gamelock] **   breaker : $(breach_field breaker)" >&2
    echo "[gamelock] **   exe md5 at that moment: $(breach_field exe_md5)" >&2
    echo "[gamelock] ** ANY RUN OF YOURS SPANNING THAT MOMENT IS SUSPECT: another lane" >&2
    echo "[gamelock] ** was free to deploy over your exe and rewrite your logs. This is" >&2
    echo "[gamelock] ** exactly what happened to Wave 25 lane ALLOC, which lost a" >&2
    echo "[gamelock] ** measurement pair and was told by nothing." >&2
    echo "[gamelock] ** Compare the deployed exe against your own build before believing" >&2
    echo "[gamelock] ** any number, then: tools/game_dir_lock.sh ack-breach $who" >&2
    echo "[gamelock] ****************************************************************" >&2
    return 0
}

exe_md5() { md5sum "$GAME/OpenBA.exe" 2>/dev/null | cut -c1-32; }

# A lock past STALE_AFTER whose holder shows NO sign of life is assumed to
# belong to a lane that died without releasing. Say so LOUDLY when breaking one
# -- and leave a breach record, because saying it here reaches the BREAKER, not
# the victim.
break_if_stale() {
    local breaker="${1:-unknown}" age verdict owner
    age=$(lock_age)
    owner=$(lock_owner)
    verdict=$(lock_staleness)
    case "$verdict" in
        YOUNG|ALIVE) return 1 ;;
    esac
    echo "[gamelock] WARNING: breaking a $verdict lock held by '$owner'" >&2
    echo "[gamelock]   held for ${age}s (floor ${STALE_AFTER}s, ceiling ${STALE_HARD}s)." >&2
    if [ "$verdict" = "STALE_HARD" ]; then
        echo "[gamelock]   THIS IS THE WALL-CLOCK CEILING AND THE EVIDENCE WAS NOT CONSULTED." >&2
        echo "[gamelock]   If that lane is still alive, BOTH runs are now unreliable --" >&2
        echo "[gamelock]   discard them, and say so to the integrator." >&2
    fi
    record_breach "$owner" "$age" "$breaker" "$verdict"
    rm -rf "$LOCK"
    return 0
}

# Is a run IN PROGRESS right now? Lane UIW established the reliable test, after
# three clobbered runs and one COMPLETE, PLAUSIBLE, WRONG summary produced from
# another lane's binary:
#
#   `tasklist`-then-launch is NOT sufficient. It is a check followed by an
#   action with a gap in between, and another lane starts in the gap. That is a
#   TOCTOU race, and it failed three times in one session.
#
# The dependable test is whether the shared logs can be DELETED. Windows keeps
# them open for the life of the process, so a failed delete means a live run --
# and unlike tasklist it cannot go stale between the check and the answer,
# because it IS the action.
run_in_progress() {
    local f
    # Test seam: GAMELOCK_FAKE_RUN=1 forces the "a run is live" answer so the
    # messages below can be exercised without stranding a real process on the
    # shared game directory. Never set in normal use.
    [ "${GAMELOCK_FAKE_RUN:-0}" = "1" ] && return 0
    # Second test seam, needed because GAMELOCK_FAKE_RUN is fixed for the whole
    # process and `acquire` REFUSES to grant while a run is live -- so an env
    # var cannot express "the lock was granted, and THEN a run started", which
    # is the only interval in which the exe-identity watcher's clobber case
    # exists. A marker FILE can. Inert unless GAMELOCK_FAKE_RUN_FILE is set, so
    # it cannot fire in normal use. Never set either in normal use.
    [ -n "${GAMELOCK_FAKE_RUN_FILE:-}" ] && [ -f "$GAMELOCK_FAKE_RUN_FILE" ] && return 0
    for f in standalone_debug.log standalone_error.log; do
        [ -f "$GAME/$f" ] || continue
        # Try to move it aside and back: same open-handle test as a delete, but
        # non-destructive if it succeeds.
        if mv "$GAME/$f" "$GAME/$f.locktest" 2>/dev/null; then
            mv "$GAME/$f.locktest" "$GAME/$f" 2>/dev/null
        else
            return 0        # held open => a run is live
        fi
    done
    return 1
}

# ---- UNLOCKED RUNS AND ORPHANS ----------------------------------------------
#
# THE FOURTH VARIANT OF THE CLOBBER CLASS, 2026-09-09. `run_in_progress` has
# always been correct and has always gated the grab. What was missing is that
# NOTHING TOLD ANYONE ABOUT IT. The failure ran like this:
#
#   * the integrator's own run finished and reported completion, but its
#     process lingered ~50 minutes holding standalone_*.log and OpenBA.exe
#     open;
#   * `status` printed "free" -- TRUE, nobody held the lock -- and said nothing
#     about the live process, because its process advisory sat behind
#     `[ -d "$LOCK" ] || return 0` and so only ran when the lock was HELD;
#   * a lane's `acquire` could never grab (run_in_progress was true), fell
#     through to the generic wait, and printed `waiting for '(unknown)'`,
#     because with no lock directory `lock_owner` has nothing to read.
#
# Every individual component was working. The instrument's fault was reporting
# a TRUE statement ("free") that answered a different question from the one
# being asked ("can I use the directory?"). Those are two questions and this
# script now answers both, separately and by name.
#
# ---------------------------------------------------------------------------
# CORRECTION, 2026-09-09, AND IT IS THE INTEGRATOR'S OWN ERROR.
#
# This block originally continued: "the lane read that as lock contention,
# reported itself 'queued behind' another lane that in fact held nothing, and
# waited 15 minutes." THAT SENTENCE WAS FALSE, and the lane it described said
# so with evidence.
#
# The orphan was real and was demonstrated: a `cp` failed with "Device or
# resource busy" while `status` said free, and killing the stranded process
# fixed it. What was NOT established is that the orphan caused the OTHER lane's
# 15-minute wait. It did not. That lane's `acquire` printed
# `queued behind 'updateload'` and then `waiting for 'updateload'` -- both
# naming a real lane -- and this script can only produce those two strings when
# a live TICKET is ahead of you (line ~651) and when the lock is genuinely HELD
# (line ~701). In the orphan case there is no lock directory, so `lock_owner`
# has nothing to read and the string would have been `(unknown)`. The message
# the lane actually saw is therefore proof of ordinary, correct FIFO queueing
# behind a lane that really did hold the lock and really was running.
#
# So two conditions were conflated: the integrator's orphan (which blocked the
# INTEGRATOR'S deploy) and a genuine queue (which delayed the other lane). The
# fix below stands on its own evidence and is unaffected -- the silent-orphan
# gap is real whatever caused any particular wait -- but the attribution was
# wrong, and it was wrong in the most seductive direction: an explanation
# offered for SOMEONE ELSE'S anomaly that made the explainer the sole cause,
# tidily accounted for both lanes, and stopped the search. The tell was
# available at the time and was not looked at: the lane quoted a message
# naming a lane, and the orphan branch cannot produce a name.
#
# DIAGNOSTIC RULE THIS LEAVES BEHIND: the two conditions have DIFFERENT
# symptoms and must not be merged. An orphan fails a `cp` with "Device or
# resource busy" while `status` reads free, and never reaches a queue. A real
# queue never reaches a `cp` at all. Ask which one happened before explaining
# either.
# ---------------------------------------------------------------------------
#
# It still does NOT kill anything. A live exe nobody locked is either an orphan
# or a lane running unlocked, and from here those are indistinguishable --
# killing the second one destroys a real measurement. Facts, then a human.

# PIDs of every running OpenBA.exe, space separated. Empty if none.
live_exe_pids() {
    # Test seam: GAMELOCK_FAKE_PIDS supplies the answer so the staleness
    # decision can be exercised on both sides without stranding a real process
    # on the shared game directory. Empty string = "no live exe". Never set in
    # normal use. Same shape as GAMELOCK_FAKE_RUN above.
    if [ -n "${GAMELOCK_FAKE_PIDS+x}" ]; then printf '%s' "$GAMELOCK_FAKE_PIDS"; return; fi
    tasklist 2>/dev/null | grep -i 'OpenBA\.exe' | awk '{print $2}' | tr '\n' ' '
}

# Seconds since the game dir was last written, or -1 if it never was. Used to
# age a suspected orphan: a live run writes its logs continuously.
game_dir_idle() {
    local newest=0 f m
    # Test seam: GAMELOCK_FAKE_IDLE supplies the answer (seconds, or -1 for
    # "never written"). Never set in normal use.
    if [ -n "${GAMELOCK_FAKE_IDLE+x}" ]; then printf '%s' "$GAMELOCK_FAKE_IDLE"; return; fi
    for f in standalone_debug.log standalone_error.log standalone_crash.log OpenBA.exe; do
        [ -f "$GAME/$f" ] || continue
        m=$(date -r "$GAME/$f" +%s 2>/dev/null) || continue
        [ "$m" -gt "$newest" ] && newest=$m
    done
    [ "$newest" -eq 0 ] && { printf '%s' -1; return; }
    printf '%s' "$(( $(now) - newest ))"
}

# Print the facts about a live run that NO LANE HAS LOCKED. Callers decide what
# it means; this deliberately draws no conclusion and kills nothing.
report_unlocked_run() {
    local pids idle
    pids=$(live_exe_pids); idle=$(game_dir_idle)
    echo "[gamelock] UNLOCKED RUN: the game dir is IN USE but NO LANE HOLDS THE LOCK." >&2
    echo "[gamelock]   OpenBA.exe pid(s): ${pids:-<none visible>}" >&2
    echo "[gamelock]   last game-dir write: ${idle}s ago" >&2
    echo "[gamelock]   deployed exe: $(md5sum "$GAME/OpenBA.exe" 2>/dev/null | cut -c1-32)" >&2
    echo "[gamelock]   This is NOT lock contention -- nobody is holding the lock, so" >&2
    echo "[gamelock]   waiting will not clear it. It is one of exactly two things:" >&2
    echo "[gamelock]     (a) an ORPHAN: a finished run whose process outlived it. Windows" >&2
    echo "[gamelock]         keeps the exe and logs open for the life of the process, so" >&2
    echo "[gamelock]         every deploy fails with 'Device or resource busy'." >&2
    echo "[gamelock]     (b) somebody RUNNING UNLOCKED, mid-measurement." >&2
    echo "[gamelock]     (c) a THREAD-LESS HUSK: the process crashed or exited, every" >&2
    echo "[gamelock]         thread is gone and every HANDLE IS ALREADY RELEASED, but the" >&2
    echo "[gamelock]         pid lingers in the table because something still holds a" >&2
    echo "[gamelock]         handle to the process object. taskkill reports 'no running" >&2
    echo "[gamelock]         instance of the task' while tasklist still lists it." >&2
    echo "[gamelock]   (a) and (b) look identical from here. (c) DOES NOT -- measure it:" >&2
    # The (a)/(b)/(c) split is not a guess: the same non-destructive move-aside
    # test `run_in_progress` already runs on the logs answers the ONE question a
    # blocked lane actually has -- will my deploy fail? A live run (b), and an
    # orphan (a) whose threads are still alive, hold the exe open. A husk (c)
    # holds nothing.
    #
    # WAVE 19: this block previously asserted "Windows keeps the exe and logs
    # open for the life of the process, so EVERY DEPLOY FAILS". That premise is
    # FALSE for (c) -- handles die with the last thread, not with the pid. On
    # 2026-09-10 pid 17292 sat here for 22 minutes after an ACCESS_VIOLATION,
    # blocking three lanes, while the exe renamed freely the whole time. The
    # tool drew a conclusion its evidence (a pid in the table) did not support.
    # A PID IN THE PROCESS TABLE IS NOT A RUNNING PROGRAM.
    local held=""
    for f in OpenBA.exe standalone_debug.log standalone_error.log; do
        [ -f "$GAME/$f" ] || continue
        if mv "$GAME/$f" "$GAME/$f.holdtest" 2>/dev/null; then
            mv "$GAME/$f.holdtest" "$GAME/$f" 2>/dev/null
        else
            held="${held}${held:+ }$f"
        fi
    done
    if [ -n "$held" ]; then
        echo "[gamelock]   HANDLES HELD: $held" >&2
        echo "[gamelock]   -> a deploy WILL fail. This is (a) or (b), and they are" >&2
        echo "[gamelock]      indistinguishable from here. Do NOT kill a process you did" >&2
        echo "[gamelock]      not start -- killing (b) destroys a real run. If it is" >&2
        echo "[gamelock]      yours, kill it; otherwise ASK, and escalate with these" >&2
        echo "[gamelock]      numbers, not with a guess." >&2
    else
        echo "[gamelock]   HANDLES HELD: none (exe and logs both move aside freely)" >&2
        echo "[gamelock]   -> a deploy will SUCCEED. This is (c), a husk. It is not" >&2
        echo "[gamelock]      holding the game dir and waiting on it is waiting on" >&2
        echo "[gamelock]      nothing. PROCEED, and take the lock as usual." >&2
        echo "[gamelock]      (This does NOT prove no lane is mid-measurement -- it" >&2
        echo "[gamelock]      proves nothing is holding these three files right now.)" >&2
    fi
}

# ---- FIFO ticketing ---------------------------------------------------------
#
# `acquire` used to be a poll-and-grab spin, which is NOT a queue: whoever
# happened to poll in the window after a release won, and a lane that had been
# waiting 29 minutes had no priority over one that arrived a second ago. Lane
# UI3 lost a slot that way on 2026-09-08 after waiting an hour. More timeout
# does not fix that -- it is unfairness, not impatience.
#
# So a waiter takes a TICKET (its lane name, containing the epoch it first
# asked) and may only attempt the grab when it is at the head. A waiter TOUCHES
# its ticket every poll, so a lane that dies while queued stops refreshing and
# its ticket is ignored after TICKET_STALE seconds -- otherwise one dead waiter
# would deadlock every other lane, which is worse than the unfairness it fixes.
#
# Lanes running an older copy of this script take no ticket and still grab as
# before. That is the pre-existing behaviour, not a regression.
QUEUE="$GAME/.gamedir_queue"
TICKET_STALE=60

ticket_take() {
    local who="$1"
    mkdir -p "$QUEUE" 2>/dev/null
    ticket_sweep_garbage
    [ -f "$QUEUE/$who" ] || printf '%s' "$(now)" > "$QUEUE/$who"
}
ticket_touch() { touch "$QUEUE/$1" 2>/dev/null; }
ticket_drop()  { rm -f "$QUEUE/$1" 2>/dev/null; }

# A ticket a caller keeps ON TIMEOUT (see cmd_acquire below) so a requeue keeps
# its original epoch is, by design, no longer touched -- ticket_head ignores it
# once it passes TICKET_STALE, but nothing else ever removes the file. A lane
# that times out and never retries would otherwise leave it in $QUEUE forever.
# That is harmless to correctness (stale tickets are never counted) but is
# unbounded garbage, so sweep anything far past any realistic retry gap.
TICKET_GARBAGE_AFTER=$(( TICKET_STALE * 20 ))   # 20 min; no real retry waits that long
ticket_sweep_garbage() {
    local f age
    [ -d "$QUEUE" ] || return 0
    for f in "$QUEUE"/*; do
        [ -f "$f" ] || continue
        age=$(( $(now) - $(date -r "$f" +%s 2>/dev/null || echo 0) ))
        case "$age" in ''|*[!0-9]*) age=0 ;; esac
        [ "$age" -gt "$TICKET_GARBAGE_AFTER" ] && rm -f "$f" 2>/dev/null
    done
}

# Echo the lane name at the head of the queue: the LIVE ticket (refreshed within
# TICKET_STALE) with the earliest first-asked epoch. Empty if nobody is queued.
#
# DIGITS GUARD, 2026-09-10: `cmd || echo 0` only fires if `cmd` FAILS. A ticket
# file that EXISTS but is EMPTY (e.g. a `ticket_take` interrupted between
# `mkdir -p` and the `printf` that fills it in, or a filesystem hiccup) makes
# `cat` succeed with empty output, so `asked=""`. The numeric test below then
# THROWS ("integer expression expected"), the `if` reads as false, and that
# ticket is silently never chosen as head -- forever, while any other lane
# holds a readable ticket. Observed in Wave 11: a lane's own ticket went blind
# this way, `ticket_head` fell through to the wrong branch, and the lane's log
# read like queued-behind-nobody. `date -r` failing has the same shape (a
# successful-but-empty read), so both are guarded the same way: force to a
# sane integer before the numeric tests use it.
ticket_head() {
    local best="" best_at="" f name asked age
    [ -d "$QUEUE" ] || return 0
    for f in "$QUEUE"/*; do
        [ -f "$f" ] || continue
        name=$(basename "$f")
        age=$(( $(now) - $(date -r "$f" +%s 2>/dev/null || echo 0) ))
        case "$age" in ''|*[!0-9]*) age=0 ;; esac
        [ "$age" -gt "$TICKET_STALE" ] && continue      # waiter died or left
        asked=$(cat "$f" 2>/dev/null || echo 0)
        case "$asked" in ''|*[!0-9]*) asked=0 ;; esac
        if [ -z "$best_at" ] || [ "$asked" -lt "$best_at" ]; then
            best_at=$asked; best=$name
        fi
    done
    printf '%s' "$best"
}

cmd_acquire() {
    local who="${1:?lane name required}" timeout="${2:-900}" waited=0 head
    mkdir -p "$GAME" 2>/dev/null
    ticket_take "$who"
    while :; do
        ticket_touch "$who"
        # Wait your turn. An empty head means nobody else is queued.
        head=$(ticket_head)
        if [ -n "$head" ] && [ "$head" != "$who" ]; then
            if [ "$waited" -ge "$timeout" ]; then
                echo "[gamelock] DENIED -- TIMEOUT after ${timeout}s; queued behind '$head'."
                echo "[gamelock] TIMEOUT after ${timeout}s; queued behind '$head'." >&2
                # Do NOT ticket_drop here. Dropping would make a requeuing
                # `acquire` call ticket_take() fresh, which stamps a NEW asked
                # epoch and sends the longest-waiting lane to the BACK of the
                # queue -- exactly the unfairness FIFO ticketing exists to
                # prevent (see the block comment above). Leave the ticket in
                # place: a caller that retries soon keeps its original place;
                # a caller that never retries has its ticket age past
                # TICKET_STALE (ignored by ticket_head) and eventually swept
                # by ticket_sweep_garbage.
                return 1
            fi
            [ $(( waited % 30 )) -eq 0 ] &&
                echo "[gamelock] queued behind '$head' (${waited}s/${timeout}s)..." >&2
            sleep 5; waited=$(( waited + 5 ))
            continue
        fi
        # Take the lock only if nobody is mid-run. A lane that crashed without
        # releasing leaves no open handle, so this does not deadlock on it.
        if ! run_in_progress && mkdir "$LOCK" 2>/dev/null; then
            printf '%s' "$who"     > "$LOCK/owner"
            printf '%s' "$(now)"   > "$LOCK/started"
            touch "$LOCK/heartbeat" 2>/dev/null
            ticket_drop "$who"
            # If THIS lane is the one whose lock was broken, this is the first
            # verb it runs afterwards -- so it is the first place it can be
            # told. (ALLOC's acquire, release and verify were all silent.)
            breach_notice "$who"
            echo "[gamelock] ACQUIRED by $who"
            echo "[gamelock] GRANT $who"
            return 0
        fi
        if [ "$(lock_owner)" = "$who" ]; then
            ticket_drop "$who"
            echo "[gamelock] already held by $who (re-entrant, not re-acquired)"
            return 0
        fi
        break_if_stale "$who" && continue
        # THE CASE THAT USED TO PRINT `waiting for '(unknown)'`: no lock
        # directory exists, so nobody holds it, yet the grab above was refused
        # -- which can only mean run_in_progress() said a run is live. Waiting
        # cannot help, so say so immediately and once, rather than spinning to
        # timeout against a phantom holder.
        if [ ! -d "$LOCK" ] && run_in_progress; then
            echo "[gamelock] DENIED -- the game dir is in use by an UNLOCKED run." >&2
            report_unlocked_run
            ticket_drop "$who"
            return 1
        fi
        if [ "$waited" -ge "$timeout" ]; then
            # Printed on BOTH streams on purpose. A caller that pipes this
            # command loses the exit status (see the DEFEATED BY A PIPE note in
            # the usage block), so the refusal has to be visible on stdout too.
            echo "[gamelock] DENIED -- TIMEOUT after ${timeout}s; '$(lock_owner)' still holds it."
            echo "[gamelock] Do NOT deploy anyway -- that is the clobber this prevents."
            echo "[gamelock] TIMEOUT after ${timeout}s; '$(lock_owner)' still holds it." >&2
            echo "[gamelock] Do NOT deploy anyway -- that is the clobber this prevents." >&2
            # Same reasoning as the other TIMEOUT branch above: keep the
            # ticket so a requeue preserves the original asked epoch instead
            # of restamping now() and losing FIFO priority.
            return 1
        fi
        [ $(( waited % 30 )) -eq 0 ] &&
            echo "[gamelock] waiting for '$(lock_owner)' (${waited}s/${timeout}s)..." >&2
        sleep 5; waited=$(( waited + 5 ))
    done
}

cmd_release() {
    local who="${1:?lane name required}"
    if [ ! -d "$LOCK" ]; then
        # "nothing to release" was TRUE for ALLOC and told it nothing. If this
        # lane's lock was taken away, say so here and FAIL, because a lane that
        # reaches its own release believing it still held the lock has a run
        # whose validity is now in question.
        if breach_notice "$who"; then
            echo "[gamelock] release: your lock was BROKEN, not released by you." >&2
            return 1
        fi
        echo "[gamelock] not held; nothing to release"
        return 0
    fi
    local owner; owner=$(lock_owner)
    if [ "$owner" != "$who" ]; then
        # Refuse rather than steal. Releasing someone else's lock would let two
        # lanes into the directory at once believing they were serialized.
        echo "[gamelock] REFUSING: lock is held by '$owner', not '$who'." >&2
        return 1
    fi
    # The orphan is BORN HERE. On 2026-09-09 a run reported completion, its
    # holder released, and its process lived another 50 minutes holding the exe
    # open -- blocking every later deploy while the lock read "free". Releasing
    # while your own run is still live is always a mistake, and this is the last
    # moment anyone can cheaply notice. It warns rather than refuses: a lane that
    # cannot kill its process must still be able to give the lock back.
    if run_in_progress; then
        echo "[gamelock] WARNING: releasing while a run is STILL LIVE." >&2
        echo "[gamelock]   OpenBA.exe pid(s): $(live_exe_pids)" >&2
        echo "[gamelock]   Your process outliving your run is what creates an ORPHAN:" >&2
        echo "[gamelock]   the next lane's deploy fails with 'Device or resource busy'" >&2
        echo "[gamelock]   while this lock reads free, and it has no way to tell why." >&2
        echo "[gamelock]   Kill it before you walk away. Note BANISHED_KILL_AT_FRAME" >&2
        echo "[gamelock]   reports completion WITHOUT guaranteeing the process exited." >&2
    fi
    rm -rf "$LOCK"
    echo "[gamelock] released by $who"
}

cmd_orphans() {
    if ! run_in_progress; then
        echo "[gamelock] no live run: the game dir is writable."
        local pids; pids=$(live_exe_pids)
        [ -n "$pids" ] && echo "[gamelock] (note: OpenBA.exe pid(s) $pids exist but hold no game-dir handle)"
        return 0
    fi
    if [ -d "$LOCK" ]; then
        echo "[gamelock] a run is live and '$(lock_owner)' holds the lock -- NORMAL. Leave it alone."
        return 0
    fi
    report_unlocked_run
    return 1
}

cmd_status() {
    local who="${1:-}"
    [ -n "$who" ] && breach_notice "$who"
    if [ -d "$LOCK" ]; then
        echo "[gamelock] HELD by '$(lock_owner)' for $(( $(now) - $(lock_started) ))s"
        # The evidence that decides expiry, printed by the command people
        # actually run. Before Wave 26 this tool COMPUTED all of it here and
        # the expiry path next door ignored every line of it.
        echo "[gamelock] staleness verdict: $(lock_staleness)"
    elif run_in_progress; then
        # "free" alone was TRUE here and cost a lane 15 minutes on 2026-09-09,
        # because the honest answer to "is the lock free?" is not the answer to
        # "can I use the directory?". Both are printed, in that order.
        echo "[gamelock] free -- BUT THE GAME DIR IS IN USE (see below); a deploy will FAIL"
        report_unlocked_run
    else
        echo "[gamelock] free"
    fi
    if [ -f "$GAME/OpenBA.exe" ]; then
        echo "[gamelock] deployed exe: $(md5sum "$GAME/OpenBA.exe" | cut -c1-32)  $(date -r "$GAME/OpenBA.exe" '+%Y-%m-%d %H:%M:%S')"
        echo "[gamelock]   compare against your own build before believing any run."
    fi
    local n
    n=$(ls "$GAME"/WinData/*.pkm 2>/dev/null | wc -l)
    echo "[gamelock] mounted .pkm mods: $n  (changes the INPUT of every run; not covered by this lock)"

    # ---- Idle advisory -------------------------------------------------------
    #
    # The third variant of the clobber class, raised by lane UI3 on 2026-09-08:
    # `verify` catches "I never got it" and the stdout refusal catches "I misread
    # the grant", but neither catches "a lane holds it and is no longer using
    # it". A queued lane then stares at a 30-minute hold with no way to tell a
    # dead holder from one legitimately analysing between launches -- and
    # guessing wrong IS the clobber this script exists to prevent.
    #
    # So this prints FACTS and refuses to draw the conclusion. It deliberately
    # does NOT auto-break: holding the lock across several launches with analysis
    # gaps between them is correct usage and looks identical from outside.
    [ -d "$LOCK" ] || return 0
    local proc idle newest
    proc=$(tasklist 2>/dev/null | grep -c -i 'OpenBA\.exe')
    newest=0
    for f in standalone_debug.log standalone_error.log standalone_crash.log OpenBA.exe; do
        [ -f "$GAME/$f" ] || continue
        local m; m=$(date -r "$GAME/$f" +%s 2>/dev/null) || continue
        [ "$m" -gt "$newest" ] && newest=$m
    done
    if [ "$newest" -gt 0 ]; then
        idle=$(( $(now) - newest ))
        echo "[gamelock] holder activity: OpenBA.exe processes=$proc, last game-dir write ${idle}s ago"
        if [ "$proc" -eq 0 ] && [ "$idle" -ge 300 ]; then
            echo "[gamelock] ADVISORY: no running exe and no write for ${idle}s. That is CONSISTENT WITH"
            echo "[gamelock]   (a) the holder analysing between launches -- correct usage, do not disturb -- or"
            echo "[gamelock]   (b) a holder that has finished or died. These look IDENTICAL from here."
            echo "[gamelock]   Do NOT break the lock on this line. ASK the holder ('$(lock_owner)'), and"
            echo "[gamelock]   escalate to a human with these numbers rather than with a guess."
        fi
    fi
}

# ---- THE PER-RUN IDENTITY CHECK, MADE AUTOMATIC -----------------------------
#
# Wave 25 lane ALLOC md5-verified the deployed exe against its own build
# immediately before every launch, and all six of its completed runs recorded
# the same hash. THAT CHECK IS THE ONLY REASON "was my measurement
# contaminated?" was answerable at all -- without it the question would have
# been an argument between two plausible stories. It was per-lane discipline,
# which means it protected exactly one lane.
#
# So `run` now samples the deployed exe's md5 for the whole life of the wrapped
# command and reports what it saw. The DISCRIMINATOR matters: the exe changing
# is NORMAL -- `tools/build.sh run` deploys, that is the point. What is not normal is
# the exe changing WHILE A RUN IS LIVE. `run_in_progress` already answers "is a
# run live" by the one test that cannot go stale between the check and the
# answer, so a change seen after a live sample is a clobber and a change seen
# between two idle samples is a deploy.
#
# RESIDUAL, STATED: if the game exits between two samples and a deploy lands in
# the same interval, the previous sample still reads live and this reports a
# clobber that was not one. It errs toward shouting. The full sample table is
# printed with the summary (and copied into --snapshot), so the claim is always
# checkable rather than a bare verdict.
run_watch_loop() {
    local who="$1" tsv="$2" stop="$3" iv="${GAMELOCK_WATCH_INTERVAL:-10}"
    local prev="" prev_live=0 cur live owner
    printf 'time\texe_md5\trun_live\towner\n' > "$tsv"
    while [ ! -f "$stop" ]; do
        cur=$(exe_md5); owner=$(lock_owner)
        if run_in_progress; then live=1; else live=0; fi
        printf '%s\t%s\t%s\t%s\n' "$(now)" "${cur:-<absent>}" "$live" "$owner" >> "$tsv"
        if [ -n "$prev" ] && [ "$cur" != "$prev" ] && [ "$prev_live" = 1 ]; then
            echo "[gamelock] !! CLOBBER: the deployed exe CHANGED while your run was live." >&2
            echo "[gamelock] !!   was $prev  ->  now ${cur:-<absent>}" >&2
            echo "[gamelock] !!   Your numbers are NOT from your binary. Discard this run." >&2
            echo "CLOBBER" >> "$tsv.flag"
        fi
        if [ ! -d "$LOCK" ] || [ "$owner" != "$who" ]; then
            echo "[gamelock] !! LOCK LOST: '$who' no longer owns the lock (now: '${owner:-<none>}')." >&2
            echo "[gamelock] !!   Another lane may deploy over you at any moment." >&2
            echo "LOCKLOST" >> "$tsv.flag"
        fi
        # Attributable proof of life, so a long legitimate run is never expired
        # as stale the way ALLOC's was at 1802s.
        [ -d "$LOCK" ] && touch "$LOCK/heartbeat" 2>/dev/null
        prev="$cur"; prev_live="$live"
        sleep "$iv"
    done
}

run_watch_summary() {
    local tsv="$1" n distinct first last flags
    n=$(( $(wc -l < "$tsv" 2>/dev/null || echo 1) - 1 ))
    distinct=$(tail -n +2 "$tsv" 2>/dev/null | cut -f2 | sort -u | wc -l)
    first=$(tail -n +2 "$tsv" 2>/dev/null | head -n 1 | cut -f2)
    last=$(tail -n 1 "$tsv" 2>/dev/null | cut -f2)
    flags=$(sort -u "$tsv.flag" 2>/dev/null | tr '\n' ' ')
    echo "[gamelock] exe identity: ${n} samples, ${distinct} distinct md5 (first=${first:-?} last=${last:-?})"
    if [ -n "$flags" ]; then
        echo "[gamelock] exe identity: FLAGGED -- $flags"
        echo "[gamelock]   the sample table is in $tsv"
        return 1
    fi
    echo "[gamelock] exe identity: clean -- no change while a run was live, lock never lost"
    return 0
}

# The safe path, made the easy one. Acquire, run, release -- with the acquire's
# exit status checked HERE, where no caller's pipe can discard it. Anything the
# caller wants to pipe is this command's output, not the acquire's.
#
#   tools/game_dir_lock.sh run lane-FOO 900 -- tools/build.sh run
#
cmd_run() {
    local who="${1:?lane name required}" timeout="${2:-900}" snapdir=""
    shift 2
    if [ "${1:-}" = "--snapshot" ]; then snapdir="${2:?--snapshot needs a directory}"; shift 2; fi
    [ "${1:-}" = "--" ] && shift
    [ $# -gt 0 ] || { echo "[gamelock] run: no command given" >&2; return 2; }

    if ! cmd_acquire "$who" "$timeout"; then
        echo "[gamelock] run: ABORTING, the lock was never granted to '$who'." >&2
        return 1
    fi
    # Release on INT/TERM/HUP as well as on the normal path. Without this, a
    # lane killed mid-run leaves the lock held until break_if_stale expires it,
    # and from every other lane's side that is indistinguishable from a lane
    # legitimately holding it for a very long time -- which is what tonight's
    # 1000-second holds looked like from the outside.
    local wdir wtsv wstop wpid
    wdir="${TMPDIR:-/tmp}/gamelock_watch.$$"; mkdir -p "$wdir" 2>/dev/null
    wtsv="$wdir/exe_identity.tsv"; wstop="$wdir/stop"
    rm -f "$wstop" "$wtsv.flag" 2>/dev/null
    run_watch_loop "$who" "$wtsv" "$wstop" &
    wpid=$!
    trap 'touch "$wstop" 2>/dev/null; cmd_release "$who" >/dev/null 2>&1; trap - INT TERM HUP; kill -INT $$' INT TERM HUP
    "$@"
    local rc=$?
    trap - INT TERM HUP
    touch "$wstop" 2>/dev/null
    wait "$wpid" 2>/dev/null
    run_watch_summary "$wtsv"
    local identity_rc=$?
    # Snapshot the logs BEFORE releasing. The lock protects the deploy+run
    # window; it says nothing about the ANALYSIS window, and reading
    # standalone_*.log after the release reads whichever lane ran next. That
    # fooled a whole analysis on 2026-09-09: three reads of "the" debug log were
    # three different processes, and another lane's verification-mode banner was
    # misread as a default-on regression in the merged tree. The tell -- object
    # pointers differing across reads -- was in the output and was walked past
    # twice.
    if [ -n "$snapdir" ]; then
        mkdir -p "$snapdir" 2>/dev/null
        local f
        for f in standalone_debug.log standalone_error.log standalone_crash.log                  standalone_boot_state.txt; do
            [ -f "$GAME/$f" ] && cp "$GAME/$f" "$snapdir/$f" 2>/dev/null
        done
        # The identity table goes WITH the logs. A snapshot that proves what you
        # ran is worth little beside one that does not say which binary produced
        # it -- that is the whole lesson of ALLOC's per-launch md5.
        cp "$wtsv" "$snapdir/exe_identity.tsv" 2>/dev/null
        [ -f "$wtsv.flag" ] && cp "$wtsv.flag" "$snapdir/exe_identity.FLAGGED" 2>/dev/null
        echo "[gamelock] logs snapshotted to $snapdir (taken BEFORE release, so they are yours)"
    fi
    # Keep the sample table on disk when it flagged something: the message
    # above names its path, and deleting the evidence a warning points at is
    # how a warning becomes unfalsifiable.
    [ "$identity_rc" -eq 0 ] && rm -rf "$wdir" 2>/dev/null
    cmd_release "$who"
    # A contaminated run must not exit 0. The wrapped command succeeding says
    # nothing about whether it measured your binary.
    if [ "$identity_rc" -ne 0 ] && [ "$rc" -eq 0 ]; then
        echo "[gamelock] run: the command succeeded but the identity check FAILED -- exiting 7." >&2
        return 7
    fi
    return $rc
}

# Returns 0 only if the lock is held BY YOU, right now. Cheap enough to call
# immediately before a launch: it answers "is the exe about to run still mine?"
# in the one way a discarded exit code cannot fake. Also reachable as
# `assert-held` (same command, different verb) for a caller that wants a name
# that cannot be misread as anything but a gate: `acquire` grants a lock,
# `assert-held` is the thing you put directly at a deploy step so the step
# itself refuses to proceed on a stolen or expired lock, regardless of what
# any earlier `acquire`'s exit code was read as.
cmd_verify() {
    local who="${1:?lane name required}" owner mine="${2:-}"
    if [ ! -d "$LOCK" ]; then
        echo "[gamelock] VERIFY FAILED: no lock is held at all (expected '$who')." >&2
        breach_notice "$who"
        return 1
    fi
    owner=$(lock_owner)
    if [ "$owner" != "$who" ]; then
        echo "[gamelock] VERIFY FAILED: held by '$owner', not '$who'." >&2
        breach_notice "$who"
        return 1
    fi
    breach_notice "$who"
    # Ownership is necessary and not sufficient: the exe under the lock can
    # have been replaced before you took it. If the caller names its own build,
    # compare -- this is Wave 25 lane ALLOC's per-launch md5 discipline, which
    # is the ONLY reason the contamination question was answerable at all,
    # made part of the gate instead of per-lane habit.
    if [ -n "$mine" ]; then
        local want have
        want=$(md5sum "$mine" 2>/dev/null | cut -c1-32)
        have=$(exe_md5)
        if [ -z "$want" ]; then
            echo "[gamelock] VERIFY FAILED: cannot read your build at '$mine'." >&2
            return 1
        fi
        if [ "$want" != "$have" ]; then
            echo "[gamelock] VERIFY FAILED: the DEPLOYED exe is not your build." >&2
            echo "[gamelock]   yours   : $want  ($mine)" >&2
            echo "[gamelock]   deployed: ${have:-<absent>}" >&2
            echo "[gamelock]   Deploy before launching, or you will measure somebody else's code." >&2
            return 1
        fi
        echo "[gamelock] verified: held by $who AND the deployed exe is your build ($want)"
        return 0
    fi
    echo "[gamelock] verified: still held by $who"
}


# Say "I am still here" without taking or giving up the lock. This is the ONE
# liveness signal attributable to the HOLDER rather than to the machine, so a
# lane that holds the lock across a long analysis gap should call it (or just
# use `run`, which touches it automatically every watch interval).
cmd_heartbeat() {
    local who="${1:?lane name required}" owner
    if [ ! -d "$LOCK" ]; then
        echo "[gamelock] heartbeat: no lock is held (expected '$who')." >&2
        breach_notice "$who"
        return 1
    fi
    owner=$(lock_owner)
    if [ "$owner" != "$who" ]; then
        echo "[gamelock] heartbeat: lock is held by '$owner', not '$who'." >&2
        breach_notice "$who"
        return 1
    fi
    touch "$LOCK/heartbeat" 2>/dev/null
    echo "[gamelock] heartbeat recorded for $who"
}

# Clear a breach record once the lane has SEEN it. Deliberately manual: the
# whole Wave 25 defect was a signal nobody had to acknowledge.
cmd_ack_breach() {
    local who="${1:?lane name required}" owner
    if [ ! -f "$BREACH_FILE" ]; then
        echo "[gamelock] no breach record to acknowledge."
        return 0
    fi
    owner=$(breach_field owner)
    if [ "$owner" != "$who" ]; then
        echo "[gamelock] REFUSING: the breach record names '$owner', not '$who'." >&2
        return 1
    fi
    rm -f "$BREACH_FILE" 2>/dev/null
    echo "[gamelock] breach acknowledged and cleared by $who"
}

# Test seam: GAMELOCK_NO_MAIN=1 sources this file for its functions (used by
# tools/game_dir_lock_selftest.sh to exercise ticket_head/ticket_take/etc.
# directly) without running the dispatch below. Never set in normal use. Same
# shape as the existing GAMELOCK_FAKE_RUN seam above.
if [ "${GAMELOCK_NO_MAIN:-0}" != "1" ]; then
case "${1:-}" in
    acquire) shift; cmd_acquire "$@" ;;
    run)     shift; cmd_run "$@" ;;
    verify|assert-held) shift; cmd_verify "$@" ;;
    release) shift; cmd_release "$@" ;;
    heartbeat) shift; cmd_heartbeat "$@" ;;
    ack-breach) shift; cmd_ack_breach "$@" ;;
    status)  shift; cmd_status "$@" ;;
    orphans) cmd_orphans ;;
    *)       usage ;;
esac
fi
