#pragma once
// ============================================================================
// src/engine/system/Value.h — System::Random / Time / Color layouts
// ============================================================================
//
// Wave 2, Lane B of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2/§6).
// Every layout below was read out of Runtime-x64.dll in Ghidra (image base
// 0x180000000, decompiled 2026-09-06); the function that pins it is named.
//
// ABI NOTE (same rule as src/engine/math/Math.h)
// --------------------------------------------------
// Random and Time each have a user-declared constructor, so a by-value return
// of either (there are none in this lane's scope, but future ports must not
// assume otherwise) would be returned BY MEMORY with the hidden slot as an
// explicit argument. Every member port here is a plain free function.
//
// ENDGAME B.3.4b SYSTEM (4c) CORRECTS THE NEXT PARAGRAPH ON BOTH COUNTS.
// `System::DateTime` IS modelled below (it is a FILETIME; see its banner), and
// `System::Configuration` has 0x30 bytes of instance data and now has its own
// header, `src/engine/system/Configuration.h`. The claim "Configuration has
// no instance data" was true only of the three `SA` statics this file happens
// to host; the class's five instance members live in
// `src/engine/system/configuration.cpp`. The paragraph is kept rather than
// rewritten because it is the Wave-2 reasoning of record.
//
// System::DateTime and System::Configuration are NOT modeled below.
// DateTime::AsString is the only DateTime function in this lane's scope and
// treats `this` as opaque (it forwards to the DLL's own DateAsString/
// TimeAsString), so no layout is needed. Configuration has no instance data
// (every member in scope is `static`). See value_types.cpp for the reasoning
// on why System::String is likewise never modeled here: it is Lane A's
// (src/engine/system/String.h), and every String this file produces is
// built by calling the DLL's own String::Set/Format/operator+=/Release
// through the registry door, never by touching its refcount/length header.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>   // B.3.4b: the "none of these acquires a vtable" pins

// ENDGAME B.3.4b: `System::DateTime::AsString` and friends return
// `System::String` BY VALUE, so the definition is needed, not a declaration.
// Global scope, above every namespace block, never inside an `#if` (B31
// hygiene / B34_BRIEFS §A.8).
#include "String.h"

// `IO::Serializer` is only ever a reference parameter here, so a forward
// declaration is enough and this header does not drag `src/engine/io/` in.
// Spelled `class` because that is what `src/engine/io/Serializer.h` declares
// it as — B34B_RULES rule B4b-5: GCC accepts a struct/class tag mismatch in a
// forward declaration SILENTLY, so the tag is matched deliberately.
namespace IO { class Serializer; }

// ============================================================
// System::Random — a Mersenne-Twister-shaped PRNG (NOT the textbook MT19937:
// the tempering constants differ, see below). Pinned by the default ctor
// (??0Random@System@@QEAA@XZ, 0x1800c7f20) and NextGroup
// (?NextGroup@Random@System@@AEAAXXZ, 0x1800c7dd0):
//   +0x00: int32  index   — position in mt[], drives the "regenerate every
//                           624 draws" cadence; starts at 0, which is also
//                           what forces the first-ever Rand() to regenerate.
//   +0x04: uint32 mt[624] — the state array (624*4 = 2496 bytes).
// Total size 4 + 2496 = 2500 (0x9c4) bytes.
//
// Seeding (both ctors) is the standard MT19937 init_genrand recurrence
// (mt[i] = 1812433253*(mt[i-1]^(mt[i-1]>>30)) + i), but NextGroup's tempering
// after the twist uses 0xff3a58ad/0xffffdf8c where textbook MT19937 uses
// 0x9d2c5680/0xefc60000 — Shining Rock's own constants, transcribed from
// ?Rand@Random@System@@QEAAIXZ (0x1800c7f90), not assumed from the textbook.
// ============================================================
struct Random {
    int32_t  index;
    uint32_t mt[624];
};
static_assert(offsetof(Random, mt) == 4, "Random::mt must sit at +0x04");
static_assert(sizeof(Random) == 2500, "System::Random must be 2500 (0x9c4) bytes");

// ============================================================
// System::Time — a tick count at Timer's resolution.
// Pinned by ??0Time@System@@QEAA@M@Z (0x1800078c0): writes one qword.
//   +0x00: int64 ticks
// The float<->ticks conversion is Timer::SecondsToTicks/TicksToSeconds
// (Timer is out of this lane's scope and not ported here); our ctor and
// AsSeconds delegate to those two exports via the registry rather than
// duplicating Timer::_secondsPerTick, which is a runtime-computed global
// (QueryPerformanceFrequency-derived) and not a compile-time constant.
//
// ENDGAME B.3.3 step 6B — THE DEFINITION IS NOT HERE ANY MORE, AND THAT IS A
// BUG FIX, NOT A TIDY. `engine/types/common.h:12` defines the same global
// `struct Time` (identical: one `int64_t ticks`, asserted at 8 bytes in both
// places), and `src/engine/math/Math.h:529` includes that header for its
// `Vector2/3/4` aliases. So ANY translation unit that included both
// `src/engine/math/Math.h` and this file was a hard `redefinition of
// 'struct Time'` error — the two headers could never meet, which is why no
// exe-side file has ever been able to include `src/engine/object/Internal.h`
// (it pulls this header for `::Random`). Proven with a two-line TU before this
// edit. The definition is taken from the one place that owns it; the pin stays
// here so a layout change still fails this header's build.
// ============================================================
#include "../global_types.h"   // the one global `struct Time`
static_assert(sizeof(Time) == 8, "System::Time must be 8 bytes");

// ============================================================
// System::Color — one packed uint32, byte-addressed B,G,R,A (little-endian,
// so as a uint32 it reads 0xAARRGGBB). Pinned by the accessors:
//   ?Blue@Color@System@@QEBAHXZ   (0x18001fe60): byte 0
//   ?Green@Color@System@@QEBAHXZ  (0x18001fe50): byte 1
//   ?Red@Color@System@@QEBAHXZ    (0x18001fe40): byte 2
//   ?Alpha@Color@System@@QEBAHXZ  (0x18001fe30): byte 3
// Normalized* divide by 255.0f (DAT_180161718, confirmed by reading the
// bytes: 0x437f0000 = 255.0f).
// ============================================================
struct Color {
    uint32_t value;
};
static_assert(sizeof(Color) == 4, "System::Color must be 4 bytes");

// ============================================================================
// ENDGAME B.3.4b — the REAL `System::Random`, `Time`, `Color` and `DateTime`.
// ============================================================================
//
// Design: B34B_RULES.md §1.1 (the exported member list, by mangled name and
// retail address), rule B4b-2 (no real constructors in 4b — the reason here is
// NOT Thread's, see below), rule B4b-5 (forward-declaration tags);
// B34_RULES.md §1.5 rule 3 (the POD stays) and §4.3 (the ctor-door question);
// ENDGAME §B.1 decision 5 (retail's names) and decision 6 (value types).
//
// *** RULE B4b-1 DOES NOT APPLY TO THIS HEADER, AND THAT IS WORTH SAYING. ***
// `Thread.h` had to move its PODs because they sat inside `namespace System`
// and occupied the classes' own names. The PODs here do NOT: `Random` and
// `Color` are at GLOBAL scope above, and `Time` is the global `struct Time`
// that `engine/types/common.h:12` owns and shares with the exe tree. So
// nothing is renamed, nothing outside `runtime/` is touched, and the four
// classes below are cross-asserted against PODs that stay exactly where they
// are. Spell the POD `::Random` / `::Time` / `::Color` everywhere inside a
// `namespace System` block — B34_RULES §7.2's grep hazard is armed for these
// three names from this commit on, and it bites SILENTLY because the layouts
// match: the wrong spelling compiles and does the right thing until the day
// one of the two gains a member.
//
// *** WHY `System::Time` COULD BE DECLARED HERE AT ALL. ***
// `::Time` lives in `engine/types/common.h`, which `src/engine/math/Math.h`
// includes and which therefore reaches most of the tree; B.3.3 step 6B
// deliberately de-duplicated it there (see the banner above) and moving it
// would be an `engine/` edit. It does not have to move: `System::Time` is a
// NEW name in a NEW namespace over the SAME eight bytes, cross-asserted
// against `::Time` below. Every one of the ~68 files that reaches a Time
// function does so through a `void*` door — `fn_Time_Ctor_Float`,
// `fn_Time_AsSeconds`, `g_fn_Time_Serialize` (engine_funcs.h:1827, 1083, 5475;
// all three typedefs take `void*`) — and every `System::Time` spelling under
// `code/Game` is a COMMENT. So the class costs ZERO edits outside `runtime/`,
// and `::Time` keeps its single definition.
//
// *** NONE OF THE FOUR GETS A REAL CONSTRUCTOR IN 4b, AND THE REASON IS NOT
// *** THREAD'S. *** B34_RULES §4.3 forbids a ctor door converting when the
// member form would change which vtable a live object wears; none of these four
// has a vtable, so that test PASSES and a real constructor would be PERMITTED.
// It is deferred anyway, for a concrete reason per class:
//
//   `Random`  — `Object::InternalFactory` is a REAL class since B.3.3 and holds
//               `::Random _random;` BY VALUE at +0x20
//               (`src/engine/object/Internal.h:662`), while
//               `Ctor_InternalFactory`
//               (`src/engine/engine/singleton_ctors.cpp:352-361`, outside this
//               step's file set) seeds it through the
//               `??0Random@System@@QEAA@XZ` DOOR. Give `System::Random` a real
//               constructor and the moment anyone re-spells that member, the
//               holder's implicit constructor seeds it AND the door seeds it
//               again — two different `time(nullptr)` seeds on the object
//               factory's RNG, i.e. a determinism change with no build error.
//               `Graphics::Particle::_rng` (0x1801fe7f0) is a second by-value
//               instance with its own seeding path.
//   `Time`    — its ~132 `fn_Time_Ctor_Float` call sites construct into RAW
//               BYTE BUFFERS and convert as placement-new (B34_RULES §7.3),
//               which is caller-batch work gated behind the 4a closing pass.
//               The constructor converts WITH them, not before.
//   `Color`   — retail's three constructors (0x18001fdd0 / 0x18001fde0 /
//               0x18001fdf0) are exported but were never ported or registered,
//               and an uncalled port is an unverified port. Nothing is invented.
//   `DateTime`— likewise: `??0DateTime@System@@QEAA@XZ` (0x1800c57f0),
//               `??0…@QEAA@U_FILETIME@@@Z` (0x1800c5800) and `??1…` (0x1800c5860)
//               have no body anywhere in this tree.
//
// So every class below declares its PORTED members only, the registered doors
// keep their addresses and delegate, and each class stays trivially
// destructible so no implicit destructor can run beside a hand-written one.
// ============================================================================

namespace System {

// ----------------------------------------------------------------------------
// System::Random — 0x9c4 bytes, NO vtable. The tempering constants are Shining
// Rock's own, not the textbook MT19937's; see the POD banner at the top.
// ----------------------------------------------------------------------------
class Random {
public:
    // ENDGAME B7 (CTOR, B7_RULES D5): the two exported ctors are REAL ctors; the
    // `RtSystem_Random_Ctor` / `_CtorSeed` doors are deleted with their last
    // caller. B4b-2's reason above is gone: every by-value holder
    // (MenuDialogLoopT, GameLoadDialogLoopT, NewGameDialog, the
    // `Graphics::Particle::_rng` static) now runs THIS ctor at the member's
    // position instead of a door in its body, and the raw-storage holders
    // (`Object::InternalFactory`, `TerrainHeightfield`) placement-new it.
    // Still NO destructor: retail has no ~Random, so the class stays trivially
    // destructible (pinned below).
    // ??0Random@System@@QEAA@XZ   0x1800c7f20 — `index = 0`, mt[0] = (uint)_time64(NULL)
    //   (CALL 0x18007fb9c), then mt[i] = 0x6c078965 * (mt[i-1] ^ (mt[i-1] >> 30)) + i.
    Random();
    // ??0Random@System@@QEAA@I@Z  0x1800c7ed0 — the same with mt[0] = seed.
    explicit Random(uint32_t seed);

    // ?Rand@Random@System@@QEAAIXZ         0x1800c7f90  — the full 32-bit draw
    uint32_t Rand();
    // ?Rand@Random@System@@QEAAII@Z        0x1800c8040  — bound==0 returns 0
    //   WITHOUT drawing, so the state must stay untouched.
    uint32_t Rand(uint32_t bound);
    // ?Rand@Random@System@@QEAAHHH@Z       0x1800c7d70
    int32_t  Rand(int32_t lo, int32_t hi);
    // ?RandFloat@Random@System@@QEAAMXZ    0x1800c7d40  — [0,1)
    float    RandFloat();
    // ?RandFloatTol@Random@System@@QEAAMXZ 0x1800c8000  — left-associated
    float    RandFloatTol();
    // ?RandTol@Random@System@@QEAAHH@Z     0x1800c7da0  — [-tol, +tol]
    int32_t  RandTol(int32_t tol);

    // ?NextGroup@Random@System@@AEAAXXZ    0x1800c7dd0
    // `AEAA` is the PRIVATE spelling, not the unexported one — it is a real
    // export (ord 3245) and it is registered. PUBLIC here rather than private
    // because its registered door calls it, and access control is not ABI.
    void NextGroup();

    // ?Serialize@Random@System@@QEAAXAEAVSerializer@IO@@@Z  0x1800c7cf0
    void Serialize(::IO::Serializer& s);

    // ---- the 0x9c4 bytes, at retail's offsets ------------------------------
    // PUBLIC for the same reason `IO::Stream`'s three are: the unconverted tree
    // reads them through a cast to the POD, and `offsetof` has to be well
    // formed.
    uint32_t _index;     // +0x000  unsigned: Rand 0x1800c7fa3 MOV EDX,[RBX] zero-extends it into the _mt index
    uint32_t _mt[624];   // +0x004
};

static_assert(sizeof(Random) == 2500, "System::Random is 2500 (0x9c4) bytes");
static_assert(sizeof(Random) == sizeof(::Random), "class and POD interchangeable");
static_assert(offsetof(Random, _index) == 0x00, "Random::_index @ +0x00");
static_assert(offsetof(Random, _mt)    == 0x04, "Random::_mt @ +0x04");
static_assert(offsetof(Random, _index) == offsetof(::Random, index), "alias index");
static_assert(offsetof(Random, _mt)    == offsetof(::Random, mt),    "alias mt");
static_assert(!std::is_polymorphic<Random>::value,
              "System::Random must never acquire a vtable: retail has none");
static_assert(std::is_standard_layout<Random>::value,
              "the doors reach these two fields through a reinterpret_cast");
static_assert(std::is_trivially_destructible<Random>::value,
              "B4b-2: no implicit destructor may run beside a ported teardown");

// ----------------------------------------------------------------------------
// System::Time — 8 bytes, NO vtable. One tick count at Timer's resolution.
// ----------------------------------------------------------------------------
class Time {
public:
    // ?AsSeconds@Time@System@@QEBAMXZ  0x180007900
    // Delegates to `Timer::TicksToSeconds` BY NAME rather than duplicating
    // `Timer::_secondsPerTick`, which is a QueryPerformanceFrequency-derived
    // runtime global and one of the eleven CUTOVER_REQUIRED cells
    // (B34B_RULES §4.1) — not a compile-time constant.
    float AsSeconds() const;

    // ?Serialize@Time@System@@QEAAXAEAVSerializer@IO@@@Z  0x1800ca740
    //
    // B.3.4b SYSTEM (5): its body is `s.Serialize(_ticks, nullptr)` — ONE
    // member call. It used to hand-inline the ReaderWriter's +0x90 slot because
    // `IO::Serializer` declared no int64 scalar overload; that overload now
    // exists (`?Serialize@Serializer@IO@@QEAAXAEA_JPEBD@Z` @ 0x180035000,
    // `standalone/rt_export_addrs.inc:1204`), and THIS SITE IS WHAT JUSTIFIED
    // ADDING IT — B34B_RULES §3 sequenced it after the 4a closing pass because
    // the member body belongs in `src/engine/io/serializer_class.cpp`. Still
    // do NOT route these eight bytes through the four-byte int door: +0x90 and
    // +0xA0 are different slots and different widths.
    void Serialize(::IO::Serializer& s);

    // ---- the 8 bytes -------------------------------------------------------
    int64_t _ticks;   // +0x00
};

static_assert(sizeof(Time) == 8, "System::Time is 8 bytes");
static_assert(sizeof(Time) == sizeof(::Time), "class and POD interchangeable");
static_assert(offsetof(Time, _ticks) == 0x00, "Time::_ticks @ +0x00");
static_assert(offsetof(Time, _ticks) == offsetof(::Time, ticks), "alias ticks");
static_assert(!std::is_polymorphic<Time>::value,
              "System::Time must never acquire a vtable: retail has none");
static_assert(std::is_standard_layout<Time>::value, "reached through a cast");
static_assert(std::is_trivially_destructible<Time>::value,
              "B4b-2: the ~132 call sites build these in RAW BYTE BUFFERS");

// ----------------------------------------------------------------------------
// System::Color — 4 bytes, NO vtable. One packed uint32, byte-addressed B,G,R,A.
// ----------------------------------------------------------------------------
// The eight accessors and three constructors retail exports are NOT declared
// here: none is ported and none is registered, and an uncalled port is an
// unverified port. Their addresses are in B34B_RULES §1.1 for whoever ports
// them. `?Serialize@` is the whole live surface — six CALL sites, all through
// `fn_System_Color_Serialize`.
class Color {
public:
    // ?Serialize@Color@System@@QEAAXAEAVSerializer@IO@@@Z  0x1800c4150
    // Slot +0x98 (`rw_vtbl::kScalarUInt`) — the same door `Random::Serialize`
    // uses for its own 4-byte scalar.
    void Serialize(::IO::Serializer& s);

    // ---- the 4 bytes -------------------------------------------------------
    uint32_t _value;   // +0x00  as a uint32 it reads 0xAARRGGBB
};

// ARGB (0xAARRGGBB) colour words the engine spells as literals.
constexpr uint32_t kColorWhite   = 0xffffffffu;   // opaque white: the no-tint default (Element::_color)
constexpr uint32_t kColorBlack   = 0xff000000u;   // opaque black: text drop shadows, fullscreen fades
constexpr uint32_t kColorGrey    = 0xff808080u;   // opaque 50% grey: list hover, the menu backdrop (DialogLoopT::Draw)
constexpr uint32_t kColorRgbMask = 0x00ffffffu;   // the RGB bits; the complement is the alpha byte

static_assert(sizeof(Color) == 4, "System::Color is 4 bytes");
static_assert(sizeof(Color) == sizeof(::Color), "class and POD interchangeable");
static_assert(offsetof(Color, _value) == 0x00, "Color::_value @ +0x00");
static_assert(offsetof(Color, _value) == offsetof(::Color, value), "alias value");
static_assert(!std::is_polymorphic<Color>::value,
              "System::Color must never acquire a vtable: retail has none");
static_assert(std::is_standard_layout<Color>::value, "reached through a cast");
static_assert(std::is_trivially_destructible<Color>::value, "B4b-2");

// ----------------------------------------------------------------------------
// System::DateTime — 8 bytes, NO vtable. IT IS A FILETIME.
// ----------------------------------------------------------------------------
// *** THIS IS THE ONE CLASS IN THIS HEADER THAT HAD NO POD AT ALL. *** The
// banner at the top of this file still says DateTime "is NOT modeled below …
// `this` IS A FILETIME", and every ported body took it as `const void*`. The
// layout is spelled out now, and the evidence is exactly the sentence that used
// to justify NOT spelling it: retail's first instruction in `DateAsString`
// (0x1800c56a0) hands `this` STRAIGHT to `FileTimeToLocalFileTime`, whose
// parameter is a `const FILETIME*`. A FILETIME is `{DWORD dwLowDateTime; DWORD
// dwHighDateTime;}` — two 32-bit halves, NOT one int64 — and
// `?AsFileTime@DateTime@System@@QEBA?AU_FILETIME@@XZ` (0x180026dc0) returns one
// by value, which is the second pin.
//
// The two halves are spelled separately rather than as a `uint64_t` so the
// header stays honest about the shape AND stays free of <windows.h>
// (PLAN.md §4.1 decision 1). `src/engine/system/value_types.cpp` is the only
// file that casts them to a real `FILETIME`, and it is the platform-side half
// that already includes the header.
//
// NO LAYOUT WAS INVENTED: there is no third field, because 8 bytes is all
// `FileTimeToLocalFileTime` reads and all `AsFileTime` returns.
class DateTime {
public:
    // ?AsString@DateTime@System@@QEBA?AVString@2@XZ      0x1800c54d0
    // ?DateAsString@DateTime@System@@QEBA?AVString@2@XZ  0x1800c56a0
    // ?TimeAsString@DateTime@System@@QEBA?AVString@2@XZ  0x1800c55b0
    //
    // ⚠ ALL THREE RETURN `System::String` BY VALUE, WHICH IS A HIDDEN RETURN
    // SLOT AT THE ABI. Retail's is `String* f(const DateTime* this, String* ret)`
    // — RCX = this, RDX = ret — and Ghidra DROPS the second parameter and the
    // return for all three (`reference_ghidra_c_output_vs_disassembly.md`;
    // `value_types.cpp` quotes the three instructions that prove it). The
    // registered DOORS keep that two-argument shape and placement-new the
    // member's prvalue into the caller's slot; the MEMBERS return by value,
    // which is what retail's source said. Never register a member directly and
    // never reach one from a raw slot caller — that is
    // `reference_hidden_retslot_wild_write.md`.
    String AsString() const;
    String DateAsString() const;
    String TimeAsString() const;

    // ---- the 8 bytes: a FILETIME ------------------------------------------
    uint32_t _lowDateTime;    // +0x00  FILETIME::dwLowDateTime
    uint32_t _highDateTime;   // +0x04  FILETIME::dwHighDateTime
};

static_assert(sizeof(DateTime) == 8, "System::DateTime is a FILETIME: 8 bytes");
static_assert(offsetof(DateTime, _lowDateTime)  == 0x00, "FILETIME::dwLowDateTime");
static_assert(offsetof(DateTime, _highDateTime) == 0x04, "FILETIME::dwHighDateTime");
static_assert(alignof(DateTime) == 4,
              "a FILETIME is two DWORDs and is 4-aligned -- NOT an int64. Every "
              "caller passes it by pointer, so this must not drift to 8.");
static_assert(!std::is_polymorphic<DateTime>::value,
              "System::DateTime must never acquire a vtable: retail has none");
static_assert(std::is_standard_layout<DateTime>::value,
              "the three doors reach it through a reinterpret_cast from void*");
static_assert(std::is_trivially_destructible<DateTime>::value, "B4b-2");

}  // namespace System

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- system/value_types.cpp
System::Time* RtSystem_Time_Ctor(System::Time* self, float seconds);  // 0x1800078c0
// ==== end B3R W0-P0c ====
