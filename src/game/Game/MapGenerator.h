#pragma once
// ============================================================================
// src/game/Game/MapGenerator.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `MapGenerator` (0x178): world map generation parameters (a start condition:
// terrain settings, the initial families / structures / resource lists and the
// description-file refs), a real `Object::External` subclass. Layout from ctor
// 0x1400a3030 + Serialize 0x1400a2980 + dtor body 0x14001aea0.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md):
//   reflected "MapGenerator", parent "Object::External", table 0x14015ed80,
//   9 slots, sizeof 0x178.
//   [0] 0x14001ae70 scalar deleting dtor (body 0x14001aea0)
//   [1] 0x14001add0 TypeName               [2] 0x14001adc0 Type
//   [3] 0x1400a3020 GetFileVersion `MOV EAX,8`
//   [4] IMPORT Object::External::Compile   [5] 0x1400a2980 Serialize (AL=1)
//   [6] 0x14001ae30 Reconstruct            [7] 0x140001640 Destruct
//   [8] 0x14001ae20 Construct
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "Object/exe_type.h"
#include "system/String.h"
#include "collection/Collection.h"
#include "Object/NamedExternalRef.h"   // MapGenerator::ExternalRef

namespace UI { class StringTable; }
class ComponentDescription;   // Component/ComponentDescription.h

// The element rows of the two String-bearing pools (mapgenerator.cpp): the grow
// bodies 0x14001b340 (0x18, ExternalRef) / 0x14001b4e0 (0x30, InitialStructure)
// and the destroy bodies 0x14001b030 / 0x14001b0a0 that retail's dtor 0x14001aea0
// runs per pool. B4 X7-GAME-A: the pools are `Collection::Array`s driven by them.
namespace MapGeneratorRows {
extern const rtcoll::ArraySpec kSpec_ExternalRef;        // exe 0x14001b340 / 0x14001b030
extern const rtcoll::ArraySpec kSpec_InitialStructure;   // exe 0x14001b4e0 / 0x14001b0a0
extern const rtcoll::ArraySpec kSpec_InitialFamily;      // exe 0x14001b270 (no destroy: the dtor is 0x14001af47's MemFree)
}  // namespace MapGeneratorRows

class MapGenerator final : public Object::External {
    DECLARE_EXE_TYPE(MapGenerator)

public:
    // An External reference stored as String + generation + resolved (0x18): the
    // one by-name record (Object/NamedExternalRef.h). B4 S4 RESOLVE: retail's
    // ctor 0x1400a3030 builds each one as a member, in member order (qword 0
    // String(), dword -1, qword 0: 0x1400a3080 / 0x1400a312f / 0x1400a3153 /
    // 0x1400a3177); its dtor (0x14001aea0, per ref) is ~String alone.
    using ExternalRef = NamedExternalRef;
    // One _citizens entry (stride 8, 0x14001b270; Serialize: bool, int).
    struct InitialFamily {
        bool     educated;        // +0x00 _educated
        uint8_t  _pad01[3];       // +0x01 never written by the resize's init
        int32_t  maxChildCount;   // +0x04 _maxChildCount
    };
    // One _structures entry (stride 0x30, 0x14001b4e0).
    struct InitialStructure {
        ExternalRef structure;    // +0x00 _structure
        int32_t  offsetX;         // +0x18 _offsetX
        int32_t  offsetY;         // +0x1C _offsetY
        int32_t  width;           // +0x20 _width (init -1)
        int32_t  height;          // +0x24 _height (init -1)
        int32_t  rotation;        // +0x28 _rotation
        uint8_t  _pad2C[4];       // +0x2C
    };

    // The two String-bearing pools (B4 X7-GAME-A): `Array<T>` over the hand-derived
    // rows above. Its ctor is the three zero stores retail's ctor 0x1400a3030
    // writes per pool; its dtor is ClearCallback (each element's ~String when the
    // count is positive, then MemFree + the three zero stores): 0x14001b030 /
    // 0x14001b0a0 step for step, the bodies DynArrayHeader's dtors called.
    using ExternalRefArray = Collection::Array<ExternalRef, Collection::DefaultAllocator,
        Collection::ExplicitSpec<&MapGeneratorRows::kSpec_ExternalRef>>;
    using InitialStructureArray = Collection::Array<InitialStructure, Collection::DefaultAllocator,
        Collection::ExplicitSpec<&MapGeneratorRows::kSpec_InitialStructure>>;
    // The InitialFamily pool (B6 row 470): `Array<T>` over its row; the ctor is the
    // zero stores retail's ctor 0x1400a3030 writes, the dtor retail's inline
    // 0x14001af47 (MemFree, then the header zeroed; since B6 row 104 ~Array tests _data first, as 0x18001c11f).
    using InitialFamilyArray = Collection::Array<InitialFamily, Collection::DefaultAllocator,
        Collection::ExplicitSpec<&MapGeneratorRows::kSpec_InitialFamily>>;

    MapGenerator();                                               // 0x1400a3030

    // body 0x14001aea0
    ~MapGenerator() override;                                     // [0] 0x14001ae70
    int32_t GetFileVersion() const override { return 8; }         // [3] 0x1400a3020
    // [4] Compile: inherited (retail's slot is the Object::External import)
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x1400a2980
    Object::External* Reconstruct() override;                     // [6] 0x14001ae30
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x14001ae20

public:
    UI::StringTable* stringTable;   // +0x20: ExternalPtr<StringTable> (_stringTable)
    System::String name;     // +0x28: _name (B.4 GAME-b: the ctor stored 0, the dtor ran ~String last)
    System::String description;     // +0x30: _description
    uint8_t  exposed;            // +0x38: bool, default 1 (_exposed)
    uint8_t  _pad39[3];          // +0x39: padding
    int32_t  startAreaSize;       // +0x3C: default 8 (_startAreaSize)
    float    citizenRadius;              // +0x40: default 2.0f (_citizenRadius)
    uint8_t  _pad44[4];          // +0x44: padding

    ExternalRef citizen;     // +0x48: _citizen template ref

    // +0x60: InitialFamily array (8-byte elements, raw MemAlloc/MemFree); the
    // count is retail's int32 at +0x68, the capacity +0x6C.
    InitialFamilyArray citizens;  // +0x60 _citizens {data, count +0x68, capacity +0x6C} (dtor 0x14001af47: MemFree, zero both qwords)

    ExternalRefArray      naturalResources;   // +0x70: ExternalRef (0x18) pool (dtor 0x14001af41 -> 0x14001b030)
    InitialStructureArray structures;   // +0x80: InitialStructure (0x30) pool (dtor 0x14001af37 -> 0x14001b0a0)
    ExternalRefArray      populations;   // +0x90: ExternalRef pool (dtor 0x14001af2a -> 0x14001b030)
    ExternalRefArray      initialVisibleList;   // +0xA0: ExternalRef pool (dtor 0x14001af1d -> 0x14001b030)
    ExternalRefArray      initialPlantList;   // +0xB0: ExternalRef pool (dtor 0x14001af10 -> 0x14001b030)
    ExternalRefArray      initialTreeList;   // +0xC0: ExternalRef pool (dtor 0x14001af03 -> 0x14001b030)
    ExternalRefArray      initialLivestockList;   // +0xD0: ExternalRef pool (dtor 0x14001aef6 -> 0x14001b030)

    // +0xE0: Scalar generation parameters
    int32_t  plantCount;            // +0xE0
    int32_t  treeCount;            // +0xE4
    int32_t  livestockCount;            // +0xE8
    float    livestockOffsetX;            // +0xEC
    float    livestockOffsetY;            // +0xF0
    float    livestockRadius;            // +0xF4
    int32_t  livestockGroupCount;            // +0xF8
    int32_t  initialWoodCount;            // +0xFC
    int32_t  initialStoneCount;           // +0x100
    int32_t  initialIronCount;           // +0x104
    int32_t  initialToolCount;           // +0x108
    int32_t  initialFoodCount;           // +0x10C
    int32_t  initialFuelCount;           // +0x110
    int32_t  initialClothingCount;           // +0x114

    ExternalRef riverAmbient;        // +0x118: terrain description ref
    float    riverAmbientSpacing;   // +0x130: default 6.0f
    uint8_t  _pad134[4];         // +0x134
    ExternalRef mapAmbient;        // +0x138: climate description ref
    int32_t  mapAmbientSpacing;          // +0x150: default 16
    uint8_t  _pad154[4];         // +0x154
    ExternalRef windAmbient;        // +0x158: vegetation description ref
    float    windAmbientHeight;           // +0x170: last serialized field
    uint8_t  _pad174[4];         // +0x174: padding to 0x178
    void SeedStartingResources();   // 0x1400a1f00 (Game/town_storage_seed.cpp)
    int SpawnStartingHerd(const float* centre, ComponentDescription* tmpl);   // 0x1400a0b10 (Component/pasturecomponent_spawn.cpp)
};

static_assert(sizeof(MapGenerator::ExternalRef) == 0x18, "ExternalRef is 0x18");
static_assert(offsetof(MapGenerator::ExternalRef, name) == 0x0, "ExternalRef::name");
static_assert(offsetof(MapGenerator::ExternalRef, _pad0C) == 0xC, "ExternalRef::_pad0C");
static_assert(sizeof(MapGenerator::ExternalRefArray) == 0x10, "pool header is 0x10");
static_assert(sizeof(MapGenerator::InitialStructureArray) == 0x10, "pool header is 0x10");
static_assert(sizeof(MapGenerator::ExternalRef) == 0x18, "ExternalRef stride 0x18 (0x14001b030)");
static_assert(sizeof(MapGenerator::InitialStructure) == 0x30, "InitialStructure stride 0x30 (0x14001b0a0)");
static_assert(offsetof(MapGenerator::InitialStructure, structure) == 0x0, "InitialStructure::structure");
static_assert(offsetof(MapGenerator::InitialStructure, offsetY) == 0x1C, "InitialStructure::offsetY");
static_assert(offsetof(MapGenerator::InitialStructure, width) == 0x20, "InitialStructure::width");
static_assert(offsetof(MapGenerator::InitialStructure, height) == 0x24, "InitialStructure::height");
static_assert(offsetof(MapGenerator::InitialStructure, _pad2C) == 0x2C, "InitialStructure::_pad2C");
static_assert(sizeof(MapGenerator) == 0x178, "MapGenerator is 0x178");
static_assert(offsetof(MapGenerator, _pad39) == 0x39, "MapGenerator::_pad39");
static_assert(offsetof(MapGenerator, _pad44) == 0x44, "MapGenerator::_pad44");
static_assert(offsetof(MapGenerator, treeCount) == 0xE4, "MapGenerator::treeCount");
static_assert(offsetof(MapGenerator, livestockCount) == 0xE8, "MapGenerator::livestockCount");
static_assert(offsetof(MapGenerator, livestockOffsetY) == 0xF0, "MapGenerator::livestockOffsetY");
static_assert(offsetof(MapGenerator, livestockRadius) == 0xF4, "MapGenerator::livestockRadius");
static_assert(offsetof(MapGenerator, livestockGroupCount) == 0xF8, "MapGenerator::livestockGroupCount");
static_assert(offsetof(MapGenerator, initialWoodCount) == 0xFC, "MapGenerator::initialWoodCount");
static_assert(offsetof(MapGenerator, initialStoneCount) == 0x100, "MapGenerator::initialStoneCount");
static_assert(offsetof(MapGenerator, initialIronCount) == 0x104, "MapGenerator::initialIronCount");
static_assert(offsetof(MapGenerator, initialToolCount) == 0x108, "MapGenerator::initialToolCount");
static_assert(offsetof(MapGenerator, initialFoodCount) == 0x10C, "MapGenerator::initialFoodCount");
static_assert(offsetof(MapGenerator, initialFuelCount) == 0x110, "MapGenerator::initialFuelCount");
static_assert(offsetof(MapGenerator, _pad134) == 0x134, "MapGenerator::_pad134");
static_assert(offsetof(MapGenerator, _pad154) == 0x154, "MapGenerator::_pad154");
static_assert(offsetof(MapGenerator, _pad174) == 0x174, "MapGenerator::_pad174");
static_assert(alignof(MapGenerator) == 8, "qword-aligned");
static_assert(offsetof(MapGenerator, stringTable) == 0x20, "+0x20");
static_assert(offsetof(MapGenerator, name) == 0x28, "+0x28");
static_assert(offsetof(MapGenerator, description) == 0x30, "+0x30");
static_assert(sizeof(MapGenerator::InitialFamily) == 8, "InitialFamily stride 8 (0x14001b270)");
static_assert(offsetof(MapGenerator::InitialFamily, educated) == 0x0, "InitialFamily::educated");
static_assert(offsetof(MapGenerator::InitialFamily, _pad01) == 0x1, "InitialFamily::_pad01");
static_assert(offsetof(MapGenerator::InitialFamily, maxChildCount) == 4, "InitialFamily +0x04");
static_assert(sizeof(MapGenerator::InitialStructure) == 0x30, "InitialStructure stride 0x30 (0x14001b4e0)");
static_assert(offsetof(MapGenerator::InitialStructure, offsetX) == 0x18, "InitialStructure +0x18");
static_assert(offsetof(MapGenerator::InitialStructure, rotation) == 0x28, "InitialStructure +0x28");
static_assert(offsetof(MapGenerator, exposed) == 0x38, "+0x38");
static_assert(offsetof(MapGenerator, startAreaSize) == 0x3C, "+0x3C");
static_assert(offsetof(MapGenerator, citizenRadius) == 0x40, "+0x40");
static_assert(offsetof(MapGenerator, citizen) == 0x48, "+0x48");
static_assert(offsetof(MapGenerator, citizens) == 0x60, "+0x60");
static_assert(offsetof(MapGenerator::InitialFamilyArray, _size) == 0x08, "+0x68 count");
static_assert(offsetof(MapGenerator::ExternalRef, generation) == 0x08, "ExternalRef +0x08");
static_assert(offsetof(MapGenerator::ExternalRef, resolved) == 0x10, "ExternalRef +0x10");

static_assert(offsetof(MapGenerator, naturalResources) == 0x70, "+0x70");
static_assert(offsetof(MapGenerator, structures) == 0x80, "+0x80");
static_assert(offsetof(MapGenerator, populations) == 0x90, "+0x90");
static_assert(offsetof(MapGenerator, initialVisibleList) == 0xA0, "+0xA0");
static_assert(offsetof(MapGenerator, initialPlantList) == 0xB0, "+0xB0");
static_assert(offsetof(MapGenerator, initialTreeList) == 0xC0, "+0xC0");
static_assert(offsetof(MapGenerator, initialLivestockList) == 0xD0, "+0xD0");
static_assert(offsetof(MapGenerator, plantCount) == 0xE0, "+0xE0");
static_assert(offsetof(MapGenerator, livestockOffsetX) == 0xEC, "+0xEC");
static_assert(offsetof(MapGenerator, initialClothingCount) == 0x114, "+0x114");
static_assert(offsetof(MapGenerator, riverAmbient) == 0x118, "+0x118");
static_assert(offsetof(MapGenerator, riverAmbientSpacing) == 0x130, "+0x130");
static_assert(offsetof(MapGenerator, mapAmbient) == 0x138, "+0x138");
static_assert(offsetof(MapGenerator, mapAmbientSpacing) == 0x150, "+0x150");
static_assert(offsetof(MapGenerator, windAmbient) == 0x158, "+0x158");
static_assert(offsetof(MapGenerator, windAmbientHeight) == 0x170, "+0x170");
