// ============================================================================
// src/engine/graphics/wave9_lane_gfx.cpp — Wave 9 lane GFX
// ============================================================================
//
// Cutover boot attempt 06 left 61 exports resolving to NOTHING. Four of them
// are closed here, plus one callee each for two of them:
//
//   ?Lerp@Transform@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z          0x18001d690
//   ?Lerp@?$InterpolatedState@VTransform@Math@@@Graphics@@...       0x18001aff0
//   ?RenderTextures@MaterialInstance@Graphics@@QEBAX...             0x180102df0
//   ?AddDrawItem@Renderer@Graphics@@QEAAXPEBVGeometry@2@...         0x1801057b0
//   (the PerlinNoise ctor is closed in ../utility/perlin.cpp, where its body
//    already sat, written and unregistered, since Wave 8 lane E.)
//
// WHY A GRAPHICS FILE REGISTERS A MATH EXPORT. `Math::Transform::Lerp` is not
// in any lane's function list and NOTHING in this project's tree resolves it by
// name — it is here only because `InterpolatedState<Transform>::Lerp` is eleven
// instructions of argument shuffling around a CALL to it, and a port of the
// caller that reached the callee through the DLL would be a port that dies at
// cutover (reference_converting_a_door_is_not_satisfying_it.md). It is
// registered under this lane's kill switch, not Math's; that is a deliberate,
// stated coupling, not an oversight.
//
// FLOAT ASSOCIATION ORDER IS LOAD-BEARING. Wave 9 has already found one defect
// where a translation term folded in FIRST instead of being added LAST — the
// same value algebraically, a different value in bits, and visible in one call
// in 23,654 (reference_moved_term_hides_where_terms_cancel.md). Every float
// expression below is transcribed in the order the SUBSS/MULSS/ADDSS sequence
// gives it and must not be "simplified".
//
// KILL SWITCH: RT_GFX9=0 leaves all four on the DLL.
// ============================================================================

#include <cstdint>
#include <cstring>


#include "math/Math.h"
#include "graphics/Camera.h"
#include "graphics/GeneratedImageBuffer.h"
#include "graphics/Material.h"
#include "graphics/Scene.h"
#include "graphics/gather_prefetch.h"   // GATHERPF: g_gatherSlots (the COMPPROF gather row)
#include "object/TypeInfo.h"          // Object::Base::IsDerivedFrom
#include "system/MemoryAllocator.h"
#include "video/ImageBuffer.h"     // B.4: Video::ImageBuffer::_kind
#include "video/VideoProgram.h"    // B.4: the selected pass's three programs
#include "video/ShaderProgram.h"   // B.4: ShaderProgram::_byteCode

using Graphics::RendererDrawItem;

namespace {

// ---------------------------------------------------------------------------
// The type id RenderTextures tests against.
//
// Retail's instruction is `MOV EDX,[0x1801f9a20]`. That address is not an
// anonymous global: `pefile` on the shipped DLL says 0x1801f9a20 IS the export
// `?_typeInfo@GeneratedImageBuffer@Graphics@@0VBaseTypeInfo@Object@@A`
// (ordinal 4864), and the FIRST DWORD of an Object::BaseTypeInfo is its type
// id — the same shape src/engine/ui/element.cpp already reads for
// ClipRegionElement and ScrollElement. In the image at rest it is 0xffffffff;
// the reflection registry fills it in at startup.
//
// AT CUTOVER this symbol is NOT minted, so the resolve returns null, the id
// stays 0xffffffff, IsDerivedFrom is false for everything and the
// render-to-texture pre-pass silently does nothing. No crash — a degraded
// sub-feature, and strictly better than today's null AddDrawItem. It is
// written up as the one open caveat in docs/history/runtime_port/WAVE9_LANE_GFX.md §5.
//
// SPELLED WITH A LITERAL `ResolveExport` ON PURPOSE, rather than through the
// RT_DATA_SLOT macro that expands to exactly this. tools/rt_data_door_audit.py
// matches a door by the identifier immediately in front of the mangled literal;
// RT_DATA_SLOT is not in its door table, so a macro spelling files this read
// under "UNCLASSIFIED doors — triage these, they are not known to be safe".
// The semantics are identical (cache the HIT, retry a miss, so a read before
// the mint is not poisoned) and the audit can now see the door is port-aware.
// ---------------------------------------------------------------------------
// ENDGAME B.3.9 TI-REST: the class's own `DEFINE_TYPE` record, read fresh
// (the function-local cache over the minted record is gone, B9-10).
uint32_t GeneratedImageBufferTypeId() { return Graphics::GeneratedImageBuffer::TypeId(); }

// ---------------------------------------------------------------------------
// Collection::Array<RendererDrawItem>::Resize(int newCount, const T& fill)
// retail 0x180063680 — UNEXPORTED, so it is reproduced here rather than called.
//
// This is the template instantiation for the 0x38-byte draw-item element: the
// byte size is `IMUL RCX,RCX,0x38` at 0x1800636d3 and the copy loop moves seven
// qwords per element at 0x180063715..0x180063747. It is NOT a general helper
// shared with other element types, so reproducing it here duplicates nothing.
//
// Retail's growth rule, transcribed exactly (it is NOT the usual doubling):
//   * newCount == oldCount            -> return, no work at all
//   * newCount <= capacity            -> no reallocation; only the NEW slots
//                                        [oldCount, newCount) are filled
//   * newCount >  capacity            -> capacity = newCount, EXCEPT that a
//                                        one-element growth from a non-empty
//                                        array sets capacity = newCount*3;
//                                        then allocate and copy [0, oldCount)
//                                        from the old buffer and fill the rest
//   * the old buffer is freed only if it actually changed and was non-null
//
// The `EAX = (RBP == this->_data) ? oldCount : 0` at 0x1800636e4 is what makes
// one loop serve both cases: on a reallocation it restarts at 0 and re-copies
// the surviving elements, on an in-place grow it starts at oldCount.
//
// ELEMENTS ARE COPIED AS RAW BYTES — seven qword MOVs, no element constructor,
// no destructor on the freed buffer. RendererDrawItem is trivially copyable so
// std::memcpy reproduces that exactly, INCLUDING the two padding gaps the
// caller never writes.
// ---------------------------------------------------------------------------
void DrawItemArrayResize(Collection::Array<RendererDrawItem>* self,
                         int32_t newCount,
                         const RendererDrawItem* fill) {
    const int32_t oldCount = self->_size;
    if (newCount == oldCount)
        return;

    RendererDrawItem* oldData = self->_data;
    self->_size = newCount;

    if (newCount > self->_capacity) {
        self->_capacity = newCount;
        // 0x1800636be: the *3 over-allocation applies ONLY to a single-element
        // append onto a non-empty array. A bulk resize allocates exactly what
        // was asked for, and the first append onto an empty array allocates 1.
        if (newCount == oldCount + 1 && oldCount != 0)
            self->_capacity = newCount * 3;
        self->_data = static_cast<RendererDrawItem*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(self->_capacity) * 0x38u, 8));
    }

    int32_t i = (oldData == self->_data) ? oldCount : 0;
    for (; i < newCount; ++i) {
        const RendererDrawItem* src = (i < oldCount) ? (oldData + i) : fill;
        std::memcpy(self->_data + i, src, sizeof(RendererDrawItem));
    }

    if (oldData != self->_data && oldData != nullptr)
        System::MemoryAllocator::MemFree(oldData);
}

}  // namespace

// ============================================================================
// Math::Transform
// Graphics::InterpolatedState<Math::Transform>::Lerp(const Scalar&) const
// ?Lerp@?$InterpolatedState@VTransform@Math@@@Graphics@@QEBA?AVTransform@Math@@AEBVScalar@4@@Z
// retail 0x18001aff0, ordinal 3188
//
// Eleven instructions. RCX = this, RDX = hidden return slot, R8 = &t:
//
//     MOV R9,R8  /  MOV R8,RCX  /  ADD RCX,0x1c  /  MOV RBX,RDX
//     CALL 0x18001d690        ; Transform::Lerp(this+0x1c, ret, this+0x00, t)
//     MOV RAX,RBX
//
// PLAN §4.2 deferred this in Wave 1 for want of `InterpolatedState<T>`'s
// layout; Wave 6 lane E pinned the layout (0x38 = two adjacent 0x1c-byte
// Transforms) but explicitly did NOT pin which half is which, and named this
// function as the one that would. It does: the FROM end is +0x1c and the TO end
// is +0x00, so +0x00 is CURRENT and +0x1c is PREVIOUS. That is now written into
// src/engine/graphics/Mesh.h.
//
// Wave 1's own note in src/engine/math/transform.cpp read this call site as
// having "only ONE Transform operand (at +0x1c) plus a bare Scalar (at +0x00),
// with no second Transform argument visible". That was wrong, and the missing
// operand is the `MOV R8,RCX`: `this` itself IS the second Transform. The note
// has been corrected in place.
// ============================================================================

// ============================================================================
// void Graphics::MaterialInstance::RenderTextures(Renderer&, const Scene&,
//                                                 const Camera&, int) const
// ?RenderTextures@MaterialInstance@Graphics@@QEBAXAEAVRenderer@2@AEBVScene@2@AEBVCamera@2@H@Z
// retail 0x180102df0, ordinal 3570
//
// Not on the 61-symbol cutover list in its own right — it is here because
// AddDrawItem calls it, and a port of AddDrawItem that reached it through the
// DLL would only MOVE the cutover row one frame deeper instead of closing it.
//
// Walks MaterialInstance::_textures (0x10 stride, count at +0x58), takes the
// ExternalPtr target at each entry's +0x08, and for every one whose +0x2c is
// exactly 5 AND which IsDerivedFrom(GeneratedImageBuffer) dispatches vtable
// slot +0x58 with (renderer, scene, camera, arg) — i.e. "render this
// render-to-texture now, before the item that samples it is drawn".
//
// TWO FAITHFUL UGLINESSES, both kept:
//   * `obj->+0x2c` is dereferenced BEFORE `obj` is null-checked (0x180102e49
//     vs 0x180102e61). A null entry faults in retail and faults here. Adding
//     the guard would not restore retail's behaviour, it would skip it
//     (CLAUDE.md, "don't fix a faithful crash with a null check").
//   * the loop bound `*(int*)(this+0x58)` is RE-READ every iteration
//     (0x180102e83), not hoisted, so a slot-58 handler that appends a texture
//     is seen. Hoisting it would change behaviour in exactly that case.
// ============================================================================
// ENDGAME B.3.5 block GC: THE BODY MOVED to
// `Graphics::MaterialInstance::RenderTextures`, at the bottom of this file. The
// door keeps its name and both its registrations (by name and at 0x180102df0).
//
// THE DEAD EFFECTIVENESS CONTROL IS GONE. The body opened with
// `const bool didWork = (hasRenderedTexture != 0) && (textures._size > 0);` and
// NOTHING ever read it — no counter, no printf, no summary line, and a repo-wide
// grep for `didWork` found this one declaration and no consumer. It was a
// warning, not a measurement, so it is deleted rather than carried into the
// member (the block brief's "dead scaffolding nobody reads" arm).
//
// B.4: the forwarding door is gone; its one caller, AddDrawItem below, calls the
// member.

// ============================================================================
// void Graphics::Renderer::AddDrawItem(const Geometry*, const MaterialInstance*,
//                                      unsigned short, unsigned short,
//                                      float, float, int, const void*, unsigned)
// ?AddDrawItem@Renderer@Graphics@@QEAAXPEBVGeometry@2@PEBVMaterialInstance@2@GGMMHPEBXI@Z
// retail 0x1801057b0, ordinal 1547
//
// ARGUMENT SLOTS, counted against the MANGLED NAME and confirmed against the
// prologue (`PUSH RDI; SUB RSP,0x70` = 0x78, so the 5th argument's home slot
// 0x28 lands at RSP+0xa0, and the body reads 0xa0/0xa8/0xb0/0xb8/0xc0/0xc8 —
// six stack arguments for arguments 5..10, which is exactly what GGMMHPEBXI
// predicts after the four register slots). NO hidden return slot: the return
// type is `X` (void).
//
// WHY THIS IS PORTED NOW AND WAS HELD BACK IN WAVE 6. Lane F gave two reasons:
// (1) no safe oracle, and (2) "it writes an element type this lane did not pin
// … the array helper it hands that to has a stride this lane did not read out."
// Reason (2) IS NOW FALSE and was falsifiable at the time: the helper is
// 0x180063680, its stride is the literal `0x38` in `IMUL RCX,RCX,0x38`, and the
// element is fully written out in src/engine/graphics/Camera.h.
// Reason (1) still stands and is honoured — see "ORACLE" below.
//
// THE THREE SILENT DROPS. Retail returns without appending anything when:
//   (a) the draw group's pass mask (+0x1bc) has no bit set for the material's
//       shader class (+0x20) — `BT EAX,R11D`, a 32-bit BT, so the index is
//       taken modulo 32;
//   (b) the group's shader index (+0x1b8) is negative, or >= the Material's
//       program count at +0x30;
//   (c) the selected VideoProgram (Material+0x28 indexed by that shader) has a
//       null +0x20, or a null +0x28/+0x30, or either of those two sub-objects
//       has +0x48 <= 0.
// This is the same list lane F recovered by decompilation; reading the bytes
// reproduces it unchanged, which is a cross-check, and it confirms the claim at
// src/diag/scene_watch.cpp:593 that AddDrawItem silently drops items.
//
// ORACLE: THERE IS NONE, AND THAT IS STATED RATHER THAN FAKED. The function's
// entire effect is an append into a LIVE array that the array helper may
// reallocate and free — running the shipped body a second time would either
// share `_data` with the real group (so "theirs" frees memory the live Renderer
// still points at) or need a deep copy of a group whose other 0x1c0 bytes this
// lane does not own. What IS instrumented:
//   * the diff oracle records how many calls reached the append at all, so a
//     "clean" run on a site that always dropped cannot read as verification
//     (reference_uncalled_port_is_not_verified.md);
//   * the diff oracle checks that the element the ARRAY HELPER landed in memory
//     is byte-identical to the record we handed it, over the 0x34 bytes that
//     are actually written. That tests THE HELPER — the one piece of new
//     transcription with a mechanical answer — and it does NOT test the port's
//     agreement with retail. Do not read it as if it did.
//
// The 0x34-not-0x38 span is the point of the two `_pad` fields: retail stores
// uninitialised stack bytes at +0x24 and +0x34 on every append, and a compare
// that included them would fail on garbage neither side wrote
// (reference_oracle_must_not_compare_unwritten_bytes.md).
// ============================================================================

// ############################################################################
// ENDGAME B.3.5 step 5, block GC — the member bodies whose live doors are the
// functions above. Their own `namespace Graphics { }` block (B34_BRIEFS A.2:
// `Graphics` and `RtGraphics` are siblings, so a member body cannot live inside
// the `Rt*` one). This file's helpers are in the GLOBAL unnamed namespace, so
// unqualified lookup from here reaches them.
// ############################################################################

namespace Graphics {

// ---------------------------------------------------------------------------
// ?RenderTextures@MaterialInstance@Graphics@@QEBAXAEAVRenderer@2@AEBVScene@2@
//                AEBVCamera@2@H@Z            retail 0x180102df0
//
// The door's banner above carries the transcription evidence and the two
// faithful uglinesses; both survive here unchanged — the `+0x2c` read still
// happens BEFORE the null test, and the loop bound is still re-read.
//
// TWO RAW BINDINGS BECAME TYPED ONES (block GC item 6):
//  * `textures._data + i*0x10`, then `+0x08`, is `_textures._data[i].image` —
//    the same two offsets, now held by `Material.h`'s own asserts.
//  * the hand-inlined `(*(void***)obj)[0x58/8]` is slot ELEVEN of
//    `Graphics::GeneratedImageBuffer`, which block C made a real class:
//    `Render(renderer, scene, camera, generation)`. It stays a VIRTUAL call, so
//    it still dispatches through whatever table the object wears — the minted
//    one for every object alive today — and 0x58/8 == 11 is checked against
//    GeneratedImageBuffer.h's slot list rather than asserted here.
//    `IsDerivedFrom(GeneratedImageBuffer)` is what licenses the cast, and it is
//    the test retail itself makes one line earlier.
// ---------------------------------------------------------------------------
void MaterialInstance::RenderTextures(Renderer& renderer, const Scene& scene,
                                      const Camera& camera, int32_t arg) const {
    if (_hasRenderedTexture == 0)
        return;
    if (_textures._size <= 0)
        return;

    const uint32_t typeId = GeneratedImageBufferTypeId();

    for (int32_t i = 0; i < _textures._size; ++i) {
        const Video::ImageBuffer* const obj = _textures._data[i].image;

        if (obj->_kind != 5)     // +0x2c
            continue;
        // ?IsDerivedFrom@Base@Object@@QEBA_NI@Z (0x1800c0390), the member.
        // Unguarded on `obj`, as retail is.
        if (!obj->IsDerivedFrom(typeId))
            continue;
        if (obj == nullptr)      // retail's order: after the deref above
            continue;

        // Slot 11 (0x58/8) — a virtual call on the real class; the derivation
        // test above licenses the downcast.
        static_cast<const GeneratedImageBuffer*>(obj)
            ->Render(renderer, scene, camera, arg);
    }
}

// ---------------------------------------------------------------------------
// ?AddDrawItem@Renderer@Graphics@@QEAAXPEBVGeometry@2@PEBVMaterialInstance@2@GGMMHPEBXI@Z
// retail 0x1801057b0 -- ENDGAME B.3.5 block GC. The argument-slot count, the three
// silent drops and RULE B5-13's two unwritten pads are in the door's banner, and
// the record is STILL not value-initialised for exactly that reason.
//
// B.4: on the real classes' named fields -- the Renderer's `_drawGroups` /
// `_currentDrawGroup` / `_sceneGroups`, the group's `_materialTypeFlags` /
// `_shader`, the instance's `_shaderClass` / `_material`, the Material's
// `_passes` and the selected `Video::VideoProgram`'s three programs.
// ---------------------------------------------------------------------------
void Renderer::AddDrawItem(const Geometry* geometry, const MaterialInstance* material,
                           uint16_t arg4, uint16_t arg5, float arg6, float arg7, int arg8,
                           const void* arg9, uint32_t arg10) {
    // MOVSXD, both of them — Ghidra's C prints these as zero extensions and
    // that misreading produced three Wave 6 defects including a crash
    // (reference_ghidra_c_output_vs_disassembly.md).
    const int64_t groupIdx = static_cast<int32_t>(_currentDrawGroup);          // +0x34
    const int64_t pass     = static_cast<int32_t>(material->_shaderClass);     // +0x20

    RendererDrawGroup* const group = _drawGroups._data + groupIdx;

    // (a) BT EAX,R11D — 32-bit BT masks the bit index to 5 bits.
    if (((group->_materialTypeFlags >> (static_cast<uint32_t>(pass) & 31u)) & 1u) == 0u) {
        return;
    }

    const int64_t   shader = static_cast<int32_t>(group->_shader);            // +0x1b8
    const Material* mtl    = material->_material.Get();                       // +0x28

    // (b)
    if (static_cast<int32_t>(shader) < 0) {  return; }
    if (static_cast<int32_t>(shader) >= mtl->_passes._size) {                 // +0x30
        return;
    }

    // (c) Material+0x28 is the VideoProgram array MaterialInstance::Setup also
    // hands to Video::VideoProgram::SetupShaders.
    const Video::VideoProgram* const prog = mtl->_passes._data[shader].Get();

    if (prog->_vertexLayout.Get() == nullptr) {  return; }                    // +0x20
    const Video::VertexProgram* const sub28 = prog->_vertexProgram.Get();    // +0x28
    if (sub28 == nullptr) {  return; }
    if (sub28->_byteCode._size <= 0) {  return; }                             // +0x48
    const Video::PixelProgram* const sub30 = prog->_pixelProgram.Get();      // +0x30
    if (sub30 == nullptr) {  return; }
    if (sub30->_byteCode._size <= 0) {  return; }

    // NOT value-initialised, deliberately: retail's record is a raw stack frame
    // and +0x24 / +0x34 are never stored to before the helper copies all seven
    // qwords out of it. Zeroing them here would be a "tidier" port that no
    // longer reproduces what lands in the array.
    RendererDrawItem rec;

    // 0x18010584d: 0x8000 unless the 7th argument is exactly 1.0f. The compare
    // is UCOMISS + JP/JNZ, so a NaN takes the not-equal path — which `!=` in
    // C++ also does.
    uint16_t flags = (arg7 == 1.0f) ? 0u : 0x8000u;
    flags = static_cast<uint16_t>(flags | arg4);   // OR AX,R9W — a 16-bit OR

    rec._geometry = geometry;                         // [RSP+0x30]
    rec._material = mtl;                              // [RSP+0x38]
    rec._instance = material;                         // [RSP+0x40]
    rec._flags    = flags;                            // [RSP+0x48]
    rec._node     = arg5;                             // [RSP+0x4a]
    rec._sortKey  = arg6;                             // [RSP+0x4c]
    rec._maxInstances     = arg8;                             // [RSP+0x50]
    rec._userData = arg9;                             // [RSP+0x58]
    rec._animationInstanceId    = arg10;                            // [RSP+0x60]

    // `group + 0x1c0 + pass*0x10` (LEA RCX,[R11+0x1c]; SHL RCX,4; ADD RCX,R10):
    // the pass's item list by name. Every caller passes 0..2, and retail applies
    // no bound check (B11: the same LEA/SHL/ADD).
    Collection::Array<RendererDrawItem>* list = &group->_items[pass];
    const int32_t oldSize = list->_size;
    DrawItemArrayResize(list, oldSize + 1, &rec);
    // GATHERPF (NOT RETAIL): the COMPPROF `gather` row's appended-item count, split by the
    // group's shader (issue #253; `shader` is already >= 0 here)
    ++g_gatherSlots[GatherShaderSlot(static_cast<int32_t>(shader))].drawItems;

    // The render-to-texture pre-pass. `_hasRenderedTexture` is +0x25, the byte
    // BuildParameters sets when any bound texture's +0x2c == 5. The entry's
    // scene and its Camera (+0x10); the Renderer's `_frameIndex` (+0x44).
    if (material->_hasRenderedTexture != 0) {
        const RendererSceneEntry& e =
            _sceneGroups._data[static_cast<int64_t>(_currentSceneGroup)];   // MOVSXD
        material->Graphics::MaterialInstance::RenderTextures(
            *this, *e._scene, e._camera, _frameIndex);
    }
}

}  // namespace Graphics
