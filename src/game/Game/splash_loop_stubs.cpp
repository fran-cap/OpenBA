// src/game/Game/splash_loop_stubs.cpp
// Standalone-only stub vtable for the exe's splash DialogLoaderLoopT subclass
// (vtable @ 0x140158800, used for both Dialog\Loading.rsc and Dialog\ShiningRock.rsc
// in the original GameMain).
//
// Layout deduced from disassembly of g_GameLoadDialogLoopT_Vtable:
//   +0x00  Destroy(self, flags)            — exe 0x140001010 (DialogLoaderLoopT::~ + free)
//   +0x08  Startup(self)                   — exe 0x140098FA0 (chains base Startup + UI setup)
//   +0x10  Shutdown(self)                  — exe 0x140098F90 (literal RET 0)
//   +0x18  Update(self, time)              — exe 0x140098DA0 (random text + base Update)
//   +0x20  Draw(self, alpha)               — thunk 0x14003A8EE -> Runtime DialogLoaderLoopT::Draw
//   +0x28  DrawFailed(self)                — thunk 0x14003A90C -> Runtime DialogLoaderLoopT::DrawFailed
//   +0x30  Load(self)                      — thunk 0x14003A8E8 -> Runtime DialogLoaderLoopT::Load
//   +0x38  SaveRestore(self, save, state)  — thunk 0x14003A906 -> Runtime DialogLoaderLoopT::SaveRestore
//   +0x40  OnVideoError(self)              — thunk 0x14003A8C4 -> Runtime DialogLoaderLoopT::OnVideoError
//   +0x48  OnVideoRestart(self)            — thunk 0x14003A900 -> Runtime DialogLoaderLoopT::OnVideoRestart
//   +0x50  GetResourceFilename(self)       — thunk 0x14003A8FA -> Runtime DialogLoaderLoopT::GetResourceFilename
//   +0x58  AllowsModChanges(self)          — thunk 0x14003A8F4 -> Runtime UpdateLoop::AllowsModChanges (FALSE)
//   +0x60  CreateDialog(self, retptr)      — exe 0x140099070 (LoaderDialog ctor)
//
// ENDGAME B.3.8 block LOOP: the hand-built table is gone. This file now holds
// the MEMBER bodies of the real class `GameLoadDialogLoopT`
// (src/game/Game/GameLoadDialogLoopT.h), whose compiler vtable 0x140158800
// serves (B38_RULES D5). The slot list above is retail's (B38_SLOTS sec. 6).


#include "object/Internal.h"
#include "global_types.h"
#include "engine_loader_gate.h"
#include "chunked_scene_loader.h"
#include "stall_debug.h"
#include "Game/uifade.h"
#include "Game/game.h"          // g_gameContainer (0x1401c3de8), GameContainer::ApplyKeyMapToUI
#include "video/Device.h"      // Video::Device::_singleton (the splash UI scale)
#include "Game/GameLoadDialogLoopT.h"   // ENDGAME B.3.8 LOOP
#include "Game/GameUpdateLoop.h"        // caller batch CB6: the "next loop is a GameUpdateLoop" test
#include "object/class_vptr.h"          // Object::ClassVptr<GameUpdateLoop>
#include <cstdio>
#include <cstdint>
#include "system/String.h"   // -Iruntime/types
#include "engine/UpdateLoop.h"   // -Iruntime/types: ENDGAME B.3.7 Engine loop classes
#include "engine/MainLoop.h"     // -Iruntime/types: ENDGAME B.3.7 Engine::MainLoop
#include "system/Value.h"    // ENDGAME B.3.4b System callers: System::Random
#include "object/TypeInfo.h" // B3R: Object::IsDerivedFromNullSafe
#include "object/External.h"                 // ENDGAME B.3.6 UI callers (K4)
#include "ui/DialogInstance.h" // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"                      // ENDGAME B.3.6 UI callers (K4)
#include "ui/String.h"                       // ENDGAME B.3.6 UI callers (K4)
#include "ui/StringTable.h"                  // ENDGAME B.3.6 UI callers (K4)
#include "ui/TextElement.h"                  // ENDGAME B.3.6 UI callers (K4)
#include "ui/UserInterface.h"                // ENDGAME B.3.6 UI callers (K4)
#include <new>
#include "platform_thread.h"   // Platform::SleepMs
#include "platform_time.h"     // Platform::PerfCounter / PerfFrequency
#include "system/Timer.h"   // ENDGAME B.3.9 SS2: System::Timer::_ticksPerSecond
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "ext/registry.h"                     // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): Ext::Ext().quick_screens
#include "ext/phone_menu/loading_screen.h"   // EXT SEAM (P9): LoadingUiScale
#include "ext/quick_screens/quick_screens.h"  // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): Ext::QuickScreens::SplashWorkDone
#include "ext/load_progress/load_progress.h"  // EXT SEAM (NOT RETAIL, docs/extensions/handins/LOADPROGRESS.md): BootEnd

// Defined in src/game/Game/gameupdateloop_load.cpp. Returns true once the
// new-game world build for `loop` (a GameUpdateLoop) has fully completed.
extern "C" bool GameUpdateLoop_IsWorldBuilt(const Engine::UpdateLoop* loop);

namespace {

// Fix 2: port of the "random loading tip" block in retail FUN_140098DA0
// (GameLoadDialogLoopT::Update). Finds the "descText" TextElement in the loading
// dialog and sets its text to a random entry from the element's string table
// (element+0xD8), using the loop's embedded System::Random at self+0x194.
//
// Retail cycles this every 2.0s (DAT_140172174) off the self+0xB58 timer; the
// caller drives the timing. This helper just sets one tip. Fully defensive: it
// silently no-ops if the dialog/element/table or any engine fn is missing (e.g.
// ShiningRock.rsc has no descText — FindElement then returns the missing-element
// sentinel, which we normalise to null and skip).
// Diagnostic: name the gate that stopped us. A silent no-op here is
// indistinguishable from "the tip rendered but is blank".
static void TipBail(const char* why, const void* p) {
    static int n = 0;
    if (n++ < 12) printf("[SplashDLT] loading tip SKIPPED: %s (%p)\n", why, p);
}

void SetRandomLoadingTip(GameLoadDialogLoopT* self) {
    if (!self) { TipBail("self==null", nullptr); return; }
    if (!self->_dialogInstance) { TipBail("m_dialogInstance==null", self); return; }

    // Find "descText" in the dialog instance (member value-return ABI:
    // RCX=this, RDX=retbuf, R8=name — the typedef carries all three).
    const System::String nameStr("descText");
    const Object::InternalPtr<UI::Element> found = self->_dialogInstance->FindElement(nameStr);

    // Normalise the missing-element sentinel (0xffffffff........) to null.
    UI::Element* elem = found.Get();
    uintptr_t raw = reinterpret_cast<uintptr_t>(elem);  // CAST-OK: ptr_int
    if (raw < 0x10000 || raw > 0x00007FFFFFFFFFFFULL) {
        TipBail("descText not found (FindElement sentinel)", elem);
        return;
    }

    // Retail gates on the TEXTELEMENT type id, read from exref 0x140151508
    // (Ghidra labels that global `fn_LabelElement_SetSprite` -- a misnamed
    // dispatch slot, which is what sent the first port to LabelElement).
    // descText is a TextDescription, so IsDerivedFrom(elem, LabelElement)
    // was FALSE and the tip was skipped on every game-load splash.
    if (!Object::IsDerivedFromNullSafe(elem, UI::TextElement::TypeId())) {   // ENDGAME B.3.9 TR
        TipBail("descText is not a TextElement", elem);
        return;
    }

    // StringTable at element+0xD8; pick a random 1-based index in [1, count].
    UI::TextElement* textElem = static_cast<UI::TextElement*>(elem);
    UI::StringTable* table = textElem->_text.GetStringTable().Get();
    if (!table) { TipBail("elem+0xD8 StringTable==null", elem); return; }
    int count = table->GetCount();
    if (count <= 0) { TipBail("StringTable count<=0", table); return; }
    uint32_t idx = self->_random.Rand(static_cast<uint32_t>(count - 1));   // the loop's own Random (+0x194)

    const UI::String uiStr(Object::ExternalPtr<UI::StringTable>(table),
                           static_cast<int>(idx + 1));
    const UI::String textVal = uiStr;
    textElem->SetText(&textVal);

    static int s_tipLog = 0;
    if (s_tipLog++ < 8) {
        printf("[SplashDLT] loading tip set: descText=%p table=%p count=%d idx=%u\n",
               (void*)elem, (void*)table, count, idx + 1);
    }
}

// Per-loop latch for the fade-out kick. It used to be a function-local static
// compared as `latch != self`, on the stated assumption that "splash loops run one
// at a time, each with a unique self". Quit-to-main-menu broke that: a SECOND new
// game builds a second splash loop, the allocator can hand back the freed first
// one's address, and `latch == self` is then true on frame 0. Clearing it in
// Startup (which runs exactly once per loop) makes the latch depend on the
// loop's LIFETIME rather than its address.
const void* g_fadeOutInitiatedFor = nullptr;

// EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): ext.quick_screens, latched once per screen
// in Startup beside the fade latch above, so a live toggle never changes a
// screen mid-hold and Update reads no registry state per frame.
bool g_quickScreen = false;

// ext.quick_screens: the work the hold covered beyond retail's loader-idle and
// world-built terms -- the menu scene's chunked build and this screen's reveal
// fade (docs/extensions/handins/SPLASH.md). Asked only when the row is on.
// Out of line so Update's faithful arm keeps its own frame and registers (the
// object proof in SPLASH.md: the residue there is the one latched-bool test).
[[gnu::noinline]] bool QuickScreenWorkDone(const void* self, const UI::UserInterface* ui) {
    return Ext::QuickScreens::SplashWorkDone(
        g_fadeOutInitiatedFor == self,
        ChunkedSceneLoader::IsArmed() || ChunkedSceneLoader::IsBusy(),
        UIFade::GetCurrentAlpha(ui));
}

} // namespace

// ============================================================================
// GameLoadDialogLoopT (vtable @ 0x140158800) — src/game/Game/GameLoadDialogLoopT.h
// ============================================================================
// ENDGAME B.3.8 block LOOP: the old hand-built `g_splashDialogLoopVtable[13]`'s
// stub bodies, moved into the real class's members unchanged.

// The ctor retail inlines at each of its 7 construction sites (header banner).
GameLoadDialogLoopT::GameLoadDialogLoopT(const System::String& filename,
                                         Engine::UpdateLoop* nextLoop,
                                         const System::Time& delay)
    : Engine::DialogLoaderLoopT(filename, nextLoop, delay),
      _fadeColor1(0xff000000),
      _fadeColor2(0xff000000),
      _fadeDone(0),
      _random() {                       // ??0Random@System@@QEAA@XZ 0x1800c7f20, after the three stores
    _tipTimer   = 0;
    _autoAdvance = 0;
}

// Retail 0x140001010 (slot 0): CALL ??1DialogLoaderLoopT@Engine@@ (import
// 0x140152b88), then MemFree on &1 (System::Random has no destructor). The
// compiler's chain: ~UserInterface, ~String, ~LoaderLoop -> ~UpdateLoop; the
// free is UpdateLoop's operator delete.
GameLoadDialogLoopT::~GameLoadDialogLoopT() {
    printf("[SplashDLT::Destroy] self=%p\n", (void*)this);
}

void GameLoadDialogLoopT::Startup() {
    StallDebug::Phase startupPhase("SplashDLT::Stub_Startup");
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/LOADPROGRESS.md): ext.load_progress's boot
    // screen ends here, before this screen's load kick (a no-op unless it was shown).
    Ext::LoadProgress::BootEnd();
    g_fadeOutInitiatedFor = nullptr;
    g_quickScreen = Ext::Ext().quick_screens;   // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): ext.quick_screens, read once per screen
    Engine::UpdateLoop* next = _nextLoop;
    printf("[SplashDLT::Startup] self=%p next=%p m_dialogExternal(before)=%p\n",
           (void*)this, (void*)next, _dialogExternal);

    // Step 1: load self's own .rsc if not yet loaded. The original game's
    // Loader thread does this asynchronously before each loop becomes active,
    // but standalone has no thread — so we call DialogLoaderLoopT::Load on
    // ourselves synchronously here. Reads _dialogFilename @ +0x48 and writes
    // the loaded External to _dialogExternal @ +0x50.
    if (!_dialogExternal) {
        StallDebug::Phase ownLoadPhase("SplashDLT own-rsc DialogLoaderLoopT::Load");
        // QUALIFIED -- slot 6 is VIRTUAL on the base, so an unqualified call
        // would re-enter this class's own Load.
        Engine::DialogLoaderLoopT::Load();
        printf("[SplashDLT::Startup] DialogLoaderLoopT::Load returned, "
               "m_dialogExternal=%p\n", _dialogExternal);
    }

    // Step 2: chain DialogLoaderLoopT::Startup. Mirrors original exe
    // 0x140098FA0 which tail-calls Runtime DialogLoaderLoopT::Startup
    // (0x1800db000). That function calls our slot 12 CreateLoopDialog to
    // produce a LoaderDialog wrapping _dialogExternal, stores it at
    // self+0x58 (_dialogInstance), invokes the dialog's Slot4 (Setup),
    // pushes the IBP into the UI's pending-open list at self+0x88, then calls
    // Loader::Load(&_loader, _nextLoop).
    //
    // THAT TAIL CALL IS THE ONE AND ONLY LOAD KICK, here and in retail. Read
    // out of the shipped exe: 0x140098fa0 is 29 instructions and its first call
    // is this chain; there is no second dispatch and no exe-side loader at all
    // (the exe does not even import Engine::Loader — see
    // docs/history/endgame_b/LOADER_READ_SPIN_GHIDRA.md §Q5). The loader
    // thread it starts loads _nextLoop's resources while the main thread below
    // animates this splash, which is exactly retail's shape.
    if (_dialogExternal) {
        StallDebug::Phase chainPhase("SplashDLT DialogLoaderLoopT::Startup");
        printf("[SplashDLT::Startup] invoking DialogLoaderLoopT::Startup "
               "(creates dialog + pushes to UI, then kicks the ONE "
               "Engine::Loader thread for next)\n");
        // QUALIFIED (slot 1 is VIRTUAL on the base; this is Startup itself).
        Engine::DialogLoaderLoopT::Startup();
        // Arm the gate the moment the kick has happened; Update polls it
        // in the same place retail polls LoaderLoop::IsFinished.
        EngineLoader::NoteKicked();
        printf("[SplashDLT::Startup] DialogLoaderLoopT::Startup returned, "
               "m_dialogInstance=%p loaderBusy=%d\n",
               _dialogInstance, (int)EngineLoader::IsBusy());
    } else {
        printf("[SplashDLT::Startup] WARN: skipping DialogLoaderLoopT::Startup "
               "(rsc=%p)\n", _dialogExternal);
    }

    // Step 3 USED TO BE HERE and is gone. It kicked next->vtable->Load a SECOND
    // time, through standalone/async_loader.{h,cpp} — first synchronously on the
    // main thread, later (for the new-game path) on a worker thread. Step 2 had
    // already kicked the Runtime Engine::Loader thread for the same loop, so the
    // standalone ran TWO resource loaders where retail runs one. Both threads
    // then went through IO::FileSystem::ReadFile's packed arm, which is a
    // SetFilePointer + ReadFile pair on ONE shared Win32 handle with no lock
    // (retail takes no lock anywhere on this path, verified in Ghidra), and an
    // interleave served one thread bytes from the other's offset: NULL
    // resources, bad magic, a spin in CompressedStream::Read, dialog crashes.
    // 11 of 24 boots were defective. Full diagnosis in
    // docs/history/endgame_b/LOADER_READ_SPIN.md; the Ghidra proof that
    // AsyncLoader was our invention and not ported retail code is §Q5 of
    // LOADER_READ_SPIN_GHIDRA.md.
    //
    // The reason the kick was deferred into Update in the first place — the
    // load was synchronous on the main thread, so the screen sat black for the
    // whole ~5.8s world build — no longer exists: step 2's loader thread does the
    // load while the main thread keeps animating this splash, which is what
    // retail does and what the deferral was hand-rolling.
    //
    // Note: ChunkedSceneLoader is only ARMED at boot (RunGameMain in
    // src/game/Game/game_main.cpp). Update runs its Init and Tick()s PhaseB
    // once the loader thread is done (see the gate there), so MenuDLT's struct
    // fields (+0xb90 menuScene, +0xb98 renderer, etc.) are populated behind a
    // drawn splash rather than during the un-presented boot window.

    // Retail's tail after the chain (0x140098fb3-0x140099050, row 1243). Every
    // read is unguarded: the container (0x1401c3de8) is built by GameMain and
    // filled by InnerLoaderLoop::Load (GameContainer_Init) before this loop
    // starts, the settings block before MainLoop::Run, the device by
    // Engine::Startup.
    //   1. ApplyKeyMapToUI(container, &_ui): the five UI sounds, the fade
    //      sprite, the UI scale from the settings (0x140098fbe).
    g_gameContainer->ApplyKeyMapToUI(&_ui);
    //   2. The reveal: fade in at once to _fadeColor1 (+0x188), then fade out
    //      animated (0x140098fd5 / 0x140098feb, SetFullscreenFade x2). This was
    //      the port's UIFade::EnsureFadeSprite + TriggerReveal with a constant
    //      black; the sprite now comes from step 1, the colour from the member.
    _ui.SetFullscreenFade(true, _fadeColor1, true);
    _ui.SetFullscreenFade(false, _fadeColor1, false);
    //   3. The container's hourglass cursor (+0x38), by value (0x140099000).
    _ui.SetCursor(g_gameContainer->cursorHourglass);
    //   4. EnableCursor(true) (0x14009900c).
    _ui.EnableCursor(true);
    //   5. The splash's UI scale is the back buffer height against 900 pixels,
    //      truncated to a whole percent (0x140099012-0x140099050): 900.0f
    //      (0x140172244) / height (Device +0x08), * 100.0f (0x140172208),
    //      CVTTSS2SI, / 100.0f. It overwrites the settings scale step 1 wrote.
    const float heightRatio =
        900.0f / static_cast<float>(::Video::Device::_singleton->backBufferHeight);
    // EXT SEAM (NOT RETAIL, P9, docs/android/P9_LOADING_SCALE.md §1): with ext.phone_menu on, the
    // loading group keeps the Options UI scale step 1 wrote (the owner's ruling); retail's otherwise.
    const float settingsScale = _ui._uiScale;
    _ui._uiScale = Ext::PhoneMenu::LoadingUiScale(
        static_cast<float>(static_cast<int32_t>(heightRatio * 100.0f)) / 100.0f, settingsScale);
}

void GameLoadDialogLoopT::Shutdown() {
    printf("[SplashDLT::Shutdown] self=%p\n", (void*)this);
    // Original is RET 0 (literal no-op).
}

bool GameLoadDialogLoopT::Update(const System::Time& time) {
    StallDebug::Tick("SplashDLT::Update");
    // Retail's slot takes MainLoop's Time by const& and never writes it: the first
    // Update after the synchronous Startup gets the whole Startup time as its dt, as
    // in retail (whose Startup is synchronous too). B6 ruling: no first-tick clamp.

    // Pump the embedded UI — flushes the pending-open list (where Startup
    // pushed our LoaderDialog), runs each open dialog's OnUpdate, processes
    // pending-close, and advances animation. Without this the splash dialog
    // is created but never moved into the active list, so Draw walks an
    // empty list and the screen stays blank. Mirrors Runtime
    // DialogLoaderLoopT::Update @ 0x1800daf20.

    _ui.Update(time);

    // Run the deferred MenuScene load, once, on the first frame where this
    // splash has actually revealed. RunGameMain only Arm()s it — running
    // LoadSceneA1 (~590ms) during boot meant paying it while the engine window
    // was already on screen and had never been presented to, which is the
    // boot-time blank-window flash. Here it lands behind a drawn splash.
    //
    // Reveal test: alpha < 0.5 (1=black, 0=fully revealed) or a 0.25s safety cap
    // so a stuck fade can't strand it.
    // ...but NOT while the one Engine::Loader thread is still loading.
    //
    // ChunkedSceneLoader IS OURS, NOT RETAIL'S. Retail loads the menu scene
    // inside MenuDialogLoopT::Load — exe MenuScene_impl_a930, reached from
    // FUN_140099ac0 — i.e. on the loader thread, as part of the very load this
    // splash is waiting for. We split it out because PhaseB's 12K-15K
    // Graphics::Scene::AddObject calls mutate global scene state that is not
    // safe off the main thread, and then drove it from here per tick. That put
    // LoadSceneA1's FindOrLoad("Game/MenuScene/SceneN.rsc") — a deep recursive
    // External::Load — on the MAIN thread at the same time as the loader
    // thread's own loads: a second concurrent reader of the packages, on every
    // boot (LOADER_READ_SPIN.md).
    //
    // Waiting for the loader here does not add a guard; it RESTORES retail's
    // exclusivity, which retail gets for free by never having the work on two
    // threads in the first place. The chunking still lands behind a drawn
    // splash — just after the loader has finished rather than beside it.
    //
    // ...and NOT while a device reset is pending (Video::Device::settingsChanged, set by
    // ApplyChangedSettings, cleared by DeviceData_Resize only when the whole rebuild succeeded).
    // The reset releases DeviceData::colorBuffer before it rebuilds the swap chain, and a failed
    // step (SetFullscreenState answering DXGI_ERROR_NOT_CURRENTLY_AVAILABLE while another window
    // holds the foreground, seen on a first-run window being clamped to the desktop) leaves it null
    // until a later BeginCommands retries. LoadSceneA1 loads the menu scene's FrameCopyImageBuffer,
    // whose constructor aliases that buffer (ImageBufferData::AttachColorBuffer): on our main thread,
    // between the failed reset and its retry, that was a null AddRef (4/4 on the short-settings
    // route). Retail runs this load inside the loader's load (above), not from the main thread's
    // splash update; the split onto the main thread is ours, so its ordering against the main
    // thread's reset retry is ours to keep. Waiting adds no check to a retail body: the load runs
    // unchanged, one frame after the reset that BeginCommands is already retrying.
    const ::Video::Device* const videoDevice = ::Video::Device::_singleton;
    const bool resetPending = videoDevice != nullptr && videoDevice->settingsChanged != 0;
    if (!EngineLoader::IsBusy() && !resetPending) {
        if (ChunkedSceneLoader::IsArmed()) {
            UI::UserInterface* ui = &_ui;
            int64_t revealCap = (::System::Timer::_ticksPerSecond > 0)
                ? ::System::Timer::_ticksPerSecond / 4 : 0;
            float revealAlpha = UIFade::GetCurrentAlpha(ui);
            if (revealAlpha < 0.5f || (revealCap && _elapsedTicks >= revealCap)) {
                printf("[SplashDLT::Update] self=%p reveal-alpha=%.2f "
                       "loader idle -> running deferred ChunkedSceneLoader init\n",
                       (void*)this, revealAlpha);
                ChunkedSceneLoader::RunArmedInit();
            }
        }

        // Drive the chunked scene loader. Each tick processes ~500 PhaseB
        // elements (~17ms of work, well within a 60Hz frame budget) so the
        // splash's loading animation keeps moving while the heavy 12K-15K
        // static-mesh iteration runs. When the chunk function reports done,
        // ChunkedSceneLoader::Tick auto-runs LoadSceneA3 (particles) and clears
        // IsBusy() — the transition gate below waits on this too.
        if (ChunkedSceneLoader::IsBusy()) {
            StallDebug::Phase phase("SplashDLT ChunkedSceneLoader::Tick");
            int processed = ChunkedSceneLoader::Tick(500);
            (void)processed;
        }
    }

    // Loading-screen frame cap (matches real LoaderLoop ~14ms sleep).
    Platform::SleepMs(14);

    _elapsedTicks += time._ticks;

    // (The "Fix 1 deferred load kick" that used to sit here is gone — it was the
    // second dispatcher of next->vtable->Load. See the Step 3 note in
    // Startup. The load now runs once, on the Engine::Loader thread that
    // Startup's step 2 started, which is retail's single loader.)

    // ---- Fix 2: cycle the loading tip every 2.0s, as retail does ----
    // Retail FUN_140098DA0 drives this off the self+0xB58 timer against
    // DAT_140172174, which is exactly 2.0f. Measured here at 2015ms -- match.
    //
    // The block is gated on nextLoop->_loadProgress != 0 (UpdateLoop+0x24),
    // exactly as retail. An earlier note here claimed the gate could not be
    // honoured because "the Runtime Loader::Load path calls SetLoadProgress and
    // our AsyncLoader replaced it" -- that was wrong on both counts. The
    // exported SetLoadProgress has ZERO callers in the whole 2017 build (no
    // '?SetLoadProgress' resolve string exists in the exe); the real publisher
    // is the exe worldgen pipeline (retail 0x1400cf390), which writes stages
    // 1..5 straight into g_pGameWorldSingleton+0x24. Our port of that pipeline
    // omitted the writes as "progress hints with no consumer" -- THIS is the
    // consumer. Restored in src/game/Game/worldgen.cpp (wg_PublishLoadProgress).
    // Net faithful behaviour: tips cycle only while a NEW-GAME worldgen is
    // running; the boot splash chain (nextLoop=MenuDLT) and save-loads never
    // write +0x24, so those screens keep the .rsc default (a single space).
    //
    // The gate is now EXACTLY retail's: `nextLoop->_loadProgress != 0` and
    // nothing else (exe 0x140098e0d: MOV RAX,[RSI+0x28]; MOV ECX,[RAX+0x24];
    // TEST ECX,ECX; JZ). The extra `g_loadKickedFor == self` term that used to
    // be here was an artefact of our deferred load kick, which is gone.
    if (::System::Timer::_ticksPerSecond > 0 &&
        _nextLoop && _nextLoop->GetLoadProgress() != 0) {
        _tipTimer += time._ticks;
        if (_tipTimer >= ::System::Timer::_ticksPerSecond * 2) {   // 2.0s
            _tipTimer = 0;
            // [diag] measure the ACTUAL interval and the retail gate value.
            // Retail cycles only while nextLoop->_loadProgress (UpdateLoop+0x24)
            // is non-zero, and its period is DAT_140172174 = 2.0s exactly.
            static long long s_prevTip = 0;
            int64_t qf, qn; Platform::PerfFrequency(&qf); Platform::PerfCounter(&qn);
            double sinceMs = s_prevTip ? 1000.0 * (double)(qn - s_prevTip) / (double)qf : -1.0;
            s_prevTip = qn;
            printf("[SplashDLT] tip cycle: wall=%.0fms tps=%lld loadProgress=%d\n",
                   sinceMs, (long long)::System::Timer::_ticksPerSecond,
                   _nextLoop ? _nextLoop->GetLoadProgress() : -1);
            SetRandomLoadingTip(this);
        }
    }

    // Hold the loading screen until (a) the minimum display time elapsed AND
    // (b) for the new-game path, the world build has finished. Retail keeps the
    // loading screen up for the ENTIRE GameUpdateLoop::Load (the build runs on a
    // background thread) and only fades the game in — with citizens already
    // mid-path — once it's done. Without (b) we'd fade to black and transition
    // into GameUpdateLoop while it is still building, drawing a half-built world.
    // For every other loop (boot splashes -> MenuDLT) worldReady stays true and
    // the transition is purely time-based, exactly as before.
    // Caller batch CB6: "is the next loop a GameUpdateLoop" compares the vptr
    // with the one GameUpdateLoop's ctor recorded (B8-5 identity; it was the by-address
    // g_GameUpdateLoop_Vtable, which 0x14015e2f0's row served with that table).
    // A vptr identity compare (GameUpdateLoop is unreflected: no type id).
    bool worldReady = true;
    if (_nextLoop &&
        *reinterpret_cast<const void* const*>(_nextLoop) == Object::ClassVptr<GameUpdateLoop>::Get()) {   // vtable probe (a vptr identity compare: GameUpdateLoop is unreflected, no type id)  // CAST-OK: vslot
        worldReady = GameUpdateLoop_IsWorldBuilt(_nextLoop);
    }

    // Once the loader thread is done AND the splash hold has elapsed, kick off
    // the fade-OUT (to opaque black) and wait one extra tick before
    // transitioning. Mirrors the original FUN_140098DA0 sequence, whose own
    // first act is `if (LoaderLoop::IsFinished(this))` — and IsFinished
    // (Runtime 0x1800dc150) is exactly
    // `!(_loader._thread.runFlags & 1) && _elapsedTicks >= _delayTicks`.
    // EngineLoader::IsBusy() is that first term, asked without the wake-up race
    // (see engine_loader_gate.h); `worldReady` is ours, because our world build
    // signals completion separately.
    //
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): with ext.quick_screens on, the hold
    // also ends once the rest of its work is done (QuickScreenWorkDone); off,
    // the `||`'s second operand is one test of the bool Startup latched.
    if (!EngineLoader::IsBusy() &&
        (_elapsedTicks >= _delayTicks || (g_quickScreen && QuickScreenWorkDone(this, &_ui))) &&
        worldReady) {
        UI::UserInterface* ui = &_ui;

        if (g_fadeOutInitiatedFor != this) {
            g_fadeOutInitiatedFor = this;
            LoadTime::StampType("splash.fadeout", typeid(*this));   // NOT RETAIL: print-only (loader idle, hold elapsed, world ready)
            UIFade::EnsureFadeSprite(ui);
            UIFade::TriggerFadeIn(ui);
            printf("[SplashDLT::Update] self=%p kicked fade-out-to-black, "
                   "waiting for alpha=1\n", (void*)this);
            return true;
        }

        float alpha = UIFade::GetCurrentAlpha(ui);
        if (alpha < 0.99f) {
            return true;
        }

        // (The loader thread was already waited for by the `!IsBusy()` term on
        // the branch above — retail asks the same question in the same place.)

        // Wait for the chunked scene loader to finish PhaseB+A3. Without
        // this gate, the splash transitions before chunking completes and
        // MenuDLT would have to finish synchronously — re-introducing the
        // freeze.
        //
        // IsArmed() matters as much as IsBusy() here: between Arm() at boot and
        // RunArmedInit() above, IsBusy() is still false even though the entire
        // scene load is outstanding. Gating on IsBusy() alone would let a short
        // splash transition straight past it.
        if (ChunkedSceneLoader::IsArmed() || ChunkedSceneLoader::IsBusy()) {
            static int waitCount = 0;
            if ((waitCount++ % 30) == 0) {
                printf("[SplashDLT::Update] self=%p fade=%.3f waiting for "
                       "ChunkedSceneLoader\n", (void*)this, alpha);
            }
            return true;
        }

        Engine::UpdateLoop* next = _nextLoop;
        Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
        printf("[SplashDLT::Update] self=%p elapsed=%lld delay=%lld "
               "fade-alpha=%.3f -> transition next=%p\n",
               (void*)this, (long long)_elapsedTicks,
               (long long)_delayTicks, alpha, (void*)next);
        if (next && mainLoop) {
            LoadTime::StampType("splash.transition", typeid(*this));   // NOT RETAIL: print-only
            mainLoop->SetNextLoop(next);
        }
        return false;
    }

    // Throttled trace so 2.0s @ 14ms (~143 ticks) doesn't spam.
    static int updateCount = 0;
    ++updateCount;
    if (updateCount <= 3 || updateCount % 50 == 0) {
        printf("[SplashDLT::Update] self=%p tick=%d elapsed=%lld delay=%lld\n",
               (void*)this, updateCount,
               (long long)_elapsedTicks, (long long)_delayTicks);
    }
    return true;
}

void GameLoadDialogLoopT::Draw(float alpha) {
    StallDebug::Tick("SplashDLT::Draw");
    (void)alpha;  // frame-blend interpolation, not the fade alpha
    // STANDALONE DIVERGENCE: retail's slot 4 is the base DialogLoaderLoopT::Draw
    // (0x1800dae20), called below unchanged; the override only adds a
    // fade-alpha trace. (The boot window reveal moved to MainLoop::Run's first
    // presented frame, src/app/boot_window.h.)
    static int draws = 0;
    ++draws;
    bool trace = (draws <= 3);

    if (trace) {
        printf("[SplashDLT::Draw] self=%p frameAlpha=%f -> DialogLoaderLoopT::Draw\n",
               (void*)this, alpha);
    }

    // Per-frame fade alpha trace — log first 5 frames + every 10th so the curve
    // is visible in standalone_debug.log without spamming. UI is at self+0x60
    // for DialogLoaderLoopT.
    if (draws <= 5 || (draws % 10) == 0) {
        UIFade::LogState("SplashDLT::Draw", &_ui);
    }

    // Retail's slot 4 IS this base body (import ?Draw@DialogLoaderLoopT@):
    // clear to black, publish the load timer to the shader constants, draw the
    // UI. QUALIFIED, or it would dispatch back into this override.
    Engine::DialogLoaderLoopT::Draw(alpha);
}

extern "C" Object::InternalPtr<UI::DialogInstance>* LoaderDialog_FromSplashDLT(
        Engine::DialogLoaderLoopT* loop, Object::InternalPtr<UI::DialogInstance>* retInternalBasePtr);   // loaderdialog.cpp

Object::InternalPtr<UI::DialogInstance>* GameLoadDialogLoopT::CreateLoopDialog(
        Object::InternalPtr<UI::DialogInstance>* retPtr) {
    printf("[SplashDLT::CreateDialog] self=%p retPtr=%p rsc=%p -> LoaderDialog_FromSplashDLT\n",
           (void*)this, (void*)retPtr, _dialogExternal);
    // Mirrors exe LoaderDialog_Ctor3 @ 0x140099070: builds a real DialogInstance
    // subclass wrapping the loaded .rsc (Dialog/ShiningRock.rsc or
    // Dialog/Loading.rsc), patches our standalone vtable, returns IBP.
    // The body (loaderdialog.cpp) returns its second argument on every path.
    LoaderDialog_FromSplashDLT(this, retPtr);
    return retPtr;
}

