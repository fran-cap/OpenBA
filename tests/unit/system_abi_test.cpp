// ============================================================================
// tests/unit/system_abi_test.cpp — ENDGAME B.3.4b SYSTEM (2): the `System`
// coexistence proof, the sibling of `io_vtable_abi_test.cpp`.
//
// WHAT THIS FILE HAS TO ESTABLISH
// -------------------------------
// `System::Thread` and `System::CriticalSection` are real C++ classes. Every
// Thread in the build is a C++ subclass built by `: System::Thread(stackSize)`
// (Engine::Loader, Audio::Stream, IO::PersistenceWorker, the exe
// PathfinderThread), wearing the compiler's tables (src/engine/system/thread.cpp).
//
// So what has to hold is that GCC's compiler-generated vtable for a class
// declared per ENDGAME §B.1 decision 2 puts each slot at retail's byte offset:
//
//   A. a raw caller reading `(*(void***)t)[1]` off a REAL `System::Thread`
//      lands on that class's slot-1 override, `this` unadjusted.
//
// THE RETAIL SLOT LIST BEING MIRRORED
// -----------------------------------
// `??_7Thread@System@@6B@` @ 0x180155f20, TWO slots:
//
//   [0] +0x00  0x18007ec28  `_purecall`  -> `virtual void Run() = 0`
//              The NAME is evidence, not a guess: the derived table
//              `??_7Loader@Engine@@6B@` has its slot-0 override exported as
//              `?Run@Loader@Engine@@UEAAXXZ` @ 0x1800dbfc0.
//   [1] +0x08  0x18000c590
//              `?OnCompleted@Thread@System@@UEAAXXZ` -> `virtual void OnCompleted()`
//
// Full derivation and the rules: docs/history/endgame_b/B34B_RULES.md
// §1.1, §2 and rules B4b-1 / B4b-2 / B4b-3.
//
// WHAT THIS FILE DOES NOT TEST. Nothing behavioural: `Start`, `WaitForFinish`,
// `ThreadFunc`, `Lock` and `Unlock` all need live OS objects and are stubbed
// to `abort()` in `system_abi_stubs.cpp`. Their bodies are verified by the real
// run, not here. This file is about LAYOUT and SLOT ORDER, which is the part a
// build cannot catch on its own.
// ============================================================================

#include "unit.h"
#include "system_abi_stubs.h"

#include "system/Thread.h"
#include "system/Value.h"   // B.3.4b SYSTEM (3), PART E
#include "system/WideString.h"  // B.3.4b SYSTEM (4a)
#include "system/Compression.h" // B.3.4b SYSTEM (4b), PART H
#include "system/Configuration.h" // B.3.4b SYSTEM (4c)
#include "system/Timer.h"   // B.3.4b SYSTEM (4), PART F
#include "system/Crc.h"
#include "system/Error.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

// ---------------------------------------------------------------------------
// The probe. Concrete, so it can be constructed; overrides BOTH slots, so a
// raw slot call can say which slot it landed in.
// ---------------------------------------------------------------------------
int g_probeRunCalls = 0;
int g_probeOnCompletedCalls = 0;

struct ThreadProbe : System::Thread {
    // ENDGAME B.3.9 ENG W2: the transitional `Thread() = default` is gone; the
    // real base ctor runs system_abi_stubs.cpp's recording InitFields.
    ThreadProbe() : System::Thread(0x40000u) {}
    void Run() override         { ++g_probeRunCalls; }
    void OnCompleted() override { ++g_probeOnCompletedCalls; }
};
using SlotFn = void (*)(void*);

// Read slot `n` out of whatever `p`'s vptr points at, the way every
// unconverted caller in the tree does it.
SlotFn RawSlot(void* p, int n) {
    void** vt = *reinterpret_cast<void***>(p);
    return reinterpret_cast<SlotFn>(vt[n]);
}

}  // namespace

// ===========================================================================
// PART A — shape. Sizes and offsets are `Thread.h` / `Value.h` static_asserts;
// only what the headers do not say is checked here.
// ===========================================================================

UNIT_TEST(system_criticalsection_is_0x28_bytes_no_vptr_real_ctor_and_dtor) {
    CHECK(!std::is_polymorphic<System::CriticalSection>::value);
    CHECK(std::is_standard_layout<System::CriticalSection>::value);
    // B6 row 50: the ??0 / ??1 doors are gone; construction and teardown are
    // the class's own ctor (0x18000c5c0) and dtor (0x18000c5b0), so its one
    // holder (PathfinderThread +0x30) runs each exactly once, as a member.
    CHECK(!std::is_trivially_default_constructible<System::CriticalSection>::value);
    CHECK(!std::is_trivially_destructible<System::CriticalSection>::value);
    CHECK(!std::is_copy_constructible<System::CriticalSection>::value);
}

// ===========================================================================
// PART B — the shape decision 2 asks for, enforced.
// ===========================================================================

UNIT_TEST(system_thread_is_abstract_which_is_what_forbids_constructing_one) {
    // B4b-2/B4b-3. Abstract makes `System::Thread t;` a hard compile error
    // (Thread.h), and retail's slot 0 really is `_purecall`, so abstract is also
    // the faithful shape rather than a trick.
    CHECK(std::is_abstract<System::Thread>::value);
    CHECK(std::is_polymorphic<System::Thread>::value);
    CHECK(!std::is_default_constructible<System::Thread>::value);

}

UNIT_TEST(system_thread_has_no_virtual_destructor_so_oncompleted_stays_at_slot_1) {
    // ENDGAME decision 2, stated as a measurement rather than a comment.
    // Itanium spends TWO slots on a virtual destructor, so declaring one on
    // `System::Thread` would push `OnCompleted` from slot 1 to slot 3, off
    // retail's slot 1 (0x18000c590). The guard is that slot 1 of a real probe
    // IS the slot-1 override (PART C below proves it) AND that the class has no
    // virtual destructor to begin with.
    CHECK(!std::has_virtual_destructor<System::Thread>::value);
}

// ===========================================================================
// PART C — DIRECTION A: a raw slot call on a REAL class lands on its override.
// ===========================================================================

UNIT_TEST(raw_slot_0_and_slot_1_on_a_real_thread_reach_run_and_oncompleted) {
    ThreadProbe probe;
    g_probeRunCalls = 0;
    g_probeOnCompletedCalls = 0;

    // Exactly what retail's `ThreadFunc` (0x1800ca440) does on every worker pass.
    RawSlot(&probe, 0)(&probe);
    CHECK_EQ(g_probeRunCalls, 1);
    CHECK_EQ(g_probeOnCompletedCalls, 0);

    RawSlot(&probe, 1)(&probe);
    CHECK_EQ(g_probeRunCalls, 1);
    CHECK_EQ(g_probeOnCompletedCalls, 1);

    // `this` unadjusted: single inheritance, one vptr, and the object's address
    // IS the vptr's address.
    CHECK_EQ(static_cast<void*>(&probe),
             static_cast<void*>(static_cast<System::Thread*>(&probe)));
}

// ===========================================================================
// PART E — B.3.4b SYSTEM (3): the value types.
// ===========================================================================
// `System::Random`, `Time`, `Color` and `DateTime` have NO vtable, so there is
// no slot order to pin and nothing here resembles PART C. What there IS
// to pin is the thing a build can get wrong silently: that each class is the
// same bytes as the POD the unconverted tree still reads through a cast, and
// that none of them acquires a vptr, a virtual, or a destructor.
//
// NO MEMBER IS CALLED HERE. Their bodies live in
// `src/engine/system/value_types.cpp`, which the unit host links for
// `random_test.cpp` (the real MT19937 draw; tests/unit/sources.cmake). This part
// is layout, and layout is what a static_assert can carry but a deleted
// static_assert cannot report.

UNIT_TEST(system_random_is_0x9c4_bytes_and_aliases_the_global_pod) {
    CHECK_EQ(sizeof(System::Random), sizeof(::Random));
    CHECK_EQ(offsetof(System::Random, _index), offsetof(::Random, index));
    CHECK_EQ(offsetof(System::Random, _mt),    offsetof(::Random, mt));
    CHECK(!std::is_polymorphic<System::Random>::value);
    CHECK(std::is_standard_layout<System::Random>::value);
    CHECK(std::is_trivially_destructible<System::Random>::value);

    // ENDGAME B7 (CTOR, D5): the two exported ctors (0x1800c7f20 / 0x1800c7ed0)
    // are REAL ctors and the doors that seeded raw storage are gone, so every
    // holder seeds exactly once (by-value members at their declaration position,
    // raw-storage holders by placement new). The old pin here (trivially
    // default-constructible) guarded against a ctor running BESIDE a door; with
    // no door left the pin is the opposite: a default-constructed Random is
    // seeded, never left as zeroed storage.
    CHECK(!std::is_trivially_default_constructible<System::Random>::value);
    CHECK((std::is_constructible<System::Random, uint32_t>::value));
}

UNIT_TEST(system_time_is_8_bytes_over_the_shared_global_struct_Time) {
    // `::Time` is `engine/types/common.h`'s, shared with the exe tree, and is
    // NOT moved by B.3.4b (rule B4b-1 does not fire: the POD never occupied the
    // class's name). `System::Time` is a new name over the same eight bytes.
    CHECK_EQ(sizeof(System::Time), sizeof(::Time));
    CHECK_EQ(offsetof(System::Time, _ticks), offsetof(::Time, ticks));
    CHECK(!std::is_polymorphic<System::Time>::value);
    CHECK(std::is_standard_layout<System::Time>::value);
    CHECK(std::is_trivially_default_constructible<System::Time>::value);
    CHECK(std::is_trivially_destructible<System::Time>::value);
}

UNIT_TEST(the_value_classes_write_the_same_bytes_the_pods_read) {
    // The live mixed state: written through the POD
    // spelling (what the unconverted tree does) and read back through the
    // class, and the reverse. This is the whole coexistence claim for a class
    // with no vtable.
    ::Random pod{};
    pod.index = 42;
    pod.mt[0] = 0xdeadbeefu;
    pod.mt[623] = 0x0badc0deu;
    System::Random* asClass = reinterpret_cast<System::Random*>(&pod);
    CHECK_EQ(asClass->_index, 42);
    CHECK_EQ(asClass->_mt[0], 0xdeadbeefu);
    CHECK_EQ(asClass->_mt[623], 0x0badc0deu);
    asClass->_index = 7;
    CHECK_EQ(pod.index, 7);

    ::Color cpod{};
    cpod.value = 0xff3366ccu;   // AARRGGBB as a uint32, per Value.h's banner
    System::Color* cAsClass = reinterpret_cast<System::Color*>(&cpod);
    CHECK_EQ(cAsClass->_value, cpod.value);

    ::Time tpod{};
    tpod.ticks = 0x0123456789abcdefLL;
    System::Time* tAsClass = reinterpret_cast<System::Time*>(&tpod);
    CHECK_EQ(tAsClass->_ticks, 0x0123456789abcdefLL);
}

// ===========================================================================
// PART F — B.3.4b SYSTEM (4): the all-static classes.
// ===========================================================================
// `System::Timer`, `System::CRC` and `System::Error` are classes whose every
// member is `static`. The mangling is the evidence and it is what separates
// them from B.3.4a's `IO::TextCodec`, which came out the other way — four
// receiverless free functions with NO exported class-scoped name, for which
// nothing landed. `?Startup@Timer@System@@SA_NXZ`, `?Compute@CRC@System@@SAIPEBXHI@Z`
// and `?SetText@Error@System@@SAXPEBD_N@Z` all carry `@<Class>@System@@` plus a
// static-member mangling (`SA` public static, `CA` private static, `0` private
// static data), which MSVC emits only for a member of a class of that name.
//
// They have NO instance state, so there is nothing to `offsetof` and no POD to
// cross-assert against — the whole of PART A has no analogue here. What IS
// worth pinning is that they stay empty and non-polymorphic, because a stray
// data member or virtual would silently give them a layout they must not have.

UNIT_TEST(the_all_static_system_classes_stay_empty_and_non_polymorphic) {
    CHECK(std::is_empty<System::Timer>::value);
    CHECK(std::is_empty<System::CRC>::value);
    CHECK(std::is_empty<System::Error>::value);
    CHECK(!std::is_polymorphic<System::Timer>::value);
    CHECK(!std::is_polymorphic<System::CRC>::value);
    CHECK(!std::is_polymorphic<System::Error>::value);
    // `System::Timer::operator=` is retail's only non-static member
    // (??4Timer@System@@QEAAAEAV01@AEBV01@@Z, 0x180007890, `MOV RAX,RCX; RET`).
    // It copies nothing, so the class stays trivially copyable and empty.
    CHECK(std::is_trivially_destructible<System::Timer>::value);
    CHECK(std::is_copy_assignable<System::Timer>::value);
}

UNIT_TEST(system_crc_compute_matches_the_standard_crc32_check_vector) {
    // THE ONE BEHAVIOURAL TEST IN THIS FILE: the unit host links `crc.cpp`
    // (tests/unit/sources.cmake), so this runs the REAL
    // `System::CRC::Compute` member, not a copy of it.
    //
    // "123456789" is the standard CRC-32 check vector and 0xCBF43926 is its
    // documented result. Retail's Compute does NOT complement the seed on
    // entry but DOES complement on exit (`NOT EAX` before the `RET`), so a
    // caller wanting the standard answer passes 0xFFFFFFFF itself — which is
    // exactly what this asserts, and what pins the entry/exit asymmetry that
    // Crc.h's banner describes.
    const char kCheck[] = "123456789";
    CHECK_EQ(System::CRC::Compute(kCheck, 9, 0xFFFFFFFFu), 0xCBF43926u);

    // The chaining convention `src/engine/io/persistence_worker.cpp`'s
    // `Crc32Chunked` uses: feed the complement of the previous result back in.
    // Splitting the vector must reproduce the whole-buffer answer.
    uint32_t part = System::CRC::Compute(kCheck, 4, 0xFFFFFFFFu);
    CHECK_EQ(System::CRC::Compute(kCheck + 4, 5, ~part), 0xCBF43926u);

    // `count <= 0` skips the loop and still complements — retail's
    // `TEST EDX,EDX / JLE done`. Not an edge case nobody hits: it is the shape
    // of an empty chunk on the persistence path.
    CHECK_EQ(System::CRC::Compute(kCheck, 0, 0xFFFFFFFFu), 0x00000000u);
    CHECK_EQ(System::CRC::Compute(kCheck, -1, 0x12345678u), ~0x12345678u);
}

// ===========================================================================
// PART H — B.3.4b SYSTEM (4b): `System::Compression`.
// ===========================================================================
// The one 4b class whose live instance is a HEAP object and whose size is
// pinned by a call site rather than by a POD: both constructors in the tree do
// `MemAlloc(8, 4)` and placement-new the class into those eight bytes
// (`src/engine/io/compressed_stream_write.cpp` and
// `src/engine/save/saveload_meta.cpp`). Neither file can be told the
// class grew, so `sizeof == 8` is not a tidiness assert — it is the contract
// with two allocations that are out of this step's file set.
//
// Nothing is constructed here: the constructor allocates 0x40000 bytes through
// `RtSystem::RtMemAlloc`. The codec itself was verified against the shipped DLL by the
// Wave-9 / Wave-22 oracles and is verified today by every package the game
// loads.

UNIT_TEST(system_compression_is_one_pointer_and_must_stay_eight_bytes) {
    CHECK(!std::is_polymorphic<System::Compression>::value);
    CHECK(std::is_standard_layout<System::Compression>::value);

    // `??1Compression@System@@` (0x1800c4170) is unported ON PURPOSE: the live
    // teardown is inlined into `IO::CompressedStream::Release`, which frees the
    // 0x40000 table and then the object itself through MemFree. A destructor
    // here would be a second, private opinion about a teardown that already
    // happens somewhere else — and it would run on top of it.
    CHECK(std::is_trivially_destructible<System::Compression>::value);

    // The constructor IS ported, so the class is not
    // default-constructible-by-accident in the trivial sense: its two callers
    // placement-new it, which is what makes the 0x40000 table appear.
    CHECK(!std::is_trivially_default_constructible<System::Compression>::value);
}
