// ============================================================================
// src/engine/ui/combodialog.cpp — VTABLE BATCH 9
//   UI::ComboDialog, 6 of 10 vtable slots (0, 1, 2, 6, 7, 8). Slots 3-5 and 9
//   (Persist/OnOpened/OnClosed/OnSerializeSettings, all inherited
//   DialogInstance bodies) are already ported elsewhere; the ctor at
//   0x180111d00 belongs to the UI-text agent (v5) per the batch brief and is
//   NOT in this file.
// ============================================================================
//
// ??_7ComboDialog@UI@@6B@ (retail 0x1801590e0), 10 slots:
//   0  deleting dtor    0x180074a10  ComboDialog.h (the class's own, B.3.9)
//   1  TypeName         0x180074760  here
//   2  Type             0x180074740  here
//   3  Persist          0x180114fa0  already ported (DialogInstance)
//   4  OnOpened         0x180114fb0  already ported (DialogInstance)
//   5  OnClosed         0x180115330  already ported (DialogInstance)
//   6  OnUpdateLayout   0x180111cb0  here
//   7  OnUpdate         0x180111bd0  here
//   8  OnElementEvent   0x180111af0  here
//   9  OnSerializeSettings 0x180115470  already ported (DialogInstance)
// plus ??1ComboDialog@UI@@UEAA@XZ (0x180111cd0): stamp own vtable, tail-call
// ~DialogInstance -- the compiler's chain (ENDGAME B.3.9 UI-D: the dtor door and
// the five slot doors are retired with the minted `??_7ComboDialog@UI@@6B@`,
// the factory pair is the class's own, below).
//
// LAYOUT. `DialogInstance` is 0xb8 bytes (src/engine/ui/DialogInstance.h);
// this class adds THREE pointers, to 0xd0 (the deleting destructor's own
// `MOV EDX,0xd0`):
//   +0xb8  ComboDescription*  _description   (read by OnElementEvent's +0xd8)
//   +0xc0  DialogController*  _controller    (read by OnElementEvent)
//   +0xc8  ComboElement*      _comboElement  (this+200 in the decompile)
//
// EVERY NON-SLOT HELPER THESE THREE BODIES REACH IS ALREADY PORTED:
//   ?SetUpperLeft@DialogInstance@UI@@       0x180114a60  src/engine/ui/dialog.cpp
//   ?SetCurrentItem@ComboElement@UI@@       0x180112540  src/engine/ui/widgets_input.cpp
//   ?Play@UserInterface@UI@@                0x1801223d0  src/engine/ui/userinterface.cpp
//   ?CloseDialog@UserInterface@UI@@         0x180120f20  src/engine/ui/userinterface_close.cpp
// so this file, like external_list.cpp and compressedcopyfilestream.cpp in
// this same batch, is composition only.
//
// ---------------------------------------------------------------------------
// ?OnUpdateLayout@ComboDialog@UI@@UEAAXXZ   0x180111cb0
// ---------------------------------------------------------------------------
// A pure tail jump: `SetUpperLeft(this, this->_root->+0x68, this->_root->+0x74,
// false)`, where `this->_root` (DialogInstance +0x50) is read straight off
// the disassembly — R9D(centered)=0 is set BEFORE R8D/RDX, and RDX/R8D come
// from the SAME object, +0x68 first:
//     MOV RAX,[this+0xc8]          ; the same object OnElementEvent reads,
//                                    though unused by name here -- see below
//     XOR R9D,R9D
//     MOV R8D,[RAX+0x74]
//     MOV EDX,[RAX+0x68]
//     JMP SetUpperLeft
// *** CORRECTION AGAINST THE DECOMPILE, READ OFF THE RAW BYTES ***: Ghidra's C
// prints `*(this+200)` (i.e. +0xc8, `_comboElement`) as the object read here,
// matching the disassembly's `[RCX+0xc8]` exactly -- so OnUpdateLayout reads
// the SAME `_comboElement` pointer OnElementEvent does, not `_root`. Recorded
// because the header comment for `_root` (+0x50) does not apply to this call.
//
// ---------------------------------------------------------------------------
// ?OnUpdate@ComboDialog@UI@@UEAAXAEBVTime@System@@@Z   0x180111bd0
// ---------------------------------------------------------------------------
// The `Time&` argument is never read (retail computes everything from the
// Input::Device singleton and DialogInstance's own `_root`).
//
//   mx = (int)((float)(int16)Input::Device::_singleton[0x109c] * _root->+0xdc)
//   my = (int)((float)(int16)Input::Device::_singleton[0x10b4] * _root->+0xdc)
//   if (no mouse button down at 0x10fe/0x1116/0x112e) return;
//   flags = this->_flags (+0x20)
//   if (!(flags & 4)) {
//       if (flags & 8) { if (this->_comboElement->vt[0x48]()) return; }
//       else { rect = this->_comboElement;
//              if (mx in [rect->+0x68, rect->+0x70] &&
//                  my in [rect->+0x6c, rect->+0x74]) return; }
//   }
//   if (this->_controller->+0x120 != 0) this->_controller->+0x120 = 0;
//   CloseDialog(this->_ui, &this)
//
// *** SAME CORRECTION AS ABOVE ***: the rect/vtable-call subject is
// `this->_comboElement` (+0xc8 via `MOV R9,[RBX+0x10]` for the scale factor
// only; the rect/Contains subject is `[RBX+0x50]`). Both offsets are read
// verbatim below with their own citation.
//
// ---------------------------------------------------------------------------
// ?OnElementEvent@ComboDialog@UI@@UEAAXV?$InternalPtr@VElement@UI@@@Object@@HHHHHPEAX@Z
// 0x180111af0
// ---------------------------------------------------------------------------
// Ghidra drops the InternalPtr<Element> argument (arg2, unused by the body)
// and the trailing two ints + void* (also unused) entirely --
// reference_ghidra_dropped_args.md again. Read off the disassembly, the
// live 5-argument shape is `(this, elem/*unused*/, evt, stage, value)`, with
// two more unused ints and an unused void* past `value`:
//
//   if (evt != 0x14 || stage != 0) return;
//   combo = this->_comboElement (+0xc8)
//   ComboElement::SetCurrentItem(combo, value)
//   UserInterface::Play(this->_ui, 5)
//   ComboElement::SetCurrentItem(combo, this->_description->+0xd8)
//   controller = this->_controller (+0xc0)
//   local = this->_comboElement (+0xc8)     // re-read, same value
//   controller->vt[0x40](controller, &local, 0x14, local->+0x88, value, 0, 0,
//                         <uninitialised>)
//   *** SLOT 0x40, NOT 0x48. *** src/engine/ui/DialogInstance.h's own
//   `kControllerVtblOnElementEvent = 0x48` is DialogInstance's iteration loop
//   calling through a GENERIC DialogController; the disassembly here
//   (`CALL qword ptr [RAX+0x40]`, confirmed against `?OnElementEvent@
//   DialogInstance@UI@@`'s own dispatch at 0x180114f5e which uses 0x48) shows
//   ComboDialog calling its OWN controller's slot 0x40 (`kControllerVtblOnUpdate`)
//   with this OnElementEvent-shaped argument list instead. Transcribed exactly
//   as shipped -- the concrete controller subclass ComboDialog drives is free
//   to put a different signature at that slot than the generic base class
//   documents, and nothing in this lane's scope needs to resolve which
//   subclass that is.
//   if (this->_comboElement->+0x120 != 0) this->_comboElement->+0x120 = 0;
//   CloseDialog(this->_ui, &this)
// ============================================================================

#include <cstdint>
#include <new>

#include "ui/ComboDialog.h"
#include "ui/ComboElement.h"
#include "ui/UserInterface.h"
#include "ui/Element.h"     // B3X W0: the root's Contains by name
#include "input/Device.h"   // ENDGAME B.3.9 SS1: Input::Device::_singleton
#include "ui/ListElement.h"

// ############################################################################
// ENDGAME B.3.6 block B — the five own slots are UI::ComboDialog members now
// (src/engine/ui/ComboDialog.h). The raw offsets became the named fields:
// +0x10 `_ui` (the old code called it "combo"; it is DialogInstance::_ui, the
// UserInterface whose +0xdc is the UI scale), +0x20 `_flags`, +0x50 `_root`,
// +0xb8 `_list`, +0xc0 `_parentDialog`, +0xc8 `_combo`. Calls onto THIS
// block's classes became member calls:
//   SetUpperLeft by name              -> SetUpperLeft(x, y, false)    (non-virtual)
//   `_parentDialog` vtable +0x40      -> _parentDialog->OnElementEvent(...)  (VIRTUAL,
//                                        DialogInstance slot 8)
// ############################################################################
namespace UI {

// ?CreateNewObject@ComboDialog@UI@@SAPEAVBase@Object@@XZ  0x180074850
// The FACTUI1 row this replaces: the locked MemAlloc of 0xd0 bytes, align 8;
// retail's null branch; then the inlined default ctor (??0DialogInstance and
// the three own zero stores).
Object::Base* ComboDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ComboDialog), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ComboDialog();
}
// ?CreateInPlaceObject@ComboDialog@UI@@SAPEAVBase@Object@@PEAX@Z  0x1800748f0
Object::Base* ComboDialog::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ComboDialog();
}

// ??1ComboDialog@UI@@UEAA@XZ 0x180111cd0 — restamp + ~DialogInstance, both
// the compiler's; the empty body is retail's.
ComboDialog::~ComboDialog() {}

// ?OnUpdateLayout@ComboDialog@UI@@UEAAXXZ   0x180111cb0 — a tail JMP into
// SetUpperLeft at the combo element's (left, bottom).
void ComboDialog::OnUpdateLayout() {
    SetUpperLeft(_combo->_layoutLeft, _combo->_layoutBottom, false);  // 180111cc1 JMP
}

// ?OnUpdate@ComboDialog@UI@@UEAAXAEBVTime@System@@@Z   0x180111bd0
// A click anywhere OUTSIDE the drop-down closes it.
//
// *** CORRECTED, B.3.6 block B: the root's Contains (Element slot 9, +0x48)
// takes (x, y, UserInterface*). 0x180111c50 `CALL [RAX+0x48]` is reached with
// EDX = mx (0x180111c0d), R8D = my (0x180111c1c) and R9 = `_ui`
// (0x180111bee) still live; the port passed `this` only and let three
// registers of garbage through — the register-passthrough arg drop
// (feedback_ghidra_dropped_args.md). Now passed, as UI::DialogInstance::
// Contains passes them. ***
//
// Input::Device +0x109c / +0x10b4 are `_mouse[0].value` / `_mouse[1].value`
// (the cursor x / y); +0x10fe / +0x1116 / +0x112e are `_mouse[4..6].pressed`.
void ComboDialog::OnUpdate(const System::Time& /*time*/) {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    const ::Input::Device* dev = ::Input::Device::_singleton;
    if (dev == nullptr) return;   // pre-existing; retail derefs it unconditionally

    const float scale = _ui->_uiScale;
    const int16_t rawMx = dev->_mouse[0].value;
    const int16_t rawMy = dev->_mouse[1].value;
    const int32_t mx = static_cast<int32_t>(static_cast<float>(rawMx) * scale);
    const int32_t my = static_cast<int32_t>(static_cast<float>(rawMy) * scale);

    const bool anyButtonDown = dev->_mouse[4].pressed != 0 ||
                               dev->_mouse[5].pressed != 0 ||
                               dev->_mouse[6].pressed != 0;
    if (!anyButtonDown) return;                                          // 180111c33

    if ((_flags & 0x4) == 0) {                                           // 180111c3f
        Element* const root = _root.Get();
        if (_flags & 0x8) {                                              // 180111c47
            if (root->Contains(mx, my, _ui))                             // Element slot 9
                return;                                                  // 180111c55
        } else {
            if (mx >= root->_layoutLeft && mx <= root->_layoutRight &&
                my >= root->_layoutTop && my <= root->_layoutBottom) return;  // 180111c71
        }
    }

    if (_combo->_openDialog != nullptr) _combo->_openDialog = nullptr;   // 180111c84

    Object::InternalPtr<DialogInstance> selfCopy(this);
    _ui->CloseDialog(&selfCopy);                                         // 180111c9d
}

// ?OnElementEvent@ComboDialog@UI@@UEAAXV?$InternalPtr@VElement@UI@@@Object@@HHHHHPEAX@Z
//      0x180111af0 — a list pick: set the combo's item, click, forward the
// event to the dialog that owns the combo, close.
void ComboDialog::OnElementEvent(Object::InternalPtr<Element>* /*element*/,
                                 int32_t evt, int32_t stage, int32_t value,
                                 int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (evt != 0x14 || stage != 0) return;                                  // 180111af4/180111b05

    ComboElement* const combo = _combo;
    combo->SetCurrentItem(value);                                           // 180111b25
    _ui->Play(5);                                                           // 180111b33

    combo->SetCurrentItem(_list->_selectedIndex);                           // 180111b4c

    // *** SLOT 0x40 on `_parentDialog`, i.e. DialogInstance slot 8 — see the
    // file banner. The handle is re-read into a local whose ADDRESS is passed.
    Object::InternalPtr<Element> local(_combo);
    _parentDialog->OnElementEvent(&local, 0x14, combo->_elementId,
                                  value, 0, 0, nullptr);                    // 180111b8a

    if (combo->_openDialog != nullptr) combo->_openDialog = nullptr;        // 180111ba2

    Object::InternalPtr<DialogInstance> selfCopy(this);
    _ui->CloseDialog(&selfCopy);                                            // 180111bb7
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ComboDialog's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ComboDialog@UI@@ (0x1801fa9e0): name "ComboDialog", parent
// "DialogInstance" (retail ?_ParentTypeName@ComboDialog@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ComboDialog, "DialogInstance", nullptr)
