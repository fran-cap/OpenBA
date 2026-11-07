// ============================================================================
// src/engine/io/persistence_begin_saveinfo.cpp — Wave 19 lane PERSIST
// ============================================================================
//
// One row: ?BeginGetSaveGameInfo@Persistence@IO@@QEAA_NAEBVString@System@@H@Z
// (retail 0x1800b7870, ordinal 1642).
//
// It sat in a _win file (`persistence_saves_win.cpp`, until Phase D.4 THREADS
// E.5) for one reason: it is the ONLY member of this lane's seven that reaches
// the Win32 event API in its own body. Its two siblings
// (BeginDelete, BeginLoad) reach the same two calls, but through an
// unexported DLL helper, so their C++ stays platform-neutral and lives in
// persistence_saves.cpp. That asymmetry is retail's, not ours: 0x1800b7870
// does not delegate to an enqueue helper, it inlines the whole issue
// sequence.
//
// The two calls are Platform::EventReset / EventSet (platform_sync.h, THREADS
// E.2). The IAT slots retail calls were identified from the shipped binary, not
// guessed: 0x18014b198 is KERNEL32!ResetEvent and 0x18014b368 is
// KERNEL32!SetEvent, read out of Runtime-x64.dll's import directory.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>    // std::move -- the pointer steal out of a POD out-param
#include "io/Persistence.h"
#include "platform_sync.h"   // Platform::EventReset / EventSet
#include "collection/Collection.h"   // ENDGAME B.3.2 step 2
#include "system/string_internal.h"


// GetSaveFilename (persistence_saves.cpp) is called directly as the member:
// retail's 0x1800b789f is a direct rel32 CALL to 0x1800b7c80.


// ============================================================================
// bool IO::Persistence::BeginGetSaveGameInfo(const String& name, int count)
// retail 0x1800b7870
// ----------------------------------------------------------------------------
// Instruction-order note: retail writes `command = 3` (0x1800b78b7) BEFORE the
// filename assignment and `status = 0` (0x1800b7903) after it. The order is
// preserved below even though nothing in this thread can observe the
// difference — the worker thread can, and it is woken only by the SetEvent at
// the end, so any reordering here is a change to what a racing reader could
// see. Faithful transcription costs nothing; reasoning about the race costs
// a wave.
// ============================================================================
// The body is the member `IO::Persistence::BeginGetSaveGameInfo`, below.

// ============================================================================
// `IO::Persistence::BeginGetSaveGameInfo` -- the member body
// ============================================================================
// Defined in `namespace IO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2). Every
// POD is spelled `RtIo::` or `RtSystem::` and every real class `System::`
// (B34_RULES section 7.2); note this file's POD namespace is `RtIo`, LOWERCASE
// o.
//
// NOTHING ELSE CHANGED: same callees in the same order, the refusal test on bit
// 0 of `_flags` still first, the two array teardowns still between the filename
// and the command fields, the guarded String assignment still guarded on the
// BUFFER, and ResetEvent before SetEvent. The `GetSaveFilename` call, which was
// already a DIRECT call into the sibling file's door (retail's 0x1800b789f is a
// direct rel32 CALL), is now a direct MEMBER call of that same body -- so which
// body runs is unchanged, and the POD-plus-move round trip it needed collapses
// into one construction with the same single AddRef.
namespace IO {

bool Persistence::BeginGetSaveGameInfo(const System::String& name, int count) {

    if (_worker.IsWorking()) return false;

    // C++17 constructs GetSaveFilename's result STRAIGHT INTO `fn`.
    // `AsString()` is the same eight bytes (Filename IS a String,
    // layout-identically), which is what retail assigns from.
    System::Filename fnFile(GetSaveFilename(name));
    const System::String& fn = fnFile.AsString();

    // Both are the `Collection::Array<T>::~Array()` bodies (0x18002a930, 0x180024aa0),
    // run on the live members exactly where retail calls them.
    // NOT RETAIL (spelling only): spelled DestroyAndFree, the same ClearCallback body,
    // because an explicit destructor call would end the members' lifetime
    // ([basic.life]) while the worker job still uses them.
    _worker._blockDescs.DestroyAndFree();                            // 0x18002a930
    _worker._blockPtrs.DestroyAndFree();                             // 0x180024aa0

    _worker._command = 3;

    // The guarded String assignment at 0x1800b78be..0x1800b7903 IS
    // `String::operator=` (retail 0x1800c9810): retail skips the whole
    // release/adopt when the two buffers are already the same pointer, which
    // is not just an optimisation — releasing first would drop the last
    // reference to the buffer it is about to re-adopt.
    if (_worker._pendingFilename._data != fn._data) {
        _worker._pendingFilename = fn;
    }

    _worker._status     = static_cast<int32_t>(PersistenceWorker::Status::Pending);
    _worker._blockCount = count;

    Platform::EventReset(_worker._hFinished);                        // doneEvent
    Platform::EventSet(_worker._hStart);                             // workEvent

    // ~fnFile is retail's release.
    return true;
}

}  // namespace IO

// ---------------------------------------------------------------------------
