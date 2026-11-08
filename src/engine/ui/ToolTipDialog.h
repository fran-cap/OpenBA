#pragma once
// ============================================================================
// src/engine/ui/ToolTipDialog.h — UI::ToolTipDialog, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block B. The tooltip popup `UserInterface::BeginToolTip`
// (0x180120860) allocates (0xc0), constructs through the DialogInstance BASE
// ctor and stamps with this class's table. It overrides only slots 0/1/2.
//
// PARENT: `UI::DialogInstance`, proved by
//   0x18004cf90  ?_ParentTypeName@ToolTipDialog@UI@@CAPEBDXZ -> "UI::DialogInstance"
// *** NAMESPACE-QUALIFIED although the parent is in the SAME namespace
// (ComboDialog's literal is bare). RULE B6-7: copied, never derived. ***
//
// `??_7ToolTipDialog@UI@@6B@` @ 0x180157880, TEN slots (B36_SLOTS.md sec. 1):
//
//   [0] +0x00  0x18004d270  (unexported) vector deleting dtor
//   [1] +0x08  0x18004cfc0  ?TypeName@ToolTipDialog@UI@@UEBAPEBDXZ
//   [2] +0x10  0x18004cfa0  ?Type@ToolTipDialog@UI@@UEBAIXZ
//   [3..9]                  ALL inherited from UI::DialogInstance
//
// sizeof 0xc0 (deleting dtor; BeginToolTip's `operator new(0xc0)`). The eight
// bytes past the base are TWO DWORDS, read from retail this block:
//   * `??0ToolTipDialog@UI@@QEAA@AEBV01@@Z` (copy ctor, 0x18004d180) copies
//     `[RBX+0xb8]` and `[RBX+0xbc]` as two separate DWORD moves;
//   * the element ctor (0x1800daca0) forwards its fourth argument to the base
//     ctor as `dataSource` and writes NOTHING past +0xb8; no ported body reads
//     them either. So they are named `_unreadCopiedHead`/`_unreadCopiedTail` (RULE B6-8: a
//     writer exists — the copy ctor — but nothing names them).
//
// ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): CONCRETE. Retail's two
// ctors are real: the default one (0x1800dad00: DialogInstance() + stamp) and
// the 4-argument one BeginToolTip inlines (0x1800daca0: the 6-argument
// DialogInstance ctor with (dataSource = *element, userData = 0, flags = 0) +
// stamp); slot 0 the class's own vector deleting dtor; ~ToolTipDialog
// protected and empty (restamp + tail-JMP ~DialogInstance, the compiler's).
// The minted `??_7ToolTipDialog@UI@@6B@`, its RT_DELDTOR row, its ??1 door
// (deleting_dtor_leaves.cpp) and the FACTUI2 row are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "DialogInstance.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class ToolTipDialog : public DialogInstance {
public:
    // ??0ToolTipDialog@UI@@QEAA@XZ 0x1800dad00 — DialogInstance() + the stamp.
    // +0xb8 / +0xbc are NOT written (retail).
    ToolTipDialog() : DialogInstance() {}
    // 0x1800daca0 (read this pass): `CALL 0x180114870` (the 6-argument
    // DialogInstance ctor) with RDX/R8/R9 passed through, dataSource = the
    // element the 5th argument's InternalPtr holds, userData = 0, flags = 0;
    // then the stamp. The InternalPtrs arrive by hidden reference in retail.
    // Defined in tooltipdialog.cpp, where `Element` is complete (the element
    // is handed on as its `Object::Base`).
    ToolTipDialog(UserInterface* ui, const Dialog* dialog,
                  Object::InternalPtr<DialogInstance> parent,
                  Object::InternalPtr<Element> element);

    // [0] 0x18004d270 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xc0`, the element size).
    // `??1ToolTipDialog@UI@@UEAA@XZ` 0x1800dac70 (stamp + tail-JMP to
    // ~DialogInstance, both the compiler's). EMPTY, retail's body.
    ~ToolTipDialog() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18004cfc0, Type 0x18004cfa0, CreateNewObject 0x18004d0b0, CreateInPlaceObject 0x18004d140
    DECLARE_TYPE(ToolTipDialog)
public:

    // ---- PORTED non-virtual member ---------------------------------------
    void SetText(const System::String& text);                    // 0x1800dab00

    // ---- exported, NOT PORTED as members ----------------------------------
    // SetPosition is `SetUpperLeft(x, y - root->_layoutHeight, false)`
    // (0x1800dac50, four instructions); no caller in the tree.
    void SetPosition(int32_t x, int32_t y);                       // 0x1800dac50
    ToolTipDialog(const ToolTipDialog& other);                    // 0x18004d180
    ToolTipDialog& operator=(const ToolTipDialog& other);         // 0x18004d1e0
    // 0x18004cfb0 — the minted record's type id (tooltipdialog.cpp).
    // MemoryAllocator statics (B36_RULES 7): `??2` 0x18004cfd0 is the door
    // `RtUi_ToolTipDialog_OperatorNew` (tooltipdialog.cpp). The factory pair is
    // the class's own (B39_RULES D2), defined and registered in tooltipdialog.cpp.
    static void* operator new(std::size_t bytes);                 // 0x18004cfd0
    static void* operator new(std::size_t bytes, void* place);    // 0x18004d040
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                        // 0x18004d050

    // RULE B5-15 / B6-7, COPIED from B36_SLOTS section 3:
    //   0x18004cf80  ?_TypeName@       -> "ToolTipDialog"
    //   0x18004cf90  ?_ParentTypeName@ -> "UI::DialogInstance"   (QUALIFIED)

public:
    int32_t _unreadCopiedHead;   // +0xb8  copied by 0x18004d180, otherwise unread
    int32_t _unreadCopiedTail;   // +0xbc  copied by 0x18004d180, otherwise unread
};

static_assert(sizeof(ToolTipDialog) == 0xc0,
              "UI::ToolTipDialog is 0xc0 bytes (deleting dtor 0x18004d270; "
              "BeginToolTip allocates 0xc0)");
static_assert(alignof(ToolTipDialog) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(ToolTipDialog, _unreadCopiedHead) == sizeof(DialogInstance),
              "ToolTipDialog's own fields begin at the end of the DialogInstance base");
static_assert(offsetof(ToolTipDialog, _unreadCopiedTail) == 0xbc, "_unknownBC @ +0xbc");

}  // namespace UI

// ==== B3R RT-UI: the operator new body (ui/tooltipdialog.cpp), called by name ====
extern "C" void* RtUi_ToolTipDialog_OperatorNew(uint64_t bytes);  // 0x18004cfd0 ??2ToolTipDialog@UI@@SAPEAX_K@Z
