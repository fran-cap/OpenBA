#pragma once
// ============================================================================
// src/game/Component/HerbalistDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "HerbalistDescription", parent "Description", ti 0x1401c43d8
//   create 0x140006bb0: MemAlloc(0x38, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140006c20.
//   table 0x140159910, 10 slots:
//     [0] 0x140006cf0 scalar deleting dtor   [1] 0x140006ba0 TypeName   [2] 0x140006b90 Type
//     [3] 0x14004b380 GetFileVersion = 2   [4] External::Compile (import, inherited)
//     [5] 0x14004b2e0 Serialize (`MOV AL,1`)
//     [6] 0x140006c80 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140006ce0 GetComponentType: HerbalistComponent's TypeId
//     [9] 0x140006c20 Construct (folded with createInPlace)
//   sizeof 0x38.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class ComponentDescription;   // Component/ComponentDescription.h (the _herbs element)

class HerbalistDescription final : public Description {
    DECLARE_EXE_TYPE(HerbalistDescription)

public:
    HerbalistDescription();

    ~HerbalistDescription() override;                                      // [0] 0x140006cf0
    int32_t           GetFileVersion() const override { return 2; }    // [3] 0x14004b380
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14004b2e0
    Object::External* Reconstruct() override;                              // [6] 0x140006c80
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140006ce0
    virtual Object::External* Construct();                                 // [9] 0x140006c20

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    ComponentDescription** _herbs;              // +0x20 Array<ExternalPtr<ComponentDescription>> data (herb material refs, 0x1400049c0)
    int32_t _herbCount;                         // +0x28
    int32_t _herbCap;                           // +0x2c
    uint32_t _resourceLimit;                              // +0x30 enum (serialized through the uint door)
    uint8_t _pad34[4];                          // +0x34 never written by the ctor
};

static_assert(sizeof(HerbalistDescription) == 0x38, "HerbalistDescription is 0x38 (create 0x140006bb0 MemAlloc(0x38))");
static_assert(offsetof(HerbalistDescription, _pad34) == 0x34, "HerbalistDescription::_pad34");
static_assert(alignof(HerbalistDescription) == 8, "HerbalistDescription alignment");
static_assert(offsetof(HerbalistDescription, _herbs) == 0x20, "HerbalistDescription::_herbs @ +0x20");
static_assert(offsetof(HerbalistDescription, _herbCount) == 0x28, "HerbalistDescription::_herbCount @ +0x28");
static_assert(offsetof(HerbalistDescription, _herbCap) == 0x2c, "HerbalistDescription::_herbCap @ +0x2c");
static_assert(offsetof(HerbalistDescription, _resourceLimit) == 0x30, "HerbalistDescription::_resourceLimit @ +0x30");
