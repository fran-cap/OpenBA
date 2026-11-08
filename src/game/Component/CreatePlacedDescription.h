#pragma once
// ============================================================================
// src/game/Component/CreatePlacedDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CreatePlacedDescription", parent "Description", ti 0x1401c7468
//   create 0x140032a60: MemAlloc(0x58, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140032aa0.
//   table 0x140162cf0, 10 slots:
//     [0] 0x140032ad0 scalar deleting dtor   [1] 0x140032a50 TypeName   [2] 0x140032a40 Type
//     [3] 0x1401199f0 GetFileVersion = 4   [4] External::Compile (import, inherited)
//     [5] 0x1401198d0 Serialize (`MOV AL,1`)
//     [6] 0x140032ab0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x140032aa0 Construct (folded with createInPlace)
//   sizeof 0x58.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/String.h"
#include "collection/Collection.h"   // Collection::Array (_placeArray)

class CreatePlacedDescription final : public Description {
    DECLARE_EXE_TYPE(CreatePlacedDescription)

public:
    CreatePlacedDescription();

    ~CreatePlacedDescription() override;                                   // [0] 0x140032ad0
    int32_t           GetFileVersion() const override { return 4; }    // [3] 0x1401199f0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x1401198d0
    Object::External* Reconstruct() override;                              // [6] 0x140032ab0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x140032aa0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    uint32_t _category;                         // +0x20 enum: placement category
    uint8_t _pad24[4];                          // +0x24 never written by the ctor
    System::String _name;                       // +0x28 String (empty)
    // +0x30 enum[] `_placeBitArray` (size +0x38, capacity +0x3c). uint32_t's trait-derived
    // row is exe 0x1400021d0's (stride 4, align 4, tail as-found, no destroy).
    Collection::Array<uint32_t> _placeArray;
    int32_t _footprintRotation;                               // +0x40
    int32_t _width;                               // +0x44
    int32_t _height;                               // +0x48
    uint8_t _pad4c[4];                          // +0x4c never written by the ctor
    class ComponentDescription* _created;       // +0x50 ExternalRef: created description (weak; B4 PLAYER H4: the entity template PlacementTool places, 0x1400fd71d)
};

static_assert(sizeof(CreatePlacedDescription) == 0x58, "CreatePlacedDescription is 0x58 (create 0x140032a60 MemAlloc(0x58))");
static_assert(offsetof(CreatePlacedDescription, _pad24) == 0x24, "CreatePlacedDescription::_pad24");
static_assert(offsetof(CreatePlacedDescription, _pad4c) == 0x4C, "CreatePlacedDescription::_pad4c");
static_assert(alignof(CreatePlacedDescription) == 8, "CreatePlacedDescription alignment");
static_assert(offsetof(CreatePlacedDescription, _category) == 0x20, "CreatePlacedDescription::_category @ +0x20");
static_assert(offsetof(CreatePlacedDescription, _name) == 0x28, "CreatePlacedDescription::_name @ +0x28");
static_assert(offsetof(CreatePlacedDescription, _placeArray) == 0x30, "CreatePlacedDescription::_placeArray @ +0x30");
static_assert(offsetof(CreatePlacedDescription, _placeArray) + offsetof(Collection::Array<uint32_t>, _size) == 0x38, "CreatePlacedDescription::_placeArray size @ +0x38");
static_assert(offsetof(CreatePlacedDescription, _placeArray) + offsetof(Collection::Array<uint32_t>, _capacity) == 0x3c, "CreatePlacedDescription::_placeArray capacity @ +0x3c");
static_assert(offsetof(CreatePlacedDescription, _footprintRotation) == 0x40, "CreatePlacedDescription::_footprintRotation @ +0x40");
static_assert(offsetof(CreatePlacedDescription, _width) == 0x44, "CreatePlacedDescription::_width @ +0x44");
static_assert(offsetof(CreatePlacedDescription, _height) == 0x48, "CreatePlacedDescription::_height @ +0x48");
static_assert(offsetof(CreatePlacedDescription, _created) == 0x50, "CreatePlacedDescription::_created @ +0x50");
