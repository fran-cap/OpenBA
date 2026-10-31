// ============================================================================
// src/diag/tweak_overlay.cpp -- tweak_overlay.h (debug flavour only: CMake compiles this
// file only with BANISHED_TWEAK_OVERLAY on)
// ============================================================================
// NOT RETAIL. Block TWEAKUI; docs/reference/tweak_overlay.md (the panels, how to add a
// tweakable). Log lines: `[TweakOverlay] ...` on stderr (standalone_error.log).
//
// Dear ImGui (third_party/imgui, v1.92.9b, unmodified) with its SDL3 and DX11 backends,
// used through their public API with four integration choices made here:
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
#include "ext/tweak/ext_tweaks.h"
#include "better_trees/foliage_aa_backend.h"   // src/render/common/better_trees: FaaBackend::Stats
#include "better_trees/billboard_shadow_backend.h"   // src/render/common/better_trees: AppliedParams
#include "graphics/SunLight.h"               // ExtTweakSunMapSize

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_sdl3.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_thread.h>

#include <cmath>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

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
constexpr const char* kRows[] = {"ambient_occlusion", "better_trees", "max_shadow_res", "render_pipe",
                                 "eye_camera"};
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

std::mutex     g_drawMu;                   // BeforePresent vs OnDeviceShutdown
ID3D11Device*  g_dx11Device  = nullptr;    // the device the DX11 backend was initialised on
uint64_t       g_lastTicks   = 0;
SDL_ThreadID   g_loggedDrawThread = 0;

// The presenting thread's own copies of the tweak structs (the UI edits these, then publishes).
Ext::Tweak::Ao  g_uiAo;
Ext::Tweak::Faa g_uiFaa;
Ext::Tweak::Bbs g_uiBbs;
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
    ImGui_ImplSDL3_InitForD3D(window);
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

    if (ImGui::CollapsingHeader("Ambient occlusion", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("row %s, pass %s", s.rowRequested[RowIndex("ambient_occlusion")] ? "on" : "off",
                    s.aoEnabled ? "enabled" : "inactive");
        int tier = s.aoQuality;
        const char* const kTiers[] = {"(0)", "1 Low", "2 Medium", "3 High"};
        if (ImGui::SliderInt("quality (ext.ao_quality)", &tier, 1, 3, kTiers[tier >= 0 && tier <= 3 ? tier : 0]))
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

    if (ImGui::CollapsingHeader("Foliage", ImGuiTreeNodeFlags_DefaultOpen)) {
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

    if (ImGui::CollapsingHeader("Billboard shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
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

    if (ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
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
void BeforePresent(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain) {
    if (!g_open.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> draw(g_drawMu);
    // A frame presented while the device is being rebuilt has none: nothing to draw into.
    if (!g_contextReady || device == nullptr || context == nullptr || swapChain == nullptr) return;

    if (g_dx11Device != device) {
        if (g_dx11Device != nullptr) ImGui_ImplDX11_Shutdown();
        ImGui_ImplDX11_Init(device, context);
        g_dx11Device = device;
        std::fprintf(stderr, "[TweakOverlay] DX11 backend on device %p (thread %llu)\n", static_cast<void*>(device),
                     static_cast<unsigned long long>(SDL_GetCurrentThreadID()));
    }
    if (!g_uiLoaded) {
        g_uiAo  = Ext::Tweak::AoValues();
        g_uiFaa = Ext::Tweak::FaaValues();
        g_uiBbs = Ext::Tweak::BbsValues();
        g_uiLoaded = true;
    }

    ID3D11Texture2D* back = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) ||  // CAST-OK: com_iid
        back == nullptr)
        return;
    D3D11_TEXTURE2D_DESC td;
    back->GetDesc(&td);
    ID3D11RenderTargetView* rtv = nullptr;
    const HRESULT hr = device->CreateRenderTargetView(back, nullptr, &rtv);
    back->Release();
    if (FAILED(hr) || rtv == nullptr) return;

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

    io.DisplaySize = ImVec2(static_cast<float>(td.Width), static_cast<float>(td.Height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const uint64_t now = SDL_GetTicksNS();
    float dt = g_lastTicks != 0 && now > g_lastTicks ? static_cast<float>(static_cast<double>(now - g_lastTicks) * 1e-9)
                                                     : 1.0f / 60.0f;
    if (dt <= 0.0f) dt = 1e-4f;
    io.DeltaTime = dt;
    g_lastTicks = now;

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    DrawPanels(snap);
    ImGui::Render();

    ID3D11RenderTargetView* oldRtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDsv = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, &oldDsv);
    context->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());   // backs up and restores the rest of the state
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, oldDsv);
    for (ID3D11RenderTargetView*& v : oldRtv)
        if (v) v->Release();
    if (oldDsv) oldDsv->Release();
    rtv->Release();

    const SDL_ThreadID self = SDL_GetCurrentThreadID();
    if (self != g_loggedDrawThread) {
        g_loggedDrawThread = self;
        std::fprintf(stderr, "[TweakOverlay] drawing on thread %llu (%s; ext.render_pipe=%d) at %ux%u\n",
                     static_cast<unsigned long long>(self), self == g_mainThread ? "the main thread" : "NOT the main thread",
                     Ext::Ext().render_pipe ? 1 : 0, td.Width, td.Height);
    }
}

void OnDeviceShutdown() {
    std::lock_guard<std::mutex> draw(g_drawMu);
    if (g_dx11Device == nullptr) return;
    ImGui_ImplDX11_Shutdown();
    g_dx11Device = nullptr;
    std::fprintf(stderr, "[TweakOverlay] DX11 backend released (device shutdown)\n");
}

}  // namespace TweakOverlay
}  // namespace Diag
