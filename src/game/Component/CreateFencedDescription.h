#pragma once
// ============================================================================
// src/game/Component/CreateFencedDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreateFencedDescription", parent "Description", ti 0x1401c4100
//   create 0x1400048e0: MemAlloc(0x78, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140004920.
//   table 0x1401591a0, 10 slots:
//     [0] 0x140004960 scalar deleting dtor   [1] 0x1400048d0 TypeName   [2] 0x1400048c0 Type
//     [3] 0x1400479c0 GetFileVersion = 3   [4] External::Compile (import, inherited)
//     [5] 0x1400478f0 Serialize (`MOV AL,1`)
//     [6] 0x140004930 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x140004920 Construct (folded with createInPlace)
//   sizeof 0x78.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class CreateFencedDescription final : public Description {
    DECLARE_EXE_TYPE(CreateFencedDescription)

public:
    CreateFencedDescription();

    ~CreateFencedDescription() override = default;                         // [0] 0x140004960
    int32_t           GetFileVersion() const override { return 3; }    // [3] 0x1400479c0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x1400478f0
    Object::External* Reconstruct() override;                              // [6] 0x140004930
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x140004920

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    uint32_t _placeBits;                              // +0x20 enum (serialized through the uint door)
    uint8_t _pad24[4];                          // +0x24 never written by the ctor
    class ComponentDescription* _sections[6];       // +0x28 ExternalPtr[6], inline (B4 PLAYER H4: the section templates FencedTool::CalcPath clones, [0] the shelter)
    int32_t _maxWidth;                               // +0x58
    int32_t _maxHeight;                               // +0x5c
    int32_t _minWidth;                               // +0x60
    int32_t _minHeight;                               // +0x64
    bool _centerBuilding;                                  // +0x68
    uint8_t _pad69[7];                          // +0x69 never written by the ctor
    class ComponentDescription* _allowAndRemove;         // +0x70 ExternalPtr (a raw pointer; B4 PLAYER H4: the template whose entities FencedTool::CalcPath clears, 0x1401018d0)
};

static_assert(sizeof(CreateFencedDescription) == 0x78, "CreateFencedDescription is 0x78 (create 0x1400048e0 MemAlloc(0x78))");
static_assert(offsetof(CreateFencedDescription, _pad24) == 0x24, "CreateFencedDescription::_pad24");
static_assert(offsetof(CreateFencedDescription, _pad69) == 0x69, "CreateFencedDescription::_pad69");
static_assert(alignof(CreateFencedDescription) == 8, "CreateFencedDescription alignment");
static_assert(offsetof(CreateFencedDescription, _placeBits) == 0x20, "CreateFencedDescription::_placeBits @ +0x20");
static_assert(offsetof(CreateFencedDescription, _sections) == 0x28, "CreateFencedDescription::_sections @ +0x28");
static_assert(offsetof(CreateFencedDescription, _maxWidth) == 0x58, "CreateFencedDescription::_maxWidth @ +0x58");
static_assert(offsetof(CreateFencedDescription, _maxHeight) == 0x5c, "CreateFencedDescription::_maxHeight @ +0x5c");
static_assert(offsetof(CreateFencedDescription, _minWidth) == 0x60, "CreateFencedDescription::_minWidth @ +0x60");
static_assert(offsetof(CreateFencedDescription, _minHeight) == 0x64, "CreateFencedDescription::_minHeight @ +0x64");
static_assert(offsetof(CreateFencedDescription, _centerBuilding) == 0x68, "CreateFencedDescription::_centerBuilding @ +0x68");
static_assert(offsetof(CreateFencedDescription, _allowAndRemove) == 0x70, "CreateFencedDescription::_allowAndRemove @ +0x70");
