#pragma once
// ============================================================================
// src/game/Component/CemeteryDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CemeteryDescription", parent "Description", ti 0x1401c40c8
//   create 0x1400040b0: MemAlloc(0x28, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140004110.
//   table 0x140159130, 10 slots:
//     [0] 0x1400041c0 scalar deleting dtor   [1] 0x1400040a0 TypeName   [2] 0x140004090 Type
//     [3] 0x140047850 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x140047800 Serialize (`MOV AL,1`)
//     [6] 0x140004160 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x1400041b0 GetComponentType: CemeteryComponent's TypeId
//     [9] 0x140004110 Construct (folded with createInPlace)
//   sizeof 0x28.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class ComponentDescription;

class CemeteryDescription final : public Description {
    DECLARE_EXE_TYPE(CemeteryDescription)

public:
    CemeteryDescription();

    ~CemeteryDescription() override = default;                             // [0] 0x1400041c0
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x140047850
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140047800
    Object::External* Reconstruct() override;                              // [6] 0x140004160
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x1400041b0
    virtual Object::External* Construct();                                 // [9] 0x140004110

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    ComponentDescription* _tombstone;               // +0x20 ExternalPtr<ComponentDescription> (Serialize 0x140047800 -> 0x140002340,
                                                //       IsDerivedFrom ComponentDescription; the tombstone the burial builds)
};

static_assert(sizeof(CemeteryDescription) == 0x28, "CemeteryDescription is 0x28 (create 0x1400040b0 MemAlloc(0x28))");
static_assert(alignof(CemeteryDescription) == 8, "CemeteryDescription alignment");
static_assert(offsetof(CemeteryDescription, _tombstone) == 0x20, "CemeteryDescription::_tombstone @ +0x20");
