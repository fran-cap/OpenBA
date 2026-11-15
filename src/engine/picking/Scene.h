#pragma once
// ============================================================================
// src/engine/picking/Scene.h — Picking::Scene, 0x60 bytes, NOT polymorphic
// ============================================================================
//
// ENDGAME B.3 step 7, block B6. The Wave 5 lane D POD `RtPicking::Scene` (and
// its `Entry`/`Cell`/`Link`/`AddDesc` rows) BECOMES the class under retail's
// name: every includer is in src/engine/picking/, so there is no POD left to
// keep and no alias to leave behind (RULE B7-6, one definition per layout).
// `PickingMesh` and its nested `SubMesh`/`Triangle`/`UV16` moved to
// src/engine/picking/PickingMesh.h.
//
// THREE CLASSES ARE SPELLED `Scene`: `Audio::Scene`, `Picking::Scene`, and the
// Graphics one (plus `Pathfinding::Scene`). Always qualify.
//
// Every offset below was read out of a decompiled or disassembled RETAIL
// Runtime-x64.dll body, and the function and instruction that pins it is named
// next to the field (docs/history/runtime_port/WAVE5_LANE_D.md is the original
// write-up). Nothing here is inferred from the profile build.
//
// HELD BY VALUE (B37_RULES 2.4 and its addendum): the exe's Game object (built
// by 0x14009ee70, `src/game/Game/gameupdateloop_load.cpp`) embeds one at +0x8
// and GameUpdateLoop allocates another (`MemAlloc(0x60,8)` at 0x140097093); both
// holders are unconverted exe PODs that construct in place through
// `RtPicking_Scene_Ctor`, which runs the C++ default constructor `Scene()`
// (`??0Scene@Picking@@QEAA@XZ`) by placement new; that delegates to the grid
// constructor `Scene(16.0f)` (0x18007aef0). A holder that declares a
// `Picking::Scene` member gets the same constructors. CONCRETE, PUBLIC members.
//
// THE DESTRUCTOR IS THE IMPLICIT ONE, and that is retail's: `??1Scene@Picking@@
// QEAA@XZ` (0x180125890) is four `~Array` calls, +0x48, +0x30, +0x18, +0x08 —
// exactly reverse member order — and nothing else. `Entry` carries its inner
// `Array<Math::Transform>` as a real member, so `~Array<Entry>` destroying each
// element first IS the 0x180073c90 J2 body.
//
// NOT TRIVIALLY COPYABLE (B7-8): the retail copy ctor (0x1800722f0 ->
// 0x180072340) is DEEP; declared, not ported, no caller.
//
// ARGUMENT PASSING (RULE B7-5): `AddMesh`'s `ExternalPtr<PickingMesh>` arrives
// as the BARE POINTER in RDX (0x1801252e0: homed at [RBP+0x48], then
// `MOV RAX,[RBP+0x48]; MOV EDX,[RAX+0x40]`), so the member takes it BY VALUE —
// GCC passes the trivially copyable 8-byte class the same way.
// HIDDEN RETURN SLOTS (B37_SLOTS.md section 4), all non-virtual and unported:
// GetMesh, GetAABB, GetLocalAABB, GetTransform.
//
// RULE B6-5: the picking members run on the main thread, but the ctor door runs
// on the LOADER thread (GameUpdateLoop::Load). No member keeps a static
// initialiser, a lazy singleton or a non-atomic first-call cache; the grid
// internals are direct calls (src/engine/picking/scene_grid.h).
//
// PORTABILITY: no <windows.h>, per PLAN.md section 4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "../global_types.h"     // Vector3
#include "../math/Math.h"                     // Scalar, Quaternion, Transform
#include "../volume/Volume.h"                 // Box, Frustum
#include "../collection/Collection.h"         // Collection::Array
#include "../object/External.h"               // Object::ExternalPtr
#include "PickingMesh.h"

namespace System { class Time; }
namespace Volume { struct Plane; }   // no header in this tree (ClipTriangle only)

namespace Picking {

class Scene {
public:
    // ========================================================================
    // The per-mesh hit callback carried by every Scene entry.
    //
    // Spelled out by the mangled parameter of both AddMesh and SetMeshUserData:
    //   P6AX 0 HH AEBVVector3@Math@@ 1 1 @Z
    //   = void (__cdecl*)(void*, int, int, const Vector3&, const Vector3&,
    //                     const Vector3&)
    // Retail never invokes it from any of the ported bodies; it is stored at
    // entry+0x40 and read back by callers. Typed so the AddMesh and
    // SetMeshUserData signatures cannot silently lose their last argument.
    // ========================================================================
    typedef void (*HitCallback)(void* userData, int, int,
                                const Vector3&, const Vector3&, const Vector3&);

    // Retail nests `ClipInfo` here (`UClipInfo@Scene@Picking@@`); its layout was
    // never read (Clip is unported and uncalled).
    struct ClipInfo;

    // ========================================================================
    // Picking::Scene::Entry — 0x48 bytes, one registered pick mesh. (Tree name;
    // retail's own name for the record is not attested.)
    //
    // The strongest single witness is the internal entry allocator at retail
    // 0x18007b210, which builds a complete entry on the stack and hands it to
    // the array grower: every field below is one store in that body. The
    // exported accessors then confirm each one independently.
    //
    // | Off  | Field       | Pinned by                                        |
    // |------|-------------|--------------------------------------------------|
    // | 0x00 | _queryStamp | Intersect (0x1801246c0) writes the Scene's stamp |
    // |      |             | here so an entry spanning several cells is       |
    // |      |             | tested once; 0x18007b210 initialises it to 0     |
    // | 0x04 | _nextFree   | 0x18007b210 stores -2 ("in use") on both paths   |
    // |      |             | and, on the reuse path, pops the free list with  |
    // |      |             | `scene->_freeEntry = entries[_freeEntry]._nextFree`|
    // | 0x08 | _cellX      | 0x1800724d0 (the re-index behind SetTransform)   |
    // | 0x0c | _cellY      | compares and stores all four of these            |
    // | 0x10 | _cellW      | ...as UINT16: `*(ushort*)(base + 0x10 + i*0x48)` |
    // | 0x12 | _cellH      | ...and `+ 0x12`                                  |
    // | 0x18 | _mesh       | GetMesh (0x180072200) returns this qword.        |
    // |      |             | Intersect dereferences it directly to reach      |
    // |      |             | PickingMesh+0x38: ExternalPtr<T> is a bare ptr   |
    // | 0x20 | _transforms | GetTransform (0x180072280) indexes `*(ptr+0x20)` |
    // |      |             | by 0x1c; Intersect bounds its submesh loop on    |
    // |      |             | `*(int*)(entry+0x28)`, that Array's _size        |
    // | 0x30 | _userData   | GetUserData (0x1800722d0), SetMeshUserData arg 2 |
    // | 0x38 | _userValue  | GetUserValue (0x1800722e0), SetMeshUserData arg 3|
    // | 0x3c | _userValue2 | SetMeshUserData arg 4 — no getter is exported    |
    // | 0x40 | _callback   | SetMeshUserData arg 5 / AddMesh arg 7            |
    //
    // +0x14 is slack that 8-aligns _mesh; +0x2c is the transform Array's
    // _capacity.
    // ========================================================================
    struct Entry {
        int32_t  _queryStamp;                            // +0x00
        int32_t  _nextFree;                              // +0x04  -2 while allocated
        int32_t  _cellX;                                 // +0x08
        int32_t  _cellY;                                 // +0x0c
        uint16_t _cellW;                                 // +0x10
        uint16_t _cellH;                                 // +0x12
        uint8_t  _pad14[4];                              // +0x14
        PickingMesh* _mesh;                              // +0x18  ExternalPtr<PickingMesh>'s pointer
        Collection::Array<::Transform> _transforms;      // +0x20
        void*       _userData;                           // +0x30
        int32_t     _userValue;                          // +0x38
        int32_t     _userValue2;                         // +0x3c
        HitCallback _callback;                           // +0x40
    };

    // ========================================================================
    // Picking::Scene::Cell — 0x10 bytes, one occupied grid cell (tree name).
    //
    // Pinned entirely by the cell iterator at retail 0x18007b6d0, which is the
    // traversal Intersect drives:
    //     hash = (y * -0x27e9c7bf + x * -0x72594cbd) % scene->_cellHeads._size
    //     if (hash < 0) hash += scene->_cellHeads._size
    //     for (c = _cellHeads[hash]; c >= 0; c = cells[c]._nextInBucket)
    //         if (cells[c]._x == x && cells[c]._y == y) -> cells[c]._firstLink
    // ========================================================================
    struct Cell {
        int32_t _x;             // +0x00
        int32_t _y;             // +0x04
        int32_t _nextInBucket;  // +0x08  index into _cells, -1 = end of chain
        int32_t _firstLink;     // +0x0c  index into _links, -1 = empty
    };

    // ========================================================================
    // Picking::Scene::Link — 8 bytes. One (cell, entry) membership (tree name).
    //
    // Pinned by the cell iterator and by Intersect, which both read
    // `*(int*)(_links + linkIdx*8)` as the ENTRY index and
    // `*(int*)(_links + linkIdx*8 + 4)` as the next link, and by 0x180072730
    // (the add-to-cells pass), which writes the pair as one qword
    // `CONCAT44(oldHead, entryIndex)` — entry index low, next high.
    // ========================================================================
    struct Link {
        int32_t _entry;  // +0x00
        int32_t _next;   // +0x04  index into _links, -1 = end
    };

    // ========================================================================
    // The descriptor AddMesh hands to the entry allocator 0x18007b210. It is
    // byte-for-byte the tail of an Entry (Entry+0x18 .. Entry+0x47), which is
    // exactly why that allocator can copy it field for field. Pinned by
    // AddMesh's own stack frame and by 0x18007b210's reads of param_2[0],
    // param_2+1, param_2[3], param_2[4], (char*)param_2+0x24 and param_2[5].
    // (Tree name; a stack record, retail names none.)
    // ========================================================================
    struct AddDesc {
        PickingMesh*                   _mesh;        // +0x00  ExternalPtr<PickingMesh>'s pointer
        Collection::Array<::Transform> _transforms;  // +0x08
        void*                          _userData;    // +0x18
        int32_t                        _userValue;   // +0x20
        int32_t                        _userValue2;  // +0x24
        HitCallback                    _callback;    // +0x28
    };

    // ---- the 22 exports, in B37_SLOTS.md section 6 order --------------------
    // ??2 / ??2(placement) / ??3 — NOT PORTED. The exe reaches `??3` only from
    // an exception-unwind funclet (Unwind@14013f41a); no tree caller.
    static void* operator new(std::size_t bytes);              // 0x1800720d0
    static void* operator new(std::size_t bytes, void* place); // 0x180072140
    static void  operator delete(void* p);                     // 0x180072150

    // ?SetTransform@Scene@Picking@@QEAAXIAEBVTransform@Math@@@Z  0x1800721b0
    void SetTransform(uint32_t id, const Math::Transform& transform);

    // NOT PORTED, no caller. RETSLOTs in retail; plain by-value returns here.
    Object::ExternalPtr<PickingMesh> GetMesh(int32_t id) const;        // 0x180072200
    ::Box GetAABB(int32_t id) const;                                    // 0x180072220
    ::Box GetLocalAABB(int32_t id) const;                               // 0x180072250
    Math::Transform GetTransform(int32_t id, int32_t subMesh) const;    // 0x180072280

    // ?GetUserData@Scene@Picking@@QEBAPEAXI@Z  0x1800722d0. One load; retail
    // does NOT range-check `id`, and neither does this (CLAUDE.md).
    void* GetUserData(uint32_t id) const { return _entries._data[id]._userData; }
    // ?GetUserValue@Scene@Picking@@QEBAHI@Z  0x1800722e0 — NOT PORTED, no caller.
    int32_t GetUserValue(uint32_t id) const;

    Scene(const Scene& other);                                          // 0x1800722f0  DEEP
    Scene& operator=(const Scene& other);                               // 0x180072320

    // NOT PORTED, no caller (Clip and Update are dormant: `get_xrefs_to` finds
    // only data references in the DLL and the exe imports neither).
    void Clip(const Math::Transform& transform, const ::Frustum& frustum, int32_t mask,
              Collection::Array<ClipInfo>& clips,
              Collection::Array<Vector3>& positions,
              Collection::Array<Vector3>& normals);                     // 0x180123c50
    void Update(const System::Time& time, bool flag, float a, float b, float c,
                const ::Frustum& frustum, const Math::Transform& transform,
                int32_t mask);                                          // 0x1801256a0

    // ?Intersect@Scene@Picking@@QEBAHAEBVVector3@Math@@0AEBVScalar@4@AEAV34@2@Z
    // 0x1801246c0. DECLARED const AND IT MUTATES THE SCENE (the query stamp):
    // the constness is retail's, and it is a lie — see scene.cpp.
    int32_t Intersect(const Vector3& start, const Vector3& dir, const Math::Scalar& maxDist,
                      Vector3& outPos, Vector3& outNormal) const;

    // ?SetMeshUserData@Scene@Picking@@QEAAXIPEAXHHP6AX0HHAEBVVector3@Math@@11@Z@Z
    // 0x180125240 — the callback is the SIXTH register argument (on the stack).
    void SetMeshUserData(uint32_t id, void* userData, int32_t userValue,
                         int32_t userValue2, HitCallback callback);
    // ?RemoveMesh@Scene@Picking@@QEAAXI@Z  0x180125290
    void RemoveMesh(uint32_t id);
    // ?AddMesh@Scene@Picking@@QEAAIV?$ExternalPtr@VPickingMesh@Picking@@@Object@@...
    // 0x1801252e0 — the ExternalPtr is BY VALUE in a register (B7-5); the
    // callback is the seventh argument.
    uint32_t AddMesh(Object::ExternalPtr<PickingMesh> mesh, const Math::Transform& root,
                     void* userData, int32_t userValue, int32_t userValue2,
                     HitCallback callback);

    // ??1Scene@Picking@@QEAA@XZ  0x180125890 — the IMPLICIT destructor (banner).
    // ??0Scene@Picking@@QEAA@XZ  0x1801258e0 — `Scene(16.0f)` (scene.cpp).
    Scene();                                                            // 0x1801258e0
    // The unexported grid constructor the default one delegates to (body in
    // src/engine/picking/closure_batch2b.cpp): the member stores, then the
    // 10,000 bucket heads filled -1 (0x18007b960).
    explicit Scene(float cellSize);                                     // 0x18007aef0

private:
    // ---- retail `AEBA` (private), exported all the same -------------------
    // NOT PORTED, no caller (Clip's helpers).
    int32_t ClipTriangle(int32_t count, Vector3* in, Vector3* out,
                         const Volume::Plane& plane) const;             // 0x180122a80
    void ClipTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2,
                      const Vector3& n0, const Vector3& n1, const Vector3& n2,
                      const Volume::Plane* planes, int32_t mask,
                      const Math::Transform& transform,
                      Collection::Array<ClipInfo>& clips,
                      Collection::Array<Vector3>& positions,
                      Collection::Array<Vector3>& normals) const;       // 0x180122ef0

public:
    // ========================================================================
    // THE 0x60 BYTES. A uniform 2D spatial hash over components [0] and [1] of
    // a Vector3 — the XY plane, NOT XZ. Two independent witnesses: Intersect
    // builds its query rectangle from `origin[0]`/`origin[1]` and
    // `dir[0]`/`dir[1]`, and 0x180125bc0 projects a Box by taking min[0],
    // min[1], max[0], max[1] (Box+0x00, +0x04, +0x0c, +0x10).
    //
    // NO VTABLE: the ctor and dtor are both `QEAA` (non-virtual), the ctor's
    // first store is a float at +0x00, and no `??_7Scene@Picking@@6B@` exists.
    //
    // Whole-object witness: the grid ctor Scene(float) (0x18007aef0, disassembly
    // 0x18007af06..0x18007af55) writes, in order, +0x00, +0x04, +0x08, +0x10,
    // +0x18, +0x20, +0x28 = -1, +0x30, +0x38, +0x40 = -1, +0x48, +0x50,
    // +0x58 = -1, +0x5c = 0 — and nothing else. It never writes +0x2c or +0x44,
    // so those two dwords are UNINITIALISED in a live Scene.
    //
    // | Off  | Field        | Pinned by                                        |
    // |------|--------------|--------------------------------------------------|
    // | 0x00 | _cellSize    | the grid ctor's float (Scene() passes 16.0f)   |
    // | 0x04 | _invCellSize | 1.0f/_cellSize. EVERY cell index in Intersect,   |
    // |      |              | 0x18007af70 and 0x1800724d0 is                   |
    // |      |              | `(int)(coord * *(float*)(scene+4))` — CVTTSS2SI, |
    // |      |              | i.e. TRUNCATION toward zero, not floor           |
    // | 0x08 | _cellHeads   | Array<int>, SetSize(10000) filled with -1 by     |
    // |      |              | 0x18007b960; the iterator takes its modulus      |
    // |      |              | against this Array's _size at +0x10              |
    // | 0x18 | _cells       | Array<Cell>                                      |
    // | 0x28 | _freeCell    | -1                                               |
    // | 0x30 | _links       | Array<Link>; 0x180072730 grows it                |
    // | 0x40 | _freeLink    | -1; 0x180072730 pops and pushes it               |
    // | 0x48 | _entries     | Array<Entry>; 0x18007b210 grows it               |
    // | 0x58 | _freeEntry   | -1; 0x18007b210 pops it                          |
    // | 0x5c | _queryStamp  | `INC dword ptr [RCX+0x5c]` at 0x1801247bf, the   |
    // |      |              | first thing Intersect does after its arguments   |
    // ========================================================================
    float                      _cellSize;      // +0x00
    float                      _invCellSize;   // +0x04
    Collection::Array<int32_t> _cellHeads;     // +0x08
    Collection::Array<Cell>    _cells;         // +0x18
    int32_t                    _freeCell;      // +0x28
    int32_t                    _pad2c;         // +0x2c  never written by the ctor
    Collection::Array<Link>    _links;         // +0x30
    int32_t                    _freeLink;      // +0x40
    int32_t                    _pad44;         // +0x44  never written by the ctor
    Collection::Array<Entry>   _entries;       // +0x48
    int32_t                    _freeEntry;     // +0x58
    // `mutable`: the const Intersect (0x1801246c0) bumps it — retail's const
    // is a lie there, and this names that instead of casting it away.
    mutable int32_t            _queryStamp;    // +0x5c
};

static_assert(sizeof(Scene::Entry) == 0x48, "Picking::Scene::Entry must be 0x48 bytes");
static_assert(offsetof(Scene::Entry, _queryStamp) == 0x0, "Entry::_queryStamp");
static_assert(offsetof(Scene::Entry, _cellY) == 0xC, "Entry::_cellY");
static_assert(offsetof(Scene::Entry, _pad14) == 0x14, "Entry::_pad14");
static_assert(offsetof(Scene::Entry, _nextFree)   == 0x04, "Entry::_nextFree @ +0x04");
static_assert(offsetof(Scene::Entry, _cellX)      == 0x08, "Entry::_cellX @ +0x08");
static_assert(offsetof(Scene::Entry, _cellW)      == 0x10, "Entry::_cellW @ +0x10");
static_assert(offsetof(Scene::Entry, _cellH)      == 0x12, "Entry::_cellH @ +0x12");
static_assert(offsetof(Scene::Entry, _mesh)       == 0x18, "Entry::_mesh @ +0x18");
static_assert(offsetof(Scene::Entry, _transforms) == 0x20, "Entry::_transforms @ +0x20");
static_assert(offsetof(Scene::Entry, _userData)   == 0x30, "Entry::_userData @ +0x30");
static_assert(offsetof(Scene::Entry, _userValue)  == 0x38, "Entry::_userValue @ +0x38");
static_assert(offsetof(Scene::Entry, _userValue2) == 0x3c, "Entry::_userValue2 @ +0x3c");
static_assert(offsetof(Scene::Entry, _callback)   == 0x40, "Entry::_callback @ +0x40");
static_assert(sizeof(Scene::Cell) == 0x10, "Picking::Scene::Cell must be 16 bytes");
static_assert(offsetof(Scene::Cell, _x) == 0x0, "Cell::_x");
static_assert(offsetof(Scene::Cell, _y) == 0x4, "Cell::_y");
static_assert(offsetof(Scene::Cell, _nextInBucket) == 0x8, "Cell::_nextInBucket");
static_assert(offsetof(Scene::Cell, _firstLink) == 0xC, "Cell::_firstLink");
static_assert(sizeof(Scene::Link) == 8, "Picking::Scene::Link must be 8 bytes");
static_assert(offsetof(Scene::Link, _entry) == 0x0, "Link::_entry");
static_assert(offsetof(Scene::Link, _next) == 0x4, "Link::_next");
static_assert(sizeof(Scene::AddDesc) == 0x30, "Picking AddMesh descriptor must be 0x30 bytes");
static_assert(offsetof(Scene::AddDesc, _transforms) == 0x8, "AddDesc::_transforms");
static_assert(offsetof(Scene::AddDesc, _userValue) == 0x20, "AddDesc::_userValue");
static_assert(offsetof(Scene::AddDesc, _userValue2) == 0x24, "AddDesc::_userValue2");
static_assert(offsetof(Scene::AddDesc, _userData) == 0x18, "AddDesc::_userData @ +0x18");
static_assert(offsetof(Scene::AddDesc, _callback) == 0x28, "AddDesc::_callback @ +0x28");
static_assert(offsetof(Scene::AddDesc, _mesh) + 0x18 == offsetof(Scene::Entry, _mesh),
              "AddDesc is Entry's tail from +0x18");

static_assert(sizeof(Scene) == 0x60,
              "Picking::Scene is 0x60 (96) bytes (exe MemAlloc(0x60,8) at 0x140097093)");
static_assert(offsetof(Scene, _cellSize) == 0x0, "Scene::_cellSize");
static_assert(offsetof(Scene, _invCellSize) == 0x4, "Scene::_invCellSize");
static_assert(offsetof(Scene, _pad2c) == 0x2C, "Scene::_pad2c");
static_assert(offsetof(Scene, _pad44) == 0x44, "Scene::_pad44");
static_assert(alignof(Scene) == 8, "Picking::Scene aligns to 8 (the Array data pointers)");
static_assert(offsetof(Scene, _cellHeads)  == 0x08, "Scene::_cellHeads @ +0x08");
static_assert(offsetof(Scene, _cells)      == 0x18, "Scene::_cells @ +0x18");
static_assert(offsetof(Scene, _freeCell)   == 0x28, "Scene::_freeCell @ +0x28");
static_assert(offsetof(Scene, _links)      == 0x30, "Scene::_links @ +0x30");
static_assert(offsetof(Scene, _freeLink)   == 0x40, "Scene::_freeLink @ +0x40");
static_assert(offsetof(Scene, _entries)    == 0x48, "Scene::_entries @ +0x48");
static_assert(offsetof(Scene, _freeEntry)  == 0x58, "Scene::_freeEntry @ +0x58");
static_assert(offsetof(Scene, _queryStamp) == 0x5c, "Scene::_queryStamp @ +0x5c");

}  // namespace Picking

// ==== B3R G-Game (R5): Runtime bodies code/Game calls by name ====
extern "C" Picking::Scene* RtPicking_Scene_Ctor(void* selfv);  // 0x1801258e0
// ==== end B3R G-Game ====
