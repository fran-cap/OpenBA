// ============================================================================
// src/engine/io/persistence_savelist_job.cpp — BATCH 5 (2026-09-12)
//   IO::Persistence's worker, COMMAND 4 — the GET-SAVE-LIST job.
//   retail 0x1800b6290, UNEXPORTED, 780 bytes / 218 instructions.
// ============================================================================
//
// This is one of the five address doors `persistence_worker.cpp` opened
// when it ported the worker's `Run` dispatcher (0x1800b7260) and declared, in
// its "WHAT THIS GROWS" section:
//
//     SYMBOL      (unexported) 0x1800b6290  the GET-SAVE-LIST job
//     CONSEQUENCE … At cutover an unported arm resolves NULL and the worker
//                   thread executes address 0 — so these five are a cutover
//                   obligation this file CREATES and does not pay.
//
// This pays one of the five. The other four (SAVE 0x1800b6e00, LOAD
// 0x1800b6aa0, DELETE 0x1800b69c0, GET-SAVE-INFO 0x1800b65b0) are still open;
// that file's addressee note is updated in the same commit rather than left to
// claim all five are outstanding.
//
// ---------------------------------------------------------------------------
// WHAT IT DOES
// ---------------------------------------------------------------------------
//   1. `Array<SaveGameInfo>::SetSize(&p->saves, 0)`      0x1800b62b3 / 0x18002aae0
//      — clears the previous list WITHOUT freeing its buffer (see array.cpp's
//        note on that address: the MemFree on its realloc arm is unreachable
//        with a new size of 0, but the shrink-path element destroy is NOT, and
//        that is what releases the old filename Strings).
//   2. `FindFiles(fs, GetSaveDirectory() + "\\Save\\", "*.sav", false, &found)`
//                                                        0x1800b6315 / 0x1800ad7a0
//   3. per hit: `name = found[i].GetPathFilename(true)`   0x1800b63c5 / 0x1800c6740
//               `stamp = fs->GetFileTimeStamp(found[i])`  0x1800b6445 / 0x1800ae270
//               `Array<SaveGameInfo>::SetSize(&p->saves, count+1, &entry)`
//                                                        0x1800b645f / 0x18002ae20
//   4. `p->status = 1`                                    0x1800b64c8
//   5. release every found Filename and free the array's buffer.
//
// The two string literals are read out of .rdata, not guessed:
//     0x180155850  "*.sav"    the FindFiles pattern
//     0x180155858  "\\Save\\" the suffix appended to the save directory
// They are two of three consecutive literals in one block, the first of which
// (0x180155840, "%sSave\\%s.sav") is the SAVE path format the sibling jobs use
// — so picking the wrong one is easy and would send FindFiles at the wrong
// directory with a format string as a pattern.
//
// ---------------------------------------------------------------------------
// `this` IS THE WORKER SUBOBJECT, NOT THE Persistence
// ---------------------------------------------------------------------------
// `??0Persistence@IO@@` embeds a Thread-derived worker at +0x10 and stamps
// vtable 0x180155860 over it, so every offset in the shipped listing is
// `Persistence offset - 0x10`:  worker+0x38 is `status` (+0x48), worker+0x70
// is `saves` (+0x80), worker+0x78 is `saveCount` (+0x88). This body recovers
// the `Persistence*` and uses the NAMED fields, exactly as
// persistence_worker.cpp does, so a future change to the layout breaks the
// static_asserts rather than silently shifting a raw offset here.
//
// ---------------------------------------------------------------------------
// `GetFileTimeStamp` IS GIVEN THE FULL FOUND PATH, NOT THE BASE NAME
// ---------------------------------------------------------------------------
// 0x1800b6437 loads `R8 = found._data + i*8` — the same element
// `GetPathFilename` was called on, NOT the base name it produced. The base name
// goes into the entry's display `name` and the FULL path goes to the
// filesystem. Reading it the other way round would stat a file in the process
// working directory, find nothing, and stamp every save 0.
//
// *** THIS PORT SETTLES WHAT `SaveGameInfo::sortKey` IS. ***
// src/engine/io/Persistence.h named +0x00 `sortKey` "for what it is used
// as" — `?UpdateList@SaveLoad@Save@@` copies it into SaveInfo+0x30 and a qsort
// comparator orders it descending, unsigned. This is its PRODUCER, and it is
// the raw FILETIME qword `GetFileTimeStamp` returns: a 100 ns tick count since
// 1601, which is exactly why an unsigned descending sort puts the newest save
// first. The header comment is completed in the same commit.
//
// ---------------------------------------------------------------------------
// NO NULL GUARDS ARE ADDED
// ---------------------------------------------------------------------------
// Retail indexes `found._data` without a bounds check, dereferences the
// `GetPathFilename` result without testing it, and frees the buffer without
// testing the count a second time. All transcribed. The only tests present are
// retail's own: the refcount-zero test in each release, and the
// `if (found._data)` before the final MemFree.
//
// ---------------------------------------------------------------------------
// ORACLE — NONE, and the refusal is structural
// ---------------------------------------------------------------------------
// The job's entire effect is a REBUILD of `p->saves` plus `p->status = 1`.
// Running the shipped body afterwards for comparison would rebuild the same
// list from the same directory on top of ours — the second run's `SetSize(0)`
// releases the Strings the first run just adopted, so the comparison is not
// merely vacuous, it destroys the subject it would compare. (Same shape
// avl_trees.cpp records for its inserts: a destructive mutator on shared state
// has no live replay.) And it runs on the WORKER THREAD, where the diff
// registry's counters are not synchronised.
//
// What a boot checks instead is end to end and is not weak: the save browser
// lists the files that are on disk, in modification order, with their base
// names. A wrong pattern, a wrong directory, a wrong stamp or a wrong element
// spec each show up there immediately.
//
// UNVERIFIED UNTIL A SAVE-LIST REFRESH RUNS: an autostart boot that never opens
// the load dialog does not reach this body, and `calls=0` is not verification
// (reference_uncalled_port_is_not_verified.md).
//
// PORTABILITY: no <windows.h>. The Win32 half of this path lives behind
// `FileSystem::FindFiles` and `FileSystem::GetFileTimeStamp`, which are
// `_win` files of their own (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <new>        // placement new into a hidden-return-slot / a raw struct field

#include "io/FileSystem.h"
#include "io/Persistence.h"
#include "io/PersistenceWorker.h"
#include "system/Filename.h"
#include "system/String.h"
#include "system/string_internal.h"
#include "system/MemoryAllocator.h"

using RtIo::SaveGameInfo;

namespace {

// B.4 Z LAST IO: the local `FilenameArray` that stood here is gone: FindFiles'
// out-parameter `RtIO::FilenameArray` holds `System::Filename` elements now,
// and this job owns each one individually (a real destructor call each).

// B3R RT-IO: the eight callees this body reached through RtResolveCached are
// called by name: String::Set (0x1800c95d0), FileSystem::GetSaveDirectory
// (0x1800acfd0), operator+(String, const char*) (0x1800c92d0),
// FileSystem::FindFiles (0x1800ad7a0), Filename::GetPathFilename (0x1800c6740),
// FileSystem::GetFileTimeStamp (0x1800ae270), MemoryAllocator::MemFree
// (0x1800c7ad0).

void MemFree(void* p) {   // the null test is INSIDE 0x1800c7ad0
    ::System::MemoryAllocator::MemFree(p);
}

// 0x180155850 and 0x180155858 — see the banner for why the neighbouring
// "%sSave\\%s.sav" at 0x180155840 is NOT one of these.
const char* const kSavePattern = "*.sav";
const char* const kSaveSubDir  = "\\Save\\";

}  // namespace

// ============================================================================
// The worker's command-4 body.                             retail 0x1800b6290
//
// `worker` is `Persistence + 0x10`; see the banner.
// ============================================================================
void IO::PersistenceWorker::GetSaveListJob() {
    // ---- 1. clear the previous list (buffer kept) --------------------------
    // ENDGAME B.3.2 step 2 — 0x18002aae0 is the SAME `Array<SaveGameInfo>` as
    // 0x18002ae20 below with the new size folded to 0 (array.cpp's
    // kSpec_SaveGameInfo banner); NOT trait-expressible (see array_internal.h),
    // so `_saves` names that hand-derived row as its spec (PersistenceWorker.h).
    _saves.SetSize(0);                                          // 0x1800b62b3

    // `IO::FileSystem::_singleton`, the class static (`0x1801fc6a8`).
    const ::IO::FileSystem* const fs = ::IO::FileSystem::_singleton;   // 0x1800b62b8

    RtIO::FilenameArray found = { nullptr, 0, 0 };              // [RBP-0x30]

    // ---- 2. FindFiles(saveDir + "\Save\", "*.sav", recursive=false) -------
    // Scoped to a block, declared in this order, so the block's automatic
    // teardown lands at the same point retail's manual release did
    // (0x1800b631b..0x1800b636c) and runs in retail's own order (dir, then
    // saveDir, then pattern — reverse of declaration).
    {
        System::String pattern;                                     // [RBP+0x48]
        pattern.Set(kSavePattern);                                  // 0x1800c95d0

        System::Filename saveDir(                                   // [RBP+0x58]
            fs->GetSaveDirectory());                                // 0x1800b62e5

        // ??HSystem@@YA?AVString@0@AEBV10@PEBD@Z, 0x1800c92d0. AsString()
        // is the conversion retail performs by pointer cast (Filename.h).
        System::String dir(saveDir.AsString() + kSaveSubDir);       // [RBP+0x50]

        fs->FindFiles(dir, pattern,                                 // 0x1800b6315
            false, &found);

        // ~dir, ~saveDir, ~pattern are retail's release sequence at
        // 0x1800b631b/0x1800b6343/0x1800b636c, now the three destructors
        // below, firing in exactly that order at the close of this block.
    }

    // ---- 3. one SaveGameInfo per hit --------------------------------------
    for (int32_t i = 0; i < found.size; ++i) {                  // 0x1800b6397
        SaveGameInfo entry;                                     // name: 0x1800b63a8's zero
        entry.sortKey = System::DateTime{};                     // 0x1800b63a0

        {
            // `Filename::GetPathFilename(true)` returns a Filename BY VALUE,
            // so the slot is hidden in RDX — retail reuses `pattern`'s dead
            // stack cell for it (`LEA RDX,[RBP+0x48]`). Filled directly: the
            // callee treats the pointer as raw memory, the same as any other
            // hidden-return-slot target.
            System::Filename baseName(found.data[i].GetPathFilename(true));   // 0x1800b63c5
            System::Filename* const got = &baseName;
            // UNGUARDED deref of the returned pointer, as retail writes it.
            // Adopt the buffer, AddRef if non-null, into the zeroed `name` --
            // what the C body wrote out by hand at 0x1800b63ce. On a null
            // String the assignment is the no-op the adopt was.
            entry.name = got->AsString();
            // ~baseName is retail's release at 0x1800b63da.
        }

        // THE FULL FOUND PATH, not the base name — see the banner.
        entry.sortKey = fs->GetFileTimeStamp(found.data[i]);    // 0x1800b644a

        // retail 0x18002ae20 — Array<SaveGameInfo>::SetSize(n, &fill), the
        // append idiom. Hand-derived row (kSpec_SaveGameInfo), bridged the
        // same way as step 1's clear above.
        _saves.SetSize(_saves._size + 1, entry);                // 0x1800b645f

        // Retail's release at 0x1800b6465 is `entry`'s destructor, as it
        // leaves the iteration.
    }

    // ---- 4. publish ---------------------------------------------------------
    _status = static_cast<int32_t>(Status::Done);               // 0x1800b64c8

    // ---- 5. tear the FindFiles array down ----------------------------------
    // `found.data` is a raw buffer of `System::Filename` from an external
    // allocator (not a container with its own RAII), so each element's
    // release is still an explicit call -- now the destructor itself.
    for (int64_t i = 0; i < static_cast<int64_t>(found.size); ++i)
        found.data[i].~Filename();                              // 0x1800b64e4
    if (found.data) MemFree(found.data);                        // 0x1800b6541
}
