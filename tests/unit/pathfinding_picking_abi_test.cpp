// ============================================================================
// tests/unit/pathfinding_picking_abi_test.cpp — ENDGAME B.3 step 7, block B6
//
//   Pathfinding::PointList  : Object::External  0x50  9 slots, Compile OVERRIDDEN
//   Pathfinding::IPathInfo  : (root)            0x08  2 pure slots, no dtor slot
//   Pathfinding::Scene      : non-polymorphic   0x90  declarations only
//   Picking::PickingMesh    : Object::External  0x48  9 slots, Compile OVERRIDDEN
//   Picking::Scene          : non-polymorphic   0x60  concrete, held by value (B5-4)
//
// Built to the contract of ui_resources_abi_test.cpp (step 6, block D); only
// what differs is spelled out.
//
// PART A — LAYOUT: sizeof / alignof / every offset the headers claim, the
//   parent edge `_ParentTypeName` proves, abstract, no public default ctor.
// PART B — SLOT ORDER: a probe subclass of the REAL class; each own slot is
//   raw-called by byte offset and the marker that runs names the member.
//   Slot 4 `Compile` is the class's OWN body (B7-1.7), a different pointer from
//   Object::External's, answering retail's 1 in EAX (no hidden slot).
// PART C — THE QUALIFIED DOOR CALL: a minted-table slot pointing at a door that
//   delegates `->Ns::C::M()` enters the door once and lands in the member.
//
// The classes' out-of-line members are defined HERE as markers
// (src/engine/pathfinding and src/engine/picking do not link into a host test).
//
// ENDGAME B.3 step 9 PILOT (B39_RULES D1/D2): Picking::PickingMesh is CONCRETE
// now -- PART A says so, and the b39_* tests construct the REAL class (its
// ctor is inline in PickingMesh.h; only ~PickingMesh is a marker here) and pin
// slot order by PMF, the ctor's stores, and both arms of retail slot 0 (free
// on `flags & 1`; the downward array arm on `flags & 2` is retired with the
// B3X W1 cutover: slot 0 is `virtual ~T()`, D1 + D0, and `delete[]` replaces
// the array arm).
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "pathfinding/PointList.h"
#include "pathfinding/IPathInfo.h"
#include "pathfinding/Scene.h"
#include "picking/PickingMesh.h"
#include "picking/Scene.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace {

const void* g_lastSelf = nullptr;
int         g_lastMark = -1;
void Note(const void* self, int mark) { g_lastSelf = self; g_lastMark = mark; }
void Reset() { g_lastSelf = nullptr; g_lastMark = -1; }

// `byteOffset` is RETAIL's; T (the object's class) decides the compiler's.
template <typename Fn, class T>
Fn SlotAt(void* obj, int byteOffset) {
    const uint8_t* vt = *reinterpret_cast<const uint8_t* const*>(obj);
    Fn f = nullptr;
    const void* raw = *reinterpret_cast<const void* const*>(vt + RetailByteOffset<T>(byteOffset));
    std::memcpy(&f, &raw, sizeof f);
    return f;
}
const void* const* VTableOf(void* obj) {
    return *reinterpret_cast<const void* const* const*>(obj);
}

constexpr int kDtorSlot   = 0x00;
constexpr int kTypeName       = 0x08;
constexpr int kType           = 0x10;
constexpr int kGetFileVersion = 0x18;
constexpr int kCompile        = 0x20;
constexpr int kSerialize      = 0x28;
constexpr int kReconstruct    = 0x30;
constexpr int kDestruct       = 0x38;
constexpr int kConstruct      = 0x40;   // introduced per leaf — B7-1.6

// A bare Object::External leaf: whatever it carries at +0x20 IS
// `?Compile@External@Object@@`.
struct BareExternalProbe final : Object::External {
    int32_t GetFileVersion() const override { return 0; }
    bool Serialize(IO::Stream&) override { return true; }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
};

}  // namespace

// ===========================================================================
// Pathfinding::PointList's out-of-line members, AS MARKERS.
// ===========================================================================
namespace Pathfinding {
int32_t PointList::GetFileVersion() const { Note(this, 3); return 5; }
Object::External::CompileResult PointList::Compile(const System::Filename&) { Note(this, 4); return 1; }
bool PointList::Serialize(IO::Stream&) { Note(this, 5); return true; }
Object::External* PointList::Reconstruct() { Note(this, 6); return this; }
void PointList::Destruct() { Note(this, 7); }
Object::External* PointList::Construct() { Note(this, 8); return this; }
}  // namespace Pathfinding

namespace {
// ENDGAME B.3.9 ENG: the destructor marker RECORDS each call, so `delete` and
// `delete[]` (B3X W1) can be seen running ~PointList once per element and in
// retail's DESCENDING order.
int         g_plDtorCount = 0;
const void* g_plDtorOrder[8] = {};
}  // namespace

namespace Pathfinding {
PointList::~PointList() {
    if (g_plDtorCount < 8) g_plDtorOrder[g_plDtorCount] = this;
    ++g_plDtorCount;
}
}  // namespace Pathfinding

namespace {

// B3X W1: records in its destructor (retail slot 0) and frees through a
// recording class-scope operator delete (retail flags 1's MemFree).
struct PointListProbe final : Pathfinding::PointList {
    ~PointListProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

}  // namespace

// ---------------------------------------------------------------------------
// PART A — Pathfinding::PointList layout
// ---------------------------------------------------------------------------
UNIT_TEST(pathfinding_pointlist_layout) {
    CHECK_EQ((int)alignof(Pathfinding::PointList), 8);
    CHECK_EQ((int)offsetof(Pathfinding::PointList, _meshName), (int)sizeof(Object::External));
    // interactdescription.cpp walks `_points` at +0x30 (data) / +0x38 (size).
    CHECK_EQ((int)(offsetof(Pathfinding::PointList, _points) +
                   offsetof(Collection::Array<Pathfinding::PointList::Point>, _size)), 0x38);
    CHECK((std::is_base_of<Object::External, Pathfinding::PointList>::value));
    // ENDGAME B.3.9 ENG (B39_RULES D1): CONCRETE, retail's public default
    // ctor; B3X W1: the destructor is public and virtual (retail slot 0 = D1 + D0).
    CHECK(!std::is_abstract<Pathfinding::PointList>::value);
    CHECK(std::is_destructible<Pathfinding::PointList>::value);
    CHECK(std::has_virtual_destructor<Pathfinding::PointList>::value);
}

// The two reference getters return the members retail's LEAs name
// (0x180071190 +0x30, 0x1800711a0 +0x40): no hidden return slot.
UNIT_TEST(pathfinding_pointlist_getters_read_retails_offsets) {
    PointListProbe probe;
    const Pathfinding::PointList& pl = probe;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&pl);
    CHECK(reinterpret_cast<const uint8_t*>(&pl.Points())     == base + 0x30);
    CHECK(reinterpret_cast<const uint8_t*>(&pl.PathPoints()) == base + 0x40);
}

// ---------------------------------------------------------------------------
// ENDGAME B.3.9 ENG (B39_RULES D1/D2, rule B9-16) -- the concrete PointList.
// ---------------------------------------------------------------------------
namespace {
}  // namespace

UNIT_TEST(b39_pointlist_slot_order_by_pmf) {
    using PL = Pathfinding::PointList;
    CHECK_DTOR_SLOT(PL);   // 0x1800713b0
    CHECK_SLOT(PL, TypeName,       0x08/8);   // 0x180070e40
    CHECK_SLOT(PL, Type,           0x10/8);   // 0x180070e20
    CHECK_SLOT(PL, GetFileVersion, 0x18/8);   // 0x18010f880
    CHECK_SLOT(PL, Compile,        0x20/8);   // 0x18010f6d0
    CHECK_SLOT(PL, Serialize,      0x28/8);   // 0x18010f6e0
    CHECK_SLOT(PL, Reconstruct,    0x30/8);   // 0x180071060
    CHECK_SLOT(PL, Destruct,       0x38/8);   // 0x180071150
    CHECK_SLOT(PL, Construct,      0x40/8);   // 0x1800710e0
}

// The real constructor makes retail's factory stores (0x180070f93..fd4): six
// zero qwords +0x20..+0x48 after External's, and the compiler's table -- the
// same one for every instance, not the probe's.
UNIT_TEST(b39_pointlist_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x50];
    std::memset(buf, 0xCD, sizeof buf);
    Pathfinding::PointList* p = ::new (buf) Pathfinding::PointList();
    CHECK(static_cast<void*>(p) == static_cast<void*>(buf));
    for (int off = 0x08; off < 0x50; off += 8) {
        uint64_t q = 1;
        std::memcpy(&q, buf + off, sizeof q);
        CHECK_EQ(q, (uint64_t)0);
    }
    alignas(8) uint8_t buf2[0x50];
    Pathfinding::PointList* p2 = ::new (buf2) Pathfinding::PointList();
    PointListProbe probe;
    CHECK(VTableOf(p) != nullptr);
    CHECK(VTableOf(p) == VTableOf(p2));
    CHECK(VTableOf(p) != VTableOf(&probe));

    g_plDtorCount = 0;
    p->~PointList();                        // retail flags 0: destroy, no free
    p2->~PointList();
    CHECK_EQ(g_plDtorCount, 2);
}

// Retail slot 0 frees on `flags & 1`; its ARRAY arm read the int32 count at
// `this - 8`, destroyed DOWNWARD with stride 0x50, freed and returned
// `this - 8` -- both arms as the retired RT_DELDTOR row 0x1800713b0 ran them.
// B3X W1: flags 1 is `delete p` (PointList's D0: ~PointList once, then its
// class-scope MemFree operator delete — InternalMemFree == free here); the free
// itself is proven on a Recording<PointList>. The array arm has no Itanium
// counterpart; `delete[]` of a `new PointList[3]` replaces it.
UNIT_TEST(b39_pointlist_deletingdtor_frees_both_arms) {
    static_assert(dtor_check::HasClassDelete<Pathfinding::PointList>::value, "class-scope operator delete");
    void* m = Object::detail::InternalMemAlloc(sizeof(Pathfinding::PointList), 8);
    CHECK(m != nullptr);
    Pathfinding::PointList* p = ::new (m) Pathfinding::PointList();
    g_plDtorCount = 0;
    delete p;
    CHECK_EQ(g_plDtorCount, 1);
    CHECK(g_plDtorOrder[0] == m);

    Pathfinding::PointList* r = dtor_check::NewOnMalloc<dtor_check::Recording<Pathfinding::PointList>>();
    g_plDtorCount = 0; dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_plDtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));

    CHECK_ARRAY_DELETE(Pathfinding::PointList, g_plDtorCount, g_plDtorOrder);
}

// ---------------------------------------------------------------------------
// Pathfinding::IPathInfo and Pathfinding::Scene — dead in retail, declarations
// only; their layouts are still pinned.
// ---------------------------------------------------------------------------
namespace {
struct PathInfoProbe final : Pathfinding::IPathInfo {
    void Slot0() override { Note(this, 100); }
    void Slot1() override { Note(this, 101); }
};
}  // namespace

UNIT_TEST(pathfinding_ipathinfo_is_two_pure_slots_and_no_dtor_slot) {
    CHECK(std::is_abstract<Pathfinding::IPathInfo>::value);
    CHECK(!std::has_virtual_destructor<Pathfinding::IPathInfo>::value);
    PathInfoProbe probe;
    void* raw = static_cast<void*>(&probe);
    using VoidFn = void (*)(void*);
    Reset(); (SlotAt<VoidFn, PathInfoProbe>(raw, 0x00)(raw)); CHECK_EQ(g_lastMark, 100);
    Reset(); (SlotAt<VoidFn, PathInfoProbe>(raw, 0x08)(raw)); CHECK_EQ(g_lastMark, 101);
}

UNIT_TEST(pathfinding_scene_layout) {
    CHECK_EQ((int)alignof(Pathfinding::Scene), 8);
    CHECK(!std::is_polymorphic<Pathfinding::Scene>::value);
    // B7-8: the retail copy constructor is DEEP — never trivially copyable.
    CHECK(!std::is_trivially_copyable<Pathfinding::Scene>::value);
}

// ===========================================================================
// Picking::PickingMesh's out-of-line members, AS MARKERS.
// ===========================================================================
namespace Picking {
int32_t PickingMesh::GetFileVersion() const { Note(this, 3); return 9; }
Object::External::CompileResult PickingMesh::Compile(const System::Filename&) { Note(this, 4); return 1; }
bool PickingMesh::Serialize(IO::Stream&) { Note(this, 5); return true; }
Object::External* PickingMesh::Reconstruct() { Note(this, 6); return this; }
void PickingMesh::Destruct() { Note(this, 7); }
Object::External* PickingMesh::Construct() { Note(this, 8); return this; }
}  // namespace Picking

namespace {
// ENDGAME B.3.9 pilot: the destructor marker RECORDS each call, so `delete`
// and `delete[]` (B3X W1) can be seen running ~PickingMesh once per element and
// in retail's DESCENDING order.
int         g_meshDtorCount = 0;
const void* g_meshDtorOrder[8] = {};
}  // namespace

namespace Picking {
PickingMesh::~PickingMesh() {
    if (g_meshDtorCount < 8) g_meshDtorOrder[g_meshDtorCount] = this;
    ++g_meshDtorCount;
}
}  // namespace Picking

namespace {

struct PickingMeshProbe final : Picking::PickingMesh {
    ~PickingMeshProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

// B7-5 pin: `Object::ExternalPtr<T>` BY VALUE travels as the bare pointer in
// the next integer register — what `Scene::AddMesh`'s door hands the member,
// and what the exe's `CALL [AddMesh]` puts in RDX.
void* g_seenMesh = nullptr;
uint32_t TakesMeshByValue(void* self, Object::ExternalPtr<Picking::PickingMesh> mesh) {
    g_seenMesh = mesh.Get();
    return self == nullptr ? 0u : 7u;
}

}  // namespace

// ---------------------------------------------------------------------------
// PART A — Picking::PickingMesh layout
// ---------------------------------------------------------------------------
UNIT_TEST(picking_pickingmesh_layout) {
    CHECK_EQ((int)alignof(Picking::PickingMesh), 8);
    CHECK_EQ((int)offsetof(Picking::PickingMesh, _meshName), (int)sizeof(Object::External));
    CHECK((std::is_base_of<Object::External, Picking::PickingMesh>::value));
    // ENDGAME B.3.9 pilot (B39_RULES D1): CONCRETE, with retail's public
    // default constructor.
    CHECK(!std::is_abstract<Picking::PickingMesh>::value);
    // B3X W1, the cutover: the destructor is PUBLIC and virtual (retail slot 0
    // = D1 + D0), so the class is default-constructible and destructible.
    CHECK(std::is_destructible<Picking::PickingMesh>::value);
    CHECK(std::has_virtual_destructor<Picking::PickingMesh>::value);
    CHECK(std::is_default_constructible<Picking::PickingMesh>::value);
}

// ---------------------------------------------------------------------------
// ENDGAME B.3.9 PILOT (B39_RULES D1/D2, rule B9-14) — the concrete class.
// ---------------------------------------------------------------------------
// Slot order read off the REAL header's pointers-to-virtual-member.
UNIT_TEST(b39_pickingmesh_slot_order_by_pmf) {
    using PM = Picking::PickingMesh;
    CHECK_DTOR_SLOT(PM);   // 0x180072050
    CHECK_SLOT(PM, TypeName,       0x08/8);   // 0x180071ab0
    CHECK_SLOT(PM, Type,           0x10/8);   // 0x180071a90
    CHECK_SLOT(PM, GetFileVersion, 0x18/8);   // 0x180122940
    CHECK_SLOT(PM, Compile,        0x20/8);   // 0x180122950
    CHECK_SLOT(PM, Serialize,      0x28/8);   // 0x180122960
    CHECK_SLOT(PM, Reconstruct,    0x30/8);   // 0x180071cd0
    CHECK_SLOT(PM, Destruct,       0x38/8);   // 0x180071db0
    CHECK_SLOT(PM, Construct,      0x40/8);   // 0x180071d40
}

// The real constructor makes retail's factory stores (0x180071c03..c3f): every
// field zero, +0x30 ONE byte (the 7 pad bytes after it untouched), and the
// compiler's table -- the same one for every instance, not the probe's.
UNIT_TEST(b39_pickingmesh_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x48];
    std::memset(buf, 0xCD, sizeof buf);
    Picking::PickingMesh* m = ::new (buf) Picking::PickingMesh();
    CHECK(static_cast<void*>(m) == static_cast<void*>(buf));
    CHECK(m->_path._data == nullptr);
    CHECK(m->_refs._data == nullptr);
    CHECK_EQ(m->_refs._size, 0);
    CHECK_EQ(m->_refs._capacity, 0);
    CHECK(m->_meshName._data == nullptr);
    CHECK(m->_subObject._data == nullptr);
    CHECK(!m->_allowChildren);
    for (int i = 0; i < 7; ++i) CHECK_EQ((int)buf[0x31 + i], 0xCD);
    CHECK(m->_nodes._data == nullptr);
    CHECK_EQ(m->_nodes._size, 0);
    CHECK_EQ(m->_nodes._capacity, 0);

    alignas(8) uint8_t buf2[0x48];
    Picking::PickingMesh* m2 = ::new (buf2) Picking::PickingMesh();
    PickingMeshProbe probe;
    CHECK(VTableOf(m) != nullptr);
    CHECK(VTableOf(m) == VTableOf(m2));
    CHECK(VTableOf(m) != VTableOf(&probe));

    g_meshDtorCount = 0;
    m->~PickingMesh();                     // retail flags 0: destroy, no free
    m2->~PickingMesh();
    CHECK_EQ(g_meshDtorCount, 2);
}

// Retail slot 0 FREES on `flags & 1`, and its ARRAY arm read the int32 count
// at `this - 8`, destroyed DOWNWARD, freed and returned `this - 8` -- both
// arms as the retired RT_DELDTOR row 0x180072050 ran them. B3X W1: flags 1 is
// `delete m` (PickingMesh's D0, its class-scope MemFree operator delete;
// InternalMemFree == free here), the free proven on a Recording<PickingMesh>;
// `delete[]` of a `new PickingMesh[3]` replaces the array arm.
UNIT_TEST(b39_pickingmesh_deletingdtor_frees_both_arms) {
    static_assert(dtor_check::HasClassDelete<Picking::PickingMesh>::value, "class-scope operator delete");
    void* p = Object::detail::InternalMemAlloc(sizeof(Picking::PickingMesh), 8);
    CHECK(p != nullptr);
    Picking::PickingMesh* m = ::new (p) Picking::PickingMesh();
    g_meshDtorCount = 0;
    delete m;
    CHECK_EQ(g_meshDtorCount, 1);
    CHECK(g_meshDtorOrder[0] == p);

    Picking::PickingMesh* r = dtor_check::NewOnMalloc<dtor_check::Recording<Picking::PickingMesh>>();
    g_meshDtorCount = 0; dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_meshDtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));

    CHECK_ARRAY_DELETE(Picking::PickingMesh, g_meshDtorCount, g_meshDtorOrder);
}

// The accessor bodies compute retail's addresses: `_nodes + i*0x80 + field`
// (Vertices 0x180071e10, Normals 0x180071e30, Triangles 0x180071e50,
// TriangleCount 0x180071e70 reads +0x68, GetNode 0x180071e90,
// GetSubMeshCount 0x180071dc0 reads +0x40).
UNIT_TEST(picking_pickingmesh_accessors_read_retails_offsets) {
    PickingMeshProbe probe;
    alignas(8) uint8_t nodes[2 * 0x80] = {};
    *reinterpret_cast<int32_t*>(nodes + 0x80 + 0x68) = 11;   // node 1's triangle count
    probe._nodes._data = reinterpret_cast<Picking::PickingMesh::SubMesh*>(nodes);
    probe._nodes._size = 2;
    const Picking::PickingMesh& pm = probe;
    CHECK_EQ(pm.GetSubMeshCount(), 2);
    CHECK(reinterpret_cast<const uint8_t*>(&pm.Vertices(1))  == nodes + 0x80 + 0x40);
    CHECK(reinterpret_cast<const uint8_t*>(&pm.Normals(1))   == nodes + 0x80 + 0x50);
    CHECK(reinterpret_cast<const uint8_t*>(&pm.Triangles(1)) == nodes + 0x80 + 0x60);
    CHECK(reinterpret_cast<const uint8_t*>(&pm.GetNode(1))   == nodes + 0x80);
    CHECK_EQ(pm.TriangleCount(1), 11);
    probe._nodes._data = nullptr;          // the stack buffer is not the Array's to free
    probe._nodes._size = 0;
}

// ---------------------------------------------------------------------------
// Picking::Scene — concrete, non-polymorphic, held by value (B5-4, B7-8)
// ---------------------------------------------------------------------------
UNIT_TEST(picking_scene_layout_and_value_shape) {
    CHECK_EQ((int)alignof(Picking::Scene), 8);
    CHECK(!std::is_polymorphic<Picking::Scene>::value);
    CHECK(!std::is_abstract<Picking::Scene>::value);
    // The default ctor is retail's 0x1801258e0 (wave Y); the dtor is the
    // implicit, public one (retail's body).
    CHECK(std::is_default_constructible<Picking::Scene>::value);
    CHECK(std::is_destructible<Picking::Scene>::value);
    // B7-8: retail's copy ctor is deep; the class is never trivially copyable.
    CHECK(!std::is_trivially_copyable<Picking::Scene>::value);
}

// GetUserData (0x1800722d0) is one load from entries[id]+0x30.
UNIT_TEST(picking_scene_getuserdata_reads_entry_plus_0x30) {
    // Raw storage, as the exe holders are: the class has no C++ default ctor.
    alignas(8) uint8_t storage[sizeof(Picking::Scene)] = {};
    Picking::Scene& scene = *reinterpret_cast<Picking::Scene*>(storage);
    alignas(8) uint8_t entries[3 * 0x48] = {};
    int marker = 0;
    *reinterpret_cast<void**>(entries + 2 * 0x48 + 0x30) = &marker;
    scene._entries._data = reinterpret_cast<Picking::Scene::Entry*>(entries);
    scene._entries._size = 3;
    CHECK(scene.GetUserData(2) == static_cast<void*>(&marker));
    // Never destroyed: the storage is plain bytes, like an exe holder's.
}

// B7-5: ExternalPtr<PickingMesh> by value is one register — raw-call a
// by-value receiver through a `(void*, void*)` pointer, retail's shape.
UNIT_TEST(picking_addmesh_externalptr_by_value_is_a_bare_register) {
    CHECK((std::is_trivially_copyable<Object::ExternalPtr<Picking::PickingMesh> >::value));
    using RawFn = uint32_t (*)(void*, void*);
    uint32_t (*typed)(void*, Object::ExternalPtr<Picking::PickingMesh>) = &TakesMeshByValue;
    RawFn raw = nullptr;
    std::memcpy(&raw, &typed, sizeof raw);   // the raw retail-order call, no cast warning
    int self = 0, mesh = 0;
    g_seenMesh = nullptr;
    CHECK_EQ(raw(&self, &mesh), 7u);
    CHECK(g_seenMesh == static_cast<void*>(&mesh));
}
