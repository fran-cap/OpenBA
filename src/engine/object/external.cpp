// ============================================================================
// src/engine/object/external.cpp — Object::External / ExternalFactory /
//                                   ExternalList          (Wave 3, lane C)
// ============================================================================
//
// Byte-faithful port of the decompilation at the retail address named above
// each function (Runtime-x64.dll, image base 0x180000000, read 2026-09-06).
// Layout evidence and per-field pins: src/engine/object/External.h.
// Wave write-up, including everything this lane deliberately did NOT port:
// docs/history/runtime_port/WAVE3_LANE_C.md.
//
// WHAT THIS FAMILY IS. `External` is one loaded resource file: a path plus a
// subclass payload. `ExternalFactory` is the global resource table (singleton
// 0x1801fc6c8) and owns two AVL trees — `_resources`, keyed by path, and
// `_references`, keyed by the reference id the save stream uses in place of a
// path. `ExternalList` is an External whose payload is a flat array of other
// Externals (the mod list, the help topics, the scenarios, the map generators).
//
// WHAT IS LIVE HERE, AND WHY THAT SPLIT
// -------------------------------------
// The factory's two trees are DLL-owned global state, and registration
// redirects only OUR call sites — the DLL's own internal calls still land in
// the DLL (PLAN.md §4.1 decision 2). So both implementations drive the same
// tables, and the only safe shape is Wave 2's allocator shape: operate on the
// LIVE singleton's own tables, byte-faithfully, never a private replica.
// Everything ported below obeys that: nothing here keeps state of its own.
//
// Ported and registered, all READ-ONLY on the factory and all with a real diff
// oracle behind them:
//     External::GetPath
//     ExternalFactory::Find / FindNext / GetReferenceId / GetPathForReferenceId
//     ExternalFactory::IsReferenceModeEnabled
//     ExternalList::Find / GetResourceCount
// Ported and registered, trivially-verifiable writers with no oracle possible:
//     External::External / ~External          (one vtable stamp + one release)
//     ExternalFactory::EnableReferenceMode    (a single byte store)
//
// NOT ported — see the write-up for the full argument:
//     ExternalFactory::FindOrLoad, ExternalFactory::Clean
//         Both rewrite the resource tree. Porting them means porting Set
//         insert/erase/rebalance for a 0x18-wide element plus ExpandRelativePath,
//         External::Load and Add — a large body of code that this wave has no
//         way to prove, since running both implementations would mutate the one
//         shared tree twice. Reversed and written up; NOT registered.
//     ExternalFactory::SerializeReferenceTable, External::SaveText
//         Both reach IO::Stream / IO::Serializer / IO::TextFileStream and the
//         filesystem, which is Wave 4. Reversed and written up; NOT registered.
//         (Wave 2 took the same decision for Time::AsSeconds, except that one
//         landed as a forwarder; a forwarder is declined here because it would
//         add indirection and a census row for zero behaviour change.)
//
// ABI. Wave 1 defect 1 / reference_hidden_retslot_wild_write.md: a member that
// returns a class BY VALUE takes a HIDDEN RETURN SLOT in RDX and every real
// argument shifts right. Three functions in this lane do:
//     External::GetPath                    (this, ret)
//     ExternalFactory::GetPathForReferenceId (this, ret, id)
//     ExternalList::Find                   (this, ret, path, type)
// Each returns the slot in RAX. The existing engine/funcs/engine_funcs.h
// typedefs for all three were checked against this and are CORRECT.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <new>          // placement new into the retail hidden return slots

#include "object/External.h"
#include "object/Type.h"     // B.4 P2: ExternalList::Find takes Object::Type
#include "system/string_internal.h"
#include "object/external_set.h"        // block PATHINDEX: rtresset::IndexedFind
#include "collection/fold_index.h"

namespace RtObject {

using RtSystem::StringRefCount;

// ============================================================
// Local helpers
// ============================================================

// Retail's tree comparisons all go through the CRT's _stricmp. That is
// locale-sensitive in MSVC and the DLL never calls setlocale, so it runs in the
// "C" locale where _stricmp is plain ASCII case folding. Spelled out here for
// the same reason filename.cpp spells it out: runtime/src must not depend on a
// CRT whose locale behaviour differs between toolchains (PLAN.md §4.1).
static int StriCmpAscii(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0)  return 0;
    }
}

// The ordering the resource tree is built with, inlined at every walk site
// (FUN_18002dd70, FindNext 0x1800be9d0, Clean 0x1800be310, Add 0x1800be570).
// It is byte-identical to System::Filename::operator< (0x180026cb0): a null
// right-hand side orders below everything, a null left-hand side orders below
// any non-null, otherwise case-insensitive.
static inline bool FilenameLess(const char* a, const char* b) {
    if (b == nullptr) return false;
    if (a == nullptr) return true;
    return StriCmpAscii(a, b) < 0;
}

// Collection::Set<ResourceEntry, …>::Find, out-of-lined by MSVC as
// FUN_18002dd70 for this one element width. Node tail offsets are baked in per
// instantiation (WAVE2.md "the Set node trap"), so this body serves the 0x18
// element and nothing else.
//
// NOT RETAIL (block PATHINDEX): the tree's hash index answers when it can
// (src/engine/collection/fold_index.h; the same tree header as rtresset's view).
static ResourceSet::Node* ResourceSetFind(const ResourceSet* set, const char* key) {
    rtresset::Node* indexed = nullptr;
    const bool answered = rtresset::IndexedFind(set, set->_count, key, &indexed);
    ResourceSet::Node* const fromIndex = reinterpret_cast<ResourceSet::Node*>(indexed);  // CAST-OK: elem_stride
    if (answered && !OPENBA_PATHINDEX_CHECK)
        return fromIndex;
    ResourceSet::Node* n = set->_root;
    while (n != nullptr) {
        if (FilenameLess(key, n->element.path._data))      n = n->left;
        else if (FilenameLess(n->element.path._data, key)) n = n->right;
        else                                              break;
    }
    Collection::FoldIndex::Check("resources/external", key, answered, fromIndex, n);
    return n;
}

// The in-order successor walk both FindNext and SerializeReferenceTable inline:
// descend the right subtree to its leftmost node, else climb parents while we
// are the right child. Transcribed from 0x1800be9d0's tail.
static ResourceSet::Node* ResourceSetLeftmost(ResourceSet::Node* n) {
    while (n->left != nullptr) n = n->left;
    return n;
}
static ResourceSet::Node* ResourceSetSuccessor(ResourceSet::Node* n) {
    if (n->right != nullptr) return ResourceSetLeftmost(n->right);
    ResourceSet::Node* p = n->parent;
    while (p != nullptr && p->right == n) { n = p; p = p->parent; }
    return p;
}

// ============================================================
// ??0External@Object@@QEAA@XZ (0x1800bc9c0), ??1External@Object@@UEAA@XZ
// (0x1800bc950) and ?Compile@External@Object@@... (0x1800066e0, slot 4) --
// GONE (ENDGAME B.3.9 OBJ). The two doors stamped the minted
// `??_7Base@Object@@6B@` / `??_7External@Object@@6B@` tables over the POD; their
// callers were the retired External / CompiledExternal RT_DELDTOR rows and the
// CompiledExternal ??1 door. The slot-4 door's only reader was the minted
// tables. The bodies are the real class's: External::External(),
// External::~External() and External::Compile (external_class.cpp).
// ============================================================

// ============================================================
// Object::External::GetPath — DIFFED
// ============================================================

// ?GetPath@External@Object@@QEBA?AVFilename@System@@XZ  retail 0x180006250
//
// HIDDEN RETURN SLOT: (this in RCX, ret in RDX). One line in retail — the
// Filename copy constructor from `this->_path`, which shares the buffer and
// bumps its refcount; it does NOT copy the characters.
//
// Diffable because ours and theirs both copy from the same `_path`, so the two
// results are the SAME char* and an 8-byte compare is exact. The oracle call
// takes one extra reference, which the scratch release below gives back.
System::Filename* RtObject_External_GetPath(const ::Object::External* self, System::Filename* ret) {

    // ENDGAME B.3.1 step 2: retail's copy ctor (0x1800067d0), spelled as itself.
    // `ret` is the hidden return slot — raw, uninitialised bytes — so the object
    // is placement-constructed into it, which is what retail's CALL does.
    new (ret) System::Filename(self->_path);

    return ret;
}

// ============================================================
// Object::ExternalFactory — the read-only half
// ============================================================

// ?Find@ExternalFactory@Object@@QEBAPEBVExternal@2@AEBVFilename@System@@@Z
//   retail 0x1800bdeb0                                             — DIFFED
//
// Look a path up in the resource tree. Three things here are not obvious:
//
//  1. **A path with no `:identifier` suffix gets `":resource"` appended.**
//     Retail calls Filename::GetPathIdentifer and, when the result is null or
//     empty, concatenates the literal at 0x180155ae0 — `":resource"` — through
//     the `operator+(String const&, char const*)` overload (0x1800c92d0). The
//     same convention appears in ExternalList::Find, which uses the
//     String&-vs-String& overload (0x1800c9440) instead. So `"Dialog/Foo.rsc"`
//     and `"Dialog/Foo.rsc:resource"` are the SAME key; the suffix names a
//     sub-resource inside the file.
//  2. **AdjustDelimiters is called five times** — twice on the working copy,
//     once more after the concatenation, then twice on a second copy. Each call
//     is idempotent and each is transcribed. Note that AdjustDelimiters rewrites
//     the buffer IN PLACE and the working copy SHARES the caller's buffer, so
//     `Find` normalises the caller's Filename as a side effect. That is retail's
//     behaviour and callers depend on it (see the ctor note in filename.cpp).
//  3. **The tree is case-INSENSITIVE.** ExternalList::Find, three functions
//     below, is case-SENSITIVE over the same kind of path. That asymmetry is
//     real, not a transcription slip.
const ::Object::External* RtObject_ExternalFactory_Find(const ExternalFactory* self,
                                              const System::Filename* path) {

    const ::Object::External* ours = nullptr;

    // ENDGAME B.3.1 step 2. Adopt-then-adjust-twice IS the Filename copy ctor
    // (0x1800067d0), whose ported body normalises twice for exactly this reason,
    // so both of the header's "twice on the working copy" calls are still spent
    // — including the in-place rewrite of the CALLER's shared buffer that note 2
    // says callers depend on, since the copy shares the buffer either way.
    System::Filename key(*path);

    System::Filename ident = path->GetPathIdentifer();
    // `GetLength` (0x1800063c0) is retail's own null-or-empty test.
    if (ident.GetLength() == 0) {
        // Retail's inlined String::operator= plus the separate, single
        // AdjustDelimiters is byte-for-byte what `Filename::operator=(const
        // String&)` (0x180026d40) does: release, adopt, normalise ONCE. `cat`'s
        // release is its destructor.
        System::String cat = key.AsString() + ":resource";
        key = cat;
    }

    // The second copy, same ctor, so its two AdjustDelimiters are still spent.
    ResourceSet::Node* node;
    {
        System::Filename probe(key);
        node = ResourceSetFind(&self->_resources, probe._data);
    }   // probe's release, still ahead of the node read, as retail has it

    if (node != nullptr)
        ours = node->element.external;

    // ident then key release in their destructors — reverse declaration order
    // is retail's teardown order.
    return ours;
}

// ?GetReferenceId@ExternalFactory@Object@@QEBAIPEBVExternal@2@@Z
//   retail 0x1800bf100                                             — DIFFED
//
// *** RETAIL DEREFERENCES THE FOUND NODE WITHOUT A NULL CHECK. ***
// The tail is literally
//     MOV EAX, dword ptr [RBX + 0x14]   ; RBX = the node Set::Find returned
//     OR ECX, -1 / CMP EAX, ECX / CMOVZ EAX, ECX
// with no test of RBX. An External that is not in the resource tree therefore
// faults reading 0x14. Left unguarded on purpose (CLAUDE.md: "Don't 'fix' a
// faithful crash with a null check") — a guard would not restore the behaviour,
// it would skip it, and every current caller passes an External the factory
// owns. If this ever faults at 0x14, the bug is upstream: something handed us
// an External the factory never Add()ed.
//
// The CMOVZ pair is a genuine no-op — `x == -1 ? -1 : x`. Transcribed as the
// plain read it compiles to.
//
// Note the double copy: retail takes GetPath's Filename and then copy-constructs
// a SECOND Filename from it before searching, releasing both. Transcribed.
namespace {
// The HITCH_SAVE memo (External.h): open addressing on the External's address, linear
// probing, power-of-two capacity grown at half load. Main thread only (the save runs
// on the main thread; the window is closed before SaveRestore returns).
struct RefIdMemo {
    struct Slot { const ::Object::External* key; uint32_t id; };
    Slot*     slots   = nullptr;
    uint32_t  cap     = 0;      // 0 = closed
    uint32_t  used    = 0;
    bool      verify  = false;
    int       differ  = 0;
    long long asks    = 0;
    long long hits    = 0;
};
RefIdMemo g_refIdMemo;

inline uint32_t RefIdMemoHash(const ::Object::External* e, uint32_t mask) {
    uint64_t v = reinterpret_cast<uintptr_t>(e);   // CAST-OK: pointer_hash
    v ^= v >> 29; v *= 0xbf58476d1ce4e5b9ull; v ^= v >> 32;
    return static_cast<uint32_t>(v) & mask;
}

void RefIdMemoInsert(const ::Object::External* e, uint32_t id) {
    RefIdMemo& m = g_refIdMemo;
    if ((m.used + 1) * 2 > m.cap) {
        const uint32_t ncap = m.cap * 2;
        RefIdMemo::Slot* const ns = new RefIdMemo::Slot[ncap]();
        for (uint32_t i = 0; i < m.cap; ++i) {
            if (m.slots[i].key == nullptr) continue;
            uint32_t h = RefIdMemoHash(m.slots[i].key, ncap - 1);
            while (ns[h].key != nullptr) h = (h + 1) & (ncap - 1);
            ns[h] = m.slots[i];
        }
        delete[] m.slots;
        m.slots = ns;
        m.cap   = ncap;
    }
    uint32_t h = RefIdMemoHash(e, m.cap - 1);
    while (m.slots[h].key != nullptr) h = (h + 1) & (m.cap - 1);
    m.slots[h] = { e, id };
    ++m.used;
}
}  // namespace

static uint32_t GetReferenceIdRetail(const ExternalFactory* self,
                                     const ::Object::External* external);

uint32_t RtObject_ExternalFactory_GetReferenceId(const ExternalFactory* self,
                                                 const ::Object::External* external) {
    RefIdMemo& m = g_refIdMemo;
    if (m.cap == 0)
        return GetReferenceIdRetail(self, external);
    ++m.asks;
    uint32_t h = RefIdMemoHash(external, m.cap - 1);
    for (;;) {
        const RefIdMemo::Slot& s = m.slots[h];
        if (s.key == external) {
            ++m.hits;
            if (m.verify && GetReferenceIdRetail(self, external) != s.id) ++m.differ;
            return s.id;
        }
        if (s.key == nullptr) break;
        h = (h + 1) & (m.cap - 1);
    }
    const uint32_t id = GetReferenceIdRetail(self, external);
    RefIdMemoInsert(external, id);
    return id;
}

void ReferenceIdMemoBegin(bool verify) {
    RefIdMemo& m = g_refIdMemo;
    delete[] m.slots;
    m = RefIdMemo();
    m.cap    = 1024;
    m.slots  = new RefIdMemo::Slot[m.cap]();
    m.verify = verify;
}

int ReferenceIdMemoEnd(long long* asks, long long* hits) {
    RefIdMemo& m = g_refIdMemo;
    const int differ = m.differ;
    if (asks) *asks = m.asks;
    if (hits) *hits = m.hits;
    delete[] m.slots;
    m = RefIdMemo();
    return differ;
}

// The retail body (0x1800bf100), unchanged.
static uint32_t GetReferenceIdRetail(const ExternalFactory* self,
                                     const ::Object::External* external) {

    ResourceSet::Node* node;
    {
        // `got` is GetPath's hidden return slot: it is declared null and then
        // CONSTRUCTED INTO by the call, so it must not be constructed twice.
        // The class->POD cast is the registered entry point's signature, which
        // B31_RULES keeps until the whole family converts.
        System::Filename got;
        RtObject_External_GetPath(external, &got);
        System::Filename key(got);   // the second copy; transcribed, see above

        node = ResourceSetFind(&self->_resources, key._data);
    }   // key then got release — retail's order, still ahead of the node read

    uint32_t ours = node->element.referenceId;   // unguarded, exactly as retail

    return ours;
}

// ?GetPathForReferenceId@ExternalFactory@Object@@QEBA?AVFilename@System@@I@Z
//   retail 0x1800bf1d0                                             — DIFFED
//
// HIDDEN RETURN SLOT: (this in RCX, ret in RDX, id in R8D). The exe-side
// `fn_ExternalFactory_GetPathForReferenceId` typedef carried all three and was
// correct; it is gone with B.3.3 step 7 (callers are member calls now).
//
// Signed 32-bit AVL descent over `_references` (the key compares as an `int`). On a miss retail does
// NOT leave the slot untouched — it copy-constructs from a null temporary, so
// the caller gets a valid empty Filename. Both branches are transcribed, since
// the empty-on-miss result is what the load path checks.
System::Filename* RtObject_ExternalFactory_GetPathForReferenceId(const ExternalFactory* self,
                                                                 System::Filename* ret, uint32_t id) {

    // 0x1800bf1f4..0x1800bf217: the descent from `_references._root` (+0x20),
    // SIGNED compares (`JL` / `JGE`), the order the insert built the tree in
    // (review F-172: the port compared unsigned and missed ids >= 0x80000000).
    ReferenceSet::Node* n = ::Object::detail::ReferenceFind(self->_references._root, id);

    // `ret` is the hidden return slot in both arms — raw bytes, so placement.
    if (n != nullptr) {
        // Adopt-then-adjust-twice is the copy ctor; `path` is a POD member of
        // the not-yet-converted node element, hence the bridge.
        System::Filename tmp(n->element.path);
        new (ret) System::Filename(tmp);
    } else {
        // Retail really does copy-construct from a null temporary rather than
        // leave the slot untouched; the named local is what keeps that CALL,
        // since C++17 would elide a copy from an unnamed one.
        System::Filename empty;
        new (ret) System::Filename(empty);
    }

    return ret;
}

// ?FindNext@ExternalFactory@Object@@QEBAPEBVExternal@2@IPEBV32@@Z
//   retail 0x1800be9d0                                             — DIFFED
//
// Iterate the resource tree in path order, returning the next External whose
// type id IS OR DERIVES FROM `typeId`. `prev == null` starts at the leftmost
// node; otherwise retail re-FINDS `prev` by its path (not by pointer) and
// resumes from its in-order successor. Re-finding by path means a resource that
// was evicted between two calls restarts the walk from wherever its key would
// have sorted; that is retail's behaviour, not an accident of this transcript.
//
// THE DERIVED-TYPE TEST IS LANE A's TERRITORY. Retail inlines an IsDerivedFrom
// walk over Object::Factory's type map here, read by name since B.4 P2
// (Factory::TypeSet::Node, TypeRecord: TypeInfo.h).
// Two consequences worth stating:
//   * `Factory::_singleton` is read by address, since Lane A owns the accessor.
//   * Retail does NOT null-check the type-map lookup: a type id absent from the
//     map falls out of the search loop with a null node and is then read at
//     +0x08. Faithful, unguarded, same argument as GetReferenceId above.
// WAVE 9 LANE Y, 2026-09-08 — this used to be resolved ONCE inside
// RtObjectExternalRegister() and cached here. That is the frozen-cache defect
// (WAVE9.md, "caches a null forever on the first call"), and the DLL-absent
// boot MEASURED it: `RtObjectPortInit()` runs at resolve_standalone.cpp:325 and
// `RtDataMintInit()` at :405, so at cutover the resolve happened EIGHTY LINES
// of init before the minted storage for `?_singleton@Factory@Object@@0PEAV12@EA`
// was registered, and the cache froze on null for the life of the process.
// Attempt 04 names it in its own words:
//     [RtResolveEarly]   BYPASS?   caller=…  "?_singleton@Factory@Object@@0PEAV12@EA"
//     [RtUnresolved] 1  …  ?_singleton@Factory@Object@@0PEAV12@EA
// The symptom would NOT have been a crash. `factoryRoot` null makes FindNext's
// inlined IsDerivedFrom walk zero nodes, so every derived-type query silently
// finds nothing — a wrong answer with a healthy-looking pointer, which is the
// failure mode this file's own header warns about one section down.
//
// ENDGAME B.3.9 OBJ (B39_RULES D7): the cell is the class static
// `Object::Factory::_singleton` (Runtime 0x1801fc6e8), onto which the minted
// row is redirected -- no resolve, no cache, no init ordering to assume.
static ::Object::Factory* const* FactorySingletonSlot() {
    return &::Object::Factory::_singleton;
}

const ::Object::External* RtObject_ExternalFactory_FindNext(const ExternalFactory* self,
                                                  uint32_t typeId,
                                                  const ::Object::External* prev) {

    ResourceSet::Node* cursor = nullptr;

    if (prev == nullptr) {
        if (self->_resources._root != nullptr)
            cursor = ResourceSetLeftmost(self->_resources._root);
    } else {
        ResourceSet::Node* found;
        {
            // As GetReferenceId above: `got` is GetPath's hidden return slot,
            // `key` is retail's second copy, and both release before the read.
            System::Filename got;
            RtObject_External_GetPath(prev, &got);
            System::Filename key(got);

            found = ResourceSetFind(&self->_resources, key._data);
        }

        if (found != nullptr)
            cursor = ResourceSetSuccessor(found);
    }

    const ::Object::External* ours = nullptr;
    using TypeNode = ::Object::Factory::TypeSet::Node;
    ::Object::Factory* const* pFactorySingleton = FactorySingletonSlot();
    const TypeNode* factoryRoot =
        pFactorySingleton ? (*pFactorySingleton)->_types._root   // Factory +0x00
                          : nullptr;

    while (cursor != nullptr) {
        ::Object::External* candidate = cursor->element.external;
        // Object::Base::Type() (retail slot 2, +0x10), by name on the real class.
        uint32_t actual =
            candidate->Type();

        if (actual == typeId) { ours = candidate; break; }

        // Inlined IsDerivedFrom: locate `actual` in Factory's type map, then
        // walk its ancestor chain looking for `typeId`.
        const TypeNode* node = factoryRoot;
        while (node != nullptr) {
            uint32_t nodeId = node->element.typeId;              // +0x00
            if (actual < nodeId)
                node = node->left;                               // +0x38
            else if (nodeId < actual)
                node = node->right;                              // +0x40
            else
                break;
        }
        const ::Object::TypeRecord* chain = &node->element.record;   // +0x08, unguarded
        bool derived = false;
        do {
            if (chain->typeId == typeId) { derived = true; break; }
            chain = chain->parent;                               // +0x10
        } while (chain != nullptr);
        if (derived) { ours = candidate; break; }

        cursor = ResourceSetSuccessor(cursor);
    }

    return ours;
}

// ============================================================
// Object::ExternalList
// ============================================================
// ?Find@ExternalList@Object@@ (retail 0x1800bf700) is the member
// `Object::ExternalList::Find`, external_class_doors.cpp (B.4 P2 wave X: the C
// body moved there unchanged; its only caller was that member).

}  // namespace RtObject
