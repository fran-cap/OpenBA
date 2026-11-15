// ============================================================================
// src/engine/ui/widgets_layout.cpp -- Wave 7 lane E -- Table/Image/Label/Text/
// DocumentText/Tab/Page/Graph
// ============================================================================
//
// Ports the 20 (of 24) `docs/history/runtime_port/wave7_lanes.txt` LANE E exports this
// lane could verify. Layouts and the evidence for every offset are in
// src/engine/ui/{TableElement,TextElement,DocumentTextElement,TabElement,
// PageElement,GraphElement,ImageElement,LabelElement}.h; the per-function
// write-up, the oracle argument, and everything held back are in
// docs/history/runtime_port/WAVE7_LANE_E.md.
//
// The layout contract the eight type headers were recovered under is
// docs/history/runtime_port/WAVE7.md, "cross-lane layout contract".
// ============================================================================
//
// ENDGAME B.3 step 6, FRAGMENT PASS F1 (2026-09-25): every body below now
// lives in its class's MEMBER — `Element::RemoveChildren` and the eight leaf
// classes' non-virtual members declared in src/engine/ui/{Element,
// TableElement,ImageElement,LabelElement,TextElement,DocumentTextElement,
// TabElement,PageElement,GraphElement}.h — and reaches the fields by name
// instead of by raw offset. The by-value 16-byte `UI::Sprite` /
// `UI::String` and the `InternalPtr<Element>` arguments arrive by HIDDEN
// POINTER (RULE B6-11), so those members take the pointer.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#include "system/String.h"
#include "rt_sink.h"
#include "ui/Element.h"
#include "object/VSlot.h"
#include "ui/TableElement.h"
#include "ui/ImageElement.h"
#include "ui/LabelElement.h"
#include "ui/TextElement.h"
#include "ui/DocumentTextElement.h"
#include "ui/TabElement.h"
#include "ui/PageElement.h"
#include "ui/GraphElement.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_I32 etc.
#include "collection/Collection.h"
#include "element_common.h"

namespace {

// Every mutator in this lane, having changed its own state, walks `this` then
// its parent chain OR-ing this bit into the dirty-flags word at +0x34. This
// lane found the field independently (eight bodies: TableElement::SetSize,
// ImageElement::SetSprite/SetWidthHeight, TextElement::SetParameter/SetText,
// DocumentTextElement::SetText x2, PageElement::SetActivePage) before lane A's
// Element.h landed; it is UI::Element::_flags / kElementFlagNeedsSizeCalc and
// UI::Element::_parent.
inline void WalkDirty(UI::Element* self) {
    for (UI::Element* node = self; node; node = node->_parent)
        node->_flags |= UI::kElementFlagNeedsSizeCalc;
}

// ---- Helpers this lane calls through rather than reimplements --------------
//
// ?RemoveChildren@Element@UI@@QEAAXXZ (0x180115570) is `UI::Element::
// RemoveChildren`, defined near the end of this file; TableElement::SetSize
// calls it as a member.
//
// ?CacheText@TextElement@UI@@AEAAXXZ (0x18011ec10) is `UI::TextElement::
// CacheText` (textelement_cachetext.cpp); called as a member.
//
// ?UpdateContainers@TabElement@UI@@AEAAXXZ (0x1800d9710) is `UI::TabElement::
// UpdateContainers` (tabelement.cpp); called as a member.

// ?MemFree@MemoryAllocator@System@@SAXPEAX@Z (0x1800c7ad0), called by name.
void RtMemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// ENDGAME B.3.2 step 2 — the `RtArrayInt32SetSize`/`RtArrayCellsSetSize`
// forwarders that used to sit here are GONE. `TableElement`'s row/col-size
// arrays and `Element::_children` bridge through their row with
// `rtcoll::AsArray`:
//
//   0x18002fea0  kSpec_I32      TRAIT-EQUIVALENT for int32_t (tail left
//                UNINITIALISED, matching `SpecFor<int32_t>()`'s empty `init`)
//   0x1800764c0  kSpec_PtrZero  NOT trait-equivalent: its `InitZero8` ZEROES
//                each new tail slot, which a trait-derived `Array<Element*>`
//                (a pointer is trivially default-constructible, so the trait
//                leaves the tail UNINITIALISED) would not do.
inline void RtArrayInt32SetSize(void* arrayHeader, int32_t n) {
    rtcoll::AsArray<&rtcoll::kSpec_I32>(arrayHeader)->SetSize(n);
}
inline void RtArrayCellsSetSize(void* arrayHeader, int32_t n) {
    rtcoll::AsArray<&rtcoll::kSpec_PtrZero>(arrayHeader)->SetSize(n);
}

// ENDGAME B.3.2 step 2 — left as a free-function call, deliberately.
// 0x1800416d0 is array_destroy.cpp's J2_ENTRY driving `FreeInnerArraysCounted`
// off a `DestroySpec` (`kD_InnerArr_18`), a different type/dispatch from the
// `rtcoll::ArraySpec` a `Collection::Array<T>::~Array()` would call, and no
// `kSpec_*` row exists for it — per the batch rule, a "DtorElem" helper may
// not be convertible at all. Called by name (B3R RT-UI).
void RtGraphArrayClear(void* arrayHeader) {
    rtcoll::RtColl_ArrayDtorElem_1800416d0(arrayHeader);
}

// TabElement::UpdateContainers (0x1800d9710) dirty-walks the _parent chain of
// each container whose hidden bit flips, the graph shape
// docs/history/runtime_port/WAVE7_LANE_B.md §2b describes for SetUpperLeft; its
// body is in src/engine/ui/tabelement.cpp.

}  // namespace

namespace UI {

// ============================================================================
// UI::TableElement
// ============================================================================
// GetRowCount / GetColumnCount are one instruction each and live inline in
// TableElement.h.

// ?SetChild@TableElement@UI@@QEAAXV?$InternalPtr@VElement@UI@@@Object@@H@Z
// 0x18011daa0 — the handle arrives as the ADDRESS of the caller's copy.
void TableElement::SetChild(Object::InternalPtr<Element>* child, int32_t index) {
    Element* elem = child->Get();
    if (elem) elem->_parent = this;

    Element** cellSlot = _children._data + index;
    Element* old = *cellSlot;
    if (old != elem) {
        if (old) {
            // Slot 0 with flag 1 — the scalar deleting destructor, the
            // InternalPtr<T> "release and possibly delete" idiom
            // (src/engine/object/Internal.h's vtable slot 0).
            delete old;
            *cellSlot = nullptr;
        }
        WalkDirty(this);
    }
    *cellSlot = elem;

    // NO ORACLE for the branch that matters: the release path destroys the
    // previous occupant, and there is no safe way to re-exercise it against a
    // second implementation. See WAVE7_LANE_E.md sec4a.
}

// ?SetChild@TableElement@UI@@QEAAXV?$InternalPtr@VElement@UI@@@Object@@HH@Z
// 0x18011db50 — pure delegate to the overload above, `_colCount*col + row`.
void TableElement::SetChild(Object::InternalPtr<Element>* child, int32_t column,
                            int32_t row) {
    SetChild(child, _columnCount * column + row);
}

// ?SetSize@TableElement@UI@@QEAAXHH@Z  0x18011db90
void TableElement::SetSize(int32_t rows, int32_t columns) {
    _rowCount = rows;
    _columnCount = columns;
    RtArrayInt32SetSize(&_rowSizes, rows);
    RtArrayInt32SetSize(&_columnSizes, columns);
    RemoveChildren();
    RtArrayCellsSetSize(&_children, rows * columns);
    WalkDirty(this);
}

// ============================================================================
// UI::ImageElement
// ============================================================================

// 16, not UI::Sprite.h's kSpriteWrittenBytes (0x0C): that constant bounds what
// a Sprite CONSTRUCTOR writes, and this function is not one -- it is a COPY,
// proven at the instruction level (0x1801182a0: two independent 8-byte
// `MOV RAX,[..]`/`MOV [..],RAX` pairs, not a 12-byte-then-stop sequence). The
// implicit copy of `UI::Sprite` moves all four fields, pad0C included.
Sprite ImageElement::GetSprite() const {
    return _sprite;
}

// ?SetRotation@ImageElement@UI@@QEAAXM@Z  0x1801182c0
void ImageElement::SetRotation(float degrees) {
    _rotation = degrees;
    // No dirty-flag walk in retail -- confirmed from the disassembly, not
    // assumed by analogy with the other setters in this file.
}

// ?SetSprite@ImageElement@UI@@QEAAXVSprite@2@@Z  0x180118600. `VSprite@2@`
// BY VALUE is a 16-byte class, which retail passes by HIDDEN POINTER (RULE
// B6-11). 16 bytes throughout: this function COPIES an existing Sprite value
// rather than constructing one, and retail's own copy is two unconditional
// 8-byte `MOV RAX,[..]`/`MOV [..],RAX` pairs -- the full 16, not a 12-then-stop.
void ImageElement::SetSprite(const Sprite* sprite) {
    const bool differs = (_sprite.spriteSheet != sprite->spriteSheet) ||
                         (_sprite.spriteIndex != sprite->spriteIndex);
    if (differs) WalkDirty(this);
    _sprite = *sprite;
}

// ?SetWidthHeight@ImageElement@UI@@QEAAXHH@Z  0x180118260
void ImageElement::SetWidthHeight(int32_t width, int32_t height) {
    const bool differs = (_width != width) || (_height != height);
    if (differs) {
        _width = width;
        _height = height;
        WalkDirty(this);
    }
}

// ============================================================================
// UI::LabelElement
// ============================================================================

// 16, not kSpriteWrittenBytes -- same "copy, not construction" reasoning as
// ImageElement::GetSprite above; retail's copy (0x1800429d0) is likewise two
// unconditional 8-byte MOV pairs.
Sprite LabelElement::GetSprite() const {
    return _sprite;
}

// 16, not kSpriteWrittenBytes -- same "copy, not construction" reasoning as
// ImageElement::SetSprite above; retail's copy (0x180042980) is likewise two
// unconditional 8-byte MOV pairs. The Sprite arrives by hidden pointer.
void LabelElement::SetSprite(const Sprite* sprite) {
    const bool differs = (_sprite.spriteSheet != sprite->spriteSheet) ||
                         (_sprite.spriteIndex != sprite->spriteIndex);
    if (differs) WalkDirty(this);
    _sprite = *sprite;
}

// ============================================================================
// UI::TextElement
// ============================================================================

// ?SetParameter@TextElement@UI@@QEAAXHAEBVString@System@@@Z  0x18011e9b0
//
// NO ORACLE. `str`'s assignment into the slot is a REAL refcount
// acquire/release on a shared System::String, so restoring the slot and
// re-running retail could be a use-after-free rather than a diagnostic, and
// running retail after ours without restoring is vacuous. See
// WAVE7_LANE_E.md sec4a.
void TextElement::SetParameter(int32_t index, const System::String& value) {
    if (index >= 0 && index < _params._size) {
        WalkDirty(this);
        _params._data[index] = value;          // ??4String@System@@ 0x1800c9810
        CacheText();
    }
}

// ?SetText@TextElement@UI@@QEAAXVString@2@@Z  0x180042070. The 0x10-byte
// UI::String BY VALUE arrives by HIDDEN POINTER (RULE B6-11). NO ORACLE, same
// reasoning as SetParameter above (CacheText's refcount management on the
// cached-text pointer at +0xF8).
void TextElement::SetText(const String* text) {
    _text = *text;
    WalkDirty(this);
    CacheText();
}

// ============================================================================
// UI::DocumentTextElement
// ============================================================================

// ?GetLink@DocumentTextElement@UI@@QEBA?AVString@System@@H@Z  0x18003c0c0 —
// RETSLOT. The link array at +0x158 has 0x18-byte entries, the target String
// at +0x00; the result is that String with its reference bumped, or an empty
// String if the slot is null — exactly System::String's copy constructor
// (Adopt: null stays null, non-null gets `++refcount`). No bounds check on
// `index` (matches SpriteSheet::GetSpriteNameFromIndex's pattern).
System::String DocumentTextElement::GetLink(int32_t index) const {
    return _links._data[index].text;      // the copy ctor: adopt (null stays null)
}

// ?SetText@DocumentTextElement@UI@@QEAAXVString@2@@Z  0x18003bfd0. UI::String
// is a POD, non-refcounted 16-byte value, passed by HIDDEN POINTER (RULE
// B6-11). Retail's own copy is two unconditional 8-byte MOV pairs
// (`MOV RAX,[RDX]`/`[RCX+0xc0],RAX` then `+0x8`/`+0xc8`) -- the full 16.
void DocumentTextElement::SetText(const UI::String* text) {
    _text = *text;
    WalkDirty(this);
}

// ?SetText@DocumentTextElement@UI@@QEAAXAEBVString@System@@@Z  0x18003c010
void DocumentTextElement::SetText(const System::String& text) {
    // Reset the by-value UI::String slot: table=0, index=-1. Retail's own
    // codegen (`CONCAT44(uStack_c, 0xffffffff)`) writes the qword's high
    // dword from an UNINITIALIZED stack local -- i.e. retail's own value at
    // +0xCC is non-deterministic. Reproducing a specific number there would
    // be inventing a fact retail does not have; only the low dword (the
    // actual -1 index reset) is written here. See WAVE7_LANE_E.md sec6.
    _text.table = nullptr;
    _text.index = -1;

    char* oldStr = _sourceText._data;
    char* newStr = text._data;
    if (oldStr != newStr) {
        if (oldStr) {
            int32_t* rc = &RtSystem::StringRefCount(oldStr);
            *rc -= 1;
            if (*rc == 0) {
                RtMemFree(oldStr - 8);
            }
            _sourceText._data = nullptr;
        }
        _sourceText._data = newStr;
        if (newStr) RtSystem::StringRefCount(newStr) += 1;
    }
    WalkDirty(this);

    // NO ORACLE for the refcount swap (see TextElement::SetParameter above).
}

// ============================================================================
// UI::TabElement / UI::PageElement
// ============================================================================

// ?SetActiveTab@TabElement@UI@@QEAAXH@Z  0x18004c890 — guard, store, then the
// private (exported) UpdateContainers.
void TabElement::SetActiveTab(int32_t tab) {
    if (tab != _activeTab) {
        _activeTab = tab;
        UpdateContainers();
    }
}

// ?SetActivePage@PageElement@UI@@QEAAXH@Z  0x1800d5110
void PageElement::SetActivePage(int32_t page) {
    const int32_t count = _children._size;
    const int32_t current = _activePage;
    if (page > -2 && page < count && page != current) {
        _activePage = page;
        WalkDirty(this);
        Element** data = _children._data;
        for (int32_t i = 0; i < count; ++i) {
            Element* child = data[i];
            if (!child) continue;
            const uint32_t flags = child->_flags;
            const uint32_t updated = (i == page) ? (flags & ~kElementFlagHidden)
                                                 : (flags | kElementFlagHidden);
            if (updated != flags) {
                child->_flags = updated;
                WalkDirty(child);
            }
        }
    }
}

// ============================================================================
// UI::GraphElement
// ============================================================================

// ?RemoveGraphs@GraphElement@UI@@QEAAXXZ  0x180040610 — tail-calls the
// destroy-and-free helper on the series array.
void GraphElement::RemoveGraphs() {
    RtGraphArrayClear(&_seriesData);
}

// ============================================================================
// ?AddGraphPoint@GraphElement@UI@@QEAAXHH@Z  retail 0x180040690  ord 1561
//                                                          (WAVE 9 LANE UIF)
// ============================================================================
//
// THE BODY, 12 instructions at 0x180040690:
//
//     AddGraphPoint(this, int graph, int value):
//         [home slot] = value                 ; spilled so R8 can point AT it
//         RCX = this->[0xf0]                  ; the graph-series array's data
//         RAX = (int64)graph                  ; MOVSXD — SIGNED widening
//         RDX = RAX * 3
//         RCX = RCX + 8 + RDX*8               ; = data + graph*0x18 + 8
//         EDX = RCX->[8] + 1                  ; the point array's size, plus one
//         R8  = &value
//         JMP/CALL 0x180023830                ; Array<uint32>::SetSize(n, &fill)
//
// So a series record is 0x18 bytes (RDX = RAX*3 then scaled by 8) and the
// per-series point Array lives at record+8. `SetSize(size+1, &value)` is this
// engine's APPEND idiom: grow by one and fill the new tail from `*fill`.
//
// THE MOVSXD IS REPRODUCED AS A SIGNED WIDENING, deliberately (Ghidra's C
// prints it as a zero extension — reference_ghidra_c_output_vs_disassembly.md).
// A negative `graph` indexes backwards exactly as retail does. NOT guarded.
//
// ORACLE: none — a replay would append the point a SECOND time.
void GraphElement::AddGraphPoint(int32_t graph, int32_t value) {
    // record = data + graph*0x18; the point Array header sits at record + 8
    // (GraphSeries::points).
    Collection::Array<int32_t>* points = &_seriesData[static_cast<int64_t>(graph)].points;
    points->SetSize(points->GetSize() + 1, value);
}

// ============================================================================
// ?AddGraph@GraphElement@UI@@QEAAXI@Z  retail 0x180040620  (WAVE 9 LANE UIW)
// ============================================================================
//
// THE BODY, 0x180040620..0x18004068b:
//
//     AddGraph(this, uint32 colour):
//         old = (int64)this->_graphs._size;      ; MOVSXD from [RCX+0xf8]
//         uint8_t tmp[0x18];                     ; at [RSP+0x28]
//         *(uint64*)(tmp+0x08) = 0;              ; the nested Array header,
//         *(uint64*)(tmp+0x10) = 0;              ;   zeroed; tmp+0..7 is NOT
//         0x1800417d0(&this->_graphs, _graphs._size + 1, tmp);
//         0x18001c750(tmp + 8);                  ; ~Array on the temp
//         this->_graphs._data[old].colour = colour;
//
// THREE THINGS WORTH THE INK:
//
//  1. **`tmp + 0x00 .. 0x07` IS DELIBERATELY UNINITIALISED.** Retail's fill
//     lives at `[RSP+0x28]` and only `[RSP+0x30]` and `[RSP+0x38]` are zeroed.
//     The LAST line of the function overwrites the copied colour with the real
//     `colour`, so nothing observable depends on those bytes.
//  2. **The temp's nested Array IS destroyed afterwards, and it is empty.** It
//     is reproduced because it is a real call retail makes.
//  3. **`old` IS TAKEN BEFORE THE APPEND AND SIGN-EXTENDED.** The final store
//     indexes `_data + old*0x18` with no bounds check, exactly as retail does.
//
// NO ORACLE, for the same reason as AddGraphPoint above.
void GraphElement::AddGraph(uint32_t colour) {
    const int64_t old = _seriesCount;                      // MOVSXD

    // B4 Y RT-UI: the fill is a GraphSeries local in its own block.
    // GraphSeries has no constructor of its own, so `colour` / `hole04`
    // (tmp+0x00..0x07) stay uninitialised — see (1) above — and `points`'
    // Array() is the two zero stores at tmp+0x08 / tmp+0x10. Its implicit
    // destructor at the closing brace is `points`' ~Array (0x18001c750, a J1,
    // trait-equivalent for `int32_t`), before the colour store, as in retail.
    {
        GraphSeries tmp;

        // ENDGAME B.3.2 step 2 — `_seriesData` is GraphElement.h's raw
        // `Array<GraphSeries>` header (NON-TRIVIAL `kSpec_GraphSeries`), so it
        // bridges through the hand-derived row.
        rtcoll::AsArray<&rtcoll::kSpec_GraphSeries>(&_seriesData)
            ->SetSizeFill(_seriesCount + 1, &tmp);
    }

    _seriesData[old].colour = colour;
}

// ============================================================
// UI::Element::RemoveChildren  retail 0x180115570  (Wave 9 lane B3)
// ============================================================
//
//   for (i = 0; i < this->_children.size; ++i) {
//       Element** data = this->_children.data;      // RE-READ every iteration
//       if (data[i]) { data[i]->vtbl[0](data[i], 1); data[i] = nullptr; }
//   }
//   Collection::Array<Element*>::SetSize(&this->_children, 0);   // TAIL JMP
//
// Slot 0 called with a flag of 1 is the scalar deleting destructor -- destroy
// AND free -- the idiom src/engine/object/Internal.h describes at its vtable
// slot 0. It is NOT `??1Element`.
//
// THE RE-READ OF `_children.data` INSIDE THE LOOP IS LOAD-BEARING, and is why
// this is an index loop over a refetched base rather than the obvious cached
// pointer: retail reloads `[RBP + 0xa0]` at 0x1801155a0 on every iteration,
// after the previous child's destructor has run.
//
// The loop bound, by contrast, IS hoisted in retail -- `_children.size` is read
// into EBX's comparand from `[RBP+0xa8]` each iteration (0x1801155ca) but the
// SetSize that changes it runs only after the loop. Reading it once here is the
// same sequence of accesses for any object whose destructors do not resize the
// parent, and for one that does, retail's per-iteration reload would be the
// faithful spelling -- noted rather than silently chosen, because it is the one
// place this port is not instruction-for-instruction.
//
// NO ORACLE, stated rather than glossed: the function DESTROYS the children.
void Element::RemoveChildren() {
    const int32_t count = _children._size;

    for (int32_t i = 0; i < count; ++i) {
        Element** data = _children._data;              // re-read, see above
        Element* child = data[i];
        if (child == nullptr) continue;
        delete child;                        // slot 0, flag 1
        data[i] = nullptr;
    }
    RtArrayCellsSetSize(&_children, 0);
}

}  // namespace UI

// (B3R RT-UI: the 23 one-line doors that sat here -- Table/Image/Label/Text/
// DocumentText/Tab/Page/Graph element setters and getters and
// Element::RemoveChildren -- went with their registrations; nothing called
// them. The members are the bodies.)

