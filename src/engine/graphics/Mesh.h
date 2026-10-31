#pragma once
// ============================================================================
// src/engine/graphics/Mesh.h — binary layouts for the Graphics mesh family
// ============================================================================
//
// Wave 6, lane E of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2,
// §6 step 3). Covers `Graphics::Geometry` (the shared base), `MeshInstance`,
// `GraphicsMesh` (only the fields lane E reads), `EdgeMesh`, `EdgeMeshInstance`
// and the two nested `EdgeMesh` vertex structs.
//
// EVERY offset below was read out of a decompiled or disassembled RETAIL
// Runtime-x64.dll body, and the function plus the instruction that pins it is
// named next to the field. Nothing here is taken from the profile build: PLAN
// §6 warns that profile NAMES win but profile OFFSETS must not
// (reference_encoding_vtable_offsets_differ_between_builds.md).
//
// The full write-up is docs/history/runtime_port/WAVE6_LANE_E.md.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../global_types.h"   // Vector3
#include "../math/Math.h"                   // Quaternion, Transform
#include "../volume/Volume.h"               // Box
#include "../collection/Collection.h"       // Collection::Array

// Graphics::Geometry is Wave 6 LANE D's type and lives in its header. lane E
// only embeds it (MeshInstance and EdgeMeshInstance both derive from it) and
// must not re-declare it: two definitions of RtGraphics::Geometry in one
// namespace across two translation units is an ODR violation, and the drift
// between them would be invisible until a field moved.
#include "AnimationState.h"                 // Graphics::AnimationState, MeshInstance::_animation
#include "Geometry.h"                       // RtGraphics::Geometry (lane D)
// ENDGAME B.3.5 block G: the real `Graphics::EdgeMesh` at the bottom of this
// file derives from `Object::External` and holds four `Object::ExternalPtr<T>`.
#include "../object/External.h"
#include "../object/Internal.h"   // ENDGAME B.3.8 RB: InternalMemFree (the concrete slot 0)
#include "../video/Buffers.h"      // B.4: Video::{Vertex,Index}Buffer, EdgeMesh's eight buffers
#include "GraphicsMesh.h"          // B.4: Graphics::GraphicsMesh (MeshInstance::GetMesh),
                                   //      GraphicsMesh::TriangleList (EdgeMesh's lists)

namespace RtGraphics {

// `RtGraphics::ExternalPtr` — the 8-byte handle — MOVED TO Geometry.h by
// ENDGAME B.3.5 block E, unchanged and with its whole banner. The real
// `Graphics::Geometry` class needs it for slot 2's `Draw` parameter, and
// Geometry.h is included ABOVE this point, so it cannot live here any more.
// Every user of `Mesh.h` still sees it, transitively, exactly as before.

// ============================================================================
// Graphics::InterpolatedState<Transform> — 0x38 bytes, two Transforms.
//
// Pinned by MeshInstance::UpdateHierarchy (retail 0x1800ff910): the node array
// walk advances 0x38 per node (`ADD RSI,0x38`), the first seven dwords are
// written from the geometry transform / the computed world transform, and the
// next seven are then copied verbatim from the first seven
// (`*(base+0x1c+k) = *(base+k)` for k in 0..6, decompiled at the tail of the
// loop). So the element is exactly two adjacent 0x1c-byte Transforms.
//
// WHICH HALF IS WHICH — SETTLED BY WAVE 9 LANE GFX, 2026-09-08. Lane E left
// this open ("do not read `_state[0]` as current without proving it") and named
// `InterpolatedState<T>::Lerp` as the function that would settle it. That
// function is 0x18001aff0, eleven instructions, and it settles it:
//
//     MOV R9,R8        ; R9  = the Scalar& argument (t)
//     MOV R8,RCX       ; R8  = this + 0x00        -> Transform::Lerp's `to`
//     ADD RCX,0x1c     ; RCX = this + 0x1c        -> Transform::Lerp's `this`
//     MOV RBX,RDX      ; RDX = the hidden return slot
//     CALL 0x18001d690 ; Math::Transform::Lerp(from, ret, to, t)
//
// `Math::Transform::Lerp` (0x18001d690, exported as ord 3193) computes
// `from + t*(to - from)` on the translation and `Quaternion::Lerp(from, to, t)`
// on the rotation — `this` is the FROM end and the argument is the TO end,
// pinned by the SUBSS/MULSS/ADDSS order at 0x18001d6d8..0x18001d6fa. So the
// interpolation runs FROM `_state[1]` (+0x1c) TO `_state[0]` (+0x00): +0x00 is
// the value the sim just produced (CURRENT) and +0x1c is the one it replaced
// (PREVIOUS), which is the ordinary render-interpolation arrangement and agrees
// with GetNodeToWorld reading the first one.
// ============================================================================

// ============================================================================
// Graphics::Geometry — the base of MeshInstance and EdgeMeshInstance — is
// declared in src/engine/graphics/Geometry.h by Wave 6 lane D, included
// above. Lane E confirmed the offsets it needs against the same retail bodies
// and agrees with lane D on all of them:
//   +0x2c _dirtyFlags     MeshInstance::SetMesh ORs kDirtyBounds (2) when the
//                         local AABB it copied out of the mesh differs.
//   +0x30 _materialTypeFlags  MeshInstance::SetMesh and EdgeMeshInstance::
//                         SetMesh both OR `1 << *(int*)(material + 0x20)`.
//   +0x34 _localBounds    MeshInstance::SetMesh copies GraphicsMesh+0x80..+0x94
//                         (six floats) here field by field.
//   +0x84 _transform      MeshInstance::UpdateHierarchy seeds node 0 from these
//                         seven floats; GetNodeToWorld reads the rotation at
//                         +0x84 and the translation at +0x94.
// Lane E deliberately uses lane D's NAMES (_detailId, _opacity, _opacityPrev)
// rather than the ones this lane would have guessed from AddDrawItems alone
// (_sortBias, _fadeA, _fadeB) — lane D's come from the exported getters
// ?GetDetailId@ / ?GetOpacity@, which is the stronger evidence.
// ============================================================================

// ============================================================================
// Graphics::GraphicsMesh::Node — 0x48 bytes.
//
// Stride pinned twice: GetNodeIndexFromName (0x1800fee90) advances its cursor
// by nine qwords per node (`puVar8 = puVar8 + 9`), and UpdateHierarchy
// (0x1800ff910) advances by 0x48 (`ADD RBP,0x48`).
//   +0x00 the node name, a plain `char*` — GetNodeIndexFromName strcmp's it
//         directly (it dereferences `*puVar8` as the first byte of a string).
//   +0x08 the parent node index — UpdateHierarchy reads
//         `*(int*)(nodes + 8 + 0x48*i)` and uses it to index _nodes.
//   +0x0c the node-local Transform. `LEA R8,[RCX+0xc]` at 0x1800ffa50 makes
//         R8 the third argument of Quaternion::operator* (the local rotation),
//         and the same R8 is read at +0x10/+0x14/+0x18 for the local
//         translation — i.e. a Transform at +0x0c whose translation sits at
//         +0x1c, exactly Math::Transform's own layout.
// +0x28 .. +0x47 is NOT pinned by this lane; no lane-E body touches it.
// ============================================================================
struct MeshNode {
    const char* _name;        // +0x00
    int32_t     _parent;      // +0x08
    Transform   _local;       // +0x0c .. +0x27
    uint8_t     _unpinned[0x20];  // +0x28 .. +0x47
};
static_assert(sizeof(MeshNode) == 0x48, "GraphicsMesh::Node must be 0x48 bytes");
static_assert(offsetof(MeshNode, _name) == 0x0, "MeshNode::_name");
static_assert(offsetof(MeshNode, _unpinned) == 0x28, "MeshNode::_unpinned");
static_assert(offsetof(MeshNode, _parent) == 0x08, "GraphicsMesh::Node::_parent @ +0x08");
static_assert(offsetof(MeshNode, _local)  == 0x0c, "GraphicsMesh::Node::_local @ +0x0c");

// ============================================================================
// Graphics::GraphicsMesh — PARTIAL. Only the fields lane E reads are declared;
// the class is far larger and no lane-E function constructs one, so this is a
// read-only overlay, not a definition. Do not `sizeof` it.
//
//   +0x50 Array<DetailLevel>, element 0xb0 — AddDrawItems (held back, see the
//         .cpp) indexes it by `level * 0xb0` and reads the level count at +0x58.
//   +0x60 Array<Node>, element 0x48 — GetNodeIndexFromName walks buffer +0x60
//         with count +0x68; MeshInstance::SetMesh resizes the instance node
//         array to `*(int*)(mesh+0x68)`; PlayAnimation passes `mesh + 0x60` as
//         the `const Array<Node>&` argument of AnimationState::PlayAnimation.
//   +0x70 the per-detail-level material Array. GetMaterialList(int)
//         (0x1800126c0) is exactly `*(mesh+0x70) + index*0x10` — index by the
//         0x10-byte Array header, so +0x70 is an ARRAY OF ARRAYS.
//
//         *** +0x70 IS A POINTER, +0x60 IS AN EMBEDDED ARRAY, AND THE TWO ARE
//         PASSED TO RUNTIME HELPERS DIFFERENTLY. *** `_nodes` at +0x60 is an
//         Array object living inside GraphicsMesh, so retail hands it over as
//         `mesh + 0x60` (its address). `_materialLists` at +0x70 is a pointer
//         TO such an array, so retail hands it over as `*(mesh + 0x70)` (the
//         loaded value). Confusing the two is silent whenever the garbage count
//         that gets read happens to be 0; it cost this lane its first
//         integration run (WAVE6_LANE_E.md §6.6). The field is declared as a
//         real pointer type here, not as a byte offset, so the compiler makes
//         the mistake impossible to repeat at this site.
//   +0x80 the mesh AABB — MeshInstance::SetMesh copies six floats from
//         +0x80..+0x94 into Geometry::_localBounds.
// ============================================================================
struct GraphicsMeshView {
    uint8_t _head[0x50];                        // +0x00 .. +0x4f (not pinned here)
    Collection::Array<uint8_t> _detailLevels;   // +0x50 data, +0x58 size, +0x5c capacity
    Collection::Array<MeshNode> _nodes;         // +0x60 data, +0x68 size, +0x6c capacity
    Collection::Array<ExternalPtr>* _materialLists;  // +0x70 -> Array<ExternalPtr>[levelCount]
    uint8_t _pad78[0x08];                       // +0x78 .. +0x7f
    Box         _aabb;                          // +0x80 .. +0x97
};
static_assert(offsetof(GraphicsMeshView, _head) == 0x0, "GraphicsMeshView::_head");
static_assert(offsetof(GraphicsMeshView, _pad78) == 0x78, "GraphicsMeshView::_pad78");
static_assert(offsetof(GraphicsMeshView, _detailLevels)  == 0x50, "GraphicsMesh detail levels @ +0x50");
static_assert(offsetof(GraphicsMeshView, _nodes)         == 0x60, "GraphicsMesh nodes @ +0x60");
static_assert(offsetof(GraphicsMeshView, _materialLists) == 0x70, "GraphicsMesh material lists @ +0x70");
static_assert(offsetof(GraphicsMeshView, _aabb)          == 0x80, "GraphicsMesh AABB @ +0x80");

// ============================================================================
// Graphics::EdgeMesh::TriangleVertex — 8 bytes, and EdgeMesh::EdgeVertex — 16.
//
// TriangleVertex ctor (0x1800a2f50, ordinal 462) writes three int16s from
// `Vector3 * 512.0f` (the multiplier is DAT_180161728, read out of the shipped
// DLL as 0x44000000 = 512.0f) and then a zero int16 at +6.
//
// EdgeVertex ctor (0x1800a2df0, ordinal 164) writes the first TriangleVertex's
// three shorts at +0/+2/+4, the int arg truncated to int16 at +6, the second
// TriangleVertex's three shorts at +8/+10/+12, and 0 at +14. So an EdgeVertex
// is two TriangleVertex-shaped halves, the first carrying the flags word the
// caller passed and the second a zero.
// ============================================================================
struct TriangleVertex {
    int16_t x, y, z;
    int16_t w;         // ctor: 0
};
static_assert(sizeof(TriangleVertex) == 8, "EdgeMesh::TriangleVertex must be 8 bytes");
static_assert(offsetof(TriangleVertex, x) == 0x0, "TriangleVertex::x");
static_assert(offsetof(TriangleVertex, y) == 0x2, "TriangleVertex::y");
static_assert(offsetof(TriangleVertex, z) == 0x4, "TriangleVertex::z");
static_assert(offsetof(TriangleVertex, w) == 0x6, "TriangleVertex::w");

struct EdgeVertex {
    TriangleVertex a;  // +0x00, a.w = the int argument truncated to int16
    TriangleVertex b;  // +0x08, b.w = 0
};
static_assert(sizeof(EdgeVertex) == 16, "EdgeMesh::EdgeVertex must be 16 bytes");
static_assert(offsetof(EdgeVertex, a) == 0x0, "EdgeVertex::a");
static_assert(offsetof(EdgeVertex, b) == 0x8, "EdgeVertex::b");

// The fixed-point scale the TriangleVertex ctor applies. Read from the shipped
// Runtime-x64.dll at 0x180161728 (bytes 00 00 00 44), not guessed.
static const float kEdgeMeshVertexScale = 512.0f;

// ============================================================================
// Graphics::EdgeMesh — 0x180 bytes, an Object::External subclass.
//
// Pinned by ??0EdgeMesh@Graphics@@QEAA@XZ (retail 0x1800a2830, ordinal 161).
// The ctor stamps ??_7Base@Object@@6B@ (0x180154788), then
// ??_7External@Object@@6B@ (0x1801547a0), zeroes +0x08/+0x10/+0x18 — that is
// Object::External's own ctor inlined — then stamps ??_7EdgeMesh@Graphics@@6B@
// (0x180154c70) and initialises its own fields.
//
// It then builds FOUR (VertexBuffer, IndexBuffer) pairs at 0x48/0x68, 0x88/0xa8,
// 0xc8/0xe8, 0x108/0x128. Each is 0x20 bytes: a dword 0 at +0x00, a byte 0 at
// +0x04, then Video::VertexBuffer::PlatformConstruct / IndexBuffer::
// PlatformConstruct fills the rest. EdgeMesh::Set reads the buffer OBJECT
// POINTERS at +0x50/+0x70, +0x90/+0xb0, +0xd0/+0xf0, +0x110/+0x130 — i.e. at
// pair+0x08, which is what PlatformConstruct writes (`*(this+8) = <device
// companion>` at 0x1800f57e0). The pair is therefore
// { int32 _size; uint8 _flags; pad; void* _deviceData; void* _prev; void* _next; }
// with the last two being the device's global intrusive buffer list.
// ============================================================================
struct VideoBufferHeader {
    int32_t _size;        // +0x00  ctor: 0
    uint8_t _flags;       // +0x04  ctor: 0
    uint8_t _pad05[3];    // +0x05
    void*   _deviceData;  // +0x08  written by PlatformConstruct
    void*   _listPrev;    // +0x10  device global buffer list
    void*   _listNext;    // +0x18
};
static_assert(sizeof(VideoBufferHeader) == 0x20, "Video vertex/index buffer header must be 0x20 bytes");
static_assert(offsetof(VideoBufferHeader, _size) == 0x0, "VideoBufferHeader::_size");
static_assert(offsetof(VideoBufferHeader, _flags) == 0x4, "VideoBufferHeader::_flags");
static_assert(offsetof(VideoBufferHeader, _pad05) == 0x5, "VideoBufferHeader::_pad05");
static_assert(offsetof(VideoBufferHeader, _listPrev) == 0x10, "VideoBufferHeader::_listPrev");
static_assert(offsetof(VideoBufferHeader, _listNext) == 0x18, "VideoBufferHeader::_listNext");
static_assert(offsetof(VideoBufferHeader, _deviceData) == 0x08, "Video buffer _deviceData @ +0x08");


// NOTE on the material slot order, because it looks like a transcription bug
// and is not. `?SetMaterials@EdgeMesh@Graphics@@QEAAX...000@Z` (retail
// 0x1800a2760, ordinal 3910) stores its FOUR arguments in the order
//     arg1 -> +0x28, arg2 -> +0x38, arg3 -> +0x30, arg4 -> +0x40
// — the middle two are crossed. That is what the shipped code does (the four
// MOVs are emitted in the source order 0x28, 0x38, 0x40, 0x30 and the decompile
// preserves the operands), so the port reproduces it exactly rather than
// "fixing" it. EdgeMeshInstance::SetMesh reads the four slots back in the order
// +0x38, +0x28, +0x40, +0x30 when it ORs their material type flags.
//
// *** AND ENDGAME B.3.5 BLOCK G CAN NOW SAY WHY, SO IT IS NOT A CROSSING AT
// ALL. *** The four exported getters name the four slots (each one instruction
// plus a hidden-retslot store, disassembled this pass):
//     ?GetMaskMaterial@     0x180012ef0  MOV RAX,[RCX+0x28]
//     ?GetSkinMaskMaterial@ 0x180012f10  MOV RAX,[RCX+0x30]
//     ?GetEdgeMaterial@     0x180012f00  MOV RAX,[RCX+0x38]
//     ?GetSkinEdgeMaterial@ 0x180012f20  MOV RAX,[RCX+0x40]
// So the FIELDS are grouped (mask, edge) x (base, skin) with SKIN adjacent to
// its base — mask, skinMask, edge, skinEdge — while the ARGUMENTS are grouped
// the other way round: mask, edge, skinMask, skinEdge. Two orderings of the same
// four things, each internally consistent. Nothing is crossed and nothing needs
// "reproducing exactly"; the port is simply right either way, and the shape is
// no longer an anomaly a future reader has to be warned about.

// ============================================================================
// Graphics::EdgeMeshInstance — 0xd0 bytes.
//
// Pinned by ??0EdgeMeshInstance@Graphics@@QEAA@XZ (retail 0x1800a3ab0, ordinal
// 163) and its copy ctor (0x1800174f0, ordinal 162).
//
// *** AN UNINITIALISED FIELD IN THE SHIPPED DLL. *** The default ctor writes
// ONLY +0xc0 (`*(this+0xc0) = 0`) after the Geometry base and the vtable stamp.
// The copy ctor writes +0xc0 AND +0xc8. So a default-constructed
// EdgeMeshInstance leaves +0xc8 — the back-pointer to the MeshInstance the
// outline follows — holding whatever the allocator handed back, and only
// EdgeMeshInstance::SetMesh (0x1800a3a20) ever fills it in. This is the same
// class of finding as Wave 5's `RemoveMesh` leaving `Entry::_userValue2`
// uninitialised (reference_shadow_predicate_must_name_its_failing_term.md).
// The port MUST NOT zero it: doing so would make our default ctor differ from
// retail's on a field a copy ctor propagates, and PLAN §4.1 decision 5 requires
// both implementations to be able to operate on one shared object.
// ============================================================================
// (B.4 Z MIRROR2 GFX: the `RtGraphics::EdgeMeshInstance` POD this banner
// described had no reader left and is deleted; the class is below.)

}  // namespace RtGraphics

// ############################################################################
// ENDGAME B.3 step 5 (block E) — `Graphics::MeshInstance` and
// `Graphics::EdgeMeshInstance`, the REAL C++ classes
// ############################################################################
//
// Both derive from the real `Graphics::Geometry` (Geometry.h). RULE B5-5: the
// `Geometry _base` embedded member of the two PODs above becomes a real base
// class in the same commit as `Geometry`, and it is the INHERITANCE that is
// real here — the PODs' `_base` was always "single inheritance written as
// embedding" (B35_RULES §3 says so in as many words).
//
// (B.4 Z MIRROR2 GFX: the `RtGraphics::MeshInstance` POD, `MeshAnimationState`,
// `MeshAnimationLayer` and `InterpolatedTransform` had no reader left and are
// deleted.) The PODs keep their names and their out-of-block users (`batchc_gfx_leaves.cpp`
// is not this block's file), so both descriptions coexist and every offset is
// cross-asserted. `GraphicsMeshView`, `EdgeMesh`, `MeshNode`, `TriangleVertex`,
// `EdgeVertex` and `VideoBufferHeader` above are NOT this block's classes —
// `Graphics::GraphicsMesh` and `Graphics::EdgeMesh` are block G's and
// `Graphics::AnimationState` is block H1's — and are left exactly as they are.
//
// ABSTRACT, per B34_BRIEFS §A.4's amended rule: nothing in the tree holds
// either class BY VALUE as a C++ member, and slot 0's body is a row in
// `src/engine/object/deleting_dtor.cpp` (which B35_RULES §11 forbids retiring
// in step 5 and which is not this block's file), so slot 0 stays `= 0`.
// ############################################################################

namespace Graphics {

// ============================================================================
// Graphics::MeshInstance : Graphics::Geometry — 0x120, NINE slots.
//
// `??_7MeshInstance@Graphics@@6B@` @ 0x180154dc0 (B35_SLOTS.md):
//   [0] 0x180017330  (unexported) vector deleting dtor   deleting_dtor.cpp row
//   [1] 0x180100800  ?Update@…                  wave29_lane_slots.cpp
//   [2] 0x180100960  ?Draw@…MHHH@Z              meshinstance_draw.cpp
//   [3] 0x180100f20  ?AddDrawItems@…            mesh.cpp
//   [4] 0x1800a4020  Geometry::UpdateAttached            INHERITED
//   [5] 0x180101200  ?GetSubTransform@…         mesh.cpp        RETSLOT
//   [6] 0x180101280  ?GetInstanceTransform@…    mesh.cpp        RETSLOT
//   [7] 0x180101660  ?SetSharedInstanceData@…   wave29_lane_slots.cpp
//   [8] 0x1800a4420  Geometry::Clean                     INHERITED
//
// `MeshInstance` overrides BOTH retslot slots (5 and 6), where
// `EdgeMeshInstance` overrides only slot 6 (B35_RULES §2.3 item 6).
//
// THE TWO UNEXPORTED `.data` STATICS `MeshInstance::Update` READS STAY
// (B35_RULES §9, §6.1): the guard byte at 0x1801ff30c and the 48-entry identity
// `Transform` table at 0x1801ff310, minted as `EXTRA_STORAGE_BY_ADDR`. They are
// NOT in the cutover manifest and so are NOT protected by the manifest gate;
// `wave29_lane_slots.cpp` carries both `ResolveAddr` rows forward explicitly and
// `MeshInstance::Update` still declines the whole update when either resolves
// NULL. `?_rootOnlyUpdates@MeshInstance@Graphics@@0_NA` (0x1801fc70b) likewise
// stays an `RT_DATA_SLOT` read — RULE B4b-4 owes it a `MintStorageCell()`
// redirect at the class-static conversion, the END of B.3, not here.
// ============================================================================
class MeshInstance : public Geometry {
public:
    // `??0MeshInstance@Graphics@@QEAA@AEBV01@@Z` @ 0x1800170c0 — the REAL member
    // copy constructor (ENDGAME B.3.9 GFX, mesh.cpp: Geometry's copy ctor in the
    // init list, then retail's stores). NOT `= default`: it zeroes each of the
    // three nested Array headers and then runs retail's deep
    // `Array<T>::operator=` over it, which a bitwise copy would not. The exe's
    // StorageDisplayComponent array resize copy-constructs through it.
    MeshInstance(const MeshInstance& src);
    MeshInstance& operator=(const MeshInstance& src);   // 0x1800171e0  not ported

    // ---- construction (ENDGAME B.3.8 block RB, B38_RULES D1/D2) -----------
    // ??0MeshInstance@Graphics@@QEAA@XZ 0x1800fffc0 — the REAL constructor,
    // PUBLIC: the exe instantiates the class itself (its table copy
    // 0x14015e240; MenuScene, MainMenuDialog, PlacementTool, TimeGoalInstance).
    // Geometry() and the stamp are the compiler's; the field stores are the
    // body (mesh.cpp). ENDGAME B.3.9 GFX: the ??0 door and `DoorBody` are
    // retired; every builder spells `::new (p) MeshInstance()`.
    MeshInstance();

    // ---- the nine slots ---------------------------------------------------
    // [0] CONCRETE: the exe copy's slot 0 is its scalar deleting dtor
    // 0x140019e80 (`CALL [??1MeshInstance]; if (flags & 1) MemFree`). ENDGAME
    // B.3.9 GFX: the minted table and its RT_DELDTOR row 0x180017330 are
    // retired; the class keeps the exe's SCALAR arm (D1.2).
    // `??1MeshInstance@Graphics@@UEAA@XZ` @ 0x180017060, `UEAA` because retail's
    // is virtual — the non-virtual `QEAA` spelling is a name the DLL does not
    // contain (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md). NON-VIRTUAL
    // here (ENDGAME decision 2). The body is empty (B.4 Z MIRROR2 GFX): the
    // implicit member dtors run in reverse declaration order -- ~_nodes
    // (0x18001c550), ~_animation (0x1800fe380 inlined: 0x18001c480),
    // ~_materials (0x180014d00) -- and then ~Geometry, retail's order.
    // (ENDGAME B.3.9 GFX: its door is retired.)
    //
    // PUBLIC since ENDGAME B.3.9 block GFX (coordinator ruling 4): retail's is
    // public (`U` = public virtual) and the exe calls it DIRECTLY on MeshInstance
    // objects EMBEDDED in its components (ModelComponent +0x28, its dtor
    // 0x140124cd0; StorageDisplayComponent's 0x128-stride array) through import
    // slot 0x1401514a8 -- a plain destructor call, never a delete. The C++
    // spelling of that call is `mi->~MeshInstance()`, which needs it public.
    ~MeshInstance() override;
    void Update(const System::Time& time) override;                      // [1] 0x180100800
    void Draw(Video::Context& context, const Camera& camera,
              ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;         // [2] 0x180100960
    void AddDrawItems(const Scene& scene, Renderer& renderer) const override; // [3] 0x180100f20
    // Both retslot slots, in retail order — see Geometry.h's B5-12 block.
    Math::Transform* GetSubTransform(Math::Transform* ret,
                                     int node) const override;            // [5] 0x180101200 RETSLOT
    Math::Matrix* GetInstanceTransform(Math::Matrix* ret, int node,
                                       float t) const override;            // [6] 0x180101280 RETSLOT
    int SetSharedInstanceData(Video::Context& context,
                              int a, float t) const override;             // [7] 0x180101660
    // slots 4 and 8 are Geometry's own bodies, inherited unchanged.

    // ---- PORTED non-virtual members (bodies in this block's files) --------
    Math::Transform GetNodeToWorld(int node) const;                       // 0x180016a40 RETSLOT
    Math::Transform GetNodeToWorld(int node, float t) const;              // 0x180016c20 RETSLOT
    Math::Vector3   RootAnimationOffset() const;                          // 0x180016ef0 RETSLOT
    bool            IsAnimating() const;                                  // 0x1800ff7d0
    void            UpdateHierarchy();                                    // 0x1800ff910
    void            SetMaterialInstance(int index, Object::ExternalPtr<MaterialInstance> value); // 0x1800ffe80
    void            SetMesh(Object::ExternalPtr<GraphicsMesh> mesh);      // 0x1800ffea0
    void            PlayAnimation(Object::ExternalPtr<AnimationGroup> group, int index,
                                  const System::Time* startTime, const System::Time* blendTime,
                                  float weight);                          // 0x1800ff840

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined -
    // Each is one to a dozen instructions in retail over offsets this header
    // already pins. Declared so no later block or caller batch edits this file.
    //
    // *** TWO OF THE MEMBERS UNDER THIS HEADING ARE PORTED, AND THE HEADING WAS
    // *** STALE. Corrected by ENDGAME B.3.5 block GC, 2026-09-19:
    //   `Serialize`  (0x1800ff680) is a MEMBER, in `wave28_lane_gfxbody.cpp` —
    //                block G moved it and recorded that this note was stale but
    //                belonged to block E's header, so it left it for a closing
    //                pass. This is that pass.
    //   `GetBones`   (0x180100070) is a MEMBER, in
    //                `animationstate_transforms.cpp` — block GC moved it.
    // Both keep their registered doors, which delegate. Every OTHER declaration
    // under this heading really is undefined on purpose.
    // 0x180016a20: the ExternalPtr at +0xc0 through the hidden return slot.
    // B.4: PORTED, inline, with retail's return type.
    Object::ExternalPtr<GraphicsMesh> GetMesh() const {
        return _mesh;
    }
    int      GetNodeCount() const;                           // 0x180016a30
    void     SetVisualScale(const Math::Vector4& scale);     // 0x180016fa0
    Math::Vector4 GetVisualScale() const;                    // 0x180016fd0 RETSLOT
    int      GetMaterialInstanceCount() const;               // 0x180017000
    Object::ExternalPtr<MaterialInstance> GetMaterialInstance(int index) const;  // 0x180017010
    void     SetSortKey(int key);                            // 0x180017030
    void     Serialize(IO::Serializer& serializer);          // 0x1800ff680
    float    GetAnimationElapsed() const;                    // 0x1800ff700
    bool     IsCurrentAnimationLooping() const;              // 0x1800ff730
    void     SetCurrentAnimationTimeScale(float scale);      // 0x1800ff760
    float    GetCurrentAnimationTimeScale() const;           // 0x1800ff780
    int      GetCurrentAnimationIndex() const;               // 0x1800ff7b0
    void     ResetAnimationState();                          // 0x1800ff810
    void     CopyHierarchy(const MeshInstance& src);         // 0x1800ffd80
    int      GetMaterialIndex(Object::ExternalPtr<MaterialInstance> material);   // 0x1800ffe30
    int      GetBones(float t, const Math::Transform& xf,
                      Math::Matrix* out) const;              // 0x180100070

    // The two CLASS STATIC accessors over `_rootOnlyUpdates`. Exported, not
    // ported, no caller as members.
    static void EnableRootOnlyUpdates(bool enable);          // 0x180017040
    static bool AreRootOnlyUpdateEnabled();                  // 0x180017050

    // ?_rootOnlyUpdates@MeshInstance@Graphics@@0_NA  0x1801fc70b — retail's
    // PRIVATE static bool (`0`). ENDGAME B.3.9 block GFX (B39_RULES D7 row A22):
    // the class static, defined in mesh.cpp and REDIRECTED onto the mint's
    // storage row by RtGraphicsPortInit, so the name / address doors the
    // foreign readers use (citizenai_executor.cpp, address_registry.cpp,
    // render_culling.cpp) and this member are ONE byte. Public because its
    // reader in this block is a free-function body (wave29_lane_slots.cpp).
    static bool _rootOnlyUpdates;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md, DESIGN.md hook rule 5):
    // ext.create_on_demand's prefetch. Null by default and under the faithful profile. Run at the end
    // of SetMesh with the instance holding its new mesh and materials: the extension queues the
    // first-use creates of their buffers and textures, ahead of the instance's first draw.
    static void (*_extMeshAssigned)(const MeshInstance& instance);
    // The same for SetMaterialInstance (a skin chosen after SetMesh, a building's charred set): run
    // with the material just stored.
    static void (*_extMaterialAssigned)(const MaterialInstance* material);
    // The same from the draw gather: run by AddDrawItems with the detail level it chose, for every
    // gathered instance (the view's, a shadow pass's), a frame or more before the render thread binds
    // it. The extension tests one handle and queues the instance's creates only when they are not made
    // (a first sight the placement did not prefetch, an object given back by the idle eviction).
    static void (*_extLevelGathered)(const MeshInstance& instance, int32_t level);
    // EXT SEAM (NOT RETAIL, docs/android/P13_OVERLAP.md §2.3, DESIGN.md hook rule 5): ext.render_pipe's
    // frozen copies. Null by default and under the faithful profile. Consulted only on the instanced
    // branch of the four pose lookups (GetBones, InstancedPoses, RtInstancedPoolSlot, SkinMemo's
    // PoseArray): non-null and answering, the copy's frozen record replaces the AnimationManager slot;
    // a live instance always gets null and the faithful lookup.
    static const InstancedAnimationState* (*_extInstancedState)(const MeshInstance& instance);

    // `??2MeshInstance@Graphics@@SAPEAX_K@Z` (0x180016940),
    // `??2…@SAPEAX_KPEAX@Z` (0x1800169b0) and `??3…@SAXPEAX@Z` (0x1800169c0) are
    // the class's own `operator new` / `operator delete`. They are exported and
    // unported, and they are deliberately NOT declared: a declared-but-undefined
    // `operator new` would make any future `new MeshInstance` a link error at
    // best and a silently different allocator at worst, and nothing in the tree
    // news one as a C++ object (see the ABSTRACT banner above). Recorded here so
    // the omission is a decision, not a gap.


public:
    Object::ExternalPtr<GraphicsMesh>              _mesh;         // +0xc0  retail ??0 0x1800fffc0 zeroes it first (0x1800fffe8); ~ 0x180017060 leaves it
    // B.4: typed. `Object::ExternalPtr<T>` is one trivially copyable,
    // trivially destructible pointer (External.h), so the implicit member dtor
    // is the same Clear-then-J1 and the element bytes are the same.
    Collection::Array<Object::ExternalPtr<MaterialInstance> > _materials;  // +0xc8
    // B.4 Z MIRROR2 GFX: retail's 0x18-byte `Graphics::AnimationState`, a real
    // member: ??0 0x1800fffc0 inlines its ctor 0x1800fdaf0 (the BYTE store at
    // 0x180100009, then +0xe0 / +0xe8), ??1 0x180017060 inlines its dtor
    // 0x1800fe380 (CALL 0x18001c480 on +0xe0) between ~_nodes and ~_materials.
    AnimationState                                 _animation;    // +0xd8 .. +0xef
    // B.4: the element is `Graphics::InterpolatedState<Math::Transform>` (the
    // type `?GetInstanceTransforms@` / UpdateHierarchy name); trivially
    // destructible, so the member dtor is unchanged.
    // B.4 Z MIRROR2 GFX: on retail's row (kSpec_InterpTransform), the one its
    // copy ctor's 0x18001b810, SetMesh's 0x18005ac50 and ~MeshInstance's
    // 0x18001c550 instantiate.
    InterpTransformArray _nodes;                                   // +0xf0
    Vector3                                        _rootAnimationOffset; // +0x100
    float                                          _color[4];     // +0x10c ctor 1,1,1,1
    int16_t                                        _sortBias;     // +0x11c ctor 0
    int16_t                                        _pad11e;       // +0x11e
};

static_assert(sizeof(MeshInstance) == 0x120,
              "Graphics::MeshInstance is 0x120 bytes (MOV EDX,0x120 in its own "
              "slot-0 vector deleting destructor 0x180017330)");
static_assert(offsetof(MeshInstance, _pad11e) == 0x11E, "MeshInstance::_pad11e");
static_assert(alignof(MeshInstance) == 8, "qword aligned (the vptr)");

static_assert(offsetof(MeshInstance, _mesh)                == 0xc0,  "MeshInstance::_mesh @ +0xc0 (??0 0x1800fffc0)");
static_assert(offsetof(MeshInstance, _materials)           == 0xc8,  "MeshInstance::_materials @ +0xc8");
static_assert(offsetof(MeshInstance, _animation)           == 0xd8,  "MeshInstance::_animation @ +0xd8 (the BYTE store at 0x180100009)");
static_assert(offsetof(MeshInstance, _nodes)               == 0xf0,  "MeshInstance::_nodes @ +0xf0");
static_assert(offsetof(MeshInstance, _rootAnimationOffset) == 0x100, "MeshInstance::_rootAnimationOffset @ +0x100");
static_assert(offsetof(MeshInstance, _color)               == 0x10c, "MeshInstance::_color @ +0x10c (four 0x3f800000 dwords)");
static_assert(offsetof(MeshInstance, _sortBias)            == 0x11c, "MeshInstance::_sortBias @ +0x11c (a WORD store, which is why sizeof is 0x120)");

// The base subobject must start at zero and the derived fields must start at
// exactly sizeof(Geometry) — no tail-padding reuse, no vptr duplication.
static_assert(offsetof(MeshInstance, _mesh) == sizeof(Geometry),
              "MeshInstance's own fields begin at the end of the Geometry base, "
              "with no gap: retail's ctor writes +0xc0 immediately after the base ctor");

// ============================================================================
// Graphics::EdgeMeshInstance : Graphics::Geometry — 0xd0, NINE slots.
//
// `??_7EdgeMeshInstance@Graphics@@6B@` @ 0x180154d70 (B35_SLOTS.md):
//   [0] 0x1800175d0  (unexported) vector deleting dtor   deleting_dtor.cpp row
//   [1] 0x1800a39b0  ?Update@…                  mesh.cpp
//   [2] 0x1800a3150  ?Draw@…MHHH@Z              meshinstance_draw.cpp
//   [3] 0x1800a2fd0  ?AddDrawItems@…            mesh.cpp
//   [4] 0x1800a4020  Geometry::UpdateAttached            INHERITED
//   [5] 0x1800a43c0  Geometry::GetSubTransform           INHERITED  (see B5-12)
//   [6] 0x1800a3840  ?GetInstanceTransform@…    mesh.cpp        RETSLOT
//   [7] 0x180015590  Geometry::SetSharedInstanceData     INHERITED
//   [8] 0x1800a4420  Geometry::Clean                     INHERITED
//
// *** IT OVERRIDES ONLY ONE OF THE TWO RETSLOT SLOTS. *** Slot 5 stays
// `Geometry::GetSubTransform` (0x1800a43c0, byte-identical target) and only slot
// 6 is its own. B35_RULES §2.3 item 6 records the same asymmetry; the class
// below therefore declares slot 6 and NOT slot 5.
//
// `_followed` (+0xc8) IS LEFT UNINITIALISED BY THE DEFAULT CONSTRUCTOR and the
// port must not zero it — see the POD's banner above.
//
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1): CONCRETE. A real public ctor
// (retail 0x1800a3ab0: `_mesh = 0` and nothing else), a real member copy ctor
// (0x1800174f0), its own VECTOR slot 0; the ??0 / copy ??0 / ??1 doors, the
// RT_DELDTOR row 0x1800175d0 and the minted table are retired
// (tools/rt_mint_retired/gfx.txt). The exe's HighlightComponent builds its
// embedded +0x28 instance with `::new (p) EdgeMeshInstance()`.
// ============================================================================
class EdgeMeshInstance : public Geometry {
public:
    // `??0EdgeMeshInstance@Graphics@@QEAA@AEBV01@@Z` @ 0x1800174f0 — the REAL
    // member copy constructor (mesh.cpp). It writes +0xc0 AND +0xc8, where the
    // default ctor writes only +0xc0.
    EdgeMeshInstance(const EdgeMeshInstance& src);
    EdgeMeshInstance& operator=(const EdgeMeshInstance& src);  // 0x180017550  not ported

    // ??0EdgeMeshInstance@Graphics@@QEAA@XZ 0x1800a3ab0 — the REAL constructor:
    // Geometry() and the stamp are the compiler's; `_mesh = 0` is the whole
    // body. `_followed` (+0xc8) is deliberately NOT written (retail's own).
    // B.4 (X-RL): the zero is `_mesh`'s own default ctor (Object::ExternalPtr).
    EdgeMeshInstance() {}

    // [0] 0x1800175d0 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xd0` = the element size).
    // 0x1800174d0 -- PORTED (ENDGAME B.3.9 GFX, mesh.cpp): retail's body is a
    // bare tail into ~Geometry, so the member is empty and the chain is the
    // compiler's. (Its ??1 door is retired with the RT_DELDTOR row.)
    ~EdgeMeshInstance() override;
    void Update(const System::Time& time) override;                      // [1] 0x1800a39b0
    void Draw(Video::Context& context, const Camera& camera,
              ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;         // [2] 0x1800a3150
    void AddDrawItems(const Scene& scene, Renderer& renderer) const override; // [3] 0x1800a2fd0
    Math::Matrix* GetInstanceTransform(Math::Matrix* ret, int node,
                                       float t) const override;            // [6] 0x1800a3840 RETSLOT
    // slots 4, 5, 7 and 8 are Geometry's own bodies, inherited unchanged.

    // PORTED, body in `mesh.cpp`. `MeshInstance*` is the outline's back-pointer.
    void SetMesh(Object::ExternalPtr<EdgeMesh> mesh, MeshInstance* followed);  // 0x1800a3a20

    // Exported, NOT PORTED, no caller: declared, deliberately undefined.
    Object::ExternalPtr<EdgeMesh> GetMesh() const;                        // 0x1800174c0

    // FIX SEAM (fix.outline_with_panel, NOT RETAIL; docs/retail_bugs.md section 16): null by default
    // and under the faithful profile. Asked once per AddDrawItems: true skips the instance in this
    // gather (an outline held back for the one frame its panel is not laid out yet). Static data: the
    // 0xd0-byte layout is untouched.
    static bool (*_extHoldDraw)(const EdgeMeshInstance* instance);

    // `??2`/`??3` operator new/delete (0x1800173e0 / 0x180017450 / 0x180017460)
    // are deliberately not declared; see MeshInstance's note above.

public:
    // B.4 (X-RL): typed. `Object::ExternalPtr<T>` is one trivially copyable,
    // trivially destructible pointer (External.h): same 8 bytes, no dtor added.
    // Every reader is in mesh.cpp / meshinstance_draw.cpp.
    Object::ExternalPtr<EdgeMesh> _mesh; // +0xc0  ExternalPtr<EdgeMesh>; ctor 0
    MeshInstance*           _followed;  // +0xc8  default ctor LEAVES IT UNSET
};

static_assert(sizeof(EdgeMeshInstance) == 0xd0,
              "Graphics::EdgeMeshInstance is 0xd0 bytes (MOV EDX,0xd0 in its own "
              "slot-0 vector deleting destructor 0x1800175d0)");
static_assert(alignof(EdgeMeshInstance) == 8, "qword aligned (the vptr)");
static_assert(offsetof(EdgeMeshInstance, _mesh)     == 0xc0, "EdgeMeshInstance::_mesh @ +0xc0");
static_assert(offsetof(EdgeMeshInstance, _followed) == 0xc8, "EdgeMeshInstance::_followed @ +0xc8");
static_assert(offsetof(EdgeMeshInstance, _mesh) == sizeof(Geometry),
              "EdgeMeshInstance's own fields begin at the end of the Geometry base");

}  // namespace Graphics

// ############################################################################
// ENDGAME B.3 step 5 (block G) — `Graphics::EdgeMesh`, the REAL C++ class
//
// This is BLOCK G's class, appended beside block E's two instance classes above
// and touching neither. The `RtGraphics::EdgeMesh` POD keeps living (B4b-1:
// `RtGraphics` is a SIBLING of `Graphics`, so nothing has to move) and has users
// outside block G's file set; every offset is cross-asserted so the two cannot
// drift.
//
// PARENT `Object::External`, read from
// `?_ParentTypeName@EdgeMesh@Graphics@@CAPEBDXZ` @0x180012c60 -> "Object::External"
// (B35_SLOTS.md section 3), and corroborated by `??0EdgeMesh@Graphics@@QEAA@XZ`
// (0x1800a2830), whose stamps are Base -> External -> EdgeMesh with NO
// `CompiledExternal` step — unlike `GraphicsMesh`, which is a `CompiledExternal`
// and stamps four. The two sit in the same header and in the same block, so that
// asymmetry is asserted in `tests/unit/graphics_material_abi_test.cpp` rather
// than trusted.
//
// *** EdgeMesh IS THE ONE CLASS OF BLOCK G'S FOUR WITH ITS OWN `Compile`. ***
// Slot 4 is `?Compile@EdgeMesh@Graphics@@` @0x1800a21c0 (`MOV EAX,1 ; RET`),
// where `Material`, `MaterialInstance` and `GraphicsMesh` all inherit
// `?Compile@External@Object@@` @0x1800066e0. Both bodies return 1, so getting it
// backwards is SILENT — which is why the unit test compares the slot-4 POINTER
// against a bare `Object::External` leaf's and requires EdgeMesh's to DIFFER.
//
// RULE B5-15 — the two literals, COPIED from B35_SLOTS.md and recorded, not
// used. No `_typeInfo` row is retired (B35_RULES section 11); slots 1/2 keep
// reading `?_typeInfo@EdgeMesh@Graphics@@0VBaseTypeInfo@Object@@A` (0x1801f99b0).
//     ?_TypeName@EdgeMesh@Graphics@@       -> "EdgeMesh"
//     ?_ParentTypeName@EdgeMesh@Graphics@@ -> "Object::External"
//
// ABSTRACT (B34_BRIEFS A.4 as amended): nothing holds an `EdgeMesh` BY VALUE as
// a C++ member, and slot 0's body (0x1800134b0) is a row in
// `src/engine/object/deleting_dtor.cpp` that B35_RULES section 11 forbids
// retiring, so slot 0 stays `= 0`.
//
// NO REAL CONSTRUCTOR (B4b-2): `??0EdgeMesh@Graphics@@QEAA@XZ` (0x1800a2830) is a
// registered door in `src/engine/graphics/mesh.cpp` that open-codes the base
// chain as vptr stamps naming the MINTED tables, then builds the four
// (VertexBuffer, IndexBuffer) pairs through
// `Video::{Vertex,Index}Buffer::PlatformConstruct`. A member constructor would
// stamp GCC's tables into an object the mint owns.
//
// ⚠ ZERO CALLERS ON A PLAIN RUN, AND THAT IS NOT VERIFICATION.
//   `edgemesh_vtable.cpp`'s banner already says it: the selection-outline chain
//   (`standalone/address_registry.cpp`, `RegisterPlayerRenderBatch`) is the only
//   consumer of `EdgeMesh` and it is gated default-OFF on
//   `BANISHED_SELECTION_OUTLINE`. A run that does not set that variable exercises
//   none of these bodies. The conversion below is a layout-and-slot change with a
//   static gate and a unit test behind it; the bodies themselves are as verified
//   as they were before it, which is to say by that one gated route.
// ############################################################################

// Classes other blocks own; taken by reference / pointer under their real retail
// names. `Video::Context` is already forward-declared by Geometry.h.
namespace Video { class IndexBuffer; class VertexBuffer; }

namespace Graphics {

class MaterialInstance;   // block G  (src/engine/graphics/Material.h)

// ============================================================================
// Graphics::EdgeMesh — 0x180 bytes, NINE slots, `Object::External`.
//
// `??_7EdgeMesh@Graphics@@6B@` @ 0x180154c70, read as `.rdata` qwords
// (B35_SLOTS.md:52-61):
//
//   [0] +0x00  0x1800134b0  (unexported) the vector deleting dtor
//   [1] +0x08  0x180012c90  ?TypeName@…
//   [2] +0x10  0x180012c70  ?Type@…
//   [3] +0x18  0x1800a23d0  ?GetFileVersion@…  (`MOV EAX,2 ; RET`)
//   [4] +0x20  0x1800a21c0  ?Compile@EdgeMesh@Graphics@@  — ITS OWN, see above
//   [5] +0x28  0x1800a21d0  ?Serialize@…
//   [6] +0x30  0x180012e10  ?Reconstruct@…
//   [7] +0x38  0x180012e40  ?Destruct@…
//   [8] +0x40  0x180012e30  ?Construct@…  — INTRODUCED HERE (RULE B5-16)
//
// THE FIELD NAMES ARE RETAIL'S GETTERS', NOT THE POD'S. Twelve one-instruction
// exports name all twelve non-trivial fields, and three of the four buffer pairs
// were named by guess in the POD above (`_edgeVerts` is the SKIN pair,
// `_capVerts` is the EDGE pair, `_extraVerts` is the EDGE-SKIN pair). Every
// offset is cross-asserted against the POD below, so the rename cannot move a
// byte.
// ============================================================================
class EdgeMesh : public Object::External {
public:
    // The two nested vertex records, 8 and 16 bytes. Both are `RtGraphics`'
    // (pinned by their own constructors, see the POD section above) and are
    // ALIASED here rather than re-declared: B31_RULES asks for the retail NAME,
    // not for a second copy of the bytes that can drift from the first.
    using TriangleVertex = RtGraphics::TriangleVertex;
    using EdgeVertex     = RtGraphics::EdgeVertex;

    // ---- the nine slots, in measured retail order --------------------------
    // [0] 0x1800134b0 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x180` = the element size).
    // ENDGAME B.3.9 GFX: concrete (B39_RULES D1).
    // `??1EdgeMesh@Graphics@@UEAA@XZ` @ 0x1800a2780. VIRTUAL (B3X W1).
    // ENDGAME B.3.9 GFX: the retired door's body (batchc_gfx_leaves.cpp) -- the
    // two list arrays and the eight PlatformDestructs in retail's order; the
    // restamp and the tail ~External are the compiler's chain.
    ~EdgeMesh() override;

    // [1] 0x180012c90 TypeName / [2] 0x180012c70 Type, TypeId 0x180012c80, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(EdgeMesh)
public:
    int32_t     GetFileVersion() const override;           // [3] 0x1800a23d0
    // *** ITS OWN, not `Object::External`'s. *** `MOV EAX,1 ; RET`, six bytes,
    // reading NEITHER `this` NOR the `Filename&` (no RCX, no RDX operand).
    CompileResult Compile(const System::Filename& path) override;  // [4] 0x1800a21c0
    bool        Serialize(IO::Stream& stream) override;           // [5] 0x1800a21d0
    Object::External* Reconstruct() override;               // [6] 0x180012e10
    void        Destruct() override;                        // [7] 0x180012e40
    virtual Object::External* Construct();                  // [8] 0x180012e30 — B5-16

    // ---- PORTED non-virtual members ---------------------------------------
    // Bodies in `src/engine/graphics/mesh.cpp`; the doors keep their
    // registrations and delegate.
    //
    // `?SetMaterials@EdgeMesh@Graphics@@QEAAXV?$ExternalPtr@VMaterialInstance@
    //  Graphics@@@Object@@000@Z` 0x1800a2760. THE ARGUMENT ORDER IS
    // (mask, edge, skinMask, skinEdge) and the FIELD order is
    // (mask, skinMask, edge, skinEdge) — see the POD's note; the parameters are
    // named so the pairing is visible at every call site.
    void SetMaterials(Object::ExternalPtr<MaterialInstance> mask,
                      Object::ExternalPtr<MaterialInstance> edge,
                      Object::ExternalPtr<MaterialInstance> skinMask,
                      Object::ExternalPtr<MaterialInstance> skinEdge);   // 0x1800a2760

    // `?Set@EdgeMesh@Graphics@@QEAA_NHPEBUTriangleList@GraphicsMesh@2@H0H
    //  PEAGHPEAXHH1H2HH1H2HH1H2HAEBVBox@Volume@@@Z` 0x1800a23e0 — PORTED, and a
    // real member since ENDGAME B.3.5's closing pass.
    //
    // TWENTY-FIVE arguments, not twenty-two: the earlier note here said 22, and
    // both that number and the "deliberately left to the door" decision are
    // withdrawn. The list below was copied MECHANICALLY from the mangled name by
    // tokenising the argument string, never retyped from the decompiler's
    // parameter list (the recorded rule for anything past the fourth argument):
    //
    //   H                                  -> int                          (1)
    //   PEBUTriangleList@GraphicsMesh@2@   -> const TriangleList*  [bref 0] (2)
    //   H                                  -> int                          (3)
    //   0                                  -> const TriangleList*          (4)
    //   H PEAG H PEAX H H                  -> int, ushort*[bref 1], int,
    //                                         void*[bref 2], int, int    (5..10)
    //   1 H 2 H H                          -> ushort*, int, void*, int, int
    //                                                                  (11..15)
    //   1 H 2 H H                          -> same five               (16..20)
    //   1 H 2 H                            -> ushort*, int, void*, int (21..24)
    //   AEBVBox@Volume@@                   -> const Volume::Box&        (25)
    //
    // The grouping is DELIBERATELY ASYMMETRIC and that is not a transcription
    // slip: the first group carries a LEADING int and the last group has NO
    // trailing int, because each group's trailing int is the NEXT pair's index
    // COUNT. The body proves it — pair 0's index byte count is `i0 * 2` (group
    // 1's leading int) while pair 1's is `v0x * 2` (group 1's trailing int).
    // Retail's parameter packing straddles the four (index, vertex) pairs, so a
    // "four identical groups of six" reading is wrong by construction and only
    // happens to work because every slot is an int or a pointer.
    //
    // B.4 wave Y: the two list parameters are retail's
    // `PEBUTriangleList@GraphicsMesh@2@` (the arrays they fill are typed).
    // `engine/funcs/engine_funcs.h` carried a SECOND transcription of these same
    // 25 slots in a symmetric "four groups of five" shape; it is ABI-identical
    // and it retires with its last caller.
    bool Set(int triCount, const GraphicsMesh::TriangleList* triLists,
             int edgeCount, const GraphicsMesh::TriangleList* edgeLists,
             int i0, uint16_t* ib0, int v0s, void* vb0, int v0n, int v0x,
             uint16_t* ib1, int v1s, void* vb1, int v1n, int v1x,
             uint16_t* ib2, int v2s, void* vb2, int v2n, int v2x,
             uint16_t* ib3, int v3s, void* vb3, int v3n,
             const ::Box& box);                                  // 0x1800a23e0

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    // The eight buffer getters. Each is `LEA RAX,[RCX + <off>]` and nothing
    // else, disassembled this pass — which is what names the four pairs.
    const Video::IndexBuffer&  GetIndexBuffer() const;            // 0x180012e50  +0x68
    const Video::VertexBuffer& GetVertexBuffer() const;           // 0x180012e60  +0x48
    const Video::IndexBuffer&  GetSkinIndexBuffer() const;        // 0x180012e70  +0xa8
    const Video::VertexBuffer& GetSkinVertexBuffer() const;       // 0x180012e80  +0x88
    const Video::IndexBuffer&  GetEdgeIndexBuffer() const;        // 0x180012e90  +0xe8
    const Video::VertexBuffer& GetEdgeVertexBuffer() const;       // 0x180012ea0  +0xc8
    const Video::IndexBuffer&  GetEdgeSkinIndexBuffer() const;    // 0x180012eb0  +0x128
    const Video::VertexBuffer& GetEdgeSkinVertexBuffer() const;   // 0x180012ec0  +0x108

    const Collection::Array<GraphicsMesh::TriangleList>& GetTriangleLists() const;   // 0x180012ed0  +0x148
    const Collection::Array<GraphicsMesh::TriangleList>& GetEdgeLists() const;       // 0x180012ee0  +0x158
    const ::Box& GetAABB() const;                                 // 0x180012f30  +0x168

    // The four material getters. Each is two instructions plus a HIDDEN RETSLOT
    // store (`MOV RAX,[RCX+off] ; MOV [RDX],RAX ; MOV RAX,RDX ; RET`), which is
    // four more measurements settling the COORDINATOR AMENDMENT's open question
    // about `ExternalPtr<T>`: retail DOES return them through a slot. The C++
    // members return by value, per the amendment's second half — none is a
    // vtable slot, so both ends of a C++ call are this same GCC.
    Object::ExternalPtr<MaterialInstance> GetMaskMaterial() const;      // 0x180012ef0  +0x28
    Object::ExternalPtr<MaterialInstance> GetSkinMaskMaterial() const;  // 0x180012f10  +0x30
    Object::ExternalPtr<MaterialInstance> GetEdgeMaterial() const;      // 0x180012f00  +0x38
    Object::ExternalPtr<MaterialInstance> GetSkinEdgeMaterial() const;  // 0x180012f20  +0x40

    EdgeMesh(const EdgeMesh& src);                          // 0x180012f70  not ported
    EdgeMesh& operator=(const EdgeMesh& src);               // 0x180013230  not ported

    // The reflection block's five statics. ENDGAME B.3.9 GFX: the factory pair
    // is the class's own (edgemesh_vtable.cpp, B39_RULES D2); the three
    // operators stay declared and undefined (placement is `::new`).
    static void* operator new(std::size_t bytes);            // 0x180012ca0
    static void* operator new(std::size_t bytes, void* place);// 0x180012d10
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                   // 0x180012d20
    // CreateNewObject 0x180012d80 / CreateInPlaceObject 0x180012e00: declared
    // by DECLARE_TYPE above.

    // ??0EdgeMesh@Graphics@@QEAA@XZ 0x1800a2830 — the REAL constructor
    // (ENDGAME B.3.9 GFX; mesh.cpp): External() and the three stamps are the
    // compiler's chain; the body is the retired door's -- the five zeroed
    // pointers, the eight (header, PlatformConstruct) pairs, the two lists and
    // the empty box. The "NO REAL CONSTRUCTOR" paragraph above is history.
    EdgeMesh();

public:
    // ---- the 0x160 bytes EdgeMesh adds after Object::External ---------------
    // PUBLIC for the reason `Object::External`'s are: unconverted bodies reach
    // them at these offsets through a cast. B.4: the four buffer pairs are the
    // REAL `Video::VertexBuffer` / `Video::IndexBuffer` (Buffers.h: 0x20 bytes,
    // trivially copyable, no ctor or dtor of their own -- `PlatformConstruct` /
    // `PlatformDestruct` are the explicit calls ~EdgeMesh and EdgeMesh() make),
    // the types the eight getters return; the POD's `VideoBufferHeader` is the
    // same 0x20 bytes.
    // An ExternalPtr<GraphicsMesh> slot: Serialize hands it to the
    // GraphicsMesh ExternalPtr serializer (0x180014230). Held as the raw
    // pointer: ~EdgeMesh 0x1800a2780 does not release it.
    GraphicsMesh* _reserved20;                            // +0x20  ctor 0
    Object::ExternalPtr<MaterialInstance> _maskMaterial;      // +0x28  ?GetMaskMaterial@
    Object::ExternalPtr<MaterialInstance> _skinMaskMaterial;  // +0x30  ?GetSkinMaskMaterial@
    Object::ExternalPtr<MaterialInstance> _edgeMaterial;      // +0x38  ?GetEdgeMaterial@
    Object::ExternalPtr<MaterialInstance> _skinEdgeMaterial;  // +0x40  ?GetSkinEdgeMaterial@
    Video::VertexBuffer _vertexBuffer;           // +0x48   ?GetVertexBuffer@
    Video::IndexBuffer  _indexBuffer;            // +0x68   ?GetIndexBuffer@
    Video::VertexBuffer _skinVertexBuffer;       // +0x88   ?GetSkinVertexBuffer@
    Video::IndexBuffer  _skinIndexBuffer;        // +0xa8   ?GetSkinIndexBuffer@
    Video::VertexBuffer _edgeVertexBuffer;       // +0xc8   ?GetEdgeVertexBuffer@
    Video::IndexBuffer  _edgeIndexBuffer;        // +0xe8   ?GetEdgeIndexBuffer@
    Video::VertexBuffer _edgeSkinVertexBuffer;   // +0x108  ?GetEdgeSkinVertexBuffer@
    Video::IndexBuffer  _edgeSkinIndexBuffer;    // +0x128  ?GetEdgeSkinIndexBuffer@
    // Both are `Array<GraphicsMesh::TriangleList>` (stride 0x18). `SetSize` is
    // the registered row at 0x1800136f0 (`rtcoll::kSpec_Tri18A4`). B.4: typed;
    // TriangleList is trivially destructible, so `~EdgeMesh`'s implicit member
    // dtors are the same Clear-then-J1 the raw headers ran.
    Collection::Array<GraphicsMesh::TriangleList> _triangleLists;   // +0x148  ?GetTriangleLists@
    Collection::Array<GraphicsMesh::TriangleList> _edgeLists;       // +0x158  ?GetEdgeLists@
    ::Box _aabb;                                          // +0x168  ?GetAABB@; ctor +/-1e8
};

static_assert(sizeof(EdgeMesh) == 0x180,
              "Graphics::EdgeMesh is 0x180 bytes (the MOV EDX,0x180 in its own "
              "slot-0 vector deleting destructor 0x1800134b0)");
static_assert(alignof(EdgeMesh) == 8, "Graphics::EdgeMesh aligns to 8 (the vptr)");
static_assert(offsetof(EdgeMesh, _reserved20) == sizeof(Object::External),
              "EdgeMesh's own fields begin at the end of the Object::External base");

// Cross-asserts against the POD, field for field, INCLUDING the three pairs the
// POD names differently — which is the whole point of asserting them.

// And against the retail getters directly, so a POD edit cannot move both.
static_assert(offsetof(EdgeMesh, _maskMaterial)         == 0x28,  "EdgeMesh::_maskMaterial @ +0x28 (0x180012ef0)");
static_assert(offsetof(EdgeMesh, _skinMaskMaterial)     == 0x30,  "EdgeMesh::_skinMaskMaterial @ +0x30 (0x180012f10)");
static_assert(offsetof(EdgeMesh, _edgeMaterial)         == 0x38,  "EdgeMesh::_edgeMaterial @ +0x38 (0x180012f00)");
static_assert(offsetof(EdgeMesh, _skinEdgeMaterial)     == 0x40,  "EdgeMesh::_skinEdgeMaterial @ +0x40 (0x180012f20)");
static_assert(offsetof(EdgeMesh, _vertexBuffer)         == 0x48,  "EdgeMesh::_vertexBuffer @ +0x48 (0x180012e60)");
static_assert(offsetof(EdgeMesh, _indexBuffer)          == 0x68,  "EdgeMesh::_indexBuffer @ +0x68 (0x180012e50)");
static_assert(offsetof(EdgeMesh, _skinVertexBuffer)     == 0x88,  "EdgeMesh::_skinVertexBuffer @ +0x88 (0x180012e80)");
static_assert(offsetof(EdgeMesh, _skinIndexBuffer)      == 0xa8,  "EdgeMesh::_skinIndexBuffer @ +0xa8 (0x180012e70)");
static_assert(offsetof(EdgeMesh, _edgeVertexBuffer)     == 0xc8,  "EdgeMesh::_edgeVertexBuffer @ +0xc8 (0x180012ea0)");
static_assert(offsetof(EdgeMesh, _edgeIndexBuffer)      == 0xe8,  "EdgeMesh::_edgeIndexBuffer @ +0xe8 (0x180012e90)");
static_assert(offsetof(EdgeMesh, _edgeSkinVertexBuffer) == 0x108, "EdgeMesh::_edgeSkinVertexBuffer @ +0x108 (0x180012ec0)");
static_assert(offsetof(EdgeMesh, _edgeSkinIndexBuffer)  == 0x128, "EdgeMesh::_edgeSkinIndexBuffer @ +0x128 (0x180012eb0)");
static_assert(offsetof(EdgeMesh, _triangleLists)        == 0x148, "EdgeMesh::_triangleLists @ +0x148 (0x180012ed0)");
static_assert(offsetof(EdgeMesh, _edgeLists)            == 0x158, "EdgeMesh::_edgeLists @ +0x158 (0x180012ee0)");
static_assert(offsetof(EdgeMesh, _aabb)                 == 0x168, "EdgeMesh::_aabb @ +0x168 (0x180012f30)");

static_assert(sizeof(EdgeMesh::TriangleVertex) == 8,  "EdgeMesh::TriangleVertex is 8 bytes");
static_assert(sizeof(EdgeMesh::EdgeVertex)     == 16, "EdgeMesh::EdgeVertex is 16 bytes");

}  // namespace Graphics

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- graphics/closure_batch2b.cpp
// B.4: typed -- the context is the `IO::Serializer`, the data the
// `GraphicsMesh::TriangleList` array (all three callers are graphics bodies).
void RtGraphics_EdgeMesh_SerializeTriangleListArray(IO::Serializer& ser, int count,
                                                    Graphics::GraphicsMesh::TriangleList* data);  // 0x180014850
// ==== end B3R W0-P0c ====

// ==== B3R RT-VG: the two EdgeMesh vertex constructors (graphics/mesh.cpp) ====
// C++ linkage, global namespace; given external linkage for their exe-side reader.
// ??0TriangleVertex@EdgeMesh@Graphics@@QEAA@AEBVVector3@Math@@@Z
RtGraphics::TriangleVertex* RtGraphics_EdgeMesh_TriangleVertex_Ctor(RtGraphics::TriangleVertex* self,
                                                                    const Math::Vector3* vec3);  // 0x1800a2f50
// ??0EdgeVertex@EdgeMesh@Graphics@@QEAA@AEBUTriangleVertex@12@0H@Z
RtGraphics::EdgeVertex* RtGraphics_EdgeMesh_EdgeVertex_Ctor(RtGraphics::EdgeVertex* self,
                                                            const RtGraphics::TriangleVertex* a,
                                                            const RtGraphics::TriangleVertex* b,
                                                            int flags);  // 0x1800a2df0
// ==== end B3R RT-VG ====

// ==== B3R W2-RES: the body the 0x180100f20 address row held (graphics/mesh.cpp) ====
// ?AddDrawItems@MeshInstance@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
// src/diag/scene_watch.cpp's IsMeshInstance compares a vtable entry with it.
// B.4: typed; standalone takes only its address.
namespace Graphics { class MeshInstance; class Scene; class Renderer; }
extern "C" void RtGraphics_MeshInstance_AddDrawItems(const Graphics::MeshInstance* self,
                                                     const Graphics::Scene* scene,
                                                     Graphics::Renderer* renderer);  // 0x180100f20

// src/engine/graphics/wave29_lane_slots.cpp -- prints the `[SLOTS29]` attendance
// line (MeshInstance::Update / SetSharedInstanceData counters). Called once at
// boot, from RuntimeInit() (src/app/runtime_init.cpp).
extern "C" void RtGraphicsWave29SlotsSummary();
