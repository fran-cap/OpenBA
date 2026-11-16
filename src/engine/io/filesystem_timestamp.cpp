// ============================================================================
// src/engine/io/filesystem_timestamp.cpp — BATCH 5 (2026-09-12)
//   `?GetFileTimeStamp@FileSystem@IO@@QEBA?AVDateTime@System@@AEBVFilename@4@@Z`
//   retail 0x1800ae270, ordinal 2607, 215 bytes / 48 instructions.
// ============================================================================
//
//     System::DateTime FileSystem::GetFileTimeStamp(const Filename& path) const
//
// The last-write time of a LOOSE FILE ON DISK, as a raw FILETIME qword. Used by
// the save-list job (0x1800b6290, ported in this same commit) to stamp every
// `.sav` it finds.
//
// ---------------------------------------------------------------------------
// THE HIDDEN RETURN SLOT — `?AV` IN THE MANGLING, RDX ON THE WIRE
// ---------------------------------------------------------------------------
// `?AVDateTime@System@@` is a CLASS RETURNED BY VALUE, so the MS x64 ABI gives
// it a hidden return slot in RDX and shifts every real argument right: RCX is
// `this`, RDX is the slot, R8 is `path` (0x1800ae289 `MOV RBX,RDX` /
// 0x1800ae28c `MOV RDX,R8`). Declaring it without that slot is a WILD WRITE
// through a stale RDX — Wave 1's defect 1, which scribbled 0x30 bytes on every
// menu scene load. The function
// returns the slot in RAX (0x1800ae334), and the port does too.
//
// `this` IS READ BY NOTHING. RCX is overwritten at 0x1800ae28f and never
// reloaded. The parameter is kept because the mangling is a const member and
// the caller passes it.
//
// ---------------------------------------------------------------------------
// THE WIN32 BLOCK, TRANSCRIBED RATHER THAN NAMED FROM HABIT
// ---------------------------------------------------------------------------
//   0x1800ae294  ??0WideString@System@@QEAA@AEBVString@1@@Z  (lane FS's port)
//   0x1800ae2c0  CreateFileW  [0x18014b320]
//                  dwDesiredAccess       0x80000000  GENERIC_READ
//                  dwShareMode           1           FILE_SHARE_READ
//                  lpSecurityAttributes  NULL
//                  dwCreationDisposition 3           OPEN_EXISTING
//                  dwFlagsAndAttributes  0x80        FILE_ATTRIBUTE_NORMAL
//                  hTemplateFile         NULL
//   0x1800ae30c  GetFileTime  [0x18014b1e8]  — creation and access times are
//                  BOTH NULL; only lpLastWriteTime is asked for
//   0x1800ae319 / 0x1800ae321  CloseHandle  [0x18014b3b8]  — on BOTH arms
//
// THE WIDESTRING IS RELEASED BEFORE THE HANDLE IS TESTED (0x1800ae2c9), not
// after the read. Kept in that order.
//
// THE HANDLE TEST IS `LEA RAX,[RDI-1] / CMP RAX,-3 / JA fail` — an UNSIGNED
// test that rejects exactly two values, NULL (0) and INVALID_HANDLE_VALUE (-1).
// Spelled as retail spells it rather than as `h == INVALID_HANDLE_VALUE || !h`,
// because the two are only equivalent by accident of those being the only wrap
// cases (the same note filesystem_openfile.cpp carries for the identical
// sequence at 0x1800ade12).
//
// EVERY FAILURE RETURNS A ZERO DateTime (0x1800ae331, `MOV [RBX],RSI` with RSI
// zero): a missing file, a locked file, and a `GetFileTime` that fails all
// produce the same 0 — there is no error channel. The caller cannot tell a
// 1601-01-01 timestamp from a failure, and that is retail's design.
//
// ---------------------------------------------------------------------------
// THE THREE WIN32 CALLS (Phase D.4 THREADS E.5)
// ---------------------------------------------------------------------------
// The whole body is three Win32 calls on a UTF-16 path: CreateFileW,
// GetFileTime, CloseHandle. They are Platform::FileOpenRead / FileLastWrite /
// FileClose now (platform_file.h), so this file left src/platform/win (it was
// `filesystem_timestamp_win.cpp`). A POSIX FileLastWrite is `fstat` and the
// FILETIME conversion.
//
// ---------------------------------------------------------------------------
// CHECK
// ---------------------------------------------------------------------------
// A boot is the end-to-end check: the save list is sorted on this value, so a
// wrong timestamp here reorders the save list visibly.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <new>

#include "platform_file.h"   // Platform::FileOpenRead / FileLastWrite / FileClose
#include "io/FileSystem.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/WideString.h"
#include "system/string_internal.h"

namespace RtIO {


// ============================================================================
// ?GetFileTimeStamp@FileSystem@IO@@QEBA?AVDateTime@System@@AEBVFilename@4@@Z
//                                                          retail 0x1800ae270
//
//   (this, hidden return slot, path) — see the banner.
// ============================================================================
// The body is the member `IO::FileSystem::GetFileTimeStamp`, below. It returns
// `System::DateTime` by value (Value.h: the raw FILETIME, two dwords).

}  // namespace RtIO

// ============================================================================
// `IO::FileSystem::GetFileTimeStamp` — the member body
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); it reaches this
// translation unit's `RtIO`-scoped names as `RtIO::X`.
//
// NOTHING ELSE CHANGED: same three Win32 calls in the same order, the same
// release-the-WideString-before-testing-the-handle order, the same unsigned
// two-value handle test, the same zero on every failure arm. `this` is still
// read by nothing, exactly as retail's RCX is not.
namespace IO {

System::DateTime FileSystem::GetFileTimeStamp(const System::Filename& path) const {

    // WideString ws(*path) -- ??0WideString@System@@QEAA@AEBVString@1@@Z; the
    // block ends at retail's 0x1800ae2c9 release, so ~WideString runs there.
    Platform::FileHandle h;
    {
        const System::WideString ws(path.AsString());            // 0x1800ae28f / 0x1800ae294
        // CreateFileW(GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NORMAL)
        h = Platform::FileOpenRead(ws._data);
    }                                                            // 0x1800ae2c9

    // 0x1800ae2f5 — rejects exactly NULL and INVALID_HANDLE_VALUE. See banner.
    const uint64_t hm1 = reinterpret_cast<uint64_t>(h) - 1u;  // CAST-OK: ptr_int
    if (hm1 <= static_cast<uint64_t>(-3)) {
        uint64_t lastWrite;                                      // [RSP+0x40]
        if (Platform::FileLastWrite(h, &lastWrite) != 0) {      // GetFileTime(h, 0, 0, &lastWrite)
            Platform::FileClose(h);                              // 0x1800ae321 CloseHandle
            return System::DateTime{static_cast<uint32_t>(lastWrite),
                                    static_cast<uint32_t>(lastWrite >> 32)};
        }
        Platform::FileClose(h);                                  // 0x1800ae319 CloseHandle
    }

    return System::DateTime{};                                   // 0x1800ae331
}

}  // namespace IO
