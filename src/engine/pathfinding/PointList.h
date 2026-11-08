#pragma once
// ============================================================================
// src/engine/pathfinding/PointList.h — Pathfinding::PointList, 0x50 bytes,
//                                          9 slots
// ============================================================================
//
// ENDGAME B.3 step 7, block B6 (RULE B7-9). No header existed for this class
// before: the 0x50 layout lived in prose and in an anonymous-namespace struct
// in src/engine/pathfinding/pointlist.cpp. It is MOVED here (RULE B6-8) and the
// fields are named by the exported getters, read as RETAIL disassembly this
// pass (Runtime-x64.dll, 15290 functions):
//
//   ?Points@PointList@Pathfinding@@QEBAAEBV?$Array@UPoint@...@Collection@@XZ
//                                     0x180071190  LEA RAX,[RCX+0x30] ; RET
//   ?PathPoints@PointList@Pathfinding@@QEBAAEBV?$Array@UPoint@...@Collection@@XZ
//                                     0x1800711a0  LEA RAX,[RCX+0x40] ; RET
//   ?GetMeshName@PointList@Pathfinding@@QEBA?AVString@System@@XZ
//                                     0x1800711b0  MOV [RDX],0 ; RAX=[RCX+0x20] ;
//                                                  bump [RAX-4] ; RAX=RDX — a
//                                                  hidden RETSLOT copy of +0x20
//
//   sizeof 0x50   `MOV EDX,0x50` in its own slot-0 vector deleting destructor
//                 (0x1800713b0) — B37_SLOTS.md section 1 — and `Allocate(0x50, 8)`
//                 in the factory row (rt_factories_pathfinding.inc).
//   parent        "Object::External" (`?_ParentTypeName@PointList@Pathfinding@@`
//                 0x180070e10, B37_SLOTS.md section 3).
//   +0x28         a second System::String. Serialize (0x18010f6e0) hands it to
//                 String::Serialize right after +0x20 and ~PointList releases it;
//                 no getter names it, so it is named by offset (B6-8).
//
// LIVE (B37_RULES 6.2): the exe imports `?_typeInfo@PointList@Pathfinding@@`
// and every InteractDescription / ParticleDescription resource loads one
// through the factory. `src/game/Component/interactdescription.cpp` reads
// `_points` (+0x30/+0x38) by RAW OFFSET — the asserts below are its contract.
//
// `??_7PointList@Pathfinding@@6B@` @ 0x180158f50, NINE slots (B37_SLOTS.md):
//   [0] +0x00 0x1800713b0  (unexported) vector deleting dtor — RT_DELDTOR row
//   [1] +0x08 0x180070e40  ?TypeName@PointList@Pathfinding@@UEBAPEBDXZ
//   [2] +0x10 0x180070e20  ?Type@PointList@Pathfinding@@UEBAIXZ
//   [3] +0x18 0x18010f880  ?GetFileVersion@PointList@Pathfinding@@UEBAHXZ  (MOV EAX,5)
//   [4] +0x20 0x18010f6d0  ?Compile@PointList@Pathfinding@@  OVERRIDDEN (B7-1.7)
//   [5] +0x28 0x18010f6e0  ?Serialize@PointList@Pathfinding@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180071060  ?Reconstruct@PointList@Pathfinding@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180071150  ?Destruct@PointList@Pathfinding@@UEAAXXZ
//   [8] +0x40 0x1800710e0  ?Construct@PointList@Pathfinding@@UEAAPEAVExternal@Object@@XZ
//                          — INTRODUCED HERE (B7-1.6, the B5-16 resource shape)
//
// ENDGAME B.3 step 9 (block ENG, B39_RULES.md D1/D2): CONCRETE. The class has
// retail's real public constructor (??0PointList 0x18010f980) and its own
// slot 0, so every live PointList wears the COMPILER's table:
// `CreateNewObject` / `CreateInPlaceObject` (retail's factory pair, registered
// by name and address from pointlist.cpp) are MemAlloc + `::new (p)
// PointList()`, replacing the FACTREST step table that stamped the minted
// `??_7PointList@Pathfinding@@6B@`. The minted row, its `RT_DELDTOR` row
// 0x1800713b0, the `??1` door and the eight slot doors are retired
// (tools/rt_mint_retired/eng.txt).
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../collection/Collection.h"
#include "../system/String.h"
#include "../math/Math.h"                     // Math::Vector3 (Point::pos)
#include "../object/Internal.h"               // Object::detail::InternalMemFree

namespace System { class Filename; }

namespace Pathfinding {

class PointList : public Object::External {
public:
    // ------------------------------------------------------------------------
    // Pathfinding::PointList::Point — 0x18 bytes (retail `UPoint@PointList@`).
    // Pinned by Serialize 0x18010f6e0's element loop: String::Serialize at
    // elem+0, kArrayFloat(n=3) at elem+8, kScalarFloat at elem+0x14; the stride
    // 0x18 by the Array<Point> bodies 0x180071730 / 0x180071890 / 0x180071600.
    //
    // B.4 R-SYS (H5, a `behaviour:` commit): `name` is the real String and
    // `pos` the real Vector3. No Point is ever constructed, copied or
    // destroyed as a C++ object: the ctor 0x18010f980 only zeroes the two
    // Array headers, ~PointList 0x18010f8e0 calls 0x180071600 on both arrays
    // (0x180071890 releases each element's String, then free, then the header
    // is ZEROED), and the grow body 0x180071730 copies the String by hand
    // (null, adopt, bump) — all three stay the hand-written bodies in
    // pointlist.cpp, which now reach `name._data` / `pos.x` by name. The
    // member `~Array` that runs after ~PointList's body sees size 0 and
    // destroys nothing, as before. Vector3: the element's +0x08 is serialized
    // as `SerializeFloatArray(3, ...)` (slot +0x28), which is exactly
    // `?Serialize@Vector3@Math@@` 0x1800150e0 inlined.
    // NAMING (known_port_issues 14f): `radius` is read as a YAW by every
    // reader (interactdescription.cpp, boatcomponent_route.cpp); the name is
    // left until retail gives evidence.
    // ------------------------------------------------------------------------
    struct Point {
        System::String name;    // +0x00  refcounted
        Math::Vector3  pos;     // +0x08
        float          radius;  // +0x14
    };

    // ??0PointList@Pathfinding@@QEAA@XZ 0x18010f980 -- the REAL constructor.
    // Its stores are the ones retail's factory pair inlines (0x180070f93..fd4):
    // Base's and External's vptr (External(), the compiler's), +0x08/+0x10/
    // +0x18 zero (External()), PointList's vptr (the compiler's), then SIX zero
    // qwords +0x20..+0x48 -- the two Strings' null and the two Arrays' headers.
    PointList() : Object::External(), _meshName(), _subObject(), _points(), _pathPoints() {}

    // ---- the nine slots, in measured order ----
    // [0] 0x1800713b0 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (DeletingDtorCore, element size
    // 0x50): `flags & 2` reads the SIGNED int32 count at `this - 8`, destroys
    // the elements DOWNWARD, frees `this - 8` on `flags & 1` and returns it;
    // otherwise ~PointList, free on `flags & 1`. Nothing `new[]`s a PointList.
    // `??1PointList@Pathfinding@@UEAA@XZ` 0x18010f8e0. VIRTUAL (the normal-virtual
    // cutover, B3X W1); pointlist.cpp holds retail's body
    // (both arrays through 0x180071600) and the compiler releases both Strings
    // and chains ~External, retail's order.
    ~PointList() override;

    // [1] 0x180070e40 TypeName / [2] 0x180070e20 Type, the static TypeId
    // 0x180070e30, `_TypeName` / `_ParentTypeName` and the factory pair
    // 0x180070f30 / 0x180070ff0 (pointlist.cpp): retail's macro (ENDGAME
    // B.3.9 TI-REST, B39_RULES D6). The record is `DEFINE_TYPE`d in
    // src/engine/pathfinding/pathfinding_type_records.cpp.
    DECLARE_TYPE(PointList)
public:
    int32_t       GetFileVersion() const override;             // [3] 0x18010f880
    CompileResult Compile(const System::Filename& path) override; // [4] 0x18010f6d0
    bool          Serialize(IO::Stream& stream) override;            // [5] 0x18010f6e0
    Object::External* Reconstruct() override;                  // [6] 0x180071060
    void          Destruct() override;                         // [7] 0x180071150
    virtual Object::External* Construct();                     // [8] 0x1800710e0 — B7-1.6

    // ---- the non-slot exported surface (read this pass, see the banner) ----
    const Collection::Array<Point>& Points() const     { return _points; }      // 0x180071190
    const Collection::Array<Point>& PathPoints() const { return _pathPoints; }  // 0x1800711a0
    // 0x1800711b0 — RETSLOT in retail; a plain by-value return here (not a
    // vtable slot, so both ends of every C++ call are this GCC). The copy
    // bumps the refcount exactly as retail's `INC [RAX-4]` does.
    System::String GetMeshName() const { return _meshName; }

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    PointList(const PointList& src);                          // 0x1800711f0
    PointList& operator=(const PointList& src);               // 0x1800712c0
    static void* operator new(std::size_t bytes);              // 0x180070e50
    static void* operator new(std::size_t bytes, void* place); // 0x180070ec0
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x180070ed0
    // Retail's factory pair (B39_RULES D2): `MemAlloc(0x50, 8)` + the ctor, and
    // the null-tested in-place ctor. Defined in pointlist.cpp. The class
    // declares its own placement `operator new` (0x180070ec0, not ported), so
    // every placement construction spells `::new (p) PointList()`.
    // (Declared by DECLARE_TYPE above.)

public:
    // ---- the 0x30 bytes PointList adds (public: the doors and the exe-side
    // raw reader reach them) ----
    System::String           _meshName;    // +0x20  GetMeshName 0x1800711b0
    System::String           _subObject;    // +0x28  serialized second; no getter
    Collection::Array<Point> _points;      // +0x30  Points 0x180071190
    Collection::Array<Point> _pathPoints;  // +0x40  PathPoints 0x1800711a0
};

static_assert(sizeof(PointList::Point) == 0x18, "Pathfinding::PointList::Point is 0x18 bytes");
static_assert(offsetof(PointList::Point, name)   == 0x00, "Point::name @ +0x00 (String::Serialize at elem+0)");
static_assert(offsetof(PointList::Point, pos)    == 0x08, "Point::pos @ +0x08 (kArrayFloat n=3)");
static_assert(offsetof(PointList::Point, radius) == 0x14, "Point::radius @ +0x14 (kScalarFloat)");

static_assert(sizeof(PointList) == 0x50,
              "Pathfinding::PointList is 0x50 bytes (MOV EDX,0x50 in slot 0, 0x1800713b0)");
static_assert(alignof(PointList) == 8, "Pathfinding::PointList aligns to 8 (the vptr)");
static_assert(offsetof(PointList, _meshName)   == 0x20, "GetMeshName 0x1800711b0 reads +0x20");
static_assert(offsetof(PointList, _subObject)   == 0x28, "Serialize's second String @ +0x28");
static_assert(offsetof(PointList, _points)     == 0x30, "Points 0x180071190 is LEA [RCX+0x30]");
static_assert(offsetof(PointList, _pathPoints) == 0x40, "PathPoints 0x1800711a0 is LEA [RCX+0x40]");
static_assert(offsetof(PointList, _meshName) == sizeof(Object::External),
              "PointList's own bytes begin right after Object::External");

}  // namespace Pathfinding
