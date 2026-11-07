// ============================================================================
// src/engine/ui/element_vtable.cpp — WAVE 10B LANE ELEM
//   the eight remaining slots of ??_7Element@UI@@6B@
// ============================================================================
//
// WHAT THIS FILE IS FOR
// ---------------------------------------------------------------------------
// `??_7Element@UI@@6B@` (DLL 0x180156160, 12 slots) is one of the four minted
// UI vtables that `tools/runtime_census.py --unported --namespace UI` still
// lists. As lane UIDATA established and standalone/reg_mint_promoted.cpp
// restates, the row is not "unwritten data" — the table is already minted in
// standalone/rt_data_mint_tables.inc. What was unwritten is its CONTENTS: a
// minted slot is filled from the port registry and stays a NAMED ABORT TRAP
// until the body behind it is ported.
//
// At the wave-10B baseline (docs/history/runtime_port/wave10b/baseline_shadow_7358732.txt)
// the row read `slots=12 ported=3 TRAP=8 purecall=1`. The three that were ours
// are slot 0 (the deleting destructor, wave 9 lane K) and slots 5/6
// (ComputeLayout/Draw, wave 7 lane A, in element.cpp). Slot 4 (ComputeSize) is
// `_purecall` in `Element` itself. This file is the other eight.
//
// Registering a vtable is NOT passive: our ported `??0Element` stamps this
// table into every Element in the game, so the DLL's own
// `UI::UserInterface::Update` dispatches straight into these bodies. That is
// the whole risk and the whole point.
//
//   | slot | Export                      | Retail      | Ord  | Bytes | Oracle |
//   |------|-----------------------------|-------------|------|-------|--------|
//   |  1   | ?TypeName@Element@UI@       | 0x180035440 | 4385 |   8   | replay |
//   |  2   | ?Type@Element@UI@           | 0x180035420 | 4178 |   7   | replay |
//   |  3   | ?Persist@Element@UI@        | 0x180116870 | 3323 |  0x4b | pin    |
//   |  7   | ?Update@Element@UI@         | 0x180116270 | 4489 | 0x5ec | pin    |
//   |  8   | ?Enable@Element@UI@         | 0x180116910 | 2288 |  0xc8 | scratch|
//   |  9   | ?Contains@Element@UI@       | 0x1801169e0 | 1862 | 0x101 | replay |
//   | 10   | ?SetColor@Element@UI@       | 0x180035620 | 3847 |   4   | scratch|
//   | 11   | ?GetBorder@Element@UI@      | 0x180035630 | 2468 |  0xe  | replay |
//
// THE FOUR ORACLE DISPOSITIONS, AND WHY EACH ONE
// ---------------------------------------------------------------------------
//  * `replay`  — the body is a pure read (or writes only a caller-owned return
//    slot) and re-running the shipped export on the SAME live object changes
//    nothing. Full comparison, no qualification.
//  * `scratch` — the body mutates `this`. Ours runs on the live object; a
//    PRIVATE 0xC0-byte copy of the pre-state is handed to the shipped export
//    and all 0xC0 bytes are compared. For `Enable` the copy additionally has
//    its child array emptied — see that body for why, stated rather than
//    hidden.
//  * `pin`     — the body cannot be replayed (it emits into a stream, or it
//    raises UI events and closes dialogs), so there is no post-state to
//    compare. What IS checked, at registration time and through an ordinary
//    diff site, is that the SHIPPED BYTES this port was transcribed from are
//    the bytes in the loaded image: an FNV-1a-64 over the whole shipped body.
//    That is a PRE-FLIGHT ON THE TRANSCRIPTION, not a post-state comparison
//    against retail, and this file says so in both places rather than letting
//    a `divergences=0` row imply more than it measured.
//
// A RETAIL DEAD BRANCH THIS LANE FOUND, AND KEPT
// ---------------------------------------------------------------------------
// `Enable` and `Update` both end a flag update with the same shape:
//
//     MOV  R8D,EAX          ; old flags
//     BTR  R8D,0x17         ; clear bit 23   (or BTS 0x11 / BTR 0x11 / BTS 0x14)
//     MOV  [RCX+0x34],R8D
//     XOR  R8D,EAX          ; what changed
//     TEST R8B,0x1          ; <-- bit ZERO
//     JZ   ...              ; skip the ancestor dirty walk
//
// The XOR can only ever have the ONE bit the BTS/BTR touched set, and that bit
// is 23, 17 or 20 — never bit 0. So `TEST R8B,1` is always zero and the
// ancestor `_flags |= 0x08000000` walk that follows is UNREACHABLE in the
// shipped build. The bytes were read out of the loaded image to be sure
// (0x18011693b: `41 f6 c0 01`), not taken from Ghidra's C; Ghidra's own
// decompiler agrees on one of the five copies, emitting
// "WARNING: Removing unreachable block (ram,0x000180116390)" for Update's
// set-bit-17 arm while keeping the identical clear-bit-17 arm.
//
// The obvious reading is that the source said `if (changed & kElementFlagHidden)`
// where it meant the bit it had just written — bit 0 IS `kElementFlagHidden`,
// and `?Hide@` / `?Show@` / `?SetFlagsOnOff@` (element.cpp) do the same
// sequence with bit 0 and DO reach the walk. That is a retail bug, not a
// decoding error, and porting it out would be a behaviour change: elements
// whose enabled-state or hover-state changes do NOT get marked needs-size-calc
// in the shipped game. The walks below are written literally, so the condition
// is evaluated and provably false rather than deleted.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "ui/Element.h"
#include "ui/UserInterface.h"
// `enum class Module` + `ResolveAddr` (declared once there, byte-identical to
// standalone/resolve.h) and `rtuielem::AtomicExport`/`AtomicAddr`, the B6-5
// door caches.
#include "element_common.h"
#include "ui/DialogInstance.h"
#include "ui/ToolTipDialog.h"   // B4: UserInterface::_toolTipDialog is typed
#include "collection/Collection.h"
#include "io/Serializer.h"   // B4: Persist's scope is IO::Serializer
#include "io/Stream.h"

using UI::Element;

// ENDGAME B.3.6 block A: the eight slot bodies below MOVED into the members of
// the real `UI::Element` (src/engine/ui/Element.h); each registered door is
// now a one-line QUALIFIED delegation. A live Element wears the MINTED
// `??_7Element@UI@@6B@`, whose slots resolve back to these very doors, so an
// unqualified virtual call from a door would re-enter it (B34_BRIEFS A.1).
//
// RULE B6-5: UI is constructed on the loader thread while the main thread
// draws. The doors of OTHER classes the member bodies call (UserInterface,
// DialogInstance, the event-array grow helper, the type-info record) are
// resolved through namespace-scope std::atomic slots, never through a
// function-local static first-call cache inside a member.

namespace {


// ---------------------------------------------------------------------------
// 0x180033690 is `Collection::Array<UI::Event>::SetSize(int, const Event*)` —
// the grow-and-fill helper every UI event push goes through (not exported):
// `rtcoll::RtColl_ArraySetSizeFill_180033690`, called by name.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// One queued UI event, 0x30 bytes — the interior src/engine/ui/
// UserInterface.h:176 leaves opaque, named from Element::Update's own eleven
// record writes (nine distinct offsets) and lane UI1's:
//
//      +0x00 dialog      +0x08 element     +0x10 eventId
//      +0x14 elementId   +0x18 mouseX      +0x1c mouseY
//      +0x20 dragId      +0x24 (never written by either site)
//      +0x28 dragData
//
// +0x24 is deliberately left uninitialised: retail never writes it and the grow
// helper never copies it (userinterface_frame.cpp:277 records the same).
// ---------------------------------------------------------------------------
struct EventRecord {
    void*    dialog;     // +0x00
    void*    element;    // +0x08
    UI::EventId eventId; // +0x10
    int32_t  elementId;  // +0x14
    int32_t  mouseX;     // +0x18
    int32_t  mouseY;     // +0x1c
    uint32_t dragId;     // +0x20
    uint32_t pad24;      // +0x24  NEVER written by retail
    void*    dragData;   // +0x28
};
static_assert(sizeof(EventRecord) == 0x30, "UI::Event stride");
static_assert(offsetof(EventRecord, eventId)  == 0x10, "");
static_assert(offsetof(EventRecord, dragId)   == 0x20, "");
static_assert(offsetof(EventRecord, dragData) == 0x28, "");

// ---------------------------------------------------------------------------
// ATTENDANCE COUNTERS for Persist and Update -- Wave 10C lane ROUTES
// ---------------------------------------------------------------------------
// These count ENTRIES, unconditionally, on every build. NEITHER COUNTER IS A
// VERIFICATION: both bodies remain PORTED, NOT DIFFERENTIALLY VERIFIED (neither
// is replayable). A non-zero count says only that the port is on a live path.
struct ElemCounts {
    unsigned long long update = 0, persist = 0;
    ~ElemCounts() {
        if (update == 0 && persist == 0) {
            printf("[Element] ATTENDANCE: neither Update nor Persist was entered "
                   "in this run.\n");
            return;
        }
        printf("[Element] ATTENDANCE (entries, not comparisons): Update=%llu "
               "Persist=%llu\n", update, persist);
    }
};
ElemCounts g_elemCounts;

// UserInterface::_toolTipDialog (+0x40), a UI::ToolTipDialog, read as the
// DialogInstance it is (an upcast: the field is typed since R-UI-b).
inline UI::DialogInstance* ToolTipDialogOf(const UI::UserInterface* u) {
    return u->_toolTipDialog;
}

}  // namespace

// ============================================================================
// The member bodies
// ============================================================================
// Every body is byte-faithful to the retail DISASSEMBLY, not to Ghidra's C
// (reference_ghidra_c_output_vs_disassembly.md). Where retail derefs without a
// null check, so do we.
namespace UI {

// ---- slots 1/2 and the static TypeId -- ?TypeName@ 0x180035440
// (`MOV RAX,[0x1801fab38]`), ?Type@ 0x180035420 / ?TypeId@ 0x180035430
// (`MOV EAX,[0x1801fab30]`): DECLARE_TYPE_INHERITED_INPLACE's inline bodies
// over the class's own record (ENDGAME B.3.9 TI-UI).

// ---- slot 10  ?SetColor@Element@UI@@UEAAXI@Z  0x180035620 ------------------
//   MOV dword ptr [RCX + 0x38],EDX / RET
// One store. NO dirty walk, NO clamp, NO notification: every OTHER mutator in
// this class ends with the ancestor walk and a reader who assumes symmetry
// would add one.
void Element::SetColor(uint32_t color) {
    _color = color;
}

// ---- slot 11  ?GetBorder@Element@UI@@UEBA…  0x180035630 --------------------
//   MOV RAX,qword ptr [RCX + 0xb0] / MOV qword ptr [RDX],RAX
//   MOV RAX,RDX / RET
// RULE B6-2: the hidden return slot is spelled as the first parameter in
// RETAIL order (`this` in RCX, the slot in RDX) and handed back in RAX.
Object::InternalPtr<Element>* Element::GetBorder(Object::InternalPtr<Element>* ret) const {
    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(_border);
    return ret;
}

// ---- slot 8  ?Enable@Element@UI@@UEAAX_N@Z  0x180116910 --------------------
//
//   old = _flags
//   _flags = enable ? (old & ~0x800000) : (old | 0x800000)   [bit 23, kDisabled]
//   if (((_flags ^ old) & 1) != 0) SetNeedsSizeCalc();       [DEAD — see banner]
//   for (i = 0; i < _children._size; ++i)
//       children[i]->Enable(enable)                          [the CHILD's slot 8]
//
// THE ARGUMENT MEANS WHAT ITS NAME SAYS. `Enable(true)` CLEARS bit 23
// (0x180116928: `TEST DL,DL / JZ`, true arm 0x18011692f: `BTR R8D,0x17`).
//
// The child recursion is a VIRTUAL call — through the CHILD's own slot 8, so a
// ButtonElement child reaches `?Enable@ButtonElement@` (0x180110cc0).
//
// NO NULL CHECK ON THE CHILD (0x1801169ab derefs it immediately), unlike
// `Contains`, which does test. Retail is asymmetric and the asymmetry is kept.
void Element::Enable(bool enable) {
    const uint32_t old = _flags;
    if (enable) _flags = old & ~kElementFlagDisabled;
    else        _flags = old |  kElementFlagDisabled;
    // DEAD IN THE SHIPPED BUILD (`TEST R8B,0x1` on a diff that can only hold
    // bit 23). Written literally so it is evaluated, not deleted.
    if (((_flags ^ old) & kElementFlagHidden) != 0) SetNeedsSizeCalc();

    for (int32_t i = 0; i < _children._size; ++i) {
        Element* child = _children._data[i];
        child->Enable(enable);
    }
}

// ---- slot 9  ?Contains@Element@UI@@UEBA_NHHPEAVUserInterface@2@@Z  0x1801169e0
//
//   if (_flags & 3) return false;                  [hidden | no-hit-test]
//   for (i = 0; i < _children._size; ++i) {
//       child = children[i];
//       if (child && ui->IsInsideClipRegion(x, y) && child->Contains(x,y,ui))
//           return true;
//   }
//   if (_children._size != 0) return false;        [a container never hits itself]
//   return x >= clip.left && x <= clip.right && y >= clip.top && y <= clip.bottom
//       && x >= _layoutLeft && x < _layoutRight
//       && y >= _layoutTop  && y < _layoutBottom;
//
// TWO ASYMMETRIES THAT LOOK LIKE TYPOS AND ARE NOT:
//   * the CLIP test is INCLUSIVE on all four sides (JL / JG at 0x180116a87,
//     0x180116a91, 0x180116a9b, 0x180116aa5) while the ELEMENT RECT test is
//     half-open — `>= left`, `< right` (JL / JGE at 0x180116aab, 0x180116ab1).
//   * `IsInsideClipRegion` is re-evaluated INSIDE the child loop, once per
//     child. Hoisting it would be a divergence; it stays where retail put it.
// The child null test IS present here (0x180116a3b) — the opposite of Enable.
bool Element::Contains(int32_t x, int32_t y, UserInterface* ui) const {
    if ((_flags & (kElementFlagHidden | kElementFlagNoHitTest)) != 0) return false;

    for (int32_t i = 0; i < _children._size; ++i) {
        const Element* child = _children._data[i];
        if (!child) continue;
        if (!ui->IsInsideClipRegion(x, y)) continue;                     // 0x180120830
        if (child->Contains(x, y, ui)) return true;
    }
    if (_children._size != 0) return false;

    return x >= ui->_clipRegion.left && x <= ui->_clipRegion.right &&
           y >= ui->_clipRegion.top  && y <= ui->_clipRegion.bottom &&
           x >= _layoutLeft          && x <  _layoutRight &&
           y >= _layoutTop           && y <  _layoutBottom;
}

// ---- slot 3  ?Persist@Element@UI@@UEAAXAEAVStream@IO@@@Z  0x180116870 ------
//
//   scope.stream = &stream;                        [rsp+0x20]
//   scope.node   = nullptr;                        [rsp+0x28]
//   Encoding* enc = stream[+0x10];
//   node = (stream[+0x08] == 0) ? enc->vt[+0x18](enc, &scope, 0)
//                               : enc->vt[+0x20](enc, &scope, 0);
//   scope.node = node;
//   if (node) node->vt[0](node, 1);                [deleting destructor]
//
// Element itself persists NOTHING: the body opens an encoding SCOPE and closes
// it immediately; every derived element that has state overrides this slot.
//
// THE THIRD ARGUMENT IS NOT OPTIONAL. The two slots are
// `IO::Encoding::CreateReader` (0x1800a8120, +0x18) and `CreateWriter`
// (0x1800a8080, +0x20), both `(Encoding*, Serializer*, int32_t version)`, and
// retail passes the 0 (`XOR R8D,R8D` at 0x180116889, live across the call). A
// two-argument typedef wrote whatever R8 held into the record as the version —
// measured by element_persist_probe.cpp (Wave 10E): ours `80 80 07 ab`,
// retail's `00 00 00 00`.
//
// The two encoding slots are read from the OBJECT by byte displacement; retail's
// IO::Encoding vtable has DIFFERENT offsets from the profile build's
// (reference_encoding_vtable_offsets_differ_between_builds.md).
void Element::Persist(IO::Stream& stream) {
    ++g_elemCounts.persist;
    // The scope above IS an IO::Serializer: {stream, readerWriter}, built by
    // ??0Serializer (0x1800b9960: the encoding's CreateReader / CreateWriter
    // chosen by _direction, version 0) and closed by ??1Serializer
    // (0x1800b99b0: `if (rw) rw->vt[0](rw, 1)`), both of which retail inlines.
    IO::Serializer scope(stream, 0);   // 180116889
}

// ---- slot 7  ?Update@Element@UI@@UEAA…  0x180116270 ------------------------
//
// THE HOT ONE: the UI frame reaches this slot for every element of every open
// dialog, every frame.
//
// `dialog` IS A POINTER TO THE CALLER'S InternalPtr TEMPORARY — retail's R9
// (see Element.h's banner: `LEA R9,[RBP+0x50]` at 0x1801162ca). It is re-read
// on EVERY child iteration (0x1801162c6 is inside the loop) and copied into a
// local the child is given the address of.
//
// Structure, in the order retail runs it:
//   1. recurse into every non-null, non-HIDDEN child through the child's own
//      slot 7 (a VIRTUAL call).
//   2. `inClip` = the mouse position lies within ui->_clipRegion, INCLUSIVE.
//   3. `hovering` = inClip AND the mouse lies STRICTLY inside the layout rect
//      — of the BORDER element if there is one, else of this element.
//   4. write hover bit 17, plus the dead ancestor walk.
//   5. tooltip, gated on bit 16 (enabled) and bit 19 (valid): begin or move it
//      while hovering, end it when not. Bit 20 is "tooltip currently shown".
//   6. raise the hover-enter (id 0) / hover-leave (id 1) event from the OLD
//      bit 17, which is why `oldFlags` is captured before step 4.
//   7. the button/wheel events, or — if the state says a drag is active — the
//      drop event (id 0xc), a Play(3) and EndDragDrop, and nothing else.
//
// THE EVENT IDS ARE NOT SEQUENTIAL AND THE ORDER IS NOT THE STATE ORDER:
// state+0x01 -> 2, +0x02 -> 4, +0x03 -> 3, +0x04 -> 5, +0x05 -> 7, +0x06 -> 6.
// Transcribed one at a time from 0x18011663a..0x1801167e5.
void Element::Update(const System::Time& time, UserInterface* ui,
                     Object::InternalPtr<DialogInstance>* dialog,
                     const UpdateState& state) {
    ++g_elemCounts.update;
    // ---- 1. children ------------------------------------------------------
    for (int32_t i = 0; i < _children._size; ++i) {
        Element* child = _children._data[i];
        if (!child) continue;
        if ((child->_flags & kElementFlagHidden) != 0) continue;
        Object::InternalPtr<DialogInstance> local = *dialog;   // re-read every iteration
        child->Update(time, ui, &local, state);
    }

    UserInterface* const u = ui;
    const int32_t mx = state.mouseX;
    const int32_t my = state.mouseY;

    // ---- 2. clip test (INCLUSIVE) -----------------------------------------
    const bool inClip = !(mx < u->_clipRegion.left || mx > u->_clipRegion.right ||
                          my < u->_clipRegion.top  || my > u->_clipRegion.bottom);

    // ---- 3/4. hover bit ---------------------------------------------------
    const uint32_t oldFlags = _flags;
    const Element* rect = _border ? _border : this;
    const int32_t l = rect->_layoutLeft, r = rect->_layoutRight;
    const int32_t t = rect->_layoutTop,  b = rect->_layoutBottom;

    bool hovering;
    if (l < mx && mx < r && t < my && my < b && inClip) {
        hovering = true;
        _flags = oldFlags | kElementFlagHovered;                // BTS 0x11
        // DEAD (see banner); Ghidra itself folds this arm away.
        if ((((oldFlags | kElementFlagHovered) ^ oldFlags) & kElementFlagHidden) != 0)
            SetNeedsSizeCalc();
    } else {
        hovering = false;
        _flags = oldFlags & ~kElementFlagHovered;               // BTR 0x11
        if ((((oldFlags & ~kElementFlagHovered) ^ oldFlags) & kElementFlagHidden) != 0)
            SetNeedsSizeCalc();
    }

    // ---- 5. tooltip -------------------------------------------------------
    const uint32_t f = _flags;
    if ((f & kElementFlagRaisesEvents) == 0) return;            // BT 0x10 / JNC exit

    if ((f & kElementFlagToolTipValid) != 0) {                  // BT 0x13
        DialogInstance* const dlg = dialog->Get();
        if (hovering && dlg && dlg->_hovering != 0) {           // +0x98
            EventRecord rec;
            rec.dialog    = dlg;
            rec.element   = this;
            rec.elementId = _elementId;
            rec.mouseX    = mx;
            rec.mouseY    = my;
            rec.dragId    = 0;
            rec.dragData  = nullptr;
            if ((f & kElementFlagToolTipShown) == 0) {          // BT 0x14 clear
                rec.eventId = EventId::ToolTipBegin;            // tooltip BEGIN
                rtcoll::RtColl_ArraySetSizeFill_180033690(&u->_events, u->_events._size + 1, &rec);
                Object::InternalPtr<DialogInstance> dlgLocal(dialog->Get());
                Object::InternalPtr<Element> elemLocal(this);
                u->BeginToolTip(&dlgLocal, &elemLocal, mx, my);         // 0x180120860
            } else {
                rec.eventId = EventId::ToolTipMove;             // tooltip MOVE
                rtcoll::RtColl_ArraySetSizeFill_180033690(&u->_events, u->_events._size + 1, &rec);
                DialogInstance* const tip = ToolTipDialogOf(u);
                if (tip) {
                    // y is offset by the tooltip ROOT ELEMENT's layout height
                    // (dlg+0x50 is the root, element+0x64 is _layoutHeight), so
                    // the tip sits ABOVE the cursor.
                    const int32_t h = tip->_root->_layoutHeight;
                    tip->SetUpperLeft(mx, my - h, false);               // 0x180114a60
                }
            }
            const uint32_t o = _flags;
            _flags = o | kElementFlagToolTipShown;              // BTS 0x14
            if ((((o | kElementFlagToolTipShown) ^ o) & kElementFlagHidden) != 0)
                SetNeedsSizeCalc();                             // DEAD

        } else if ((f & kElementFlagToolTipShown) != 0) {
            dialog->Get()->EndHover(u);                                 // 0x180113d10
            DialogInstance* const tip = ToolTipDialogOf(u);
            bool clear = false;
            if (!tip) {
                clear = true;
            } else if (this == tip->_dataSource) {                     // +0x30
                // Retail re-tests `tip` here after already branching on it
                // (0x1801164fc). Kept: it is the body.
                if (tip) {
                    Object::InternalPtr<DialogInstance> local(tip);
                    u->CloseDialog(&local);                             // 0x180120f20
                }
                clear = true;
            }
            if (clear) {
                u->_toolTipDialog = nullptr;
                u->_toolTipOwner  = nullptr;
            }
            const uint32_t o = _flags;
            _flags = o & ~kElementFlagToolTipShown;             // BTR 0x14
            if ((((o & ~kElementFlagToolTipShown) ^ o) & kElementFlagHidden) != 0)
                SetNeedsSizeCalc();                             // DEAD
        }
    }

    // ---- 6. hover enter / leave, from the OLD bit 17 ----------------------
    EventRecord rec;
    rec.dialog    = dialog->Get();
    rec.element   = this;
    rec.elementId = _elementId;
    rec.mouseX    = mx;
    rec.mouseY    = my;
    rec.dragId    = 0;
    rec.dragData  = nullptr;

    const bool wasHovering = (oldFlags & kElementFlagHovered) != 0;
    if (!wasHovering) {
        if (!hovering) return;
        rec.eventId = EventId::HoverEnter;                      // hover ENTER
        rtcoll::RtColl_ArraySetSizeFill_180033690(&u->_events, u->_events._size + 1, &rec);
    } else if (!hovering) {
        rec.eventId = EventId::HoverLeave;                      // hover LEAVE
        rtcoll::RtColl_ArraySetSizeFill_180033690(&u->_events, u->_events._size + 1, &rec);
        return;
    }

    // ---- 7. the input events ---------------------------------------------
    auto push = [&](EventId id) {
        rec.dialog    = dialog->Get();
        rec.element   = this;
        rec.eventId   = id;
        rec.elementId = _elementId;
        rec.mouseX    = mx;
        rec.mouseY    = my;
        rec.dragId    = 0;
        rec.dragData  = nullptr;
        rtcoll::RtColl_ArraySetSizeFill_180033690(&u->_events, u->_events._size + 1, &rec);
    };

    if (state.dragActive) {
        // A drop only happens on the button the drag was started with; retail
        // returns without raising ANY other event when +0x04 is clear.
        if (!state.b04) return;
        rec.dialog    = dialog->Get();
        rec.element   = this;
        rec.eventId   = EventId::Drop;                          // DROP
        rec.elementId = _elementId;
        rec.mouseX    = mx;
        rec.mouseY    = my;
        rec.dragId    = u->_dragDropId;
        rec.dragData  = u->_dragDropData;
        rtcoll::RtColl_ArraySetSizeFill_180033690(&u->_events, u->_events._size + 1, &rec);
        u->Play(3);                                                     // 0x1801223d0
        u->EndDragDrop();                                               // 0x180120af0
        return;
    }

    if (state.buttonPressed) push(EventId::LeftPress);
    if (state.b02)           push(EventId::MiddlePress);
    if (state.b03)           push(EventId::RightPress);
    if (state.b04)           push(EventId::LeftRelease);
    if (state.b05)           push(EventId::MiddleRelease);
    if (state.b06)           push(EventId::RightRelease);
    if (state.leftDown && (state.dragDx != 0 || state.dragDy != 0)) push(EventId::Drag);
}

}  // namespace UI

// (ENDGAME B.3.9 UI-D: the eight slot doors that stood here -- TypeName,
// Type, Persist, Update, Enable, Contains, SetColor, GetBorder -- are
// retired with the minted `??_7Element@UI@@6B@`; nothing else reached them.)

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::Element's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@Element@UI@@ (0x1801fab30): name "Element", parent
// "Object::Internal" (retail ?_ParentTypeName@Element@UI@@), no file extension; factories:
// CreateNewObject the class's own, CreateInPlaceObject Object::Internal's (inherited).
// ============================================================================
DEFINE_TYPE(UI::Element, "Object::Internal", nullptr)
