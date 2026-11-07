// ============================================================================
// src/engine/graphics/wave27_lane_gfxbig.cpp — WAVE 27 LANE GFXBIG
// The three large Graphics BODY rows of the census gap.
// ============================================================================
//
//   | Export                                        | Retail      | Ord  | Bytes |
//   |-----------------------------------------------|-------------|------|-------|
//   | ?BuildParameters@MaterialInstance@Graphics@@   | 0x1801033e0 | 1678 |  1421 |
//   | ?Draw@Renderer@Graphics@@                      | 0x180104bb0 | 2258 |   920 |
//   | ?Update@AnimationState@Graphics@@              | 0x1800fdb20 | 4474 |  1062 |
//
// Every address and ordinal above is `pefile`'s reading of the export directory
// of the shipped C:\GOG Games\Banished\Runtime-x64.dll, taken before a line of
// this file was written (reference_resolveexport_phantom_names.md). The row list
// was re-derived with `tools/runtime_census.py --unported` on this tip and
// filtered to ported==0 && symbol_kind==BODY && namespace==Graphics; that filter
// returns FOUR rows, and the fourth (?AddDrawItems@MeshInstance@Graphics@@)
// belongs to lane GFXMESH, not here.
//
// ---------------------------------------------------------------------------
// *** THE SOURCE OF TRUTH FOR EVERY BYTE BELOW IS objdump, NOT GHIDRA. ***
// ---------------------------------------------------------------------------
// The lane brief said to prefer `disassemble_function` over
// `decompile_function`. That was the right instinct and it was not sufficient:
// on `?Update@AnimationState@Graphics@@` (0x1800fdb20) the ghidra-mcp tool
// `disassemble_function(address="0x1800fdb20")` returned THE WRONG FUNCTION'S
// LISTING — 126 instructions spanning 0x1800fd9a0..0x1800fdb58, an
// `Array<T>::SetSize` of stride 0x18 — while printing them under the addresses
// that had been asked for. It reported the instruction at 0x1800fdb20 as
// `LEA RAX,[RBX + -0x8]`.
//
// The bytes in the shipped file at that address are
//   4c 89 4c 24 20   48 89 54 24 10   53 55 56 41 55   48 81 ec 98 00 00 00
// = MOV [RSP+0x20],R9 / MOV [RSP+0x10],RDX / PUSH RBX / PUSH RBP / PUSH RSI /
//   PUSH R13 / SUB RSP,0x98 — a prologue, which is what an export entry has to
// be. Ghidra holds TWO OVERLAPPING function bodies here (`FUN_1800fd9a0`
// claiming 0x1800fd9a0-0x1800fdb58 and `Update` claiming
// 0x1800fdb20-0x1800fdf45) and the endpoint resolves an address to the first
// body that contains it. **It gave no warning of any kind.**
//
// So every listing behind this file was taken from `objdump -d -M intel` over
// the shipped DLL. Ghidra was used only for what it is authoritative about here
// — the function-extent question — and even that was cross-checked against the
// PE `.pdata` table, which is why no extent above is quoted from .pdata alone:
// `?Update@AnimationState@Graphics@@` is assembled from FIVE CHAINED .pdata
// fragments (0x1800fdb20 -> 0xfdb71 -> 0xfdcc0 -> 0xfdf15 -> 0xfdf34 ->
// 0xfdf46), so its first fragment's 81 bytes understate a 1062-byte body 13x
// (reference_pdata_is_a_lower_bound_on_function_extent.md).
//
// ---------------------------------------------------------------------------
// THE GFXR LESSON, RE-RUN ON THIS LANE'S OWN CALLEES
// ---------------------------------------------------------------------------
// wave9_lane_gfxd.cpp's banner is emphatic that a source-tree grep records an
// unnamed internal where the EXPORT TABLE has a name. Every callee of all three
// bodies was put through pefile before it was written down, and the payoff was
// large: of thirteen distinct callees, NINE are ordinary named exports.
//
//   0x180060d30  Array<MaterialParameterSet>::SetSize   NOT EXPORTED  (ported here)
//   0x180102900  ?GetTextureName@Material@Graphics@@           ord 2970
//   0x180103020  ?FindTexture@MaterialInstance@Graphics@@      ord 2361
//   0x180102830  ?GetConstantName@Material@Graphics@@          ord 2506
//   0x180102f40  ?FindConstant@MaterialInstance@Graphics@@     ord 2343
//   0x180105110  ?Reset@Renderer@Graphics@@                    ord 3574
//   0x180105700  ?AddScene@Renderer@Graphics@@                 ord 1579
//   0x180104fd0  ?BeginDrawGroup@Renderer@Graphics@@           ord 1640
//   0x1801017a0  ?AddCulledDrawItems@Scene@Graphics@@          ord 1543
//   0x180062d60  the draw-item sort                 NOT EXPORTED (ported, BATCH 8b)
//   0x180063b90  its selection-sort callee          NOT EXPORTED (ported, BATCH 8b)
//   0x1800fd4c0  ?CopyInstancedToUnique@AnimationState@        ord 1868
//   0x1800fc870  ?UpdateRootOnly@AnimationState@Graphics@@     ord 4525
//   0x1800fcc20  ?UpdateNodes@AnimationState@Graphics@@        ord 4522
//   0x1800fa9a0  ?ApplyEvents@Animation@Graphics@@             ord 1605
//   0x18005aa30  Array<Node>::SetSize                   NOT EXPORTED  (door)
//   0x18005b1e0  Array<AnimationState>::SetSize         NOT EXPORTED  (door)
//
// FOUR of those exported names DECIDED AN ARGUMENT MAPPING that the prologue
// alone could not, which is the whole reason the lookup came first:
// `?UpdateNodes@…QEAAXMAEBV?$Array@…@AEBVTransform@Math@@PEAV?$InterpolatedState
// @…@PEAV56@AEAVVector3@6@@Z` spells six parameters with a FLOAT FIRST, and that
// is what says the `MOVAPS XMM1,XMM6` at 0x180104c03's analogue is argument 2
// rather than a spill (reference_read_the_callee_before_holding_back_on_an_arg.md).
//
// ---------------------------------------------------------------------------
// WHAT THIS LANE DOES NOT CLAIM
// ---------------------------------------------------------------------------
// THREE unexported internals are NOT ported and are called at their retail
// addresses through documented address doors, each commented at its call site in
// the shape WAVE27_KICKOFF.md §4 #10 asks for (SYMBOL / CONDITION /
// CONSEQUENCE). They are sized and given a NAMED ADDRESSEE in
// docs/history/runtime_port/WAVE27_LANE_GFXBIG.md §6. None is a vtable slot and none
// belongs to another lane's table.
//
// The ONE unexported internal this lane DID port is 0x180060d30, because
// BuildParameters cannot run without it and it is a plain `SetSizeCore`
// instantiation that lane Q's parameterisation already covers.
//
// NO VTABLE ROW IS TOUCHED. All three bodies make virtual calls — Draw makes
// four kinds — and every one of them is a dispatch through an object's own
// vtable, not a slot this lane owns or stamps. `?AddDrawItems@MeshInstance@` is
// reached as slot 3 from Draw's scene walk; it is lane GFXMESH's row and is left
// entirely alone here.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>   // B.4: std::less, the pointer order DrawItemLess compares by

#include "graphics/AnimationState.h"
#include "graphics/AnimationManager.h"   // B.3.9: AnimationManager::_singleton
#include "graphics/AnimationGroup.h"   // B.4: AnimationGroup::GetAnimation
#include "graphics/Animation.h"   // ENDGAME B.3.5 closing pass:
                                              // Graphics::Animation, the clip record
                                              // ApplyEvents is called on
#include "system/Value.h"   // System::Time -- ENDGAME B.3.5 block GC
#include "system/Timer.h"   // ENDGAME B.3.9 close: Timer::_secondsPerTick
#include "graphics/Camera.h"
#include "graphics/skin_memo.h"   // SKINMEMO: Renderer::Draw opens the bone memo's scope
#include "graphics/Scene.h"   // ENDGAME B.3.5 caller batch: Graphics::Scene
#include "graphics/Light.h"   // B3X W0: Light::DrawShadowMap / DrawLight by name
#include "graphics/SunLight.h"   // ext.staggered_passes: the shadow hold's emit flag (NOT RETAIL)
#include "graphics/Material.h"
#include "graphics/material_binding_bounds.h"   // review S-003: the bindings BuildParameters appends
#include "video/Context.h"
#include "video/Device.h"          // B.4: Video::RenderBuffer::width / height
#include "video/ImageBuffer.h"     // B.4: Video::ImageBuffer::_kind
#include "video/VideoProgram.h"    // B.4: Video::VideoProgram::_pixelProgram
#include "video/ShaderProgram.h"   // B.4: ShaderProgram::_textures / _constants
#include "../collection/array_internal.h"
#include "../collection/introsort_msvc.h"
#include "graphics/draw_sort.h"   // DRAWSORT: the comparator, the reference and the exact keyed sort
#include "system/string_internal.h"

namespace rtgfxbig {

using RtGraphics::MaterialBinding;
using RtGraphics::MaterialParameterSet;
using rtcoll::ArraySpec;

// ---------------------------------------------------------------------------
// Attendance. Counts the DOOR and, separately, the BRANCH that does the work.
// `calls=0` and `calls=N with the interesting arm never taken` are different
// findings and an entry counter cannot tell them apart
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md).
// ---------------------------------------------------------------------------
void Attend(const char* what, unsigned long long& n) {
    ++n;
    if (n == 1 || n == 10 || n == 1000 || (n % 100000) == 0)
        printf("[GFXBIG27] %s calls=%llu\n", what, n);
}

unsigned long long g_buildParamCalls  = 0;
unsigned long long g_buildParamBinds  = 0;   // BRANCH: bindings actually appended
unsigned long long g_drawCalls        = 0;
unsigned long long g_drawGroups       = 0;   // BRANCH: scene entries walked in pass 1
unsigned long long g_drawSorts        = 0;   // BRANCH: item lists actually sorted
unsigned long long g_animStateCalls   = 0;
unsigned long long g_animStateLayers  = 0;   // BRANCH: layers actually stepped
unsigned long long g_animStateExpired = 0;   // RARE ARM: layers removed

// ============================================================================
// §1.  THE ARRAY MACHINERY UNDER BuildParameters
// ============================================================================
// BuildParameters reaches Collection::Array in three DIFFERENT shapes, not one
// repeated:
//
//   (a) 0x180060d30  Array<MaterialParameterSet>::SetSize(int)   [NOT EXPORTED]
//       stride 0x20, align 8, NON-TRIVIAL element (two sub-Arrays). Called ONCE
//       at the top of BuildParameters. PORTED, via lane Q's SetSizeCore.
//   (b) the append into `set.textures` / `set.constants` — element stride 8,
//       **align 4** — which retail INLINES rather than calls. Also SetSizeCore,
//       with a `fill`.
//   (c) System::String release, inlined at the two Get*Name sites.
//
// *** (b) IS NOT kSpec_Fill8. *** kSpec_Fill8 (array.cpp:1012) is stride 8
// ALIGN 8. The allocation inlined at 0x1801035c4 and 0x1801037ec is
// `MOV R8D,0x4` — align FOUR. The element is a {int32,int32} pair, so 4 is the
// honest alignment and 8 would be a different call into the allocator. A spec is
// defined here rather than reusing the neighbour whose NAME matches, because the
// thing that has to match is the immediate in the instruction (array.cpp's own
// rule: "`align` is the immediate in EDX at the MemAlloc call site"). Checked
// against the bytes, not inferred from the element size.
// ----------------------------------------------------------------------------

// 0x180005800 is `Collection::Array<T>::~Array` for a trivially-destructible T,
// instruction-for-instruction lane Q's `ArrayDtorImpl`: MemFree(_data) (the null
// test lives inside 0x1800c7ad0), then _data = 0 and the _size/_capacity qword
// = 0. NOT re-written here; lane Q's body is called
// (reference_grep_for_the_port_before_writing_it.md).

// `Collection::Array<{int32,int32}>::operator=` — retail 0x180005770.
// Destroys the destination first, then copies `src._size` elements. The loop
// bound and both data pointers are RE-READ every iteration (`CMP R8D,[RBX+8]`
// at 0x1800057e9, `MOV RAX,[RDI]` at 0x1800057d8); transcribed unhoisted because
// that is what the bytes do, not for style.
void Array8Assign(Collection::Array<MaterialBinding>* dst,
                  const Collection::Array<MaterialBinding>* src) {
    if (dst == src) return;                       // CMP RCX,RDX / JZ

    // ENDGAME B.3.2 step 2 (callers) — `dst` is always `&set->textures` or
    // `&set->constants` (PassAssign, below), both real
    // `Collection::Array<MaterialBinding>` members (Material.h). MaterialBinding
    // is a plain POD, so `Array<MaterialBinding>::~Array` is trait-derived and
    // bit-for-bit `rtcoll::ArrayDtorImpl` (spec->destroy is null either way).
    dst->~Array();                                // CALL 0x180005800

    const int32_t n       = src->_size;           // MOV ECX,[RDI+8]
    const int32_t oldSize = dst->_size;           // MOV EDX,[RBX+8]  (0 by now)
    dst->_size = n;

    if (n > dst->_capacity) {
        dst->_capacity = n;
        // Retail's `*3` bump. DEAD on this path — ArrayDtorImpl just zeroed
        // _size, so `oldSize != 0` never holds. Written anyway: it is in the
        // bytes, and a future caller reaching this body with a live destination
        // would see the difference.
        if (n == static_cast<int32_t>(oldSize + 1) && oldSize != 0)
            dst->_capacity = static_cast<int32_t>(n * 3);
        dst->_data = static_cast<MaterialBinding*>(
            rtcoll::MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(dst->_capacity)) * 8,
                             4));                 // MOV EDX,0x4 at 0x1800057b0
    }
    for (int32_t i = 0; i < dst->_size; ++i) {    // bound re-read: see above
        MaterialBinding* const d = dst->_data + static_cast<int64_t>(i);
        if (d == nullptr) continue;               // retail tests the COMPUTED address
        *d = src->_data[static_cast<int64_t>(i)];  // one 8-byte copy
    }
}

// The ASSIGN arm of Array<MaterialParameterSet>::SetSize, read at 0x180060dd5.
// Retail ZEROES each sub-array header before calling operator= on it, because the
// destination is raw MemAlloc memory and operator= starts by DESTROYING the
// destination — i.e. by freeing whatever pointer it finds there. Skipping the
// zero would hand MemFree a garbage pointer.
//
// B.4: the ArraySpec callbacks take the element as bytes (array_internal.h's
// signature); each views it ONCE as the `MaterialParameterSet` it is (floor: elem_stride).
void PassAssign(uint8_t* dstv, const uint8_t* srcv) {
    MaterialParameterSet* const dst = reinterpret_cast<MaterialParameterSet*>(dstv);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    const MaterialParameterSet* const src = reinterpret_cast<const MaterialParameterSet*>(srcv);   // the erased container core's element callback (void* dst / src): elem_stride  // CAST-OK: elem_stride

    dst->textures._data = nullptr;                        // MOV [RBX],R8 (R8==0)
    dst->textures._size = 0;                              // MOV [RBX+8],0 (one qword)
    dst->textures._capacity = 0;
    Array8Assign(&dst->textures, &src->textures);

    dst->constants._data = nullptr;                       // LEA RCX,[RBX+0x10] ...
    dst->constants._size = 0;
    dst->constants._capacity = 0;
    Array8Assign(&dst->constants, &src->constants);
}

// The INIT arm, read at 0x180060e1b: four zeroed qwords, nothing else.
void PassInit(uint8_t* dstv) {
    MaterialParameterSet* const dst = reinterpret_cast<MaterialParameterSet*>(dstv);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    dst->textures._data  = nullptr;  dst->textures._size  = 0;  dst->textures._capacity  = 0;
    dst->constants._data = nullptr;  dst->constants._size = 0;  dst->constants._capacity = 0;
}

// The DESTROY arm — retail 0x180005680, a loop of `ArrayDtorImpl` over the two
// sub-arrays. THE ORDER IS +0x10 FIRST, THEN +0x00 (RBX starts at base+0x10 and
// RSI trails it by 0x10, 0x1800056b1/0x1800056ff). Preserved: the two frees reach
// the allocator in that order and the allocator is shared state.
void PassDestroy(void* /*ignoredThis*/, uint8_t* base, int count) {
    if (count <= 0) return;                       // TEST R8D,R8D / JLE ret
    MaterialParameterSet* const sets = reinterpret_cast<MaterialParameterSet*>(base);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    for (int i = 0; i < count; ++i) {
        // `e` is one `MaterialParameterSet` (Material.h): `constants` @ +0x10,
        // `textures` @ +0x00 — trait-equivalent for the same MaterialBinding
        // reason Array8Assign's dtor call gives.
        MaterialParameterSet* const set = sets + static_cast<int64_t>(i);
        set->constants.~Array();
        set->textures.~Array();
    }
}

// The element assign for (b): one 8-byte copy.
void Binding8Assign(uint8_t* dst, const uint8_t* src) {
    std::memcpy(dst, src, sizeof(MaterialBinding));
}

const ArraySpec kSpec_MatPass = {                 // 0x180060d30
    "Array<Graphics::MaterialParameterSet>", 0x20, 8,
    &PassAssign, &PassInit, &PassDestroy,
    /*byteCopy=*/false,
    /*assignUnwritten=*/0, 0, /*initUnwritten=*/0, 0, /*seedZero=*/0, 0x20 };

// `byteCopy` is FALSE and `seedZero` covers the WHOLE 0x20-byte element, both
// deliberately: the element operations DEREFERENCE every part of it (each
// sub-array's `_data` goes to MemFree), so a probe that seeded this element with
// a varying non-zero pattern would hand the allocator two wild pointers on the
// first assign. That is exactly what array_internal.h's `seedZero` exists for.
// *** NO PROBE DRIVES kSpec_MatPass TODAY *** — said out loud so a correctly
// filled-in field is not read as evidence that one does.

const ArraySpec kSpec_Binding8 = {                // INLINED into BuildParameters
    "Array<Graphics::MaterialBinding>", 8, 4,
    &Binding8Assign, nullptr, nullptr,
    /*byteCopy=*/true, 0, 0, 0, 0, 0, 0 };

// Retail 0x180060d30, 420 bytes, NOT EXPORTED. Registered BY ADDRESS below.
//
// ENDGAME B.3.2 step 2 (callers) — bridged through `AsArray<&kSpec_MatPass>`
// rather than naming `rtcoll::SetSizeCore` directly. NOT trait-equivalent:
// `MaterialInstance::parameters` is a real `Collection::Array<
// MaterialParameterSet>` (Material.h), but MaterialParameterSet has no
// user-defined copy constructor, so a trait derivation would placement-copy
// it via the implicit (shallow, `=default`) copy of its two inner Arrays —
// not the zero-then-deep-assign `PassAssign` performs.
extern "C" void RtCollection_Array_MaterialParameterSet_SetSize(void* a, int n) {
    rtcoll::AsArray<&kSpec_MatPass>(a)->SetSize(n);
}

// ============================================================================
// §2.  ?BuildParameters@MaterialInstance@Graphics@@QEAAXXZ
//      retail 0x1801033e0, ordinal 1678. 1421 bytes / 361 instructions.
// ============================================================================
// Resolves every texture and constant NAME the bound Material declares into an
// INDEX into this instance's own texture/constant arrays, and caches the
// resulting {index, shaderSlot} pairs per pass. `?Setup@MaterialInstance@`
// (already ported, Wave 21 lane GFXBODY) is the consumer.
//
// src/engine/graphics/Material.h WAS ALREADY PINNED FROM THIS BODY by Wave 6
// lane F — it names BuildParameters beside `hasRenderTexture`,
// `hasRenderedTexture`, `shaderClass` and `MaterialParameterSet`. Every one of
// those readings held against the disassembly, which is worth stating because
// the SIGN of such an agreement carries no information until somebody runs it
// (reference_ghidra_c_output_vs_disassembly.md). Nothing in Material.h needed a
// correction.
//
// THE TWO Get*Name CALLS RETURN A String BY VALUE, SO THERE IS A HIDDEN RETURN
// SLOT, and it is the SECOND argument (RCX=material, RDX=&slot, R8D=pass,
// R9D=index). An `fn_*` typedef that omitted it would be a wild write that still
// returned the right answer, because RAX comes back holding the slot address
// either way (reference_hidden_retslot_wild_write.md). Both mangled names say
// `?AVString@System@@` for the return, so this is read off the export table, not
// guessed from the call site.
//
// THE FINAL LOOP IS ASYMMETRIC AND THE ASYMMETRY IS RETAIL'S. `+0x24` is
// CLEARED before the loop (`MOV BYTE PTR [R15+0x24],0x0` at 0x18010390c) and
// `+0x25` is NOT — it is loaded into R8B (`MOVZX R8D,BYTE PTR [R15+0x25]` at
// 0x18010391b) and OR'd into. So a second BuildParameters on the same instance
// can only ever ADD to `hasRenderedTexture`, never clear it, while
// `hasRenderTexture` is recomputed from scratch. Transcribed, not tidied.
// ----------------------------------------------------------------------------
namespace {

// The per-pass texture/constant DESCRIPTION arrays hang off the material's pass
// object at +0x30 -- B.4: that is the pass's `Video::VideoProgram::_pixelProgram`,
// and the two arrays are its `ShaderProgram::_textures` (+0x20, 0x1801034b0 /
// 0x180103479) and `_constants` (+0x30, 0x18010377d / 0x1801036ca), both of
// 0x10-byte `ShaderProgram::Parameter`s. Within a record: `dest` (+0x08) is the
// shader slot, and for TEXTURES ONLY `internal` (+0x0c) is a SKIP flag
// (`CMP BYTE PTR [RDX+R12*1+0xc],0x0 / JNZ` at 0x1801034b4). The constant loop
// has NO such test — checked, not assumed by symmetry.

// ENDGAME B.3.5 block GC: the receiver is the REAL `Graphics::MaterialInstance`
// and the three-step chain is spelled with the two real classes' own members —
// `_material` (`ExternalPtr<Material>` by value, +0x28) then that `Material`'s
// `_passes` array (+0x28). Retail's `MOV RAX,[R15+0x28] / MOV RCX,[RAX+0x28] /
// MOV RAX,[RSI+RCX*1]` is unchanged; only the spelling is, and `Material.h`'s
// `static_assert`s now hold the offsets.
inline const Video::PixelProgram* PassPixelProgram(const Graphics::MaterialInstance* m,
                                                   int32_t pass) {
    // Re-read from `this` on EVERY use in retail (five separate reloads inside
    // the two inner loops), so it is a function rather than a hoisted local.
    return m->_material.Get()->_passes._data[pass].Get()->_pixelProgram.Get();
}

}  // namespace

// The BODY is at the bottom of this file, as `Graphics::MaterialInstance::BuildParameters`.
// ============================================================================
// §3.  ?Draw@Renderer@Graphics@@QEAAXAEAVContext@Video@@MAEBVScene@2@AEBVCamera@2@@Z
//      retail 0x180104bb0, ordinal 2258. 920 bytes / 191 instructions.
// ============================================================================
// The frame's top-level render dispatch, and it is TWO PASSES over the scene
// entry array, not one:
//
//   PASS 1 (forward, 0x180104c31..0x180104d29) — CULL AND COLLECT. For each
//       scene entry: BeginDrawGroup, walk the scene's drawable list calling
//       AddDrawItems (vtable slot 3) on everything whose pass mask matches and
//       whose +0x28 is within the entry's field_f8, optionally
//       Scene::AddCulledDrawItems, then SORT each of the three per-pass item
//       lists.
//
//   PASS 2 (BACKWARD, 0x180104d60..0x180104f1d) — EMIT. r13 counts DOWN from
//       scenes._size-1 to 0 (`DEC R13 / JNS`). For each entry: a prepare call
//       on every drawable (slot 9), then rebind the render buffer, reconcile the
//       viewport and the scissor, Clear, and finally three passes x N drawables
//       of the draw call (slot 10).
//
// THE BACKWARD DIRECTION IS NOT COSMETIC — render-to-texture entries are
// produced before the entry that samples them, so reversing the order would
// sample a stale target. It is transcribed as a decrementing signed counter with
// the `JNS` termination retail uses, not rewritten as a forward loop.
//
// WAVE 6 LANE F ALREADY PINNED EVERY STRUCT THIS BODY TOUCHES, FROM THIS BODY.
// src/engine/graphics/Camera.h says so in as many words ("One scene entry,
// stride 0x110. Pinned by Renderer::Draw (0x180104bb0), which reads every one of
// these fields"). This lane re-read all of them against the disassembly and
// found NO correction needed — including the two deliberate holes at Renderer
// +0x3c and +0x4c, which this body is the reason for: it ASSIGNS +0x3c from a
// scene entry in pass 2 before anything reads it, and reads it in pass 1 before
// anything writes it, so on the first frame AddScene is handed whatever the
// caller's memory held. That is retail's behaviour and this port reproduces it
// rather than zeroing the field.
// ----------------------------------------------------------------------------
namespace {

using Graphics::RendererDrawItem;

// (B3X W0: slot 3 of a scene drawable -- `Geometry::AddDrawItems` -- and slots
// 9 / 10 of a Renderer drawable -- `Light::DrawShadowMap` / `DrawLight` -- are
// by-name calls on the real classes now. B.4: Context slot 4, Clear, is
// `Video::Context::Clear` by name, and the Reset / AddScene / BeginDrawGroup
// typedefs went with their doors.)

// ============================================================================
// Graphics::Renderer::DrawItemSort       retail 0x180062d60  (2311 bytes)
// Graphics::Renderer::DrawItemSortSmall  retail 0x180063b90  ( 344 bytes)
//   - BATCH 8b.  Replaces BATCH 8's std::sort, which was NOT faithful.
// ============================================================================
// Neither address is exported (checked against the PE export directory), so
// both are address-only rows. Both are now ported and registered; the
// `ResolveAddr` in Draw's pass loop therefore lands on our body, because the
// resolver consults the port registry first.
//
// WHY THE PREVIOUS PORT WAS REPLACED. BATCH 8 wrote this as
// `std::sort(first, last, DrawItemLess)` and argued that the comparator is a
// strict total order over distinguishable items, so any correct sort gives the
// same array and the algorithm need not be retail's. Reading the instructions
// instead of reconstructing them shows two defects:
//
//   1. THE FLOAT TERM WAS BACKWARDS. At 0x180062e0e retail does
//        MOVSS XMM0,[mid+0x1c] / MOVSS XMM1,[lo+0x1c]
//        COMISS XMM0,XMM1 / JA  <swap, i.e. "mid sorts before lo">
//        COMISS XMM1,XMM0 / JA  <no swap>
//      so `sortKey` is DESCENDING. The std::sort port had `a.sortKey <
//      b.sortKey`. Every frame's draw order was wrong in that term.
//   2. "EQUAL IMPLIES INDISTINGUISHABLE" IS FALSE HERE. `arg8` (+0x20) and
//      `_pad24` (+0x24) are never compared. Two items that tie on all eight
//      compared terms can still differ in `arg8`, and then which one lands
//      first is decided by the swap sequence - so the swap sequence must be
//      retail's.
//
// FUN_180063b90 IS NOT AN INSERTION SORT, which BATCH 8 also assumed when it
// declined to give it a body ("std::sort's own small-range handling subsumes
// it"). It is a SELECTION SORT that repeatedly moves the MAXIMUM of [lo,hi] to
// `hi`: 0x180063c47 `MOV RDI,R8` sits on the JB arms, so the tracked position
// advances only on STRICTLY less. It is a distinct closure row and is ported
// and registered here in its own right.
//
// The skeleton itself lives in ../collection/introsort_msvc.h, transcribed once
// from these bytes and instantiated a second time for the Array<Mod> pair
// (0x180028760 / 0x180028fc0), which is the same template with a different
// element, comparator and swap.
//
// NO ORACLE WRAPPER: `sort` mutates the pass list in place and returns nothing,
// so there is no value to replay. The diff harness sees it as a registered
// address; the verdict is Draw's per-frame attendance climbing with the frame
// on screen unchanged.
// ============================================================================

// DRAWSORT (2026-10-13): the comparator and the swap MOVED, unchanged, to
// src/engine/graphics/draw_sort.h (`DrawSort::DrawItemLess` / `DrawItemSwap`),
// and the two bodies below to draw_sort.cpp (`DrawSort::SortReference` /
// `SortReferenceSmall`), so that the unit tests can drive the reference and the
// faster exact sort side by side without this file. The reading above is still
// the evidence for every term and every swap.
}  // namespace

// 0x180063b90 - the small-range pass. `lo`/`hi` are INCLUSIVE bounds, as
// 0x180062dc1 passes them (`MOV RDX,R15 / MOV RCX,RBP`).
extern "C" void RtGraphics_Renderer_DrawItemSortSmall(RendererDrawItem* lo,
                                                      RendererDrawItem* hi) {
    DrawSort::SortReferenceSmall(lo, hi);
}

// 0x180062d60. Matches the call site's `(data, int32_t count, void* scratch)`;
// `scratch` is retail's spare register window and the body never reads R8.
// Both callers (Renderer::Draw's pass loop, Renderer::EndDrawGroup) come through
// here. DrawSort::Sort is retail's sort when `DrawSort::kFastSort` is false, and
// otherwise the exact keyed sort (draw_sort.h): the same array afterwards, bit
// for bit.
extern "C" void RtGraphics_Renderer_DrawItemSort(RendererDrawItem* data, int32_t count, void*) {
    DrawSort::Sort(data, count);
}

// ============================================================================
// §4.  ?Update@AnimationState@Graphics@@QEAA_N...
//      retail 0x1800fdb20, ordinal 4474. 1062 bytes / five chained .pdata
//      fragments. NINE arguments.
// ============================================================================
// THE ARGUMENT MAPPING WAS SETTLED THREE WAYS, not read off the prologue:
//
//   (1) the EXPORTED MANGLED NAME spells all eight parameters and their types:
//       (const Time&, const Array<GraphicsMesh::Node>&, const Transform&,
//        InterpolatedState<Transform>*, InterpolatedState<Transform>*,
//        Vector3&, bool, bool).
//   (2) the CALLER, `?Update@AnimationManager@Graphics@@` — already ported in
//       wave23_lane_gfxbody.cpp — passes them in exactly that order, and its
//       lane read the call site from the disassembly.
//   (3) the HOME-SLOT ARITHMETIC of this body, computed and checked: four pushes
//       (0x20) plus `SUB RSP,0x98` puts the caller's frame base at RSP+0xB8, so
//       [RSP+0xD8]=arg4(R9 home), [RSP+0xE0]=arg5, [RSP+0xE8]=arg6,
//       [RSP+0xF0]=arg7, [RSP+0xF8]=arg8, [RSP+0x100]=arg9. Every one of those
//       six is loaded at exactly the offset that arithmetic predicts, and
//       [RSP+0xF0] is the pointer whose three floats are zeroed in the prologue
//       — which is what the mangled name calls `AEAVVector3@8@`.
//
// Three independent derivations agreeing is worth stating because ONE of them
// would not have been: an nine-argument prototype read off the prologue alone is
// exactly where Ghidra drops arguments.
//
// NO HIDDEN RETURN SLOT. The return is `_N` (bool, in AL: `MOV AL,0x1` at
// 0x1800fdf32 and `XOR AL,AL` at 0x1800fdb66), so the four register arguments
// are not shifted (reference_hidden_retslot_wild_write.md — the trap checked for
// and found absent here).
//
// TIME IS TICKS, AND THE SCALE IS A RUNTIME GLOBAL, NOT A CONSTANT.
// `MULSD XMM0,[0x1801fe728]` looks like a literal. It is not: 0x1801fe728 sits
// at offset 0x8728 in `.data`, whose SizeOfRawData is 0x5400 — i.e. in the
// uninitialised tail. It is `System::Timer::_secondsPerTick`, already resolved
// in the tree at engine/funcs/engine_funcs.cpp:1546, and it is written at engine
// startup. A port that baked in a number read from the file would have baked in
// zero. Found by trying to read the bytes and getting none.
//
// THE dt==0 ARM CONTAINS A NO-OP AND IT IS TRANSCRIBED AS ONE. At
// 0x1800fdc50..0x1800fdc7c retail loads seven dwords from `[RBX-0x10 .. RBX+8]`
// and stores them straight back to the SAME seven addresses, then does the real
// work. It is MSVC's rendering of `a = a` on an inlined Transform assignment. It
// has no observable effect, so this port omits the stores and says so here
// rather than writing seven self-assignments the compiler would delete anyway.
// ----------------------------------------------------------------------------
namespace {

// `System::Timer::_secondsPerTick`, retail 0x1801fe728 — a DOUBLE in .data's
// uninitialised tail, written at engine startup. ENDGAME B.3.9 close (D7): the
// class static, read fresh -- the cached by-address door is gone.
inline double SecondsPerTick() {
    return ::System::Timer::_secondsPerTick;
}

}  // namespace

// ENDGAME B.3.5 block GC: THE BODY MOVED to
// `Graphics::AnimationState::Update`, in this file's trailing `namespace Graphics`
// block. It cannot live here: everything above is inside `namespace rtgfxbig` and
// `Graphics` is its SIBLING, not its child (B34_BRIEFS A.2). The door keeps its
// name and its counter. B.4: its one caller (`AnimationManager::GetInstanceId`,
// animationstate_transforms.cpp) holds typed pointers, so the door takes them.
extern "C" bool RtGraphics_AnimationState_Update(
        Graphics::AnimationState* self, const System::Time* time,
        const Graphics::GraphicsMesh::NodeArray* nodes,
        const Math::Transform* baseTransform,
        Graphics::InterpolatedState<Math::Transform>* interp, Math::Transform* pose,
        Math::Vector3* outVec3, bool rootOnly, bool applyEvents) {
    Attend("AnimationState::Update", g_animStateCalls);
    return self->Graphics::AnimationState::Update(*time, *nodes, *baseTransform,
                                                  interp, pose, *outVec3,
                                                  rootOnly, applyEvents);
}

// One line beside the other wave summaries, so a run that entered none of these
// says so rather than staying silent. The BRANCH counters sit next to the door
// counters on purpose: `Draw calls=N sorts=0` is a completely different finding
// from `Draw calls=0`.
void RtGraphicsWave27GfxbigSummary() {
    printf("[GFXBIG27] BuildParameters=%llu (bindings=%llu) Draw=%llu "
           "(entries=%llu sorts=%llu) AnimStateUpdate=%llu (layers=%llu expired=%llu)\n",
           g_buildParamCalls, g_buildParamBinds, g_drawCalls, g_drawGroups, g_drawSorts,
           g_animStateCalls, g_animStateLayers, g_animStateExpired);
}

}  // namespace rtgfxbig

void RtGraphicsWave27GfxbigSummary() { rtgfxbig::RtGraphicsWave27GfxbigSummary(); }

// ############################################################################
// ENDGAME B.3.5 step 5, block GC -- the member bodies whose live doors are the
// `extern "C"` functions above.
//
// Their own `namespace Graphics { }` block, because a member definition has to
// appear in a namespace ENCLOSING the class and `Graphics` / `RtGraphics` are
// siblings (B34_BRIEFS A.2). Every file-local helper here lives in
// `rtgfxbig`'s own unnamed namespace, NOT the global one, so unqualified lookup
// does not reach it from `namespace Graphics` -- each is spelled `rtgfxbig::`,
// the trap wave28_lane_gfxbody.cpp:957 records.
//
// The transcription banners stay beside the doors above; only the spelling of
// the receiver changed.
// ############################################################################

namespace Graphics {

// ?BuildParameters@MaterialInstance@Graphics@@QEAAXXZ   retail 0x1801033e0
void MaterialInstance::BuildParameters() {
    // MOV RDX,[RCX+0x28] / TEST RDX,RDX / JZ epilogue. The early return skips
    // the whole function INCLUDING the +0x24/+0x25 accumulation block.
    Graphics::Material* const material = _material.Get();
    if (material == nullptr) return;

    _shaderClass = static_cast<uint32_t>(material->_type);      // material+0x20

    // Array<RtGraphics::MaterialParameterSet>::SetSize(material->passCount). 0x180060d30.
    // `material + 0x30` is `Material::_passes._size` — the pass COUNT, which is
    // why `rtgfxbig::kPassDesc` and the pass object's own +0x30 share a number.
    rtgfxbig::RtCollection_Array_MaterialParameterSet_SetSize(
        &_parameters, material->_passes.GetSize());

    // CMP [R15+0x38],R8D / JLE — the bound is `_parameters._size`, and it is
    // RE-READ at the bottom of the loop (0x1801038fe), not hoisted.
    for (int32_t pass = 0; pass < _parameters.GetSize(); ++pass) {
        const Video::PixelProgram* const desc0 = rtgfxbig::PassPixelProgram(this, pass);

        // ---- textures -----------------------------------------------------
        // `desc ? desc->count : 0` — retail's own null arm (XOR EAX,EAX at
        // 0x180103475), computed ONCE per pass and then used as a DOWN counter.
        const int32_t texCount = desc0 ? desc0->_textures._size : 0;
        for (int32_t i = 0; i < texCount; ++i) {
            const Video::PixelProgram* const desc = rtgfxbig::PassPixelProgram(this, pass);
            if (desc->_textures._data[i].internal != 0) continue;   // skip flag

            RtGraphics::MaterialBinding v;
            {
                // B.3.5 block GC item 6: two `RT_EXPORT_ONCE` edges onto classes
                // that are now real become direct QUALIFIED member calls. Retail
                // hands GetTextureName its uninitialised stack slot [RSP+0x30]
                // (0x1801034c3, no store before the call): the by-value return
                // constructs `name` there.
                System::String name =
                    material->Graphics::Material::GetTextureName(pass, i);

                v.sourceIndex = this->Graphics::MaterialInstance::FindTexture(name);

                // ~name here is retail's inlined String::Release, byte for byte:
                // decrement the refcount at data-4, and on zero rewind to data-8
                // and free, the sharing branch nulling the slot.
                //
                // ONE INSTRUCTION DIFFERS AND IT CANNOT FIRE: retail tests
                // `data - 8 == 0` before freeing (the ZF of `SUB RDI,0x8` at
                // 0x1801034f5), which is true only if `data == 8`. ~String
                // instead relies on the null test inside 0x1800c7ad0. Recorded
                // rather than quietly treated as identical.
            }

            // The shader slot is read AFTER the call, through a FRESHLY RELOADED
            // chain (0x180103546..0x18010355f). Retail reloads because
            // FindTexture takes a non-const `this` and could in principle move
            // the material's arrays; transcribed unhoisted for that reason.
            v.shaderSlot = rtgfxbig::PassPixelProgram(this, pass)->_textures._data[i].dest;

            // NOT RETAIL (review S-003): a binding Setup cannot apply is not appended
            // (material_binding_bounds.h). Retail appends it unchecked.
            static_assert(sizeof(Video::Context::_imageBuffers) ==
                              MaterialBindingBounds::kTextureSlots * sizeof(void*),
                          "S-003: the texture slot bound is Context::_imageBuffers");
            static_assert(sizeof(Video::Context::PixelConstantStaging::material) ==
                              MaterialBindingBounds::kConstantSlots * 4 * sizeof(float),
                          "S-003: the constant slot bound is PixelConstantStaging::material");
            if (!MaterialBindingBounds::IsTextureBinding(v.sourceIndex, v.shaderSlot)) {
                continue;
            }

            // `_parameters._data` is RE-READ here (MOV RSI,[RBP] at 0x180103563)
            // rather than kept from the top of the pass loop.
            RtGraphics::MaterialParameterSet* const set = &_parameters[pass];
            // retail 0x1801035c4, kSpec_Binding8 — trait-equivalent:
            // MaterialBinding (Material.h) is a plain POD, so
            // SpecFor<MaterialBinding>() is the same {8-byte copy, no init,
            // no destroy, align 4} shape.
            set->textures.SetSize(set->textures.GetSize() + 1, v);
            ++rtgfxbig::g_buildParamBinds;
        }

        // ---- constants ----------------------------------------------------
        const Video::PixelProgram* const desc1 = rtgfxbig::PassPixelProgram(this, pass);
        const int32_t conCount = desc1 ? desc1->_constants._size : 0;
        for (int32_t i = 0; i < conCount; ++i) {
            // NO SKIP FLAG on this arm — see the PassPixelProgram note above.
            RtGraphics::MaterialBinding v;
            {
                // block GC item 6, as in the textures arm above; ~name is the
                // release before the shader-slot reload.
                System::String name =
                    material->Graphics::Material::GetConstantName(pass, i);
                v.sourceIndex = this->Graphics::MaterialInstance::FindConstant(name);
            }

            v.shaderSlot = rtgfxbig::PassPixelProgram(this, pass)->_constants._data[i].dest;

            // NOT RETAIL (review S-003): as in the textures arm.
            if (!MaterialBindingBounds::IsConstantBinding(v.sourceIndex, v.shaderSlot)) {
                continue;
            }

            RtGraphics::MaterialParameterSet* const set = &_parameters[pass];
            // retail 0x1801037ec, kSpec_Binding8 — trait-equivalent, same as
            // the textures arm above.
            set->constants.SetSize(set->constants.GetSize() + 1, v);
            ++rtgfxbig::g_buildParamBinds;
        }
    }

    // ---- the render-texture latches ---------------------------------------
    // See the banner: +0x24 is cleared and recomputed, +0x25 is only ever OR'd
    // into. Both are stored back INSIDE the loop on every iteration, and the
    // texture's `image` pointer is RE-READ between the two tests (`MOV RAX,[RCX]`
    // at both 0x180103930 and 0x180103940). All three quirks transcribed.
    _hasRenderTexture = 0;
    const int32_t texN = _textures._size;
    if (texN <= 0) return;

    uint8_t  acc24 = 0;
    uint8_t  acc25 = _hasRenderedTexture;     // NOT reset — retail's asymmetry
    const RtGraphics::MaterialTexture* p = _textures._data;
    for (int32_t k = 0; k < texN; ++k) {
        // `image->_kind` (+0x2c): 4 is a render texture, 5 a rendered one.
        const Video::ImageBuffer* img = p->image;
        acc24 = static_cast<uint8_t>(acc24 | (img->_kind == 4 ? 1 : 0));
        _hasRenderTexture = acc24;

        img = p->image;                                        // re-read
        acc25 = static_cast<uint8_t>(acc25 | (img->_kind == 5 ? 1 : 0));
        _hasRenderedTexture = acc25;

        ++p;
    }
}

// ---------------------------------------------------------------------------
// ?Update@AnimationState@Graphics@@QEAA_N...   retail 0x1800fdb20
// ENDGAME B.3.5 block GC. The argument mapping (settled three independent ways),
// the tick/second conversions, the dt==0 snapshot and the layer-removal arm are
// all documented in the banner beside the door above.
//
// THE THREE `RT_EXPORT_ONCE` EDGES ONTO THIS CLASS'S OWN PRIVATE MEMBERS ARE
// GONE (block GC item 6). `CopyInstancedToUnique`, `UpdateRootOnly` and
// `UpdateNodes` are real members of `Graphics::AnimationState` now — the first
// two in `animationstate_transforms.cpp`, the third in
// `wave28_lane_gfxbody.cpp` — so retail's three direct CALLs are three
// QUALIFIED non-virtual member calls instead of three resolver hits. Their
// registered doors are untouched and still serve every caller outside this file.
//
// `?ApplyEvents@Animation@Graphics@@` IS CONVERTED (ENDGAME B.3.5 closing pass).
// The note that used to stand here said the receiver was "NOT a
// `Graphics::Animation` object -- it is an element of the group's clip array", as
// though those were alternatives. They are the same thing: see the proof at the
// call site below.
// ---------------------------------------------------------------------------
bool AnimationState::Update(const System::Time& timeRef,
                            const GraphicsMesh::NodeArray& nodes,
                            const Math::Transform& root,
                            InterpolatedState<Math::Transform>* interp,
                            Math::Transform* pose,
                            Math::Vector3& rootOffsetOut, bool rootOnly, bool applyEvents) {
    const int64_t deltaTicks = timeRef._ticks;

    // MOVSS [RBX],XMM7 x3 — the out Vector3 is zeroed before the early return,
    // so a caller that ignores the `false` still sees a defined value.
    rootOffsetOut.x = 0.0f; rootOffsetOut.y = 0.0f; rootOffsetOut.z = 0.0f;

    if (_layers._size == 0) return false;          // CMP [RCX+0x10],0 / XOR AL,AL

    _layers._data[0]._restarted = true;            // MOV BYTE PTR [RAX+0x30],1

    const int32_t nodeCount = nodes._size;         // MOVSXD R14,[R8+8]

    // seconds = ticks * _secondsPerTick, computed in DOUBLE and then narrowed to
    // float by CVTPD2PS — NOT computed in float. The association order is
    // retail's and is preserved exactly, because a moved term in a float sum is
    // algebraically identical and numerically different
    // (reference_moved_term_hides_where_terms_cancel.md).
    const float dt = static_cast<float>(
        static_cast<double>(deltaTicks) * rtgfxbig::SecondsPerTick());

    const int32_t layerCount = _layers._size;
    Layer* const lastLayer = _layers._data + (layerCount - 1);

    if (layerCount == 1 && lastLayer->_handle >= 0) {
        // CMP ECX,0x1 / JNE, then CMP [RDX+0xc],0 / JL, then MOV [RSI],CL —
        // and CL is the LAYER COUNT, which on this branch is exactly 1.
        _instanced = true;
    } else {
        if (_instanced) {
            this->Graphics::AnimationState::CopyInstancedToUnique(
                nodes, root, interp, lastLayer->_handle);
            _instanced = false;
        }
        if (rootOnly) {
            this->Graphics::AnimationState::UpdateRootOnly(dt, rootOffsetOut);
        } else {
            this->Graphics::AnimationState::UpdateNodes(
                dt, nodes, root, interp, pose, rootOffsetOut);
        }
    }

    // A zero frame delta SNAPSHOTS every node's interpolated pair, so an
    // interpolation at any alpha yields the same pose. Copy direction is
    // _current (+0x00) -> _previous (+0x1c). A trivially-copyable Transform
    // assignment is a bit copy (retail: seven dword moves).
    if (deltaTicks == 0 && nodeCount > 0) {
        for (int32_t i = 0; i < nodeCount; ++i)
            interp[i]._previous = interp[i]._current;
    }

    // ---------------- the layer loop --------------------------------------
    // The bound is RE-READ at the bottom (`CMP R12D,[RSI+0x10]` at 0x1800fdefd)
    // because the removal arm DECREMENTS it mid-loop.
    for (int32_t li = 0; li < _layers._size; ++li) {
        // `layers` is reloaded at the TOP of every iteration (MOV R15,[RSI+8]).
        Layer* const L = _layers._data + li;
        ++rtgfxbig::g_animStateLayers;

        // `?GetAnimation@AnimationGroup@` inlined: `_clips + clipIndex * 0x70`.
        const Animation& clip = L->_group->GetAnimation(L->_clipIndex);

        const int64_t duration = clip._duration;

        // At speed exactly 1.0 the frame delta is used VERBATIM in ticks — no
        // round trip through seconds. UCOMISS + JP + JNE, so a NaN speed takes
        // the scaled arm (the JP catches unordered). Preserved: a `== 1.0f`
        // comparison in C++ has the same NaN behaviour, but the ORDER of the two
        // branches is retail's.
        int64_t advance;
        if (L->_speed == 1.0f) {
            advance = deltaTicks;
        } else {
            // ticks = (int64)((double)(speed * dtSeconds) / _secondsPerTick).
            // MULSS in FLOAT first, then widened by CVTPS2PD, then DIVSD, then
            // CVTTSD2SI (truncation toward zero). Every step in that order.
            const float scaled = L->_speed * dt;
            advance = static_cast<int64_t>(static_cast<double>(scaled) / rtgfxbig::SecondsPerTick());
        }

        int64_t newTime = advance + L->_time._ticks;

        if (applyEvents && li == 0 && clip._events._size > 0) {
            // ApplyEvents(base, fromTime, toTime) — fromTime is the layer's
            // CURRENT time and toTime a local holding the new one. The store of
            // `newTime` into the layer happens AFTER this call (0x1800fdd80), so
            // the callee sees the OLD value through its second argument.
            const System::Time to = { newTime };
            clip.Graphics::Animation::ApplyEvents(root, L->_time, to);
        }

        L->_time._ticks = newTime;

        if (clip._looping) {
            if (newTime >= duration) {
                // Retail's do/while subtract, not a modulo: `SUB RBX,RDI` in a
                // loop while `RBX >= RDI` (0x1800fdd90). Identical result for
                // positive duration and NON-TERMINATING for duration == 0 in
                // both retail and here — the port does not "fix" that, because a
                // zero-duration looping clip is a data defect retail would also
                // hang on and a guard would hide it.
                do { newTime -= duration; } while (newTime >= duration);
                L->_time._ticks = newTime;

                // The restart offset. loopFrame == 0 short-circuits the entire
                // fraction to zero (TEST EAX,EAX / JNZ at 0x1800fdd9f), which is
                // NOT the same as computing it and getting zero: with
                // frameLast == frameFirst the division would be 0/0.
                float offsetSeconds;
                if (clip._loopFrame == 0) {
                    offsetSeconds = 0.0f;
                } else {
                    const float num = static_cast<float>(clip._loopFrame - clip._startFrame);
                    const float den = static_cast<float>(clip._endFrame  - clip._startFrame);
                    // +0x50 is the clip's FRAME COUNT: fraction * frameCount is
                    // a frame index, and +0x48 converts that to seconds.
                    const float rate = static_cast<float>(clip._frameCount);
                    // (((num / den) * rate) * loopScale) — THREE separate
                    // instructions in this exact order (DIVSS, MULSS, MULSS at
                    // 0x1800fddc7..0x1800fddcf).
                    offsetSeconds = ((num / den) * rate) * clip._loopScale;
                }
                const int64_t offsetTicks = static_cast<int64_t>(
                    static_cast<double>(offsetSeconds) / rtgfxbig::SecondsPerTick());
                L->_time._ticks = offsetTicks + newTime;
            }
        } else if (newTime >= duration) {
            L->_time._ticks = duration;            // clamp, 0x1800fddf7
        }

        // ---- the fade countdown and the layer removal --------------------
        if (L->_fadeLength._ticks <= 0) continue;

        L->_fadeRemaining._ticks -= static_cast<int64_t>(
            static_cast<double>(dt) / rtgfxbig::SecondsPerTick());
        if (L->_fadeRemaining._ticks > 0) continue;

        ++rtgfxbig::g_animStateExpired;

        // Release the manager-side instance this layer's handle names. The
        // handle is RE-READ from the array (`MOV RAX,[RSI+8]` then
        // `MOV ECX,[R14+RAX+0xc]` at 0x1800fde2c) rather than reused from L.
        const int32_t handle = _layers._data[li]._handle;
        if (handle >= 0) {
            const uint32_t gi = AnimationManager::HandleInstance(handle);    // SAR then MOVZX CL
            // LEFT (B9): the two null tests (the singleton, then its instance
            // buffer) are the tree's; retail reads both unguarded. Kept, not
            // extended -- their removal is NEXT_SESSION §0.2's.
            AnimationManager* const mgr = AnimationManager::_singleton;
            AnimationManager::Instance* const groups =
                mgr != nullptr ? mgr->_instances._data : nullptr;
            if (groups != nullptr) {
                AnimationManager::Instance& g = groups[gi];
                if (--g._refCount == 0) {
                    g._group     = nullptr;
                    g._animIndex = -1;
                    // `Collection::Array<T>::SetSize` for the instance's node
                    // array (+0x10) and its state array (+0x20), both with
                    // newSize 0: the two instantiations by body name (B3R).
                    rtcoll::Rt_SetSize_18005aa30(&g._nodes, 0);
                    rtcoll::Rt_SetSize_18005b1e0(&g._states, 0);
                }
            }
        }

        // Erase layer `li` by shifting the tail down one slot. Retail copies
        // SEVEN QWORDS per element (0x1800fde9c..0x1800fded6), i.e. the whole
        // 0x38 stride, and RE-READS `_layers` and the count inside the shift
        // loop. Both transcribed.
        for (int32_t k = li; k + 1 < _layers._size; ++k) {
            Layer* const dst = _layers._data + k;
            if (dst == nullptr) break;             // retail tests the COMPUTED address
            dst[0] = dst[1];
        }
        --_layers._size;
        --li;                                      // DEC R12D then the INC at the top
    }

    return true;                                   // MOV AL,0x1
}

// ---------------------------------------------------------------------------
// ?Draw@Renderer@Graphics@@QEAAXAEAVContext@Video@@MAEBVScene@2@AEBVCamera@2@@Z
// retail 0x180104bb0 -- ENDGAME B.3.5 block GC. THE WHOLE FRAME. The two passes,
// the four kinds of virtual call, the _shaderDetail-read-uninitialised-on-frame-
// one note and the clear colour/flag immediates are in the banner beside the door.
//
// B.4: on the real classes' named fields -- the Renderer's own (`_sceneGroups`,
// `_drawGroups`, `_drawables`, `_currentSceneGroup` / `_currentDrawGroup`,
// `_shaderDetail` +0x3c, `_alphaT` +0x40, `_frameIndex` +0x44, `_detailId` +0x48),
// each `RendererSceneEntry`, the Scene's `_globals` list and each Geometry's
// `_materialTypeFlags` / `_detailId`; the Context setters and `Clear` by name.
// ---------------------------------------------------------------------------
void Renderer::Draw(Video::Context& context, float alpha, const Scene& sceneRef,
                    const Camera& cameraRef) {
    // Reset FIRST, then the two scalars, then AddScene.
    //
    // BLOCK GC ITEM 6: this was `RT_EXPORT_ONCE("?Reset@Renderer@Graphics@@…")`
    // reaching our own body through the resolver. `Reset` is a member of the real
    // `Graphics::Renderer` (block F), so it is a QUALIFIED non-virtual member call
    // -- retail's own direct CALL to 0x180105110. Same for `AddScene` and
    // `BeginDrawGroup` below.
    // SKINMEMO (NOT RETAIL, graphics/skin_memo.h): the bone memo is valid for
    // exactly this call. Its epoch moves at entry and again at exit (the guard's
    // dtor), so no entry made in one Renderer::Draw can answer another.
    const SkinMemo::Scope skinMemoScope;

    this->Graphics::Renderer::Reset();

    _alphaT     = alpha;                     // MOVSS [RDI+0x40],XMM6
    _frameIndex = context._frameIndex;        // MOV EAX,[RSI+8] / MOV [RDI+0x44],EAX

    // _shaderDetail is read here UNINITIALISED on the first frame. See the banner.
    // LEFT: Context::_defaultRenderBuffer is `void*` (Context.h).
    this->Graphics::Renderer::AddScene(
        &sceneRef,
        static_cast<const Video::RenderBuffer*>(context._defaultRenderBuffer),
        cameraRef, _shaderDetail, 0x40, static_cast<int32_t>(MaterialPass::Normal), Bits(kMainScenePassMask));

    // ---------------- PASS 1: cull and collect ----------------------------
    _currentSceneGroup = 0;
    if (_sceneGroups._size > 0) {
        do {
            RendererSceneEntry* const e = _sceneGroups._data + _currentSceneGroup;

            _shaderDetail = e->_shaderDetail;
            _detailId     = e->_detailId;

            // Retail passes the entry's ONE camera as BOTH the draw camera and
            // the cull camera (`MOV R9,R8` at 0x180104c5f); transcribed.
            const int32_t nextGroup = this->Graphics::Renderer::BeginDrawGroup(
                e->_renderBuffer, e->_camera, e->_camera,
                e->_shader, e->_materialTypeFlags);

            // The CURRENT group index is written into the entry, and it is the
            // value from BEFORE this BeginDrawGroup (0x180104c78 reads
            // [RDI+0x34], which is only updated at the bottom of the loop).
            e->_drawGroupIndex = _currentDrawGroup;

            const uint32_t passMask =
                _drawGroups._data[_currentDrawGroup]._materialTypeFlags;

            // The scene's global drawable list (`Scene::_globals`, +0x60): each
            // node's element is the Geometry, `next` the link (+0x10).
            for (const Collection::LinkedList<Geometry*>::Node* node = sceneRef._globals._head;
                 node != nullptr; node = node->next) {
                const Geometry* const obj = node->element;
                if ((obj->_materialTypeFlags & passMask) != 0 &&
                    obj->_detailId <= _detailId) {
                    // Slot 3 = ?AddDrawItems@…@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z.
                    // A VIRTUAL DISPATCH through the object's own vtable, NOT a
                    // door: lane GFXMESH owns ??_7MeshInstance@ and this call
                    // reaches whatever is in the live table. By name since B3X W0.
                    obj->AddDrawItems(sceneRef, *this);
                }
            }

            if (_detailId != 0) {
                // ENDGAME B.3.5 caller batch: `Graphics::Scene::AddCulledDrawItems`
                // is a DEFINED member (cull.cpp:363) and the receiver is this
                // body's own `const Scene&` parameter, so this is the qualified
                // non-virtual member call retail makes.
                sceneRef.Graphics::Scene::AddCulledDrawItems(*this);
            }

            for (int32_t k = 0; k < 3; ++k) {
                Collection::Array<RendererDrawItem>& list =
                    _drawGroups._data[_currentDrawGroup]._items[k];
                // TWO tests, `> 0` then `> 1` (0x180104cfa, 0x180104cfe). Only
                // the second can decide anything; the first is retail's and is
                // kept because a zero-size list with a non-null _data would take
                // a different path if it were removed.
                if (list._size > 0 && list._size > 1) {
                    // RtGraphics_Renderer_DrawItemSort, above, with its
                    // 0x180063b90 selection-sort callee; called by name (B3R).
                    // Retail hands it its OWN unused R8 home slot (X+0x18) as
                    // scratch — a float argument in XMM2 leaves that slot free.
                    // Four qwords is strictly more than the 16 bytes retail's
                    // window can hold.
                    uint64_t scratch[4];
                    rtgfxbig::RtGraphics_Renderer_DrawItemSort(list._data, list._size, scratch);
                    ++rtgfxbig::g_drawSorts;
                }
            }

            ++_currentSceneGroup;
            _currentDrawGroup = nextGroup;
            ++rtgfxbig::g_drawGroups;
        } while (_currentSceneGroup < _sceneGroups._size);
    }

    // NOT RETAIL (CULLPAR, ext.gather_threads): pass 1 is over; the gather helpers
    // park (graphics/Scene.h). Null unless the helper pool exists.
    if (Scene::_extDrawPass1End != nullptr) Scene::_extDrawPass1End();

    // EXT SEAM (NOT RETAIL, issue #253): ext.staggered_passes. Pass 1 just decided whether the
    // sun holds its map; this thread emits the same frame (false unless the row is on).
    SunLight::ExtShadowHoldEmit(SunLight::ExtTakeShadowHeld());

    // ---------------- PASS 2: emit, BACKWARD ------------------------------
    // EXT SEAM (NOT RETAIL, docs/extensions/AO.md): ext.ambient_occlusion (renderer_ext_ao.h), read once
    // per frame; null (retail) unless the game's draw armed it for this call.
    const ExtAoFrame* const extAo = _extAo;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): the ground draw (Camera.h), read once per
    // frame; null (retail) unless the game's draw armed it for this call.
    const ExtGroundDrawFn extGround = _extGroundDraw;
    for (int64_t si = static_cast<int32_t>(_sceneGroups._size) - 1; si >= 0; --si) {
        // `_data` is RE-READ at the top of every iteration (MOV RBP,[RDI] at
        // 0x180104d60) while the byte offset is carried in RBX and decremented.
        RendererSceneEntry* const e = _sceneGroups._data + si;

        _shaderDetail = e->_shaderDetail;
        context._dirty |= RtVideo::kDirtyVertexProgram | RtVideo::kDirtyPixelProgram;   // OR [RSI+0x10],0xc0
        context._shaderDetail = e->_shaderDetail;   // MOV [RSI+0xc],EAX

        // Slot 9 on every drawable the entry names. 5th argument is a literal 1.
        for (int32_t j = 0; j < e->_drawableIds._size; ++j) {
            const int32_t id = e->_drawableIds._data[j];
            RendererDrawable& d = _drawables._data[id];
            d._object->DrawShadowMap(
                context, *this, d._value, 1);
        }

        // Rebind the render target only on a change: `Context::SetRenderBuffer`
        // (0x1800f2100) is exactly that compare, the dirty bit 1 and the store.
        context.SetRenderBuffer(e->_renderBuffer);

        // Viewport then scissor, each reconciled against the buffer's full
        // extent. The RenderBuffer pointer is RE-READ for each of the two blocks
        // (0x180104dec and 0x180104e37) rather than reused. `SetViewport`
        // (0x18001a060) is the viewport's compare / 0x200 / store; the scissor's
        // setter has no body in the tree, so its identical sequence is spelled.
        {
            const Video::RenderBuffer* const rb = e->_renderBuffer;
            Video::Viewport vp;
            vp.x = 0;
            vp.y = 0;
            vp.width  = rb->width;
            vp.height = rb->height;
            context.SetViewport(vp);
        }
        {
            const Video::RenderBuffer* const rb = e->_renderBuffer;
            const int32_t w = rb->width, h = rb->height;
            if (!(context._scissor.x == 0 && context._scissor.y == 0 &&
                  context._scissor.width == w && context._scissor.height == h)) {
                context._dirty |= RtVideo::kDirtyScissor;
                context._scissor.x = 0;             // x and y: retail's one 8-byte store
                context._scissor.y = 0;
                context._scissor.width = w;
                context._scissor.height = h;
            }
        }

        // Clear — `Context::Clear` (ContextData slot 4). The colour 0x1184d6 and
        // the flag word 6 are IMMEDIATES in the instruction stream (0x180104e8d,
        // 0x180104e85); the 1.0f depth is XMM6, loaded once at 0x180104d3d from
        // [0x18016169c], whose four bytes were READ (00 00 80 3f) rather than
        // assumed (reference_crt_coefficients_must_be_lifted_not_retyped.md).
        context.Clear(Video::ClearFlags::Depth | Video::ClearFlags::Stencil, 0x1184d6u, 1.0f, 0);

        // Three passes x every drawable. Slot 10.
        for (int32_t k = 0; k < 3; ++k) {
            for (int32_t j = 0; j < e->_drawableIds._size; ++j) {
                const int32_t id = e->_drawableIds._data[j];
                RendererDrawable& d = _drawables._data[id];
                d._object->DrawLight(
                    context, *this, e->_drawGroupIndex, d._value, k, e->_shader);
            }
            // EXT SEAM (NOT RETAIL, docs/extensions/AO.md): ext.ambient_occlusion, between the Opaque and
            // the Blended lists of the MAIN entry only (the reflection is shader 2).
            if (k == 0 && extAo != nullptr &&
                e->_shader == static_cast<int32_t>(MaterialPass::Normal) &&
                e->_renderBuffer == context._defaultRenderBuffer)
                ExtAoAfterOpaque(context, *extAo, *e);
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): the ground draw, at the same point
            // of the same entry, after the AO pass.
            if (k == 0 && extGround != nullptr &&
                e->_shader == static_cast<int32_t>(MaterialPass::Normal) &&
                e->_renderBuffer == context._defaultRenderBuffer)
                extGround(context, e->_camera);
        }
    }
    SunLight::ExtShadowHoldEmit(false);   // NOT RETAIL (ext.staggered_passes): this frame's emit is over
}

// NOT RETAIL: the ext.ambient_occlusion seam (Camera.h, renderer_ext_ao.h).
const ExtAoFrame* Renderer::_extAo = nullptr;
// NOT RETAIL: the ground-draw seam (Camera.h).
Renderer::ExtGroundDrawFn Renderer::_extGroundDraw = nullptr;

}  // namespace Graphics
