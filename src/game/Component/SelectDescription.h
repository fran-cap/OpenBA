#pragma once
// ============================================================================
// src/game/Component/SelectDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "SelectDescription", parent "Description", ti 0x1401c48a8
//   create 0x14000bb90: MemAlloc(0x48, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x14000bc00.
//   table 0x14015a5f0, 10 slots:
//     [0] 0x14000bcc0 scalar deleting dtor   [1] 0x14000bb80 TypeName   [2] 0x14000bb70 Type
//     [3] 0x1400556c0 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x140055630 Serialize (`MOV AL,1`)
//     [6] 0x14000bc60 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004950 GetComponentType: 0 (data-only: shared body 0x140004950 `MOV dword [RDX],0`)
//     [9] 0x14000bc00 Construct (folded with createInPlace)
//   dtor: ~String(+0x38), ~String(+0x28) — the members' reverse order, as retail 0x14000bcc0
//   sizeof 0x48.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/String.h"
namespace UI { class SpriteSheet; class StringTable; }   // B4 H4 (DLG-e)

class SelectDescription final : public Description {
    DECLARE_EXE_TYPE(SelectDescription)

public:
    SelectDescription();

    ~SelectDescription() override = default;                               // [0] 0x14000bcc0
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x1400556c0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140055630
    Object::External* Reconstruct() override;                              // [6] 0x14000bc60
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004950
    virtual Object::External* Construct();                                 // [9] 0x14000bc00

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    UI::SpriteSheet* _spriteSheet;                    // +0x20 ExternalPtr (a raw pointer)
    System::String _spriteName;                        // +0x28 String
    UI::StringTable* _stringTable;                    // +0x30 ExternalPtr (a raw pointer)
    System::String _stringName;                        // +0x38 String
    uint32_t _group;                              // +0x40 enum (serialized through the uint door)
    uint8_t _pad44[4];                          // +0x44 never written by the ctor
};

static_assert(sizeof(SelectDescription) == 0x48, "SelectDescription is 0x48 (create 0x14000bb90 MemAlloc(0x48))");
static_assert(offsetof(SelectDescription, _pad44) == 0x44, "SelectDescription::_pad44");
static_assert(alignof(SelectDescription) == 8, "SelectDescription alignment");
static_assert(offsetof(SelectDescription, _spriteSheet) == 0x20, "SelectDescription::_spriteSheet @ +0x20");
static_assert(offsetof(SelectDescription, _spriteName) == 0x28, "SelectDescription::_spriteName @ +0x28");
static_assert(offsetof(SelectDescription, _stringTable) == 0x30, "SelectDescription::_stringTable @ +0x30");
static_assert(offsetof(SelectDescription, _stringName) == 0x38, "SelectDescription::_stringName @ +0x38");
static_assert(offsetof(SelectDescription, _group) == 0x40, "SelectDescription::_group @ +0x40");
