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
//          `_sheets._size` (UI::SpriteCapture::DropEntriesNamingNoSheet).
//   F-025  ModelComponent::Persist: the save's mesh group / index against the
//          description's group and mesh counts (ModelDescriptionMesh).
//   F-026  Map::Serialize (0x14009f100): the save's map edge against the cell
//          array's count (Map::CellsCoverSize).
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
//
// SpriteCapture::Serialize's read arm calls DropEntriesNamingNoSheet right after
// the entry array is read, before it rebuilds `_sprites`. These tests drive that
// member over a capture laid out as the reader leaves it. Serialize itself is
// not in the unit target: spritecapture_class.cpp also holds Draw, which needs the
// SpriteContext / material chain.

namespace {

// A capture as Serialize leaves it after reading `sheetCount` sheet slots and the
// given entries' sheetIndex fields (each 4 bytes little-endian, inline hex).
struct LoadedCapture {
    alignas(UI::SpriteCapture) unsigned char buf[sizeof(UI::SpriteCapture)];
    UI::SpriteSheet* sheets[2] = {nullptr, nullptr};
    UI::SpriteCapture* c;

    LoadedCapture(int32_t sheetCount, const uint8_t* rawIndexes, int32_t entryCount) {
        c = ::new (static_cast<void*>(buf)) UI::SpriteCapture();
        c->_sheets._data = sheets;   // borrowed: put back before the destructor runs
        c->_sheets._size = sheetCount;
        const UI::CaptureEntry blank{};   // every entry starts with an empty name
        c->_entries.SetSize(entryCount, blank);
        for (int32_t i = 0; i < entryCount; ++i)
            std::memcpy(&c->_entries._data[i].sheetIndex, rawIndexes + 4 * i, 4);
    }
    ~LoadedCapture() {
        c->_sheets._data = nullptr;
        c->_sheets._size = 0;
        c->~SpriteCapture();
    }
};

}  // namespace

UNIT_TEST(review_bounds_f024_capture_entry_past_the_sheets_drops_the_entries) {
    // One sheet slot; entries: a scissor (-1), a sprite on sheet 0, a sprite on
    // sheet 5 (05 00 00 00), past the one slot. Draw would take the sprite arm for
    // the last one and dereference a sheet that does not exist.
    const uint8_t raw[12] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00};
    LoadedCapture cap(1, raw, 3);
    CHECK(cap.c->DropEntriesNamingNoSheet());
    CHECK_EQ(cap.c->_entries._size, 0);
}

UNIT_TEST(review_bounds_f024_capture_entry_at_the_sheet_count_drops_the_entries) {
    // Two sheet slots; a sprite on sheet 2 (02 00 00 00), the first index past them.
    const uint8_t raw[8] = {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};
    LoadedCapture cap(2, raw, 2);
    CHECK(cap.c->DropEntriesNamingNoSheet());
    CHECK_EQ(cap.c->_entries._size, 0);
}

UNIT_TEST(review_bounds_f024_capture_negative_non_scissor_index_drops_the_entries) {
    // sheetIndex -3 (FD FF FF FF): neither scissor sentinel, so Draw's sprite arm,
    // with no sheet.
    const uint8_t raw[4] = {0xFD, 0xFF, 0xFF, 0xFF};
    LoadedCapture cap(1, raw, 1);
    CHECK(cap.c->DropEntriesNamingNoSheet());
    CHECK_EQ(cap.c->_entries._size, 0);
}

UNIT_TEST(review_bounds_f024_capture_valid_entries_are_unchanged) {
    // Two sheet slots; a scissor (-1), sprites on sheets 0 and 1, a full-screen
    // scissor (-2): every entry a valid capture can hold.
    const uint8_t raw[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
                             0x01, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0xFF, 0xFF};
    LoadedCapture cap(2, raw, 4);
    CHECK(!cap.c->DropEntriesNamingNoSheet());
    CHECK_EQ(cap.c->_entries._size, 4);
    CHECK_EQ(cap.c->_entries._data[1].sheetIndex, 0);
    CHECK_EQ(cap.c->_entries._data[2].sheetIndex, 1);

    // No sheets and only scissors is valid too.
    const uint8_t scissors[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF};
    LoadedCapture none(0, scissors, 2);
    CHECK(!none.c->DropEntriesNamingNoSheet());
    CHECK_EQ(none.c->_entries._size, 2);
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
    groups[0].animations   = nullptr;
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
//
// Map::Serialize refuses the load (System::Error's fatal box, then exit) when
// Map::CellsCoverSize is false, right after it reads the two ints. Serialize is not
// in the unit target (its load tail builds zones, the pathfinder and the road mesh)
// and the refusal exits the process, so these tests drive the predicate only.

UNIT_TEST(review_bounds_f026_map_size_larger_than_the_cell_array_is_refused) {
    // The head of Map::Serialize's record, inline: size 512 (00 02 00 00), then the
    // cell array's count 1 (01 00 00 00). Every load walk covers size * size cells.
    const uint8_t head[8] = {0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    int32_t size;
    int32_t count;
    std::memcpy(&size, head + 0, 4);
    std::memcpy(&count, head + 4, 4);
    CHECK(!Map::CellsCoverSize(size, count));

    // One cell short of a 512 map.
    CHECK(!Map::CellsCoverSize(512, 512 * 512 - 1));
    // An edge whose square leaves int32 (46341^2 > INT32_MAX) against a large count.
    CHECK(!Map::CellsCoverSize(46341, 0x7fffffff));
    // A negative edge.
    CHECK(!Map::CellsCoverSize(-4, 16));
    // Edge 0: the game divides cell indexes by the edge (action_base_cnr.cpp and others).
    CHECK(!Map::CellsCoverSize(0, 0));
}

UNIT_TEST(review_bounds_f026_map_valid_sizes_are_unchanged) {
    CHECK(Map::CellsCoverSize(512, 512 * 512));
    CHECK(Map::CellsCoverSize(384, 384 * 384));
    CHECK(Map::CellsCoverSize(256, 256 * 256));
    // A count above size * size is accepted: no walk reaches the extra cells.
    CHECK(Map::CellsCoverSize(4, 17));
}
