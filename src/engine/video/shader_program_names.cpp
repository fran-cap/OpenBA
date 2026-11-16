// ============================================================================
// src/engine/video/shader_program_names.cpp — WAVE 29 lane AVBODY
//
//   ?GetConstantName@ShaderProgram@Video@@QEBA?AVString@System@@H@Z  0x18004e4b0
//   ?GetTextureName@ShaderProgram@Video@@QEBA?AVString@System@@H@Z   0x18004e420
//
// ============================================================================
// THE RETURN IS `System::String` BY VALUE => A HIDDEN RDX RETURN SLOT
// ============================================================================
// `?AV` in `?AVString@System@@` is the by-VALUE spelling. `AEBV` would be
// reference-to-const. The difference is the whole defect class: MSVC returns a
// non-trivially-copyable class through a caller-allocated slot passed in RDX,
// so the real register layout is
//
//     RCX = this, RDX = String* ret, R8D = index
//
// An `fn_*` typedef that omits the slot (`String (*)(void*, int32_t)`) puts the
// index in RDX and returns RAX as if it were the String. It COMPILES, and it
// RETURNS THE RIGHT ANSWER, because RAX comes back holding the slot address
// either way -- while having written 8 bytes through whatever the caller had in
// RDX. That self-consistent wild write is the trap this
// lane was scoped around.
//
// TWO INDEPENDENT CONFIRMATIONS that the slot is real, neither of them "the
// mangling says so":
//   1. The bytes READ RDX three times: `MOV [RSP+0x10],RDX` (spill to home
//      space), `MOV [RDX],R9` (the store), `MOV RAX,RDX` (the return). A body
//      with no return slot never touches RDX.
//   2. Lane IOLANE's discriminator, applied to these two by that lane and by
//      this one independently: "does the body READ RDX at all?" It does.
//      IOLANE's own four bodies do NOT (two are a bare `LEA RAX,[RCX+0x58]`),
//      which is why the hidden-slot half of the shared subject is this lane's
//      alone. Recorded because the kickoff and both briefs described the
//      problem as shared, and MEASURED it is not.
//
// ============================================================================
// THE TWO BODIES DIFFER IN EXACTLY ONE BYTE, PROVED MECHANICALLY
// ============================================================================
// Not read by eye. Both extents are 0x44 bytes, derived independently of
// .pdata (first `CC CC` inter-function padding at +0x44 in both cases, which
// agrees with Ghidra's bound -- two derivations, kickoff warning 16), and
// neither body contains a single rel32 or RIP-relative field, so a RAW byte
// diff is SOUND here rather than reporting the ~23% noise that rel32 and
// RIP-relative distances add between two addresses. The precondition was checked
// by the script, not assumed.
//
//   A GetConstantName 48895424104883ec18...4803 41 30 4c890a488b08...
//   B GetTextureName  48895424104883ec18...4803 41 20 4c890a488b08...
//   differing byte offsets: 1
//     +0x23   A=0x30  B=0x20        <-- the disp8 of ADD RAX,[RCX+disp]
//
// So: SAME CODE over a DIFFERENT MEMBER ARRAY. `_constants` at +0x30,
// `_textures` at +0x20. Any second differing byte would have refuted that, and
// port-by-analogy is only legitimate because the diff was run.
//
// ============================================================================
// THE BODY, AND THE TWO WRITE WIDTHS THAT DIFFER
// ============================================================================
//   18004e4b0: MOV [RSP+0x10],RDX      ; ret slot -> home space (not observable)
//   18004e4b5: SUB RSP,0x18
//   18004e4b9: MOV [RSP+0x8],-0x2      ; MSVC EH frame marker (not reproduced)
//   18004e4c2: XOR R9D,R9D
//   18004e4c5: MOV [RSP],R9D           ; EH state 0 (not reproduced)
//   18004e4c9: MOVSXD RAX,R8D          ; *** SIGN-extended, see below
//   18004e4cc: SHL RAX,0x4             ; * 16 -- element stride
//   18004e4d0: ADD RAX,[RCX+0x30]      ; + this->_constants
//   18004e4d4: MOV [RDX],R9            ; *** QWORD store of 0, UNCONDITIONAL
//   18004e4d7: MOV RCX,[RAX]           ; element[index].name  (char*)
//   18004e4da: TEST RCX,RCX
//   18004e4dd: JZ  18004e4e5
//   18004e4df: MOV [RDX],RCX           ; *** QWORD store
//   18004e4e2: INC dword [RCX + -0x4]  ; *** DWORD increment -- NOT a qword
//   18004e4e5: MOV [RSP],0x1           ; EH state 1 (not reproduced)
//   18004e4ec: MOV RAX,RDX
//   18004e4ef: ADD RSP,0x18
//   18004e4f3: RET
//
// WRITTEN-OPERAND SET (kickoff §2.2 -- compared as a SET over a derived extent,
// which is how Camera::operator='s byte-vs-dword and EdgeMeshInstance's 1-of-19
// were both caught):
//     [RDX]      QWORD, written TWICE (0 then the pointer)
//     [RCX-0x4]  DWORD, written ONCE and only on the non-null path
// The widths DIFFER, and that is load-bearing in both directions:
//   * writing the refcount as a QWORD would clobber `chars[0..3]` of the
//     string payload, which lives at data+0 immediately after it;
//   * writing the out-slot as a DWORD would leave the high half of the
//     caller's String holding whatever was there before.
// src/engine/system/String.h already spells the refcount `int32_t` and
// already provides StringRefCount(data) == data[-1] as an int32_t&, derived in
// an earlier wave from different functions. This body agrees with it, which is
// a second derivation rather than a restatement.
//
// THE UNCONDITIONAL `*ret = nullptr` IS NOT DEAD. It is the default-construct
// of the String temporary, and it is what makes the null-name case return the
// EMPTY string rather than leaving the caller's slot untouched. A port that
// only assigned on the non-null path would pass every test whose `ret` happened
// to start zeroed -- which is every test anyone would write by hand.
//
// MOVSXD, NOT A ZERO EXTENSION. Ghidra's decompiled C prints a MOVSXD as an
// unsigned widening, so this is
// read off the DISASSEMBLY. A negative index walks BACKWARD from the array
// base in retail, and does so here. `(uint32_t)index * 16` would read at
// +0xFFFFFFF0 instead of -0x10 -- a 64 GB-away wild read that a non-negative
// sweep can never distinguish.
//
// NO NULL CHECK ON `this` OR ON THE ARRAY BASE, and that is transcribed rather
// than "fixed" (CLAUDE.md: retail's unguarded derefs are load-bearing). The
// element's NAME is checked because retail checks it; nothing else is.
//
// ============================================================================
// THE LIVE ROUTE
// ============================================================================
// `?GetConstantName@Material@Graphics@@` / `?GetTextureName@...` are forwarders
// onto these two (src/engine/graphics/wave28_lane_gfxbody.cpp), so these get
// exercised by anything that reads a material's constant names.
// ============================================================================

// ============================================================================
// ENDGAME B.3.5 BLOCK B — THE TWO BODIES MOVED INTO THE MEMBERS, AND THE HIDDEN
// SLOT IS NOW THE LANGUAGE'S
// ============================================================================
// `Video::ShaderProgram::GetTextureName` / `GetConstantName` are members of the
// real class (src/engine/video/ShaderProgram.h) and RETURN `System::String`
// BY VALUE. GCC's Microsoft-ABI lowering of a by-value class return produces the
// SAME hidden slot in RDX and the SAME `MOV RAX,RDX` that retail's `?AV`
// mangling describes — which is the point of using a real class here (RULE
// B5-12) rather than keeping the slot hand-written forever.
//
// The attendance counters are in the members, with the branch they count: a
// call count alone cannot see the non-null arm.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#include "system/string_internal.h"
#include "video/ShaderProgram.h"

namespace rtvidshadername {

using RtSystem::StringRefCount;

namespace {

// ---- attendance ------------------------------------------------------------
// Counted at the CALL and, separately, at the BRANCH that does the work: a
// call count alone cannot say whether the non-null path ever ran, since a body
// can run every time down the branch that does no work.
unsigned long long g_constCalls = 0;
unsigned long long g_constHits  = 0;   // BRANCH: name was non-null
unsigned long long g_texCalls   = 0;
unsigned long long g_texHits    = 0;   // BRANCH: name was non-null

// `MOVSXD RAX,R8D / SHL RAX,0x4 / ADD RAX,[RCX+disp] / MOV RCX,[RAX]`.
// int64_t, so the sign extension is the C-level MOVSXD.
//
// B.3.5: the displacement argument is GONE — the two members name their own
// array member instead, and `Parameter` is a real 0x10-byte type, so the
// `SHL RAX,0x4` is now `array._data[index]` and the stride is `sizeof(Parameter)`
// (asserted 0x10 in the header). The MOVSXD survives as the `int64_t` cast: a
// NEGATIVE index walks BACKWARD from the array base in retail and must here too
// — `(uint32_t)index * 16` would read 64 GB away. Ghidra's C prints a MOVSXD as
// an unsigned widening, which is why this was read off the disassembly.
inline char* NameAt(const decltype(Video::ShaderProgram::_textures)& arr,
                    int32_t index) {
    const Video::ShaderProgram::Parameter* const e =
        arr._data + static_cast<int64_t>(index);
    return e->name._data;
}

}  // namespace

// ---- attendance, printed UNCONDITIONALLY -----------------------------------
// Wave 28 defect #7 was that a lane's *Summary() was written and nothing
// called it. RtGraphicsWave28GfxbodySummary() is called from src/app/main.cpp
// and prints the g_getConstNameCalls counter that reports these two bodies'
// attendance through Material::GetConstantName. This report does
// not rely on an aggregator calling it: a static destructor runs it, the same
// mechanism that actually produced Wave 28's one measured figure (799 calls,
// audio_scene_update.cpp's UpdateReport).
//
// It prints even when every counter is 0, deliberately. A report guarded by
// `if (count)` makes "never ran" and "ran and counted zero" the same silence,
// which is the failure mode that made defect #7 invisible in the first place.
namespace {
struct NameGetterReport {
    ~NameGetterReport() {
        printf("[AVBODY29] ShaderProgram ATTENDANCE: "
               "GetConstantName calls=%llu (nameNonNull=%llu) ; "
               "GetTextureName calls=%llu (nameNonNull=%llu)\n",
               g_constCalls, g_constHits, g_texCalls, g_texHits);
        fflush(stdout);
    }
};
NameGetterReport g_nameGetterReport;
}  // namespace

}  // namespace rtvidshadername

// ============================================================================
// THE MEMBER BODIES — `Video::ShaderProgram::Get{Texture,Constant}Name`.
//
// At `namespace Video` scope and OUTSIDE `namespace rtvidshadername`, per
// B34_BRIEFS A.2: a member of `Video::X` cannot be defined inside a sibling
// namespace. The helpers are reached as `rtvidshadername::…` — an anonymous
// namespace's members are members of the ENCLOSING namespace's declarative
// region, so that spelling works from here in the same translation unit.
//
// THE UNCONDITIONAL `*ret = nullptr` IS NOT DEAD, and it is the default
// construction of the return object: `System::String name;` below. It is what
// makes the null-name case return the EMPTY string rather than leaving the
// caller's slot untouched. A port that only assigned on the non-null path would
// pass every test whose `ret` happened to start zeroed.
//
// `Adopt` is the AddRef half of retail's copy constructor — `_data = src;
// ++src[-4]` — i.e. exactly the `MOV [RDX],RCX ; INC dword [RCX-0x4]` pair, with
// the DWORD width the disassembly shows and src/engine/system/String.h
// already spells `int32_t`. It is reached through `StringPrivateAccess` because
// retail's own bodies write String's privates the same way; assigning
// `System::String(name)` instead would run the `const char*` constructor and
// ALLOCATE A COPY of the characters, which is a different function.
//
// NO NULL CHECK ON `this` OR ON THE ARRAY BASE, transcribed rather than "fixed"
// (CLAUDE.md: retail's unguarded derefs are load-bearing). The element's NAME is
// checked because retail checks it; nothing else is.
// ============================================================================
namespace Video {

System::String ShaderProgram::GetConstantName(int32_t index) const {
    ++rtvidshadername::g_constCalls;
    System::String name;                                   // MOV [RDX],R9
    char* const p = rtvidshadername::NameAt(_constants, index);
    if (p) {                                               // TEST RCX,RCX / JZ
        ++rtvidshadername::g_constHits;
        System::StringPrivateAccess::Adopt(name, p);       // MOV [RDX],RCX ; INC [RCX-4]
    }
    return name;
}

// The `_textures`/`_constants` member is the ONLY difference between the two —
// the byte diff above proved it is the one differing disp8 and nothing else.
System::String ShaderProgram::GetTextureName(int32_t index) const {
    ++rtvidshadername::g_texCalls;
    System::String name;
    char* const p = rtvidshadername::NameAt(_textures, index);
    if (p) {
        ++rtvidshadername::g_texHits;
        System::StringPrivateAccess::Adopt(name, p);
    }
    return name;
}

}  // namespace Video
