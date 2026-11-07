// ============================================================================
// src/engine/object/external_add.cpp — Wave 11 follow-up 2, lane IOSTREAM
//   `Object::ExternalFactory::Add`                              retail 0x1800be570
// ============================================================================
//
//   ?Add@ExternalFactory@Object@@QEAAXPEAVExternal@2@@Z
//
// THE ROW LANE OBJEXT OPENED. Porting `FindOrLoad` meant naming `Add`, because
// `FindOrLoad` calls it unconditionally on both branches — so `Add` entered the
// unported set in the same commit that closed four other rows
// (WAVE11_LANE_OBJEXT.md §5.2). This closes it.
//
// ---------------------------------------------------------------------------
// WHY IT WAS BLOCKED, AND WHAT ACTUALLY UNBLOCKED IT
// ---------------------------------------------------------------------------
// OBJEXT recorded the blocker as "stack-constructs an IO::VisitorStream by
// value, which has no layout or constructor in the tree". Half of that is
// right. `??0VisitorStream@IO@@QEAA@XZ` (ordinal 511) and
// `??1VisitorStream@IO@@UEAA@XZ` (ordinal 715) are both EXPORTED — this DLL
// exports its private and protected members, and reading `QEAA`/`AEAA` as
// "unexported" is the mistake reference_aeaa_is_private_not_unexported.md
// records. What was genuinely missing was the LAYOUT, i.e. how many bytes to
// reserve and which fields the inlined destructor touches.
//
// src/engine/io/FileStream.h now pins it: `IO::VisitorStream` is 0x18
// bytes, adds NO fields to `IO::Stream`, and its `Write` is literally
// `return true`. It is a NULL SINK, and that is the whole mechanism of this
// function's interesting branch — see below.
//
// ---------------------------------------------------------------------------
// WHAT THE FUNCTION DOES
// ---------------------------------------------------------------------------
// Register `external` in the factory's path-keyed resource tree, and mark it
// used by the CURRENT clean generation:
//
//   1. copy the External's own path (`GetPath`, a by-value return with a
//      hidden slot) into a local,
//   2. descend `_resources` for that path,
//   3. MISS  -> insert {path, external, usageMask = 1 << _cleanBitIndex,
//               referenceId = -1},
//      HIT   -> OR the current clean bit into the existing row's usageMask,
//               and IF THAT BIT WAS NOT ALREADY SET, run the reference-visit
//               pass over the object,
//   4. release the local path.
//
// STEP 3'S "IF THAT BIT WAS NOT ALREADY SET" IS THE POINT OF THE WHOLE
// FUNCTION. The visit pass runs exactly once per resource per clean
// generation, not once per Add — and `FindOrLoad` reaches Add ~22.6k times in
// one menu-to-gameplay run (lane FNHOOK's `[ResLoad] census: FindOrLoad=22591`),
// so a port that ran the pass unconditionally would do tens of thousands of
// full object walks instead of a few thousand. That is a behavioural
// difference, not just a slow one: the pass has side effects.
//
// THE VISIT PASS. `VisitorStream` accepts every byte and stores none, so
// calling the External's own `Serialize` (vtable +0x28) against it walks the
// object exactly as a save would and writes nothing. The only thing that
// actually happens is the side effect in the Encoding the VisitorStream's
// constructor installs at +0x10 (vtable 0x1801559f8), which records the
// resource references the walk passes through. Serializer.h:154 and
// serialize_family.cpp:125 describe the same pass from the Serializer end;
// this is the stream end of it.
//
// ---------------------------------------------------------------------------
// EVERYTHING ELSE IT NEEDS WAS ALREADY IN THE TREE
// ---------------------------------------------------------------------------
//   ?GetPath@External@Object@@         0x180006250   ported, external.cpp
//   Set<ResourceEntry>::Insert         0x18002ca00   ported, external_set.cpp
//   the AVL node layout / EntryValue                 external_set.h
//   ResourceEntry field offsets                      types/object/External.h
//   Filename copy-ctor and the String release idiom  external_set.cpp
//
// so the only new code here is the descent, the clean-bit test, and the
// stack-constructed VisitorStream.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "external_set.h"
#include "collection/fold_index.h"   // block PATHINDEX
#include "system/string_internal.h"
#include "object/External.h"
#include "io/FileStream.h"
#include "object/resource_ext.h"   // NOT RETAIL: the ext.visit_replay / ext.find_fastpath seams

// `??0Filename@System@@QEAA@AEBV01@@Z` (0x1800067d0) used to be forward-declared
// here as a C entry point. ENDGAME B.3.1 step 2 calls it as what it is — the
// `System::Filename` copy constructor — so the declaration is gone with its one
// call site.


namespace RtObject {


namespace {

// `?GetPath@External@Object@@QEBA?AVFilename@System@@XZ` (0x180006250) returns
// a Filename BY VALUE: RCX is `this` and RDX the hidden return slot, read off
// the call site (1800be594 lea rdx,[rbp+0x30]; 1800be598 mov rcx,rsi). B3R
// RT-Core: RtObject_External_GetPath is called by name.

// External vtable slot +0x28 — `Serialize(IO::Stream&)`. The same slot
// `External::Load` drives and the one External.h names. B3X W0: called by
// name; the bool result is discarded, as retail does here.
void ExternalSerialize(::Object::External* self, IO::Stream& stream) {
    self->Serialize(stream);
}

// ---------------------------------------------------------------------------
// The comparator, and why it is spelled out here again.
//
// This is the third copy in the tree (external.cpp's `StriCmpAscii` /
// `FilenameLess`, external_set.cpp's, and this one). They are duplicated rather
// than shared because the other two are `static` in files other lanes own, and
// because reference_a_rule_on_the_callers_side_gets_broken.md is about exactly
// what happens when a shared helper drifts from the call site that depends on
// it. The behaviour is pinned by the assertion below, not by inspection.
//
// Retail's comparisons go through the CRT `_stricmp` at 0x18007eefc. The DLL
// never calls `setlocale`, so that runs in the "C" locale: plain ASCII case
// folding. `runtime/src` must not depend on a CRT whose locale behaviour
// differs between toolchains (PLAN §4.1).
// ---------------------------------------------------------------------------
int StriCmpAscii(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0)  return 0;
    }
}

// `a < b`, with retail's ASYMMETRIC null arms transcribed from the branch
// structure rather than from an idea of what a comparator should do: a null
// RIGHT-hand side orders below everything, a null LEFT-hand side orders below
// any non-null. Identical to external_set.cpp's, minus its counters.
bool FilenameLess(const char* a, const char* b) {
    if (b == nullptr) return false;
    if (a == nullptr) return true;
    return StriCmpAscii(a, b) < 0;
}

// ENDGAME B.3.1 step 2 removed this file's `ReleaseInline(Filename*)` helper.
// Both of its call sites were plain locals, and retail's inlined release on a
// local is `~Filename` (0x180006980) — so the two are now destructors and there
// is nothing left for the helper to do.

}  // namespace

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS.
//
// WAVE9's standing rule and reference_calls_zero_can_mean_always_took_the_
// uncovered_branch.md: an entry count cannot tell a body that ran and did
// nothing interesting from one that did. The two numbers that matter here are
// `hitVisited` and `hitSkipped` — the clean-bit branch, which is the ONLY
// branch in this function with a side effect beyond the tree — and they are
// the ones a "run the pass every time" paraphrase would collapse into one.
// ---------------------------------------------------------------------------
struct AddCounters {
    uint64_t calls;
    uint64_t inserted;    // descent fell off the tree
    uint64_t hit;         // equal key found
    uint64_t hitVisited;  // ... and the clean bit was NOT already set -> visit pass ran
    uint64_t hitSkipped;  // ... and it WAS already set -> no pass
    uint64_t descLeft, descRight;
    uint64_t emptyTree;   // root was null on entry
};
AddCounters g_ac{};

// ---------------------------------------------------------------------------
// THE PASS CENSUS (NOT RETAIL, print-only; docs/extensions/handins/VISITREPLAY.md
// section 1). Rows whose bit a first touch set, per loader bracket: the count
// and an order-free sum of a mix of the row pointers (equal within one run when
// two passes mark the same rows). The debug exe also sums a hash of the rows'
// keys, which is comparable across runs (ext.visit_replay on against off).
// Loader::Run prints and resets it (loops.cpp). Racy by design when the main
// thread marks rows during a loader bracket: the line counts both.
// ---------------------------------------------------------------------------
#if defined(_DEBUG)
#define OPENBA_VISITCENSUS_PATHHASH 1
#else
#define OPENBA_VISITCENSUS_PATHHASH 0
#endif

namespace {

struct PassCensus {
    uint64_t marked;
    uint64_t ptrHash;
    uint64_t pathHash;
};
PassCensus g_pass{};

uint64_t CensusMix(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    return h ^ (h >> 33);
}

#if OPENBA_VISITCENSUS_PATHHASH
uint64_t CensusPathHash(const char* s) {
    uint64_t h = 1469598103934665603ull;
    if (s == nullptr)
        return h;
    for (; *s; ++s) {
        h ^= static_cast<unsigned char>(*s);
        h *= 1099511628211ull;
    }
    return h;
}
#endif

}  // namespace

void VisitCensusPass(const char* what, const char* loopName, int bit) {
    if (g_pass.marked != 0) {
        // stderr: the perf exe strips printf (src/app/log_strip.h).
        fprintf(stderr, "[VisitCensus] %s %s bit=%d marked=%llu ptrHash=%016llx pathHash=%016llx\n", what, loopName,
                bit, (unsigned long long)g_pass.marked, (unsigned long long)g_pass.ptrHash,
                (unsigned long long)g_pass.pathHash);
    }
    g_pass = PassCensus{};
}

// ============================================================================
// Add's HIT arm, 1800be68b..1800be6c7, on the node the descent landed on:
// OR the clean bit in, and when it was not already set run the reference-visit
// pass over the object. Its own function because the visit pass's shortcut
// (block VISITPASS, external_visit.cpp) reaches this arm with the node it
// already holds, skipping the descent and nothing else. `viaAdd` (NOT RETAIL)
// says which of the two reached it, for ext.visit_replay's record.
//
// NOT RETAIL in shape only (docs/extensions/handins/VISITREPLAY.md): the arm is
// split at its branch so ext.visit_replay can run the same two halves --
// `ArmMark` (the OR, the counters, "was it clear") and `VisitRow` (the visit) --
// on the rows a record names.
// ============================================================================
bool RtObject_ExternalFactory_ArmMark(ExternalFactory* self, rtresset::Node* node) {
    // The clean-generation bit for THIS generation (the same expression as the
    // body's below: `_cleanBitIndex` masked with `& 0x1f`, as retail's SHL does).
    const uint32_t bit = 1u << (static_cast<uint32_t>(self->_cleanBitIndex) & 31u);

    ResourceEntry* const entry = rtresset::Elem(node);
    const uint32_t before = entry->usageMask;
    entry->usageMask = before | bit;                            // 1800be694

    if ((bit & before) != 0) {
        ++g_ac.hitSkipped;
        return false;
    }
    // First time this resource has been touched in this clean generation: the
    // caller runs the reference-visit pass.
    ++g_ac.hitVisited;
    ++g_pass.marked;   // the pass census (print-only)
    g_pass.ptrHash += CensusMix(reinterpret_cast<uintptr_t>(node));   // CAST-OK: the address is the datum
#if OPENBA_VISITCENSUS_PATHHASH
    g_pass.pathHash += CensusMix(CensusPathHash(entry->path._data));
#endif
    return true;
}

void RtObject_ExternalFactory_VisitRow(ExternalFactory* self, rtresset::Node* node, ::Object::External* external) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): ext.visit_replay records the
    // rows this visit hands to the arm. Null by default.
    const ::Object::ExtVisitReplay* const replay = ::Object::ExternalFactory::_extVisitReplay;
    if (replay != nullptr)
        replay->visitBegin(*self, node);

    // ---- the stack-constructed IO::VisitorStream --------------------
    // 0x18 bytes of stack, built by the REAL constructor (ENDGAME
    // B.3.9 IO: retail's ??0VisitorStream 0x1800bb090 -- vptr,
    // direction 1, the Visitor encoding). `Stream::pad0c` is not
    // written, as in retail.
    alignas(8) unsigned char vsMem[sizeof(IO::VisitorStream)];
    IO::VisitorStream* vs = ::new (vsMem) IO::VisitorStream();

    ExternalSerialize(external, *vs);                       // 1800be6a9

    // ---- the INLINED ~VisitorStream (1800be673..) --------------------
    // Retail's two table stores and the encoding free are
    // ~VisitorStream -> ~Stream; slot 0 with flags 0 runs exactly that
    // (called non-virtually, as retail's inlined copy is).
    vs->IO::VisitorStream::~VisitorStream();

    if (replay != nullptr)
        replay->visitEnd(*self, node);
}

void RtObject_ExternalFactory_AddHit(ExternalFactory* self, rtresset::Node* node, ::Object::External* external,
                                     bool viaAdd) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): ext.visit_replay records
    // which rows a first-touch visit hands to this arm. Null by default.
    const ::Object::ExtVisitReplay* const replay = ::Object::ExternalFactory::_extVisitReplay;
    if (replay != nullptr)
        replay->edge(*self, node, external, viaAdd);

    if (!RtObject_ExternalFactory_ArmMark(self, node))
        return;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): ext.visit_replay hands the
    // rows this visit would hand to the arm to it again, from the visit's last record.
    if (replay != nullptr && replay->replay(*self, node, external))
        return;
    RtObject_ExternalFactory_VisitRow(self, node, external);
}

// ============================================================================
// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md): `Add(found)` for a
// `found` whose row is already in hand. Add copies found->GetPath(), descends
// `_resources` for it and runs the hit arm on the row it lands on. When the
// row's key is `found->_path` character for character, that descent lands on
// this row (the tree never holds two fold-equal keys: an insert meeting one
// inserts nothing), so the hit arm can run there directly: `AddDirect` says
// whether it can and, when it can, counts Add's call and hit. Otherwise Add runs.
// ============================================================================
bool RtObject_ExternalFactory_AddDirect(::Object::External* found, rtresset::Node* node) {
    const char* key = rtresset::Elem(node)->path._data;
    const char* path = found->_path._data;
    bool same = (key == path);
    if (!same && key != nullptr && path != nullptr)
        same = std::strcmp(key, path) == 0;
    if (!same || rtresset::Elem(node)->external != found)
        return false;
    ++g_ac.calls;
    ++g_ac.hit;
    return true;
}

void RtObject_ExternalFactory_AddFound(ExternalFactory* self, ::Object::External* found, rtresset::Node* node) {
    if (!RtObject_ExternalFactory_AddDirect(found, node)) {
        RtObject_ExternalFactory_Add(self, found);
        return;
    }
    RtObject_ExternalFactory_AddHit(self, node, found, true);
}

// ============================================================================
// Object::ExternalFactory::Add — the body.               retail 0x1800be570
// ============================================================================
extern "C" void RtObject_ExternalFactory_Add(ExternalFactory* self, ::Object::External* external) {
    ++g_ac.calls;

    // ---- 1800be589..1800be5d9 -------------------------------------------
    // `Filename path = external->GetPath();`
    //
    // Retail spills GetPath's by-value result into one slot, COPY-CONSTRUCTS a
    // second Filename from it, and releases the first. It is not a wasted copy
    // that can be elided: `GetPath` hands back a Filename holding a NEW
    // reference, the copy takes a SECOND one, and the release drops the first —
    // so collapsing the pair would leave the refcount one too high on every one
    // of the ~22.6k calls a run makes. Transcribed as retail wrote it.
    // ENDGAME B.3.1 step 2. `tmp` is GetPath's hidden return slot: it is
    // declared null and CONSTRUCTED INTO by the call, never assigned to.
    System::Filename tmp;
    RtObject_External_GetPath(external, &tmp);   // 0x180006250

    // 1800be5d3 — the second reference the comment above is about, taken by
    // retail's copy ctor (0x1800067d0) and kept.
    System::Filename path(tmp);
    // `tmp`'s 1800be5d9 release is its destructor, which runs at the end of the
    // function rather than here. `path` holds a reference to the same buffer
    // across the whole body, so the buffer's lifetime is unchanged and nothing
    // between the two points reads either refcount.

    // ---- 1800be5e2..1800be6b8 -------------------------------------------
    // The descent, inlined by MSVC rather than calling the exported
    // `Set<ResourceEntry>::Find` — and it has to be inlined here, because this
    // one needs the NODE (to update its usageMask in place) while `Find`
    // returns the `External*`.
    rtresset::Set* const set = reinterpret_cast<rtresset::Set*>(&self->_resources);  // CAST-OK: elem_stride
    // NOT RETAIL (block PATHINDEX): the tree's hash index answers when it can
    // (src/engine/collection/fold_index.h); the descent runs otherwise.
    rtresset::Node* indexed = nullptr;
    const bool answered = rtresset::IndexedFind(set, set->count, path._data, &indexed);
    if (set->root == nullptr) ++g_ac.emptyTree;

    rtresset::Node* node = nullptr;
    if (answered && !OPENBA_PATHINDEX_CHECK) {
        node = indexed;
    } else {
        node = set->root;
        while (node != nullptr) {
            const char* const nodeKey = rtresset::Elem(node)->path._data;
            if (FilenameLess(path._data, nodeKey)) {
                ++g_ac.descLeft;
                node = node->left;                              // 1800be6bb
                continue;
            }
            if (FilenameLess(nodeKey, path._data)) {
                ++g_ac.descRight;
                node = node->right;                             // 1800be6b2
                continue;
            }
            break;                                              // equal key
        }
        Collection::FoldIndex::Check("resources/Add", path._data, answered, indexed, node);
    }

    // The clean-generation bit for THIS generation. `_cleanBitIndex` is a plain
    // int and retail masks the shift count with `& 0x1f` (`SHL EAX,CL` on x86
    // does that in hardware; the mask is explicit in the decompilation and is
    // reproduced rather than relied on, because C++ shifts by >= 32 are UB).
    const uint32_t bit = 1u << (static_cast<uint32_t>(self->_cleanBitIndex) & 31u);

    if (node != nullptr) {
        // ---- HIT: 1800be68b..1800be6c7 ----------------------------------
        ++g_ac.hit;
        RtObject_ExternalFactory_AddHit(self, node, external, true);
    } else {
        // ---- MISS: 1800be699..1800be6c2 ---------------------------------
        ++g_ac.inserted;

        // The 0x10-byte value half, built on the stack SEPARATELY from the key
        // and handed in a different register — which is why external_set.h
        // gives it its own type rather than passing a whole ResourceEntry.
        //
        // `referenceId = -1` is the seed External.h records; it is NOT a
        // "not found" sentinel invented here.
        rtresset::EntryValue value;
        value.external    = external;
        value.usageMask   = bit;
        value.referenceId = ::Object::kNoReferenceId;

        rtresset::Node* out = nullptr;
        rtresset::Insert(set, &out, &path, &value);             // 1800be6bd
    }

    // ---- 1800be6c7..1800be6f7 -------------------------------------------
    // Released on EVERY path, hit and miss alike. `Insert` copies the key into
    // the node and takes its own reference, so skipping this on the insert path
    // would leak one string reference per resource.
    // ...and that release is `path`'s destructor now.

}

void DumpAddCounters() {
    printf("[ExtAdd] calls=%llu insert=%llu hit=%llu "
                "(visited=%llu skipped=%llu) descL=%llu descR=%llu empty=%llu\n",
                (unsigned long long)g_ac.calls,
                (unsigned long long)g_ac.inserted,
                (unsigned long long)g_ac.hit,
                (unsigned long long)g_ac.hitVisited,
                (unsigned long long)g_ac.hitSkipped,
                (unsigned long long)g_ac.descLeft,
                (unsigned long long)g_ac.descRight,
                (unsigned long long)g_ac.emptyTree);
}

}  // namespace RtObject

// EXT SEAMS (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): object/resource_ext.h.
const Object::ExtVisitReplay* Object::ExternalFactory::_extVisitReplay = nullptr;
const Object::ExtFindFast* Object::ExternalFactory::_extFindFast = nullptr;
