// ============================================================================
// src/engine/ui/userinterface_close.cpp — Wave 10 lane CLOSEDLG
//
//   ?CloseDialog@UserInterface@UI@@QEAAXV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z
//   0x180120f20   ordinal 1725
// ============================================================================
//
// WHAT THIS ROW WAS BLOCKED ON, AND WHAT IT IS ACTUALLY BLOCKED ON
//
// Recorded blocker through Waves 7-9: "allocator NO-GO". Lane UI1 retired that
// (docs/history/runtime_port/WAVE10_LANE_UI1.md §1b): the NO-GO is a verdict on
// REPLACING the allocator arena, not on a ported body CALLING the shipped one,
// and this function's allocator use is two inlined `MemFree`s plus a
// `DynArrayPush`, all of which have exported doors.
//
// The blocker UI1 said would survive is ORACLE COVERAGE, and it does survive,
// but it is SMALLER than UI1 predicted. UI1 wrote:
//
//     "a predictive round over CloseDialog would compare a handful of scalars
//      on `this` (+0x38/+0x40 tooltip clear, +0x30 array shrink) against a very
//      large Exclude set."
//
// Two of the five effects UI1 listed as unreachable are in fact FULLY
// PREDICTABLE and are compared here:
//
//   * **the unlinks in another object's list** (UI1's item 4). `parent+0x38`,
//     `parent+0x40` and `parent+0x48`, plus the two neighbour-node fixups
//     `prev->next` and `next->prev`, are computed from pointers this body can
//     read and are left in memory retail does not free. UI1 called this "a
//     graph whose extent must be enumerated before Watch can see it" — so this
//     body ENUMERATES IT, by calling `Watch` from inside itself once it has
//     walked to the node. That is sound precisely because a predicting body
//     writes nothing: the snapshot is still taken before the DLL runs.
//
//   * **the `_addDialogs` compaction** — the shift loop at 0x180121337 writes
//     the array's own storage, which is not inside `this` at all. It is watched
//     and compared element by element.
//
// The three that genuinely are not expressible, each declared with `Exclude`
// naming the callee that owns it:
//
//   1. the two `MemFree(node)` calls — the node's post-state belongs to the
//      allocator, so the `node->prev = 0` / `node->next = 0` stores retail
//      makes just before freeing are recorded for read-back and NOT compared
//      (they land in an Exclude region and count as `unverifiable`);
//   2. the `DynArrayPush` onto `_removedDialogs` (+0x18/+0x20) — the pushed
//      element lands at an address the allocator chose;
//   3. the `OnClosed` vtable release, `EndDragDrop`, `UpdateEvents` and
//      `Array<UI::Event>::SetSize` — four suppressed calls whose footprints on
//      `this` are `_events` (+0x48) and the drag-drop block (+0x60).
//
// ---------------------------------------------------------------------------
// THE ARMING PREDICATE, AND WHY A ROUND THAT IS NOT ARMED IS COUNTED
// ---------------------------------------------------------------------------
// `CloseDialog` recurses into itself twice — once per child dialog
// (0x180120f6b) and once for the tooltip (0x180120fa6). The child loop's
// TERMINATION CONDITION is the very count the recursive call decrements:
//
//     while (*(int*)(dlg + 0x48) > 0) { ... CloseDialog(child) ... }
//
// In predict mode a suppressed call decrements nothing, so that loop would spin
// forever. So the round is ARMED only on calls where neither recursion is
// entered — `dlg` has no children and the tooltip arm's condition is false.
// Everything else is handed straight to the DLL and COUNTED.
//
// The count is printed at exit as `indeterminate=N`. It has to be, or the
// coverage claim is a lie by omission: a green board over the armable subset,
// reported without the size of the unarmed subset, is exactly the
// partial-oracle-reading-as-full failure this project keeps hitting. `rounds=`
// alone cannot express it — an unarmed call never constructs a round.
//
// ---------------------------------------------------------------------------
// FACTS THE DECOMPILED C DROPS OR GETS WRONG
// ---------------------------------------------------------------------------
// Read off 0x180120f20..0x1801213b6, not off Ghidra's C
// (reference_ghidra_c_output_vs_disassembly.md):
//
//  1. **A dead compare with a LIVE load**, 0x180120f8a:
//         MOV RAX,[RCX+0x30] ; CMP RAX,RAX ; JNZ
//     `CMP RAX,RAX` is never unequal, so the branch is unreachable — but the
//     LOAD still happens and still faults on a bad `_toolTipDialog`. The same
//     shape UI1 found in `Update`. Reproduced as a `volatile` read; dropping it
//     would make our body survive a pointer retail dies on.
//
//  2. **The `_dialogs` path pushes the NODE, not the InternalPtr**, 0x18012115f:
//     `MOV R8,RBX` where RBX is the list node. `*node` is the dialog, so the
//     eight bytes copied are the same either way — but the pointer handed to
//     the helper is not the caller's `InternalPtr`, and the `_addDialogs` path
//     three hundred bytes later DOES hand it the caller's (`MOV R8,R9`,
//     0x18012139f). Ghidra prints both as `local_res10`.
//
//  3. **A dead store inside the compaction**, 0x18012134b:
//         MOV [RCX],RBP        ; RBP is 0
//         MOV RAX,[RCX+8] ; MOV [RCX],RAX
//     The slot is zeroed and then immediately overwritten with its successor.
//     Both stores are reproduced; the predictor supersedes the first on range
//     overlap (UI1's `RangesOverlap` fix) and compares only the second.
//
//  4. **`TEST RCX,RCX` on `base + i*8`**, 0x180121346 — a null test on an
//     address that can only be null if the array base is exactly `-i*8`. Dead,
//     and kept.
//
//  5. **The tail is `Play(1)` inlined.** 0x180121167 reads `this+0xa0`, which
//     is `_sounds[1]` (the array starts at +0x98), then reproduces
//     `?Play@UserInterface@UI@@QEAAXH@Z` (0x1801223d0) instruction for
//     instruction — including `0.0 / _secondsPerTick`, which is NOT folded to
//     zero here for the reason userinterface.cpp gives at the real `Play`: were
//     that slot ever 0.0, retail computes NaN and `CVTTSD2SI` yields INT64_MIN.
//     Reached ONLY from the `_dialogs` path; the `_addDialogs` path returns
//     without it (0x1801213a7).
//
//  6. **`EDI` is 1 at the sound tail**, set at 0x18012111a, and it is what is
//     stored into the channel's `+0x08`. The `_addDialogs` path reuses EDI as
//     the array index and never reaches the tail, so there is no conflict.
//
//  7. **The vtable slot is `+0x28` = index 5 = `?OnClosed@DialogInstance@UI@@`**
//     (0x180115330), read out of `standalone/rt_data_mint_tables.inc`'s minted
//     copy of `??_7DialogInstance@UI@@6B@` rather than guessed from the name.
//     It is the mirror of `AddDialog`'s slot 4 `OnOpened`. It is NOT a refcount
//     release, so the dialog is still alive when this body reads `d+0x28`
//     afterwards.
//
//  8. **The two unlink blocks are the SAME code twice**, at 0x180121074 and
//     0x180121270, differing only in which register holds the dialog. Written
//     once here as `UnlinkFromParent`. Ghidra's C rotated the first one into a
//     `while` with the loop head below the return, which is why it reads as two
//     unrelated fragments.
//
// ---------------------------------------------------------------------------
// FIELD NAMES NEAR THIS CLASS
// ---------------------------------------------------------------------------
// `src/engine/ui/Dialog.h` names `DialogControllerConfigEntry`'s two
// `ExternalPtr` fields BACKWARDS and its own warning comment carries the error.
// Nothing in this file touches that struct, and every offset below is written
// as a raw byte offset with the retail address that proves it, deliberately —
// pin to addresses, never to field names, anywhere near this class.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "ui/ToolTipDialog.h"
#include "ui/ComboDialog.h"   // review S-035: ForgetClosingComboDialog
#include "audio/Audio.h"
// ENDGAME B.3.6 block E: element_common.h carries the file-scope `Module` /
// `ResolveAddr` declaration this file used to spell (the address door lives in
// standalone/, and pulling its header into a runtime/src file would invert the
// dependency the whole tree is arranged around), and the RULE B6-5 atomic
// door caches `rtuielem::AtomicExport` / `AtomicAddr`.
#include "element_common.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "collection/Collection.h"

namespace {

using UI::UserInterface;


// ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, no lazy cache.
inline const double* TimerSecondsPerTick() {
    return &::System::Timer::_secondsPerTick;
}

// ---- the callees ------------------------------------------------------------
// B3R RT-UI: called by name -- UserInterface::EndDragDrop, System::
// MemoryAllocator::MemFree (0x1800c7ad0) and the two Array bodies
// 0x180033690 (Array<UI::Event>::SetSize-fill) and 0x18007a0d0 (the generic
// 8-byte push), neither of which is exported.

// The live dialog list's node, and a dialog's child list's node: both the one
// `Collection::LinkedList` node shape {element, prev, next}, 0x18 bytes.
using DialogNode = Collection::LinkedList<UI::DialogInstancePtr>::Node;
using ChildNode  = Collection::LinkedList<Object::InternalPtr<UI::DialogInstance> >::Node;
static_assert(sizeof(DialogNode) == 0x18, "LinkedList node is value/prev/next");
static_assert(sizeof(ChildNode) == 0x18, "LinkedList node is value/prev/next");

// ---------------------------------------------------------------------------
// The shared unlink — 0x180121074..0x180121109 and 0x180121270..0x180121307
// ---------------------------------------------------------------------------
// Removes `dlg` from its parent's child list (`_children`, +0x38) and frees the
// node. Both call sites in retail are the same instructions with a different
// register holding the dialog, so they are one function here.
//
void UnlinkFromParent(UI::DialogInstance* dlg) {
    UI::DialogInstance* const par = dlg->_parent.Get();          // +0x28
    if (!par) return;                                   // 0x18012105e / 0x180121255

    auto& children = par->_children;                    // +0x38 head, +0x40 tail, +0x48 count
    ChildNode* const head = children._head;
    if (!head) return;                                  // 0x18012106e / 0x180121265

    for (ChildNode* n = head; n; n = n->next) {         // +0x10 is `next`
        if (n->element.Get() != dlg) continue;

        if (n == head)                    children._head = n->next;   // 0x180121088
        if (n == children._tail)          children._tail = n->prev;   // 0x180121095
        if (n->prev)                      n->prev->next = n->next;    // 0x1801210a3
        if (n->next)                      n->next->prev = n->prev;    // 0x1801210b4

        n->prev = nullptr;                                          // 0x1801210c7
        n->next = nullptr;                                          // 0x1801210cb
        --children._count;                                          // 0x1801210cf

        // Retail inlines EnterCriticalSection / MemoryAllocator::Free /
        // LeaveCriticalSection here (0x1801210d2..0x180121108). That sequence is
        // byte-for-byte the body of the exported static `MemFree`, which is why
        // calling it reproduces the lock discipline exactly with no windows.h in
        // a runtime/src file. Lane UI1 §1b decompiled and compared the two.
        System::MemoryAllocator::MemFree(n);
        return;
    }
}

// ---------------------------------------------------------------------------
// The shared "append to _removedDialogs unless it is already there"
// 0x180121115..0x180121165 and 0x180121361..0x1801213a5
// ---------------------------------------------------------------------------
// Note the loop: retail does NOT break on a hit. It walks the whole array
// setting a flag with `CMOVZ`, then tests once. A port that broke early would
// be faster and would read the same, which is precisely why it is written the
// long way here.
//
// `src` differs between the two call sites and is not an accident — see fact 2
// in the banner.
void PushRemovedUnlessPresent(UserInterface* self,
                              Object::InternalPtr<UI::DialogInstance>* dialog,
                              const void* src) {
    auto& removed = self->_removedDialogs;
    int32_t n = removed._size;
    if (n > 0) {
        UI::DialogInstancePtr* p = removed._data;
        void*  want  = dialog->Get();
        bool   found = false;
        for (int64_t c = n; c != 0; --c, ++p)
            if (*p == want) found = true;                   // CMOVZ, no break
        if (found) return;
    }

    rtcoll::RtColl_ArraySetSizeFill_18007a0d0(&removed,                 // DynArrayPush(_removedDialogs)
                                              removed._size + 1, src);
}

} // namespace

// ENDGAME B.3.6 block E: the body is the member. `dialog` is the ADDRESS of the
// caller's InternalPtr temporary (RULE B6-11; 0x180120f3d `MOV RDI,RDX` then
// `MOV RBX,[RDX]`).
void UI::UserInterface::CloseDialog(Object::InternalPtr<DialogInstance>* dialog) {
    UserInterface* const self = this;
    DialogInstance* const dlg = dialog->Get();                  // RBX at 0x180120f43

    // ---- Block A, 0x180120f46..0x180120f74 -------------------------------
    // Close every child dialog first. The loop's bound is the count the
    // recursive call decrements. `dlg->_ui` (+0x10) is the child's owning
    // UserInterface, which is not necessarily `self`.
    while (dlg->_children._count > 0) {                         // +0x48
        Object::InternalPtr<DialogInstance> tmp(                // the by-value InternalPtr
            dlg->_children._head->element);                     // +0x38, node[0]
        // B.4 LEFT: `DialogInstance::_ui` (+0x10) is `void*` (R-UI-a).
        dlg->_ui->CloseDialog(&tmp);
    }

    // ---- Block B, 0x180120f76..0x180120fb1 -------------------------------
    // If the dialog being closed OWNS the current tooltip, close the tooltip.
    ToolTipDialog* const tip = _toolTipDialog;
    if (tip && dialog->Get() == _toolTipOwner) {
        // 0x180120f8a  MOV RAX,[RCX+0x30] / CMP RAX,RAX / JNZ — see fact 1.
        // The compare is unreachable; the LOAD is not, and it faults on a bad
        // `_toolTipDialog` exactly as retail does. +0x30 is `_dataSource`.
        Object::Base* volatile deadLoad = tip->_dataSource;
        (void)deadLoad;

        Object::InternalPtr<DialogInstance> tmp(tip);
        CloseDialog(&tmp);                                        // 0x180120fa6
        _toolTipDialog = nullptr;                                 // tooltip arm
        _toolTipOwner  = nullptr;
    }

    // ---- Block C, 0x180120fb3..0x180120fd4 -------------------------------
    // Already queued for removal? Then there is nothing left to do.
    {
        int32_t n = _removedDialogs._size;
        if (n > 0) {
            DialogInstancePtr* p = _removedDialogs._data;
            void*  want = dialog->Get();
            for (int32_t i = 0; i < n; ++i, ++p)
                if (*p == want) return;                           // 0x180120fc6
        }
    }

    // NOT RETAIL (review S-035): a drop-down closed by any caller other than its own four
    // close sites leaves its combo's `_openDialog` naming it after CleanRemovedDialogs frees
    // it. Cleared here, on the first close, while the combo is alive (combodialog_close.cpp).
    ForgetClosingComboDialog(dlg);

    // ---- Block D, 0x180120fd6..0x180120ff1 -------------------------------
    // Find the dialog in the live `_dialogs` list (+0x00).
    DialogNode* node = _dialogs._head;
    while (node && node->element != dialog->Get()) node = node->next;

    if (node) {
        // ---- Block E, 0x180120ff8..0x180121051 ---------------------------
        // DialogInstance vtable slot 5 (+0x28) `OnClosed` — VIRTUAL, so an exe
        // dialog's hand-built slot 5 is what runs.
        node->element->OnClosed();

        // If this dialog owns the in-flight drag, queue a cancel event and end it.
        DialogInstance* drag = _dragDropDialog;
        if (node->element == drag) {
            // `_pad24` is NEVER written by retail — the record is a raw stack
            // frame there. Zeroing it here is UNOBSERVABLE: the grow helper at
            // 0x180033690 copies +0x00, +0x08, +0x10, +0x14, +0x18, +0x1c and
            // +0x20/+0x28 field by field, never +0x24.
            Event ev;
            ev.dialog   = drag;                                            // 0x180121011
            ev.element  = nullptr;                                         // 0x180121016
            ev.eventId  = UI::EventId::DragDropEnd;                        // 0x18012101b
            ev.arg14    = -1;                                              // 0x180121023
            ev.arg18    = 0;                                               // 0x18012102b } one QWORD
            ev.arg1C    = 0;                                               //             }
            ev.arg20    = static_cast<int32_t>(_dragDropId);               // 0x180121030
            ev._pad24   = 0;   // never written by retail, never copied
            ev.userData = _dragDropData;                                   // 0x180121034

            // Array<UI::Event>::SetSize (drag cancel, id 0xd)
            rtcoll::RtColl_ArraySetSizeFill_180033690(&_events, _events._size + 1, &ev);

            EndDragDrop();
        }

        // ---- Block F, 0x180121054..0x18012110b -----------------------------
        // Retail RE-LOADS the dialog out of the node here (0x180121054), after
        // OnClosed has run. Reproduced rather than reused from above.
        UnlinkFromParent(node->element);

        // ---- 0x18012110d..0x180121165 --------------------------------------
        UpdateEvents();

        // FACT 2: the source is the NODE (its element, +0x00), not the
        // caller's InternalPtr.
        PushRemovedUnlessPresent(self, dialog, &node->element);

        // ---- Block H, 0x180121167..0x1801211f1 -----------------------------
        // `Play(1)` inlined: `_sounds[1]` (+0xa0).
        Audio::SoundEffect* sound = _sounds[1];
        if (!sound) return;
        // B.4 LEFT: `_audioScene` (+0x90) stays `void*` (code/Game assigns it a
        // `void*`: gameupdateloop.cpp:1652, menudialogloopt.cpp:1158).
        Audio::Scene* const scene = _audioScene;
        if (!scene) return;
        Audio::Scene::Channel* const table = scene->channels._data;       // +0x10

        // NOT folded to zero — see fact 5 and the same note at the real `Play`.
        const double  secondsPerTick = *TimerSecondsPerTick();
        const int64_t ticks = static_cast<int64_t>(0.0 / secondsPerTick);

        int32_t index = 0;
        for (int64_t guard = 0; table[index].handleTag != 0; ) {          // +0x68
            ++index;
            if (++guard >= 0x100) return;        // full table: retail drops the sound
        }
        if (index < 0) return;                   // 0x1801211be, dead: index starts at 0

        Audio::Scene::Channel& slot = table[index];

        slot.soundEffectRef = sound;                                      // 0x1801211d0
        slot.soundType      = 1;                                          // 0x1801211d3
        slot.startTime._ticks = ticks;                                    // 0x1801211d6
        const uint32_t counter = static_cast<uint32_t>(scene->playGeneration) + 1;  // 0x1801211da
        scene->playGeneration = static_cast<int32_t>(counter);            // +0x08
        slot.handleTag = static_cast<uint32_t>(index * 2) | (counter << 9);  // 0x1801211ee
        return;
    }

    // ---- Block G, 0x1801211f6..0x180121227 -------------------------------
    // Not live yet — look for it in `_addDialogs`, the queue AddDialog pushes to.
    int32_t count = _addDialogs._size;
    if (count <= 0) return;

    DialogInstancePtr* const base = _addDialogs._data;
    void*    target = dialog->Get();
    int32_t  i      = 0;
    {
        DialogInstancePtr* p = base;
        while (*p != target) {
            ++i; ++p;
            if (i >= count) return;                                    // 0x180121220
        }
    }

    // ---- Block J, 0x180121229..0x1801213a5 -------------------------------
    int32_t k = i;                                                     // RBX, MOVSXD

    // DialogInstance vtable slot 5 (+0x28) `OnClosed` — VIRTUAL.
    base[k]->OnClosed();

    // The array base is RE-LOADED after the virtual (0x180121246): OnClosed can
    // reallocate the queue. Reproduced.
    UnlinkFromParent(_addDialogs._data[k]);

    UpdateEvents();                                                    // 0x180121314

    // 0x180121319 — note this compares against +0x40 and clears BOTH, where
    // block B compared against +0x38. Not a typo in either place.
    if (dialog->Get() == _toolTipDialog) {
        _toolTipDialog = nullptr;
        _toolTipOwner  = nullptr;
    }

    // ---- the compaction, 0x180121332..0x18012135c ------------------------
    if (i < _addDialogs._size) {
        do {
            ++i;
            if (i < _addDialogs._size) {
                DialogInstancePtr* const c = &_addDialogs._data[k];
                if (c != nullptr) {                                    // fact 4, dead, kept
                    // fact 3: the dead zero-store, then the real shift.
                    c[0] = nullptr;
                    c[0] = c[1];
                }
            }
            ++k;
        } while (i < _addDialogs._size);
    }
    --_addDialogs._size;                                               // 0x18012135e

    // FACT 2 again, the other way: here the source IS the caller's InternalPtr.
    PushRemovedUnlessPresent(self, dialog, dialog);
    // and the sound tail is NOT reached on this path — 0x1801213a2 falls into
    // the epilogue at 0x1801213a7.
}
