#pragma once
// ============================================================================
// src/game/Component/CreateRoadDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreateRoadDescription", parent "Description", ti 0x1401c74d8
//   create 0x140032d30: MemAlloc(0x30, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140032da0.
//   table 0x140162df0, 10 slots:
//     [0] 0x140032e50 scalar deleting dtor   [1] 0x140032d20 TypeName   [2] 0x140032d10 Type
//     [3] 0x140119ce0 GetFileVersion = 2   [4] External::Compile (import, inherited)
//     [5] 0x140119c80 Serialize (`MOV AL,1`)
//     [6] 0x140032df0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x140032da0 Construct (folded with createInPlace)
//   sizeof 0x30.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class CreateRoadDescription final : public Description {
    DECLARE_EXE_TYPE(CreateRoadDescription)

public:
    CreateRoadDescription();

    ~CreateRoadDescription() override = default;                           // [0] 0x140032e50
    int32_t           GetFileVersion() const override { return 2; }    // [3] 0x140119ce0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140119c80
    Object::External* Reconstruct() override;                              // [6] 0x140032df0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x140032da0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    bool _index;                                  // +0x20
    uint8_t _pad21[7];                          // +0x21 never written by the ctor
    class ComponentDescription* _created;       // +0x28 ExternalPtr (created-resource description: the BuildRoad work's +0x50)
};

static_assert(sizeof(CreateRoadDescription) == 0x30, "CreateRoadDescription is 0x30 (create 0x140032d30 MemAlloc(0x30))");
static_assert(offsetof(CreateRoadDescription, _pad21) == 0x21, "CreateRoadDescription::_pad21");
static_assert(alignof(CreateRoadDescription) == 8, "CreateRoadDescription alignment");
static_assert(offsetof(CreateRoadDescription, _index) == 0x20, "CreateRoadDescription::_index @ +0x20");
static_assert(offsetof(CreateRoadDescription, _created) == 0x28, "CreateRoadDescription::_created @ +0x28");
