#pragma once
// ============================================================================
// src/engine/system/Timer.h — the REAL `System::Timer`
// ============================================================================
//
// ENDGAME B.3.4b SYSTEM (4). Design: B34B_RULES.md §1.1 (the export list by
// mangled name and retail address), rule B4b-4 (the five statics are NOT taken
// into C++ in this step), rule B4b-5 (forward-declaration tags);
// ENDGAME §B.1 decision 5 (retail's names).
//
// *** IT IS A CLASS WITH STATIC MEMBERS, AND THE MANGLING IS THE EVIDENCE. ***
// This was the question the step had to answer before writing a line, because
// B.3.4a's `IO::TextCodec` came out the other way — four receiverless free
// functions with no exported class-scoped names, and NOTHING landed for it,
// which is a valid recorded outcome. `Timer` is not that shape. Every one of
// its fourteen exported symbols carries `@Timer@System@@` and a
// member-function or static-data mangling:
//
//   `SA` = PUBLIC STATIC member function
//       ?Startup@Timer@System@@SA_NXZ            0x1800ca7c0
//       ?Shutdown@Timer@System@@SAXXZ            0x1800ca7b0
//       ?CurrentTime@Timer@System@@SAMXZ         0x1800ca760
//       ?CurrentTimeTicks@Timer@System@@SA_JXZ   0x1800ca8f0
//       ?SecondsToTicks@Timer@System@@SA_JM@Z    0x1800ca800
//       ?TicksToSeconds@Timer@System@@SAM_J@Z    0x1800ca820
//   `CA` = PRIVATE STATIC member function
//       ?PlatformStartup@Timer@System@@CA_NXZ    0x1800ca860
//       ?PlatformShutdown@Timer@System@@CAXXZ    0x1800ca8e0
//       ?GetTicks@Timer@System@@CA_KXZ           0x1800ca840
//   `QEAA` = PUBLIC NON-STATIC member function
//       ??4Timer@System@@QEAAAEAV01@AEBV01@@Z    0x180007890  (operator=)
//   `0` = PRIVATE STATIC DATA member — the five in §4.1's redirect table.
//
// A free function cannot be spelled `?X@Timer@System@@SA…`; MSVC only emits
// that for a static member of a class named `System::Timer`. So the class
// exists in retail's source and this header declares it.
//
// *** IT HAS NO INSTANCE STATE, AND THEREFORE NO LAYOUT. ***
// All five data members are `private: static`. `operator=` is the
// compiler-generated copy assignment and its whole shipped body is
// `MOV RAX,RCX; RET` — it copies nothing because there is nothing to copy.
// That is why this header carries no `offsetof` and no `sizeof` assert against
// a POD: there is no POD, and `.claude/rules/type-headers.md`'s "static_assert
// every field" has no field to act on. An empty class is 1 byte in both ABIs
// and nothing in the tree constructs one.
//
// *** SUPERSEDED BY ENDGAME B.3.9 SYS: the five statics ARE class statics now
// (bottom of the class), redirected together. The B.3.4b text below is history. ***
//
// *** THE FIVE STATICS STAY `RT_DATA_SLOT` READS — RULE B4b-4. ***
// `?_ticksPerSecond@`, `?_secondsPerTick@`, `?_systemTicksPerSecond@`,
// `?_systemInitialTick@` and `?_systemSecondsPerTick@` are all
// `CUTOVER_REQUIRED` rows (manifest lines 75, 83, 85, 87, 89) answered by a
// minted storage cell. Turning one into a real `static` member REDIRECTS that
// cell at `standalone/rt_data_mint.cpp`'s `MintStorageCell()` — the B.3.3
// `?_firstTypeInfo@BaseTypeInfo@Object@@` precedent — and `standalone/` is
// another agent's file set. So the FUNCTIONS convert here and the STATICS do
// not; the debt is tabled per row in B34B_RULES §4.1 and is owed at the
// class-static conversion, the end of B.3.
//
// And they must redirect TOGETHER or not at all: `timer.cpp:124-142`'s own
// banner explains that all five have to be published at the same moment,
// because an unported reader that sees three of them moved and two not is the
// IDIV-by-zero that banner was written to fix.
//
// The storage behind them stays where `timer.cpp` put it (file-scope globals
// reached through `src/engine/system/timer_internal.h`'s five `RtSystem::Timer*`
// accessors, which have four consumers in `application/`, `audio/`, `engine/`
// and `save/`). Nothing about that moves either.
// ============================================================================

#include <cstdint>

namespace System {

class Timer {
public:
    // ---- the six PUBLIC statics (`SA`) -------------------------------------
    // ?Startup@Timer@System@@SA_NXZ           0x1800ca7c0
    static bool     Startup();
    // ?Shutdown@Timer@System@@SAXXZ           0x1800ca7b0 — a bare RET in the
    //   shipped build; ported as empty so it is not a missing symbol.
    static void     Shutdown();
    // ?CurrentTime@Timer@System@@SAMXZ        0x1800ca760
    // ?CurrentTimeTicks@Timer@System@@SA_JXZ  0x1800ca8f0
    //   ⚠ BOTH RETURN AN OFFSET FROM AN EPOCH, and ours is a different
    //   `QueryPerformanceCounter` sample from retail's. `timer.cpp` records
    //   them as calls=0 and UNVERIFIED for exactly that reason
    //   (reference_uncalled_port_is_not_verified.md). `CurrentTimeTicks` has
    //   since grown one caller — `src/engine/engine/loops.cpp:1453-1456`,
    //   `MainLoop::Draw`'s frame timestamp — which makes `timer.cpp`'s own
    //   banner ("no file names CurrentTimeTicks") stale; corrected here rather
    //   than there, because that banner is about the Wave-9 sweep it was true of.
    static float    CurrentTime();
    static int64_t  CurrentTimeTicks();
    // ?SecondsToTicks@Timer@System@@SA_JM@Z   0x1800ca800  — float widened to
    //   double, divided in double, TRUNCATED toward zero.
    static int64_t  SecondsToTicks(float seconds);
    // ?TicksToSeconds@Timer@System@@SAM_J@Z   0x1800ca820  — SIGNED widening,
    //   multiply in double, one rounding to float at the end.
    static float    TicksToSeconds(int64_t ticks);

    // ---- the three PRIVATE statics (`CA`) ----------------------------------
    // PUBLIC here rather than private because their registered doors call them
    // and access control is not ABI — the same call `System::Thread::ThreadFunc`
    // makes one file over. Retail's spelling is recorded above so the fact is
    // not lost.
    // ?PlatformStartup@Timer@System@@CA_NXZ   0x1800ca860
    static bool     PlatformStartup();
    // ?PlatformShutdown@Timer@System@@CAXXZ   0x1800ca8e0 — a bare RET.
    static void     PlatformShutdown();
    // ?GetTicks@Timer@System@@CA_KXZ          0x1800ca840 — a raw QPC sample.
    static uint64_t GetTicks();

    // ---- the one non-static member (`QEAA`) --------------------------------
    // ??4Timer@System@@QEAAAEAV01@AEBV01@@Z   0x180007890
    // The compiler-generated copy assignment: `MOV RAX,RCX; RET`. There is no
    // instance state, so it copies nothing and returns `*this`. Declared
    // because it is a real export with a registered door, not because anything
    // calls it.
    Timer& operator=(const Timer& other);

    // ---- the five statics (`0…A`: PRIVATE STATIC DATA in retail) ----------
    // ENDGAME B.3.9 SYS (B39_RULES D7): class statics, defined in timer.cpp
    // with retail's constant initialisers (Timer::Startup's two stores) or
    // zero (the three PlatformStartup writes). The five minted rows are
    // RETIRED (ENDGAME B.3.9 close): every reader names these members; no
    // name or address door is left. Read every frame on the main thread and
    // by the loader thread: plain statics, no lazy init, no lock. PUBLIC
    // because foreign readers (engine, graphics, UI, audio) read them directly.
    static int64_t _ticksPerSecond;         // ?_ticksPerSecond@Timer@System@@0_JA       0x1801fe720
    static double  _secondsPerTick;         // ?_secondsPerTick@Timer@System@@0NA        0x1801fe728
    static int64_t _systemTicksPerSecond;   // ?_systemTicksPerSecond@Timer@System@@0_JA 0x1801fe730
    static int64_t _systemInitialTick;      // ?_systemInitialTick@Timer@System@@0_JA    0x1801fe738
    static double  _systemSecondsPerTick;   // ?_systemSecondsPerTick@Timer@System@@0NA  0x1801fe740
};

// `elapsed * ticksPerSecond / counterPerSecond`, the engine-tick conversion retail spells
// IMUL / CQO / IDIV in CurrentTimeTicks (0x1800ca8f0), CurrentTime (0x1800ca760) and the
// MainLoop / SleepThread copies of it. NOT RETAIL (review F-151, signed overflow): the
// product passes INT64_MAX after about 10.7 days of a 10 MHz counter, which retail's IMUL
// wraps and C++ leaves undefined. Split at the counter's whole seconds, the quotient is
// the same as retail's for every elapsed count that does not overflow.
inline int64_t CounterToTicks(int64_t elapsed, int64_t ticksPerSecond, int64_t counterPerSecond) {
    const int64_t whole = elapsed / counterPerSecond;
    const int64_t rest = elapsed % counterPerSecond;
    return whole * ticksPerSecond + rest * ticksPerSecond / counterPerSecond;
}

}  // namespace System
