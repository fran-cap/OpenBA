// ============================================================================
// src/engine/ui/combodialog_ctor.cpp — UI::ComboDialog construction
//
// SPLIT AT MERGE, 2026-09-12. This file and src/engine/ui/combodialog.cpp were
// written in parallel by two batches — vtable batch 9 took the class's SLOTS,
// the UI-text batch took its CONSTRUCTION — and both landed on the same path
// with the same `RtUiComboDialogRegister` name. Neither body set overlaps the
// other, so the merge kept both and gave this half its own file and its own
// `RtUiComboDialogCtorRegister`. Read the other file's banner for the vtable.
//
//   ??2ComboDialog@UI@@SAPEAX_K@Z                        retail 0x180074770
//   ??0DialogInstance@UI@@QEAA@PEAVUserInterface@1@
//       V?$InternalPtr@VElement@UI@@@Object@@
//       V?$InternalPtr@VDialogInstance@UI@@@4@
//       PEAVBase@4@PEAXH@Z                               retail 0x1801146b0
//   ??0ComboDialog@UI@@QEAA@PEAVUserInterface@1@
//       V?$InternalPtr@VElement@UI@@@Object@@
//       V?$InternalPtr@VDialogInstance@UI@@@4@
//       V?$InternalPtr@VComboElement@UI@@@4@@Z           retail 0x180111d00
// ============================================================================
//
// ---------------------------------------------------------------------------
// ABSENCE ESTABLISHED BEFORE A LINE WAS WRITTEN
// ---------------------------------------------------------------------------
// A body that is ported but never REGISTERED is indistinguishable from an
// absent one to the census, so the grep was by ADDRESS *and* by NAME, scoped to
// the source roots (never the repo root — `.claude/worktrees/` holds sibling
// lane checkouts and an unscoped grep returns a confident wrong number):
//
//   grep -rn '180111d00\|1801146b0\|180074770'
//       runtime/ code/ standalone/ engine/ src/render/dx11/
//     -> ONE hit, and it is a COMMENT, not a port:
//        src/engine/ui/ui_operator_delete.cpp:47 names 0x180074770 only to say
//        that its sibling `operator delete` (0x1800747f0) is the one in THAT
//        batch. The `operator new` itself was left on the DLL.
//   grep -rn 'ComboDialog' <same roots>
//     -> factory table rows (factory_ui1_tables.inc) and vtable-mint rows
//        (rt_data_mint_tables.inc). No body.
//
// ---------------------------------------------------------------------------
// THE SECOND DialogInstance CONSTRUCTOR, AND WHY IT IS A SEPARATE BODY
// ---------------------------------------------------------------------------
// src/engine/ui/dialog_create.cpp already ports a DialogInstance ctor — but a
// DIFFERENT OVERLOAD, 0x180114870, whose second parameter is a `const Dialog*`
// resource and which therefore builds its own root Element from the Dialog's
// root ElementDescription, copies the Dialog's sprite sheet, seeds `_flags`
// from the Dialog and runs the Dialog's controller table.
//
// THIS overload (0x1801146b0) takes an ALREADY-BUILT root Element instead of a
// Dialog, and consequently:
//   * `_dialog` (+0x18) is left NULL,
//   * `_flags` / `_result` (+0x20 / +0x24) are zeroed rather than seeded,
//   * `_spriteSheet` (+0x58) is NULL and `_spriteIndex` (+0x60) is -1 with no
//     SpriteSheet lookup at all,
//   * there is no controller loop.
// Everything else — the Object::Internal base ctor, the SAME exported vtable
// `??_7DialogInstance@UI@@6B@`, the root's back-pointer at Element+0x90, and
// the child-list PREPEND into the parent — is identical instruction for
// instruction. The two bodies are siblings, not duplicates.
//
// ---------------------------------------------------------------------------
// ARGUMENT MAPPING — WORKED OUT FROM THE STACK, NOT FROM THE MANGLING
// ---------------------------------------------------------------------------
// Getting this wrong corrupts every combo box in the game, so both frames were
// laid out by hand.
//
// 0x1801146b0 pushes RSI/RDI/R14 (0x18) and SUBs 0x30, so the incoming home
// space sits at RSP+0x50 .. RSP+0x80. The body reads:
//     RCX  -> this            (R14)
//     RDX  -> UserInterface*  (RBX)   stored RAW at +0x10
//     R8   -> InternalPtr<Element>,        DEREFERENCED at 0x18011471e
//     R9   -> InternalPtr<DialogInstance>, DEREFERENCED at 0x1801146fe
//     [RSP+0x70] -> Object::Base* dataSource  -> +0x30
//     [RSP+0x78] -> void*        userData     -> +0xa8
//     [RSP+0x80] -> int          userFlags    -> +0xb0
// So BOTH InternalPtr parameters arrive as a POINTER TO A CALLER TEMPORARY and
// the ctor takes NO reference on either — it copies the raw pointer out and
// nothing else. That matches how dialog_create.cpp spells the other overload's
// `parent`, and how all the exe call sites pass one.
//
// 0x180111d00 pushes seven registers (0x38) and SUBs 0x50, and spills R8 and
// RCX into their home slots FIRST (`MOV [R11+0x18],R8` / `MOV [R11+8],RCX`
// with R11 = entry RSP), which is what lets it reuse the Element InternalPtr
// pointer after the two searches clobber the scratch slots. Its arg5, the
// InternalPtr<ComboElement>, is read at [RSP+0xb0] == [R11+0x28] — the fifth
// home slot — and DEREFERENCED, so it too is by hidden reference.
//
// *** THE PARENT IS NOT PASSED DOWN. *** At 0x180111d31 the ctor zeroes the
// arg4 home slot and at 0x180111d48 passes the address of THAT ZERO as the base
// ctor's `parent`. A ComboDialog is therefore deliberately NOT linked into its
// parent dialog's child list; the parent pointer is kept at ComboDialog+0xc0
// instead. Reading the mangled name alone gets this backwards.
//
// NO HIDDEN RETURN SLOT on either constructor — checked against the
// disassembly, not the mangling. Both return `this` in RAX
// (0x180114803 `MOV RAX,R14`; 0x180112106 `MOV RAX,R14`), which is what MSVC
// always does for a ctor and what the callers are typed to receive.
//
// ---------------------------------------------------------------------------
// FOUR INLINED CALLEES, EACH IDENTIFIED AGAINST ITS OWN EXPORTED BODY
// ---------------------------------------------------------------------------
// The ComboDialog ctor is large because MSVC inlined four member functions into
// it. Each inlined sequence below was diffed instruction for instruction
// against the exported body it came from, and the exported body's address is
// quoted at the site:
//
//   ?SetElementId@Element@UI@@QEAAXH@Z         0x180115820  (one store)
//   ?SetFlagsOnOff@Element@UI@@QEAAXII@Z       0x180116160
//   ?GetContentSize@ClipRegionElement@UI@@QEAAXAEAH0@Z   0x1800381e0
//   ?GetPaddedSize@Element@UI@@                0x180115f10
//   ?SetSize@ContainerElement@UI@@QEAAXHH@Z    0x180039eb0
//
// They are reproduced INLINE here rather than routed through the seam, because
// inlining is what the shipped bytes do: calling the exports would be a
// different instruction stream reaching the same result, and one of them
// (`GetPaddedSize`) differs from its export in exactly one register-vs-memory
// reload of a value nothing writes in between.
//
// `?SetFlagsOnOff@`'s dirty-walk is DEAD at this call site and is kept anyway.
// The export tests `(new ^ old) & 1` — the HIDDEN bit — to decide whether to
// mark ancestors dirty. Here the only bit touched is bit 16, so the XOR can
// never have bit 0 set and MSVC still emitted the compare (0x180111e37). It is
// written out faithfully below; the C++ compiler folds it, and the folding
// changes nothing because the branch is provably never taken.
//
// ---------------------------------------------------------------------------
// UNGUARDED DEREFERENCES THAT ARE KEPT UNGUARDED
// ---------------------------------------------------------------------------
// Three places deref a pointer retail has just allowed to be null:
//   0x180114782  root->[0x90] = this        (root came from *rootPtr)
//   0x180111e1e  _list->[0x88] = 0          (_list is `IsDerivedFrom ? x : 0`)
//   0x1801120e1  container->[0xc0] = w      (same CMOV-null shape)
// Retail relies on the value always being there; a guard would not restore the
// behaviour, it would skip it. They are reproduced as they ship.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "ui/ComboDialog.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"   // ENDGAME B.3.6 closing pass: FindElementType
#include "ui/ClipRegionElement.h"   // B.3.9 TI-UI: X::TypeId()
#include "ui/ContainerElement.h"
#include "ui/ListElement.h"
#include "ui/ComboElement.h"   // B4: _combo is typed
#include "video/Device.h"   // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "ui/UserInterface.h"   // B4: UserInterface::_uiScale
#include "system/MemoryAllocator.h"
#include <new>


namespace {

// ---------------------------------------------------------------------------
// ATTENDANCE — entries, and NOT a verification of anything. A zero here means
// the row is registered and DEAD on the route that was run; a non-zero means
// only that the body was entered.
// ---------------------------------------------------------------------------
uint64_t g_newEntries   = 0;
uint64_t g_newLastBytes = 0;

struct ComboDialogSummary {
    ~ComboDialogSummary() {
        printf("[RtPort] UI::ComboDialog ATTENDANCE (entries, not comparisons): "
               "operator_new=%llu lastBytes=0x%llx\n",
               (unsigned long long)g_newEntries,
               (unsigned long long)g_newLastBytes);
        fflush(stdout);
    }
};
ComboDialogSummary g_summary;

// ===========================================================================
// Callees, called by name (B3R RT-UI).
// ===========================================================================

// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z`, 0x1800c7a50.
void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}

// The three type ids the ComboDialog ctor loads. Retail reads the FIRST DWORD
// of each class's `_typeInfo` static (`MOV R12D,dword ptr [0x1801fa120]` for
// ListElement; 0x1801f9d68 ClipRegionElement, 0x1801f9dd8 ContainerElement),
// which is `X::TypeId()`. ENDGAME B.3.9 TI-UI: read at each use, as retail
// does (the by-name lookup and its first-use cache are gone, B9-10).

// (B3X W0: the VCallType / VCallComputeSize byte-offset helpers are gone; the
// three call sites below dispatch Type() / ComputeSize() by name.)

// ===========================================================================
// The four inlined member functions, each reproduced as the shipped stream
// inlines it.
// ===========================================================================

/// Inlined at 0x180111d91-0x180111df8 and again at 0x180111e56-0x180111ec5:
// "is the root itself this type, and if not, does any DIRECT child contain one".
// The recursion lives in ?FindElementType@ (0x180115760); this wrapper is the
// one level retail open-coded. Both the children POINTER and the child COUNT
// are re-read every iteration, exactly as 0x180111dc0 / 0x180111df0 do.
UI::Element* FindSelfOrUnderChildren(UI::Element* root, uint32_t typeId) {
    if (root->Type() == typeId) return root;         // 0x180111da4 CMP / JZ

    int32_t i = 0;
    while (i < root->_children._size) {
        UI::Element* const child = root->_children._data[i];
        if (child) {
            UI::Element* const out = child->FindElementType(typeId).Get();   // 0x180115760
            if (out) return out;                     // 0x180111de8 JNZ
        }
        ++i;
    }
    return nullptr;                                  // 0x180111df5 MOV RDI,RBX
}

// ?SetFlagsOnOff@Element@UI@@QEAAXII@Z  0x180116160, inlined at 0x180111e24
// with `off` folded to 0 and the OR turned into `BTS EDX,0x10`. The dirty walk
// is dead at this call site — see the banner — and is kept.
void SetFlagsOnOff(UI::Element* el, uint32_t on, uint32_t off) {
    const uint32_t old = el->_flags;
    const uint32_t now = (old | on) & ~off;
    el->_flags = now;
    if ((now ^ old) & UI::kElementFlagHidden) {
        for (UI::Element* p = el; p; p = p->_parent)
            p->_flags |= UI::kElementFlagNeedsSizeCalc;
    }
}

// ?SetSize@ContainerElement@UI@@QEAAXHH@Z  0x180039eb0, inlined at 0x1801120e1.
// The walk here is UNCONDITIONAL — it is ?SetNeedsSizeCalc@ (0x180035470), not
// the guarded walk above.
void ContainerSetSize(UI::ContainerElement* el, int32_t w, int32_t h) {
    el->_requestedWidth  = w;
    el->_requestedHeight = h;
    for (UI::Element* p = el; p; p = p->_parent)
        p->_flags |= UI::kElementFlagNeedsSizeCalc;
}

// ?GetContentSize@ClipRegionElement@UI@@QEAAXAEAH0@Z  0x1800381e0, inlined at
// 0x180111f3e: ComputeSize() first, then the two dwords.
void ClipGetContentSize(UI::ClipRegionElement* el, int32_t* outW, int32_t* outH) {
    el->ComputeSize();
    *outW = el->_innerWidth;
    *outH = el->_innerHeight;
}

// ?GetPaddedSize@Element@UI@@  0x180115f10, inlined at 0x180111f61-0x180112010.
// Layout size plus both pads, floored at the min size, and then — when either
// size-to-screen bit is set — replaced outright by the Video::Device screen
// dimension scaled by the ROOT dialog's UI scale. `flags` is captured ONCE,
// before the walk to the root, which is what both the export and the inline do.
void GetPaddedSize(UI::Element* el, int32_t* outW, int32_t* outH) {
    el->ComputeSize();

    int32_t w = el->_layoutWidth + el->_rightPad + el->_leftPad;
    int32_t h = el->_layoutHeight + el->_bottomPad + el->_topPad;
    if (w < el->_minWidth)  w = el->_minWidth;
    if (h < el->_minHeight) h = el->_minHeight;

    const uint32_t flags = el->_flags;
    if (flags & UI::kElementFlagSizeToScreen) {
        UI::Element* root = el;
        while (root->_parent) root = root->_parent;

        UI::DialogInstance* const dialog = root->_dialogParent;
        const ::Video::Device* const device = ::Video::Device::_singleton;

        if (flags & UI::kElementFlagSizeToScreenW) {
            UI::UserInterface* const ui = dialog->_ui;
            w = static_cast<int32_t>(
                static_cast<float>(device->backBufferWidth) * ui->_uiScale);
        }
        if (flags & UI::kElementFlagSizeToScreenH) {
            UI::UserInterface* const ui = dialog->_ui;
            h = static_cast<int32_t>(
                static_cast<float>(device->backBufferHeight) * ui->_uiScale);
        }
    }

    *outW = w;
    *outH = h;
}

}  // namespace

// ============================================================================
// ??2ComboDialog@UI@@SAPEAX_K@Z                            retail 0x180074770
//
//     void* UI::ComboDialog::operator new(size_t bytes)
//
// The whole shipped body is EnterCriticalSection(allocator+0x640) /
// Allocate(allocator, bytes, 8) / LeaveCriticalSection — which is
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) with the
// alignment frozen to 8. The two disassemblies were compared instruction for
// instruction; they differ only in which callee-saved register holds `bytes`
// and in MemAlloc taking the alignment from EDX instead of an immediate. This
// is the same equivalence src/engine/ui/spritesheet_new.cpp states for
// `??2SpriteSheet@UI@@`.
//
// Its ONE call site asks for 0xd0 (`MOV ECX,0xd0` at 0x18011282b), which is
// what pins sizeof(UI::ComboDialog) in DialogInstance.h.
// ============================================================================
extern "C" void* RtUi_ComboDialog_OperatorNew(uint64_t bytes) {
    ++g_newEntries;
    g_newLastBytes = bytes;
    return MemAlloc(bytes, 8);
}

// ============================================================================
// ??0DialogInstance@UI@@QEAA@PEAVUserInterface@1@
//     V?$InternalPtr@VElement@UI@@@Object@@
//     V?$InternalPtr@VDialogInstance@UI@@@4@
//     PEAVBase@4@PEAXH@Z                                   retail 0x1801146b0
//
//     DialogInstance::DialogInstance(UserInterface* ui,
//                                    InternalPtr<Element> root,
//                                    InternalPtr<DialogInstance> parent,
//                                    Object::Base* dataSource,
//                                    void* userData,
//                                    int userFlags)
//
// The root-Element overload. See the banner for how it differs from the
// Dialog-resource overload at 0x180114870 that dialog_create.cpp ports, and for
// why both InternalPtr parameters arrive as pointers to caller temporaries.
//
// Every field offset below is asserted in src/engine/ui/DialogInstance.h.
// The header names four holes the OTHER overload never writes; this one leaves
// the same four alone, and additionally writes +0x18 / +0x20 / +0x24 / +0x58 as
// zero and +0x60 as -1 rather than deriving them from a Dialog.
//
// Note the qword-width zero stores: retail clears +0x20 as ONE qword
// (0x1801146f6), covering `_flags` AND `_result`, and likewise +0x70, +0x80 and
// +0x88 each cover a pair of dwords. Spelled out field by field here; the bytes
// are identical.
// ============================================================================
// ENDGAME B.3.9 UI-D: the real constructor (DialogInstance.h). `??0Internal`
// (0x1801146dc) and the vptr store (0x1801146e2) are the compiler's; the
// `_timers` / `_controllers` headers are zeroed by their own Array ctors (the
// three qword stores retail makes at +0x68..+0x80).
UI::DialogInstance::DialogInstance(UserInterface* ui, Object::InternalPtr<Element> root,
                                   Object::InternalPtr<DialogInstance> parent,
                                   Object::Base* dataSource, void* userData,
                                   int32_t userFlags)
    : _root(Object::InternalBasePtr::NoStore()) {   // stored below, retail's two stores
    _ui         = ui;                           // +0x10
    _dialog     = nullptr;                      // +0x18  no Dialog resource
    _flags      = 0;                            // +0x20  \ one qword store
    _result     = 0;                            // +0x24  /
    _parent     = Object::InternalPtr<UI::DialogInstance>();   // +0x28  written 0 then...
    _parent     = parent;                       // +0x28  ...the real value
    _dataSource = dataSource;                   // +0x30
    // ENDGAME B.3.2 step 2 — these three stay raw field writes, deliberately:
    // `Collection::LinkedList<T>` has NO user-provided default constructor on
    // purpose (Collection.h's banner), so there is no member call this could
    // become; it is the ctor's own explicit zeroing, exactly like retail's.
    _children._head  = nullptr;                 // +0x38
    _children._tail  = nullptr;                 // +0x40
    _children._count = 0;                       // +0x48   (+0x4c left alone)
    _root       = nullptr;                      // +0x50  written 0 then...
    _root       = root;                         // +0x50  ...the real value
    _spriteSheet = nullptr;                     // +0x58
    _spriteIndex = -1;                          // +0x60   (+0x64 left alone)
    // +0x68..+0x87: the two Array headers, zeroed by their own ctors.
    _upperLeftX = 0;                            // +0x88  \ one qword store
    _upperLeftY = 0;                            // +0x8c  /
    _hoverMouseX = -1;                          // +0x90  a QWORD -1 --
    _hoverMouseY = -1;                          // +0x94  both hover ints
    _hovering    = 0;                           // +0x98  a single BYTE
    _hoverTime   = 0;                           // +0xa0   (+0x99.. left alone)
    _userData    = userData;                    // +0xa8
    _userFlags   = userFlags;                   // +0xb0   (+0xb4 left alone)

    // 0x18011477e: the root Element points back at us. UNGUARDED in retail — if
    // the caller handed us a null InternalPtr this faults. Faithful.
    _root->_dialogParent = this;

    // ---- link into the parent's child list -------------------------------
    // A PREPEND: head at +0x38, tail at +0x40, node = {element, prev, next}
    // with prev at +0x08 and next at +0x10. DialogInstance.h records the
    // tie-break (`?GetFirstDialog@UserInterface@UI@@`, 0x180031150, reads the
    // FIRST element from the list's +0x00) and warns that reading a ctor alone
    // gets this backwards.
    //
    // ENDGAME B.3.2 step 2 — NOT converted to a LinkedList member, deliberately:
    // `LinkedList<T>::Add()` appends at the TAIL; this body inserts at the HEAD,
    // retail's only head-insert site of the `LinkedList<T=8>` instantiation.
    // Left as the node manipulation, including the guard-free linking through a
    // possibly-null `node` on OOM (retail's own asymmetry).
    using ChildNode = Collection::LinkedList<Object::InternalPtr<DialogInstance> >::Node;
    if (UI::DialogInstance* const parentDlg = _parent.Get()) {
        // 0x1801147af: `LEA EDX,[RBP+0x18] / LEA R8D,[RBP+0x8]` with RBP == 0,
        // wrapped in Enter/Allocate/Leave — i.e. exactly ?MemAlloc@…(0x18, 8).
        ChildNode* node = static_cast<ChildNode*>(Object::detail::InternalMemAlloc(0x18, 8));   // LEFT: InternalMemAlloc result (tier-I alloc shape not recognised for Object::detail::InternalMemAlloc; metric)
        if (node) {
            node->element = Object::InternalPtr<DialogInstance>(this);
            node->prev    = nullptr;
            node->next    = nullptr;
        }
        // 0x1801147e1: on an OOM `node` is null (RBP stayed 0) and retail links
        // the null in anyway. Reproduced, guard-free.
        if (ChildNode* head = parentDlg->_children._head) {
            head->prev = node;                              // old head->prev
            node->next = parentDlg->_children._head;        // node->next
        } else {
            parentDlg->_children._tail = node;
        }
        parentDlg->_children._head = node;
        parentDlg->_children._count++;
    }
    // 0x180114803 `MOV RAX,R14`: a ctor returns `this`.
}

// ============================================================================
// ??0ComboDialog@UI@@QEAA@PEAVUserInterface@1@
//     V?$InternalPtr@VElement@UI@@@Object@@
//     V?$InternalPtr@VDialogInstance@UI@@@4@
//     V?$InternalPtr@VComboElement@UI@@@4@@Z               retail 0x180111d00
//
//     ComboDialog::ComboDialog(UserInterface* ui,
//                              InternalPtr<Element> root,
//                              InternalPtr<DialogInstance> parent,
//                              InternalPtr<ComboElement> combo)
//
// Builds the drop-down popup for a combo box: run the base ctor on an
// already-built Element tree, find the ListElement and the ClipRegionElement
// inside it, then size the list's container so the drop-down is exactly as wide
// and tall as the combo box that opened it — unless the clip region already
// reports more content than fits, in which case the padded size wins.
//
// All four InternalPtr parameters arrive by hidden reference; the parent is
// deliberately NOT forwarded to the base ctor. Both facts are argued in the
// banner. Returns `this` in RAX.
// ============================================================================
// ---- 0x180111d31-0x180111d55: the base ctor, in the init list ------------
// The Element InternalPtr is COPIED (0x180111d35) and the parent slot is
// ZEROED (0x180111d31) before the base ctor sees them: the parent is
// deliberately NOT forwarded. dataSource / userData / userFlags are zero — the
// three stack arguments at [R11-0x68], [R11-0x60] and [RSP+0x30].
// 0x180111d56: the stamp is the compiler's. ENDGAME B.3.9 UI-D: the real
// constructor (ComboDialog.h), replacing the door `RtUi_ComboDialog_Ctor`.
UI::ComboDialog::ComboDialog(UserInterface* ui, Object::InternalPtr<Element> root,
                             Object::InternalPtr<DialogInstance> parent,
                             Object::InternalPtr<ComboElement> combo)
    : UI::DialogInstance(ui, root, Object::InternalPtr<UI::DialogInstance>(),
                         nullptr, nullptr, 0) {
    _list         = nullptr;      // +0xb8
    _parentDialog = Object::InternalPtr<UI::DialogInstance>();   // +0xc0  written 0 then...
    _parentDialog = parent;       // +0xc0  ...the real value
    _combo        = nullptr;      // +0xc8  written 0 then...
    _combo        = combo.Get();  // +0xc8  ...the real value

    // ---- 0x180111d91-0x180111e1e: find the ListElement --------------------
    // Re-reads the INCOMING InternalPtr (`MOV RDI,[R15]`, R15 == the R8 spilled
    // at entry), not the local copy handed to the base ctor.
    const uint32_t tidList = UI::ListElement::TypeId();
    {
        UI::Element* const found = FindSelfOrUnderChildren(root.Get(), tidList);
        // 0x180111e0e: CMOVNZ — null unless it really is a ListElement.
        _list = found->IsDerivedFrom(tidList) ? static_cast<UI::ListElement*>(found) : nullptr;
    }

    // 0x180111e1e / 0x180111e24: ?SetElementId@(0) then
    // ?SetFlagsOnOff@(UI::kElementFlagRaisesEvents, 0), both on the list.
    // UNGUARDED derefs of a pointer the CMOV above may have just nulled.
    _list->_elementId = 0;
    ::SetFlagsOnOff(_list, UI::kElementFlagRaisesEvents, 0);

    // ---- 0x180111e56-0x180111edb: find the ClipRegionElement --------------
    const uint32_t tidClip = UI::ClipRegionElement::TypeId();
    UI::ClipRegionElement* clipRegion = nullptr;
    {
        UI::Element* const found = FindSelfOrUnderChildren(root.Get(), tidClip);
        clipRegion = found->IsDerivedFrom(tidClip)
            ? static_cast<UI::ClipRegionElement*>(found) : nullptr;
    }

    // ---- 0x180111edf-0x180111f38: the list's second child, as a container --
    // Retail re-walks `_list -> _children.data[1]` and re-runs IsDerivedFrom
    // for EACH of the two dwords. Reproduced rather than hoisted: the two
    // lookups are separate in the shipped stream and hoisting them would be a
    // different instruction count for the same answer only if nothing in
    // between can change the tree, which is not something this port can claim.
    const uint32_t tidContainer = UI::ContainerElement::TypeId();
    auto AsContainer = [&](UI::Element* kid) -> UI::ContainerElement* {
        return kid->IsDerivedFrom(tidContainer) ? static_cast<UI::ContainerElement*>(kid)
                                                : nullptr;
    };
    int32_t targetWidth = 0;
    int32_t targetHeight = 0;
    {
        UI::ContainerElement* const c = AsContainer(_list->_children._data[1]);
        targetWidth = c->_requestedWidth;
    }
    {
        UI::ContainerElement* const c = AsContainer(_list->_children._data[1]);
        targetHeight = c->_requestedHeight;
    }

    // ---- 0x180111f3e-0x180111f5a: the clip region's content size ----------
    int32_t contentWidth = 0;
    int32_t contentHeight = 0;
    ClipGetContentSize(clipRegion, &contentWidth, &contentHeight);

    // ---- 0x180111f61-0x18011200e: the combo box's padded size -------------
    int32_t paddedWidth = 0;
    int32_t paddedHeight = 0;
    GetPaddedSize(_combo, &paddedWidth, &paddedHeight);

    // ---- 0x180112010-0x1801120b9: grow to the combo when nothing forces a
    //      size and the clip region does not already need more ---------------
    // `container` here is the RAW second child — retail keeps the untyped
    // pointer in RDI for the pads and uses the type-checked one only for the
    // +0xc0 / +0xc4 tests.
    UI::Element* const container = _list->_children._data[1];
    {
        UI::ContainerElement* const c = AsContainer(_list->_children._data[1]);
        if (c->_requestedWidth <= 0) {
            const int32_t left  = container->_leftPad;
            const int32_t right = container->_rightPad;
            if (contentWidth < paddedWidth - right - left)
                targetWidth = paddedWidth - left - right;
        }
    }
    {
        UI::ContainerElement* const c = AsContainer(_list->_children._data[1]);
        if (c->_requestedHeight <= 0) {
            const int32_t top    = container->_topPad;
            const int32_t bottom = container->_bottomPad;
            if (contentHeight < paddedHeight - bottom - top)
                targetHeight = paddedHeight - top - bottom;
        }
    }

    // ---- 0x1801120bb-0x180112104: ?SetSize@ContainerElement@ --------------
    {
        UI::ContainerElement* const c = AsContainer(_list->_children._data[1]);
        ContainerSetSize(c, targetWidth, targetHeight);
    }
    // 0x180112106 `MOV RAX,R14`: a ctor returns `this`.
}
