#pragma once
// ============================================================================
// src/engine/collision2d/CollisionMesh.h — Collision2D::CollisionMesh,
//     0x70 bytes, 9 slots (original `Collision2DMesh.h`)
// ============================================================================
//
// ENDGAME B.3 step 9, block ENG (B39_RULES.md D1/D2). Until this commit the
// class was a file-local POD in src/engine/collision2d/collisionmesh.cpp and
// every live CollisionMesh was built by the FACTREST step table, wearing the
// MINTED `??_7CollisionMesh@Collision2D@@6B@`. It is a real class now: retail's
// public constructor (??0CollisionMesh 0x180065380), its own slot 0, and the
// factory pair as its own statics -- every live CollisionMesh wears the
// COMPILER's table. The layout and the nested element types are the ones the
// POD carried (collisionmesh.cpp's banner has the evidence), moved here.
//
// `??_7CollisionMesh@Collision2D@@6B@` @ 0x180158e90, NINE slots:
//   [0] +0x00 0x1800656b0  (unexported) vector deleting dtor, element 0x70
//   [1] +0x08 0x180064f70  ?TypeName@CollisionMesh@Collision2D@@UEBAPEBDXZ
//   [2] +0x10 0x180064f50  ?Type@CollisionMesh@Collision2D@@UEBAIXZ
//   [3] +0x18 0x180108cc0  ?GetFileVersion@CollisionMesh@Collision2D@@UEBAHXZ (3)
//   [4] +0x20 0x180108cb0  ?Compile@CollisionMesh@Collision2D@@  OVERRIDDEN (1)
//   [5] +0x28 0x180108cd0  ?Serialize@CollisionMesh@Collision2D@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x1800651b0  ?Reconstruct@CollisionMesh@Collision2D@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x1800652c0  ?Destruct@CollisionMesh@Collision2D@@UEAAXXZ
//   [8] +0x40 0x180065240  ?Construct@CollisionMesh@Collision2D@@UEAAPEAVExternal@Object@@XZ
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree
#include "../collection/Collection.h"
#include "../system/String.h"

namespace System { class Filename; }

namespace Collision2D {

class Surface;

class CollisionMesh : public Object::External {
public:
    // ---- the element types (collisionmesh.cpp's banner) ----
    struct Vec2 { float x, y; };
    struct Triple3 { uint16_t a, b, c; };
    struct Quad4 { uint16_t a, b, c, d; };
    struct Face {
        int32_t                    surfaceIndex;   // +0x00  kScalarInt
        int32_t                    _pad04;   // +0x04
        Collection::Array<Triple3> triples;  // +0x08
        Collection::Array<Quad4>   quads;    // +0x18
    };

    // ??0CollisionMesh@Collision2D@@QEAA@XZ 0x180065380 -- the REAL
    // constructor. Its stores are the ones retail's factory pair inlines
    // (0x1800650c3..114): External() (Base's and External's vptr, three zero
    // qwords), CollisionMesh's vptr (the compiler's), then TEN zero qwords
    // +0x20..+0x68 -- the two Strings' null and the four Arrays' headers.
    CollisionMesh()
        : Object::External(), _name1(), _name2(), _ptsA(), _ptsB(), _faces(), _surfaces() {}

    // [0] 0x1800656b0 -- MSVC's VECTOR deleting destructor (retail; element
    // size 0x70); here the virtual destructor (B3X W1).
    // ??1CollisionMesh@Collision2D@@UEAA@XZ 0x180065400. VIRTUAL (B3X W1);
    // collisionmesh.cpp holds retail's body (the four ~Array CALLs by address,
    // which free and zero each header); the compiler then releases both
    // Strings and chains ~External, retail's order.
    ~CollisionMesh() override;
    // [1] 0x180064f70 TypeName / [2] 0x180064f50 Type, the static TypeId
    // 0x180064f60, `_TypeName` / `_ParentTypeName` and the factory pair
    // 0x180065060 / 0x180065130 (collisionmesh.cpp): retail's macro (ENDGAME
    // B.3.9 TI-REST, B39_RULES D6); the record is `DEFINE_TYPE`d in
    // src/engine/collision2d/collision2d_type_records.cpp.
    DECLARE_TYPE(CollisionMesh)
public:
    int32_t           GetFileVersion() const override;            // [3] 0x180108cc0
    CompileResult     Compile(const System::Filename& path) override; // [4] 0x180108cb0
    bool              Serialize(IO::Stream& stream) override;           // [5] 0x180108cd0
    Object::External* Reconstruct() override;                     // [6] 0x1800651b0
    void              Destruct() override;                        // [7] 0x1800652c0
    virtual Object::External* Construct();                        // [8] 0x180065240

    // Retail's factory pair (B39_RULES D2; declared by DECLARE_TYPE), defined
    // in collisionmesh.cpp. The class has its own placement `operator new` in
    // retail (0x180064ff0, not ported): every placement construction spells
    // `::new (p) CollisionMesh()`.

public:
    System::String              _name1;     // +0x20
    System::String              _name2;     // +0x28
    Collection::Array<Vec2>     _ptsA;      // +0x30
    Collection::Array<Vec2>     _ptsB;      // +0x40
    Collection::Array<Face>     _faces;     // +0x50
    Collection::Array<Surface*> _surfaces;  // +0x60  ExternalPtr<Surface> (B4 H4: typed; same array traits as void*)
};

static_assert(sizeof(CollisionMesh::Vec2) == 8, "Vec2 is 8 bytes");
static_assert(offsetof(CollisionMesh::Vec2, x) == 0x0, "Vec2::x");
static_assert(offsetof(CollisionMesh::Vec2, y) == 0x4, "Vec2::y");
static_assert(sizeof(CollisionMesh::Triple3) == 6, "Triple3 is 6 bytes");
static_assert(offsetof(CollisionMesh::Triple3, a) == 0x0, "Triple3::a");
static_assert(offsetof(CollisionMesh::Triple3, b) == 0x2, "Triple3::b");
static_assert(offsetof(CollisionMesh::Triple3, c) == 0x4, "Triple3::c");
static_assert(sizeof(CollisionMesh::Quad4) == 8, "Quad4 is 8 bytes");
static_assert(offsetof(CollisionMesh::Quad4, a) == 0x0, "Quad4::a");
static_assert(offsetof(CollisionMesh::Quad4, b) == 0x2, "Quad4::b");
static_assert(offsetof(CollisionMesh::Quad4, c) == 0x4, "Quad4::c");
static_assert(offsetof(CollisionMesh::Quad4, d) == 0x6, "Quad4::d");
static_assert(sizeof(CollisionMesh::Face) == 0x28, "CollisionMesh::Face is 0x28 bytes");
static_assert(offsetof(CollisionMesh::Face, surfaceIndex) == 0x0, "Face::surfaceIndex");
static_assert(offsetof(CollisionMesh::Face, _pad04) == 0x4, "Face::_pad04");
static_assert(offsetof(CollisionMesh::Face, triples) == 0x08, "Face +0x08");
static_assert(offsetof(CollisionMesh::Face, quads)   == 0x18, "Face +0x18");

static_assert(sizeof(CollisionMesh) == 0x70, "Collision2D::CollisionMesh is 0x70 bytes");
static_assert(alignof(CollisionMesh) == 8, "Collision2D::CollisionMesh aligns to 8 (the vptr)");
static_assert(offsetof(CollisionMesh, _name1)    == 0x20, "+0x20");
static_assert(offsetof(CollisionMesh, _name2)    == 0x28, "+0x28");
static_assert(offsetof(CollisionMesh, _ptsA)     == 0x30, "+0x30");
static_assert(offsetof(CollisionMesh, _ptsB)     == 0x40, "+0x40");
static_assert(offsetof(CollisionMesh, _faces)    == 0x50, "+0x50");
static_assert(offsetof(CollisionMesh, _surfaces) == 0x60, "+0x60");
static_assert(offsetof(CollisionMesh, _name1) == sizeof(Object::External),
              "CollisionMesh's own bytes begin right after Object::External");

}  // namespace Collision2D
