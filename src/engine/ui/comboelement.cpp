// ============================================================================
// src/engine/ui/comboelement.cpp — UI::ComboElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7ComboElement@UI@@6B@` (retail 0x180159030),
// a `UI::ButtonElement` subclass whose Update opens a popup list.
//
//   slot  method                       retail       where
//   ----  ---------------------------  -----------  --------------------------
//    0    vector deleting dtor         0x180075390  ComboElement.h (the class's own)
//    1    TypeName                     0x180074f30  here
//    2    Type                         0x180074f10  here
//    4    ButtonElement::ComputeSize   0x180110d10  buttonelement.cpp
//    5    ButtonElement::ComputeLayout 0x180110e70  buttonelement.cpp
//    6    ButtonElement::Draw          0x180111020  buttonelement.cpp
//    7    Update                       0x1801125d0  here
//    8    ButtonElement::Enable        0x180110cc0  buttonelement.cpp
//
// plus ??1ComboElement@UI@@UEAA@XZ 0x180112490 and the factory pair (ENDGAME
// B.3.9 UI-A: the real ctor ??0 0x1801123f0 is in the header; the copy-ctor
// door is retired).
//
// ENDGAME B.3.6 block C1: the three slot bodies are MEMBERS of
// `UI::ComboElement` (src/engine/ui/ComboElement.h, which now owns the
// whole 0x128-byte layout, including the two description handles this file
// used to carry as file-local constants). `?AddItem@`, `?RemoveAllItems@`,
// `?SetCurrentItem@`, the three getters and `?UpdateText@` are declared on the
// class and stay DOORS in src/engine/ui/widgets_input.cpp (fragment pass).
// ============================================================================
#include "element_common.h"
#include "ui/ComboElement.h"
#include "ui/ComboDialog.h"
#include "ui/Dialog.h"
#include "ui/ListElement.h"   // B.3.9 TI-UI: X::TypeId()
#include "ui/TextElement.h"
#include "ui/LabelElement.h"   // B4: the item's +0x100 _sprite
#include "ui/UserInterface.h"
#include "system/String.h"
#include "collection/Collection.h"
#include <new>

using namespace rtuielem;

namespace {

// UI::TextElement (block C3's class), the per-item element the list is filled
// with — reached at raw offsets.

// UI::ListElement (block C4's class)

// UI::DialogInstance (block B's class)

// UI::UserInterface (block E's): the open-dialog array at +0x28 {data, size, capacity}.

constexpr int kDialogSize = 0xd0;

// Element::_flags bits Element.h does not name (see buttonelement.cpp).
constexpr uint32_t kFlagHover        = 0x00020000u;
constexpr uint32_t kFlagPressStarted = 0x00200000u;

// ENDGAME B.3.9 TI-UI: the two type ids Update tests against are
// `UI::ListElement::TypeId()` / `UI::TextElement::TypeId()`, read at the use.

}  // namespace

namespace UI {

// ============================================================================
// ?Update@ComboElement@UI@@   0x1801125d0
// ============================================================================
// On a click, build the popup: instantiate the list description, instantiate
// one item description per entry, fill each item's text and sprite, add it to
// the list, then wrap the list in a `UI::ComboDialog` and push that dialog onto
// the UserInterface. Then run ButtonElement::Update (QUALIFIED) either way.
//
// ---------------------------------------------------------------------------
// THE STACK `UI::Dialog` IS RETAIL'S, AND IT IS THE ARGUMENT EVERY
// CreateInstance RECEIVES.
// ---------------------------------------------------------------------------
// Retail constructs a 0xd0-byte `UI::Dialog` IN THE FRAME (the three vtable
// stamps at 0x180112646/0x180112652/0x180112675 are Base, External and
// `??_7Dialog@UI@@6B@`), hands its address to both descriptions, and destroys
// it before returning. The constructor and destructor are INLINED there;
// `??0Dialog@UI@@QEAA@XZ` (0x180112db0) and `??1Dialog@UI@@UEAA@XZ`
// (0x180112e40) are the SAME two bodies out of line — src/engine/ui/
// dialog_ctor_dtor.cpp transcribes them instruction for instruction — so they
// are CALLED here rather than re-inlined. That is the same deliberate departure
// src/engine/object/deleting_dtor.cpp's banner records for its 22 inlined
// destructors: identical computation, one extra call.
//
// The two descriptions' slot 8 is a VIRTUAL call on `UI::ElementDescription`
// through the description's own (minted) table, with the hidden return slot in
// retail order (RULE B6-2).
//
// ---------------------------------------------------------------------------
// FOUR THINGS THE BYTES SAY
// ---------------------------------------------------------------------------
//  1. `IsDerivedFrom` failing yields a NULL that is then dereferenced —
//     `pLVar10` for the list (0x1801127fc reads `+0xe8` off it) and `pDVar` for
//     each item. Retail relies on the descriptions being the right types. No
//     guard is added.
//  2. The item's text is written into `_parameters[i]` — the TextElement's
//     parameter array at +0xc8 — and ONLY when that array is non-empty
//     (`if (_paramCount > 0)`). An item description with no parameter slot
//     silently shows its own default text.
//  3. The dirty walk around the text write happens BEFORE the assignment
//     (0x180112750), not after it.
//  4. The sprite comparison is sheet + index, and the store is one 16-byte
//     move — the same shape as AnimatedImageElement::Update.
void ComboElement::Update(const System::Time& time, UserInterface* ui,
                          Object::InternalPtr<DialogInstance>* dialog,
                          const UpdateState& state) {
    const uint32_t flags = _flags;
    const bool open = (flags & kFlagDisabledMask) == 0 &&
                      _strings._size > 0 &&
                      state.b04 != 0 &&
                      (flags & kFlagHover) != 0 &&
                      (flags & kFlagPressStarted) != 0;

    if (open) {
        alignas(16) uint8_t dialogBuf[kDialogSize];
        Dialog* const stackDialog = ::new (static_cast<void*>(dialogBuf)) Dialog();   // ??0Dialog

        Object::InternalPtr<Element> listSlot;
        Object::InternalPtr<Element>* made = _listDesc->CreateInstance(&listSlot, stackDialog);
        Element* const madeList = made->Get();
        ListElement* list = madeList->IsDerivedFrom(UI::ListElement::TypeId())
                                ? static_cast<ListElement*>(madeList) : nullptr;

        const int32_t count = _strings._size;
        for (int32_t i = 0; i < count; ++i) {
            Object::InternalPtr<Element> itemSlot;
            Object::InternalPtr<Element>* madeItem = _itemDesc->CreateInstance(&itemSlot, stackDialog);
            Element* const madeItemRaw = madeItem->Get();
            TextElement* item = madeItemRaw->IsDerivedFrom(UI::TextElement::TypeId())
                                    ? static_cast<TextElement*>(madeItemRaw) : nullptr;

            if (item->_params._size > 0) {
                rtuielem::SetNeedsSizeCalc(item);
                item->_params._data[0] = _strings._data[i];                    // 0x1800c9810
                item->CacheText();                                    // 0x18011ec10
            }

            // item+0x100 lies past TextElement (0x100 bytes): it is
            // LabelElement::_sprite {sheet +0x100, index +0x108}, the one
            // TextElement subclass (LabelElement.h). Retail relies on the item
            // description making a LabelElement; no check is added.
            LabelElement* const label = static_cast<LabelElement*>(item);
            const UI::Sprite& sp = _sprites._data[i];                  // stride 0x10
            if (label->_sprite.spriteSheet != sp.spriteSheet ||         // the QWORD sheet
                label->_sprite.spriteIndex != sp.spriteIndex) {         // the DWORD index
                rtuielem::SetNeedsSizeCalc(item);
            }
            label->_sprite = sp;                                        // one 16-byte move

            Object::InternalPtr<Element> tmp(item);
            list->AddItem(&tmp, nullptr);                             // 0x1800d44b0
        }

        const int32_t cur = _currentItem;
        if (cur < 0 || cur >= list->_userData._size) list->_selectedIndex = 0;
        else                                         list->_selectedIndex = cur;
        list->_alignment = 0;

        void* combo = RtUi_ComboDialog_OperatorNew(kDialogSize);   // ??2ComboDialog 0x180074770
        ComboDialog* made2 = nullptr;
        if (combo != nullptr) {
            made2 = ::new (combo) ComboDialog(
                ui, Object::InternalPtr<Element>(list),
                *dialog, Object::InternalPtr<ComboElement>(this));
        }
        _openDialog = made2;

        made2->SetUpperLeft(_layoutLeft, _layoutBottom, false);    // 0x180114a60
        made2->_flags |= 2u;                                        // == _openDialog

        DialogInstance* pushed = made2;
        pushed->OnOpened();                                         // DialogInstance slot 4
        rtcoll::RtColl_ArraySetSizeFill_18007a0d0(&ui->_addDialogs,
                                                  ui->_addDialogs._size + 1, &pushed);

        stackDialog->UI::Dialog::~Dialog();                          // ??1Dialog
    }

    Object::InternalPtr<DialogInstance> dlgCopy = *dialog;
    ButtonElement::Update(time, ui, &dlgCopy, state);
}

}  // namespace UI

// ============================================================================
// ??1ComboElement@UI@@UEAA@XZ   0x180112490 -- retail's body, moved here from
// the retired door (ENDGAME B.3.9 UI-A)
// ============================================================================
// Close the popup if it is still open -- through the UserInterface the
// DialogInstance itself points at (`_openDialog->[+0x10]`), NOT through any
// cached manager -- then destroy the three arrays with their element
// destructors (by address, retail's three instantiations: Strings released,
// Sprites and user data trivially). Each of those ZEROES the array header, so
// the compiler-emitted member `~Array`s that run after this body see an empty
// array (no element destroy, MemFree(null)). Then the compiler restamps and
// chains ~ButtonElement / ~Element.
namespace UI {
ComboElement::~ComboElement() {
    ComboDialog* const open = _openDialog;
    if (open != nullptr) {
        Object::InternalPtr<DialogInstance> slot(open);
        open->_ui->CloseDialog(&slot);                                   // 0x180120f20
    }
    rtcoll::RtColl_ArrayDtor_180024aa0(&_userData);
    rtcoll::RtColl_ArrayDtor_180037310(&_sprites);
    // +0xe8 `_strings` (retail's third call, 0x180010ab0): the member
    // ~Array<System::String> runs it after this body, last of the three as
    // retail's order (+0x108, +0xf8, +0xe8) has it.
}
}  // namespace UI

namespace UI {
// ?CreateNewObject@ComboElement@UI@@SAPEAVBase@Object@@XZ  0x180075020
// The factory-interpreter row this replaces: MemAlloc(0x128, 8), retail's
// null branch, then the out-of-line ctor ??0ComboElement (0x1801123f0).
Object::Base* ComboElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ComboElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ComboElement();
}
// ?CreateInPlaceObject@ComboElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x1800750a0
Object::Base* ComboElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ComboElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ComboElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ComboElement@UI@@ (0x1801faa50): name "ComboElement", parent
// "ButtonElement" (retail ?_ParentTypeName@ComboElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ComboElement, "ButtonElement", nullptr)
