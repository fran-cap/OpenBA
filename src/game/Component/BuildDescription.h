#pragma once
// ============================================================================
// src/game/Component/BuildDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "BuildDescription", parent "Description", ti 0x1401c7040
//   create 0x14002f320: MemAlloc(0x38, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x14002f390.
//   table 0x1401622f0, 10 slots:
//     [0] 0x14002f460 scalar deleting dtor   [1] 0x14002f310 TypeName   [2] 0x14002f300 Type
//     [3] 0x140109220 GetFileVersion = 2   [4] External::Compile (import, inherited)
//     [5] 0x140109140 Serialize (`MOV AL,1`)
//     [6] 0x14002f3f0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x14002f450 GetComponentType: BuildComponent's TypeId
//     [9] 0x14002f390 Construct (folded with createInPlace)
//   sizeof 0x38.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class ComponentDescription;   // Component/ComponentDescription.h

// One cost entry of the +0x20 array (0x10 stride). Serialize 0x140109140 reads
// the int +0x08, then the reference +0x00 through 0x140002340 (ExternalPtr,
// IsDerivedFrom ComponentDescription 0x1401c7318). No key names: the names say
// what retail does with them (HaveAllMaterials 0x140108630 needs `quantity` x
// the cost multiplier of `material` in the site's storage).
struct BuildCost {
    ComponentDescription* material;   // +0x00 the raw material's entity description
    int32_t               quantity;   // +0x08
    uint8_t               _pad0c[4];  // +0x0c
};
static_assert(sizeof(BuildCost) == 0x10, "BuildCost stride 0x10 (0x140109140)");
static_assert(offsetof(BuildCost, _pad0c) == 0xC, "BuildCost::_pad0c");
static_assert(offsetof(BuildCost, material) == 0x00, "BuildCost::material @ +0x00");
static_assert(offsetof(BuildCost, quantity) == 0x08, "BuildCost::quantity @ +0x08");

class BuildDescription final : public Description {
    DECLARE_EXE_TYPE(BuildDescription)

public:
    BuildDescription();

    ~BuildDescription() override;                                          // [0] 0x14002f460
    int32_t           GetFileVersion() const override { return 2; }    // [3] 0x140109220
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140109140
    Object::External* Reconstruct() override;                              // [6] 0x14002f3f0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x14002f450
    virtual Object::External* Construct();                                 // [9] 0x14002f390

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    BuildCost* _costs;                          // +0x20 cost array data (0x10 stride)
    int32_t _costCount;                         // +0x28 NOT initialised by the tree's ctor (only +0x20..+0x27 are)
    int32_t _costCap;                           // +0x2c idem
    int32_t _workRequired;                               // +0x30
    bool _scaledWithSize;                                  // +0x34
    uint8_t _pad35[3];                          // +0x35 never written by the ctor
};

static_assert(sizeof(BuildDescription) == 0x38, "BuildDescription is 0x38 (create 0x14002f320 MemAlloc(0x38))");
static_assert(offsetof(BuildDescription, _pad35) == 0x35, "BuildDescription::_pad35");
static_assert(alignof(BuildDescription) == 8, "BuildDescription alignment");
static_assert(offsetof(BuildDescription, _costs) == 0x20, "BuildDescription::_costs @ +0x20");
static_assert(offsetof(BuildDescription, _costCount) == 0x28, "BuildDescription::_costCount @ +0x28");
static_assert(offsetof(BuildDescription, _costCap) == 0x2c, "BuildDescription::_costCap @ +0x2c");
static_assert(offsetof(BuildDescription, _workRequired) == 0x30, "BuildDescription::_workRequired @ +0x30");
static_assert(offsetof(BuildDescription, _scaledWithSize) == 0x34, "BuildDescription::_scaledWithSize @ +0x34");
