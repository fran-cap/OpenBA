// ============================================================================
// src/engine/system/allocator.cpp — System::MemoryAllocator (Wave 2, lane C)
// ============================================================================
//
// Retail: Runtime-x64.dll, image base 0x180000000. The complete reverse
// engineering — tag bit layout, bins, the free-list encoding, the arena
// lifecycle — is in src/engine/system/MemoryAllocator.h. Read that first;
// this file is the transcription, not the explanation.
//
// Address map (all verified against the export table with pefile, not guessed):
//
//   0x1800c6c00  ?Free@MemoryAllocator@System@@QEAAXPEAX@Z
//   0x1800c6e60  ?Allocate@MemoryAllocator@System@@QEAAPEAX_KH@Z
//   0x1800c71d0  ?GetAllocationSize@…@@QEAA_KPEAX@Z
//   0x1800c71f0  ?GetMemoryRange@…@@AEAAPEAUMemoryRange@12@PEAX@Z
//   0x1800c7230  ?GetFreeTagForSize@…@@AEAAPEAUFreeTag@12@_K@Z
//   0x1800c7310  ?LinkFreeTag@…@@AEAAXPEAUMemoryTag@12@@Z
//   0x1800c7410  ?UnlinkFreeTag@…@@AEAAXPEAUFreeTag@12@0@Z
//   0x1800c7580  ?UnlinkFreeTag@…@@AEAAXPEAUMemoryTag@12@@Z
//   0x1800c75b0  ?Shutdown@…@@QEAAXXZ
//   0x1800c7620  ?Startup@…@@QEAA_N_K0@Z
//   0x1800c7690  ?AddNewMemoryRange@…@@AEAA_N_K@Z
//   0x1800c77e0  ?ExpandMemoryRangeTable@…@@AEAA_NXZ
//   0x1800c78d0  ?GetBinForSize@…@@AEBAH_K@Z
//   0x1800c7920  ?GetBinForLargeSize@…@@AEBAH_K@Z
//   0x1800c7960  ??1MemoryAllocator@System@@QEAA@XZ
//   0x1800c79a0  ??0MemoryAllocator@System@@QEAA@XZ
//   0x1800c7a50  ?MemAlloc@…@@SAPEAX_KH@Z
//   0x1800c7ad0  ?MemFree@…@@SAXPEAX@Z
//   0x1800c7b30  ?PlatformConstruct   0x1800c7b50 ?PlatformDestruct
//   0x1800c7b60  ?PlatformAllocate    0x1800c7bb0 ?PlatformFree
//   0x1800c7bd0  ?CommitToPage        0x1800c7c40 ?PlatformStartup
//   0x1800c7c70  ?PlatformShutdown    0x1800c7c80 ?ReleaseToPage
//   0x1800534e0  ?Get@…@@SAPEAV12@XZ
//   0x18007c0b0  ?GetAllocatedSize@…@@QEBA_JXZ
//   0x18007c0c0  ??4MemoryAllocator@System@@QEAAAEAV01@AEBV01@@Z   (memcpy 0x670)
//   0x180034d60  FUN_180034d60 — SetPrevTagInfo, an out-of-line private helper
//   0x180034e20  ?PointerOffset@…@@CA_JPEAX0@Z  (`return a - b`)
//   0x1801fc718  ?_singleton@MemoryAllocator@System@@0PEAV12@EA
//   0x1801ff300  file-static: largest `size` ever passed to Allocate
//
// ---------------------------------------------------------------------------
// ARMING. RtSystemAllocatorArm() (below, called once at boot from
// src/app/runtime_init.cpp) installs the platform seam and hands the host's
// cutover armer its chance to refuse; it prints `[RtAlloc] ARMED` when both are
// in place. Every caller reaches the allocator by name.
//
// ---------------------------------------------------------------------------
// FAITHFULNESS NOTES (things a "cleanup" would break)
// ---------------------------------------------------------------------------
//  * `align8 - 8` is SIGN-extended to 64 bits (CDQE at 0x1800c6ea1). align == 0
//    therefore SUBTRACTS 8 from the request; it does not add 4 GB.
//  * The alignment padding is computed with a 64-bit DIV but a 32-bit IMUL/SUB
//    (0x1800c6f7d..0x1800c6f80). The truncation is reproduced.
//  * Allocate's and Free's range lookup is an inlined copy of GetMemoryRange
//    that returns null on miss and is then dereferenced unguarded. Retail does
//    not check it and neither do we (CLAUDE.md: do not "fix" a faithful crash).
//  * Free decrements _allocCount/_allocatedBytes BEFORE coalescing.
//  * No exceptions anywhere (PLAN.md §4.3 risk 5).
//
// THE HOT PATH (2026-11-18). Allocate, Free, GetFreeTagForSize, Link/UnlinkFreeTag and the
// bin and page arithmetic compute retail's results with cheaper instructions: a
// count-leading-zeros for GetBinForLargeSize' bit-scan loop, a shift / mask for a 64-bit
// DIV whose divisor is a power of two, and an index of non-empty bins (binGroups, retail's
// pad at +0x62c) in place of stepping over empty free-list heads. Every block, tag, list
// link and counter is what the transcription produces, for any call sequence:
// tests/unit/allocator_hot_path_test.cpp keeps the transcription as its oracle
// (tests/unit/allocator_oracle.h) and compares the two after every call.
// ============================================================================

#include <cstdio>
#include <cstring>

#include "system/MemoryAllocator.h"
#include "system/Error.h"   // B3R: FatalOutOfMemory calls Error by name

using rt::system::MemoryAllocator;
using rt::system::MemoryRange;
using rt::system::RtSysMemPlatform;
using rt::system::MemoryTag;
using rt::system::FreeTag;
using rt::system::kTagSizeMask;
using rt::system::kTagPrevSizeMask;
using rt::system::kTagPrevFree;
using rt::system::kTagThisFree;
using rt::system::kTagPrevHiMask;
using rt::system::kSizeBytesMask;
using rt::system::kThisFreeShl2;
using rt::system::kTagPrevSizeShift;
using rt::system::kLinkDeltaHiMask;
using rt::system::kFreeListBinCount;

// ?_singleton@MemoryAllocator@System@@0PEAV12@EA (0x1801fc718): BSS in retail,
// zero until the ctor below runs (B3R W0-SM M1: was the mint's storage cell).
System::MemoryAllocator* System::MemoryAllocator::_singleton = nullptr;

// EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md): null by default; ext.heap_slack
// sets it once at boot through RtSystemAllocatorSetCommitSeam (end of this file).
const rt::system::MemoryAllocatorCommitSeam* System::MemoryAllocator::_extCommitSeam = nullptr;

// The instrument seat (MemoryAllocator.h): STANDALONE DEVIATION, instrument
// seat, retail has no such branch. Constant-initialised null; armed at boot only.
// The AddressSanitizer build (CMakeLists.txt BANISHED_SANITIZE=asan|fuzz) arms the seat from the
// start with the C heap: ASan cannot see inside this allocator's boundary-tag blocks (an overrun
// lands in the neighbour's tag, a freed block is reused at once), and through malloc / free every
// engine allocation gets ASan's redzones, quarantine and leak report. Nothing outside this file
// reads the tags (GetAllocationSize / GetMemoryRange have no other caller), and every engine
// allocation and free goes through MemAlloc / MemFree. Not in any shipped build.
#if BANISHED_ASAN_HEAP
#include <cstdlib>
namespace {
void* AsanMemAlloc(uint64_t size, int align) {
    size_t a = align < 16 ? 16 : static_cast<size_t>(align);
    void* p = nullptr;
    if (posix_memalign(&p, a, size == 0 ? 1 : static_cast<size_t>(size)) != 0)
        std::abort();
    return p;
}
void AsanMemFree(void* p) {
    std::free(p);
}
}  // namespace
System::MemoryAllocatorInstrument System::g_memoryAllocatorInstrument = { AsanMemAlloc, AsanMemFree };
#else
System::MemoryAllocatorInstrument System::g_memoryAllocatorInstrument = { nullptr, nullptr };
#endif

// ---------------------------------------------------------------------------
// Platform seam (see MemoryAllocator.h). Null until the host installs it.
// ---------------------------------------------------------------------------
// Deliberately NOT in an anonymous namespace: allocator_win.cpp's
// PlatformAllocate/PlatformFree reach the same table, because retail's
// Platform* bodies ARE this seam rather than a layer sitting above it.
RtSysMemPlatform g_plat = {};
bool             g_platInstalled = false;

extern "C" void RtSysMemPlatformInstall(const RtSysMemPlatform* p) {
    if (!p) { g_platInstalled = false; return; }
    g_plat = *p;
    g_platInstalled = p->reserve && p->reserveCommit && p->commit && p->decommit &&
                      p->release && p->pageSize && p->lockInit && p->lockEnter &&
                      p->lockLeave && p->lockDelete;
}

// ---------------------------------------------------------------------------
// Tag access helpers. Every offset in this allocator is a BYTE offset; the tag
// size field is in 4-byte units.
// ---------------------------------------------------------------------------
namespace {

inline uint64_t& Tag(uint8_t* t)             { return *reinterpret_cast<uint64_t*>(t); }  // CAST-OK: alloc_header
inline uint64_t  TagC(const uint8_t* t)      { return *reinterpret_cast<const uint64_t*>(t); }  // CAST-OK: alloc_header
inline uint64_t& Node(uint8_t* freeTag)      { return *reinterpret_cast<uint64_t*>(freeTag); }  // CAST-OK: alloc_header

// The file-static at Runtime 0x1801ff300: largest `size` ever requested.
// Nothing in retail reads it, and nothing in the tree does; the store is kept
// because retail makes it.
uint64_t g_largestRequestedSize = 0;

// Retail's fatal path. This comment used to end "after cutover this becomes our
// own Error class" -- Wave 9 lane ENG, iteration 3, is when that happened, and
// the door had to change with it.
//
// `System::Error::SetText` (0x1800c5fa0) and `Display` (0x1800c6060) are
// src/engine/system/error.cpp's, called by name (B3R: they were two
// ResolveExport doors; both rows are registered and ported, so the readiness
// guards and the "nothing registered" fallback print were dead arms).
void FatalOutOfMemory(const char* text, bool stopApplication) {
    System::Error::SetText(text, stopApplication);
    if (stopApplication) System::Error::Display();
}

// ---------------------------------------------------------------------------
// FUN_180034d60 — record "my previous block is free, and here is its size".
// Called on the tag that FOLLOWS a newly created free block.
// ---------------------------------------------------------------------------
void SetPrevTagInfo(uint8_t* next, const uint8_t* prev) {
    Tag(next) |= kTagPrevFree;
    uint64_t v = Tag(next);
    if ((TagC(prev) & kTagSizeMask) < 5) {
        uint64_t enc = static_cast<uint64_t>((next - prev) >> 2) << kTagPrevSizeShift;
        Tag(next) = ((enc ^ v) & kTagPrevSizeMask) ^ v;
    } else {
        *reinterpret_cast<int64_t*>(next - 8) = next - prev;  // CAST-OK: alloc_header
        Tag(next) = v & ~kTagPrevSizeMask;
    }
}

inline MemoryTag* AsTag(uint8_t* p)  { return reinterpret_cast<MemoryTag*>(p); }  // CAST-OK: alloc_header
inline FreeTag*   AsNode(uint8_t* p) { return reinterpret_cast<FreeTag*>(p); }  // CAST-OK: alloc_header

// ---------------------------------------------------------------------------
// THE HOT-PATH HELPERS (NOT RETAIL CODE, RETAIL RESULTS). Each one computes exactly what
// the retail instruction sequence it replaces computes, for every input; the proof is
// tests/unit/allocator_hot_path_test.cpp, which runs the frozen transcription
// (tests/unit/allocator_oracle.h) beside these bodies and compares every address and the
// whole heap after every step.
// ---------------------------------------------------------------------------

// `(x / d) * d` and `x / d` where retail executes a 64-bit DIV. Every divisor the hot path
// meets is a power of two (the page size; align8 for every aligned request), where the
// quotient is a shift and the product a mask, exactly. Any other divisor, 0 included, takes
// the DIV: align == 0 makes retail divide by zero (DIV RCX at 0x1800c6f66 with RCX = 0), and
// so does this.
inline bool IsPowerOfTwo(uint64_t d) {
    return d != 0 && (d & (d - 1)) == 0;
}

inline uint64_t DivideU64(uint64_t x, uint64_t d) {
    if (IsPowerOfTwo(d)) return x >> __builtin_ctzll(d);
    return x / d;
}

inline uint64_t RoundDownU64(uint64_t x, uint64_t d) {
    if (IsPowerOfTwo(d)) return x & ~(d - 1);
    return (x / d) * d;
}

// GetBinForSize (0x1800c78d0) / GetBinForLargeSize (0x1800c7920). The large path's
// bit-scan loop ends at 119 + floor(log2 n) == 0xb6 - clz64(n) for n != 0 and at 118 ==
// 0xb6 - 64 for n == 0 (MemoryAllocator.h, THE BINS).
inline int BinForLargeSize(uint64_t n) {
    const int lz = n == 0 ? 64 : __builtin_clzll(n);
    return 0xb6 - lz;
}

inline int BinForSize(uint64_t n) {
    if (n < 0x201) return static_cast<int>(static_cast<int32_t>(n)) >> 2;
    return BinForLargeSize(n);
}

// ---------------------------------------------------------------------------
// THE BIN GROUPS (NOT RETAIL: MemoryAllocator::binGroups, retail's never-written pad at
// +0x62c). Bit g is set exactly when one of freeLists[8g .. 8g+7] is non-null. Every
// store that can turn a head null or non-null updates it (LinkFreeTag sets the bit,
// UnlinkFreeTag re-tests the group when it nulls a head), and Construct zeroes it with the
// heads. With that invariant, NextNonEmptyBin(from) is the first bin >= from whose head is
// non-null, or kFreeListBinCount when there is none -- the bin retail's linear scans
// (GetFreeTagForSize's `continue` on a null head, UnlinkFreeTag's minBin walk) stop at,
// without reading the empty heads in between. In the work-search pattern (a tree of 0x30
// nodes allocated from the one big block and freed back into it) GetFreeTagForSize used to
// step over ~120 empty heads per allocation.
// ---------------------------------------------------------------------------
constexpr int kBinGroupShift = 3;   // 8 bins per bit, 24 bits for 192 bins

inline void MarkBinNonEmpty(MemoryAllocator* self, int bin) {
    self->binGroups |= 1u << (bin >> kBinGroupShift);
}

// Called after freeLists[bin] was set to null.
inline void NoteBinEmptied(MemoryAllocator* self, int bin) {
    FreeTag* const* group = &self->freeLists[bin & ~((1 << kBinGroupShift) - 1)];
    uintptr_t any = 0;
    for (int i = 0; i < (1 << kBinGroupShift); ++i)
        any |= reinterpret_cast<uintptr_t>(group[i]);  // CAST-OK: ptr_int
    if (any == 0) self->binGroups &= ~(1u << (bin >> kBinGroupShift));
}

inline int NextNonEmptyBin(const MemoryAllocator* self, int from) {
    if (from >= kFreeListBinCount) return kFreeListBinCount;
    if (self->freeLists[from] != nullptr) return from;   // the common case: the exact bin
    const int group = from >> kBinGroupShift;
    uint32_t groups = self->binGroups;
    if ((groups & (1u << group)) != 0) {
        const int groupEnd = (group + 1) << kBinGroupShift;
        for (int bin = from; bin < groupEnd; ++bin) {
            if (self->freeLists[bin] != nullptr) return bin;
        }
    }
    groups &= ~((2u << group) - 1u);   // the groups above `group` (group <= 23)
    if (groups == 0) return kFreeListBinCount;
    int bin = __builtin_ctz(groups) << kBinGroupShift;
    while (self->freeLists[bin] == nullptr) ++bin;   // the group's bit says one head is set
    return bin;
}

// LinkFreeTag (0x1800c7310), the body. Push `tag` onto the head of its bin. Blocks under
// 16 bytes are never linked.
inline void LinkBlock(MemoryAllocator* self, uint8_t* tag) {
    uint64_t bytes = (Tag(tag) & kTagSizeMask) * 4;
    if (bytes < 0x10) return;

    uint8_t* node = tag + 8;
    int bin = BinForSize(bytes);
    FreeTag** head = &self->freeLists[bin];

    if (*head == nullptr) {
        Node(node) = 0;
        Tag(tag) &= ~kTagPrevHiMask;          // clear the prev-link high bits
        if (bin < self->minBin) self->minBin = bin;
        MarkBinNonEmpty(self, bin);           // NOT RETAIL: the bin-group index
    } else {
        uint8_t* oldHead = reinterpret_cast<uint8_t*>(*head);  // CAST-OK: alloc_header
        Node(node) = static_cast<uint64_t>(oldHead - node) & kTagSizeMask;
        Tag(tag) &= ~kTagPrevHiMask;

        uint64_t back = static_cast<uint64_t>(node - oldHead);
        Node(oldHead) &= kTagSizeMask;
        Node(oldHead) |= back << 41;
        Tag(oldHead - 8) &= ~kTagPrevHiMask;
        Tag(oldHead - 8) |= (back & kLinkDeltaHiMask) << 23;
    }
    *head = AsNode(node);
    self->freeTagCount++;
}

// UnlinkFreeTag(FreeTag*, FreeTag*) (0x1800c7410), the body.
inline void UnlinkNode(MemoryAllocator* self, uint8_t* node) {
    const uint64_t tagv  = Tag(node - 8);
    const uint64_t nodev = Node(node);
    int bin = BinForSize((tagv & kTagSizeMask) * 4);

    uint64_t pdelta = (static_cast<uint64_t>(static_cast<int64_t>(tagv) >> 23) &
                       kLinkDeltaHiMask) | (nodev >> 41);
    uint8_t* prev = pdelta ? node + static_cast<int64_t>(pdelta) : nullptr;

    int64_t ndelta = static_cast<int64_t>(nodev << 23) >> 23;
    uint8_t* next = ndelta ? node + ndelta : nullptr;

    if (prev == nullptr) {
        if (next == nullptr) {
            self->freeLists[bin] = nullptr;
            NoteBinEmptied(self, bin);        // NOT RETAIL: the bin-group index
        } else {
            self->freeLists[bin] = AsNode(next);
            Node(next) &= kTagSizeMask;
            Tag(next - 8) &= ~kTagPrevHiMask;
        }
    } else {
        Node(prev) &= ~kTagSizeMask;        // drop prev's next field
        if (next != nullptr) {
            Node(prev) |= static_cast<uint64_t>(next - prev) & kTagSizeMask;
            uint64_t back = static_cast<uint64_t>(prev - next);
            Node(next) &= kTagSizeMask;
            Node(next) |= back << 41;
            Tag(next - 8) &= ~kTagPrevHiMask;
            Tag(next - 8) |= (back & kLinkDeltaHiMask) << 23;
        }
    }

    Node(node) = 0;
    Tag(node - 8) &= ~kTagPrevHiMask;

    // Retail walks minBin up one bin at a time while its head is null, stopping at the
    // first non-null head or at kFreeListBinCount (192, one past the last bin):
    //     while (cursor < 192 && freeLists[minBin] == null) { cursor = minBin + 1; minBin = cursor; }
    // NextNonEmptyBin lands on the same bin.
    if (bin == self->minBin && self->freeLists[bin] == nullptr)
        self->minBin = NextNonEmptyBin(self, bin);
    self->freeTagCount--;
}

}  // namespace

// ===========================================================================
// The members (B3R W0-A0). Each body is the transcription that used to be the
// free function `rtalloc::X(MemoryAllocator* self, …)`; `self` is `this`.
// The parameter types are the ones the DLL's own mangled signatures name:
// PEAUMemoryTag@12@ and PEAUFreeTag@12@. A FreeTag* is the USER pointer of a
// free block (tag + 8); a MemoryTag* is the block header itself.
// ===========================================================================

// --- GetBinForLargeSize (0x1800c7920) / GetBinForSize (0x1800c78d0) ---------
// Retail is the binary-search bit scan
//     shift = 32; bits = 64; do { if (n >> shift) { bits -= shift; n >>= shift; } shift >>= 1; } while (shift);
//     return (int)n - bits + 0xb6;
// computed here with a count-leading-zeros (BinForLargeSize above, MemoryAllocator.h THE BINS).
int MemoryAllocator::GetBinForLargeSize(uint64_t n) const {
    return BinForLargeSize(n);
}

int MemoryAllocator::GetBinForSize(uint64_t n) const {
    return BinForSize(n);
}

// --- GetMemoryRange (0x1800c71f0) ------------------------------------------
MemoryRange* MemoryAllocator::GetMemoryRange(void* p) {
    MemoryRange* r = ranges;
    for (int i = 0; i < rangeCount; ++i, ++r) {
        if (r->base <= static_cast<uint8_t*>(p) && static_cast<uint8_t*>(p) < r->end)  // CAST-OK: alloc_header
            return r;
    }
    return nullptr;
}

// --- GetAllocationSize (0x1800c71d0) ---------------------------------------
uint64_t MemoryAllocator::GetAllocationSize(void* p) {
    return (*reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(p) - 8) & kTagSizeMask) << 2;  // CAST-OK: alloc_header
}

// --- GetAllocatedSize (0x18007c0b0) ----------------------------------------
int64_t MemoryAllocator::GetAllocatedSize() const { return allocatedBytes; }

// --- GetFreeTagForSize (0x1800c7230) ---------------------------------------
// Returns a FreeTag* (== tag + 8), i.e. the USER pointer of a free block whose
// byte size is >= `want`. First fit within a bin, then the next bin up.
// Retail steps `++bin` over every bin and `continue`s on a null head; NextNonEmptyBin
// visits the same non-null heads in the same order without reading the null ones.
FreeTag* MemoryAllocator::GetFreeTagForSize(uint64_t want) {
    int bin = BinForSize(want);
    if (bin < minBin) bin = minBin;

    for (bin = NextNonEmptyBin(this, bin); bin < kFreeListBinCount; bin = NextNonEmptyBin(this, bin + 1)) {
        uint8_t* cur = reinterpret_cast<uint8_t*>(freeLists[bin]);  // CAST-OK: alloc_header
        for (;;) {
            if (((TagC(cur - 8) & kTagSizeMask) << 2) >= want) return AsNode(cur);
            int64_t next = static_cast<int64_t>(Node(cur) << 23) >> 23;
            if (next == 0) break;
            cur = cur + next;
            if (cur == nullptr) break;   // retail tests this; unreachable in practice
        }
    }
    return nullptr;
}

// --- LinkFreeTag (0x1800c7310) ---------------------------------------------
// Push `tag` onto the head of its bin. Blocks under 16 bytes are never linked.
// The body is LinkBlock (above), shared with Allocate and Free so they inline it.
void MemoryAllocator::LinkFreeTag(MemoryTag* tagp) {
    LinkBlock(this, reinterpret_cast<uint8_t*>(tagp));  // CAST-OK: alloc_header
}

// --- UnlinkFreeTag(FreeTag*, FreeTag*) (0x1800c7410) -----------------------
// The second parameter is unused by retail; kept for ABI shape. The body is UnlinkNode
// (above), shared with Allocate and Free so they inline it.
void MemoryAllocator::UnlinkFreeTag(FreeTag* nodep, FreeTag* /*unused*/) {
    UnlinkNode(this, reinterpret_cast<uint8_t*>(nodep));  // CAST-OK: alloc_header
}

// --- UnlinkFreeTag(MemoryTag*) (0x1800c7580) -------------------------------
void MemoryAllocator::UnlinkFreeTag(MemoryTag* tagp) {
    uint8_t* tag = reinterpret_cast<uint8_t*>(tagp);  // CAST-OK: alloc_header
    if (3 < (Tag(tag) & kTagSizeMask)) UnlinkNode(this, tag + 8);
}

// --- CommitToPage (0x1800c7bd0) --------------------------------------------
// Retail: need = ((page - base - 1 + upTo) / page) * page, a 64-bit DIV; RoundDownU64 is
// the same value (a mask for the power-of-two page every platform reports).
void MemoryAllocator::CommitToPage(MemoryRange* range, void* upTo) {
    uint64_t page = pageSize;
    uint64_t have = range->committed;
    uint64_t need = RoundDownU64(page - reinterpret_cast<uint64_t>(range->base) - 1 +  // CAST-OK: alloc_header
                                     reinterpret_cast<uint64_t>(upTo), page);  // CAST-OK: alloc_header
    if (have <= need && need - have != 0) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md): ext.heap_slack decides
        // what reaches the platform; retail's mark is stored either way.
        if (_extCommitSeam != nullptr)
            _extCommitSeam->commit(g_plat, range->base, have, need);
        else
            g_plat.commit(range->base + have, need - have);
        range->committed = need;
    }
}

// --- ReleaseToPage (0x1800c7c80) -------------------------------------------
void MemoryAllocator::ReleaseToPage(MemoryRange* range, void* downTo) {
    uint64_t page = pageSize;
    uint64_t need = RoundDownU64(page - reinterpret_cast<uint64_t>(range->base) - 1 +  // CAST-OK: alloc_header
                                     reinterpret_cast<uint64_t>(downTo), page);  // CAST-OK: alloc_header
    if (need < range->committed) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md): ext.heap_slack may keep
        // the pages committed; retail's mark is lowered either way.
        if (_extCommitSeam != nullptr)
            _extCommitSeam->decommit(g_plat, range->base, need, range->committed);
        else
            g_plat.decommit(range->base + need, range->committed - need);
        range->committed = need;
    }
}

// --- ExpandMemoryRangeTable (0x1800c77e0) ----------------------------------
// Grows the range table by TWO entries at a time, into its own committed
// VirtualAlloc reservation — never through the arena itself.
bool MemoryAllocator::ExpandMemoryRangeTable() {
    uint64_t page   = pageSize;
    int      oldCap = rangeCapacity;
    void*    src    = ranges;

    rangeCapacity = oldCap + 2;
    uint64_t bytes = ((page - 1 + static_cast<uint64_t>(oldCap + 2) * 0x18) / page) * page;
    void* dst = g_plat.reserveCommit(nullptr, bytes);
    ranges = static_cast<MemoryRange*>(dst);  // CAST-OK: alloc_header
    if (dst == nullptr) return false;

    // NOT RETAIL (review F-003, undefined behaviour): the first table grows from capacity 0,
    // whose `ranges` is null, and memcpy from a null pointer is undefined even for 0 bytes.
    // Retail copies unguarded; skipping the empty copy changes no byte.
    if (oldCap != 0)
        std::memcpy(dst, src, static_cast<size_t>(oldCap) * 0x18);
    std::memset(static_cast<uint8_t*>(dst) + static_cast<size_t>(oldCap) * 0x18, 0,  // CAST-OK: alloc_header
                static_cast<size_t>(rangeCapacity - oldCap) * 0x18);
    if (oldCap != 0) g_plat.release(src);
    return true;
}

// --- AddNewMemoryRange (0x1800c7690) ---------------------------------------
bool MemoryAllocator::AddNewMemoryRange(uint64_t want) {
    uint64_t size = minRangeSize;
    if (size < want) size = want;

    if (maxTotalSize != 0 && maxTotalSize < size + totalReserved)
        return false;

    if (rangeCount == rangeCapacity && !ExpandMemoryRangeTable())
        return false;

    MemoryRange* range = ranges + rangeCount;
    uint64_t page = pageSize;
    rangeCount++;

    uint8_t* base = static_cast<uint8_t*>(g_plat.reserve(nullptr, ((page - 1 + size) / page) * page));  // CAST-OK: alloc_header
    range->base = base;
    if (base != nullptr) {
        range->committed = 0;
        range->end = base + size;
        CommitToPage(range, base + 0x10);
        Tag(base) = (((size & kSizeBytesMask) | kThisFreeShl2) >> 2) |
                    (Tag(base) & (kTagPrevHiMask | kTagThisFree));
        Node(base + 8) = 0;
        Tag(base) &= ~kTagPrevHiMask;
        LinkFreeTag(AsTag(base));
        totalReserved += size;
    } else {
        FatalOutOfMemory("Memory: The application failed to allocate a memory range!", false);
    }
    return base != nullptr;
}

// --- Allocate (0x1800c6e60) ------------------------------------------------
void* MemoryAllocator::Allocate(uint64_t size, int align) {
    if (g_largestRequestedSize < size) g_largestRequestedSize = size;

    // NOT RETAIL (review F-102, size wrap): retail computes `size + 7` and `total + 8` below
    // unchecked, so a request near 2^64 (an int -1 or a negative count sign-extended into a
    // size) wraps to a tiny block, or to a zero `want` whose block stays on the free list and
    // is handed out twice. No tag can describe a block past kSizeBytesMask, so such a request
    // ends where retail's own out-of-memory path ends.
    if (size > kSizeBytesMask) {
        FatalOutOfMemory(
            "Failed to allocate memory. Either too many packages (mods) are loaded, "
            "no more memory is available, or an error has occured.", true);
        return nullptr;
    }

    const uint32_t align8 = (static_cast<uint32_t>(align) + 7u) & 0xfffffff8u;

    // CDQE: (align8 - 8) is SIGN-extended before the add. align == 0 subtracts 8.
    uint64_t total = static_cast<uint64_t>(static_cast<int64_t>(
                         static_cast<int32_t>(align8 - 8u))) + ((size + 7) & ~7ull);
    if (total < 8) total = 8;
    uint64_t want = total + 8;

    FreeTag* freeTag = GetFreeTagForSize(want);
    if (freeTag == nullptr) {
        AddNewMemoryRange(want);
        freeTag = GetFreeTagForSize(want);
        if (freeTag == nullptr) {
            FatalOutOfMemory(
                "Failed to allocate memory. Either too many packages (mods) are loaded, "
                "no more memory is available, or an error has occured.", true);
        }
    }
    UnlinkNode(this, reinterpret_cast<uint8_t*>(freeTag));   // UnlinkFreeTag(freeTag, nullptr)  // CAST-OK: alloc_header

    uint8_t* tag = reinterpret_cast<uint8_t*>(freeTag) - 8;  // CAST-OK: alloc_header

    // Inlined GetMemoryRange. Retail does not guard the miss and neither do we.
    MemoryRange* range = nullptr;
    {
        MemoryRange* r = ranges;
        for (int i = 0; i < rangeCount; ++i, ++r) {
            if (r->base <= tag && tag < r->end) { range = r; break; }
        }
    }

    // Alignment padding: 64-bit DIV, 32-bit IMUL/SUB. The truncation is retail's. The
    // divide is DivideU64: a shift for a power-of-two align8, the DIV otherwise (align8 == 0
    // divides by zero, as retail's DIV RCX does).
    uint8_t* user = tag + 8;
    uint64_t quot = DivideU64(static_cast<uint64_t>(static_cast<int64_t>(
                                  static_cast<int32_t>(align8 - 1u))) +
                                  reinterpret_cast<uint64_t>(user),  // CAST-OK: alloc_header
                              static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(align8))));
    int32_t pad = static_cast<int32_t>(static_cast<uint32_t>(quot) * align8) -
                  static_cast<int32_t>(static_cast<uint32_t>(reinterpret_cast<uint64_t>(user)));  // CAST-OK: alloc_header

    if (pad == 0) {
        Tag(tag) &= ~(kTagPrevSizeMask | kTagPrevFree | kTagThisFree);   // clear prevSize, PREV_FREE, THIS_FREE
    } else {
        const uint64_t orig     = Tag(tag);
        const uint64_t origBytes = (orig & kTagSizeMask) * 4;
        const int64_t  padS     = pad;       // MOVSXD

        // The head of the block becomes a `pad`-byte free block …
        Tag(tag) = (((static_cast<uint64_t>(padS) & kSizeBytesMask) | kThisFreeShl2) >> 2) |
                   (orig & (kTagPrevHiMask | kTagThisFree));
        LinkBlock(this, tag);                 // LinkFreeTag

        // … and the aligned remainder becomes the block we hand out.
        uint8_t* newTag = tag + (Tag(tag) & kTagSizeMask) * 4;
        Tag(newTag) &= kTagPrevHiMask;
        Tag(newTag) |= ((origBytes - static_cast<uint64_t>(padS)) >> 2) & kTagSizeMask;
        SetPrevTagInfo(newTag, tag);
        Tag(newTag) |= kTagPrevFree;
        want -= static_cast<uint64_t>(padS);
        tag = newTag;
    }

    const uint64_t tv     = Tag(tag);
    const uint64_t sizeQ  = tv & kTagSizeMask;

    if (sizeQ * 4 - want < 0x10) {
        // Take the whole block: the remainder would be unusable.
        uint8_t* next = tag + sizeQ * 4;
        if (next != range->end && (Tag(next) & kTagPrevFree) != 0)
            Tag(next) &= ~kTagPrevFree;
    } else {
        // Split. The XOR dance is retail's way of writing the size field.
        uint64_t nv = (((want >> 2) ^ tv) & kTagSizeMask) ^ tv;
        Tag(tag) = nv;
        uint64_t nq = nv & kTagSizeMask;
        CommitToPage(range, tag + (nq + 4) * 4);

        uint8_t* rem = tag + nq * 4;
        uint64_t remBytes = (sizeQ * 4 - (Tag(tag) & kTagSizeMask) * 4) & kSizeBytesMask;
        Tag(rem) = ((remBytes | kThisFreeShl2) >> 2) | (Tag(rem) & (kTagPrevHiMask | kTagThisFree));

        uint8_t* next = rem + (Tag(rem) & kTagSizeMask) * 4;
        if (next != range->end) {
            uint64_t nextv = Tag(next);
            if ((nextv & kTagPrevFree) != 0) {
                uint64_t s = nextv | kTagPrevFree;
                Tag(next) = s;
                if ((Tag(rem) & kTagSizeMask) < 5) {
                    uint64_t enc = static_cast<uint64_t>((next - rem) >> 2) << kTagPrevSizeShift;
                    Tag(next) = ((enc ^ s) & kTagPrevSizeMask) ^ s;
                } else {
                    *reinterpret_cast<int64_t*>(next - 8) = next - rem;  // CAST-OK: alloc_header
                    Tag(next) = nextv & ~kTagPrevSizeMask;
                }
            }
        }
        LinkBlock(this, rem);                 // LinkFreeTag
    }

    // Inlined CommitToPage over the block we are handing out (retail's DIV: RoundDownU64).
    {
        uint64_t page = pageSize;
        uint64_t have = range->committed;
        uint64_t need = RoundDownU64((Tag(tag) & kTagSizeMask) * 4 -
                                         reinterpret_cast<uint64_t>(range->base) - 1 + page +  // CAST-OK: alloc_header
                                         reinterpret_cast<uint64_t>(tag), page);  // CAST-OK: alloc_header
        if (have <= need && need - have != 0) {
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md): as in CommitToPage.
            if (_extCommitSeam != nullptr)
                _extCommitSeam->commit(g_plat, range->base, have, need);
            else
                g_plat.commit(range->base + have, need - have);
            range->committed = need;
        }
    }

    allocatedBytes += static_cast<int64_t>((Tag(tag) & kTagSizeMask) * 4);
    allocCount++;
    return tag + 8;
}

// --- Free (0x1800c6c00) ----------------------------------------------------
void MemoryAllocator::Free(void* p) {
    if (p == nullptr) return;

    uint8_t* tag = static_cast<uint8_t*>(p) - 8;  // CAST-OK: alloc_header

    MemoryRange* range = nullptr;
    {
        MemoryRange* r = ranges;
        for (int i = 0; i < rangeCount; ++i, ++r) {
            if (r->base <= tag && tag < r->end) { range = r; break; }
        }
    }

    allocCount--;
    allocatedBytes -= static_cast<int64_t>((Tag(tag) & kTagSizeMask) * 4);

    uint64_t tv  = Tag(tag);
    uint8_t* blk = tag;

    // Backward coalesce.
    if ((tv & kTagPrevFree) != 0) {
        uint64_t back;
        if ((tv & kTagPrevSizeMask) == 0)
            back = *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(p) - 0x10);  // footer  // CAST-OK: alloc_header
        else
            back = static_cast<uint64_t>(static_cast<uint32_t>(tv >> 0x27) & 0x1c);

        blk = tag - back;
        if (3 < (Tag(blk) & kTagSizeMask)) UnlinkNode(this, blk + 8);       // UnlinkFreeTag
        uint64_t pv = Tag(blk);
        Tag(blk) = ((((Tag(tag) + pv) & 0x3fffffffffffffffull) ^ pv) & kTagSizeMask) ^ pv;
    }

    uint64_t bv   = Tag(blk);
    uint8_t* next = blk + (bv & kTagSizeMask) * 4;

    if (next != range->end) {
        uint64_t nv = Tag(next);
        if ((nv & kTagThisFree) == 0) {
            // Neighbour is live: just tell it its predecessor is free now.
            uint64_t s = nv | kTagPrevFree;
            Tag(next) = s;
            if ((Tag(blk) & kTagSizeMask) < 5) {
                uint64_t enc = static_cast<uint64_t>((next - blk) >> 2) << kTagPrevSizeShift;
                Tag(next) = ((enc ^ s) & kTagPrevSizeMask) ^ s;
            } else {
                *reinterpret_cast<int64_t*>(next - 8) = next - blk;  // CAST-OK: alloc_header
                Tag(next) = (nv & ~kTagPrevSizeMask) | kTagPrevFree;
            }
        } else {
            // Forward coalesce, then repeat the bookkeeping on the block after.
            Tag(blk) = ((((nv + bv) & 0x3fffffffffffffffull) ^ bv) & kTagSizeMask) ^ bv;
            if (3 < (Tag(next) & kTagSizeMask)) UnlinkNode(this, next + 8);  // UnlinkFreeTag
            next = next + (Tag(next) & kTagSizeMask) * 4;
            if (next != range->end) {
                uint64_t n2 = Tag(next);
                if ((n2 & kTagPrevFree) != 0) {
                    uint64_t s = n2 | kTagPrevFree;
                    Tag(next) = s;
                    if ((Tag(blk) & kTagSizeMask) < 5) {
                        uint64_t enc = static_cast<uint64_t>((next - blk) >> 2) << kTagPrevSizeShift;
                        Tag(next) = ((enc ^ s) & kTagPrevSizeMask) ^ s;
                    } else {
                        *reinterpret_cast<int64_t*>(next - 8) = next - blk;  // CAST-OK: alloc_header
                        Tag(next) = (n2 & ~kTagPrevSizeMask) | kTagPrevFree;
                    }
                }
            }
        }
    }

    if (blk + (Tag(blk) & kTagSizeMask) * 4 == range->end)
        ReleaseToPage(range, blk + 0x10);

    Tag(blk) = (Tag(blk) & ~(kTagPrevSizeMask | kTagPrevFree)) | kTagThisFree;
    LinkBlock(this, blk);                     // LinkFreeTag
}

// --- Startup (0x1800c7620) -------------------------------------------------
bool MemoryAllocator::Startup(uint64_t minRangeMB, uint64_t maxTotalMB) {
    minRangeSize = minRangeMB << 20;
    maxTotalSize = maxTotalMB << 20;
    pageSize = g_plat.pageSize();
    if (!AddNewMemoryRange(0)) {
        FatalOutOfMemory("Memory: The application failed to start the memory allocator!", false);
        return false;
    }
    return true;
}

// --- Shutdown (0x1800c75b0) ------------------------------------------------
void MemoryAllocator::Shutdown() {
    for (int i = 0; i < rangeCount; ++i) g_plat.release(ranges[i].base);
    g_plat.release(ranges);
}

// --- Get (0x1800534e0) -----------------------------------------------------
MemoryAllocator* MemoryAllocator::Get() {
    return _singleton;
}

// --- MemAlloc (0x1800c7a50) / MemFree (0x1800c7ad0) ------------------------
// Both open with the instrument seat (MemoryAllocator.h): STANDALONE DEVIATION,
// instrument seat, retail has no such branch. Null unless the AddressSanitizer
// build (BANISHED_ASAN_HEAP, above), BANISHED_REDZONE or BANISHED_ALLOC_STATS armed
// it; the armed hook does the whole call.
void* MemoryAllocator::MemAlloc(uint64_t size, int align) {
    if (System::g_memoryAllocatorInstrument.memAlloc)                // STANDALONE DEVIATION
        return System::g_memoryAllocatorInstrument.memAlloc(size, align);
    MemoryAllocator* self = Get();
    g_plat.lockEnter(&self->lock);
    void* r = self->Allocate(size, align);
    g_plat.lockLeave(&self->lock);
    return r;
}

void MemoryAllocator::MemFree(void* p) {
    if (System::g_memoryAllocatorInstrument.memFree) {               // STANDALONE DEVIATION
        System::g_memoryAllocatorInstrument.memFree(p);
        return;
    }
    if (p == nullptr) return;
    MemoryAllocator* self = Get();
    g_plat.lockEnter(&self->lock);
    self->Free(p);
    g_plat.lockLeave(&self->lock);
}

// --- PointerOffset (0x180034e20) -------------------------------------------
int64_t MemoryAllocator::PointerOffset(void* a, void* b) {
    return static_cast<uint8_t*>(a) - static_cast<uint8_t*>(b);  // CAST-OK: ptr_int
}

// --- The Platform* family (0x1800c7b30 / b50 / b60 / bb0 / c40 / c70) ------
// These six are defined in each platform twin: src/platform/win/allocator_win.cpp,
// src/platform/posix/allocator_posix.cpp and src/platform/darwin/allocator_darwin.cpp.
// They are the platform seam the ORIGINAL authors drew, and PLAN.md 4.1's `_win` rule
// falls on the same line, so the OS headers go there and nowhere else.

// ===========================================================================
// rtalloc:: — what is left of the free-function door.
// ===========================================================================
namespace rtalloc {

// --- ctor (0x1800c79a0) / dtor (0x1800c7960) -------------------------------
// NOT members: the class declares no ctor/dtor (see MemoryAllocator.h, "NOT
// DECLARED, ON PURPOSE"). Called from src/engine/engine/singleton_ctors.cpp
// (Ctor_/Dtor_MemoryAllocator, the ??0 / ??1 rows). Both write the class static
// System::MemoryAllocator::_singleton (0x1801fc718).
void Construct(MemoryAllocator* self) {
    self->ranges = nullptr;
    self->rangeCount = 0;
    self->rangeCapacity = 0;
    self->minRangeSize = 0;
    self->maxTotalSize = 0;
    self->totalReserved = 0;
    self->minBin = kFreeListBinCount - 1;
    self->allocatedBytes = 0;
    self->allocCount = 0;
    self->freeTagCount = 0;
    g_plat.lockInit(&self->lock);
    std::memset(self->freeLists, 0, sizeof(self->freeLists));
    self->binGroups = 0;                      // NOT RETAIL: the bin-group index, empty with the heads
    MemoryAllocator::_singleton = self;
    self->pageSize = 0;                       // inlined PlatformConstruct
}

void Destruct(MemoryAllocator* self) {
    MemoryAllocator::_singleton = nullptr;
    g_plat.lockDelete(&self->lock);
}

// --- The `self`-explicit pair ----------------------------------------------
// Nothing calls these two `self`-explicit bodies (no header declares them).
// B3R RT-Core: the other fifteen registration thunks went with their
// rows, which had no reader.
void*    Allocate(MemoryAllocator* self, uint64_t size, int align) { return self->Allocate(size, align); }
void     Free(MemoryAllocator* self, void* p)                      { self->Free(p); }

}  // namespace rtalloc

// ============================================================================
// ARMING -- RtSystemAllocatorArm(), called once at boot (src/app/runtime_init.cpp)
// ============================================================================

namespace {

// The host's cutover armer (src/diag/alloc_cutover_arm.cpp), handed in by
// RtSystemAllocatorSetCutoverArmer() before RtSystemAllocatorArm() runs.
RtAllocCutoverArmer    g_armCutover     = nullptr;

// Which member of the platform seam is still null. Returns nullptr when the
// table is complete. Naming it matters: `g_platInstalled` is one bool over ten
// function pointers, and lane X spent a probe on a half-built seam because the
// refusal said only "not installed".
const char* PlatformSeamMissing() {
    if (!g_plat.reserve)       return "reserve (VirtualAlloc MEM_RESERVE)";
    if (!g_plat.reserveCommit) return "reserveCommit (VirtualAlloc RESERVE|COMMIT)";
    if (!g_plat.commit)        return "commit (VirtualAlloc MEM_COMMIT)";
    if (!g_plat.decommit)      return "decommit (VirtualFree MEM_DECOMMIT)";
    if (!g_plat.release)       return "release (VirtualFree MEM_RELEASE)";
    if (!g_plat.pageSize)      return "pageSize (GetSystemInfo)";
    if (!g_plat.lockInit)      return "lockInit (InitializeCriticalSectionAndSpinCount)";
    if (!g_plat.lockEnter)     return "lockEnter (EnterCriticalSection)";
    if (!g_plat.lockLeave)     return "lockLeave (LeaveCriticalSection)";
    if (!g_plat.lockDelete)    return "lockDelete (DeleteCriticalSection)";
    return nullptr;
}

}

extern "C" void RtSystemAllocatorSetCutoverArmer(RtAllocCutoverArmer fn) {
    g_armCutover = fn;
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md). The store is made under the live
// allocator's lock: a commit or decommit already inside Allocate / Free finishes under the policy
// it started with, and every later one sees the new seam.
void RtSystemAllocatorSetCommitSeam(const rt::system::MemoryAllocatorCommitSeam* seam) {
    MemoryAllocator* self = MemoryAllocator::Get();
    if (self == nullptr || !g_platInstalled) {
        MemoryAllocator::_extCommitSeam = seam;
        return;
    }
    g_plat.lockEnter(&self->lock);
    MemoryAllocator::_extCommitSeam = seam;
    g_plat.lockLeave(&self->lock);
}

// WAVE 22 LANE ALLOC -- SELF-ATTESTATION, and why it is fprintf and not printf.
//
// Every [RtAlloc] line in this file that reports the outcome of arming is a
// `printf`, and the PERF build compiles printf to `((void)0)`
// (src/app/log_strip.h). So the one binary an A/B is measured on is
// precisely the binary that cannot say whether it armed. A perf run with
// whose allocator silently REFUSED (missing platform seam, missing
// cutover armer) is indistinguishable from an armed
// one, and the A/B would then be comparing a build against itself and would
// report `no regression` -- the measurement reading GREEN because the subject
// never ran. That is this project's own recurring defect shape.
//
// stderr survives the strip (see the note in src/app/log_strip.h) and lands
// in standalone_error.log, which tools/stress_run.sh already collects per run.
// One line, once, at init, outside every measurement window.
static void AttestArming(const char* verdict) {
    fprintf(stderr, "[RtAlloc] ATTEST: %s\n", verdict);
    std::fflush(stderr);
}

void RtSystemAllocatorArm() {
    // Every platform installs its seam here: allocator_win.cpp (Win32), allocator_posix.cpp or
    // allocator_darwin.cpp (mmap / pthread). The Wave 2 `#if defined(_WIN32)` around this call
    // predates any POSIX body; off Windows it left the seam empty, the arm refused,
    // MemoryAllocator::_singleton stayed null and the first allocation called through it
    // (Phase D.4 LINUX-1's first boot).
    if (!g_platInstalled) RtSystemAllocatorInstallPlatform();
    if (!g_platInstalled) {
        const char* missing = PlatformSeamMissing();
        printf("[RtPort] MemoryAllocator: REFUSING to arm -- platform seam incomplete, "
               "first missing entry is '%s' (src/platform/win/allocator_win.cpp did "
               "not install it). A half-built seam is worse than none: rtalloc::Construct "
               "would call a null lockInit. See docs/history/runtime_port/WAVE2_ALLOCATOR.md\n",
               missing ? missing : "(table complete but g_platInstalled is false)");
        AttestArming("REFUSED: platform seam incomplete");
        return;
    }

    // There is no DLL, so no detour is possible OR necessary; the cutover armer
    // is the host's hook (src/diag/alloc_cutover_arm.cpp).
    if (g_armCutover == nullptr) {
        printf("[RtPort] MemoryAllocator: REFUSING to arm -- Runtime-x64.dll is absent "
               "and no cutover armer is installed. "
               "src/app/main.cpp must call RtSystemAllocatorSetCutoverArmer().\n");
        AttestArming("REFUSED: cutover, no armer installed");
        return;
    }
    if (!g_armCutover()) {
        printf("[RtPort] MemoryAllocator: cutover armer REFUSED\n");
        AttestArming("REFUSED: cutover armer said no");
        return;
    }
    printf("[RtAlloc] ARMED (cutover: Runtime-x64.dll absent) -- no detour installed "
           "because there are no DLL-internal callers to redirect.\n");
    AttestArming("ARMED (cutover, DLL absent)");
}

// 0x180009c30 -- the allocator-lock Free wrapper, and a TRAP worth spelling out
//
// Retail's body is `if (p) { Enter; Free(_singleton, p); Leave; }` -- byte for
// byte the `?MemFree@` static -- but its SIGNATURE is not the
// same. It takes TWO parameters and ignores the first: retail's
// RemoveGlobalObject tail-calls it passing `this` in RCX and the pointer in RDX
// (the shim below, `rtalloc::FreeLockedIgnoringSelf(void*, void*)`).
//
// So substituting `MemoryAllocator::MemFree` for it -- which is what "it is
// exactly the MemFree static" invites -- would be silently, catastrophically wrong. Our
// MemFree reads its one argument from RCX, which at this call site holds the
// IGNORED `this`. It would free an object pointer instead of the buffer, leave
// the buffer leaked, and corrupt the free list with a block the allocator never
// handed out. Nothing would fault at the call; the damage would surface
// arbitrarily later in an unrelated allocation.
//
// The shim costs one instruction of shuffle and makes the ABI difference
// explicit instead of load-bearing-and-invisible.
// WAVE 10 LANE SHADOWBISECT — this body used to call `rtalloc::MemFree(p)`
// DIRECTLY, and that one C++ call is what made the allocator shadow read red in
// all seven of lane SHADOWRE's cells.
//
// `rtalloc::MemFree` is `Get(); lockEnter; rtalloc::Free(self, p); lockLeave` —
// OUR ported free logic, run against whatever `_singleton` names. With the DLL
// present and the allocator port unarmed, `_singleton` is RETAIL's arena. So a
// registration that is unconditional and default-on was running the ported Free
// on the shipped allocator's heap, never entering the DLL's `Free`
// (0x1800c6c00) — which is where standalone/allocator_lock.cpp's patch, and
// therefore every observer that rides on it, lives.
//
// Two consequences, and the second is the serious one:
//   * MEASUREMENT: the shadow never saw those frees, so its replica kept blocks
//     retail had released — `ours live` higher, `freeTagCount` lower, `bytes`
//     higher by a multiple of 32 (the 0x18-payload LinkedList node), and
//     `free_unmapped` clean because the Free never reached the hook at all.
//     That is SHADOWRE's signature, exactly, and the divergence count is
//     precisely the number of comparisons after the first such free.
//   * SUBJECT: "the allocator port is off" was not true on this path. An
//     unarmed run was already mutating retail's arena with our code, by a route
//     no kill switch could reach and no counter reported.
//
// B3R W0-A0: the static, called directly; the instrument seat lives inside
// MemoryAllocator::MemFree, so a free through this shim reaches it.
// B3R RT-Core: external linkage, declared in src/engine/system/
// MemoryAllocator.h, so its readers call it by name.
namespace rtalloc {
void FreeLockedIgnoringSelf(void* /*ignoredThis*/, void* p) {
    MemoryAllocator::MemFree(p);
}
}  // namespace rtalloc

// RegisterAllocatorAddresses() is gone. B3R W2-RT: 0x1800c7a50 / 0x1800c7ad0
// (the two statics) had no reader left -- every reader calls
// MemoryAllocator::MemAlloc / MemFree by name -- and went. B3R W2-EZ: its last
// row, 0x1800c6e60, went with its one reader (engine_funcs.cpp's fn_MemAlloc).
// 0x180009c30 is not registered at all any more (B3R RT-Core): every reader
// calls rtalloc::FreeLockedIgnoringSelf by name.
