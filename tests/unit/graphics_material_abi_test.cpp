// ============================================================================
// tests/unit/graphics_material_abi_test.cpp — ENDGAME B.3 step 5, block G
//   the four `Graphics` material/mesh resource classes as real C++ classes
//
//   Graphics::Material      : Object::External,         abstract, 0x38,  9 slots
//   Graphics::MaterialInstance : Object::External,       abstract, 0x60,  9 slots
//   Graphics::GraphicsMesh  : Object::CompiledExternal,  abstract, 0x98,  9 slots
//   Graphics::EdgeMesh      : Object::External,          abstract, 0x180, 9 slots
//
// The sibling of `graphics_animation_abi_test.cpp` (block H1) and
// `graphics_geometry_abi_test.cpp` (block E), and it is built to the same
// contract, so only what is DIFFERENT here is spelled out:
//
// PART A — LAYOUT. Every size, alignment and field offset the four headers
//   claim, measured as well as `static_assert`ed, plus the cross-asserts against
//   the PODs that still describe the same bytes (`RtGraphics::MaterialInstance`,
//   `RtGraphics::EdgeMesh`, `RtGraphics::GraphicsMeshView`, `RtGraphics::MeshNode`).
//   It also pins the one structural fact the conversion rests on: each class's
//   own fields begin at exactly `sizeof(base)` — no tail-padding reuse, no
//   second vptr — and that `GraphicsMesh`'s base is `Object::CompiledExternal`
//   while the other three sit directly on `Object::External`. That split is the
//   exact mistake B35_RULES section 2.1 warns about for `Video::VideoProgram`,
//   and here it is asserted in both directions for all four.
//
// PART B — SLOT ORDER. Probe subclasses declared in retail slot order, whose
//   GCC vtable is read back by BYTE OFFSET and compared against the member each
//   index is supposed to name. This is the claim that licenses the conversion to
//   coexist with the MINTED tables in `standalone/rt_data_mint_tables.inc`: an
//   object wearing a hand-built `void*[]` at retail byte offsets and one wearing
//   GCC's table are interchangeable slot for slot, in BOTH directions.
//
//   *** SLOT 4 IS THE INTERESTING ONE, AND IT IS SHARED, NOT OVERRIDDEN. ***
//   All four classes INHERIT `?Compile@External@Object@@` (0x1800066e0) at slot
//   4 — B35_SLOTS.md marks it `inherited-from` on every one of the four — except
//   `EdgeMesh`, which has its OWN `?Compile@EdgeMesh@Graphics@@` (0x1800a21c0,
//   `MOV EAX,1 ; RET`). The probes check that asymmetry directly: three tables
//   must carry the SAME pointer at +0x20 as a bare `Object::External` subclass
//   does, and EdgeMesh's must carry a different one. Getting that backwards is
//   silent — both bodies return 1 — which is exactly why it is a test.
//
// PART C — RULE B5-16, `Construct` AT SLOT 8. `Object::External` and
//   `Object::CompiledExternal` are EIGHT-entry tables; `Construct` is introduced
//   per branch, by the Video/Graphics leaf, at slot 8. Each probe's table is
//   therefore checked to have NINE distinct entries with `Construct` at +0x40,
//   and `Object::External`'s own table to have no such slot.
//
// PART D — THE QUALIFIED CALL (B34_BRIEFS A.1), on the same probe shape the
//   other two block tests use: a door delegates with
//   `reinterpret_cast<Ns::C*>(self)->Ns::C::Method(args)` and the `Ns::C::` is
//   load-bearing, because a live object in this build wears a MINTED table whose
//   slots resolve back to those very doors.
//
// WHAT IS *NOT* PROVEN HERE. Nothing about MSVC and nothing about the ported
// bodies. `src/engine/graphics/material.cpp`, `graphicsmesh_vtable.cpp`,
// `edgemesh_vtable.cpp` and `mesh.cpp` all stand on `RegisterRuntimeExport` /
// `RT_DATA_SLOT` / `ResolveAddr` and cannot link into a host test, so the
// classes' out-of-line key-function bodies are defined AS MARKERS at the bottom
// of this file — which is also what makes GCC emit their vtables in this
// translation unit so the probes can raw-call every slot. The serializers and
// the reflection records are verified by the real run.
//
// The two retail prologues quoted in `Material.h`'s banner — the finding that
// `ExternalPtr<T>` getters really ARE hidden-retslot, against block E's doubt —
// are a fact about the RETAIL DLL and cannot be tested here; what IS tested is
// that `Object::ExternalPtr<T>` is the eight trivially-copyable bytes the
// layouts depend on.
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "graphics/Material.h"
#include "graphics/GraphicsMesh.h"
// `Mesh.h` is included HERE and not from `GraphicsMesh.h`, so this file can
// cross-assert the real class against lane E's `RtGraphics::GraphicsMeshView` /
// `RtGraphics::MeshNode` overlays. `GraphicsMesh.h`'s include banner records why
// it cannot do that itself: `Mesh.h` and `AnimationState.h` each define
// `RtGraphics::InterpolatedTransform` with different members, so no translation
// unit may include both, and `GraphicsMesh.h` has to reach
// `src/engine/graphics/animationstate.cpp`.
#include "graphics/Mesh.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace {

// ---------------------------------------------------------------------------
// Observation channel, same shape as the two sibling block tests.
// ---------------------------------------------------------------------------
const void* g_lastSelf = nullptr;
int         g_lastMark = -1;

void Note(const void* self, int mark) { g_lastSelf = self; g_lastMark = mark; }
void Reset() { g_lastSelf = nullptr; g_lastMark = -1; }

// The one instruction every hand-inlined `(*(void***)p)[N]` caller compiles to,
// spelled by BYTE OFFSET because that is how the tree spells it. The offset is
// RETAIL's; T (the object's class) decides the compiler's.
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

// The nine retail byte offsets, shared by every Object::External leaf.
constexpr int kDtorSlot
   = 0x00;
constexpr int kTypeName       = 0x08;
constexpr int kType           = 0x10;
constexpr int kGetFileVersion = 0x18;
constexpr int kCompile        = 0x20;
constexpr int kSerialize      = 0x28;
constexpr int kReconstruct    = 0x30;
constexpr int kDestruct       = 0x38;
constexpr int kConstruct      = 0x40;   // introduced per branch — RULE B5-16

// ENDGAME B.3.9 GFX: the destructor markers RECORD each call, so `delete`, the
// complete destructor and `delete[]` can be seen running them once per
// element, downward (B3X W1).
int         g_gfxDtorCount = 0;
const void* g_gfxDtorOrder[4] = {};
void NoteDtor(const void* self) {
    if (g_gfxDtorCount < 4) g_gfxDtorOrder[g_gfxDtorCount] = self;
    ++g_gfxDtorCount;
}

// ---------------------------------------------------------------------------
// A bare `Object::External` leaf, existing only so PART B has something to read
// slot 4 out of: whatever pointer THIS table carries at +0x20 is
// `?Compile@External@Object@@` (0x1800066e0), and the three inheriting classes
// must carry the very same one.
// ---------------------------------------------------------------------------
struct BareExternalProbe final : Object::External {
    int32_t GetFileVersion() const override { return 0; }
    bool Serialize(IO::Stream&) override { return true; }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
};

// B3X W1: retail slot 0 is the destructor pair. Each probe records in its
// destructor and frees through a recording class-scope operator delete.
struct MaterialProbe final : Graphics::Material {
    ~MaterialProbe() override { Note(this, kDtorSlot / 8); }
    DTOR_CHECK_RECORDING_DELETE
};

struct MaterialInstanceProbe final : Graphics::MaterialInstance {
    ~MaterialInstanceProbe() override { Note(this, kDtorSlot / 8); }
    DTOR_CHECK_RECORDING_DELETE
};

struct GraphicsMeshProbe final : Graphics::GraphicsMesh {
    ~GraphicsMeshProbe() override { Note(this, kDtorSlot / 8); }
    DTOR_CHECK_RECORDING_DELETE
};

struct EdgeMeshProbe final : Graphics::EdgeMesh {
    ~EdgeMeshProbe() override { Note(this, kDtorSlot / 8); }
    DTOR_CHECK_RECORDING_DELETE
};

// Retail slot 0 read raw: compiler [0] D1 runs ~P once and frees nothing
// (retail flags 0); [1] D0 runs it and hands the block to the class-scope
// operator delete (flags 1). D1 runs on a placement-new'd probe that is never
// destroyed again (a stack probe would be destroyed twice).
template <class P>
void CheckDtorPair() {
    alignas(P) unsigned char buf[sizeof(P)];
    P* p = ::new (static_cast<void*>(buf)) P();
    CHECK(VTableOf(p)[0] != VTableOf(p)[1]);
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD1(p);
    CHECK_EQ(g_lastMark, kDtorSlot / 8); CHECK(g_lastSelf == p);
    CHECK_EQ(dtor_check::g_frees, 0);
    P* h = dtor_check::NewOnMalloc<P>();
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD0(h);
    CHECK_EQ(g_lastMark, kDtorSlot / 8); CHECK(g_lastSelf == h);
    CHECK_EQ(dtor_check::g_frees, 1); CHECK(dtor_check::g_lastFree == h);
}

// The eight slots whose signature is plain machine words. `Compile` takes a
// `const System::Filename&` (one pointer) and returns a four-byte `?AW4` enum in
// EAX with NO hidden return slot (B35_RULES section 2.5 excludes `?AW4`), so it
// raw-calls safely too — but it is NOT marked, because three of the four classes
// inherit `Object::External`'s body, which does not Note().
template <class T>
void CheckExternalFamilySlots(void* raw, const ::Object::BaseTypeInfo* record) {
    using NameFn = const char* (*)(const void*);
    using TypeFn = uint32_t (*)(const void*);
    using VerFn  = int32_t (*)(const void*);
    using SerFn  = bool (*)(void*, void*);
    using PtrFn  = void* (*)(void*);
    using VoidFn = void (*)(void*);

    CheckDtorPair<T>();   // retail slot 0 (kDtorSlot): D1 / D0

    if (record) {   // DECLARE_TYPE's real slots 1/2 over the class's record (B.3.9 TI-REST)
        CHECK((SlotAt<NameFn, T>(raw, kTypeName)(raw)) == record->name);
        CHECK_EQ((SlotAt<TypeFn, T>(raw, kType)(raw)), record->typeId);
    } else {
        Reset(); SlotAt<NameFn, T>(raw, kTypeName)(raw);
        CHECK_EQ(g_lastMark, kTypeName / 8);

        Reset(); SlotAt<TypeFn, T>(raw, kType)(raw);
        CHECK_EQ(g_lastMark, kType / 8);
    }

    Reset(); SlotAt<VerFn, T>(raw, kGetFileVersion)(raw);
    CHECK_EQ(g_lastMark, kGetFileVersion / 8);

    Reset(); SlotAt<SerFn, T>(raw, kSerialize)(raw, nullptr);
    CHECK_EQ(g_lastMark, kSerialize / 8);

    Reset(); SlotAt<PtrFn, T>(raw, kReconstruct)(raw);
    CHECK_EQ(g_lastMark, kReconstruct / 8);

    Reset(); SlotAt<VoidFn, T>(raw, kDestruct)(raw);
    CHECK_EQ(g_lastMark, kDestruct / 8);

    Reset(); SlotAt<PtrFn, T>(raw, kConstruct)(raw);
    CHECK_EQ(g_lastMark, kConstruct / 8);
}

// NINE retail slots, all distinct. B3X W1: they are TEN compiler entries
// (retail slot 0 = D1 + D0), and D1 != D0 is part of the check.
void CheckNineDistinctSlots(void* raw) {
    const void* const* vt = VTableOf(raw);
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            CHECK(vt[i] != vt[j]);
}

// ---------------------------------------------------------------------------
// PART B/C — slot order
// ---------------------------------------------------------------------------
UNIT_TEST(graphics_material_slot_order_is_retails) {
    MaterialProbe probe;
    void* raw = static_cast<void*>(&probe);

    CheckExternalFamilySlots<MaterialProbe>(raw, &Graphics::Material::GetTypeInfo());
    CheckNineDistinctSlots(raw);

    // *** SLOT 4 IS INHERITED. *** B35_SLOTS.md:141 marks Material's [4] as
    // `0x1800066e0 ?Compile@External@Object@@ … inherited-from Object::External`,
    // so the entry at +0x20 must be byte-identical to a bare External leaf's.
    BareExternalProbe bare;
    CHECK(RetailEntry<MaterialProbe>(raw, kCompile / 8) ==
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));

    // `this` is unadjusted at +0x00: single inheritance, no virtual bases.
    CHECK(static_cast<void*>(static_cast<Graphics::Material*>(&probe)) == raw);
    CHECK(static_cast<void*>(static_cast<Object::External*>(&probe)) == raw);
    CHECK(static_cast<void*>(static_cast<Object::Base*>(&probe)) == raw);
}

UNIT_TEST(graphics_materialinstance_slot_order_is_retails) {
    MaterialInstanceProbe probe;
    void* raw = static_cast<void*>(&probe);

    CheckExternalFamilySlots<MaterialInstanceProbe>(raw, &Graphics::MaterialInstance::GetTypeInfo());
    CheckNineDistinctSlots(raw);

    BareExternalProbe bare;
    CHECK(RetailEntry<MaterialInstanceProbe>(raw, kCompile / 8) ==
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));

    CHECK(static_cast<void*>(static_cast<Object::External*>(&probe)) == raw);
}

// ---------------------------------------------------------------------------
// PART D — the qualified call. A door reached through a MINTED table must
// delegate NON-virtually or it re-enters itself (B34_BRIEFS A.1).
// ---------------------------------------------------------------------------
int g_qualifiedDoorEntries = 0;

int32_t MintedGetFileVersionDoor(const void* self) {
    ++g_qualifiedDoorEntries;
    // THE QUALIFIED FORM. Without `Graphics::Material::` this is a virtual call
    // that lands back in this same function through the stamped table below.
    return reinterpret_cast<const Graphics::Material*>(self)      // B.3.5 bridge
               ->Graphics::Material::GetFileVersion();
}

UNIT_TEST(graphics_material_qualified_call_does_not_reenter_the_door) {
    MaterialProbe probe;
    void* raw = static_cast<void*>(&probe);

    // A hand-built nine-entry table, the shape `rt_data_mint_tables.inc` stamps,
    // with slot 3 pointing at the door.
    const void* minted[10] = {};   // nine retail slots = ten compiler entries
    const void* const* real = VTableOf(raw);
    for (int i = 0; i < 10; ++i) minted[i] = real[i];
    minted[RetailSlot<MaterialProbe>(kGetFileVersion / 8)] =
        reinterpret_cast<const void*>(&MintedGetFileVersionDoor);

    const void* const* saved = real;
    *reinterpret_cast<const void* const**>(raw) = minted;

    g_qualifiedDoorEntries = 0;
    Reset();
    using VerFn = int32_t (*)(const void*);
    const int32_t v = SlotAt<VerFn, MaterialProbe>(raw, kGetFileVersion)(raw);

    // Entered ONCE — the qualified call went to the member, not back through
    // the minted slot — and the member's marker ran.
    CHECK_EQ(g_qualifiedDoorEntries, 1);
    CHECK_EQ(g_lastMark, kGetFileVersion / 8);
    CHECK_EQ(v, 5);                       // Material::GetFileVersion's literal

    *reinterpret_cast<const void* const**>(raw) = saved;
}

// ---------------------------------------------------------------------------
// PART A — layout
// ---------------------------------------------------------------------------
UNIT_TEST(graphics_material_layout) {
    CHECK_EQ((int)alignof(Graphics::Material), 8);

    // The derived fields begin exactly at the end of the base — no tail-padding
    // reuse, no second vptr.
    CHECK_EQ((int)offsetof(Graphics::Material, _type), (int)sizeof(Object::External));

    // `Object::External`, NOT `Object::CompiledExternal`
    // (?_ParentTypeName@Material@Graphics@@ @0x1800118a0 -> "Object::External").
    CHECK((std::is_base_of<Object::External, Graphics::Material>::value));
    CHECK((!std::is_base_of<Object::CompiledExternal, Graphics::Material>::value));

    // ENDGAME B.3.9 GFX: CONCRETE now (its own slot 0; the RT_DELDTOR row and
    // the mint row are retired) -- b39_gfx_material_* below.
    CHECK(!std::is_abstract<Graphics::Material>::value);
    CHECK(std::is_polymorphic<Graphics::Material>::value);

    // The pass array's element is eight bytes — the `MOV RAX,[RAX+RDX*8]` at
    // 0x1801027a0 inside ?IsPassValid@.
    CHECK(std::is_trivially_copyable<Object::ExternalPtr<Video::VideoProgram> >::value);

    // A field write through the CLASS is visible at the bytes every
    // `reinterpret_cast` in material.cpp reads.
    MaterialProbe probe;
    probe._type = 3;
    probe._passes._size = 2;
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(static_cast<Graphics::Material*>(&probe));
    CHECK_EQ(*reinterpret_cast<const int32_t*>(bytes + 0x20), 3);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(bytes + 0x30), 2);
}

UNIT_TEST(graphics_materialinstance_layout) {
    CHECK_EQ((int)alignof(Graphics::MaterialInstance), 8);

    CHECK_EQ((int)offsetof(Graphics::MaterialInstance, _shaderClass),
             (int)sizeof(Object::External));
    CHECK((std::is_base_of<Object::External, Graphics::MaterialInstance>::value));
    CHECK((!std::is_base_of<Object::CompiledExternal, Graphics::MaterialInstance>::value));
    CHECK(!std::is_abstract<Graphics::MaterialInstance>::value);   // concrete since B.3.8 RB (exe table copy 0x14015c400)

    // The POD that still describes the same bytes, and has live users outside
    // block G's file set, so the two must not drift.

    // The four element types the arrays hold, unchanged by block G.

    // `RendererDrawItem::material` is this +0x28 handle BY VALUE (B35_RULES
    // section 4), so the handle has to be the eight trivially-copyable bytes an
    // array element bulk-copy moves.
    CHECK(std::is_trivially_copyable<Object::ExternalPtr<Graphics::Material> >::value);

    MaterialInstanceProbe probe;
    probe._shaderClass        = 4;
    probe._hasRenderTexture   = 1;
    probe._hasRenderedTexture = 1;
    probe._textures._size     = 6;
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(static_cast<Graphics::MaterialInstance*>(&probe));
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(bytes + 0x20), 4u);
    CHECK_EQ((int)bytes[0x24], 1);
    CHECK_EQ((int)bytes[0x25], 1);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(bytes + 0x58), 6);
    // B7: MaterialTexture holds a real System::String, so the member ~Array
    // walks `_size` elements; the forged count goes before the probe dies.
    probe._textures._size = 0;
}

// ---------------------------------------------------------------------------
// Graphics::GraphicsMesh — the ONE class of block G's four whose base is
// `Object::CompiledExternal` (?_ParentTypeName@ @0x180012360 ->
// "Object::CompiledExternal", and its ctor stamps FOUR tables where Material's
// and MaterialInstance's stamp three).
// ---------------------------------------------------------------------------
UNIT_TEST(graphics_graphicsmesh_slot_order_is_retails) {
    GraphicsMeshProbe probe;
    void* raw = static_cast<void*>(&probe);

    CheckExternalFamilySlots<GraphicsMeshProbe>(raw, &Graphics::GraphicsMesh::GetTypeInfo());
    CheckNineDistinctSlots(raw);

    // Slot 4 is INHERITED `?Compile@External@Object@@` (0x1800066e0), the same
    // body a bare `Object::External` leaf carries — `Object::CompiledExternal`
    // does NOT override it, which is why the pointer must be identical even
    // though the two probes sit on different bases.
    BareExternalProbe bare;
    CHECK(RetailEntry<GraphicsMeshProbe>(raw, kCompile / 8) ==
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));

    CHECK(static_cast<void*>(static_cast<Object::CompiledExternal*>(&probe)) == raw);
    CHECK(static_cast<void*>(static_cast<Object::External*>(&probe)) == raw);
    CHECK(static_cast<void*>(static_cast<Object::Base*>(&probe)) == raw);
}

UNIT_TEST(graphics_graphicsmesh_layout) {
    CHECK_EQ((int)alignof(Graphics::GraphicsMesh), 8);

    // *** THE BASE SPLIT, ASSERTED IN BOTH DIRECTIONS. *** This is the
    // `Video::VideoProgram` mistake of B35_RULES section 2.1, one namespace over:
    // GraphicsMesh is a CompiledExternal and the other three are not.
    CHECK((std::is_base_of<Object::CompiledExternal, Graphics::GraphicsMesh>::value));
    CHECK((!std::is_base_of<Object::CompiledExternal, Graphics::Material>::value));
    CHECK((!std::is_base_of<Object::CompiledExternal, Graphics::MaterialInstance>::value));
    // …and `CompiledExternal` adds no bytes, so the derived fields still start
    // where `Object::External`'s end.
    CHECK_EQ((int)sizeof(Object::CompiledExternal), (int)sizeof(Object::External));
    CHECK_EQ((int)offsetof(Graphics::GraphicsMesh, _name),
             (int)sizeof(Object::CompiledExternal));

    CHECK(!std::is_abstract<Graphics::GraphicsMesh>::value);   // concrete since B.3.9 GFX

    // The two nested value types, and the detail-level record the +0x50 array
    // holds. `graphicsmesh_vtable.cpp` used to call that array an
    // `Array<TriangleList>`; it is an `Array<DetailLevel>` and the
    // `Array<TriangleList>` is INSIDE each level, at +0x98.

    // The four `_deviceData` qwords `TriangleListPass` / `PlatformSerialize`
    // dispatch through are the record's +0x40, +0x20, +0x80, +0x60 — which is
    // the reading that fixed the whole 0xb0 layout, so it is a test.
    CHECK_EQ((int)(offsetof(Graphics::GraphicsMesh::DetailLevel, _indexBuffer) +
                   offsetof(Video::IndexBuffer, platformData)), 0x40);
    CHECK_EQ((int)(offsetof(Graphics::GraphicsMesh::DetailLevel, _vertexBuffer) +
                   offsetof(Video::VertexBuffer, platformData)), 0x20);
    CHECK_EQ((int)(offsetof(Graphics::GraphicsMesh::DetailLevel, _skinIndexBuffer) +
                   offsetof(Video::IndexBuffer, platformData)), 0x80);
    CHECK_EQ((int)(offsetof(Graphics::GraphicsMesh::DetailLevel, _skinVertexBuffer) +
                   offsetof(Video::VertexBuffer, platformData)), 0x60);

    // `Node::_parent` is LEFT UNINITIALISED by retail's default constructor and
    // the copy constructor DOES propagate it; nothing may zero it. The test that
    // can be written for that here is that `Node` is not implicitly zeroing
    // itself — it has no default member initialiser and no default constructor
    // that would supply one.
    CHECK(!std::is_trivially_copyable<Graphics::GraphicsMesh::Node>::value);
    CHECK(!std::is_default_constructible<Graphics::GraphicsMesh::Node>::value);

    GraphicsMeshProbe probe;
    probe._scale = 2.0f;
    probe._detailLevels._size = 3;
    probe._skinMaterials._size = 5;
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(static_cast<Graphics::GraphicsMesh*>(&probe));
    CHECK_EQ(*reinterpret_cast<const float*>(bytes + 0x44), 2.0f);
    // +0x58 is what ?GetDetailLevelCount@ reads and +0x78 what ?GetSkinCount@
    // reads — the `_size` halves of the arrays based at +0x50 and +0x70.
    CHECK_EQ(*reinterpret_cast<const int32_t*>(bytes + 0x58), 3);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(bytes + 0x78), 5);
    // B.4 (R-GFX): the two arrays carry their element types now, and this
    // test's stub ~GraphicsMesh does not run retail's explicit ~Array calls,
    // so the sizes written over null buffers are put back before the
    // implicit member dtors see them.
    probe._detailLevels._size = 0;
    probe._skinMaterials._size = 0;
}

// ---------------------------------------------------------------------------
// Graphics::EdgeMesh — the ONE class of block G's four with its OWN slot 4.
// ---------------------------------------------------------------------------
UNIT_TEST(graphics_edgemesh_slot_order_is_retails) {
    EdgeMeshProbe probe;
    void* raw = static_cast<void*>(&probe);

    CheckExternalFamilySlots<EdgeMeshProbe>(raw, &Graphics::EdgeMesh::GetTypeInfo());
    CheckNineDistinctSlots(raw);

    // *** SLOT 4 IS ITS OWN, AND THAT IS THE ASYMMETRY THIS TEST EXISTS FOR. ***
    // `?Compile@EdgeMesh@Graphics@@` (0x1800a21c0) is `MOV EAX,1 ; RET` and
    // `?Compile@External@Object@@` (0x1800066e0) also answers 1, so a table that
    // wrongly INHERITED slot 4 would behave identically and the mistake would
    // never surface. The only thing that can see it is the pointer.
    BareExternalProbe bare;
    CHECK(RetailEntry<EdgeMeshProbe>(raw, kCompile / 8) !=
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));

    // And the marker proves the override is EdgeMesh's body, not something that
    // merely differs.
    Reset();
    using CompFn = uint32_t (*)(void*, const void*);
    const uint32_t r = SlotAt<CompFn, EdgeMeshProbe>(raw, kCompile)(raw, nullptr);
    CHECK_EQ(g_lastMark, kCompile / 8);
    CHECK_EQ((int)r, 1);

    // …while the other three DO inherit it.
    MaterialProbe mat;
    GraphicsMeshProbe gm;
    MaterialInstanceProbe mi;
    CHECK(RetailEntry<MaterialProbe>(&mat, kCompile / 8) ==
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));
    CHECK(RetailEntry<GraphicsMeshProbe>(&gm, kCompile / 8) ==
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));
    CHECK(RetailEntry<MaterialInstanceProbe>(&mi, kCompile / 8) ==
          RetailEntry<BareExternalProbe>(&bare, kCompile / 8));

    CHECK(static_cast<void*>(static_cast<Object::External*>(&probe)) == raw);
    CHECK((!std::is_base_of<Object::CompiledExternal, Graphics::EdgeMesh>::value));
}

UNIT_TEST(graphics_edgemesh_layout) {
    CHECK_EQ((int)alignof(Graphics::EdgeMesh), 8);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _reserved20), (int)sizeof(Object::External));
    // B.3.9 GFX: concrete -- slot 0 is the class's own (vector) deleting
    // destructor, since B3X W1 the virtual destructor.
    CHECK(!std::is_abstract<Graphics::EdgeMesh>::value);

    // The four material slots, as retail's four getters name them.

    // The four buffer pairs, as retail's eight `LEA RAX,[RCX+off]` getters name
    // them. THREE OF THE FOUR WERE NAMED BY GUESS in `RtGraphics::EdgeMesh`
    // (`_edgeVerts` is the SKIN pair, `_capVerts` is the EDGE pair,
    // `_extraVerts` is the EDGE-SKIN pair), so the cross-asserts are the point.

    // The eight offsets `EdgeMesh::Serialize` dispatches through are each
    // header+0x08 — the `_deviceData` qword. Retail's step 7 order is
    // 0x70, 0x50, 0xb0, 0x90, 0xf0, 0xd0, 0x130, 0x110, i.e. INDEX before VERTEX
    // in each pair; the member now spells that with named fields, so the
    // arithmetic is asserted here.
    const int dd = (int)offsetof(RtGraphics::VideoBufferHeader, _deviceData);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _indexBuffer) + dd,          0x70);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _vertexBuffer) + dd,         0x50);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _skinIndexBuffer) + dd,      0xb0);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _skinVertexBuffer) + dd,     0x90);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _edgeIndexBuffer) + dd,      0xf0);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _edgeVertexBuffer) + dd,     0xd0);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _edgeSkinIndexBuffer) + dd,  0x130);
    CHECK_EQ((int)offsetof(Graphics::EdgeMesh, _edgeSkinVertexBuffer) + dd, 0x110);

    // The two nested vertex records are ALIASES of the PODs, not copies.
    CHECK((std::is_same<Graphics::EdgeMesh::TriangleVertex,
                        RtGraphics::TriangleVertex>::value));
    CHECK((std::is_same<Graphics::EdgeMesh::EdgeVertex,
                        RtGraphics::EdgeVertex>::value));
    // They stay TRIVIALLY COPYABLE on purpose: they are vertex data that retail
    // bulk-copies, and giving either a real constructor would change that.
    CHECK(std::is_trivially_copyable<Graphics::EdgeMesh::TriangleVertex>::value);
    CHECK(std::is_trivially_copyable<Graphics::EdgeMesh::EdgeVertex>::value);

    // A write through the CLASS is visible at the bytes every
    // `reinterpret_cast` in mesh.cpp and edgemesh_vtable.cpp reads.
    EdgeMeshProbe probe;
    probe._triangleLists._size = 9;
    probe._edgeLists._size     = 4;
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(static_cast<Graphics::EdgeMesh*>(&probe));
    CHECK_EQ(*reinterpret_cast<const int32_t*>(bytes + 0x150), 9);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(bytes + 0x160), 4);
}

// RULE B5-16 across all four classes at once: nine slots each, `Construct` at
// +0x40, and `Object::External`'s own table stops at eight.
UNIT_TEST(graphics_block_g_all_four_introduce_construct_at_slot8) {
    MaterialProbe mat;
    MaterialInstanceProbe mi;
    GraphicsMeshProbe gm;
    EdgeMeshProbe em;
    void* all[4] = { &mat, &mi, &gm, &em };

    for (int c = 0; c < 4; ++c) {
        Reset();
        using PtrFn = void* (*)(void*);
        SlotAt<PtrFn, Object::Base>(all[c], kConstruct)(all[c]);
        CHECK_EQ(g_lastMark, kConstruct / 8);
        CHECK(g_lastSelf == all[c]);
        // And slot 8 is distinct from all of slots 0..7 in that same table
        // (compiler 0..8: retail slot 0 is D1 + D0).
        for (int i = 0; i < 9; ++i)
            CHECK(VTableOf(all[c])[i] != RetailEntry<Object::Base>(all[c], kConstruct / 8));
    }
}

// ===========================================================================
// ENDGAME B.3.9 block GFX -- Material / GraphicsMesh CONCRETE (D1, B9-16).
// ===========================================================================

// B3X W1: retail flags 1 is `delete` (T's own D0, its class-scope MemFree
// operator delete; the free proven on a Recording<T> stand-in); the vector arm
// (flags 3, stride = sizeof) is retired: `delete[]` replaces it.
template <class T> void CheckBothSlot0Arms(size_t stride) {
    static_assert(dtor_check::HasClassDelete<T>::value, "D0 frees through a class-scope operator delete");
    CHECK_EQ(stride, sizeof(T));
    void* m = Object::detail::InternalMemAlloc(sizeof(T), 8);
    T* h = ::new (m) T();
    g_gfxDtorCount = 0;
    delete h;
    CHECK_EQ(g_gfxDtorCount, 1);

    T* r = dtor_check::NewOnMalloc<dtor_check::Recording<T>>();
    g_gfxDtorCount = 0; dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_gfxDtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));

    CHECK_ARRAY_DELETE(T, g_gfxDtorCount, g_gfxDtorOrder);
}

UNIT_TEST(b39_gfx_material_concrete_ctor_stores_and_both_slot0_arms) {
    using M = Graphics::Material;
    CHECK(std::is_destructible<M>::value);            // B3X W1: public virtual
    CHECK(std::has_virtual_destructor<M>::value);
    CHECK_DTOR_SLOT(M);   // 0x180011d20
    CHECK_SLOT(M, TypeName,       0x08/8);
    CHECK_SLOT(M, Type,           0x10/8);
    CHECK_SLOT(M, GetFileVersion, 0x18/8);
    CHECK_SLOT(M, Serialize,      0x28/8);
    CHECK_SLOT(M, Reconstruct,    0x30/8);
    CHECK_SLOT(M, Destruct,       0x38/8);
    CHECK_SLOT(M, Construct,      0x40/8);

    // retail 0x1801029a0 / the FACTREST steps: External's three zero qwords,
    // +0x20 dword 0, +0x24 untouched, +0x28 / +0x30 zero.
    alignas(8) uint8_t buf[0x38];
    std::memset(buf, 0xCD, sizeof buf);
    M* m = ::new (static_cast<void*>(buf)) M();
    for (size_t off = 0x08; off < 0x20; off += 8)
        CHECK_EQ(*reinterpret_cast<const uint64_t*>(buf + off), (uint64_t)0);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(buf + 0x20), 0u);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(buf + 0x24), 0xCDCDCDCDu);
    CHECK_EQ(*reinterpret_cast<const uint64_t*>(buf + 0x28), (uint64_t)0);
    CHECK_EQ(*reinterpret_cast<const uint64_t*>(buf + 0x30), (uint64_t)0);
    g_gfxDtorCount = 0;
    m->~Material();                                   // retail flags 0
    CHECK_EQ(g_gfxDtorCount, 1);

    CheckBothSlot0Arms<M>(0x38);   // retired vector arm: 0x180011d20
}

UNIT_TEST(b39_gfx_graphicsmesh_concrete_ctor_stores_and_both_slot0_arms) {
    using G = Graphics::GraphicsMesh;
    CHECK(std::is_destructible<G>::value);            // B3X W1: public virtual
    CHECK(std::has_virtual_destructor<G>::value);
    CHECK_DTOR_SLOT(G);   // 0x180012bd0
    CHECK_SLOT(G, TypeName,       0x08/8);
    CHECK_SLOT(G, Type,           0x10/8);
    CHECK_SLOT(G, GetFileVersion, 0x18/8);
    CHECK_SLOT(G, Serialize,      0x28/8);
    CHECK_SLOT(G, Reconstruct,    0x30/8);
    CHECK_SLOT(G, Destruct,       0x38/8);
    CHECK_SLOT(G, Construct,      0x40/8);

    // retail 0x1800ff170, the store list read as bit patterns.
    alignas(8) uint8_t buf[0x98];
    std::memset(buf, 0xCD, sizeof buf);
    G* g = ::new (static_cast<void*>(buf)) G();
    auto D = [&](size_t off) { uint32_t v; std::memcpy(&v, buf + off, 4); return v; };
    auto Q = [&](size_t off) { uint64_t v; std::memcpy(&v, buf + off, 8); return v; };
    for (size_t off = 0x08; off < 0x28; off += 8) CHECK_EQ(Q(off), (uint64_t)0);   // External + _name
    for (size_t off = 0x28; off < 0x34; off += 4) CHECK_EQ(D(off), 0x3f800000u);   // ( 1, 1, 1)
    for (size_t off = 0x34; off < 0x40; off += 4) CHECK_EQ(D(off), 0xbf800000u);   // (-1,-1,-1)
    CHECK_EQ(D(0x40), 1u);
    CHECK_EQ(D(0x44), 0x3f800000u);
    CHECK_EQ(D(0x48), 0x461c4000u);                                                 // 10000.0f
    CHECK_EQ(D(0x4c), 0xCDCDCDCDu);                                                 // never written
    for (size_t off = 0x50; off < 0x80; off += 8) CHECK_EQ(Q(off), (uint64_t)0);   // three Arrays
    for (size_t off = 0x80; off < 0x8c; off += 4) CHECK_EQ(D(off), 0x4cbebc20u);   //  1e8f
    for (size_t off = 0x8c; off < 0x98; off += 4) CHECK_EQ(D(off), 0xccbebc20u);   // -1e8f
    g_gfxDtorCount = 0;
    g->~GraphicsMesh();                               // retail flags 0
    CHECK_EQ(g_gfxDtorCount, 1);

    CheckBothSlot0Arms<G>(0x98);   // retired vector arm: 0x180012bd0
}

}  // namespace

// ############################################################################
// THE OUT-OF-LINE BODIES THE PROBES NEED AT LINK TIME.
//
// See the file banner: `src/engine/graphics/material.cpp` holds the real ones
// and is NOT in `UNIT_TREE_SRCS`, so there is exactly one definition per binary.
// Every body here is a MARKER, deliberately not a reimplementation — this file
// tests slot order and layout, not the reflection records or the serializer.
// `GetFileVersion` returns retail's literal 5 / 4 because PART D reads it back.
// ############################################################################

namespace Graphics {

int32_t     Material::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 5; }
bool        Material::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* Material::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        Material::Destruct() { Note(this, kDestruct / 8); }
Object::External* Material::Construct() { Note(this, kConstruct / 8); return this; }
Material::~Material() { NoteDtor(this); }

int32_t     MaterialInstance::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 4; }
bool        MaterialInstance::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* MaterialInstance::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        MaterialInstance::Destruct() { Note(this, kDestruct / 8); }
Object::External* MaterialInstance::Construct() { Note(this, kConstruct / 8); return this; }
MaterialInstance::~MaterialInstance() {}

int32_t     GraphicsMesh::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 0x3415; }
bool        GraphicsMesh::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* GraphicsMesh::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        GraphicsMesh::Destruct() { Note(this, kDestruct / 8); }
Object::External* GraphicsMesh::Construct() { Note(this, kConstruct / 8); return this; }
GraphicsMesh::~GraphicsMesh() { NoteDtor(this); }

int32_t     EdgeMesh::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 2; }
// The ONE marker in this file that stands for an OVERRIDE of slot 4 rather than
// an inherited body. It answers retail's 1, which the slot-order test reads back.
Object::External::CompileResult EdgeMesh::Compile(const System::Filename&) {
    Note(this, kCompile / 8); return 1u;
}
bool        EdgeMesh::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* EdgeMesh::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        EdgeMesh::Destruct() { Note(this, kDestruct / 8); }
Object::External* EdgeMesh::Construct() { Note(this, kConstruct / 8); return this; }
EdgeMesh::EdgeMesh() {}
EdgeMesh::~EdgeMesh() {}

}  // namespace Graphics

// The two buffer destructors (retail ??1 0x1800f3480 / 0x1800f55e0, buffers.cpp)
// the stub ~EdgeMesh / ~DetailLevel reach; the host test does not link video/.
Video::IndexBuffer::~IndexBuffer() {}
Video::VertexBuffer::~VertexBuffer() {}
