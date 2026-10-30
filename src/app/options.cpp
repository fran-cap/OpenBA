// ============================================================================
// src/app/options.cpp — the ONE place the process reads its environment.
//
// See options.h for the inventory and the rules. This file is the only getenv
// site in the whole tree, and
// OptionsInit() is called from the top of WinMain before anything else looks
// at a BANISHED_* variable.
//
// The stress-town block at the bottom fills the defaults the stress town needs
// that the caller did not give. It replaces src/diag/stress_harness.cpp's old
// SetIfUnset() calls, which ran mid-boot and were the reason "read the
// environment once" was not true before. Since ENDGAME C.2/C.4 it writes only
// struct fields: nothing in the tree reads the environment by name any more.
// ============================================================================
#include "options.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {

Options g_opt;

const char* Raw(const char* name) {
    const char* v = std::getenv(name);
    return (v && v[0]) ? v : nullptr;          // an empty value reads as unset everywhere
}

// The project's near-universal truth test: set, and not "0" / "false".
bool On(const char* name) {
    const char* v = Raw(name);
    return v && std::strcmp(v, "0") != 0 && std::strcmp(v, "false") != 0;
}

int Int(const char* name, int dflt) {
    const char* v = Raw(name);
    return v ? std::atoi(v) : dflt;
}

// The stress town's defaults: set only what the caller left unset, exactly as
// stress_harness.cpp's SetIfUnset did. Since ENDGAME C.2/C.4 nothing reads the
// environment by name any more, so this sets the FIELD and writes nothing.
void StressDefault(const char*, const char* value, const char*& field) {
    if (field) return;                         // the caller's value wins
    field = value;
}

void ApplyStressDefaults(Options& o) {
    if (!o.autostart) o.autostart = true;
    StressDefault("BANISHED_MAP_SIZE", "medium", o.mapSize);
    // 31337: the town sits on a 27k-cell connected flat area (seed 12345: 11k, cut up by
    // low mountains and rivers, too small for 333 door-reachable footprints — 2026-09-04).
    StressDefault("BANISHED_SEED", "31337", o.seed);
    StressDefault("BANISHED_DIFFICULTY", "easy", o.difficulty);
    StressDefault("BANISHED_CLIMATE", "fair", o.climate);
    StressDefault("BANISHED_FPS_CAP", "off", o.fpsCap);
    static char speedBuf[64];
    std::snprintf(speedBuf, sizeof(speedBuf), "%d", o.stressSpeed);
    StressDefault("BANISHED_FORCE_SPEED", speedBuf, o.forceSpeed);
    static char profBuf[64];
    std::snprintf(profBuf, sizeof(profBuf), "%d", o.stressInterval);
    StressDefault("BANISHED_SIMPROF", profBuf, o.simprof);
    if (o.stressFrames > 0) {
        if (!Raw("BANISHED_PAUSEMENU")) {
            o.pausemenu = o.stressFrames;
            if (o.pausemenu > 0 && o.pausemenu < 600) o.pausemenu = 600;
        }
        if (!Raw("BANISHED_PAUSEMENU_QUIT"))   o.pausemenuQuit   = 60;
        if (!Raw("BANISHED_PAUSEMENU_BUTTON")) o.pausemenuButton = 6;
    }
    StressDefault("BANISHED_CAMERA_PIN", "town,90", o.cameraPin);
    // The buildings starttown.cpp can only site through their own dedicated blocks
    // (water adjacency, drag areas, animal-density siting). One of each.
    StressDefault("BANISHED_BUILD_DEMO_FISHINGDOCK", "1", o.buildDemoFishingDock);
    StressDefault("BANISHED_BUILD_DEMO_TRADINGPOST", "1", o.buildDemoTradingPost);
    StressDefault("BANISHED_BUILD_DEMO_CROPFIELD",   "1", o.buildDemoCropField);
    StressDefault("BANISHED_BUILD_DEMO_ORCHARD",     "1", o.buildDemoOrchard);
    StressDefault("BANISHED_BUILD_DEMO_PASTURE",     "1", o.buildDemoPasture);
    StressDefault("BANISHED_PASTURE_AUTOTYPE",       "1", o.pastureAutoType);
    StressDefault("BANISHED_BUILD_DEMO_HUNTERLODGE", "1", o.buildDemoHunterLodge);
    StressDefault("BANISHED_BUILD_DEMO_GATHERERHUT", "1", o.buildDemoGathererHut);
    // A field with no crop and an orchard with no tree type never open for workers
    // (accepting stays 0, so the seat cascade skips them). In retail the player picks
    // those in the panel; these two harnesses make the same choice for EVERY field and
    // orchard in the world a few frames in (cropfield_force_start.cpp mode 2 also posts
    // the plant command so the field does not wait for the seasonal window).
    StressDefault("BANISHED_CROPFIELD_FORCE_START", "2", o.cropFieldForceStart);
    StressDefault("BANISHED_ORCHARD_FORCE_START",   "1", o.orchardForceStart);
}

} // namespace

const Options& Opt() { return g_opt; }

void OptionsInit() {
    Options& o = g_opt;
    o.airconv = Raw("BANISHED_AIRCONV");
    o.metalStats = On("BANISHED_METAL_STATS");
    o.inputTrace = On("BANISHED_INPUT_TRACE");
    o.background = On("BANISHED_BACKGROUND");
    o.userDir = Raw("BANISHED_USER_DIR");

    // ── harness drivers: menu → game ─────────────────────────────────────────
    o.autostart       = On("BANISHED_AUTOSTART");
    if (const char* v = Raw("BANISHED_AUTOSTART")) { const int n = std::atoi(v); if (n > 1) o.autostartFrames = n; }
    o.autostartGames  = Int("BANISHED_AUTOSTART_GAMES", 0);
    o.autoquit        = On("BANISHED_AUTOQUIT");
    if (const char* v = Raw("BANISHED_AUTOQUIT")) { const int n = std::atoi(v); if (n > 1) o.autoquitFrames = n; }
    o.autoload        = On("BANISHED_AUTOLOAD");
    if (o.autoload) {
        const char* v = Raw("BANISHED_AUTOLOAD");
        if (std::strcmp(v, "1") != 0) o.autoloadName = v;   // "1" = the autosave "Auto"
    }
    o.autotutorial    = On("BANISHED_AUTOTUTORIAL");
    if (o.autotutorial) {
        const char* v = Raw("BANISHED_AUTOTUTORIAL");
        if (std::strcmp(v, "1") != 0) o.autotutorialIndex = std::atoi(v);
    }
    // AUTOMENU_BUTTON is "set and not 0" — "false" is not a value it ever takes.
    if (const char* v = Raw("BANISHED_AUTOMENU_BUTTON"))
        if (std::strcmp(v, "0") != 0) o.automenuButton = std::atoi(v);
    o.automenuClicks  = Raw("BANISHED_AUTOMENU_CLICKS");

    // ── harness drivers: pause menu ──────────────────────────────────────────
    o.pausemenu       = Int("BANISHED_PAUSEMENU", 0);
    if (o.pausemenu > 0 && o.pausemenu < 600) o.pausemenu = 600;
    o.pausemenuQuit   = Int("BANISHED_PAUSEMENU_QUIT", o.pausemenuQuit);
    o.pausemenuButton = Int("BANISHED_PAUSEMENU_BUTTON", o.pausemenuButton);
    o.pausemenuClicks = Raw("BANISHED_PAUSEMENU_CLICKS");

    // ── modal dialogs ────────────────────────────────────────────────────────
    // MODAL_DRIVER deliberately does NOT use the "=0 means off" rule: 0 is a
    // legitimate dialog result (MessageDialog Cancel), so presence arms it.
    if (const char* v = Raw("BANISHED_MODAL_DRIVER")) {
        o.modalDriver = true;
        o.modalDriverRaw = v;
        o.modalDriverAnswer = std::atoi(v);
        if (const char* comma = std::strchr(v, ',')) {
            const int t = std::atoi(comma + 1);
            if (t > 0) o.modalDriverTicks = t;
        }
    }
    if (const char* v = Raw("BANISHED_MODAL_ANSWER")) { o.modalAnswer = true; o.modalAnswerValue = std::atoi(v); }

    // ── front end / save-load ────────────────────────────────────────────────
    o.menuScene       = Int("BANISHED_MENU_SCENE", -1);
    o.saveTest        = Raw("BANISHED_SAVE_TEST");
    o.saveTestRepeat  = Int("BANISHED_SAVE_TEST_REPEAT", 0);
    o.userProfile     = Raw("USERPROFILE");

    // ── world setup (code/Game parses these itself) ──────────────────────────
    o.seed            = Raw("BANISHED_SEED");
    o.mapSize         = Raw("BANISHED_MAP_SIZE");
    o.climate         = Raw("BANISHED_CLIMATE");
    o.difficulty      = Raw("BANISHED_DIFFICULTY");
    o.startCondition  = Raw("BANISHED_START_CONDITION");
    o.cameraPin       = Raw("BANISHED_CAMERA_PIN");
    o.forceSpeed      = Raw("BANISHED_FORCE_SPEED");
    o.simprof         = Raw("BANISHED_SIMPROF");

    o.buildDemoFishingDock = Raw("BANISHED_BUILD_DEMO_FISHINGDOCK");
    o.buildDemoTradingPost = Raw("BANISHED_BUILD_DEMO_TRADINGPOST");
    o.buildDemoCropField   = Raw("BANISHED_BUILD_DEMO_CROPFIELD");
    o.buildDemoOrchard     = Raw("BANISHED_BUILD_DEMO_ORCHARD");
    o.buildDemoPasture     = Raw("BANISHED_BUILD_DEMO_PASTURE");
    o.buildDemoHunterLodge = Raw("BANISHED_BUILD_DEMO_HUNTERLODGE");
    o.buildDemoGathererHut = Raw("BANISHED_BUILD_DEMO_GATHERERHUT");
    o.pastureAutoType      = Raw("BANISHED_PASTURE_AUTOTYPE");
    o.cropFieldForceStart  = Raw("BANISHED_CROPFIELD_FORCE_START");
    o.orchardForceStart    = Raw("BANISHED_ORCHARD_FORCE_START");

    o.forceConstruct     = On("BANISHED_FORCE_CONSTRUCT");
    {   const char* v = Raw("BANISHED_BUILD_SYNC_LABOR");
        o.buildSyncLabor = !(v && v[0] == '0'); }        // default ON; only "=0" disables

    // ── perf: the stress town ────────────────────────────────────────────────
    o.stress          = On("BANISHED_STRESS");
    o.stressCitizens  = Int("BANISHED_STRESS_CITIZENS", o.stressCitizens);
    o.stressSets      = Int("BANISHED_STRESS_SETS",     o.stressSets);
    o.stressFrames    = Int("BANISHED_STRESS_FRAMES",   o.stressFrames);
    o.stressInterval  = Int("BANISHED_STRESS_INTERVAL", o.stressInterval);
    o.stressSpeed     = Int("BANISHED_STRESS_SPEED",    o.stressSpeed);
    o.stressFoodMult  = Int("BANISHED_STRESS_FOOD_MULT", 0);
    o.stressStaff     = Int("BANISHED_STRESS_STAFF",     1);
    o.stressCompProf  = Int("BANISHED_STRESS_COMPPROF",  1);
    if (o.stressCompProf < 0) o.stressCompProf = 0;
    if (o.stressCompProf > 4) o.stressCompProf = 4;   // 3 = level 1 + ext.fast_paths shadow mode (PFBENCH);
                                                      // 4 = level 1 + the ELPROF verification shadows
                                                      //     (StressHarness_Verify: bucket PathDistance vs AVL,
                                                      //     cached tick list vs a fresh component scan)
    o.stressCsv       = Raw("BANISHED_STRESS_CSV");
    {   const char* v = Raw("BANISHED_STRESS_HOMEWATCH");
        o.stressHomeWatch = v && std::atoi(v) != 0; }
    {   const char* v = Raw("BANISHED_STRESS_HITCH_MS");   // HITCH: the excess-over-median threshold
        if (v && v[0]) o.stressHitchMs = std::atof(v);
        if (o.stressHitchMs < 0) o.stressHitchMs = 0; }
    if (o.stressCitizens < 0) o.stressCitizens = 0;
    // stressSets < 0 is kept: |sets| sets in the spread layout (PFBENCH, stress_harness.h).
    if (o.stressInterval < 60) o.stressInterval = 60;
    if (o.stressSpeed < 1) o.stressSpeed = 1;
    if (o.stressFoodMult <= 0) o.stressFoodMult = o.stressCitizens > 10 ? o.stressCitizens / 10 : 1;

    // ── perf: profiling and frame pacing ─────────────────────────────────────
    {   const char* v = Raw("BANISHED_SAMPLE");
        if (v && v[0] != '0') o.sample = std::atoi(v) > 1 ? std::atoi(v) : 1000; }
    o.streamCacheShadow = On("BANISHED_STREAMCACHE_SHADOW");
    o.fpsCap            = Raw("BANISHED_FPS_CAP");
    o.fpsHeadroom       = Raw("BANISHED_FPS_HEADROOM");
    // (the BANISHED_PERF_* knobs are retired: options.h, PERFXEXT)

    // ── heap: the redzone allocator / watchdog ───────────────────────────────
    o.redzone           = Raw("BANISHED_REDZONE");
    {   const char* v = Raw("BANISHED_REDZONE_ABORT");
        o.redzoneAbort = !(v && v[0] == '0'); }
    o.redzoneSelfTestMs = Int("BANISHED_REDZONE_SELFTEST", 0);
    o.redzoneWatchMs    = Int("BANISHED_REDZONE_WATCH", -1);
    o.redzoneScan       = Int("BANISHED_REDZONE_SCAN", 0);
    o.redzonePad        = Int("BANISHED_REDZONE_PAD", 0);
    {   const char* z = Raw("BANISHED_REDZONE_ZERO");
        o.redzoneZero = z && z[0] != '0' && z[0] != 'f' && z[0] != 'F'; }
    {   const char* v = Raw("BANISHED_TEARDOWN_HEAPCHECK");
        o.teardownHeapCheck = v && v[0] != '0'; }

    // ── boot / engine plumbing ───────────────────────────────────────────────
    // Which flags mean "nobody is watching this run": the set
    // HarnessGuardUnattendedBoot has always used (src/app/main.cpp).
    o.unattendedBoot = o.autostart || o.autoquit || o.autoload || o.autotutorial ||
                       On("BANISHED_AUTOMENU_BUTTON") || On("BANISHED_PAUSEMENU_BUTTON");
    {   const char* v = Raw("BANISHED_DEFER_WINDOW_SHOW");
        o.deferWindowShow = !v || (std::strcmp(v, "0") != 0 && std::strcmp(v, "false") != 0); }
    {   const char* v = Raw("BANISHED_SKIP_ENGINE_LOADER");
        o.skipEngineLoader = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_NO_FOL_LOCK");
        o.noFolLock = v && v[0] != '0'; }


    // ── code/Game readers migrated off getenv (ENDGAME C.2/C.4 close) ────────
    o.buildDemoForce         = Raw("BANISHED_BUILD_DEMO_FORCE");
    o.buildDemoFrames        = Raw("BANISHED_BUILD_DEMO_FRAMES");
    o.buildDemoIndex         = Raw("BANISHED_BUILD_DEMO_INDEX");
    o.buildDemoHouse         = Raw("BANISHED_BUILD_DEMO_HOUSE");
    o.buildDemoForesterLodge = Raw("BANISHED_BUILD_DEMO_FORESTERLODGE");
    o.buildDemoMarket        = Raw("BANISHED_BUILD_DEMO_MARKET");
    o.buildDemoSchoolHouse   = Raw("BANISHED_BUILD_DEMO_SCHOOLHOUSE");
    o.buildDemoTavern        = Raw("BANISHED_BUILD_DEMO_TAVERN");
    o.buildDemoWell          = Raw("BANISHED_BUILD_DEMO_WELL");
    o.buildDemoHospital      = Raw("BANISHED_BUILD_DEMO_HOSPITAL");
    o.buildDemoCemetery      = Raw("BANISHED_BUILD_DEMO_CEMETERY");
    o.buildDemoChapel        = Raw("BANISHED_BUILD_DEMO_CHAPEL");
    o.buildDemoQuarry        = Raw("BANISHED_BUILD_DEMO_QUARRY");
    o.stressAllowPockets     = Raw("BANISHED_STRESS_ALLOW_POCKETS");
    o.stressKindMap          = Raw("BANISHED_STRESS_KINDMAP");
    o.forceWork              = Raw("BANISHED_FORCE_WORK");
    o.forceHarvest           = Raw("BANISHED_FORCE_HARVEST");
    o.forceForesterWork      = Raw("BANISHED_FORCE_FORESTERWORK");
    o.forceSnow              = Raw("BANISHED_FORCE_SNOW");
    o.forceDeviceLost        = Raw("BANISHED_FORCE_DEVICE_LOST");
    o.screenshotAt           = Raw("BANISHED_SCREENSHOT_AT");
    o.showroom               = Raw("BANISHED_SHOWROOM");
    o.captureIngame          = Raw("BANISHED_CAPTURE_INGAME");
    o.achIgnoreMods          = Raw("BANISHED_ACH_IGNORE_MODS");
    o.treeGuards             = Raw("BANISHED_TREE_GUARDS");
    o.treePrefetch           = Raw("BANISHED_TREE_PREFETCH");
    o.mapTrace               = Raw("BANISHED_MAP_TRACE");
    o.movetickTrace          = Raw("BANISHED_MOVETICK_TRACE");
    o.plagueTrace            = Raw("BANISHED_PLAGUE_TRACE");
    o.cropDiseaseTrace       = Raw("BANISHED_CROPDISEASE_TRACE");
    o.tornadoTrace           = Raw("BANISHED_TORNADO_TRACE");
    o.serializeTrace         = Raw("BANISHED_SERIALIZE_TRACE");
    o.selectDiag             = Raw("BANISHED_SELECT_DIAG");

    // ── diagnostics ──────────────────────────────────────────────────────────
    {   const char* v = Raw("BANISHED_ALLOC_STATS");
        o.allocStats = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_ALLOC_BIG");
        o.allocBig = v ? std::strtoull(v, nullptr, 0) : 0; }
    o.achTimeScale  = Raw("BANISHED_ACH_TIME_SCALE");
    o.achYearBoost  = Raw("BANISHED_ACH_YEAR_BOOST");
    o.achYearMax    = Raw("BANISHED_ACH_YEAR_MAX");
    {   const char* v = Raw("BANISHED_COMP_VALIDATE");
        o.compValidate = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_SCENE_WATCH");
        o.sceneWatch = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_SCENE_ADDTALLY");
        o.sceneAddTally = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_SCENE_CENSUS");
        o.sceneCensus = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_FORCE_LOD0");
        o.forceLod0 = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_LOD_CENSUS");
        o.lodCensus = v && v[0] != '0'; }
    {   const char* v = Raw("BANISHED_VITALS");
        o.citizenVitals = !(v && v[0] == '0'); }          // default ON, "0" disables

    // ── gameplay harnesses (raw; each parses at its own site) ────────────────
    o.builders              = Raw("BANISHED_BUILDERS");
    o.workValidateTrace     = Raw("BANISHED_WORKVALIDATE_TRACE");
    o.finishBuilds          = Raw("BANISHED_FINISH_BUILDS");
    o.autoStaff             = Raw("BANISHED_AUTOSTAFF");
    o.autoStaffTrace        = Raw("BANISHED_AUTOSTAFF_TRACE");
    o.autoStaffUnseat       = Raw("BANISHED_AUTOSTAFF_UNSEAT");
    o.killCitizens          = Raw("BANISHED_KILL_CITIZENS");
    o.killAtFrame           = Raw("BANISHED_KILL_AT_FRAME");
    o.killWhenCemeteryReady = Raw("BANISHED_KILL_WHEN_CEMETERY_READY");
    o.familyCensus          = Raw("BANISHED_FAMILY_CENSUS");
    o.killFamily            = Raw("BANISHED_KILL_FAMILY");
    o.marryHarness          = Raw("BANISHED_MARRY_HARNESS");
    o.birthTrace            = Raw("BANISHED_BIRTH_TRACE");
    o.forceBirth            = Raw("BANISHED_FORCE_BIRTH");
    o.forceBirthFrame       = Raw("BANISHED_FORCE_BIRTH_FRAME");
    o.foodPanel             = Raw("BANISHED_FOOD_PANEL");
    o.foodPanelFrame        = Raw("BANISHED_FOOD_PANEL_FRAME");
    o.foodPanelClose        = Raw("BANISHED_FOOD_PANEL_CLOSE");
    o.foodPanelClick        = Raw("BANISHED_FOOD_PANEL_CLICK");
    o.foodPanelSeat         = Raw("BANISHED_FOOD_PANEL_SEAT");
    o.foodPanelArmBuildTool = Raw("BANISHED_FOOD_PANEL_ARM_BUILDTOOL");
    o.foodPlace             = Raw("BANISHED_FOOD_PLACE");
    o.foodPlaceFrame        = Raw("BANISHED_FOOD_PLACE_FRAME");
    o.foodDiag              = Raw("BANISHED_FOOD_DIAG");
    o.foodStock             = Raw("BANISHED_FOOD_STOCK");
    o.foodWorkers           = Raw("BANISHED_FOOD_WORKERS");
    o.marketOverstock       = Raw("BANISHED_MARKET_OVERSTOCK");
    o.marketOverstockFrame  = Raw("BANISHED_MARKET_OVERSTOCK_FRAME");
    o.marketOverstockPeriod = Raw("BANISHED_MARKET_OVERSTOCK_PERIOD");
    o.orchardCut            = Raw("BANISHED_ORCHARD_CUT");
    o.orchardDiag           = Raw("BANISHED_ORCHARD_DIAG");
    o.orchardGrow           = Raw("BANISHED_ORCHARD_GROW");
    o.orchardWorkers        = Raw("BANISHED_ORCHARD_WORKERS");
    o.pastureTend           = Raw("BANISHED_PASTURE_TEND");
    o.pastureProduce        = Raw("BANISHED_PASTURE_PRODUCE");
    o.pastureFill           = Raw("BANISHED_PASTURE_FILL");
    o.tradeSell             = Raw("BANISHED_TRADE_SELL");
    o.tradeSellPerRow       = Raw("BANISHED_TRADE_SELL_PERROW");
    o.tradeSellSeed         = Raw("BANISHED_TRADE_SELL_SEED");
    o.panelHold             = Raw("BANISHED_PANEL_HOLD");
    o.panelHoldFrame        = Raw("BANISHED_PANEL_HOLD_FRAME");
    o.panelHoldClick        = Raw("BANISHED_PANEL_HOLD_CLICK");
    o.panelHoldClickEvery   = Raw("BANISHED_PANEL_HOLD_CLICK_EVERY");

    o.mouseInject  = Raw("BANISHED_MOUSE_INJECT");
    o.mouseHold    = Raw("BANISHED_MOUSE_HOLD");
    o.mouseStart   = Raw("BANISHED_MOUSE_START");
    o.mouseTrace   = Raw("BANISHED_MOUSE_TRACE");
    o.mouseTipStr  = Raw("BANISHED_MOUSE_TIPSTR");
    o.mouseControl = Raw("BANISHED_MOUSE_CONTROL");
    o.mouseTarget  = Raw("BANISHED_MOUSE_TARGET");

    o.toolHarness         = Raw("BANISHED_TOOL_HARNESS");
    o.placementTemplate   = Raw("BANISHED_PLACEMENT_TEMPLATE");
    o.placementCell       = Raw("BANISHED_PLACEMENT_CELL");
    o.placementRadius     = Raw("BANISHED_PLACEMENT_RADIUS");
    o.roadTemplate        = Raw("BANISHED_ROAD_TEMPLATE");
    o.roadRuns            = Raw("BANISHED_ROAD_RUNS");
    o.roadLen             = Raw("BANISHED_ROAD_LEN");
    o.pastureOverNaturals = Raw("BANISHED_PASTURE_OVER_NATURALS");
    o.fencedRuns          = Raw("BANISHED_FENCED_RUNS");
    o.fencedSide          = Raw("BANISHED_FENCED_SIDE");
    o.utilityTemplate     = Raw("BANISHED_UTILITY_TEMPLATE");
    o.mapTestPattern      = Raw("BANISHED_MAP_TESTPATTERN");
    o.dlgSettingsRoute    = Raw("BANISHED_DLGSETTINGS_ROUTE");
    o.housePanelIndex     = Raw("BANISHED_HOUSEPANEL_INDEX");
    o.uiCompCensusShow    = Raw("BANISHED_UICOMP_CENSUS_SHOW");
    o.pasturePanelSelect  = Raw("BANISHED_PASTUREPANEL_SELECT");
    o.orchardPanelSelect  = Raw("BANISHED_ORCHARDPANEL_SELECT");
    o.upgradeWaitFrames   = Raw("BANISHED_UPGRADE_WAIT_FRAMES");
    o.upgradeForceFrame   = Raw("BANISHED_UPGRADE_FORCE_FRAME");
    o.upgradeClickDelay   = Raw("BANISHED_UPGRADE_CLICK_DELAY");
    o.bridgeDragFrames    = Raw("BANISHED_BRIDGE_DRAG_FRAMES");
    o.bridgePoll          = Raw("BANISHED_BRIDGE_POLL");
    o.bridgeWaitFrames    = Raw("BANISHED_BRIDGE_WAIT_FRAMES");
    o.bridgeTemplate      = Raw("BANISHED_BRIDGE_TEMPLATE");
    o.bridgeLongest       = Raw("BANISHED_BRIDGE_LONGEST");
    o.bridgeSelect        = Raw("BANISHED_BRIDGE_SELECT");
    o.pastureFullDelay    = Raw("BANISHED_PASTUREFULL_DELAY");

    o.disasterCount[0] = Raw("BANISHED_FORCE_FIRE");
    o.disasterFrame[0] = Raw("BANISHED_FORCE_FIRE_FRAME");
    o.disasterPeriod[0]= Raw("BANISHED_FORCE_FIRE_PERIOD");
    o.disasterCount[1] = Raw("BANISHED_FORCE_TORNADO");
    o.disasterFrame[1] = Raw("BANISHED_FORCE_TORNADO_FRAME");
    o.disasterPeriod[1]= Raw("BANISHED_FORCE_TORNADO_PERIOD");
    o.disasterCount[2] = Raw("BANISHED_FORCE_SICK");
    o.disasterFrame[2] = Raw("BANISHED_FORCE_SICK_FRAME");
    o.disasterPeriod[2]= Raw("BANISHED_FORCE_SICK_PERIOD");
    o.disasterCount[3] = Raw("BANISHED_FORCE_INFEST");
    o.disasterFrame[3] = Raw("BANISHED_FORCE_INFEST_FRAME");
    o.disasterPeriod[3]= Raw("BANISHED_FORCE_INFEST_PERIOD");
    o.forceSickDisease = Raw("BANISHED_FORCE_SICK_DISEASE");

    o.cellDump  = Raw("BANISHED_CELL_DUMP");
    o.cellTrace = On("BANISHED_CELL_TRACE");

    // The stress town rewrites the run's world setup, so it goes LAST — and it
    // is the only thing in the process that writes the environment.
    if (o.stress) ApplyStressDefaults(o);
}
