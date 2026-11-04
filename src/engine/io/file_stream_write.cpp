// ============================================================================
// src/engine/io/file_stream_write.cpp — Wave 19 lane FILE, post-report tranche
// The two IO::FileStream vtable slots that reach the OS file handle directly
// (src/engine/io/file_stream_write.cpp until Phase D.4 THREADS E.5; the two
// Win32 calls are Platform::FileWrite / FileSeek32 now, platform_file.h)
// ============================================================================
//
//   | Export                                    | Retail      | Ord  | Slot | Instrs |
//   |-------------------------------------------|-------------|------|------|--------|
//   | ?Write@FileStream@IO@@UEAA_NHPEBX@Z       | 0x1800aab90 | 4549 |  2   |  22    |
//   | ?SetCurrentPos@FileStream@IO@@UEAA_NH@Z   | 0x1800aac10 | 3856 |  4   |  15    |
//
// Ordinals printed out of the shipped export directory with `pefile`. THE FIRST
// DRAFT OF THIS TABLE CARRIED TWO INVENTED ORDINALS (4058 and 3916), written
// from nothing and corrected before the file was compiled. A number in a
// comment is not load-bearing for the code, which is exactly why nothing else
// would ever have caught it — recorded rather than silently fixed.
//
// slot bodies — the seven are one vtable row and an A/B that could only move
// some of them could not attribute anything).
//
// SPLIT OUT OF file_stream.cpp PURELY FOR THE PLATFORM SEAM (PLAN.md §4.1
// decision 1): these two call `WriteFile` and `SetFilePointer` and nothing else
// in the family does. Both were read out of Runtime-x64.dll's OWN import table
// with `pefile` — `0x18014b108 WriteFile`, `0x18014b0f0 SetFilePointer`.
//
// ---------------------------------------------------------------------------
// WHY THESE SEVEN SLOTS, AND WHY NOW
// ---------------------------------------------------------------------------
// `??_7FileStream@IO@@6B@` and `??_7BinaryFileStream@IO@@6B@` are minted as of
// Wave 19 phase 0b and BOTH trap on the SAME seven bodies: their slots 1..7 are
// BYTE-IDENTICAL (verified here by reading both tables' bytes, not inherited),
// and the two rows differ ONLY in slot 0's deleting-destructor thunk. So one
// body of work clears 8 + 8 = 16 trap slots across 9 distinct functions.
// **Scope a DATA row by FUNCTION, never by slot.**
//
// ---------------------------------------------------------------------------
// TWO SIGNATURE FACTS THE EXPORT TABLE SETTLES AND `FileStream.h` DOES NOT
// ---------------------------------------------------------------------------
// `FileStream.h` names these slots but not their C++ signatures, and two are
// not what the field types suggest:
//
//   * `?SetCurrentPos@FileStream@IO@@UEAA_NH@Z` returns **`_N`, a bool** — not
//     void. It reports whether the seek landed where it was asked.
//   * `?GetCurrentPos@` and `?GetSize@` are `UEBAIXZ` — they return **unsigned
//     int**, while `FileInfo::cursor` and `::size` are modelled as `int32_t`.
//     The one-instruction bodies are `MOV EAX,[RCX+0x2c]` / `[RCX+0x28]`, so
//     the bits are the same either way; the return TYPE is what a caller's
//     comparison sees, and a signed reading flips every test above 2GB.
//
// Read off the shipped export directory with `pefile`, not guessed from the
// accessor bodies, which cannot show a return type.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "platform_file.h"   // Platform::FileWrite / FileSeek32

namespace RtIO {

using RtIo::FileStream;

// (Wave 20 lane PROMOTE's vptr-identity counter used to be declared here and
// called from both bodies below. It died WHOLE with `IO::BinaryFileStream` —
// B34_RULES section 4.1, and file_stream.cpp says why.)

// ENDGAME B.3.9 IO: the ?Write@ / ?SetCurrentPos@FileStream@ slot doors
// (0x1800aab90 / 0x1800aac10) had the minted FileStream / BinaryFileStream /
// TextFileStream tables as their only callers and were deleted with them; the
// bodies are the members below.

}  // namespace RtIO

// ============================================================================
// `IO::FileStream`'s two WIN32 member bodies — ENDGAME B.3.4 block 4 (STREAMS)
// ============================================================================
// Slots 2 and 4 of ??_7FileStream@IO@@6B@ live here rather than in
// file_stream.cpp because they are the two that talk to Win32 directly. The
// other five members are next door; this block opens `namespace IO` after the
// `RtIO` doors close, for the reason B34_BRIEFS section A.2 gives (a member of
// `IO::X` cannot be defined inside the sibling namespace `RtIO`).
namespace IO {

// ============================================================================
// IO::FileStream::Write(int, const void*)                retail 0x1800aab90
// ============================================================================
// Straight to Win32 through the FileInfo's handle, then the byte count is added
// to `FileInfo::cursor` BY HAND — the stream does not go through
// FileSystem for writes the way `Read` goes through `FileSystem::ReadFile`.
// That asymmetry is retail's and it is the reason this file exists.
//
// THE RETURN IS `written == len`, NOT the `WriteFile` BOOL. 0x1800aabd5 is
// `CMP R8D,ESI ; SETZ AL` on the byte count against the requested length, and
// `WriteFile`'s own result is discarded entirely. A short write therefore
// returns false while still having advanced the cursor by what it wrote — both
// halves are faithful and a port that returned the BOOL would report success on
// a truncated write.
//
// THE CURSOR IS ADVANCED UNCONDITIONALLY, before the comparison (0x1800aabcc),
// so it moves even on the short-write path.
bool FileStream::Write(int len, const void* buf) {
    uint32_t written = 0;                                       // 1800aabb8
    Platform::FileWrite(_file.winHandle,      // 1800aaba5: [RCX+0x38]
                        buf, static_cast<uint32_t>(len), &written);  // 1800aabc1 WriteFile

    _file.cursor += static_cast<int32_t>(written);              // 1800aabcc: ADD [RBX+0x2c]

    const bool ok = (static_cast<int32_t>(written) == len);     // 1800aabd5
    return ok;
}

// ============================================================================
// IO::FileStream::SetCurrentPos(int)                     retail 0x1800aac10
// ============================================================================
// THREE THINGS IN FIFTEEN INSTRUCTIONS, and the order of the first two matters:
//
//  1. `file.cursor = pos` happens FIRST, at 0x1800aac1c, BEFORE the branch — so
//     the cursor is updated on BOTH paths, including the packed-file path that
//     never touches Win32 and including a seek that subsequently fails.
//  2. The discriminator is `CMP dword [RCX+0x20],-0x3 ; JA`, which is
//     **UNSIGNED**: it takes the Win32 path when `handle` is -1 or -2, i.e. the
//     "nothing opened" and "loose file" sentinels FileSystem.h documents, and
//     the early `return true` when the handle is a PACKED `(pack<<24)|file`
//     descriptor. Read as a signed test it inverts, and every packed read would
//     issue a `SetFilePointer` on a handle that is not one.
//  3. The Win32 return is compared against the REQUESTED position
//     (`CMP EAX,EBX ; SETZ AL`), not against `INVALID_SET_FILE_POINTER`.
//
// `SetFilePointer(h, pos, nullptr, FILE_BEGIN)` — `FILE_BEGIN` is 0 and arrives
// as `XOR R9D,R9D` at 0x1800aac2d, the high-order-word pointer as `XOR R8D,R8D`.
bool FileStream::SetCurrentPos(int pos) {
    _file.cursor = pos;                                  // 1800aac1c — BEFORE the branch

    // Unsigned: -1 and -2 take the Win32 path, a packed descriptor does not.
    if (static_cast<uint32_t>(_file.handle) <= 0xFFFFFFFDu) {   // 1800aac16/1f
        return true;                                     // 1800aac21
    }

    const uint32_t got = Platform::FileSeek32(
        _file.winHandle,        // 1800aac29
        pos);                                             // 1800aac33 SetFilePointer(.., NULL, FILE_BEGIN)
    return static_cast<int32_t>(got) == pos;              // 1800aac39
}

}  // namespace IO
