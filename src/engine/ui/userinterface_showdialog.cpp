// ============================================================================
// src/engine/ui/userinterface_showdialog.cpp — Wave 10B lane SHOW
// ?ShowDialog@UserInterface@UI@@QEAAHV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z
// retail 0x18011fd10, ordinal 4034
// ============================================================================
//
// THE RECORDED BLOCKER, AND WHAT WAS WRONG WITH IT
//
// WAVE10_UI_CENSUS.md §6 and WAVE10_LANE_UI1.md §4 record this row as blocked
// because "`BANISHED_MODAL_ANSWER` is NOT a route -- it is a BYPASS. Both
// harness call sites skip `fn_UI_ShowDialog` entirely."
//
// The MECHANISM in that sentence is exactly right and is re-derived below. The
// ENUMERATION is wrong: there are FOUR call sites, not two, and the two UI1 did
// not name are not bypassed at all --
//
//     src/game/Dialog/optionsdialog.cpp:1646   (the input-bind capture dialog)
//     src/game/Dialog/optionsdialog.cpp:1759   (the binding-conflict confirm)
//
// both call `fn_UI_ShowDialog` unconditionally. And the two that ARE bypassed
// are bypassed CONDITIONALLY: `moddialog.cpp:697` takes the bypass only when
// `BANISHED_MODAL_ANSWER` is set, and `saveloaddialog.cpp:938` only when it is
// set or the run is unattended. So the body was always reachable; what did not
// exist was a way to make the nested pump TERMINATE, which is what
// `src/diag/modal_driver.cpp` now provides.
//
// ---------------------------------------------------------------------------
// WHAT THE BODY DOES (shipped bytes; see modal_driver.cpp for the full trace)
// ---------------------------------------------------------------------------
// Allocate + construct a SECOND UserInterface, inherit eight qwords of the
// caller's presentation state into it, hand the dialog to it, register a
// draw/update callback pair with `Engine::MainLoop`, spin `MainLoop::Frame`
// until the dialog leaves the nested UI's list, un-register both callbacks,
// destroy and free the nested UI, and return the dialog's result dword.
//
// FACTS THE DECOMPILED C DROPPED OR MISLEADS ON
// (reference_ghidra_c_output_vs_disassembly.md):
//
//  * **`dialog->_flags |= 0x20` is NOT the modal bit.** Ghidra shows the OR and
//    nothing more. `?IsModal@DialogInstance@UI@@QEBA_NXZ` (0x180114170) reads
//    `(flags >> 1) & 1`; the bit ShowDialog sets is read by
//    `?DeleteOnClose@DialogInstance@UI@@QEBA_NXZ` (0x180114110) as
//    `!((flags >> 5) & 1)`. Setting it makes DeleteOnClose FALSE -- the dialog
//    survives its own close so the caller can read the result out of it. Every
//    call site depends on that: optionsdialog.cpp reads dialog+0xC0/+0xC4
//    AFTER ShowDialog returns and only then DeletePtr's.
//
//  * **The return is read from the InternalPtr a SECOND time.** 0x1801202e2 is
//    `MOV RAX,[RDI]; MOV EAX,[RAX+0x24]` where RDI is the reloaded argument --
//    it is NOT cached from the top of the function. If the dialog pointer in
//    the caller's InternalPtr were rewritten during the modal, retail would read
//    the new one. Reproduced faithfully (`*(int*)(*dialogPtr + 0x24)`).
//
//  * **+0x24 is the RESULT, not `_flags2`.** `src/engine/ui/DialogInstance.h`
//    currently calls it "_flags2 ... no ported body reads it". It is the field
//    `?GetResult@DialogInstance@UI@@QEBAHXZ` (0x1800455c0, two instructions:
//    `MOV EAX,[RCX+0x24]; RET`) returns and `?SetDialogResult@` (0x1800455d0,
//    `MOV [RCX+0x24],EDX; RET`) writes, and it is what this function returns.
//
//  * **The nested-loop test reloads the dialog pointer every iteration.**
//    0x180120150 `MOV RCX,[RDI]` sits INSIDE the loop, above the list walk.
//
//  * **The loop has TWO exits.** The list-walk falling off the end (dialog gone)
//    AND `MainLoop::Frame` returning true (a WM_QUIT / Application::Quit while
//    the modal is up). Ghidra's `while (!bVar3)` renders this correctly but it
//    is easy to lose: the second exit is why a modal does not wedge a quit.
//
//  * **The two callback appends are the SAME inlined grow-and-compact routine
//    twice**, once on (+0x80,+0x88,+0x8c) and once on (+0x90,+0x98,+0x9c), with
//    the `*3` overallocation policy applied only when growing by exactly one
//    from a non-empty array. Factored here as AppendCallback / RemoveCallback;
//    the arithmetic is transcribed, not re-derived.
//
//  * **`MemAlloc(0x128, 4)`** -- alignment FOUR, not 8. 0x18011fd59 is
//    `MOV R8D,0x4`. It pins sizeof(UI::UserInterface) at 0x128.
//
// ---------------------------------------------------------------------------
// WHAT IS AND IS NOT VERIFIED -- read this before reading the switch as a claim
// ---------------------------------------------------------------------------
// A PREDICTIVE oracle over this body is worth almost nothing, and UI1 said so
// correctly: everything it writes other than one flag bit is an address the
// allocator chose. This lane therefore uses the TWIN-SUBJECT DIFFERENTIAL in
// `src/diag/modal_driver.cpp` instead -- two identically-built dialogs, one
// shown through the shipped DLL body and one through ours, comparing the return
// value, the flag bit, and MainLoop's two callback counts back at their
// pre-call values. See docs/history/runtime_port/WAVE10B_LANE_SHOW.md for the numbers
// and for the list of effects that instrument does NOT cover.
// ============================================================================

#include <cstdint>
#include <cstdio>


#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "engine/MainLoop.h"   // ENDGAME B.3.7 callers (K5): Engine::MainLoop::Frame
#include "system/Timer.h"      // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "system/Value.h"      // System::Time
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

// ENDGAME B.3.6 block E: the three UserInterface doors this body used to reach
// by name are members or doors of the same class now. The ctor and the dtor
// STAY doors (B4b-2) and are called as the doors; the two static callbacks go
// into MainLoop's arrays as the class's own static members (B.4 wave X: their
// (context, userData) shape IS MainLoop's PostDrawFunc / PostUpdateFunc).
extern "C" void  RtUi_UserInterface_Dtor(UI::UserInterface* self);

namespace {

// --- Shipped addresses of the callees, for the reader ------------------------
// B3R RT-UI: every callee is called by name; the addresses are listed
// because a Ghidra address is what a future reader will have in hand.
//   0x180120de0  ??0UserInterface@UI@@QEAA@XZ
//   0x180120b90  ??1UserInterface@UI@@QEAA@XZ
//   0x1801213c0  ?Update@UserInterface@UI@@QEAAHAEBVTime@System@@@Z
//   0x180120310  ?UpdateStandAloneUI@UserInterface@UI@@CAXAEBVTime@System@@PEAX@Z
//   0x1801203f0  ?DrawStandAloneUI@UserInterface@UI@@CAXAEAVContext@Video@@PEAX@Z
//   0x1800dd100  ?Frame@MainLoop@Engine@@QEAA_N_N@Z
//   0x1801fe750  ?_singleton@MainLoop@Engine@@1PEAV12@EA          (DATA)
//   0x1801fe728  ?_secondsPerTick@Timer@System@@0NA               (DATA)
//   0x18007a0d0  Array<InternalPtr<DialogInstance>>::SetSize (not exported:
//                rtcoll::RtColl_ArraySetSizeFill_18007a0d0)
//   0x1800c7a50 / 0x1800c7ad0  MemoryAllocator::MemAlloc / MemFree

// The one retail sizeof the whole class rests on: `MOV EDX,0x128` before the
// nested UI's Allocate (0x18011fd10).
static_assert(sizeof(UI::UserInterface) == 0x128, "the nested UI's Allocate size");

// MainLoop's `_postDraw` (+0x80) / `_postUpdate` (+0x90) element.
// One element type per array (B.4 wave X): `F` is the array's function type.
template <class F> using Entry = RtEngine::MainLoopCallback<F>;
template <class F> using CallbackArray = Collection::Array<RtEngine::MainLoopCallback<F>>;

// Retail inlines `EnterCriticalSection(alloc+0x640); Allocate(...);
// LeaveCriticalSection(...)` six times in this body;
// `System::MemoryAllocator::MemAlloc` (0x1800c7a50) and `MemFree` (0x1800c7ad0)
// ARE that sequence -- lane UI1 decompiled and compared them.
void MemFreeS(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, no lazy cache.
const double* SecondsPerTick(){ return &::System::Timer::_secondsPerTick; }

// Retail's inlined callback-array append, transcribed.
// 0x18011fe7d..0x18011ffd6 (draw) and 0x18011ffe7..0x18012013e (update) are the
// same code on different offsets.
//
//   newCount = count + 1
//   if (newCount == count) return;                 // dead in practice; kept
//   old = arr
//   count32 = newCount
//   if (newCount > cap) {
//       cap = newCount
//       if (newCount == oldCount + 1 && oldCount != 0) cap = newCount * 3
//       arr = MemAlloc(cap * 16, 8)
//   }
//   // copy: if the buffer did NOT move, only the tail is written; if it DID
//   // move, everything from index 0 is written, sourced from `old` below
//   // oldCount and from `entry` at or above it.
//   start = (old == arr) ? oldCount : 0
//   for (i = start; i < newCount; ++i)
//       arr[i] = (i < oldCount) ? old[i] : entry;
//   if (old != arr && old) MemFree(old)
template <class F>
void AppendCallback(CallbackArray<F>& arr, const Entry<F>& entry) {
    int oldCount = arr._size;
    int newCount = oldCount + 1;
    if (newCount == oldCount) return;

    Entry<F>* old = arr._data;
    arr._size = newCount;

    if (newCount > arr._capacity) {
        arr._capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0)
            arr._capacity = newCount * 3;
        int cap = arr._capacity;
        arr._data = static_cast<Entry<F>*>(System::MemoryAllocator::MemAlloc((uint64_t)cap << 4, 8));
    }

    Entry<F>* cur = arr._data;
    int i = (old == cur) ? oldCount : 0;
    for (; i < newCount; ++i) {
        Entry<F>* dst = cur ? (cur + i) : nullptr;
        if (!dst) continue;                       // retail's `if (puVar17 != 0)`
        const Entry<F>* src = (i < oldCount) ? (old + i) : &entry;
        *dst = *src;
    }

    if (old != cur && old) MemFreeS(old);
}

// Retail's inlined removal-and-compaction, 0x180120179.. and 0x180120210..
// Scans for every (fn, ctx) match and shifts the tail down over it.
template <class F>
void RemoveCallback(CallbackArray<F>& arr, F fn, void* ctx) {
    int i = 0;
    int count = arr._size;
    if (count <= 0) return;
    do {
        Entry<F>* a0 = arr._data;
        if (a0 && a0[i]._func == fn && a0[i]._context == ctx) {
            int j = i;
            while (j + 1 < arr._size) {
                Entry<F>* a = arr._data;
                if (a) a[j] = a[j + 1];
                ++j;
            }
            arr._size -= 1;
            --i;
        }
        ++i;
        count = arr._size;
    } while (i < count);
}

// Target of defect 1 (see the negative-control block below). Never registered
// in any shipping configuration.
// One per array: each has that array's function type.
void DefectNoop(Video::Context&, void*) {}
void DefectNoop(const System::Time&, void*) {}

}  // namespace

// ---------------------------------------------------------------------------
// NEGATIVE CONTROL FOR THE TWIN-SUBJECT DIFFERENTIAL
// ---------------------------------------------------------------------------
// `src/diag/modal_driver.cpp` sets this immediately before the OURS cell and
// clears it after, so each round injects ONE defect aimed at ONE screen of the
// comparison. Scoring the screens separately is deliberate: a control scored on
// the UNION of several screens reads 3/3 while hiding that one screen caught
// nothing (reference_union_control_hides_a_blind_screen.md).
//
//   1  append an extra BENIGN callback pair and never remove it
//        -> must be caught by drawCount / updCount
//        (benign on purpose: leaking one of OUR OWN entries would leave retail's
//         Frame calling UpdateStandAloneUI on a freed nested UI every tick)
//   2  return the dword at +0x28 instead of the result at +0x24
//        -> must be caught by `return`
//   3  additionally set flags bit 0x80000000 (nothing reads it) alongside 0x20
//        -> must be caught by `flags`
//
// Zero in every shipping configuration; nothing but the self-test writes it.
extern "C" { int g_RtShowDialogDefect = 0; }

// ENDGAME B.3.6 block E: the body is the member. `dialogArg` is the ADDRESS of
// the caller's InternalPtr (RULE B6-11: 0x18011fd32 `MOV R14,RDX`, then
// `MOV RAX,[R14]`).
int32_t UI::UserInterface::ShowDialog(Object::InternalPtr<DialogInstance>* dialogArg) {
    // 1. Allocate + construct the nested UserInterface. Alignment 4 (MOV R8D,4).
    //    The ctor is a DOOR (B4b-2), called as one.
    UserInterface* nested = static_cast<UserInterface*>(
        System::MemoryAllocator::MemAlloc(sizeof(UserInterface), 4));
    if (nested) {
        // retail takes the RETURN, not the argument
        nested = RtUi_UserInterface_Ctor(nested);
    }

    // 2. Inherit +0x90..+0xc8 (`_audioScene` and the seven `_sounds`). Retail
    //    does this with EIGHT unconditional qword moves -- including on the path
    //    where the allocation FAILED and `nested` is null. That is an unguarded
    //    null store in the shipped code and it stays unguarded here (CLAUDE.md:
    //    don't "fix" a faithful crash).
    nested->_audioScene = _audioScene;
    for (int i = 0; i < 7; ++i) {
        nested->_sounds[i] = _sounds[i];
    }

    // FIX SEAM (fix.ui_scale_layout, NOT RETAIL; docs/retail_bugs.md section 5): retail's nested
    // UserInterface keeps its ctor's `_uiScale = 1.0f`, so every modal shown through here (the
    // mods' "restart" notice, the save / load warnings, the Options confirmations) is laid out
    // and drawn at 100 % whatever the Options UI scale. Fixed: the modal takes the scale of the
    // UI it is shown from. Off / null = retail's 1.0.
    if (_extFixScaleLayout != nullptr && *_extFixScaleLayout)
        nested->_uiScale = _uiScale;

    // 3. Hand the dialog to the nested UI and clear its DeleteOnClose.
    DialogInstance* const dlg = dialogArg->Get();
    dlg->_ui = nested;                                                   // +0x10
    dlg->_flags |= kDialogFlagNoDelete;                                  // +0x20
    if (g_RtShowDialogDefect == 3)
        dlg->_flags |= 0x80000000u;                                      // control

    // 4. Dialog vtable slot 4 (+0x20). Retail re-reads the InternalPtr, stores
    //    the dialog into a stack slot, and calls through it with RCX = dialog.
    //    ENDGAME B.3.6: slot 4 is `DialogInstance::OnOpened` — a VIRTUAL call,
    //    dispatched through the dialog's own (exe hand-built) table.
    DialogInstance* const opened = dialogArg->Get();
    opened->OnOpened();
    const DialogInstancePtr dialog = opened;

    // 5. Push the dialog into nested->_addDialogs. `&dialog` is the by-value
    //    InternalPtr the stack slot from step 4 holds -- retail passes that same
    //    slot (LEA R8,[RSP+0x80]).
    rtcoll::RtColl_ArraySetSizeFill_18007a0d0(&nested->_addDialogs,
                                              nested->_addDialogs._size + 1, &dialog);

    // 6. Update(nested, Time(0)). Retail computes `(int64)(0.0 / _secondsPerTick)`
    //    -- a DIVSD by a loaded double, not a constant zero. 0.0/x is 0 for every
    //    finite non-zero x, but it is NaN->0 (CVTTSD2SI of NaN yields the
    //    integer indefinite 0x8000000000000000) if _secondsPerTick is 0.
    //    Transcribed as the division so that degenerate case behaves alike.
    {
        double spt = *SecondsPerTick();
        System::Time t;
        t._ticks = (int64_t)(0.0 / spt);
        nested->Update(t);
    }

    // 7 + 8. Register the draw and update callbacks, in that order. The entries
    //    are the registered DOORS (see the includes).
    const ::Engine::MainLoop::PostDrawFunc   drawFn = &UI::UserInterface::DrawStandAloneUI;
    const ::Engine::MainLoop::PostUpdateFunc updFn  = &UI::UserInterface::UpdateStandAloneUI;

    AppendCallback(::Engine::MainLoop::_singleton->_postDraw,
                   ::Engine::MainLoop::PostDrawCallback{ drawFn, nested });
    // Retail re-reads MainLoop::_singleton before the second append. Kept.
    AppendCallback(::Engine::MainLoop::_singleton->_postUpdate,
                   ::Engine::MainLoop::PostUpdateCallback{ updFn, nested });

    // 9. The nested pump. Two exits: the dialog leaving nested->_dialogs, and
    //    Frame() returning true (quit). The dialog pointer is reloaded from the
    //    caller's InternalPtr on every iteration.
    {
        for (;;) {
            void* want = dialogArg->Get();
            bool present = false;
            for (auto* node = nested->_dialogs._head; node; node = node->next) {
                if (node->element == want) { present = true; break; }
            }
            if (!present) break;
            if (::Engine::MainLoop::_singleton->Frame(true)) break;
        }
    }

    // 10. Un-register both callbacks (draw first, then update -- retail's order).
    RemoveCallback(::Engine::MainLoop::_singleton->_postDraw, drawFn, nested);
    RemoveCallback(::Engine::MainLoop::_singleton->_postUpdate, updFn, nested);

    // 11. Destroy and free the nested UI.
    RtUi_UserInterface_Dtor(nested);                             // the dtor DOOR (B4b-2)
    MemFreeS(nested);

    if (g_RtShowDialogDefect == 1) {                                     // control
        // A no-op, NOT drawFn/updFn: a leaked real entry would have retail's
        // Frame calling UpdateStandAloneUI on a freed nested UI every tick, and
        // a control that crashes the subject measures nothing.
        AppendCallback(::Engine::MainLoop::_singleton->_postDraw,
                       ::Engine::MainLoop::PostDrawCallback{ &DefectNoop, nullptr });
        AppendCallback(::Engine::MainLoop::_singleton->_postUpdate,
                       ::Engine::MainLoop::PostUpdateCallback{ &DefectNoop, nullptr });
    }

    // 12. The result (`_result`, +0x24), re-read through the caller's InternalPtr.
    // The defect-2 control returns the low dword of `_parent` (+0x28): the
    // pointer's integer value, truncated.
    if (g_RtShowDialogDefect == 2)                                       // control
        return static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(dialogArg->Get()->_parent.Get()));   // the defect-2 control's value is the parent pointer's low dword as an integer  // CAST-OK: ptr_int
    return dialogArg->Get()->_result;
}
