#!/usr/bin/env bash
# =============================================================================
# tools/harness_route.sh -- one unattended harness route on the debug exe
# =============================================================================
#
# Revived from docs/history/endgame_b/b8_session/harness_route.sh (the B8 tool-harness
# route runner). Each ROUTE launches a new game with its own BANISHED_* harness flags
# (src/app/options.h, harness section), waits for the run to quit itself through the
# pause menu, copies the logs out, then greps the route's own PASS / FAIL markers.
#
# usage: tools/harness_route.sh <route> [outdir]
#   route   one of the names in the ROUTES table below (`tools/harness_route.sh list`)
#   outdir  where the logs land (default build/harness_routes/<route>)
#
# EXIT: 0 = PASS, 1 = FAIL (a FAIL marker, a missing PASS marker, a crash or a hang),
#       2 = could not run (no exe, lock not granted, exe busy).
#
# GAME DIR: the script takes the FUNCTIONAL game dir itself (tools/game_dirs.sh env
# <lane> functional; lane = $HARNESS_LANE, default harness-route) and releases it on
# exit, after the logs are copied out. Hold no lock of your own around it.
# HARNESS_EXE overrides the exe (default build/cmake-debug/OpenBA.exe); build it first
# (tools/build.sh build debug). The run is /profile:faithful (fixes and extensions off).
# HARNESS_FIXES=<id>[,<id>...] runs the per-entry profile instead with exactly those fixes
# on: every `fix.<id>=` of the registry (src/ext/registry.cpp, Tier::Fix rows) is written
# to the functional user dir's settings.cfg as 0 except the listed ones (1), and every
# `ext.<id>=` line there is set to 0; the file is restored at exit. Class A rows the file
# does not name keep their default (no sim effect).
#
# ADDING A ROUTE: a `route_<name>` function that sets ENV (the harness flags, one
# KEY=VALUE per element), SECS (the wall-clock deadline) and DESC, plus a
# `check_<name>` function that reads "$OUT/standalone_error.log" and returns 0 on PASS.
# Add the name to ROUTES.
# =============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
ROUTES="marry birth immigration trade autotrade pick clearoverlap"

ROUTE="${1:-}"
[ -n "$ROUTE" ] || { echo "usage: tools/harness_route.sh <route> [outdir]   (routes: $ROUTES)" >&2; exit 2; }
if [ "$ROUTE" = list ]; then
    echo "$ROUTES"
    exit 0
fi
case " $ROUTES " in
    *" $ROUTE "*) ;;
    *) echo "[route] unknown route '$ROUTE' (routes: $ROUTES)" >&2; exit 2 ;;
esac
OUT="${2:-$REPO/build/harness_routes/$ROUTE}"
EXE="${HARNESS_EXE:-$REPO/build/cmake-debug/OpenBA.exe}"
LANE="${HARNESS_LANE:-harness-route}"

# ---------------------------------------------------------------- the routes
# Common to every gameplay route: a new game (seed pinned), quit through the pause menu.
ENV=()
SECS=600
DESC=""
QUIT_FRAME=0

# err <regex>: the matching lines of the run's standalone_error.log.
err() {
    grep -aE "$1" "$OUT/standalone_error.log" 2>/dev/null
}

# A route passes only when its PASS marker is present and no FAIL marker is.
verdict() {
    local pass_re="$1" fail_re="$2"
    if [ -n "$(err "$fail_re" | head -1)" ]; then
        err "$fail_re" | head -10
        return 1
    fi
    if [ -z "$(err "$pass_re" | head -1)" ]; then
        echo "[route] no PASS marker ($pass_re)"
        return 1
    fi
    err "$pass_re" | tail -5
    return 0
}

# --- marry: CitizenComponent::FindSpouse (0x14010fe40), the two-sided age window.
# The spouse probe asks FindSpouse directly with a candidate moved just past the window
# (older, then younger) and inside it; its organic stage widows two founding couples and
# lets CitizenComponent::Update marry the survivors, first held apart beyond the window,
# then inside it. The census checks every couple FindSpouse formed against the window
# (src/diag/death_harness.cpp).
route_marry() {
    DESC="FindSpouse never pairs beyond the +0x4c age window (either direction); a pair inside it marries"
    QUIT_FRAME=6600
    SECS=540
    ENV=(BANISHED_FORCE_SPEED=10 BANISHED_FAMILY_CENSUS=600 BANISHED_SPOUSE_PROBE=1200)
}
check_marry() {
    verdict "\[SpouseOrganic\] PASS" "\[SpouseProbe\] FAIL|\[SpouseOrganic\] FAIL|\[Couple\] FAIL" || return 1
    local m
    for m in "\[SpouseProbe\] PASS" "\[SpouseOrganic\] beyond ok" "\[Couple\] PASS"; do
        [ -n "$(err "$m" | head -1)" ] || { echo "[route] missing: $m"; return 1; }
        err "$m" | head -2
    done
    return 0
}

# --- birth: the newborn path (CitizenComponent::GiveBirth 0x140111b90 ->
# Initialize 0x140117080) and known_port_issues row 990: every newborn carries a
# profession (+0x1f0).
route_birth() {
    DESC="forced births; every newborn's profession (+0x1f0) is non-null"
    QUIT_FRAME=3000
    SECS=420
    ENV=(BANISHED_FORCE_SPEED=10 BANISHED_FORCE_BIRTH=6 BANISHED_FORCE_BIRTH_FRAME=900
         BANISHED_FAMILY_CENSUS=600)
}
check_birth() {
    verdict "\[BirthCheck\] PASS" "\[BirthCheck\] FAIL|\[ProfessionCensus\].*null=[1-9]"
}

# --- immigration: the ImmigrationComponent state machine (immigrationcomponent_states.cpp,
# row 1340). The nomad harness cuts the 2..6-year wait short, lets the group walk to the
# town hall, then answers ALLOW on the first group and DENY on the second
# (src/diag/death_harness.cpp). CanAccept wants a population floor (desc+0x28, 8 in the
# shipped data) and its required buildings; the stress town is the one with a town hall.
route_immigration() {
    DESC="a nomad group arrives and is accepted, a second arrives and is denied"
    QUIT_FRAME=12000
    SECS=600
    ENV=(BANISHED_STRESS=1 BANISHED_STRESS_CITIZENS=120 BANISHED_FORCE_SPEED=10
         BANISHED_NOMAD_HARNESS=300 BANISHED_FAMILY_CENSUS=1200)
}
check_immigration() {
    verdict "\[NomadHarness\] PASS" "\[NomadHarness\] FAIL|\[ProfessionCensus\].*null=[1-9]"
}

# --- trade: TradeComponent::TickIdle (tradecomponent_states.cpp) reads the
# BoatComponent unguarded; a merchant boat spawns, routes and docks at a trading post in
# an ordinary (non-stress) town (src/diag/trade_buy_harness.cpp).
route_trade() {
    DESC="a merchant boat spawns, routes and docks at the trading post"
    QUIT_FRAME=12000
    SECS=600
    ENV=(BANISHED_FORCE_SPEED=10 BANISHED_BUILD_DEMO_TRADINGPOST=1 BANISHED_TRADE_WATCH=300)
}
check_trade() {
    verdict "\[TradeWatch\] PASS" "\[TradeWatch\] FAIL"
}

# --- autotrade: docs/retail_bugs.md section 24. The trade route's boat docks; the watch then
# stocks the post with a good the merchant prices 0 and one he prices above 0, asks 3 units of
# every buy-order category and runs TradeComponent::AutoTrade (0x140059ad0) once
# (src/diag/trade_buy_harness.cpp). PASS when the priced-0 stack is kept, which needs
# HARNESS_FIXES=autotrade_free_goods; the faithful run reports GAVE AWAY and FAILs.
route_autotrade() {
    DESC="auto-trade pays with priced goods only; a priced-0 stack stays in the post"
    QUIT_FRAME=12000
    SECS=600
    ENV=(BANISHED_FORCE_SPEED=10 BANISHED_BUILD_DEMO_TRADINGPOST=1 BANISHED_TRADE_WATCH=300,autotrade)
}
check_autotrade() {
    verdict "\[TradeWatch\] AUTOTRADE KEPT" "\[TradeWatch\] (FAIL|AUTOTRADE GAVE|AUTOTRADE ERROR)"
}

# --- pick: known_port_issues rows 252 and 1261. BANISHED_TOOL_HARNESS=pick
# (src/diag/tool_harness.cpp) picks a citizen through Picking::Scene::Intersect and
# EditTool::SelectEntity, then places a building through PlacementTool::CreateAt and a
# bridge through BridgeTool::CalcPath (no explicit RefreshMesh after RegisterEntity) and
# checks each new entity's pick mesh: none at placement (a site's model is at meshIndex
# -1, so the OnAdded pass registers nothing), then, once BuildComponent shows the
# foundation frame, one live scene entry per PickingComponent, none stale or shared, and
# a ray onto the model that resolves to the owner (the bridge segments answer the master).
route_pick() {
    DESC="citizen pick opens CitizenUI; a placed building and a bridge register one pick mesh each and pick"
    QUIT_FRAME=1200
    SECS=300
    ENV=(BANISHED_FORCE_SPEED=40 BANISHED_TOOL_HARNESS=pick)
}
check_pick() {
    verdict "\[ToolHarness\]\[pick(place|bridge)\] PICK PASS|\[ToolHarness\]\[pick\] PICKED" \
            "\[ToolHarness\]\[pick(place|bridge)?\] (PICK FAIL|FAILED|ERROR)|\) BAD" || return 1
    [ -n "$(err "\[ToolHarness\]\[pick\] PICKED citizen.*CitizenUI=1" | head -1)" ] || { echo "[route] the citizen pick did not open CitizenUI"; return 1; }
    [ -n "$(err "\[ToolHarness\]\[pickplace\] PICK PASS" | head -1)" ] || { echo "[route] no pickplace PASS"; return 1; }
    [ -n "$(err "\[ToolHarness\]\[pickbridge\] PICK PASS" | head -1)" ] || { echo "[route] no pickbridge PASS"; return 1; }
    return 0
}

# --- clearoverlap: a stockpile placed over a clear-tool area while it is still being
# cleared (src/diag/clear_overlap_harness.cpp). Four builders, a town hall over trees,
# Clear Tree / Stone / Iron dragged over a 10x10 window, then Template\StorageYard.rsc
# over the same window 900 frames later; a census every 300 frames. PASS when the window
# holds no natural resource and both sites hold their footprints by the deadline.
# CLEAR_OVERLAP overrides the flag value (e.g. 400,900,300,9300,4,0: a 4x4 window, no hall).
route_clearoverlap() {
    DESC="a stockpile over a clear-tool area: the area is cleared and both sites finish clearing"
    QUIT_FRAME=9600
    SECS=600
    ENV=(BANISHED_FORCE_SPEED=10 BANISHED_BUILDERS=4,300 BANISHED_CLEAR_OVERLAP="${CLEAR_OVERLAP:-400,900,300,9300}")
}
check_clearoverlap() {
    verdict "\[ClearOverlap\] PASS" "\[ClearOverlap\] (FAIL|ERROR)"
}

"route_$ROUTE"
# HARNESS_QUIT overrides the route's quit frame (a shorter look while debugging a route).
QUIT_FRAME="${HARNESS_QUIT:-$QUIT_FRAME}"
if [ "$ROUTE" = pick ] && [ ${#ENV[@]} -eq 0 ]; then
    check_pick
    exit 2
fi

# ---------------------------------------------------------------- the run
[ -f "$EXE" ] || { echo "[route] no exe: $EXE (tools/build.sh build debug)" >&2; exit 2; }
mkdir -p "$OUT"
rm -f "$OUT"/standalone_*.log "$OUT"/standalone_boot_state.txt

LOCKED=0
release_lock() {
    if [ "$LOCKED" = 1 ]; then
        "$HERE/game_dirs.sh" release "$LANE"
        LOCKED=0
    fi
}
trap release_lock EXIT
ENV_LINES="$("$HERE/game_dirs.sh" env "$LANE" functional 1800)" || { echo "[route] lock not granted" >&2; exit 2; }
eval "$ENV_LINES"
LOCKED=1
G="$BANISHED_DIR"
command -v cygpath >/dev/null 2>&1 && G="$(cygpath -u "$G")"

cp -f "$EXE" "$G/OpenBA.exe" || { echo "[route] exe busy, is a game running?" >&2; exit 2; }

PROFILE_ARGS=(/profile:faithful)
CFG_SAVED=""
if [ -n "${HARNESS_FIXES:-}" ]; then
    U="${BANISHED_USER_DIR:-}"
    [ -n "$U" ] || { echo "[route] HARNESS_FIXES needs the functional user dir (BANISHED_USER_DIR)" >&2; exit 2; }
    command -v cygpath >/dev/null 2>&1 && U="$(cygpath -u "$U")"
    mkdir -p "$U"
    CFG="$U/settings.cfg"
    CFG_SAVED="$(mktemp)"
    if [ -f "$CFG" ]; then cp -f "$CFG" "$CFG_SAVED"; else : > "$CFG_SAVED"; touch "$CFG"; fi
    restore_cfg() {
        if [ -n "$CFG_SAVED" ]; then
            cp -f "$CFG_SAVED" "$CFG"
            rm -f "$CFG_SAVED"
            CFG_SAVED=""
        fi
    }
    trap 'restore_cfg; release_lock' EXIT
    sed -i 's/^\(ext\.[a-z0-9_]*\)=.*/\1=0/; /^fix\./d' "$CFG"
    for fx in $(grep -oE '\{ "[a-z0-9_]+", Tier::Fix' "$REPO/src/ext/registry.cpp" | cut -d'"' -f2); do
        v=0
        case ",$HARNESS_FIXES," in *",$fx,"*) v=1 ;; esac
        printf 'fix.%s=%s\n' "$fx" "$v" >> "$CFG"
    done
    PROFILE_ARGS=()
    echo "[route] per-entry profile: $(grep '^fix\.' "$CFG" | tr '\n' ' ')"
fi
rm -f "$G"/standalone_{debug,error,crash,hang}.log "$G/standalone_boot_state.txt"

echo "[route] $ROUTE: $DESC"
echo "[route] flags: ${ENV[*]} (quit at frame $QUIT_FRAME, deadline ${SECS}s)"
START=$(date +%s)
(
    cd "$G" || exit 2
    exec env BANISHED_AUTOSTART=1 BANISHED_SEED="${HARNESS_SEED:-12345}" \
        BANISHED_PAUSEMENU="$QUIT_FRAME" BANISHED_PAUSEMENU_QUIT=120 "${ENV[@]}" \
        MSYS2_ARG_CONV_EXCL="/profile:" ./OpenBA.exe "${PROFILE_ARGS[@]}"
) &
PID=$!
"$HERE/boot_state.sh" --wait 300
BOOT_RC=$?
echo "[route] boot_state rc=$BOOT_RC"
# The run quits itself at QUIT_FRAME; the deadline catches a run that does not.
while kill -0 "$PID" 2>/dev/null; do
    [ $(( $(date +%s) - START )) -ge "$SECS" ] && break
    sleep 2
done
KILLED=no
if kill -0 "$PID" 2>/dev/null; then
    KILLED=YES
    taskkill //F //IM OpenBA.exe >/dev/null 2>&1
    kill -9 "$PID" 2>/dev/null
fi
wait "$PID" 2>/dev/null
RC=$?
for f in standalone_debug.log standalone_error.log standalone_crash.log standalone_hang.log standalone_boot_state.txt; do
    [ -f "$G/$f" ] && cp -f "$G/$f" "$OUT/"
done
[ -n "$CFG_SAVED" ] && restore_cfg
release_lock

CRASH=$(stat -c %s "$OUT/standalone_crash.log" 2>/dev/null || echo 0)
HANG=no
[ -f "$OUT/standalone_hang.log" ] && HANG=YES
echo "[route] $ROUTE: exit $RC crashlog=$CRASH hang=$HANG killed=$KILLED ($(( $(date +%s) - START ))s) logs: $OUT"
FAIL=0
if [ "$CRASH" != 0 ]; then
    echo "[route] CRASH -- read $OUT/standalone_crash.log"
    head -40 "$OUT/standalone_crash.log"
    FAIL=1
fi
[ "$HANG" = YES ] && FAIL=1
[ "$KILLED" = YES ] && echo "[route] note: killed at the deadline (no clean quit)"
if ! "check_$ROUTE"; then
    FAIL=1
fi
if [ "$FAIL" = 0 ]; then
    echo "[route] $ROUTE: PASS"
    exit 0
fi
echo "[route] $ROUTE: FAIL"
exit 1
