#pragma once
// ============================================================================
// src/game/Game/TerrainGenerator.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `TerrainGenerator` (0x90): terrain height/noise generation parameters, a real
// `Object::External` subclass. Layout from ctor 0x1400b1810 + Serialize
// 0x1400b1690 + dtor.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6):
//   reflected "TerrainGenerator", parent "Object::External", table
//   0x14015f170, 9 slots, sizeof 0x90.
//   [0] 0x140020600 scalar deleting dtor   [1] 0x140020580 TypeName
//   [2] 0x140020570 Type                   [3] 0x1400b1800 GetFileVersion `MOV EAX,4`
//   [4] IMPORT Object::External::Compile   [5] 0x1400b1690 Serialize (AL=1)
//   [6] 0x1400205e0 Reconstruct            [7] 0x140001640 Destruct
//   [8] 0x1400205d0 Construct
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "system/String.h"   // System::String members
#include "Object/exe_type.h"
#include "collection/Collection.h"
#include "Game/MapGenerator.h"   // MapGenerator::ExternalRef, the {String, generation, External*} record

namespace UI { class StringTable; }

struct TerrainSizeRow;   // Game/TerrainHeightfield.h

// The size table's element row: the grow body 0x1400206b0 (terraingenerator.cpp).
namespace TerrainGeneratorRows {
extern const rtcoll::ArraySpec kSpec_TerrainSizeRow;   // exe 0x1400206b0
}  // namespace TerrainGeneratorRows

class TerrainGenerator final : public Object::External {
    DECLARE_EXE_TYPE(TerrainGenerator)

public:
    TerrainGenerator();                                           // 0x1400b1810

    ~TerrainGenerator() override;                                 // [0] 0x140020600
    int32_t GetFileVersion() const override { return 4; }         // [3] 0x1400b1800
    // [4] Compile: inherited (retail's slot is the Object::External import)
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x1400b1690
    Object::External* Reconstruct() override;                     // [6] 0x1400205e0
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x1400205d0

public:
    UI::StringTable* stringTable;   // +0x20: ExternalPtr<StringTable> (serialized via FUN_14000bf40, as Map / Climate)
    System::String text;     // +0x28: String (ctor 0x1400b1810 stores 0; dtor 0x140020630 ~String)
    bool     exposed;           // +0x30: 1 (bool, enabled?; retail serializes it through Serialize(bool&))
    uint8_t  _pad31[7];          // +0x31: padding
    // The three material refs, each {String filename, int32 generation (-1 =
    // unresolved), External* resolved} (B4 S4 RESOLVE: the one NamedExternalRef;
    // the ctor 0x1400b1810 builds each as a member, 0 / -1 / 0, the dtor ~String
    // of each name).
    MapGenerator::ExternalRef terrainMaterial;   // +0x38: noise layer 1 (the terrain material)
    MapGenerator::ExternalRef waterMaterialA;   // +0x50: noise layer 2 (water material A)
    MapGenerator::ExternalRef waterMaterialB;   // +0x68: noise layer 3 (water material B)
    // +0x80: the size table (TerrainSizeRow, 0x44 per entry). B4 X7-GAME-A (H5):
    // an Array member; retail's dtor body 0x140020630 frees it FIRST (MemFree
    // [+0x80], zero +0x80 / +0x88) and then runs ~String on +0x68 / +0x50 / +0x38
    // / +0x28: exactly reverse member order with this as the last member.
    Collection::Array<TerrainSizeRow, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&TerrainGeneratorRows::kSpec_TerrainSizeRow>> sizeRows;
};

static_assert(sizeof(TerrainGenerator) == 0x90, "TerrainGenerator is 0x90");
static_assert(offsetof(TerrainGenerator, text) == 0x28, "TerrainGenerator::text");
static_assert(offsetof(TerrainGenerator, _pad31) == 0x31, "TerrainGenerator::_pad31");
static_assert(alignof(TerrainGenerator) == 8, "qword-aligned");
static_assert(offsetof(TerrainGenerator, stringTable) == 0x20, "+0x20");
static_assert(offsetof(TerrainGenerator, exposed) == 0x30, "+0x30");
static_assert(offsetof(TerrainGenerator, terrainMaterial) == 0x38, "+0x38");
static_assert(offsetof(TerrainGenerator, waterMaterialA) == 0x50, "+0x50");
static_assert(offsetof(TerrainGenerator, waterMaterialB) == 0x68, "+0x68");
static_assert(offsetof(TerrainGenerator, sizeRows) == 0x80, "+0x80");
static_assert(offsetof(TerrainGenerator, sizeRows) + offsetof(decltype(TerrainGenerator::sizeRows), _size) == 0x88, "+0x88 count");
