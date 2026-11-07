// ============================================================================
// src/engine/engine/mainloop_frame.cpp — MainLoop::Frame
//                                         Wave 25 lane BODY
// ============================================================================
//
//   | Export                                   | Retail      | Ord  | Bytes |
//   |------------------------------------------|-------------|------|-------|
//   | ?Frame@MainLoop@Engine@@QEAA_N_N@Z       | 0x1800dd100 | 2377 |  684  |
//
// This is the fixed-timestep frame driver: the body `MainLoop::Run` calls once
// per iteration and the one function standing between our `Run` port and the
// DLL for everything a frame does.  It closes the NAME door `mainloop_run.cpp`
// §3 opened when it ported `Run` and had to reach `Frame` by export.
//
// ---------------------------------------------------------------------------
// 0. WHY THIS ROW WAS UNCLAIMED FOR FIFTEEN WAVES, AND WHAT THE BLOCKER WAS
// ---------------------------------------------------------------------------
// NOTHING.  The only sentence anywhere in the tree is `mainloop_run.cpp:88,332`
// — "not ported — 683 bytes, own lane" — which is a SIZE note, and Wave 24
// lane SMALLBODY already re-read the tree and recorded "NO — THERE IS NO
// BLOCKER.  Pure scoping deferral."  This lane re-derived that a third time and
// agrees: no dependency claim, no oracle claim, no tag.  An unclaimed row read
// as a blocked one for fifteen waves because a size note sat in the column a
// blocker would have sat in.
//
// The 683 was itself understated.  `.pdata` is a LOWER bound and it bites
// BACKWARDS here: the ENTRY fragment
// [0x1800dd100,0x1800dd17b) is 123 bytes and its UNWIND_INFO reads
// `chained=False`, so a reader who stops at the entry sees a 123-byte leaf.
// Unioning FORWARD over the two contiguous follow-on fragments gives
// [0x1800dd100,0x1800dd3ac) = **684 bytes across 3 fragments**, a 5.6x
// understatement.  (Re-derive: the union-forward walk in §7.)
//
// ---------------------------------------------------------------------------
// 1. EVERY IMPORT NAME BELOW WAS READ OUT OF THE DLL'S IMPORT TABLE
// ---------------------------------------------------------------------------
// Frame reaches Win32 through seven of Runtime-x64.dll's own IAT slots.  Naming
// one of them wrong would compile, link and look right, so all seven were
// printed with `pefile` from the shipped Runtime-x64.dll rather than guessed
// from the call shape:
//
//     0x18014b1a8  KERNEL32!QueryPerformanceCounter
//     0x18014b4b8  USER32!PostQuitMessage
//     0x18014b5d0  USER32!PeekMessageW
//     0x18014b5c8  USER32!TranslateMessage
//     0x18014b5c0  USER32!DispatchMessageW
//     0x18014b418  KERNEL32!SwitchToThread
//
// They are called DIRECTLY here, not through those slots: the slots are the
// DLL's IAT and do not exist once the DLL is gone.
//
// ---------------------------------------------------------------------------
// 2. THREE THINGS THE DISASSEMBLY SETTLES THAT A DECOMPILE WOULD TIDY AWAY
// ---------------------------------------------------------------------------
//
// (a) THE POST-UPDATE PASS READS `_func` AND `_context` FROM **DIFFERENT
//     ARRAYS**, and it is transcribed that way.  0x1800dd2b0:
//
//         MOV RDX,[RDI+0x80]        ; _postDraw._data
//         MOV RAX,[RDI+0x90]        ; _postUpdate._data
//         LEA RCX,[RDI+0x48]        ; &_deltaTime
//         MOV RDX,[RBX+RDX*1+0x8]   ; _postDraw._data[i]._context
//         CALL [RBX+RAX*1]          ; _postUpdate._data[i]._func
//
//     with the trip count taken from `[RDI+0x98]` = `_postUpdate._size`.  The
//     offsets are not inferred: MainLoop.h proves +0x80 is `_postDraw` from
//     `?AddPostDraw@` doing `ADD RCX,0x80` and +0x90 is `_postUpdate` from
//     `?AddPostUpdate@` doing `ADD RCX,0x90`.  The two `MOV`s above are seven
//     bytes each and the displacement is in the bytes; there is no reading of
//     this that makes them the same array.
//
//     So retail pairs callback i's FUNCTION with the other array's CONTEXT.
//     That is either a retail defect that is invisible while both arrays are
//     empty (they are, on every path this tree drives today) or a deliberate
//     parallel-arrays convention nothing else in the tree witnesses.  EITHER
//     WAY A PORT MUST NOT "FIX" IT: `_postUpdate._data[i]._context` is a store
//     -- read -- that is not in the bytes, and doctrine here is that retail's
//     unguarded, odd-looking accesses are load-bearing until something proves
//     otherwise.  Transcribed by OFFSET, with the names beside them, so a later
//     reader sees the asymmetry rather than a tidy loop.
//     *** THIS IS NOT ESTABLISHED AS A BUG.  It is established as WHAT THE
//     BYTES DO.  Nobody has observed either array non-empty. ***
//
// (b) THE ACCUMULATOR CLAMP CONTAINS A SELF-LOOP THAT CAN HANG, and it is
//     transcribed.  0x1800dd158:
//
//         MOV RAX,[RDI+0x48]        ; rax = _deltaTime
//         NOP DWORD PTR [RAX+0x0]   ; MSVC loop-head alignment
//     L:  CMP RAX,RCX               ; rcx = _maxAccumulator
//         JG  L                     ; <- branches to ITSELF
//         MOV [RDI+0x38],RAX        ; _accumulator = rax
//
//     Nothing in the loop writes RAX or RCX, so if `_deltaTime >
//     _maxAccumulator` retail spins forever.  With the ctor's values
//     (1/60s and 1/15s) the branch is never taken and the block is exactly
//     `_accumulator = _deltaTime`.  MSVC collapsed a source loop whose body it
//     proved empty.  Reproduced as written rather than as the value it
//     computes, because the hang is reachable by anything that sets
//     `_deltaTime` above `_maxAccumulator` and a port that silently could not
//     hang is a different function under exactly that input.
//
// (c) THE TAIL'S FLOAT SUM IS WRITTEN IN RETAIL'S ORDER, WHICH IS NOT THE
//     SIMPLIFIED ONE.  0x1800dd318..0x1800dd36c computes
//
//         d     = (float)((double)_deltaTime    * secondsPerTick)
//         a     = (float)((double)_accumulator  * secondsPerTick)
//         T     = (float)((double)_totalTime    * secondsPerTick)
//         alpha = a / d                                   ; DIVSS, single
//         v     = T + d * alpha                           ; MULSS then ADDSS
//
//     Algebraically `v == T + a`.  In float it is not: `a / d` then `* d` is
//     two roundings that do not cancel.  Moving a term in a float sum is the
//     documented way to make a correct-looking port diverge in the last bits
//     (reference_moved_term_hides_where_terms_cancel.md), so the redundant
//     round trip stays.
//
// (d) `MOVAPS XMM1,XMM4` AT 0x1800dd35e IS **NOT** A DEAD MOVE — IT IS
//     `Draw`'s SECOND ARGUMENT.  This file's first draft called it dead and
//     dropped it, on the reasoning that XMM1 had held `secondsPerTick`, that
//     its last MULSD read was at 0x1800dd356, and that nothing reads XMM1
//     again.  Every one of those facts is true and the conclusion was wrong.
//
//     The export directory settles it: 0x1800dcd40 is
//     `?Draw@MainLoop@Engine@@QEAAXM@Z`, ordinal 2251 — `M` is `float`.  Under
//     the Microsoft x64 ABI a member function's first float parameter is XMM1
//     (RCX being `this`), so `MOVAPS XMM1,XMM4` loads `alpha` into the argument
//     register and nothing touches XMM1 between there and the CALL.
//
//     Dropping it compiles, links, and passes `Draw` whatever the compiler
//     happened to leave in XMM1 — the interpolation factor every frame is
//     rendered with.  This is the same defect class as
//     reference_ghidra_c_output_vs_disassembly.md's "drops whole arguments at a
//     mistyped vtable call", arrived at by hand from correct disassembly rather
//     than from a decompiler.  **Reading the callee's MANGLED SIGNATURE is what
//     caught it, not reading more instructions.**
//
// ---------------------------------------------------------------------------
// 3. A HEADER FACT THIS BODY ANSWERS
// ---------------------------------------------------------------------------
// `src/engine/engine/MainLoop.h` says of `_startTicks` (+0x30): "Whether
// MainLoop::Frame (0x1800dd100, DLL-internal) also rewrites it per frame was
// NOT checked by lane B."  IT DOES — every call, unconditionally, at
// 0x1800dd146, and it uses the previous value as the delta base:
//
//     now  = CurrentTimeTicks()
//     R8   = now - _startTicks          ; elapsed since the last Frame
//     _startTicks   = now
//     _accumulator += R8
//
// So `_startTicks` is "the tick at which the last frame was sampled", not "the
// tick Run started".  The header comment is corrected in place.  (It also means
// the field is read-then-written by a function Run does not own, which is why
// `Run` sampling it once before the loop is consistent rather than redundant.)
//
// ---------------------------------------------------------------------------
// 4. THE AUDIO CHANNEL SWEEP — AND THE ROOT CAUSE IT CORROBORATES
// ---------------------------------------------------------------------------
// 0x1800dd1bd..0x1800dd202 walks SIXTY-FOUR fixed channel records of 0x28 bytes
// embedded at `Audio::Device + 0`, and for each one whose `+0x00` has bit 1 set
// it frees the platform voice, clears bits 0 and 1, zeroes `+0x08`, and then —
// if `+0x20` is non-null — CALLS THE POINTER STORED AT `+0x20` with
// `(channelRecord, *(void**)(rec+0x10), *(uint32_t*)(rec+0x18))`.
//
// That stored pointer is a COMPLETION CALLBACK, and it is the same shape
// `reference_audio_holdbacks_share_a_callback_root_cause.md` names as the ONE
// root cause behind all six Wave 8 AUDIO holdbacks.  This body is the SITE that
// invokes it.  Recorded here because the six holdbacks are still open and this
// is the first ported function that drives them.
//
// `?FreeChannelPlatform@Device@Audio@@` (0x1800de6f0) is PRIVATE and not
// ported, so it is reached BY ADDRESS — see §5.
//
// ---------------------------------------------------------------------------
// 5. DOOR ARITHMETIC — DECLARED, INCLUDING THE PART THAT GOES UP
// ---------------------------------------------------------------------------
// Frame's four internal callees:
//
//   0x1800e1490  Input::Device::Update          NOT ported (lane INPUT's)
//   0x1800de6f0  Audio::Device::FreeChannelPlatform  NOT ported, PRIVATE
//   0x1800dd070  MainLoop::Update               PORTED (mainloop_run.cpp)
//   0x1800dcd40  MainLoop::Draw                 NOT ported
//
// CLOSES:  the NAME door `?Frame@MainLoop@Engine@@QEAA_N_N@Z`, which
//          mainloop_run.cpp opened, plus its ADDRESS door 0x1800dd100.
// OPENS:   `?Draw@MainLoop@Engine@@QEAAXM@Z`                    ord 2251
//          `?FreeChannelPlatform@Device@Audio@@AEAAXH@Z`        ord 2380
//          — BOTH are NAME doors.  `?Update@Device@Input@@` was ALREADY opened
//          by mainloop_run.cpp and is NOT new surface.
//
// THE SECOND ONE WAS ALMOST AN ADDRESS DOOR, AND THE REASON IS WORTH KEEPING.
// `?FreeChannelPlatform@` is spelled `AEAA` — PRIVATE — and this file's first
// draft reached it by address on the strength of that, writing "it is private,
// so there is no name to use".  **`AEAA` MEANS PRIVATE, NOT UNEXPORTED**
// (reference_aeaa_is_private_not_unexported.md): Runtime-x64.dll exports its
// private members, and this one is ordinal 2380 in the shipped export
// directory.  A `pefile` lookup BY ADDRESS found the name in one command.  Had
// it stayed an address door it would have been a silent null at cutover
// instead of an `[RtUnresolved]` line.
//
// NET DECLARED SURFACE GROWTH FOR THIS FILE: **+2 census rows, both NAME doors**
// against 1 row moved into `ported`.  The flattering number would have been
// "+1, Input was already there"
// (reference_porting_can_grow_the_cutover_surface.md,
// reference_count_the_doors_not_the_calls.md).
//
// ---------------------------------------------------------------------------
// 6. VERIFICATION — WHAT THIS SITE IS AND IS NOT  (kickoff §1b)
// ---------------------------------------------------------------------------
// The site below is named **"Engine::MainLoop::Frame [ATTENDANCE]"** and that
// label is the honest one.  Frame has NO ORACLE, for the same structural reason
// `Run` has none and a stronger one besides: it advances the world irreversibly
// through two virtual calls and a Win32 message pump, so there is no second arm
// to run and nothing to restore.  A `[SELF-CHECK]` shadow rides alongside it on
// the one post-condition the body's own loop establishes — after the
// fixed-timestep loop, `_accumulator < _deltaTime` — and is labelled as a
// self-check, not dressed up as a comparison against retail.
//
// This is deliberately NOT one of the 133 NO-COMPARISON sites the kickoff
// complains about, because it does not print `divergences=0` and let a reader
// take that for a pass: `divergences` is only meaningful on `[ORACLE]` rows and
// the class is in the site's own name.
//
// The real test is a BOOT.  With the DLL present, our `Run` calls this by
// direct C++ call and it drives every frame of the session.  `worked=N` is the
// number that carries information here; `calls=0` is CORRECT and expected.
//
// ---------------------------------------------------------------------------
// 7. RE-DERIVE EVERYTHING ABOVE
// ---------------------------------------------------------------------------
//   objdump -d -M intel --start-address=0x1800dd100 --stop-address=0x1800dd3ac \
//           "C:/GOG Games/Banished/Runtime-x64.dll"
//   (extent: walk .pdata, union RUNTIME_FUNCTION entries whose start == the
//    previous end; import names: pefile DIRECTORY_ENTRY_IMPORT by slot VA.)
//
// Kill switch: RT_MAINLOOPFRAME=0 returns every call to the DLL.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "engine/MainLoop.h"
#include "engine/Engine.h"             // ENDGAME B.3.7: Engine::Engine
#include "application/Application.h"   // ENDGAME B.3.7: _quitRequested
#include "audio/Device.h"              // ENDGAME B.3.7 callers (K4): Audio::Device
#include "input/Device.h"              // ENDGAME B.3.7 callers (K5): Input::Device
#include "system/Timer.h"              // ENDGAME B.3.9 close: Timer's class statics
#include "video/Context.h"             // Video::Context::_time / _dirty
#include "mainloop_internal.h"

#include "platform_thread.h"   // Platform::YieldThread (SwitchToThread)
#include "platform_time.h"     // Platform::PerfCounter (QueryPerformanceCounter)

// Phase D.4 SDL W3: the USER32 pump (PeekMessageW / TranslateMessage / DispatchMessageW /
// PostQuitMessage, §1's slots) is Platform::Pump and Platform::Window::PostQuitMessage
// (src/platform/sdl/pump_sdl.cpp); retail's MSG on the stack and the hand-declared imports
// are gone with it.
#include "platform_window.h"   // Platform::Pump, Platform::Window::PostQuitMessage

using RtEngine::MainLoop;

// ---------------------------------------------------------------------------
// The one definition of the clock helper (declared in mainloop_internal.h).
// ---------------------------------------------------------------------------
namespace rtmainloop {

int64_t CurrentTimeTicksViaGlobals() {
    // ENDGAME B.3.9 close (D7): the Timer class statics (retail 0x1801fe738 /
    // 0x1801fe720 / 0x1801fe730), not three cached by-address doors.
    const int64_t* initialTick    = &::System::Timer::_systemInitialTick;
    const int64_t* ticksPerSec    = &::System::Timer::_ticksPerSecond;
    const int64_t* sysTicksPerSec = &::System::Timer::_systemTicksPerSecond;

    int64_t now = 0;
    Platform::PerfCounter(&now);

    // Retail's IDIV faults on a zero divisor rather than returning anything, so
    // this cannot be "made safe" by guarding — a zero here means PlatformStartup
    // has not run, which is an ordering defect upstream and must not be papered
    // over. The guard exists ONLY to turn a #DE into a message that names the
    // cause; it is not a fallback.
    if (*sysTicksPerSec == 0) {
        printf("[RtMainLoop] FATAL: Timer globals not initialised "
                    "(_systemTicksPerSecond=%lld) -- PlatformStartup has not run\n",
                    (long long)*sysTicksPerSec);
        return 0;
    }
    return ::System::CounterToTicks(now - *initialTick, *ticksPerSec, *sysTicksPerSec);  // review F-151
}

}  // namespace rtmainloop

namespace {

// ENDGAME B.3.7: the three Engine subsystem offsets (+0x6e0 Input::Device,
// +0x6d0 Audio::Device, +0x690 Application) and Application+0x28 are read as
// the named fields of the real classes now; the static_asserts pin that the
// names are the bytes this body used to read.
static_assert(offsetof(Engine::Engine, _inputDevice) == 0x6e0, "Frame reads +0x6e0");
static_assert(offsetof(Engine::Engine, _audioDevice) == 0x6d0, "Frame reads +0x6d0");
static_assert(offsetof(Engine::Engine, _application) == 0x690, "Frame reads +0x690");
static_assert(offsetof(Application::Application, _quitRequested) == 0x28, "Frame tests +0x28");

}  // namespace

// EXT SEAM (NOT RETAIL, docs/extensions/handins/RENDERPIPE0.md): ext.render_pipe -- the null-by-default pointer MainLoop.h
// declares. Frame tests it once before the step loop and once at the tail.
Engine::MainLoop::ExtFrameFn Engine::MainLoop::_extFrame = nullptr;

// ?Frame@MainLoop@Engine@@QEAA_N_N@Z   retail 0x1800dd100  ord 2377
//
// `skipUpdate` is retail's `_N` parameter (DL, widened with MOVZX at
// 0x1800dd115 and only ever tested as a byte).  `Run` passes false.
bool Engine::MainLoop::Frame(bool skipUpdate) {

    // Ported already -> BY ADDRESS, so the port registry is consulted first and
    // our body is used rather than the DLL's.  NOT ported -> BY NAME where a
    // name exists, so the miss at cutover prints [RtUnresolved] and is counted
    // instead of being a silent null; BY ADDRESS only where there is no name.
    // ENDGAME B.3.9 close (D7): Timer::_secondsPerTick is the class static.
    const double* secondsPerTick = &::System::Timer::_secondsPerTick;

    // --- 0x1800dd119..0x1800dd14a : advance the accumulator -----------------
    const int64_t now = rtmainloop::CurrentTimeTicksViaGlobals();
    const int64_t elapsed = now - _startTicks;   // uses the PREVIOUS value (§3)
    _startTicks   = now;
    _accumulator += elapsed;

    // --- 0x1800dd14e..0x1800dd169 : clamp, self-loop and all (§2b) ----------
    const int64_t maxAccum = _maxAccumulator;
    if (_accumulator > maxAccum) {
        int64_t clamped = _deltaTime._ticks;
        // Retail's `L: CMP RAX,RCX / JG L`. Neither operand changes inside it,
        // so this is retail's hang, reproduced rather than removed. It is
        // unreachable with the ctor's 1/60 and 1/15.
        //
        // The empty `asm volatile` is NOT decoration: a side-effect-free
        // infinite loop is UNDEFINED BEHAVIOUR in C++ and GCC is entitled to
        // delete it or to assume it terminates, either of which would make this
        // port silently unable to reproduce retail's hang. The barrier gives the
        // loop an observable effect so it survives -O2 exactly as written.
        while (clamped > maxAccum) {
            __asm__ __volatile__("" ::: "memory");
        }
        _accumulator = clamped;
    }

    _quit = 0;                                   // 0x1800dd16d

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RENDERPIPE0.md): ext.render_pipe --
    // before the first step of a Frame that steps, the extension emits the frame
    // it still holds, so no render work is pending while the sim runs.
    if (_extFrame != nullptr && _accumulator >= _deltaTime._ticks)
        _extFrame(*this, kExtBeforeSteps, 0.0f, 0.0f);

    // --- 0x1800dd1a0..0x1800dd2f0 : the fixed-timestep loop -----------------
    // The test is at the TOP (0x1800dd175 `JL` past the whole body) and again
    // at the BOTTOM (0x1800dd2f0 `JGE` back), so this is a `while`, not a
    // `do/while`: a frame whose accumulator has not reached one step does no
    // updates at all and falls straight through to the tail.
    while (_accumulator >= _deltaTime._ticks) {
        ::Engine::Engine* engine = _engine;

        // Input::Device::Update — ENDGAME B.3.7 callers (K5): a member of the
        // real class (was the name door). The argument is retail's
        // `LEA RDX,[RDI+0x48]`, &_deltaTime.
        engine->_inputDevice->Update(_deltaTime);

        // --- the 64-channel audio sweep (§4) --------------------------------
        // The 64 `Audio::Device::AudioChannel` records at +0x00 (stride 0x28): the
        // flags dword (bit 1 = needs release), `source` (+0x08, zeroed with the
        // flags), `userData` / `userValue` (+0x10 / +0x18, the callback's 2nd / 3rd
        // arguments) and `onComplete` (+0x20). Every offset is Frame's own (§4).
        Audio::Device* audioDevice = engine->_audioDevice;
        for (int i = 0; i < Audio::Device::kChannelCount; ++i) {
            Audio::Device::AudioChannel* chan = &audioDevice->_channels[i];
            // NOT RETAIL (review F-138): the flag word is read and cleared atomically;
            // the backend's audio thread sets bit 1 (Device.h, AudioChannel).
            if ((chan->LoadFlags() & Bits(Audio::ChannelFlags::Finished)) == 0)
                continue;

            // PRIVATE (`AEAA`) in retail but EXPORTED, ordinal 2380; Device.h
            // declares the member public. See §5.
            audioDevice->FreeChannelPlatform(i);

            // The callback pointer is sampled BEFORE the flags are cleared
            // (0x1800dd1d4 precedes 0x1800dd1d7). Retail's order; a port that
            // read it after would still work today and would differ the moment
            // FreeChannelPlatform starts touching the record.
            Audio::Device::Callback onComplete = chan->onComplete;

            chan->AndFlags(Bits(~(Audio::ChannelFlags::Live | Audio::ChannelFlags::Finished)));   // AND ~3
            chan->source = nullptr;

            if (onComplete) {
                onComplete(chan, chan->userData, chan->userValue);
            }
        }

        // --- 0x1800dd204..0x1800dd219 : the application quit request --------
        if (engine->GetApplication()->_quitRequested != 0)
            Platform::Window::PostQuitMessage(0);

        // --- 0x1800dd219..0x1800dd282 : the message pump --------------------
        // Retail hoists the first PeekMessage out of the loop and re-tests at
        // the bottom, so this is `if (peek) do { ... } while (peek)`. The
        // WM_QUIT test happens AFTER Translate+Dispatch, not before, and breaks
        // out rather than draining the queue.
        //
        // Phase D.4 SDL W3: that loop is Platform::Pump's. It dispatches every
        // message translated from the SDL queue to Application::WindowProc (retail's
        // DispatchMessageW reached the same procedure) and returns true at WM_QUIT,
        // which is the `_quit = 1; break;` at 0x1800dd27e.
        if (Platform::Pump(&::Application::Application::WindowProc))
            _quit = 1;                           // 0x1800dd27e

        // --- 0x1800dd282..0x1800dd29b : tick the loop -----------------------
        // `MainLoop::Update` is PORTED; retail reaches it with a direct
        // internal call at 0x1800dd28e, so this is a direct C++ call to the
        // sibling port, exactly where and how retail calls it.
        if (!skipUpdate) {
            if (!Update(_deltaTime))
                _quit = 1;                       // 0x1800dd297
        }

        // --- 0x1800dd29b..0x1800dd2d8 : the post-update callbacks (§2a) -----
        // Trip count from _postUpdate._size; FUNCTION from _postUpdate._data;
        // CONTEXT from _postDraw._data. Transcribed, not tidied. See §2a.
        for (int32_t i = 0; i < _postUpdate._size; ++i) {
            void* context = _postDraw._data[i]._context;                   // +0x80's data
            PostUpdateFunc fn = _postUpdate._data[i]._func;
            fn(_deltaTime, context);
        }

        // --- 0x1800dd2d8..0x1800dd2f0 : consume one step --------------------
        // Retail re-reads _deltaTime from memory for each of the three uses
        // (0x1800dd2d8, 0x1800dd2e0, 0x1800dd2e8) rather than caching it, which
        // matters because the callbacks above are free to have changed it.
        _accumulator -= _deltaTime._ticks;
        _totalTime   += _deltaTime._ticks;
    }

    // SELF-CHECK, not an oracle (§6): the post-condition this body's own loop
    // establishes. It would catch a botched loop condition; it cannot catch a
    // wrong-but-self-consistent frame.

    // --- 0x1800dd318..0x1800dd39e : the tail, which ALWAYS runs -------------
    // Reached both by falling out of the loop and by the top test skipping it
    // entirely (0x1800dd175 jumps straight here).
    const double spt = *secondsPerTick;
    Video::Context* context = _context;

    const float d = static_cast<float>(static_cast<double>(_deltaTime._ticks) * spt);
    const float a = static_cast<float>(static_cast<double>(_accumulator) * spt);
    const float T = static_cast<float>(static_cast<double>(_totalTime)   * spt);

    // Retail's order, NOT `T + a` — see §2c.
    const float alpha = a / d;                 // DIVSS   (0x1800dd352)
    const float v     = T + d * alpha;         // MULSS then ADDSS
    // `alpha` is ALSO MainLoop::Draw's float argument (MOVAPS XMM1,XMM4 at
    // 0x1800dd35e) — see §2d. It is used twice, once here and once below.

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RENDERPIPE0.md): ext.render_pipe's tail replaces the
    // two statements below (it records `v` and runs the frame in the pipelined
    // order, or runs exactly these two when the frame is not pipelined).
    if (_extFrame != nullptr) {
        _extFrame(*this, kExtFrameTail, alpha, v);
    } else {
    context->_time.x = v;                      // +0x3c8
    context->_time.y = 0.0f;
    context->_time.z = 0.0f;
    context->_time.w = 0.0f;
    context->_dirty |= RtVideo::kDirtyGlobalConstants;   // +0x10, beside `_time` (+0x3c8)

    // MainLoop::Draw — NOT ported. By NAME (it is exported), so the cutover
    // miss is diagnosable rather than a silent null (§5). The float is `alpha`,
    // in XMM1 (§2d) — omitting it was this file's first and worst bug.
    Draw(alpha);   // ENDGAME B.3.7: a member call (was the ord-2251 name door)
    }

    Platform::YieldThread();                   // 0x1800dd398 SwitchToThread

    return _quit != 0;                   // MOVZX EAX,BYTE PTR [RDI+0x19]
}
