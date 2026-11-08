#pragma once
// ============================================================================
// src/game/Game/Toolbar.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `Toolbar` (0x68): a toolbar resource (Game\Toolbar.rsc: items, layout,
// selection state, building-description refs), a real `Object::External`
// subclass. Layout from ctor 0x1400d2130, factory 0x140024ba0, Serialize
// 0x1400d2010.
//
// Serialize order: ExtPtr2 +0x20, ExtPtr +0x28, String +0x30, String +0x38,
//   uint +0x48, ExtPtrToolbar +0x60, ExtPtrBuild +0x58, uint +0x40,
//   bool +0x45, bool +0x44, int +0x4c, int +0x50
//
// RETAIL (Application-x64.exe, B38_SLOTS.md):
//   reflected "Toolbar", parent "Object::External", table 0x140160120,
//   9 slots, sizeof 0x68.
//   [0] 0x140024c10 scalar deleting dtor   [1] TypeName
//   [2] 0x140024b80 Type                   [3] 0x1400d2120 GetFileVersion `MOV EAX,4`
//   [4] IMPORT Object::External::Compile   [5] 0x1400d2010 Serialize
//   [6] 0x140024bf0 Reconstruct            [7] 0x140001640 Destruct
//   [8] 0x140024be0 Construct
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "Object/exe_type.h"
#include "system/String.h"

namespace UI { class StringTable; class SpriteSheet; }
class ComponentDescription;

class Toolbar final : public Object::External {
    DECLARE_EXE_TYPE(Toolbar)

public:
    Toolbar();                                                    // 0x1400d2130

    ~Toolbar() override;                                          // [0] 0x140024c10
    int32_t GetFileVersion() const override { return 4; }         // [3] 0x1400d2120
    // [4] Compile: inherited (retail's slot is the import; the old table's was null)
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x1400d2010
    Object::External* Reconstruct() override;                     // [6] 0x140024bf0
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x140024be0

public:
    UI::StringTable* textTable;   // +0x20: ExternalPtr<UI::StringTable> (serialized via fn_ExternalPtr_Serialize2; B4 H4: typed, DLG-d -- the button text table)
    UI::SpriteSheet* spriteSheet;   // +0x28: ExternalPtr<UI::SpriteSheet> (serialized via fn_ExternalPtr_Serialize; B4 H4: typed, DLG-d -- the button sprite sheet)
    System::String textKey;     // +0x30: serialized (ctor 0x1400d2130 zeroes it; dtor 0x140024c10 destroys it second)
    System::String spriteName;     // +0x38: serialized (ctor zeroes it; dtor destroys it first)
    uint32_t shortcutKey;           // +0x40: 8 (serialized through the uint door 0x98: retail type uint)
    bool     autoShortcut;           // +0x44: bool (serialized, default 1)
    bool     visible;           // +0x45: bool (serialized, default 1)
    uint8_t  _pad46[2];          // +0x46: padding
    uint32_t actionType;           // +0x48: (serialized through the uint door 0x98: retail type uint)
    int32_t  selectedIndex;      // +0x4C: -1 sentinel (serialized as int)
    int32_t  sortPriority;           // +0x50: int (serialized as int; the ctor leaves it)
    uint8_t  _pad54[4];          // +0x54: padding
    ComponentDescription* componentDescription;  // +0x58: ExternalPtr<ComponentDescription> (B4 H4: typed, DLG-d -- the template the button acts on)
    Toolbar* parentToolbar;           // +0x60: ExternalPtr<Toolbar> (FUN_140024c70; B4 H4: typed, DLG-d -- the parent item)
};

static_assert(sizeof(Toolbar) == 0x68, "Toolbar is 0x68");
static_assert(offsetof(Toolbar, spriteSheet) == 0x28, "Toolbar::spriteSheet");
static_assert(offsetof(Toolbar, _pad46) == 0x46, "Toolbar::_pad46");
static_assert(offsetof(Toolbar, _pad54) == 0x54, "Toolbar::_pad54");
static_assert(offsetof(Toolbar, textTable) == 0x20, "+0x20");
static_assert(offsetof(Toolbar, textKey) == 0x30, "+0x30");
static_assert(offsetof(Toolbar, spriteName) == 0x38, "+0x38");
static_assert(offsetof(Toolbar, shortcutKey) == 0x40, "+0x40");
static_assert(offsetof(Toolbar, autoShortcut) == 0x44, "+0x44");
static_assert(offsetof(Toolbar, visible) == 0x45, "+0x45");
static_assert(offsetof(Toolbar, actionType) == 0x48, "+0x48");
static_assert(offsetof(Toolbar, selectedIndex) == 0x4C, "+0x4C");
static_assert(offsetof(Toolbar, sortPriority) == 0x50, "+0x50");
static_assert(offsetof(Toolbar, componentDescription) == 0x58, "+0x58");
static_assert(offsetof(Toolbar, parentToolbar) == 0x60, "+0x60");
