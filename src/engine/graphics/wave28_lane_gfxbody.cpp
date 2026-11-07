// ============================================================================
// src/engine/graphics/wave28_lane_gfxbody.cpp — WAVE 28 LANE GFXBODY
// The ten remaining named Graphics BODY rows of the census gap.
// ============================================================================
//
// Every one of these ten is a CALLEE that WAVE 27 lane GFXBIG's three bodies
// introduced into the `called` column (WAVE27_LANE_GFXBIG.md §11.1). They were
// always in the DLL and always unported; porting a body that names them is what
// made them visible. Each already has a name door in the tree — an
// `RT_EXPORT_ONCE` inside `wave27_lane_gfxbig.cpp` — so the registrations at
// the bottom of this file are LIVE the moment they land, not inert.
//
//   | Export                                        | Retail      | Ord  | Bytes |
//   |-----------------------------------------------|-------------|------|-------|
//   | ?GetConstantName@Material@Graphics@@           | 0x180102830 | 2506 |    72 |
//   | ?GetTextureName@Material@Graphics@@            | 0x180102900 | 2970 |    72 |
//   | ?FindConstant@MaterialInstance@Graphics@@      | 0x180102f40 | 2343 |   211 |
//   | ?FindTexture@MaterialInstance@Graphics@@       | 0x180103020 | 2361 |   211 |
//   | ?ApplyEvents@Animation@Graphics@@              | 0x1800fa9a0 | 1605 |   242 |
//   | ?BeginDrawGroup@Renderer@Graphics@@            | 0x180104fd0 | 1640 |   314 |
//   | ?UpdateRootOnly@AnimationState@Graphics@@      | 0x1800fc870 | 4525 |   934 |
//   | ?CopyInstancedToUnique@AnimationState@         | 0x1800fd4c0 | 1868 |  1127 |
//   | ?UpdateNodes@AnimationState@Graphics@@         | 0x1800fcc20 | 4522 |  2191 |
//   | ?AddCulledDrawItems@Scene@Graphics@@           | 0x1801017a0 | 1543 |  2950 |
//
// ADDRESSES AND ORDINALS: `pefile` over the export directory of the shipped
// C:\GOG Games\Banished\Runtime-x64.dll, read before a line of this file was
// written (reference_resolveexport_phantom_names.md).
//
// EXTENTS: NOT quoted from a single `.pdata` row, which is a LOWER bound
// (reference_pdata_is_a_lower_bound_on_function_extent.md). Five of these ten
// are assembled from CHAINED fragments and the first fragment understates them
// badly — `UpdateNodes` by 17x (126 bytes of a 2191-byte body). The extents
// above come from walking `.pdata` forward while `UNW_FLAG_CHAININFO` (0x4) is
// set on the successor AND its chained parent points back into this body:
//
//   UpdateNodes            0x1800fcc20..0x1800fd4af   5 fragments   2191
//   UpdateRootOnly         0x1800fc870..0x1800fcc16   3 fragments    934
//   CopyInstancedToUnique  0x1800fd4c0..0x1800fd927   3 fragments   1127
//   ApplyEvents            0x1800fa9a0..0x1800faa92   3 fragments    242
//   the other six          single fragment
//
// *** THE SOURCE OF TRUTH FOR EVERY BYTE BELOW IS objdump, NOT GHIDRA. ***
// Same rule lane GFXBIG adopted after `disassemble_function` silently returned
// a different function's listing under the addresses that had been asked for.
// No listing behind this file came from Ghidra at all.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "graphics/Animation.h"        // Graphics::Animation -- block GC
#include "system/Value.h"             // System::Time, ditto
#include "graphics/AnimationState.h"
#include "graphics/AnimationManager.h"   // B.3.9: AnimationManager::_singleton
#include "graphics/AnimationGroup.h"     // B.4: AnimationGroup::GetAnimation
#include "graphics/Camera.h"
#include "graphics/Material.h"
#include "video/ShaderProgram.h"   // ENDGAME B.3.5 caller batch: Video::ShaderProgram
#include "video/VideoProgram.h"    // B.4: Video::VideoProgram::_pixelProgram
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "../collection/array_internal.h"
#include "system/string_internal.h"

// System::Timer::_secondsPerTick (0x1801fe728). NOT a literal that can be
// lifted: RVA 0x1fe728 lies BEYOND `.data`'s SizeOfRawData, i.e. in the
// uninitialised tail, so there are no bytes in the file to read and a port that
// baked in "the constant" would have baked in ZERO (WAVE27_LANE_GFXBIG.md §4).
// ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now (redirect in SYS's
// *PortInit); the `engine_funcs.cpp` global this used to read is dropped.

namespace rtgfxbody28 {

using RtGraphics::MaterialConstant;
using RtGraphics::MaterialTexture;

// ---------------------------------------------------------------------------
// Attendance. Counts the DOOR and, separately, the BRANCH that does the work:
// `calls=0` and `calls=N with the interesting arm never taken` are different
// findings and an entry counter cannot tell them apart
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md).
// ---------------------------------------------------------------------------
void Attend(const char* what, unsigned long long& n) {
    ++n;
    if (n == 1 || n == 10 || n == 1000 || (n % 100000) == 0)
        printf("[GFXBODY28] %s calls=%llu\n", what, n);
}

unsigned long long g_getConstNameCalls = 0;
unsigned long long g_getTexNameCalls   = 0;
unsigned long long g_findConstCalls    = 0;
unsigned long long g_findConstHits     = 0;   // BRANCH: a name actually matched
unsigned long long g_findTexCalls      = 0;
unsigned long long g_findTexHits       = 0;   // BRANCH: a name actually matched
unsigned long long g_applyEventsCalls  = 0;
unsigned long long g_applyEventsFired  = 0;   // RARE ARM: an event dispatched
unsigned long long g_beginGroupCalls   = 0;
unsigned long long g_beginGroupAppends = 0;   // BRANCH: the grow path, not reuse
unsigned long long g_updateRootCalls   = 0;
unsigned long long g_updateRootLayers  = 0;   // BRANCH: layers actually blended
unsigned long long g_cameraAssignCalls = 0;

// ============================================================================
// §1.  Material::GetConstantName / Material::GetTextureName
//      retail 0x180102830 (ord 2506) and 0x180102900 (ord 2970), 72 bytes each.
// ============================================================================
// Both are four-line forwarders onto Video::ShaderProgram:
//
//   180102830: MOV [RSP+0x10],RDX          ; the hidden return slot, spilled
//   180102843: MOV RBX,RDX
//   180102846: MOV RAX,[RCX+0x28]          ; Material::_passes._data
//   180102852: MOVSXD RCX,R8D              ; pass index, SIGN-extended
//   180102855: MOV RCX,[RAX+RCX*8]         ; the pass object
//   180102859: MOV R8D,R9D                 ; index becomes ShaderProgram's arg
//   18010285c: MOV RCX,[RCX+0x30]          ; pass->shaderProgram
//   180102860: CALL 0x18004e4b0            ; ?GetConstantName@ShaderProgram@Video@@
//   18010286e: MOV RAX,RBX                 ; return the slot address
//
// THE RETURN IS `System::String` BY VALUE, SO RDX IS A HIDDEN RETURN SLOT, and
// it is passed STRAIGHT THROUGH to the callee — RDX is never written between
// entry and the call. An `fn_*` typedef that omitted the slot would be a wild
// write that still returned the right answer, because RAX comes back holding
// the slot address either way (reference_hidden_retslot_wild_write.md). Both
// mangled names spell `?AVString@System@@` as the return, so this is read off
// the EXPORT TABLE, not guessed from a call site.
//
// THE TWO DIFFER IN EXACTLY ONE FIELD: the callee. 0x18004e4b0 is
// `?GetConstantName@ShaderProgram@Video@@QEBA?AVString@System@@H@Z` (ord 2507)
// and 0x18004e420 is `?GetTextureName@ShaderProgram@Video@@…` (ord 2971) —
// both named from the export directory rather than inferred from the
// surrounding symmetry. Port-by-analogy is what warning 3 of the kickoff is
// about; these two were each disassembled in full and then compared.
//
// NO NULL CHECK ANYWHERE, and that is not an oversight in retail: the
// NEIGHBOURING export `?GetConstantCount@Material@Graphics@@` (0x180102880)
// walks the SAME chain and DOES test `pass->shaderProgram` for null before
// dereferencing it. So the absence here is a deliberate asymmetry in the
// shipped code, and per the standing rule it is transcribed rather than
// "fixed" — a guard would skip the behaviour, not restore it.
//
// `MOV [RSP+0x20],0` / `MOV [RSP+0x20],1` around the call are the MSVC EH state
// index for the String temporary; they have no observable effect and are not
// reproduced.
// ----------------------------------------------------------------------------
namespace {

// RCX = ShaderProgram*, RDX = hidden return slot, R8D = index.
// ENDGAME B.3.5 caller batch: `ShaderGetNameFn` is gone with the two retslot
// doors it typed; both are member calls placement-new'd into the caller's slot.

// `MOV RAX,[RCX+0x28] / MOVSXD RCX,<idx> / MOV RCX,[RAX+RCX*8]` — the pass
// pointer array. MOVSXD, not a zero extension: Ghidra's C prints a MOVSXD as an
// unsigned widening and that has cost this project real defects
// (reference_ghidra_c_output_vs_disassembly.md). A negative pass index indexes
// BACKWARD in retail and does so here too.
//
// B.4: `Material::_passes` (+0x28) is `Array<ExternalPtr<Video::VideoProgram>>`
// and `MOV RCX,[RCX+0x30]` is the pass's `_pixelProgram` -- the pass object's
// pixel `Video::ShaderProgram`.
inline const Video::PixelProgram* PassShaderProgram(const Graphics::Material* material,
                                                    int32_t pass) {
    return material->_passes._data[pass].Get()->_pixelProgram.Get();
}

}  // namespace

// ============================================================================
// §2.  MaterialInstance::FindConstant / FindTexture
//      retail 0x180102f40 (ord 2343) and 0x180103020 (ord 2361), 211 bytes each.
// ============================================================================
// A linear search of the instance's own constant/texture array for an element
// whose NAME equals the argument, returning the index or -1.
//
// THE COMPARISON IS AN INLINED System::String EQUALITY AND IT HAS TWO HALVES.
// The first is a LENGTH TEST off the block header (`MOV ECX,[R8-0x8]` at
// 0x180102f6f — `data[-2]` is the length, `data[-1]` the refcount), with the
// null pointer standing in for length 0 on BOTH sides. Only when the lengths
// match does it fall into the second half, an inlined `strcmp`.
//
// *** THE LENGTH TEST IS LOAD-BEARING AND IT IS WHAT MAKES THIS SAFE TO PORT. ***
// reference_uniform_population_is_not_a_verification.md records a ported inline
// strcmp that returned 0 for every distinct pair sharing a first byte, and a
// shared BST find then matched everything. The loop below was therefore traced
// by hand against the bytes rather than recognised:
//
//   180102f9f: MOVZX ECX,BYTE PTR [R8]        ; c = a[0]
//   180102fa5: CMP CL,BYTE PTR [R9]           ; a[0] vs b[0]
//   180102fa8: JNE diff                       ; with RDX (the index) still 0
//   180102faa: MOV R10,R8 / MOV RAX,R9 / SUB R10,R9   ; R10 = a - b, RAX = &b[i]
//   180102fb3: TEST CL,CL      / JE diff      ; a[i] == 0  -> equal
//   180102fb7: CMP BYTE PTR [RAX],0 / JE diff ; b[i] == 0  -> differ
//   180102fbc: MOVZX ECX,BYTE PTR [R10+RAX+1] ; c = a[i+1]
//   180102fc2: INC RAX / INC RDX              ; i++
//   180102fc8: CMP CL,BYTE PTR [RAX] / JE loop
//   diff:      MOVSX ECX,[RDX+R8] ; MOVSX EAX,[RDX+R9] ; SUB ECX,EAX
//
// so the result is `(signed char)a[i] - (signed char)b[i]` at the first index
// where they differ or where `a[i]` is NUL — the textbook answer, but read out
// rather than assumed. THE SIGNED WIDENING MATTERS: a high-bit byte compares as
// NEGATIVE, which an `unsigned char` port would get backwards. Only the
// `== 0` verdict is consumed here, so the sign cannot change THIS body's
// answer; it is transcribed anyway because the shape is the thing a future
// reader copies.
//
// TWO EARLY EXITS INSIDE THE MATCH ARM, both taken from the bytes:
//   * `TEST R8,R8 / JE found` (0x180102f88) — a NULL element name whose length
//     already matched (i.e. the query is also empty) is a MATCH.
//   * `CMP DWORD PTR [R8-0x8],0 / JE found` (0x180102f8d) — a zero-length
//     element name, same.
// And one arm that CANNOT fire: `TEST R9,R9 / JNE ... / LEA ECX,[R9+1]`
// (0x180102f94) makes a null QUERY compare unequal — but a null query has
// length 0, and we are past a length test against a non-empty element name, so
// control never arrives there. Transcribed, and named as unreachable rather
// than quietly dropped.
//
// THE TWO BODIES DIFFER IN THREE FIELDS AND NOTHING ELSE, all read from the
// bytes rather than from the element types:
//        FindConstant                      FindTexture
//   size  [RCX+0x48] = _constants._size    [RCX+0x58] = _textures._size
//   data  [RCX+0x40] = _constants._data    [RCX+0x50] = _textures._data
//   step  ADD R11,0x18                     ADD R11,0x10
// which is `Material.h`'s existing layout exactly, unchanged: MaterialConstant
// is stride 0x18 at +0x40 and MaterialTexture stride 0x10 at +0x50. The one
// thing this lane ADDS to that header is the meaning of MaterialConstant +0x00,
// which lane F left as an unread `_head[8]`: it is the entry's NAME, the same
// `System::String::data` MaterialTexture carries at +0x00.
// ----------------------------------------------------------------------------
namespace {

// The inlined comparison, written once and used by both bodies. `a` is the
// ELEMENT name and `b` the QUERY, in retail's register order (R8 / R9).
bool StringEqualsInline(const char* a, const char* b) {
    const int32_t alen = a ? RtSystem::StringLength(a) : 0;   // [R8-0x8] or 0
    const int32_t blen = b ? RtSystem::StringLength(b) : 0;   // [R9-0x8] or 0
    if (alen != blen) return false;                           // CMP ECX,EAX / JNE

    if (a == nullptr) return true;                            // TEST R8,R8 / JE found
    if (RtSystem::StringLength(a) == 0) return true;          // CMP [R8-8],0 / JE found
    if (b == nullptr) return false;                           // UNREACHABLE, see banner

    int64_t i = 0;
    int32_t c = static_cast<unsigned char>(a[0]);
    if (c == static_cast<unsigned char>(b[0])) {
        while (c != 0 && b[i] != 0) {
            c = static_cast<unsigned char>(a[i + 1]);
            ++i;
            if (c != static_cast<unsigned char>(b[i])) break;
        }
    }
    // MOVSX, not MOVZX — a signed char subtraction.
    const int32_t d = static_cast<int32_t>(static_cast<signed char>(a[i])) -
                      static_cast<int32_t>(static_cast<signed char>(b[i]));
    return d == 0;
}

// Each element's leading `System::String` data pointer (+0x00).
inline const char* ElementName(const MaterialTexture& t) { return t.name._data; }
inline const char* ElementName(const MaterialConstant& c) { return c.name._data; }

// The shared loop. The stride is the element's size (`ADD R11,<n>` at the
// bottom: 0x18 for a constant, 0x10 for a texture).
template <class Element>
int32_t FindByName(const Element* data, int32_t size, const System::String* query) {
    const int64_t n = size;                       // MOVSXD RSI,<size>
    if (n <= 0) return -1;                        // TEST RSI,RSI / JLE  -> OR EAX,-1

    const char* const b = query->_data;           // MOV R9,[RDX]
    const Element* p = data;                      // MOV R11,[RCX+0x40 / 0x50]
    for (int64_t i = 0; i < n; ++i) {
        if (StringEqualsInline(ElementName(*p), b))
            return static_cast<int32_t>(i);       // MOV EAX,EDI
        ++p;
    }
    return -1;                                    // OR EAX,0xffffffff
}

}  // namespace

// ============================================================================
// §3.  Animation::ApplyEvents
//      retail 0x1800fa9a0, ordinal 1605, 242 bytes across 3 chained fragments.
// ============================================================================
// `?ApplyEvents@Animation@Graphics@@QEBAXAEBVTransform@Math@@AEBVTime@System@@1@Z`
// RCX = this, RDX = const Math::Transform&, R8 = const System::Time& from,
// R9 = const System::Time& to.
//
// *** `Graphics::Animation` IS THE 0x70 CLIP RECORD AnimationState::Update
// WALKS, AND THAT IS MEASURED HERE, NOT ASSUMED. *** Wave 27 lane GFXBIG built
// `AnimationClipPrefix` entirely from the OTHER side — from Update's reads of
// `group->clips[i]` — and named +0x30 frameFirst, +0x34 frameLast, +0x48
// loopScale, +0x50 frameRate (SINCE CORRECTED to frameCount -- see
// AnimationState.h, 2026-09-12), +0x58 duration, +0x08 eventCount. This body's
// `this` reads EVERY ONE OF THOSE SIX at exactly those offsets and uses them in
// the same roles. Two bodies reached from opposite directions agreeing on six
// offsets is a far stronger statement than either alone, and it is the reason
// this file reuses `AnimationClipPrefix` rather than minting a new struct.
//
// The one field it ADDS is +0x00: lane GFXBIG left it as `_head_00[8]` with "no
// reader in Update". It is the EVENT ARRAY BASE (`MOV RDX,[R15]` at
// 0x1800fa9f0), walked with stride 0x10 against the +0x08 count. So +0x00/+0x08
// are a `Collection::Array<AnimationEventEntry>` data/size pair.
//
// THE EVENT TIME IS COMPUTED, NOT STORED, and the float association order is
// retail's exactly:
//
//   1800fa9f9: CVTDQ2PS XMM1, (int)clip->frameCount         ; +0x50, an INT
//   1800fa9fc: EAX = entry.frame - clip->frameFirst         ; both INTs
//   1800faa08: EAX2 = clip->frameLast - clip->frameFirst
//   1800faa1a: DIVSS XMM2, XMM0                             ; (a) / (b)
//   1800faa1e: MULSS XMM2, XMM1                             ;     * frameCount
//   1800faa22: MULSS XMM2, [R15+0x48]                       ;     * loopScale
//   1800faa28: CVTPS2PD XMM0, XMM2                          ; float -> double
//   1800faa2b: DIVSD  XMM0, [0x1801fe728]                   ; / _secondsPerTick
//   1800faa33: CVTTSD2SI RAX, XMM0                          ; TRUNCATE to int64
//
// `((num/den) * rate) * loopScale` is the SAME three-instruction expression, in
// the same order, that AnimationState::Update's loop-restart offset uses
// (WAVE27_LANE_GFXBIG.md §8.3). Re-associating it is algebraically identical
// and numerically different, so it is written with explicit parentheses.
// THE SINGLE-PRECISION STEP IS KEPT SINGLE-PRECISION: the divide and both
// multiplies are DIVSS/MULSS and only then does CVTPS2PD widen. Doing the whole
// chain in `double` would agree to more digits and would be wrong.
//
// THERE IS NO DIVISION GUARD. `frameLast == frameFirst` divides by zero and
// produces an infinity that CVTTSD2SI turns into INT64_MIN — in retail too.
// Per the standing rule that is a data defect retail would also produce, and a
// guard would hide it rather than restore behaviour.
//
// THE WINDOW TEST HAS TWO ARMS AND THE SECOND IS THE LOOP-WRAP ARM:
//
//   1800faa38: CMP RAX,[R8]  / JL  A       ; t <  from        -> try the wrap arm
//   1800faa3d: CMP RAX,[R12] / JL  FIRE    ; from <= t < to   -> fire
//   A:
//   1800faa43: CMP [R12],RBX / JL  SKIP    ; to < clip->duration -> no wrap
//   1800faa49: CMP RAX,RDI   / JL  SKIP    ; t <  (from - duration)
//   1800faa4e: CMP RAX,RSI   / JGE SKIP    ; t >= (to   - duration)
//                                          ; otherwise         -> fire
//
// RDI and RSI are computed ONCE in the prologue as `*from - duration` and
// `*to - duration` (0x1800fa9c7/0x1800fa9ca), so the wrap arm asks whether the
// event falls in the window SHIFTED BACK by one clip length — i.e. whether the
// playhead has wrapped past the end since the last call. `duration` is read
// once, before the loop, from +0x58.
//
// THE DISPATCH IS A VIRTUAL CALL ON SLOT 8 (`CALL QWORD PTR [RAX+0x40]`,
// 0x1800faa5e) with RCX = the event object and RDX = the Transform& — the
// SECOND parameter is forwarded and the two System::Time& are NOT. Ghidra's C
// drops whole arguments at a mistyped vtable call
// (reference_ghidra_c_output_vs_disassembly.md); this was read off the
// instruction, where RDX is reloaded from R13 (the spilled RDX) immediately
// before the call and nothing writes R8/R9.
//
// AND R8 IS RELOADED AFTER THE CALL (`MOV R8,[RSP+0x60]`, 0x1800faa61) because
// R8 is volatile. The port keeps `from` in a local, which is the same thing;
// named here so the reload is not read as a missing store.
// ----------------------------------------------------------------------------
namespace {

// One entry of the clip's event array, stride 0x10, is `Animation::Event`
// (Animation.h): `_frame` (+0x00, `MOV EAX,[R14+RDX]`) and `_event`
// (+0x08, `MOV RCX,[R14+RDX+0x8]`).

}  // namespace

// ============================================================================
// §4.  Renderer::BeginDrawGroup
//      retail 0x180104fd0, ordinal 1640, 314 bytes, single .pdata fragment.
// ============================================================================
// `?BeginDrawGroup@Renderer@Graphics@@QEAAHPEBVRenderBuffer@Video@@AEBVCamera@2@1HI@Z`
// RCX = this, RDX = Video::RenderBuffer*, R8 = const Camera& (the DRAW camera),
// R9 = const Camera& (the second `1` in the mangled name — a back-reference to
// the same parameter TYPE, not the same argument), then TWO STACK ARGUMENTS.
//
// THE TWO STACK ARGUMENTS WERE DERIVED, NOT GUESSED. The prologue is
// `PUSH RDI` + `SUB RSP,0x230` = 0x238, so the caller's shadow space starts at
// RSP+0x238 and arguments 5 and 6 live at RSP+0x260 and RSP+0x268. Retail reads
// exactly those two addresses (0x18010502b and 0x180105039) and nothing else
// above RSP+0x238 — which is the home-slot arithmetic check lane GFXBIG used on
// its nine-argument body, re-run here. The mangled name spells them `H` (int)
// and `I` (unsigned), which is the independent second derivation. No hidden
// return slot: the return is `H`, an int in EAX, so the register arguments are
// not shifted.
//
// WHAT IT RETURNS IS THE **PREVIOUS** GROUP INDEX, read BEFORE anything is
// written (`MOV EBP,[RCX+0x34]` at 0x180104ff6, returned by `MOV EAX,EBP` at
// 0x1801050ef). Not the new one. Getting this backwards is the kind of thing
// that renders and looks nearly right, so it is stated: Renderer+0x34 is
// `currentDrawGroup` and the caller uses the return value to RESTORE it.
//
// TWO PATHS, chosen by `drawGroups._size == nextIndex`:
//
//   APPEND (size == index, 0x180105009..0x1801050a1)
//     Build a whole 0x1f0 RendererDrawGroup on the stack, then grow the array
//     by one with it as the fill:
//       +0x000  = renderBuffer                        MOV [RSP+0x38],RDX
//       +0x008  = Camera(drawCamera)  copy CTOR       CALL 0x180017a70
//       +0x0e0  = Camera(cullCamera)  copy CTOR       CALL 0x180017a70
//       +0x1b8  = arg5   +0x1bc = arg6
//       +0x1c0  = three zeroed Array<DrawItem>        eh vector ctor, 0x1800801e8
//       then Array<RendererDrawGroup>::SetSize(&drawGroups, size+1, &temp)
//       then the three Arrays are destroyed again     eh vector dtor, 0x18007ec98
//     and `drawGroups._size` is INCREMENTED A SECOND TIME afterwards
//     (`INC DWORD PTR [RDI+0x38]` at 0x18010509e). *** THAT INC IS ON +0x38,
//     NOT ON THE ARRAY SIZE. *** Renderer+0x38 is the "groups used this frame"
//     counter, and BOTH paths bump it — the reuse path writes `index+1` into it
//     directly (0x1801050a6). Reading the INC as an array-size fixup would have
//     produced an off-by-one that only shows up on the second frame.
//
//   REUSE (size != index, 0x1801050a3..0x1801050ed)
//     Overwrite slot `index` in place, using Camera::operator= (0x180017c40)
//     rather than the copy ctor. The distinction is load-bearing: the copy ctor
//     initialises raw memory, operator= assigns over a live Camera. Both are
//     ORDINARY NAMED EXPORTS (??0Camera@Graphics@@QEAA@AEBV01@@Z ord 71 and
//     ??4Camera@Graphics@@QEAAAEAV01@AEBV01@@Z ord 1060) — read from the export
//     directory, per the GFXR lesson — so neither is an address door.
//
// THE APPEND PATH'S ARRAY GROW WAS THE ONE THING THIS BODY COULD NOT DO ITSELF.
// 0x1800638e0 is `Collection::Array<RendererDrawGroup>::SetSize(int, const T*)`,
// 404 bytes, NOT EXPORTED (checked with pefile, not inferred from a comment —
// AEAA means private, not unexported). It is a plain `SetSizeCore`
// instantiation of stride 0x1f0 / align 8 whose element assign (0x18001be60)
// and element destroy (the eh-vector iterator at 0x18007ec98 over `+0x1c0`) are
// THEMSELVES unexported. array.cpp's Wave 27 `kSpec_DrawGroup1f0` row already
// opened all three doors (array_internal.h), so ENDGAME B.3.2 step 2 bridges
// the call below through that row on the real `Collection::Array<
// RendererDrawGroup>` member instead of resolving 0x1800638e0 by address here.
// ----------------------------------------------------------------------------
namespace {

// The eh-vector ctor at 0x1800801e8 runs 0x180062950 over the three 0x10-byte
// item arrays, and 0x180062950 is five instructions:
//     XOR EAX,EAX / MOV [RCX],RAX / MOV [RCX+8],RAX / MOV RAX,RCX / RET
// i.e. `Collection::Array`'s default constructor. The destroy side, the
// eh-vector iterator at 0x18007ec98, runs 0x18001b960 -- a five-byte JMP thunk
// to 0x18001c650, a twelfth copy of lane Q's J1 body, i.e. `~Array` -- over
// the three BACKWARD. B.4: both are the stack `RendererDrawGroup`'s own
// implicit constructor and destructor in `BeginDrawGroup` (C++ destroys an
// array member's elements last-first, as the iterator does).

}  // namespace

// ============================================================================
// §5.  AnimationState::UpdateRootOnly
//      retail 0x1800fc870, ordinal 4525, 934 bytes across 3 chained fragments.
// ============================================================================
// `?UpdateRootOnly@AnimationState@Graphics@@AEAAXMAEAVVector3@Math@@@Z`
// RCX = this, XMM1 = float dt, R8 = Math::Vector3& out.
//
// Produces ONLY the blended ROOT TRANSLATION of the layer stack — the cheap
// path `AnimationManager::Update` takes for a mesh that is off-screen, where
// the per-node pose is not needed but the root motion still has to accumulate.
//
// THE ACCUMULATOR IS SEEDED FROM THE **LAST** LAYER AND THE LOOP RUNS
// **BACKWARD** FROM `layerCount - 2` TO 0. `SUB EAX,0x2 / MOVSXD RSI,EAX /
// JS <end>` at 0x1800fc9bf and `DEC RSI / JNS` at 0x1800fcb7b. A forward
// rewrite blends the stack in the wrong order and still produces plausible
// motion, which is why the direction is called out rather than left implicit.
//
// TWO WAYS A LAYER PRODUCES ITS POSE, chosen by the SIGN of `layer.handle`:
//
//   handle >= 0 — a STATIC pose read straight out of the AnimationManager's
//     group table. The index decode is `(handle >> 8) & 0xff` for the group and
//     `handle & 0xff` for the entry — an ARITHMETIC shift (`SAR RAX,0x8`) of a
//     SIGN-EXTENDED dword, then `MOVZX EAX,AL`. The group stride is 0x30, which
//     MSVC spells as `LEA RDX,[RAX+RAX*2] / ADD RDX,RDX` then `*8`; the entry
//     stride is 0x48, spelled `LEA R8,[RAX+RAX*8]` then `*8`. Both were
//     multiplied out rather than copied as shapes. The quaternion is set to
//     IDENTITY (0,0,0,1) — the 1.0f is the literal at 0x18016169c, read out of
//     the file as `00 00 80 3f`, not typed from memory.
//
//   handle < 0 — a SAMPLED pose: `?GetRootPoseAtTime@Animation@Graphics@@`
//     (0x1800faaa0, ord 2880 — an ordinary named export, checked with pefile,
//     so NOT an address door) called with the layer's own accumulated `time`
//     and a delta derived from `dt * layer.speed`.
//
// THE DELTA CONVERSION IS SINGLE-PRECISION UNTIL THE DIVIDE:
//   MULSS XMM0,[layer+0x10] / CVTSS2SD / DIVSD by _secondsPerTick / CVTTSD2SI.
// Widening earlier would agree to more digits and be wrong.
//
// *** THE BLEND WEIGHT COMES FROM THE **NEXT** LAYER, NOT THIS ONE. ***
// `CVTSI2SD XMM0,[RAX+RBX+0x28]` and `[RAX+RBX+0x20]` at 0x1800fcb08/0x1800fcb0f,
// where RBX is `(i+1) * 0x38` — every other read in the iteration uses
// `RBX - 0x38`. So the weight is `layers[i+1].fadeRemaining /
// layers[i+1].fadeLength`, the fade of the layer being blended FROM. Reading
// RBX as "this layer" is a one-line error that produces a working cross-fade
// with the wrong layer's timing.
//
// AND BOTH HALVES ARE NARROWED TO FLOAT BEFORE THE DIVIDE:
//   (double)rem * spt -> CVTPD2PS ; (double)len * spt -> CVTPD2PS ; DIVSS.
// Dividing in double and narrowing once is a different number.
//
// *** COUNT THE STORES: `Quaternion::Lerp` SUPPLIES ONLY FOUR OF THE SEVEN
// FLOATS. *** The call at 0x1800fcb72 is
// `?Lerp@Quaternion@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z` (0x18001d500,
// ord 3191) and the loop copies exactly FOUR dwords out of its result
// (0x1800fcb7e..0x1800fcbb0 -> `[RSP+0x30..0x3c]`). The accumulator's other
// three floats — the translation at `[RSP+0x40/0x44/0x48]` — are written from
// XMM7/XMM8/XMM9, which were lerped BY HAND three instructions earlier
// (`cur + (accum - cur) * t`). A port that handed the whole 0x1c-byte Transform
// to `Lerp` and copied the whole thing back would pass every quaternion check
// and put the DLL's translation where the hand-computed one belongs. This is
// warning 3 of the kickoff in its exact shape, found by counting MOVSS stores.
//
// ONE MORE ASYMMETRY, TRANSCRIBED: `this->needsUniqueCopy` (+0x00) is CLEARED
// unconditionally at 0x1800fc8e3, before either branch and before the loop.
// AnimationState::Update sets that flag; this cheap path consumes it and
// throws it away without ever acting on it.
// ----------------------------------------------------------------------------
namespace {

// The static-pose lookup both AnimationState bodies share. `MOV R15,
// [0x1801fe7b0]` is ?_singleton@AnimationManager@Graphics@@ (the class static);
// its `_instances` (stride 0x30) indexed by `(handle >> 8) & 0xff` -- an
// ARITHMETIC shift of the sign-extended dword -- then that instance's `_states`
// (stride 0x48) by `handle & 0xff`, and the state's `_rootOffset` (+0x18).
inline const Math::Vector3& StaticRootVector(int32_t handle) {
    const Graphics::AnimationManager* const mgr = Graphics::AnimationManager::_singleton;
    using Graphics::AnimationManager;
    const int32_t gi = static_cast<int32_t>(
        (static_cast<int64_t>(handle) >> AnimationManager::kHandleIndexBits) &
        AnimationManager::kHandleIndexMask);
    const int32_t si = static_cast<int32_t>(static_cast<uint8_t>(handle));
    return mgr->_instances._data[gi]._states._data[si]._rootOffset;
}

// `MOV RAX,[layer+0x00] / MOVSXD RCX,[layer+0x08] / IMUL RCX,RCX,0x70 /
//  ADD RCX,[RAX+0x20]` — the clip record the layer plays:
// `AnimationGroup::GetAnimation`, inline in AnimationGroup.h.
inline const Graphics::Animation& LayerClip(const Graphics::AnimationState::Layer* L) {
    return L->_group->GetAnimation(L->_clipIndex);
}

}  // namespace

// ENDGAME B.3.5 block GC: THE BODY MOVED to
// `Graphics::AnimationState::UpdateRootOnly`, in this file's trailing
// `namespace Graphics` block — it cannot live here, because everything above is
// inside `namespace rtgfxbody28` and `Graphics` is its SIBLING, not its child
// (B34_BRIEFS A.2). Block H1 left the body here because it is wrapped in this
// lane's attendance counters; the counters stay in the door, where they belong,
// and the door keeps its name and its registration.
//
// `AnimationState.h`'s declaration takes `Math::Vector3&` (retail's
// `AEAVVector3@Math@@`); the body's `out` is that reference's three floats. The
// three fields the body reads are the real class's: `_layers._size` is the
// POD's `layerCount` (+0x10) and `_layers._data` its `layers` (+0x08), both
// welded by AnimationState.h's own `static_assert`s. The LAYER record is still
// spelled `RtGraphics::AnimationLayer`, because that POD has live users outside
// this file and B4b-1 keeps it until the last one converts; it is field-for-field
// `Graphics::AnimationState::Layer` and the header cross-asserts the two.

// ============================================================================
// §6.  Camera::operator=  — retail 0x180017c40, ordinal 1060, 460 bytes.
// ============================================================================
// NOT ONE OF THIS LANE'S TEN ROWS. It is here because `BeginDrawGroup`'s reuse
// path is the tree's FIRST caller of it, so porting §4 PUT THIS ROW INTO THE
// `called` COLUMN (reference_porting_can_grow_the_cutover_surface.md). A lane
// that opens a gap row and leaves it open has moved work, not done it — and
// this one is 460 bytes of dword copies.
//
// *** THIS IS A MEASURED INSTANCE OF THE PORT-BY-ANALOGY WARNING, AND THE
// SIBLING IS ALREADY IN OUR TREE. ***
//
// `RtGraphics_Camera_CopyCtor` (wave9_lane_gfxd.cpp:184) is the copy CTOR,
// 0x180017a70, and its comment reads "FIFTY-THREE dword copies covering
// +0x00..+0xd3, then ONE byte at +0xd4". That description is correct FOR THE
// CTOR. `operator=` is NOT the same body:
//
//   copy ctor  0x180017afe:  MOV EAX,[RDX+0x60]      ; a DWORD
//              0x180017b01:  MOV [RCX+0x60],EAX
//   operator=  0x180017cce:  MOVZX EAX,BYTE [RDX+0x60] ; a BYTE
//              0x180017cd2:  MOV BYTE [RCX+0x60],AL
//              0x180017cd5:  MOV EAX,[RDX+0x64]      ; resumes at 0x64
//
// So the CTOR writes `+0x61..+0x63` and `operator=` DOES NOT. +0x60 is the
// `bool` the default ctor sets with `MOV BYTE PTR [RCX+0x60],1`
// (wave9_lane_gfxd.cpp:218), and the assignment operator respects its width
// while the constructor — writing into raw memory, where the padding is
// meaningless — does not.
//
// Writing this body as "the same 53-dword loop as the sibling" would write
// three bytes retail leaves alone, into a live Camera, every frame. "I checked
// the sibling" is not a check; the bytes are. THE WRITTEN EXTENT OF A COPY IS A
// PROPERTY OF THE WRITER, NOT OF THE TYPE
// (reference_never_written_needs_by_whom.md).
//
// The unwritten windows, stated so an oracle does not compare them:
// `+0x61..+0x63` and `+0xd5..+0xd7` (sizeof is 0xd8; the last store is the byte
// at +0xd4). Returns `this` (`MOV RAX,RCX` at 0x180017e08).
// ----------------------------------------------------------------------------
//
// B.4: on the named fields -- the frustum's 24 floats (+0x00..+0x5f) and its
// `isOrthographic` byte (+0x60), the Transform3 and projection (+0x64..+0xd3)
// and the `_zUp` byte (+0xd4); the same bytes, the same holes.
extern "C" Graphics::Camera* RtGraphics_Camera_Assign(Graphics::Camera* self,
                                                     const Graphics::Camera* src) {
    Attend("Camera::operator=", g_cameraAssignCalls);

    for (int k = 0; k < 6; ++k)                                      // +0x00..+0x5f
        self->_frustum.planes[k] = src->_frustum.planes[k];
    self->_frustum.isOrthographic = src->_frustum.isOrthographic;   // +0x60, a BYTE
    self->_cameraToWorld = src->_cameraToWorld;                      // +0x64..+0x93
    self->_projection    = src->_projection;                         // +0x94..+0xd3
    self->_zUp = src->_zUp;                                          // +0xd4, a BYTE
    return self;
}

// One line beside the other wave summaries, so a run that entered none of these
// says so rather than staying silent.
void RtGraphicsWave28GfxbodySummary() {
    printf("[GFXBODY28] GetConstantName=%llu GetTextureName=%llu "
           "FindConstant=%llu (hits=%llu) FindTexture=%llu (hits=%llu) "
           "ApplyEvents=%llu (fired=%llu) BeginDrawGroup=%llu (appends=%llu)\n",
           g_getConstNameCalls, g_getTexNameCalls,
           g_findConstCalls, g_findConstHits, g_findTexCalls, g_findTexHits,
           g_applyEventsCalls, g_applyEventsFired,
           g_beginGroupCalls, g_beginGroupAppends);
    // WAVE 29 LANE VERIFY: the printf above covered 6 of this lane's 8
    // registered rows. `UpdateRootOnly` and `Camera::operator=` each had a
    // counter that was incremented by `Attend()` and then printed NOWHERE — the
    // same defect class as the unreached summary itself, one level down: a
    // counter with no reader is indistinguishable from a body with no caller.
    // Camera::operator= is the row Wave 29 §2.2 warns about by name, so its
    // attendance is the one number that must never be inferred from silence.
    printf("[GFXBODY28] UpdateRootOnly=%llu (layers=%llu) CameraAssign=%llu\n",
           g_updateRootCalls, g_updateRootLayers, g_cameraAssignCalls);
}

}  // namespace rtgfxbody28

void RtGraphicsWave28GfxbodySummary()  { rtgfxbody28::RtGraphicsWave28GfxbodySummary(); }

// ############################################################################
// ENDGAME B.3.5 step 5, block G — `Graphics::Material`'s and
// `Graphics::MaterialInstance`'s member bodies, whose live doors are the four
// functions above. Their own `namespace Graphics { }` block, because a member
// definition has to appear in a namespace ENCLOSING the class and `Graphics` /
// `RtGraphics` are siblings (B34_BRIEFS A.2). The file-local helpers here sit in
// `rtgfxbody28`'s OWN unnamed namespace, not the global one, so unqualified
// lookup does NOT reach them from `namespace Graphics` and they are spelled
// `rtgfxbody28::` — the other half of the A.2 trap, and the reason this block
// does not just work the way mesh.cpp's does.
// ############################################################################

namespace Graphics {

// ---------------------------------------------------------------------------
// ?GetConstantName@Material@Graphics@@QEBA?AVString@System@@HH@Z  0x180102830
// ?GetTextureName@Material@Graphics@@QEBA?AVString@System@@HH@Z   0x180102900
//
// Four-line forwarders onto `Video::ShaderProgram`. `_passes` at +0x28 is the
// `Array<ExternalPtr<Video::VideoProgram>>` the real class declares, and the pass
// object's +0x30 is its pixel `ShaderProgram` (one of the four `ExternalPtr`
// members `Video::VideoProgram`'s own getters expose, at +0x20/+0x28/+0x30/+0x38).
//
// THE CALLER'S RETURN SLOT IS PASSED STRAIGHT THROUGH — `ret` is never written
// here, only handed on — which is why the member keeps the out-parameter. See
// the doors' banner above.
//
// NO NULL CHECK ANYWHERE, and that is not an oversight in retail: the
// NEIGHBOURING export `?GetConstantCount@Material@Graphics@@` (0x180102880) walks
// the SAME chain and DOES test the shader program for null first. The asymmetry is
// deliberate in the shipped code and is transcribed rather than "fixed".
// ---------------------------------------------------------------------------
// ENDGAME B.3.5 caller batch: both `Video::ShaderProgram::GetConstantName` and
// `::GetTextureName` are DEFINED members (shader_program_names.cpp:313/326) and
// both are HIDDEN-RETSLOT doors (B5-12). The conversion is the only shape the
// brief allows for one — the DOOR BODY MOVED TO THE CALL SITE: exactly one
// `System::String` is constructed, placement-new'd into the SAME caller storage
// `ret` that the door pointer used to be handed, from the member's prvalue. That
// is line for line what `RtVideo_ShaderProgram_GetConstantName` does, minus the
// indirection, so the refcount arithmetic is unchanged. `ShaderGetNameFn` is gone.
// B.4 acceptance MOD: the member returns by value (retail's signature); the
// callee's prvalue is constructed straight into the caller's slot, as the
// placement new into `ret` did.
System::String Material::GetConstantName(int32_t pass, int32_t index) const {
    return rtgfxbody28::PassShaderProgram(this, pass)
        ->::Video::ShaderProgram::GetConstantName(index);
}

System::String Material::GetTextureName(int32_t pass, int32_t index) const {
    return rtgfxbody28::PassShaderProgram(this, pass)
        ->::Video::ShaderProgram::GetTextureName(index);
}

// ---------------------------------------------------------------------------
// ?FindConstant@MaterialInstance@Graphics@@AEBAHAEBVString@System@@@Z 0x180102f40
// ?FindTexture@MaterialInstance@Graphics@@AEBAHAEBVString@System@@@Z  0x180103020
//
// A linear search of the instance's own constant / texture array for an element
// whose leading `System::String` equals the argument, returning the index or -1.
// The two differ in THREE fields and nothing else — the array, its count and the
// element stride — and `Material.h`'s real class names all of them; the stride is
// the element type's.
//
// `AEBA` is PRIVATE, and this DLL exports its private members
// (reference_aeaa_is_private_not_unexported.md) — which is also why the two are
// PUBLIC here for now rather than private: the doors above are `extern "C"` free
// functions and cannot be friends of the class without a declaration this header
// does not need.
// ---------------------------------------------------------------------------
int32_t MaterialInstance::FindConstant(const System::String& name) const {
    return rtgfxbody28::FindByName(_constants._data, _constants._size, &name);
}

int32_t MaterialInstance::FindTexture(const System::String& name) const {
    return rtgfxbody28::FindByName(_textures._data, _textures._size, &name);
}

// ---------------------------------------------------------------------------
// ?UpdateRootOnly@AnimationState@Graphics@@AEAAXMAEAVVector3@Math@@@Z 0x1800fc870
// ENDGAME B.3.5 block GC. The evidence — the backward loop, the next-layer blend
// weight, the single-precision delta, and above all "count the stores: Lerp
// supplies only FOUR of the seven floats" — is in the banner beside the door.
//
// B.4: on `Layer`'s named fields, the clip through `AnimationGroup::
// GetAnimation`, the static pose through `AnimationManager::_instances`, and the
// accumulator as the `Math::Transform` it is.
// ---------------------------------------------------------------------------
// `_layers._data` is RE-READ at every use, never hoisted into a local: retail
// reloads it (`MOV RAX,[RDI+8]`) at the top of the loop and again for the
// next-layer weight, and `GetRootPoseAtTime` is a call between those reloads.
void AnimationState::UpdateRootOnly(float dt, Math::Vector3& rootOffsetOut) {
    Math::Transform accum;                            // the [RSP+0x30] local
    float ax, ay, az;                                 // XMM7 / XMM8 / XMM9

    {   // ---- seed from the LAST layer --------------------------------------
        const Layer* const L = &_layers._data[_layers._size - 1];
        const Animation& clip = rtgfxbody28::LayerClip(L);
        _instanced = false;                           // MOV BYTE PTR [RDI],0x0

        if (L->_handle >= 0) {                        // TEST EAX,EAX / JS
            accum.rotation.x = 0.0f; accum.rotation.y = 0.0f;   // XORPS XMM6,XMM6
            accum.rotation.z = 0.0f; accum.rotation.w = 1.0f;   // [0x18016169c] == 00 00 80 3f
            const Math::Vector3& v = rtgfxbody28::StaticRootVector(L->_handle);
            ax = v.x; ay = v.y; az = v.z;
        } else {
            const float secs = dt * L->_speed;        // MULSS XMM0,[R8+0x10]
            const System::Time delta = { static_cast<int64_t>(
                static_cast<double>(secs) / ::System::Timer::_secondsPerTick) };
            // ?GetRootPoseAtTime@Animation@Graphics@@QEBA?AVTransform@Math@@AEBVTime@
            // System@@0@Z (0x1800faaa0), the member: EXACTLY ONE construction, into
            // the caller's storage.
            const Math::Transform p = clip.Graphics::Animation::GetRootPoseAtTime(L->_time, delta);
            accum.rotation = p.rotation;
            ax = p.translation.x; ay = p.translation.y; az = p.translation.z;
        }
        accum.translation.x = ax; accum.translation.y = ay; accum.translation.z = az;
    }

    // ---- blend the rest, BACKWARD from layerCount-2 -------------------------
    // `_secondsPerTick` is hoisted into XMM10 before the loop (0x1800fc9e6) and
    // RE-LOADED after every GetRootPoseAtTime call (0x1800fca9f) because XMM10
    // is volatile. A single local is the same thing.
    const double spt = ::System::Timer::_secondsPerTick;
    for (int32_t i = _layers._size - 2; i >= 0; --i) {
        const Layer* const L = &_layers._data[i];     // re-read each pass
        const Animation& clip = rtgfxbody28::LayerClip(L);

        Math::Transform cur;
        float cx, cy, cz;                              // XMM3 / XMM4 / XMM5
        if (L->_handle >= 0) {
            cur.rotation.x = 0.0f; cur.rotation.y = 0.0f;
            cur.rotation.z = 0.0f; cur.rotation.w = 1.0f;
            const Math::Vector3& v = rtgfxbody28::StaticRootVector(L->_handle);
            cx = v.x; cy = v.y; cz = v.z;
        } else {
            const float secs = dt * L->_speed;
            const System::Time delta = { static_cast<int64_t>(static_cast<double>(secs) / spt) };
            const Math::Transform p = clip.Graphics::Animation::GetRootPoseAtTime(L->_time, delta);
            cur.rotation = p.rotation;
            cx = p.translation.x; cy = p.translation.y; cz = p.translation.z;
        }
        cur.translation.x = cx; cur.translation.y = cy; cur.translation.z = cz;

        // The weight is the NEXT layer's fade. See the banner.
        const Layer* const N = &_layers._data[i + 1];
        const float t = static_cast<float>(static_cast<double>(N->_fadeRemaining._ticks) * spt) /
                        static_cast<float>(static_cast<double>(N->_fadeLength._ticks)    * spt);

        // SUBSS then MULSS then ADDSS, in that order, on the ACCUMULATOR minus
        // the current layer — `cur + (accum - cur) * t`.
        const float dx = (ax - cx) * t;
        const float dy = (ay - cy) * t;
        const float dz = (az - cz) * t;
        ax = cx + dx; ay = cy + dy; az = cz + dz;

        // FOUR floats out of Lerp, and only four.
        accum.rotation = cur.rotation.Lerp(accum.rotation, Math::Scalar(t));
        accum.translation.x = ax; accum.translation.y = ay; accum.translation.z = az;
        ++rtgfxbody28::g_updateRootLayers;
    }

    rootOffsetOut.x = ax; rootOffsetOut.y = ay; rootOffsetOut.z = az;   // MOVSS [R14], ...
}

// ---------------------------------------------------------------------------
// ?BeginDrawGroup@Renderer@Graphics@@  0x180104fd0 -- ENDGAME B.3.5 block GC.
// The reuse-versus-append split, the `_drawGroupsUsed` cursor (B35_RULES 13
// item 10, SETTLED by this body) and the hand-derived `kSpec_DrawGroup1f0` bridge
// are in the banner beside the door.
//
// B.4: on the real Renderer's and RendererDrawGroup's named fields. The append
// path's stack group is a `RendererDrawGroup`: its implicit constructor is the
// eh-vector zeroing of the three item arrays (0x1800801e8) and its implicit
// destructor their backward destruction (0x18007ec98) -- see the banner.
// ---------------------------------------------------------------------------
int Renderer::BeginDrawGroup(const Video::RenderBuffer* renderBuffer,
                             const Camera& drawCamera, const Camera& cullCamera,
                             int shader, uint32_t passMask) {
    const int32_t prev = _currentDrawGroup;            // MOV EBP,[RCX+0x34]
    const int32_t idx  = _drawGroupsUsed;              // MOVSXD RCX,[RCX+0x38]
    _currentDrawGroup = idx;                           // MOV [RDI+0x34],ECX

    if (_drawGroups.GetSize() != idx) {
        // ---- REUSE -------------------------------------------------------
        _drawGroupsUsed = idx + 1;                     // LEA EAX,[RCX+1]
        RendererDrawGroup& g = _drawGroups._data[idx];
        g._renderBuffer = renderBuffer;
        // ??4Camera@Graphics@@ (0x180017c40): the door body above, by name (it
        // counts its calls for the [GFXBODY28] line).
        rtgfxbody28::RtGraphics_Camera_Assign(&g._camera, &drawCamera);
        rtgfxbody28::RtGraphics_Camera_Assign(&g._cullCamera, &cullCamera);
        g._shader            = shader;
        g._materialTypeFlags = passMask;
        return prev;
    }

    // ---- APPEND ----------------------------------------------------------
    ++rtgfxbody28::g_beginGroupAppends;
    // B6 row 32: the stack record is built by its own constructor, retail's
    // stores only: +0x000, the two Camera copy ctors (??0Camera@Graphics@@QEAA@
    // AEBV01@@Z 0x180017a70) at +0x008 / +0x0e0, +0x1b8, +0x1bc, the three zeroed
    // Arrays. (It was default-constructed first, the two Cameras' members
    // included, then copied over.)
    RendererDrawGroup temp(renderBuffer, drawCamera, cullCamera, shader, passMask);

    //   SYMBOL:      0x1800638e0 — Collection::Array<RendererDrawGroup>::SetSize.
    //                CONFIRMED ABSENT from the export directory with pefile, not
    //                inferred from a comment (AEAA means private, not unexported).
    //
    // ENDGAME B.3.2 step 2 (callers) — `_drawGroups`'s DECLARED spec is the
    // trait-derived default, which is NOT what retail's 0x1800638e0 does (see
    // array_internal.h's `kSpec_DrawGroup1f0` banner: the element assign is a
    // deep nested-array copy the trait derivation cannot reproduce). Bridged
    // through the hand-derived row at the call site instead of trusting the
    // field's own (wrong) spec.
    rtcoll::AsArray<&rtcoll::kSpec_DrawGroup1f0>(&_drawGroups)
        ->SetSizeFill(_drawGroups.GetSize() + 1, &temp);

    ++_drawGroupsUsed;                                 // INC [RDI+0x38] — NOT the size
    return prev;                                       // ~RendererDrawGroup: the three arrays
}

// ---------------------------------------------------------------------------
// ?ApplyEvents@Animation@Graphics@@QEBAXAEBVTransform@Math@@AEBVTime@System@@1@Z
// retail 0x1800fa9a0 -- ENDGAME B.3.5 block GC. The ordinary and loop-wrap firing
// windows, the re-read event count and the +0x50 frame-count correction are in the
// banner beside the door.
//
// B.4: on `Graphics::Animation`'s own fields -- `_events` (+0x00/+0x08),
// `_startFrame` / `_endFrame` (+0x30/+0x34, the prefix's frameFirst / frameLast),
// `_loopScale` (+0x48), `_frameCount` (+0x50) and `_duration` (+0x58).
//
// SLOT 8 OF THE EVENT OBJECT is a by-name call of the pure
// `AnimationEvent::Construct(const Math::Transform&)` (B3R owed 2: the header
// now carries retail's argument). Its receiver is a subclass defined in the EXE.
// ---------------------------------------------------------------------------
void Animation::ApplyEvents(const Math::Transform& base, const System::Time& from,
                            const System::Time& to) const {
    const int64_t* const fromTime = &from._ticks;
    const int64_t* const toTime   = &to._ticks;

    const int64_t duration = _duration;                 // MOV RBX,[RCX+0x58]
    const int64_t fromWrapped = *fromTime - duration;   // SUB RDI,RBX
    const int64_t toWrapped   = *toTime   - duration;   // SUB RSI,RBX

    // CMP [RCX+0x8],EBP / JLE — count read from `this` on every iteration
    // (0x1800faa6c re-reads it), so it is not hoisted.
    for (int32_t i = 0; i < _events._size; ++i) {
        const Event* const ev = _events._data + i;      // MOV RDX,[R15]

        // +0x50 is the FRAME COUNT, not the frame rate -- corrected
        // 2026-09-12 in AnimationState.h; the arithmetic is unchanged.
        const float rate  = static_cast<float>(_frameCount);   // CVTDQ2PS
        const float num   = static_cast<float>(ev->_frame - _startFrame);
        const float den   = static_cast<float>(_endFrame - _startFrame);
        const float secs  = ((num / den) * rate) * _loopScale;
        const int64_t t   = static_cast<int64_t>(
            static_cast<double>(secs) / ::System::Timer::_secondsPerTick);

        bool fire = false;
        if (t >= *fromTime && t < *toTime) {
            fire = true;                                 // the ordinary window
        } else if (*toTime >= duration &&
                   t >= fromWrapped && t < toWrapped) {
            fire = true;                                 // the loop-wrap window
        }
        if (!fire) continue;

        // Slot 8 (`CALL [RAX+0x40]`, 0x1800faa5e) with RDX = the transform
        // (R13, ApplyEvents' own second argument); RAX is not read after.
        ev->_event->Construct(base);
        ++rtgfxbody28::g_applyEventsFired;
    }
}

}  // namespace Graphics
