// ============================================================================
// src/engine/engine/engine_lifecycle.cpp — Wave 9 lane ENG
// Engine::Engine ctor / dtor / Startup / Shutdown  (+ 4 helpers they need)
// ============================================================================
//
// These four are the wall cutover boot attempts 04, 05 and 06 all died on:
// with Runtime-x64.dll absent, `src/game/Game/game_main.cpp` resolves all four
// to null and returns 1 or 2 out of `RunGameMain` before the engine ever
// starts. Nothing else in Wave 9 can be measured past that point.
//
//   | Export                              | Retail      | Ord  | This file |
//   |-------------------------------------|-------------|------|-----------|
//   | ??0Engine@0@QEAA@PEAX@Z             | 0x1800dbf00 |  174 | ported    |
//   | ??1Engine@0@QEAA@XZ                 | 0x1800dbec0 |  586 | ported    |
//   | ?Startup@Engine@1@QEAAIPEBDII_N@Z   | 0x1800db8a0 | 4094 | ported    |
//   | ?Shutdown@Engine@1@QEAAXXZ          | 0x1800db4f0 | 4047 | ported    |
//   | ??0Configuration@System@@QEAA@XZ    | 0x1800c51b0 |  113 | ported    |
//   | ??0AchievementManager@Achievement@@… | 0x18009eb00 |   28 | ported    |
//   | ??2Device@Video@@SAPEAX_K@Z         | 0x18000c9b0 |  765 | ported    |
//   | ??3Device@Video@@SAXPEAX@Z          | 0x18000ca30 |  947 | ported    |
//
// Every mangled name in this file — the eight above and all 40-odd
// dependencies — was checked against the export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE the body was
// written. The addresses and ordinals above are that query's output. There are
// no phantom names here.
//
// Kill switch: RT_ENGINELIFE=0.
//
// ---------------------------------------------------------------------------
// 1. THE "ALLOCATOR, NO-GO" HOLDBACK WAS REAL WHEN IT WAS WRITTEN AND IS NOW
//    STALE — AND AT CUTOVER IT HAS INVERTED
// ---------------------------------------------------------------------------
// Wave 8 lane B held all four back with one reason: `System::MemoryAllocator`
// is embedded at Engine offset 0, `Startup` seeds three of its fields and calls
// its init, `Shutdown` calls its shutdown, and WAVE2_ALLOCATOR.md carried a
// NO-GO on the allocator port. Every one of those facts is still true. What has
// changed is the NO-GO, and it changed twice:
//
//   * Wave 9 lanes E/E2/E3 built the shadow oracle the NO-GO demanded and ran
//     it. The allocator port is complete — Allocate, Free, Startup, Shutdown,
//     Construct, Destruct, AddNewMemoryRange, ExpandMemoryRangeTable and the
//     six Platform* members all have bodies in src/engine/system/allocator*.
//   * Lane E4 wired the CUTOVER arming path, and cutover attempt 06 logged
//         [RtAlloc] ARMED (cutover: Runtime-x64.dll absent) -- 27 exports
//         registered, no detour installed ...
//     So with the DLL gone, OUR allocator is the only allocator in the process.
//     "We must not take over the allocator" has become "we already have".
//
// ---------------------------------------------------------------------------
// 2. THE ONE DECISION THIS LANE HAD TO MAKE: DOES THE PORT CREATE THE ARENA?
// ---------------------------------------------------------------------------
// YES. IT MUST. Nothing else does, and the "ARMED" line above does not mean
// what it looks like it means.
//
// `RtSystemAllocatorArm()` (allocator.cpp) installs the platform seam and calls
// the host's `RtAllocSwapArmCutover`, which arms BANISHED_ALLOC_STATS and
// returns. **Neither constructs a MemoryAllocator, and neither reserves one byte
// of arena.** "ARMED" does not mean an arena exists.
//
// The arena is created in exactly one place in the whole process. Lane E2
// proved it two independent ways and printed the proof:
//
//     [RtAllocShadow] Engine::Startup ENTRY (min=512MB max=0MB):
//                     real arena live=0 bytes=0 ranges=0, skipped so far=0
//
//   * a full rel32 scan of Runtime-x64.dll's .text finds ZERO calls to
//     `?Startup@MemoryAllocator@System@@QEAA_N_K0@Z` (0x1800c7620): MSVC
//     inlined it into `Engine::Startup` and nothing else ever calls it;
//   * `rtalloc::Construct` leaves `pageSize` at 0, and `AddNewMemoryRange`
//     divides by `pageSize`, so no allocation CAN precede `Engine::Startup`.
//
// So there is no double-creation hazard to avoid, and the correct behaviour is
// the byte-faithful one: reproduce retail's writes and let them create the
// arena once. Concretely, retail's first four steps
//
//     0x1800db8d9  this->_allocator.minRangeSize = (u64)minMB << 20
//     0x1800db8e4  this->_allocator.maxTotalSize = (u64)maxMB << 20
//     0x1800db8f9  this->_allocator.pageSize     = GetSystemInfo().dwPageSize
//     0x1800db905  CALL 0x1800c7690              ; AddNewMemoryRange(this, 0)
//     0x1800db917  CALL 0x1800c5fa0              ; Error::SetText("Memory: The
//                                                ; application failed to start
//                                                ; the memory allocator!", 0)
//
// ARE `MemoryAllocator::Startup`, verbatim — the same four operations in the
// same order, ending in the same error string (read at 0x180155e48 and
// byte-compared with the literal in `rtalloc::Startup`). This body therefore
// calls `?Startup@MemoryAllocator@System@@QEAA_N_K0@Z` and does not re-derive
// them. That is not a shortcut: it is what keeps the DLL-PRESENT path honest
// too, because with `RT_ALLOC` unset that name resolves to the DLL's
// own 0x1800c7620 and the arena is created by retail's code exactly as before.
//
// The alternative — "arm from Startup and stop arming early" — was considered
// and rejected at the time: the allocator's registration had to run inside the
// boot-time registry init because ~1,180 call sites resolved MemAlloc/MemFree
// before GameMain (WAVE9_LANE_H.md §5). (B3R W2-RES: the registry is gone; every
// caller reaches MemAlloc/MemFree by name.)
//
// ---------------------------------------------------------------------------
// 3. THE .data GLOBALS ARE THE REAL CUTOVER HAZARD, NOT THE ALLOCATOR
// ---------------------------------------------------------------------------
// Retail's four bodies write eleven absolute .data addresses (0x1801fc670,
// 0x1801fc678, 0x1801fc6a8, 0x1801fc6b0, 0x1801fc6c8, 0x1801fc6d0, 0x1801fc6e8,
// 0x1801fc6f8, 0x1801fc709, 0x1801fe720/728, 0x1801fe748, 0x1801fe758,
// 0x1801fe768, 0x1801fe7a0). With the DLL absent every one of those addresses
// is unmapped. A "faithful" port that stored to them would be a wild write to
// nothing on the very first boot.
//
// Ten of them are `?_singleton@…` DATA exports, and MSVC inlined the
// constructor that owns each one straight into `Engine::Startup`. Wave 9 lane X
// ported those constructors as standalone bodies and registered them, and each
// publishes its slot through `RT_DATA_SLOT` — the port-aware door — so the
// store follows lane C2's mint at cutover. This body therefore CALLS the
// constructor by its exported name instead of replaying the inlined stores.
// That is byte-equivalent, checked one at a time: e.g. `??0Configuration@Video@@`
// (0x1800f6f10) is instruction-for-instruction the block at 0x1800dbc2f-c87,
// singleton store included, and the same holds for FileSystem, Modification,
// Factory, ExternalFactory, Application, Audio::Configuration and
// Input::Configuration.
//
// TWO of the inlined constructors are NOT in lane X's fourteen, and this lane
// found that gap: `System::Configuration` (0x1800c51b0) and
// `Achievement::AchievementManager` (0x18009eb00). Both publish a singleton;
// `?_singleton@AchievementManager@Achievement@@0PEAV12@EA` has two LIVE readers
// in our own tree (src/game/Game/gameupdateloop_serialize.cpp:189 and
// eventlog_achievement.cpp), so at cutover the mint would have handed out
// storage that nothing ever wrote and both readers would see null forever. They
// are ported below, as #15 and #16 of lane X's list.
//
// The remaining stores are handled as follows:
//   0x1801fe720 / 0x1801fe728  Timer's two constants + PlatformStartup, which
//                              together ARE `?Startup@Timer@System@@SA_NXZ`
//                              (0x1800ca7c0 writes the same two and tail-jumps
//                              to 0x1800ca860). Called by name; Wave 9 lane B3
//                              owns the storage, so it is ours already.
//   0x1801fe748                `?_singleton@Engine@1@0PEAV11@EA`, ord 4808 —
//                              an exported DATA symbol, reached with
//                              RT_DATA_SLOT like the other ten.
//   0x1801fc709                `?_hideDisplay@Error@System@@0_NA`, ord 4791 —
//                              likewise.
//   0x1801fc6d0                NOT exported, and the ONE address in these four
//                              bodies with no name. It is a plugin HMODULE that
//                              Shutdown FreeLibrary's; see the note at that
//                              call site for the only guard in this file.
//
// ---------------------------------------------------------------------------
// 4. THE ORACLE, HONESTLY
// ---------------------------------------------------------------------------
// There is NO differential oracle for any of the four, and this file will not
// manufacture one. Each runs exactly once per process and each mutates global
// state that a replay would mutate twice — running `Startup` again would create
// a second FileSystem, a second Video::Device and a second 512 MB arena;
// running `Shutdown` again would double-free every subsystem. `the raw DLL door`
// against the shipped body is not a comparison here, it is a second side
// effect.
//
// What they get instead is a SHADOW on post-conditions the body did not compute
// directly, plus a did-work counter, per
// reference_probe_needs_an_effectiveness_control.md. The load-bearing terms are
// the ones written by somebody ELSE's code:
//
//   ctor      `*?_singleton@Engine@1@ == this` (through the exported slot, so a
//             ctor that stamped the wrong global fails), and the allocator
//             sub-object's ctor post-conditions minBin==0xbf / pageSize==0 /
//             ranges==null — all three written by rtalloc::Construct, not here.
//   Startup   `_startupComplete == 1`, `rangeCount >= 1` and
//             `totalReserved >= minRangeSize` (written by AddNewMemoryRange),
//             `minRangeSize == (u64)minMB << 20` (the one shift that could be
//             wrong), and all sixteen subsystem pointers non-null (written by
//             the sixteen constructors we called, not by us).
//   dtor      both singleton slots null.
//   Shutdown  every subsystem pointer that was non-null on entry has had its
//             teardown run, counted; and `ranges == nullptr` is NOT asserted
//             because `MemoryAllocator::Shutdown` releases the ranges without
//             nulling the pointer (allocator.cpp) — asserting it would be the
//             reference_oracle_must_not_compare_unwritten_bytes.md mistake.
//
// A shadow is weaker than a byte diff and this file says so plainly. Two
// further limits, stated rather than buried:
//
//   * `Engine::Shutdown`'s `calls=` is ZERO and will stay zero. game_main.cpp
//     resolves `?Shutdown@Engine@1@QEAAXXZ` only to satisfy its own null guard
//     (line 426) and then calls its own `ShutdownEngineStaged()` instead (line
//     924). Nothing in this project calls the real one. It is ported because
//     the guard demands a non-null pointer and because leaving it null is what
//     was blocking the boot — NOT because it is verified. It is not.
//   * Arming this lane DISABLES the allocator shadow driver. `RtAllocShadowInstall`
//     detours the DLL's `Engine::Startup` at 0x1800db8a0 to arm the replica; our
//     port never executes those bytes, so the detour never fires and the driver
//     reports `skipped_pre_startup=N`, i.e. INVALID. Any run of
//     `RT_ALLOC_SHADOW=1` must also set `RT_ENGINELIFE=0`.
// ============================================================================

#include <new>   // B7 LX: placement-construct System::Configuration

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "engine/Engine.h"
#include "application/Application.h"   // ENDGAME B.3.7: PlatformStartup / PlatformShutdown
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 fragment: the ctor door's named fields
#include "input/Input.h"    // ENDGAME B.3.7 callers (K5): Input::Configuration
#include "input/Device.h"   // ENDGAME B.3.7 callers (K5): Input::Device
#include "video/Device.h"      // ENDGAME B.3.5: Video::Device / Configuration
#include "audio/Audio.h"       // ENDGAME B.3.7 callers (K4): Audio::Configuration / Device
#include "audio/Stream.h"      // ENDGAME B.3.9 ENG W2: ~Stream in Shutdown
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "system/Error.h"      // ENDGAME B.3.9 SYS: Error::_hideDisplay
#include "system/Configuration.h"   // ENDGAME B.3.9 SYS: Configuration::_singleton
#include "object/TypeInfo.h"        // ENDGAME B.3.9 SS1: Object::Factory::_compilerModule
#include "io/FileSystem.h"          // B3R: the members Startup / Shutdown call by name
#include "io/Persistence.h"
#include "io/Modification.h"
#include "system/Timer.h"


// FreeLibrary is Platform::ReleaseLibrary (the gfx seam, Phase D.4 SDL-5; was
// RtEngineLifecyclePlatformFreeLibrary in src/platform/win/engine_lifecycle_win.cpp). The
// two COM calls (Platform::ComInit / ComUninit since THREADS E.7) are gone since SDL-5 D2.
#include "platform_gfx.h"
#include "load_time.h"     // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

// singleton_ctors_lock.cpp (Wave 9 lane X). `??0MemoryAllocator@System@@` routes
// to `rtalloc::Construct`, whose first act is `g_plat.lockInit(self->lock)`;
// `g_plat` is filled by `RtSystemAllocatorInstallPlatform()` and on a run with
// `RT_ALLOC` unset NOBODY calls it, so that would be an EXEC of 0.
// This lane checks the seam at REGISTRATION time, not in the body.
bool RtLaneXEnsureAllocatorPlatform();

// ENDGAME B.3.7: `Engine::Engine` is the real class; a global
// `using RtEngine::Engine;` is ill-formed next to `namespace Engine`.
using EngineT = ::Engine::Engine;
// NOT `using rt::system::MemoryAllocator;` -- src/engine/common.h
// forward-declares a GLOBAL `struct MemoryAllocator`, and the using-declaration
// collides with it. Spelled out at each use instead.
typedef rt::system::MemoryAllocator RtMemoryAllocator;

// The unexported Input::KeyMap scalar deleting destructor 0x180053440, ported
// and registered by address in src/engine/input/keymap_save.cpp (block B5).
Input::KeyMap* RtInput_KeyMap_DeletingDtor(Input::KeyMap* self);

namespace {

// ---------------------------------------------------------------------------
// Dependencies. B3R: every one is called by name -- a member of the real class,
// or the body (rtengine::Ctor_* in singleton_ctors.cpp, the ??1 bodies that
// are free functions) -- where these bodies used to go through RT_EXPORT_ONCE.
// ---------------------------------------------------------------------------
// The two allocator statics every `new`/`delete` in these bodies goes through.
// Retail's unexported per-class `operator new` at 0x180011640 is
// `MemAlloc(size, 4)` and nothing else — 20 instructions, read in full; the
// exported `??2Device@Video@@SAPEAX_K@Z` (0x18000c9b0) is the identical body
// with `R8D = 8` instead of `4`, and `??3Device@Video@@` (0x18000ca30) and the
// unexported delete at 0x180006a10 are both `MemFree` verbatim.
void* MemAlloc(uint64_t size, int align) {
    return System::MemoryAllocator::MemAlloc(size, align);   // 0x1800c7a50
}
void MemFree(void* p) {
    System::MemoryAllocator::MemFree(p);                     // 0x1800c7ad0
}

// Effectiveness counters. Read by RtEngineLifecycleSummary(); the four bodies
// run once each, so "did it run at all" is the only coverage question there is.
unsigned g_ctorRuns     = 0;
unsigned g_startupRuns  = 0;
unsigned g_dtorRuns     = 0;
unsigned g_shutdownRuns = 0;
unsigned g_shutdownSteps = 0;   // subsystems actually torn down
unsigned g_startupFailAt = 0;   // 0 == reached the end

}  // namespace

// ??0Configuration@System@@QEAA@XZ (retail 0x1800c51b0): B7 LX deleted the
// `RtSystem_Configuration_Ctor` door that stood here; the body is the class's
// real constructor (system/Configuration.h, header-defined because Startup
// inlines it at 0x1800db938..0x1800db954).

// ============================================================================
// ??0AchievementManager@Achievement@@QEAA@XZ               retail 0x18009eb00
// ============================================================================
// Lane X's #16, and the one that matters most of the two: two files under
// src/game/ read `?_singleton@AchievementManager@Achievement@@0PEAV12@EA` at
// runtime, so at cutover an unwritten mint row is a null in live game code.
//
// Eight qwords and a dword, then the singleton. Identical to the inlined block
// at 0x1800dbe55-0x1800dbe83.
//
// ENDGAME B.3.7 fragment: the stores name the class's fields (same bytes:
// +0x00..+0x3f and the dword at +0x40; `_pad44` stays untouched, as retail
// leaves it). The door stays a door (B36_RULES 7 / B7-13).
extern "C" Achievement::AchievementManager*
RtAchievement_AchievementManager_Ctor(Achievement::AchievementManager* self) {
    Achievement::AchievementManager* const m = self;
    m->_achievements          = nullptr;   // +0x00
    m->_completed._data       = nullptr;   // +0x08
    m->_completed._size       = 0;         // +0x10
    m->_completed._capacity   = 0;         // +0x14
    m->_instances._data       = nullptr;   // +0x18
    m->_instances._size       = 0;         // +0x20
    m->_instances._capacity   = 0;         // +0x24
    m->_eventHandler          = nullptr;   // +0x28
    m->_updateTimes._data     = nullptr;   // +0x30
    m->_updateTimes._size     = 0;         // +0x38
    m->_updateTimes._capacity = 0;         // +0x3c
    m->_lastAchievementUpdate = 0;         // +0x40
    Achievement::AchievementManager::_singleton = m;   // class static (B.3.9 ENG)
    return self;
}

// ============================================================================
// ??2Device@Video@@SAPEAX_K@Z / ??3Device@Video@@SAXPEAX@Z
//                                        retail 0x18000c9b0 / 0x18000ca30
// ============================================================================
// Video::Device's class `operator new` / `operator delete`. Both were read in
// full: each takes `MemoryAllocator::_singleton`'s critical section at +0x640,
// calls `Allocate(size, 8)` / `Free(p)`, and releases it — which is exactly
// `MemAlloc(size, 8)` / `MemFree(p)`. `operator delete` keeps retail's leading
// `TEST RCX,RCX; JZ ret`, which `MemFree` also has.
extern "C" void* RtVideo_Device_OperatorNew(uint64_t size) {
    return MemAlloc(size, 8);
}
extern "C" void RtVideo_Device_OperatorDelete(void* p) {
    MemFree(p);
}

// ============================================================================
// ??0Engine@0@QEAA@PEAX@Z                                  retail 0x1800dbf00
// ============================================================================
// Retail, instruction for instruction:
//
//   CALL 0x1800c79a0                    ; MemoryAllocator::MemoryAllocator(this)
//   XOR EAX,EAX
//   [RDI+0x670 .. +0x6e8] = 0           ; fifteen qwords, skipping +0x698
//   [RDI+0x698] = RBX                   ; = the platform handle argument
//   MOV word ptr [RDI+0x6f0],AX         ; _startupComplete AND _noConfigure, as
//                                       ; ONE 16-bit store
//   MOV [0x1801fe748],RDI               ; Engine::_singleton = this
//   MOV RAX,RDI                         ; return this
//
// Note the allocator ctor runs FIRST and on the unmodified `this` — that is the
// evidence in Engine.h that the allocator sub-object is at offset 0, and it is
// why the ctor may not be reordered: `rtalloc::Construct` initialises the
// critical section every later allocation takes.
//
// `_platformHandle` is stored between the zeroed fields, from RBX, which was
// loaded from RDX before the call. Writing it in field order below matches.
// ENDGAME B7 (CTOR, D5): the class's REAL ctor (the `RtEngine_Engine_Ctor` door
// whose body this was is deleted). No mem-initialiser: every field is a pointer
// or a byte with no initialiser, so the allocator ctor in the body is still the
// first thing that runs on `this`, as at 0x1800dbf00.
Engine::Engine::Engine(void* platformHandle) {

    rtengine::Ctor_MemoryAllocator(&_allocator);            // 0x1800c79a0

    _fileSystem         = nullptr;
    _persistence        = nullptr;
    _modification       = nullptr;
    _configuration      = nullptr;
    _application        = nullptr;
    _platformHandle     = platformHandle;
    _factory            = nullptr;
    _internalFactory    = nullptr;
    _externalFactory    = nullptr;
    _videoConfiguration = nullptr;
    _videoDevice        = nullptr;
    _audioConfiguration = nullptr;
    _audioDevice        = nullptr;
    _inputConfiguration = nullptr;
    _inputDevice        = nullptr;
    _achievementManager = nullptr;
    _startupComplete    = 0;
    _noConfigure        = 0;
    // +0x6f2..+0x6f7: retail's 16-bit store at +0x6f0 does not reach them and
    // neither do we.

    ::Engine::Engine::_singleton = this;                    // MOV [0x1801fe748],RDI

    ++g_ctorRuns;
}

// ============================================================================
// ??1Engine@0@QEAA@XZ                                      retail 0x1800dbec0
// ============================================================================
// The whole function:
//
//   XOR EAX,EAX
//   MOV [0x1801fe748],RAX     ; Engine::_singleton         = null
//   MOV [0x1801fc718],RAX     ; MemoryAllocator::_singleton = null
//   ADD RCX,0x640
//   JMP DeleteCriticalSection ; the allocator's lock, in place
//
// The last two lines are `MemoryAllocator::~MemoryAllocator` (0x1800c7960)
// inlined: that body is `MOV [0x1801fc718],0` + the same tail jump, and
// `rtalloc::Destruct` is its port. Calling the exported dtor rather than
// replaying the two stores is what routes the singleton null through the
// port-aware slot; the ORDER is preserved (Engine's global first, then the
// allocator's), which matters only in that nothing observes between them.
//
// The Engine object itself is NOT freed here: game_main.cpp holds it on the
// stack, exactly as retail's caller did.
// ENDGAME B7 (CTOR, D5): the class's REAL dtor (the `RtEngine_Engine_Dtor` door is
// deleted). Every member is trivially destructible, so nothing runs after it.
Engine::Engine::~Engine() {

    ::Engine::Engine::_singleton = nullptr;                 // MOV [0x1801fe748],0

    rtengine::Dtor_MemoryAllocator(&_allocator);            // 0x1800c7960

    ++g_dtorRuns;
}

// ============================================================================
// ?Startup@Engine@1@QEAAIPEBDII_N@Z                        retail 0x1800db8a0
// ============================================================================
// 367 instructions, and the shape is one pattern repeated sixteen times:
//
//     void* p = operator new(SIZE);        // == MemAlloc(SIZE, 4)
//     if (p) <constructor, INLINED>;       // else p stays null
//     this-><field> = p;                   // the null is STORED, not rejected
//     if (!<Subsystem>::Startup(p)) return 0;
//
// Two faithfulness points the assembly makes that a tidy rewrite would lose:
//
//   * The allocation failure branch stores NULL into the Engine field and then
//     calls the subsystem's Startup ON THE NULL (0x1800dba7e/0x1800dba85 for
//     Persistence is the clearest). Retail does not check. Neither do we —
//     CLAUDE.md's "don't fix a faithful crash with a null check" applies
//     exactly here, and in practice MemAlloc on a fresh 512 MB arena does not
//     fail.
//   * Every failure after the allocator's own jumps to 0x1800db91c, which is
//     `XOR EAX,EAX` and NOTHING ELSE — no Error::SetText, no cleanup, no
//     Shutdown. Only the allocator failure sets the error text. So a failed
//     Startup leaves a half-built Engine that the caller must still destroy,
//     and `_startupComplete` stays 0, which is what arms the launcher dialog
//     on the next run (Engine.h, CLAUDE.md "Launcher Dialog").
//
// The `unsigned` return is 1 on success and 0 on every failure.
uint32_t Engine::Engine::Startup(const char* cmdLine, uint32_t minRangeMB,
                                uint32_t maxTotalMB, bool noConfigure) {

    ++g_startupRuns;
    LoadTime::Stamp("engine.startup");   // NOT RETAIL: print-only
    g_startupFailAt = 0;

    // 0x1800db8c5-0x1800db8cf. The bool argument arrives on the stack (5th).
    _noConfigure     = noConfigure ? 1 : 0;
    _startupComplete = 0;

    // --- 0x1800db8d6 .. 0x1800db91e: MemoryAllocator::Startup, inlined -------
    // See §2 of the banner. This is the only place in the process an arena is
    // created, and this call is retail's four operations plus retail's error
    // string, not a substitute for them.
    if (!_allocator.Startup(static_cast<uint64_t>(minRangeMB),        // 0x1800c7620
                            static_cast<uint64_t>(maxTotalMB))) {
        g_startupFailAt = 1;
        return 0;
    }

    // --- 0x1800db923: System::Configuration ---------------------------------
    System::Configuration* configuration = static_cast<System::Configuration*>(MemAlloc(0x30, 4));
    if (configuration) ::new (configuration) System::Configuration();   // 0x1800db938..0x1800db954, ??0Configuration inlined
    _configuration = configuration;

    // --- 0x1800db967: Configuration::SetCommandLine(String(cmdLine)) --------
    // A temporary System::String, which is one pointer. Retail zeroes it,
    // calls String::Set, passes it by reference and then inlines ~String
    // (0x180006c80). ENDGAME B.3.1 step 2: the zero-then-Set pair IS the
    // `const char*` constructor (0x18000b630) and the inlined release IS the
    // destructor, so the whole block is one declaration and its scope.
    {
        System::String tmp(cmdLine);
        _configuration->SetCommandLine(tmp);                        // 0x1800c47a0
    }

    // --- 0x1800db9aa .. 0x1800db9d9: System::Timer::Startup, inlined --------
    // Retail writes `_ticksPerSecond = 1000000` and
    // `_secondsPerTick = *0x1801616d0 / (double)1000000` and then CALLs
    // Timer::PlatformStartup. `?Startup@Timer@System@@SA_NXZ` (0x1800ca7c0) is
    // those same two stores plus a tail jump to the same PlatformStartup, so
    // the call below is the inlined block, not an approximation. Wave 9 lane B3
    // owns the two constants' storage, which is also why ours is never the zero
    // the DLL's slot holds before this point.
    if (!System::Timer::Startup()) {                                 // 0x1800ca7c0
        g_startupFailAt = 2;
        return 0;
    }

    // --- 0x1800db9df: IO::FileSystem ----------------------------------------
    IO::FileSystem* fileSystem = static_cast<IO::FileSystem*>(MemAlloc(0x50, 4));
    if (fileSystem)
        rtengine::Ctor_FileSystem(fileSystem);                      // 0x1800ac7c0
    _fileSystem = fileSystem;
    LoadTime::Stamp("engine.filesystem");   // NOT RETAIL: print-only (package mount #1)
    if (!fileSystem->Startup()) {                                   // 0x1800abc50
        g_startupFailAt = 3;
        return 0;
    }

    // --- 0x1800dba5e: IO::Persistence ---------------------------------------
    IO::Persistence* persistence = static_cast<IO::Persistence*>(MemAlloc(0x98, 4));
    if (persistence)
        new (persistence) IO::Persistence();                        // 0x1800b8020
    _persistence = persistence;
    if (!persistence->Startup()) {
        g_startupFailAt = 4;
        return 0;
    }

    // --- 0x1800dba95: IO::Modification --------------------------------------
    // RefreshList returns void and its result is not tested; a failure here
    // cannot stop Startup, by retail's design.
    IO::Modification* modification = static_cast<IO::Modification*>(MemAlloc(0x28, 4));
    if (modification)
        rtengine::Ctor_Modification(modification);                  // 0x1800b04b0
    _modification = modification;
    LoadTime::Stamp("engine.mods");   // NOT RETAIL: print-only
    modification->RefreshList();

    // --- 0x1800dbae0: Object::Factory ---------------------------------------
    ::Object::Factory* factory = static_cast< ::Object::Factory*>(MemAlloc(0x10, 4));
    if (factory)
        rtengine::Ctor_Factory(factory);                            // 0x1800c09d0
    _factory = factory;
    LoadTime::Stamp("engine.factory");   // NOT RETAIL: print-only
    if (!factory->Startup()) {                                      // 0x1800c0630
        g_startupFailAt = 5;
        return 0;
    }

    // --- 0x1800dbb20: Object::InternalFactory (no Startup) -------------------
    ::Object::InternalFactory* internalFactory =
        static_cast< ::Object::InternalFactory*>(MemAlloc(0x9e8, 4));
    if (internalFactory)
        rtengine::Ctor_InternalFactory(internalFactory);            // 0x1800c0030
    _internalFactory = internalFactory;

    // --- 0x1800dbb47: Object::ExternalFactory (no Startup) -------------------
    ::Object::ExternalFactory* externalFactory =
        static_cast< ::Object::ExternalFactory*>(MemAlloc(0x48, 4));
    if (externalFactory)
        rtengine::Ctor_ExternalFactory(externalFactory);            // 0x1800be7e0
    _externalFactory = externalFactory;

    // --- 0x1800dbbb3: Engine::Application ------------------------------------
    // The ctor takes the platform handle, read back out of the Engine field at
    // 0x1800dbbc9 rather than from the register — so an Engine whose +0x698 was
    // changed since the ctor would see the new value. Kept.
    ::Application::Application* application =
        static_cast< ::Application::Application*>(MemAlloc(0x30, 4));
    if (application)
        rtengine::Ctor_Application(application, _platformHandle);   // 0x18009f590
    _application = application;
    // ENDGAME B.3.7: `?PlatformStartup@Application@1@AEAA_NXZ` was a name
    // door; a member call on the real class now (the ctor above is the
    // `Ctor_Application` body, B7-10).
    LoadTime::Stamp("engine.window");   // NOT RETAIL: print-only
    if (!application->PlatformStartup()) {
        g_startupFailAt = 6;
        return 0;
    }

    // --- 0x1800dbc12: CoInitializeEx(nullptr, COINIT_MULTITHREADED) ---------
    //     0x1800dbc10  XOR EDX,EDX ; XOR ECX,ECX
    //     0x1800dbc16  CALL qword ptr [0x18014b618]   ; ole32!CoInitializeEx, result discarded
    // NOT MADE since Phase D.4 SDL-5 D2 (`behaviour:`). Retail's MTA init served its COM
    // users: XAudio2's CoCreateInstance, DirectInput8Create and ShellExecuteW. This exe has
    // none left (audio is SDL's since SDL-4, input SDL's since SDL-C, ShellExecuteW is
    // SDL_OpenURL's, which initialises COM around it itself), and D3D11 / DXGI need no COM
    // init. So no caller in the exe can observe the apartment; SDL's video init now sets
    // the main thread's (src/app/main.cpp says how). known_port_issues row 1279.

    // --- 0x1800dbc1c: Video::Configuration ----------------------------------
    Video::Configuration* videoConfiguration =
        static_cast<Video::Configuration*>(MemAlloc(0x44, 4));
    if (videoConfiguration)
        rtengine::Ctor_VideoConfiguration(videoConfiguration);      // 0x1800f6f10
    _videoConfiguration = videoConfiguration;
    if (!videoConfiguration->Startup()) {
        g_startupFailAt = 7;
        return 0;
    }

    // --- 0x1800dbcaa: the two _noConfigure stores into Video::Configuration --
    // Both re-READ `this->_videoConfiguration` (0x1800dbcb3, 0x1800dbcc5) and
    // dereference it unguarded. If the allocation above failed this faults, in
    // retail exactly as here.
    if (_noConfigure)
        _videoConfiguration->fullscreen = 0;                        // +0x40
    _videoConfiguration->hideWindow = _noConfigure;                    // +0x42

    // --- 0x1800dbccf: Video::Device -----------------------------------------
    // Allocated through Video::Device's own class operator new (align 8, not 4)
    // and constructed with DeviceType 0. Lane X's negative control proved the
    // ctor's CMOVE publishes `?_singleton@Device@Video@@` only for type 0, so
    // this argument is load-bearing.
    // EnumerateVideoModes returns bool and retail does NOT test it.
    // LEFT: `??2Device@Video@@` (0x18000c9b0) is not declared in Video::Device
    // (R-VIDEO's header); its body is RtVideo_Device_OperatorNew.
    Video::Device* videoDevice = static_cast<Video::Device*>(RtVideo_Device_OperatorNew(0x108));   // LEFT: the class operator-new door returns void* (an allocation result castmetric does not recognise: ledger close)
    if (videoDevice)
        rtengine::Ctor_VideoDevice(videoDevice, 0);                 // 0x1800f7160
    _videoDevice = videoDevice;
    LoadTime::Stamp("engine.videomodes");   // NOT RETAIL: print-only
    (void)videoDevice->EnumerateVideoModes();

    // --- 0x1800dbd00: Audio::Configuration ----------------------------------
    Audio::Configuration* audioConfiguration =
        static_cast<Audio::Configuration*>(MemAlloc(0x10, 4));
    if (audioConfiguration)
        rtengine::Ctor_AudioConfiguration(audioConfiguration);      // 0x1800ddeb0
    _audioConfiguration = audioConfiguration;
    if (!audioConfiguration->Startup()) {
        g_startupFailAt = 8;
        return 0;
    }

    // --- 0x1800dbd51: Input::Configuration ----------------------------------
    ::Input::Configuration* inputConfiguration =
        static_cast< ::Input::Configuration*>(MemAlloc(0x10, 4));
    if (inputConfiguration)
        rtengine::Ctor_InputConfiguration(inputConfiguration);      // 0x1800e0de0
    _inputConfiguration = inputConfiguration;
    if (!inputConfiguration->Startup()) {
        g_startupFailAt = 9;
        return 0;
    }

    // --- 0x1800dbd94: the launcher dialog ------------------------------------
    // `PlatformConfigure(this, true, this->_platformHandle)`. On refusal retail
    // sets `?_hideDisplay@Error@System@@0_NA` (the player pressed Quit, so no
    // error box) and returns 0. CLAUDE.md's "Launcher Dialog" section and
    // src/app/main.cpp's HarnessGuardUnattendedBoot both depend on this
    // branch being reached exactly when `_noConfigure` is false; it is.
    if (!_noConfigure) {
        // ENDGAME B.3.7: a member call (was the name door).
        if (!PlatformConfigure(true, _platformHandle)) {
            System::Error::_hideDisplay = true;   // class static (B.3.9 SYS)
            g_startupFailAt = 10;
            return 0;
        }
    }

    // --- 0x1800dbdc0: Video::Device::PlatformStartup ------------------------
    LoadTime::Stamp("engine.device");   // NOT RETAIL: print-only
    if (!_videoDevice->PlatformStartup()) {
        g_startupFailAt = 11;
        return 0;
    }

    // --- 0x1800dbdd4: Audio::Device -----------------------------------------
    Audio::Device* audioDevice = static_cast<Audio::Device*>(MemAlloc(0x1298, 4));
    if (audioDevice)
        rtengine::Ctor_AudioDevice(audioDevice);                    // 0x1800de3a0
    _audioDevice = audioDevice;
    LoadTime::Stamp("engine.audio");   // NOT RETAIL: print-only
    if (!audioDevice->PlatformStartup()) {
        g_startupFailAt = 12;
        return 0;
    }

    // --- 0x1800dbe0b: Input::Device -----------------------------------------
    ::Input::Device* inputDevice = static_cast< ::Input::Device*>(MemAlloc(0x1200, 4));
    if (inputDevice)
        rtengine::Ctor_InputDevice(inputDevice);                    // 0x1800e13b0
    _inputDevice = inputDevice;
    if (!inputDevice->PlatformStartup()) {
        g_startupFailAt = 13;
        return 0;
    }

    // --- 0x1800dbe42: Achievement::AchievementManager (no Startup) ----------
    Achievement::AchievementManager* achievementManager =
        static_cast<Achievement::AchievementManager*>(MemAlloc(0x48, 4));
    if (achievementManager)
        RtAchievement_AchievementManager_Ctor(achievementManager);
    _achievementManager = achievementManager;

    // --- 0x1800dbe96 -------------------------------------------------------
    LoadTime::Stamp("engine.startup.done");   // NOT RETAIL: print-only
    _startupComplete = 1;


    return 1;
}

// ============================================================================
// ?Shutdown@Engine@1@QEAAXXZ                               retail 0x1800db4f0
// ============================================================================
// *** THIS BODY HAS calls=0 AND IS NOT VERIFIED. READ §4 OF THE BANNER. ***
// game_main.cpp resolves the name only to satisfy its own null guard and then
// runs its own `ShutdownEngineStaged()` instead. Nothing in this project calls
// the real one, so no run this wave can exercise a single line below. It is
// ported because a null here is what returned 2 out of `RunGameMain` on cutover
// attempts 04-06, and because a written body makes the remaining gap legible.
//
// The order is retail's and it is not arbitrary: EVERY subsystem is asked to
// SAVE its state (phase 1) before ANY of them is destroyed (phase 2). Interleave
// them and Video::Configuration::Save would run after the Video::Device that
// owns the numbers it persists is gone.
//
// Ten of retail's calls here are unexported inlined destructors. Each was
// matched to the exported destructor that consists of exactly those calls —
// e.g. `??1Factory@Object@@` (0x1800071a0) is `JMP 0x180008110` and nothing
// else, `??1AchievementManager@Achievement@@` (0x18009eab0) is the null store
// at 0x1801fc670 plus the same three calls in the same order. Using the
// exported name is byte-equivalent AND port-aware; using the raw address would
// be dead memory at cutover. The one place that was NOT reducible to an export
// is flagged inline.
void Engine::Engine::Shutdown() {

    ++g_shutdownRuns;
    unsigned steps = 0;

    // ---- phase 1: persist ---------------------------------------------------

    // 0x1800db50f. The launcher-dialog arming bit: byte 0 of Video::Configuration
    // becomes `_startupComplete == 0`, and Video::Configuration::Save (below)
    // stores it for WriteSettings (settings.cfg; retail: the registry).
    // Engine.h documents the whole chain.
    if (Video::Configuration* videoCfg = _videoConfiguration)
        videoCfg->settingsValid = (_startupComplete == 0) ? 1 : 0;

    if (::Achievement::AchievementManager* p = _achievementManager) {
        p->RemoveAll(); ++steps;
    }
    if (::Input::Device* p = _inputDevice) {
        p->PlatformShutdown(); ++steps;
    }
    if (::Input::Configuration* p = _inputConfiguration) {
        p->Save();
        // 0x1800db55d: destroy the object hanging off the config's first field.
        // 0x180053440 is an unnamed scalar deleting destructor (a two-node BST
        // teardown plus MemFree) with NO export in either build, so it can only
        // be reached by address. ENDGAME B.3.7 callers (K5): that address door
        // resolved (registry-first) to `RtInput_KeyMap_DeletingDtor`, ported in
        // src/engine/input/keymap_save.cpp and registered there BY ADDRESS, so
        // it is a direct call now — the same conversion block B5 made in
        // Input::Configuration::Startup's re-entry arm.
        if (::Input::KeyMap* inner = p->_keyMap) {
            RtInput_KeyMap_DeletingDtor(inner);
        }
        ++steps;
    }
    if (Audio::Device* dev = _audioDevice) {
        // 0x1800db576: the mastering voice's DestroyVoice (+0x90) and the
        // engine's Release (+0x10), each followed by an unconditional null
        // store -- Audio::Device::PlatformShutdown (0x1800de690) inlined,
        // instruction for instruction. Phase D.4 A1 calls it.
        dev->PlatformShutdown();
        ++steps;
    }
    if (Audio::Configuration* p = _audioConfiguration) {
        p->Save(); ++steps;
    }
    if (Video::Device* p = _videoDevice) {
        p->PlatformShutdown();
        // 0x1800db5cd: the three 0x18-byte plugin slots at device+0xb0;
        // FreeLibrary each non-null module and clear all three qwords of every
        // slot, whether or not it held one.
        for (int i = 0; i < 3; ++i) {
            Video::Device::InterfaceSlot& slot = p->dllSlots[i];
            if (void* mod = slot.module)
                Platform::ReleaseLibrary(mod);                // 0x1800db5e8
            slot.module           = nullptr;
            slot.getDeviceName    = nullptr;
            slot.createDeviceData = nullptr;
        }
        ++steps;
    }
    if (Video::Configuration* p = _videoConfiguration) {
        p->Save(); ++steps;
    }
    if (::Application::Application* p = _application) {
        p->PlatformShutdown(); ++steps;
    }
    if (_factory != nullptr) {
        // 0x1800db62d: FreeLibrary on a module held in an UNEXPORTED global at
        // 0x1801fc6d0 — the only address in these four bodies with no name in
        // either build. ENDGAME B.3.9 SS1 (B39_RULES D7): the cell is
        // `Object::Factory::_compilerModule` now, a class static that always
        // exists (the "standalone has no such variable at all" gap the old
        // comment described is closed); the value stays null unless something
        // in this tree ever ports LoadCompilers, so `*slot` is unguarded here
        // exactly as retail's own deref of its always-live global is.
        if (::Object::Factory::_compilerModule)
            Platform::ReleaseLibrary(::Object::Factory::_compilerModule);   // 0x1800db639
        ::Object::Factory::_compilerModule = nullptr;
        ++steps;
    }
    if (IO::Modification* p = _modification) {
        p->SaveList(); ++steps;
    }
    if (_persistence != nullptr && _noConfigure == 0) {
        _persistence->WriteSettings(); ++steps;
    }
    if (IO::FileSystem* p = _fileSystem) {
        p->ClosePackages(); ++steps;
    }

    // ---- phase 2: destroy and free -----------------------------------------
    // Reverse-ish of construction, and NOT the exact reverse — retail frees
    // Achievement, Input::Device, Input::Configuration, Audio::Device,
    // Audio::Configuration, Video::Device, Video::Configuration, Application,
    // System::Configuration, Factory, InternalFactory, ExternalFactory,
    // Modification, Persistence, FileSystem, in that order. Kept as written.

    if (::Achievement::AchievementManager* p = _achievementManager) {
        // ~AchievementManager is a REAL non-virtual member (the null store at
        // 0x1801fc670, then the three Array dtors); the door was its export.
        p->Achievement::AchievementManager::~AchievementManager();
        MemFree(p);
    }
    if (::Input::Device* p = _inputDevice) {
        // 0x1800db6c4: retail calls NO destructor here — just the free. Input::
        // Device's dtor exists (??1Device@Input@@, 0x1800e1480) and Shutdown
        // does not call it. Faithful, and worth flagging as a retail leak
        // rather than a transcription slip.
        MemFree(p);
    }
    if (::Input::Configuration* p = _inputConfiguration) {
        RtInput_Configuration_Dtor(p);                      // 0x1800e0e10
        MemFree(p);
    }
    if (Audio::Device* p = _audioDevice) {
        // 0x1800db6fe: only the Audio::Stream sub-object at +0xa00 is destroyed;
        // ??1Device@Audio@@ (0x1800de4f0) is NOT called. Faithful.
        // ENDGAME B.3.9 ENG W2: the class's real (public, QEAA) ~Stream.
        p->_stream.~Stream();
        MemFree(p);
    }
    if (Audio::Configuration* p = _audioConfiguration) {
        // ~Configuration is a REAL non-virtual member (audio.cpp: the null store
        // to _singleton); ??1Configuration@Audio@@ is its door. B.3.7 close.
        p->Audio::Configuration::~Configuration();
        MemFree(p);
    }
    if (Video::Device* p = _videoDevice) {
        Video_Device_Dtor(p);                               // 0x1800f7290
        RtVideo_Device_OperatorDelete(p);     // ??3Device@Video@@, == MemFree
    }
    if (Video::Configuration* p = _videoConfiguration) {
        Video_Configuration_Dtor(p);                        // 0x1800f6f80
        MemFree(p);
    }
    if (::Application::Application* p = _application) {
        // 0x18004da00 is ~Application immediately followed by MemFree(this) —
        // a scalar deleting destructor. Split into its two halves here. The
        // exported ??1Application@0@ writes the two adjacent zeroes in the
        // opposite order to the inlined copy; both are zero stores to fields
        // nothing reads between them.
        RtApp_Dtor(p);                                      // 0x18009f520
        MemFree(p);
    }
    if (System::Configuration* p = _configuration) {
        p->~Configuration();                                // 0x1800c5170
        MemFree(p);
    }
    if (::Object::Factory* p = _factory) {
        RtObjFactoryDtor::RtObject_Factory_Dtor(p);                           // 0x1800071a0
        MemFree(p);
    }
    if (::Object::InternalFactory* p = _internalFactory) {
        RtObjFactoryDtor::RtObject_InternalFactory_Dtor(p);                   // 0x18002f860
        MemFree(p);
    }
    if (::Object::ExternalFactory* p = _externalFactory) {
        RtObjFactoryDtor::RtObject_ExternalFactory_Dtor(p);                   // 0x1800be760
        MemFree(p);
    }
    if (IO::Modification* p = _modification) {
        RtIo::RtIo_Modification_Dtor(p);                                      // 0x1800b0470
        MemFree(p);
    }
    if (IO::Persistence* p = _persistence) {
        p->~Persistence();                                  // 0x1800b7fb0
        MemFree(p);
    }
    if (IO::FileSystem* p = _fileSystem) {
        p->~FileSystem();                                   // 0x1800ac6d0
        MemFree(p);
    }

    // 0x1800db877: release every arena range. NOTE it does NOT null
    // `_allocator.ranges` — allocator.cpp's Shutdown releases the table and
    // leaves the pointer dangling, exactly as retail does, so the shadow below
    // must not assert on it.
    _allocator.Shutdown();                                  // 0x1800c75b0

    // 0x1800db88b: the tail jump, `JMP qword ptr [0x18014b610]` (ole32!CoUninitialize).
    // Not made since SDL-5 D2: it balanced the CoInitializeEx above, which is gone.

    g_shutdownSteps = steps;

}

// ============================================================================
// Summary — the effectiveness control
// ============================================================================
// Each of the four runs at most once per process, so `divergences=0` from the
// diff harness says almost nothing on its own: what matters is whether the body
// executed at all. Printed beside .
extern "C" void RtEngineLifecycleSummary() {
    if (g_ctorRuns == 0 && g_startupRuns == 0 &&
        g_dtorRuns == 0 && g_shutdownRuns == 0) return;
    printf("[RtEngineLife] ctor=%u startup=%u (failAt=%u) dtor=%u "
                "shutdown=%u (phase1 steps=%u)\n",
                g_ctorRuns, g_startupRuns, g_startupFailAt, g_dtorRuns,
                g_shutdownRuns, g_shutdownSteps);
    if (g_startupRuns && g_startupFailAt)
        printf("[RtEngineLife] Engine::Startup FAILED at step %u -- the engine "
                    "did not start.\n", g_startupFailAt);
    if (g_shutdownRuns == 0)
        printf("[RtEngineLife] Engine::Shutdown ran 0 times -- STRUCTURAL, not a "
                    "regression: game_main.cpp calls ShutdownEngineStaged() instead. "
                    "That port is UNVERIFIED.\n");
    std::fflush(stdout);
}

// ?_singleton@Engine@1@0PEAV11@EA (0x1801fe748) -- the class static (ENDGAME B.3.9 ENG, B39_RULES D7);
// RtEnginePortInit redirects the minted storage row onto it.
::Engine::Engine* ::Engine::Engine::_singleton = nullptr;
