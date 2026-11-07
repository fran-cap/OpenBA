// ============================================================================
// src/engine/io/persistence_saveinfo_job.cpp — BATCH 8b (2026-09-12)
//   IO::Persistence's worker, COMMAND 3 — the GET-SAVE-INFO job.
//   retail 0x1800b65b0, UNEXPORTED, 1034 bytes / 258 instructions.
// ============================================================================
//
// The fourth of the five address doors `persistence_worker.cpp` opened when
// it ported the worker's `Run` dispatcher (0x1800b7260). BATCH 5 paid
// GET-SAVE-LIST (0x1800b6290, persistence_savelist_job.cpp); this pays
// GET-SAVE-INFO. LOAD (0x1800b6aa0) and DELETE (0x1800b69c0) remain.
//
// "Unexported" is MEASURED, not read off a mangling: the retail export
// directory was walked with pefile for this address before the file was
// written and returned nothing (reference_aeaa_is_private_not_unexported.md).
// So this is an ADDRESS row, registered with `RegisterRuntimeAddr` only, and
// renamed in retail Ghidra to `IO_Persistence_Worker_GetSaveInfoJob`.
//
// ---------------------------------------------------------------------------
// WHY BATCH 8 DECLINED IT, AND WHAT WAS ACTUALLY MISSING
// ---------------------------------------------------------------------------
// BATCH 8's report held this row back needing "FileStream::Open verification,
// an Array<{u32,u32,u32}>::SetSize port, and FileSystem+0x19 flag semantics".
// All three were already in the tree and none of them needed new work:
//
//   * `?Open@FileStream@IO@@UEAA_NAEBVFilename@System@@@Z` is EXPORTED
//     (ordinal 3304) and file_stream.cpp:1270 has ported it since lane FILE;
//     it is registered at both its name and 0x1800aac70.
//   * the Array is `kSpec_Blk12` — array.cpp:1191 has owned 0x18002a810 since
//     WAVE 25 lane INTERNAL42, under the name "the Persistence block
//     directory", which is exactly this array.
//   * `FileSystem+0x19` is `platformExistsGate`, named and static_asserted in
//     src/engine/io/FileSystem.h since lane FIND. This body saves it, forces
//     it to 1 for the duration and restores it on every exit path (including
//     the early "could not open" one) — a scoped override, not a semantic
//     question.
//
// Nothing is ported here that another file already owned. The one door this
// body opens, `?Compute@CRC@System@@SAIPEBXHI@Z` (0x1800c5490, ordinal 1752),
// was closed the same day by src/engine/system/crc.cpp; the by-name resolve
// below now lands on our body through the registry.
//
// ---------------------------------------------------------------------------
// WHAT IT DOES — verify a save file's header and checksum without loading it
// ---------------------------------------------------------------------------
//   0. save `fs->platformExistsGate`, force it to 1.
//   1. open a stack BinaryFileStream on `pendingFilename` (AdjustDelimiters
//      first). Failure -> status = 2 (I/O) and straight to teardown.
//   2. read 4 bytes into `fileHeader`. Its HIGH 16 bits must equal
//      `blockCount` (what BeginGetSaveGameInfo asked for) or status = 3.
//   3. if the LOW 16 bits (the version) are 2, read 8 more bytes into
//      `magic`/`crc` and require magic == 0x80808080.
//   4. the version must be 1 or 2 (`(uint16_t)(ver-1) <= 1`), else status = 3.
//   5. SetSize(blockDescData, blockCount) and read blockCount*0xc bytes of
//      descriptors.
//   6. the file's size must equal exactly
//        headerBytes(4 or 12) + blockCount*0xc + sum(desc[i].size)
//      or status = 3.
//   7. version 2 only: recompute the CRC-32 the SAVE job wrote —
//        crc = CRC::Compute(descriptors, blockDescCount*0xc, seed 0xFFFFFFFF)
//        then, per descriptor IN ORDER: seek to desc[i].offset and stream
//        desc[i].size bytes through a 0x8000-byte stack buffer, folding each
//        0x8000 slice and COMPLEMENTING the accumulator once per slice.
//      That is `Crc32Chunked(crc, ., size, 0x8000)` — the same chunk rule the
//      SAVE job's payload pass uses, which is what makes writer and reader
//      agree (persistence_worker.cpp:187). A mismatch is status = 3.
//   8. success: CloseFile, status = 1.
//   9. teardown (every path): close the handle if still open, destroy the
//      BinaryFileStream, restore `platformExistsGate`.
//
// STATUS CODES, from this body alone: 1 = ok, 2 = could not open / short read,
// 3 = the file is not a save of the requested shape. `?GetStatus@` (0x1800b7c40)
// is the reader.
//
// ---------------------------------------------------------------------------
// THE SEEK IS CONDITIONAL ON THE FILE BEING LOOSE, AND THE TEST LOOKS INVERTED
// ---------------------------------------------------------------------------
//   1800b683c: cmp dword [rsp+0x40],0xfffffffd    ; FileInfo::handle
//   1800b6841: jbe 1800b6854                      ; SKIP the seek
// `handle` is not a handle (FileSystem.h): -1 = nothing opened, -2 = a LOOSE
// file with the real Win32 HANDLE at +0x18, anything else = a PACKED entry
// encoded as (packIndex<<24)|fileIndex. Read UNSIGNED, only -1 and -2 are
// above 0xfffffffd, so the `SetFilePointer` runs exactly for the loose case —
// where there is a real HANDLE to move — and a packed entry just gets its
// `cursor` field set, which is all `FileSystem::ReadFile` needs. The write to
// `cursor` (`[rsp+0x4c]`) happens for BOTH.
//
// The `SetFilePointer` call leaves EDX UNSET on purpose: it is still holding
// `desc[i].offset` from the `MOV EDX,[R12+RAX+4]` two instructions earlier.
// That is the distance argument, and it is why the store to `cursor` and the
// seek use one value with no reload.
//
// THE ReadFile RETURN IS IGNORED inside the checksum loop (no `TEST AL,AL`
// after 0x1800b688f), unlike the three header reads. Reproduced: a short read
// there folds stale buffer bytes and fails the CRC compare, which is retail's
// answer to a truncated save.
//
// ---------------------------------------------------------------------------
// NO ORACLE — same refusal as its siblings. The body reads a real file through
// a real HANDLE and writes one field; there is nothing to replay it against
// that does not re-read the save. Its verdict is end to end: the load dialog
// either accepts a save or reports it corrupt.
//
// PORTABILITY: the direct `SetFilePointer` is Platform::FileSeek32 since Phase
// D.4 THREADS E.5, as persistence_worker.cpp's direct `WriteFile` is
// Platform::FileWrite.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "platform_file.h"   // Platform::FileSeek32
#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "io/Persistence.h"
#include "io/PersistenceWorker.h"
#include "system/Filename.h"
#include "collection/Collection.h"   // ENDGAME B.3.2 step 2
#include "system/Crc.h"

namespace {

// B.4 R-IO: the block directory is `IO::PersistenceWorker::_blockDescs`, a
// `Collection::Array<RtIo::BlockDesc>` (Persistence.h): stride 0xc, +0x04 the
// payload's file offset (0x1800b6833 MOV EDX,[R12+RAX+4]), +0x08 its length
// (0x1800b6857 MOV R14D,[R12+RAX+8]). 0x18002a810 (kSpec_Blk12, array.cpp) is
// TRAIT-EQUIVALENT for it: stride 0xc == sizeof(BlockDesc), align 4, assign a
// pure 3-dword copy, init and destroy null -- so the trait-derived
// `Collection::Array<RtIo::BlockDesc>::SetSize` IS 0x18002a810.

// ---- CRC-32, reflected table (polynomial 0xEDB88320) ------------------------
// System::CRC::_crcTable, retail 0x1801f9080, which 0x1800b689f loads by RIP.
// Generated rather than read out of the DLL so this body survives cutover
// (PLAN.md §4.1 decision 1); persistence_worker.cpp and type_system.cpp
// each carry their own generator for the same reason.
const uint32_t* CrcTable() {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    return table;
}

}  // namespace

// ============================================================================
// The worker's command-3 body.                              retail 0x1800b65b0
//
// `this` is the worker, `Persistence + 0x10`; every offset in the shipped
// listing is worker-relative, and PersistenceWorker.h pins each field.
// ============================================================================
void IO::PersistenceWorker::GetSaveInfoJob() {
    ::IO::FileSystem* const fs = ::IO::FileSystem::_singleton;   // the class static

    // ---- 0. scoped override of the Exists gate ----------------------------
    const uint8_t savedGate = fs->_platformExistsGate;       // 0x1800b65e5
    fs->_platformExistsGate = 1;                             // 0x1800b65f5

    {
        // ENDGAME B.3.9 IO (W2) / B.4 R-IO: the REAL class on the stack (inlined
        // 0x1800b6600); its destructor at the close of this block is retail's
        // teardown.
        IO::BinaryFileStream bfs;

        // ---- 1. open pendingFilename ------------------------------------------
        // A raw refcount SHARE of the worker's String (0x1800b666f: `MOV
        // RAX,[RCX+0x30] / TEST / INC dword [RAX-4]`) followed by AdjustDelimiters
        // (0x1800b6689) IS `Filename(const String&)` (retail 0x18000c780): adopt
        // the buffer (AddRef, no copy), then normalise -- see B31_RULES
        // DELIMITERS. Scoped to a block so its release lands at the same point
        // retail's manual one did (0x1800b66a6), not at function exit.
        bool opened;
        {
            System::Filename f(_pendingFilename);

            opened = bfs.IO::FileStream::Open(f);   // ENDGAME B.3.9 IO: the member

            // ~f is retail's release at 0x1800b66a6.
        }

        if (!opened) {
            _status = static_cast<int32_t>(Status::Failed);      // 0x1800b66e0
        } else {
            // `headerBytes` is retail's ESI: 4 after the first read, 12 after the
            // optional second one, and it is the term the size check starts from.
            uint32_t headerBytes = RtIo::kSaveHeaderBytesV1;     // 0x1800b66ed
            bool ioFail  = false;
            bool badFile = false;

            // ---- 2. the 4-byte file header ------------------------------------
            if (!fs->ReadFile(bfs._file, 4, &_fileHeader)) {
                _status = static_cast<int32_t>(Status::Failed);
                ioFail = true;
            } else if (static_cast<uint32_t>(static_cast<uint16_t>(_fileHeader >> 16)) !=
                       static_cast<uint32_t>(_blockCount)) {      // 0x1800b6720
                badFile = true;
            } else {
                // ---- 3. version 2 carries {magic, crc} ------------------------
                // The 8 bytes land on `_magic` and the adjacent `_crc` (+0x48,
                // +0x4c): retail reads them in one call from &magic.
                if (static_cast<uint16_t>(_fileHeader) == RtIo::kSaveFileVersion) {   // 0x1800b672a
                    if (!fs->ReadFile(bfs._file, 8, &_magic)) {
                        _status = static_cast<int32_t>(Status::Failed);
                        ioFail = true;
                    } else {
                        headerBytes = RtIo::kSaveHeaderBytesV2;   // 0x1800b675e
                        if (_magic != RtIo::kSaveFileMagic) badFile = true;
                    }
                }

                // ---- 4. version must be 1 or 2 --------------------------------
                // `MOVZX EAX,WORD [R15+0x44] / DEC AX / CMP AX,1 / JA` — the
                // unsigned wrap is what rejects version 0.
                if (!ioFail && !badFile) {
                    const uint16_t ver = static_cast<uint16_t>(_fileHeader);
                    if (static_cast<uint16_t>(ver - 1) > 1) badFile = true;  // 0x1800b6771
                }

                // ---- 5. the block directory -----------------------------------
                if (!ioFail && !badFile) {
                    Collection::Array<RtIo::BlockDesc>* const descs = &_blockDescs;
                    // retail 0x18002a810 — trait-derived Array<BlockDesc>::SetSize
                    // (see the note above CrcTable).
                    descs->SetSize(static_cast<uint16_t>(_fileHeader >> 16));  // 0x1800b678c

                    const int32_t n = descs->_size;               // 0x1800b6791
                    if (!fs->ReadFile(bfs._file, n * static_cast<int32_t>(RtIo::kBlockDescBytes),
                                      descs->_data)) {
                        _status = static_cast<int32_t>(Status::Failed);
                        ioFail = true;
                    } else {
                        // ---- 6. the file size must add up ----------------------
                        // headerBytes + hdrBlockCount*0xc + sum(desc[i].size),
                        // where the middle term uses the count out of the HEADER
                        // and the sum uses the ARRAY's size. Retail reads the two
                        // from different places (0x1800b67c3 vs 0x1800b67d1) and
                        // SetSize has just made them equal.
                        uint32_t total = headerBytes +
                            static_cast<uint32_t>(
                                static_cast<uint16_t>(_fileHeader >> 16)) * RtIo::kBlockDescBytes;
                        for (int32_t i = 0; i < n; ++i)           // 0x1800b67e1
                            total += descs->_data[i].size;

                        if (total != static_cast<uint32_t>(bfs._file.size)) {
                            badFile = true;                       // 0x1800b67f1
                        } else if (static_cast<uint16_t>(_fileHeader) == RtIo::kSaveFileVersion) {
                            // ---- 7. recompute the checksum --------------------
                            // ?Compute@CRC@System@@SAIPEBXHI@Z (ordinal 1752,
                            // retail 0x1800c5490): System::CRC::Compute, by name.
                            uint32_t crc = ::System::CRC::Compute(
                                        descs->_data, n * static_cast<int32_t>(RtIo::kBlockDescBytes),
                                        0xFFFFFFFFu);  // 0x1800b6811

                            // The 0x8000-byte stack buffer at [RBP-0x80]; the
                            // frame is `_chkstk(0x8088)`, so this size is retail's.
                            static const uint32_t kChunk = RtIo::kSaveCrcChunkBytes;
                            uint8_t buffer[RtIo::kSaveCrcChunkBytes];
                            const uint32_t* const table = CrcTable();

                            for (int32_t i = 0; i < descs->_size; ++i) { // 0x1800b6830
                                const RtIo::BlockDesc* const d = &descs->_data[i];

                                bfs._file.cursor =
                                    static_cast<int32_t>(d->offset);        // 0x1800b6838
                                // Loose (-1/-2) only; a packed entry needs no seek.
                                if (!RtIO::FileInfo::IsPackedHandle(bfs._file.handle))
                                    Platform::FileSeek32(                   // SetFilePointer
                                        bfs._file.winHandle,
                                        static_cast<int32_t>(d->offset));

                                int32_t remaining = static_cast<int32_t>(d->size);
                                while (remaining > 0) {                     // 0x1800b6870
                                    int32_t take = static_cast<int32_t>(kChunk);
                                    if (remaining < take) take = remaining; // CMOVL
                                    // The return value is IGNORED here; see banner.
                                    fs->ReadFile(bfs._file, take, buffer);
                                    for (int32_t k = 0; k < take; ++k)      // 0x1800b68b0
                                        crc = table[(crc & 0xFFu) ^ buffer[k]] ^ (crc >> 8);
                                    crc = ~crc;                             // 0x1800b68ce
                                    remaining -= take;
                                }
                            }

                            if (crc != _crc) badFile = true;                // 0x1800b68f3
                        }
                    }
                }
            }

            if (!ioFail) {
                if (badFile) {
                    _status = static_cast<int32_t>(Status::BadFile);        // 0x1800b691b
                } else {
                    // 0x1800b68f9 — retail's own close, BEFORE the teardown's.
                    if (bfs._file.handle != RtIO::FileInfo::kHandleNone)
                        fs->CloseFile(bfs._file);
                    _status = static_cast<int32_t>(Status::Done);           // 0x1800b6911
                }
            }
        }

        // ---- 9. teardown, taken by EVERY path above ----------------------------
        // The failure-to-open arm jumps past the `LEA RSI,[0x1801549a0]` at
        // 0x1800b6923 only because RSI still holds that value from 0x1800b65f9;
        // the teardown itself is the same on both paths.
        if (bfs._file.handle != RtIO::FileInfo::kHandleNone)
            fs->CloseFile(bfs._file);                                       // 0x1800b6942
    }   // ~bfs: ??1BinaryFileStream                                    // 0x1800b694f..
    fs->_platformExistsGate = savedGate;                                // 0x1800b69a3
}
