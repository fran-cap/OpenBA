#pragma once
// ============================================================================
// src/game/Component/ForesterDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "ForesterDescription", parent "Description", ti 0x1401c7778
//   create 0x1400348e0: MemAlloc(0x30, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140034950.
//   table 0x140163510, 10 slots:
//     [0] 0x140034a10 scalar deleting dtor   [1] 0x1400348d0 TypeName   [2] 0x1400348c0 Type
//     [3] 0x14011fb00 GetFileVersion = 2   [4] External::Compile (import, inherited)
//     [5] 0x14011fa80 Serialize (`MOV AL,1`)
//     [6] 0x1400349b0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140034a00 GetComponentType: ForesterComponent's TypeId
//     [9] 0x140034950 Construct (folded with createInPlace)
//   sizeof 0x30.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class ComponentDescription;

class ForesterDescription final : public Description {
    DECLARE_EXE_TYPE(ForesterDescription)

public:
    ForesterDescription();

    ~ForesterDescription() override = default;                             // [0] 0x140034a10
    int32_t           GetFileVersion() const override { return 2; }    // [3] 0x14011fb00
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14011fa80
    Object::External* Reconstruct() override;                              // [6] 0x1400349b0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140034a00
    virtual Object::External* Construct();                                 // [9] 0x140034950

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    ComponentDescription* _naturalResource;               // +0x20 ExternalPtr (tree/sapling material: ForesterWork scans its sub-descriptions, 0x14000c3c0)
    uint32_t _type;                             // +0x28 enum (serialized through the uint door)
    uint8_t _pad2c[4];                          // +0x2c never written by the ctor
};

static_assert(sizeof(ForesterDescription) == 0x30, "ForesterDescription is 0x30 (create 0x1400348e0 MemAlloc(0x30))");
static_assert(offsetof(ForesterDescription, _pad2c) == 0x2C, "ForesterDescription::_pad2c");
static_assert(alignof(ForesterDescription) == 8, "ForesterDescription alignment");
static_assert(offsetof(ForesterDescription, _naturalResource) == 0x20, "ForesterDescription::_naturalResource @ +0x20");
static_assert(offsetof(ForesterDescription, _type) == 0x28, "ForesterDescription::_type @ +0x28");
