#pragma once
// ============================================================================
// src/game/Component/FishingDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "FishingDescription", parent "Description", ti 0x1401c7698
//   create 0x1400343c0: MemAlloc(0x30, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140034430.
//   table 0x1401632e0, 10 slots:
//     [0] 0x140034500 scalar deleting dtor   [1] 0x1400343b0 TypeName   [2] 0x1400343a0 Type
//     [3] 0x14011f200 GetFileVersion = 4   [4] External::Compile (import, inherited)
//     [5] 0x14011f170 Serialize (`MOV AL,1`)
//     [6] 0x140034490 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x1400344f0 GetComponentType: FishingComponent's TypeId
//     [9] 0x140034430 Construct (folded with createInPlace)
//   sizeof 0x30.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class ComponentDescription;

class FishingDescription final : public Description {
    DECLARE_EXE_TYPE(FishingDescription)

public:
    FishingDescription();

    ~FishingDescription() override = default;                              // [0] 0x140034500
    int32_t           GetFileVersion() const override { return 4; }    // [3] 0x14011f200
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14011f170
    Object::External* Reconstruct() override;                              // [6] 0x140034490
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x1400344f0
    virtual Object::External* Construct();                                 // [9] 0x140034430

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    ComponentDescription* _fish;                // +0x20 ExternalPtr (fish material: FishWork hands it to 0x1400beae0 as the material)
    uint32_t _resourceLimit;                              // +0x28 enum (serialized through the uint door)
    uint32_t _terrainMask;                      // +0x2c accepted terrain bits (cell +0x29), ProduceTick 0x14011ec10
};

static_assert(sizeof(FishingDescription) == 0x30, "FishingDescription is 0x30 (create 0x1400343c0 MemAlloc(0x30))");
static_assert(alignof(FishingDescription) == 8, "FishingDescription alignment");
static_assert(offsetof(FishingDescription, _fish) == 0x20, "FishingDescription::_fish @ +0x20");
static_assert(offsetof(FishingDescription, _resourceLimit) == 0x28, "FishingDescription::_resourceLimit @ +0x28");
static_assert(offsetof(FishingDescription, _terrainMask) == 0x2c, "FishingDescription::_terrainMask @ +0x2c");
