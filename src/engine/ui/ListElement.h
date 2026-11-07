#pragma once
// ============================================================================
// src/engine/ui/ListElement.h — UI::ListElement, the REAL C++ class
// (ENDGAME B.3 step 6, block C4)
// ============================================================================
//
// PARENT `UI::Element` — READ, not inferred:
//     ?_ParentTypeName@ListElement@UI@@CAPEBDXZ @0x180044f60 -> "Element"
//
// RULE B5-15 / B6-7 — THE TWO NAME LITERALS, RECORDED, NOT USED (slots 1/2 keep
// reading the minted `?_typeInfo@ListElement@UI@@…`, 0x1801fa120):
//     ?_TypeName@ListElement@UI@@CAPEBDXZ       @0x180044f50 -> "ListElement"
//     ?_ParentTypeName@ListElement@UI@@CAPEBDXZ @0x180044f60 -> "Element"
//
// `??_7ListElement@UI@@6B@` @ 0x180156dc0, TWELVE slots (B36_SLOTS.md
// section 1; sizeof from `MOV EDX,0x100` in slot 0 0x180045320):
//   [0] 0x180045320 deleting dtor (row)  [1] 0x180044f90 TypeName
//   [2] 0x180044f70 Type                 [3] Element::Persist INHERITED
//   [4] 0x1800d4380 ComputeSize          [5] 0x1800d4210 ComputeLayout
//   [6] 0x1800d3eb0 Draw                 [7] 0x1800d3c20 Update
//   [8]..[11] Element's, INHERITED
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~ListElement holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7ListElement@UI@@6B@`,
// its RT_DELDTOR row 0x180045320 and the ctor / dtor / slot doors are retired
// (tools/rt_mint_retired/ui-b.txt).
// ABSTRACT (slot 0 pure), no real ctor: every live one is built by the
// `??0ListElement` DOOR (listelement.cpp) and wears the MINTED table.
//
// LAYOUT (RULE B6-8) — MOVED here from listelement.cpp's file-local constants
// and element_bodies_b5.cpp's AddItem/DrawHighlight raw reads. Evidence:
// `??0ListElement@UI@@QEAA@XZ` (0x1800d4570), read as disassembly:
//     MOV qword [RBX+0xc0],0 ; MOV dword [RBX+0xc8],-1
//     MOV qword [RBX+0xd0],-0x7f7f80 ; MOV qword [RBX+0xd8],-1
//     MOV qword [RBX+0xe0/0xe8/0xf0/0xf8],0
// +0xc0/+0xc8 is a UI::Sprite {sheet, index}: ListDescription::CreateInstance
// (0x1800d45f0) writes the sheet and the looked-up index there, and
// DrawHighlight (0x1800d3f70) hands `this+0xc0` to SpriteContext::AddSprite.
// Its +0xcc pad is written by nothing. ?SetCurrentSelection@ (0x180045110)
// clamps against the dword at +0xe8 — the +0xe0 array's size — and stores +0xd8;
// ?GetCurrentSelection@ (0x180045130) is `MOV EAX,[RCX+0xd8]`.
// ~ListElement (0x1800d4530) destroys exactly the +0xe0 array (0x180024aa0).
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "Element.h"
#include "Sprite.h"   // ::Sprite (block D folds it into UI::Sprite with a global alias)

namespace UI {

class ListElement : public Element {
public:
    // ---- the twelve slots, in measured order -------------------------------
    // ??0ListElement@UI@@QEAA@XZ 0x1800d4570 -- the REAL ctor: `CALL
    // ??0Element`, then retail's stores (the Sprite and Array members' own
    // ctors make {null, -1} and the three zeroes).
    ListElement() : Element() {
        _hoverColor    = System::kColorGrey;    // one qword store, low half ...
        _selectColor   = System::kColorWhite;   // ... high half
        _selectedIndex = -1;
        _hoverIndex    = -1;
        _ribbon        = nullptr;
        _clip          = nullptr;
    }

    // [0] 0x180045320 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x100` = the element size).
    // `??1ListElement@UI@@UEAA@XZ` (0x1800d4530): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in listelement.cpp;
    // the compiler chains the base's.
    ~ListElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180044f90, Type 0x180044f70, _TypeName 0x180044f50, _ParentTypeName 0x180044f60
    DECLARE_TYPE(ListElement)
public:
    void ComputeSize() override;                                  // [4] 0x1800d4380
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t right, int32_t bottom) override;   // [5] 0x1800d4210
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800d3eb0
    void Update(const System::Time& time, UserInterface* ui,
                Object::InternalPtr<DialogInstance>* dialog,
                const UpdateState& state) override;               // [7] 0x1800d3c20

    // ---- non-virtual members -----------------------------------------------
    int32_t GetCurrentSelection() const { return _selectedIndex; } // 0x180045130 MOV EAX,[RCX+0xd8]
    // PORTED, bodies in ANOTHER BLOCK'S FILE (element_bodies_b5.cpp) —
    // fragment pass; declared so no later block edits this header.
    // Retail passes the by-value InternalPtr as the ADDRESS of the caller's
    // temporary (block A's slot-7 finding), so it is spelled as that pointer.
    void AddItem(Object::InternalPtr<Element>* item, void* userData);  // 0x1800d44b0
    void DrawHighlight(SpriteContext& context, int32_t index,
                       uint32_t color) const;                     // 0x1800d3f70
    // Exported, not ported: declared, deliberately undefined.
    void    SetCurrentSelection(int32_t index);                   // 0x180045110
    int32_t GetHeight() const;                                    // 0x1800d43c0
    int32_t GetWidth() const;                                     // 0x1800d4400
    void    SetSize(int32_t w, int32_t h);                        // 0x1800d4440
    bool    GetCellLayout(int32_t index, int32_t& left, int32_t& top,
                          int32_t& right, int32_t& bottom) const; // 0x1800d4120 (private in retail)

    ListElement(const ListElement& src);                          // 0x180045140  not ported
    ListElement& operator=(const ListElement& src);               // 0x180045220  not ported

private:

public:
    // ---- the 0x40 bytes after Element --------------------------------------
    ::Sprite _highlight;         // +0xc0  ctor {null, -1}; the selection-bar sprite
    uint32_t _hoverColor;        // +0xd0  ctor 0xff808080 (one qword with +0xd4)
    uint32_t _selectColor;       // +0xd4  ctor 0xffffffff
    int32_t  _selectedIndex;     // +0xd8  ctor -1 (one qword of -1 with +0xdc)
    int32_t  _hoverIndex;        // +0xdc  ctor -1
    // Per-row user data, appended by AddItem; the one member ~ListElement
    // destroys (0x180024aa0).
    Collection::Array<void*> _userData;   // +0xe0 .. +0xef
    // The UI::RibbonElement (block C5) three levels down that holds the rows;
    // Update/DrawHighlight read its own fields (+0xc0 vertical byte, +0xc4
    // spacing, +0xc8 extent array) raw. Found by CreateInstance's type test.
    Element* _ribbon;            // +0xf0  ctor null
    // The UI::ClipRegionElement (block C1) two levels down: the visible window.
    Element* _clip;              // +0xf8  ctor null
};

static_assert(sizeof(ListElement) == 0x100,
              "UI::ListElement is 0x100 bytes (MOV EDX,0x100 in its slot-0 "
              "vector deleting destructor 0x180045320)");
static_assert(alignof(ListElement) == 8, "UI::ListElement aligns to 8");
static_assert(offsetof(ListElement, _highlight) == sizeof(Element), "");
static_assert(offsetof(ListElement, _highlight)     == 0xc0, "");
static_assert(offsetof(ListElement, _hoverColor)    == 0xd0, "");
static_assert(offsetof(ListElement, _selectColor)   == 0xd4, "");
static_assert(offsetof(ListElement, _selectedIndex) == 0xd8, "");
static_assert(offsetof(ListElement, _hoverIndex)    == 0xdc, "");
static_assert(offsetof(ListElement, _userData)      == 0xe0, "");
static_assert(offsetof(ListElement, _ribbon)        == 0xf0, "");
static_assert(offsetof(ListElement, _clip)          == 0xf8, "");

}  // namespace UI
