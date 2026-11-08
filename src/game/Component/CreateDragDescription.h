#pragma once
// ============================================================================
// src/game/Component/CreateDragDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreateDragDescription", parent "Description", ti 0x1401c7430
//   create 0x140032970: MemAlloc(0x40, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x1400329b0.
//   table 0x140162ca0, 10 slots:
//     [0] 0x1400329e0 scalar deleting dtor   [1] 0x140032960 TypeName   [2] 0x140032950 Type
//     [3] 0x140119810 GetFileVersion = 2   [4] External::Compile (import, inherited)
//     [5] 0x140119750 Serialize (`MOV AL,1`)
//     [6] 0x1400329c0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x1400329b0 Construct (folded with createInPlace)
//   sizeof 0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class CreateDragDescription final : public Description {
    DECLARE_EXE_TYPE(CreateDragDescription)

public:
    CreateDragDescription();

    ~CreateDragDescription() override = default;                           // [0] 0x1400329e0
    int32_t           GetFileVersion() const override { return 2; }    // [3] 0x140119810
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140119750
    Object::External* Reconstruct() override;                              // [6] 0x1400329c0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x1400329b0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    uint32_t _placeBits;                            // +0x20 enum: drag axis/shape
    bool _ignoreZones;                                  // +0x24
    uint8_t _pad25[3];                          // +0x25 never written by the ctor
    int32_t _maxWidth;                               // +0x28
    int32_t _maxHeight;                               // +0x2c
    int32_t _minWidth;                               // +0x30
    int32_t _minHeight;                               // +0x34
    class ComponentDescription* _allowAndRemove;       // +0x38 ExternalRef: created-resource description (weak; B4 PLAYER H4: the entity template DragPlacementTool / FencedTool place)
};

static_assert(sizeof(CreateDragDescription) == 0x40, "CreateDragDescription is 0x40 (create 0x140032970 MemAlloc(0x40))");
static_assert(offsetof(CreateDragDescription, _pad25) == 0x25, "CreateDragDescription::_pad25");
static_assert(alignof(CreateDragDescription) == 8, "CreateDragDescription alignment");
static_assert(offsetof(CreateDragDescription, _placeBits) == 0x20, "CreateDragDescription::_placeBits @ +0x20");
static_assert(offsetof(CreateDragDescription, _ignoreZones) == 0x24, "CreateDragDescription::_ignoreZones @ +0x24");
static_assert(offsetof(CreateDragDescription, _maxWidth) == 0x28, "CreateDragDescription::_maxWidth @ +0x28");
static_assert(offsetof(CreateDragDescription, _maxHeight) == 0x2c, "CreateDragDescription::_maxHeight @ +0x2c");
static_assert(offsetof(CreateDragDescription, _minWidth) == 0x30, "CreateDragDescription::_minWidth @ +0x30");
static_assert(offsetof(CreateDragDescription, _minHeight) == 0x34, "CreateDragDescription::_minHeight @ +0x34");
static_assert(offsetof(CreateDragDescription, _allowAndRemove) == 0x38, "CreateDragDescription::_allowAndRemove @ +0x38");
