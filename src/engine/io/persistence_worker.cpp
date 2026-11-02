// ============================================================================
// src/engine/io/persistence_worker.cpp — Wave 24 lane INTERNAL8
// ============================================================================
//
// The four UNEXPORTED bodies of IO::Persistence's embedded save/load worker.
// Wave 19 lane PERSIST ported the seven EXPORTED entry points around them and
// left these four as address doors into the shipped DLL; they are four of the
// eight real bodies inside the census's 45 `internal` UNKNOWN rows
// (WAVE23_LANE_INTERNAL.md §4B/B4).
//
//   0x1800b7390  enqueue command 2 (DELETE)     188 B
//   0x1800b7460  enqueue command 1 (LOAD)       343 B
//   0x1800b75c0  enqueue the SAVE request       364 B
//   0x1800b7740  the embedded worker's DTOR     183 B
//
// EXTENT — how each was bounded, and why not from `.pdata` alone.
// ---------------------------------------------------------------------------
// `.pdata` UNDERSTATES a function's extent: MSVC chains fragments through
// UNW_FLAG_CHAININFO and it is the ENTRY fragment that reads `chained=False`,
// so a chained function looks complete when you look it up by its own address
// (WAVE23_LANE_INTERNAL.md §3.2 measured a 20.4x understatement on a row in
// this very bucket). Each extent above is instead the address range of the
// instruction listing this file transcribes, taken from Ghidra's function
// body, which terminates at the tail JMP / RET — an INDEPENDENT instrument
// from `.pdata`, and the one §3.2's control validated against. All four are
// single-fragment; the three enqueue bodies end in a tail
// `JMP qword ptr [0x18014b368]` (SetEvent) and the dtor in a tail
// `JMP qword ptr [0x18014b3b8]` (CloseHandle).
//
// THE OS CALLS (this file was `persistence_worker_win.cpp` until Phase D.4)
// ---------------------------------------------------------------------------
// All four bodies call the Win32 event/handle API DIRECTLY through the DLL's
// own import thunks — ResetEvent (0x18014b198), SetEvent (0x18014b368),
// WaitForSingleObject (0x18014b3c0), CloseHandle (0x18014b3b8) — and SaveJob
// writes with WriteFile. Those calls are Platform:: primitives now
// (platform_sync.h since THREADS E.2, platform_file.h's FileWrite since E.5),
// beside persistence_begin_saveinfo.cpp's ?BeginGetSaveGameInfo@.
//
// THE `worker` ARGUMENT IS `Persistence + 0x10`, NOT `Persistence`
// ---------------------------------------------------------------------------
// ??0Persistence@IO@@QEAA@XZ embeds a Thread-derived worker at +0x10 and
// stamps the derived vtable 0x180155860 over it (Persistence.h). All four
// bodies below take that SUBOBJECT in RCX, so every offset in the shipped
// listing is `Persistence offset - 0x10`. The header already pins both
// spellings against each other field by field; this file therefore takes a
// `Persistence*` recovered as `(char*)worker - 0x10` and uses the NAMED
// Persistence fields, so a future change to the layout breaks the
// static_asserts rather than silently shifting a raw offset here.
//
// AEAA / THE NAME DOOR — checked, not assumed
// ---------------------------------------------------------------------------
// "AEAA means PRIVATE, not unexported" — this DLL exports its private members,
// so an address door justified by the word "unexported" in a comment is often
// a plain name door somebody missed. It does not bite here: Wave 23 pushed all
// 183 address-keyed census rows through an export-table screen with 100%
// recall and 0 false positives on 1,125 in-population controls, and got 0
// names back. These four have no mangled name to register under, so they are
// registered by ADDRESS only (RegisterRuntimeAddr).
//
// ORACLE — a SHADOW site, and why not an input sweep or a replay
// ---------------------------------------------------------------------------
// Kickoff §1c, stated before the work started:
//   * NOT an input sweep. None of the four is pure. All four mutate the live
//     worker object and three of them signal a real Win32 event a real thread
//     is blocked on; running the shipped body "for comparison" would enqueue a
//     SECOND request, and running the dtor twice would WaitForSingleObject on
//     a handle already closed.
//   * NOT a write-path replay. There is no Serializer here.
//   * NOT a synthesised oracle. A synthetic worker is buildable for the three
//     enqueue bodies (the whole issued state is a String, some ints and two
//     event handles) but NOT for the dtor, which joins a thread; and an oracle
//     that covers three of four rows while the fourth is the risky one is the
//     shape this project keeps mistaking for coverage.
//   * SO: the diff oracle, exercised by lane DIFF's armed sweep. This lane has
//     no game-directory lock by design.
//
// THESE FOUR SITES ARE LABELLED `[SELF-CHECK]` IN THEIR OWN SITE NAMES, and
// that labelling is a deliverable, not decoration. Wave 24 lane DIFF's armed
// sweep (99.7M calls over 630 sites) measured that AT LEAST 286 of 630 existing
// sites (45%) cannot catch a shared misunderstanding of retail — 115 SELF-CHECK,
// 38 ATTENDANCE, and 133 structurally incapable of printing anything but
// `divergences=0`. None of them said so. A site that cannot fail reports GREEN,
// which is worse than no site. So each site here declares its own class in the
// string the sweep prints, and the "theirs" description says in words that there
// is no DLL side rather than naming a comparison that never happened.
//
// AND THE LIMIT OF THAT, SAID PLAINLY: a shadow site is a SELF-CHECK, not an
// oracle. It takes this port's own verdict and never consults the DLL
// (reference_shadow_marker_is_not_an_oracle.md). The predicates below are
// therefore written to re-read the OBJECT after the body has run and assert
// the fields retail's listing says must hold — which catches a wrong offset, a
// wrong constant and a dropped store, and does NOT catch a shared
// misunderstanding of what retail meant. `divergences=0` on these four sites
// means "the stores landed where this file says", not "the port is right".
//
// WHAT THIS FILE GROWS — the cutover surface
// ---------------------------------------------------------------------------
// Porting a body faithfully makes its callees OURS to satisfy at cutover
// (reference_porting_can_grow_the_cutover_surface.md). Two addresses below are
// NEW to the census — no literal door for them existed in the tree before this
// file:
//     0x18002a9d0   Array<void*>::SetSize(n)        (blockPtr array)
//     0x18002a810   Array<{u32,u32,u32}>::SetSize(n) (blockDesc array, stride 0xc)
// and two more were already census rows and are still unported:
//     0x18002a930   ~Array clear-on-null, blockDesc
//     0x18002ac40   ~Array clear-on-null, the save list
// All four are `Collection::Array<T>` instantiations against the parameterised
// cores this tree already owns (SetSizeCore / ArrayDtorImpl in
// src/engine/collection/), i.e. ArraySpec/DestroySpec table entries and not
// function bodies (WAVE23_LANE_INTERNAL.md §6).
// **ADDRESSEE: the Wave 25 lane that takes WAVE23_LANE_INTERNAL.md residual #6
// (the 14 spec entries).** Adding 0x18002a9d0 and 0x18002a810 to that list is
// this file's debt, and it is recorded here rather than left for a census diff
// to rediscover.
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "platform_file.h"   // Platform::FileWrite
#include "io/Persistence.h"
#include "platform_sync.h"   // Platform::EventReset / EventSet
#include "io/PersistenceWorker.h"   // ENDGAME B.3.9 IO (W2)
#include "io/FileStream.h"
#include "system/Filename.h"
#include "system/string_internal.h"
#include "io/FileSystem.h"
#include "system/WideString.h"   // fix.save_failure_notice: the move's paths
#include <string>

using RtIo::SaveBlock;

namespace {

// The FileSystem singleton, read exactly where retail reads it (the class
// static, ENDGAME B.3.9 IO D7).
::IO::FileSystem* Fs() {
    return ::IO::FileSystem::_singleton;
}

// ---- CRC-32, reflected table (polynomial 0xEDB88320) -----------------------
// System::CRC::_crcTable (exported DATA, retail 0x1801f9080) is the textbook
// reflected CRC-32 table byte-for-byte (type_system.cpp already verified this
// table against the DLL's copy for the type-id hash); generated here rather
// than read out of the DLL so the SAVE job survives cutover with the DLL
// absent (PLAN.md §4.1 decision 1). A second, independent generator from
// type_system.cpp's rather than a shared one, for the same reason this file's
// banner gives for not sharing ArrayClear helpers: the census greps for what
// each file actually depends on.
uint32_t* CrcTable() {
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

// The SAVE and GET-SAVE-INFO jobs both compute this same chunked-with-invert
// checksum (Persistence.h's `fileHeader`/`magic`/`crc` banner): a plain
// running CRC-32 over `data[0..len)`, EXCEPT that when `chunkCap != 0` the
// accumulator is complemented (~) after every `chunkCap`-byte slice rather
// than only once at the end. Read out of FUN_1800b6e00's payload loop
// (chunkCap==0x8000, one reload of `crc` per SaveBlock, persisted across
// blocks) and cross-checked against FUN_1800b65b0's disk-streaming twin,
// which chunks identically at the same 0x8000 cap for the same reason: the
// two must agree bit-for-bit for a save's own writer and reader to concur on
// its checksum, which is the entire point of the field.
uint32_t Crc32Chunked(uint32_t crc, const void* bytes, uint32_t len, uint32_t chunkCap) {
    const uint8_t* data = static_cast<const uint8_t*>(bytes);   // CAST-OK: byte_io
    const uint32_t* table = CrcTable();
    if (chunkCap == 0) {
        for (uint32_t i = 0; i < len; ++i) crc = table[(crc & 0xFFu) ^ data[i]] ^ (crc >> 8);
        return ~crc;
    }
    uint32_t remaining = len;
    while (remaining > 0) {
        const uint32_t chunk = remaining < chunkCap ? remaining : chunkCap;
        for (uint32_t i = 0; i < chunk; ++i) crc = table[(crc & 0xFFu) ^ data[i]] ^ (crc >> 8);
        crc = ~crc;
        data += chunk;
        remaining -= chunk;
    }
    return crc;
}

// B3R RT-IO: the file-local MemFree (a by-address door onto 0x1800c7ad0) had
// no caller left and is deleted.

// ---- Collection::Array<T> instantiations --------------------------------
// ENDGAME B.3.2 step 2 (2026-09-13) — the five by-address forwarders that
// used to sit here are GONE; each call site below now names the container
// directly. What each address was, and the instantiation it names:
//
//   0x180024aa0  Array<void*>::~Array           trait-derived, `Array<void*>`
//   0x18002a930  Array<BlockDesc12>::~Array     trait-derived (kSpec_Blk12 is
//                                               trait-equivalent — see
//                                               persistence_saveinfo_job.cpp)
//   0x18002ac40  Array<SaveGameInfo>::~Array    SAME element/spec as
//                                               0x18002ae20/0x18002aae0
//                                               (array_destroy.cpp's J2_ENTRY
//                                               table: both call the identical
//                                               0x18002afc0 String-release
//                                               helper) — kSpec_SaveGameInfo,
//                                               NOT trait-expressible, so this
//                                               is the hand-derived row
//   0x18002a9d0  Array<void*>::SetSize(n)       trait-derived (kSpec_Fill8 is
//                                               trait-equivalent for `void*`)
//   0x18002a810  Array<BlockDesc12>::SetSize(n) trait-derived (kSpec_Blk12)
//
// B.4 R-IO: all three are `IO::PersistenceWorker` members now
// (`_blockDescs`, `_blockPtrs`, `_saves`, PersistenceWorker.h), so every call
// site below names the container.

// ---- the String field assignment the three enqueue bodies share -------------
//
// Byte for byte, 0x1800b73b3..0x1800b741e (and its two clones):
//
//     String tmp = { nullptr };
//     RtSystem_String_Set(&tmp, filename->data);   // CALL 0x1800c95d0
//     // refcount ASSIGN into worker->filename:
//     RAX = worker->filename; RBX = tmp.data;
//     if (RAX != RBX) {
//         if (RAX) {
//             if (--RAX[-1] == 0) { worker->filename -= 8; MemFree(that); }
//             else                  worker->filename  = nullptr;
//         }
//         worker->filename = RBX;
//         if (RBX) ++RBX[-1];
//     }
//     // release tmp:
//     if (RBX) { if (--RBX[-1] == 0) { RBX -= 8; tmp.data = RBX; MemFree(RBX); }
//                else                  tmp.data = nullptr; }
//
// TWO DETAILS THAT ARE NOT COSMETIC, and that a "tidier" spelling loses:
//
//  1. `ADD qword ptr [RDI+0x30],-0x8` at 0x1800b73dc MUTATES THE FIELD and then
//     reloads it to pass to MemFree. Between that store and the following
//     `MOV [RDI+0x30],RBX` the field transiently holds `block`, not `data`.
//     Nothing in this cluster observes the window, but the allocator watchdog
//     (BANISHED_REDZONE=watch) can, so it is reproduced rather than folded.
//  2. The `else` arm writes NULL into the field (0x1800b73ec) even though the
//     very next instruction overwrites it. Same reasoning.
//
// `data == nullptr` IS the empty string in this class (String.h:82), so a null
// `filename->data` is a legal input and the whole chain degenerates to
// assigning null — which is why there is no guard here. This is a faithful
// deref of a field retail derefs unconditionally.
// ENDGAME B.3.1 step 2: the guarded field assign below IS `String::operator=`
// (retail 0x1800c9810) byte for byte -- guard on the buffer pointer, Release(),
// adopt, AddRef if non-null -- and the trailing manual release of `tmp` IS
// `String::Release()` (retail's inline, string.cpp). Both of the "two details
// that are not cosmetic" the banner calls out (the transient `block` value,
// the else-arm null store) live INSIDE those two bodies already, so calling
// them reproduces the exact instruction sequence rather than losing it.
// B.4 R-IO: `_pendingFilename` is the worker's System::String member.
void AssignPendingFilename(IO::PersistenceWorker* w, const System::String* filename) {
    System::String tmp;
    tmp.Set(filename->_data);
    w->_pendingFilename = tmp;
    // ~tmp is retail's release.
}

// The two event calls every enqueue body ends with, in retail's order:
// ResetEvent(doneEvent) then a TAIL JMP to SetEvent(workEvent). The two
// events are System::Thread's `_hFinished` (worker+0x28, Persistence+0x38
// doneEvent) and `_hStart` (worker+0x18, Persistence+0x28 workEvent).
void SignalWorker(IO::PersistenceWorker* w) {
    Platform::EventReset(w->_hFinished);   // ResetEvent [0x18014b198]
    Platform::EventSet(w->_hStart);        // tail JMP SetEvent [0x18014b368]
}

}  // namespace

// ============================================================================
// 0x1800b7390 — enqueue command 2 (DELETE)
// ----------------------------------------------------------------------------
// void (worker /*RCX*/, const String* filename /*RDX*/)
//
// Stores, in retail's order: filename, command=2, status=0, then the two
// events. `MOV dword ptr [RDI+0x3c],0x2` is a DWORD at worker+0x3c ==
// Persistence+0x4c == `command`; `MOV dword ptr [RDI+0x38],ESI` with ESI==0 is
// `status`. Note the command is written BEFORE the status, and both before the
// events — so the worker can never observe a fresh command with a stale status.
// ============================================================================
extern "C" void RtIo_PersistenceWorker_EnqueueDelete(IO::PersistenceWorker* w, const System::String* filename) {
    AssignPendingFilename(w, filename);
    w->_command = 2;
    w->_status  = 0;
    SignalWorker(w);

    // SHADOW — a SELF-CHECK, not an oracle (banner). It re-reads the object and
    // asserts the two constants this body's whole purpose is to publish. It
    // catches a wrong offset or a dropped store; it cannot catch a shared
    // misreading of retail.
}

// ============================================================================
// 0x1800b7460 — enqueue command 1 (LOAD)
// ----------------------------------------------------------------------------
// void (worker /*RCX*/, const String* filename /*RDX*/,
//       int blockCount /*R8D*/, const SaveBlock* blocks /*R9*/)
//
// `MOVSXD RBP,R8D` at 0x1800b747f is a SIGN extension of a 32-bit count, not a
// zero extension — Ghidra's C prints MOVSXD as a zero extension
// (reference_ghidra_c_output_vs_disassembly.md), which is why the loop bound
// below is a SIGNED int and the `TEST EBP,EBP / JLE` guard is reproduced as
// `> 0` rather than `!= 0`.
//
// After the filename/command/status stores it:
//   1. ~Array's the blockPtr array (0x180024aa0) and SetSize's it to
//      blockDescCount (0x18002a9d0) — note the size comes from
//      `blockDescCount` (+0x68), NOT from `blockCount`;
//   2. NULLs every slot of it, walking `blockPtrCount` (+0x78) — the count the
//      SetSize just published, which is why the two loops cannot disagree;
//   3. for each of `blockCount` caller blocks, LINEARLY SEARCHES the blockDesc
//      table for a descriptor whose id (+0x00) equals the block's id (+0x0c)
//      and, on a hit, stores the block's `data` at the SAME INDEX in the
//      blockPtr array. A block whose id matches nothing is silently dropped —
//      its slot stays the null from step 2. That is the shipped behaviour and
//      it is the reason step 2 exists at all.
// ============================================================================
extern "C" void RtIo_PersistenceWorker_EnqueueLoad(IO::PersistenceWorker* w, const System::String* filename,
                                                   int blockCount, const SaveBlock* blocks) {

    AssignPendingFilename(w, filename);
    w->_command = 1;
    w->_status  = 0;

    w->_blockPtrs.~Array();                           // 0x180024aa0
    w->_blockPtrs.SetSize(w->_blockDescs._size);      // 0x18002a9d0

    void** slots = w->_blockPtrs._data;
    for (int i = 0; i < w->_blockPtrs._size; ++i) slots[i] = nullptr;

    // Counts how many caller blocks actually found a descriptor. This is the
    // EFFECTIVENESS number: a call with blockCount==0, or with ids that match
    // nothing, enters the body and compares clean while having matched nothing
    // (reference_probe_needs_an_effectiveness_control.md).
    int matched = 0;
    const RtIo::BlockDesc* const desc0 = w->_blockDescs._data;
    for (int b = 0; b < blockCount; ++b) {
        const SaveBlock& blk = blocks[b];
        const int n = w->_blockDescs._size;
        if (n > 0) {
            const RtIo::BlockDesc* d = desc0;
            for (int i = 0; i < n; ++i, ++d) {        // stride 0xc
                if (d->id == blk.id) {
                    w->_blockPtrs._data[i] = blk.data;
                    ++matched;
                    break;
                }
            }
        }
    }

    SignalWorker(w);

    char ours[96];
    std::snprintf(ours, sizeof(ours), "command=1 status=0 slots=%d matched=%d",
                  w->_blockPtrs._size, matched);
}

// ============================================================================
// 0x1800b75c0 — enqueue the SAVE request
// ----------------------------------------------------------------------------
// void (worker /*RCX*/, const String* filename /*RDX*/,
//       int blockCount /*R8D*/, const SaveBlock* blocks /*R9*/)
//
// THIS BODY DOES NOT WRITE A COMMAND WORD, and that is the single most
// surprising thing in the cluster — so it is stated rather than quietly
// reproduced. `MOV qword ptr [RSI+0x38],RDI` at 0x1800b765d is a QWORD zero
// store covering worker+0x38..0x3f == Persistence+0x48..0x4f, i.e. `status`
// AND `command` together; nothing afterwards sets a command. The command for a
// save is therefore written by the CALLER (Save::SaveLoad::UpdateSave —
// src/engine/save/saveload_update.cpp:324 is our side of that call), not
// here. Reading the qword store as "status = 0" alone and adding a
// `command = 4` would be an invented store; it is not in the bytes.
//
// It then stages a 12-byte HEADER at worker+0x44..0x4f, which lands inside
// Persistence's `_pad54[0xc]` gap:
//     MOV word  [RSI+0x46],BP        ; blockCount as a UINT16
//     MOV EAX,2 / MOV word [RSI+0x44],AX
//     MOV qword [RSI+0x48],-0x7f7f7f80   ; 0xFFFFFFFF80808080, sign-extended imm32
// Those three fields are NOT named in Persistence.h and this lane does NOT
// name them: a name is a claim about meaning, and nothing read here pins one.
// What IS pinned is the WIDTH of each store and the fact that the block-offset
// accumulator starts at `12 * (blockCount + 1)` — one 12-byte slot more than
// there are blocks, which is the arithmetic that says this header is slot 0 of
// the same table. They are written through a raw byte view of the pad, with a
// static_assert that the pad still covers them.
//
// Then, per block (the loop at 0x1800b76c0), it fills descriptor i with
//     +0x00 = blocks[i].id        (MOV EAX,[RDX+0x4] where RDX = &block.size)
//     +0x04 = running offset      (R8D)
//     +0x08 = blocks[i].size      (MOV EAX,[RDX])
// and blockPtr[i] = blocks[i].data (MOV RAX,[RDX-0x8]), then advances the
// offset by the size IT JUST WROTE, re-read from the array
// (`ADD R8D,[RDI+RCX+0x8]`) rather than from the caller's block. Those two are
// the same value here, but the re-read is what retail does and a moved term is
// exactly the kind of algebraically-identical change that hides where terms
// cancel (reference_moved_term_hides_where_terms_cancel.md).
// ============================================================================
extern "C" void RtIo_PersistenceWorker_EnqueueSave(IO::PersistenceWorker* w, const System::String* filename,
                                                   int blockCount, const SaveBlock* blocks) {

    AssignPendingFilename(w, filename);

    // The single QWORD zero store over status+command. Spelled as two field
    // writes because the header names them separately; the widths add up to
    // the 8 bytes the instruction covers.
    w->_status  = 0;
    w->_command = 0;

    w->_blockDescs.~Array();   // 0x18002a930
    w->_blockPtrs.~Array();    // 0x180024aa0

    // The 12-byte header at Persistence+0x54..0x5f -- BATCH 8 named these three
    // fields (`fileHeader`/`magic`/`crc`, Persistence.h) from the SAVE job and
    // GET-SAVE-INFO job that read them back; this site is the WRITE side and
    // corroborates the same layout independently: version(u16) then
    // blockCount(u16) packed into `fileHeader`, then retail's ONE qword store
    // of 0xFFFFFFFF80808080 spanning `magic` (low 32 bits, 0x80808080) and
    // `crc` (high 32 bits, the seed 0xFFFFFFFF) -- spelled as the two named
    // dword stores it covers.
    w->_fileHeader = (static_cast<uint32_t>(static_cast<uint16_t>(blockCount)) << 16) | 2u;
    w->_magic = 0x80808080u;
    w->_crc   = 0xFFFFFFFFu;

    w->_blockDescs.SetSize(blockCount);   // 0x18002a810
    w->_blockPtrs.SetSize(blockCount);    // 0x18002a9d0

    // LEA EAX,[RBP+0x1] / LEA R8D,[RAX+RAX*2] / SHL R8D,0x2  ==  12 * (n + 1).
    // Computed BEFORE the `TEST EBP,EBP / JLE` guard, so it is evaluated even
    // when there are no blocks; harmless, and reproduced for that reason.
    uint32_t offset = static_cast<uint32_t>(12 * (blockCount + 1));

    int wrote = 0;
    for (int i = 0; i < blockCount; ++i) {
        // Re-read the base pointer each store, as retail does: three separate
        // `MOV RCX,[RSI+0x50]` loads inside one iteration.
        w->_blockDescs._data[i].id     = blocks[i].id;
        w->_blockDescs._data[i].offset = offset;
        w->_blockDescs._data[i].size   = blocks[i].size;
        w->_blockPtrs._data[i] = blocks[i].data;
        offset += w->_blockDescs._data[i].size;   // re-read, see banner
        ++wrote;
    }

    SignalWorker(w);

    char ours[112];
    std::snprintf(ours, sizeof(ours),
                  "status=0 command=0 hdr={2,%d,0x%x%08x} descs=%d endOffset=%u",
                  blockCount, w->_crc, w->_magic, wrote, offset);
}

// ============================================================================
// 0x1800b7740 — the embedded worker's DESTRUCTOR
// ----------------------------------------------------------------------------
// void (worker /*RCX*/)
//
// THE STATED BLOCKER, RE-CHECKED RATHER THAN INHERITED. Wave 19 recorded this
// row as held back because it "joins and closes a real thread". That is TRUE
// of the body — the WaitForSingleObject at 0x1800b77c3 is INFINITE and the
// handle it waits on is the worker thread — and it is NOT a reason not to port
// it. Re-checked on this tip:
//   * the only caller is ??1Persistence@IO@@QEAA@XZ (0x1800b7fb0), which Wave
//     19 ALREADY PORTED and which already reaches this body by address;
//   * the join is a shutdown-time wait on a thread that is signalled to exit
//     one instruction earlier (`SetEvent(readyEvent)` at 0x1800b77b6), so it
//     is bounded by the worker's own loop, not by a game state;
//   * the blocker was never a PORT blocker — it is a VERIFICATION blocker, and
//     it is exactly why the oracle for this row is a shadow site and not a
//     replay. Six stale blockers in two waves; this one was real about the
//     oracle and wrong about the port.
//
// THE VTABLE STAMPS ARE LOAD-BEARING. The body writes 0x180155860 over
// *worker on ENTRY and 0x180155f20 (the base System::Thread vtable) on the way
// out. That is MSVC's ordinary derived-then-base dtor sequence and it means a
// virtual call made between the two lands in the DERIVED table. Both addresses
// are DATA doors, not code: they are resolved through ResolveAddr so the mint
// can take them over at cutover, and 0x180155860 is itself one of the census's
// 45 `internal` rows (a 2-slot vtable — WAVE23_LANE_INTERNAL.md §4C records
// that its own screen mis-called it DATA/POINTER at a 3-slot threshold, and
// says so).
//
// It does NOT call the base System::Thread destructor. The four CloseHandles
// are inlined here, and the base dtor's own work — if any — is not reached.
// Reproduced as written.
// ============================================================================
// ENDGAME B.3.9 IO (W2): THE CLASS DESTRUCTOR. The door that stood here
// stamped 0x180155860 on entry and 0x180155f20 (System::Thread's table) half
// way, both BY ADDRESS; the first is the compiler's own store on entry to
// ~PersistenceWorker, and the second -- with the SetEvent / Wait / four
// CloseHandles after it -- is exactly System::Thread::~Thread (ReleaseFields,
// src/engine/system/thread.cpp: SetEvent(hStop = worker+0x20),
// WaitForSingleObject(hThread = worker+0x10, INFINITE), CloseHandle on
// +0x10 / +0x18 / +0x20 / +0x28 in that order), which the compiler runs after
// this body with its own base-vptr store. The fields are read through the
// Persistence POD view, as every job body here reads them.
IO::PersistenceWorker::~PersistenceWorker() {
    // B.4 R-IO: the four releases are the members' destructors, which the
    // compiler runs after this (empty) body in reverse declaration order --
    //   _saves       worker+0x70  0x18002ac40 (the hand-derived
    //                             kSpec_SaveGameInfo row, array_destroy.cpp's
    //                             J2_ENTRY table)
    //   _blockPtrs   worker+0x60  0x180024aa0
    //   _blockDescs  worker+0x50  0x18002a930
    //   _pendingFilename worker+0x30, `String::~String` (retail's inline
    //                release; the field keeps pointing at the freed block)
    // -- retail's order. ~System::Thread follows (the compiler's): see the
    // banner.
}

// ============================================================================
// 0x1800b6e00 — the SAVE job, `Run`'s command-0 arm  (BATCH 8, io cluster)
// ============================================================================
// `worker` in, nothing out but the two fields every job writes: `status`
// (1 ok / 2 I/O failure) and, on the way, `crc` (left holding the final
// checksum — GET-SAVE-INFO's job to verify, not this one's).
//
// SHIPPED SEQUENCE, worker-relative offsets (Persistence-relative in
// parentheses, this file's convention):
//   1. Filename f = pendingFilename (+0x30 / P+0x40); AdjustDelimiters(f).
//   2. BinaryFileStream::Create(f) on a stack BinaryFileStream. Failure ->
//      status=2 and skip straight to teardown (retail's `bVar2` gate).
//   3. magic (+0x48/P+0x58) = 0x80808080.
//   4. crc (+0x4c/P+0x5c) = Crc32Chunked(seed=crc-as-read, blockDescData,
//      blockDescCount*0xc, chunkCap=0) — ONE uncapped pass over the
//      descriptor array, complemented once at the end. `crc` on entry is
//      whatever the enqueuer (BeginSaveGame, not yet ported) preset; retail
//      does not initialise it here.
//   5. crc = Crc32Chunked(crc, block.data, block.size, chunkCap=0x8000) for
//      each of the blockDescCount blocks in blockPtrData, IN ORDER, folding
//      into the SAME accumulator (Persistence.h's banner explains why the
//      per-block cap differs from step 4's uncapped pass).
//   6. Write, in order: fileHeader (4 bytes), {magic,crc} together (8 bytes),
//      the descriptor array (blockDescCount*0xc bytes), then each block's
//      payload (blockDescData[i].size bytes from blockPtrData[i]). ANY short
//      write aborts the remaining writes and sets status=2.
//   7. On full success, CloseFile and status=1.
//   8. Teardown: BinaryFileStream::~BinaryFileStream (closes the handle again
//      if step 6 never reached CloseFile — retail's FileInfo dtor no-ops on
//      an already--1 handle) and release the local Filename.
//
// NO ORACLE, same refusal as `Run`'s banner: this writes a real file through
// a real HANDLE and there is nothing to replay it against without writing the
// save twice. `the diff oracle` asserts the one thing this body's contract
// actually promises — status ends at 1 or 2, never left at whatever it was —
// which catches a body that returns without touching it at all.
// ============================================================================
const bool* IO::PersistenceWorker::_extFixSaveTemp = nullptr;   // fix.save_failure_notice seam

void IO::PersistenceWorker::SaveJob() {

    // ENDGAME B.3.9 IO (W2) / B.4 R-IO: the REAL class, a stack object; its
    // destructor at the close of this body is retail's teardown.
    IO::BinaryFileStream bfs;

    // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §27): fix.save_failure_notice writes
    // `<name>.tmp` and moves it over the save below, once every byte is written.
    // Read once: the whole job takes one path.
    const bool viaTemp = _extFixSaveTemp != nullptr && *_extFixSaveTemp;
    std::string tempName;
    if (viaTemp) {
        tempName = _pendingFilename._data != nullptr ? _pendingFilename._data : "";
        tempName += ".tmp";
    }

    bool created;
    {
        // Set(...) then AdjustDelimiters on the same object IS
        // `Filename(const char*)` (retail 0x180026cf0) -- see B31_RULES
        // DELIMITERS. `Set` COPIES the bytes (it does not adopt
        // `_pendingFilename`'s buffer), exactly as this ctor does.
        System::Filename f(viaTemp ? tempName.c_str() : _pendingFilename._data);

        created = bfs.IO::FileStream::Create(f);   // ENDGAME B.3.9 IO: the member

        // ~f is retail's release of the local Filename copy.
    }

    bool wroteAll = false;
    if (created) {
        _magic = 0x80808080u;

        const int32_t descCount = _blockDescs._size;
        const RtIo::BlockDesc* const descs = _blockDescs._data;
        uint32_t crc = _crc;
        if (descCount > 0) {
            crc = Crc32Chunked(crc, descs, static_cast<uint32_t>(descCount) * 0xC, 0);
        }
        _crc = crc;

        if (descCount > 0) {
            void* const* const blockPtrs = _blockPtrs._data;
            for (int32_t i = 0; i < descCount; ++i) {
                const uint32_t size = descs[i].size;
                if (size > 0) {
                    crc = Crc32Chunked(_crc, blockPtrs[i], size, 0x8000);
                    _crc = crc;
                }
            }
        }

        // Four WriteFile calls, each Platform::FileWrite (its BOOL and byte count).
        uint32_t written = 0;
        RtIO::FileInfo* file = &bfs._file;
        int32_t ok = Platform::FileWrite(file->winHandle, &_fileHeader, 4, &written);
        if (ok && written == 4) {
            // `magic` and `crc` are adjacent (+0x48, +0x4c): retail writes the
            // 8 bytes from &magic in one call.
            ok = Platform::FileWrite(file->winHandle, &_magic, 8, &written);
            if (ok && written == 8) {
                const uint32_t descBytesLen = static_cast<uint32_t>(descCount) * 0xC;
                ok = Platform::FileWrite(file->winHandle, _blockDescs._data, descBytesLen, &written);
                if (ok && written == descBytesLen) {
                    wroteAll = true;
                    void* const* const blockPtrs = _blockPtrs._data;
                    for (int32_t i = 0; i < descCount; ++i) {
                        const uint32_t size = descs[i].size;
                        ok = Platform::FileWrite(file->winHandle, blockPtrs[i], size, &written);
                        if (!ok || written != size) { wroteAll = false; break; }
                    }
                }
            }
        }

        if (viaTemp) {
            // NOT RETAIL (fix.save_failure_notice): close the temporary file, then move it
            // over the save; a short write or a failed move deletes it and the old save
            // stays. The status is stored last, so a reader that sees 1 finds the save in place.
            if (bfs._file.handle != -1) Fs()->CloseFile(bfs._file);
            const System::Filename tempPath(tempName.c_str());
            const System::Filename savePath(_pendingFilename._data);
            const System::WideString tempWide(tempPath);
            const System::WideString saveWide(savePath);
            if (wroteAll) wroteAll = Platform::FileReplace(tempWide._data, saveWide._data) != 0;
            if (!wroteAll) Platform::FileDelete(tempWide._data);
        }
        _status = wroteAll ? 1 : 2;
    } else {
        _status = 2;
    }

    // Retail's ONE close-if-still-open check sits here, UNCONDITIONALLY,
    // after the success/failure branches above rather than inside either of
    // them: a failed Create left the handle at -1 (never opened) and this is
    // a no-op; a full write success already matches (nothing above closed it
    // yet, so this is where retail's real close happens); a short write
    // leaves the handle open for this same check to close. One site, not
    // three, exactly as FUN_1800b6e00's tail reads.
    if (bfs._file.handle != -1) Fs()->CloseFile(bfs._file);
    // ~bfs is retail's ~BinaryFileStream.
}

// ============================================================================
// WAVE 28 LANE SLOTS — the worker's `Run()`, slot 0 of vtable 0x180155860
// ============================================================================
//
//   0x1800b7260   49 bytes   ?Run@<unnamed worker>@ — UNEXPORTED, address only
//
// This is the ONE slot the census's `0x180155860  1 / 2` row was missing; slot
// 1 (`?OnCompleted@Thread@System@@UEAAXXZ`, 0x18000c590) has been ours for
// waves. With this body the row is 2 / 2 COMPLETE.
//
// THE SHIPPED BODY, from `objdump -d --start-address=0x1800b7260
// --stop-address=0x1800b7292 "C:/GOG Games/Banished/Runtime-x64.dll"` — the
// retail file on disk, true-VA addressing, no Ghidra involved:
//
//   1800b7260: 8b 51 3c        mov  0x3c(%rcx),%edx        worker->command
//   1800b7263: 85 d2           test %edx,%edx
//   1800b7265: 74 24           je   1800b728b -> jmp 0x1800b6e00     command 0
//   1800b7267: ff ca           dec  %edx
//   1800b7269: 74 1b           je   1800b7286 -> jmp 0x1800b6aa0     command 1
//   1800b726b: ff ca           dec  %edx
//   1800b726d: 74 12           je   1800b7281 -> jmp 0x1800b69c0     command 2
//   1800b726f: ff ca           dec  %edx
//   1800b7271: 74 09           je   1800b727c -> jmp 0x1800b65b0     command 3
//   1800b7273: ff ca           dec  %edx
//   1800b7275: 75 19           jne  1800b7290 -> repz ret            (default)
//   1800b7277:                 jmp  0x1800b6290                      command 4
//
// It is a five-way jump table on ONE field and nothing else: no allocation, no
// synchronisation, no state of its own. Every arm is a TAIL JMP, so the job
// body inherits this frame and `Run`'s own contribution is the dispatch.
//
// ---------------------------------------------------------------------------
// THE COMMAND ENCODING IS CORROBORATED BY AN INDEPENDENT WITNESS
// ---------------------------------------------------------------------------
// `src/engine/io/Persistence.h` has said since Wave 19 lane PERSIST, from
// reading the ENQUEUE side (`movl $3,0x4c(%rbx)` and friends in the public
// entry points):
//
//     int32_t command;   // +0x4C — 1 load · 2 delete · 3 info · 4 list
//
// `worker+0x3c` IS `Persistence+0x4c` (the worker subobject starts at +0x10),
// so the dispatch above assigns exactly that meaning to 1, 2, 3 and 4 — derived
// here from the CONSUMER, there from the PRODUCER, and the two agree with no
// shared premise. Command 0 is the one the header does not list, and this file
// already names it: `RtIo_PersistenceWorker_EnqueueSave` (0x1800b75c0) is the
// only enqueue path that does not write a non-zero command, i.e. **0 is SAVE**.
// That is a reading of an ABSENCE and is labelled INFERRED, not measured.
//
// ---------------------------------------------------------------------------
// WHAT THIS GROWS — five new address doors, declared, not discovered later
// ---------------------------------------------------------------------------
// reference_porting_can_grow_the_cutover_surface.md. Porting a dispatcher makes
// its five arms OURS to satisfy at cutover. None of the five is exported —
// checked individually against the retail export directory with pefile; the
// nearest preceding export to every one of them is
// `?Serialize@PackageFile@IO@@UEAA_NAEAVStream@2@@Z` several hundred bytes
// back, i.e. none of them has a name to register under and all five need
// ADDRESS doors. `AEAA` means private, not unexported, so "unexported" was
// MEASURED rather than read off a mangling
// (reference_aeaa_is_private_not_unexported.md).
//
//   SYMBOL      (unexported) 0x1800b6e00  the SAVE job
//   SYMBOL      (unexported) 0x1800b6aa0  the LOAD job
//   SYMBOL      (unexported) 0x1800b69c0  the DELETE job
//   SYMBOL      (unexported) 0x1800b65b0  the GET-SAVE-INFO job
//                                         *** PAID 2026-09-12, BATCH 8b ***
//                                         src/engine/io/persistence_saveinfo_job.cpp
//                                         registers this address.
//   SYMBOL      (unexported) 0x1800b6290  the GET-SAVE-LIST job
//                                         *** PAID 2026-09-12, batch 5 ***
//                                         src/engine/io/persistence_savelist_job.cpp
//                                         registers this address. With the SAVE
//                                         job ported in this file and BATCH 8b
//                                         above, TWO of the five remain open:
//                                         LOAD (0x1800b6aa0) and DELETE
//                                         (0x1800b69c0).
//   CONDITION   each door is live the moment `Run` is dispatched out of a
//               PROMOTED/minted 0x180155860 with the matching command pending.
//   CONSEQUENCE with the DLL present each resolves to the shipped body and
//               behaviour is unchanged. At cutover an unported arm resolves
//               NULL and the worker thread executes address 0 — so these five
//               are a cutover obligation this file CREATES and does not pay.
//
// Each address is spelled as a LITERAL inside its own `ResolveAddr` call, not
// routed through a shared helper, because `tools/runtime_census.py` and
// `tools/addr_door_audit.py` both find doors by grepping for that literal and
// a door reached through a variable is invisible to both
// (reference_literal_grep_blind_to_variables.md — and GFXMESH found a door
// hidden this way in `mesh.cpp:201` for 21 waves).
//
// > **[ADDRESSEE — the FOUR remaining Persistence job bodies]** (was five;
// > 0x1800b6290 landed 2026-09-12, batch 5) the Wave 29 lane scoped
// > to IO's remaining `internal` address rows, or the cutover lane that finds
// > them in the `[RtRawResidue]` list, whichever comes first. They are the
// > save/load/delete/info/list workers themselves and each is several hundred
// > bytes; they are NOT slot work and were deliberately not attempted here.
//
// ---------------------------------------------------------------------------
// NO ORACLE — and the refusal is structural
// ---------------------------------------------------------------------------
// `Run` is the thread entry of a worker that is blocked on an event for the
// whole of an ordinary run. Calling it a second time "for comparison" would
// execute the pending save/load/delete a SECOND time — writing a file twice or
// deleting an already-deleted one. There is nothing to compare and nothing safe
// to replay. A `the diff oracle` would also be dishonest here: the body has NO
// post-condition of its own (it stores nothing), so a self-check could only
// assert something the body never wrote
// (reference_oracle_must_not_compare_unwritten_bytes.md). **No site is added.**
//
// UNVERIFIED: this body has not been observed executing.
//
// NO NULL TEST ON THE RESOLVED ARM, on purpose. Retail tail-JMPs into each job
// with no check, and an `if (f)` here would convert "this arm is unported at
// cutover" from a loud fault at address 0 into a SILENTLY SKIPPED SAVE. The
// guard would not restore the behaviour, it would hide its absence — CLAUDE.md,
// "Don't 'fix' a faithful crash with a null check". B3R RT-IO: the four arms
// are direct calls now, so there is no pointer left to test.
// ============================================================================
// ENDGAME B.3.9 IO (W2): slot 0 of the class's compiler table. The door that
// stood here was registered BY ADDRESS (0x1800b7260) so the minted 0x180155860
// table's unnamed slot 0 reached it; that table is retired. The four arms that
// are not this file's are called by name (B3R RT-IO; they were address doors).
void IO::PersistenceWorker::Run() {
    switch (_command) {
        case 0:   // SAVE — see the encoding note above (INFERRED, absence-based)
            SaveJob();          // 0x1800b6e00, above
            return;
        case 1:   // LOAD
            LoadJob();          // 0x1800b6aa0, closure_batch2b.cpp
            return;   // retail tail-JMPs
        case 2:   // DELETE
            DeleteJob();        // 0x1800b69c0, closure_batch2b.cpp
            return;   // retail tail-JMPs
        case 3:   // GET SAVE INFO
            GetSaveInfoJob();   // 0x1800b65b0, persistence_saveinfo_job.cpp
            return;   // retail tail-JMPs
        case 4:   // GET SAVE LIST
            GetSaveListJob();   // 0x1800b6290, persistence_savelist_job.cpp
            return;   // retail tail-JMPs
        default:
            // retail's `repz ret` — every other command value is a no-op.
            return;
    }
}
