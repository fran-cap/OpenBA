#pragma once
// ============================================================================
// src/engine/picking/PickingMesh.h — Picking::PickingMesh, 0x48 bytes,
//                                        9 slots
// ============================================================================
//
// ENDGAME B.3 step 7, block B6. Split out of src/engine/picking/Scene.h,
// which carried a POD `RtPicking::PickingMesh` with only +0x38/+0x40 pinned and
// no size, while src/engine/picking/pickingmesh.cpp:87 kept a SECOND,
// anonymous-namespace `PickingMesh` with the full Array header. Both FOLD into
// this one class (RULE B7-6); the nested value types move with it under
// retail's nesting (`Picking::PickingMesh::SubMesh` is the mangled
// `USubMesh@12@`, `Triangle` is `UTriangle@PickingMesh@Picking@@`).
//
//   sizeof 0x48   `MOV EDX,0x48` in its own slot-0 vector deleting destructor
//                 (0x180072050) — B37_SLOTS.md section 1. The missing size
//                 assert the step-7 rule sheet asks for (B37_RULES 2.3).
//   parent        "Object::External" (`?_ParentTypeName@PickingMesh@Picking@@`
//                 0x180071a80, B37_SLOTS.md section 3).
//
// FIELD EVIDENCE (Construct/Reconstruct zero +0x20/+0x28 as qwords, +0x30 as
// ONE byte, +0x38/+0x40 as qwords; the factory rows agree):
//   +0x20  System::String   serialized first by Serialize (0x180122960) and
//   +0x28  System::String   released by ~PickingMesh (0x1801228a0); no getter
//                           names either, so both are named by offset (B6-8)
//   +0x30  bool             Serialize: kScalarBool; no getter
//   +0x38  Array<SubMesh>   _nodes. GetNode 0x180071e90 indexes `_nodes + i*0x80`,
//                           GetSubMeshCount 0x180071dc0 returns +0x40 (its size),
//                           and the resize Serialize CALLs (0x18007aa40) reads
//                           the capacity at +0x44 — a full Array header, which
//                           the old Scene.h POD spelled `SubMesh*; int32` only
//                           because that lane never needed the capacity.
//
// `??_7PickingMesh@Picking@@6B@` @ 0x180158fb0, NINE slots (B37_SLOTS.md):
//   [0] +0x00 0x180072050  (unexported) vector deleting dtor — RT_DELDTOR row
//   [1] +0x08 0x180071ab0  ?TypeName@PickingMesh@Picking@@UEBAPEBDXZ
//   [2] +0x10 0x180071a90  ?Type@PickingMesh@Picking@@UEBAIXZ
//   [3] +0x18 0x180122940  ?GetFileVersion@PickingMesh@Picking@@UEBAHXZ  (MOV EAX,9)
//   [4] +0x20 0x180122950  ?Compile@PickingMesh@Picking@@  OVERRIDDEN (B7-1.7)
//   [5] +0x28 0x180122960  ?Serialize@PickingMesh@Picking@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180071cd0  ?Reconstruct@PickingMesh@Picking@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180071db0  ?Destruct@PickingMesh@Picking@@UEAAXXZ
//   [8] +0x40 0x180071d40  ?Construct@PickingMesh@Picking@@UEAAPEAVExternal@Object@@XZ
//                          — INTRODUCED HERE (B7-1.6)
//
// ENDGAME B.3 step 9 PILOT (B39_RULES.md D1/D2): CONCRETE. The class has a
// real public constructor and its own slot 0, so every live PickingMesh wears
// the COMPILER's table: `CreateNewObject` / `CreateInPlaceObject` (retail's
// factory pair, registered by name and address from pickingmesh.cpp) are
// MemAlloc + `::new (p) PickingMesh()`, replacing the FACTREST step table
// that stamped the minted `??_7PickingMesh@Picking@@6B@`. The minted row, its
// `RT_DELDTOR` row 0x180072050, the `??1` door and the eight slot doors are
// retired (tools/rt_mint_retired/pilot.txt). The exe reaches this class only
// through `?_typeInfo@` and the `Vertices` import (quarry / terraform
// deformation).
//
// PORTABILITY: no <windows.h>, per PLAN.md section 4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../global_types.h"     // Vector3
#include "../math/Math.h"                     // Transform
#include "../volume/Volume.h"                 // Box
#include "../collection/Collection.h"
#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree
#include "../system/String.h"                  // SubMesh::_name

namespace rtcoll {
// B.4 wave Y (RT-GFX): the row of `Array<PickingMesh::SubMesh>`
// (src/engine/collection/array.cpp, SetSize 0x18007aa40).
extern const ArraySpec kSpecD2_SubMesh80;
}

namespace System { class Filename; }

namespace Picking {

class PickingMesh : public Object::External {
public:
    // ========================================================================
    // Picking::PickingMesh::Triangle — 12 bytes: SIX uint16 fields.
    //
    // The first three are pinned by Scene::Intersect (retail 0x1801246c0): the
    // triangle walk advances by 0xc per iteration (`ADD RBX,0xc` at
    // 0x1801250af) and reads exactly three ushorts at +0x00/+0x02/+0x04, each
    // multiplied by 0xc to index the vertex Array. The other six bytes are
    // pinned by Serialize's triangle walk (0x18007a350): TWO back-to-back
    // `kArrayUShort(n=3)` calls, at elem+0x00 and elem+0x06. What j0..j2 MEAN
    // is unverified; the BYTES are fully accounted for.
    // ========================================================================
    struct Triangle {
        uint16_t i0;  // +0x00  vertex index (Intersect)
        uint16_t i1;  // +0x02  vertex index (Intersect)
        uint16_t i2;  // +0x04  vertex index (Intersect)
        uint16_t j0;  // +0x06  second triple (Serialize 0x18007a350) -- MEANING unverified
        uint16_t j1;  // +0x08
        uint16_t j2;  // +0x0a
    };

    // Two uint16s per element of SubMesh::_uvs (Serialize: `kArrayUShort(n=2)`,
    // resize 0x18007a900 stride 4 align 2). Named for the most plausible
    // reading (a compact UV pair); MEANING unverified, byte shape is not.
    // Not a retail-attested name.
    struct UV16 {
        uint16_t u;  // +0x00
        uint16_t v;  // +0x02
    };

    // ========================================================================
    // Picking::PickingMesh::SubMesh — 0x80 bytes, one node of a small
    // hierarchy. Stride 0x80 is pinned three independent ways: GetNode
    // (0x180071e90) returns `_nodes + i*0x80`; Vertices/Normals/Triangles
    // (0x180071e10/e30/e50) return `_nodes + <field> + i*0x80`; and AddMesh
    // (0x1801252e0) walks the array with `lVar14 += 0x80`.
    //
    // | Off  | Field       | Pinned by                                         |
    // |------|-------------|---------------------------------------------------|
    // | 0x00 | _name       | Serialize's per-SubMesh loop (0x18007ac20) opens  |
    // |      |             | with String::Serialize(elem+0x00); the fresh-tail |
    // |      |             | init in 0x18007aa40 zeroes this qword             |
    // | 0x08 | _parent     | AddMesh indexes the WORLD transform array with    |
    // |      |             | it; its loop starts at i == 1 (node 0 is the root)|
    // | 0x0c | _local      | AddMesh: quaternion at +0x0c, translation at      |
    // |      |             | +0x1c..+0x27 — exactly a Math::Transform          |
    // | 0x28 | _aabb       | AABB (0x180071dd0) copies six dwords +0x28..+0x3f |
    // | 0x40 | _vertices   | Vertices  (0x180071e10)                           |
    // | 0x50 | _normals    | Normals   (0x180071e30)                           |
    // | 0x60 | _triangles  | Triangles (0x180071e50); TriangleCount            |
    // |      |             | (0x180071e70) reads +0x68, that Array's _size     |
    // | 0x70 | _uvs        | Serialize's loop tail: size at elem+0x78, resize  |
    // |      |             | 0x18007a900 (stride 4, align 2)                   |
    //
    // `_name` is a System::String (B.4 wave Y, RT-GFX). Array<SubMesh> is grown
    // by the transcribed 0x18007aa40 body (element copy ctor 0x1800734a0 adopts
    // the String with a refcount bump) and destroyed by 0x180073ee0 -- the row
    // kSpecD2_SubMesh80 that `_nodes` names. ~PickingMesh calls the registered
    // ~Array 0x180073720, which zeroes the header, so the member's implicit
    // ~Array releases nothing; no SubMesh is built, copied or destroyed through
    // C++ outside the row.
    // ========================================================================
    struct SubMesh {
        System::String               _name;           // +0x00  (the row owns its lifecycle)
        int32_t                      _parent;         // +0x08
        ::Transform                  _local;          // +0x0c  (0x1c bytes)
        ::Box                        _aabb;           // +0x28
        Collection::Array<Vector3>   _vertices;       // +0x40
        Collection::Array<Vector3>   _normals;        // +0x50
        Collection::Array<Triangle>  _triangles;      // +0x60
        Collection::Array<UV16>      _uvs;            // +0x70
    };

    // ??0PickingMesh@Picking@@QEAA@XZ 0x180122830 — the REAL constructor. Its
    // stores are the ones retail's factory pair inlines (0x180071c03..c3f):
    // Base's and External's vptr (External(), the compiler's), +0x08/+0x10/
    // +0x18 zero (External()), PickingMesh's vptr (the compiler's), +0x20 and
    // +0x28 zero, +0x30 ONE zero byte, +0x38/+0x40 zero (the Array's ctor).
    // B4 X-RH: the +0x20 / +0x28 zeroes are the two System::String members'
    // default constructors, run before this body.
    PickingMesh() : Object::External() {
        _allowChildren = false;
    }

    // ---- the nine slots, in measured order ----
    // [0] 0x180072050 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (deleting_dtor.cpp DeletingDtorCore,
    // `MOV EDX,0x48` = the element size): `flags & 2` reads the SIGNED int32
    // count at `this - 8`, destroys the elements DOWNWARD, frees `this - 8` on
    // `flags & 1` and returns it; otherwise ~PickingMesh, free on `flags & 1`.
    // Nothing `new[]`s a PickingMesh, so the array arm is the faithful
    // unreachable it is in Object::Internal's (internal_class.cpp).
    // `??1PickingMesh@Picking@@UEAA@XZ` 0x1801228a0. VIRTUAL (the normal-virtual
    // cutover, B3X W1); pickingmesh.cpp holds retail's
    // body (the SubMesh array through 0x180073720, then both Strings) and the
    // compiler chains ~External, retail's tail call.
    ~PickingMesh() override;

    // [1] 0x180071ab0 TypeName / [2] 0x180071a90 Type, the static TypeId
    // 0x180071aa0, `_TypeName` / `_ParentTypeName` and the factory pair
    // 0x180071ba0 / 0x180071c60 (pickingmesh.cpp): retail's macro (ENDGAME
    // B.3.9 TI-REST, B39_RULES D6). The record is `DEFINE_TYPE`d in
    // src/engine/picking/picking_type_records.cpp.
    DECLARE_TYPE(PickingMesh)
public:
    int32_t       GetFileVersion() const override;             // [3] 0x180122940
    CompileResult Compile(const System::Filename& path) override; // [4] 0x180122950
    bool          Serialize(IO::Stream& stream) override;            // [5] 0x180122960
    Object::External* Reconstruct() override;                  // [6] 0x180071cd0
    void          Destruct() override;                         // [7] 0x180071db0
    virtual Object::External* Construct();                     // [8] 0x180071d40 — B7-1.6

    // ---- the non-slot exported surface ---------------------------------------
    // Each is one address computation in retail (read by the Wave 5 lane D
    // write-up and re-cited in the SubMesh table above). References: NO hidden
    // return slot. None range-checks `i` (no guard added: CLAUDE.md).
    int32_t GetSubMeshCount() const { return _nodes._size; }                         // 0x180071dc0
    const Collection::Array<Vector3>& Vertices(int32_t i) const  { return _nodes._data[i]._vertices; }  // 0x180071e10
    const Collection::Array<Vector3>& Normals(int32_t i) const   { return _nodes._data[i]._normals; }   // 0x180071e30
    const Collection::Array<Triangle>& Triangles(int32_t i) const { return _nodes._data[i]._triangles; } // 0x180071e50
    int32_t TriangleCount(int32_t i) const { return _nodes._data[i]._triangles._size; }              // 0x180071e70
    const SubMesh& GetNode(int32_t i) const { return _nodes._data[i]; }                              // 0x180071e90

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    // 0x180071dd0 — RETSLOT in retail (`?AVBox@Volume@@`); no caller anywhere.
    ::Box AABB(int32_t i) const;
    PickingMesh(const PickingMesh& src);                      // 0x180071ea0
    PickingMesh& operator=(const PickingMesh& src);           // 0x180071f60
    static void* operator new(std::size_t bytes);              // 0x180071ac0
    static void* operator new(std::size_t bytes, void* place); // 0x180071b30
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x180071b40
    // Retail's factory pair (B39_RULES D2): `MemAlloc(0x48, 8)` + the ctor, and
    // the null-tested in-place ctor. Defined in pickingmesh.cpp. NOTE the class
    // declares its own placement `operator new` (0x180071b30, not ported), so
    // every placement construction in the tree spells `::new (p) PickingMesh()`.
    // (Declared by DECLARE_TYPE above.)

public:
    // ---- the 0x28 bytes PickingMesh adds (public: the doors and the Scene
    // bodies reach them) ----
    // B4 X-RH: System::String members. Retail's ~PickingMesh 0x1801228a0
    // destroys +0x38 (0x1801228c4), then +0x28 (0x1801228ca..), then +0x20
    // (0x1801228f4..), then ~External (0x180122926): the body does +0x38, the
    // compiler the two Strings in reverse declaration order.
    System::String              _meshName;   // +0x20
    System::String              _subObject;   // +0x28
    bool                        _allowChildren;     // +0x30  kScalarBool
    uint8_t                     _pad31[7];   // +0x31  never written (one-byte store)
    Collection::Array<SubMesh, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpecD2_SubMesh80>>
                                _nodes;      // +0x38  data, size@+0x40, cap@+0x44
};

static_assert(sizeof(PickingMesh::Triangle) == 0xc, "PickingMesh::Triangle must be 12 bytes");
static_assert(offsetof(PickingMesh::Triangle, i0) == 0x0, "Triangle::i0");
static_assert(offsetof(PickingMesh::Triangle, i1) == 0x2, "Triangle::i1");
static_assert(offsetof(PickingMesh::Triangle, i2) == 0x4, "Triangle::i2");
static_assert(offsetof(PickingMesh::Triangle, j0) == 0x6, "Triangle::j0");
static_assert(offsetof(PickingMesh::Triangle, j1) == 0x8, "Triangle::j1");
static_assert(offsetof(PickingMesh::Triangle, j2) == 0xA, "Triangle::j2");
static_assert(sizeof(PickingMesh::UV16) == 4, "PickingMesh::UV16 must be 4 bytes");
static_assert(offsetof(PickingMesh::UV16, u) == 0x0, "UV16::u");
static_assert(offsetof(PickingMesh::UV16, v) == 0x2, "UV16::v");
static_assert(sizeof(PickingMesh::SubMesh) == 0x80, "PickingMesh::SubMesh must be 0x80 bytes");
static_assert(offsetof(PickingMesh::SubMesh, _name) == 0x0, "SubMesh::_name");
static_assert(offsetof(PickingMesh::SubMesh, _parent)    == 0x08, "SubMesh::_parent @ +0x08");
static_assert(offsetof(PickingMesh::SubMesh, _local)     == 0x0c, "SubMesh::_local @ +0x0c");
static_assert(offsetof(PickingMesh::SubMesh, _aabb)      == 0x28, "SubMesh::_aabb @ +0x28");
static_assert(offsetof(PickingMesh::SubMesh, _vertices)  == 0x40, "SubMesh::_vertices @ +0x40");
static_assert(offsetof(PickingMesh::SubMesh, _normals)   == 0x50, "SubMesh::_normals @ +0x50");
static_assert(offsetof(PickingMesh::SubMesh, _triangles) == 0x60, "SubMesh::_triangles @ +0x60");
static_assert(offsetof(PickingMesh::SubMesh, _uvs)       == 0x70, "SubMesh::_uvs @ +0x70");

static_assert(sizeof(PickingMesh) == 0x48,
              "Picking::PickingMesh is 0x48 bytes (MOV EDX,0x48 in slot 0, 0x180072050)");
static_assert(offsetof(PickingMesh, _pad31) == 0x31, "PickingMesh::_pad31");
static_assert(alignof(PickingMesh) == 8, "Picking::PickingMesh aligns to 8 (the vptr)");
static_assert(offsetof(PickingMesh, _meshName) == 0x20, "PickingMesh +0x20 String");
static_assert(offsetof(PickingMesh, _subObject) == 0x28, "PickingMesh +0x28 String");
static_assert(offsetof(PickingMesh, _allowChildren)   == 0x30, "PickingMesh +0x30 bool");
static_assert(offsetof(PickingMesh, _nodes)    == 0x38, "GetNode 0x180071e90 indexes +0x38");
static_assert(offsetof(PickingMesh, _nodes) + offsetof(Collection::Array<PickingMesh::SubMesh>, _size) == 0x40,
              "GetSubMeshCount 0x180071dc0 returns +0x40");
static_assert(offsetof(PickingMesh, _meshName) == sizeof(Object::External),
              "PickingMesh's own bytes begin right after Object::External");

}  // namespace Picking
