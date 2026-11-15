// ============================================================================
// src/diag/modal_driver.cpp — Wave 10B lane SHOW
//
// THE ROUTE INTO THE NESTED MODAL PUMP.
//
// `UI::UserInterface::ShowDialog` (retail 0x18011fd10) is a nested modal loop.
// Read off the shipped bytes, it:
//
//   1. MemAlloc(0x128, 4) a SECOND UserInterface and construct it (0x180120de0)
//   2. copy this->[+0x90 .. +0xc8] (eight qwords) into the nested one
//   3. dialog->_ui (+0x10) = nestedUI ;  dialog->_flags (+0x20) |= 0x20
//      -- 0x20 is the bit `?DeleteOnClose@DialogInstance@UI@@QEBA_NXZ`
//      (0x180114110) reads INVERTED: `!(flags>>5 & 1)`. So the OR makes
//      DeleteOnClose FALSE: the modal dialog is NOT destroyed when it closes,
//      because the CALLER still has to read the result out of it. Every
//      call site does exactly that (optionsdialog.cpp reads +0xC0/+0xC4 after
//      ShowDialog returns, then DeletePtr's).
//      It is NOT the modal bit -- `?IsModal@` (0x180114170) reads bit 1.
//   4. call the dialog's vtable slot 4 (+0x20)          [0x18011fe2a]
//   5. Array::SetSize push the dialog into nestedUI->_addDialogs (+0x28)
//   6. UserInterface::Update(nestedUI, Time(0))          [0x18011fe67]
//   7. append {DrawStandAloneUI  (0x1801203f0), nestedUI} to MainLoop+0x80
//   8. append {UpdateStandAloneUI(0x180120310), nestedUI} to MainLoop+0x90
//   9. WHILE the dialog is still in nestedUI->_dialogs (list head at +0x00,
//      chained through +0x10): call `MainLoop::Frame(_singleton, true)`
//      (0x1800dd100) and stop early if it returns true (quit requested)
//  10. compact both callback arrays, removing its own two entries
//  11. ~UserInterface(nestedUI) (0x180120b90) + MemFree
//  12. return *(int*)(dialog + 0x24)  -- the dialog RESULT, i.e. exactly what
//      `?GetResult@DialogInstance@UI@@QEBAHXZ` (0x1800455c0: `MOV EAX,[RCX+0x24]`)
//      returns and `?SetDialogResult@` (0x1800455d0: `MOV [RCX+0x24],EDX`) writes.
//
// Step 9 is the wall. `MainLoop::Frame(this, param_1=true)` skips
// `MainLoop::Update` -- 0x1800dd28e sits inside `if (!param_1)` -- so every
// existing harness tick (MenuDialogLoopT::Update, ToolHarness_Tick, AutoStart)
// stops running the moment a modal opens, and only resumes when it closes.
// That is why `BANISHED_MODAL_ANSWER` had to substitute the answer and skip
// ShowDialog altogether: it is a BYPASS, not a route.
//
// But `Frame` DOES walk one array on every fixed-timestep tick, `param_1` or
// not (0x1800dd2a3..0x1800dd2d6):
//
//     MOV RDX, [RDI+0x80]        ; DRAW array base
//     MOV RAX, [RDI+0x90]        ; UPDATE array base
//     LEA RCX, [RDI+0x48]        ; arg1 = &fixedTimestep   (System::Time*)
//     MOV RDX, [RBX+RDX+8]       ; arg2 = DRAW[i].ctx      <-- SEE BELOW
//     CALL [RBX+RAX]             ; fn   = UPDATE[i].fn
//
// so a callback we append to MainLoop+0x90 runs inside the nested pump. That
// is the route, and this file is it.
//
// ---------------------------------------------------------------------------
// THE RETAIL QUIRK THAT DICTATES THIS FILE'S SHAPE
// ---------------------------------------------------------------------------
// Look again at those two loads. The FUNCTION comes from the +0x90 (update)
// array and the CONTEXT comes from the +0x80 (draw) array, at the same index.
// This is not a decompiler artefact -- it is in the disassembly above, and
// Ghidra's C prints it too. It is invisible in retail because `ShowDialog` is
// the only writer and it appends to both arrays in lockstep with the same
// `nestedUI` context, so DRAW[i].ctx == UPDATE[i].ctx for every i.
//
// Two consequences for us, both load-bearing:
//
//   (a) Our callback MUST IGNORE its second argument. We cannot make the ctx
//       we register in +0x90 arrive; we would receive whatever sits at the same
//       index of +0x80. State lives in this file's statics instead.
//
//   (b) We MUST append a paired entry to +0x80 as well. If +0x90 is longer
//       than +0x80, `MOV RDX,[RBX+RDX+8]` reads past the draw array -- and if
//       +0x80 has never been allocated it is NULL, making that a read of
//       address 0x8+i*0x10 and an immediate access violation IN RETAIL'S
//       FRAME LOOP. Appending in lockstep, exactly as ShowDialog does, is what
//       keeps the quirk harmless.
//
// We are armed BEFORE any ShowDialog runs, so we occupy the low indices and
// ShowDialog appends after us. Its removal pass matches on (fn, ctx) pairs of
// its own, so it compacts its entries and leaves ours in place.
//
// ---------------------------------------------------------------------------
// HOW THE DISMISSAL IS DONE, AND WHY IT IS NOT AN INVENTION
// ---------------------------------------------------------------------------
// Every MessageDialog button in the ported exe does precisely this
// (src/game/Dialog/messagedialog.cpp, OnOk / OnCancel / OnYes / OnNo, retail 0x14006CFF0
// /0x14006D030/0x14006CFB0/0x14006CF70):
//
//     self->_result = <result>;                              // +0x24
//     self->_ui->CloseDialog(InternalPtr(self));             // _ui at +0x10
//
// so this driver performs the same two operations on the front dialog of the
// nested UI. It is a synthetic CLICK, not a synthetic dialog system: retail's
// CloseDialog, retail's removal list, retail's `CleanRemovedDialogs` and
// retail's loop-exit test all run untouched.
//
// Note `*(void**)(dialog+0x10)` is the NESTED UserInterface, because step 3 of
// ShowDialog put it there. So CloseDialog is aimed at the nested UI without us
// having to thread it through -- the same way a real button reaches it.
// ============================================================================

#include "modal_driver.h"
#include "engine/MainLoop.h"   // ENDGAME B.3.9 SS2: Engine::MainLoop::_singleton

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include <new>
#include "ui/DialogInstance.h"
#include "options.h"
#include "Dialog/Dialogs.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K4); UserInterface::UpdateStandAloneUI
#include "system/MemoryAllocator.h"   // B3R: System::MemoryAllocator::MemAlloc / MemFree / _singleton

namespace ModalDriver {
namespace {

// --- Layout, all read off the shipped Runtime-x64.dll -----------------------
// (UpdateStandAloneUI 0x180120310 is appended to MainLoop+0x90 and DrawStandAloneUI
// 0x1801203f0 to MainLoop+0x80 by ShowDialog 0x18011fd10.)

// MainLoop::_postDraw (+0x80) and ::_postUpdate (+0x90): Array<MainLoopCallback>
// {data, count, cap}; DialogInstance::_ui (+0x10), ::_flags (+0x20, bit 0x20 =>
// !DeleteOnClose), ::_result (+0x24, ?GetResult@ / ?SetDialogResult@).
using Entry = ::Engine::MainLoop::PostUpdateCallback;   // 16 bytes {_func, _context}; the +0x90 element type

// --- Configuration ---------------------------------------------------------
bool s_parsed      = false;
bool s_enabled     = false;
int  s_answer      = 0;
int  s_ticks       = 3;

bool s_armed       = false;
int  s_dismissed   = 0;

// Per-modal state. The callback cannot receive a context (see quirk (a)).
UI::DialogInstance* s_seenDialog = nullptr;
int   s_seenTicks  = 0;

void ParseEnv() {
    if (s_parsed) return;
    s_parsed = true;
    // Deliberately NOT the usual `=0 means off` rule: 0 is a legitimate dialog
    // result (MessageDialog Cancel). Presence of the variable arms the driver.
    if (!Opt().modalDriver) return;
    s_enabled = true;
    s_answer  = Opt().modalDriverAnswer;
    if (Opt().modalDriverTicks > 0) s_ticks = Opt().modalDriverTicks;   // "<answer>,<ticks>"
    std::fprintf(stderr,
        "[ModalDriver] ARMED (BANISHED_MODAL_DRIVER=\"%s\") - will answer any modal "
        "with result %d after %d nested tick(s)\n", Opt().modalDriverRaw, s_answer, s_ticks);
}

::Engine::MainLoop* MainLoopSingleton() {
    return ::Engine::MainLoop::_singleton;   // the class static (ENDGAME B.3.9 SS2)
}

// Append `e` to `arr` ({data, count, cap}). Reallocates through
// System::MemoryAllocator, the same one ShowDialog's inlined push uses,
// and sets capacity == count so retail's own next push takes its grow path.
template <class E> bool AppendEntry(Collection::Array<E>& arr, const E& e) {
    E* old   = arr._data;
    int    count = arr._size;
    if (count < 0) return false;

    E* fresh = static_cast<E*>(
        System::MemoryAllocator::MemAlloc((uint64_t)(count + 1) * sizeof(E), 8));
    if (!fresh) return false;

    for (int i = 0; i < count; ++i) fresh[i] = old[i];
    fresh[count] = e;

    arr._data     = fresh;
    arr._size     = count + 1;
    arr._capacity = count + 1;

    if (old) System::MemoryAllocator::MemFree(old);
    return true;
}

// The paired no-op in the DRAW array. It exists only so that quirk (b) reads a
// valid slot; MainLoop::Draw will call it, so it must be a real function that
// returns. It takes no meaningful arguments under any x64 convention because it
// touches none.
void NoopDraw(Video::Context&, void*) {}

// Find the front dialog of a live nested modal UI, or nullptr.
//
// Detection is by SIGNATURE, not by cooperation from our port: we scan
// MainLoop+0x90 for an entry whose fn is `UpdateStandAloneUI`. That entry's
// ctx is the nested UserInterface.
UI::DialogInstance* FindModalDialog() {
    ::Engine::MainLoop* ml = MainLoopSingleton();
    if (!ml) return nullptr;

    Entry* arr   = ml->_postUpdate._data;
    int    count = ml->_postUpdate._size;
    if (!arr || count <= 0) return nullptr;

    // OUR ShowDialog port (userinterface_showdialog.cpp) installs
    // UI::UserInterface::UpdateStandAloneUI (retail 0x180120310) itself;
    // it is compared by name.
    const ::Engine::MainLoop::PostUpdateFunc wantedPort = &UI::UserInterface::UpdateStandAloneUI;

    for (int i = 0; i < count; ++i) {
        const ::Engine::MainLoop::PostUpdateFunc fn = arr[i]._func;
        if (fn != wantedPort) continue;
        void* nestedUiCtx = arr[i]._context;
        if (!nestedUiCtx) continue;
        UI::UserInterface* nestedUi = reinterpret_cast<UI::UserInterface*>(nestedUiCtx);   // callback void* context (MainLoop callback userData, retail PEAX)  // CAST-OK: user_data
        // nestedUI->_dialogs head at +0x00; node->dialog at +0x00.
        auto* head = nestedUi->_dialogs._head;
        if (!head) continue;
        return head->element;
    }
    return nullptr;
}

// The callback itself. Registered in MainLoop+0x90; runs on every fixed-timestep
// tick of every Frame, nested or not. MUST ignore both arguments (quirk (a)).
void Tick(const System::Time&, void*) {
    UI::DialogInstance* dialog = FindModalDialog();
    if (!dialog) { s_seenDialog = nullptr; s_seenTicks = 0; return; }

    if (dialog != s_seenDialog) { s_seenDialog = dialog; s_seenTicks = 0; }
    if (++s_seenTicks < s_ticks) return;

    UI::DialogInstance* d = dialog;
    UI::UserInterface*  ui = d->_ui;
    uint32_t  flags = d->_flags;

    std::fprintf(stderr,
        "[ModalDriver] dismissing modal dialog=%p nestedUi=%p flags=0x%x "
        "(DeleteOnClose=%d) with result %d\n",
        dialog, (void*)ui, flags, (flags & 0x20) ? 0 : 1, s_answer);

    // Exactly what MessageDialog::OnOk / OnCancel / OnYes / OnNo do (see the header comment).
    d->_result = s_answer;
    Object::InternalPtr<UI::DialogInstance> ibp(d);
    ui->CloseDialog(&ibp);

    ++s_dismissed;
    s_seenDialog = nullptr;
    s_seenTicks  = 0;
}

}  // namespace

bool Enabled() { ParseEnv(); return s_enabled; }
int  DismissedCount() { return s_dismissed; }

bool Poll() {
    ParseEnv();
    if (!s_enabled) return false;
    if (s_armed)    return true;

    ::Engine::MainLoop* ml = MainLoopSingleton();
    if (!ml) return false;                 // Engine::Startup has not built it yet
    if (!System::MemoryAllocator::_singleton) return false;   // B3R: read live (was the g_pMemAllocSingleton snapshot)

    // Order matters only for readability -- both must succeed, and the DRAW
    // append must not be left dangling if the UPDATE one fails, or we would
    // have made the arrays UNEVEN in the direction quirk (b) tolerates (draw
    // longer than update is harmless; update longer than draw is the fault).
    ::Engine::MainLoop::PostDrawCallback draw{ &NoopDraw, ml };
    if (!AppendEntry(ml->_postDraw, draw)) {
        std::fprintf(stderr, "[ModalDriver] ARM FAILED: could not grow MainLoop+0x80\n");
        return false;
    }
    Entry upd{ &Tick, ml };

    if (!AppendEntry(ml->_postUpdate, upd)) {
        std::fprintf(stderr, "[ModalDriver] ARM FAILED: could not grow MainLoop+0x90 "
                             "(the +0x80 no-op stays; draw-longer-than-update is safe)\n");
        return false;
    }

    s_armed = true;
    std::fprintf(stderr,
        "[ModalDriver] armed on MainLoop=%p  draw[%d] update[%d]  "
        "(callback %p paired with no-op %p)\n",
        (void*)ml, ml->_postDraw._size,
        ml->_postUpdate._size,
        (void*)&Tick, (void*)&NoopDraw);
    return true;
}

}  // namespace ModalDriver
