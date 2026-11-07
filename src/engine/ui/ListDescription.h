#pragma once
// ============================================================================
// src/engine/ui/ListDescription.h — UI::ListDescription, the REAL C++ class
// (ENDGAME B.3 step 6, block C4)
// ============================================================================
//
// PARENT `UI::ElementDescription` — READ, not inferred:
//     ?_ParentTypeName@ListDescription@UI@@CAPEBDXZ @0x180044b70 -> "ElementDescription"
//
// RULE B5-15 / B6-7 — THE TWO NAME LITERALS, RECORDED, NOT USED (slots 1/2 keep
// reading the minted `?_typeInfo@ListDescription@UI@@…`, 0x1801fa0e8):
//     ?_TypeName@ListDescription@UI@@CAPEBDXZ       @0x180044b60 -> "ListDescription"
//     ?_ParentTypeName@ListDescription@UI@@CAPEBDXZ @0x180044b70 -> "ElementDescription"
//
// `??_7ListDescription@UI@@6B@` @ 0x180156e20, TEN slots (B36_SLOTS.md
// section 1; sizeof from `MOV EDX,0x90` in slot 0 0x180044ed0):
//   [0] 0x180044ed0 deleting dtor (row)  [1] 0x180044ba0 TypeName
//   [2] 0x180044b80 Type                 [3] 0x1800d4900 GetFileVersion
//   [4] 0x1800066e0 External::Compile INHERITED
//   [5] 0x1800d4810 Serialize            [6] 0x180044d20 Reconstruct
//   [7] 0x180044d50 Destruct             [8] 0x1800d45f0 CreateInstance RETSLOT
//   [9] 0x180044d40 Construct            INTRODUCED by the leaf (RULE B6-1.1)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~ListDescription holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7ListDescription@UI@@6B@`,
// its RT_DELDTOR row 0x180044ed0 and the ctor / dtor / slot doors are retired
// (tools/rt_mint_retired/ui-b.txt).
// ABSTRACT (slot 0 pure), no real ctor: every live one is built by the
// `??0ListDescription` DOOR (listdescription.cpp) and wears the MINTED table.
//
// LAYOUT (RULE B6-8) — MOVED here from listdescription.cpp's raw offsets.
// Evidence: `??0ListDescription@UI@@QEAA@XZ` (0x1800d49b0), tail:
//     MOV qword [RCX+0x68/0x70/0x78/0x80],0 ; MOV qword [RCX+0x88],-0x7f7f80
// (one qword: +0x88 = 0xff808080, +0x8c = 0xffffffff), and Serialize
// (0x1800d4810): 0x180038f90 x2 over +0x68/+0x70 (ExternalPtr<ElementDescription>),
// 0x180004590 over +0x78 (ExternalPtr<SpriteSheet>), 0x1800c9930 over +0x80
// (System::String), kScalarUInt over +0x88 and +0x8c. Every byte is written.
// The names are what CreateInstance (0x1800d45f0) copies each field INTO on the
// ListElement (src/engine/ui/ListElement.h).
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "ElementDescription.h"

namespace UI {

class Dialog;
class Element;

class SpriteSheet;
class StringTable;
class Font;
class ElementDescription;

class ListDescription : public ElementDescription {
public:
    // ---- the ten slots, in measured order ----------------------------------
    // ??0ListDescription@UI@@QEAA@XZ 0x1800d49b0 -- the REAL ctor:
    // ElementDescription's stores (inlined in retail), then +0x68..+0x80 zero
    // and ONE qword covering both colours.
    ListDescription() : ElementDescription() {
        _partDescs[0] = nullptr;
        _partDescs[1] = nullptr;
        _spriteSheet  = nullptr;
        _hoverColor   = System::kColorGrey;
        _selectColor  = System::kColorWhite;
    }

    // [0] 0x180044ed0 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x90` = the element size).
    // `??1ListDescription@UI@@UEAA@XZ` (0x1800d4910): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in listdescription.cpp;
    // the compiler chains the base's.
    ~ListDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180044ba0, Type 0x180044b80, _TypeName 0x180044b60, _ParentTypeName 0x180044b70
    DECLARE_TYPE(ListDescription)
public:
    int32_t       GetFileVersion() const override;                // [3] 0x1800d4900
    bool          Serialize(IO::Stream& stream) override;               // [5] 0x1800d4810
    Object::External* Reconstruct() override;                     // [6] 0x180044d20
    void          Destruct() override;                            // [7] 0x180044d50
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x1800d45f0 RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180044d40

    // ---- exported, not ported: declared, deliberately undefined ------------
    ListDescription(const ListDescription& src);                  // 0x180044d60
    ListDescription& operator=(const ListDescription& src);       // 0x180044df0

private:

public:
    // ---- the 0x28 bytes after ElementDescription ---------------------------
    // ExternalPtr<UI::ElementDescription> x2: CreateInstance builds [0] then
    // [1] as the ListElement's _children[0] (backdrop) and [1] (content).
    ElementDescription* _partDescs[2]; // +0x68, +0x70 (B4 H4: typed)
    SpriteSheet* _spriteSheet;    // +0x78 (B4 H4: typed)  ExternalPtr<UI::SpriteSheet>  -> ListElement::_highlight.sheet
    System::String _spriteName;  // +0x80  ctor 0x1800d49b0, dtor 0x1800d4910 (B4 Y RT-UI: a real member)
    uint32_t _hoverColor;         // +0x88  ctor 0xff808080 -> ListElement::_hoverColor (+0xd0)
    uint32_t _selectColor;        // +0x8c  ctor 0xffffffff -> ListElement::_selectColor (+0xd4)
};

static_assert(sizeof(ListDescription) == 0x90,
              "UI::ListDescription is 0x90 bytes (MOV EDX,0x90 in its slot-0 "
              "vector deleting destructor 0x180044ed0)");
static_assert(alignof(ListDescription) == 8, "UI::ListDescription aligns to 8");
static_assert(offsetof(ListDescription, _partDescs) == sizeof(ElementDescription), "");
static_assert(offsetof(ListDescription, _partDescs)   == 0x68, "");
static_assert(offsetof(ListDescription, _spriteSheet) == 0x78, "");
static_assert(offsetof(ListDescription, _spriteName)  == 0x80, "");
static_assert(offsetof(ListDescription, _hoverColor)  == 0x88, "");
static_assert(offsetof(ListDescription, _selectColor) == 0x8c, "");

}  // namespace UI
