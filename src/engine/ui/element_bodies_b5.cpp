// ============================================================================
// src/engine/ui/element_bodies_b5.cpp — thirteen assorted UI::*Element /
// UI::*Description non-slot bodies
// ============================================================================
//
// Every body below was transcribed from `disassemble_function` against RETAIL
// Runtime-x64.dll (image base 0x180000000), never from Ghidra's decompiled C:
// this batch contains four of the exact shapes that output is known to get
// wrong — an argument dropped on an indirect call, a QWORD read of a DWORD
// stack slot, a `bool` arriving in the seventh (stack) slot, and a 91-entry
// jump table (reference_ghidra_c_output_vs_disassembly.md).
//
//   retail       mangled name                                            note
//   -----------  ------------------------------------------------------  ----
//   0x180118c70  ??0NumberElement@UI@@QEAA@XZ                            ctor
//   0x18011e090  ?GetRowHeight@TableElement@UI@@QEAAHH@Z
//   0x18011e1a0  ?GetColumnWidth@TableElement@UI@@QEAAHH@Z
//   0x1800d6c20  ?UpdateNumber@SliderElement@UI@@AEAAXXZ
//   0x1800448e0  ?AddItem@RibbonElement@UI@@QEAAXV?$InternalPtr@…@Z
//   0x1800d44b0  ?AddItem@ListElement@UI@@QEAAXV?$InternalPtr@…@PEAX@Z
//   0x1800d3f70  ?DrawHighlight@ListElement@UI@@QEBAXAEAVSpriteContext@2@HI@Z
//   0x18011a2d0  ?GetBarLayout@ScrollElement@UI@@AEBAXAEAH000@Z
//   0x18011ef70  ?CreateInstance@TextDescription@UI@@QEBAX…@Z
//   0x1801108d0  ?CreateInstance@ButtonDescription@UI@@QEBAX…@Z
//   0x1800d2dc0  ?GetChildLayout@LayoutElement@UI@@AEAAXHHHHHH_N@Z
//   0x1800cfd40  ?SelectCharacter@EditElement@UI@@AEBADH@Z
//
// ---------------------------------------------------------------------------
// THE RETURN-SLOT AUDIT, DONE FIRST BECAUSE GETTING IT WRONG IS A WILD WRITE
// ---------------------------------------------------------------------------
// * `?SelectCharacter@EditElement@UI@@AEBADH@Z` — `AEBA` is "private const
//   member", `D` is `char`. It returns a char BY VALUE, not `char&` (that
//   would be `AEBAAEAD`). The disassembly agrees: every arm ends `MOV AL,<imm>
//   ; RET` or `CMOVZ EAX,ECX ; RET`, and nothing writes through RDX. NO hidden
//   slot, and RDX is the plain first argument.
//
// * BOTH `CreateInstance` bodies return **void**. Their `InternalPtr<…>`
//   parameter is passed BY VALUE in the C++ source, which on the MS x64 ABI
//   means MSVC hands the callee a POINTER to the caller's copy. So RCX is
//   `this`, RDX is `InternalPtr<T>*`, R8 is the `Dialog*` — there is no return
//   slot anywhere in either. Read off the entry sequence, which in both is
//   `MOV RAX,[RDX]` (a LOAD through RDX, i.e. RDX is an in-parameter, not an
//   uninitialised out-slot).
//
//   R8 IS READ BY EXACTLY ONE OF THE TWO, and that difference is the whole
//   reason they are not one function:
//     - TextDescription's (0x18011ef70) never touches R8 after the prologue.
//       It creates no child elements. (Same as ImageDescription's, which
//       src/engine/ui/animatedimagedescription.cpp records.)
//     - ButtonDescription's (0x1801108d0) parks R8 in RBP at 0x1801108ef and
//       passes it as RCX to five `?CreateElement@Dialog@UI@@` calls. Dropping
//       it would build a button with no face, no label and no highlight.
//
// * `?CreateElement@Dialog@UI@@QEBA?AV?$InternalPtr@VElement@UI@@@Object@@…@Z`
//   — which ButtonDescription's body CALLS — DOES return a class by value and
//   therefore DOES have a hidden slot: RCX `this`, **RDX the hidden slot**, R8
//   the description. Retail reuses ONE stack local (`[RSP+0x30]`) as both the
//   InternalPtr copy it hands to `ElementDescription::CreateInstance` and the
//   return slot for all five `CreateElement` calls; that reuse is reproduced.
//
// ---------------------------------------------------------------------------
// THE ONE STALE COMMENT THIS FILE DISPROVES (PLAN.md §6 step 8)
// ---------------------------------------------------------------------------
// `src/engine/ui/ui_factory_wave28.cpp` (deleted by ENDGAME B.3.9 UI-D) said
// of both constructors ported here:
// "The ctor is NOT ported: it calls ?CacheText@…, a private member this lane
// does not own and did not read." For TextElement that premise expired before
// it was written — `?CacheText@TextElement@UI@@AEAAXXZ` (0x18011ec10) has been
// ported and registered by `src/engine/ui/textelement_cachetext.cpp` since
// Wave 10, together with `?SetParameterCount@` (0x18011e8e0). Its comment is
// corrected at its source in this same commit.
//
// For NumberElement the callee is a DIFFERENT function with the same method
// name — `?CacheText@NumberElement@UI@@AEAAXXZ` at 0x1801189f0, which is NOT
// ported by anything in the tree (grep of runtime/ code/ standalone/ engine/
// for both the name and the address finds only descriptive mentions in
// src/engine/ui/NumberElement.h). This file reaches it through the
// port-aware `ResolveAddr`, so it becomes ours the day somebody ports it, and
// it is reported as a residual rather than silently skipped. Skipping it would
// leave `_cachedText` (+0xD0) null on every freshly constructed NumberElement,
// and both `ComputeSize` (0x180119000) and `Draw` (0x1801191E0) dereference it.
//
// ---------------------------------------------------------------------------
// UI::NumberElement DOES NOT DERIVE FROM UI::TextElement
// ---------------------------------------------------------------------------
// Confirming, from the two constructors side by side rather than from the
// class names: BOTH call `??0Element@UI@@QEAA@XZ` (0x180115e50) directly, and
// neither calls the other. src/engine/ui/NumberElement.h already records
// this (Wave 9 lane N) and the two ctors are its strongest evidence, so the
// two "TextElement territory" padding comments in that header and in
// EditElement.h describe fields those classes own in their OWN right.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
//
// ENDGAME B.3 step 6, FRAGMENT PASS F1 (2026-09-25): every non-ctor body below
// now lives in its class's MEMBER (TableElement::GetRowHeight/GetColumnWidth,
// SliderElement::UpdateNumber, RibbonElement::AddItem, ListElement::AddItem/
// DrawHighlight, ScrollElement::GetBarLayout, TextDescription's non-virtual
// CreateInstance applier, LayoutElement::GetChildLayout, EditElement::
// SelectCharacter) and reaches fields by name; each registered door is a
// one-line QUALIFIED delegation. (`??0TextElement`'s door was retired by
// ENDGAME B.3.9 UI-A: TextElement's real ctor.) The ButtonDescription and
// NumberElement notes above are history: that applier moved to
// buttondescription.cpp in block C1, and the NumberElement ctor/CacheText
// live in numberelement.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "system/String.h"
#include "ui/Element.h"
#include "ui/EditElement.h"
#include "ui/LayoutElement.h"
#include "ui/ListElement.h"
#include "ui/NumberElement.h"
#include "ui/RibbonElement.h"
#include "ui/ScrollElement.h"
#include "ui/SliderElement.h"
#include "ui/SpriteContext.h"
#include "ui/SpriteSheet.h"
#include "video/Device.h"   // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "input/Device.h"   // ENDGAME B.3.9 SS1: Input::Device::_singleton
#include "ui/StringTable.h"
#include "ui/TableElement.h"
#include "ui/TextDescription.h"
#include "ui/TextElement.h"
// B6-5 door caches (AtomicExport) and the ResolveAddr declaration this file
// used to spell for itself.
#include "element_common.h"

namespace {

// `?SetNeedsSizeCalc@Element@UI@@` (0x180035470) INLINED. Every mutator in this
// file carries its own copy, exactly as retail does:
//
//     loop: OR dword [RCX+0x34],0x8000000 ; MOV RAX,[RCX+0x98] ; MOV RCX,RAX
//           TEST RAX,RAX ; JNZ loop
//
// Note the shape: the flag is set on the element FIRST and the parent is read
// AFTER, so a chain of length one still gets the bit. It is a `do`, not a
// `while`, and passing a null here would fault in retail too.
void SetNeedsSizeCalcChain(UI::Element* elem) {
    UI::Element* e = elem;
    do {
        e->_flags |= UI::kElementFlagNeedsSizeCalc;                // +0x34
        e = e->_parent;                                            // +0x98
    } while (e != nullptr);
}

// ---- the DLL singletons these bodies read ---------------------------------
// ENDGAME B.3.9 SS1 (B39_RULES D7): both class statics, read fresh:
// ::Video::Device::_singleton (0x1801fe7a8), ::Input::Device::_singleton
// (0x1801fe770).
// 0x1801facb8 — the FIRST DWORD of NumberElement's `_typeInfo`, i.e. its
// typeId, which is what retail loads with a `MOV EDX,dword ptr [0x1801facb8]`:
// `UI::NumberElement::TypeId()` (ENDGAME B.3.9 TI-UI; no cache).

// `?IsDerivedFrom@Base@Object@@QEBA_NI@Z` (0x1800c0390) is `Object::Base::
// IsDerivedFrom`, and `?GetIndexFromName@StringTable@UI@@QEBAHAEBVString@
// System@@@Z` (0x180002470) is `UI::StringTable::GetIndexFromName`
// (sprite.cpp): both are called as members by the bodies below. The unused
// `DialogCreateElement` forwarder (ButtonDescription's, which moved to
// buttondescription.cpp in block C1) is gone.

// `?CreateInstance@ElementDescription@UI@@QEBAXV?$InternalPtr@VElement@UI@@@
// Object@@@Z` (0x180115ad0) — the shared "apply the description's Element-level
// fields to the new element" pass — is `UI::ElementDescription::
// CreateInstance(InternalPtr<Element>*)` (dialog.cpp, fragment pass F2), the
// InternalPtr arriving by address. TextDescription's applier below calls it
// QUALIFIED: the leaf's own two CreateInstance overloads hide the base name.

// The three `Collection::Array<T>::SetSize(n)`-then-fill-the-new-tail helpers
// retail inlines a call to. All three are ported in
// src/engine/collection/array.cpp and registered BY ADDRESS (the template is
// header-only and exports nothing under any name), so `ResolveAddr` is the only
// door there is and it is registry-first.
//   0x180076900  stride 8, zero-then-assign   (kSpec_Fill8Z)
//   0x180023830  stride 4                     (kSpec_Fill4)
//   0x180024fe0  stride 8
void ArrayGrow8Z(void* a, int32_t n, const void* v) { rtcoll::RtColl_ArraySetSizeFill_180076900(a, n, v); }
void ArrayGrow4 (void* a, int32_t n, const void* v) { rtcoll::RtColl_ArraySetSizeFill_180023830(a, n, v); }
void ArrayGrow8 (void* a, int32_t n, const void* v) { rtcoll::RtColl_ArraySetSizeFill_180024fe0(a, n, v); }

}  // namespace

// ============================================================================
// ?GetRowHeight@TableElement@UI@@QEAAHH@Z                    retail 0x18011e090
// ?GetColumnWidth@TableElement@UI@@QEAAHH@Z                  retail 0x18011e1a0
// ============================================================================
// The two are mirror images across the X/Y axis and are written as one helper
// with an axis selector, because every divergence between them is an offset:
//
//                      GetRowHeight(row)          GetColumnWidth(col)
//   loop bound         _colCount (+0xc4)          _rowCount (+0xc0)
//   cell index         _colCount*row + i          i*_colCount + col
//   extent             _layoutHeight +0x64        _layoutWidth  +0x60
//   near/far pad       _topPad +0x20 / _bot +0x24 _left +0x18 / _right +0x1c
//   floor              _minHeight +0x2c           _minWidth +0x28
//   screen-size bit    0x10                       0x20
//   Video::Device      +0x08 (height)             +0x04 (width)
//
// So the cell grid is ROW-MAJOR with a stride of `_colCount`, which both index
// expressions independently confirm, and which agrees with
// `?SetChild@TableElement@UI@@` (0x18011db50, `_colCount*col + row`) as
// src/engine/ui/TableElement.h records it.
//
// WHAT THE PER-CELL BODY IS: `?GetPaddedSize@Element@UI@@` (0x180115f10)
// INLINED. Retail inlines it; so does this. The chain
// `root = walk _parent to the top; scale = root->_dialogParent[+0x10][+0xdc]`
// is the same one Element.h documents for GetPaddedSize, and the walked pointer
// is used ONLY for the scale — the FLAGS tested afterwards are the ORIGINAL
// cell's (retail keeps them in DL across the walk), not the root's. That is not
// a subtlety worth losing: reading the root's flags instead would silently
// change which cells stretch to the screen.
//
// The vtable call is slot 4, `ComputeSize()` — `CALL qword ptr [RAX+0x20]`,
// which is 0x20/8 = 4. It takes `this` and nothing else.
namespace {

// The axis selector picks the fields by pointer-to-member.
using ElemField   = int32_t UI::Element::*;
using DeviceField = int32_t Video::Device::*;

int32_t TablePaddedExtent(UI::Element* cell,
                          ElemField extent, ElemField nearPad, ElemField farPad,
                          ElemField minField, uint32_t screenBit, DeviceField screen) {
    // slot 4 first: the extent it reads below is what this call computes.
    cell->ComputeSize();                                           // 18011e0f8 / 18011e1fe  VIRTUAL

    const uint32_t flags = cell->_flags;                           // 18011e0fe / 18011e204
    int32_t v = cell->*extent + cell->*farPad + cell->*nearPad;
    const int32_t floorV = cell->*minField;
    if (v < floorV) v = floorV;                                    // CMOVL

    if ((flags & UI::kElementFlagSizeToScreen) != 0) {             // 18011e10e / 18011e214
        const UI::Element* root = cell;
        while (root->_parent != nullptr) {                         // 18011e120 / 18011e223
            root = root->_parent;
        }
        if ((flags & screenBit) != 0) {                            // 18011e134 / 18011e237
            // root->_dialogParent (+0x90) -> _ui (+0x10) -> _uiScale (+0xdc)
            const float scale = root->_dialogParent->_ui->_uiScale;
            const float px = (float)(::Video::Device::_singleton->*screen);
            v = (int32_t)(px * scale);                             // CVTTSS2SI: truncate
        }
    }
    return v;
}

}  // namespace

namespace UI {

int32_t TableElement::GetRowHeight(int32_t row) {
    int32_t best = 0;                                              // EDI

    for (int32_t i = 0; i < _columnCount; ++i) {
        Element* const* cells = _children._data;
        const int32_t idx  = _columnCount * row + i;
        Element* cell = cells[idx];                                // 18011e0df
        if (cell == nullptr) continue;                             // 18011e0e6
        if ((cell->_flags & kElementFlagHidden) != 0) continue;   // 18011e0f0

        const int32_t h = TablePaddedExtent(cell, &Element::_layoutHeight, &Element::_topPad,
                                            &Element::_bottomPad, &Element::_minHeight,
                                            kElementFlagSizeToScreenH,
                                            &::Video::Device::backBufferHeight);   // Device +0x08
        if (h > best) best = h;                                    // 18011e161 CMOVG
    }
    return best;
}

int32_t TableElement::GetColumnWidth(int32_t column) {
    int32_t best = 0;

    for (int32_t i = 0; i < _rowCount; ++i) {
        Element* const* cells = _children._data;
        const int32_t idx  = i * _columnCount + column;
        Element* cell = cells[idx];                                // 18011e1e5
        if (cell == nullptr) continue;                             // 18011e1ec
        if ((cell->_flags & kElementFlagHidden) != 0) continue;   // 18011e1f6

        const int32_t w = TablePaddedExtent(cell, &Element::_layoutWidth, &Element::_leftPad,
                                            &Element::_rightPad, &Element::_minWidth,
                                            kElementFlagSizeToScreenW,
                                            &::Video::Device::backBufferWidth);    // Device +0x04
        if (w > best) best = w;                                    // 18011e264 CMOVG
    }
    return best;
}

}  // namespace UI

// ============================================================================
// ?UpdateNumber@SliderElement@UI@@AEAAXXZ                    retail 0x1800d6c20
// ============================================================================
// Pushes the slider's own float value at +0x128 into its FIRST CHILD, but only
// when that child really is a UI::NumberElement — checked with
// `Object::Base::IsDerivedFrom` against `NumberElement::_typeInfo`'s type id
// rather than with a vtable comparison, so a subclass passes too.
//
// Which of the two `SetValue` overloads it calls is decided by the CHILD's
// `_mode` at +0xC8, not by the slider: mode 0 takes the INT overload with the
// float TRUNCATED (`CVTTSS2SI`), mode non-zero takes the FLOAT overload
// unrounded. Both overloads are ported in src/engine/ui/widgets_input.cpp.
//
// `TEST RBX,RBX` appears TWICE (0x1800d6c37 and 0x1800d6c4e), the second time
// on a value that cannot have changed. That is retail's redundancy, not a
// guard this port added, and it is kept.
//
// This body proves SliderElement has state of its own at +0x128, which
// src/engine/ui/SliderElement.h previously (and honestly) said no ported
// function had shown. That header is corrected in this same commit.
namespace UI {

void SliderElement::UpdateNumber() {
    Element* const* children = _children._data;
    Element* first = *children;                                    // 1800d6c34  children[0]
    if (first == nullptr) return;                                  // 1800d6c3a

    if (!first->IsDerivedFrom(UI::NumberElement::TypeId())) return;      // 1800d6c45 / 4c
    NumberElement* child = static_cast<NumberElement*>(first);
    if (child == nullptr) return;                                  // 1800d6c51  retail's second test

    const float value = _value;                                    // +0x128

    // THE OVERLOAD IS CHOSEN BY THE CHILD'S MODE: `QEAAXH@Z` / `QEAAXM@Z`.
    if (child->_mode == 0) {                                       // 1800d6c53  _mode
        child->SetValue(static_cast<int32_t>(value));              // 1800d6c5f/71  TAIL JMP
    } else {
        child->SetValue(value);                                    // 1800d6c7e
    }
}

}  // namespace UI

// ============================================================================
// ?AddItem@RibbonElement@UI@@QEAAXV?$InternalPtr@VElement@UI@@@Object@@@Z
//                                                            retail 0x1800448e0
// ============================================================================
// TWO parallel arrays grow together and the dirty walk runs TWICE, once after
// each — which is not a transcription artefact but exactly what the two
// `OR / MOV / TEST / JNZ` loops at 0x180044920 and 0x180044950 do.
//
//   +0xa0  Element::_children, Array<InternalPtr<Element>>   grown via 0x180076900
//   +0xc8  Array<int32>, one slot per item, appended as ZERO via 0x180023830
//
// THE ZERO IS WRITTEN INTO THE SAME STACK SLOT the element pointer was in:
// `MOV dword ptr [RSP+0x30],EAX` at 0x180044942 overwrites only the LOW FOUR
// BYTES, and EAX is zero there solely because the preceding walk loop exits
// with `RAX == 0`. The callee is a stride-4 grow and reads four bytes, so a
// separate int32 zero is byte-for-byte the same call; the reuse is recorded
// here rather than reproduced, because reproducing it would mean a
// deliberately-aliased local for no observable difference.
namespace UI {

void RibbonElement::AddItem(Object::InternalPtr<Element>* itemPtr) {
    Element* item = itemPtr->Get();                                // 1800448e6
    if (item != nullptr) {                                         // 1800448f4
        item->_parent = this;                                      // 1800448f6
    }

    ArrayGrow8Z(&_children, _children._size + 1, &item);           // 18004490e

    SetNeedsSizeCalcChain(this);                                   // 180044920

    const int32_t zero = 0;                                        // 180044942 (see banner)
    ArrayGrow4(&_extentsData, _extentsCount + 1, &zero);           // 18004494b  (+0xc8)

    SetNeedsSizeCalcChain(this);                                   // 180044950
}

}  // namespace UI

// ============================================================================
// ?AddItem@ListElement@UI@@QEAAXV?$InternalPtr@VElement@UI@@@Object@@PEAX@Z
//                                                            retail 0x1800d44b0
// ============================================================================
// A ListElement is a RibbonElement at +0xF0 plus a clip element at +0xF8 plus a
// parallel `Array<void*>` of per-row user data at +0xE0. Adding an item is:
// hand the element to the inner ribbon, append the user-data pointer, then
// dirty the RIBBON — not `this`. The walk starts from `[RBX+0xf0]`, re-read
// AFTER the two calls (0x1800d44ec), and that distinction is load-bearing:
// dirtying `this` would leave the ribbon's own cached size stale.
//
// The `InternalPtr` handed to the ribbon is a fresh stack copy, not the
// caller's slot — retail builds it at [RSP+0x30] and passes its address.
namespace UI {

void ListElement::AddItem(Object::InternalPtr<Element>* itemPtr, void* userData) {
    Object::InternalPtr<Element> local = *itemPtr;                 // 1800d44ba
    // `_ribbon` is the RibbonElement ListDescription::CreateInstance found by
    // its type test (ListElement.h).
    static_cast<RibbonElement*>(_ribbon)->AddItem(&local);         // 1800d44c0 / 1800d44d1

    ArrayGrow8(&_userData, _userData._size + 1, &userData);        // 1800d44e7  (+0xe0)

    SetNeedsSizeCalcChain(_ribbon);                                // 1800d44ec, re-read
}

}  // namespace UI

// ============================================================================
// ?DrawHighlight@ListElement@UI@@QEBAXAEAVSpriteContext@2@HI@Z
//                                                            retail 0x1800d3f70
// ============================================================================
// Draws the selection bar behind row `index`, clipped to the viewport element
// at +0xF8, using the sprite stored inline at `this+0xC0`.
//
// STEP 1 is a PREFIX SUM over the ribbon's per-item extents:
//     offset = SUM over i<index of (sizes[i] + spacing)
// Retail's copy is AUTO-VECTORISED — four lanes of PADDD with a scalar tail —
// and this port is the scalar loop. That substitution is safe here and would
// NOT be safe one type over: these are 32-bit integers, whose addition is
// associative under two's-complement wraparound, so the reassociation the
// vectoriser performed cannot change a single bit. (The same move on the floats
// in `Math::Transform::FromDirection` is precisely the Wave 1 defect, which is
// why the reason is written down instead of assumed.)
//
// STEP 2 turns that into a rectangle whose ORIENTATION comes from the ribbon's
// byte at +0xC0:
//     vertical   -> top = ribbon.top + offset, bottom = top + sizes[index],
//                   left/right = the ribbon's own
//     horizontal -> left = ribbon.left + offset, right = left + sizes[index],
//                   top/bottom = the ribbon's own
//
// STEP 3 is a reject-then-clamp against the clip element, and the asymmetry in
// it is retail's, not a typo: the FAR edges clamp to `clip.far + 1`
// (`LEA EDI,[RCX+1]`) while the NEAR edges clamp to `clip.near` exactly. Then
// the axis that was NOT the scroll axis is replaced wholesale by the clip's,
// which is why a highlight always spans the full width of a vertical list.
//
// The colour argument survives in R14D across all of it (`MOV R14D,R9D` at
// 0x1800d3f94) because R9D is reused for the rectangle's top edge.
namespace UI {

void ListElement::DrawHighlight(SpriteContext& context, int32_t index,
                                uint32_t color) const {
    if (index < 0) return;                                         // 1800d3f73  JS

    const RibbonElement* ribbonElem = static_cast<const RibbonElement*>(_ribbon);

    const int32_t  spacing = ribbonElem->_spacing;                 // +0xc4
    const int32_t* sizes   = ribbonElem->_extentsData;             // +0xc8

    int32_t offset = 0;
    for (int32_t i = 0; i < index; ++i) {                          // 1800d3fe0 / 1800d4031
        offset += sizes[i] + spacing;
    }

    const bool vertical = ribbonElem->_vertical != 0;              // 1800d4042  (+0xc0)
    int32_t left   = ribbonElem->_layoutLeft;                      // 1800d4049
    int32_t top    = ribbonElem->_layoutTop;                       // 1800d404d
    int32_t right;
    int32_t bottom;
    if (vertical) {
        right  = ribbonElem->_layoutRight;                         // 1800d4065
        top    = top + offset;                                     // 1800d4068
        bottom = sizes[index] + top;                               // 1800d4062/6b
    } else {
        bottom = ribbonElem->_layoutBottom;                        // 1800d4073
        left   = left + offset;                                    // 1800d4076
        right  = sizes[index] + left;                              // 1800d4070/79
    }

    const Element* clip = _clip;                                   // +0xf8
    const int32_t clipLeft   = clip->_layoutLeft;                  // 1800d4083
    const int32_t clipRight  = clip->_layoutRight;                 // 1800d4087
    const int32_t clipTop    = clip->_layoutTop;                   // 1800d408a
    const int32_t clipBottom = clip->_layoutBottom;                // 1800d408e

    if (right  <  clipLeft)   return;                              // 1800d4094
    if (left   >  clipRight)  return;                              // 1800d4099
    if (bottom <  clipTop)    return;                              // 1800d409e
    if (top    >  clipBottom) return;                              // 1800d40a3

    if (bottom > clipBottom) bottom = clipBottom + 1;              // 1800d40a9  note the +1
    if (top    < clipTop)    top    = clipTop;                     // 1800d40af  CMOVL
    if (right  > clipRight)  right  = clipRight + 1;               // 1800d40b7  note the +1
    if (left   < clipLeft)   left   = clipLeft;                    // 1800d40bd  CMOVL

    if (vertical) {                                                // 1800d40c1
        left  = clipLeft;
        right = clipRight;
    } else {
        top    = clipTop;
        bottom = clipBottom;
    }

    // ?AddSprite@SpriteContext@UI@@ (0x18011d3c0): the RECT overload
    // (Sprite const&, l, t, r, b, color) — `UI::SpriteContext::AddSprite`
    // (src/engine/ui/spritecontext_add.cpp), called as that member. The
    // sprite is `_highlight`, this+0xc0.
    context.AddSprite(_highlight, left, top, right, bottom, color);
}

}  // namespace UI

// ============================================================================
// ?GetBarLayout@ScrollElement@UI@@AEBAXAEAH000@Z             retail 0x18011a2d0
// ============================================================================
// Computes the scrollbar THUMB's rectangle into four out-parameters, whose
// order is (left, top, right, bottom) — read off the four opening stores, which
// take `_layoutLeft` to RDX, `_layoutTop` to R8, `_layoutRight` to R9 and
// `_layoutBottom` to the fifth (stack) argument. `000` in the mangled name is
// "same as the previous parameter" three times, so all four are `int&`.
//
// Fields this body proves:
//     +0x150  bool  vertical
//     +0x15c  int   the inset the track is recessed by at BOTH ends
//     +0x160  float numerator of the travel scale
//     +0x164  float the visible fraction, in [0,1]
//     +0x168  float the total content extent; ZERO is the "nothing to scroll"
//                   sentinel, but only together with fraction == 1.0f
//     +0xf0/+0xf8, +0x100/+0x108, +0x110/+0x118
//                   three inline UI::Sprite values {SpriteSheet*, int index} —
//                   the thumb's cap, body and cap. Their extents are summed to
//                   give the thumb a MINIMUM size.
//
// THE EARLY-OUT IS A CONJUNCTION AND IT IS EASY TO GET BACKWARDS. Retail is
// `if (total == 0.0f) { if (fraction == 1.0f) return; }` — the float constant
// at 0x18016169c reads 00 00 80 3f, i.e. 1.0f, verified by reading the four
// bytes rather than trusting the name. A NaN in either field takes the JP and
// falls through to the general path, which is what the unordered-parity checks
// are for and why this is written with `==` on floats deliberately.
//
// The three sprite extents are read at SpriteInfo +0x20 (height) when vertical
// and +0x1c (width) when not, with the 0x38-byte stride and the `_sprites`
// array head at SpriteSheet +0x20 — all three confirmed by
// src/engine/ui/SpriteSheet.h, whose Wave 7 correction moved width/height
// off the +0x18/+0x1c this body would otherwise have disagreed with.
// A NEGATIVE sprite index contributes ZERO and does not index the array.
namespace {

int32_t ScrollSpriteExtent(const ::Sprite& sprite, bool vertical) {
    const int32_t idx = sprite.spriteIndex;
    if (idx < 0) return 0;                                         // 18011a3a8 JNS / else 0
    // LEFT: UI::Sprite::spriteSheet is void* (Sprite.h, R-UI-b).
    const UI::SpriteSheet* sheet = sprite.spriteSheet;
    // `SpriteInfo` lives at GLOBAL scope in src/engine/ui/SpriteSheet.h, not
    // in namespace UI, and its sizeof is static_asserted there to be 0x38 —
    // which is the stride retail's `IMUL RDX,RDX,0x38` uses.
    const ::SpriteInfo& info = sheet->m_sprites._data[(uint32_t)idx];   // m_sprites +0x20
    return vertical ? info.height : info.width;                    // +0x20 / +0x1c
}

}  // namespace

namespace UI {

// `AEAH000`: four `int&` out-parameters — bare pointers in RDX, R8, R9 and the
// fifth (stack) slot; the member takes them as references.
void ScrollElement::GetBarLayout(int32_t& left, int32_t& top,
                                 int32_t& right, int32_t& bottom) const {
    left   = _layoutLeft;                                          // 18011a2e1
    right  = _layoutRight;                                         // 18011a2e9
    top    = _layoutTop;                                           // 18011a2f2
    bottom = _layoutBottom;                                        // 18011a2f8

    const bool    vertical = _vertical != 0;                       // 18011a2fa  (+0x150)
    const int32_t inset    = _inset;                               // 18011a301  (+0x15c)
    if (vertical) {
        top    += inset;                                           // 18011a309
        bottom -= _inset;                                          // 18011a312  re-read
    } else {
        left  += inset;                                            // 18011a316
        right -= _inset;                                           // 18011a31e  re-read
    }

    const float total    = _maxValue;                              // +0x168
    const float fraction = _displayPercent;                        // +0x164
    if (total == 0.0f && fraction == 1.0f) return;                 // 18011a32c .. 18011a344

    const float scale = _currentValue / total;                     // 18011a35f  (+0x160)

    const int32_t span = vertical ? (bottom - top) : (right - left);            // 18011a368/6f
    const float   spanF   = (float)span;                                        // CVTDQ2PS
    float         thumb   = spanF * fraction;                                   // 18011a395
    float         travel  = spanF - thumb;                                      // 18011a39d

    const int32_t minThumb = ScrollSpriteExtent(_thumbStart,  vertical)
                           + ScrollSpriteExtent(_thumbMiddle, vertical)
                           + ScrollSpriteExtent(_thumbEnd,    vertical);        // 18011a47a
    const float minThumbF = (float)minThumb;
    if (minThumbF > thumb) {                                       // 18011a491 COMISS/JBE
        travel = spanF - minThumbF;                                // 18011a49c
        thumb  = minThumbF;                                        // 18011a499
    }

    const int32_t offset = (int32_t)(travel * scale);              // 18011a4a0 / CVTTSS2SI
    if (vertical) {
        top    += offset;                                          // 18011a4b2
        bottom  = (int32_t)thumb + top;                            // 18011a4b5/bc
    } else {
        left  += offset;                                           // 18011a4c5
        right  = (int32_t)thumb + left;                            // 18011a4c7/cd
    }
}

}  // namespace UI

// ============================================================================
// ?CreateInstance@TextDescription@UI@@QEBAXV?$InternalPtr@VTextElement@UI@@@
//   Object@@PEBVDialog@2@@Z                                  retail 0x18011ef70
// ============================================================================
// RCX = this, RDX = the by-value InternalPtr<TextElement>, R8 = the Dialog* —
// AND R8 IS NEVER READ (see the banner's return-slot audit). Returns void.
//
// Note that the element is RE-READ from the caller's InternalPtr slot before
// every single store (`MOV RCX,qword ptr [RSI]` appears seven times). That is
// not defensive: retail has no reason to believe the slot changed, it simply
// never cached it, and a port that hoists the load is a different function if
// anything below it ever writes through that slot.
//
// *** THE QWORD-FROM-A-DWORD SLOT. *** 0x18011efba and 0x18011efc0 write
// `dword [RSP+0x28]`; 0x18011efcb reads `qword [RSP+0x28]`; 0x18011efd7 stores
// all EIGHT bytes into the element's +0xE0. The upper four bytes are whatever
// the stack held. Reproduced with an uninitialised 64-bit local because the
// WIDTH of the store is the faithful part — exactly the shape
// src/engine/ui/animatedimagedescription.cpp records for
// AnimatedImageDescription::CreateInstance.
namespace UI {

void TextDescription::CreateInstance(Object::InternalPtr<TextElement>* element,
                                     const Dialog* /*dialog — never read*/) const {
    Object::InternalPtr<Element> local(element->Get());            // 18011ef7f
    ElementDescription::CreateInstance(&local);                    // 18011ef92

    TextElement* elem = element->Get();                            // 18011ef97, re-read
    elem->_font = _font;                                           // 18011ef9e  +0xc0 <- +0x68

    StringTable* table = _stringTable2;                            // 18011efa5  +0x70
    uint64_t indexSlot;                                            // [RSP+0x28], see the banner
    if (table != nullptr) {                                        // 18011efac
        const int32_t idx = table->GetIndexFromName(               // 18011efb5
            _name);                                                // +0x78
        __builtin_memcpy(&indexSlot, &idx, sizeof idx);            // 18011efba  DWORD store
    } else {
        const int32_t minusOne = -1;
        __builtin_memcpy(&indexSlot, &minusOne, sizeof minusOne);  // 18011efc0  DWORD store
    }

    elem = element->Get();                                         // 18011efc8
    elem->_text.table = table;                                     // 18011efd0  UI::String.table +0xd8
    // 18011efd7 QWORD store: +0xe0 index AND +0xe4 pad0C (the stack residue).
    __builtin_memcpy(&elem->_text.index, &indexSlot, sizeof indexSlot);

    SetNeedsSizeCalcChain(elem);                                   // 18011efe1

    elem->CacheText();                                             // 18011eff7

    // Five more stores, each through its own re-read of the slot, and note the
    // TWO SEPARATE BYTES at +0xe8 / +0xe9 (the ctor above writes them as one
    // word) and that +0xf4 is written BEFORE +0xf0.
    element->Get()->_drawShadow   = _dropShadow;                          // 18011f00b  +0xe8 <- +0x84
    element->Get()->_wrap         = _wordWrap;                          // 18011f01b  +0xe9 <- +0x85
    element->Get()->_wordWrapSize = _wordWrapSize;                 // 18011f02a  +0xec <- +0x88
    element->Get()->_justify      = static_cast<int32_t>(_justify);    // 18011f039  +0xf4 <- +0x80
    element->Get()->_maxWidth     = _maxWidth;                          // 18011f04d  +0xf0 <- +0x8c
}

}  // namespace UI


// ============================================================================
// ?GetChildLayout@LayoutElement@UI@@AEAAXHHHHHH_N@Z          retail 0x1800d2dc0
// ============================================================================
// The constraint solver for one child of a LayoutElement: given a bounding box,
// resolve the child's X and Y independently, either against the box or against
// ANOTHER already-solved child, and write the resulting rectangle back.
//
// ---------------------------------------------------------------------------
// THE ENTRY TABLE, 0x44 BYTES PER CHILD, DERIVED FROM THE BODY ALONE
// ---------------------------------------------------------------------------
// `this+0xC0` is the array head and the stride is 0x44 (`IMUL R10,R10,0x44`):
//     +0x00  int32  alignX          1..5, anything else means "no adjustment"
//     +0x04  int32  alignY
//     +0x08  int32  refX            index of the sibling to align to; <0 means
//                                   "align to the bounding box instead"
//     +0x0c  int32  refY
//     +0x10  int32  offsetX         added ONLY when the bool argument is true
//     +0x14  int32  offsetY
//     +0x1c  int32  sizeX
//     +0x20  int32  sizeY
//     +0x24  int32  left  [2]
//     +0x2c  int32  top   [2]
//     +0x34  int32  right [2]
//     +0x3c  int32  bottom[2]
// The four TWO-ELEMENT arrays are what pins the stride: the second argument
// indexes them (`[R10 + RBP*0x4 + 0x24]`), it can only be 0 or 1 without the
// four arrays overlapping, and 0x3c + 2*4 == 0x44 exactly. So the second
// argument is a PASS SELECTOR and every child carries two solved rectangles.
// +0x18 is never touched by this body and is not claimed.
//
// The alignment cases, identical in both axes and in both the sibling and the
// bounding-box variants:
//     1  flush against the reference's NEAR edge
//     2  flush against the reference's FAR  edge
//     3  NEAR edge minus this child's size
//     4  FAR  edge minus this child's size
//     5  centred:  ((near + far) >> 1) - (size >> 1)
//     _  no adjustment (the literal zero retail keeps in EDX for the whole
//        function and adds unchanged)
// Note `>>` is retail's `SAR`, an ARITHMETIC shift, so a negative midpoint
// rounds toward negative infinity and NOT toward zero the way a `/ 2` would.
// That is a real difference for a box straddling the origin.
//
// The two sibling reads are `base + ref*0x44 + pass*4 + <edge>` — the SAME
// array this entry lives in, so a child may only align to a sibling in the
// same LayoutElement, and retail RE-READS `this+0xC0` for the Y lookup
// (0x1800d2f8a) rather than keeping the base it already had.
//
// Parameter order, read off the frame: after `PUSH RDI; PUSH R15` the stack
// arguments sit at RSP+0x38, +0x40, +0x48 and +0x50, and they are consumed as
// top, right, bottom and the bool. So the signature is
// (index, pass, left, top, right, bottom, applyOffsets).
namespace {

// One axis, resolved against two reference edges. `nearEdge`/`farEdge` are the
// reference's; `size` is this child's.
inline int32_t AlignAxis(int32_t mode, int32_t nearEdge, int32_t farEdge, int32_t size) {
    switch (mode) {
        case 1:  return nearEdge;                                  // 1800d2e76 / 1800d2ee5
        case 2:  return farEdge;                                   // 1800d2e6a / 1800d2ee1
        case 3:  return nearEdge - size;                           // 1800d2e5a / 1800d2edb
        case 4:  return farEdge  - size;                           // 1800d2e48 / 1800d2ed5
        case 5:  return ((farEdge + nearEdge) >> 1) - (size >> 1);  // 1800d2e2f / 1800d2eca
        default: return 0;                                         // EDX, zero all the way through
    }
}

}  // namespace

namespace UI {

void LayoutElement::GetChildLayout(int32_t index, int32_t pass, int32_t left, int32_t top,
                                   int32_t right, int32_t bottom, bool applyOffsets) {
    ChildRecord* base  = _childRecords._data;                      // 1800d2dcd  (+0xc0)
    ChildRecord* entry = base + index;                             // 1800d2de7  stride 0x44

    // ---- X -----------------------------------------------------------------
    const int32_t refX    = entry->refX;                           // 1800d2df4
    const int32_t sizeX   = entry->sizeX;
    const int32_t alignX  = entry->alignX;
    const int32_t offsetX = applyOffsets ? entry->offsetX : 0;     // 1800d2e05 / 1800d2e8a

    int32_t solvedLeft;
    if (refX < 0) {                                                // 1800d2dfa  JNS
        solvedLeft = offsetX + AlignAxis(alignX, left, right, sizeX);
    } else {
        const ChildRecord* ref = base + refX;                      // 1800d2e9d  IMUL 0x11, *4
        const int32_t refLeft  = ref->left[pass];                  // 1800d2eab
        const int32_t refRight = ref->right[pass];                 // 1800d2ea7
        solvedLeft = offsetX + AlignAxis(alignX, refLeft, refRight, sizeX);
    }
    entry->left[pass] = solvedLeft;                                // 1800d2ef0

    // ---- Y -----------------------------------------------------------------
    const int32_t refY    = entry->refY;                           // 1800d2ef5
    const int32_t sizeY   = entry->sizeY;
    const int32_t alignY  = entry->alignY;
    const int32_t offsetY = applyOffsets ? entry->offsetY : 0;     // 1800d2f07 / 1800d2f7b

    int32_t solvedTop;
    if (refY < 0) {                                                // 1800d2f00  JNS
        solvedTop = offsetY + AlignAxis(alignY, top, bottom, sizeY);
    } else {
        // 1800d2f8a RE-READS this+0xc0 rather than reusing the base above.
        const ChildRecord* base2 = _childRecords._data;
        const ChildRecord* ref   = base2 + refY;
        const int32_t refTop    = ref->top[pass];                  // 1800d2f9d
        const int32_t refBottom = ref->bottom[pass];               // 1800d2f98
        solvedTop = offsetY + AlignAxis(alignY, refTop, refBottom, sizeY);
    }
    entry->top[pass] = solvedTop;                                  // 1800d2fe6

    entry->right[pass]  = entry->sizeX + solvedLeft;               // 1800d2feb/f6
    entry->bottom[pass] = entry->sizeY + solvedTop;                // 1800d2ffb/3002
}

}  // namespace UI

// ============================================================================
// ?SelectCharacter@EditElement@UI@@AEBADH@Z                  retail 0x1800cfd40
// ============================================================================
// Maps an engine key code to the character it types, honouring shift. Returns a
// `char` BY VALUE — `AEBAD`, a private const member returning `D` — so there is
// no hidden slot and RDX is the plain key argument. The banner's audit has the
// full argument for that.
//
// The shift state is read from `Input::Device::_singleton` (0x1801fe770) as
// THREE 16-bit fields: +0xC7C, +0xC94 and +0xCF4. Shift is on if ANY of the
// three is non-zero, and the three are tested with early exits, so a caller
// cannot tell which one fired. They are not named here: this body proves their
// offsets and that they are 16-bit and that any of them means shift, and one
// body is not enough to name a field.
//
// Retail is a 91-entry JUMP TABLE at 0x1800d00ac, indexed by `key - 0x0D` after
// an unsigned `> 0x5A` reject. Every arm is one of exactly two shapes —
// `shift ? A : B` or an unconditional `MOV AL,<c>` — so the table below is the
// jump table with each arm collapsed to its pair, read out of the binary with
// `read_memory` and decoded target by target. A {0,0} row is an arm that jumps
// to the shared `XOR AL,AL ; RET` at 0x1800d00a9.
//
// The two entries that are NOT a plain pair are worth naming because they look
// like transcription slips and are not:
//   * index 45 is `TEST DL,DL ; SETZ AL ; ADD AL,0x3A` — ':' when shifted and
//     ';' when not, i.e. the ordinary pair written as arithmetic.
//   * indices 58 and 75..90 (space and the numeric keypad) ignore shift
//     entirely and are recorded as a pair with both halves equal.
namespace {

struct KeyChar { char shifted; char plain; };

// index = keyCode - 0x0D.
const KeyChar kKeyChars[0x5B] = {
    /* 00 */ {'\x29','\x30'}, {'\x21','\x31'}, {'\x40','\x32'}, {'\x23','\x33'},
    /* 04 */ {'\x24','\x34'}, {'\x25','\x35'}, {'\x5e','\x36'}, {'\x26','\x37'},
    /* 08 */ {'\x2a','\x38'}, {'\x28','\x39'},
    /* 10 */ {'A','a'}, {'B','b'}, {'C','c'}, {'D','d'}, {'E','e'}, {'F','f'},
    /* 16 */ {'G','g'}, {'H','h'}, {'I','i'}, {'J','j'}, {'K','k'}, {'L','l'},
    /* 22 */ {'M','m'}, {'N','n'}, {'O','o'}, {'P','p'}, {'Q','q'}, {'R','r'},
    /* 28 */ {'S','s'}, {'T','t'}, {'U','u'}, {'V','v'}, {'W','w'}, {'X','x'},
    /* 34 */ {'Y','y'}, {'Z','z'},
    /* 36 */ {'~','`'}, {'_','-'}, {'+','='},
    /* 39 */ {0,0}, {0,0},
    /* 41 */ {'{','['}, {'}',']'}, {'|','\\'},
    /* 44 */ {0,0},
    /* 45 */ {':',';'}, {'"','\''},
    /* 47 */ {0,0}, {0,0}, {0,0},
    /* 50 */ {'<',','}, {'>','.'}, {'?','/'},
    /* 53 */ {0,0}, {0,0}, {0,0}, {0,0}, {0,0},
    /* 58 */ {' ',' '},
    /* 59 */ {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0},
    /* 67 */ {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0},
    /* 75 */ {'/','/'}, {'*','*'}, {'-','-'}, {'.','.'}, {'+','+'},
    /* 80 */ {0,0},
    /* 81 */ {'0','0'}, {'1','1'}, {'2','2'}, {'3','3'}, {'4','4'},
    /* 86 */ {'5','5'}, {'6','6'}, {'7','7'}, {'8','8'}, {'9','9'},
};
static_assert(sizeof(kKeyChars) / sizeof(kKeyChars[0]) == 0x5B,
              "SelectCharacter's jump table at 0x1800d00ac has 0x5B entries "
              "(the reject is CMP EAX,0x5A / JA, so 0x00..0x5A inclusive)");

}  // namespace

namespace UI {

char EditElement::SelectCharacter(int32_t key) const {   // `this` is never read
    const ::Input::Device* input = ::Input::Device::_singleton;

    // The WORD compares read State::value (+0x04) of _keyboard[61], [62] and
    // [66] (+0x6c0 + k*0x18): +0xc7c, +0xc94, +0xcf4.
    bool shift = true;                                             // 1800cfd6c MOV DL,1
    if (input->_keyboard[61].value == 0) {                         // 1800cfd4a
        if (input->_keyboard[62].value == 0) {                     // 1800cfd54
            if (input->_keyboard[66].value == 0) shift = false;    // 1800cfd5e / 1800cfd68 XOR DL,DL
        }
    }

    const uint32_t idx = (uint32_t)(key - 0x0d);                   // 1800cfd6e LEA
    if (idx > 0x5au) return '\0';                                  // 1800cfd72 JA (UNSIGNED)

    const KeyChar& kc = kKeyChars[idx];
    return shift ? kc.shifted : kc.plain;
}

}  // namespace UI
