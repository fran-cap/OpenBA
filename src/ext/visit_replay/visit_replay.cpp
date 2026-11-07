// ============================================================================
// src/ext/visit_replay/visit_replay.cpp -- ext.visit_replay (class A, next start)
//   NOT RETAIL, exact: docs/extensions/handins/VISITREPLAY.md
// ============================================================================
//
// WHAT RETAIL DOES. ExternalFactory::Add's hit arm (external_add.cpp: ArmMark,
// then on a clear bit VisitRow) ORs the current clean bit into a row and, when
// the bit was clear, serializes the row's object into a VisitorStream. That
// visit's only effect on the resource table is a sequence of calls to the same
// hit arm: each ExternalPtr the object holds goes to the arm on its own row (the
// VISITPASS shortcut) or through FindOrLoad -> Add, which descends for the
// found object's path and runs the arm on the row it lands on. Each of those
// calls recurses the same way on a clear bit and stops on a set one.
//
// THE RECORD. While a row N's visit runs, the rows handed to the arm from it
// (directly: not from the visits nested inside it) are recorded in order, each
// with whether it came through Add. The record is kept only when nothing could
// have made it depend on more than the object's pointers and the key set:
//   - loading enabled and the path stack empty, at both ends (the visit's walk
//     calls Add only under `_loadEnabled`; the stack is Expand's only context);
//   - no FindOrLoad miss and no new row anywhere while it ran (a miss is the one
//     answer an insert could change; a list holds positive answers only);
//   - no row erased while it ran, and no table drop (Clear, an in-place reload,
//     a `_path` rewrite of a row's object);
//   - every arm call's object is the row's own (`external == row->external`).
//
// THE REPLAY. The next first touch of N (another clean bit) finds N's record and
// makes the recorded arm calls in the same order, with the same counters: an
// edge that came through Add is AddDirect (Add's counters, when the row's key
// is the object's path character for character; otherwise Add itself runs),
// then the arm: ArmMark, and on a clear bit the row's own record (pushed on an
// explicit stack, the same depth-first order as the recursion) or, without one,
// its real visit (VisitRow). A row already carrying the bit stops there, exactly
// as in the visit. Why the calls are the same: every call the visit makes is
// the arm on `Find(Expand(P))`'s row (or the object's own row) for a pointer's
// path P. Under an empty stack Expand is a function of P's characters
// (expand_memo.h, point 4); Find's answer is the row whose key is fold-equal to
// that string, and the tree never holds two fold-equal keys, so a positive
// answer survives every insert and changes only when its row is erased (Settle
// drops the store when a record names an erased row) or the key set loses it.
// The pointers themselves are the object's fields: an in-place reload drops the
// store. Game code rewriting an ExternalPtr inside a loaded resource object
// would break this; nothing found does, and the debug exe's shadow below is
// what shows it on the game's whole stream.
//
// THREADS. The loader thread and the main thread both reach Add. Each thread
// keeps its own frames and scratch. The store is read and written under
// ResLoadGuard: a replay holds it while it walks records, and lets go of it
// (keeping its own copy of the records it is in the middle of) around every
// call that can do real work: a row's real visit and a full Add. The epochs are
// atomics, so a load or an erase on one thread spoils a record in flight on the
// other.
//
// THE SHADOW (debug exe; OPENBA_VISITREPLAY_CHECK). At each replay that is not
// nested in another shadow: (A) the replay runs and the rows whose bit it set
// are collected; (B) those bits are cleared again (they were clear before A) and
// the real visit runs, collecting the rows whose bit IT sets and recording N's
// list afresh; the two sets and the two lists are compared. One `[VisitReplay]
// MISMATCH` line per difference; the counts are in the exit line. A or B
// meeting a load is "inconclusive" (A's result stands, nothing is undone).
// ============================================================================

#include "ext/visit_replay/visit_replay.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

#include "ext/registry.h"
#include "ext/visit_replay/edge_store.h"
#include "object/External.h"
#include "object/external_set.h"
#include "object/resource_ext.h"
#include "res_load_lock.h"

#if defined(_DEBUG)
#define OPENBA_VISITREPLAY_CHECK 1
#else
#define OPENBA_VISITREPLAY_CHECK 0
#endif

namespace Ext { namespace VisitReplay {

namespace {

using Object::External;
using Object::ExternalFactory;

// Never destroyed: a loader thread inside Add at process exit must not meet a
// destructed store.
EdgeStore& Store() {
    static EdgeStore* const s = new EdgeStore();
    return *s;
}

std::atomic<uint64_t> g_missEpoch{0};   // FindOrLoad misses + new rows
std::atomic<uint64_t> g_dropEpoch{0};   // erases + table drops

struct Counters {
    std::atomic<uint64_t> replays{0};      // first touches answered from a record
    std::atomic<uint64_t> replayEdges{0};  // arm calls made from records
    std::atomic<uint64_t> recorded{0};     // real visits whose record was kept
    std::atomic<uint64_t> declined{0};     // real visits whose record was not
    std::atomic<uint64_t> drops{0};        // the whole store emptied
    std::atomic<uint64_t> checked{0};      // shadow: replays compared with the real visit
    std::atomic<uint64_t> checkedRows{0};  // ... rows whose bit the real visit set
    std::atomic<uint64_t> inconclusive{0};
    std::atomic<uint64_t> mismatches{0};
    std::atomic<uint64_t> tagOnly{0};      // ... lists equal row for row, Add tags differ
};
Counters g_c;

struct Frame {
    const void* row;
    size_t start;       // this frame's edges begin here in the thread's scratch
    uint64_t miss;
    uint64_t drop;
    bool record;
    bool tainted;
    bool shadowRoot;
};

// The shadow check's phase on this thread (debug exe): None -> Replay -> None when inconclusive,
// None -> Replay -> RealVisit -> None when checked.
enum class ShadowPhase : int {
    None = 0,       // no shadow active
    Replay = 1,     // the replay (A)
    RealVisit = 2,  // the real visit (B)
};

struct ThreadState {
    std::vector<Frame> frames;
    std::vector<uintptr_t> scratch;
    // the shadow (debug exe)
    ShadowPhase shadowPhase = ShadowPhase::None;
    bool shadowPending = false;   // the next visitBegin is B's root
    std::vector<const void*> collect;
    std::vector<const void*> shadowA;
    std::vector<uintptr_t> shadowOld;
};

ThreadState& Tls() {
    thread_local ThreadState t;
    return t;
}

rtresset::Node* NodeOf(const void* row) {
    return static_cast<rtresset::Node*>(const_cast<void*>(row));   // CAST-OK: the seam's row handle
}

uint32_t BitOf(const ExternalFactory& f) {
    return 1u << (static_cast<uint32_t>(f._cleanBitIndex) & 31u);
}

bool Recordable(const ExternalFactory& f) {
    return f._loadEnabled && f._pathStack._size == 0;
}

// Why the store was emptied, for the exit line.
enum DropWhy { kDropClear, kDropReload, kDropPath, kDropSettle, kDropWhyCount };
std::atomic<uint64_t> g_dropsBy[kDropWhyCount];

void DropAll(DropWhy why) {
    g_dropEpoch.fetch_add(1, std::memory_order_relaxed);
    ResLoadGuard guard;
    Store().Clear();
    g_c.drops.fetch_add(1, std::memory_order_relaxed);
    g_dropsBy[why].fetch_add(1, std::memory_order_relaxed);
}

// Settles a sweep's erases (with the lock held), counting a store it empties.
void SettleLocked() REQUIRES(ResLoad) {
    if (Store().HasPending() && Store().Settle()) {
        g_c.drops.fetch_add(1, std::memory_order_relaxed);
        g_dropsBy[kDropSettle].fetch_add(1, std::memory_order_relaxed);
    }
}

// One record being walked: pool[off + i] while it is read in place, or
// own[off + i] once the walk let go of the lock and copied what was left.
struct Level {
    uint32_t off;
    uint32_t cnt;
    uint32_t i;
    bool owned;
};

// The walk of one replay, entered and left with the lock held.
class Walk {
public:
    Walk(ExternalFactory& f, std::vector<const void*>* collect) : _f(f), _collect(collect) {}

    void Run(uint32_t off, uint32_t cnt) REQUIRES(ResLoad) {
        _stack.push_back(Level{off, cnt, 0, false});
        ++_rows;
        while (!_stack.empty()) {
            Level& top = _stack.back();
            if (top.i == top.cnt) {
                _stack.pop_back();
                continue;
            }
            const uintptr_t e = top.owned ? _own[top.off + top.i] : Store().Pool()[top.off + top.i];
            ++top.i;
            ++_edges;
            rtresset::Node* const c = NodeOf(EdgeStore::RowOf(e));
            External* const ce = rtresset::Elem(c)->external;
            if (EdgeStore::ViaAdd(e) && !RtObject::RtObject_ExternalFactory_AddDirect(ce, c)) {
                LetGo();
                RtObject::RtObject_ExternalFactory_Add(&_f, ce);
                ResLoadLockAcquire();
                continue;
            }
            // the hit arm on `c`
            if (!RtObject::RtObject_ExternalFactory_ArmMark(&_f, c))
                continue;
            if (_collect != nullptr)
                _collect->push_back(c);
            uint32_t o = 0;
            uint32_t n = 0;
            if (Recordable(_f) && Store().Find(c, &o, &n)) {
                _stack.push_back(Level{o, n, 0, false});
                ++_rows;
                continue;
            }
            LetGo();
            RtObject::RtObject_ExternalFactory_VisitRow(&_f, c, ce);
            ResLoadLockAcquire();
        }
        g_c.replays.fetch_add(_rows, std::memory_order_relaxed);
        g_c.replayEdges.fetch_add(_edges, std::memory_order_relaxed);
    }

private:
    // Copies what is left of every record still read in place, then releases
    // the lock: the call that follows may record (the pool grows or moves) or
    // drop the store.
    void LetGo() RELEASE(ResLoad) {
        for (Level& l : _stack) {
            if (l.owned)
                continue;
            const uintptr_t* const pool = Store().Pool();
            const uint32_t at = static_cast<uint32_t>(_own.size());
            _own.insert(_own.end(), pool + l.off + l.i, pool + l.off + l.cnt);
            l.cnt -= l.i;
            l.i = 0;
            l.off = at;
            l.owned = true;
        }
        ResLoadLockRelease();
    }

    ExternalFactory& _f;
    std::vector<const void*>* _collect;
    std::vector<Level> _stack;
    std::vector<uintptr_t> _own;
    uint64_t _rows = 0;
    uint64_t _edges = 0;
};

// Replays `row`'s record (found at off/cnt with the lock held); leaves the lock
// held. A non-recording frame keeps a full Add made from here out of an
// enclosing visit's record.
void RunRecord(ExternalFactory& f, uint32_t off, uint32_t cnt, std::vector<const void*>* collect)
    REQUIRES(ResLoad) {
    ThreadState& t = Tls();
    t.frames.push_back(Frame{nullptr, t.scratch.size(), 0, 0, false, false, false});
    Walk walk(f, collect);
    walk.Run(off, cnt);
    Tls().frames.pop_back();
}

#if OPENBA_VISITREPLAY_CHECK
const char* KeyOf(const void* row) {
    const char* k = rtresset::Elem(NodeOf(row))->path._data;
    return k != nullptr ? k : "(null)";
}

// B's end: the two sets of rows marked, and the two lists.
void ShadowCompare(const void* row, const uintptr_t* fresh, size_t freshCount) {
    ThreadState& t = Tls();
    std::vector<const void*> b = t.collect;
    std::vector<const void*> a = t.shadowA;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    g_c.checked.fetch_add(1, std::memory_order_relaxed);
    g_c.checkedRows.fetch_add(b.size(), std::memory_order_relaxed);
    // The lists are compared row for row. The Add tag may differ: an edge recorded from the
    // verifying walk (FindOrLoad -> Add) is the VISITPASS shortcut in a later visit; both run the
    // arm on the same row with the same object (AddDirect requires the row's key to be the
    // object's path, or runs Add), and only Add's own counters see the difference.
    bool listSame = t.shadowOld.size() == freshCount;
    bool tagsSame = listSame;
    for (size_t i = 0; listSame && i < freshCount; ++i) {
        listSame = EdgeStore::RowOf(t.shadowOld[i]) == EdgeStore::RowOf(fresh[i]);
        tagsSame = tagsSame && t.shadowOld[i] == fresh[i];
    }
    if (listSame && !tagsSame)
        g_c.tagOnly.fetch_add(1, std::memory_order_relaxed);
    if (a != b || !listSame) {
        g_c.mismatches.fetch_add(1, std::memory_order_relaxed);
        std::vector<const void*> diff;
        std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(diff));
        std::fprintf(stderr,
                     "[VisitReplay] MISMATCH root=\"%s\" replayMarked=%zu visitMarked=%zu differ=%zu first=\"%s\" "
                     "list=%zu/%zu%s\n",
                     KeyOf(row), a.size(), b.size(), diff.size(), diff.empty() ? "-" : KeyOf(diff.front()),
                     t.shadowOld.size(), freshCount, listSame ? "" : " (lists differ)");
        std::fflush(stderr);
    }
    const uint64_t n = g_c.checked.load(std::memory_order_relaxed);
    if (n >= 1024 && (n & (n - 1)) == 0) {
        std::fprintf(stderr, "[VisitReplay] checked %llu replays (%llu rows), %llu differ\n",
                     static_cast<unsigned long long>(n),
                     static_cast<unsigned long long>(g_c.checkedRows.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(g_c.mismatches.load(std::memory_order_relaxed)));
        std::fflush(stderr);
    }
}

// A, then the undo; false = the engine runs B (the real visit) next. Entered
// and left with the lock held.
bool Shadow(ExternalFactory& f, uint32_t off, uint32_t cnt) REQUIRES(ResLoad) {
    ThreadState& t = Tls();
    t.shadowOld.assign(Store().Pool() + off, Store().Pool() + off + cnt);
    const uint64_t miss0 = g_missEpoch.load(std::memory_order_relaxed);
    const uint64_t drop0 = g_dropEpoch.load(std::memory_order_relaxed);
    t.collect.clear();
    t.shadowPhase = ShadowPhase::Replay;
    RunRecord(f, off, cnt, &t.collect);
    t.shadowPhase = ShadowPhase::None;
    t.shadowA.swap(t.collect);
    t.collect.clear();
    if (g_missEpoch.load(std::memory_order_relaxed) != miss0 || g_dropEpoch.load(std::memory_order_relaxed) != drop0) {
        g_c.inconclusive.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    const uint32_t bit = BitOf(f);
    for (const void* r : t.shadowA)
        rtresset::Elem(NodeOf(r))->usageMask &= ~bit;
    t.shadowPhase = ShadowPhase::RealVisit;
    t.shadowPending = true;
    return false;
}
#endif

// ---- the hooks (object/resource_ext.h) --------------------------------------

void Edge(ExternalFactory& f, void* row, External* external, bool viaAdd) {
    ThreadState& t = Tls();
    if (t.shadowPhase != ShadowPhase::None && (rtresset::Elem(NodeOf(row))->usageMask & BitOf(f)) == 0)
        t.collect.push_back(row);
    if (t.frames.empty())
        return;
    Frame& top = t.frames.back();
    if (!top.record)
        return;
    if (external != rtresset::Elem(NodeOf(row))->external) {
        top.tainted = true;
        return;
    }
    t.scratch.push_back(EdgeStore::MakeEdge(row, viaAdd));
}

bool Replay(ExternalFactory& f, void* row, External* external) {
    ThreadState& t = Tls();
    if (t.shadowPhase == ShadowPhase::RealVisit)
        return false;   // the shadow's real visit
    if (!Recordable(f) || external != rtresset::Elem(NodeOf(row))->external)
        return false;
    ResLoadGuard guard;
    SettleLocked();
    uint32_t off = 0;
    uint32_t cnt = 0;
    if (!Store().Find(row, &off, &cnt))
        return false;
#if OPENBA_VISITREPLAY_CHECK
    if (t.shadowPhase == ShadowPhase::None)
        return Shadow(f, off, cnt);
#endif
    RunRecord(f, off, cnt, t.shadowPhase != ShadowPhase::None ? &t.collect : nullptr);
    return true;
}

void VisitBegin(ExternalFactory& f, void* row) {
    ThreadState& t = Tls();
    Frame fr{row,
             t.scratch.size(),
             g_missEpoch.load(std::memory_order_relaxed),
             g_dropEpoch.load(std::memory_order_relaxed),
             Recordable(f),
             false,
             t.shadowPending};
    t.shadowPending = false;
    t.frames.push_back(fr);
}

void VisitEnd(ExternalFactory& f, void* row) {
    ThreadState& t = Tls();
    const Frame fr = t.frames.back();
    t.frames.pop_back();
    const uintptr_t* const edges = t.scratch.data() + fr.start;
    const size_t count = t.scratch.size() - fr.start;
#if OPENBA_VISITREPLAY_CHECK
    if (fr.shadowRoot) {
        ShadowCompare(row, edges, count);
        t.shadowPhase = ShadowPhase::None;
        t.collect.clear();
    }
#endif
    const bool keep = fr.record && !fr.tainted && Recordable(f) &&
                      g_missEpoch.load(std::memory_order_relaxed) == fr.miss &&
                      g_dropEpoch.load(std::memory_order_relaxed) == fr.drop;
    if (keep) {
        ResLoadGuard guard;
        if (g_dropEpoch.load(std::memory_order_relaxed) == fr.drop) {
            SettleLocked();
            Store().Put(row, edges, count);
            g_c.recorded.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        g_c.declined.fetch_add(1, std::memory_order_relaxed);
    }
    t.scratch.resize(fr.start);
}

void Event(ExternalFactory& f, Object::ResourceEvent e, void* row, const External* external, const char* newPath) {
    switch (e) {
    case Object::ResourceEvent::Insert:
    case Object::ResourceEvent::Miss:
        g_missEpoch.fetch_add(1, std::memory_order_relaxed);
        return;
    case Object::ResourceEvent::Erase: {
        g_dropEpoch.fetch_add(1, std::memory_order_relaxed);
        ResLoadGuard guard;
        Store().Erase(row);
        return;
    }
    case Object::ResourceEvent::Clear:
        DropAll(kDropClear);
        return;
    case Object::ResourceEvent::Reload: {
        // The object is rebuilt in place: its own pointers (its record) may change; its row, its
        // address and its key do not, so the records naming that row stand. A row the index
        // cannot name empties the store.
        g_dropEpoch.fetch_add(1, std::memory_order_relaxed);
        rtresset::Set* const set = reinterpret_cast<rtresset::Set*>(&f._resources);  // CAST-OK: elem_stride
        rtresset::Node* node = nullptr;
        bool verified = false;
        if (!rtresset::VisitIndexFind(set, set->count, external, &node, &verified)) {
            DropAll(kDropReload);
            return;
        }
        if (node != nullptr) {
            ResLoadGuard guard;
            Store().Forget(node);
        }
        return;
    }
    case Object::ResourceEvent::PathWrite: {
        // A fresh object's first path cannot move a record (no row holds it), nor can the same
        // characters again. A row's object whose `_path` an in-place reload's constructor nulled,
        // given its row key's characters back, is the object the records were made with (the
        // window between the two is the reload's own, on one thread). Anything else empties the
        // store: Add's descent for that object, or the visit's own-row test, could answer otherwise.
        rtresset::Set* const set = reinterpret_cast<rtresset::Set*>(&f._resources);  // CAST-OK: elem_stride
        rtresset::Node* node = nullptr;
        bool verified = false;
        const bool answered = rtresset::VisitIndexFind(set, set->count, external, &node, &verified);
        if (answered && node == nullptr)
            return;
        auto equal = [](const char* a, const char* b) {
            return a == b || (a != nullptr && b != nullptr && std::strcmp(a, b) == 0);
        };
        const char* const old = external->_path._data;
        bool same = equal(old, newPath);
        if (!same && old == nullptr && answered)
            same = equal(rtresset::Elem(node)->path._data, newPath);
        if (!same)
            DropAll(kDropPath);
        return;
    }
    case Object::ResourceEvent::RefTable:
        return;
    }
}

const Object::ExtVisitReplay kHooks = {&Edge, &Replay, &VisitBegin, &VisitEnd, &Event};

void Dump() {
    size_t lists = 0;
    size_t edges = 0;
    {
        ResLoadGuard guard;
        lists = Store().Lists();
        edges = Store().Edges();
    }
    // stderr: the perf exe strips printf (src/app/log_strip.h).
    std::fprintf(stderr,
                 "[VisitReplay] replays=%llu edges=%llu recorded=%llu declined=%llu drops=%llu (clear %llu reload %llu "
                 "path %llu sweep %llu) store: lists=%zu "
                 "edges=%zu shadow: checked=%llu rows=%llu inconclusive=%llu mismatches=%llu tagOnly=%llu\n",
                 static_cast<unsigned long long>(g_c.replays.load()),
                 static_cast<unsigned long long>(g_c.replayEdges.load()),
                 static_cast<unsigned long long>(g_c.recorded.load()),
                 static_cast<unsigned long long>(g_c.declined.load()),
                 static_cast<unsigned long long>(g_c.drops.load()),
                 static_cast<unsigned long long>(g_dropsBy[kDropClear].load()),
                 static_cast<unsigned long long>(g_dropsBy[kDropReload].load()),
                 static_cast<unsigned long long>(g_dropsBy[kDropPath].load()),
                 static_cast<unsigned long long>(g_dropsBy[kDropSettle].load()), lists, edges,
                 static_cast<unsigned long long>(g_c.checked.load()),
                 static_cast<unsigned long long>(g_c.checkedRows.load()),
                 static_cast<unsigned long long>(g_c.inconclusive.load()),
                 static_cast<unsigned long long>(g_c.mismatches.load()),
                 static_cast<unsigned long long>(g_c.tagOnly.load()));
}

}  // namespace

void Sync() {
    const bool on = CurrentProfile() != Profile::Faithful && Ext().visit_replay;
    Object::ExternalFactory::_extVisitReplay = on ? &kHooks : nullptr;
    if (on) {
        std::atexit(&Dump);
        std::fprintf(stderr, "[VisitReplay] on%s\n", OPENBA_VISITREPLAY_CHECK ? " (shadow: every replay checked)" : "");
    }
}

}}  // namespace Ext::VisitReplay
