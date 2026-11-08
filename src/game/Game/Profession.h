#pragma once
// ============================================================================
// src/game/Game/Profession.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `Profession` (0x60): a citizen profession definition (name, description,
// priority, skill multipliers), a real `Object::External` subclass. Layout from
// ctor 0x1400a9030 + factory 0x14001e530.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md):
//   reflected "Profession", parent "Object::External", table 0x14015efa0,
//   9 slots, sizeof 0x60.
//   [0] 0x14001e6f0 scalar deleting dtor   [1] TypeName  [2] Type
//   [3] GetFileVersion `MOV EAX,3`         [4] IMPORT Object::External::Compile
//   [5] 0x1400a8f10 Serialize              [6] 0x14001e580 Reconstruct
//   [7] 0x140001640 Destruct               [8] 0x14001e570 Construct
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "Object/exe_type.h"
#include "system/String.h"

namespace UI { class StringTable; class SpriteSheet; }

class Profession final : public Object::External {
    DECLARE_EXE_TYPE(Profession)

public:
    Profession();                                                 // 0x1400a9030

    ~Profession() override;                                       // [0] 0x14001e6f0
    int32_t GetFileVersion() const override { return 3; }         // [3]
    // [4] Compile: inherited (retail's slot is the Object::External import)
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x1400a8f10
    Object::External* Reconstruct() override;                     // [6] 0x14001e580
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x14001e570

public:
    // B.4 W0-H1: types from the ctor 0x1400a9030 and the serializer 0x1400a8f10
    // (0x14000bf40 / 0x14000bd30 = ExternalPtr serialize validating the imported
    // _typeInfo 0x140151488 UI::StringTable / 0x140151480 UI::SpriteSheet (the
    // latter is the type GameContainer_Init 0x14009a7a3 validates
    // "Dialog/SpriteSheet.rsc" with); 0x140151238 String; 0x140152ac8 int;
    // 0x140152a58 bool; 0x140152a60 float). The serializer passes no names.
    // The three String fields are System::String: retail's ctor stores 0 in each
    // (String() is `_data = nullptr`) and its dtor 0x14001e6f0 runs ~String on
    // +0x40, +0x38, +0x30, which is the compiler's member order (B.4 GAME-b).
    UI::StringTable* stringTable;  // +0x20: ExternalPtr<UI::StringTable> (0x14000bf40); the table the
                                   //        profession's text keys are looked up in (PostLifeEvent,
                                   //        CitizenComponent::Update)
    UI::SpriteSheet* spriteSheet;  // +0x28: ExternalPtr<UI::SpriteSheet> (0x14000bd30)
    System::String stringName;     // +0x30: the title key; PostLifeEvent builds
                                   //        UI::String(stringTable, +0x30) as the citizen's title
    System::String stringDesc;     // +0x38: name NOT confirmed from retail
    System::String stringTool;     // +0x40: name NOT confirmed from retail:
                                   //        CitizenComponent::Update builds UI::String(stringTable,
                                   //        +0x40) for HandleLifeEvent(7) (a profession change)
    int32_t  sortPriority;              // +0x48: int (serialized)
    bool     transient;              // +0x4C: bool (serialized); CitizenComponent::Update raises the
                                   //        "no job" icon only when it is 0
    bool     immuneToDisease;              // +0x4D: bool (serialized)
    bool     isAvailable;          // +0x4E: bool, default 1 (serialized); name NOT confirmed from retail
    uint8_t  _pad4F;               // +0x4F: padding
    int32_t  priority;             // +0x50: int, default 100 (serialized); name NOT confirmed from retail
    float    skillMult1;           // +0x54: float, default 1.0f (serialized); name NOT confirmed
    float    skillMult2;           // +0x58: float, default 1.0f (serialized); name NOT confirmed
    uint8_t  _pad5C[4];            // +0x5C: padding
};

static_assert(sizeof(Profession) == 0x60, "Profession is 0x60");
static_assert(offsetof(Profession, stringTable) == 0x20, "+0x20");
static_assert(offsetof(Profession, spriteSheet) == 0x28, "+0x28");
static_assert(offsetof(Profession, stringName) == 0x30, "+0x30");
static_assert(offsetof(Profession, stringDesc) == 0x38, "+0x38");
static_assert(offsetof(Profession, stringTool) == 0x40, "+0x40");
static_assert(offsetof(Profession, sortPriority) == 0x48, "+0x48");
static_assert(offsetof(Profession, transient) == 0x4C, "+0x4C");
static_assert(offsetof(Profession, immuneToDisease) == 0x4D, "+0x4D");
static_assert(offsetof(Profession, isAvailable) == 0x4E, "+0x4E");
static_assert(offsetof(Profession, _pad4F) == 0x4F, "+0x4F");
static_assert(offsetof(Profession, priority) == 0x50, "+0x50");
static_assert(offsetof(Profession, skillMult1) == 0x54, "+0x54");
static_assert(offsetof(Profession, skillMult2) == 0x58, "+0x58");
static_assert(offsetof(Profession, _pad5C) == 0x5C, "+0x5C");
