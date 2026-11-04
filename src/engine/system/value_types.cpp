// ============================================================================
// src/engine/system/value_types.cpp — Random/Time/DateTime/Color/Configuration
// ============================================================================
//
// Wave 2, Lane B of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2/§6).
// Every function below is a byte-faithful port of the decompilation at the
// retail address named in its comment (Runtime-x64.dll, image base
// 0x180000000, decompiled 2026-09-06). See src/engine/system/Value.h for
// the governing layouts.
//
// STRING NOTE: four functions in this lane's scope return System::String
// (DateTime::AsString, Configuration::CurrentBuildId/CurrentVersion/
// GetVersionString). System::String's refcounted layout is Lane A's
// (src/engine/system/String.h) — this file never models or touches it.
// Every String these functions produce is built by calling the DLL's own
// String::Set / String::Format / String::operator+= / String::Release
// through the DLL's own String routines, exactly as retail's own code does; we
// only ever
// read a String's PUBLIC content (its data pointer used as a plain
// null-terminated C string), never its private refcount/length header.
//
// Those calls go through RT_EXPORT_ONCE. Set / Format / operator+= / Release
// are all PORTED.
//
// ENDGAME B.3.1 step 2 REPLACES THE PARAGRAPH ABOVE. Runtime-x64.dll is gone,
// `System::String` is a real class over the same eight bytes, and the
// file-local opaque `StrSlot` — whose whole purpose was to avoid modelling a
// type that belonged to another binary — is deleted. The four bodies below
// hold real `System::String` locals with RAII, and the Set / Format /
// operator+= / Release name doors are the class's own members. The retail
// addresses on each call stay: they are the provenance.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <ctime>
// WAVE 22 LANE SYSBODY: DateAsString/TimeAsString are Win32 date formatting
// (FileTimeToLocalFileTime / FileTimeToSystemTime / GetDateFormatW /
// GetTimeFormatW). Since Phase D.4 THREADS E.7 those four calls are
// Platform::FormatLocalDateTime (platform_file.h), and this file includes no OS
// header.
#include "platform_file.h"
#include <new>          // placement new into the retail hidden return slot
#include "system/Value.h"
#include "system/String.h"
#include "system/Configuration.h"   // ENDGAME B.3.4b SYSTEM (4c)
#include "system/Timer.h"   // B3R: the Timer statics by name
#include "io/Serializer.h"   // ENDGAME B.3.4 callers batch H
#include "system/widestring_internal.h"

// ============================================================================
// ENDGAME B.3.4b SYSTEM (3) — THE CLASSES ARE REAL; THE DOORS DELEGATE.
// ============================================================================
// `System::Random`, `System::Time`, `System::Color` and `System::DateTime` are
// real C++ classes since B.3.4b (src/engine/system/Value.h). Unlike
// `Thread.h`, NO POD MOVED: `::Random` and `::Color` were already at global
// scope and `::Time` is `engine/types/common.h`'s, shared with the exe tree.
// Rule B4b-1 therefore does not fire here, and this commit touches nothing
// outside `runtime/`.
//
// NONE OF THE FOUR GETS A REAL CONSTRUCTOR — Value.h's banner gives the
// per-class reason, and for `Random` it is concrete: `Object::InternalFactory`
// holds one BY VALUE at +0x20 (src/engine/object/Internal.h:662) while
// `Ctor_InternalFactory` (src/engine/engine/singleton_ctors.cpp:352-361, not
// this step's file) seeds it through the `??0Random@System@@QEAA@XZ` door. A
// real constructor would seed the object factory's RNG twice, from two
// different `time(nullptr)` values, with no build error.
//
// MEMBER BODIES ARE DEFINED AT GLOBAL SCOPE with qualified names
// (`uint32_t System::Random::Rand() {...}`) rather than inside a
// `namespace System { ... }` block. Both are legal — the global namespace
// encloses `System` — and the qualified form is chosen DELIBERATELY: inside a
// `namespace System` block a bare `Random`, `Time` or `Color` binds the CLASS,
// not the POD, and in this file the two are layout-identical, so the wrong
// spelling would compile and behave correctly right up until one of them gains
// a member (B34_RULES §7.2). Keeping the block out removes the hazard instead
// of documenting it.
// ============================================================================

namespace {

// ---- Random: seeding, twist and core draw (retail algorithm, see Value.h) --
//
// B.3.4b: these three retyped from the POD `::Random*` to the class
// `System::Random*` (and `index`/`mt` to `_index`/`_mt`). Same bytes, same
// arithmetic, same loop bounds — the alternative was a `reinterpret_cast` at
// every one of the nine call sites below.

// Standard MT19937 init_genrand recurrence. Shared by both ctors.
// Pinned by ??0Random@System@@QEAA@I@Z (0x1800c7ed0).
void SeedMT(System::Random* self, uint32_t seed) {
    self->_index = 0;
    self->_mt[0] = seed;
    for (int i = 1; i < 624; ++i) {
        uint32_t prev = self->_mt[i - 1];
        self->_mt[i] = (prev ^ (prev >> 30)) * 0x6c078965u + static_cast<uint32_t>(i);
    }
}

// ?NextGroup@Random@System@@AEAAXXZ  retail 0x1800c7dd0 (private, not exported;
// no dispatch slot exists for it — it is only ever reached through Rand()).
// Standard MT19937 twist (UPPER_MASK/LOWER_MASK/MATRIX_A), confirmed against
// the disassembly instruction-by-instruction.
void NextGroupImpl(System::Random* self) {
    constexpr uint32_t kUpper = 0x80000000u, kLower = 0x7fffffffu, kMatrixA = 0x9908b0dfu;
    for (int i = 0; i < 624; ++i) {
        uint32_t y = (self->_mt[i] & kUpper) | (self->_mt[(i + 1) % 624] & kLower);
        uint32_t next = self->_mt[(i + 397) % 624] ^ (y >> 1);
        if (y & 1u) next ^= kMatrixA;
        self->_mt[i] = next;
    }
}

// Core of ?Rand@Random@System@@QEAAIXZ (0x1800c7f90), factored out so every
// overload below (which all draw one uint32 and post-process it) shares one
// transcription of the tempering constants instead of five copies.
uint32_t RandImpl(System::Random* self) {
    if (self->_index == 0) NextGroupImpl(self);
    uint32_t y = self->_mt[self->_index];
    y ^= y >> 11;
    y ^= (y & 0xff3a58adu) << 7;
    y ^= (y & 0xffffdf8cu) << 15;
    uint32_t next = self->_index + 1;
    self->_index = (next == 0x270) ? 0 : next;
    return y ^ (y >> 18);
}

}  // namespace

// ============================================================
// System::Random
// ============================================================

// ??0Random@System@@QEAA@XZ  retail 0x1800c7f20  (time-seeded default ctor).
// Not diffed: two calls made at different wall-clock times legitimately seed
// to different states, so there is no meaningful "original" to compare
// against (same rationale as Math::Vector2's default ctor in vector.cpp).
// Retail seeds from `(uint)_time64(&t)`; std::time's 64-bit time_t on this
// target truncates to unsigned the same way.
// ENDGAME B7 (CTOR, D5): the REAL constructor; the `RtSystem_Random_Ctor` door
// whose body this was is deleted with its last caller. Retail's order is kept:
// the `index = 0` store (0x1800c7f2d) precedes the _time64 call (0x1800c7f3c),
// mt[0] is stored from its result (0x1800c7f4c), then the recurrence.
System::Random::Random() {
    _index = 0;
    _mt[0] = static_cast<uint32_t>(std::time(nullptr));
    for (int i = 1; i < 624; ++i) {
        uint32_t prev = _mt[i - 1];
        _mt[i] = (prev ^ (prev >> 30)) * 0x6c078965u + static_cast<uint32_t>(i);
    }
}

// ??0Random@System@@QEAA@I@Z  retail 0x1800c7ed0
// ENDGAME B7 (CTOR, D5): the REAL constructor (the `RtSystem_Random_CtorSeed`
// door is deleted). 0x1800c7ed4 index = 0, 0x1800c7ee0 mt[0] = seed, then the
// 623-step recurrence 0x1800c7ef0..0x1800c7f13: SeedMT's body.
System::Random::Random(uint32_t seed) {
    SeedMT(this, seed);
}

// ?RandFloat@Random@System@@QEAAMXZ  retail 0x1800c7d40
// DAT_18016165c = 1/2^32 exactly (read as bytes: 0x2f800000): maps a full
// 32-bit draw to [0,1).
namespace { constexpr float kRandUnit = 2.3283064365386963e-10f; }  // 0x2f800000

// ============================================================================
// ENDGAME B.3.4b — the `System::Random` member BODIES.
// ============================================================================
// Defined at GLOBAL scope with qualified names rather than inside a
// `namespace System { ... }` block; the file banner says why. Every body is the
// one that used to sit in the door directly above it.
//
// ONE THING DELETED, AND IT IS REPORTED RATHER THAN SMUGGLED. Six of these
// bodies opened with `Random before;` — a 2500-byte uninitialised stack local
// that was never written and never read, left over from the Wave-2 diff harness
// that compared our draw against the DLL's. It is dead in every build and its
// removal changes no instruction that touches `this`. It is called out here
// because a keyword tally of this commit shows `Random before;` going 6 -> 0
// and that delta must have a stated reason rather than being explained away as
// a rename.
// ============================================================================

uint32_t System::Random::Rand() {
    return RandImpl(this);
}

// ?Rand@Random@System@@QEAAII@Z  retail 0x1800c8040  (bound==0 short-circuits
// to 0 WITHOUT drawing -- state must stay untouched).
uint32_t System::Random::Rand(uint32_t bound) {
    // bound==0 short-circuits to 0 WITHOUT drawing — the state must stay
    // untouched, hence the ternary rather than an early `if` + call.
    return (bound == 0) ? 0u : (RandImpl(this) % bound);
}

// ?Rand@Random@System@@QEAAHHH@Z  retail 0x1800c7d70
int32_t System::Random::Rand(int32_t lo, int32_t hi) {
    uint32_t r = RandImpl(this);
    return lo + static_cast<int32_t>(r % (static_cast<uint32_t>(hi - lo) + 1u));
}

float System::Random::RandFloat() {
    return static_cast<float>(RandImpl(this)) * kRandUnit;
}

// ?RandFloatTol@Random@System@@QEAAMXZ  retail 0x1800c8000
float System::Random::RandFloatTol() {
    // 1.0f - (u * (1/2^32)) * 2.0f, left-associated exactly as retail.
    return 1.0f - (static_cast<float>(RandImpl(this)) * kRandUnit) * 2.0f;
}

// ?RandTol@Random@System@@QEAAHH@Z  retail 0x1800c7da0
int32_t System::Random::RandTol(int32_t tol) {
    uint32_t r = RandImpl(this);
    return static_cast<int32_t>(r % (static_cast<uint32_t>(tol) * 2u + 1u)) - tol;
}

void System::Random::NextGroup() {
    NextGroupImpl(this);
}

// ?Serialize@Random@System@@QEAAXAEAVSerializer@IO@@@Z  retail 0x1800c7cf0
// Two calls: slot +0x98 serializes the plain `index`, slot +0x40 the
// 624-entry uint32 array.
void System::Random::Serialize(::IO::Serializer& s) {
    s.Serialize(_index, nullptr);   // slot +0x98, the uint overload
    s.Serialize(0x270, _mt, nullptr);   // (0x40 = uint-array door)
    // NOT RETAIL (review F-126, out-of-bounds read): retail 0x1800c7cf0 takes _index from
    // the save unchecked, and Rand only wraps it when it reaches exactly 0x270, so a stored
    // 0x270 or more reads past _mt on every draw. Such an index restarts the state block
    // (the twist runs on the next draw); every index a valid save holds is below 0x270.
    if (_index >= 0x270u)
        _index = 0;
}

// ============================================================
// System::Time
// ============================================================

// ??0Time@System@@QEAA@M@Z  retail 0x1800078c0
// Delegates to Timer::SecondsToTicks. Not diffed here: the delegation is one
// call and the arithmetic it delegates to now carries its OWN diff site
// (src/engine/system/timer.cpp), so diffing it again would count the same
// comparison twice under a second name.
//
// B.3.4b: NOT a real constructor. Its ~132 call sites construct into RAW BYTE
// BUFFERS and convert as placement-new (B34_RULES section 7.3), which is
// caller-batch work gated behind the 4a closing pass; the constructor converts
// WITH them, not before. Value.h's banner restates this.
System::Time* RtSystem_Time_Ctor(System::Time* self, float seconds) {
    // System::Timer::SecondsToTicks (0x1800ca800), called by name (B3R).
    self->_ticks = System::Timer::SecondsToTicks(seconds);
    return self;
}

// ============================================================================
// ENDGAME B.3.4b — the `System::Time` member BODIES.
// ============================================================================

// ?AsSeconds@Time@System@@QEBAMXZ  retail 0x180007900
float System::Time::AsSeconds() const {
    return System::Timer::TicksToSeconds(_ticks);   // 0x1800ca820
}

// ENDGAME B.3.4b SYSTEM (5) — THE HAND-INLINED SLOT CALL IS GONE, and this is
// the site that justified adding the overload rather than the other way round.
//
// What used to be here was six lines that reached `s + 8` (the ReaderWriter;
// the Stream is at Serializer+0x00), loaded its vtable and tail-called
// `[vtbl + 0x90]` — `rw_vtbl::kScalarInt64` — because `IO::Serializer` did not
// declare an int64 scalar overload and routing eight bytes through the
// four-byte `int&` door would have been a save-format change. B34B_RULES §3
// sequenced the overload AFTER the 4a closing pass, because its body belongs in
// `src/engine/io/serializer_class.cpp`, which another agent owned. That agent
// has landed; the overload exists (`0x180035000`, registered by name and by
// address in `src/engine/io/serializer.cpp`); this is now one member call to
// the same slot. The trailing `0` was the ignored `const char* name`.
void System::Time::Serialize(::IO::Serializer& s) {
    s.Serialize(_ticks, nullptr);   // 0x180035000  +0x90 = rw_vtbl::kScalarInt64
}

// ============================================================
// System::Color
// ============================================================

// ENDGAME B.3.4b — the `System::Color` member BODY.
// ?Serialize@Color@System@@QEAAXAEAVSerializer@IO@@@Z  retail 0x1800c4150
// Slot +0x98 serializes the packed 4-byte `value`.
void System::Color::Serialize(::IO::Serializer& s) {
    s.Serialize(_value, nullptr);   // (0x98 = uint door)
}

// ============================================================
// System::DateTime / System::Configuration — String-returning functions.
// See the file header for why StrSlot is opaque and why these delegate to
// the DLL's own String routines rather than modeling System::String.
// ============================================================

// ENDGAME B.3.1 step 2: `struct StrSlot { void* p; };` is gone — the four
// bodies below hold `System::String` itself.

System::String System::DateTime::AsString() const {
    // Retail's own AsString CALLs the two exported siblings; B3R calls the
    // members by name (the doors were one-line delegations to them), and each
    // by-value result is constructed into its slot as the doors did.
    //
    // ENDGAME B.3.1 step 2. THE DECLARATION ORDER IS DELIBERATELY THE REVERSE
    // OF THE FILL ORDER: retail releases dateStr FIRST and timeStr second
    // (0x1800c5546 / 0x1800c5555), and reverse-order destruction of these two
    // reproduces that. They are separate one-reference allocations, so both
    // are freed here and the ORDER of the two MemFree calls is visible to the
    // allocator's free list — which is why it is matched rather than assumed
    // not to matter.
    System::String timeStr;
    System::String dateStr;
    ::new (&dateStr) System::String(DateAsString());   // 0x1800c56a0
    ::new (&timeStr) System::String(TimeAsString());   // 0x1800c55b0

    // ?Format@String@System@@SA?AV12@PEBDZZ (0x1800c9730), spelled as itself.
    //
    // B.3.4b: this was `new (ret) String(Format(...))` writing the caller's
    // hidden slot directly; it is now a `return` of the same prvalue, and the
    // DOOR does the placement-new. THE STRING TRAFFIC IS UNCHANGED: under
    // C++17 a prvalue initialises the object being created, so `Format`'s
    // result constructs straight into this function's return object and that
    // return object constructs straight into the door's `ret`. No temporary, no
    // extra AddRef/Release pair retail did not spend — the same guarantee the
    // old comment relied on, one frame further out.
    return System::String::Format("%s %s", dateStr._data, timeStr._data);

    // The two `?Release@String@System@@AEAAXXZ` calls are the destructors now.
}

// ============================================================================
// WAVE 22 LANE SYSBODY — DateAsString / TimeAsString
// ============================================================================
//   ?DateAsString@DateTime@System@@QEBA?AVString@2@XZ   0x1800c56a0  ord 2149
//   ?TimeAsString@DateTime@System@@QEBA?AVString@2@XZ   0x1800c55b0  ord 4131
//
// These are the two functions AsString above has been forwarding to since
// Wave 9. Registering them therefore lights up a route that already exists:
// AsString resolves both BY NAME through RT_EXPORT_ONCE (the registry), so the
// bodies below become the ones it calls the moment RtSystemValueRegister runs.
// That is a real caller, not a synthetic driver.
//
// THE HIDDEN RETURN SLOT IS REAL, AND GHIDRA DROPPED IT. `decompile_function`
// prints both of these as `void __thiscall DateAsString(DateTime *this)` — no
// second parameter and no return — which is the documented failure mode
// (reference_ghidra_c_output_vs_disassembly.md: whole arguments dropped). The
// disassembly is unambiguous:
//     1800c56a0  MOV qword ptr [RSP+0x10],RDX    ; RDX is a REAL argument
//     1800c56b6  MOV RBX,RDX                     ; kept across the whole body
//     1800c5773  MOV RAX,RBX / RET               ; and returned again in RAX
// so the signature is `String* f(const DateTime* this, String* ret)` — RCX =
// this, RDX = ret — and the callee returns the slot. Writing this as a C++
// method returning `String` by value is the wild write in
// reference_hidden_retslot_wild_write.md, which "works" because the right
// answer still lands in the right place while a second, uninitialised pointer
// is scribbled through. Spelled out as a parameter here, exactly as every
// other String-returning port in this tree does, and `StrSlot` is the same
// opaque 8-byte pass-through the rest of this file uses.
//
// THE TWO BODIES ARE THE SAME FUNCTION WITH TWO CONSTANTS. Instruction for
// instruction, DateAsString and TimeAsString differ in exactly two places:
//     1800c56fc  LEA EDX,[R9+0x1] / CALL [0x18014b130]   GetDateFormatW, flag 1
//     1800c560c  LEA EDX,[R9+0x2] / CALL [0x18014b138]   GetTimeFormatW, flag 2
// (1 = DATE_SHORTDATE, 2 = TIME_NOSECONDS; locale 0x400 = LOCALE_USER_DEFAULT
// in both, cchOut 0x100 in both.) They share one helper below rather than
// being transcribed twice, because two copies of a 20-line body is how the
// second one drifts.
//
// `this` IS A FILETIME. Retail's first instruction hands `this` straight to
// FileTimeToLocalFileTime, so System::DateTime is an 8-byte FILETIME and
// needs no layout of its own — the same reason the file header gives for
// AsString. Left as `const void*` for that reason.
//
// THE WIDESTRING TEMPORARY IS RELEASED INLINE, AND WE ALREADY OWN THAT.
// 0x1800c5742..0x1800c5773 is `WideString::~WideString` inlined: decrement the
// refcount at data-4, and on zero rewind to data-8 and MemFree. That is
// bit-for-bit the sequence `RtSystem_WideString_Dtor` (widestring.cpp, Wave 9
// lane FS) already implements, including its asymmetry, so this calls that
// rather than open-coding a third copy of the idiom.
// B.3.4b: still a file-static helper over the retslot, and still shared by the
// two members below, for the reason its own banner gives — two copies of a
// 20-line body is how the second one drifts. `self` is now typed.
static System::String* RtSystem_DateTime_FormatPart(const System::DateTime* self,
                                                    System::String* ret,
                                                    bool timePart) {
    // DELIBERATELY UNINITIALISED, and this is the one thing the sweep below
    // CAUGHT rather than confirmed. Retail's prologue reserves 0x250 bytes and
    // writes NOTHING into these three slots before the API calls
    // (0x1800c56a5..0x1800c56c1 is a PUSH, a SUB RSP,0x250 and two stores of a
    // frame cookie -- no zeroing of [RSP+0x40], [RSP+0x50] or [RSP+0x270]).
    // It then IGNORES both BOOL results. So when the conversion FAILS --
    // FILETIME 0, the 1601 epoch, is the easy case -- retail formats from an
    // uninitialised stack buffer and returns whatever was lying there.
    //
    // The first draft of this port wrote `FILETIME local{}; SYSTEMTIME st{};`
    // and the sweep reported a divergence at exactly that input: ours produced
    // one piece of mojibake and retail another. That is not a defect in either
    // body; it is two different stacks. Zero-initialising is nevertheless the
    // WRONG transcription -- a behaviour change on the failure path, invisible
    // on the success path, and exactly the sort of tidy-up this project's
    // rules exist to prevent. Left uninitialised so the port says what retail
    // does. Do NOT "fix" a -Wmaybe-uninitialized warning here by adding `{}`.
    //
    // The failure path is UNCOMPARABLE BY CONSTRUCTION rather than merely
    // untested, and the sweep says so explicitly instead of scoring it.
    // Phase D.4 THREADS E.7: the FILETIME / SYSTEMTIME locals (retail's [RSP+0x40]
    // and [RSP+0x270]) live in Platform::FormatLocalDateTime now, still
    // uninitialised there.
    // Unguarded, and faithful: retail passes `this` straight through with no
    // null test and ignores both BOOL results. A guard here would change what
    // the function returns rather than protect anything.
    // B.3.4b: `self` is a `System::DateTime*` now instead of a `void*`; its two
    // dwords ARE the FILETIME, low first, and the primitive reads them in place.
    static_assert(sizeof(System::DateTime) == 2 * sizeof(uint32_t),
                  "System::DateTime IS a FILETIME -- Value.h's layout pin");
    // 0x100 WCHARs, retail's [RSP+0x50] scratch -- uninitialised; since review F-152 the
    // Windows FormatLocalDateTime writes an empty string into it when the conversion fails.
    Platform::PathChar buf[Platform::kDateTimeChars];
    // FileTimeToLocalFileTime + FileTimeToSystemTime, then GetTimeFormatW(0x400, 2)
    // or GetDateFormatW(0x400, 1) into buf, 0x100 WCHARs.
    Platform::FormatLocalDateTime(&self->_lowDateTime, timePart, buf);

    // Retail zeroes the WideString slot before Set, because Set releases
    // whatever is there first (0x1800c570b: MOV qword [RSP+0x260],0): that
    // pair IS ??0WideString(const wchar_t*) (0x18000a6e0). AsUTF8 constructs
    // into the hidden return slot; ~WideString is the inlined release at
    // 0x1800c5742..0x1800c5773.
    {
        const System::WideString ws(buf);
        new (ret) System::String(ws.AsUTF8());
    }
    return ret;   // MOV RAX,RBX
}

// ============================================================================
// ENDGAME B.3.4b — the two `System::DateTime` member BODIES.
// ============================================================================
// They return `System::String` BY VALUE, which is what retail's source said;
// the helper above still fills a slot, so each member hands it a local and
// returns that.
//
// THE STRING TRAFFIC, STATED PRECISELY RATHER THAN OPTIMISTICALLY. The door's
// `new (ret) String(member())` is guaranteed by C++17 to build the member's
// return object AT `ret` — that part is the standard, not a hope. The member's
// local into its own return object is NRVO, which is permitted but NOT
// guaranteed; if the compiler declines it, the cost is exactly one MOVE
// construction of a `System::String`, which steals the pointer and leaves a
// null behind, so the moved-from local's destructor is a no-op and no refcount
// moves. Either way the allocator sees the same one allocation retail's body
// made. The local is default-constructed (data == nullptr) before
// `FormatPart` placement-constructs over it, which releases nothing because an
// empty String owns nothing.
System::String System::DateTime::DateAsString() const {
    System::String ret;
    RtSystem_DateTime_FormatPart(this, &ret, /*timePart=*/false);
    return ret;
}

System::String System::DateTime::TimeAsString() const {
    System::String ret;
    RtSystem_DateTime_FormatPart(this, &ret, /*timePart=*/true);
    return ret;
}

// ?CurrentBuildId@Configuration@System@@SA?AVString@2@XZ  retail 0x1800c4c60
//
// ⚠ ENDGAME B.3.4b SYSTEM (4c): THIS IS THE ONE PORTED `Configuration` MEMBER
// THAT DID **NOT** BECOME A MEMBER, AND IT IS A RECORDED OUTCOME, NOT A GAP.
// Every other String-returning body in this class and in `System::DateTime`
// placement-constructs the hidden return slot before writing it; this one does
// not — see the "NOTE, TRANSCRIBED NOT TIDIED" paragraph at the bottom of the
// body. A by-value member plus a placement-newing door would zero the slot
// FIRST, which is either the faithful reading (MSVC inlined `Set`, proved
// `data` null and folded the store away) or a behaviour change on a slot the
// caller did not zero. That question is open, this file already decided "left
// exactly as retail spells it", and B.3.4b does not overturn a prior decision
// on inference. The body stays the registered free door it is; the conversion
// is OWED once someone reads 0x1800c4c60's prologue and settles which it is.
// Recorded in src/engine/system/Configuration.h and in B34B_RULES section 1.5.
//
// Builds a 6-character stamp "<last 2 digits of year><2-letter month><day,
// zero-padded>" from the DLL's own compiled-in date string (e.g.
// "Sep 10 2017" -> "17Se10"), read via String::Set/Release so the DLL's own
// allocation is what we read characters out of, not a layout we assume.
System::String* RtSystem_Configuration_CurrentBuildId(System::String* ret) {
    static const char* const kMonths[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    // ENDGAME B.3.1 step 2: the Set (0x1800c95d0) and Release (0x1800c9870)
    // name doors are the class's own member and destructor now.
    System::String dateStr;
    dateStr.Set("Sep 10 2017");  // 0x180155c50 -- the DLL's own __DATE__-style literal
    const char* d = dateStr._data;  // "Sep 10 2017"

    char scratch[7] = "000000";
    scratch[0] = d[9];   // last two digits of the year ("17")
    scratch[1] = d[10];
    for (const char* month : kMonths) {
        if (d[0] == month[0] && d[1] == month[1] && d[2] == month[2]) {
            scratch[2] = month[0];
            scratch[3] = month[1];
            break;
        }
    }
    // Day-of-month digits overwrite the '0' padding only where retail's
    // source is actually a digit (a single-digit day is space-padded, e.g.
    // "Jan 5 2017", and that space must NOT clobber the zero).
    if (d[4] >= '0' && d[4] <= '9') scratch[4] = d[4];
    if (d[5] >= '0' && d[5] <= '9') scratch[5] = d[5];

    // NOTE, TRANSCRIBED NOT TIDIED: retail calls Set on `ret` WITHOUT zeroing
    // the hidden return slot first, so this is `Set` on the raw slot and not a
    // placement-construct. Set's own release runs on whatever the caller left
    // there. Left exactly as retail spells it — the caller is the one that
    // guarantees the slot; changing it here would be a behaviour change.
    ret->Set(scratch);
    // `release(&dateStr)` is dateStr's destructor now.
    return ret;
}

// ============================================================================
// ENDGAME B.3.4b SYSTEM (4c) — two of `System::Configuration`'s three `SA`
// statics move onto the class; the doors delegate.
// ============================================================================
// The class is declared in `src/engine/system/Configuration.h`, whose banner
// carries the "is it a class?" evidence, the 0x30-byte layout and the four rows
// that deliberately did NOT convert. `?CurrentBuildId@` (0x1800c4c60, below) is
// one of those four and stays exactly where it is — see the header, and the
// note on its own body.
//
// Both doors below are HIDDEN-RETSLOT doors: `ret` is the caller's RAW BYTES,
// so the member's prvalue is placement-CONSTRUCTED into it and the door returns
// that slot, which is retail's `MOV RAX,R14`.

// ?GetVersionString@Configuration@System@@SA?AVString@2@I_N@Z  retail 0x1800c4f90
// version is packed major<<24 | minor<<16 | patch<<8 | suffix. suffix in
// {0xfb..0xfe} appends a pre-release tag (short form when `detailed`, e.g.
// "R"/"P"/"A"/"B", else the full word); any other suffix (incl. retail's
// shipped 0xff) appends nothing. Text taken from the DLL's own .rdata
// literals at 0x180155c14..0x180155c40, transcribed to source for
// readability -- content verified byte-for-byte against inspect_memory.
System::String System::Configuration::GetVersionString(uint32_t version, bool detailed) {
    // ENDGAME B.3.1 step 2: ?Format@String@System@@SA?AV12@PEBDZZ (0x1800c9730)
    // spelled as itself. B.3.4b: the hidden return slot is the compiler's
    // business now, so this is an ordinary local and the DOOR places it.
    System::String ret = System::String::Format(
        "%i.%i.%i", version >> 24, (version >> 16) & 0xffu, (version >> 8) & 0xffu);

    const char* text = nullptr;
    switch (version & 0xffu) {
        case 0xfb: text = detailed ? "R" : " Prototype"; break;
        case 0xfc: text = detailed ? "P" : " Pre Alpha"; break;
        case 0xfd: text = detailed ? "A" : " Alpha"; break;
        case 0xfe: text = detailed ? "B" : " Beta"; break;
        default: return ret;
    }

    // ??YString@System@@QEAA?AV01@PEBD@Z (0x1800c9120) returns the new value BY
    // VALUE and the caller discards the copy; that discarded temporary IS
    // retail's `tmp`, and its destructor IS the 0x1800c9870 release below it.
    // Both lines collapse into this one (cookbook APPEND).
    ret += text;
    return ret;
}

// ?CurrentVersion@Configuration@System@@SA?AVString@2@XZ  retail 0x1800c4f40
System::String System::Configuration::CurrentVersion() {
    return GetVersionString(0x010007ffu, false);
}
