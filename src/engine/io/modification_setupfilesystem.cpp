// ============================================================================
// src/engine/io/modification_setupfilesystem.cpp — BATCH 5 (2026-09-12)
//   `?SetupFileSystem@Modification@IO@@QEAAXXZ`
//   retail 0x1800aebb0, ordinal 4030, 2617 bytes / 709 instructions.
// ============================================================================
//
//     void IO::Modification::SetupFileSystem()
//
// APPLY THE MOD LIST. Called when the mod set has changed (`_dirty`), it
// rebuilds the packfile stack from the enabled mods and then RELOADS exactly
// those already-loaded resources whose compiled asset has moved to a different
// packfile — leaving everything else alone. It is the whole of "Ok" in the mods
// menu, and it runs at boot on any install with mods enabled, which is why it
// is on the cutover closure's critical path.
//
// Shape, in five acts:
//
//   1. SNAPSHOT      For every loaded `Object::External`, record
//                    {compiledPath -> packfileId} in a stack-local
//                    `Collection::Set<Pair<Filename,uint32>>`.
//   2. RESTACK       `FileSystem::ClosePackages()`, clear the user-pack list,
//                    refill it from the ENABLED mods in REVERSE order (writing
//                    each mod's index into `Mod::order` on the way),
//                    `FileSystem::LoadPackages()`.
//   3. DIFF          Walk the externals again. For each, look its compiled path
//                    up in the snapshot; if the packfile id CHANGED:
//                      * the asset still exists (in a packfile, or loose on
//                        disk)  -> queue {typeId, External*, path} for reload
//                      * it does not                   -> `ForceUnload` it
//   4. RELOAD        Per queued entry: virtual slot 7 on the object, then
//                    re-run its type's IN-PLACE FACTORY over the same storage,
//                    then re-point `_path` and `External::Load` it.
//   5. TEARDOWN      `_dirty = false`; release the queue and the snapshot.
//
// ---------------------------------------------------------------------------
// WHAT THIS PORT NEEDED, AND WHAT IT DID NOT OPEN
// ---------------------------------------------------------------------------
// Every callee is ALREADY OURS or is ported in this same commit. Enumerated
// rather than asserted (reference_porting_can_grow_the_cutover_surface.md):
//
//   0x1800be9d0 FindNext           ported, external.cpp
//   0x180006250 External::GetPath  ported, external.cpp
//   0x1800bec10 GetCompiledPath    ported, external_compiled.cpp
//   0x1800ab050 GetPackfileId      ported, filesystem.cpp
//   0x1800abab0 ClosePackages      ported, filesystem_packages.cpp
//   0x1800ab460 LoadPackages       ported, filesystem_packages.cpp
//   0x1800ab180 ExistsInPackfile   ported, filesystem.cpp
//   0x1800ad7a0 (not reached here)
//   0x1800bc620 External::Load     ported, external_load.cpp
//   0x1800c6970 AdjustDelimiters   ported, filename.cpp
//   0x1800cab50 WideString ctor    ported, widestring.cpp
//   0x1800098d0 ~Array<Filename>   ported, modification.cpp
//   0x180026880 Array<Filename>::SetSize  ported, filesystem_find.cpp
//   0x1800beec0 ForceUnload        ported THIS COMMIT, object/external_forceunload.cpp
//   0x180027380 Set::Insert        ported THIS COMMIT, collection/set_avl_pathid.cpp
//   0x180027970 Set subtree free   ported THIS COMMIT, collection/set_avl_pathid.cpp
//   0x180028cd0 Array SetSize      ported THIS COMMIT, collection/array.cpp
//   0x1800c7ad0 MemFree            ported, system/allocator.cpp
//
// The remaining callees are VIRTUAL and are the objects' own: slot 2
// (`GetTypeId`) and slot 7 on `Object::External`, and `BaseTypeInfo::
// inPlaceFactoryFn` (+0x28) on the type record. Those are dispatch, not doors.
//
// ---------------------------------------------------------------------------
// THE SNAPSHOT'S COMPARATOR IS CASE-SENSITIVE
// ---------------------------------------------------------------------------
// Act 3's descent is INLINED here (0x1800aee50..0x1800aeeef) and is the same
// plain signed `strcmp` act 1's `Set::Insert` uses — NOT the `_stricmp` the
// resource tree and the packfile index use. Both sides of the comparison are
// `GetCompiledPath` outputs from the same `External::GetPath`, so they agree
// by construction; the sensitivity still has to be right, because a
// case-insensitive descent would take a DIFFERENT PATH through a tree that was
// built with a case-sensitive one and could miss an entry that is present. It
// is transcribed in `SnapshotFind` below from this body's own listing.
//
// ---------------------------------------------------------------------------
// THE RELOAD IS `inPlaceFactoryFn`, NOT `factoryFn`
// ---------------------------------------------------------------------------
//   1800af330  MOV RAX,[RAX+0x10]     node + 0x10 == TypeRecord::info
//   1800af337  CALL [RAX+0x28]        BaseTypeInfo::inPlaceFactoryFn(storage)
//                                     with RCX = the EXISTING External*
// `+0x20` (the allocating factory) would return a NEW object and leak the old
// one, and every pointer the rest of the engine holds to the old one would go
// stale. Retail re-constructs IN PLACE so the identity survives the reload,
// which is the entire reason a mod change does not invalidate live handles.
// `BaseTypeInfo::inPlaceFactoryFn` at +0x28 is pinned in
// src/engine/object/TypeInfo.h.
//
// THE TYPE LOOKUP'S MISS ARM PRODUCES NULL AND RETAIL DEREFERENCES IT
// (0x1800af32e `XOR EAX,EAX` then 0x1800af330 `MOV RAX,[RAX+0x10]`). Faithful;
// no guard is added. The queue is built from live objects whose type ids came
// from their own vtables, so a miss means the type tree has lost a type that
// exists — a state the engine does not survive anyway.
//
// ---------------------------------------------------------------------------
// ACT 2 RUNS THE MOD LIST BACKWARDS, AND THE INDEX IT WRITES IS THE LOOP INDEX
// ---------------------------------------------------------------------------
//   1800aed49  EBX = _count - 1;  RDI = EBX << 7   (sizeof(Mod) == 0x80)
//   1800aed64  MOV [RDI+RAX+0x20],EBX              Mod::order = i
//   1800aed68  CMP byte [RDI+RAX+0x50],0           Mod::enabled
//   1800aed7e  Array<Filename>::SetSize(fs->userPackFilenames, count+1,
//                                       &mods[i].file)
// So the LAST mod in the list is appended FIRST, i.e. the pack stack ends up in
// reverse list order, and `Mod::order` is re-stamped on EVERY mod (enabled or
// not) as a side effect. Both are retail's and both are visible to the player:
// the stack order is mod precedence.
//
// ---------------------------------------------------------------------------
// ACT 3'S "DOES IT STILL EXIST" TEST IS TWO GATES, NOT ONE
// ---------------------------------------------------------------------------
//   1800aef90  if (fs->existsInPackfileGate && ExistsInPackfile(p)) -> RELOAD
//   1800aefab  if (fs->platformExistsGate) {
//                  WideString w(p); ok = GetFileAttributesW(w) != -1;
//                  release w;  if (ok) -> RELOAD
//              }
//              otherwise -> ForceUnload
// This is `FileSystem::Exists` OPEN-CODED with its two gate bytes (+0x18,
// +0x19, both named in src/engine/io/FileSystem.h from `Exists`'s own
// `CMP byte [RCX+0x18],0`). It is NOT a call to `Exists`, and the difference
// matters: when BOTH gates are off, `Exists` would be asked and this code is
// not — it goes straight to `ForceUnload`, unloading everything whose packfile
// id moved. Transcribed as written.
//
// ---------------------------------------------------------------------------
// WHY THIS IS **NOT** A `_win` FILE EVEN THOUGH IT CALLS GetFileAttributesW
// ---------------------------------------------------------------------------
// PLAN.md §4.1 decision 1 draws the platform seam where the ORIGINAL tree draws
// it, and `IO\Modification.cpp` has no `_win` twin: retail inlined one Win32
// call into an otherwise platform-neutral body. Splitting the whole function
// into a `_win` file to carry one `GetFileAttributesW` would move 700
// instructions of portable logic behind the seam. Instead the one call is
// isolated in `PlatformFileExists` below; since Phase D.4 THREADS E.6 it is
// Platform::PathAttributes (platform_file.h) and this file includes no OS header.
//
// ---------------------------------------------------------------------------
// ORACLE — NONE, and the refusal is structural
// ---------------------------------------------------------------------------
// The function CLOSES AND REOPENS EVERY PACKFILE and then destroys and
// re-constructs live objects in place. Running the shipped body a second time
// for comparison would do all of that again on top of our result — and the
// second pass's snapshot would be taken from the state the first pass left, so
// its diff would be EMPTY and the comparison vacuously green. Worse, act 4 is
// not idempotent on a failed reload: an object re-constructed in place twice
// has been through its own destructor twice.
//
// What a boot checks, and it is not weak: this function runs whenever the mod
// list is dirty, and a defect anywhere in it is immediately visible as the
// wrong packfile stack (wrong assets), a resource that vanished (a wrong
// ForceUnload), or a crash in act 4 (a wrong factory slot). The
// `BANISHED_AUTOMENU_BUTTON=7` mods route in CLAUDE.md drives it end to end.
//
// UNVERIFIED ON AN UNMODDED BOOT: with `_dirty` false the body returns at
// 0x1800aebdb before doing anything, so a plain autostart run reaches the first
// instruction and nothing else. `calls=0` is not verification
// (reference_uncalled_port_is_not_verified.md).
// ============================================================================

#include "platform_file.h"    // Platform::PathAttributes (GetFileAttributesW) — see the banner.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>                // placement new -- see ReloadEntry::file below

#include "io/FileSystem.h"
#include "io/Modification.h"
// TypeInfo.h MUST come before External.h. External.h:343 includes it from
// INSIDE `namespace RtObject`, so if it is first seen through that include the
// guard leaves every name as `RtObject::RtObject::TypeNode`. Including it here
// at global scope first makes the nested include a no-op and the names come out
// as `RtObject::TypeNode`.
#include "object/TypeInfo.h"
#include "object/External.h"
#include "system/Filename.h"
#include "system/String.h"
#include "system/string_internal.h"
#include "system/MemoryAllocator.h"
#include "system/WideString.h"
#include "collection/Collection.h"   // rtpathid::Insert / SubtreeFree, RtColl_ArraySetSizeFill_180028cd0
#include "io/Modification.h"         // RtColl_ArrayFilenameDtor_1800098d0
#include "../collection/set_avl_pathid.h"          // rtpathid::Insert / SubtreeFree, PathIdNode / PathIdSet
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "collection/fold_index.h"   // block PKGCACHE: OPENBA_PATHINDEX_CHECK (the debug exe's check)

#include <cstring>
#include <vector>

namespace RtIO {


namespace {

// The snapshot's element, node and {root, count} header are rtpathid's
// (collection/set_avl_pathid.h): the Set is a STACK LOCAL of this function
// ([RBP-0x49] root, [RBP-0x41] count), built by 0x180027380 and torn down
// in-line at the end.
using rtpathid::PathIdNode;
using rtpathid::PathIdSet;

// The reload queue's element is `RtIO::ReloadEntry` (types/io/Modification.h,
// beside `Mod`): `file` stays the POD -- see the ENDGAME B.3.1 note on the
// queueing code below, which promotes `entry.file` to the class only for the
// span where IT constructs and releases it.

struct ReloadArray {
    ReloadEntry* data;      // [RSP+0x30]
    int32_t      size;      // [RSP+0x38]
    int32_t      capacity;
};

// ---- the callees ------------------------------------------------------
// B3R RT-IO: every callee below was a resolve door (RtResolveCached by name,
// ResolveAddr by address, RT_EXPORT_ONCE); each is called by name now.

void MemFree(void* p) {   // the null test is INSIDE 0x1800c7ad0
    ::System::MemoryAllocator::MemFree(p);
}

// Retail's inlined String release, with its asymmetry: the FREEING branch
// leaves the slot holding `data - 8` and only the still-referenced branch
// nulls it. Every one of this body's ~12 release sites has that shape.
void ReleaseInline(char** slot) {
    char* p = *slot;
    if (!p) return;
    if (--reinterpret_cast<int32_t*>(p)[-1] == 0) {  // CAST-OK: string_header
        *slot = p - 8;
        MemFree(p - 8);
    } else {
        *slot = nullptr;
    }
}

// ENDGAME B.3.1 step 2: the file-local `ReleaseThenAdopt` (retail's inlined
// `Filename::operator=`, the `!=` guard on the buffer at 0x1800af06c and
// 0x1800af341) is gone -- both call sites are now the class's own
// `operator=`/copy ctor, which carry the identical guard (Filename.h's
// DELIMITERS note).

// MSVC's inlined `strcmp`, transcribed from this body's own listing: the scan
// compares UNSIGNED bytes and the answer is a SIGNED-char subtraction
// (`MOVSX` at 0x1800aee89/0x1800aee8e and the mirror at 0x1800aeed8). The two
// halves disagree only on bytes >= 0x80, which is why they are not collapsed.
int StrCmpSigned(const char* a, const char* b) {
    size_t i = 0;
    if (static_cast<unsigned char>(*a) == static_cast<unsigned char>(*b)) {
        while (a[i] != '\0' && b[i] != '\0') {
            ++i;
            if (static_cast<unsigned char>(a[i]) != static_cast<unsigned char>(b[i])) break;
        }
    }
    return static_cast<int>(static_cast<signed char>(a[i])) -
           static_cast<int>(static_cast<signed char>(b[i]));
}

// Act 3's inlined descent, 0x1800aee50..0x1800aeeef. The two null arms are
// ASYMMETRIC and that is retail's: a null NODE key skips the left test
// entirely, a null SEARCH key takes the left branch on the way down and
// reports EQUAL at the right test. Identical in shape to `rtpathid::
// InsertCore`, which is what makes the lookup agree with the insert.
PathIdNode* SnapshotFind(PathIdNode* node, const char* search) {
    while (node != nullptr) {
        const char* nodeKey = node->element.key;

        bool goLeft = false;
        if (nodeKey != nullptr) {                              // 0x1800aee53
            if (search == nullptr)                      goLeft = true;
            else if (StrCmpSigned(search, nodeKey) < 0) goLeft = true;
        }
        if (goLeft) { node = node->left; continue; }           // 0x1800aee9c

        if (search == nullptr) return node;                    // 0x1800aeea5
        if (nodeKey != nullptr && StrCmpSigned(nodeKey, search) >= 0)
            return node;                                       // 0x1800aeee9
        node = node->right;                                    // 0x1800aeeeb
    }
    return nullptr;
}

// BLOCK PKGCACHE (NOT RETAIL): the snapshot as an open-addressing table over the compiled paths'
// own buffers (act 1 has the exactness argument). The table holds a reference on each key's
// buffer, the one retail's tree node takes, so a key stays readable until the table goes.
class SnapshotTable {
public:
    explicit SnapshotTable(int32_t expected) {
        size_t cap = 1024;
        while (cap < static_cast<size_t>(expected > 0 ? expected : 0) * 2)
            cap *= 2;
        _slots.assign(cap, Slot{});
        _held.reserve(static_cast<size_t>(expected > 0 ? expected : 0));
    }

    // rtpathid::Insert's answer: an equal key keeps its first value.
    void Insert(const System::String& key, uint32_t value) {
        if ((_used + 1) * 2 > _slots.size())
            Grow();
        const uint64_t h = Collection::FoldIndex::Hash(key._data);
        size_t i = static_cast<size_t>(h) & (_slots.size() - 1);
        while (_slots[i].key != nullptr) {
            if (_slots[i].hash == h && std::strcmp(_slots[i].key, key._data) == 0)
                return;
            i = (i + 1) & (_slots.size() - 1);
        }
        _held.push_back(key);   // the reference (String's copy ctor adopts the buffer)
        _slots[i] = Slot{ h, key._data, value };
        ++_used;
    }

    // SnapshotFind's answer: the value under the key with exactly these characters, or null.
    const uint32_t* Find(const char* key) const {
        const uint64_t h = Collection::FoldIndex::Hash(key);
        for (size_t i = static_cast<size_t>(h) & (_slots.size() - 1); _slots[i].key != nullptr;
             i = (i + 1) & (_slots.size() - 1)) {
            if (_slots[i].hash == h && std::strcmp(_slots[i].key, key) == 0)
                return &_slots[i].value;
        }
        return nullptr;
    }

private:
    struct Slot {
        uint64_t    hash = 0;
        const char* key = nullptr;
        uint32_t    value = 0;
    };

    void Grow() {
        std::vector<Slot> old;
        old.swap(_slots);
        _slots.assign(old.size() * 2, Slot{});
        for (const Slot& s : old) {
            if (s.key == nullptr)
                continue;
            size_t i = static_cast<size_t>(s.hash) & (_slots.size() - 1);
            while (_slots[i].key != nullptr)
                i = (i + 1) & (_slots.size() - 1);
            _slots[i] = s;
        }
    }

    std::vector<Slot> _slots;
    std::vector<System::String> _held;
    size_t _used = 0;
};

// BLOCK PKGCACHE (NOT RETAIL, exact): act 1's compiled path of each resource, kept for act 3, which
// asks GetCompiledPath again for the very same resources (~376,000 under MegaMod, each answer four
// splits, five concatenations and their allocations). The answer is a function of the path's
// characters and the data directory's characters alone (BuildCompiledPath, external_compiled.cpp:
// the rest are literals); the data directory is set once at boot (filesystem_setupdirs.cpp) and
// ClosePackages / LoadPackages between the two acts touch only the packages. So act 3 takes the
// kept answer when the resource's path has the characters act 1 compiled, and runs GetCompiledPath
// otherwise. The kept answer shares its buffer with act 1's copy where retail's is a fresh one with
// the same characters; act 3 only reads it (the snapshot, GetPackfileId, ExistsInPackfile,
// PlatformFileExists), and Filename(const String&)'s AdjustDelimiters finds it already normalised.
// A pointer match is not trusted alone: the path's characters decide. The debug exe runs
// GetCompiledPath behind every reuse and compares ([CompiledReuse] MISMATCH).
class CompiledPaths {
public:
    explicit CompiledPaths(int32_t expected) {
        _kept.reserve(static_cast<size_t>(expected > 0 ? expected : 0));
    }

    // Act 1, in its FindNext order. The Strings share the buffers (a copy only adds a reference).
    void Keep(const void* resource, const System::Filename& raw, const System::Filename& compiled) {
        _kept.push_back(Entry{ resource, raw.AsString(), compiled.AsString() });
    }

    // Act 3 walks the same resources in the same order, so the next kept entry is normally this
    // resource's; anything else (or a path that changed) answers null and act 3 compiles it.
    const System::String* Next(const void* resource, const System::Filename& raw) {
        if (_cursor >= _kept.size() || _kept[_cursor].resource != resource)
            return nullptr;
        const Entry& e = _kept[_cursor];
        ++_cursor;
        if (e.raw._data == nullptr || raw._data == nullptr || std::strcmp(e.raw._data, raw._data) != 0)
            return nullptr;
        return &e.compiled;
    }

private:
    struct Entry {
        const void*    resource;
        System::String raw;
        System::String compiled;
    };
    std::vector<Entry> _kept;
    size_t _cursor = 0;
};

// THE ONE WIN32 CALL, isolated — see the banner. `GetFileAttributesW` on a
// UTF-16 copy of the path; anything but INVALID_FILE_ATTRIBUTES counts as
// present, INCLUDING a directory. Retail compares against -1 and nothing else
// (0x1800aefce `CMP EAX,-0x1 / SETNZ`).
bool PlatformFileExists(const System::Filename* path) {
    // Filename read AS a String -- the same same-layout conversion retail
    // performs by pointer cast throughout (Filename.h header note). The block
    // ends where retail's 0x1800aefd5 release stands: ~WideString (0x18000a730).
    bool ok;
    {
        const System::WideString ws(path->AsString());          // 0x1800aefbf
        const uint32_t attrs = Platform::PathAttributes(ws._data);   // GetFileAttributesW
        ok = (attrs != Platform::kInvalidPathAttributes);       // 0x1800aefce
    }                                                           // 0x1800aefd5
    return ok;
}

// B3X W0: the two virtuals this body calls go by name --
// `Object::Base::Type()` (slot 2, +0x10) and `Object::External::Destruct()`
// (slot 7, +0x38, the pre-reload teardown the object does to itself).

}  // namespace

// ============================================================================
// ?SetupFileSystem@Modification@IO@@QEAAXXZ                 retail 0x1800aebb0
// ============================================================================

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::Modification::SetupFileSystem`'s member body
// ============================================================================
namespace IO {

void Modification::SetupFileSystem() {
    // A member of `IO::X` must be DEFINED in a namespace ENCLOSING the class
    // (B34_BRIEFS section A.2), so this body cannot live inside `namespace
    // RtIO`, where every helper it calls sits in a nested anonymous namespace.
    // ONE function-scope using-directive brings them in unchanged and keeps
    // this a MOVE rather than a rewrite. Safe against B34_RULES section 7.2's
    // grep hazard: the only name the two namespaces share is `Modification`,
    // and inside a member the injected-class-name wins outright, so a collision
    // would be a compile error, never a silent rebind.
    using namespace RtIO;
    if (_dirty == 0) return;                              // 0x1800aebd8
    LoadTime::Stamp("fs.remount");   // NOT RETAIL: print-only (a package remount begins)

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): ext.menu_entry answers true when
    // the packages this remount would mount are the ones mounted now (the fingerprint taken at the
    // end of the last remount). Acts 1-4 are then skipped; act 2's order stamp and act 5's
    // `_dirty = 0` still run, so SaveList and the Mods dialog read the same values.
    if (_extRemountSkip != nullptr && _extRemountSkip(*this)) {
        for (int32_t i = _mods._size - 1; i >= 0; --i)
            _mods._data[i].order = i;   // act 2, 0x1800aed64
        _dirty = 0;                     // act 5, 0x1800af447
        LoadTime::Stamp("fs.remount.skip");   // NOT RETAIL: print-only
        return;
    }

    // The three singletons, all exported DATA symbols, all class statics now
    // (ENDGAME B.3.9 SS1, B39_RULES D7) — read fresh every call, no lazy cache
    // (B9-10).

    // *** `RtIO::` IN FULL, AND THE COMPILER IS WHY. *** This body lives in a
    // `namespace IO` block (B34_BRIEFS section A.2), and since B.3.4 PACKAGE+FS
    // commit 4 a bare `FileSystem` there binds to the REAL CLASS
    // `IO::FileSystem` instead of to the POD the five field reads below use.
    // That is B34_RULES section 7.2's grep hazard; it presents as a build error
    // rather than a silent rebind, which is exactly why the rule says to spell
    // the POD namespace out.
    ::IO::FileSystem* const fsObj = ::IO::FileSystem::_singleton;   // ENDGAME B.3.9 IO (D7): the class static.
    ::Object::ExternalFactory* const extFactory = ::Object::ExternalFactory::_singleton;
    // `Object::Factory::_singleton` is captured HERE, at 0x1800aebef, and used
    // only in act 4. Retail spills it to [RBP-0x39] across the whole function
    // rather than re-reading it, and so does this.
    const ::Object::Factory* const factory = ::Object::Factory::_singleton;

    // `?_typeInfo@External@Object@@0VBaseTypeInfo@2@A` + 0x00 — the type id
    // FindNext filters on. Read through the NAME door so a promotion of the row
    // is followed rather than the DLL's node being pinned; this is the same
    // address (0x1801f9b38) `?Type@External@Object@@UEBAIXZ` is two
    // instructions of.
    // ENDGAME B.3.9 OBJ: the class record (the minted one is retired).
    const uint32_t kExternalTypeId = ::Object::External::TypeId();

    const ::Object::ExternalFactory* const extFactoryObj = extFactory;

    // ========================= ACT 1 — SNAPSHOT =============================
    // BLOCK PKGCACHE (NOT RETAIL, exact): the snapshot is a hash table (SnapshotTable above) from the
    // compiled path's characters to its packfile id instead of retail's case-SENSITIVE AVL tree (a ~376,000-node
    // tree under MegaMod, two signed-strcmp descents per resource). The tree is a local of this
    // function: nothing but act 3's SnapshotFind reads it and act 5 frees it, so what it holds is
    // only the answers SnapshotFind gives. Its comparator is a total order whose equality is
    // byte equality, so SnapshotFind finds the node whose key has the very characters searched
    // for, and rtpathid::Insert keeps the FIRST value inserted under a key (an equal key inserts
    // nothing): the table's Insert answers the same. A compiled path is never null (it always
    // holds "_" and ".crs"), so the null arms never run. The debug exe still builds retail's tree
    // beside the table and compares every lookup ([Snapshot] MISMATCH).
    SnapshotTable snapshotIndex(extFactory->_resources._count);   // sized for one key per resource
    PathIdSet snapshot = { nullptr, 0, 0 };                    // [RBP-0x49] (the debug exe's check only)
    CompiledPaths compiledPaths(extFactory->_resources._count);   // PKGCACHE: act 1's answers, for act 3

    for (const ::Object::External* ext = extFactoryObj->FindNext(kExternalTypeId, nullptr);
         ext != nullptr; ) {
        System::Filename compiled;                             // [RBP+0x77]
        {
            // GetPath's hidden return slot is `raw` (retail's `got`);
            // GetCompiledPath's is `compiled`, constructed over the default
            // ctor's null exactly as the door that stood here did.
            System::Filename raw(ext->GetPath());        // 0x1800aec29
            ::new (&compiled) System::Filename(                // 0x1800aec36
                ::Object::ExternalFactory::GetCompiledPath(raw));
            compiledPaths.Keep(ext, raw, compiled);            // PKGCACHE: for act 3
        }  // ~raw releases here, exactly where retail's 0x1800aec3c release sat

        const uint32_t id = fsObj->GetPackfileId(compiled);    // 0x1800aec9f
        snapshotIndex.Insert(compiled.AsString(), id);         // PKGCACHE: in place of the insert
        if (OPENBA_PATHINDEX_CHECK) {
            PathIdNode* outSlot = nullptr;                     // [RBP-0x4?]
            rtpathid::Insert(&snapshot,                     // 0x1800aecb7
                             &outSlot,
                             &compiled._data, &id);
        }

        ext = extFactoryObj->FindNext(kExternalTypeId, ext);   // 0x1800aecc8
        // ~compiled releases here, exactly where retail's 0x1800aecd3 release sat.
    }

    // ========================= ACT 2 — RESTACK ==============================
    fsObj->ClosePackages();

    {   // ~Array<Filename>(fs->userPackFilenames) -- 0x1800098d0, the body
        // modification.cpp owns.
        RtIO::RtColl_ArrayFilenameDtor_1800098d0(&fsObj->_userPackFilenames);   // 0x1800aed44
    }

    {

        // BACKWARDS, and `order` is stamped on every mod — see the banner.
        for (int32_t i = _mods._size - 1; i >= 0; --i) {      // 0x1800aed49
            Mod* const m = &_mods._data[i];
            m->order = i;                                      // 0x1800aed64
            if (m->enabled != 0)                               // 0x1800aed68
                RtIO::RtColl_ArraySetSizeFill_180026880(&fsObj->_userPackFilenames,   // 0x180026880
                       fsObj->_userPackFilenames.size + 1, &m->file);
        }
    }

    fsObj->LoadPackages();

    // ========================= ACT 3 — DIFF =================================
    ReloadArray queue = { nullptr, 0, 0 };                     // [RSP+0x30]
    int32_t unloads = 0;   // NOT RETAIL (MENUENTRY.md): act 3's ForceUnloads, for _extRemountDone

    for (::Object::External* ext =
             extFactoryObj->FindNextAs<::Object::External>(kExternalTypeId, nullptr);
         ext != nullptr; ) {
        System::Filename compiled;                             // [RBP+0x7f]
        {
            System::Filename raw(ext->GetPath());        // 0x1800aedc7  [RBP-0x59]
            // PKGCACHE: act 1's answer when the path is unchanged (CompiledPaths above).
            const System::String* const kept = compiledPaths.Next(ext, raw);
            if (kept != nullptr) {
                ::new (&compiled) System::Filename(*kept);
                if (OPENBA_PATHINDEX_CHECK) {
                    const System::Filename body(::Object::ExternalFactory::GetCompiledPath(raw));
                    if (body._data == nullptr || std::strcmp(body._data, compiled._data) != 0) {
                        std::fprintf(stderr, "[CompiledReuse] MISMATCH path=\"%s\"\n", raw._data);
                        std::fflush(stderr);
                    }
                }
            } else {
                ::new (&compiled) System::Filename(            // 0x1800aedd4
                    ::Object::ExternalFactory::GetCompiledPath(raw));
            }
        }  // ~raw releases here, exactly where retail's 0x1800aedda release sat

        // PKGCACHE: the table's answer (see act 1); the debug exe compares it with retail's descent.
        const uint32_t* const hit = snapshotIndex.Find(compiled._data);
        const bool inSnapshot = hit != nullptr;
        if (OPENBA_PATHINDEX_CHECK) {
            PathIdNode* const node = SnapshotFind(snapshot.root, compiled._data);
            if ((node != nullptr) != inSnapshot || (inSnapshot && node->element.value != *hit)) {
                std::fprintf(stderr, "[Snapshot] MISMATCH key=\"%s\"\n", compiled._data);
                std::fflush(stderr);
            }
        }

        // `TEST RBX,RBX` at 0x1800aeefd is redundant on the found path and is
        // kept; the id compare is what decides.
        if (inSnapshot && *hit != fsObj->GetPackfileId(compiled)) {
            System::Filename compiled2;                         // [RBP+0x77]
            {
                System::Filename raw2(ext->GetPath());   // 0x1800aef22  [RBP-0x51]
                ::new (&compiled2) System::Filename(            // 0x1800aef2f
                    ::Object::ExternalFactory::GetCompiledPath(raw2));
            }  // ~raw2 releases here, retail's 0x1800aef35 release

            // `FileSystem::Exists` OPEN-CODED with its two gate bytes; see the
            // banner for why this is not a call to Exists.
            bool present = false;
            if (fsObj->_existsInPackfileGate != 0) {           // 0x1800aef90
                present = fsObj->ExistsInPackfile(compiled2);
            }
            if (!present && fsObj->_platformExistsGate != 0)   // 0x1800aefab
                present = PlatformFileExists(&compiled2);

            if (present) {
                // ---- queue the reload, 0x1800af03d --------------------------
                // B.4 Z MIRROR2 (`behaviour:`, disassembly read): GetPath
                // 0x180006250 returns into raw3 [RBP-0x79] FIRST, then the
                // Type() slot (0x1800af052); the entry is built from both
                // (ReloadEntry's ctor: the copy ctor 0x1800067d0 inlined on
                // +0x10, _hole04 never written), pushed, and then released in
                // reverse declaration order: entry.file (0x1800af0d5), raw3
                // LAST (0x1800af130). (The tree released raw3 before the push.)
                const System::Filename raw3 = ext->GetPath();   // 0x1800af044
                ReloadEntry entry(ext->Type(), ext, raw3);

                rtcoll::RtColl_ArraySetSizeFill_180028cd0(&queue, queue.size + 1, &entry);   // 0x1800af0c7
                // ~entry (its file), then ~raw3.
            } else {
                // ---- drop it, 0x1800af195 ----------------------------------
                System::Filename raw4(ext->GetPath());   // 0x1800af19d  [RBP-0x?]
                extFactory->ForceUnload(raw4);                  // 0x1800af1a9
                ++unloads;   // NOT RETAIL (MENUENTRY.md)
                // ~raw4 releases here, retail's release right after ForceUnload
            }

            // ~compiled2 releases here, retail's 0x1800af243 release
        }

        ext = extFactoryObj->FindNextAs<::Object::External>(kExternalTypeId, ext);   // 0x1800af275
        // ~compiled releases here, retail's 0x1800af281 release
    }

    // ========================= ACT 4 — RELOAD ===============================
    for (int32_t i = 0; i < queue.size; ++i) {                 // 0x1800af2e6
        ReloadEntry* const e = &queue.data[i];
        ::Object::External* const ext = e->external;

        ext->Destruct();   // 0x1800af306

        // the type-tree descent, inlined; a MISS yields null and is then
        // dereferenced. See the banner.
        const uint32_t typeId = e->typeId;                     // 0x1800af309
        using TypeNode = ::Object::Factory::TypeSet::Node;
        TypeNode* node = factory->GetTypes().GetRoot();        // 0x1800af30c
        TypeNode* found = nullptr;
        while (node != nullptr) {
            if (typeId < node->element.typeId)  { node = node->left;  continue; }
            if (node->element.typeId >= typeId) { found = node; break; }
            node = node->right;
        }
        ::Object::BaseTypeInfo* const info = found->element.record.info;   // 0x1800af330
        // IN-PLACE factory, +0x28 — not the allocating one at +0x20.
        info->inPlaceFactoryFn(ext);   // the returned Base* is not read

        // re-point `External::_path` (+0x08) at the queued filename. A single
        // AdjustDelimiters after release-then-adopt on an object that already
        // owns a string is `Filename::operator=(const Filename&)`, 0x180006770
        // (Filename.h's DELIMITERS note). `ext->_path` is the class's
        // `System::Filename`, and so is `e->file`.
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): a `_path` write.
        ::Object::NotifyResourceEvent(::Object::ResourceEvent::PathWrite, nullptr, ext, e->file._data);
        ext->_path = e->file;                              // 0x1800af341/0x1800af3b3

        System::Filename compiled(                            // 0x1800af3c0  [RBP+0x6f]
            ::Object::ExternalFactory::GetCompiledPath(e->file));
        RtObject::RtObject_External_Load(&compiled, ext);      // 0x1800af3cc
        // ~compiled releases here, retail's 0x1800af3d2 release
    }

    // ========================= ACT 5 — TEARDOWN =============================
    _dirty = 0;                                           // 0x1800af447
    const int32_t reloads = queue.size;   // NOT RETAIL (MENUENTRY.md): for _extRemountDone

    // Each surviving entry's Filename dtor, 0x1800af460 -- the queue's own
    // storage is raw bytes (see ReloadEntry's banner), so the release is
    // spelled as the explicit destructor rather than a member's own RAII.
    for (int64_t i = 0; i < static_cast<int64_t>(queue.size); ++i)
        queue.data[i].file.~Filename();
    if (queue.data) MemFree(queue.data);
    queue.data = nullptr;
    queue.size = 0;

    // `Set::~Set`, INLINED by retail: it recurses on the root's two children
    // and then frees the ROOT ITSELF by hand rather than calling the recursive
    // helper on the root. Transcribed, not tidied — a `SubtreeFree(root)` would
    // be the same net effect through a different call shape and would stop this
    // matching the listing.
    if (snapshot.root != nullptr) {
        PathIdNode* const root = snapshot.root;
        rtpathid::SubtreeFree(&snapshot, root->left);    // 0x1800af52d
        rtpathid::SubtreeFree(&snapshot, root->right);   // 0x1800af53a
        ReleaseInline(&root->element.key);
        MemFree(root);
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): a remount completed; act 3's
    // counts (the queue is released above, its size was read before the release).
    if (_extRemountDone != nullptr)
        _extRemountDone(*this, reloads, unloads);
    LoadTime::Stamp("fs.remount.done");   // NOT RETAIL: print-only
}

}  // namespace IO
