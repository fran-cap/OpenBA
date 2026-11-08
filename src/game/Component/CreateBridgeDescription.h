#pragma once
// ============================================================================
// src/game/Component/CreateBridgeDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreateBridgeDescription", parent "Description", ti 0x1401c73f8
//   create 0x140032880: MemAlloc(0x60, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x1400328c0.
//   table 0x140162c30, 10 slots:
//     [0] 0x1400328f0 scalar deleting dtor   [1] 0x140032870 TypeName   [2] 0x140032860 Type
//     [3] 0x1401196a0 GetFileVersion = 2   [4] External::Compile (import, inherited)
//     [5] 0x1401195d0 Serialize (`MOV AL,1`)
//     [6] 0x1400328d0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x1400328c0 Construct (folded with createInPlace)
//   sizeof 0x60.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class CreateBridgeDescription final : public Description {
    DECLARE_EXE_TYPE(CreateBridgeDescription)

public:
    CreateBridgeDescription();

    ~CreateBridgeDescription() override = default;                         // [0] 0x1400328f0
    int32_t           GetFileVersion() const override { return 2; }    // [3] 0x1401196a0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x1401195d0
    Object::External* Reconstruct() override;                              // [6] 0x1400328d0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x1400328c0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    uint32_t _placeBits;                              // +0x20 enum (serialized through the uint door)
    uint32_t _invalidBits;                              // +0x24 enum (serialized through the uint door)
    uint32_t _startEndBits;                              // +0x28 enum (serialized through the uint door)
    uint32_t _requiredBits;                              // +0x2c enum (serialized through the uint door)
    uint32_t _edgeBits;                              // +0x30 enum (serialized through the uint door)
    int32_t _edgeWidth;                               // +0x34
    class ComponentDescription* _sections[5];       // +0x38 ExternalPtr[5], inline (B4 PLAYER H4: the segment templates BridgeTool::CalcPath clones: end piece, 1/2/4/8 cells)
};

static_assert(sizeof(CreateBridgeDescription) == 0x60, "CreateBridgeDescription is 0x60 (create 0x140032880 MemAlloc(0x60))");
static_assert(alignof(CreateBridgeDescription) == 8, "CreateBridgeDescription alignment");
static_assert(offsetof(CreateBridgeDescription, _placeBits) == 0x20, "CreateBridgeDescription::_placeBits @ +0x20");
static_assert(offsetof(CreateBridgeDescription, _invalidBits) == 0x24, "CreateBridgeDescription::_invalidBits @ +0x24");
static_assert(offsetof(CreateBridgeDescription, _startEndBits) == 0x28, "CreateBridgeDescription::_startEndBits @ +0x28");
static_assert(offsetof(CreateBridgeDescription, _requiredBits) == 0x2c, "CreateBridgeDescription::_requiredBits @ +0x2c");
static_assert(offsetof(CreateBridgeDescription, _edgeBits) == 0x30, "CreateBridgeDescription::_edgeBits @ +0x30");
static_assert(offsetof(CreateBridgeDescription, _edgeWidth) == 0x34, "CreateBridgeDescription::_edgeWidth @ +0x34");
static_assert(offsetof(CreateBridgeDescription, _sections) == 0x38, "CreateBridgeDescription::_sections @ +0x38");
