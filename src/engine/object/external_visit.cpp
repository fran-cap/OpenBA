// ============================================================================
// src/engine/object/external_visit.cpp -- the resource-reference visit of ONE
//   ExternalPtr, with its shortcut (block VISITPASS, NOT RETAIL, exact)
// ============================================================================
//
// WHAT RETAIL DOES. `ExternalPtr<T>::Serialize` against a VisitorStream
// (Encoding kind 3; external_ptr_serialize.cpp, mode 1) is, for a non-null
// pointer `cur`:
//
//     Filename scratch = cur->GetPath();                  // a copy of cur->_path
//     const External* found = factory->FindOrLoad(scratch);
//     if (found) (void)found->IsDerivedFrom(T);           // result discarded
//
// and FindOrLoad(P) is: `E = ExpandRelativePath(P, true)`, `found = Find(E)`
// (the private Find: ":resource" appended when E has no identifier, a descent
// of `_resources`), and on a hit `if (_loadEnabled) Add(found)`, where Add
// copies `found->GetPath()`, descends `_resources` for it again, ORs the clean
// bit into that row and, when the bit was clear, runs the visit pass over the
// object. On a miss FindOrLoad loads E's compiled file (or asks the extension
// providers) and inserts a new row.
//
// Nothing of that writes to a stream: the whole observable effect is on the
// rows of `_resources` (which rows exist, their usage masks) and the loads it
// triggers. After the real load every pointer visited is a HIT whose row is
// its own, and the visit costs the path round trip (ExpandRelativePath with the
// memo's hashing, Find, Add's GetPath copy and descent) per pointer: ~0.9 s per
// loader pass with zero loads under MegaMod (scratchpad bootprof s1).
//
// THE SHORTCUT. The row the pointer's visit ends on is found from the pointer
// (ptr_index.h: `_resources` row holding `cur`, kept in step at insert, erase
// and clear), and Add's hit arm (`RtObject_ExternalFactory_AddHit`) runs on it.
// It is taken only when all of this holds:
//
//   (a) the row N holding `cur` exists and the index can answer (the index is
//       bound to this tree, its count agrees with the tree's, no pointer is
//       held by two rows);
//   (b) N carries the VERIFIED flag: a previous path walk for this object, made
//       while (c) and (d) held, returned `found == cur`;
//   (c) the factory's path stack is empty;
//   (d) N's key is, character for character, `cur->_path`.
//
// WHY THAT IS EXACT. Retail's walk for `cur` lands on `Find(Expand(P))` with
// P = cur->_path. Under an empty stack Expand's answer is a function of P's
// characters alone (expand_memo.h, point 4: the stack top and the three
// existence probes are the only other inputs, and with no stack the probes
// cannot change the answer). The private Find's answer for a string is a
// function of that string and of which keys the tree holds; the tree never
// holds two fold-equal keys (fold_index.h), so Find(E) is "the row whose key is
// fold-equal to E, or none". Hence Find(Expand(P)) names ONE row for a given P
// and a given key set, and it named N at the verifying walk ((b), with (d): P
// was N's key). A later visit with (d) again asks Find(Expand(same characters)):
// the only way its answer differs from N is a change in the key set that moves
// "the row fold-equal to E" -- N erased (then the index has no row for `cur`
// and (a) fails), or another row with a key fold-equal to E inserted while N is
// live (impossible: an insert meeting a fold-equal key inserts nothing). So
// `found == cur` again, and the walk's one effect is `if (_loadEnabled)
// Add(cur)`: Add descends for cur->_path, which by (d) is N's key, so it lands
// on N and runs its hit arm there. The shortcut runs that same arm on N under
// the same `_loadEnabled` test, and `IsDerivedFrom` (pure) on the same object.
// Identical rows, identical masks, identical visits (the hit arm's recursion is
// the same code), no load either way: the save's reference table, numbered
// from these rows, cannot see the difference.
//
// What the shortcut skips is only work whose effects stay inside the walk: the
// `scratch` / `expanded` / `key` / `probe` Filename copies and their refcount
// traffic, ExpandRelativePath's memo store (the memo is exact and holds the
// body's answer; a store it does not make is a later miss that recomputes it),
// the fold-index reads, and the counters.
//
// REPLACEMENT PATHS, each one checked against (a)-(d):
//   * ForceUnload zeroes a row's mask: the row stays, the pointer stays, nothing
//     above changes; the shortcut sets the bit again exactly as Add would.
//   * Clean deletes the object and erases the row: the index drops the row at
//     Erase, so (a) fails for that pointer (a pointer still held is dangling in
//     retail too, and the walk is what runs for it). An allocation that reuses
//     the address for a new External is a new row holding that address, with
//     its own (unverified) flag.
//   * A remount (Modification::SetupFileSystem: ForceUnload, then the loops'
//     Clean) is the two above. OnVideoRestart destructs and reloads an object IN
//     PLACE (`External::Load(path, existing)`): same pointer, same row, same key.
//   * Something rewrites `cur->_path` after insert (FindOrLoad assigns it BEFORE
//     Add; the extension providers' Pin and Publish set it before their Add):
//     (d) fails and the walk runs. A write of the same characters into another
//     buffer (FindOrLoad's `loaded->_path = expanded` after Pin inserted the
//     row) passes (d) by the strcmp.
//   * ~ExternalFactory frees every row: `IndexClear` unbinds the index.
//   * A non-empty path stack (no caller pushes one today): (c) fails.
//
// THE CHECK. The debug exe runs the walk behind every shortcut it would take
// and compares: `found == cur`, and N's mask afterwards equal to what the hit
// arm would have left (OPENBA_VISITPASS_CHECK, one `[VisitPass] MISMATCH` line
// per difference, a running count at every power of two from 4096). The perf and release
// exes trust it. The verifying walk and the recorded flag are the same in every
// build.
//
// THREADS. The index is read here where the tree is read (the same visit reads
// and writes `_resources` rows through Add); no new exclusion is needed and
// none is added. The walk's ExpandRelativePath took ResLoadGuard around its own
// string work and released it before Find and Add ran; the shortcut has no
// string work.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "external_visit.h"
#include "external_set.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "external_find.h"       // g_fc: the [FindCensus] line
#include "external_compiled.h"   // g_cc; FindOrLoadVisit (VISITREPLAY.md)

// The debug exe walks behind every shortcut and compares (see THE CHECK).
#if defined(_DEBUG)
#define OPENBA_VISITPASS_CHECK 1
#else
#define OPENBA_VISITPASS_CHECK 0
#endif

namespace RtObject {

namespace {

struct VisitCounters {
    uint64_t visits;      // mode-1 visits of a non-null pointer
    uint64_t shortcuts;   // answered from the row in hand
    uint64_t walks;       // retail's path walk ran
    uint64_t verified;    // rows flagged by a walk that landed on its own row
    uint64_t checked;     // OPENBA_VISITPASS_CHECK: shortcuts compared with the walk
    uint64_t mismatches;  // ... and differed
};
VisitCounters g_vc{};

}  // namespace

PtrCensus g_ptrCensus{};

namespace {

// (d): the row's key is `cur->_path`, character for character. The common case
// is one shared buffer (Add's key is a Filename copy of the object's path).
bool SamePath(const rtresset::Node* node, const ::Object::External* cur) {
    const char* key = rtresset::Elem(node)->path._data;
    const char* path = cur->_path._data;
    if (key == path)
        return true;
    if (key == nullptr || path == nullptr)
        return false;
    return std::strcmp(key, path) == 0;
}

}  // namespace

void VisitExternal(::Object::External* cur, const ::Object::BaseTypeInfo& typeInfo) {
    ++g_vc.visits;
    ::Object::ExternalFactory* const f = ::Object::ExternalFactory::_singleton;
    rtresset::Set* const set = reinterpret_cast<rtresset::Set*>(&f->_resources);  // CAST-OK: elem_stride

    // (a) and (b): the row holding `cur`, and its flag.
    rtresset::Node* node = nullptr;
    bool verified = false;
    const bool answered = rtresset::VisitIndexFind(set, set->count, cur, &node, &verified);
    // (c)
    const bool stackEmpty = (f->_pathStack._size == 0);
    // (d)
    const bool rowIsOwn = answered && node != nullptr && stackEmpty && SamePath(node, cur);

    bool shortcut = rowIsOwn && verified;

#if OPENBA_VISITPASS_CHECK
    const bool check = shortcut;
    uint32_t predicted = 0;
    if (check) {
        const uint32_t bit = 1u << (static_cast<uint32_t>(f->_cleanBitIndex) & 31u);
        const uint32_t before = rtresset::Elem(node)->usageMask;
        predicted = f->_loadEnabled ? (before | bit) : before;
        shortcut = false;   // the walk runs; its result is compared below
    }
#endif

    if (shortcut) {
        ++g_vc.shortcuts;
        if (f->_loadEnabled)
            RtObject_ExternalFactory_AddHit(f, node, cur, false);
        (void)cur->IsDerivedFrom(typeInfo.typeId);
        return;
    }

    // ---- retail's walk (external_ptr_serialize.cpp mode 1, 0x1800042ce) -----
    ++g_vc.walks;
    System::Filename scratch = cur->GetPath();                  // [rbp+0x38]
    // NOT RETAIL: FindOrLoad's body without ext.find_fastpath (docs/extensions/handins/VISITREPLAY.md).
    const ::Object::External* found = RtObject_ExternalFactory_FindOrLoadVisit(f, &scratch);
    if (found != nullptr) {
        // The answer is genuinely discarded -- retail calls it for the side
        // effect of nothing, and keeps the pointer it already had.
        (void)found->IsDerivedFrom(typeInfo.typeId);
    }

#if OPENBA_VISITPASS_CHECK
    if (check) {
        static std::atomic<uint64_t> checked{0};
        const uint32_t after = rtresset::Elem(node)->usageMask;
        if (found != cur || after != predicted) {
            ++g_vc.mismatches;
            std::fprintf(stderr, "[VisitPass] MISMATCH path=\"%s\" found=%p cur=%p mask=%08x predicted=%08x\n",
                         cur->_path._data ? cur->_path._data : "(null)", static_cast<const void*>(found),
                         static_cast<const void*>(cur), after, predicted);
            std::fflush(stderr);
        }
        const uint64_t n = checked.fetch_add(1, std::memory_order_relaxed) + 1;
        g_vc.checked = n;
        if (n >= 4096 && (n & (n - 1)) == 0) {
            std::fprintf(stderr, "[VisitPass] checked %llu shortcuts, %llu differ\n",
                         static_cast<unsigned long long>(n), static_cast<unsigned long long>(g_vc.mismatches));
            std::fflush(stderr);
        }
        return;   // the flag is already set
    }
#endif

    // (b) for next time: this walk, made under (c) and (d), landed on the
    // object's own row. The row pointer is still the row's: the walk adds and
    // rotates nodes but never moves an element, and only Clean erases.
    if (rowIsOwn && !verified && found == cur) {
        if (rtresset::VisitIndexSetVerified(set, cur))
            ++g_vc.verified;
    }
}

void DumpVisitCounters() {
    uint64_t size = 0;
    uint64_t duplicates = 0;
    bool broken = false;
    rtresset::VisitIndexStats(&size, &broken, &duplicates);
    // stderr: the perf exe strips printf (src/app/log_strip.h) and this line is the
    // gate's reading of the shortcut (standalone_error.log).
    fprintf(stderr, "[VisitPass] visits=%llu shortcuts=%llu walks=%llu verified=%llu checked=%llu mismatches=%llu "
            "index: rows=%llu broken=%d duplicates=%llu\n",
           (unsigned long long)g_vc.visits, (unsigned long long)g_vc.shortcuts, (unsigned long long)g_vc.walks,
           (unsigned long long)g_vc.verified, (unsigned long long)g_vc.checked,
           (unsigned long long)g_vc.mismatches, (unsigned long long)size, broken ? 1 : 0,
           (unsigned long long)duplicates);
    fprintf(stderr, "[FindCensus] findOrLoad=%llu find=%llu hit=%llu miss=%llu ptrVisit=%llu ptrRead2=%llu ptrRead3=%llu\n",
            (unsigned long long)g_cc.findOrLoadCalls, (unsigned long long)g_fc.calls,
            (unsigned long long)g_cc.cacheHit, (unsigned long long)g_cc.cacheMiss,
            (unsigned long long)g_ptrCensus.visit, (unsigned long long)g_ptrCensus.read2,
            (unsigned long long)g_ptrCensus.read3);
}

}  // namespace RtObject
