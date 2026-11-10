// ============================================================================
// src/diag/tweak_overlay.cpp -- tweak_overlay.h (debug flavour only: CMake compiles this
// file only with BANISHED_TWEAK_OVERLAY on)
// ============================================================================
// NOT RETAIL. Block TWEAKUI; docs/reference/tweak_overlay.md (the panels, how to add a
// tweakable). Log lines: `[TweakOverlay] ...` on stderr (standalone_error.log).
//
// Dear ImGui (third_party/imgui, v1.92.9b, unmodified) with its SDL3 platform backend; the
// renderer is the graphics backend's (tweak_overlay.h RENDERERS). Four integration choices:
//   * ImGui_ImplSDL3_NewFrame is NOT called: it calls SDL video / mouse functions
//     (SDL_CaptureMouse, SDL_WarpMouseInWindow, SDL_SetCursor, the IME) that belong on the
//     main thread, and the frame is built on the presenting thread (the render thread
//     under ext.render_pipe). The frame setup it does is done here instead: DisplaySize =
//     the back buffer's size, DeltaTime from SDL_GetTicksNS. The SDL3 backend is used for
//     its Init (main thread) and ProcessEvent (pure io updates, run on the presenting
//     thread over the events the pump queued).
//   * The two SDL hints ImGui_ImplSDL3_Init sets (MOUSE_FOCUS_CLICKTHROUGH,
//     MOUSE_AUTO_CAPTURE) are put back right after it: opening the overlay must not
//     change how the game's own mouse behaves once it is closed.
//   * No imgui.ini / log file, no cursor changes, no IME / clipboard / shell callbacks.
//   * The game's input: while open the pump swallows mouse / keyboard / text events and
//     input_sdl.cpp reports every key and button released (IsOpen()).
// ============================================================================
#include "tweak_overlay.h"

#include "ext/ambient_occlusion/ambient_occlusion.h"
#include "ext/better_trees/better_trees.h"
#include "ext/registry.h"
#include "ext/better_water/better_water.h"   // StripEnabled (the Better water panel)
#include "ext/tweak/ext_tweaks.h"
#include "better_trees/foliage_aa_backend.h"   // src/render/common/better_trees: FaaBackend::Stats
#include "better_trees/billboard_shadow_backend.h"   // src/render/common/better_trees: AppliedParams
#include "graphics/SunLight.h"               // ExtTweakSunMapSize

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_thread.h>

#include <cmath>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace Diag {
namespace TweakOverlay {

namespace detail {
std::atomic<bool> g_open{false};
}

namespace {

using detail::g_open;

// ---------------------------------------------------------------------------
// The rows and keys the overlay touches (registry ids, registry.cpp's tables).
// ---------------------------------------------------------------------------
constexpr const char* kRows[] = {"ambient_occlusion", "better_trees", "better_water", "max_shadow_res", "render_pipe",
                                 "eye_camera", "winter_ice", "better_clouds", "fancy_terrain", "depth_of_field",
                                 "leaf_lod"};
constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));
constexpr const char* kKeys[] = {"ao_quality", "ao_opacity", "foliage_aa_mode", "max_shadow_size"};
constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

// ---------------------------------------------------------------------------
// Main thread -> presenting thread: the registry as the UI shows it.
// ---------------------------------------------------------------------------
struct Snapshot {
    bool faithful = true;
    bool valid    = false;
    bool rowRequested[kRowCount] = {};
    bool rowActive[kRowCount]    = {};
    bool rowLocked[kRowCount]    = {};
    int  aoQuality = 2, aoOpacity = 20, faaMode = 2;
    int  shadowChoice = 0;   // Ext::MaxShadowResChoice(): 0 off, 4096, 8192
    bool aoEnabled = false;
};

// ---------------------------------------------------------------------------
// Presenting thread -> main thread: what the panels asked for.
// ---------------------------------------------------------------------------
enum class Act { Row, Value, FaaMode, FaaSync, BbsSync, ShadowRes, Persist, Reset };
struct Action {
    Act         kind;
    const char* id;      // a kRows / kKeys entry (static), or null
    int         value;
};

std::mutex          g_mu;          // guards everything below up to g_drawMu
std::vector<SDL_Event> g_events;   // pump -> presenting thread
std::vector<Action> g_actions;     // presenting thread -> pump
Snapshot            g_snap;
bool                g_justOpened = false;
std::atomic<bool>   g_mainWork{false};   // open, or actions still queued: MainThreadTick has work

bool          g_contextReady = false;     // ImGui context + SDL3 backend (main thread, first open)
SDL_ThreadID  g_mainThread   = 0;

std::mutex     g_drawMu;                   // BeforePresent vs OnRendererShutdown
Renderer*      g_renderer    = nullptr;    // the renderer registered with ImGui (Init done)
uint64_t       g_lastTicks   = 0;
SDL_ThreadID   g_loggedDrawThread = 0;

// The presenting thread's own copies of the tweak structs (the UI edits these, then publishes).
Ext::Tweak::Ao  g_uiAo;
Ext::Tweak::Faa g_uiFaa;
Ext::Tweak::Bbs g_uiBbs;
Ext::Tweak::Water g_uiWater;
Ext::Tweak::Ice g_uiIce;
Ext::BetterClouds::Look g_uiClouds;
Ext::FancyTerrain::Look g_uiTerrain;
Ext::DepthOfField::Look g_uiDof;
Ext::LeafLod::Look g_uiLeafLod;
bool            g_uiLoaded = false;

void Queue(Act kind, const char* id, int value) {
    std::lock_guard<std::mutex> lock(g_mu);
    // A slider dragged over many frames queues one value per frame: keep only the latest.
    if (kind == Act::Value || kind == Act::FaaMode || kind == Act::FaaSync || kind == Act::BbsSync ||
        kind == Act::ShadowRes) {
        for (Action& a : g_actions)
            if (a.kind == kind && a.id == id) { a.value = value; return; }
    }
    g_actions.push_back(Action{kind, id, value});
    g_mainWork.store(true, std::memory_order_release);
}

bool IsInputEvent(uint32_t type) {
    switch (type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return true;
        default:
            return false;
    }
}

bool IsToggle(const SDL_Event& ev) {
    return ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.scancode == TWEAK_OVERLAY_KEY &&
           (ev.key.mod & TWEAK_OVERLAY_KEY_MOD) != 0;
}

// Main thread, first open: the ImGui context and the SDL3 backend.
void InitContext(SDL_Window* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nothing written into the game directory
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(1.5f);
    style.FontScaleMain = 1.5f;

    // ImGui_ImplSDL3_Init sets two process-wide SDL hints; the game's mouse keeps its own.
    const char* const kHints[] = {SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, SDL_HINT_MOUSE_AUTO_CAPTURE};
    std::string saved[2];
    bool        had[2] = {};
    for (int i = 0; i < 2; ++i) {
        const char* v = SDL_GetHint(kHints[i]);
        had[i] = v != nullptr;
        if (v) saved[i] = v;
    }
    ImGui_ImplSDL3_InitForOther(window);   // the renderer is the graphics backend's
    for (int i = 0; i < 2; ++i) {
        if (had[i]) SDL_SetHint(kHints[i], saved[i].c_str());
        else        SDL_ResetHint(kHints[i]);
    }
    // We honour neither (ImGui_ImplSDL3_NewFrame is not called, see the banner).
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos);
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_SetImeDataFn       = nullptr;
    pio.Platform_GetClipboardTextFn = nullptr;
    pio.Platform_SetClipboardTextFn = nullptr;
    pio.Platform_OpenInShellFn      = nullptr;
    g_mainThread   = SDL_GetCurrentThreadID();
    g_contextReady = true;
    std::fprintf(stderr, "[TweakOverlay] ImGui %s context created (main thread %llu)\n", ImGui::GetVersion(),
                 static_cast<unsigned long long>(g_mainThread));
}

void Snap(Snapshot& s) {
    s.faithful = Ext::CurrentProfile() == Ext::Profile::Faithful;
    for (int i = 0; i < kRowCount; ++i) {
        const Ext::Entry* e = Ext::FindEntry(kRows[i]);
        s.rowRequested[i] = e && Ext::Requested(*e);
        s.rowActive[i]    = e && Ext::Active(*e);
        s.rowLocked[i]    = e && Ext::EntryLocked(*e);
    }
    s.aoQuality = Ext::Val().ao_quality;
    s.aoOpacity = Ext::Val().ao_opacity;
    s.faaMode   = Ext::Val().foliage_aa_mode;
    s.shadowChoice = Ext::MaxShadowResChoice();
    s.aoEnabled = Ext::AmbientOcclusion::Enabled();
    s.valid     = true;
}

void LogConstants() {
    const Ext::Tweak::Ao  a = Ext::Tweak::AoValues();
    const Ext::Tweak::Faa f = Ext::Tweak::FaaValues();
    const Ext::Tweak::Bbs b = Ext::Tweak::BbsValues();
    std::fprintf(stderr,
                 "[TweakOverlay] constants: ao radiusStreet %.3f radiusOverview %.3f shadowMultiplier %.3f "
                 "shadowPower %.3f horizonAngleThreshold %.3f sharpness %.3f detailShadowStrength %.3f "
                 "blurPasses %d fogFade %d fogFromScale %.3f fogToScale %.3f fadeCeiling %.1f; "
                 "faa fillK %.3f fillFloor %.3f; bbs basis/margin/jitter/noise/scale %.2f/%.2f/%.2f/%.2f/%.0f\n",
                 a.radiusStreet, a.radiusOverview, a.shadowMultiplier, a.shadowPower, a.horizonAngleThreshold,
                 a.sharpness, a.detailShadowStrength, a.blurPasses, a.fogFade ? 1 : 0, a.fogFromScale,
                 a.fogToScale, a.fadeCeiling, f.fillK, f.fillFloor, b.p.basis, b.p.margin, b.p.jitter, b.p.noise,
                 b.p.noiseScale);
    const Ext::BetterWater::ShoreParams w = Ext::Tweak::WaterValues().p;
    std::fprintf(stderr,
                 "[TweakOverlay] constants: water lineIn %.3f lineOut0 %.3f lineOut1 %.3f linePlain %.3f lineBend %.3f "
                 "bendRadius %.3f bendFrom %.3f bendTo %.3f wetRise %.3f wetFall %.3f wetEnd %.3f wetStrength %.3f "
                 "wetShade %.3f edgeAmp %.3f edgeScale %.3f opacity %.3f\n",
                 w.lineIn, w.lineOut0, w.lineOut1, w.linePlain, w.lineBend, w.bendRadius, w.bendFrom, w.bendTo,
                 w.wetRise, w.wetFall, w.wetEnd, w.wetStrength, w.wetShade, w.edgeAmp, w.edgeScale, w.opacity);
}

// Main thread: one queued change through the extension's own live path.
void Apply(const Action& a) {
    switch (a.kind) {
        case Act::Row:
            if (const Ext::Entry* e = Ext::FindEntry(a.id)) Ext::SetAndRecord(*e, a.value != 0);
            break;
        case Act::Value:
            if (const Ext::ValueKey* k = Ext::FindValueKey(a.id)) Ext::SetValueAndRecord(*k, a.value);
            break;
        case Act::FaaMode: {
            Ext::BetterTrees::Params p;
            p.mode = a.value;
            p.naturalGrowth = Ext::Val().natural_tree_growth != 0;   // kept (NATURAL_GROWTH.md)
            Ext::BetterTrees::Set(p);   // 0 turns the row off (the Options combo's rule)
            break;
        }
        case Act::FaaSync:
            Ext::BetterTrees::Sync();   // the tweak struct's k / floor -> FaaBackend::Request
            break;
        case Act::BbsSync:
            Ext::BetterTrees::Sync();   // the tweak struct's dapple set -> BbsBackend::RequestParams (TS-TWEAK)
            break;
        case Act::ShadowRes:
            Ext::SetMaxShadowResAndRecord(a.value);   // 0 = the row off; 4096 / 8192 = the key + the row on
            break;
        case Act::Persist:
            for (const char* id : kRows)
                if (const Ext::Entry* e = Ext::FindEntry(id)) Ext::SetAndRecord(*e, Ext::Requested(*e));
            for (const char* id : kKeys)
                if (const Ext::ValueKey* k = Ext::FindValueKey(id)) Ext::SetValueAndRecord(*k, Ext::Value(*k));
            Ext::PersistSettings();
            std::fprintf(stderr, "[TweakOverlay] settings.cfg written (rows + ao_quality, ao_opacity, foliage_aa_mode, "
                                 "max_shadow_size)\n");
            LogConstants();
            break;
        case Act::Reset:
            Ext::Tweak::SetAo(Ext::Tweak::Ao{});
            Ext::Tweak::SetFaa(Ext::Tweak::Faa{});
            Ext::Tweak::SetBbs(Ext::Tweak::Bbs{});
            Ext::Tweak::SetWater(Ext::Tweak::Water{});
            for (const char* id : kKeys)
                if (const Ext::ValueKey* k = Ext::FindValueKey(id)) Ext::SetValueAndRecord(*k, k->defaultValue);
            Ext::BetterTrees::Sync();
            std::fprintf(stderr, "[TweakOverlay] reset: tweak structs and valued keys at their defaults\n");
            LogConstants();
            break;
    }
}

// ---------------------------------------------------------------------------
// The panels (presenting thread, inside NewFrame / Render).
// ---------------------------------------------------------------------------
int RowIndex(const char* id) {
    for (int i = 0; i < kRowCount; ++i)
        if (std::strcmp(kRows[i], id) == 0) return i;
    return 0;
}

void RowCheckbox(const Snapshot& s, int i, const char* where) {   // `where`: a unique ImGui id per panel
    bool on = s.rowRequested[i];
    char label[96];
    std::snprintf(label, sizeof label, "%s%s##%s%d", kRows[i],
                  s.rowLocked[i] ? " (locked for this game)" : (on != s.rowActive[i] ? " (pending)" : ""), where, i);
    if (ImGui::Checkbox(label, &on)) Queue(Act::Row, kRows[i], on ? 1 : 0);
}

void DrawPanels(const Snapshot& s) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tweak overlay (Alt+F10)", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    // Stats line.
    const FaaBackend::TweakStats faa = FaaBackend::Stats();
    const int32_t sunMap = Graphics::SunLight::ExtTweakSunMapSize();
    ImGui::Text("%.1f fps  %.2f ms   sun map %dx%d   FAA gen %u mode %d: %d/%d blobs transformed (%d programs), "
                "%lld/%lld binds",
                io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f, sunMap, sunMap, faa.gen, faa.mode,
                faa.blobsTransformed, faa.blobsSeen, faa.programsTransformed, static_cast<long long>(faa.xfBinds),
                static_cast<long long>(faa.pixelBinds));
    ImGui::Separator();

    if (s.faithful) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "faithful profile: nothing to tweak");
    ImGui::BeginDisabled(s.faithful || !s.valid);

    if (ImGui::CollapsingHeader("Rows", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < kRowCount; ++i) RowCheckbox(s, i, "rows");
    }

    // ext.better_water (BETTERWATER): the shore program's look, live (Ext::BetterWater::ShoreParams, read by
    // DrawShore every water range). Depths in frame-alpha units: 0 = the bank, 1 = about 1.5 world units down.
    if (ImGui::CollapsingHeader("Better water", ImGuiTreeNodeFlags_DefaultOpen)) {
        Ext::BetterWater::ShoreParams& w = g_uiWater.p;
        bool changed = false;
        ImGui::Text("row %s", s.rowRequested[RowIndex("better_water")] ? "on" : "off");
        changed |= ImGui::SliderFloat("overall opacity", &w.opacity, 0.0f, 2.0f, "%.2f");
        ImGui::SeparatorText("wet bed (the darker band under the shallows)");
        changed |= ImGui::SliderFloat("wet strength", &w.wetStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("wet shade (bed brightness)", &w.wetShade, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("wet rise (fade-in depth)", &w.wetRise, 0.0f, 0.5f, "%.3f");
        changed |= ImGui::SliderFloat("wet band width (holds to depth)", &w.wetFall, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("wet end (gone by depth)", &w.wetEnd, 0.0f, 1.5f, "%.3f");
        ImGui::SeparatorText("line at the waterline");
        changed |= ImGui::SliderFloat("line opacity (plain bank)", &w.linePlain, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("line opacity (outside of bends)", &w.lineBend, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("line fade-in depth", &w.lineIn, 0.0f, 0.2f, "%.3f");
        changed |= ImGui::SliderFloat("line fade-out from", &w.lineOut0, 0.0f, 0.5f, "%.3f");
        changed |= ImGui::SliderFloat("line fade-out to (band size)", &w.lineOut1, 0.0f, 0.6f, "%.3f");
        ImGui::SeparatorText("bends (where the line turns white)");
        changed |= ImGui::SliderFloat("bend ring radius (world units)", &w.bendRadius, 0.5f, 8.0f, "%.2f");
        changed |= ImGui::SliderFloat("bend from (land share, 0.5 = straight)", &w.bendFrom, 0.3f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("bend to", &w.bendTo, 0.3f, 1.0f, "%.3f");
        ImGui::SeparatorText("edge");
        changed |= ImGui::SliderFloat("waterline wander (depth)", &w.edgeAmp, 0.0f, 0.3f, "%.3f");
        changed |= ImGui::SliderFloat("wander scale (cells per world unit)", &w.edgeScale, 0.02f, 2.0f, "%.3f");
        ImGui::SeparatorText("Fancy: the shore strip (bank side; heights in world units)");
        Ext::BetterWater::StripParams& t = g_uiWater.strip;
        ImGui::Text("level %s", Ext::BetterWater::StripEnabled() ? "Fancy (strip drawn)" : "Plain (no strip)");
        changed |= ImGui::SliderFloat("strip opacity", &t.opacity, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("wet bank strength", &t.wetStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("wet bank shade", &t.wetShade, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("wet bank top (height)", &t.wetTop, 0.0f, 0.6f, "%.3f");
        changed |= ImGui::SliderFloat("wet bank top breakup", &t.wetBreakup, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("foam line drop (below the true waterline)", &t.lineDrop, 0.0f, 0.5f, "%.3f");
        changed |= ImGui::SliderFloat("foam fringe strength", &t.foamStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("foam fringe above its line", &t.foamUp, 0.0f, 0.3f, "%.3f");
        changed |= ImGui::SliderFloat("foam fringe below its line", &t.foamDown, 0.0f, 0.3f, "%.3f");
        changed |= ImGui::SliderFloat("foam fringe breakup scale", &t.foamScale, 0.1f, 4.0f, "%.2f");
        changed |= ImGui::SliderFloat("strip waterline wander", &t.wanderAmp, 0.0f, 0.3f, "%.3f");
        changed |= ImGui::SliderFloat("strip wander scale", &t.wanderScale, 0.02f, 2.0f, "%.3f");
        changed |= ImGui::SliderFloat("strip fade into the water", &t.waterFade, 0.01f, 0.3f, "%.3f");
        ImGui::SeparatorText("Fancy: contact foam (piers, bridge feet, boats; world units)");
        Ext::BetterWater::ContactParams& cf = g_uiWater.contact;
        ImGui::Text("contact foam %s", Ext::BetterWater::ContactEnabled() ? "on" : "off (Plain, or this backend copies no depth)");
        changed |= ImGui::SliderFloat("contact opacity", &cf.opacity, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("contact band (depth at the waterline)", &cf.width, 0.02f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("contact line strength", &cf.strength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("spread onto the water (radius)", &cf.ringRadius, 0.1f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("spread strength", &cf.ringStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("contact breakup scale", &cf.breakupScale, 0.1f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat("contact breakup amount", &cf.breakup, 0.0f, 1.0f, "%.2f");
        if (changed) Ext::Tweak::SetWater(g_uiWater);
        if (ImGui::Button("water defaults")) {
            g_uiWater = Ext::Tweak::Water{};
            Ext::Tweak::SetWater(g_uiWater);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("copy into src/ext/better_water/shore_params.h:");
        ImGui::Text("line in/out0/out1/plain %.3f/%.3f/%.3f/%.2f  bend %.2f r %.2f from/to %.3f/%.3f", w.lineIn,
                    w.lineOut0, w.lineOut1, w.linePlain, w.lineBend, w.bendRadius, w.bendFrom, w.bendTo);
        ImGui::Text("wet rise/fall/end/strength/shade %.3f/%.3f/%.3f/%.2f/%.2f  edge %.3f/%.3f  opacity %.2f", w.wetRise,
                    w.wetFall, w.wetEnd, w.wetStrength, w.wetShade, w.edgeAmp, w.edgeScale, w.opacity);
        ImGui::Text("strip wet top/strength/shade/breakup %.3f/%.2f/%.2f/%.2f  foam up/down/strength/scale "
                    "%.3f/%.3f/%.2f/%.2f  wander %.3f/%.3f  fade %.3f  opacity %.2f  drop %.3f",
                    t.wetTop, t.wetStrength, t.wetShade, t.wetBreakup, t.foamUp, t.foamDown, t.foamStrength,
                    t.foamScale, t.wanderAmp, t.wanderScale, t.waterFade, t.opacity, t.lineDrop);
        ImGui::Text("contact width/strength/ring/ringStrength %.3f/%.2f/%.2f/%.2f  breakup %.2f/%.2f  opacity %.2f",
                    cf.width, cf.strength, cf.ringRadius, cf.ringStrength, cf.breakupScale, cf.breakup, cf.opacity);
    }

    // ext.winter_ice (WINTERICE): the ice program's look, live (Ext::WinterIce::IceParams, read by DrawIce
    // every lake range), and a preview of the fronts and the snow cover so the ice can be judged in any
    // season. Depths in frame-alpha units: 0 = the bank, 1 = about 1.5 world units down.
    if (ImGui::CollapsingHeader("Winter ice", ImGuiTreeNodeFlags_DefaultOpen)) {
        Ext::WinterIce::IceParams& p = g_uiIce.p;
        bool changed = false;
        ImGui::Text("row %s", s.rowRequested[RowIndex("winter_ice")] ? "on" : "off (tick winter_ice under Rows)");
        ImGui::SeparatorText("preview (drawing only; replaces the weather's ice)");
        changed |= ImGui::Checkbox("preview", &g_uiIce.preview);
        changed |= ImGui::SliderFloat("freeze front (1..1.65 = the middle closes)", &g_uiIce.freeze, 0.0f, 1.75f, "%.2f");
        changed |= ImGui::SliderFloat("thaw front (sides open)", &g_uiIce.thaw, 0.0f, 1.65f, "%.2f");
        changed |= ImGui::SliderFloat("snow cover (-1 = the weather's)", &g_uiIce.snow, -1.0f, 1.0f, "%.2f");
        ImGui::SeparatorText("look");
        changed |= ImGui::SliderFloat("texture tile (world units)", &p.tile, 2.0f, 40.0f, "%.1f");
        changed |= ImGui::SliderFloat("normal strength", &p.normal, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("inner layer depth", &p.inner, 0.0f, 1.5f, "%.2f");
        changed |= ImGui::SliderFloat("frost haze", &p.frost, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("sheen (reflection)", &p.reflect, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::ColorEdit3("thin ice colour", &p.thinR);
        changed |= ImGui::SliderFloat("front wander (depth)", &p.edgeAmp, 0.0f, 0.4f, "%.3f");
        changed |= ImGui::SliderFloat("boat channel half width (cells)", &g_uiIce.channel.halfWidth, 0.5f, 6.0f, "%.2f");
        changed |= ImGui::SliderFloat("boat channel centre pull", &g_uiIce.channel.centrePull, 0.0f, 4.0f, "%.2f");
        changed |= ImGui::SliderFloat("boat channel smoothing (cells per key)", &g_uiIce.channel.smoothing, 1.0f, 12.0f, "%.1f");
        changed |= ImGui::SliderFloat("open edge softness", &p.edgeSoft, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("open edge wobble", &g_uiIce.edge.wobble, 0.0f, 1.5f, "%.2f");
        changed |= ImGui::SliderFloat("open edge wobble size (cells)", &g_uiIce.edge.wobbleSize, 0.5f, 12.0f, "%.2f");
        changed |= ImGui::SliderFloat("open edge grain", &g_uiIce.edge.grain, 0.0f, 1.0f, "%.2f");
        ImGui::SeparatorText("snow (times the snow cover)");
        changed |= ImGui::SliderFloat("snow banked at the shore", &p.bankSnow, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("snow patches on the ice", &p.dusting, 0.0f, 2.0f, "%.2f");
        if (changed) Ext::Tweak::SetIce(g_uiIce);
        if (ImGui::Button("ice defaults")) {
            g_uiIce = Ext::Tweak::Ice{};
            Ext::Tweak::SetIce(g_uiIce);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("copy into src/ext/winter_ice/winter_ice.h (IceParams):");
        ImGui::Text("bankSnow %.2f tile %.1f edgeAmp %.3f thin %.3f/%.3f/%.3f reflect %.2f", p.bankSnow, p.tile,
                    p.edgeAmp, p.thinR, p.thinG, p.thinB, p.reflect);
        ImGui::Text("frost %.2f normal %.2f inner %.2f dusting %.2f",
                    p.frost, p.normal, p.inner, p.dusting);
        ImGui::Text("channel width %.2f pull %.2f smoothing %.1f  edge soft %.2f wobble %.2f size %.2f grain %.2f",
                    g_uiIce.channel.halfWidth, g_uiIce.channel.centrePull, g_uiIce.channel.smoothing, p.edgeSoft,
                    g_uiIce.edge.wobble, g_uiIce.edge.wobbleSize, g_uiIce.edge.grain);
    }

    // ext.fancy_terrain: the terrain light's look, live (Ext::FancyTerrain::Look, read every frame; a new
    // radius rebuilds the horizon map).
    if (ImGui::CollapsingHeader("Fancy terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
        Ext::FancyTerrain::Look& t = g_uiTerrain;
        bool changed = false;
        ImGui::Text("row %s", s.rowRequested[RowIndex("fancy_terrain")] ? "on" : "off (tick fancy_terrain under Rows)");
        changed |= ImGui::SliderFloat("ambient share of the light", &t.ambientShare, 0.05f, 0.95f, "%.2f");
        changed |= ImGui::SliderFloat("sky visibility strength", &t.skyStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("sun shadow strength (far)", &t.sunStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("sun shadow share near the camera", &t.sunNear, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("full sun shadow from (distance)", &t.nearDistance, 0.0f, 400.0f, "%.0f");
        changed |= ImGui::SliderFloat("sun edge softness (rad)", &t.softness, 0.005f, 0.3f, "%.3f");
        changed |= ImGui::SliderFloat("fade above the ground (units)", &t.heightFade, 0.5f, 40.0f, "%.1f");
        changed |= ImGui::SliderFloat("bounce", &t.bounce, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("fog fade range (x the fog's)", &t.fogScale, 0.5f, 6.0f, "%.2f");
        changed |= ImGui::ColorEdit3("bounce colour", t.tint);
        changed |= ImGui::SliderInt("horizon radius (rebuilds)", &t.radius, 8, 192);
        if (changed) Ext::Tweak::SetTerrain(t);
        if (ImGui::Button("terrain defaults")) {
            t = Ext::FancyTerrain::Look{};
            Ext::Tweak::SetTerrain(t);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("copy into src/ext/fancy_terrain/terrain_params.h (Look):");
        ImGui::Text("sky %.2f sun %.2f near %.2f nearDist %.0f soft %.3f fade %.1f bounce %.2f tint %.2f %.2f %.2f",
                    t.skyStrength, t.sunStrength, t.sunNear, t.nearDistance, t.softness, t.heightFade, t.bounce,
                    t.tint[0], t.tint[1], t.tint[2]);
        ImGui::Text("ambientShare %.2f fogScale %.2f radius %d", t.ambientShare, t.fogScale, t.radius);
    }

    // ext.leaf_lod (LEAF_LOD.md): the zoom range and the reduced canopies, live (keep / grow rebuild them).
    if (ImGui::CollapsingHeader("Lighter far trees", ImGuiTreeNodeFlags_DefaultOpen)) {
        Ext::LeafLod::Look& t = g_uiLeafLod;
        bool changed = false;
        ImGui::Text("row %s", s.rowRequested[RowIndex("leaf_lod")] ? "on" : "off (tick leaf_lod under Rows)");
        changed |= ImGui::SliderFloat("first level from zoom (units)", &t.start, 20.0f, 300.0f, "%.0f");
        changed |= ImGui::SliderFloat("last level from zoom (units)", &t.end, 20.0f, 300.0f, "%.0f");
        changed |= ImGui::SliderFloat("leaf cards kept at the last level", &t.keep, 0.05f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("kept cards grow (1 = same total area)", &t.grow, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("outline may move in by (share of crown)", &t.outline, 0.0f, 0.20f, "%.3f");
        changed |= ImGui::SliderInt("force level (-1 = by zoom)", &t.force, -1, Ext::LeafLod::kLevels);
        if (changed) Ext::Tweak::SetLeafLod(t);
        if (ImGui::Button("leaf lod defaults")) {
            t = Ext::LeafLod::Look{};
            Ext::Tweak::SetLeafLod(t);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("copy into src/ext/leaf_lod/leaf_lod_params.h (Look):");
        ImGui::Text("start %.0f end %.0f keep %.2f grow %.2f outline %.3f", t.start, t.end, t.keep, t.grow,
                    t.outline);
    }

    // ext.depth_of_field (DOF.md): the depth blur's look and focus, live (Ext::DepthOfField::Look, read every
    // frame).
    if (ImGui::CollapsingHeader("Depth blur", ImGuiTreeNodeFlags_DefaultOpen)) {
        Ext::DepthOfField::Look& d = g_uiDof;
        bool changed = false;
        ImGui::Text("row %s", s.rowRequested[RowIndex("depth_of_field")] ? "on" : "off (tick depth_of_field under Rows)");
        static const char* const kFocusModes[] = { "camera target", "screen centre", "manual distance" };
        changed |= ImGui::Combo("focus", &d.focusMode, kFocusModes, 3);
        if (d.focusMode == static_cast<int>(Ext::DepthOfField::FocusMode::CameraTarget))
            changed |= ImGui::SliderFloat("focus x target distance", &d.focusScale, 0.5f, 1.5f, "%.2f");
        if (d.focusMode == static_cast<int>(Ext::DepthOfField::FocusMode::Manual))
            changed |= ImGui::SliderFloat("focus distance (units)", &d.focusDistance, 5.0f, 400.0f, "%.0f");
        changed |= ImGui::SliderFloat("strength (gain)", &d.gain, 0.0f, 10.0f, "%.2f");
        changed |= ImGui::SliderFloat("max blur (half px @1080)", &d.maxCoc, 1.0f, 24.0f, "%.1f");
        changed |= ImGui::SliderFloat("in-focus band", &d.band, 0.0f, 0.5f, "%.3f");
        changed |= ImGui::SliderFloat("highlight boost", &d.boost, 0.0f, 10.0f, "%.2f");
        changed |= ImGui::SliderFloat("highlight threshold (luma)", &d.threshold, 0.0f, 0.99f, "%.2f");
        changed |= ImGui::Checkbox("show blur map (red far, blue near)", &d.showCoc);
        if (changed) Ext::Tweak::SetDof(d);
        if (ImGui::Button("depth blur defaults")) {
            d = Ext::DepthOfField::Look{};
            Ext::Tweak::SetDof(d);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("copy into src/ext/depth_of_field/dof_params.h (Look):");
        ImGui::Text("gain %.2f maxCoc %.1f band %.3f boost %.2f threshold %.2f focusMode %d focusScale %.2f "
                    "focusDistance %.0f", d.gain, d.maxCoc, d.band, d.boost, d.threshold, d.focusMode, d.focusScale,
                    d.focusDistance);
    }

    // ext.better_clouds (BETTERCLOUDS): the sky program's look, live (Ext::BetterClouds::Look, published
    // by BeginFrame every frame); the reflection in the water uses the same values.
    if (ImGui::CollapsingHeader("Better clouds", ImGuiTreeNodeFlags_DefaultOpen)) {
        Ext::BetterClouds::Look& c = g_uiClouds;
        bool changed = false;
        ImGui::Text("row %s", s.rowRequested[RowIndex("better_clouds")] ? "on" : "off (tick better_clouds under Rows)");
        changed |= ImGui::SliderFloat("cloud white (lit side)", &c.white, 0.5f, 1.2f, "%.2f");
        changed |= ImGui::SliderFloat("shaded side (x white)", &c.shade, 0.3f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("sky lift (toward white)", &c.skyLift, 0.0f, 0.6f, "%.2f");
        changed |= ImGui::SliderFloat("coverage (+ the weather's)", &c.coverage, -0.4f, 0.4f, "%.2f");
        changed |= ImGui::SliderFloat("layer scale (smaller = bigger clouds)", &c.scale, 0.05f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("wind speed (x)", &c.speed, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat("morph (shapes change as they travel)", &c.morph, 0.0f, 1.0f, "%.2f");
        ImGui::SeparatorText("Fancy: shadows on the ground (mode 2, DX11 / Vulkan)");
        changed |= ImGui::SliderFloat("shadow strength", &c.shadowStrength, 0.0f, 0.8f, "%.2f");
        changed |= ImGui::SliderFloat("shadow scale (tiles per unit)", &c.shadowScale, 0.001f, 0.03f, "%.4f");
        changed |= ImGui::SliderFloat("cloud height (sun slant)", &c.shadowHeight, 0.0f, 200.0f, "%.0f");
        changed |= ImGui::SliderFloat("shadow edge softness", &c.shadowSoftness, 0.05f, 1.0f, "%.2f");
        if (changed) Ext::Tweak::SetClouds(c);
        if (ImGui::Button("clouds defaults")) {
            c = Ext::BetterClouds::Look{};
            Ext::Tweak::SetClouds(c);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("copy into src/ext/better_clouds/cloud_params.h (Look):");
        ImGui::Text("white %.2f shade %.2f skyLift %.2f coverage %.2f scale %.3f speed %.2f morph %.2f", c.white,
                    c.shade, c.skyLift, c.coverage, c.scale, c.speed, c.morph);
        ImGui::Text("shadowStrength %.2f shadowScale %.4f shadowHeight %.0f shadowSoftness %.2f", c.shadowStrength,
                    c.shadowScale, c.shadowHeight, c.shadowSoftness);
    }

    if (ImGui::CollapsingHeader("Ambient occlusion")) {
        ImGui::Text("row %s, pass %s", s.rowRequested[RowIndex("ambient_occlusion")] ? "on" : "off",
                    s.aoEnabled ? "enabled" : "inactive");
        int tier = s.aoQuality;
        const char* const kTiers[] = {"(0)", "1 Low", "2 Medium", "3 High", "4 Lowest"};
        if (ImGui::SliderInt("quality (ext.ao_quality)", &tier, 1, 4, kTiers[tier >= 0 && tier <= 4 ? tier : 0]))
            Queue(Act::Value, "ao_quality", tier);
        int strength = s.aoOpacity;
        if (ImGui::SliderInt("strength (ext.ao_opacity)", &strength, 0, 30)) Queue(Act::Value, "ao_opacity", strength);
        bool aoChanged = false;
        aoChanged |= ImGui::SliderFloat("radius street", &g_uiAo.radiusStreet, 0.1f, 10.0f, "%.2f");
        aoChanged |= ImGui::SliderFloat("radius overview", &g_uiAo.radiusOverview, 0.1f, 20.0f, "%.2f");
        aoChanged |= ImGui::SliderFloat("shadow multiplier", &g_uiAo.shadowMultiplier, 0.0f, 5.0f, "%.2f");
        aoChanged |= ImGui::SliderFloat("shadow power", &g_uiAo.shadowPower, 0.5f, 5.0f, "%.2f");
        aoChanged |= ImGui::SliderFloat("horizon angle threshold", &g_uiAo.horizonAngleThreshold, 0.0f, 0.2f, "%.3f");
        aoChanged |= ImGui::SliderInt("blur passes (-1 = tier)", &g_uiAo.blurPasses, -1, 6);
        aoChanged |= ImGui::SliderFloat("sharpness", &g_uiAo.sharpness, 0.0f, 1.0f, "%.3f");
        aoChanged |= ImGui::SliderFloat("detail shadow strength", &g_uiAo.detailShadowStrength, 0.0f, 5.0f, "%.2f");
        aoChanged |= ImGui::Checkbox("fade with the fog", &g_uiAo.fogFade);
        aoChanged |= ImGui::SliderFloat("fog fade from x", &g_uiAo.fogFromScale, 0.0f, 2.0f, "%.2f");
        aoChanged |= ImGui::SliderFloat("fog fade to x", &g_uiAo.fogToScale, 0.1f, 2.0f, "%.2f");
        aoChanged |= ImGui::SliderFloat("fade ceiling", &g_uiAo.fadeCeiling, 50.0f, 510.0f, "%.0f");
        if (aoChanged) Ext::Tweak::SetAo(g_uiAo);   // read by the next frame's FrameFor / FadeFromFog
    }

    if (ImGui::CollapsingHeader("Foliage")) {
        const bool faaRow = s.rowRequested[RowIndex("better_trees")];
        int mode = faaRow ? s.faaMode : 0;
        const char* const kModes[] = {"0 off", "1 smooth edges", "2 + distance fill"};
        if (ImGui::SliderInt("mode (ext.foliage_aa_mode)", &mode, 0, 2, kModes[mode >= 0 && mode <= 2 ? mode : 0]))
            Queue(Act::FaaMode, nullptr, mode);
        // k / floor: applied on release (a change re-creates the 24 transformed shaders).
        ImGui::SliderFloat("fill k", &g_uiFaa.fillK, 0.0f, 1.0f, "%.3f");
        const bool kDone = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("fill floor", &g_uiFaa.fillFloor, 0.0f, 1.0f, "%.3f");
        const bool floorDone = ImGui::IsItemDeactivatedAfterEdit();
        if (kDone || floorDone) {
            Ext::Tweak::SetFaa(g_uiFaa);
            Queue(Act::FaaSync, nullptr, 0);
        }
        ImGui::TextDisabled("(no fwidth clamp: the rewriter has no hook for it; k and floor only)");
        ImGui::Text("stage: requested mode %d gen %u, applied mode %d gen %u, MSAA %s, %s, k %.3f floor %.3f",
                    faa.reqMode, faa.reqGen, faa.mode, faa.gen, faa.msaa ? "on" : "off",
                    faa.mode == 0 ? "retail" : (faa.coverage ? "A2C" : "discard kept"), faa.fillK, faa.fillFloor);
    }

    if (ImGui::CollapsingHeader("Billboard shadow")) {
        // TS-TWEAK / TS-DAPPLE: the rewrite's three immediates; each applied on release (a change
        // re-creates the rewritten copies once, through the stage's reset path), snapped to 0.05.
        RowCheckbox(s, RowIndex("better_trees"), "better_trees");
        bool changed = false;
        auto slider = [&](const char* label, float* v, float lo, float hi) {
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::SliderFloat(label, v, lo, hi, "%.2f")) *v = std::round(*v / 0.05f) * 0.05f;
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        };
        slider("basis (0 = P's depth, retail; 1 = card centre's)", &g_uiBbs.p.basis, 0.0f, 1.0f);
        slider("margin k (mean offset toward the sun, in half extents)", &g_uiBbs.p.margin, -1.0f, 1.5f);
        slider("jitter (per-card spread of the cut, in half extents)", &g_uiBbs.p.jitter, 0.0f, 1.5f);
        slider("noise (per-texel edge dapple, in half extents)", &g_uiBbs.p.noise, 0.0f, 1.5f);
        {
            // noiseScale: cells per atlas uv unit, 8..256, snapped to a multiple of 8, applied on release.
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::SliderFloat("noise scale (cells per atlas uv unit)", &g_uiBbs.p.noiseScale, 8.0f, 256.0f, "%.0f"))
                g_uiBbs.p.noiseScale = std::round(g_uiBbs.p.noiseScale / 8.0f) * 8.0f;
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        }
        if (changed) {
            Ext::Tweak::SetBbs(g_uiBbs);
            Queue(Act::BbsSync, nullptr, 0);
        }
        if (ImGui::SmallButton("default##bbs")) {
            g_uiBbs = Ext::Tweak::Bbs{};
            Ext::Tweak::SetBbs(g_uiBbs);
            Queue(Act::BbsSync, nullptr, 0);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("retail (0/0/0/0)##bbs")) {
            g_uiBbs.p = BbsRewrite::kRetailParams;
            Ext::Tweak::SetBbs(g_uiBbs);
            Queue(Act::BbsSync, nullptr, 0);
        }
        const BbsRewrite::Params ap = BbsBackend::AppliedParams();
        ImGui::Text("stage: copies made with basis/margin/jitter/noise/scale %.2f/%.2f/%.2f/%.2f/%.0f (shipped %.2f/%.2f/%.2f/%.2f/%.0f)",
                    static_cast<double>(ap.basis), static_cast<double>(ap.margin), static_cast<double>(ap.jitter),
                    static_cast<double>(ap.noise), static_cast<double>(ap.noiseScale),
                    static_cast<double>(BbsRewrite::kDefaultParams.basis), static_cast<double>(BbsRewrite::kDefaultParams.margin),
                    static_cast<double>(BbsRewrite::kDefaultParams.jitter), static_cast<double>(BbsRewrite::kDefaultParams.noise),
                    static_cast<double>(BbsRewrite::kDefaultParams.noiseScale));
    }

    if (ImGui::CollapsingHeader("Shadow")) {
        // The Enhanced tab's three-way choice: Off = the row off, 4096 / 8192 = ext.max_shadow_size + the row on.
        int choice = s.shadowChoice == 8192 ? 2 : (s.shadowChoice == 4096 ? 1 : 0);
        const char* const kChoices[] = {"Off", "4096", "8192"};
        if (ImGui::Combo("max shadow resolution (ext.max_shadow_res + ext.max_shadow_size)", &choice, kChoices, 3))
            Queue(Act::ShadowRes, nullptr, choice == 2 ? 8192 : (choice == 1 ? 4096 : 0));
        ImGui::Text("sun map %dx%d (the last [ShadowRes] line; High only, Medium / Low stay 1024 / 512)", sunMap,
                    sunMap);
    }

    ImGui::Separator();
    if (ImGui::Button("Write to settings.cfg")) Queue(Act::Persist, nullptr, 0);
    ImGui::SameLine();
    if (ImGui::Button("Reset defaults")) {
        g_uiAo  = Ext::Tweak::Ao{};
        g_uiFaa = Ext::Tweak::Faa{};
        g_uiBbs = Ext::Tweak::Bbs{};
        g_uiWater = Ext::Tweak::Water{};
        Queue(Act::Reset, nullptr, 0);
    }
    ImGui::TextDisabled("no settings key (copy into ao_math.h / better_trees.h / receiver_rewrite.h):");
    ImGui::Text("ao radius %.3f / %.3f  multiplier %.3f  power %.3f  horizon %.3f  blur %d",
                g_uiAo.radiusStreet, g_uiAo.radiusOverview, g_uiAo.shadowMultiplier, g_uiAo.shadowPower,
                g_uiAo.horizonAngleThreshold, g_uiAo.blurPasses);
    ImGui::Text("ao sharpness %.3f  detail %.3f  fog fade %d from x%.2f to x%.2f ceiling %.0f", g_uiAo.sharpness,
                g_uiAo.detailShadowStrength, g_uiAo.fogFade ? 1 : 0, g_uiAo.fogFromScale, g_uiAo.fogToScale,
                g_uiAo.fadeCeiling);
    ImGui::Text("faa kDistanceFillK %.3f  kDistanceFillFloor %.3f", g_uiFaa.fillK, g_uiFaa.fillFloor);
    ImGui::Text("bbs basis/margin/jitter/noise/scale %.2f/%.2f/%.2f/%.2f/%.0f", static_cast<double>(g_uiBbs.p.basis), static_cast<double>(g_uiBbs.p.margin), static_cast<double>(g_uiBbs.p.jitter), static_cast<double>(g_uiBbs.p.noise), static_cast<double>(g_uiBbs.p.noiseScale));

    ImGui::EndDisabled();
    ImGui::End();
}

}  // namespace

// ---------------------------------------------------------------------------
// Pump thread
// ---------------------------------------------------------------------------
bool OnSdlEvent(const SDL_Event& ev) {
    if (IsToggle(ev)) {
        const bool open = !g_open.load(std::memory_order_acquire);
        if (open && !g_contextReady) InitContext(SDL_GetWindowFromEvent(&ev));
        if (open) {
            std::lock_guard<std::mutex> lock(g_mu);
            g_events.clear();
            Snap(g_snap);
            g_justOpened = true;
            // Where the cursor is now: ImGui has seen no motion since it was last open.
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            SDL_Event m;
            SDL_zero(m);
            m.type = SDL_EVENT_MOUSE_MOTION;
            m.motion.windowID = ev.key.windowID;
            m.motion.x = mx;
            m.motion.y = my;
            g_events.push_back(m);
            g_mainWork.store(true, std::memory_order_release);
        }
        g_open.store(open, std::memory_order_release);
        std::fprintf(stderr, "[TweakOverlay] %s (Alt+F10)\n", open ? "open" : "closed");
        return true;
    }
    if (!g_open.load(std::memory_order_acquire) || !IsInputEvent(ev.type)) return false;
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_events.size() < 4096) g_events.push_back(ev);
    return true;
}

void MainThreadTick() {
    if (!g_mainWork.load(std::memory_order_acquire)) return;
    std::vector<Action> todo;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        todo.swap(g_actions);
    }
    for (const Action& a : todo) Apply(a);
    std::lock_guard<std::mutex> lock(g_mu);
    Snap(g_snap);
    if (!g_open.load(std::memory_order_acquire) && g_actions.empty())
        g_mainWork.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// The presenting thread
// ---------------------------------------------------------------------------
void BeforePresent(Renderer& renderer, unsigned width, unsigned height) {
    if (!g_open.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> draw(g_drawMu);
    // A frame presented while the device is being rebuilt has no size: nothing to draw into.
    if (!g_contextReady || width == 0 || height == 0) return;

    if (g_renderer != &renderer) {
        if (g_renderer != nullptr) {
            std::fprintf(stderr, "[TweakOverlay] %s renderer released (now %s)\n", g_renderer->Name(), renderer.Name());
            g_renderer->Shutdown();
            g_renderer = nullptr;
        }
        if (!renderer.Init()) return;
        g_renderer = &renderer;
        std::fprintf(stderr, "[TweakOverlay] %s renderer ready (thread %llu)\n", renderer.Name(),
                     static_cast<unsigned long long>(SDL_GetCurrentThreadID()));
    }
    if (!g_uiLoaded) {
        g_uiAo  = Ext::Tweak::AoValues();
        g_uiFaa = Ext::Tweak::FaaValues();
        g_uiBbs = Ext::Tweak::BbsValues();
        g_uiWater = Ext::Tweak::WaterValues();
        g_uiIce = Ext::Tweak::IceValues();
        g_uiClouds = Ext::Tweak::CloudsValues();
        g_uiTerrain = Ext::Tweak::TerrainValues();
        g_uiDof = Ext::Tweak::DofValues();
        g_uiLeafLod = Ext::Tweak::LeafLodValues();
        g_uiLoaded = true;
    }

    // Input and the registry snapshot, taken from the pump thread's queue.
    std::vector<SDL_Event> events;
    Snapshot snap;
    bool justOpened = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        events.swap(g_events);
        snap = g_snap;
        justOpened = g_justOpened;
        g_justOpened = false;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (justOpened) {
        // Keys / buttons held when it was last closed were released while it was closed.
        io.ClearInputKeys();
        io.ClearInputMouse();
        g_lastTicks = 0;
    }
    for (const SDL_Event& e : events) ImGui_ImplSDL3_ProcessEvent(&e);

    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const uint64_t now = SDL_GetTicksNS();
    float dt = g_lastTicks != 0 && now > g_lastTicks ? static_cast<float>(static_cast<double>(now - g_lastTicks) * 1e-9)
                                                     : 1.0f / 60.0f;
    if (dt <= 0.0f) dt = 1e-4f;
    io.DeltaTime = dt;
    g_lastTicks = now;

    renderer.NewFrame();
    ImGui::NewFrame();
    DrawPanels(snap);
    ImGui::Render();
    renderer.Render(ImGui::GetDrawData());

    const SDL_ThreadID self = SDL_GetCurrentThreadID();
    if (self != g_loggedDrawThread) {
        g_loggedDrawThread = self;
        std::fprintf(stderr, "[TweakOverlay] %s drawing on thread %llu (%s; ext.render_pipe=%d) at %ux%u\n",
                     renderer.Name(), static_cast<unsigned long long>(self),
                     self == g_mainThread ? "the main thread" : "NOT the main thread", Ext::Ext().render_pipe ? 1 : 0,
                     width, height);
    }
}

void OnRendererShutdown(Renderer& renderer) {
    std::lock_guard<std::mutex> draw(g_drawMu);
    if (g_renderer != &renderer) return;
    renderer.Shutdown();
    g_renderer = nullptr;
    std::fprintf(stderr, "[TweakOverlay] %s renderer released (device shutdown)\n", renderer.Name());
}

}  // namespace TweakOverlay
}  // namespace Diag
