#pragma once
// ============================================================================
// src/engine/graphics/draw_sort.h -- DRAWSORT target 2: the per-pass draw-item sort
// ============================================================================
//
// The comparator and the swap of retail's draw-item sort (0x180062d60 /
// 0x180063b90; banner in wave27_lane_gfxbig.cpp), and NOT RETAIL beside them:
// an exact faster sort (docs/extensions/handins/DRAWSORT.md section 2).
//
// WHY RETAIL'S ALGORITHM STAYS. The consumer, `Renderer::DrawItemList`
// (batchc_gfx_leaves.cpp), reads `_maxInstances` (the instancing batch limit) and
// `_maxInstances` is not one of the eight compared terms; and a NaN `_sortKey` makes the
// comparator a non-strict-weak order, where the result depends on the algorithm.
// So this keeps retail's skeleton (introsort_msvc.h) and its swap sequence, and
// makes each DECISION cheaper:
//
//   * `Keys`: each item's eight compared terms are packed once, most significant
//     first, into the high 288 bits of a 320-bit key (five words); the low 32
//     bits are the item's index. Comparing the 288 bits lexicographically IS
//     DrawItemLess (proof: DRAWSORT.md section 2.2): every term is a fixed-width
//     field whose unsigned order is the term's order (the float term mapped
//     order-reversingly, +0 and -0 to one value, a denormal to the zero value
//     exactly when `== 0.0f` says so under the live MXCSR), and the four pointer
//     terms fit 48 bits each (checked per list).
//   * retail's skeleton runs on the KEY array: the same decisions at the same
//     positions, so the same swap sequence; the items are then moved once, by the
//     index each final key carries. Every swap moves a whole record, so the item
//     array ends bit for bit as retail's sort leaves it, the two never-written pad
//     dwords included.
//   * a list with a NaN `_sortKey` or a pointer at or above 2^48 cannot be keyed:
//     it takes retail's skeleton on the items with the comparator and swap
//     inlined (the same code as the reference, without the indirect calls).
//
// SORTCMP (docs/extensions/handins/SORTCMP.md): FEWER COMPARES, EXACT FOR THE
// CONSUMER. After any correct sort, items with equal full keys are contiguous.
// Equal keys mean equal flags, material, instance, arg5, arg10, userData and
// geometry; if their `_maxInstances` is equal too, the records agree on every field
// `DrawItemList` reads (the pads and the raw `_sortKey` bits are never read),
// so the order inside such a tie group cannot change a draw. `SortPlain`
// therefore sorts the keyed list by any correct method (hash-group by the
// 240-bit prefix above the geometry term, sort the distinct prefixes, sort each
// group by its 48-bit geometry) and scans the result: one tie whose `_maxInstances`
// differs and the list falls back to the keyed skeleton above, run from the
// ORIGINAL order (neither the keys nor the items are touched before the final
// gather). Kill switch `kPlainSort`.
//
// SORTMEMO (docs/extensions/handins/SORTMEMO.md): THE SAME LIST AS LAST FRAME.
// Every path above is a pure function of the list's bytes outside the two pad
// dwords (and of MXCSR.DAZ, which decides whether a denormal sort key equals
// zero): no path reads a pad, and the scratch is rewritten before it is read.
// Every path moves whole records, so the output is the input permuted. So per
// item buffer the memo keeps the previous sort's input, output and permutation;
// the next sort of that buffer compares the fresh input with the stored one
// (sequentially, exiting at the first differing non-pad byte). Equal bytes:
// the stored output is copied over the list. Equal except pad bytes: the stored
// permutation is applied to the fresh items (it carries the fresh pads), which
// is what a fresh sort would do. Anything else: a miss, the list is sorted and
// the entry rewritten. The buffer is only where to look: a hit needs the full
// compare, so a buffer reused by another list cannot hit wrongly. At most
// kMemoMaxEntries entries; one unused for kMemoMaxAge sorts is freed. Kill
// switch `kSortMemo`.
// ============================================================================

#include <cstdint>
#include <functional>

#include "graphics/Camera.h"   // Graphics::RendererDrawItem

namespace DrawSort {

// The kill switch. false = every draw-item sort is the reference (retail's
// skeleton with the comparator and swap called through function pointers).
inline constexpr bool kFastSort = true;

// SORTCMP's kill switch (only read when kFastSort is true). false = every keyed
// list takes the DRAWSORT keyed skeleton, exactly as before this block.
inline constexpr bool kPlainSort = true;

// The plain path carries the item index in 16 bits of a geometry word; a longer
// list (never seen: the longest is ~4,000) takes the keyed skeleton.
inline constexpr int32_t kPlainMaxCount = 0xFFFF;

// SORTMEMO's kill switch (only read when kFastSort is true). false = no memo:
// every list is sorted, exactly as before this block (the call site vanishes).
inline constexpr bool kSortMemo = true;

// The memo's bounds: lists shorter than kMemoMinCount are not memoised (the
// lookup costs more than their sort); at most kMemoMaxEntries buffers are held;
// every kMemoSweepEvery sorts, an entry unused for kMemoMaxAge sorts is freed
// (a frame sorts 17-41 lists, so ~25-60 frames); when the table is full, entries
// unused for kMemoFullAge sorts go first, and if none is that old the list is
// sorted without being stored.
inline constexpr int32_t  kMemoMinCount   = 32;
inline constexpr size_t   kMemoMaxEntries = 256;
inline constexpr uint64_t kMemoSweepEvery = 1024;
inline constexpr uint64_t kMemoMaxAge     = 1024;
inline constexpr uint64_t kMemoFullAge    = 64;

// Lists shorter than this skip the key build (retail's selection sort handles
// up to 8 directly; below this the build and the move cost more than they save).
inline constexpr int kKeyedMinCount = 32;

using Graphics::RendererDrawItem;

// ---------------------------------------------------------------------------
// Retail's comparator. Every term is one `CMP`/`COMISS` pair read out of
// 0x180062dfe..0x180062e85 (the banner in wave27_lane_gfxbig.cpp); the field
// ORDER is the order of the compares, not of the struct.
// ---------------------------------------------------------------------------
inline bool DrawItemLess(const RendererDrawItem& a, const RendererDrawItem& b) {
    // +0x18, MOVZX / CMP AX,CX - UNSIGNED 16-bit.  JB -> less, JA -> not less.
    if (a._flags != b._flags) return a._flags < b._flags;

    // +0x1c, COMISS both ways - DESCENDING.  Written as two one-sided tests
    // rather than `!=` so that a NaN (for which both JA arms fail) falls
    // through to the next term exactly as retail's does.
    if (a._sortKey > b._sortKey) return true;
    if (b._sortKey > a._sortKey) return false;

    // The remaining six are unsigned integer compares, in this order. The
    // pointer terms are the address order (`std::less`, which is the unsigned
    // compare retail's CMP/JB makes).
    const std::less<> less;
    if (a._material != b._material) return less(a._material, b._material);   // +0x08
    if (a._instance != b._instance) return less(a._instance, b._instance);   // +0x10
    if (a._node  != b._node)  return a._node  < b._node;                      // +0x1a
    if (a._animationInstanceId != b._animationInstanceId) return a._animationInstanceId < b._animationInstanceId;                     // +0x30
    if (a._userData != b._userData) return less(a._userData, b._userData);   // +0x28
    // +0x00, the last term: `CMP RAX,[RBP] / JAE <not less>`.
    return less(a._geometry, b._geometry);
}

// Retail swaps the 0x38-byte element as SEVEN qword moves. The element is
// trivially copyable, so whole-record copies through a temporary move the same
// bytes. Self-swap is a no-op either way (the selection sort can swap best == last).
inline void DrawItemSwap(RendererDrawItem* a, RendererDrawItem* b) {
    const RendererDrawItem t = *a;
    *a = *b;
    *b = t;
}

// ---------------------------------------------------------------------------
// The packed key. Bit string, most significant first:
//   w0 = flags(16) | desc(sortKey)(32) | material[47:32](16)
//   w1 = material[31:0](32) | instance[47:16](32)
//   w2 = instance[15:0](16) | arg5(16) | arg10(32)
//   w3 = userData[47:0](48) | geometry[47:32](16)
//   w4 = geometry[31:0](32) | index(32)          <- only the high 32 bits compare
// ---------------------------------------------------------------------------
struct Key {
    uint64_t w[5];
};
static_assert(sizeof(Key) == 40, "DrawSort::Key is five words");

inline bool KeyLess(const Key& a, const Key& b) {
    if (a.w[0] != b.w[0]) return a.w[0] < b.w[0];
    if (a.w[1] != b.w[1]) return a.w[1] < b.w[1];
    if (a.w[2] != b.w[2]) return a.w[2] < b.w[2];
    if (a.w[3] != b.w[3]) return a.w[3] < b.w[3];
    return (a.w[4] >> 32) < (b.w[4] >> 32);
}
inline bool KeyEqual(const Key& a, const Key& b) {
    return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3] &&
           (a.w[4] >> 32) == (b.w[4] >> 32);
}
inline uint32_t KeyIndex(const Key& k) { return static_cast<uint32_t>(k.w[4]); }

// The float term, order-REVERSING (retail sorts `_sortKey` descending): for
// non-NaN x, y under the current MXCSR, `x > y` <=> SortKeyBits(x) < SortKeyBits(y)
// and `!(x > y) && !(y > x)` <=> equal bits. Undefined for NaN (the caller checks).
uint32_t SortKeyBits(float x);

// Builds the keys of data[0..n). Returns false (and the keys are not to be used)
// when an item has a NaN `_sortKey` or a pointer term at or above 2^48.
// `nanFound` / `ptrFound` say which.
bool BuildKeys(const RendererDrawItem* data, int32_t n, Key* out, bool* nanFound, bool* ptrFound);

// The reference: retail's 0x180062d60 / 0x180063b90 as ported before this block
// (the skeleton with DrawItemLess / DrawItemSwap through function pointers).
void SortReference(RendererDrawItem* data, int32_t count);
void SortReferenceSmall(RendererDrawItem* lo, RendererDrawItem* hi);

// The fast sort: keyed when the list can be keyed and is long enough, else the
// inlined skeleton on the items. With kPlainSort a keyed list takes SortPlain
// (keyed skeleton on its fallback). Afterwards: ConsumerEqual to SortReference's
// array item by item, and byte-equal unless a tie group of agreeing _maxInstances was
// permuted (then only pad / raw sort-key bytes differ, never a read field).
void SortFast(RendererDrawItem* data, int32_t count);

// The DRAWSORT path alone (kPlainSort ignored): keyed skeleton or inlined
// skeleton. Bit for bit SortReference's array (the unit tests' oracle).
void SortFastExact(RendererDrawItem* data, int32_t count);

// SortFast without SORTMEMO's memo (kPlainSort honoured): a fresh sort every
// call. The unit tests' oracle for the memo's hits.
void SortFastNoMemo(RendererDrawItem* data, int32_t count);

// SORTMEMO's table: free every entry; the number of entries and the bytes they
// hold (vector capacities); and a test hook that corrupts the stored output and
// permutation of `buffer`'s entry (swaps the first and last records) so the
// shadow test can check that a bad hit is caught. Returns false without an entry.
void   MemoClear();
size_t MemoEntries();
size_t MemoBytesHeld();
bool   MemoCorruptForTest(const void* buffer);

// SORTCMP's plain sort of a keyed list (keys from BuildKeys, n <= kPlainMaxCount).
// Returns false, with data and keys untouched, when a tie group carries differing
// `_maxInstances` (the caller then runs the keyed skeleton); else permutes data into
// sorted order. `compares` counts every key comparison including hash probes;
// `groups` = the distinct 240-bit prefixes.
bool SortPlain(RendererDrawItem* data, int32_t count, const Key* keys,
               uint64_t* compares, uint32_t* groups);

// Equal in everything the consumer can observe: the seven fields DrawItemList
// reads, `_flags` exactly, and `_sortKey` up to the comparator's equivalence
// (neither greater). The shadow's verdict and the unit tests' oracle.
inline bool ConsumerEqual(const RendererDrawItem& a, const RendererDrawItem& b) {
    return a._geometry == b._geometry && a._material == b._material && a._instance == b._instance &&
           a._flags == b._flags && a._node == b._node && !(a._sortKey > b._sortKey) &&
           !(b._sortKey > a._sortKey) && a._maxInstances == b._maxInstances && a._userData == b._userData &&
           a._animationInstanceId == b._animationInstanceId;
}

// The entry the renderer calls (both callers of 0x180062d60): the kill switch,
// the COMPPROF counters and the COMPPROF=4 shadow.
void Sort(RendererDrawItem* data, int32_t count);

// ---- the stress harness's hooks (set only by src/diag/stress_harness.cpp) ----
extern int g_profOn;                                        // COMPPROF >= 1
extern int g_verify;                                        // COMPPROF == 4
extern void (*g_report)(bool ok, long long detail);         // the shadow's verdict

struct Counters {
    uint64_t sorts;         // lists sorted (count > 1)
    uint64_t items;         // items in them
    uint64_t keyed;         // lists that took the keyed path
    uint64_t smallLists;    // lists under kKeyedMinCount (inlined skeleton on items)
    uint64_t nanLists;      // lists with a NaN sort key (inlined skeleton on items)
    uint64_t ptrLists;      // lists with a pointer >= 2^48 (inlined skeleton on items)
    uint64_t compares;      // comparator calls
    uint64_t ties;          // adjacent pairs in the sorted output that compare equal
    uint64_t tiesArg8;      // of those, pairs whose _maxInstances differs
    uint64_t maxItems;      // the longest list
    // ---- SORTCMP's census ----
    uint64_t plainLists;    // keyed lists that took SortPlain (fallbacks included)
    uint64_t fallbacks;     // of those, lists with a differing-_maxInstances tie: keyed skeleton rerun
    uint64_t groups;        // distinct 240-bit prefixes, summed over plain lists
    uint64_t runs;          // ascending runs of the key array before sorting, keyable lists
    uint64_t repeatChecks;  // keyable lists whose buffer was sorted before (a previous frame)
    uint64_t repeatHits;    // of those, key array hash equal to that previous sort's
    uint64_t maxTie;        // the longest tie group (items) in any sorted output
    uint64_t instItems;     // items with _maxInstances > 1 (each can open an instancing batch)
    uint64_t shadowBytes;   // shadow: lists consumer-equal to the reference but not byte-equal
    // ---- SORTMEMO's census (taken whatever kSortMemo says) ----
    uint64_t keyable;       // lists >= kKeyedMinCount whose keys build (the census's base)
    uint64_t repeatRaw;     // of repeatChecks, all 56 bytes per item hash equal to the previous sort's
    uint64_t repeatMasked;  // of repeatChecks, the bytes outside the pads hash equal
    // ---- SORTMEMO's memo (kSortMemo; lists >= kMemoMinCount) ----
    uint64_t memoHits;      // input byte-equal to the stored one: stored output copied
    uint64_t memoPadHits;   // equal except pad bytes: stored permutation applied to the fresh items
    uint64_t memoMisses;    // an entry was there, the input differed (or count / DAZ): sorted
    uint64_t memoNew;       // no entry for the buffer: sorted
    uint64_t memoStores;    // entries written after a sort (input, output, permutation)
    uint64_t memoFull;      // misses not stored: the table was full of recent entries
    uint64_t memoCompared;  // bytes of fresh input compared (each side), hits and misses
    uint64_t memoStored;    // bytes written into entries (input + output + permutation)
    uint64_t memoEntries;   // gauge at TakeCounters: entries held
    uint64_t memoHeld;      // gauge at TakeCounters: bytes the entries hold
};
extern Counters g_counters;
Counters TakeCounters();     // copies and resets

}  // namespace DrawSort
