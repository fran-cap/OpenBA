// ============================================================================
// src/engine/ui/userinterface_hover.cpp — Wave 10C lane UIF6
// ============================================================================
//
// The four ORDINARY-MEMBER rows that entered the UI census last wave because
// two faithful ports (lane ELEM's ??_7Element@UI@@6B@ slots and lane CLOSEDLG's
// CloseDialog) began calling callees retail itself calls, and both lanes
// resolved them BY NAME rather than through a silent address door. That is the
// correct trade (reference_porting_can_grow_the_cutover_surface.md); this file
// is the follow-through.
//
//   0x180120830  ?IsInsideClipRegion@UserInterface@UI@@QEBA_NHH@Z
//   0x180120af0  ?EndDragDrop@UserInterface@UI@@QEAAXXZ
//   0x180113d10  ?EndHover@DialogInstance@UI@@QEAAXPEAVUserInterface@2@@Z
//   0x180120860  ?BeginToolTip@UserInterface@UI@@QEAAX
//                    V?$InternalPtr@VDialogInstance@UI@@@Object@@
//                    V?$InternalPtr@VElement@UI@@@4@HH@Z
//
// All four were confirmed ABSENT from the tree before a line was written, by
// ADDRESS and by MANGLED NAME across code/, runtime/, engine/ and standalone/
// (reference_grep_for_the_port_before_writing_it.md). Every hit was a CONSUMER
// -- an RT_EXPORT_ONCE / ResolveExport call site in element_vtable.cpp or
// userinterface_close.cpp -- never a definition. `?BeginToolTip@` in particular
// is referenced through a SPLIT STRING LITERAL (element_vtable.cpp:749-750),
// which a naive grep for the full mangled name does not find; the search that
// found it was for the bare identifier.
//
// ---------------------------------------------------------------------------
// THE ONE ADDRESS DOOR, DECLARED RATHER THAN HIDDEN
// ---------------------------------------------------------------------------
// 0x180033690 `Collection::Array<UI::Event>::SetSize(int, const Event*)` and
// 0x18007a0d0 `Collection::Array<InternalPtr<DialogInstance>>::SetSize` are the
// only two callees in this file with no exported name (checked against the
// shipped export table with pefile, not assumed). NEITHER IS NEW DEBT:
//   * 0x180033690 is already owned by userinterface_frame.cpp:216,
//     userinterface_close.cpp:201 and element_vtable.cpp:177 -- this is the
//     FOURTH call site of the same helper, not a fourth obligation.
//   * 0x18007a0d0 is already owned by userinterface_showdialog.cpp:110, and is
//     already PORTED and registered by src/engine/collection/array.cpp
//     (J3_ENTRY3(18007a0d0, kSpec_Fill8Z)).
// Both go through ResolveAddr(Module::Runtime, ...), which consults the port
// registry FIRST, so a port of either is reached
// (reference_resolveaddr_consults_port_registry_first.md).
//
// EVERY OTHER callee is reached by NAME. That was checked against the export
// table rather than assumed -- several of them mangle `QEAA`/`SA`, and this DLL
// exports its private members anyway (reference_aeaa_is_private_not_unexported).
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "system/String.h"
#include "ui/ToolTipDialog.h"
#include "ui/String.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
// ResolveAddr lives in standalone/resolve.h, which drags in <windows.h> and so
// must not be included from runtime/src (rt_port.h explains why).
// ENDGAME B.3.6 block E: element_common.h carries the declaration exactly as
// ~30 sibling runtime files spell it, and the RULE B6-5 atomic door caches.
#include "element_common.h"
#include "collection/Collection.h"

#include <new>

namespace {

using UI::UserInterface;
using UI::Element;

// ---------------------------------------------------------------------------
// Dependencies these bodies CALL rather than reimplement.
// ---------------------------------------------------------------------------
// B3R RT-UI: called by name -- 0x180033690 (Array<UI::Event>::SetSize-fill),
// 0x18007a0d0 (Array<InternalPtr<DialogInstance>>::SetSize-fill) and
// `??2ToolTipDialog@UI@@SAPEAX_K@Z` (0x18004cfd0, RtUi_ToolTipDialog_OperatorNew).
// `?GetToolTipString@Element@UI@@QEBA?AVString@System@@XZ` (0x1800356a0) —
// fragment pass F2 defined the member (tooltipdialog.cpp): a direct member
// call now, its prvalue constructed straight into the caller's slot exactly
// as retail's RETSLOT call writes it.
void GetToolTipStringInto(const Element* elem, System::String* ret) {
    new (ret) System::String(elem->GetToolTipString());
}

// ---------------------------------------------------------------------------
// ENTRY COUNTERS — the evidence that a registered body RAN AT ALL.
//
// Deliberately separate from any oracle counter: none of those could
// distinguish "the port ran and the oracle was quiet" from "the port was never
// reached"
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md,
// reference_uncalled_port_is_not_verified.md). These count EVERY entry in EVERY
// configuration and print unconditionally, so a zero here means the row is
// registered and DEAD whatever else the board says.
// ---------------------------------------------------------------------------
uint64_t g_clipEntries       = 0;
uint64_t g_clipInside        = 0;   // effectiveness control: how many said true
uint64_t g_endDragEntries    = 0;
uint64_t g_endDragDestroyed  = 0;   // took the "destroy the drag element" arm
uint64_t g_endHoverEntries   = 0;
uint64_t g_endHoverPushed    = 0;   // took the _hovering arm (pushed event 0x11)
uint64_t g_tipEntries        = 0;
uint64_t g_tipRejected       = 0;   // failed one of the four early-out guards
uint64_t g_tipClosedPrev     = 0;   // closed an existing tooltip first
uint64_t g_tipNonEmpty       = 0;   // took the GetToolTipString arm
uint64_t g_tipAllocFailed    = 0;

struct HoverSummary {
    ~HoverSummary() {
        printf("[RtPort] UI::UserInterface::IsInsideClipRegion  entries=%llu inside=%llu\n",
                    (unsigned long long)g_clipEntries, (unsigned long long)g_clipInside);
        printf("[RtPort] UI::UserInterface::EndDragDrop  entries=%llu destroyedElement=%llu\n",
                    (unsigned long long)g_endDragEntries, (unsigned long long)g_endDragDestroyed);
        printf("[RtPort] UI::DialogInstance::EndHover  entries=%llu pushedEvent0x11=%llu\n",
                    (unsigned long long)g_endHoverEntries, (unsigned long long)g_endHoverPushed);
        printf("[RtPort] UI::UserInterface::BeginToolTip  entries=%llu rejected=%llu "
                    "closedPrev=%llu nonEmptyString=%llu allocFailed=%llu\n",
                    (unsigned long long)g_tipEntries, (unsigned long long)g_tipRejected,
                    (unsigned long long)g_tipClosedPrev, (unsigned long long)g_tipNonEmpty,
                    (unsigned long long)g_tipAllocFailed);
        fflush(stdout);
    }
};
HoverSummary g_hoverSummary;

// The queued UI event is `UI::Event` (UserInterface.h). `_pad24` is called out
// because RETAIL LEAVES IT UNINITIALISED at both push sites in this file -- the
// stack record is never zeroed and no store covers +0x24..+0x27 -- and this
// port does not "tidy" it to zero either.

// ---------------------------------------------------------------------------
// ENDGAME B.3.1 step 2: the two release idioms inlined in BeginToolTip
// (0x18012096a..0x180120993 and 0x1801209dc..0x180120a05) are `~String`, retail
// asymmetry and all (src/engine/system/String.h). The hand-written copy that
// lived here is gone; the two call sites are inner scopes that end exactly
// where retail's releases are.

}  // namespace

// ============================================================================
// ?IsInsideClipRegion@UserInterface@UI@@QEBA_NHH@Z          retail 0x180120830
// ============================================================================
// Twelve instructions, no calls, no writes -- a pure predicate over the four
// ints of _clipRegion at +0xfc. The four comparisons are SIGNED (JL/JG), and
// both bounds are INCLUSIVE.
//
//   180120830  CMP EDX,[RCX+0xfc]   JL  false      x <  left   -> false
//   180120838  CMP EDX,[RCX+0x104]  JG  false      x >  right  -> false
//   180120840  CMP R8D,[RCX+0x100]  JL  false      y <  top    -> false
//   180120849  CMP R8D,[RCX+0x108]  JG  false      y >  bottom -> false
//                                                  otherwise   -> true
//
// Note the operand order the disassembly forces and the C would hide: the
// FIRST pair tests x against left/right (+0xfc/+0x104) and the SECOND tests y
// against top/bottom (+0x100/+0x108), i.e. the struct is {left, top, right,
// bottom} and the tests interleave it. ClipRegion in UserInterface.h already
// names that member order from GetClipRegion (0x180031000).
// ============================================================================
bool UI::UserInterface::IsInsideClipRegion(int32_t x, int32_t y) const {
    ++g_clipEntries;
    const ClipRegion& c = _clipRegion;
    if (x < c.left)   return false;
    if (x > c.right)  return false;
    if (y < c.top)    return false;
    if (y > c.bottom) return false;
    ++g_clipInside;
    return true;
}

// ============================================================================
// ?EndDragDrop@UserInterface@UI@@QEAAXXZ                    retail 0x180120af0
// ============================================================================
// Pushes one 0x30-byte UI event (id 0xe, "drag-drop CANCEL") naming the current
// drag, then tears the whole drag-drop state down.
//
// THREE THINGS A TIDY REWRITE WOULD GET WRONG, all kept:
//
//  1. The event's `elementId` field (+0x14) is -1 here, where EndHover writes 0.
//     0x180120b2e: MOV dword ptr [RSP+0x34],0xffffffff.
//  2. +0x24 of the record is NEVER WRITTEN (see EventRecord above). The record
//     is a raw stack local in retail and stays one here.
//  3. `_dragDropActive` (+0x88) is NOT cleared, even though every neighbouring
//     field is. Retail simply does not touch it -- the stores stop at +0x80.
//
// The `_dragDropElement` store appears TWICE in the disassembly, at 0x180120b5a
// (inside the destroy arm) and 0x180120b5e (the merge point), because the
// compiler duplicated the tail. Both are reproduced; the second is redundant
// and harmless, exactly as shipped.
//
// Vtable slot 0 called with 1 is the DELETING destructor: retail destroys AND
// frees the drag-drop element here. UserInterface.h already records that
// ("_dragDropElement  Update destroys it (vslot 0)").
// ============================================================================
void UI::UserInterface::EndDragDrop() {
    ++g_endDragEntries;

    Event rec;                              // deliberately NOT zeroed
    rec.dialog    = _dragDropDialog;        // 0x180120afa / 0x180120b04
    rec.element   = nullptr;                // 0x180120b21
    rec.eventId   = UI::EventId::DragDropCancel;  // 0x180120b26  drag-drop CANCEL
    rec.elementId     = -1;                     // 0x180120b2e
    rec.mouseX     = 0;                      // 0x180120b36 } one QWORD
    rec.mouseY     = 0;                      //             }
    rec.value     = static_cast<int32_t>(_dragDropId);   // 0x180120b09 / 0x180120b10
    rec.userData  = _dragDropData;          // 0x180120b14 / 0x180120b3b
    rtcoll::RtColl_ArraySetSizeFill_180033690(&_events, _events._size + 1, &rec);               // 0x180120b40

    _dragDropDialog = nullptr;                                        // 0x180120b45

    Element* const elem = _dragDropElement;                           // 0x180120b49
    if (elem) {
        ++g_endDragDestroyed;
        // Element vtable slot 0, flag 1 == deleting destructor (destroy AND
        // free): `delete` through the virtual destructor.
        delete elem;                                                  // 0x180120b58
        _dragDropElement = nullptr;                                   // 0x180120b5a
    }
    _dragDropElement   = nullptr;                                     // 0x180120b5e
    _dragDropId        = 0;                                           // 0x180120b62
    _dragDropData      = nullptr;                                     // 0x180120b65
    _dragDropGrabX     = 0;                                           // 0x180120b69 } one QWORD
    _dragDropGrabY     = 0;                                           //             }
    // +0x88 _dragDropActive is NOT cleared by retail. Left alone.
}

// ============================================================================
// ?EndHover@DialogInstance@UI@@QEAAXPEAVUserInterface@2@@Z  retail 0x180113d10
// ============================================================================
// Clears the dialog's hover flag, emits UI event 0x11 ("end hover") naming the
// dialog, and resets the hover timestamp.
//
// FIELD-NAME CORRECTION SHIPPED WITH THIS PORT. src/engine/ui/
// DialogInstance.h called +0x98 `_closing` and flagged it "NAME IS A GUESS",
// and +0xa0 `_opaqueA0` "nothing in this lane reads it". Both are now named
// from evidence:
//   * ?IsHovering@DialogInstance@UI@@QEBA_NXZ (0x1800455a0) is the ENTIRE
//     function `MOVZX EAX,byte ptr [RCX+0x98] / RET` -- a one-line exported
//     accessor, the strongest field evidence class this tree recognises.
//     So +0x98 is `_hovering`, and "closing" was actively misleading.
//   * +0xa0 takes (int64)(0.0 / _secondsPerTick) here, i.e. a System::Time
//     tick count, not a pointer. Retyped void* -> int64_t; same 8 bytes.
//
// THE TRAILING STORE IS UNCONDITIONAL. It sits AFTER the `if`, so a dialog that
// was not hovering still gets its timer reset. 0.0 / 1e-6 is 0.0 and truncates
// to 0, so this is a plain zero store in every real run; it is written as the
// division retail wrote (via the exported _secondsPerTick) rather than folded
// to a literal, so it keeps tracking the constant Engine::Startup installs.
// ============================================================================
// ENDGAME B.3.6 block E: this is a `UI::DialogInstance` member (block B
// declared it on the real class and left it undefined because the body lives
// in this file); DEFINED here, the door delegates.
void UI::DialogInstance::EndHover(UserInterface* ui) {
    ++g_endHoverEntries;

    if (_hovering != 0) {                           // 0x180113d16
        ++g_endHoverPushed;
        _hovering = 0;                              // 0x180113d24

        UserInterface::Event rec;                   // deliberately NOT zeroed
        rec.dialog    = this;                       // 0x180113d2b
        rec.element   = nullptr;                    // 0x180113d45
        rec.eventId   = UI::EventId::HoverEnd;      // 0x180113d39 (a QWORD store,
        rec.elementId     = 0;                          //   covering +0x10 AND +0x14)
        rec.mouseX     = 0;                          // 0x180113d4a } one QWORD
        rec.mouseY     = 0;                          //             }
        rec.value     = 0;                          // 0x180113d51
        rec.userData  = nullptr;                    // 0x180113d55
        rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events, ui->_events._size + 1, &rec);   // 0x180113d5a
    }

    // 0x180113d5f: XORPS XMM0,XMM0 / DIVSD XMM0,[0x1801fe728] / CVTTSD2SI
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    _hoverTime = static_cast<int64_t>(0.0 / ::System::Timer::_secondsPerTick);   // 0x180113d6f
}

// ============================================================================
// ?BeginToolTip@UserInterface@UI@@QEAAX...                  retail 0x180120860
// ============================================================================
// Builds the tooltip dialog for `elem` and queues it for the next Update.
//
// Both InternalPtr<> parameters are passed BY ADDRESS, not by value: MSVC hands
// a class with a non-trivial copy constructor to a callee by hidden reference,
// and the disassembly reads through both (MOV RAX,[R8] at 0x18012088e, MOV
// RCX,[R14] at 0x180120904). element_vtable.cpp:747 already calls it that way,
// passing `&dlgLocal` / `&elemLocal`, so the two agree.
//
// THE FOUR EARLY-OUT GUARDS (0x18012088e..0x1801208b8), all on the ELEMENT:
//     elem->_toolTipDialog (+0x40) != 0        the Dialog template to instance
//     elem->_toolTip.table (+0x48) != 0   \    i.e. UIString::IsValid(), spelled
//     elem->_toolTip.index (+0x50) >= 0   /    out rather than called
//     (elem->_flags (+0x34) & 0x800000) == 0
//
// A RETAIL QUIRK, KEPT AND NOT "FIXED" (0x1801208ca):
//     MOV RAX,[RCX+0x30] ; CMP RAX,RAX ; JNE skip
// compares a register with ITSELF, so the branch is never taken and the guard
// it looks like is inert: an existing tooltip is ALWAYS closed. This is the
// same class of thing as the "retail re-tests `tip`" note at
// element_vtable.cpp:781 -- it is the body, so it is reproduced. Writing the
// comparison the code LOOKS like it wanted (tip->_dataSource != elem) would be
// a behavioural change, not a cleanup.
//
// UNGUARDED DEREF, DELIBERATELY LEFT UNGUARDED: if operator new returns null,
// retail stores null into _toolTipDialog and then hands it straight to
// ToolTipDialog::SetText. Per CLAUDE.md, a faithful crash there is retail's
// contract with its own allocator, not a bug for this port to paper over.
// ============================================================================

// ENDGAME B.3.6 block E: the body is the member. Both InternalPtr arguments are
// the ADDRESSES of the caller's temporaries (RULE B6-11, see above), spelled
// `dlgPtr`/`elemPtr` below as the port always spelled them.
void UI::UserInterface::BeginToolTip(Object::InternalPtr<DialogInstance>* dialog,
                                     Object::InternalPtr<Element>* element,
                                     int32_t x, int32_t y) {
    ++g_tipEntries;
    UserInterface* const self = this;

    Element* const elem = element->Get();                              // 0x18012088e
    if (elem->_toolTipDialog == nullptr)            { ++g_tipRejected; return; }  // +0x40
    if (elem->_toolTip.table == nullptr)            { ++g_tipRejected; return; }  // +0x48
    if (elem->_toolTip.index < 0)                   { ++g_tipRejected; return; }  // +0x50
    if ((elem->_flags & kElementFlagDisabled) != 0) { ++g_tipRejected; return; }  // +0x34

    // --- close whatever tooltip is already up (always, see the quirk above) --
    if (self->_toolTipDialog != nullptr) {                            // 0x1801208be
        ++g_tipClosedPrev;
        Object::InternalPtr<DialogInstance> local(self->_toolTipDialog); // 0x1801208d3
        CloseDialog(&local);                                          // 0x1801208de
        self->_toolTipDialog = nullptr;                               // 0x1801208e3
        self->_toolTipOwner  = nullptr;                               // 0x1801208e7
    }

    // --- allocate and construct the ToolTipDialog ---------------------------
    // 0xc0 is retail's literal, and it is why DialogInstance.h's size assert
    // says the ALLOCATED size of a DialogInstance-derived object is 0xc0 while
    // the ctor-initialised extent is 0xb8.
    //
    // `??2ToolTipDialog` stays a MemoryAllocator door (B36_RULES 7). ENDGAME
    // B.3.9 UI-D: the construction is retail's inlined ??0ToolTipDialog
    // (0x1800daca0) — the 6-argument DialogInstance ctor with dataSource = the
    // element, userData = 0, flags = 0, then the stamp — spelt as the real
    // constructor (the DialogInstance ctor door and the minted
    // `??_7ToolTipDialog` stamp are retired).
    void* const mem = RtUi_ToolTipDialog_OperatorNew(0xc0);            // 0x1801208f0
    ToolTipDialog* tip = nullptr;
    if (mem) {
        tip = ::new (mem) ToolTipDialog(                               // 0x180120907..0x18012092e
            self, elem->_toolTipDialog,
            *dialog, Object::InternalPtr<Element>(elem));
    } else {
        ++g_tipAllocFailed;                                            // 0x18012093a
    }
    self->_toolTipDialog = tip;                                        // 0x18012093d

    // --- decide which string the tooltip shows ------------------------------
    // Retail asks GetToolTipString FIRST purely to test whether it is empty,
    // RELEASES that string, and then asks a SECOND time on the non-empty arm.
    // The double call is retail's (0x180120948 and 0x18012099f); it is not
    // hoisted, because GetToolTipString is not obviously pure and hoisting it
    // would change the refcount traffic the DLL sees.
    bool nonEmpty;
    {
        System::String probe;
        GetToolTipStringInto(elem, &probe);                             // 0x180120948

        // 0x18012094e..0x180120966: empty == (data == 0) || (length == 0), then
        // SETE inverts it, so `nonEmpty` is what the branch below actually tests.
        nonEmpty = !(probe._data == nullptr ||
                     RtSystem::StringLength(probe._data) == 0);
    }                                                                  // 0x18012096a

    {
        System::String text;
        if (nonEmpty) {
            ++g_tipNonEmpty;
            GetToolTipStringInto(elem, &text);                         // 0x18012099f
        } else {
            // A 16-byte copy of the element's UI::String, then the conversion
            // operator UI::String -> System::String — a member of the real
            // value type now (block D, sprite.cpp). Its by-value result is
            // constructed straight into `text`'s storage, exactly as retail's
            // retslot call writes the caller's slot (`text` is still the empty
            // default-constructed string there, so nothing is overwritten).
            ::UIString tmp = elem->_toolTip;                           // 0x1801209b4
            new (&text) System::String(tmp.operator System::String()); // 0x1801209c8
        }
        // `ToolTipDialog::SetText` (0x1800dab00) — non-virtual member of the
        // real class (block B).
        self->_toolTipDialog->SetText(text);
    }                                                                   // 0x1801209dc

    // --- position it, open it, queue it -------------------------------------
    self->_toolTipOwner = dialog->Get();                                // 0x180120a05

    if (self->_toolTipDialog != nullptr) {                              // 0x180120a0c
        // y is offset UP by the tooltip root element's laid-out height, so the
        // tip sits ABOVE the cursor. element_vtable.cpp:761 does the same sum
        // for the tooltip MOVE arm, from the same two offsets. `_root` (+0x50)
        // through the dialog's own one-load accessor (0x180045580).
        const int32_t h = self->_toolTipDialog->GetRoot()->_layoutHeight;   // 0x180120a15 / 0x180120a1d
        // `DialogInstance::SetUpperLeft` (0x180114a60) — non-virtual member.
        self->_toolTipDialog->SetUpperLeft(x, y - h, false);            // 0x180120a27
    }

    ToolTipDialog* const queued = self->_toolTipDialog;                 // 0x180120a2c
    // vtable slot 4 (byte offset 0x20) is DialogInstance::OnOpened — VIRTUAL,
    // dispatched through the stamped ToolTipDialog table.
    queued->OnOpened();                                                 // 0x180120a37

    const DialogInstancePtr fill = queued;
    rtcoll::RtColl_ArraySetSizeFill_18007a0d0(&self->_addDialogs,
                        self->_addDialogs._size + 1, &fill);            // 0x180120a47
}
