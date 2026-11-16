// ============================================================================
// src/engine/engine/engine.cpp — Wave 8 lane B — Engine::Engine + Engine::MainLoop
// ============================================================================
//
// Lane B of PLAN.md §4.2 wave 8. Eleven symbols were in scope; lane B ported
// THREE and held EIGHT back with written reasons; all ten functions among them
// are ported now. The layouts and the
// evidence for every offset are in src/engine/engine/Engine.h and
// src/engine/engine/MainLoop.h; the full write-up, the oracle argument per
// function and the reasoning behind each holdback are in
// docs/history/runtime_port/WAVE8_LANE_B.md.
//
//   | Export                              | Retail      | Ord  | Ported | Oracle        |
//   |-------------------------------------|-------------|------|--------|---------------|
//   | ??0Engine@0@QEAA@PEAX@Z             | 0x1800dbf00 |  174 | LANE ENG (Wave 9) |
//   | ??1Engine@0@QEAA@XZ                 | 0x1800dbec0 |  586 | LANE ENG (Wave 9) |
//   | ?Startup@Engine@1@QEAAIPEBDII_N@Z   | 0x1800db8a0 | 4094 | LANE ENG (Wave 9) |
//   | ?Shutdown@Engine@1@QEAAXXZ          | 0x1800db4f0 | 4047 | LANE ENG (Wave 9) |
//   | ??0MainLoop@Engine@@QEAA@PEAV11@@Z  | 0x1800dd6b0 |  263 | yes    | shadow        |
//   | ??1MainLoop@Engine@@QEAA@XZ         | 0x1800dd640 |  627 | yes    | shadow        |
//   | ?SetNextLoop@MainLoop@Engine@@...   | 0x1800dd7a0 | 3929 | yes    | replay        |
//   | ?Run@MainLoop@Engine@@...           | 0x1800dd3c0 | 3601 | mainloop_run.cpp   |
//   | ?OnVideoError@MainLoop@Engine@@...  | 0x1800dc5e0 | 3291 | mainloop_video.cpp |
//   | ?OnVideoRestart@MainLoop@Engine@@...| 0x1800dc3b0 | 3295 | mainloop_video.cpp |
//   | ?_singleton@MainLoop@Engine@@1PE... | 0x1801fe750 | 4813 | n/a    | DATA symbol   |
//
// All eleven mangled names were checked against the export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE any body was
// written; the addresses and ordinals above are that query's output, not
// Ghidra's guess. All eleven are present — this lane contributes none of the
// three phantom names WAVE8.md §4 lists (those are lanes C and E).
//
// ---------------------------------------------------------------------------
// NEITHER CLASS IS POLYMORPHIC — SO THERE IS NO VTABLE-OWNERSHIP PROBLEM
// ---------------------------------------------------------------------------
// `??_7Engine@Engine@@6B@` and `??_7MainLoop@Engine@@6B@` are both absent from
// the shipped export table, and WAVE8.md §2 is right that absence is not
// proof. It is shown here from the ctors' first stores instead:
//
//   MainLoop 0x1800dd6c6:  XOR EAX,EAX
//            0x1800dd6c8:  MOV qword ptr [RCX],RAX        <- a ZERO, not a vtable
//   Engine   0x1800dbf1e:  CALL 0x1800c79a0               <- MemoryAllocator ctor
//            0x1800dbf24:  XOR EAX,EAX
//            0x1800dbf26:  MOV qword ptr [RDI+0x670],RAX  <- a ZERO, not a vtable
//
// and confirmed against the profile build (MainLoop ctor 0x1800d5420 opens
// with the identical zero store). No method of either class is virtual.
// PLAN §4.3 risk 3 therefore does not apply to lane B at all.
//
// ---------------------------------------------------------------------------
// NO DETOUR IS INSTALLED — AND FOR ONCE THAT IS NOT LUCK
// ---------------------------------------------------------------------------
// Waves 6 and 7 each expected to be the first user of standalone/runtime_detour.h
// and each was not. Lane B checked rather than assumed: `get_xrefs_to` over
// the retail DLL for all four ported/considered entry points —
// ??0MainLoop (0x1800dd6b0), ??1MainLoop (0x1800dd640), ?Run (0x1800dd3c0),
// ?SetNextLoop (0x1800dd7a0) — and for ??0Engine / ?Startup returns ONLY
// DATA references: the export address table (0x18020eb..-0x18020ec..), the
// .pdata/unwind tables (0x1801b....) and the SEH scope tables (0x18019....).
// **Zero DLL-internal CALL sites.** These are exe-entry-point functions: the
// game's own game_main.cpp is the only caller of every one of them, and
// registering the export flips that call site. A detour would add nothing.
//
// The two functions that DO have a DLL-internal caller are the two video
// callbacks, and that is the first genuine detour requirement this project has
// found in 4,632 functions:
//
//   ?OnVideoError@   <- 0x1800dcd27 in MainLoop::OnVideoError_static
//   ?OnVideoRestart@ <- 0x1800dc5c7 in MainLoop::OnVideoRestart_static
//
// Those two static thunks are what the MainLoop CTOR installs into
// `Video::Device::_singleton` at +0x50/+0x58 (see RtEngine_MainLoop_Ctor
// below), and the device's own lost/restore path is the ONLY thing that ever
// calls them. So a port of either member that registered the export and
// stopped there would be dead code on the only path that reaches it, while
// the diff oracle reported `calls=0 divergences=0` — and a port with zero calls
// is unverified, not clean. Both members are ported, in
// src/engine/engine/mainloop_video.cpp.
//
// ⚠️ THE REST OF THIS PARAGRAPH USED TO READ "lane B's ported ctor keeps
// installing the DLL's OWN thunk addresses so that path stays entirely inside
// the DLL until then." THAT WAS TRUE AND IT WAS A CUTOVER DEFECT. Wave 9 lane
// VID changed it: the ctor now installs OUR OWN thunks
// (`RtEngine_MainLoop_OnVideoError_static` / `…OnVideoRestart_static` below),
// which call the member through `OnVideoError_` / `OnVideoRestart_`. The "needs a detour"
// hold-back reason cannot survive cutover — there is no DLL body left to
// detour past — but what replaces it is not "nothing", it is "the two stores
// are null", and no gate row covered that because no NAME is involved.
//
// ---------------------------------------------------------------------------
// WHY THE FOUR `Engine::Engine` MEMBERS WERE HELD BACK -- AND NO LONGER ARE
// ---------------------------------------------------------------------------
// SUPERSEDED 2026-09-08 by Wave 9 lane ENG. All four are ported, in
// src/engine/engine/engine_lifecycle.cpp. This section is kept because the
// facts it established are still the facts; only the verdict changed.
//
// `Engine::Engine` embeds `System::MemoryAllocator` at offset 0 -- proven in
// src/engine/engine/Engine.h, not taken on faith from PLAN 4.3 risk 2 --
// and at the time the allocator port carried a NO-GO verdict
// (WAVE2_ALLOCATOR.md: its shadow proof had never executed, and an unmeasured
// port is not a port).
//
// WAVE8.md 3 anticipates that the ctor and dtor fall to this. What the
// disassembly added is that `Startup` and `Shutdown` fall to it too, which was
// NOT anticipated:
//
//   Startup  0x1800db8d9: this->_allocator.minRangeSize = (u64)arg2 << 20;
//            0x1800db8e4: this->_allocator.maxTotalSize = (u64)arg3 << 20;
//            0x1800db8f9: this->_allocator.pageSize     = GetSystemInfo().dwPageSize;
//            0x1800db905: CALL 0x1800c7690     <- the allocator's own init
//   Shutdown 0x1800db87a: CALL 0x1800c75b0     <- the allocator's own shutdown
//
// Those five lines are exactly `MemoryAllocator::Startup` / `::Shutdown`
// inlined -- same operations, same order, same error string -- which is what
// let lane ENG port them by CALLING the ported allocator rather than
// re-deriving them. Wave 9 lanes E/E2/E3 discharged the NO-GO and lane E4
// armed the allocator at cutover, where our allocator is the only one in the
// process. Neither body has a differential oracle, and lane ENG says so
// plainly instead of manufacturing one.
//
// One consequence worth stating because docs/reference/engine_architecture.md
// ("Launcher dialog and settings") and src/app/main.cpp's HarnessGuardUnattendedBoot
// depend on it: the persisted arming bit is written from `Engine+0x6f0` by the first four
// instructions of `Engine::Shutdown`, not by `Engine::Startup`. Lane ENG's
// port keeps that store bit-for-bit -- and note game_main.cpp does not call
// Engine::Shutdown at all, so on this project's runs the bit still travels
// through ShutdownEngineStaged exactly as before. See
// src/engine/engine/Engine.h for the four stores.
//
// ---------------------------------------------------------------------------
// `MainLoop::Run`
// ---------------------------------------------------------------------------
// `Run` (0x1800dd3c0) is 45 instructions, every one of them accounted for in
// docs/history/runtime_port/WAVE8_LANE_B.md. It has no oracle of any kind: it
// is called once per process and does not return until the game quits. It and
// its two DLL-internal, unexported callees, `MainLoop::Update` (0x1800dd070)
// and `MainLoop::Frame` (0x1800dd100), are ported: mainloop_run.cpp (Run,
// Update) and mainloop_frame.cpp (Frame).
//
// ============================================================================

#include <cstdint>
#include "system/timer_internal.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include <cstddef>
#include <cstdio>

#include "engine/Engine.h"
#include "engine/MainLoop.h"
#include "video/Device.h"   // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "video/Context.h"  // B3R: RtVideo_Context_Dtor, Video_Context_OperatorDelete
#include "collection/Collection.h"  // B3R: rtcoll::RtColl_ArrayDtorElem_180033880

// ----------------------------------------------------------------------------

// ENDGAME B.3.7: `Engine::MainLoop` and `Engine::Engine` are real classes and
// `namespace Engine` exists at global scope, so a global `using RtEngine::Engine`
// is ill-formed; the Engine class is spelled `::Engine::Engine` below.
using RtEngine::MainLoop;       // == Engine::MainLoop (alias, MainLoop.h)
namespace MainLoopAddr = RtEngine::MainLoopAddr;

namespace {

// ---------------------------------------------------------------------------
// Dependencies
// ---------------------------------------------------------------------------
using VoidThisFn   = void  (*)(void*);
using CtorFn       = void* (*)(void*, void*);
using SetNextFn    = void  (*)(void*, void*);
using SecToTicksFn = int64_t (*)(float);

// The MainLoop singleton slot: the class static `Engine::MainLoop::_singleton` (0x1801fe750).
MainLoop** MainLoopSingletonSlot() {
    return &::Engine::MainLoop::_singleton;
}


// System::Timer::_secondsPerTick — the divisor in the inlined SecondsToTicks.
// Was a RAW-DOOR data read of the DLL's slot (WAVE9_LANE_V.md §4 names this
// line). Wave 9 lane B3's System::Timer port owns the storage, so it comes
// from us; the value is the compile-time constant 1e-6 either way, and ours is
// never the zero the DLL's slot holds before Engine::Startup.
const double* SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7, B9-10): no lazy cache -- the class
    // static (System::Timer::_secondsPerTick) is read fresh every call.
    return &::System::Timer::_secondsPerTick;
}

// The two clock constants, as the exact doubles retail loads. These are
// `(double)(1.0f/60.0f)` and `(double)(1.0f/15.0f)` — a float widened by the
// compiler, NOT `1.0/60.0`. Retail loads them from 0x1801616a8 / 0x1801616b0;
// the profile build (0x1800d5493) shows the source wrote the float and called
// `System::Timer::SecondsToTicks(float)`. Writing the decimal literals here
// would give a different double and a different tick count. See
// src/engine/engine/MainLoop.h.
constexpr float kFixedStepSeconds  = 1.0f / 60.0f;   // -> 0x3f91111120000000
constexpr float kMaxAccumSeconds   = 1.0f / 15.0f;   // -> 0x3fb1111120000000

// Retail's inlined `SecondsToTicks`: widen the float to double, divide by
// _secondsPerTick, truncate toward zero (CVTTSD2SI).
int64_t SecondsToTicksInline(float seconds) {
    return static_cast<int64_t>(static_cast<double>(seconds) / *SecondsPerTick());
}

}  // namespace

// ============================================================================
// ?SetNextLoop@MainLoop@Engine@@QEAAXPEAVUpdateLoop@2@@Z   retail 0x1800dd7a0
// ============================================================================
// The whole function:
//     1800dd7a0  MOV qword ptr [RCX + 0x28],RDX
//     1800dd7a4  RET
//
void Engine::MainLoop::SetNextLoop(UpdateLoop* loop) {
    _nextLoop = loop;
}

// ============================================================================
// ??0MainLoop@Engine@@QEAA@PEAV11@@Z                       retail 0x1800dd6b0
// ============================================================================
// Retail, instruction for instruction:
//
//   zero +0x00,+0x08,+0x10, the WORD at +0x18, and +0x20..+0x70
//   this->_engine = engine                                        (+0x78)
//   zero +0x80..+0x98
//   this->_deltaTime      = (i64)(1/60 / _secondsPerTick)         (+0x48)
//   this->_maxAccumulator = (i64)(1/15 / _secondsPerTick)         (+0x50)
//   MainLoop::_singleton = this
//   Video::Device::_singleton->[+0x50] = &OnVideoError_static
//   Video::Device::_singleton->[+0x58] = &OnVideoRestart_static
//   this->CreateVideoContext()
//   return this
//
// TWO FAITHFULNESS POINTS the assembly makes and a memset would destroy:
//
//   * The ctor writes a WORD at +0x18 (both flag bytes at once) and NEVER
//     touches +0x1a..+0x1f. Those six bytes keep whatever the allocator left
//     there. A `memset(self, 0, 0xa0)` would be wrong, and — worse — it would
//     be wrong in a way no oracle in this project could see, because nothing
//     ever reads them. The field-by-field writes below are deliberate.
//   * The two callbacks installed into the Video::Device are our own static
//     thunks (RtEngine_MainLoop_OnVideoError_static / _OnVideoRestart_static,
//     below), which call the ported OnVideoError / OnVideoRestart members.
//
// ============================================================================
// The two static video callbacks — OUR equivalents of the shipped thunks
// 0x1800dcd20 (`OnVideoError_static`) and 0x1800dc5c0 (`OnVideoRestart_static`),
// Wave 9 lane VID.
//
// Each shipped thunk is two instructions:
//
//     MOV RCX, qword ptr [0x1801fe750]     ; MainLoop::_singleton
//     JMP  <the member>
//
// and each is the ONLY thing in the process that ever calls its member
// (get_xrefs_to on both members returns the export table, .pdata and this ctor,
// nothing else). They are installed into `Video::Device` at +0x50 / +0x58 and
// invoked by `src/render/dx11/contextdata.cpp`'s Present-failure path.
//
// WHY THIS WAS A THUNK AND NOT A PORT OF THE MEMBERS — SUPERSEDED 2026-09-08 by
// Wave 9 lane VIDL, which ports both members in
// src/engine/engine/mainloop_video.cpp. THREE CLAIMS BELOW ARE WRONG and are
// corrected here in place (PLAN §6 step 8) rather than deleted, because each
// one was load-bearing for a decline that stood for two waves:
//
//   1. "ten UNEXPORTED .data globals" — all ten are EXPORTED DATA
//      (`?_typeInfo@…@@0VBaseTypeInfo@Object@@A`, ordinals 4853..4926, read out
//      of the shipped export directory with pefile). That matters: an
//      unexported global can only be reached by address, an exported one is a
//      NAME the scanners and the mint generator can both see.
//   2. "Not one of its seven internal callees is ported" — it has EIGHT
//      distinct direct callees and SEVEN OF THEM WERE ALREADY OURS when this
//      sentence was written. The one that was not is
//      `Array<VideoResource>::SetSize` (0x18004f620), now ported.
//   3. "Porting either would relocate 17 nulls rather than remove any" — the
//      real residue was ONE Array instantiation for `OnVideoError` (which
//      therefore closes with zero new doors) and three NAMED exports for
//      `OnVideoRestart`. Wrong by roughly a factor of three.
//
// The original text follows. `MainLoop::OnVideoError`
// (0x1800dc5e0) is ~250 instructions that build a `Collection::Array<int>` of
// ten resource TYPE IDS read from ten `.data` globals
// (0x1801fa548, 0x1801fa698, 0x1801fa580, 0x1801fa5f0, 0x1801fa660,
// 0x1801fa5b8, 0x1801f99b0, 0x1801fa708, 0x1801f9a90, 0x1801f99e8), walk the
// `ExternalFactory` AVL tree once per id (0x1800be9d0), release each object
// through a vtable slot, and destroy the `Video::Context`. Not one of its seven
// internal callees is ported. `OnVideoRestart` (0x1800dc3b0) opens by calling
// `CreateVideoContext` — unported, but EXPORTED as
// `?CreateVideoContext@MainLoop@Engine@@AEAAXXZ`, ordinal 2139; the
// "unexported" half of this sentence was wrong and is corrected at its call
// site below (Wave 9 lane VID2). Porting either
// would relocate 17 nulls rather than remove any, which is the shape lane Z's
// triage rule exists to refuse; both are DECLINED with that evidence, and the
// full sizing is in docs/history/runtime_port/WAVE9_LANE_VID.md.
//
// ============================================================================
//
// ENDGAME B.3.7: BOTH MEMBERS ARE PORTED (mainloop_video.cpp) AND ARE MEMBERS OF
// THE REAL CLASS, so the "member missing" branch this thunk used to carry (log
// once and return) can no longer be taken and is gone. The two static thunks
// are now retail's `OnVideoError_` / `OnVideoRestart_` statics themselves:
// `MOV RCX,[_singleton]; JMP <member>`, a direct member call as retail's
// same-image JMP is. The `extern "C"` names stay because the MainLoop ctor
// installs THEM into the Device (src/render/dx11/contextdata.cpp calls them).
void Engine::MainLoop::OnVideoError_() {
    (*MainLoopSingletonSlot())->OnVideoError();        // MOV RCX,[0x1801fe750]; JMP
}

void Engine::MainLoop::OnVideoRestart_() {
    (*MainLoopSingletonSlot())->OnVideoRestart();      // MOV RCX,[0x1801fe750]; JMP
}

extern "C" void RtEngine_MainLoop_OnVideoError_static() {
    Engine::MainLoop::OnVideoError_();
}

extern "C" void RtEngine_MainLoop_OnVideoRestart_static() {
    Engine::MainLoop::OnVideoRestart_();
}

extern "C" MainLoop* RtEngine_MainLoop_Ctor(MainLoop* self, ::Engine::Engine* engine) {

    self->_videoResources._data     = nullptr;
    self->_videoResources._size     = 0;
    self->_videoResources._capacity = 0;
    self->_context         = nullptr;
    self->_contextDirty    = 0;
    self->_quit            = 0;
    // +0x1a..+0x1f: retail does not write these. Neither do we.
    self->_currentLoop     = nullptr;
    self->_nextLoop        = nullptr;
    self->_startTicks      = 0;
    self->_accumulator     = 0;
    self->_totalTime       = 0;
    self->_deltaTime._ticks = 0;
    self->_maxAccumulator  = 0;
    self->_frameTime       = 0;
    self->_frameStartTicks = 0;
    self->_frameUpdateTime = 0;
    self->_frameEndTicks   = 0;
    self->_engine          = engine;
    self->_postDraw._data       = nullptr;
    self->_postDraw._size       = 0;
    self->_postDraw._capacity   = 0;
    self->_postUpdate._data     = nullptr;
    self->_postUpdate._size     = 0;
    self->_postUpdate._capacity = 0;

    self->_deltaTime._ticks = SecondsToTicksInline(kFixedStepSeconds);
    self->_maxAccumulator = SecondsToTicksInline(kMaxAccumSeconds);

    *MainLoopSingletonSlot() = self;

    // Video::Device::SetVideoErrorFuncs(device, &OnVideoError_static,
    //                                   &OnVideoRestart_static), inlined by
    // retail into two stores.
    //
    // THESE STORE OUR OWN THUNKS, NOT THE DLL'S — Wave 9 lane VID.
    // This line used to read
    //     ResolveAddr(Module::Runtime, MainLoopAddr::OnVideoError_static)
    // and that was a live cutover defect. `ResolveAddr(Module::Runtime, …)`
    // returns 0 when the module handle is null and nothing has registered the
    // address (standalone/resolve_standalone.cpp:484), so with the DLL absent
    // this ctor stored TWO NULLS into the live `Video::Device` at +0x50/+0x58 —
    // an unported function reached by ADDRESS, which no residue list, no
    // rawdoor row and no export-name audit reports, because there is no name
    // involved at any point.
    //
    // What the nulls would have cost, measured rather than assumed:
    // `src/render/dx11/contextdata.cpp:568-612` reads both slots and calls each
    // `if (onLost)` / `if (onRestored)` — so the immediate consequence is a
    // SILENTLY SKIPPED notification, not the EXEC-of-0 it looks like. That is
    // worse in one respect and better in another: the device-lost path would
    // release every GPU companion (`Device::ReleaseDynamic`, now ported next to
    // this file) and never tell `Engine::MainLoop` to rebuild the engine-side
    // `Video::Context` resource list, leaving the renderer holding stale
    // handles with nothing in the log.
    //
    // The fix installs functions that exist unconditionally. Each is our own
    // two-line equivalent of the shipped static thunk (0x1800dcd20 /
    // 0x1800dc5c0 — `MOV RCX,[MainLoop::_singleton]; JMP <member>`), and each
    // calls its member directly. See the two bodies above.
    // Video::Device::_singleton's +0x50 / +0x58 -- the profile build's setter
    // `Video::Device::SetVideoErrorFuncs` (0x1800fd860), inlined by retail.
    // B5 CTX (B5_RULES C4): Device.h names the two callback slots.
    static_assert(MainLoopAddr::DeviceErrorFuncSlot   == offsetof(::Video::Device, onVideoError), "error callback @ +0x50");
    static_assert(MainLoopAddr::DeviceRestartFuncSlot == offsetof(::Video::Device, onVideoRestart), "restart callback @ +0x58");
    ::Video::Device::_singleton->onVideoError   = &RtEngine_MainLoop_OnVideoError_static;     // +0x50
    ::Video::Device::_singleton->onVideoRestart = &RtEngine_MainLoop_OnVideoRestart_static;   // +0x58

    // `?CreateVideoContext@MainLoop@Engine@@AEAAXXZ` (0x1800dd500, ordinal 2139), ported in
    // video/context_ctor.cpp as a member of the real class. A faithful unguarded call: a
    // guard would leave `MainLoop::_context` null and move the fault to whichever
    // `Video::Context` call site touches it first.
    self->CreateVideoContext();


    return self;
}

// ============================================================================
// ??1MainLoop@Engine@@QEAA@XZ                              retail 0x1800dd640
// ============================================================================
// Retail:
//
//   if (this->_context) {
//       Video::Context::~Context(this->_context);      // 0x1800f16e0, ord 562
//       Video::Context::operator delete(this->_context);// 0x180018500, ord 945
//   }
//   this->_context = nullptr;                           // UNCONDITIONAL
//   ~Array(&this->_postUpdate);                         // 0x180033a40, +0x90
//   ~Array(&this->_postDraw);                           // 0x180033960, +0x80
//   ~Array(&this->_videoResources);                     // 0x180033880, +0x00 (tail JMP)
//
// Note the null store at 0x1800dd674 is OUTSIDE the `if` — retail writes it
// even when the context was already null. Kept.
//
// The three Array destructors are DLL-internal (no export name; checked with
// pefile). They are three DIFFERENT functions
// because the element types differ, which is the per-instantiation trap
// PLAN §4.3 risk 1 warns about: one Array body cannot serve two element
// widths, and here it cannot serve two element TYPES either — 0x180033880
// destroys a `System::String` inside each 0x18-byte entry, the other two
// destroy a plain 0x10-byte POD pair.
//
extern "C" void RtEngine_MainLoop_Dtor(MainLoop* self) {

    Video::Context* context = self->_context;
    if (context) {
        RtVideo_Context_Dtor(context);                                // ~Context 0x1800f16e0
        Video_Context_OperatorDelete(context);                        // operator delete 0x180018500
    }
    self->_context = nullptr;

    // retail 0x180033a40 / 0x180033960 — both J1 bodies (array.cpp's J1_ENTRY
    // table), i.e. trivial-destroy `Array<uint8_t>::~Array`. `MainLoopCallback`
    // (src/engine/engine/MainLoop.h) is a trivially destructible two-pointer
    // POD, so the trait-derived `~Array()` on `_postUpdate`/`_postDraw` IS that
    // exact body.
    self->_postUpdate.~Array();
    self->_postDraw.~Array();
    // NOT CONVERTED (0x180033880): this is a J2 entry (array_destroy.cpp),
    // `ReleaseStringsCounted(..., kD_Str_18)` then `ArrayDtorImpl` — a
    // per-element String RELEASE, not a trivial destroy (the element's
    // `_path` Filename, MainLoop.h). Retail's instantiation is a hand-derived
    // spec (kSpec_VideoResource / kD_Str_18, mainloop_video.cpp), not the
    // trait-derived `Array<VideoResourceEntry>::~Array()`. Bridging it needs an `ArraySpec` whose `destroy` calls
    // `ReleaseStringsCounted` with `kD_Str_18` (a `DestroySpec`, a different
    // struct from `ArraySpec`, defined in array_destroy.cpp) — that is new
    // Collection-side plumbing this batch does not own (src/engine/collection
    // is out of scope here), so this stays a by-name call of the 0x180033880 body
    // (Collection.h) rather than a forced (and wrong) `.~Array()`.
    rtcoll::RtColl_ArrayDtorElem_180033880(&self->_videoResources);

}

// ?_singleton@MainLoop@Engine@@1PEAV12@EA (0x1801fe750) -- the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
::Engine::MainLoop* ::Engine::MainLoop::_singleton = nullptr;
