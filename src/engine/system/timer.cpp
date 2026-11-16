// ============================================================================
// src/engine/system/timer.cpp — System::Timer (Wave 9, lane B3)
// ============================================================================
//
// The whole class, ten exported functions, byte-faithful against the disassembly of
// Runtime-x64.dll at the retail addresses named per function (read as
// DISASSEMBLY, not decompiled C).
//
// WHY THIS CLASS FIRST. `?SecondsToTicks@Timer@System@@SA_JM@Z` and
// `?TicksToSeconds@Timer@System@@SAM_J@Z` are two of lane V's REAL DEPENDENCY
// rows (docs/history/runtime_port/WAVE9_LANE_V.md §1b) and they sit under
// `System::Time`'s ctor and `AsSeconds` — the value type every
// `Update(const Time&)` in the game constructs.
//
// ---------------------------------------------------------------------------
// THE FIVE STATICS, AND A CORRECTION TO LANE V
// ---------------------------------------------------------------------------
// Lane V (WAVE9_LANE_V.md §4) lists `?_secondsPerTick@Timer@System@@0NA` as a
// DATA READ row and says of it: *"it is written at runtime by System::Timer's
// init from QueryPerformanceFrequency, so minting a VALUE for it is wrong; the
// port has to own the initialisation."*
//
// **The first half of that is wrong, and it is worth being precise about
// because it changes who has to do the work.** Reading `Timer::Startup`
// (0x1800ca7c0) there is no QPC in it at all:
//
//     MOV  EAX,0xf4240                 ; 1,000,000
//     MOVSD XMM1,[0x1801616d0]         ; 1.0
//     CVTSI2SD XMM0,RAX
//     MOV  [0x1801fe720],RAX           ; _ticksPerSecond   = 1000000
//     DIVSD XMM1,XMM0
//     MOVSD [0x1801fe728],XMM1         ; _secondsPerTick   = 1.0 / 1000000.0
//     JMP  0x1800ca860                 ; tail into PlatformStartup
//
// `_ticksPerSecond` and `_secondsPerTick` are compile-time constants written by
// a store; the engine's tick is a fixed microsecond, not the platform's. It is
// `_systemTicksPerSecond`, `_systemSecondsPerTick` and `_systemInitialTick`
// (0x1801fe730 / 0x1801fe740 / 0x1801fe738) that are QPC-derived, and those are
// `PlatformStartup`'s (0x1800ca860). So the two hot functions depend on a
// CONSTANT, which is why they can be initialised statically below and carry no
// ordering hazard whatsoever.
//
// ---------------------------------------------------------------------------
// WHY OUR STATICS ARE INITIALISED HERE AND NOT BY A CALL TO Startup
// ---------------------------------------------------------------------------
// `?Startup@Timer@System@@SA_NXZ` has **no internal caller in the DLL** — its
// only xrefs are the export directory and the entry point. `Engine::Startup`
// (0x1800db8a0) INLINES the body instead: at 0x1800db9aa..0x1800db9ca it writes
// the same two constants to the same two globals itself and then makes a direct
// `CALL 0x1800ca860` to `PlatformStartup`.
//
// Initialising `_ticksPerSecond` and `_secondsPerTick` at namespace scope, to the
// exact constants the store above writes, produces a value identical to retail's
// under every ordering — it is the same two literals.
//
// `_systemInitialTick` and the two QPC values genuinely need `PlatformStartup`
// to have run, so `CurrentTime`/`CurrentTimeTicks` call `EnsurePlatform()`
// first. See the note above `CurrentTimeTicks` for what that does and does not
// change, and for why it is not a "fix a faithful crash with a null check"
// (CLAUDE.md) — retail's own guarantee that Startup ran first does not survive
// the seam, and this is exactly the case where our standalone differs.
// ============================================================================

// ENDGAME B.3.4b SYSTEM (4): `System::Timer` is a real class with static
// members (src/engine/system/Timer.h). The mangling is the evidence —
// `?X@Timer@System@@SA…` / `CA…` is a static member of a class named
// `System::Timer`, which a free function cannot carry; Timer.h's banner has the
// full argument and the contrast with B.3.4a's `IO::TextCodec`, which came out
// the other way and landed nothing.
//
// TEN BODIES are members. Member bodies are defined at GLOBAL scope with
// qualified names rather than inside a `namespace System { ... }` block — the
// shape SYSTEM (3) adopted.
#include <cstdint>
#include "system/timer_internal.h"
#include "system/Timer.h"

// Phase D.4 THREADS E.4: QueryPerformanceCounter / QueryPerformanceFrequency are
// Platform::PerfCounter / PerfFrequency (src/platform/common/platform_time.h), same
// out-parameter and result; the raw dllimport declarations are gone.
#include "platform_time.h"

// ENDGAME B.3.9 SYS (B39_RULES D7): THE FIVE ARE CLASS STATICS, so
// `System::Timer::_x` is the only cell. The two constants are 1000000 and
// 1.0/1000000.0: retail Timer::Startup's two stores.

// ?_ticksPerSecond@Timer@System@@0_JA   retail 0x1801fe720
// ?_secondsPerTick@Timer@System@@0NA    retail 0x1801fe728
// Constants, per the Startup disassembly quoted in the file header.
int64_t System::Timer::_ticksPerSecond = 1000000;
double  System::Timer::_secondsPerTick = 1.0 / 1000000.0;

// ?_systemTicksPerSecond@Timer@System@@0_JA  retail 0x1801fe730
// ?_systemInitialTick@Timer@System@@0_JA     retail 0x1801fe738
// ?_systemSecondsPerTick@Timer@System@@0NA   retail 0x1801fe740
// QPC-derived; written by PlatformStartup only.
int64_t System::Timer::_systemTicksPerSecond = 0;
int64_t System::Timer::_systemInitialTick    = 0;
double  System::Timer::_systemSecondsPerTick = 0.0;

// ---------------------------------------------------------------------------
// Retail's `Engine::MainLoop::Run` (0x1800dd3c0, src/engine/engine/mainloop_run.cpp) divides by
// `_systemTicksPerSecond` (0x1800dd3f9, `IDIV qword ptr [0x1801fe730]`). The DLL-present crash on
// that IDIV and the data-slot publication that answered it:
// docs/history/runtime_port/WAVE22_LANE_TIMER.md; the MainLoop export table:
// docs/history/runtime_port/WAVE8_LANE_B.md.
// ---------------------------------------------------------------------------

// ?PlatformStartup@Timer@System@@CA_NXZ  retail 0x1800ca860 (private static).
//
// Retail on the QPF-failed branch calls the DLL's error reporter
// (0x1800c5fa0) with the literal at 0x180155f38 and returns false. That
// reporter is System::Error's, is not in this lane, and the branch is
// unreachable on every Windows version this game runs on (QPF has not been
// able to fail since XP). We return false without the message and say so here
// rather than reaching back into the DLL for one string on a dead path.
bool System::Timer::PlatformStartup() {
    int64_t freq = 0;
    if (!Platform::PerfFrequency(&freq)) return false;

    _systemTicksPerSecond = freq;
    // CVTSI2SD is signed; retail then adds 2^64 (0x180161720 = 1.8446744e19)
    // when the sign bit is set, i.e. it corrects the signed conversion back to
    // unsigned. Reproduced rather than simplified: a frequency with bit 63 set
    // is impossible in practice but the arithmetic is what the DLL does.
    double freqAsDouble = static_cast<double>(freq);
    if (freq < 0) freqAsDouble += 18446744073709551616.0;
    _systemSecondsPerTick = 1.0 / freqAsDouble;

    Platform::PerfCounter(&_systemInitialTick);
    return true;
}

// ?Startup@Timer@System@@SA_NXZ  retail 0x1800ca7c0
// The two stores are already done by the namespace-scope initialisers above;
// they are repeated here because this function's contract is to establish
// them, and a caller that ran Shutdown first (a no-op in every shipped build,
// see below) must still get a live Timer back.
bool System::Timer::Startup() {
    _ticksPerSecond = 1000000;
    _secondsPerTick = 1.0 / static_cast<double>(1000000);
    return PlatformStartup();
}

// ?Shutdown@Timer@System@@SAXXZ          retail 0x1800ca7b0 — `RET`, no body.
// ?PlatformShutdown@Timer@System@@CAXXZ  retail 0x1800ca8e0 — `RET`, no body.
// Both are a bare return in the shipped build, and both are empty here.
void System::Timer::Shutdown() {}
void System::Timer::PlatformShutdown() {}

// Engine::Startup (src/engine/engine/engine_lifecycle.cpp) calls Timer::Startup,
// which runs PlatformStartup. TimerEnsurePlatformStarted runs PlatformStartup
// itself while `_systemTicksPerSecond` is still zero, for an absolute-time reader
// that runs before that.
void RtSystem::TimerEnsurePlatformStarted() {
    if (System::Timer::_systemTicksPerSecond == 0) System::Timer::PlatformStartup();
}

// The five data accessors declared in timer_internal.h. TicksPerSecond and
// SecondsPerTick are constants and need no init; the three QPC-derived ones
// ensure PlatformStartup has run, so a consumer that reaches them before
// Engine::Startup gets a live value instead of zero.
int64_t RtSystem::TimerTicksPerSecond()       { return System::Timer::_ticksPerSecond; }
double  RtSystem::TimerSecondsPerTick()       { return System::Timer::_secondsPerTick; }
int64_t RtSystem::TimerSystemTicksPerSecond() { TimerEnsurePlatformStarted(); return System::Timer::_systemTicksPerSecond; }
int64_t RtSystem::TimerSystemInitialTick()    { TimerEnsurePlatformStarted(); return System::Timer::_systemInitialTick; }
double  RtSystem::TimerSystemSecondsPerTick() { TimerEnsurePlatformStarted(); return System::Timer::_systemSecondsPerTick; }

namespace { inline void EnsurePlatform() { RtSystem::TimerEnsurePlatformStarted(); } }

// ?GetTicks@Timer@System@@CA_KXZ  retail 0x1800ca840 (private static).
// A raw QPC sample; the name's `_K` is unsigned but the OS writes a signed
// LARGE_INTEGER and retail moves it straight out.
uint64_t System::Timer::GetTicks() {
    int64_t now = 0;
    Platform::PerfCounter(&now);
    return static_cast<uint64_t>(now);
}

// ?CurrentTimeTicks@Timer@System@@SA_JXZ  retail 0x1800ca8f0
//   (QPC() - _systemInitialTick) * _ticksPerSecond / _systemTicksPerSecond
// with a signed IMUL/CQO/IDIV — engine ticks (microseconds) since startup.
//
int64_t System::Timer::CurrentTimeTicks() {
    EnsurePlatform();
    int64_t now = 0;
    Platform::PerfCounter(&now);
    return CounterToTicks(now - _systemInitialTick, _ticksPerSecond, _systemTicksPerSecond);  // F-151
}

// ?CurrentTime@Timer@System@@SAMXZ  retail 0x1800ca760
// The same integer expression, then `(float)(ticks * _secondsPerTick)` —
// CVTSI2SD, MULSD, CVTPD2PS, i.e. the multiply is done in double and rounded
// to float exactly once.
float System::Timer::CurrentTime() {
    EnsurePlatform();
    int64_t now = 0;
    Platform::PerfCounter(&now);
    int64_t ticks = CounterToTicks(now - _systemInitialTick, _ticksPerSecond, _systemTicksPerSecond);  // F-151
    return static_cast<float>(static_cast<double>(ticks) * _secondsPerTick);
}

// ?SecondsToTicks@Timer@System@@SA_JM@Z  retail 0x1800ca800
//   XORPS XMM1,XMM1 / CVTSS2SD XMM1,XMM0 / DIVSD XMM1,[_secondsPerTick]
//   / CVTTSD2SI RAX,XMM1
// float widened to double, divided in double, then TRUNCATED toward zero.
int64_t System::Timer::SecondsToTicks(float seconds) {
    int64_t ours = static_cast<int64_t>(static_cast<double>(seconds) / _secondsPerTick);
    using Fn = int64_t (*)(float);
    return ours;
}

// ?TicksToSeconds@Timer@System@@SAM_J@Z  retail 0x1800ca820
//   CVTSI2SD XMM0,RCX / MULSD XMM0,[_secondsPerTick] / CVTPD2PS XMM0,XMM0
// SIGNED widening (CVTSI2SD, not the unsigned pair), multiply in double, one
// rounding to float at the end.
float System::Timer::TicksToSeconds(int64_t ticks) {
    return static_cast<float>(static_cast<double>(ticks) * _secondsPerTick);
}

// ??4Timer@System@@QEAAAEAV01@AEBV01@@Z  retail 0x180007890
// The compiler-generated copy assignment: `MOV RAX,RCX; RET`. System::Timer
// has no instance state at all -- its five data members are all class statics
// -- so assignment copies nothing and returns `this`. Two instructions,
// and it is the tenth and last export of the class.
System::Timer& System::Timer::operator=(const System::Timer& /*other*/) { return *this; }
