#pragma once
// ============================================================================
// src/engine/ui/ComboDialog.h — UI::ComboDialog, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block B. The drop-down list a UI::ComboElement opens.
// Per-field evidence: the `RtUI::ComboDialog` POD's banner in DialogInstance.h,
// which stays (the ctor door `RtUi_ComboDialog_Ctor` builds through it).
//
// PARENT: `UI::DialogInstance`, proved by
//   0x180074730  ?_ParentTypeName@ComboDialog@UI@@CAPEBDXZ -> "DialogInstance"
//
// `??_7ComboDialog@UI@@6B@` @ 0x1801590e0, TEN slots (B36_SLOTS.md sec. 1):
//
//   [0] +0x00  0x180074a10  (unexported) vector deleting dtor
//   [1] +0x08  0x180074760  ?TypeName@ComboDialog@UI@@UEBAPEBDXZ
//   [2] +0x10  0x180074740  ?Type@ComboDialog@UI@@UEBAIXZ
//   [3..5]                  inherited from UI::DialogInstance (Persist/OnOpened/OnClosed)
//   [6] +0x30  0x180111cb0  ?OnUpdateLayout@ComboDialog@UI@@UEAAXXZ
//   [7] +0x38  0x180111bd0  ?OnUpdate@ComboDialog@UI@@UEAAXAEBVTime@System@@@Z
//   [8] +0x40  0x180111af0  ?OnElementEvent@ComboDialog@UI@@UEAAXV?$InternalPtr@VElement@UI@@@Object@@HHHHHPEAX@Z
//   [9]                     inherited (OnSerializeSettings)
//
// sizeof 0xd0: `MOV ECX,0xd0` at 0x18011282b (the one ??2 call site) and the
// deleting dtor's stride.
//
// ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): CONCRETE. Two REAL public
// ctors -- the default one the factory pair inlines (DialogInstance() then
// +0xb8/+0xc0/+0xc8 zero) and retail's 4-argument ??0ComboDialog (0x180111d00,
// combodialog_ctor.cpp) over DialogInstance's root-Element ctor; slot 0 the
// class's own vector deleting dtor; ~ComboDialog protected and empty (retail:
// restamp + ~DialogInstance). The minted `??_7ComboDialog@UI@@6B@`, its
// RT_DELDTOR row and the FACTUI1 row are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "DialogInstance.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class ComboElement;   // block C1
class ListElement;

class ComboDialog : public DialogInstance {
public:
    // The default ctor the factory pair inlines (0x180074850 / 0x1800748f0):
    // ??0DialogInstance@UI@@QEAA@XZ, then the three own zero stores.
    ComboDialog() : DialogInstance() {
        _list = nullptr;                                  // 1800748c3
        _parentDialog = Object::InternalPtr<DialogInstance>();   // 1800748ca
        _combo = nullptr;                                 // 1800748d1
    }
    // ??0ComboDialog@UI@@QEAA@PEAVUserInterface@1@V?$InternalPtr@VElement@UI@@@
    //     Object@@V?$InternalPtr@VDialogInstance@UI@@@4@V?$InternalPtr@
    //     VComboElement@UI@@@4@@Z                             0x180111d00
    // (combodialog_ctor.cpp). Every InternalPtr arrives by hidden reference in
    // retail; by value here (a non-virtual, both ends this GCC).
    ComboDialog(UserInterface* ui, Object::InternalPtr<Element> root,
                Object::InternalPtr<DialogInstance> parent,
                Object::InternalPtr<ComboElement> combo);

    // [0] 0x180074a10 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xd0`, the element size).
    // `??1ComboDialog@UI@@UEAA@XZ` 0x180111cd0: restamp + ~DialogInstance,
    // both the compiler's — the empty body is retail's.
    ~ComboDialog() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180074760, Type 0x180074740, CreateNewObject 0x180074850, CreateInPlaceObject 0x1800748f0
    DECLARE_TYPE(ComboDialog)
public:
    void OnUpdateLayout() override;                               // [6] 0x180111cb0
    void OnUpdate(const System::Time& time) override;             // [7] 0x180111bd0
    void OnElementEvent(Object::InternalPtr<Element>* element,
                        int32_t a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x180111af0

    // ---- exported, NOT PORTED as members ----------------------------------
    ComboDialog(const ComboDialog& other);                        // 0x180074940
    // 0x180074750 — the minted record's type id (combodialog.cpp).
    // MemoryAllocator statics (B36_RULES 7): `??2` 0x180074770 IS ported as the
    // door `RtUi_ComboDialog_OperatorNew` (combodialog_ctor.cpp); the rest not.
    // Placement is spelt `::new (p) ComboDialog(...)` (B9-5).
    static void* operator new(std::size_t bytes);                 // 0x180074770
    static void* operator new(std::size_t bytes, void* place);    // 0x1800747e0
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                        // 0x1800747f0
    // Retail's factory pair (B39_RULES D2), defined and registered in combodialog.cpp.

    // RULE B5-15 / B6-7, COPIED from B36_SLOTS section 3 (the parent literal
    // is BARE here, unlike ToolTipDialog's):
    //   0x180074720  ?_TypeName@       -> "ComboDialog"
    //   0x180074730  ?_ParentTypeName@ -> "DialogInstance"

public:
    // ---- the 0x18 bytes ComboDialog adds ------------------------------------
    ListElement* _list;                                 // +0xb8  InternalPtr<UI::ListElement> (B4 H4)
    // NOT the base's `_parent`: the ctor hands the base a zeroed parent and
    // keeps the real one here (POD banner).
    Object::InternalPtr<DialogInstance> _parentDialog;  // +0xc0
    ComboElement* _combo;                               // +0xc8  InternalPtr<UI::ComboElement> (B4 H4)
};

static_assert(sizeof(ComboDialog) == 0xd0,
              "UI::ComboDialog is 0xd0 bytes (MOV ECX,0xd0 at 0x18011282b)");
static_assert(alignof(ComboDialog) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(ComboDialog, _list) == sizeof(DialogInstance),
              "ComboDialog's own fields begin at the end of the DialogInstance base");
static_assert(offsetof(ComboDialog, _list)         == 0xb8, "_list @ +0xb8");
static_assert(offsetof(ComboDialog, _parentDialog) == 0xc0, "_parentDialog @ +0xc0");
static_assert(offsetof(ComboDialog, _combo)        == 0xc8, "_combo @ +0xc8");

// NOT RETAIL (review S-035): UserInterface::CloseDialog calls this on the first close of a
// dialog. For a ComboDialog whose combo's `_openDialog` still names it, it clears that
// back-pointer while the combo is certainly alive (combodialog_close.cpp has the argument).
void ForgetClosingComboDialog(DialogInstance* dialog);

}  // namespace UI

// ==== B3R RT-UI: the operator new body (ui/combodialog_ctor.cpp), called by name ====
extern "C" void* RtUi_ComboDialog_OperatorNew(uint64_t bytes);  // 0x180074770 ??2ComboDialog@UI@@SAPEAX_K@Z
