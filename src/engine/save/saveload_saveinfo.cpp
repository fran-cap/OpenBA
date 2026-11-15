// ============================================================================
// src/engine/save/saveload_saveinfo.cpp — Wave 24 lane INTERNAL8
// ============================================================================
//
// Two of the eight real bodies inside the census's 45 `internal` UNKNOWN rows
// (WAVE23_LANE_INTERNAL.md §4B/B4):
//
//   0x1800c3db0   the qsort comparator UpdateList sorts the save list with
//   0x1800c3de0   Save::SaveLoad::SaveInfo::SetThumbnail(row, px, w, h)
//
// Both were already fully DESCRIBED by our own tree — saveload_update.cpp's
// "THE UpdateList DEPENDENCY SET" banner transcribes the comparator byte for
// byte and names SetThumbnail's three stores — and both were still address
// doors into the shipped DLL. That is the whole of the `internal` mystery: a
// row printed UNKNOWN because the census reads names and a comment is not a
// name.
//
// EXTENTS, AND HOW THEY WERE BOUNDED
// ---------------------------------------------------------------------------
//   0x1800c3db0   34 bytes, 0x1800c3db0..0x1800c3dd1, then 0xCC padding.
//   0x1800c3de0  332 bytes, 0x1800c3de0..0x1800c3f2b (tail JMP).
//
// NEITHER came from `.pdata`, and for the comparator `.pdata` could not have
// answered at all. 52.9% of this DLL's `.text` exports have NO `.pdata` entry
// (leaf functions) and this one is a leaf with no unwind data and no Ghidra
// function object either. Its extent is read out of the IMAGE BYTES: 34 bytes
// of instructions terminating in `C3`, followed by a run of `CC`. SetThumbnail
// is bounded by Ghidra's function body, which ends at the tail
// `JMP qword ptr [0x18014b388]` (LeaveCriticalSection).
//
// `.pdata` UNDERSTATES an extent — MSVC chains fragments through
// UNW_FLAG_CHAININFO, and the ENTRY fragment is the one that reads
// `chained=False`, so a chained function looks complete when looked up by its
// own address (a 20.4x understatement was measured on a row in this same
// bucket). Understating makes work look cheap and truncates a port silently.
// Neither body here is chained, but the bound is taken from an instrument that
// cannot understate rather than from one that can.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "save/SaveLoad.h"
#include "video/ImageBuffer.h"
#include "video/Device.h"   // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "saveload_internal.h"
#include "system/MemoryAllocator.h"

using RtSave::SaveInfo;

// Video::Device's recursive render lock, already ported in
// src/engine/video/buffers_lock.cpp and declared in video/Device.h (B.4
// R-VIDEO: it takes the typed singleton cell).

namespace {

// ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z, 0x1800c7a50.
//
// SetThumbnail does NOT call this symbol — it OPEN-CODES the triple
// `EnterCriticalSection(*0x1801fc718 + 0x640); Allocate(*0x1801fc718, 0x68, 8);
// LeaveCriticalSection(...)`. Lane Q disassembled 0x1800c7a50 and proved that
// triple IS its body, instruction for instruction; array.cpp:200 and
// message_callbacks.cpp:82 already reach the allocator this way for exactly
// the same open-coded shape. Calling the composed symbol keeps this file free
// of <windows.h> (PLAN §4.1) at the cost of nothing observable.
//
// B3R RT-IO: the static member by name; the atomic by-address slot (and its
// readiness test, always true: 0x1800c7a50 has always been registered) is gone.
void* SaveInfoMemAlloc(uint64_t bytes, int align) {
    return ::System::MemoryAllocator::MemAlloc(bytes, align);
}

// ??0ImageBuffer@Video@@QEAA@XZ, 0x1800f2580. ENDGAME B.3.9 VS: `::new (p) Video::ImageBuffer()` -- the ??0 door and the minted row are retired
// (the by-address door lookup and its atomic slot went with it). Called
// inline in SetThumbnail below.

// ?_singleton@Device@Video@@0PEAV12@EA, 0x1801fe7a8 — the SLOT, not the Device.
// ENDGAME B.3.9 SS1 (B39_RULES D7): the class static's address IS this cell.
Video::Device** DeviceSingletonSlot() {
    return &::Video::Device::_singleton;
}

// ---------------------------------------------------------------------------
// 0x1800c3db0 as a plain function; `SaveInfo::Compare` below forwards to it.
//
// The bytes, read out of the image (Ghidra has no function object here):
//
//     83 79 40 00  CMP dword ptr [RCX+0x40],0x0
//     7c 16        JL  .greater             ; a.saveIndex < 0 -> return 1
//     83 7a 40 00  CMP dword ptr [RDX+0x40],0x0
//     7c 10        JL  .greater             ; b.saveIndex < 0 -> return 1
//     48 8b 41 30  MOV RAX,[RCX+0x30]       ; a.sortKey
//     48 8b 4a 30  MOV RCX,[RDX+0x30]       ; b.sortKey
//     48 3b c8     CMP RCX,RAX              ; UNSIGNED, b vs a
//     77 03        JA  .greater
//     1b c0        SBB EAX,EAX              ; CF set iff b<a  ->  -1 ; else 0
//     c3           RET
//   .greater:
//     b8 01 00 00 00 / c3                   ; return 1
//
// THREE THINGS A "TIDIER" SPELLING GETS WRONG, all of them observable:
//
//  1. The compare is UNSIGNED (`JA`, and `SBB` on CF). sortKey is a uint64 and
//     the top bit is live in a FILETIME-shaped value; a signed compare would
//     reverse the order of any pair straddling 2^63.
//  2. It is NOT A TOTAL ORDER. A row with saveIndex < 0 compares GREATER in
//     BOTH positions — cmp(blank, x) == 1 and cmp(x, blank) == 1 — so a blank
//     row and any other row are each "after" the other. That is a genuine
//     violation of the strict weak ordering qsort requires, and it is retail's
//     behaviour. UpdateList gets away with it because the only such row is the
//     "new save" slot and state 1 deliberately sorts the sub-range that
//     EXCLUDES it (the base+0x58 / count-1 shift in saveload_update.cpp). The
//     comparator is reproduced as written, violation included; "fixing" it
//     would change the row order the player sees on any input where retail's
//     inconsistency actually fires.
//  3. Ordering is DESCENDING by sortKey (newest first), because the compare is
//     `b vs a` and not `a vs b`.
// ---------------------------------------------------------------------------
// The FILETIME's 64-bit tick count: retail reads the DateTime as one qword.
static inline uint64_t FileTimeTicks(const System::DateTime& t) {
    return (static_cast<uint64_t>(t._highDateTime) << 32) | t._lowDateTime;
}

int SaveInfoCompareImpl(const void* a, const void* b) {
    // qsort hands the comparator its elements as `const void*`.
    const SaveInfo* pa = static_cast<const SaveInfo*>(a);  // CAST-OK: crt
    const SaveInfo* pb = static_cast<const SaveInfo*>(b);  // CAST-OK: crt

    if (pa->saveIndex < 0) return 1;
    if (pb->saveIndex < 0) return 1;

    const uint64_t ka = FileTimeTicks(pa->sortKey);   // MOV RAX,[RCX+0x30]
    const uint64_t kb = FileTimeTicks(pb->sortKey);   // MOV RCX,[RDX+0x30]
    if (kb > ka) return 1;     // JA  — UNSIGNED
    if (kb < ka) return -1;    // SBB EAX,EAX with CF set
    return 0;                  // SBB EAX,EAX with CF clear
}

}  // namespace

// ============================================================================
// 0x1800c3db0 — the qsort comparator. PURE.
// ============================================================================
// It is passed to qsort as DATA, so retail never "calls" it from a site we can
// wrap.
int Save::SaveLoad::SaveInfo::Compare(const void* a, const void* b) {
    return SaveInfoCompareImpl(a, b);
}

// ============================================================================
// 0x1800c3de0 — SaveInfo::SetThumbnail(row, pixels, w, h)
// ----------------------------------------------------------------------------
// void (SaveInfo* row /*RCX*/, void* pixels /*RDX*/, int w /*R8D*/, int h /*R9D*/)
//
// Retail, in order:
//   1. allocate 0x68 bytes / align 8 under the ALLOCATOR's lock and
//      placement-construct a Video::ImageBuffer in it. 0x68 is exactly
//      sizeof(Video::ImageBuffer) as this tree's header already asserts, which
//      is an independent corroboration of the layout and not a coincidence.
//   2. publish it at row+0x18 (`imageBuffer`) and call vtable slot 9,
//      ?Create@ImageBuffer@Video@@ — eleven arguments, four in registers and
//      seven on the stack.
//   3. under VIDEO DEVICE's lock (a different lock from step 1), call the
//      ImageBufferData at +0x60 through ITS vtable slot 5 with (pixels, w, h).
//   4. store pixels/w/h at row+0x28/+0x20/+0x24 — the three stores that are the
//      independent confirmation of this header's thumbPixels/Width/Height
//      names, since OnVideoRestart reads the same three back as upload args.
//
// THE CTOR'S RETURN VALUE. Retail does
// `RDI = alloc; if (RDI) { CALL ctor; RDI = RAX; }` — an MSVC constructor
// returns `this` in RAX, so RAX == RDI and the move is a no-op. Here the
// placement-new expression yields `mem` itself, so the two are provably the
// same value.
//
// Retail does NOT null-check `row`, `row->imageBuffer` after Create, or the
// ImageBufferData at +0x60, and neither does this. The one `if` it does have —
// `TEST RDI,RDI` after the allocation — is reproduced, and note what it guards:
// on an allocation failure retail skips only the CONSTRUCTOR and then stores
// null at row+0x18 and DEREFERENCES IT one instruction later at
// `MOV RAX,[RDI]`. That is a fault on OOM in the shipped binary. Transcribed
// as written (CLAUDE.md: do not "fix" a faithful crash).
// ============================================================================
void Save::SaveLoad::SaveInfo::SetThumbnail(void* pixels, int32_t w, int32_t h) {
    void* mem = SaveInfoMemAlloc(0x68, 8);
    // `if (RDI) { ctor; RDI = RAX; }`: the constructed object, or the null the
    // allocator returned.
    Video::ImageBuffer* img = (mem != nullptr) ? ::new (mem) Video::ImageBuffer() : nullptr;

    imageBuffer = img;

    // vtable slot 9 (+0x48):
    //   ?Create@ImageBuffer@Video@@UEAA_N W4ImageType W4Usage W4Format I H H
    //                                     W4FilterMode W4AddressMode 4 4
    //                                     PEAV?$Array@V?$Array@E@Collection@@@
    // Argument values are immediates in the shipped body and are transcribed,
    // not interpreted: type=0, usage=2, format=5, 0, w, h, filter=0,
    // addressU=addressV=addressW=1, no initial data.
    // ENDGAME B.3.5: `img` is a PROVED `Video::ImageBuffer` — it is the 0x68-byte
    // allocation the ImageBuffer constructor (0x1800f2580) just built above,
    // so the receiver is known from the code that produced the pointer and not
    // from the variable's name. Slot 9 is therefore the virtual, called as one.
    img->Create(0, 2, 5, 0u, w, h, 0, 1, 1, 1, nullptr);

    // ---- the VIDEO DEVICE lock, open-coded in retail ----------------------
    // 0x1800c3eb4..0x1800c3eca is EnterCriticalSection(dev+0x60) /
    // GetCurrentThreadId / [dev+0x88]=tid / INC [dev+0x8c] — the OWNER-FIRST
    // inline copy, and the unlock at 0x1800c3eee RE-READS the singleton before
    // decrementing. That is exactly RtVideoDeviceLock/Unlock, which take the
    // SLOT for that reason. This is a different lock from the allocator's, and
    // running the two in the wrong nesting would be a deadlock, not a style
    // difference — the allocator's is already released by step 1.
    Video::Device** devSlot = DeviceSingletonSlot();
    RtVideoDeviceLock(devSlot);

    // ImageBufferData vtable slot 5 (+0x28) — the pixel upload. PURECALL in
    // the base Video table; the live body is the DX11 override.
    // `img->_data` is the companion object the ctor/`Create` pair built
    // for THIS ImageBuffer, so the receiver is proved. Slot 5 of the abstract
    // `Video::ImageBufferData` is `bool Update(void*, int, int)`
    // (CompanionData.h); retail ignores the result and so does this. The
    // CONCRETE `*Data` subclass and the dx11 seam are a declared leave for step 5
    // (B35_RULES 1.1 item 4 / section 11) — only the call converts.
    img->_data->Update(pixels, w, h);                  // +0x60

    thumbPixels = pixels;   // +0x28
    thumbWidth  = w;        // +0x20
    thumbHeight = h;        // +0x24

    RtVideoDeviceUnlock(devSlot);

    // The Create arguments are verified by TRANSCRIPTION against the
    // immediates in the listing.
}

// (B.4 R-IO: `SweepRow` / `MakeRow`, the retired sweep's row builder, had no
// caller and are deleted.)
