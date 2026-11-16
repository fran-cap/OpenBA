// ============================================================================
// src/engine/ui/userinterface_dragstart.cpp — Wave 10E lane DRAG
// ============================================================================
//
//   0x18011f5f0  ?BeginDragDrop@UserInterface@UI@@QEAAX
//                    V?$InternalPtr@VDialogInstance@UI@@@Object@@
//                    V?$InternalPtr@VElement@UI@@@4@HHIPEAX@Z
//
// ---------------------------------------------------------------------------
// THE LISTING, RE-DERIVED RATHER THAN INHERITED
// ---------------------------------------------------------------------------
// Wave 10D shipped two lane docs whose instruction COUNTS were wrong in 4/4 and
// 2/2 checked cells while every byte size beside them was correct, and lane
// MOUSE's own published count for THIS function was ELEVEN. It is FIFTEEN.
// The surrounding analysis in WAVE10D_LANE_MOUSE.md §6 — the field map, the
// read-only element, the tail-JMP target — was right in every particular and is
// confirmed below; only the number summarising it was wrong.
//
// Counted with an instrument, not by eye (objdump over the shipped
// Runtime-x64.dll, `grep -c` on the address column, twice, = 15). The body runs
// 0x18011f5f0..0x18011f62e inclusive, 63 bytes, and 0x18011f62f begins INT3
// padding — so the tail JMP is the last instruction and there is no second
// basic block. There are NO branches: fifteen straight-line instructions, one
// exit. That is why this port has no `if` in it.
//
//     18011f5f0:  48 8b 02              mov    (%rdx),%rax          ; *dlgPtr
//     18011f5f3:  8b 54 24 28           mov    0x28(%rsp),%edx      ; arg5 y
//     18011f5f7:  48 89 41 60           mov    %rax,0x60(%rcx)      ; _dragDropDialog
//     18011f5fb:  8b 44 24 30           mov    0x30(%rsp),%eax      ; arg6 id
//     18011f5ff:  89 41 70              mov    %eax,0x70(%rcx)      ; _dragDropId
//     18011f602:  48 8b 44 24 38        mov    0x38(%rsp),%rax      ; arg7 data
//     18011f607:  48 89 41 78           mov    %rax,0x78(%rcx)      ; _dragDropData
//     18011f60b:  49 8b 00              mov    (%r8),%rax           ; *elemPtr
//     18011f60e:  44 2b 48 68           sub    0x68(%rax),%r9d      ; x -= _layoutLeft
//     18011f612:  44 89 89 80 00 00 00  mov    %r9d,0x80(%rcx)      ; grab offset X
//     18011f619:  49 8b 00              mov    (%r8),%rax           ; *elemPtr AGAIN
//     18011f61c:  2b 50 6c              sub    0x6c(%rax),%edx      ; y -= _layoutTop
//     18011f61f:  89 91 84 00 00 00     mov    %edx,0x84(%rcx)      ; grab offset Y
//     18011f625:  ba 02 00 00 00        mov    $0x2,%edx            ; sound id 2
//     18011f62a:  e9 a1 2d 00 00        jmp    0x1801223d0          ; tail: Play(this, 2)
//
// ARGUMENTS. `HHIPEAX` after the two InternalPtr<> classes is (int, int,
// unsigned int, void*), so seven parameters counting `this`. Both InternalPtr<>
// go BY HIDDEN REFERENCE — retail dereferences RDX and R8 rather than using
// them — which is why the signature takes a pointer to each InternalPtr<>. With
// four in registers, arg5 lands at [rsp+0x28] (return address at +0x00, shadow
// space +0x08..+0x27), which is exactly what the listing loads.
//
// ---------------------------------------------------------------------------
// FOUR THINGS A TIDY REWRITE WOULD GET WRONG, all kept
// ---------------------------------------------------------------------------
//  1. THE STORE ORDER IS LOAD-BEARING. +0x60, +0x70 and +0x78 are all written
//     BEFORE the first `*elemPtr` deref at 0x18011f60b. A null element
//     therefore faults at 0x18011f60e with the dialog, id and data ALREADY
//     COMMITTED — which is precisely the crash lane MOUSE hit and correctly
//     refused to paper over. CLAUDE.md is explicit: a guard here would not
//     restore the behaviour, it would skip it. There is no null check in this
//     file, and the field writes stay in retail's order so that a future null
//     leaves the same partial state retail leaves.
//
//  2. `*elemPtr` IS RELOADED. 0x18011f60b and 0x18011f619 are two separate
//     loads of the same InternalPtr. Nothing writes it in between (both stores
//     target `self`), so the two reads cannot disagree and this is a compiler
//     artefact rather than a semantic one — but it is reproduced as two reads
//     rather than folded into one local, because "it cannot differ" is an
//     argument about the code as shipped, not a licence to change its shape.
//
//  3. THE ARITHMETIC IS 32-BIT. `SUB 0x68(%rax),%r9d` is a 32-bit signed
//     subtract, not a MOVSXD-widened one — and Ghidra's C output is exactly
//     where that distinction gets lost: it prints MOVSXD as a zero extension.
//     Read off the DISASSEMBLY above, not off decompiled C. Both
//     operands are int32_t here and the result wraps like retail's.
//
//  4. +0x80 IS TWO int32s, NOT A POINTER. UserInterface.h declares the pair
//     `_dragDropGrabX` / `_dragDropGrabY` at +0x080 / +0x084. Retail's writer —
//     this function — writes them as a separate pair of int32 GRAB OFFSETS (the
//     cursor's position within the dragged element), and EndDragDrop
//     (userinterface_hover.cpp) and Update (userinterface_frame.cpp) clear the
//     pair as one qword.
//
// THE TAIL CALL. `?Play@UserInterface@UI@@QEAAXH@Z` (0x1801223d0) is the
// member `Play` in src/engine/ui/userinterface.cpp, called directly.
//
// ---------------------------------------------------------------------------
// WHAT THIS BODY DOES NOT DO, AND WHY IT MATTERS DOWNSTREAM
// ---------------------------------------------------------------------------
// It NEVER WRITES `_dragDropElement` (+0x68). All fifteen instructions are
// accounted for above and not one of them touches +0x68. This is now settled
// from the complete listing rather than inferred from a crash, and it CONFIRMS
// lane MOUSE's reading: the element argument is strictly READ-ONLY (two loads,
// two field reads, no store), no ownership is taken, and handing this function
// the live hovered element destroys nothing.
//
// The consequence for EndDragDrop's destroy arm is therefore PERMANENT, not a
// gap in the injector: `EndDragDrop` destroys `_dragDropElement` through vtable
// slot 0, and BeginDragDrop cannot arm it because it does not write the field.
// See §"the open question" in docs/history/runtime_port/WAVE10E_LANE_DRAG.md for what
// this lane could and could not establish about which API does.
//
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/UserInterface.h"
#include "ui/Element.h"

namespace {

using UI::UserInterface;
using UI::Element;

// ---------------------------------------------------------------------------
// ENTRY COUNTERS, printed at exit by DragStartSummary's destructor.
//
// g_beginDragGrabOffset is the EFFECTIVENESS CONTROL. `entries` alone cannot
// distinguish "the elemPtr deref did real work" from "it subtracted zero and
// the grab offset is just the raw cursor position" — which is exactly what a
// null-ish or origin-anchored element would look like. This counts the entries
// where at least one of the two subtractions actually CHANGED the value, i.e.
// where the element had a non-zero layout origin and the +0x68/+0x6c reads
// mattered.
// ---------------------------------------------------------------------------
uint64_t g_beginDragEntries    = 0;
uint64_t g_beginDragGrabOffset = 0;

struct DragStartSummary {
    ~DragStartSummary() {
        printf("[RtPort] UI::UserInterface::BeginDragDrop  entries=%llu nonZeroGrabOffset=%llu\n",
               (unsigned long long)g_beginDragEntries,
               (unsigned long long)g_beginDragGrabOffset);
        fflush(stdout);
    }
};
DragStartSummary g_dragStartSummary;

}  // namespace

// ============================================================================
// ?BeginDragDrop@UserInterface@UI@@QEAAX...@Z               retail 0x18011f5f0
// ============================================================================
// ENDGAME B.3.6 block E: the body is the member; both InternalPtr arguments are
// the ADDRESSES of the caller's temporaries (RULE B6-11: `MOV RAX,[RDX]` at
// 0x18011f5f0 and `MOV RAX,[R8]` at 0x18011f60b), spelled `dialog`/`element`
// below.
void UI::UserInterface::BeginDragDrop(Object::InternalPtr<DialogInstance>* dialog,
                                      Object::InternalPtr<Element>* element,
                                      int32_t x, int32_t y, uint32_t id, void* data) {
    ++g_beginDragEntries;
    // Retail's order: the three plain fields commit BEFORE the element is
    // touched. See note 1 above -- this order is what a null element observes.
    _dragDropDialog = dialog->Get();                                   // 0x18011f5f0 / 0x18011f5f7
    _dragDropId     = id;                                              // 0x18011f5fb / 0x18011f5ff
    _dragDropData   = data;                                            // 0x18011f602 / 0x18011f607

    // +0x80 / +0x84, the pair of int32 grab offsets -- note 4 above.
    // Unguarded on purpose. Retail faults here on a null element and the crash
    // is load-bearing (CLAUDE.md; lane MOUSE recovered the whole field map from
    // exactly this fault). If this ever fires, the null came from upstream.
    const Element* e1 = element->Get();                                // 0x18011f60b
    const int32_t  gx = x - e1->_layoutLeft;                           // 0x18011f60e  32-bit SUB
    _dragDropGrabX = gx;                                               // 0x18011f612

    const Element* e2 = element->Get();                                // 0x18011f619  reloaded
    const int32_t  gy = y - e2->_layoutTop;                            // 0x18011f61c  32-bit SUB
    _dragDropGrabY = gy;                                               // 0x18011f61f

    if (gx != x || gy != y) ++g_beginDragGrabOffset;   // effectiveness control only

    // 0x18011f625 / 0x18011f62a -- MOV EDX,2 then tail-JMP to Play. Sound id 2
    // is the drag-start sound.
    Play(2);
}
