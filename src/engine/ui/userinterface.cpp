// ============================================================================
// src/engine/ui/userinterface.cpp — Wave 7 lane C — UI::UserInterface
// ============================================================================
//
// Ports 8 of the 22 `UI::UserInterface` entries our tree calls (PLAN.md §4.2
// wave 7, §6). The recovered layout and the evidence for every offset are in
// src/engine/ui/UserInterface.h; the per-function write-up, the oracle
// argument for each of the 8, the reason for each of the 14 hold-backs, and
// the three phantom-name resolutions are in docs/history/runtime_port/WAVE7_LANE_C.md.
//
//   | Export (short form)         | Retail      | Ord  | Ported | Oracle    |
//   |-----------------------------|-------------|------|--------|-----------|
//   | ??0UserInterface (ctor)     | 0x180120de0 |  471 | yes    | twin      |
//   | ??1UserInterface (dtor)     | 0x180120b90 |  705 | W10-UIF| twin      |
//   | ?AddDialog@                 | 0x180120ee0 | 1546 | W9-Z   | none      |
//   | ?BringDialogToFront@ (V)    | 0x180120440 | 1672 | NO     | —         |
//   | ?CloseDialog@ (V)           | 0x180120f20 | 1725 | NO     | —         |
//   | ?Draw@                      | 0x180121890 | 2272 | NO     | —         |
//   | ?EnableCursor@              | 0x180122580 | 2293 | W9-Z   | none      |
//   | ?BeginSpriteCapture@        | 0x18011f4c0 |      | yes    | none      |
//   | ?EndSpriteCapture@          | 0x18011f4a0 | 2316 | yes    | replay    |
//   | ?GetAddDialogCount@         | 0x180031160 | 2441 | yes    | replay    |
//   | ?GetFirstContainedDialog@   | 0x18011fb00 | 2669 | yes    | replay    |
//   | ?GetFirstDialog@            | 0x180031150 | 2670 | yes    | replay    |
//   | ?PausesGame@                | 0x18011fa10 | 3317 | yes    | replay    |
//   | ?Play@                      | 0x1801223d0 | 3418 | W9-Z   | none      |
//   | ?SaveAllSettings@           | 0x1801204b0 | 3608 | W10-UIF| replay+twin|
//   | ?SetCursor@                 | 0x1801225f0 | 3865 | W9-Z   | none      |
//
// `W9-Z` = ported by Wave 9 lane Z as a NO-ORACLE port, for the cutover. See
// the lane-Z block further down for why an oracle is not merely absent but
// would be actively misleading on all four.
//   | ?SetFullscreenFade@         | 0x18011f500 | 3887 | yes    | replay    |
//   | ?SetFullscreenFadeSprite@   | 0x18011f560 | 3888 | yes    | replay    |
//   | ?ShowDialog@                | 0x18011fd10 | 4034 | NO     | —         |
//   | ?Update@                    | 0x1801213c0 | 4511 | NO     | —         |
//
// (The lane's scope list is 22 rows, but three of them are PHANTOM SPELLINGS
// of rows already in this table — see "THE THREE PHANTOM NAMES" below — so
// there are 19 distinct functions and the achievable denominator is 19.)
//
// Every mangled name above was checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with a pefile query BEFORE any body was
// written (reference_resolveexport_phantom_names.md: a name the DLL does not
// contain resolves to NULL in silence, the port still registers, and the census
// still counts it). The addresses and ordinals above are that query's output.
//
// ---------------------------------------------------------------------------
// WHY REGISTERING THIS PORT IS SAFE, EVEN THOUGH THE DLL KEEPS CALLING ITSELF
// ---------------------------------------------------------------------------
// `UI::UserInterface` is a stateful object the DLL drives internally:
// `Engine::DialogLoaderLoopT` and `DialogLoopT` construct one, `ShowDialog`
// constructs a nested one, and `Engine::MainLoop` pumps `Update`/`Draw` on it
// every frame. Registering a port redirects OUR callers only — the DLL reaches
// its own methods through direct near-CALLs the registry cannot see — so for
// anything ported here there really are two implementations operating on one
// live object. That is the hazard PLAN §4.2's closing note names, and WAVE7.md
// flagged this lane as the most likely first user of the detour mechanism.
//
// It is safe here, and NO DETOUR IS INSTALLED, because of four facts, each
// CHECKED rather than assumed:
//
//   1. The class is NOT polymorphic. The ctor's first store is
//      `_dialogs._head = 0`, not a vtable pointer (disassembly quoted in
//      src/engine/ui/UserInterface.h), and none of the 52 exported members
//      is `UEAA`. So there is no vtable-ownership split: PLAN §4.3 risk 3 does
//      not apply, exactly as it did not for `Video::Context` in Wave 6 lane A.
//   2. Every ported body is a leaf. Five are pure reads; three write only
//      scalar fields of `this`. NONE of the eight allocates, frees, links,
//      unlinks, calls another ported body, or reaches any global. The layout
//      they read is `static_assert`ed offset by offset.
//   3. Everything that DOES reshape the object — the dtor, AddDialog,
//      CloseDialog, BringDialogToFront, Update, Draw, ShowDialog — is held
//      back and stays in the DLL. So the DLL remains the sole owner of the
//      object's structure, and our eight only observe it or set flags in it.
//   4. The replay oracle below proves, on every diffed call, that our body
//      leaves the WHOLE 0x128-byte object byte-identical to what the DLL's
//      body would have left, and returns what the DLL's body would have
//      returned.
//
// The honest form of the detour verdict, since this lane was asked for it
// explicitly: a detour is NOT needed *for what this lane ported*. It is not a
// finding that `UserInterface` in general needs no detour. The day someone
// ports `Update` or `AddDialog`, the DLL's `MainLoop` will still be calling
// the DLL's `Update` on the same object our `AddDialog` mutated, and that is
// the question the detour exists to answer. This lane deliberately did not
// walk up to that line.
//
// ---------------------------------------------------------------------------
// THE THREE PHANTOM NAMES — resolved, and they are NOT dead code
// ---------------------------------------------------------------------------
// WAVE7.md lists three lane-C mangled names that do not exist in the shipped
// export table, and expected the Wave 6 `Geometry::Attach`/`Detach` shape: a
// silent NULL behind an `if (ptr)` guard, i.e. behaviour that has never
// happened. **That is not what these are.** All three call sites have an
// explicit `ResolveAddr(Module::Runtime, …)` fallback, and all three fall back
// onto the RIGHT function:
//
//   engine_funcs.cpp:1603  CloseDialog         AEAV… -> NULL -> 0x180120F20 = the V twin
//   engine_funcs.cpp:3252  BringDialogToFront  AEAV… -> NULL -> 0x180120440 = the V twin
//   engine_funcs.cpp:2905  GetFirstContained…  QEAA… -> NULL -> 0x18011FB00 = the QEBA twin
//
// and the ABI is identical in every case, so the calls have always worked:
//   * `AEAV?$InternalPtr@…` (by reference) versus `V?$InternalPtr@…` (by
//     value) are the SAME machine calling sequence. `InternalPtr<T>` is an
//     8-byte class with a non-trivial copy constructor, so MSVC x64 passes it
//     by address either way — which the decompilations confirm directly: all
//     three callees dereference RDX (`AddDialog` does `*(void**)param_2`,
//     `CloseDialog` does `lVar2 = *param_2`). The only real difference is
//     whose copy the callee gets, and none of these three callees writes
//     through the slot, so passing the live pointer is behaviourally
//     identical to passing retail's temporary.
//   * `QEAA` versus `QEBA` is const-ness, which has no ABI at all.
//
// So this is a naming defect, not a functional gap, and the fix is at the
// source: engine_funcs.cpp now resolves the names the DLL actually exports,
// keeping the address fallback. Two things follow that DO matter:
//
//   (a) It makes the export-name audit honest again. Three of Wave 7's six
//       phantoms were lane C's, and all three were benign; a phantom masked by
//       an address fallback is exactly the case
//       reference_resolveexport_phantom_names.md warns is invisible.
//   (b) It changes what a PORT of one of these has to do. Had someone ported
//       `CloseDialog` and registered it under the DLL's real name only, the
//       phantom-name resolve would still have returned NULL, the address
//       fallback would still have found the DLL, and the port would have been
//       DEAD at its only call site while reporting `calls=0 divergences=0`
//       (reference_uncalled_port_is_not_verified.md). Registering under BOTH
//       the name and the retail address — which this file does for all eight
//       ports — is what closes that hole.
//
// ---------------------------------------------------------------------------
// WHAT STAYS IN THE DLL, AND WHY (short form; the long form is the lane doc)
// ---------------------------------------------------------------------------
//   ??1UserInterface (dtor)   Unlinks and frees every list node under the
//                             allocator's CRITICAL_SECTION and runs each
//                             dialog's deleting destructor. Win32 in a
//                             non-_win file (PLAN §4.1 decision 1), the Wave 2
//                             allocator (NO-GO), and no oracle: a destructor
//                             runs once.
//   ?AddDialog@               Grows _addDialogs. A replay would push the SAME
//                             dialog twice through the SAME buffer pointer,
//                             or realloc and free the buffer the live object
//                             still points at. No oracle exists.
//   ?CloseDialog@             Recursive, allocates, frees, unlinks nodes from
//                             a DialogInstance's OWN child list, and calls
//                             UpdateEvents. Not replayable for the same
//                             reason, several times over.
//   ?BringDialogToFront@      Removes and re-appends a list node. A replay
//                             moves it twice and allocates a second node.
//   ?Draw@                    Builds a Camera and a SpriteContext, submits
//                             every dialog's geometry and frees four buffers.
//                             A replay draws the frame twice.
//   ?Update@                  The class's whole tick: drains _addDialogs into
//                             the list, allocates a node per dialog, frees the
//                             array, advances the fade, calls
//                             DialogInstance::Update on every dialog and
//                             consumes input. Not replayable; not even
//                             idempotent.
//   ?ShowDialog@              A NESTED `Engine::MainLoop::Frame` LOOP. See the
//                             dedicated note below.
//   ?Play@                    Queues a sound on the Audio::Scene. A replay
//                             plays it twice, audibly.
//   ?EnableCursor@            Mutates the Application singleton and dispatches
//                             through a handler tree. See the note below.
//   ?SetCursor@               Calls the Video::Device backend. See the note.
//   ?SaveAllSettings@         Builds an IO::MemoryStream and serialises every
//                             dialog's settings. Allocates, frees, and writes
//                             persisted state.
//
// ?ShowDialog@ — READ THIS BEFORE PORTING IT
// ------------------------------------------
// `ShowDialog` (0x18011fd10) allocates a SECOND UserInterface (this is where
// the literal `Allocate(_singleton, 0x128, 4)` that pins our sizeof comes
// from), copies the audio scene and the seven sound slots into it, moves the
// dialog onto it, registers `DrawStandAloneUI`/`UpdateStandAloneUI` callbacks
// on `Engine::MainLoop`, and then
//
//     do { ... } while (!Engine::MainLoop::Frame(_singleton, true));
//
// pumps whole frames until the dialog leaves the nested UI's list. It then
// unregisters both callbacks, destroys the nested UI and frees it, and returns
// `*(int*)(dialog + 0x24)` — the dialog's result code.
//
// This single fact was the whole `Save9.sav` autoload stall and blocked the
// plan for two waves (docs/history/runtime_port/WAVE4_LANE_H.md §1): no unattended
// driver can reach a modal, because the driver runs inside the nested loop
// with its own state machine already finished. It is why `BANISHED_MODAL_ANSWER`
// exists. Porting it would mean owning `MainLoop::Frame` re-entrancy, two
// callback arrays' growth policy, and the lifetime of a second UserInterface —
// with no oracle of any kind, because running it twice pumps two frame loops.
// HELD BACK. This is the good outcome.
//
// One correction to WAVE4_LANE_H.md while we are here, since a port has now
// read the body: that document says ShowDialog "takes its argument by value
// and writes through the slot it is given". The by-value half is right and the
// caller-side fix built on it is right. The "writes through the slot" half is
// not: the decompiled body only READS `*param_2` (four times) and never stores
// to it. What retail's caller at exe 0x1400b61e0 is really doing with its
// scratch copy is honouring MSVC's by-value convention, where the CALLER owns
// and destroys the copy. Nothing about the fix changes.
//
// ?EnableCursor@ and ?SetCursor@ — held back for the same shape of reason
// -----------------------------------------------------------------------
// Both are small and fully reversed, and both would register a body whose
// risky half the oracle cannot see.
//   EnableCursor writes `_cursorEnabled`, then — unless
//   `Input::Configuration::_singleton[10]` forces it off — writes
//   `Application::_singleton[0x10]` and walks a keyed handler tree to fire one
//   indirect callback with id 0x20. A replay is WORSE than useless here: our
//   body would have already set the Application flag, so the DLL's replay
//   takes the early-out and never exercises the tree walk at all. The object
//   compare would pass while the only thing that can go wrong went untested.
//   SetCursor writes `_cursor` and then calls
//   `Video::Device::_singleton` -> `+0x100` -> vtable `+0x40` with the
//   cursor's sheet image and hotspot. A replay issues that backend call a
//   second time per diffed call. Setting the same cursor twice is very likely
//   idempotent, but "very likely" is not an oracle, and a graphics-backend
//   call is not the place to find out.
// Both are one afternoon's work for whoever wants them, with a shadow oracle
// that snapshots the Application singleton / the device state instead.
//
// ---------------------------------------------------------------------------
// PORTABILITY: no <windows.h> anywhere in this file, per PLAN §4.1 decision 1.
// Every held-back function above is also, not coincidentally, one that takes a
// CRITICAL_SECTION or talks to a device.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <new>

#include "io/Stream.h"          // WAVE 10 LANE UIF — SaveAllSettings' MemoryStream
#include "ui/UserInterface.h"
// ENDGAME B.3.6 block E: the real classes the member bodies call, and
// element_common.h's RULE B6-5 atomic door caches. element_common.h also
// carries the file-scope `Module` / `ResolveAddr` declaration described below.
#include "ui/DialogInstance.h"
#include "ui/DialogController.h"
#include "ui/Element.h"
#include "ui/Cursor.h"
#include "ui/SpriteSheet.h"
#include "ui/Sprite.h"
#include "audio/Audio.h"
#include "element_common.h"
#include "video/Device.h"        // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "video/CompanionData.h"  // Video::DeviceData::SetImageBuffer
#include "graphics/Material.h"   // Graphics::MaterialInstance::_textures
#include "application/Application.h"  // ENDGAME B.3.9 SS1: Application::_singleton
#include "input/Input.h"         // ENDGAME B.3.9 SS1: Input::Configuration::_singleton
#include "system/Timer.h"        // ENDGAME B.3.9 SS1: Timer::_secondsPerTick

// ----------------------------------------------------------------------------
// ResolveAddr, forward-declared rather than pulled in from standalone/resolve.h
// — the same reasoning rt_port.h gives for declaring RegisterRuntimeExport
// locally: resolve.h drags in <windows.h>, and PLAN.md §4.1 requires
// src/engine/ to stay platform-neutral. These declarations must stay
// byte-identical to resolve.h's; the linker checks them against it.
//
// Added by Wave 9 lane Z for `AddDialog`, whose array push is a Runtime
// INTERNAL with no exported name — 0x18007a0d0, ported by Wave 9 lane Q. Note
// this is the PORT-AWARE door: ResolveAddr consults the runtime address
// registry before the DLL (reference_resolveaddr_consults_port_registry_first
// .md), which is exactly what is wanted for a dependency and is also what makes
// the call survive cutover.
// ----------------------------------------------------------------------------
// (The declaration now comes from element_common.h, included above.)

namespace {

using UI::UserInterface;

constexpr size_t kUiSize = sizeof(UserInterface);
static_assert(kUiSize == 0x128, "layout header and this file must agree");

// ---- the four exported DATA slots Wave 9 lane Z's ports read ----------------
// Through RT_DATA_SLOT, which never caches a null: at cutover these are supplied
// by lane C2's mint, and a slot resolved before RtDataMintInit() must be able to
// retry rather than freeze the DLL's answer for the process
// (rt_port.h, "Resolve-and-cache that NEVER caches a null").
//
// Every one of the four is already a minted row today — see the DEPENDENCY
// AUDIT in the lane-Z block below for the row numbers — so none of these is a
// new cutover obligation.
//
// ENDGAME B.3.9 SS1 (B39_RULES D7): all four are class statics now (redirect
// in their namespace's *PortInit) -- read fresh, no lazy AtomicExport cache
// (B9-10; this body runs on the LOADER THREAD, per the banner above).
inline const double* TimerSecondsPerTick() {
    return &::System::Timer::_secondsPerTick;
}

} // namespace

// ============================================================================
// The ports
// ============================================================================
// Every body is byte-faithful to the retail disassembly (read, not only the
// decompiled C — reference_ghidra_c_output_vs_disassembly.md). No null guard
// retail does not have; the CLAUDE.md rule holds even where a guard looks
// free.

// ---- ??0UserInterface@UI@@QEAA@XZ  0x180120de0 ord 471 ---------------------
// 42 stores of a constant, in address order, and RAX = this on return. The two
// non-zero constants are `_uiScale = 1.0f` (0x3f800000 at +0xdc) and the pair
// `_fadeSpriteIndex = -1` (+0xe8) / `_fadeColor = 0xffffffff` (+0xf8).
//
// Retail writes these as raw-width stores, and several straddle field
// boundaries — the WORD at +0xd8 clears `_cursorEnabled` and `_mouseCaptured`
// together and stops (so +0xda..+0xdb stay unwritten), the QWORD at +0x20
// covers `_removedDialogs._size` / `_capacity`, the QWORDs at +0xfc and +0x104
// cover the four ints of `_clipRegion` in two pairs and stop (so
// +0x10c..+0x10f stay unwritten). The tree writes the NAMED fields in address
// order (B4_RULES H7): the same values into the same bytes, and the padding
// retail leaves alone (+0x14, +0x74, +0x89, +0xda, +0xec, +0x10c) is left
// alone here too.
static void CtorBody(UserInterface* self) {
    self->_dialogs._head  = nullptr;                    // +0x00
    self->_dialogs._tail  = nullptr;                    // +0x08
    self->_dialogs._count = 0;                          // +0x10 (DWORD)
    self->_removedDialogs._data     = nullptr;          // +0x18
    self->_removedDialogs._size     = 0;                // +0x20 } one QWORD
    self->_removedDialogs._capacity = 0;                // +0x24 }
    self->_addDialogs._data     = nullptr;              // +0x28
    self->_addDialogs._size     = 0;                    // +0x30 } one QWORD
    self->_addDialogs._capacity = 0;                    // +0x34 }
    self->_toolTipOwner  = nullptr;                     // +0x38
    self->_toolTipDialog = nullptr;                     // +0x40
    self->_events._data     = nullptr;                  // +0x48
    self->_events._size     = 0;                        // +0x50 } one QWORD
    self->_events._capacity = 0;                        // +0x54 }
    self->_mouseDownX = 0;                              // +0x58 } one QWORD
    self->_mouseDownY = 0;                              // +0x5c }
    self->_dragDropDialog  = nullptr;                   // +0x60
    self->_dragDropElement = nullptr;                   // +0x68
    self->_dragDropId      = 0;                         // +0x70 (DWORD)
    self->_dragDropData    = nullptr;                   // +0x78
    self->_dragDropGrabX   = 0;                         // +0x80 } one QWORD
    self->_dragDropGrabY   = 0;                         // +0x84 }
    self->_dragDropActive  = 0;                         // +0x88 (BYTE)
    self->_audioScene = nullptr;                        // +0x90
    for (Audio::SoundEffect*& sound : self->_sounds) sound = nullptr; // +0x98..+0xc8
    self->_cursor = nullptr;                            // +0xd0
    self->_cursorEnabled = 0;                           // +0xd8 } one WORD
    self->_mouseCaptured = 0;                           // +0xd9 }
    self->_uiScale = 1.0f;                              // +0xdc
    self->_fadeSpriteSheet = nullptr;                   // +0xe0
    self->_fadeSpriteIndex = -1;                        // +0xe8 (empty sprite)
    self->_fadeVelocity = 0.0f;                         // +0xf0 } one QWORD
    self->_fadeAmount   = 0.0f;                         // +0xf4 }
    self->_fadeColor = 0xffffffffu;                     // +0xf8
    self->_clipRegion.left   = 0;                       // +0xfc  } one QWORD
    self->_clipRegion.top    = 0;                       // +0x100 }
    self->_clipRegion.right  = 0;                       // +0x104 } one QWORD
    self->_clipRegion.bottom = 0;                       // +0x108 }
    self->_spriteCapture = nullptr;                     // +0x110
    std::memset(&self->_captureViewport, 0, sizeof self->_captureViewport);  // +0x118, two QWORDs
}

extern "C" UserInterface* RtUi_UserInterface_Ctor(UserInterface* self) {

    CtorBody(self);

    return self;
}

// ---- ?GetAddDialogCount@…@QEBAHXZ  0x180031160 ord 2441 --------------------
//   MOV EAX,[RCX+0x30] / RET
int32_t UI::UserInterface::GetAddDialogCount() const {
    return _addDialogs._size;
}

// ---- ?GetFirstDialog@…  0x180031150 ord 2670 -------------------------------
//   MOV RAX,[RCX] / MOV [RDX],RAX / MOV RAX,RDX / RET
// Returns a `LinkedList<…>::Iterator` by value. The iterator is EIGHT BYTES
// and is just the node pointer (Collection.h), so the hidden return slot in
// RDX gets one qword and the function hands the slot back in RAX —
// reference_hidden_retval_and_int_pointer_args.md: forget the retval slot and
// this becomes a wild write that still "works".
UI::UserInterface::Iterator UI::UserInterface::GetFirstDialog() {
    return _dialogs.GetFirst();          // the head node, one qword
}

// RETSLOT door (B5-12): `(this, ret)`, the member's prvalue placed in the slot.
extern "C" void* RtUi_UserInterface_GetFirstDialog(UserInterface* self, void* retval) {
    new (retval) UserInterface::Iterator(self->UI::UserInterface::GetFirstDialog());
    return retval;
}

// ---- ?PausesGame@…@QEBA_NXZ  0x18011fa10 ord 3317 --------------------------
// Walks the dialog list FORWARD from _head through `next` (+0x10) and returns
// true at the first dialog whose flag word at +0x20 has bit 0 set.
//
//   18011fa20  MOV RCX,[RAX]                ; node->element (the dialog)
//   18011fa23  TEST byte ptr [RCX+0x20],0x1 ; DialogInstance::_flags bit 0
//   18011fa29  MOV RAX,[RAX+0x10]           ; node = node->next
//
// The +0x20 flag word belongs to lane B's DialogInstance; it is read here as a
// raw offset with the disassembly above as its evidence, rather than by
// including a header another lane owns.
bool UI::UserInterface::PausesGame() const {
    for (auto* node = _dialogs._head; node; node = node->next) {
        const auto* dlg = node->element;
        if (dlg->_flags & 1u) return true;          // DialogInstance +0x20 bit 0
    }
    return false;
}

// ---- ?GetFirstContainedDialog@…@QEBA…HH@Z  0x18011fb00 ord 2669 ------------
// Walks the dialog list BACKWARD from _tail through `prev` (+0x08) — i.e.
// front-most first, so the topmost dialog wins the hit test — and returns the
// first dialog that contains (x, y), or a null InternalPtr.
//
// Per dialog, from the disassembly at 0x18011fb30:
//   flags = *(uint32*)(dlg + 0x20)
//   if (flags & 0x4)  -> skip this dialog entirely
//   if (flags & 0x8)  -> custom hit test:
//         element = *(void**)(dlg + 0x50);          root element
//         ui      = *(void**)(dlg + 0x10);          owning UserInterface
//         hit     = element->vtable[0x48](element, x, y, ui)
//   else              -> rectangle test against the root element's bounds:
//         e = *(void**)(dlg + 0x50)
//         hit = x >= e[+0x68] && x <= e[+0x70] && y >= e[+0x6c] && y <= e[+0x74]
//
// NOTE THE COMPARISON DIRECTION: the JL/JG/JL/JLE chain makes the rectangle
// INCLUSIVE on all four sides, and it pairs +0x68 with +0x70 for x and +0x6c
// with +0x74 for y — left/right are NOT adjacent to top/bottom. Those four
// offsets are lane A's Element bounds, read raw here for the same reason as
// the flag word above.
//
// ORACLE CAVEAT, stated because it is the one thing here that is not free:
// under a diff the custom-hit-test virtual is invoked a SECOND time, on the
// same element with the same arguments. The method is `const` in every widget
// that overrides it (it answers "do you contain this point"), so the second
// call is a pure re-evaluation — but it is a virtual into another lane's code,
// and if some widget ever makes it stateful this site is where that surfaces.
Object::InternalPtr<UI::DialogInstance>
UI::UserInterface::GetFirstContainedDialog(int32_t gx, int32_t gy) const {
    for (auto* node = _dialogs._tail; node; node = node->prev) {
        auto* dlg = node->element;
        const uint32_t flags = dlg->_flags;                            // +0x20
        if (flags & 0x4u) continue;

        // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md): a dialog with its own scale k is
        // tested in its own units. Null seam: retail's point.
        int32_t x = gx;
        int32_t y = gy;
        if (_extDialogScale != nullptr) {
            const float k = _extDialogScale(dlg);
            if (k != 1.0f) {
                x = static_cast<int32_t>(static_cast<float>(gx) / k);
                y = static_cast<int32_t>(static_cast<float>(gy) / k);
            }
        }

        bool hit;
        if (flags & 0x8u) {
            // Element vtable slot 9 (+0x48), `Contains(x, y, ui)` — a VIRTUAL
            // call dispatches through the element's own (minted) table.
            // `_root` (+0x50) through the dialog's own one-load accessor
            // (0x180045580): DialogInstance.h still spells the field `void*`.
            hit = dlg->GetRoot()->Contains(x, y, dlg->_ui); // +0x10
        } else {
            const Object::InternalPtr<UI::Element> e = dlg->GetRoot();
            hit = (x >= e->_layoutLeft) && (x <= e->_layoutRight) &&
                  (y >= e->_layoutTop)  && (y <= e->_layoutBottom);
        }
        if (hit) return Object::InternalPtr<UI::DialogInstance>(dlg);
    }
    return Object::InternalPtr<UI::DialogInstance>();
}

// RETSLOT door (B5-12): `(this, ret, x, y)`.
extern "C" void* RtUi_UserInterface_GetFirstContainedDialog(
        const UserInterface* self, void* retval, int x, int y) {
    new (retval) Object::InternalPtr<UI::DialogInstance>(
        self->UI::UserInterface::GetFirstContainedDialog(x, y));
    return retval;
}

// ---- ?BeginSpriteCapture@…  0x18011f4c0 -----------------------------------
//   MOV [RCX+0x110],RDX, then the viewport's four dwords from [R8] into +0x118..+0x124
//   (objdump of Runtime-x64.dll, D.4 RESIDUE) / RET. The capture is drawn into by
//   Draw (0x180121890) until EndSpriteCapture clears +0x110.
void UI::UserInterface::BeginSpriteCapture(SpriteCapture* capture, const Video::Viewport* viewport) {
    _spriteCapture = capture;
    _captureViewport = *viewport;
}

// ---- ?EndSpriteCapture@…@QEAAXXZ  0x18011f4a0 ord 2316 ---------------------
//   MOV qword ptr [RCX+0x110],0x0 / RET
// It does NOT touch the capture viewport at +0x118, and it does NOT tell the
// SpriteCapture object anything — Draw's `if (capture) EndAddingSprites` is
// what closes the capture out. Faithful: one store, nothing else.
void UI::UserInterface::EndSpriteCapture() {
    _spriteCapture = nullptr;
}

// ---- ?SetFullscreenFade@…@QEAAX_NI0@Z  0x18011f500 ord 3887 ----------------
// Three arguments and two completely different behaviours, which the parameter
// names hide: `immediate` (the third arg, R9B) does not mean "reverse".
//
//   immediate == true : set the fade AMOUNT directly, to 1.0f (fadeIn) or 0.0f,
//                       and do NOT start an animation. _fadeVelocity is left
//                       alone, so IsFading() stays false.
//   immediate == false: set the fade VELOCITY to +1.0f (fadeIn) or -1.0f and
//                       let Update integrate _fadeAmount toward the limit.
//
// FAITHFULNESS DETAIL: the colour is written in BOTH branches but at different
// points — first in the immediate branch, last in the animated one — and the
// animated branch writes it TWICE in the disassembly, once per sub-branch
// (0x18011f534 and 0x18011f546). Same value either way; reproduced as one
// store per path, which is what the byte compare sees.
void UI::UserInterface::SetFullscreenFade(bool fadeIn, uint32_t color, bool immediate) {
    if (immediate) {
        _fadeColor  = color;
        _fadeAmount = fadeIn ? 1.0f : 0.0f;
    } else {
        _fadeVelocity = fadeIn ? 1.0f : -1.0f;
        _fadeColor    = color;
    }
}

// ---- ?SetFullscreenFadeSprite@…@QEAAXVSprite@2@@Z  0x18011f560 ord 3888 ----
//   MOV RAX,[RDX] / MOV [RCX+0xe0],RAX / MOV RAX,[RDX+0x8] / MOV [RCX+0xe8],RAX
// A `UI::Sprite` is 0x10 bytes and is copied WHOLE, including the four padding
// bytes at its +0x0c — so this writes UserInterface +0xe0..+0xef, and the port
// must copy 16 bytes rather than assign the two named fields, or the byte
// compare will trip on padding the DLL propagated and we did not.
//
// `Sprite` is passed by value, which for a class with a non-trivial copy
// constructor means BY ADDRESS: RDX is a pointer to the caller's copy. That is
// exactly how our tree already calls it — see the 16-byte `uint8_t sprite[16]`
// that src/game/Dialog/optionsdialog.cpp's ApplyKeyMapToUI_Impl builds.
void UI::UserInterface::SetFullscreenFadeSprite(const Sprite* sprite) {
    _fadeSpriteSheet = sprite->spriteSheet;   // +0xe0 <- +0x00 } one QWORD
    _fadeSpriteIndex = sprite->spriteIndex;   // +0xe8 <- +0x08 } one QWORD
    _pad0ec          = sprite->pad0C;         // +0xec <- +0x0c }
}

// ============================================================================
// WAVE 9 LANE Z — the four UserInterface members the CUTOVER still needs
// ============================================================================
// Wave 7 lane C held `AddDialog`, `Play`, `SetCursor` and `EnableCursor` back.
// Its reasons are in the HELD BACK block above and none of them was wrong: all
// four mutate state that lives OUTSIDE the 0x128-byte object, so the replay
// oracle this file is built around cannot see the half that can break.
//
// What changed is not the argument, it is the cost of declining. Wave 9 removes
// `Runtime-x64.dll`. `ResolveExport` on an unported name then returns null in
// silence (standalone/resolve_standalone.cpp:692), and the four `fn_UI_*`
// pointers in engine/funcs/engine_funcs.cpp become null calls. All four are in
// the DLL-absent boot's `[RtUnresolved]` list — that list IS the work list —
// and "no oracle is available" stops being a reason to hold back once the
// alternative is an EXEC-of-0.
//
// So they are ported here as **NO-ORACLE PORTS**, the disposition Wave 9 lane M
// established for `EditElement::SetText`, `NumberElement::SetValueInt` /
// `SetValueFloat` and `NumberEditElement::SetMinMax`: a real body, registered,
// with NO call into the shipped body at all, because for each of the four a
// second execution is not a comparison, it is a second side effect.
// Per function:
//
//   AddDialog     calls the dialog's OWN vtable slot 4 (`OnOpened`) and then
//                 pushes into `_addDialogs`. A replay would open the dialog
//                 twice and push twice — and because the scratch copy shares
//                 the array BUFFER with the live object, the second push would
//                 write through into the caller's real array.
//   Play          claims a free channel in `Audio::Scene`'s 256-entry table and
//                 stamps a handle built from the scene's monotonically
//                 increasing counter. A replay claims a SECOND channel and
//                 bumps that counter again, so the two results cannot be equal
//                 even when both are right — and the sound plays twice.
//   SetCursor     ends in `Video::Device::_singleton -> +0x100 -> vslot 8`.
//                 Lane C's note stands: setting the same cursor twice is very
//                 likely idempotent, but "very likely" is not an oracle and a
//                 graphics-backend call is not where to find out.
//   EnableCursor  is the case where a replay is WORSE than useless, and lane C
//                 spelled out why: our body has already written
//                 `Application::_singleton[0x10]`, so the DLL's replay takes
//                 the equality early-out at 0x1801225a3 and never walks the
//                 handler tree — the object compare would pass while the only
//                 risky half went untested.
//
// Three of these four have an early-out that dominates in normal play: `Play`
// returns immediately for any UI sound id with no sound bound, and
// `EnableCursor` returns immediately whenever the requested state already
// matches.
//
// DEPENDENCY AUDIT — why these four and not the other 27 UI names in the
// residue. A port that calls a DLL internal does not close a cutover row, it
// MOVES it (WAVE9.md, "porting a function can GROW the cutover surface"). Every
// non-vtable callee of these four is already satisfied on OUR side:
//
//   0x18007a0d0  the `_addDialogs` push  -> PORTED, Wave 9 lane Q, registered
//                as `RtColl_ArraySetSizeFill_18007a0d0`
//                (src/engine/collection/array.cpp:812, whose own comment
//                already names it "the dialog pending-list push").
//   0x1801fe728  `?_secondsPerTick@Timer@System@@0NA`           -> MINTED
//   0x1801fe7a8  `?_singleton@Device@Video@@0PEAV12@EA`         -> MINTED
//   0x1801fc678  `?_singleton@Application@1@0PEAV11@EA`         -> MINTED
//   0x1801fe768  `?_singleton@Configuration@Input@@0PEAV12@EA`  -> MINTED
//
// (all four rows in standalone/rt_data_mint_tables.inc, which lane C2 owns and
// this lane did not touch). The remaining indirect calls go through vtables of
// objects the CALLER owns, which is the caller's cutover problem and not a new
// dependency of ours.
// ============================================================================

// ---- ?AddDialog@UserInterface@UI@@QEAAXV?$InternalPtr@VDialogInstance@UI@@@Object@@@Z
//      0x180120ee0  ord 1546  ------------------------------------------------
//
//     180120eed  MOV RCX,[RDX]            ; the DialogInstance* inside the ptr
//     180120ef3  MOV RAX,[RCX]            ; its vtable
//     180120ef6  CALL [RAX+0x20]          ; slot 4
//     180120ef9  MOV EDX,[RBX+0x30]       ; _addDialogs._size
//     180120efc  LEA RCX,[RBX+0x28]       ; &_addDialogs
//     180120f00  INC EDX
//     180120f02  MOV R8,RDI               ; the InternalPtr, BY ADDRESS
//     180120f0f  JMP 0x18007a0d0          ; SetSize(size+1), filling the new tail
//
// SLOT +0x20 IS `OnOpened`, and that is read out of a table rather than guessed
// from the name. `standalone/rt_data_mint_tables.inc` carries the whole of
// `??_7DialogInstance@UI@@6B@` slot by slot, minted from the DLL's own vtable:
// index 4 (byte offset 0x20) is `?OnOpened@DialogInstance@UI@@UEAAXXZ` at
// 0x180114fb0. That "add a dialog" opens it is the corroboration, not the
// evidence.
//
// The argument is `Object::InternalPtr<DialogInstance>` BY VALUE, which for a
// class with a non-trivial copy constructor means BY ADDRESS: RDX is a pointer
// to the caller's 8-byte InternalPtr. Retail dereferences it once for the
// vtable call and then hands the SAME pointer to the array push as the fill
// source, which is why it is spelled `void**` and not `void*`
// (reference_hidden_retval_and_int_pointer_args.md).
//
// NOTE what retail does NOT do: no null check on `*dlgPtr`. It relies on the
// caller never passing an empty pointer, and CLAUDE.md is explicit that a guard
// here would not restore the behaviour, it would skip it.
void UI::UserInterface::AddDialog(Object::InternalPtr<DialogInstance>* dialog) {
    // DialogInstance vtable slot 4 (+0x20) `OnOpened` — a VIRTUAL call, so it
    // dispatches through the dialog's own (minted or exe hand-built) table.
    dialog->Get()->OnOpened();

    // retail 0x18007a0d0 — kSpec_Fill8Z, trait-equivalent for
    // `DialogInstancePtr` (a pointer): `_addDialogs` is that trait-derived
    // `Array<DialogInstancePtr>` instantiation already. Retail hands the
    // caller's InternalPtr (RDX) to the push as the fill source; the fill is
    // its one pointer, copied here by name into the new tail slot.
    const DialogInstancePtr fill = dialog->Get();
    _addDialogs.SetSize(_addDialogs._size + 1, fill);
    // no early-out: every call does the whole body
}

// ---- ?Play@UserInterface@UI@@QEAAXH@Z  0x1801223d0  ord 3418 ---------------

void UI::UserInterface::Play(int32_t id) {

    Audio::SoundEffect* sound = _sounds[id];
    if (!sound) {  return; }

    // B.4 LEFT: `_audioScene` (+0x90) stays `void*` (code/Game assigns it a
    // `void*`: gameupdateloop.cpp:1652, menudialogloopt.cpp:1158). The body
    // is Audio::Scene::PlayEffect inlined (0x1801223ec..0x180122458).
    Audio::Scene* const scene = _audioScene;
    if (!scene) {  return; }

    Audio::Scene::Channel* const table = scene->channels._data;   // +0x10

    const double secondsPerTick = *TimerSecondsPerTick();
    const int64_t ticks = static_cast<int64_t>(0.0 / secondsPerTick);

    int index = -1;
    for (int i = 0; i < 0x100; ++i) {
        if (table[i].handleTag == 0) {                            // +0x68
            index = i;
            break;
        }
    }
    if (index < 0) {  return; }   // table full: retail drops it

    Audio::Scene::Channel& slot = table[index];
    slot.soundEffectRef = sound;                                  // +0x00
    slot.startTime._ticks = ticks;                                // +0x10
    slot.soundType      = 1;                                      // +0x08

    // `INC dword [R10+8] ; SHL ECX,9`: both wrap at 32 bits, so the counter
    // and the shift are unsigned here (an int32 overflow or shift is UB).
    const uint32_t counter = static_cast<uint32_t>(scene->playGeneration) + 1;   // +0x08
    scene->playGeneration = static_cast<int32_t>(counter);
    slot.handleTag = (static_cast<uint32_t>(index) * 2u) | (counter << 9);   // +0x68

}

void UI::UserInterface::SetCursor(Object::ExternalPtr<Cursor> cursorPtr) {
    // `ExternalPtr` by value is the bare pointer in RDX (banner, B6-11).
    Cursor* const cursor = cursorPtr.Get();

    _cursor = cursor;
    if (!cursor) {  return; }

    // B.4 LEFT: `SpriteSheet::m_field30` (+0x30) is a `uint64_t` another block
    // writes as an integer (saveload_draw.cpp). The sheet's MaterialInstance
    // texture 0 (`_textures` +0x50, entry +0x08 the image) goes to
    // `DeviceData` slot 8 (+0x40) `SetImageBuffer(image, hotX, hotY)`
    // (CompanionData.h).
    Video::DeviceData* const backend =
        Video::Device::_singleton->deviceData;

    const SpriteSheet* sheet = cursor->_spriteSheet.Get();
    const Graphics::MaterialInstance* const material =
        sheet->m_field30;
    Video::ImageBuffer* const image = material->_textures.GetData()[0].image;

    backend->SetImageBuffer(image, cursor->_hotSpotX, cursor->_hotSpotY);

}

// ---- ?EnableCursor@UserInterface@UI@@QEAAX_N@Z  0x180122580  ord 2293 ------

void UI::UserInterface::EnableCursor(bool enable) {

    _cursorEnabled = static_cast<uint8_t>(enable);

    // `Input::Configuration::_mouseDisplay` (+0x0a) forces the OS cursor off.
    // CMOVNZ at 0x18012259c.
    const bool wanted = enable && !::Input::Configuration::_singleton->_mouseDisplay;

    // 0x18012259f..0x1801225d9 is `Application::EnableCursor` (0x18009f800)
    // INLINED: compare +0x10, early-out, store, descend the message-callback
    // set for WM_SETCURSOR (0x20) and tail-call it with (hwnd, 0x20, 0, 0).
    // The member is that body (application_win.cpp), called by name.
    ::Application::Application::_singleton->EnableCursor(wanted);
}

// ============================================================================
// Registration
// ============================================================================

void UI::UserInterface::BringDialogToFront(Object::InternalPtr<DialogInstance>* dialog) {

    // ENDGAME B.3.2 step 2 — `_dialogs` is the real
    // `Collection::LinkedList<DialogInstancePtr>` member (UserInterface.h,
    // +0x000), the ONE retail instantiation of the class. The unguarded tail
    // read below is retail's own (CLAUDE.md: a faithful crash on an empty
    // list is not ours to fix) — `GetLast()` returns a null-node `Iterator`
    // exactly like the raw `_tail` this used to read, so dereferencing it
    // still faults the same way on an empty list.
    DialogInstance* dlg = dialog->Get();
    if (*_dialogs.GetLast() == dlg) {
        return;
    }

    // retail 0x18007a010 (Remove) then 0x180032750 (NewNode) + the inlined
    // tail-append — that whole sequence, re-reading `_tail` after Remove, IS
    // `LinkedList<T>::Add`'s own body (Collection.h), so the conversion is
    // Remove-then-Add rather than a re-derivation of the append.
    _dialogs.Remove(dlg);
    _dialogs.Add(dlg);
}

// ============================================================================
// WAVE 10 LANE UIF — ??1UserInterface@UI@@QEAA@XZ   0x180120b90  ord 705
// ============================================================================
//
// WHY THIS ROW IS OPEN NOW AND WAS NOT BEFORE. Wave 7 lane C held it back with
// three reasons, quoted at the top of this file: "Win32 in a non-_win file, the
// Wave 2 allocator (NO-GO), and no oracle: a destructor runs once." All three
// are answered rather than waived:
//
//   * WIN32. Retail INLINES `EnterCriticalSection` / `MemoryAllocator::Free`
//     (0x1800c6c00) / `LeaveCriticalSection` at all four of this body's frees.
//     The port calls the exported STATIC `?MemFree@MemoryAllocator@System@@
//     SAXPEAX@Z` (0x1800c7ad0), which is the out-of-line spelling of exactly
//     that inline — the same substitution `??1DialogInstance@` already makes in
//     src/engine/ui/dialog.cpp, and it keeps <windows.h> out of this file
//     (PLAN §4.1 decision 1) while taking the SAME critical section.
//   * THE ALLOCATOR NO-GO. Opened by Wave 10 lane ALLOC
//     (docs/history/runtime_port/WAVE10_LANE_ALLOC.md): 27 exports registered, so
//     there is ONE heap. Note what this port actually needs from that. It frees
//     through whichever `MemFree` is live, and the blocks it frees were
//     allocated through the same one. The flag is a variable this lane's runs
//     sweep, not a dependency this body has.
//   * "A DESTRUCTOR RUNS ONCE" — true, and it rules out a REPLAY. It does not
//     rule out an oracle. See the twin-synthetic oracle below, which builds two
//     byte-identical synthetic subjects out of the real allocator, runs OUR
//     body on one and the SHIPPED body on the other, and compares both the
//     0x128-byte object and the ORDERED LOG of destructor dispatches.
//
// LANE ALLOC'S ONE STILL-STANDING CAVEAT DOES NOT REACH THIS ROW. Its §6 says
// "do NOT arm `Shutdown`/`~MemoryAllocator` into lane N's open teardown crash".
// That is the ALLOCATOR'S OWN teardown pair. `~UserInterface` is a client of the
// allocator, not part of it: it takes the allocator's lock and returns blocks,
// which the allocator has been doing for 900k operations per 30 s run under the
// armed configuration (lane ALLOC §5b). Nothing here arms `Shutdown`.
//
// ---------------------------------------------------------------------------
// THE BODY, from the disassembly at 0x180120b90 (0x243 bytes, tail-JMP at
// 0x180120dd2). Four phases:
//
//   1. For each of the `_removedDialogs[i]` (i < _removedDialogs._size):
//        find the `_dialogs` node whose element is that dialog; unlink it from
//        head/tail/prev/next, null the node's own two links, `--_count`, and
//        FREE THE NODE. Then re-read `_removedDialogs._data` (retail re-loads
//        it at 0x180120c74, AFTER the free) and, unless the dialog's flag bit 5
//        is set, run its deleting destructor and null the array slot.
//   2. Walk whatever is LEFT of `_dialogs` and run each remaining dialog's
//        deleting destructor, nulling the node's element. THE NODES ARE NOT
//        FREED HERE — phase 4 does that.
//   3. Free `_events._data` (+0x48), then `_addDialogs._data` (+0x28), then
//        `_removedDialogs._data` (+0x18), zeroing each pointer AND the qword
//        that follows it (size+capacity together) in that order.
//   4. TAIL-JMP to `Collection::LinkedList<8>::Clear` (0x1800325f0), which
//        frees the surviving nodes and zeroes head/tail/count.
//
// TWO TRANSCRIPTION TRAPS, both taken from the DISASSEMBLY and not from the
// decompiled C (reference_ghidra_c_output_vs_disassembly.md):
//
//   (a) THE FLAG WORD IS READ BEFORE THE NULL TEST, in both loops:
//           180120c78  MOV RAX,[RBX+R14]        ; the dialog
//           180120c7c  MOV ECX,[RAX+0x20]       ; <-- deref
//           180120c8c  TEST RAX,RAX             ; <-- null test, AFTER
//       This is an unguarded deref that retail relies on. It is reproduced in
//       that order. CLAUDE.md's rule applies: a null there is an upstream bug,
//       and a guard would not restore the behaviour, it would skip it.
//   (b) `_removedDialogs._data` IS RE-READ after the node free (0x180120c74),
//       so a body that caches it across the free is wrong in exactly the way a
//       reallocating allocator would expose. The port re-reads.
//
// EVERY CALLEE IS SATISFIED, so this row CLOSES rather than moving one frame
// deeper:
//   * 0x1800c7ad0 `MemFree`                — ported, Wave 9 lane Q
//   * 0x1800325f0 `LinkedList<8>::Clear`   — ported, src/engine/collection/
//                                            linkedlist.cpp:334
//   * vtable slot 0 of each dialog         — the deleting destructor, which is
//                                            the DLL's (or the mint's) and is
//                                            not ours to satisfy either way.
// ---------------------------------------------------------------------------

namespace {

// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` 0x1800c7ad0 — retail's four
// inlined lock+Free sequences, out of line. Called directly (B3R RT-UI).
inline void UiMemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// ENDGAME B.3.2 step 2 — the 0x1800325f0 forwarder (`UiListClear`) that used
// to sit here is GONE; its one caller now calls `self->_dialogs.Clear()`
// directly (see `UiDtorBody`'s phase 4).

// Slot 0 of any Object::Base-derived vtable is the scalar deleting destructor;
// retail calls it with EDX = 1 ("destruct AND free"). ENDGAME B.3.6: that is
// `delete dlg` on the real `UI::DialogInstance` — a VIRTUAL call, dispatched
// through the dialog's own (minted or exe) table.

// ---- the NEGATIVE CONTROL knob ---------------------------------------------
// `BANISHED_UIF_NEGCTRL=<n>` deliberately breaks ONE thing in one of this
// lane's three ports, so that the three twin oracles can be shown to CATCH a
// defect rather than merely to report zero. A green oracle that has never
// failed is an untested instrument, and this project has paid for that twice
// (reference_probe_needs_an_effectiveness_control.md, and
// reference_control_must_pick_a_compared_subject.md on choosing a subject the
// comparison actually looks at — each of the three below is chosen inside the
// compared extent, not beside it).
//
//   1  ~UserInterface        ignore the dialog's flag bit 5, so the dialog a
//                            nested ShowDialog UI owns is destroyed anyway.
//                            Must show up in the twin's ORDERED LOG.
//   2  CreateInstance        (src/engine/ui/dialog.cpp) drop the _id/_tabOrder
//                            crossover. Must show up in the BYTE compare.
//   3  SaveAllSettings       skip the cursor advance past the 0xdeadbeef magic.
//                            Must show up in the SETTINGS BUFFER compare.
//
// Unset (the normal case) every one of these is a compile-time-visible no-op
// branch on a value read once.
// Defined below at file scope so src/engine/ui/dialog.cpp's CreateInstance can
// use the same knob for control 2.
// The body, factored out so the twin oracle can run OUR implementation on a
// synthetic subject without re-entering the diff wrapper.
void UiDtorBody(UserInterface* self) {
    using Node = Collection::LinkedList<UI::DialogInstancePtr>::Node;
    auto& list = self->_dialogs;

    // ---- phase 1 ----------------------------------------------------------
    if (self->_removedDialogs._size > 0) {
        int32_t i = 0;
        do {
            Node* const head = list._head;
            Node* node = head;
            if (node) {
                void* want = self->_removedDialogs._data[i];
                for (;;) {
                    if (node->element == want) {
                        if (node == head)        list._head = node->next;
                        if (node == list._tail)  list._tail = node->prev;
                        if (Node* prev = node->prev) prev->next = node->next;
                        if (Node* next = node->next) next->prev = node->prev;
                        node->prev = nullptr;
                        node->next = nullptr;
                        --list._count;
                        UiMemFree(node);
                        break;
                    }
                    node = node->next;
                    if (!node) break;
                }
            }

            // RE-READ, at 0x180120c74, after the free. See trap (b).
            UI::DialogInstance*& slot = self->_removedDialogs._data[i];
            UI::DialogInstance* const dlg = slot;
            // Trap (a): the flag word is read BEFORE the null test, as retail
            // does. Kept in that order deliberately.
            const uint32_t flags = dlg->_flags;
            if (((~static_cast<uint8_t>(flags >> 5)) & 1) != 0 && dlg != nullptr) {
                delete dlg;
                slot = nullptr;
            }

            ++i;
        } while (i < self->_removedDialogs._size);
    }

    // ---- phase 2 ----------------------------------------------------------
    for (Node* node = list._head; node; node = node->next) {
        UI::DialogInstance* const dlg = node->element;
        const uint32_t flags = dlg->_flags;
        if (((~static_cast<uint8_t>(flags >> 5)) & 1) != 0 && dlg != nullptr) {
            delete dlg;
            node->element = nullptr;
        }
    }
    // ---- phase 3 ----------------------------------------------------------
    // Retail's order is events, addDialogs, removedDialogs, and each pair of
    // stores is two QWORDs — the second covers `_size` AND `_capacity`.
    if (void* ev = self->_events._data) UiMemFree(ev);
    self->_events._data     = nullptr;
    self->_events._size     = 0;
    self->_events._capacity = 0;

    if (void* ad = self->_addDialogs._data) UiMemFree(ad);
    self->_addDialogs._data     = nullptr;
    self->_addDialogs._size     = 0;
    self->_addDialogs._capacity = 0;

    if (void* rm = self->_removedDialogs._data) UiMemFree(rm);
    self->_removedDialogs._data     = nullptr;
    self->_removedDialogs._size     = 0;
    self->_removedDialogs._capacity = 0;

    // ---- phase 4: the tail JMP -------------------------------------------
    // retail 0x1800325f0 — `self->_dialogs` is the real
    // `Collection::LinkedList<DialogInstancePtr>` member; `.Clear()` IS that
    // body (Collection.h's banner: it frees every node, then zeroes
    // head/tail/count).
    self->_dialogs.Clear();
}

// ============================================================================
// UI leftovers batch (docs/history/runtime_port/closure/WORKLIST.md) — the three
// remaining UserInterface frame bodies.
// ============================================================================

// Drop every pending `_events` entry (stride 0x30) whose dialog pointer at
// +0x00 matches `dlg`, shifting later entries down in place. Retail's own
// loop (0x18011f88a..0x18011f947) is a field-by-field copy of event[i+1] onto
// event[i] repeated in place rather than a single memmove, but the net effect
// over the whole run is identical to erasing every match and closing the gap,
// so it is written as one for clarity — the field layout it copies is exactly
// AssignEvent30's (array.cpp), which is the byte-level cross-check.
void RemoveEventsForDialog(Collection::Array<UI::Event>& events, void* dlg) {
    UI::Event* const data = events._data;
    int32_t& size = events._size;
    for (int32_t i = 0; i < size; ) {
        if (data[i].dialog == dlg) {
            for (int32_t j = i; j < size - 1; ++j) data[j] = data[j + 1];
            --size;
            // do not advance i -- re-check the slot the shift just filled
        } else {
            ++i;
        }
    }
}

// ?CleanRemovedDialogs@UserInterface@UI@@AEAAXXZ — retail 0x18011f790, ord 1704.
// For each dialog in `_removedDialogs` (+0x18, size +0x20): unlink it from
// `_dialogs` (same walk-and-splice UiDtorBody's phase 1 above already does,
// which is this same body's inlined shape one caller over), drop any pending
// `_events` entry that names it, then release it through its own vtable slot
// 0 UNLESS flag bit 5 says a nested ShowDialog UI owns it (the same bit
// UiDtorBody's phase 1 tests, same "read the flags before the null test"
// order retail uses). Finishes by clearing `_removedDialogs` to empty --
// `FUN_180079c90` (array.cpp's registration note), which is PROVEN to reduce
// to `_size = 0` and nothing else: its fill loop's bound folds to `< 0` for a
// compile-time-constant new size of 0, which is never true for a
// non-negative old size, so the loop, the realloc arm and the free arm are
// all unreachable.
} // namespace

void UI::UserInterface::CleanRemovedDialogs() {
    using Node = Collection::LinkedList<DialogInstancePtr>::Node;
    // The bound is re-read every iteration (0x18011f951 `CMP EBP,[RDI+0x20]`):
    // a dialog destructor below may queue another dialog (CloseDialog's
    // PushRemovedUnlessPresent), and retail unlinks and deletes it in this
    // same drain.
    for (int32_t i = 0; i < _removedDialogs._size; ++i) {
        void* want = _removedDialogs._data[i];

        for (Node* node = _dialogs._head; node; node = node->next) {
            if (node->element != want) continue;
            if (node == _dialogs._head) _dialogs._head = node->next;
            if (node == _dialogs._tail) _dialogs._tail = node->prev;
            if (Node* prev = node->prev) prev->next = node->next;
            if (Node* next = node->next) next->prev = node->prev;
            node->prev = nullptr;
            node->next = nullptr;
            --_dialogs._count;
            UiMemFree(node);
            break;
        }

        RemoveEventsForDialog(_events, want);

        // (A null `_data` here faults on the element load exactly as the
        // tree's `*(nullptr)` read did.)
        DialogInstance* const dlg = _removedDialogs._data[i];
        const uint32_t flags = dlg->_flags;   // trap (a): read before the null test
        if (((~static_cast<uint8_t>(flags >> 5)) & 1) != 0 && dlg != nullptr) {
            delete dlg;
            // NOT RETAIL: retail stores the null through the base it loaded
            // BEFORE the delete (0x18011f920 `MOV RBX,[RDI+0x18]`, 0x18011f947
            // `MOV [RBX+R14],R15`). When the destructor's queued dialog grew
            // the array, that is a write into the freed buffer (use after
            // free); the base is re-read here so the store lands in the live one.
            _removedDialogs._data[i] = nullptr;
        }
    }

    _removedDialogs._size = 0;   // FUN_180079c90 -- see banner
}

// ?UpdateEvents@UserInterface@UI@@QEAAXXZ — retail 0x18011fc60, ord 4515.
// Re-entrancy guarded by +0x88 (set for the duration of the drain, exactly
// like retail). Dispatches each queued event through its own dialog's
// OnElementEvent (vtable slot 8/+0x40 -- src/engine/ui/dialog.cpp's
// RtUiDialogInstance_OnElementEvent is that slot's body), handing it a FRESH
// stack copy of the event's +0x08 field by address on every iteration (the
// same "controller must not see the caller's own storage" contract
// RtUiDialogInstance_OnElementEvent's own banner states for its element
// argument). Finishes by clearing `_events` -- `FUN_180079df0`, proven the
// same way `FUN_180079c90` is above: the dead-branch reasoning is identical,
// just at stride 0x30 instead of 8.
void UI::UserInterface::UpdateEvents() {
    if (_dragDropActive != 0) return;
    _dragDropActive = 1;

    // The base and the bound are re-read every iteration (0x18011fc90
    // `MOV RDX,[RBX+0x48]`, 0x18011fcdb `CMP EDI,[RBX+0x50]`): a handler may
    // post an event (CloseDialog's drag-cancel arm), which can grow the array,
    // and retail delivers it in this same drain. The record is copied out
    // before the call, as retail loads every field before it.
    for (int32_t i = 0; i < _events._size; ++i) {
        const Event ev = _events._data[i];
        // The fresh stack copy of the event's element, passed BY ADDRESS
        // (RULE B6-11: slot 8's InternalPtr<Element> argument is a hidden
        // pointer to the caller's temporary).
        Object::InternalPtr<UI::Element> elem(ev.element);
        // DialogInstance vtable slot 8 (+0x40) `OnElementEvent` — VIRTUAL, so
        // an exe dialog's hand-built slot 8 is what runs.
        ev.dialog->OnElementEvent(&elem,
            ev.eventId, ev.arg14, ev.arg18, ev.arg1C, ev.arg20, ev.userData);
    }

    _events._size = 0;   // FUN_180079df0 -- see banner
    _dragDropActive = 0;
}

// ?GetMouseCursorIndex@UserInterface@UI@@QEBAHHH@Z — retail 0x18011fa40, ord
// 2800. Walks `_dialogs` back-to-front (from _tail, +0x08, via each node's
// `prev` at +0x08 -- the same front-to-back convention UserInterface.h
// documents for BringDialogToFront/GetFirstContainedDialog), returns 0 if
// none contains (x,y), otherwise defers to that dialog's OWN
// GetMouseCursorIndex starting from its root Element.
int32_t UI::UserInterface::GetMouseCursorIndex(int32_t x, int32_t y) const {
    for (const auto* node = _dialogs._tail; node; node = node->prev) {
        DialogInstance* const dlg = node->element;
        const uint32_t flags = dlg->_flags;
        if ((flags >> 2) & 1) continue;
        // `_root` (+0x50) through the dialog's own accessor (0x180045580, one
        // load): DialogInstance.h still spells the field `void*`.
        const Object::InternalPtr<UI::Element> root = dlg->GetRoot();
        bool hit;
        if ((flags >> 3) & 1) {
            // Element vtable slot 9 (+0x48) `Contains` — VIRTUAL.
            // B.4 LEFT: `DialogInstance::_ui` (+0x10) is `void*` (R-UI-a).
            hit = root->Contains(x, y, dlg->_ui);
        } else {
            const int32_t left   = root->_layoutLeft;     // +0x68
            const int32_t right  = root->_layoutRight;    // +0x70
            const int32_t top    = root->_layoutTop;      // +0x6c
            const int32_t bottom = root->_layoutBottom;   // +0x74
            hit = left <= x && x <= right && top <= y && y <= bottom;
        }
        if (hit) {
            if (!dlg) return 0;
            // The dialog's own `GetMouseCursorIndex(InternalPtr<Element>, x, y)`
            // (0x180114010), a non-virtual member of the real class now.
            return dlg->GetMouseCursorIndex(dlg->GetRoot(), x, y);
        }
    }
    return 0;
}

extern "C" void RtUi_UserInterface_Dtor(UserInterface* self) {
    UiDtorBody(self);
}

// ============================================================================
// WAVE 10 LANE UIF — ?SaveAllSettings@UserInterface@UI@@QEAAXXZ
//                                                 0x1801204b0  ord 3608
// ============================================================================
//
// "Serialize every open dialog's settings into that dialog's OWN settings
// buffer." It is a PURE SIDE EFFECT: it returns void, it touches no field of
// the UserInterface, and everything it does lands in memory somebody else owns.
// reference_port_must_reproduce_side_effects.md is the whole risk here, and it
// is why the oracle below diffs the BUFFERS and not the object.
//
// WHY IT WAS BLOCKED AND WHY IT IS NOT NOW. Wave 7 lane C's one-line reason was
// "Builds an IO::MemoryStream and serialises every dialog's settings.
// Allocates, frees, and writes persisted state." Two of those three are the
// allocator NO-GO, retired by Wave 10 lane ALLOC. The third — "writes persisted
// state" — turned out to be the thing that MAKES it verifiable rather than the
// thing that blocks it: nothing here reaches a file. `SaveAllSettings` writes
// into `DialogInstance::_userData` (+0xa8), a caller-owned byte buffer whose
// length is `+0xb0`. A buffer can be snapshotted, restored and re-written; a
// file handle cannot. reference_write_path_serialize_is_replayable.md, for the
// fifth time in this project.
//
// ---------------------------------------------------------------------------
// THE TWO FIELDS AT +0xa8/+0xb0 ARE NOT WHAT DialogInstance.h CALLS THEM
// ---------------------------------------------------------------------------
// src/engine/ui/DialogInstance.h names them `_userData` (void*) and
// `_userFlags` (int) — correctly, from the CONSTRUCTOR, where they are
// parameters 5 and 6. This body reads them as `(settingsBuffer, byteCount)`:
//
//     1801204f8  CMP qword ptr [R14 + 0xa8],0x0   ; skip the dialog if null
//     1801205ae  MOV EAX,dword ptr [R14 + 0xb0]   ; -> MemoryStream::size
//     1801205b5  MOV RDX,qword ptr [R14 + 0xa8]   ; -> MemoryStream::data
//     1801205d4  MOV dword ptr [RDX],0xdeadbeef   ; a MAGIC written INTO it
//
// Both readings are of the same two words. This port does not rename lane B's
// fields — it reads them at their offsets and records here what this caller
// means by them, which is the honest form when two callers disagree.
//
// ---------------------------------------------------------------------------
// THE BODY, from the disassembly at 0x1801204b0 (0x2fb bytes). Per dialog in
// `_dialogs`, skipping any whose +0xa8 is null:
//
//   1. Construct an `IO::MemoryStream` ON THE STACK, inlined. Its layout is
//      already recovered and static_assert'ed in src/engine/io/Stream.h and
//      every offset this body uses agrees with it field for field:
//        vptr = ??_7Stream@IO@@6B@ (0x1801549a0); direction = 0; encoding = 0;
//        vptr = ??_7MemoryStream@IO@@6B@ (0x1801555b0); data = 0; ownsData = 0;
//        cursor:size = 0 (one qword covering +0x24 and +0x28).
//   2. Allocate an 8-byte `IO::Encoding` (`Allocate(_singleton, 8, 4)` under the
//      allocator's critical section) and stamp the SHARED, UNEXPORTED encoding
//      vtable 0x1801554a8 into it. If the allocation fails retail carries on
//      with a NULL encoding — reproduced. The old `stream.encoding` is freed
//      first (always null here; the store order is retail's).
//   3. `data = dialog->+0xa8; size = dialog->+0xb0; cursor = 0; direction = 1`.
//      Then, IF `size >= 4`, write the literal `0xdeadbeef` at buffer offset 0
//      and advance `cursor` to 4. The magic is the settings blob's header.
//   4. Build a stack `IO::Serializer` {&stream, nullptr} and ask the encoding
//      for a ReaderWriter: slot +0x20 (`CreateWriter`) when `direction != 0`,
//      slot +0x18 (`CreateReader`) otherwise. `direction` is 1 by step 3, so
//      the reader arm is DEAD in this function — transcribed anyway, because
//      the branch is in the shipped bytes (0x1801205f4).
//   5. `dialog->vtbl[+0x48](dialog, &stream)` — the dialog's own
//      OnSerializeSettings, taking the raw Stream.
//   6. `count = dialog->+0x80` (the `_controllers` array size), serialized
//      through `rw->vtbl[+0xa0]` (`ScalarInt`, src/engine/io/Serializer.h's
//      `rw_vtbl` table). Then for each controller in `dialog->+0x78`:
//        id = ctl->vtbl[+0x10](ctl);        rw->vtbl[+0x98](rw, &id, nullptr);
//        a SECOND, throw-away ReaderWriter over the SAME stream;
//        ctl->vtbl[+0x50](ctl, &stream);    then delete that ReaderWriter.
//      The controller pointer is RE-READ from +0x78 after the ReaderWriter is
//      created (0x1801206a1), not cached across it — transcribed.
//   7. Delete the top-level ReaderWriter, then run the inlined
//      `~MemoryStream`: `if (ownsData && data) MemFree(data)` — DEAD here,
//      `ownsData` is never set — and `if (encoding) MemFree(encoding)`.
//
// EVERY CALLEE IS SATISFIED — the admission test:
//   * `MemAlloc`/`MemFree` (0x1800c7a50 / 0x1800c7ad0)  — ported, lane Q, and
//     they are the out-of-line spelling of retail's four inlined
//     lock+Allocate/Free sequences (the same substitution `??1DialogInstance@`
//     makes in src/engine/ui/dialog.cpp).
//   * `??_7Stream@IO@@6B@` / `??_7MemoryStream@IO@@6B@` — both EXPORTED data
//     rows, taken out of the DLL's own table rather than minted.
//   * the shared encoding vtable 0x1801554a8 — UNEXPORTED, so the address door
//     is the only one, exactly as src/engine/io/compressedstream.cpp:800
//     already reaches it; lane C2 mints that row by address.
//   * encoding slots +0x18/+0x20 (`CreateReader`/`CreateWriter`, 0x1800a8120 /
//     0x1800a8080) — both PORTED in src/engine/io/encoding.cpp.
//   * the dialog's +0x48 and each controller's +0x10/+0x50/+0x00 — virtuals on
//     objects we do not own, unchanged by this port either way.
// ---------------------------------------------------------------------------

// B6 row 371: the two `IO::Serializer` records (the outer one, then one per
// controller) are real `IO::Serializer` locals. Retail inlines ??0Serializer
// (0x1800b9960) with NO test on the encoding: 0x1801205f6 / 0x18012068c load
// the encoding's table and call slot +0x20 / +0x18 unguarded, and every
// ReaderWriter call (0x180120622 +0xa0, 0x180120661 +0x98) is unguarded; only
// the two deletes test the pointer (0x1801206b8, 0x1801206da: ??1Serializer
// 0x1800b99b0's shape).

void UI::UserInterface::SaveAllSettings() {
    auto* node = _dialogs._head;
    if (!node) return;

    do {
        DialogInstance* const dlg = node->element;
        if (dlg->_userData != nullptr) {
            // ---- 1+2. the inlined IO::MemoryStream and its 8-byte encoding -
            // ENDGAME B.3.9 IO (foreign site, IO's class): retail's inlined
            // ??0MemoryStream is the class's real constructor -- the Stream
            // base, the field stores, MemAlloc(8,4) + the Binary encoding (null
            // on a failed allocation), the dead free of the previous one.
            alignas(IO::MemoryStream) unsigned char stMem[sizeof(IO::MemoryStream)];
            IO::MemoryStream* const st = ::new (static_cast<void*>(stMem)) IO::MemoryStream();

            // ---- 3. point the stream at the dialog's settings buffer ------
            const int32_t size = dlg->_userFlags;   // +0xb0, read here as the byte count
            void*         buf  = dlg->_userData;    // +0xa8, the settings buffer
            st->_data      = buf;
            st->_size      = static_cast<uint32_t>(size);
            st->_cursor    = 0;
            st->_direction = 1;
            if (size >= 4) {
                *reinterpret_cast<uint32_t*>(buf) = 0xdeadbeefu;  // CAST-OK: byte_io
                st->_cursor += 4;
            }

            {
                // ---- 4. the top-level ReaderWriter ----------------------------
                // ??0Serializer inlined (0x1801205e1..0x180120607): the stream, a
                // null ReaderWriter, then encoding slot 4 (+0x20) CreateWriter when
                // writing, slot 3 (+0x18) CreateReader when reading; version 0.
                IO::Serializer ser(*st, 0);

                // ---- 5. the dialog's own OnSerializeSettings ------------------
                // DialogInstance vtable slot 9 (+0x48) — VIRTUAL.
                dlg->OnSerializeSettings(*st);

                // ---- 6. the controller loop -----------------------------------
                int32_t count = dlg->_controllers._size;
                ser.Serialize(count, nullptr);                                     // +0xa0
                for (int32_t i = 0; i < count; ++i) {
                    DialogController* ctl = dlg->_controllers._data[i];
                    // DialogController vtable slot 2 (+0x10) `Type()` — VIRTUAL.
                    uint32_t id  = ctl->Type();
                    ser.Serialize(id, nullptr);                                    // +0x98

                    // The per-controller Serializer (0x180120675..0x18012069d).
                    IO::Serializer ser2(*st, 0);

                    // RE-READ at 0x1801206a1, not cached across the CreateWriter.
                    ctl = dlg->_controllers._data[i];
                    // DialogController vtable slot 10 (+0x50) `OnSerializeSettings`
                    // (B6-1.2: one slot LATER than DialogInstance's) — VIRTUAL.
                    ctl->OnSerializeSettings(*st);
                    // ~Serializer: delete the ReaderWriter (0x1801206b4..0x1801206c5).
                }

                // ---- 7. teardown ----------------------------------------------
                // ~Serializer: delete the outer ReaderWriter (0x1801206d6..0x1801206e7),
                // before the stream's own teardown.
            }
            // ~MemoryStream (dead buffer free: never owns) then ~Stream (the
            // encoding): the class's destructor chain, no free of the storage.
            st->IO::MemoryStream::~MemoryStream();
        }
        node = node->next;
    } while (node);
}
