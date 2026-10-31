#!/usr/bin/env bash
# tools/disaster_report.sh -- one-shot verdict table for a disaster soak run.
#
# WHY: four disaster lanes (fire / plague / crop-infestation / tornado) each emit their
# own trace tags into one very large log. Eyeballing that is how this project has
# repeatedly published a wrong root cause. The distinction this script exists to make is
# NOT-ARMED vs ARMED-BUT-INERT: "the gate was off" and "the feature is broken" look
# identical in a log tail, and confusing them has cost real time here more than once.
#
# It therefore never reports a bare zero for a tag it never saw. `tag never appeared` and
# `tag appeared, count 0` are different diagnoses and are printed differently.
#
# USAGE
#   tools/disaster_report.sh                  # read the default logs in the game dir
#   tools/disaster_report.sh <logfile>...     # read specific log files instead
#   BANISHED_DIR=/some/dir tools/disaster_report.sh
#
# EXIT CODES (so the caller can branch without parsing)
#   0  all four disasters WORKING
#   1  one or more PARTIAL / ARMED-BUT-INERT / NOT-ARMED
#   2  crash detected (standalone_crash.log non-empty) -- nothing else is trustworthy
#   3  log missing or empty
#
# TAG FIDELITY NOTE. [Plague] / [GUL::DiseaseList], [CropDisease] and [Tornado] are parsed
# EXACTLY, on the event word, from formats their lanes supplied. [Fire] is STILL parsed
# from behaviour keywords -- that lane had not finalised its format -- so for Fire the
# script prints the distinct line shapes it could not match, making a format mismatch
# visible unmatched output rather than a silent zero.
#
# [Tornado] goes to stderr (standalone_error.log), which is one reason both logs are read
# by default. Its spawn and damage records WRAP over several physical lines whose
# continuations carry no tag, so tornado fields are harvested BY KEY regardless of which
# physical line they landed on; only the event counting keys off the tag. Verified to give
# identical numbers for the wrapped and single-line forms.
#
# CENTURY-SCALE TUNING -- the thing most likely to cause a wrong reading. The authored
# intervals are: crop infestation 150-200 YEARS, fire 20-40 years, and half the building
# templates have _years=0 and can NEVER ignite organically. So an organic roll that never
# fires is CORRECT retail behaviour, not a defect. This script therefore reports a zero
# as ARMED-BUT-INERT only when a FORCED trigger (BANISHED_FORCE_*) was actually armed;
# otherwise it reports NOT-ARMED and says the organic arm is untestable in a session.
# The one exception is CropDisease STARTUP: CropField/Orchard/Pasture are confirmed to
# carry a DiseaseDescription, so live=1 with zero STARTUP lines is a real defect.
#
# Robust to CRLF (every line is stripped) and to very large logs (ONE awk pass over all
# inputs; no repeated greps).

set -u

GAME_DIR="${BANISHED_DIR:-C:/GOG Games/Banished}"

if [ "$#" -gt 0 ]; then
    LOGS=("$@")
    # Explicit logs: look for the crash log of THAT run -- beside the first log given --
    # not the game dir's. Otherwise an unrelated crash from a different run blocks the
    # analysis of the logs you actually asked about.
    CRASH_LOG="$(dirname "$1")/standalone_crash.log"
else
    LOGS=("$GAME_DIR/standalone_debug.log" "$GAME_DIR/standalone_error.log")
    CRASH_LOG="$GAME_DIR/standalone_crash.log"
fi

# ---------------------------------------------------------------------------
# 0. Crash log FIRST. Per CLAUDE.md this is the thing to read before anything
#    else -- [REDZONE-DUMP] lines in the debug log can mislead, and EXEC-of-0
#    means a null function pointer was called.
# ---------------------------------------------------------------------------
if [ -s "$CRASH_LOG" ]; then
    echo "############################################################"
    echo "## CRASH DETECTED -- read this before any verdict below.  ##"
    echo "############################################################"
    echo "   $CRASH_LOG"
    echo
    sed 's/\r$//' "$CRASH_LOG" | head -60
    echo
    if grep -qa 'EXEC-of-0' "$CRASH_LOG"; then
        echo ">> EXEC-of-0 present: a NULL FUNCTION POINTER was called."
        echo "   That is an unregistered / unwired vtable slot or dispatch slot,"
        echo "   not a logic bug. Symbolize with:"
        echo "     nm build/cmake-debug/OpenBA.exe   (image base 0x140000000)"
    fi
    echo
    echo "No disaster verdicts are reported: a crashed run proves nothing about"
    echo "whether a disaster chain works."
    exit 2
fi

# ---------------------------------------------------------------------------
# 1. Input sanity.
# ---------------------------------------------------------------------------
PRESENT=()
for f in "${LOGS[@]}"; do
    [ -s "$f" ] && PRESENT+=("$f")
done
if [ "${#PRESENT[@]}" -eq 0 ]; then
    echo "NO LOG DATA"
    for f in "${LOGS[@]}"; do
        if [ ! -f "$f" ]; then echo "  missing: $f"; else echo "  empty:   $f"; fi
    done
    echo
    echo "Nothing to analyse. Run the soak first, or pass log paths explicitly."
    exit 3
fi

echo "=============================================================================="
echo " DISASTER SOAK REPORT"
echo "=============================================================================="
for f in "${PRESENT[@]}"; do
    printf ' log: %s (%s bytes)\n' "$f" "$(wc -c < "$f" | tr -d ' ')"
done
echo " crash log: EMPTY (clean)"
echo

# ---------------------------------------------------------------------------
# 2. Single awk pass. Everything below is computed in one traversal.
# ---------------------------------------------------------------------------
awk '
BEGIN {
    # The disaster env surface, as reported by the harness + the four lanes. Listing it
    # explicitly means a gate that never logged at all is VISIBLE as "not observed",
    # instead of being invisible -- a silently-off gate is the likeliest way to misread
    # a soak. Value: 1 = forced-trigger gate, 3 = trace/demo gate. (The four
    # BANISHED_LIVE_* feature gates graduated to unconditional code, ENDGAME C.2.)
    split("BANISHED_FORCE_FIRE BANISHED_FORCE_TORNADO BANISHED_FORCE_SICK BANISHED_FORCE_INFEST", a, " ")
    for (i in a) gate_roster[a[i]] = 1
    split("BANISHED_CROPDISEASE_TRACE BANISHED_PLAGUE_TRACE BANISHED_TORNADO_TRACE BANISHED_BUILD_DEMO_WELL BANISHED_BUILD_DEMO_HOSPITAL", a, " ")
    for (i in a) gate_roster[a[i]] = 3
    gate_order = "BANISHED_FORCE_FIRE BANISHED_FORCE_TORNADO BANISHED_FORCE_SICK BANISHED_FORCE_INFEST " \
                 "BANISHED_CROPDISEASE_TRACE BANISHED_PLAGUE_TRACE BANISHED_TORNADO_TRACE BANISHED_BUILD_DEMO_WELL BANISHED_BUILD_DEMO_HOSPITAL"
}
function note(tag) { seen[tag] = 1 }

# Numeric value following "key=" on a line, or -1 when absent. Used so a damage line
# reading "tree=3" contributes 3, not 1.
function getnum(s, key,   t) {
    if (s !~ key "=") return -1
    t = s; sub(".*" key "=", "", t); sub(/[^0-9.-].*/, "", t)
    return (t == "" ? -1 : t + 0)
}

# Hex string (with or without 0x) -> number. Hand-rolled rather than strtonum() so this
# does not depend on the awk being gawk.
function hex2dec(h,   i, c, v, d) {
    sub(/^0[xX]/, "", h); h = tolower(h); v = 0
    for (i = 1; i <= length(h); i++) {
        c = substr(h, i, 1); d = index("0123456789abcdef", c) - 1
        if (d < 0) break
        v = v * 16 + d
    }
    return v
}

{ sub(/\r$/, "") }                      # CRLF hardening, every line

# ---- env-gate echoes -------------------------------------------------------
# Each lane logs its own gate state at startup. Parse that rather than guessing:
# a silently-off gate is the single most likely way to misread these results.
/BANISHED_/ && /DISEASE|PLAGUE|FIRE|TORNADO|CROP|DISASTER|SICK|INFEST|WELL|HOSPITAL/ {
    if (!(env_seen[$0]++)) envlines[++nenv] = $0
    # Which named gates were OBSERVED at all, and which read as on. "unset"/"disabled"
    # is the wording the harness uses for off; anything else counts as armed.
    off = ($0 ~ /unset|disabled|OFF|not set/)
    for (g in gate_roster) if (index($0, g)) { gate_saw[g] = 1; if (!off) gate_on[g] = 1 }
}

# Forced-trigger harness. Century-scale organic tuning means a forced trigger is the
# ONLY way most of these fire in a session, so whether one was armed decides whether a
# zero is a defect or simply an untested arm.
/\[Disaster\]/ { note("harness"); if (!(harn_seen[$0]++) && nharn < 8) harnlines[++nharn] = $0 }

# ==========================================================================
# PLAGUE  (formats known exactly -- this lane wrote them)
# ==========================================================================
/\[GUL::DiseaseList\]/ {
    note("plague_load")
    if (index($0, "OFF —") || index($0, "OFF -")) dl_off = 1
    if (index($0, "gw+0xa8=")) dl_result = $0
    if (index($0, "SKIP"))     dl_skip = $0
    if (index($0, "<== ZERO")) { dl_zero++; if (dl_zero <= 4) dl_zero_line[dl_zero] = $0 }
    if ($0 ~ /diseases=/) {
        n = $0; sub(/.*diseases=/, "", n); sub(/[^0-9-].*/, "", n); dl_count = n
    }
}
/\[Plague\]/ {
    note("plague")

    # Dispatch on the EVENT WORD, not on a substring search. "SPREAD ... -> INFECTING"
    # contains "INFECT", and a substring parser silently books it as an infection and
    # loses the spread -- exactly the kind of miscount this script exists to prevent.
    ev = $0; sub(/.*\[Plague\][ \t]+/, "", ev); sub(/[ \t:].*/, "", ev)

    if (ev == "BOOT") {
        if (index($0, "careMult")) {
            boot_tuning = $0
            v = $0; sub(/.*careMult\(desc\+0x128\)=/, "", v); sub(/[ 	].*/, "", v)
            care_mult = v
            if (index($0, "<== ZERO")) care_mult_zero = 1
        }
        if (index($0, "hospital census") || index($0, "hospital scan")) {
            # DEFERRED / GAVE UP are NOT answers -- the census could not search yet.
            # Treating them as "no hospital" would be exactly the false assertion of
            # absence this probe was rewritten to avoid.
            if (index($0, "DEFERRED"))      { hosp_deferred = $0; hosp_defer_n++ }
            else if (index($0, "GAVE UP"))  { hosp_gaveup = $0 }
            else {
                boot_hosp = $0
                v = $0; sub(/.*matchingHealerProfession=/, "", v); sub(/[^0-9-].*/, "", v)
                hosp_match = v
                if (index($0, "NO HOSPITAL")) no_hospital = 1
            }
        }
        next
    }
    if (ev == "CENSUS")   { census = $0; ncensus++; next }
    if (ev == "ROLL")     { roll_fired++; next }
    if (ev == "CATCH") {
        if (index($0, "NO BUCKET"))            catch_nobucket++
        else if (index($0, "draw"))            catch_draw++
        else if (index($0, "noDiseaseList"))   catch_bail_nolist++
        else if (index($0, "unresolved"))      catch_bail_unres++
        next
    }
    if (ev == "INFECT") {
        if (index($0, "INFECT ok"))                 infect_ok++
        else if (index($0, "alreadySick"))          skip_sick++
        else if (index($0, "immune"))               skip_immune++
        else if (index($0, "exemptProfession"))     skip_exempt++
        else                                        skip_other++
        if (index($0, "NOT-LINKED")) infect_notlinked++
        else if (index($0, "LINKED")) infect_linked++
        next
    }
    if (ev == "SPREAD") {
        if (index($0, "roll=FAILED"))            spread_fail++
        else if (index($0, "NO ELIGIBLE"))       spread_novictim++
        else if (index($0, "INFECTING"))         spread_infect++
        next
    }
    if (ev == "RESOLVE") {
        if (index($0, "outcome=DIED"))           died++
        else if (index($0, "outcome=RECOVERED")) recovered++
        next
    }
    if (ev == "CARE") {
        if (index($0, "NOT-LINKED") == 0 && index($0, "LINKED")) care_linked++
        else {
            care_notlinked++
            r = $0; sub(/.*reason=/, "", r); sub(/[ 	].*/, "", r)
            if (!(care_reason_seen[r]++)) care_reasons = care_reasons (care_reasons ? ", " : "") r
        }
        next
    }
}

# ==========================================================================
# FIRE / CROPDISEASE / TORNADO
# Keyword-bucketed: exact formats unknown to this lane. Unmatched lines are
# retained and printed so a format mismatch is visible, never silent.
# ==========================================================================
/\[Fire\]/ {
    note("fire"); fire_n++
    # EXACT formats, supplied verbatim by the fire lane. Anchored on the ASCII prefix
    # of each line because several of them contain a UTF-8 em-dash. Most specific
    # first, exclusive else-if, so one line can never book two events.
    if ($0 ~ /\[Fire\] tick live:/ || $0 ~ /\[Fire\] tick suppressed:/) {
        fire_gw = $0
        # The lane emits "GameWorld+0xD0=0x........" -- NOT "gw+0xD0=". Decide the
        # noFire bit from the VALUE; the word noFire appears in both arms.
        hv = $0; sub(/.*GameWorld\+0x[Dd]0=/, "", hv); sub(/[^0-9A-Fa-fxX].*/, "", hv)
        fire_gw_val = hex2dec(hv); fire_gw_known = 1
        fire_nofire = (int(fire_gw_val / 64) % 2)
    }
    else if ($0 ~ /\[Fire\] type ids:/)                    { fire_tids = $0 }
    else if ($0 ~ /\[Fire\] FireComponent vtable LIVE:/)   { fire_vtable = 1 }
    else if ($0 ~ /\[Fire\] ignition roll/) {
        if ($0 ~ /-> FIRED/) fire_roll_fired++; else fire_roll_miss++
    }
    else if ($0 ~ /\[Fire\] IGNITED/) {
        fire_ignite_ok++
        if ($0 ~ /posted=YES/) fire_work++; else fire_posted_no++
    }
    else if ($0 ~ /\[Fire\] IGNITE REFUSED/) {
        fire_ignite_fail++
        r = $0; sub(/.*reason=/, "", r); sub(/[ \t].*/, "", r)
        if (!(fire_refuse_seen[r]++))
            fire_refuse_list = fire_refuse_list (fire_refuse_list == "" ? "" : ", ") r
    }
    else if ($0 ~ /\[Fire\] first fire in town/)           { fire_banner++ }
    else if ($0 ~ /SPREAD ABORTED/)                        { fire_spread_abort++; fire_abort_line = $0 }
    else if ($0 ~ /\[Fire\] spread from=/) {
        if      ($0 ~ /-> CAUGHT/)          fire_spread_hit++
        else if ($0 ~ /NO FireComponent/)   fire_spread_nofc++
        else if ($0 ~ /empty cell/)         fire_spread_empty++
        else                                fire_spread_miss++
    }
    else if ($0 ~ /\[Fire\] BUCKET/)                       { fire_bucket++ }
    else if ($0 ~ /\[Fire\] EXTINGUISHED/) {
        fire_ext++; fire_ext_line = $0
        if      ($0 ~ /reason=buckets-reached-threshold/) fire_ext_buckets++
        else if ($0 ~ /reason=timer-expired/)             fire_ext_timer++
    }
    else if ($0 ~ /ForceSMState/) {
        fire_sm++
        if      ($0 ~ /SM chain EMPTY/)            fire_sm_empty++
        else if ($0 ~ /NO StateMachineComponent/)  fire_sm_nosm++
    }
    else if ($0 ~ /\[Fire\] BeginBurning:/)                { fire_notfinished++ }
    else if (!(fire_un_seen[$0]++) && fire_nun < 6)         fire_un[++fire_nun] = $0
    next
}
# CROPDISEASE -- EXACT formats, supplied by that lane from its completed port.
/\[CropDisease\]/ {
    note("crop"); crop_n++
    ev = $0; sub(/.*\[CropDisease\][ \t]+/, "", ev); sub(/[ \t].*/, "", ev)

    if (ev == "init") {
        crop_init = $0
        crop_live = getnum($0, "live")
    }
    else if (ev == "STARTUP")  crop_startup++
    else if (ev == "REGISTER") {
        crop_register++
        n = getnum($0, "registry-count"); if (n >= 0) crop_regcount = n
    }
    else if (ev == "ROLL") {
        crop_roll++
        if ($0 ~ /-> INFECT/) crop_roll_infect++; else crop_roll_no++
        if ($0 ~ /sameCrop=HIT/) crop_samecrop_hit++; else if ($0 ~ /sameCrop=miss/) crop_samecrop_miss++
    }
    else if (ev == "INFECT")          crop_infect++
    else if (ev == "INFECT-SKIPPED") {
        crop_infect_skip++
        r = $0; sub(/.*reason=/, "", r); sub(/[ \t].*/, "", r)
        if (!(crop_skip_seen[r]++)) crop_skip_reasons = crop_skip_reasons (crop_skip_reasons ? ", " : "") r
    }
    else if (ev == "KILL-PLANT")  { if ($0 ~ /killed=yes/) crop_killplant++; else crop_killplant_no++ }
    else if (ev == "KILL-ANIMAL") { if ($0 ~ /killed=yes/) crop_killanimal++; else crop_killanimal_no++ }
    else if (ev == "SPREAD-TRY") {
        # FIVE outcomes and only "-> SPREAD" is a success. The word SPREAD also appears
        # in the event name itself ("SPREAD-TRY"), so key on the token after the ARROW,
        # never on a bare substring search.
        o = $0
        if (index(o, "-> ")) { sub(/.*-> /, "", o); sub(/[ \t].*/, "", o) } else o = "(no-arrow)"
        crop_spread_try++
        if (o == "SPREAD") crop_spread_hit++
        else { crop_spread_miss++
               if (!(crop_sp_seen[o]++)) crop_spread_reasons = crop_spread_reasons (crop_spread_reasons ? ", " : "") o }
    }
    else if (ev == "RUN-OUT") {
        crop_runout++
        if ($0 ~ /no-plants-left/)        crop_runout_cleared++
        else if ($0 ~ /duration-expired/) crop_runout_expired++
    }
    else if (ev == "CURE")  crop_cure++
    else if (!(crop_un_seen[$0]++) && crop_nun < 6) crop_un[++crop_nun] = $0
    next
}
# TORNADO -- EXACT formats, supplied by that lane. These go to stderr
# (standalone_error.log), which is why both logs are read by default.
#
# The spawn and damage records WRAP across several physical lines and the
# continuation lines carry no [Tornado] prefix. So the distinctive fields are harvested
# by KEY, independently of which physical line they landed on; only the event counting
# keys off the tag. That makes the parser correct whether the lane emits one long line
# or the wrapped form.
/\[Tornado\]/ || /waypoints=|path=(FRACTAL|STRAIGHT)|particleComponent=|building-NOFIRE=|maxLateralDeviation=/ {
    if (index($0, "[Tornado]")) note("tornado")

    # --- field harvest (each key appears once per record, so no double counting) ---
    if ($0 ~ /path=FRACTAL/)              torn_fractal++
    if ($0 ~ /path=STRAIGHT/)             torn_straight++
    if ($0 ~ /particleComponent=MISSING/) torn_pc_missing++
    if ($0 ~ /particleComponent=found/)   torn_pc_found++
    n = getnum($0, "waypoints");     if (n >= 0) torn_waypoints = n
    n = getnum($0, "damageRadius");  if (n >= 0) torn_radius = n
    # building-NOFIRE is a SEPARATE ARM from building, not a variant spelling of it.
    # Harvest it first and never let it be read as "building".
    nf = getnum($0, "building-NOFIRE")
    if (nf >= 0) { torn_bld_nofire += nf; torn_dmg_fields = 1 }
    n = getnum($0, "tree");    if (n >= 0) { torn_tree += n;    torn_dmg_fields = 1 }
    n = getnum($0, "building");
    if (n >= 0 && $0 ~ /(^|[^-])building=/) { torn_bld_ign += n; torn_dmg_fields = 1 }
    n = getnum($0, "loose");   if (n >= 0) { torn_loose += n;   torn_dmg_fields = 1 }
    n = getnum($0, "citizen"); if (n >= 0) { torn_citizen += n; torn_dmg_fields = 1 }

    if (!index($0, "[Tornado]")) next

    ev = $0; sub(/.*\[Tornado\][ \t]+/, "", ev); sub(/[ \t].*/, "", ev); sub(/:$/, "", ev)

    if (ev == "init") {
        torn_init = $0
        if ($0 ~ /fire lane NULL/)   torn_fire_null = 1
        if ($0 ~ /arm is INERT/)     torn_arm_inert = 1
    }
    else if (ev == "spawn")     torn_spawn++
    else if (ev == "waypoint")  torn_wp_lines++
    else if (ev == "damage") {
        torn_dmg++
        # "non-zero arm" == this record actually destroyed something. Evaluated on the
        # harvested totals at END, so a wrapped record still counts correctly.
    }
    else if (ev == "path")      { if ($0 ~ /exhausted/) torn_pathend++ }
    else if (ev == "expired") {
        torn_expired++
        n = getnum($0, "pathIndex")
        if (n >= 0) { torn_exp_idx = n; if (n < 4) torn_died_early++ }
    }
    else if (!(torn_un_seen[$0]++) && torn_nun < 6) torn_un[++torn_nun] = $0
    next
}

END {
    # ---- env gates -------------------------------------------------------
    print "-- DISASTER ENV GATES AS THE RUN SAW THEM ------------------------------------"
    if (nenv == 0) {
        print "   (no disaster-related BANISHED_* gate line found in the log)"
        print "   A gate that never logged its state is a gate you cannot rule out."
    } else {
        for (i = 1; i <= nenv; i++) print "   " envlines[i]
    }
    print ""
    print "   roster (a gate that never logged is shown so it stays visible):"
    ng = split(gate_order, gl, " ")
    for (i = 1; i <= ng; i++) {
        g = gl[i]
        printf "     %-30s %s\n", g, \
            (!gate_saw[g] ? "not observed in log" : (gate_on[g] ? "ON" : "off"))
    }
    if (nharn > 0) {
        print "   forced-trigger harness lines:"
        for (i = 1; i <= nharn; i++) print "     " harnlines[i]
    } else {
        print "   no [Disaster] harness lines -- no disaster was force-triggered this run."
    }
    print ""
    print "   >> AUTHORED TUNING IS DENOMINATED IN CENTURIES: crop infestation mean"
    print "      interval is 150-200 years, fire 20-40 years, and half the building"
    print "      templates carry _years=0 and can NEVER ignite organically. An organic"
    print "      roll that never fires is CORRECT retail behaviour. Only a FORCED"
    print "      trigger makes these arms testable in a session."
    print ""

    # ---- verdicts --------------------------------------------------------
    # arm  = the trigger could fire at all (gate on / resource loaded)
    # star = the disaster actually started on something
    # resp = the response/resolution chain completed
    #
    # PLAGUE
    if (!seen["plague"] && !seen["plague_load"])      pv = "NOT-ARMED"
    else if (dl_off)                                   pv = "NOT-ARMED"
    else if (dl_skip != "" && dl_result == "")         pv = "NOT-ARMED"
    else if (infect_ok + 0 == 0)                       pv = "ARMED-BUT-INERT"
    else if (died + recovered + 0 == 0)                pv = "PARTIAL"
    else if (care_linked + 0 == 0)                     pv = "PARTIAL"
    else                                               pv = "WORKING"

    # FIRE. Organic ignition is authored at 20-40 YEARS, and half the building
    # templates carry _years=0 and can never ignite at all -- so "no ignition" without a
    # forced trigger is CORRECT RETAIL BEHAVIOUR, not a defect. Only call it inert when
    # a forced trigger was actually armed and still produced nothing.
    if (!seen["fire"])                                 fv = "NOT-ARMED"
    else if (fire_nofire)                              fv = "NOT-ARMED"
    else if (fire_ignite_ok + 0 == 0)
        fv = (gate_on["BANISHED_FORCE_FIRE"] ? "ARMED-BUT-INERT" : "NOT-ARMED")
    else if (fire_bucket + 0 == 0)                     fv = "PARTIAL"
    else if (fire_ext_buckets + 0 == 0)                fv = "PARTIAL"
    else                                               fv = "WORKING"

    # CROPDISEASE. Same century-scale caveat on the organic roll (mean interval
    # 150-200 years). But STARTUP is different: the game data is CONFIRMED to carry a
    # DiseaseDescription on CropField, Orchard and Pasture, so live=1 with zero STARTUP
    # lines is a genuine defect regardless of any trigger.
    kills = crop_killplant + crop_killanimal
    if (!seen["crop"])                                 cv = "NOT-ARMED"
    else if (crop_live == 0)                           cv = "NOT-ARMED"
    else if (crop_startup + 0 == 0)                    cv = "ARMED-BUT-INERT"
    else if (crop_infect + 0 == 0)
        cv = (gate_on["BANISHED_FORCE_INFEST"] ? "ARMED-BUT-INERT" : "NOT-ARMED")
    else if (kills + 0 == 0)                           cv = "PARTIAL"
    else if (crop_cure + 0 == 0)                       cv = "PARTIAL"
    else                                               cv = "WORKING"

    # TORNADO. WORKING needs at least one damage record that actually destroyed
    # something -- spawn alone is PARTIAL. Path exhaustion is reported but not required.
    torn_destroyed = torn_tree + torn_bld_ign + torn_loose + torn_citizen
    if (!seen["tornado"])                              tv = "NOT-ARMED"
    else if (torn_spawn + 0 == 0)
        tv = (gate_on["BANISHED_FORCE_TORNADO"] ? "ARMED-BUT-INERT" : "NOT-ARMED")
    else if (torn_dmg + 0 == 0)                        tv = "PARTIAL"
    else if (torn_destroyed + 0 == 0)                  tv = "PARTIAL"
    else                                               tv = "WORKING"

    print "-- VERDICT -------------------------------------------------------------------"
    printf "   %-12s %-18s %s\n", "DISASTER", "VERDICT", "BASIS"
    printf "   %-12s %-18s %s\n", "FIRE",        fv, basis_fire(fv)
    printf "   %-12s %-18s %s\n", "PLAGUE",      pv, basis_plague(pv)
    printf "   %-12s %-18s %s\n", "CROPDISEASE", cv, basis_crop(cv)
    printf "   %-12s %-18s %s\n", "TORNADO",     tv, basis_torn(tv)
    print ""
    print "   NOT-ARMED       gate off / harness never fired -- says NOTHING about the code"
    print "   ARMED-BUT-INERT trigger ran, produced no downstream effect -- a real defect"
    print "   PARTIAL         started, response chain did not complete"
    print "   WORKING         full chain including the response"
    print ""

    # ---- PLAGUE detail ---------------------------------------------------
    print "-- PLAGUE --------------------------------------------------------------------"
    if (!seen["plague_load"]) {
        print "   [GUL::DiseaseList] tag never appeared -- the step-1 loader did not run."
    } else {
        if (dl_off)      print "   gw+0xa8 (disease list) is NULL"
        if (dl_skip!="") print "   " dl_skip
        if (dl_result != "") print "   " dl_result
        if (dl_count != "") print "   diseases loaded: " dl_count
        if (dl_zero + 0 > 0) {
            print "   !! " dl_zero " disease(s) have recipMortality(+0x40)=0 -> deathChance INFINITE:"
            for (i = 1; i <= dl_zero && i <= 4; i++) print "      " dl_zero_line[i]
        }
    }
    if (!seen["plague"]) {
        print "   [Plague] tag never appeared -- UpdateHealth never ran, or the trace is off"
        print "   (it is off unless BANISHED_PLAGUE_TRACE=1). This is NOT the same as a zero count."
    } else {
        if (boot_tuning != "") print "   " boot_tuning
        if (care_mult_zero) print "   !! careMult is ZERO -- every treated citizen dies instantly."
        if (boot_hosp != "") print "   " boot_hosp
        if (boot_hosp == "" && hosp_gaveup != "") {
            print "   " hosp_gaveup
            print "   !! the attendance registry never populated -- FindCareBuilding,"
            print "      FindStorage and TryEnrolInSchool are all blind together."
        }
        else if (boot_hosp == "" && hosp_deferred != "") {
            printf "   hospital census never settled (%d DEFERRED lines, no verdict).\n", hosp_defer_n
            print "   " hosp_deferred
            print "   >> This is NOT a no-hospital result. The census had nothing to search."
        }
        if (no_hospital) {
            print "   !! NO HOSPITAL in this town: FindCareBuilding can never link and the"
            print "      cure arm is UNREACHABLE. A death-only result here is expected, not a bug."
        }
        printf "   rolls fired=%d | catch: draw=%d noBucket=%d bail(noList)=%d bail(unresolved)=%d\n", \
               roll_fired, catch_draw, catch_nobucket, catch_bail_nolist, catch_bail_unres
        printf "   infect: ok=%d (linked=%d notLinked=%d) | skipped: sick=%d immune=%d exempt=%d other=%d\n", \
               infect_ok, infect_linked, infect_notlinked, skip_sick, skip_immune, skip_exempt, skip_other
        printf "   spread: rollFailed=%d infected=%d noEligibleVictim=%d\n", \
               spread_fail, spread_infect, spread_novictim
        printf "   resolve: DIED=%d RECOVERED=%d", died, recovered
        if (died + recovered > 0) printf "  (survival %.1f%%)", 100.0 * recovered / (died + recovered)
        printf "\n"
        printf "   care: LINKED=%d NOT-LINKED=%d%s\n", care_linked, care_notlinked, \
               (care_reasons != "" ? "  reasons: " care_reasons : "")
        if (census != "") print "   " census
        else if (roll_fired + 0 == 0) print "   (no CENSUS line yet -- fewer than one census period of rolls)"
    }
    print ""

    # ---- FIRE detail -----------------------------------------------------
    print "-- FIRE ----------------------------------------------------------------------"
    if (!seen["fire"]) {
        print "   [Fire] tag never appeared -- nothing to measure (not a zero result)."
    } else {
        if (fire_gw != "") {
            print "   " fire_gw
            if (fire_nofire)
                print "   !! noFire is SET (gw+0xD0 bit 6) -- nothing can ignite. Gate, not defect."
            else if (fire_gw_known)
                printf "   gw+0xD0=%d (0x%x): bit 6 CLEAR -- ignition is permitted.\n", fire_gw_val, fire_gw_val
            else
                print "   noFire reported clear -- ignition is permitted."
        } else print "   (no gw+0xD0 one-shot line found -- cannot confirm the noFire bit)"
        if (fire_tids != "")   print "   " fire_tids
        if (fire_vtable)       print "   FireComponent vtable LIVE (slots 3/9/15 installed)"
        else                   print "   !! no vtable-LIVE line -- FireComponent slots 9/15 are inert stubs."
        printf "   ignition roll: fired=%d missed=%d | IGNITED=%d (FireWork posted=%d, NOT posted=%d) refused=%d\n", \
               fire_roll_fired, fire_roll_miss, fire_ignite_ok, fire_work, fire_posted_no, fire_ignite_fail
        if (fire_refuse_list != "") print "   ignite refusal reasons: " fire_refuse_list
        printf "   spread: caught=%d occupantHasNoFireComponent=%d emptyCell=%d other=%d\n", \
               fire_spread_hit, fire_spread_nofc, fire_spread_empty, fire_spread_miss
        printf "   buckets(slot15)=%d | EXTINGUISHED=%d (bucketsReachedThreshold=%d timerExpired=%d)\n", \
               fire_bucket, fire_ext, fire_ext_buckets, fire_ext_timer
        if (fire_ext_line != "") print "   " fire_ext_line
        if (fire_ext_buckets + 0 == 0 && fire_ext_timer + 0 > 0)
            print "   NOTE: every fire ended by TIMER, not by buckets -- it burned out on its own. The bucket brigade did NOT put it out, so the player-response arm is UNPROVEN."
        if (fire_sm_empty + 0 > 0)
            print "   !! ForceSMState reports SM chain EMPTY -- the owning ControllerComponent is inert, so buildings cannot enter the BURNING state. Real finding, not noise."
        if (fire_sm_nosm + 0 > 0)
            printf "   !! %d ignite(s) found NO StateMachineComponent on the burning entity.\n", fire_sm_nosm
        if (fire_spread_abort + 0 > 0) print "   !! " fire_abort_line
        if (fire_notfinished + 0 > 0)
            printf "   %d ignite(s) refused: building not finished (BuildComponent+0x48==0).\n", fire_notfinished
        if (fire_nun > 0) {
            print "   unmatched [Fire] lines (format may differ from this parser):"
            for (i = 1; i <= fire_nun; i++) print "      " fire_un[i]
        }
    }
    print ""

    # ---- CROPDISEASE detail ---------------------------------------------
    print "-- CROPDISEASE ---------------------------------------------------------------"
    if (!seen["crop"]) {
        print "   [CropDisease] tag never appeared -- nothing to measure (not a zero result)."
    } else {
        if (crop_init != "") print "   " crop_init
        printf "   STARTUP=%d REGISTER=%d (registry-count=%s)\n", \
               crop_startup, crop_register, (crop_regcount == "" ? "n/a" : crop_regcount)
        if (crop_live == 1 && crop_startup + 0 == 0) {
            print "   !! live=1 but ZERO STARTUP lines -- no entity carries a DiseaseDescription."
            print "      CropField / Orchard / Pasture are CONFIRMED to carry one in the game data,"
            print "      so this is a real defect, not a data gap."
        }
        printf "   roll: total=%d -> INFECT=%d -> no=%d | sameCrop HIT=%d miss=%d\n", \
               crop_roll, crop_roll_infect, crop_roll_no, crop_samecrop_hit, crop_samecrop_miss
        if (crop_roll + 0 > 0 && crop_roll_infect + 0 == 0)
            print "      (roll -> no on every attempt is EXPECTED: authored mean interval is 150-200 years)"
        printf "   infect=%d skipped=%d%s\n", crop_infect, crop_infect_skip, \
               (crop_skip_reasons != "" ? "  reasons: " crop_skip_reasons : "")
        printf "   kills: plant yes=%d no=%d | animal yes=%d no=%d\n", \
               crop_killplant, crop_killplant_no, crop_killanimal, crop_killanimal_no
        printf "   spread-try=%d -> SPREAD=%d, other=%d%s\n", \
               crop_spread_try, crop_spread_hit, crop_spread_miss, \
               (crop_spread_reasons != "" ? "  (" crop_spread_reasons ")" : "")
        printf "   run-out=%d (no-plants-left=%d duration-expired=%d) | CURE=%d\n", \
               crop_runout, crop_runout_cleared, crop_runout_expired, crop_cure
        if (crop_cure + 0 == 0 && crop_runout_expired + 0 > 0 && crop_runout_cleared + 0 == 0) {
            print "      note: infestations only TIMED OUT. duration-expired is not the player"
            print "            response arm -- no-plants-left (cure by clearing) + CURE is."
        }
        if (crop_nun > 0) {
            print "   unmatched [CropDisease] lines (format may differ from this parser):"
            for (i = 1; i <= crop_nun; i++) print "      " crop_un[i]
        }
    }
    print ""

    # ---- TORNADO detail --------------------------------------------------
    print "-- TORNADO -------------------------------------------------------------------"
    if (!seen["tornado"]) {
        print "   [Tornado] tag never appeared -- nothing to measure, or the trace is off"
        print "   (it is off unless BANISHED_TORNADO_TRACE=1). Not a zero result."
    } else {
        if (torn_init != "") print "   " torn_init
        printf "   spawns=%d waypoints=%s damageRadius=%s | path: FRACTAL=%d STRAIGHT=%d | exhausted=%d\n", \
               torn_spawn, (torn_waypoints == "" ? "n/a" : torn_waypoints), \
               (torn_radius == "" ? "n/a" : torn_radius), torn_fractal, torn_straight, torn_pathend
        # The path being fractal is a CORRECTNESS check, proven from the disassembly --
        # the Ghidra "* 0.0" was a mis-modelled Normalize out-param, not a real zero.
        if (torn_straight + 0 > 0) {
            print "   !! path=STRAIGHT REPORTED -- THE PORT HAS REGRESSED. The path is proven"
            print "      fractal in the disassembly; STRAIGHT means the lateral deviation was"
            print "      lost again. This is a defect even if everything else here looks fine."
        }
        printf "   damage records=%d | tree=%d building=%d building-NOFIRE=%d loose=%d citizen=%d\n", \
               torn_dmg, torn_tree, torn_bld_ign, torn_bld_nofire, torn_loose, torn_citizen
        if (torn_bld_nofire + 0 > 0 && torn_bld_ign + 0 == 0) {
            print "   >> CROSS-LANE FAILURE, NOT A TORNADO DEFECT: every building it hit took the"
            print "      building-NOFIRE arm, meaning the FIRE lane helpers resolved null. The"
            print "      tornado found and processed those buildings correctly; the ignition"
            print "      hook is what has not landed. Chase this in the fire lane."
        }
        # The lifetime gate belongs to the PARTICLE, not to the path, and the Tornado particle
        # visual is deliberately deferred this wave -- so this is the likeliest failure.
        if (torn_pc_missing + 0 > 0) {
            print "   !! particleComponent=MISSING at spawn -- the lifetime gate has nothing to"
            print "      read and the tornado dies on frame 1. Check the ParticleComponent gate."
        }
        printf "   expired=%d%s | waypoint lines=%d (every 16th)\n", torn_expired, \
               (torn_exp_idx == "" ? "" : " lastPathIndex=" torn_exp_idx), torn_wp_lines
        if (torn_died_early + 0 > 0) {
            print "   !! DIED IMMEDIATELY -- expired at pathIndex < 4. Check the ParticleComponent"
            print "      lifetime gate: Update kills the tornado when no live particle instance"
            print "      is reported, and the Tornado particle is deferred this wave."
        }
        if (torn_fire_null || torn_arm_inert) {
            print "   note: init reports the fire lane NULL / building arm INERT, so"
            print "         building-NOFIRE is the EXPECTED arm this wave."
        }
        if (torn_nun > 0) {
            print "   unmatched [Tornado] lines (format may differ from this parser):"
            for (i = 1; i <= torn_nun; i++) print "      " torn_un[i]
        }
    }
    print ""

    # ---- exit code -------------------------------------------------------
    bad = 0
    if (pv != "WORKING") bad = 1
    if (fv != "WORKING") bad = 1
    if (cv != "WORKING") bad = 1
    if (tv != "WORKING") bad = 1
    print "=============================================================================="
    if (bad) print " RESULT: not all disasters WORKING (see table above)"
    else     print " RESULT: all four disasters WORKING"
    print "=============================================================================="
    exit (bad ? 1 : 0)
}

function basis_plague(v) {
    if (v == "NOT-ARMED")       return (seen["plague_load"] ? "disease list not loaded" : "no [GUL::DiseaseList] tag at all")
    if (v == "ARMED-BUT-INERT") return "list loaded, but 0 infections ever applied"
    if (v == "PARTIAL")         return (died + recovered + 0 == 0 ? "infected, but no illness ever resolved" : "resolved, but no citizen was ever linked to a hospital")
    return "infect -> spread -> care -> resolve all observed"
}
function basis_fire(v) {
    if (v == "NOT-ARMED") {
        if (!seen["fire"])  return "no [Fire] tag at all"
        if (fire_nofire)    return "gw+0xD0 noFire bit set"
        return "no forced trigger; organic ignition is 20-40 YEARS -- untestable in a session"
    }
    if (v == "ARMED-BUT-INERT") return "BANISHED_FORCE_FIRE armed, still 0 successful ignitions"
    if (v == "PARTIAL") {
        if (fire_bucket + 0 == 0)      return "ignited, but no water was ever applied (slot 15 never ran)"
        if (fire_ext_buckets + 0 == 0) return "buckets applied, but every fire ended by timer -- the brigade never won"
        return "ignited, but the response chain did not complete"
    }
    return "ignite -> work -> buckets -> extinguish all observed"
}
function basis_crop(v) {
    if (v == "NOT-ARMED") {
        if (!seen["crop"])   return "no [CropDisease] tag at all"
        if (crop_live == 0)  return "init reports live=0 -- the component is gated off"
        return "no forced trigger; organic roll is 150-200 YEARS -- untestable in a session"
    }
    if (v == "ARMED-BUT-INERT") {
        if (crop_startup + 0 == 0)
            return "live=1 but 0 STARTUP lines -- no entity carries the component (DEFECT)"
        return "BANISHED_FORCE_INFEST armed, still 0 infections"
    }
    if (v == "PARTIAL")         return (crop_killplant + crop_killanimal + 0 == 0 ? "infected, but nothing was ever killed" : "infected + killed, but never cured")
    return "infect -> kill -> spread -> cure all observed"
}
function basis_torn(v) {
    if (v == "NOT-ARMED") {
        if (!seen["tornado"]) return "no [Tornado] tag at all"
        return "no forced trigger (BANISHED_FORCE_TORNADO) and 0 spawns"
    }
    if (v == "ARMED-BUT-INERT") return "BANISHED_FORCE_TORNADO armed, still 0 spawns"
    if (v == "PARTIAL")         return (torn_dmg + 0 == 0 ? "spawned, but no damage record" : "damage records ran, but every arm was zero")
    return "spawn -> path -> damage (non-zero arm) all observed"
}
' "${PRESENT[@]}"

exit $?
