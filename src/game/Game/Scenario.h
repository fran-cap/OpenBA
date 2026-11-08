#pragma once
// ============================================================================
// src/game/Game/Scenario.h — ENDGAME B.3.8 block GOAL (B38_RULES D1-D5, D8)
//
// A game scenario: an Achievement::Achievement (title, goals) plus the map /
// terrain / climate generators and the save name it starts with.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "Scenario", parent "::Achievement::Achievement" (leading "::",
//   exactly retail's literal), ti 0x1401c5b08
//   create 0x14001e9f0 (MemAlloc(0x90, 8)), createInPlace 0x14001ea70
//   ctor 0x1400a9480, copy ctor 0x1400a94d0
//   table 0x14015f020, 9 slots (the tree's 13 ran into the zero qword and
//   past it; B38_RULES D3.5):
//     [0] 0x14001eb40 scalar deleting dtor: stamp, ~String +0x88, ~String +0x58,
//         ~Achievement, MemFree on &1
//     [1] TypeName  [2] Type
//     [3] 0x1400a9460 GetFileVersion: Achievement's | 0x600
//     [4] Object::External::Compile (import) — inherited
//     [5] 0x1400a9370 Serialize
//     [6] 0x14001ead0 Reconstruct: this->~T(), then the ctor on `this`
//     [7] 0x140001640 Destruct: this->~T()
//     [8] 0x14001ea70 Construct (Achievement's slot 8; folded with createInPlace)
//   sizeof 0x90.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "achievement/AchievementManager.h"
#include "Object/exe_type.h"
#include "system/String.h"

class MapGenerator;
class TerrainGenerator;
class ClimateGenerator;

class Scenario final : public Achievement::Achievement {
    DECLARE_EXE_TYPE(Scenario)

public:
    Scenario();                                                    // 0x1400a9480

    ~Scenario() override;                                          // [0] 0x14001eb40
    int32_t           GetFileVersion() const override;             // [3] 0x1400a9460
    bool              Serialize(IO::Stream& stream) override;            // [5] 0x1400a9370
    Object::External* Reconstruct() override;                      // [6] 0x14001ead0
    void              Destruct() override;                         // [7] 0x140001640
    Object::External* Construct() override;                        // [8] 0x14001ea70

public:
    System::String scenarioName; // +0x58 (ctor 0x1400a9480 stores 0 = String(); dtor 0x14001eb40 runs ~String on +0x88 then +0x58, the compiler's order)
    int32_t  randomSeed;           // +0x60: int, serialized
    int32_t  terrainSize;           // +0x64: int, serialized
    bool     disasters;           // +0x68: bool, serialized (default true)
    bool     tutorial;           // +0x69: bool, serialized (default false)
    uint8_t  _pad6A[6];          // +0x6A: padding
    MapGenerator*     mapGenerator;      // +0x70: ExternalPtr<MapGenerator>, serialized
    TerrainGenerator* terrainGenerator;  // +0x78: ExternalPtr<TerrainGenerator>, serialized
    ClimateGenerator* climateGenerator;  // +0x80: ExternalPtr<ClimateGenerator>, serialized
    System::String saveGame;     // +0x88: the save name
};

static_assert(offsetof(Scenario, scenarioName) == 0x58, "Scenario +0x58 name");
static_assert(offsetof(Scenario, randomSeed) == 0x60, "Scenario +0x60");
static_assert(offsetof(Scenario, terrainSize) == 0x64, "Scenario +0x64");
static_assert(offsetof(Scenario, disasters) == 0x68, "Scenario +0x68");
static_assert(offsetof(Scenario, tutorial) == 0x69, "Scenario +0x69");
static_assert(offsetof(Scenario, mapGenerator) == 0x70, "Scenario +0x70");
static_assert(offsetof(Scenario, terrainGenerator) == 0x78, "Scenario +0x78");
static_assert(offsetof(Scenario, climateGenerator) == 0x80, "Scenario +0x80");
static_assert(offsetof(Scenario, saveGame) == 0x88, "Scenario +0x88");
static_assert(sizeof(Scenario) == 0x90, "Scenario is 0x90 (create 0x14001e9f0 MemAlloc(0x90))");
static_assert(offsetof(Scenario, _pad6A) == 0x6A, "Scenario::_pad6A");
static_assert(alignof(Scenario) == 8, "Scenario is qword-aligned");
