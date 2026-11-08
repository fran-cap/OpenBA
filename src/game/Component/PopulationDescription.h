#pragma once
// ============================================================================
// src/game/Component/PopulationDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "PopulationDescription", parent "Description", ti 0x1401c47c8
//   create 0x14000afb0: MemAlloc(0x58, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x14000aff0.
//   table 0x14015a3e0, 10 slots:
//     [0] 0x14000b030 scalar deleting dtor   [1] 0x14000afa0 TypeName   [2] 0x14000af90 Type
//     [3] 0x1400548e0 GetFileVersion = 3   [4] External::Compile (import, inherited)
//     [5] 0x1400547e0 Serialize (`MOV AL,1`)
//     [6] 0x14000b000 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x14000b020 GetComponentType: PopulationComponent's TypeId
//     [9] 0x14000aff0 Construct (folded with createInPlace)
//   +0x20 System::String: the member's own ctor/dtor are retail's inlined String() / ~String()
//   sizeof 0x58.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/String.h"

class ComponentDescription;   // Component/ComponentDescription.h

class PopulationDescription final : public Description {
    DECLARE_EXE_TYPE(PopulationDescription)

public:
    PopulationDescription();

    ~PopulationDescription() override = default;                           // [0] 0x14000b030
    int32_t           GetFileVersion() const override { return 3; }    // [3] 0x1400548e0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x1400547e0
    Object::External* Reconstruct() override;                              // [6] 0x14000b000
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x14000b020
    virtual Object::External* Construct();                                 // [9] 0x14000aff0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    System::String _name;                       // +0x20 String (empty)
    // Names from what PopulationComponent does with them (no strings in retail).
    int32_t _habitatBlock;                      // +0x28 map tiles per habitability cell edge (default 8)
    int32_t _herdBlock;                         // +0x2c map tiles per herd cell edge (default 0x20)
    uint32_t _habitatTerrain;                   // +0x30 terrain-type bits that count as habitat (uint door; default 0xe1f)
    uint32_t _blockingTerrain;                  // +0x34 terrain-type bits that make a cell uninhabitable (uint door)
    bool _blockedByEntity;                      // +0x38 a tile holding an entity makes the cell uninhabitable (default 1)
    uint8_t _pad39[3];                          // +0x39 never written by the ctor
    int32_t _habitatDensity;                    // +0x3c a habitable cell's density (default 4)
    int32_t _densityGrowth;                     // +0x40 added to every smoothed density per smoothing pass (default 1)
    int32_t _populationDecreasePerHarvest;                               // +0x44 (default 1)
    bool _rescan;                               // +0x48 Update rescans the habitability grid (default 0)
    uint8_t _pad49[7];                          // +0x49 never written by the ctor
    ComponentDescription* _herdTemplate;        // +0x50 ExternalPtr<ComponentDescription> (Serialize 0x1400548b2 -> 0x140002340)
};

static_assert(sizeof(PopulationDescription) == 0x58, "PopulationDescription is 0x58 (create 0x14000afb0 MemAlloc(0x58))");
static_assert(offsetof(PopulationDescription, _pad39) == 0x39, "PopulationDescription::_pad39");
static_assert(offsetof(PopulationDescription, _pad49) == 0x49, "PopulationDescription::_pad49");
static_assert(alignof(PopulationDescription) == 8, "PopulationDescription alignment");
static_assert(offsetof(PopulationDescription, _name) == 0x20, "PopulationDescription::_name @ +0x20");
static_assert(offsetof(PopulationDescription, _habitatBlock) == 0x28, "PopulationDescription::_habitatBlock @ +0x28");
static_assert(offsetof(PopulationDescription, _herdBlock) == 0x2c, "PopulationDescription::_herdBlock @ +0x2c");
static_assert(offsetof(PopulationDescription, _habitatTerrain) == 0x30, "PopulationDescription::_habitatTerrain @ +0x30");
static_assert(offsetof(PopulationDescription, _blockingTerrain) == 0x34, "PopulationDescription::_blockingTerrain @ +0x34");
static_assert(offsetof(PopulationDescription, _blockedByEntity) == 0x38, "PopulationDescription::_blockedByEntity @ +0x38");
static_assert(offsetof(PopulationDescription, _habitatDensity) == 0x3c, "PopulationDescription::_habitatDensity @ +0x3c");
static_assert(offsetof(PopulationDescription, _densityGrowth) == 0x40, "PopulationDescription::_densityGrowth @ +0x40");
static_assert(offsetof(PopulationDescription, _populationDecreasePerHarvest) == 0x44, "PopulationDescription::_populationDecreasePerHarvest @ +0x44");
static_assert(offsetof(PopulationDescription, _rescan) == 0x48, "PopulationDescription::_rescan @ +0x48");
static_assert(offsetof(PopulationDescription, _herdTemplate) == 0x50, "PopulationDescription::_herdTemplate @ +0x50");
