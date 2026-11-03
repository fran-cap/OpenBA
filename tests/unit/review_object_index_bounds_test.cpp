// ============================================================================
// tests/unit/review_object_index_bounds_test.cpp — review 2026-11-03, group
// object-index-bounds (findings F-022, F-024, F-025, F-026)
//
// Four places where an index or a size read from a save or a resource indexes an
// array whose length was serialized separately. Each case hands the reader the
// malformed value inline and lays the array out so that retail's unchecked read
// lands on a DETERMINISTIC element past the logical end (a buffer larger than its
// `_size`), so the unchecked form fails an assertion instead of reading heap noise.
//
//   F-022  InternalFactory::PersistObject (0x1800bff10), read arm: the save's
//          object index against `_persistIds._size`. The real member runs here.
//   F-024  SpriteCapture::Serialize (0x1800d9530): an entry's sheetIndex against
//          `_sheets._size` (UI::SpriteCapture::HasSheetSlot, the load loop's test).
//   F-025  ModelComponent::Persist: the save's mesh group / index against the
//          description's group and mesh counts (ModelDescriptionMesh).
//   F-026  Map::Serialize (0x14009f100): the save's map edge against the cell
//          array's count (Map::LoadedSize).
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#include "object/Internal.h"
#include "io/Stream.h"
#include "ui/SpriteCapture.h"
#include "Component/ModelDescription.h"
#include "Game/Map.h"

namespace Object {
namespace detail {
extern InternalFactory* g_unitInternalFactory;
}  // namespace detail
}  // namespace Object

namespace {

// ---------------------------------------------------------------------------
// F-022 — the save's object index on PersistObject's read arm
// ---------------------------------------------------------------------------

int g_persistCalls = 0;

class BoundsInternal : public ::Object::Internal {
    DECLARE_TYPE(BoundsInternal)

public:
    BoundsInternal() {}
    ~BoundsInternal() override {}

    static void operator delete(void* p) { ::Object::detail::InternalMemFree(p); }

    void Persist(IO::Stream& /*stream*/) override { ++g_persistCalls; }
};

DEFINE_TYPE(BoundsInternal, "Internal", nullptr)

::Object::Base* BoundsInternal::CreateNewObject() { return nullptr; }
::Object::Base* BoundsInternal::CreateInPlaceObject(void*) { return nullptr; }

struct FactoryScope {
    ::Object::InternalFactory  factory;
    ::Object::InternalFactory* saved;
    FactoryScope() : factory(), saved(::Object::detail::g_unitInternalFactory) {
        ::Object::detail::g_unitInternalFactory = &factory;
    }
    ~FactoryScope() { ::Object::detail::g_unitInternalFactory = saved; }
};

// A reading stream. PersistObject's read arm with an already-resolved slot reads
// only `_direction`; the MemoryStream is built in static storage and never torn
// down (its destructor is protected; the one encoding allocation is left).
IO::Stream& ReadingStream() {
    alignas(IO::MemoryStream) static unsigned char storage[sizeof(IO::MemoryStream)];
    static IO::MemoryStream* stream = ::new (static_cast<void*>(storage)) IO::MemoryStream();
    stream->_direction = 0;
    return *stream;
}

}  // namespace

UNIT_TEST(review_bounds_f022_persistobject_rejects_an_index_past_persistids) {
    FactoryScope scope;
    void* mem = ::Object::detail::InternalMemAlloc(sizeof(BoundsInternal), 8);
    BoundsInternal* obj = ::new (mem) BoundsInternal();   // _factoryIndex 0
    CHECK_EQ(scope.factory.GetObjects().GetSize(), 1);

    // A save that resolved object 0 once: ResetPersistState sized `_persistIds`
    // for one object. The buffer is grown to 8 and every element says "slot 0",
    // then `_size` is put back to 1, so an unchecked _persistIds[4] reads a
    // valid-looking slot instead of heap noise.
    ::Object::detail::ArrayIntSetSize(&scope.factory._persistIds, 8);
    for (int i = 0; i < 8; ++i)
        scope.factory._persistIds._data[i] = 0;
    scope.factory._persistIds._size = 1;

    IO::Stream& in = ReadingStream();
    g_persistCalls = 0;

    // Valid data: index 0 is in range and already resolved -> the object, no Persist.
    CHECK(scope.factory.PersistObject(in, 0u, 0, 0u) == static_cast< ::Object::Internal*>(obj));

    // The InternalPtr record from the save, inline: index 4, generation 0, type 0
    // (little-endian int32 / uint32 / uint32, as SerializeInt / SerializeUInt read it).
    const uint8_t record[12] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    int32_t index;
    uint32_t gen;
    uint32_t type;
    std::memcpy(&index, record + 0, 4);
    std::memcpy(&gen, record + 4, 4);
    std::memcpy(&type, record + 8, 4);
    CHECK(scope.factory.PersistObject(in, gen, index, type) == nullptr);

    // index == _size is the first slot past the end.
    CHECK(scope.factory.PersistObject(in, 0u, 1, 0u) == nullptr);
    CHECK_EQ(g_persistCalls, 0);

    scope.factory.ClearPersistState();
    ::Object::Destroy(obj);
}

UNIT_TEST(review_bounds_f022_persistobject_rejects_a_negative_index) {
    FactoryScope scope;
    void* mem = ::Object::detail::InternalMemAlloc(sizeof(BoundsInternal), 8);
    BoundsInternal* obj = ::new (mem) BoundsInternal();
    ::Object::detail::ArrayIntSetSize(&scope.factory._persistIds, 1);
    scope.factory._persistIds._data[0] = 0;

    // index 0xFFFFFFFF (-1) from the save: one int32 before the buffer.
    const uint8_t rawIndex[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    int32_t index;
    std::memcpy(&index, rawIndex, 4);
    CHECK(scope.factory.PersistObject(ReadingStream(), 0u, index, 0u) == nullptr);

    scope.factory.ClearPersistState();
    ::Object::Destroy(obj);
}

// ---------------------------------------------------------------------------
// F-024 — a capture entry's sheetIndex against the loaded sheet count
// ---------------------------------------------------------------------------

UNIT_TEST(review_bounds_f024_spritecapture_sheet_index_past_sheets_is_no_sheet) {
    alignas(UI::SpriteCapture) unsigned char buf[sizeof(UI::SpriteCapture)];
    UI::SpriteCapture* c = ::new (static_cast<void*>(buf)) UI::SpriteCapture();

    // `_sheets` as Serialize leaves it after a sheet count of 0; the entry that
    // follows in the resource carries sheetIndex 5 (bytes 05 00 00 00).
    const uint8_t rawIndex[4] = {0x05, 0x00, 0x00, 0x00};
    int32_t sheetIndex;
    std::memcpy(&sheetIndex, rawIndex, 4);
    CHECK(!c->HasSheetSlot(sheetIndex));
    CHECK(!c->HasSheetSlot(0));

    // The scissor sentinels stay the scissor arm.
    CHECK(!c->HasSheetSlot(-1));
    CHECK(!c->HasSheetSlot(-2));

    c->~SpriteCapture();
}

UNIT_TEST(review_bounds_f024_spritecapture_valid_sheet_index_is_unchanged) {
    alignas(UI::SpriteCapture) unsigned char buf[sizeof(UI::SpriteCapture)];
    UI::SpriteCapture* c = ::new (static_cast<void*>(buf)) UI::SpriteCapture();

    UI::SpriteSheet* sheets[2] = {nullptr, nullptr};
    c->_sheets._data = sheets;   // borrowed: put back before the destructor runs
    c->_sheets._size = 2;
    CHECK(c->HasSheetSlot(0));
    CHECK(c->HasSheetSlot(1));
    CHECK(!c->HasSheetSlot(2));

    c->_sheets._data = nullptr;
    c->_sheets._size = 0;
    c->~SpriteCapture();
}

// ---------------------------------------------------------------------------
// F-025 — the save's mesh group / index against the description's arrays
// ---------------------------------------------------------------------------

UNIT_TEST(review_bounds_f025_model_mesh_group_and_index_past_the_description) {
    // A description with ONE group of ONE mesh. The arrays carry a second,
    // populated element past their counts, so an unchecked read returns it.
    Graphics::GraphicsMesh* const meshA = reinterpret_cast<Graphics::GraphicsMesh*>(uintptr_t{0x1000});
    Graphics::GraphicsMesh* const meshB = reinterpret_cast<Graphics::GraphicsMesh*>(uintptr_t{0x2000});
    Graphics::GraphicsMesh* meshes[2] = {meshA, meshB};
    ModelMeshGroup groups[2];
    groups[0].field_0x00   = nullptr;
    groups[0].meshes       = meshes;
    groups[0].meshCount    = 1;
    groups[0].meshCapacity = 2;
    groups[1]              = groups[0];

    // Valid data: (0, 0) is the mesh.
    CHECK(ModelDescriptionMesh(groups, 1, 0, 0) == meshA);

    // The save's two int16 fields, inline: meshGroup 1 (01 00), meshIndex 0 (00 00).
    const uint8_t rec1[4] = {0x01, 0x00, 0x00, 0x00};
    int16_t g;
    int16_t i;
    std::memcpy(&g, rec1 + 0, 2);
    std::memcpy(&i, rec1 + 2, 2);
    CHECK(ModelDescriptionMesh(groups, 1, g, i) == nullptr);

    // meshGroup 0 (00 00), meshIndex 1 (01 00).
    const uint8_t rec2[4] = {0x00, 0x00, 0x01, 0x00};
    std::memcpy(&g, rec2 + 0, 2);
    std::memcpy(&i, rec2 + 2, 2);
    CHECK(ModelDescriptionMesh(groups, 1, g, i) == nullptr);

    // Negative fields and no groups stay null, as before.
    CHECK(ModelDescriptionMesh(groups, 1, -1, 0) == nullptr);
    CHECK(ModelDescriptionMesh(groups, 1, 0, -1) == nullptr);
    CHECK(ModelDescriptionMesh(nullptr, 0, 0, 0) == nullptr);
}

// ---------------------------------------------------------------------------
// F-026 — the save's map edge against the cell count
// ---------------------------------------------------------------------------

UNIT_TEST(review_bounds_f026_map_size_larger_than_the_cell_array_is_rejected) {
    // The head of Map::Serialize's record, inline: size 512 (00 02 00 00), then the
    // cell array's count 1 (01 00 00 00). Every load walk covers size * size cells.
    const uint8_t head[8] = {0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    int32_t size;
    int32_t count;
    std::memcpy(&size, head + 0, 4);
    std::memcpy(&count, head + 4, 4);
    const int32_t kept = Map::LoadedSize(size, count);
    CHECK(static_cast<int64_t>(kept) * kept <= count);

    // An edge whose square leaves int32 (46341^2 > INT32_MAX) against a large count.
    CHECK_EQ(Map::LoadedSize(46341, 0x7fffffff), 0);
    // A negative edge.
    CHECK_EQ(Map::LoadedSize(-4, 16), 0);
}

UNIT_TEST(review_bounds_f026_map_valid_sizes_are_unchanged) {
    CHECK_EQ(Map::LoadedSize(512, 512 * 512), 512);
    CHECK_EQ(Map::LoadedSize(256, 256 * 256), 256);
    CHECK_EQ(Map::LoadedSize(0, 0), 0);
}
