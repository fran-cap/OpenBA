// src/game/Action/region_paircache.cpp
// Pathfinder region pair-cache: ring-buffer initializer + object builder.
//
// Faithful 1:1 port of FUN_14001cca0 @ 0x14001cca0 (the region-pair ring-buffer
// resize/init) plus a builder that replicates the pair-cache creation snippet from
// CreateNaturalResource_impl_sub_e8c0 @ 0x14009e8c0 (the region-system builder).
#include "Action/exe_bodies.h"
//
// The pair-cache caches region-to-region path lengths: RegionPairCache (Action/PathSearch.h,
// 0x38 bytes) — the clock, the lifetime, the ring's write / expiry indices, the ring
// (RawArray<RegionPairEntry> at +0x18) and the AVL index at +0x28. FUN_14001cca0 resizes
// the ring to 0x20000 entries of stride 0x18 (~6 MB), which is faithful/correct.
//
// The lifetime is seeded via System::Time::Time(&tmp, DAT_140172210) with
// DAT_140172210 = 120.0f (see the builder below).

#include "Work/exe_bodies.h"          // WorkCellBST_Remove_ca60
#include "Action/PathSearch.h"        // RegionPairCache, RegionPairEntry
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "system/Value.h"             // RtSystem_Time_Ctor
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// 0x14001ca60 - the shared intrusive-AVL delete, WorkCellBST_Remove_ca60 (Work/exe_bodies.h).

// ---- FUN_14001cca0: resize the region-pair ring to 0x20000 entries (stride 0x18): the
// exe's Array<RegionPairEntry>::SetSize(0x20000), new entries {-1, -1, -1, <0x0C>, 0}.
static void RegionPairRingResize(RawArray<RegionPairEntry>* ring) {
    int iVar1 = ring->size;
    if (iVar1 != 0x20000) {
        RegionPairEntry* pvVar2 = ring->data;
        ring->size = 0x20000;
        if (ring->capacity < 0x20000) {
            ring->capacity = 0x20000;
            if (iVar1 == 0x1ffff) {
                ring->capacity = 0x60000;
            }
            ring->data = static_cast<RegionPairEntry*>(System::MemoryAllocator::MemAlloc((int64_t)ring->capacity * 0x18, 8));
        }
        int iVar3 = 0;
        if (pvVar2 == ring->data) {
            iVar3 = iVar1;
        }
        int64_t lVar7 = (int64_t)iVar3;
        if (lVar7 < 0x20000) {
            do {
                RegionPairEntry* puVar6 = &ring->data[lVar7];
                if (lVar7 < iVar1) {
                    if (puVar6 != nullptr) {
                        *puVar6 = pvVar2[lVar7];
                    }
                } else {
                    if (puVar6 != nullptr) {
                        puVar6->regionA = -1;          // H7: one qword store of -1 in retail
                        puVar6->regionB = -1;
                        puVar6->distance = -1;
                        puVar6->time = 0;
                    }
                }
                lVar7 = lVar7 + 1;
            } while (lVar7 < 0x20000);
        }
        if (pvVar2 != ring->data) {
            System::MemoryAllocator::MemFree(pvVar2);
        }
    }
}

// ---- Builder: allocate + init the whole 0x38-byte pair cache.
// Replicates the snippet in CreateNaturalResource_impl_sub_e8c0 (0x14009e8c0).
// Returns null on allocation failure (caller then zeroes Map+0x78 fields).
extern "C" RegionPairCache* Pf_RegionPairCache_Build() {
    RegionPairCache* pc = static_cast<RegionPairCache*>(System::MemoryAllocator::MemAlloc(0x38, 4));
    if (pc == nullptr) {
        return nullptr;
    }
    pc->newestTime = 0;
    // +0x08 = the cache RETENTION WINDOW, in Time units. Retail seeds it with
    // Time::Time(DAT_140172210) where DAT_140172210 = 120.0f, i.e. 120 seconds.
    // This was previously seeded 0 as a flagged deviation. That is NOT inert: the
    // evictor at 0x1400a5d10 drops every entry for which
    //   (now - entry.timestamp) > window
    // so a 0 window evicts each entry on the first tick after it is inserted and
    // defeats the cache entirely. Fixed 2026-08-30 alongside LANE1 row 41.
    {
        System::Time window{0};
        RtSystem_Time_Ctor(&window, 120.0f);   // slot 0x140152b00
        pc->lifetime = window._ticks;
    }
    pc->newest = 0;              // the ring write index
    pc->oldest = 0;              // the ring expiry index
    pc->ring.data = nullptr;
    pc->ring.size = 0;
    pc->ring.capacity = 0;
    pc->index.root = nullptr;
    pc->index.count = 0;         // the tree's trailing int (+0x30)

    // Init the ring at +0x18: 0x20000 entries of stride 0x18.
    RegionPairRingResize(&pc->ring);   // 0x14001cca0

    return pc;
}

// ============================================================
// 0x1400a5d10 - RegionPairCache::Expire(dt)
// ============================================================
// The pair-cache's per-sim-tick STALENESS SWEEP. Called from GameUpdateLoop::Update
// (retail 0x140096380 +0x7e) once per simulation iteration on Map +0x78.
//
// Our source called this "Game::UpdateTimedEvents" / fn_Game_UpdateTimedEvents. That is a
// MISNOMER - there is no timed-event queue. The receiver is the region-pair distance cache
// built by Pf_RegionPairCache_Build above (retail: the tail of 0x14009e8c0), and this
// function only ages entries out of it.
//
// Two distinct jobs, and the FIRST one is easy to miss:
//
//  1. `newestTime += *dt` - THE CACHE'S CLOCK. Nothing else in the binary advances it.
//     RegionPairCache::Store (0x1400a5b30) stamps every new entry's time with the
//     then-current clock. So while this function was unported the clock sat at 0, every
//     entry was stamped 0, and no entry could ever be considered stale.
//  2. Evict from the ring's oldest index while the oldest entry is older than `lifetime`
//     (120 game-seconds; see Pf_RegionPairCache_Build). For each evicted slot: erase its
//     (regionA, regionB) from the index tree via the AVL delete 0x14001ca60, reset the
//     slot to {-1,-1,-1,<pad>,0}, and advance the oldest index with wraparound at the
//     ring size.
//
// Note the insert already evicts on ring-FULL (newest==oldest), so the ring cannot overflow
// without this. What this adds is AGE-based invalidation: without it a cached region-to-
// region distance survives until 0x20000 (131072) further inserts push it out, so distances
// stay stale across bridges/roads/terrain edits that re-split or re-merge regions.
//
// Faithfulness notes:
//  - Retail's slot reset stores 8 bytes at entry+0x08 whose low half is 0xFFFFFFFF and
//    whose high half is an uninitialized stack slot (the decompiler's uStack_1c). We
//    transcribe the pad as 0, matching how the insert port handles its uStack_3c.
//  - The outer test is `> lifetime` (retail JLE skips the whole loop), and the loop repeats
//    on the same strict `>` after advancing - so an entry exactly AT the window survives.
void RegionPairCache::Expire(const int64_t* dt) {
    // 1) advance the cache clock.
    newestTime += *dt;

    const int64_t window = lifetime;
    RegionPairEntry* ringBase = ring.data;

    int head = oldest;
    if (newestTime - ringBase[head].time <= window) {
        return;
    }

    do {
        // stop when the expiry index catches the write index
        if (head == newest) {
            return;
        }

        const RegionPairEntry* entry = &ringBase[head];

        // 2a) erase this pair from the index tree
        AvlNode30* node = index.root;
        if (node != nullptr) {
            const int keyA = entry->regionA;
            do {
                const int nodeA = node->key0;
                if (keyA < nodeA) {
                    node = node->left;
                } else {
                    if (keyA <= nodeA) {
                        const int keyB = entry->regionB;
                        if (keyB < node->key1) {
                            node = node->left;
                            if (node == nullptr) break;
                            continue;
                        }
                        if (keyB <= node->key1) {
                            WorkCellBST_Remove_ca60(&index, node);   // 0x14001ca60
                            break;
                        }
                    }
                    node = node->right;
                }
            } while (node != nullptr);
        }

        // 2b) reset the slot
        RegionPairEntry* slot = &ringBase[oldest];
        slot->regionA = -1;          // H7: one qword store of -1 in retail
        slot->regionB = -1;
        slot->distance = -1;         // H7: one qword store {-1, pad} in retail
        slot->unusedDistanceHigh = 0;        // retail's uninitialized uStack_1c high half
        slot->time = 0;

        // 2c) advance the expiry index, wrapping at the ring size
        int next = oldest + 1;
        if (ring.size <= next) next = 0;
        oldest = next;
        head = next;

        ringBase = ring.data;
    } while (newestTime - ringBase[head].time > window);

}
