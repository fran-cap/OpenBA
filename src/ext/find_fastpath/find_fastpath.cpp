// ============================================================================
// src/ext/find_fastpath/find_fastpath.cpp -- ext.find_fastpath (class A, next start)
//   NOT RETAIL, exact: docs/extensions/handins/VISITREPLAY.md
// ============================================================================
//
// WHAT RETAIL DOES. FindOrLoad(P), for a non-empty P: E = ExpandRelativePath(P),
// found = Find(E) (the private Find: ":resource" appended when E has no
// identifier, a case-folding descent of `_resources`), and on a hit
// `if (_loadEnabled) Add(found)`, returning found. On a save load the pointer's
// path comes from a reference id first: P = GetPathForReferenceId(id), a pure
// read of `_references`.
//
// THE CACHE. A hit arm's request characters -> the row Find returned (the row
// holding `found`: ptr_index.h), and a reference id -> that row. Next time the
// same characters (or id) arrive with the path stack empty, FindOrLoad's hit arm
// runs on the row directly (RtObject_ExternalFactory_FindOrLoadHitRow: the same
// counters, `if (_loadEnabled)` AddFound, the same result).
//
// WHY THAT IS EXACT. Under an empty path stack Expand(P) is a function of P's
// characters (expand_memo.h, point 4). Find(E)'s answer is the row whose key is
// fold-equal to E's probe, and the tree never holds two fold-equal keys (an
// insert meeting one inserts nothing), so a positive answer holds through every
// insert and changes only if its row is erased: any erase (and a wholesale
// Clear) retires every entry (the generation below). A reference id's path
// changes only when SerializeReferenceTable rebuilds `_references` (its own
// generation). Add(found) descends for found->_path; AddFound runs the arm on the
// row in hand only when its key is that path character for character (else it
// runs Add), so a `_path` rewrite needs no invalidation here. What is skipped:
// Expand (its memo already held the answer), Find's and Add's Filename copies,
// the descent, the reference-id descent and its copies; no table, bit or stream
// state.
//
// THREADS. Both threads reach FindOrLoad; the tables are touched under
// ResLoadGuard (Expand takes it on every request anyway).
//
// THE CHECK (debug exe; OPENBA_FINDFAST_CHECK). Every answer is compared with
// the faithful one (Find(Expand(P)), and for an id GetPathForReferenceId first);
// a difference prints `[FindFastpath] MISMATCH` and the faithful path runs.
// ============================================================================

#include "ext/find_fastpath/find_fastpath.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ext/find_fastpath/request_table.h"
#include "ext/registry.h"
#include "object/External.h"
#include "object/external_find.h"
#include "object/external_path.h"
#include "object/external_set.h"
#include "object/resource_ext.h"
#include "res_load_lock.h"
#include "system/Filename.h"

#if defined(_DEBUG)
#define OPENBA_FINDFAST_CHECK 1
#else
#define OPENBA_FINDFAST_CHECK 0
#endif

namespace Ext { namespace FindFastpath {

namespace {

using Object::External;
using Object::ExternalFactory;

// More distinct requests than this means something generates names: start over.
constexpr size_t kMaxEntries = 1u << 21;

struct RefEntry {
    void* row = nullptr;
    uint64_t gen = 0;
    uint64_t refGen = 0;
};

struct Tables {
    RequestTable chars;
    std::vector<RefEntry> refs;
    uint64_t gen = 1;     // bumped by every erase and Clear
    uint64_t refGen = 1;  // bumped by every SerializeReferenceTable
};

// Never destroyed (a loader thread at exit).
Tables& T() {
    static Tables* const t = new Tables();
    return *t;
}

struct Counters {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> refHits{0};
    std::atomic<uint64_t> stored{0};
    std::atomic<uint64_t> refStored{0};
    std::atomic<uint64_t> checked{0};
    std::atomic<uint64_t> mismatches{0};
};
Counters g_c;

rtresset::Set* ResourceSet(ExternalFactory& f) {
    return reinterpret_cast<rtresset::Set*>(&f._resources);  // CAST-OK: elem_stride
}

#if OPENBA_FINDFAST_CHECK
// The faithful answer for request P: Find(Expand(P)).
const External* Faithful(ExternalFactory& f, const System::Filename& p) {
    System::Filename expanded;
    RtObject::RtObject_ExternalFactory_ExpandRelativePath(&f, &expanded, &p, true);
    return RtObject::RtObject_ExternalFactory_Find(&f, &expanded);
}

bool Check(ExternalFactory& f, const System::Filename& p, void* row, const char* what, uint32_t id) {
    const External* const real = Faithful(f, p);
    const External* const ours = rtresset::Elem(static_cast<rtresset::Node*>(row))->external;
    const uint64_t n = g_c.checked.fetch_add(1, std::memory_order_relaxed) + 1;
    if (real != ours) {
        g_c.mismatches.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[FindFastpath] MISMATCH %s id=%u path=\"%s\" cached=%p faithful=%p\n", what, id,
                     p._data != nullptr ? p._data : "(null)", static_cast<const void*>(ours),
                     static_cast<const void*>(real));
        std::fflush(stderr);
        return false;
    }
    if (n >= 4096 && (n & (n - 1)) == 0) {
        std::fprintf(stderr, "[FindFastpath] checked %llu answers, %llu differ\n", static_cast<unsigned long long>(n),
                     static_cast<unsigned long long>(g_c.mismatches.load(std::memory_order_relaxed)));
        std::fflush(stderr);
    }
    return true;
}
#endif

// ---- the hooks (object/resource_ext.h) --------------------------------------

void* Find(ExternalFactory& f, const char* request, int len) {
    void* row = nullptr;
    {
        ResLoadGuard guard;
        Tables& t = T();
        row = t.chars.Find(request, static_cast<size_t>(len), t.gen);
    }
    if (row == nullptr)
        return nullptr;
#if OPENBA_FINDFAST_CHECK
    if (!Check(f, System::Filename(request), row, "request", 0))
        return nullptr;
#else
    (void)f;
#endif
    g_c.hits.fetch_add(1, std::memory_order_relaxed);
    return row;
}

void Store(ExternalFactory& f, const char* request, int len, const External* found) {
    rtresset::Set* const set = ResourceSet(f);
    rtresset::Node* node = nullptr;
    bool verified = false;
    if (!rtresset::VisitIndexFind(set, set->count, found, &node, &verified) || node == nullptr)
        return;
    ResLoadGuard guard;
    Tables& t = T();
    if (t.chars.Size() >= kMaxEntries)
        t.chars.Clear();
    t.chars.Put(request, static_cast<size_t>(len), node, t.gen);
    g_c.stored.fetch_add(1, std::memory_order_relaxed);
}

void* FindRef(ExternalFactory& f, uint32_t refId) {
    void* row = nullptr;
    {
        ResLoadGuard guard;
        Tables& t = T();
        if (refId >= t.refs.size())
            return nullptr;
        const RefEntry& e = t.refs[refId];
        if (e.row == nullptr || e.gen != t.gen || e.refGen != t.refGen)
            return nullptr;
        row = e.row;
    }
#if OPENBA_FINDFAST_CHECK
    if (!Check(f, f.GetPathForReferenceId(refId), row, "refId", refId))
        return nullptr;
#else
    (void)f;
#endif
    g_c.refHits.fetch_add(1, std::memory_order_relaxed);
    return row;
}

void StoreRef(ExternalFactory& f, uint32_t refId, const char* request, int len) {
    if (!RefIdCacheable(refId, static_cast<int32_t>(f._references._count)))
        return;
    ResLoadGuard guard;
    Tables& t = T();
    void* const row = t.chars.Find(request, static_cast<size_t>(len), t.gen);
    if (row == nullptr)
        return;
    if (refId >= t.refs.size())
        t.refs.resize(static_cast<size_t>(refId) + 1);
    t.refs[refId] = RefEntry{row, t.gen, t.refGen};
    g_c.refStored.fetch_add(1, std::memory_order_relaxed);
}

void Event(ExternalFactory& f, Object::ResourceEvent e, void* row, const External* external, const char* newPath) {
    (void)f;
    (void)row;
    (void)external;
    (void)newPath;
    switch (e) {
    case Object::ResourceEvent::Erase:
    case Object::ResourceEvent::Clear: {
        ResLoadGuard guard;
        ++T().gen;
        return;
    }
    case Object::ResourceEvent::RefTable: {
        ResLoadGuard guard;
        ++T().refGen;
        return;
    }
    default:
        return;   // inserts, misses, reloads and path writes move no positive answer
    }
}

const Object::ExtFindFast kHooks = {&Find, &Store, &FindRef, &StoreRef, &Event};

void Dump() {
    std::fprintf(stderr,
                 "[FindFastpath] hits=%llu refHits=%llu stored=%llu refStored=%llu checked=%llu mismatches=%llu\n",
                 static_cast<unsigned long long>(g_c.hits.load()), static_cast<unsigned long long>(g_c.refHits.load()),
                 static_cast<unsigned long long>(g_c.stored.load()),
                 static_cast<unsigned long long>(g_c.refStored.load()),
                 static_cast<unsigned long long>(g_c.checked.load()),
                 static_cast<unsigned long long>(g_c.mismatches.load()));
}

}  // namespace

void Sync() {
    const bool on = CurrentProfile() != Profile::Faithful && Ext().find_fastpath;
    Object::ExternalFactory::_extFindFast = on ? &kHooks : nullptr;
    if (on) {
        std::atexit(&Dump);
        std::fprintf(stderr, "[FindFastpath] on%s\n", OPENBA_FINDFAST_CHECK ? " (every answer checked)" : "");
    }
}

}}  // namespace Ext::FindFastpath
