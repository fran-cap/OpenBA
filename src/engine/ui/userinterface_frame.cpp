// ============================================================================
// src/engine/ui/userinterface_frame.cpp — Wave 10 lane UI1
// ?Update@UserInterface@UI@@QEAAHAEBVTime@System@@@Z   retail 0x1801213c0
// ============================================================================
//
// WHY THIS FILE EXISTS AND WHY IT IS NOT `userinterface.cpp`
//
// `userinterface.cpp` is Wave 7 lane C's file and every body in it is a LEAF
// verified by the REPLAY oracle (snapshot 0x128 bytes, run ours live, run
// theirs on the copy, memcmp). Lane C explicitly refused `Update` on that
// instrument, and was right to:
//
//     "`Update` drains `_addDialogs` into the list (allocating a node each),
//      frees the array, advances the fade, calls `DialogInstance::Update` on
//      every dialog, consumes input from `Input::Device::_singleton`, and ends
//      drag-drop; it is not replayable and not even idempotent."
//
// Every clause of that is true and none of it has expired. A replay of `Update`
// would drain the SAME `_addDialogs._data` pointer twice, MemFree it twice, and
// pump a second round of `DialogInstance::Update` into live dialogs.
//
// WHAT DID EXPIRE — the three blockers this lane re-tested from scratch:
//
//  1. "SPLIT BRAIN, needs a detour."  EXPIRED. `standalone/runtime_detour.{h,cpp}`
//     is a mature relocating installer with live `[port]` detours, and
//     `standalone/ui_detour.cpp` is already a UI subject on it
//     (`TabElement::UpdateContainers`, 0x1800d9710). Building the mechanism is
//     not this lane's work; using it is. See `RtUiUserInterfaceUpdateInstallDetour`.
//
//  2. "`EnterCriticalSection` cannot appear in a non-`_win` file, and the
//     allocator is Wave 2's NO-GO."  EXPIRED, and it was a CONFLATION.
//     Retail's inline `EnterCriticalSection(alloc+0x640); Allocate(...);
//     LeaveCriticalSection(...)` sequence at 0x180121429..0x18012144b is
//     BYTE-FOR-BYTE the body of the exported static
//     `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) — decompiled
//     and compared, not assumed. Calling that export reproduces the lock
//     discipline exactly with no `windows.h` in this file. The Wave 2 NO-GO is a
//     verdict on REPLACING the allocator arena (`standalone/rt_alloc_swap.cpp`),
//     not on a ported body calling it; ~30 ported files already call `MemAlloc`.
//
//  3. "No oracle."  The body writes the named fields directly (B.4 retired
//     the `rt_sink.h` pass-through it was written against).
//
// ---------------------------------------------------------------------------
// FACTS THE DECOMPILED C DROPPED (read off the disassembly, per
// reference_ghidra_c_output_vs_disassembly.md)
// ---------------------------------------------------------------------------
//
//  * **`Update` RETURNS THE MOUSE CURSOR INDEX.** Ghidra prints the prototype as
//    `void`. The disassembly does not: the last call on the main path is
//    `CALL 0x18011fa40` (`GetMouseCursorIndex`) at 0x18012185e and the epilogue
//    is reached by a bare `JMP` that never touches EAX, so its return value IS
//    `Update`'s. The fullscreen-fade early-out at 0x180121865 is
//    `XOR EAX,EAX` — **return 0**. `docs/history/runtime_port/export_name_verdicts.md`
//    already corrected the declaration from `X` (void) to `H` (int); this is
//    what the int MEANS, and it is load-bearing because the caller feeds it
//    straight to the cursor.
//
//  * **`CloseDialog(this)` is `CloseDialog(this, &tmp)`.** Ghidra dropped the
//    second argument entirely. 0x1801214b2 stores `_toolTipDialog` into a stack
//    slot and 0x1801214b6 passes its ADDRESS — the by-value `InternalPtr` ABI
//    (`WAVE7.md`: a non-trivial copy ctor means MSVC x64 passes by address in
//    both spellings).
//
//  * **A dead compare with a LIVE load.** 0x1801214a9 is
//    `MOV RAX,[RDX+0x30]; CMP RAX,RAX; JNZ` — `CMP RAX,RAX` is never unequal, so
//    the branch is unreachable, but the LOAD still happens and still faults on a
//    bad `_toolTipDialog`. Ghidra printed the whole thing as `(lVar != lVar)` and
//    then folded it away. Reproduced below as a `volatile` read: dropping it
//    would make our body survive a pointer retail dies on, which is a behaviour
//    change in the direction this project explicitly refuses.
//
//  * **`node->prev = tail` is UNGUARDED.** 0x18012147e writes `[RDI+8]` where RDI
//    is the freshly allocated node, on the path where the allocation may have
//    returned null (0x180121456 sets RDI = 0 and jumps INTO the same tail
//    fixup). Retail dereferences null here if `MemAlloc` ever fails. Kept
//    faithful — CLAUDE.md, "Don't 'fix' a faithful crash with a null check".
//
//  * **The fade's zero-out is one QWORD, not two DWORDs.** 0x1801215d7 is
//    `MOV qword[RSI+0xf0], 0`, which clears `_fadeVelocity` AND `_fadeAmount`
//    together. The `>= 1.0f` arm instead writes two separate DWORDs
//    (`_fadeAmount = 1.0f`, `_fadeVelocity = 0`). Different widths on the two
//    arms; a typed-field port would silently unify them.
//
//  * **`Update`'s float constant is 4.0f.** `DAT_1801616e4` read out of the
//    shipped `.rdata`: `00 00 80 40`. The fade step is
//    `amount += (float)(ticks * _secondsPerTick) * velocity * 4.0f`.
//
// ---------------------------------------------------------------------------
// THE ORACLE'S COVERAGE BOUNDARY — stated before the code, not after
// ---------------------------------------------------------------------------
// In predict mode our body makes NO call (every one goes through
// `RtSink::Call`, which returns false). So anything a suppressed callee writes
// is not ours to predict, and anything whose VALUE depends on a suppressed
// callee is not ours to predict either. Both are `Exclude()`d:
//
//   PREDICTED and COMPARED (the whole point):
//     +0x028/+0x030  `_addDialogs` cleared after the drain      (changes on any
//                    frame a dialog was added — a real `changed=` contributor)
//     +0x0fc..+0x10c `_clipRegion`  (fully determined by Video::Device + scale)
//     +0x0f0/+0x0f4  the fade advance (fully determined by time + pre-state —
//                    the strongest `changed=` contributor: it moves EVERY frame
//                    of every fade)
//     +0x058/+0x05c  `_mouseDown{X,Y}` (fully determined by Input::Device)
//     +0x0d9         the entry `_mouseCaptured = 0` store (an INTERMEDIATE; the
//                    exit store is excluded, and the instrument counts this as
//                    `superseded`, never as a claim about the post-state)
//
//   EXCLUDED, with the specific reason each:
//     +0x000..+0x018 `_dialogs`      — `CleanRemovedDialogs` (x3) unlinks here,
//                                      and the drain's node POINTERS are
//                                      addresses `MemAlloc` chose, which no
//                                      predictor can name.
//     +0x018..+0x028 `_removedDialogs` — cleared by `CleanRemovedDialogs`.
//     +0x038/+0x040  the tooltip pair  — written by the suppressed `CloseDialog`
//                                      arm.
//     +0x048..+0x058 `_events`         — `UpdateEvents`, `CleanRemovedDialogs`
//                                      and the two suppressed array-grow pushes.
//     +0x060..+0x089 the drag-drop block — the branch that clears it is gated on
//                                      a byte `DialogInstance::Update` writes
//                                      into our stack-local `UpdateState`. With
//                                      that call suppressed the branch condition
//                                      is UNKNOWABLE, so the body ABSTAINS
//                                      rather than guessing — excluded only when
//                                      the dialog list is non-empty, and fully
//                                      predicted when it is empty and the
//                                      condition is therefore determined.
//     +0x0d9         the EXIT store    — same cause: it is `UpdateState+0x20`.
//
// The return value is NOT compared in predict mode and must not be: our body
// runs on the PRE-drain list and retail's runs on the POST-drain list, so a
// difference would be correct rather than a defect. The detour returns RETAIL's
// value in predict mode, which is also what keeps the frame correct.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/ToolTipDialog.h"
#include "system/Value.h"
// ENDGAME B.3.6 block E: element_common.h carries the same file-scope
// `Module` / `ResolveAddr` declaration userinterface.cpp and dialog.cpp use
// (the address door lives in standalone/), and the RULE B6-5 atomic door
// caches.
#include "element_common.h"
#include "video/Device.h"    // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "input/Device.h"    // ENDGAME B.3.9 SS1: Input::Device::_singleton
#include "input/Input.h"     // ENDGAME B.3.9 SS1: Input::Configuration::_singleton
#include "system/Timer.h"    // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "collection/Collection.h"
#include "system/MemoryAllocator.h"


namespace {

using UI::UserInterface;

// ---- data slots -------------------------------------------------------------
// ENDGAME B.3.9 SS1 (B39_RULES D7): the class statics, no lazy AtomicExport
// cache (B9-10).
inline const double* TimerSecondsPerTick() {
    return &::System::Timer::_secondsPerTick;
}

// ---- the callees ------------------------------------------------------------
// B3R RT-UI: all called by name -- MemoryAllocator::MemAlloc / MemFree
// (0x1800c7a50 / 0x1800c7ad0) and 0x180033690,
// `Collection::Array<UI::Event>::SetSize(int, const Event*)`, the grow-and-fill
// helper both event pushes call (not exported:
// rtcoll::RtColl_ArraySetSizeFill_180033690).

// ---------------------------------------------------------------------------
// UI::Element::UpdateState — 0x30 bytes, recovered from THIS function's frame
// ---------------------------------------------------------------------------
// B4 Y RT-UI: the frame record IS `UI::Element::UpdateState` (Element.h). The
// frame's first stores (0x180121635..0x18012164d: qword 0 at base+0x01, byte
// +0x09, qword +0x0c, qword -1 at +0x18, byte +0x20, base = RBP-0x51) are the
// inlined ??0UpdateState (0x180115600) with the +0x00 byte left to the SETNZ
// at 0x18012165d; +0x0a/+0x0b and +0x21..+0x27 are written by neither. The
// tree's value-initialised file-local copy of the struct (it zeroed all 0x30
// bytes) is deleted; `DialogInstance::Update` takes the state by reference
// and WRITES BACK into it -- at least `+0x07` (the mouse-down byte reloaded
// at 0x180121781) and `+0x20` (stored into `_mouseCaptured` at 0x18012184e).

// A list node of `_dialogs`: element, prev, next. Proven by the drain's own
// stores (tail->[+0x10] = node, node->[+0x08] = tail).
using Node = Collection::LinkedList<UI::DialogInstancePtr>::Node;
static_assert(sizeof(Node) == 0x18, "MemAlloc(0x18, 8) in the drain");

} // namespace

// ENDGAME B.3.6 block E: the body is the member (the main-loop hot path).
const bool* UI::UserInterface::_extFixScaleLayout = nullptr;   // fix.ui_scale_layout seam
void (*UI::UserInterface::_extDialogPlace)(int32_t, const UI::DialogInstance*, int32_t, int32_t,
                                           int32_t*) = nullptr;   // ext.phone_menu seam
float (*UI::UserInterface::_extDialogScale)(const UI::DialogInstance*) = nullptr;   // ext.touch_mode seam
bool (*UI::UserInterface::_extHoverWhileDown)(const UI::DialogInstance*) = nullptr;   // ext.phone_menu seam (P11b)
// ext.phone_menu seam (P9)
bool (*UI::UserInterface::_extBackdropOver)(const UI::DialogInstance*, int32_t, int32_t, int32_t*, int32_t*,
                                            UI::SpriteSheet**, int32_t*) = nullptr;

int32_t UI::UserInterface::Update(const System::Time& timeRef) {
    // 0x1801213ef — _mouseCaptured = 0, BEFORE anything else.
    _mouseCaptured = 0;

    // 0x1801213f6
    CleanRemovedDialogs();                                      // #1

    // ---- drain _addDialogs back-to-front into the list ---------------------
    // Retail walks i = _size-1 down to 0 (0x1801213fb: MOVSXD R14, size-1, and
    // JS past the whole loop when negative — a genuine sign-extension, which
    // Ghidra prints as a zero extension).
    const int32_t addCount = _addDialogs._size;
    for (int64_t i = static_cast<int64_t>(addCount) - 1; i >= 0; --i) {
        DialogInstancePtr* const addData = _addDialogs._data;

        Node* node = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x18, 8));
        if (node) {
            node->element = nullptr;        // retail zeroes then stores
            node->element = addData[i];
            node->prev    = nullptr;
            node->next    = nullptr;
        }
        // else: RDI := R13 (0) at 0x180121456 — and the tail fixup below
        // still runs. That unguarded `node->prev = tail` is retail's.

        Node* const tail = _dialogs._tail;
        if (tail) {
            tail->next = node;                                  // _dialogs._tail->next = node
            node->prev = _dialogs._tail;                        // node->prev = _dialogs._tail
        } else {
            _dialogs._head = node;
        }
        _dialogs._tail = node;
        ++_dialogs._count;

        // ---- the tooltip teardown arm ------------------------------------
        ToolTipDialog* const toolTip = _toolTipDialog;
        if (toolTip) {
            DialogInstancePtr* const reload = _addDialogs._data;
            const DialogInstance* const dlg = static_cast<const DialogInstance*>(reload[i]);
            if ((dlg->_flags >> 4) & 1u) {
                // 0x1801214a9: MOV RAX,[RDX+0x30]; CMP RAX,RAX; JNZ ...
                // The compare can never be unequal, so the branch is dead — but
                // the LOAD (+0x30, `_dataSource`) is real and faults on a bad
                // pointer exactly as retail does. Kept `volatile` so no compiler
                // deletes it.
                Object::Base* volatile deadLoad = toolTip->_dataSource;
                (void)deadLoad;

                // the by-value InternalPtr copy, passed by address (B6-11)
                Object::InternalPtr<DialogInstance> tmp(toolTip);
                CloseDialog(&tmp);
                _toolTipDialog = nullptr;
                _toolTipOwner  = nullptr;
            }
        }
    }

    // 0x1801214d3 — the "a dialog opened" sound, gated on the PRE-drain count.
    if (addCount > 0) {
        Play(0);
    }

    // 0x1801214e3 — free the array and clear BOTH size and capacity with one
    // QWORD store (0x180121525: MOV qword[RSI+0x30], 0).
    if (void* const addData = _addDialogs._data) {
        System::MemoryAllocator::MemFree(addData);
    }
    _addDialogs._data     = nullptr;
    _addDialogs._size     = 0;                                  // } one QWORD
    _addDialogs._capacity = 0;                                  // }

    // ---- clip region ------------------------------------------------------
    const float scale = _uiScale;
    const ::Video::Device* const vdev = ::Video::Device::_singleton;
    const int32_t w = static_cast<int32_t>(
        static_cast<float>(vdev->backBufferWidth) * scale);    // +0x04
    const int32_t h = static_cast<int32_t>(
        static_cast<float>(vdev->backBufferHeight) * scale);   // +0x08
    _clipRegion.left   = 0;                                     // } one QWORD
    _clipRegion.top    = 0;                                     // }
    _clipRegion.right  = w - 1;
    _clipRegion.bottom = h - 1;

    // ---- fullscreen fade --------------------------------------------------
    const float vel = _fadeVelocity;
    if (vel != 0.0f) {
        const double secs = static_cast<double>(timeRef._ticks)
                          * *TimerSecondsPerTick();
        const float amount =
            static_cast<float>(secs) * vel * 4.0f
            + _fadeAmount;
        _fadeAmount = amount;

        if (!(amount < 1.0f)) {
            _fadeAmount   = 1.0f;
            _fadeVelocity = 0.0f;
        } else if (!(0.0f < amount)) {
            // ONE qword — clears velocity AND amount together (0x1801215d7).
            _fadeVelocity = 0.0f;
            _fadeAmount   = 0.0f;
        }
    }

    // 0x1801215de / 0x1801215e9
    UpdateEvents();
    CleanRemovedDialogs();                                      // #2

    // ---- layout every dialog, head -> tail --------------------------------
    // `DialogInstance::ComputeLayout` (0x180113d90) — non-virtual member of the
    // real class (block B).
    //
    // FIX SEAM (fix.ui_scale_layout, NOT RETAIL): retail lays out with the
    // `w, h` read above, BEFORE UpdateEvents. When a handler in UpdateEvents
    // wrote `_uiScale` (the Options dialog's Apply / Ok), every dialog is laid
    // out in the OLD unit-space screen and drawn under the NEW scale: one frame
    // with each dialog displaced (docs/retail_bugs.md section 5). Fixed: the
    // layout size follows the scale the events left. Off / null = retail's line.
    int32_t layoutW = w;
    int32_t layoutH = h;
    if (_extFixScaleLayout != nullptr && *_extFixScaleLayout && _uiScale != scale) {
        layoutW = static_cast<int32_t>(static_cast<float>(vdev->backBufferWidth) * _uiScale);
        layoutH = static_cast<int32_t>(static_cast<float>(vdev->backBufferHeight) * _uiScale);
    }
    if (_extDialogPlace == nullptr && _extDialogScale == nullptr) {
        for (Node* n = _dialogs._head; n; n = n->next) {
            n->element->ComputeLayout(0, 0, layoutW, layoutH);
        }
    } else {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): ext.phone_menu may lay a
        // dialog out inside a safe area; retail's rectangle otherwise.
        // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md): ext.touch_mode lays a dialog with its
        // own scale k out in its own units (the placed rectangle divided by k).
        for (Node* n = _dialogs._head; n; n = n->next) {
            int32_t rect[4] = {0, 0, layoutW, layoutH};
            if (_extDialogPlace != nullptr)
                _extDialogPlace(kExtPlaceLayout, n->element, layoutW, layoutH, rect);
            const float k = _extDialogScale != nullptr ? _extDialogScale(n->element) : 1.0f;
            if (k != 1.0f) {
                for (int32_t& v : rect)
                    v = static_cast<int32_t>(static_cast<float>(v) / k);
            }
            n->element->ComputeLayout(rect[0], rect[1], rect[2], rect[3]);
            if (_extDialogPlace != nullptr)
                _extDialogPlace(kExtPlaceAfterLayout, n->element, layoutW, layoutH, rect);
        }
    }

    // 0x18012161e — a fade in progress swallows the whole input half AND the
    // return value.
    if (_fadeVelocity != 0.0f)
        return 0;

    // ---- gather input into UpdateState ------------------------------------
    // ??0UpdateState (0x180115600) inlined, 0x180121635..0x18012164d: the
    // zeroes and mouseX / mouseY = -1 (B4 Y RT-UI; see the note above).
    Element::UpdateState st;

    const ::Input::Device* const in = ::Input::Device::_singleton;
    st.dragActive = (_dragDropDialog != nullptr) ? 1 : 0;

    const int32_t mx = static_cast<int32_t>(
        static_cast<float>(in->_mouse[0].value) * scale);      // +0x109c
    const int32_t my = static_cast<int32_t>(
        static_cast<float>(in->_mouse[1].value) * scale);      // +0x10b4
    st.mouseX = mx;
    st.mouseY = my;

    st.buttonPressed = in->_mouse[4].pressed;                   // +0x10fe
    st.b03           = in->_mouse[5].pressed;                   // +0x1116
    st.b02           = in->_mouse[6].pressed;                   // +0x112e
    st.b04           = in->_mouse[4].released;                  // +0x10ff
    st.b06           = in->_mouse[5].released;                  // +0x1117
    st.b05           = in->_mouse[6].released;                  // +0x112f
    st.leftDown      = (in->_mouse[4].value > 0) ? 1 : 0;       // +0x10fc
    st.b08           = (in->_mouse[5].value > 0) ? 1 : 0;       // +0x1114
    st.b09           = (in->_mouse[6].value > 0) ? 1 : 0;       // +0x112c
    st.wheel         = in->_mouse[12].value;                    // +0x11bc
    st.config        = ::Input::Configuration::_singleton->_keyMap;   // +0x00
    st.mouseCaptured = 0;

    uint8_t leftDown = st.leftDown;

    if (st.buttonPressed) {
        _mouseDownX = mx;
        _mouseDownY = my;
        st.dragDx = 0;                     // the JMP to 0x180121746 zeroes both
        st.dragDy = 0;
    } else if (leftDown) {
        st.dragDx = _mouseDownX - mx;
        st.dragDy = _mouseDownY - my;
    } else {
        st.dragDx = 0;
        st.dragDy = 0;
    }

    // ---- pump every dialog, tail (frontmost) -> head -----------------------
    Node* n = _dialogs._tail;
    if (n) {
        for (;;) {
            DialogInstance* const dlg = n->element;
            if (dlg->_parent.Get() == nullptr) {                    // +0x28
                // `DialogInstance::Update(const Time&, UserInterface*,
                // Element::UpdateState&)` (0x180113980) — non-virtual member of
                // the real class (block B), which spells the state as its
                // address; it WRITES BACK into `st`.
                // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md): a dialog with its own scale k
                // hit-tests the mouse in its own units; the shared state keeps the global ones. The
                // screen's clip region (every element's hover test, IsInsideClipRegion) is the
                // screen in the dialog's own units too: with k < 1 (ext.phone_menu's tall-dialog fit,
                // P11) the global one would cut off the dialog's lower and right parts.
                const float k = _extDialogScale != nullptr ? _extDialogScale(dlg) : 1.0f;
                if (k == 1.0f) {
                    dlg->Update(timeRef, this, st);                 // 0x18012176a
                } else {
                    const int32_t gx = st.mouseX;
                    const int32_t gy = st.mouseY;
                    const int32_t gdx = st.dragDx;
                    const int32_t gdy = st.dragDy;
                    const ClipRegion clip = _clipRegion;
                    st.mouseX = static_cast<int32_t>(static_cast<float>(gx) / k);
                    st.mouseY = static_cast<int32_t>(static_cast<float>(gy) / k);
                    st.dragDx = static_cast<int32_t>(static_cast<float>(gdx) / k);
                    st.dragDy = static_cast<int32_t>(static_cast<float>(gdy) / k);
                    _clipRegion.right = static_cast<int32_t>(static_cast<float>(clip.right + 1) / k) - 1;
                    _clipRegion.bottom = static_cast<int32_t>(static_cast<float>(clip.bottom + 1) / k) - 1;
                    dlg->Update(timeRef, this, st);
                    _clipRegion = clip;
                    st.mouseX = gx;
                    st.mouseY = gy;
                    st.dragDx = gdx;
                    st.dragDy = gdy;
                }
            }
            if ((dlg->_flags >> 1) & 1u) break;
            n = n->prev;
            if (!n) break;
        }
        // 0x180121781 — RELOADED from the UpdateState, which the callee may
        // have rewritten.
        leftDown = st.leftDown;
    }

    // ---- end drag-drop ----------------------------------------------------
    if (!leftDown && _dragDropDialog != nullptr) {
        for (int32_t id = 0xd; id <= 0xe; ++id) {
            Event rec;
            rec.dialog   = _dragDropDialog;
            rec.element  = nullptr;
            rec.eventId  = id;
            rec.arg14    = -1;
            rec.arg18    = 0;                                   // } one QWORD
            rec.arg1C    = 0;                                   // }
            rec.arg20    = static_cast<int32_t>(_dragDropId);
            rec.userData = _dragDropData;
            // rec._pad24 deliberately left uninitialised: retail never writes
            // it and the grow helper never copies it.
            const int32_t newSize = _events._size + 1;
            rtcoll::RtColl_ArraySetSizeFill_180033690(&_events, newSize, &rec);
        }

        _dragDropDialog = nullptr;
        if (Element* const dragEl = _dragDropElement) {
            // Element vtable slot 0 `delete` (slot 0, flags 1) — VIRTUAL.
            delete dragEl;
            _dragDropElement = nullptr;                          // inside the guard
        }
        _dragDropElement = nullptr;                              // again, unguarded
        _dragDropId      = 0;
        _dragDropData    = nullptr;
        _dragDropGrabX   = 0;                                    // } one QWORD
        _dragDropGrabY   = 0;                                    // }
    }

    CleanRemovedDialogs();                                      // #3

    _mouseCaptured = st.mouseCaptured;                          // UpdateState+0x20

    return GetMouseCursorIndex(mx, my);
}
