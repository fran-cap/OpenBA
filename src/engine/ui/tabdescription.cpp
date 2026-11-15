// ============================================================================
// src/engine/ui/tabdescription.cpp — UI::TabDescription, the whole class
// ============================================================================
//
// Vtable batch 2. Derives straight from UI::ElementDescription.
//
// ??_7TabDescription@UI@@6B@ (retail 0x1801577f0), ten slots:
//   0  deleting dtor      0x18004c640  the compiler's (sizeof 0xb8)
//   1  TypeName           0x18004c1f0  here
//   2  Type               0x18004c1d0  here
//   3  GetFileVersion     0x1800da8d0  here  (returns 0x104)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800da760  here
//   6  Reconstruct        0x18004c370  here
//   7  Destruct           0x18004c3a0  here
//   8  CreateInstance     0x1800da410  here
//   9  Construct          0x18004c390  here
// plus ??0TabDescription@UI@@QEAA@XZ 0x1800daa00 (out of line) and
// ??1TabDescription@UI@@UEAA@XZ 0x1800da8e0.
//
// LAYOUT: src/engine/ui/TabDescription.h (sizeof 0xb8; RULE B6-8 moved
// this file's offsets there under the retail name). +0xa0 IS NEVER WRITTEN
// BY THE CONSTRUCTOR (0x1800daa9e -> 0x1800daaa5): it is serialized and
// copied to the element's _gap, so a description that was never
// deserialized hands the element whatever the allocator left. Retail's.
//
// ENDGAME B.3.6 block C5: every slot body MOVED into the members of the real
// `UI::TabDescription`. CreateInstance's by-address UpdateContainers (0x1800d9710)
// is now the member call.
//
// TWO NEW COLLECTION HELPERS, both appended to
// src/engine/collection/array.cpp rather than written here (that file is the
// one place the family lives):
//   0x18004cdf0  Array<Tab>::SetSize, two-argument, zeroed tail  -> kSpec_ExtPtrPair10
//   0x18004cc90  ~Array<Tab>, the canonical free-and-zero body   -> J1_ENTRY
// Everything else this class calls was already ported, including
// ?UpdateContainers@TabElement@UI@@ (0x1800d9710, tabelement.cpp) and
// ??0TabElement@UI@@ (0x1800da390, TabElement.h).
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "io/Serializer.h"
#include "ui/TabDescription.h"
#include "ui/TabElement.h"
#include "ui/Dialog.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"
#include "ui/SpriteSheet.h"

#include <new>

#include "io/Stream.h"

namespace {




// One child: create it from `desc` through the dialog, parent it, push it onto
// the element's child array and dirty the chain. Retail writes this out SIX
// times in CreateInstance (three fixed children plus two per tab); `slot` is
// the caller's InternalPtr local, and WHICH local matters — see the note in
// CreateInstance.
void AddChild(const UI::Dialog* dialog, Object::InternalPtr<UI::Element>* slot,
              UI::Element* elem, UI::ElementDescription* childDesc) {
    // ?CreateElement@Dialog@UI@@ 0x180112ed0: RETSLOT, the member's prvalue
    // placed in the caller's slot.
    Object::InternalPtr<UI::Element>* made = slot;
    *made = dialog->CreateElement(Object::ExternalPtr<UI::ElementDescription>(childDesc));

    UI::Element* child = made->Get();
    if (child != nullptr)
        child->_parent = elem;

    rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children, elem->_children.GetSize() + 1, made);

    for (UI::Element* n = elem; n != nullptr; n = n->_parent)
        n->_flags |= UI::kElementFlagNeedsSizeCalc;
}

}  // namespace



// ============================================================================
// ============================================================================
// The MEMBERS
// ============================================================================
namespace {
// ?GetSpriteIndexFromName@SpriteSheet@UI@@ (0x1800031f0), called directly.
int32_t GetSpriteIndexFromName(const UI::SpriteSheet* sheet, const System::String* name) {
    return sheet->GetSpriteIndexFromName(*name);
}
}  // namespace

namespace UI {

// [3] 0x1800da8d0
int32_t TabDescription::GetFileVersion() const { return 0x104; }

// [9] 0x18004c390 — the ctor, placement (the null test sat in the retired door).
Object::External* TabDescription::Construct() {
    return ::new (this) TabDescription();
}
// [6] 0x18004c370 — destroy in place through slot 0 (flag 0), re-run the ctor.
Object::External* TabDescription::Reconstruct() {
    this->~TabDescription();
    return ::new (this) TabDescription();
}
// [7] 0x18004c3a0
void TabDescription::Destruct() { this->~TabDescription(); }

// ============================================================================
// [5] ?Serialize@TabDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800da760
// ============================================================================
// The scalar count is read into a LOCAL (`MOV EAX,[RDI+0xb0]` ->
// `MOV [RSP+0x50],EAX`) and the local is what gets serialized and what the
// read-path SetSize and the element loop then use — not `_tabs._size`, which
// SetSize is in the middle of changing.
bool TabDescription::Serialize(IO::Stream& streamArg) {
    IO::Stream* stream = &streamArg;

    // QUALIFIED: this object wears the LEAF's table, whose slot 5 is this body.
    ElementDescription::Serialize(*stream);                              // 1800da77a

    // The inlined ??0Serializer (0x1800b9960): CreateReader / CreateWriter
    // of the stream's encoding, version 0.
    IO::Serializer ser(*stream, 0);
    IO::ReaderWriter* const rw = ser._readerWriter;

    Object::SerializeExternal<UI::ElementDescription>(ser, _stripBackdropDescription);   // 1800da7b8
    Object::SerializeExternal<UI::ElementDescription>(ser, _pageBackdropDescription);    // 1800da7c6
    Object::SerializeExternal<UI::ElementDescription>(ser, _pageFrameDescription);       // 1800da7d4
    Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet);            // 1800da7e5 ExternalPtr<SpriteSheet>
    _edgeLeftName.Serialize(ser);           // 1800da7f6
    _edgeMiddleName.Serialize(ser);         // 1800da807
    _edgeRightName.Serialize(ser);          // 1800da818

    rw->SerializeInt(_gap, nullptr);

    int32_t n = _tabsCount;                                             // 1800da835
    rw->SerializeInt(n, nullptr);

    if (ser._stream->_direction == 0) {                                        // 1800da85a (re-read)
        rtcoll::RtColl_ArraySetSize_18004cdf0(&_tabsData, n);               // 1800da86b
    }

    // The pair loop: BOTH halves of every element, through ONE array-data
    // pointer read BEFORE the loop (0x1800da870) — retail does not re-read it,
    // which is safe here only because SetSize has already finished.
    ElementDescription** tab = _tabsData;
    for (int32_t i = 0; i < n; ++i, tab += 2) {                         // 1800da87d / 1800da8a3, 0x10 per pair
        Object::SerializeExternal<UI::ElementDescription>(ser, tab[0]);         // 1800da889
        Object::SerializeExternal<UI::ElementDescription>(ser, tab[1]);           // 1800da897
    }

    // The inlined ~Serializer deletes the ReaderWriter (1800da8ad).
    return true;
}

// ============================================================================
// [8] ?CreateInstance@TabDescription@UI@@...   retail 0x1800da410
// ============================================================================
// TabElement is 0x108 bytes (`MOV EDX,0x108` at 0x1800da454) and its ctor is
// CALLED out of line (0x1800da390, TabElement.h).
//
// SIX child creations: the three fixed sub-elements from +0x68/+0x70/+0x78,
// then per tab the pair at tabs[i][0] and tabs[i][8].
//
// *** THE SECOND CHILD OF EACH TAB USES A DIFFERENT STACK SLOT. *** Five of the
// six `Dialog::CreateElement` calls pass `LEA RDX,[RSP+0x70]`; the one at
// 0x1800da604 passes `LEA RDX,[RSP+0x78]`. Those are two distinct
// InternalPtr<Element> locals, so the two are kept as two locals here. Using one
// would make the second call overwrite a slot retail leaves alone, and
// InternalPtr construction is not free of side effects on the handle table.
//
// Then three sprite lookups with the SAME dword-written/qword-read slot the
// Image and Slider appliers have, and finally +0xc0/+0xc4 from the description's
// +0xb0/+0xa0 — note the CROSS: count goes to _tabCount and the never-initialised
// +0xa0 goes to _gap.
Object::InternalPtr<Element>* TabDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = System::MemoryAllocator::MemAlloc(0x108, 8);          // 1800da454 (inlined)

    TabElement* elem = nullptr;
    if (raw != nullptr)                                                 // 1800da481
        elem = ::new (raw) TabElement();                                // 1800da486 ??0 (out of line)

    Object::InternalPtr<Element> slotA(elem);                           // [RSP+0x70]
    Object::InternalPtr<Element> slotB;                                 // [RSP+0x78]

    // The NON-virtual ElementDescription::CreateInstance(InternalPtr<Element>)
    // (0x180115ad0): a QUALIFIED member call since fragment pass F2.
    UI::ElementDescription::CreateInstance(&slotA);                     // 1800da4a0

    AddChild(dialog, &slotA, elem, _stripBackdropDescription);          // 1800da4b1
    AddChild(dialog, &slotA, elem, _pageBackdropDescription);           // 1800da502
    AddChild(dialog, &slotA, elem, _pageFrameDescription);              // 1800da552

    const int32_t tabs = _tabsCount;
    for (int32_t i = 0; i < tabs; ++i) {                                // 1800da596 / 1800da658
        ElementDescription* const* row = _tabsData + i * 2;             // 1800da5a8
        AddChild(dialog, &slotA, elem, row[0]);                         // 1800da5bb
        // THE OTHER SLOT — see the banner.
        ElementDescription* const* row2 = _tabsData + i * 2;            // 1800da5f8 (re-read)
        AddChild(dialog, &slotB, elem, row2[1]);                        // 1800da60c
    }

    // The three {sheet, index} pairs. ONE stack slot, written 4 bytes wide and
    // read 8 — the same deliberate width mismatch imagedescription.cpp records.
    uint64_t idxSlot;                                                   // [RSP+0x30]
    const System::String* const names[3] = { &_edgeLeftName, &_edgeMiddleName, &_edgeRightName };
    ::Sprite* const sprites[3] = { &elem->_edgeLeft, &elem->_edgeMiddle, &elem->_edgeRight };
    for (int i = 0; i < 3; ++i) {
        SpriteSheet* sheet = _spriteSheet;                              // RE-READ each time
        if (sheet != nullptr) {
            const int32_t idx = GetSpriteIndexFromName(sheet, names[i]);
            __builtin_memcpy(&idxSlot, &idx, sizeof idx);               // DWORD store
        } else {
            const int32_t minusOne = -1;
            __builtin_memcpy(&idxSlot, &minusOne, sizeof minusOne);
        }
        // One QWORD store over `spriteIndex` and `pad0C`: the index in the low
        // dword, the slot's never-written high dword above it.
        sprites[i]->spriteSheet = sheet;
        sprites[i]->spriteIndex = static_cast<int32_t>(idxSlot);
        sprites[i]->pad0C       = static_cast<int32_t>(idxSlot >> 32);
    }

    elem->_tabCount = _tabsCount;                                       // 1800da71e
    elem->_gap      = _gap;                                             // 1800da72a

    elem->UpdateContainers();                                           // 1800da733 (was by address)

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800da738
    return ret;
}

// ??1TabDescription@UI@@UEAA@XZ   0x1800da8e0 — retail's body, moved from the
// retired door (ENDGAME B.3.9 UI-B): destroy the tab Array (0x18004cc90 —
// frees the buffer and does NOT visit the pairs), then the three Strings in
// REVERSE order (+0x98, +0x90, +0x88). The restamps, the base's _textName
// (+0x60) and the tail JMP into ~External are the compiler's chain. The four
// ExternalPtrs at +0x68..+0x80 are not released.
TabDescription::~TabDescription() {
    rtcoll::RtColl_ArrayDtor_18004cc90(&_tabsData);           // 1800da90c, +0xa8
    // B4 P2 H5: the three releases (MemFree at 1800da937 / 969 / 99b) are the
    // members ~String (0x180006c80), run after this body in reverse order.
}

// ?CreateNewObject@TabDescription@UI@@SAPEAVBase@Object@@XZ 0x18004c2e0 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0xb8, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* TabDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TabDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) TabDescription();
}
// ?CreateInPlaceObject@TabDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x18004c360
Object::Base* TabDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) TabDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7TabDescription@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// TabDescription now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::TabDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@TabDescription@UI@@ (0x1801fa430): name "TabDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@TabDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::TabDescription, "ElementDescription", nullptr)
