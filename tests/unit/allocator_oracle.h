#pragma once
// ============================================================================
// tests/unit/allocator_oracle.h -- the REFERENCE allocator for allocator_hot_path_test.cpp
// ============================================================================
// A frozen copy of the literal transcription of retail's System::MemoryAllocator hot path
// (Runtime-x64.dll 0x1800c6c00 Free, 0x1800c6e60 Allocate, 0x1800c7230 GetFreeTagForSize,
// 0x1800c7310 LinkFreeTag, 0x1800c7410 / 0x1800c7580 UnlinkFreeTag, 0x1800c78d0 /
// 0x1800c7920 GetBin*, 0x1800c7bd0 CommitToPage, 0x1800c7c80 ReleaseToPage, 0x1800c7690
// AddNewMemoryRange, 0x1800c77e0 ExpandMemoryRangeTable), as src/engine/system/allocator.cpp
// held it at 0809be4c4 before the hot path was optimised. It is the oracle the optimised
// bodies are compared against, step by step: same addresses, same tags, same lists, same
// counters. Do not "fix" or speed it up; it is the definition of correct.
//
// Differences from that file, none of which reaches the heap state:
//   * free functions over a System::MemoryAllocator& (the struct's fields are public), so
//     both allocators share one layout and the comparison is field by field;
//   * the platform seam is a table handed in (the test's fake), not the global g_plat;
//   * the ext.heap_slack commit seam is absent (the comparison runs with it null);
//   * the fatal out-of-memory path aborts (no test trace reaches it);
//   * the file-static "largest requested size" is a member of the oracle's context.
// The oracle never touches binGroups (+0x62c, retail's pad): only the port keeps it.
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "system/MemoryAllocator.h"

namespace alloc_oracle {

using rt::system::FreeTag;
using rt::system::MemoryRange;
using rt::system::MemoryTag;
using rt::system::RtSysMemPlatform;
using rt::system::kFreeListBinCount;
using rt::system::kLinkDeltaHiMask;
using rt::system::kSizeBytesMask;
using rt::system::kTagPrevFree;
using rt::system::kTagPrevHiMask;
using rt::system::kTagPrevSizeMask;
using rt::system::kTagPrevSizeShift;
using rt::system::kTagSizeMask;
using rt::system::kTagThisFree;
using rt::system::kThisFreeShl2;

struct Oracle {
    System::MemoryAllocator* self;
    const RtSysMemPlatform*  plat;
    uint64_t                 largestRequestedSize;
};

inline uint64_t& Tag(uint8_t* t) { return *reinterpret_cast<uint64_t*>(t); }  // CAST-OK: alloc_header
inline uint64_t TagC(const uint8_t* t) { return *reinterpret_cast<const uint64_t*>(t); }  // CAST-OK: alloc_header
inline uint64_t& Node(uint8_t* freeTag) { return *reinterpret_cast<uint64_t*>(freeTag); }  // CAST-OK: alloc_header
inline MemoryTag* AsTag(uint8_t* p) { return reinterpret_cast<MemoryTag*>(p); }  // CAST-OK: alloc_header
inline FreeTag* AsNode(uint8_t* p) { return reinterpret_cast<FreeTag*>(p); }  // CAST-OK: alloc_header

[[noreturn]] inline void FatalOutOfMemory() {
    std::abort();
}

inline void SetPrevTagInfo(uint8_t* next, const uint8_t* prev) {
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

inline int GetBinForLargeSize(uint64_t n) {
    int shift = 0x20;
    int bits  = 0x40;
    do {
        uint64_t v = n >> shift;
        if (v != 0) {
            bits -= shift;
            n = v;
        }
        shift >>= 1;
    } while (shift != 0);
    return static_cast<int>(n) - bits + 0xb6;
}

inline int GetBinForSize(uint64_t n) {
    if (n < 0x201) return static_cast<int>(static_cast<int32_t>(n)) >> 2;
    return GetBinForLargeSize(n);
}

inline FreeTag* GetFreeTagForSize(Oracle& o, uint64_t want) {
    System::MemoryAllocator* s = o.self;
    int bin = GetBinForSize(want);
    if (bin < s->minBin) bin = s->minBin;

    for (; bin < kFreeListBinCount; ++bin) {
        uint8_t* cur = reinterpret_cast<uint8_t*>(s->freeLists[bin]);  // CAST-OK: alloc_header
        if (cur == nullptr) continue;
        for (;;) {
            if (((TagC(cur - 8) & kTagSizeMask) << 2) >= want) return AsNode(cur);
            int64_t next = static_cast<int64_t>(Node(cur) << 23) >> 23;
            if (next == 0) break;
            cur = cur + next;
            if (cur == nullptr) break;
        }
    }
    return nullptr;
}

inline void LinkFreeTag(Oracle& o, MemoryTag* tagp) {
    System::MemoryAllocator* s = o.self;
    uint8_t* tag = reinterpret_cast<uint8_t*>(tagp);  // CAST-OK: alloc_header
    uint64_t bytes = (Tag(tag) & kTagSizeMask) * 4;
    if (bytes < 0x10) return;

    uint8_t* node = tag + 8;
    int bin = GetBinForSize(bytes);
    FreeTag** head = &s->freeLists[bin];

    if (*head == nullptr) {
        Node(node) = 0;
        Tag(tag) &= ~kTagPrevHiMask;
        if (bin < s->minBin) s->minBin = bin;
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
    s->freeTagCount++;
}

inline void UnlinkFreeTag(Oracle& o, FreeTag* nodep) {
    System::MemoryAllocator* s = o.self;
    uint8_t* node = reinterpret_cast<uint8_t*>(nodep);  // CAST-OK: alloc_header
    uint64_t bytes = (TagC(node - 8) & kTagSizeMask) * 4;
    int bin = GetBinForSize(bytes);

    const uint64_t tagv  = Tag(node - 8);
    const uint64_t nodev = Node(node);

    uint64_t pdelta = (static_cast<uint64_t>(static_cast<int64_t>(tagv) >> 23) &
                       kLinkDeltaHiMask) | (nodev >> 41);
    uint8_t* prev = pdelta ? node + static_cast<int64_t>(pdelta) : nullptr;

    int64_t ndelta = static_cast<int64_t>(nodev << 23) >> 23;
    uint8_t* next = ndelta ? node + ndelta : nullptr;

    if (prev == nullptr) {
        if (next == nullptr) {
            s->freeLists[bin] = nullptr;
        } else {
            s->freeLists[bin] = AsNode(next);
            Node(next) &= kTagSizeMask;
            Tag(next - 8) &= ~kTagPrevHiMask;
        }
    } else {
        Node(prev) &= ~kTagSizeMask;
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

    int cursor = s->minBin;
    if (bin == cursor && s->freeLists[bin] == nullptr) {
        while (cursor < kFreeListBinCount && s->freeLists[s->minBin] == nullptr) {
            cursor = s->minBin + 1;
            s->minBin = cursor;
        }
    }
    s->freeTagCount--;
}

inline void UnlinkFreeTag(Oracle& o, MemoryTag* tagp) {
    uint8_t* tag = reinterpret_cast<uint8_t*>(tagp);  // CAST-OK: alloc_header
    if (3 < (Tag(tag) & kTagSizeMask)) UnlinkFreeTag(o, AsNode(tag + 8));
}

inline void CommitToPage(Oracle& o, MemoryRange* range, void* upTo) {
    uint64_t page = o.self->pageSize;
    uint64_t have = range->committed;
    uint64_t need = ((page - reinterpret_cast<uint64_t>(range->base) - 1 +  // CAST-OK: alloc_header
                      reinterpret_cast<uint64_t>(upTo)) / page) * page;  // CAST-OK: alloc_header
    if (have <= need && need - have != 0) {
        o.plat->commit(range->base + have, need - have);
        range->committed = need;
    }
}

inline void ReleaseToPage(Oracle& o, MemoryRange* range, void* downTo) {
    uint64_t page = o.self->pageSize;
    uint64_t need = ((page - reinterpret_cast<uint64_t>(range->base) - 1 +  // CAST-OK: alloc_header
                      reinterpret_cast<uint64_t>(downTo)) / page) * page;  // CAST-OK: alloc_header
    if (need < range->committed) {
        o.plat->decommit(range->base + need, range->committed - need);
        range->committed = need;
    }
}

inline bool ExpandMemoryRangeTable(Oracle& o) {
    System::MemoryAllocator* s = o.self;
    uint64_t page   = s->pageSize;
    int      oldCap = s->rangeCapacity;
    void*    src    = s->ranges;

    s->rangeCapacity = oldCap + 2;
    uint64_t bytes = ((page - 1 + static_cast<uint64_t>(oldCap + 2) * 0x18) / page) * page;
    void* dst = o.plat->reserveCommit(nullptr, bytes);
    s->ranges = static_cast<MemoryRange*>(dst);  // CAST-OK: alloc_header
    if (dst == nullptr) return false;

    if (oldCap != 0)
        std::memcpy(dst, src, static_cast<size_t>(oldCap) * 0x18);
    std::memset(static_cast<uint8_t*>(dst) + static_cast<size_t>(oldCap) * 0x18, 0,  // CAST-OK: alloc_header
                static_cast<size_t>(s->rangeCapacity - oldCap) * 0x18);
    if (oldCap != 0) o.plat->release(src);
    return true;
}

inline bool AddNewMemoryRange(Oracle& o, uint64_t want) {
    System::MemoryAllocator* s = o.self;
    uint64_t size = s->minRangeSize;
    if (size < want) size = want;

    if (s->maxTotalSize != 0 && s->maxTotalSize < size + s->totalReserved)
        return false;

    if (s->rangeCount == s->rangeCapacity && !ExpandMemoryRangeTable(o))
        return false;

    MemoryRange* range = s->ranges + s->rangeCount;
    uint64_t page = s->pageSize;
    s->rangeCount++;

    uint8_t* base = static_cast<uint8_t*>(o.plat->reserve(nullptr, ((page - 1 + size) / page) * page));  // CAST-OK: alloc_header
    range->base = base;
    if (base != nullptr) {
        range->committed = 0;
        range->end = base + size;
        CommitToPage(o, range, base + 0x10);
        Tag(base) = (((size & kSizeBytesMask) | kThisFreeShl2) >> 2) |
                    (Tag(base) & (kTagPrevHiMask | kTagThisFree));
        Node(base + 8) = 0;
        Tag(base) &= ~kTagPrevHiMask;
        LinkFreeTag(o, AsTag(base));
        s->totalReserved += size;
    } else {
        FatalOutOfMemory();
    }
    return base != nullptr;
}

inline bool Startup(Oracle& o, uint64_t minRangeMB, uint64_t maxTotalMB) {
    System::MemoryAllocator* s = o.self;
    s->minRangeSize = minRangeMB << 20;
    s->maxTotalSize = maxTotalMB << 20;
    s->pageSize = o.plat->pageSize();
    return AddNewMemoryRange(o, 0);
}

inline void* Allocate(Oracle& o, uint64_t size, int align) {
    System::MemoryAllocator* s = o.self;
    if (o.largestRequestedSize < size) o.largestRequestedSize = size;

    if (size > kSizeBytesMask) FatalOutOfMemory();

    const uint32_t align8 = (static_cast<uint32_t>(align) + 7u) & 0xfffffff8u;

    uint64_t total = static_cast<uint64_t>(static_cast<int64_t>(
                         static_cast<int32_t>(align8 - 8u))) + ((size + 7) & ~7ull);
    if (total < 8) total = 8;
    uint64_t want = total + 8;

    FreeTag* freeTag = GetFreeTagForSize(o, want);
    if (freeTag == nullptr) {
        AddNewMemoryRange(o, want);
        freeTag = GetFreeTagForSize(o, want);
        if (freeTag == nullptr) FatalOutOfMemory();
    }
    UnlinkFreeTag(o, freeTag);

    uint8_t* tag = reinterpret_cast<uint8_t*>(freeTag) - 8;  // CAST-OK: alloc_header

    MemoryRange* range = nullptr;
    {
        MemoryRange* r = s->ranges;
        for (int i = 0; i < s->rangeCount; ++i, ++r) {
            if (r->base <= tag && tag < r->end) {
                range = r;
                break;
            }
        }
    }

    uint8_t* user = tag + 8;
    uint64_t quot = (static_cast<uint64_t>(static_cast<int64_t>(
                         static_cast<int32_t>(align8 - 1u))) +
                     reinterpret_cast<uint64_t>(user)) /  // CAST-OK: alloc_header
                    static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(align8)));
    int32_t pad = static_cast<int32_t>(static_cast<uint32_t>(quot) * align8) -
                  static_cast<int32_t>(static_cast<uint32_t>(reinterpret_cast<uint64_t>(user)));  // CAST-OK: alloc_header

    if (pad == 0) {
        Tag(tag) &= ~(kTagPrevSizeMask | kTagPrevFree | kTagThisFree);
    } else {
        const uint64_t orig      = Tag(tag);
        const uint64_t origBytes = (orig & kTagSizeMask) * 4;
        const int64_t  padS      = pad;

        Tag(tag) = (((static_cast<uint64_t>(padS) & kSizeBytesMask) | kThisFreeShl2) >> 2) |
                   (orig & (kTagPrevHiMask | kTagThisFree));
        LinkFreeTag(o, AsTag(tag));

        uint8_t* newTag = tag + (Tag(tag) & kTagSizeMask) * 4;
        Tag(newTag) &= kTagPrevHiMask;
        Tag(newTag) |= ((origBytes - static_cast<uint64_t>(padS)) >> 2) & kTagSizeMask;
        SetPrevTagInfo(newTag, tag);
        Tag(newTag) |= kTagPrevFree;
        want -= static_cast<uint64_t>(padS);
        tag = newTag;
    }

    const uint64_t tv    = Tag(tag);
    const uint64_t sizeQ = tv & kTagSizeMask;

    if (sizeQ * 4 - want < 0x10) {
        uint8_t* next = tag + sizeQ * 4;
        if (next != range->end && (Tag(next) & kTagPrevFree) != 0)
            Tag(next) &= ~kTagPrevFree;
    } else {
        uint64_t nv = (((want >> 2) ^ tv) & kTagSizeMask) ^ tv;
        Tag(tag) = nv;
        uint64_t nq = nv & kTagSizeMask;
        CommitToPage(o, range, tag + (nq + 4) * 4);

        uint8_t* rem = tag + nq * 4;
        uint64_t remBytes = (sizeQ * 4 - (Tag(tag) & kTagSizeMask) * 4) & kSizeBytesMask;
        Tag(rem) = ((remBytes | kThisFreeShl2) >> 2) | (Tag(rem) & (kTagPrevHiMask | kTagThisFree));

        uint8_t* next = rem + (Tag(rem) & kTagSizeMask) * 4;
        if (next != range->end) {
            uint64_t nextv = Tag(next);
            if ((nextv & kTagPrevFree) != 0) {
                uint64_t sv = nextv | kTagPrevFree;
                Tag(next) = sv;
                if ((Tag(rem) & kTagSizeMask) < 5) {
                    uint64_t enc = static_cast<uint64_t>((next - rem) >> 2) << kTagPrevSizeShift;
                    Tag(next) = ((enc ^ sv) & kTagPrevSizeMask) ^ sv;
                } else {
                    *reinterpret_cast<int64_t*>(next - 8) = next - rem;  // CAST-OK: alloc_header
                    Tag(next) = nextv & ~kTagPrevSizeMask;
                }
            }
        }
        LinkFreeTag(o, AsTag(rem));
    }

    {
        uint64_t page = s->pageSize;
        uint64_t have = range->committed;
        uint64_t need = (((Tag(tag) & kTagSizeMask) * 4 -
                          reinterpret_cast<uint64_t>(range->base) - 1 + page +  // CAST-OK: alloc_header
                          reinterpret_cast<uint64_t>(tag)) / page) * page;  // CAST-OK: alloc_header
        if (have <= need && need - have != 0) {
            o.plat->commit(range->base + have, need - have);
            range->committed = need;
        }
    }

    s->allocatedBytes += static_cast<int64_t>((Tag(tag) & kTagSizeMask) * 4);
    s->allocCount++;
    return tag + 8;
}

inline void Free(Oracle& o, void* p) {
    System::MemoryAllocator* s = o.self;
    if (p == nullptr) return;

    uint8_t* tag = static_cast<uint8_t*>(p) - 8;  // CAST-OK: alloc_header

    MemoryRange* range = nullptr;
    {
        MemoryRange* r = s->ranges;
        for (int i = 0; i < s->rangeCount; ++i, ++r) {
            if (r->base <= tag && tag < r->end) {
                range = r;
                break;
            }
        }
    }

    s->allocCount--;
    s->allocatedBytes -= static_cast<int64_t>((Tag(tag) & kTagSizeMask) * 4);

    uint64_t tv  = Tag(tag);
    uint8_t* blk = tag;

    if ((tv & kTagPrevFree) != 0) {
        uint64_t back;
        if ((tv & kTagPrevSizeMask) == 0)
            back = *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(p) - 0x10);  // CAST-OK: alloc_header
        else
            back = static_cast<uint64_t>(static_cast<uint32_t>(tv >> 0x27) & 0x1c);

        blk = tag - back;
        if (3 < (Tag(blk) & kTagSizeMask)) UnlinkFreeTag(o, AsNode(blk + 8));
        uint64_t pv = Tag(blk);
        Tag(blk) = ((((Tag(tag) + pv) & 0x3fffffffffffffffull) ^ pv) & kTagSizeMask) ^ pv;
    }

    uint64_t bv   = Tag(blk);
    uint8_t* next = blk + (bv & kTagSizeMask) * 4;

    if (next != range->end) {
        uint64_t nv = Tag(next);
        if ((nv & kTagThisFree) == 0) {
            uint64_t sv = nv | kTagPrevFree;
            Tag(next) = sv;
            if ((Tag(blk) & kTagSizeMask) < 5) {
                uint64_t enc = static_cast<uint64_t>((next - blk) >> 2) << kTagPrevSizeShift;
                Tag(next) = ((enc ^ sv) & kTagPrevSizeMask) ^ sv;
            } else {
                *reinterpret_cast<int64_t*>(next - 8) = next - blk;  // CAST-OK: alloc_header
                Tag(next) = (nv & ~kTagPrevSizeMask) | kTagPrevFree;
            }
        } else {
            Tag(blk) = ((((nv + bv) & 0x3fffffffffffffffull) ^ bv) & kTagSizeMask) ^ bv;
            if (3 < (Tag(next) & kTagSizeMask)) UnlinkFreeTag(o, AsNode(next + 8));
            next = next + (Tag(next) & kTagSizeMask) * 4;
            if (next != range->end) {
                uint64_t n2 = Tag(next);
                if ((n2 & kTagPrevFree) != 0) {
                    uint64_t sv = n2 | kTagPrevFree;
                    Tag(next) = sv;
                    if ((Tag(blk) & kTagSizeMask) < 5) {
                        uint64_t enc = static_cast<uint64_t>((next - blk) >> 2) << kTagPrevSizeShift;
                        Tag(next) = ((enc ^ sv) & kTagPrevSizeMask) ^ sv;
                    } else {
                        *reinterpret_cast<int64_t*>(next - 8) = next - blk;  // CAST-OK: alloc_header
                        Tag(next) = (n2 & ~kTagPrevSizeMask) | kTagPrevFree;
                    }
                }
            }
        }
    }

    if (blk + (Tag(blk) & kTagSizeMask) * 4 == range->end)
        ReleaseToPage(o, range, blk + 0x10);

    Tag(blk) = (Tag(blk) & ~(kTagPrevSizeMask | kTagPrevFree)) | kTagThisFree;
    LinkFreeTag(o, AsTag(blk));
}

}  // namespace alloc_oracle
