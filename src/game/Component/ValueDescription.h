#pragma once
// ============================================================================
// src/game/Component/ValueDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "ValueDescription", parent "Description", ti 0x1401c4bf0
//   create 0x14000faa0: MemAlloc(0x50, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x14000fb10.
//   table 0x14015add0, 10 slots:
//     [0] 0x14000fbf0 scalar deleting dtor   [1] 0x14000fa90 TypeName   [2] 0x14000fa80 Type
//     [3] 0x14005ce10 GetFileVersion = 3   [4] External::Compile (import, inherited)
//     [5] 0x14005cd60 Serialize (`MOV AL,1`)
//     [6] 0x14000fb80 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x14000fb10 Construct (folded with createInPlace)
//   sizeof 0x50.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/String.h"
namespace UI { class SpriteSheet; class StringTable; }   // B4 H4 (DLG-e)

class ValueDescription final : public Description {
    DECLARE_EXE_TYPE(ValueDescription)

public:
    ValueDescription();

    ~ValueDescription() override = default;                                // [0] 0x14000fbf0
    int32_t           GetFileVersion() const override { return 3; }    // [3] 0x14005ce10
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14005cd60
    Object::External* Reconstruct() override;                              // [6] 0x14000fb80
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x14000fb10

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    UI::SpriteSheet* _sprite;                   // +0x20 ExternalPtr (sprite/icon)
    System::String _name;                       // +0x28 String
    UI::StringTable* _stringTable;                    // +0x30 ExternalPtr (a raw pointer)
    System::String _stringName;                        // +0x38 String
    int32_t _baseValue;                               // +0x40
    int32_t _stackCount;                         // +0x44
    int32_t _minStackCount;                            // +0x48
    uint8_t _pad4c[4];                          // +0x4c never written by the ctor
};

static_assert(sizeof(ValueDescription) == 0x50, "ValueDescription is 0x50 (create 0x14000faa0 MemAlloc(0x50))");
static_assert(offsetof(ValueDescription, _pad4c) == 0x4C, "ValueDescription::_pad4c");
static_assert(alignof(ValueDescription) == 8, "ValueDescription alignment");
static_assert(offsetof(ValueDescription, _sprite) == 0x20, "ValueDescription::_sprite @ +0x20");
static_assert(offsetof(ValueDescription, _name) == 0x28, "ValueDescription::_name @ +0x28");
static_assert(offsetof(ValueDescription, _stringTable) == 0x30, "ValueDescription::_stringTable @ +0x30");
static_assert(offsetof(ValueDescription, _stringName) == 0x38, "ValueDescription::_stringName @ +0x38");
static_assert(offsetof(ValueDescription, _baseValue) == 0x40, "ValueDescription::_baseValue @ +0x40");
static_assert(offsetof(ValueDescription, _stackCount) == 0x44, "ValueDescription::_stackCount @ +0x44");
static_assert(offsetof(ValueDescription, _minStackCount) == 0x48, "ValueDescription::_minStackCount @ +0x48");
