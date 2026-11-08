// src/game/Game/terraingenerator.cpp
// Clean reimplementation of TerrainGenerator game logic.
//
// TerrainGenerator (0x90 bytes) -- terrain height/noise generation parameters.
// Extends Object::External (0x20 base). Vtable at 0x14015f170.
//
// ENDGAME B.3.8 block LOOP: a REAL class (src/game/Game/TerrainGenerator.h,
// field layout and retail slot map there) with a compiler-generated vtable.
// Gone: the 11-slot `g_TerrainGenerator_Vtable_Standalone` (two over-read
// guards past retail's 9), its trampolines and stubs, the lazy vtable resolve
// in the old Init, the lazily-resolved door pointers + EnsureInit (a non-atomic
// first-call cache reached on the LOADER thread, B6-5) and the C entry points.
//
// Contains ExternalPtr, 3 noise layer filename strings (each with a -1 sentinel int),
// and a dynamic array of terrain data entries (0x44 bytes each).
//
// Field layout (from constructor + serialize + dtor):
//   +0x00: vtable
//   +0x08: External base (0x18 bytes)
//   +0x20: ExternalPtr (8 bytes)
//   +0x28: System::String noiseFile1 (8 bytes)
//   +0x30: bool exposed (default 1)
//   +0x31: pad (7 bytes)
//   +0x38: System::String noiseFile2 (8 bytes)
//   +0x40: int32_t sentinel1 (default -1)
//   +0x44: pad (4 bytes)
//   +0x48: void* field48 (default 0)
//   +0x50: System::String noiseFile3 (8 bytes)
//   +0x58: int32_t sentinel2 (default -1)
//   +0x5C: pad (4 bytes)
//   +0x60: void* field60 (default 0)
//   +0x68: System::String noiseFile4 (8 bytes)
//   +0x70: int32_t sentinel3 (default -1)
//   +0x74: pad (4 bytes)
//   +0x78: void* field78 (default 0)
//   +0x80: void* terrainDataArray (dynamic array ptr, freed in dtor)
//   +0x88: void* terrainDataInfo (count/capacity)
//
// Decompiled from Application-x64.exe.

#include "Game/TerrainGenerator.h"
#include "Game/TerrainHeightfield.h"   // TerrainSizeRow
#include "object/Internal.h"   // Object::detail::InternalMemAlloc / InternalMemFree
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/Filename.h"
#include "system/String.h"
#include "collection/Collection.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include <cstdint>
#include <cstring>
#include <new>
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

// FUN_14001fd10 -- resolve a deserialized noise-layer filename String to its
// External. Registered at that address to this body (standalone/reg_game_leaf.cpp);
// called directly now, as are the two array helpers below (0x1400206b0 /
// 0x140020870), whose registered bodies live in this file.
void Game_TerrainResolveMaterial(MapGenerator::ExternalRef* ref);   // game_leaf_helpers.cpp

// ============================================================
// Standalone reimplementations of the +0x80 terrain-data-array helpers.
// In standalone these exe addresses aren't mapped, so ResolveAddr returns
// null and the size table never deserializes (gen+0x80 stays null -> worldgen
// reads dim=0). These faithful reimpls (decompiled from FUN_1400206b0 /
// FUN_140020870) populate the size table so the in-game terrain build works.
// Each row is a SizeRow: 17 ints, stride 0x44; row[0] = map dimension.
// ============================================================

// FUN_1400206b0 -- stride-0x44 dynamic-array resize. New rows get the engine
// default SizeRow template (row[0]=256, etc.).
//
// ENDGAME B.3.2 step 2: the body is now `Collection::Array<>::SetSize` over the
// `kSpec_TerrainSizeRow` row. Compared arm for arm against rtcoll::SetSizeCore
// and the retail body 0x1400206b0 (re-decompiled 2026-09-13): exact-equal early
// return, `_size` first, treble-on-single-append-from-non-empty, cap*0x44 at
// align 4, null element address skipped, 17-dword copy of the prefix only when
// the block moved, default-row init for the tail, old block freed only when it
// moved, no destroy. Every arm matched, nothing changed.
namespace {
void AssignTerrainSizeRow(uint8_t* d, const uint8_t* s) {
    std::memcpy(d, s, 0x44);                         // 17 dwords, the whole stride
}
void InitTerrainSizeRow(uint8_t* d) {
    static const int32_t kDefaultRow[17] = {
        0x100, 0x32, 10, 0x14, 0, 10, 0x14, 3, 1, 1000000, 0,
        1, 10, 0x14, 0x23, 6, 0xc
    };
    std::memcpy(d, kDefaultRow, 0x44);
}
// stays explicit: `init` copies the 17-dword default SizeRow template, a
// patterned fill no element type's value-initialisation can produce.
}  // namespace
const rtcoll::ArraySpec TerrainGeneratorRows::kSpec_TerrainSizeRow = {     // exe 0x1400206b0
    "Array<SizeRow>", 0x44, 4,
    &AssignTerrainSizeRow, &InitTerrainSizeRow, nullptr, /*byteCopy*/true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };

// FUN_140020870 -- serialize `count` rows of 17 ints each. NOTE row[9]
// (+0x24, erosionIterations) is serialized LAST (after row[16]) -- this field
// order must match the .rsc write order exactly or the stream misaligns.
static void Reimpl_TerrainDataSerialize(IO::Serializer* serializer, uint32_t count, TerrainSizeRow* rows) {
    if (static_cast<int>(count) <= 0 || !rows) return;
    TerrainSizeRow* row = rows;
    for (uint32_t r = 0; r < count; ++r) {
        serializer->Serialize(row->mapSize, nullptr);             // [0]
        serializer->Serialize(row->riverCount2, nullptr);         // [1]
        serializer->Serialize(row->riverMinEdge2, nullptr);       // [2]
        serializer->Serialize(row->riverMaxEdge2, nullptr);       // [3]
        serializer->Serialize(row->riverCount1, nullptr);         // [4]
        serializer->Serialize(row->riverMinEdge1, nullptr);       // [5]
        serializer->Serialize(row->riverMaxEdge1, nullptr);       // [6]
        serializer->Serialize(row->flatAreaConnections, nullptr);          // [7]
        serializer->Serialize(row->secondaryPathCount, nullptr);  // [8]
        serializer->Serialize(row->minRangeCount, nullptr);       // [10]
        serializer->Serialize(row->maxRangeCount, nullptr);       // [11]
        serializer->Serialize(row->minRangeBand, nullptr);        // [12]
        serializer->Serialize(row->maxRangeBand, nullptr);        // [13]
        serializer->Serialize(row->basinsPerRange, nullptr);      // [14]
        serializer->Serialize(row->basinRadiusMax, nullptr);      // [15]
        serializer->Serialize(row->basinRadiusMin, nullptr);      // [16]
        serializer->Serialize(row->erosionIterations, nullptr);   // [9]
        row += 1;
    }
}

// ============================================================
// Helper: Get serializer version
// ============================================================
static inline int GetSerializerVersion(IO::Serializer* serializer) {
    return static_cast<int>(serializer->GetDirection());   // IO::Serializer::GetDirection, retail 0x180006210
}

// ============================================================
// Helper: Serialize a filename string with version-0 resolution
// ============================================================
// Pattern from TerrainGenerator Slot5: Filename ctor, String::Serialize,
// if version==0 resolve filename (FUN_14001fd10), Filename dtor.
static void SerializeFilenameString(MapGenerator::ExternalRef* ref, IO::Serializer* serializer) {
    System::Filename filenameObj;   // retail's scope temp; only ctor + dtor
    (void)filenameObj;

    ref->name.Serialize(*serializer);

    if (GetSerializerVersion(serializer) == 0) {
        Game_TerrainResolveMaterial(ref);
    }
}

// ============================================================
// TerrainGenerator::TerrainGenerator (0x1400b1810)
// ============================================================
// External() and the 0x14015f170 stamp are the compiler's; then every member in
// declaration order, as retail builds them (B4 S4 RESOLVE): +0x20, the String
// +0x28, +0x30, the three NamedExternalRef noise layers (String() qword 0,
// generation -1, resolved 0: retail 0x1400b184e / 0x1400b1865 / 0x1400b187c),
// then the +0x80 size table's Array() (one qword zero each; H7).
TerrainGenerator::TerrainGenerator()
    : stringTable(nullptr),         // ExternalPtr
      exposed(true) {}          // +0x30

// ============================================================
// TerrainGenerator::~TerrainGenerator / DeletingDtor (vtable +0x00)
// ============================================================
// Original: 0x140020600 -> body FUN_140020630: frees the terrain data array
// at +0x80, destroys the 4 Strings at +0x68, +0x50, +0x38, +0x28; then
// External::~External (the compiler's chain) and MemFree if flags & 1.
TerrainGenerator::~TerrainGenerator() {
    // The members are destroyed after this empty body in reverse order: the
    // +0x80 SizeRow array's ~Array (MemFree the block, zero data and the
    // count/capacity qword; retail 0x140020646..0x14002065c), then the Strings
    // +0x68, +0x50, +0x38, +0x28 (retail 0x140020663..0x140020697).
}

// ============================================================
// TerrainGenerator::Reconstruct (vtable +0x30, Slot6)
// ============================================================
// Original: 0x1400205e0. Destroys the object (slot 0, flags 0: no free) then
// re-constructs in place.
Object::External* TerrainGenerator::Reconstruct() {
    this->~TerrainGenerator();
    return ::new (static_cast<void*>(this)) TerrainGenerator();
}

// [7] 0x140001640 — the shared body `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]`:
// slot 0 with flags 0 (destroy, no free).
void TerrainGenerator::Destruct() { this->~TerrainGenerator(); }

// ============================================================
// TerrainGenerator::Serialize (vtable +0x28, Slot5)
// ============================================================
// Original: 0x1400b1690. ExternalPtr, String, bool, 3 filename strings with
// version-0 resolution, and the dynamic terrain data array. Returns AL=1.
bool TerrainGenerator::Serialize(IO::Stream& stream) {
    IO::Serializer s(stream, 0);
    IO::Serializer* serializer = &s;   // destroyed at the return (retail's order)

    // Serialize ExternalPtr at +0x20
    ::Object::SerializeExternal<UI::StringTable>(*serializer, stringTable);

    // Serialize String at +0x28
    text.Serialize(*serializer);

    // Serialize bool at +0x30
    s.Serialize(exposed, nullptr);

    // 3 filename strings with the version-0 resolution: +0x38, +0x50, +0x68
    SerializeFilenameString(&terrainMaterial, serializer);
    SerializeFilenameString(&waterMaterialA, serializer);
    SerializeFilenameString(&waterMaterialB, serializer);

    // Terrain data array count: read the current count from +0x88, serialize
    // it, resize when reading (FUN_1400206b0), then the rows (FUN_140020870).
    int32_t count = sizeRows._size;
    s.Serialize(count, nullptr);   // the int overload

    if (GetSerializerVersion(serializer) == 0)
        sizeRows.SetSize(count);   // FUN_1400206b0

    Reimpl_TerrainDataSerialize(serializer, static_cast<uint32_t>(count), sizeRows._data);

    return true;   // ~Serializer
}

// ============================================================
// TerrainGenerator::Construct (vtable +0x40)
// ============================================================
// Original: 0x1400205d0 — placement-new of the class on `this` (MSVC's
// placement new null-checks `this`, as the old slot did).
Object::External* TerrainGenerator::Construct() {
    return ::new (static_cast<void*>(this)) TerrainGenerator();
}

// ============================================================
// Reflection: create / createInPlace, the registration
// ============================================================
// TerrainGenerator is loaded as each element of Game/Terrain/TerrainList.rsc
// (Valleys/Mountains .rsc). Without the reflected type ExternalFactory::
// FindOrLoad's per-element load hits a NullFactory and crashes in Runtime
// IsDerivedFrom. GetFileVersion MUST return 4: Object::External::Load gates
// the element Serialize on `fileVersion == obj->GetFileVersion()`.
::Object::Base* TerrainGenerator::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TerrainGenerator), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TerrainGenerator());
}
::Object::Base* TerrainGenerator::CreateInPlaceObject(void* memory) {
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) TerrainGenerator());
}

DEFINE_EXE_TYPE(TerrainGenerator, "Object::External")
