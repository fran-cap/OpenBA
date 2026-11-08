#pragma once
// ============================================================================
// src/game/Component/CreateRemovalDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreateRemovalDescription", parent "Description", ti 0x1401c74a0
//   create 0x140032b70: MemAlloc(0x48, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140032be0.
//   table 0x140162d80, 10 slots:
//     [0] 0x140032ca0 scalar deleting dtor   [1] 0x140032b60 TypeName   [2] 0x140032b50 Type
//     [3] 0x140119bc0 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x140119ac0 Serialize (`MOV AL,1`)
//     [6] 0x140032c40 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x140032be0 Construct (folded with createInPlace)
//   sizeof 0x48.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (_removeTypes)

class ComponentDescription;   // Component/ComponentDescription.h (the _removeTypes entries)

// _removeTypes grows through retail's stride-8 row (Serialize's 0x140001bb0 SetSize(n): stride 8,
// align 8, the new tail ZEROED, no element destroy) and is freed by the inlined ~Array
// (dtor 0x140032cc7: MemFree(+0x28), then +0x28 = +0x30 = 0): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class CreateRemovalDescription final : public Description {
    DECLARE_EXE_TYPE(CreateRemovalDescription)

public:
    CreateRemovalDescription();

    ~CreateRemovalDescription() override;                                  // [0] 0x140032ca0
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x140119bc0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140119ac0
    Object::External* Reconstruct() override;                              // [6] 0x140032c40
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x140032be0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    bool _removeResources;                                  // +0x20
    uint8_t _pad21[7];                          // +0x21 never written by the ctor
    // +0x28 Array<ExternalPtr<ComponentDescription>> (count +0x30, capacity +0x34; entries are
    // non-owning; B4 PLAYER H4: the field templates RemovalTool compares entity descriptions with)
    using RefList = Collection::Array<ComponentDescription*, Collection::DefaultAllocator,
                                      Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    RefList _removeTypes;                              // +0x28
    class ComponentDescription* _remover;         // +0x38 ExternalPtr (B4 PLAYER H4: the template RemovalTool compares entity descriptions with, 0x140104670)
    bool _removeRoads;                                  // +0x40
    bool _removeBuildings;                                  // +0x41
    bool _cancelClear;                                  // +0x42
    bool _increasePriority;                                  // +0x43
    uint8_t _pad44[4];                          // +0x44 never written by the ctor
};

static_assert(sizeof(CreateRemovalDescription) == 0x48, "CreateRemovalDescription is 0x48 (create 0x140032b70 MemAlloc(0x48))");
static_assert(offsetof(CreateRemovalDescription, _pad21) == 0x21, "CreateRemovalDescription::_pad21");
static_assert(offsetof(CreateRemovalDescription, _pad44) == 0x44, "CreateRemovalDescription::_pad44");
static_assert(alignof(CreateRemovalDescription) == 8, "CreateRemovalDescription alignment");
static_assert(offsetof(CreateRemovalDescription, _removeResources) == 0x20, "CreateRemovalDescription::_removeResources @ +0x20");
static_assert(offsetof(CreateRemovalDescription, _removeTypes) == 0x28, "CreateRemovalDescription::_removeTypes @ +0x28");
static_assert(offsetof(CreateRemovalDescription, _removeTypes) + offsetof(CreateRemovalDescription::RefList, _size) == 0x30, "CreateRemovalDescription::_removeTypes count @ +0x30");
static_assert(offsetof(CreateRemovalDescription, _removeTypes) + offsetof(CreateRemovalDescription::RefList, _capacity) == 0x34, "CreateRemovalDescription::_removeTypes capacity @ +0x34");
static_assert(offsetof(CreateRemovalDescription, _remover) == 0x38, "CreateRemovalDescription::_remover @ +0x38");
static_assert(offsetof(CreateRemovalDescription, _removeRoads) == 0x40, "CreateRemovalDescription::_removeRoads @ +0x40");
static_assert(offsetof(CreateRemovalDescription, _removeBuildings) == 0x41, "CreateRemovalDescription::_removeBuildings @ +0x41");
static_assert(offsetof(CreateRemovalDescription, _cancelClear) == 0x42, "CreateRemovalDescription::_cancelClear @ +0x42");
static_assert(offsetof(CreateRemovalDescription, _increasePriority) == 0x43, "CreateRemovalDescription::_increasePriority @ +0x43");
