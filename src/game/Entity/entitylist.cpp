// src/game/Entity/entitylist.cpp
// Clean reimplementation of EntityList methods from Application-x64.exe.
//
// EntityList (0xE0 bytes) — manages all ComponentEntity instances in the world.
// NOT directly hooked — called by GameUpdateLoop::Update (already hooked).
//
// The original EntityList::Update (0x1400c4090) HANGS due to a spinlock.
// This module provides a custom reimplementation that replicates the logic
// without the spinlock, preserving the workaround from gameupdateloop.cpp.
//
// Methods reimplemented:
//   - EntityList::Update           (0x1400c4090) — custom reimpl (original hangs)
//   - EntityList::PreUpdate        (0x1400c3bd0) — deep reimpl
//   - EntityList::PostUpdate       (0x1400c43d0) — deep reimpl
//   - EntityList::TreeNodeHandler  (0x1400c2de0) — deep reimpl
//   - EntityList::PostUpdateNodeHandler (0x1400c2c70) — deep reimpl
//   - EntityList::PostUpdateStaging(0x1400c3ab0) — deep reimpl (static helper)
//   - EntityList::RemoveEntity     (0x1400c38e0) — deep reimpl
//   - EntityList::FlagAggregation  (0x1400c3470) — is ComponentEntity::RefreshEntityFlags
//   - EntityList::Serialize        (0x1400c44a0) — deep reimpl
//
// Decompiled from Application-x64.exe using Ghidra.

#include "ui/UserInterface.h"
#include "options.h"
#include "ext/render_culling/render_culling.h"   // [PerfX] TREE_SNAPSHOT walk
#include "sampler.h"
#include <vector>
#include "Entity/EntityList.h"
#include "Entity/ComponentEntity.h"
#include "Entity/tick_list.h"      // [PerfX WALKCACHE] the slot-8 walk, retail and cached
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Dialog/EntityUI.h"             // caller batch CB6: TypeId()
#include "Player/EditTool.h"
#include "Game/PlayerToolManager.h"   // m_toolManager->currentTool
#include "Component/ModelComponent.h"
#include "Component/ParticleComponent.h"   // AuditDyingEntity's particle arm
#include "Component/CharacterComponent.h"  // [PREFETCH] the staged walk prefetch (kWalkPrefetch == 2)
#include "graphics/ParticleInstance.h"     // [PREFETCHCOV] the emitter instance lines (kPrefetchCovParticle)
#include "Action/MoveTo.h"                 // [PREFETCHCOV] the MoveTo lines (kPrefetchCovMoveTo)
#include "stress_harness.h"               // g_compProfOn / CompProf_AddVtbl (BANISHED_STRESS)
#include "ext/walk_batch/walk_batch.h"    // EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKBATCH.md, WALKGRID.md): ext.walk_batch (A) / ext.walk_batch_grid (B), the batched walk instantiations
#include "ext/walk_batch_grid/walk_batch_grid.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKGRID.md): ext.walk_batch_grid (B), the grid walk's character tick
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "platform_time.h"   // Platform::PerfCounter for the BANISHED_SAVE_TIMING probe
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include "system/Value.h"   // ENDGAME B.3.4b System callers: System::Time
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include <new>
#include <new>
#include "Entity/exe_bodies.h"
#include "Game/exe_bodies.h"   // BucketTree_Serialize_21ba0
#include "Work/exe_bodies.h"   // WorkBST_RegisterWork_1a30, WorkBST_BSTRemove_a6e0
#include "object/class_vptr.h"   // Object::ClassVptr (the vptr hint)


// ============================================================
// ComponentEntity::TickComponents (0x1400c2de0)
// ============================================================
// Per-entity handler during update tree walk.
// Checks entity flag bit 2 (0x4) and calls component vtable +0x40
// on components that also have flag bit 2 set.
//
// Decompiled from 0x1400c2de0:
//   if ((entity+0x40 & 0x4) && !(entity+0x40 >> 10 & 1))
//     for each component:
//       if (comp+0x20 >> 2 & 1)  // component flag bit 2
//         call comp->vtable[+0x40](comp, time)
//       break if entity deleted (entity+0x40 & 0x400)
// [STRESS] EntityList_Update sub-phase accumulators (QPC ticks), read + reset by the
// stress harness's COMPPROF report. Only touched when g_compProfOn.
long long g_elProfDrainTicks = 0, g_elProfPreTicks = 0, g_elProfTreeTicks = 0, g_elProfBucketTicks = 0;
long long g_elProfTreeNodes = 0, g_elProfBucketVisits = 0, g_elProfBucketTicked = 0;
extern "C" void CompProf_End(int slot, long long qpcTicks);

// One component's slot-8 Tick, as both walks call it.
static inline void TickOneComponent(Component* c, const System::Time* time) {
    // Component slot 8 (+0x40) Tick, by name.
    if (g_compProfOn >= 2) {
        // [STRESS] BANISHED_STRESS_COMPPROF=2: attribute the per-frame (slot 8) tick
        // per component type, separately from the slot-9 bucket Update (kind 1 =
        // "/tree"). Opt-in: ~30k QPC pairs per frame at 800x10 (~+0.8 ms/frame).
        // OFFSCREEN: a character's tick (and its model's) is also charged to its class x
        // visibility ([COMPPROF] /char rows, src/diag/char_prof.h); read outside the
        // timed bracket.
        const int slot = CompProf_BeginKind(c, 1);
        const int charTok = CharProf_Begin(c, slot);
        const long long q0 = CompProf_Now();
        c->Tick(time);
        const long long dt = CompProf_Now() - q0;
        CharProf_End(charTok, dt);
        CompProf_End(slot, dt);
    } else {
        c->Tick(time);
    }
}

// The retail body is written once, in Entity/tick_list.h (TickList::Retail /
// RetailFrom: the entity flag tests, the count read once, the slot re-read
// before the call, the deleted-bit break after every component). The tree walk
// and the snapshot walk's stale entries both run exactly that loop.
void ComponentEntity::TickComponents(const System::Time* time) {
    auto tick = [time](Component* c) { TickOneComponent(c, time); };
    TickList::Retail(this, tick);
}

// ============================================================
// ComponentEntity::PostUpdateComponents (0x1400c2c70)
// ============================================================
// Per-entity handler during post-update tree walk.
// Checks entity flag bit 4 (0x10) and calls component vtable +0x50
// on components that also have flag bit 4 set.
//
// Decompiled from 0x1400c2c70:
//   if ((entity+0x40 & 0x10) && !(entity+0x40 >> 10 & 1))
//     for each component:
//       if (comp+0x20 >> 4 & 1)  // component flag bit 4
//         call comp->vtable[+0x50](comp)
//       break if entity deleted (entity+0x40 & 0x400)
void ComponentEntity::PostUpdateComponents() {
    uint32_t entityFlags = _entityFlags;

    // Check entity has "needs post-update" flag (bit 4) and is not deleted (bit 10)
    if ((entityFlags & 0x10) == 0) return;
    if ((entityFlags >> 10) & 1) return;

    int32_t compCount = _components._size;
    int64_t ci = 0;

    if (compCount <= 0) return;

    do {
        // Read component IBP and CheckPtr
        Component* c = _components[ci].Get();

        // Component flags at +0x20, bit 4 = "needs post-update"
        uint32_t compFlags = c->_flags;

        if ((compFlags >> 4) & 1) {
            // CheckPtr again before vtable call; slot 10 (+0x50) PostUpdate, by name.
            _components[ci].Get()->PostUpdate();
        }

        ci++;

        // Break if entity was deleted during component processing
        if (_entityFlags & 0x400) break;

    } while (ci < compCount);
}

// (B3R W2-EZ: IBPTreeInsert_CppImpl / IBPTreeRemove_CppImpl, the plain-BST
// stand-ins engine_funcs.cpp installed when 0x140021a30 / 0x14001a6e0 resolved null,
// are gone with it. Both addresses are registered (the AVL ports in
// src/game/Work/work_base_bst.cpp), so the fallback never ran; nothing else called them.)

// ============================================================
// Tree-walk corruption guard + diagnostics
// ============================================================
// The in-order walk (TreeMinimum/TreeSuccessor) intermittently faults reading
// a node's child/parent pointer through a HALF-CORRUPTED value — high dword a
// valid heap segment, low dword small garbage (e.g. 0x00000258_0000F1FA). See
// docs/history/notes/project_entitylist_postupdate_treesuccessor_crash. Every real node is
// allocated 8-byte aligned (fn_MemAlloc(...,0x28,8)) with a heap address in the
// low-4GB..high range, so a non-null pointer that is MISALIGNED or has a set
// high dword with a tiny low dword cannot be a live node — it is corruption.
// We refuse to dereference it (converting the hard crash into a safe early
// stop) and record where it came from so the root cause can be pinned down.
// A non-null 64-bit value that cannot be a live heap pointer: misaligned, or
// a half-written pointer (valid high dword, tiny low dword — the freelist-link
// pattern left in a freed block, e.g. 0x000001AC_0000D661).
// PERF A/B (2026-09-04): BANISHED_TREE_GUARDS=0 turns the corruption guards into no-ops
// so their walk cost can be measured against the faithful (unguarded) retail walk.
static const bool g_treeGuards = [] {
    const char* e = Opt().treeGuards;
    return !(e && e[0] == '0');
}();
static inline bool PtrCorrupt(const void* pv) {   // LEFT: void* is a raw address (diagnostic probe), not an object: misaligned or half-written tree links (Opt().treeGuards)
    if (!g_treeGuards) return false;
    const uintptr_t p = reinterpret_cast<uintptr_t>(pv);  // CAST-OK: ptr_int
    if (p == 0) return false;                 // null = legitimate terminator / cleared slot
    if (p & 0x7) return true;                 // heap objects are 8-byte aligned
    if ((uint32_t)(p >> 32) != 0 && (uint32_t)p < 0x10000) return true; // half-written
    return false;
}

// A non-null pointer that cannot be a live tree node.
static inline bool NodeCorrupt(const EntityTreeNode* n) {
    return PtrCorrupt(n);
}

// Set when a walk refuses a corrupt pointer; caller logs + continues.
static bool             g_treeCorrupt = false;
// PERF A/B: successor prefetch in the update-tree walk. OFF by default — measured
// 2026-09-04 at 800x10: no prefetch 0.25 ms/iter, one level 0.26, two levels 0.27; the
// extra TreeSuccessor walk costs more than the hidden miss. BANISHED_TREE_PREFETCH=1 enables.
static const bool       g_treePrefetch = [] {
    const char* e = Opt().treePrefetch;
    return e && e[0] == '1';
}();
static const char*      g_treeCorruptSite = nullptr;   // which deref tripped
static EntityTreeNode*  g_treeCorruptFrom = nullptr;   // node holding the bad field
static const void*      g_treeCorruptBad  = nullptr;   // the refused pointer value

static void FlagTreeCorrupt(const char* site, EntityTreeNode* from, const void* bad) {
    g_treeCorrupt = true;
    g_treeCorruptSite = site;
    g_treeCorruptFrom = from;
    g_treeCorruptBad = bad;
}

// Dump the captured corruption context. `from` is a live node whose child/parent
// field held the bad pointer — its full contents reveal whether the whole node
// was trashed (freed+reused) or just one field (partial write).
// Guard for the STAGING search/remove path (PreUpdate/PostUpdate staging loops).
//
// The always-on [TreeGuard] above covers the in-order WALK (TreeMinimum /
// TreeSuccessor / the walk body's entityIBP key). It does NOT cover the staging
// loops' own key-search, nor the AVL delete they call
// (fn_IBPTreeRemove -> WorkBST_BSTRemove_a6e0 -> SimpleBST_Retrace, in
// src/game/Work/work_base_bst.cpp — a faithful 1:1 port of 0x14001a6e0 that
// dereferences node->parent unguarded, exactly as retail does).
//
// So the SAME corruption the walk guard was built for still crashed here
// (2026-09-04, tutorial run): ACCESS_VIOLATION reading 0x00000235_00006D1C — the
// documented half-written signature, high dword a live heap segment, low dword a
// small cell-index-like value, landing on a node's +0x08 parent field. Stack was
// SimpleBST_Retrace <- WorkBST_BSTRemove_a6e0 <- EntityList_PostUpdateStaging.
// See memory project_entitylist_postupdate_treesuccessor_crash: that writer was
// root-caused (async pathfinder result commit, fixed 17g) and this is either a
// second writer or an uncovered path for the same class.
//
// Refusing the remove here is NOT masking a faithful retail crash: retail never
// has the corrupt pointer. It converts a run-ending AV into a logged event that
// carries the forensics needed to name the writer, and it is the same
// defense-in-depth policy already applied to the walk. BANISHED_TREE_GUARDS=0
// still turns it off for a faithful A/B.
//
// Returns true when the node is safe to hand to the AVL delete.
static bool StagingNodeRemovable(EntityTreeNode* node) {
    if (NodeCorrupt(node)) return false;
    if (PtrCorrupt(node->parent)) return false;
    if (PtrCorrupt(node->left))   return false;
    if (PtrCorrupt(node->right))  return false;
    if (PtrCorrupt(node->element)) return false;
    return true;
}

static void ReportStagingCorrupt(const char* which, EntityList* self, EntityTreeNode* node) {
    // Rate-limited on the node identity: an isolated corrupt node re-trips every
    // frame until something unlinks it, and an unbuffered flood is its own bug.
    static EntityTreeNode* lastNode = nullptr;
    if (node == lastNode) return;
    lastNode = node;

    printf("[TreeGuard] CORRUPT node refused during %s staging REMOVE: node=%p\n", which, (void*)node);
    if (node && !NodeCorrupt(node)) {
        printf("[TreeGuard]   node entityIBP=%p parent=%p left=%p right=%p bal=%d ht=%d\n",
               static_cast<void*>(node->element), static_cast<void*>(node->parent),
               static_cast<void*>(node->left), static_cast<void*>(node->right),
               (int)node->balance, (int)node->height);
    }
    printf("[TreeGuard]   postUpdateRoot=%p (cnt=%lld)  updateRoot=%p (cnt=%lld)\n",
           (void*)self->postUpdateTree._root, (long long)self->postUpdateTree._count,
           (void*)self->updateTree._root, (long long)self->updateTree._count);
    fflush(stdout);
}

static void ReportTreeCorrupt(const char* walk, EntityList* self, EntityTreeNode* prev) {
    // Rate-limit: the same isolated corruption re-trips every frame. Log the
    // full context only when the (site, from-node, bad-ptr) tuple changes.
    static const char*     lastSite = nullptr;
    static EntityTreeNode*  lastFrom = nullptr;
    static const void*      lastBad  = nullptr;
    if (g_treeCorruptSite == lastSite && g_treeCorruptFrom == lastFrom &&
        g_treeCorruptBad == lastBad) {
        return;
    }
    lastSite = g_treeCorruptSite; lastFrom = g_treeCorruptFrom; lastBad = g_treeCorruptBad;

    printf("[TreeGuard] CORRUPT node refused during %s walk: site=%s bad=%p\n",
           walk, g_treeCorruptSite ? g_treeCorruptSite : "?", g_treeCorruptBad);
    EntityTreeNode* f = g_treeCorruptFrom;
    if (f && !NodeCorrupt(f)) {
        printf("[TreeGuard]   from-node=%p  entityIBP=%p parent=%p left=%p right=%p bal=%d ht=%d\n",
               static_cast<void*>(f), static_cast<void*>(f->element),
               static_cast<void*>(f->parent), static_cast<void*>(f->left),
               static_cast<void*>(f->right), (int)f->balance, (int)f->height);
    }
    if (prev && !NodeCorrupt(prev) && prev != f) {
        printf("[TreeGuard]   prev-node=%p  entityIBP=%p parent=%p left=%p right=%p\n",
               static_cast<void*>(prev), static_cast<void*>(prev->element),
               static_cast<void*>(prev->parent), static_cast<void*>(prev->left),
               static_cast<void*>(prev->right));
    }
    printf("[TreeGuard]   postUpdateRoot=%p (cnt=%lld)  updateRoot=%p (cnt=%lld)\n",
           (void*)self->postUpdateTree._root, (long long)self->postUpdateTree._count,
           (void*)self->updateTree._root, (long long)self->updateTree._count);
    fflush(stdout);

    // BANISHED_TREE_CATCH=1 — name the WRITER with a hardware write watchpoint.
    //
    // The corruption is always the same shape: a fully live node (valid entityIBP,
    // valid children, sane balance/height, real parent linking down to it) whose
    // parent field at +0x08 has had only its LOW 32 BITS overwritten with a small
    // value (0x096d, 0x3ccb, 0x41f8, 0x6d63, 0x6d7f, 0x6d86 …) while the high dword
    // keeps the live heap segment. So it is an EXTERNAL 32-bit store landing on
    // node+0x08, not anything entitylist.cpp does — our tree code only ever writes
    // that field as a full 64-bit pointer.
    //
    // One writer of this class was root-caused and fixed in July (async pathfinder
    // result commit, see memory project_entitylist_postupdate_treesuccessor_crash).
    // This is a second one, and it is not confined to tree nodes: the 2026-09-04
    // tutorial crash was UI::Element::Enable walking a child whose vtable had been
    // zeroed, with RCX=0x00000171_000045F6 — the identical half-written signature on
    // a UI object.
}

// ============================================================
// Helper: In-order tree traversal (corruption-guarded)
// ============================================================
// Finds the leftmost node in the tree (minimum).
static EntityTreeNode* TreeMinimum(EntityTreeNode* node) {
    if (!node) return nullptr;
    for (;;) {
        EntityTreeNode* l = node->left;
        if (NodeCorrupt(l)) { FlagTreeCorrupt("min.left", node, l); return node; }
        if (!l) return node;
        node = l;
    }
}

// In-order successor of a node.
static EntityTreeNode* TreeSuccessor(EntityTreeNode* node) {
    if (!node) return nullptr;

    // If right child exists, successor is leftmost of right subtree
    EntityTreeNode* r = node->right;
    if (NodeCorrupt(r)) { FlagTreeCorrupt("succ.right", node, r); return nullptr; }
    if (r) {
        return TreeMinimum(r);
    }

    // Walk up until we find a node that is a left child of its parent
    EntityTreeNode* parent = node->parent;
    if (NodeCorrupt(parent)) { FlagTreeCorrupt("succ.parent", node, parent); return nullptr; }
    while (parent && parent->right == node) {
        node = parent;
        parent = node->parent;
        if (NodeCorrupt(parent)) { FlagTreeCorrupt("succ.up-parent", node, parent); return nullptr; }
    }
    return parent;
}

// ============================================================
// EntityList::Update (0x1400c4090 — CUSTOM REIMPL)
// ============================================================
// The original function hangs due to a spinlock issue in the hooked environment.
// This reimplementation replicates the exact logic from the Ghidra decompilation.
//
// Steps:
//   1. Accumulate time: *self += *time
//   2. PreUpdate: process staging arrays into trees
//   3. Tree walk: in-order traversal of update tree, call TreeNodeHandler per entity
//   4. 5-bucket round-robin: for each bucket, tick processCount entities with vtable[+0x48]
//   5. If removal list non-empty: PreUpdate again, PostUpdateStaging, then process removals
// ============================================================
// [PerfX TREE_SNAPSHOT -- NON-RETAIL data structure, IDENTICAL visit order]
// ============================================================
// The update tree is walked in order once per speed iteration (10x per frame at 10x)
// and is a plain BST of heap-pointer keys -- a pointer chase whose only product is the
// in-order entity sequence. That sequence changes only when a node is inserted or
// removed (PreUpdate / DrainRemovals, both OUTSIDE the walk: entities created during
// a tick land in the staging arrays, never directly in the tree), so it is cached as a
// flat array and rebuilt when either mutation site marks it dirty or the tree's node
// count no longer matches. The flat walk also prefetches the next entities' headers
// and component arrays. Faithful perf work since PERFXEXT (docs/extensions/handins/
// PERFXEXT.md): on in every flavour, no toggle; the kill-switch is kTreeSnapshot (was
// BANISHED_PERF_TREE_SNAPSHOT=0, default-on only in the perf build). WALKCACHE rides it.
static constexpr bool kTreeSnapshot = true;
static std::vector<ComponentEntity*> s_walkSnap;
static bool    s_walkDirty = true;
static int64_t s_walkCountAtBuild = -1;
static const EntityList* s_walkOwner = nullptr;
extern "C" void EntityList_MarkUpdateTreeDirty() { s_walkDirty = true; }

// [PerfX WALKCACHE -- NON-RETAIL side table, IDENTICAL ticks] (Entity/tick_list.h,
// docs/extensions/handins/WALKCACHE.md): one TickList::Entry per snapshot entity,
// the indices of its components that carry the slot-8 bit. Rebuilt (all stale)
// with the snapshot; an entry is built lazily after a retail pass over its
// entity. RefreshEntityFlags (every post-ctor writer of a component's flag low
// word calls it) marks its entity's entry stale through
// EntityList_TickListInvalidate. The snapshot is the tree's in-order walk, and the
// tree is keyed by the entity pointer (compared unsigned), so the lookup is a
// binary search; s_tickSorted re-proves the order at each rebuild and, if it
// ever fails, every entity takes the retail loop.
static std::vector<TickList::Entry> s_tickList;
static bool s_tickSorted = false;

void EntityList_TickListInvalidate(const ComponentEntity* entity) {
    if (!s_tickSorted) return;
    const int64_t i = TickList::IndexOf(s_walkSnap.data(), s_walkSnap.size(), entity);
    if (i >= 0) s_tickList[(size_t)i].state = TickList::kStale;
}

// kReserve: the faithful walk's rebuild (EntityList::Update) as it always was; ext.walk_batch's
// walk (EntityList::UpdateWalkBatch) rebuilds without the capacity hint, so the faithful
// copy keeps its single inlined std::vector::reserve and compiles as before (WALKBATCH.md
// section 6). The snapshot built is the same either way.
template <bool kReserve>
[[gnu::always_inline]] static inline bool RebuildWalkSnapshot(EntityList* self) {
    s_walkSnap.clear();
    s_tickList.clear();
    s_tickSorted = false;
    s_walkOwner = self;
    s_walkCountAtBuild = self->updateTree._count;
    EntityTreeNode* treeRoot = self->updateTree._root;
    if (!treeRoot) { s_walkDirty = false; Ext::RenderCulling::OnWalkSnapshot(nullptr, 0, true); return true; }
    g_treeCorrupt = false;
    EntityTreeNode* node = TreeMinimum(treeRoot);
    EntityTreeNode* prev = nullptr;
    if constexpr (kReserve)
        s_walkSnap.reserve((size_t)(self->updateTree._count > 0 ? self->updateTree._count : 16));
    while (node && !g_treeCorrupt) {
        ComponentEntity* entity = node->element;
        if (PtrCorrupt(entity)) { FlagTreeCorrupt("snap.entityIBP", node, entity); break; }
        s_walkSnap.push_back(entity);
        prev = node;
        node = TreeSuccessor(node);
    }
    if (g_treeCorrupt) {
        ReportTreeCorrupt("Snapshot", self, prev); s_walkSnap.clear();
        Ext::RenderCulling::OnWalkSnapshot(nullptr, 0, false);   // PERFXTBL: no side table on the tree walk
        return false;
    }
    s_tickList.assign(s_walkSnap.size(), TickList::Entry{nullptr, 0, TickList::kStale, 0, {0, 0, 0, 0}});
    s_tickSorted = TickList::StrictlyAscending(s_walkSnap.data(), s_walkSnap.size());
    // [PerfX PERFXTBL] the per-entity PerfX side table (ext/render_culling/side_table.h) follows the
    // snapshot: live per-frame state moves with its entity, the rest goes back to
    // PerfX's hash table.
    Ext::RenderCulling::OnWalkSnapshot(s_walkSnap.data(), s_walkSnap.size(), s_tickSorted);
    s_walkDirty = false;
    return true;
}

// [PerfX PREFETCH -- NON-RETAIL hints only, IDENTICAL ticks] (docs/extensions/handins/
// PREFETCH.md). The snapshot walk's software prefetch, chosen at compile time:
//   0 = none (measures what the pipeline buys at all);
//   1 = the three-stage walk prefetch (TREE_SNAPSHOT / WALKCACHE: header i+3, component
//       array i+2, the components' flag words i+1) -- the walk shipped before PREFETCH;
//   2 = SHIPPED (PREFETCH, 2026-10-12: treeWalk -3..-6% over 1, -13..-16% over 0, same sign in
//       every interleaved pair, stride 1 the best of 1 / 2 / 4 / 8; handins/PREFETCH.md section 9):
//       the staged tick prefetch: the same chain one stage deeper, plus the first lines
//       the slot-8 ticks read (the whole component object, the character's current
//       action, the model's animation layers and pose array). WalkPrefetchStaged below.
// A prefetch never faults and never changes a value; every address a stage computes is
// re-derived from live objects in that same stage (nothing is carried between stages
// but the TickList entry, validated against the live count as the visit does).
static constexpr int kWalkPrefetch = 2;
// Stage spacing for kWalkPrefetch == 2: stage k sits k * kWalkPrefetchStride entities ahead
// (1 = one entity apart; the PREFETCH stride sweep measured 1 / 2 / 4 / 8).
static constexpr size_t kWalkPrefetchStride = 1;
// [PerfX PREFETCHCOV -- NON-RETAIL hints only, IDENTICAL ticks] (docs/extensions/handins/
// PREFETCHCOV.md; IDEAS_20261012 section 2.1, "prefetch coverage II"). Sub-arms of the
// kWalkPrefetch == 2 pipeline: no new stage, the stride unchanged, the same rules. Each is
// its own switch so the A/B can take them one at a time; all OFF = the shipped variant 2
// (the object is the same code). Every one is a measured keep / drop (PREFETCH.md section 7).
//   kPrefetchCovParticle     ParticleComponent: the emitter slot array (i+2); each live
//                            emitter's ParticleSystemInstance lines the tick and its Update
//                            read (+0x00 vptr / _dirtyFlags / _localBounds, +0x7C _opacity,
//                            +0xC0 _resource / _boundsPtr) (i+1). The first kPrefetchCovEmitters
//                            emitters only.
//   kPrefetchCovMoveTo       CharacterComponent, stage i+1: besides the current Action's head, the
//                            lines a walking character's MoveTo is read at next (ext.walk_batch's
//                            Eligible and MoveTo::Execute): +0x00..+0x43 (vptr, _status, _next,
//                            character, description, destination), +0x60 (pathIndex, pathCount),
//                            +0x474 (field_0x474, speedMultiplier). The vptr is not resident at
//                            i+1, so the lines are named for any action; a shorter one wastes the
//                            two far prefetches. ARM only (docs/android/P13_WALK.md): measured on
//                            the Galaxy S10, not on the desktop, whose object stays as it was.
static constexpr bool kPrefetchCovParticle     = true;
static constexpr int  kPrefetchCovEmitters     = 4;
#if defined(__aarch64__)
static constexpr bool kPrefetchCovMoveTo       = true;
#else
static constexpr bool kPrefetchCovMoveTo       = false;
#endif

// kWalkPrefetch == 2. One entity apart per stage (an entity visit is ~75 ns, about one
// DRAM latency), each stage touching only lines the stage before pulled in:
//   i+5  entity header: the _components line and the _entityFlags line (+0x30 / +0x40
//        straddle a line for a quarter of the 16-aligned entities);
//   i+4  the component IBP slots the visit reads (the listed ones, or the first six);
//   i+3  each such component object's first 0x80 bytes: vptr, _flags, and the whole of
//        a CharacterComponent (0x80), ParticleComponent (0x48), StateMachineComponent
//        (0x58), the head of a ModelComponent;
//   i+2  by dynamic type (the vptr, resident since i+3): a character's current action
//        slot; a model's remaining lines (+0x80 .. +0x16F: the MeshInstance, the transform);
//   i+1  a character's current Action object (vptr, _next, _status); a model's animation
//        layer array and pose array (MeshInstance::Update / IsAnimating read them first).
//
// Every piece is always_inline: GCC treats __builtin_prefetch as touching no memory, so a
// helper that is not inlined early and only prefetches is classed `pure' by IPA and its
// calls are deleted as dead (seen in the first build of this block: the whole stage set
// vanished from the perf object).
struct WalkVptrs { const void* character; const void* model; };
// [PREFETCHCOV] the vptr only the coverage arm compares (null when its arm is off).
// A struct of its own so WalkVptrs keeps its size and the arms-off object stays the
// shipped code.
struct WalkCovVptrs { const void* particle; };

template <class Fn>
[[gnu::always_inline]] static inline void ForEachWalkCandidate(const ComponentEntity* e, const TickList::Entry& en,
                                        bool cached, Fn&& fn) {
    const Object::InternalPtr<Component>* comps = e->_components._data;
    int cc = e->_components._size;
    if (!comps || cc <= 0) return;
    if (cached && en.state == TickList::kCached) {
        for (int k = 0; k < en.count; ++k) {
            if (en.idx[k] >= cc) break;   // a stale key; the visit rebuilds it
            fn(comps, (int)en.idx[k]);
        }
    } else {
        if (cc > 6) cc = 6;
        for (int k = 0; k < cc; ++k) fn(comps, k);
    }
}

// [PREFETCHCOV] kPrefetchCovParticle, stage i+2: the emitter slot array (the component's
// +0x38 header, resident since i+3). Slots are 0x18 apart: the first and the last of the
// first kPrefetchCovEmitters name their lines.
[[gnu::always_inline]] static inline void PrefetchCovParticleSlots(const ParticleComponent* p) {
    const ParticleEmitterSlot* d = p->emitters._data;
    const int sz = p->emitters._size;
    if (!d || sz <= 0) return;
    __builtin_prefetch(d);
    __builtin_prefetch(&d[(sz < kPrefetchCovEmitters ? sz : kPrefetchCovEmitters) - 1]._pad14);
}

// [PREFETCHCOV] stage i+1: each live emitter's instance (the slot lines, from i+2, are read
// here; the same live test as the tick's Update arm: instance set, sceneId >= 0).
[[gnu::always_inline]] static inline void PrefetchCovParticleInstances(const ParticleComponent* p) {
    const ParticleEmitterSlot* d = p->emitters._data;
    int sz = p->emitters._size;
    if (!d || sz <= 0) return;
    if (sz > kPrefetchCovEmitters) sz = kPrefetchCovEmitters;
    for (int k = 0; k < sz; ++k) {
        const Graphics::ParticleInstance* inst = d[k].instance;
        if (!inst || d[k].sceneId < 0) continue;
        __builtin_prefetch(inst);              // vptr, _dirtyFlags, _localBounds
        __builtin_prefetch(&inst->_opacity);   // _opacity / _opacityPrev, the transform's head
        __builtin_prefetch(&inst->_resource);  // _resource / _boundsPtr (Update)
    }
}

[[gnu::always_inline]] static inline void WalkPrefetchStaged(ComponentEntity* const* arr, const TickList::Entry* tl,
                                      size_t n, size_t i, bool cached, const WalkVptrs& vt,
                                      const WalkCovVptrs& cv) {
    if (i + 5 * kWalkPrefetchStride < n) {
        const ComponentEntity* e = arr[i + 5 * kWalkPrefetchStride];
        __builtin_prefetch(&e->_components);
        __builtin_prefetch(&e->_entityFlags);
    }
    if (i + 4 * kWalkPrefetchStride < n) {
        ForEachWalkCandidate(arr[i + 4 * kWalkPrefetchStride], tl[i + 4 * kWalkPrefetchStride], cached,
            [](const Object::InternalPtr<Component>* comps, int k) __attribute__((always_inline)) {
                __builtin_prefetch(&comps[k]);
            });
    }
    if (i + 3 * kWalkPrefetchStride < n) {
        ForEachWalkCandidate(arr[i + 3 * kWalkPrefetchStride], tl[i + 3 * kWalkPrefetchStride], cached,
            [](const Object::InternalPtr<Component>* comps, int k) __attribute__((always_inline)) {
                const char* c = reinterpret_cast<const char*>(comps[k].Get());  // CAST-OK: crash_walk (a prefetch names a line, never a value)
                if (!c) return;
                __builtin_prefetch(c);
                __builtin_prefetch(c + 0x40);
                __builtin_prefetch(c + 0x7F);
            });
    }
    if (i + 2 * kWalkPrefetchStride < n) {
        ForEachWalkCandidate(arr[i + 2 * kWalkPrefetchStride], tl[i + 2 * kWalkPrefetchStride], cached,
            [&vt, &cv](const Object::InternalPtr<Component>* comps, int k) __attribute__((always_inline)) {
                const Component* c = comps[k].Get();
                if (!c) return;
                const void* vp = *reinterpret_cast<const void* const*>(c);  // CAST-OK: vslot (the vptr picks the stage)
                if (vp == vt.character) {
                    const ActionStack& st = static_cast<const CharacterComponent*>(c)->actionStack;
                    const int32_t cur = st.current;
                    if (cur >= 0 && cur < st.actions._size && st.actions._data)
                        __builtin_prefetch(&st.actions._data[cur]);
                } else if (vp == vt.model) {
                    const char* m = reinterpret_cast<const char*>(c);  // CAST-OK: crash_walk (a prefetch names a line, never a value)
                    __builtin_prefetch(m + 0x80);
                    __builtin_prefetch(m + 0xC0);
                    __builtin_prefetch(m + 0x100);
                    __builtin_prefetch(m + 0x140);
                    __builtin_prefetch(m + sizeof(ModelComponent) - 1);
                } else {
                    // [PREFETCHCOV] the other arms (empty when all of them are off).
                    if constexpr (kPrefetchCovParticle) {
                        if (vp == cv.particle) {
                            PrefetchCovParticleSlots(static_cast<const ParticleComponent*>(c));
                            return;
                        }
                    }
                }
            });
    }
    if (i + 1 * kWalkPrefetchStride < n) {
        ForEachWalkCandidate(arr[i + 1 * kWalkPrefetchStride], tl[i + 1 * kWalkPrefetchStride], cached,
            [&vt, &cv](const Object::InternalPtr<Component>* comps, int k) __attribute__((always_inline)) {
                const Component* c = comps[k].Get();
                if (!c) return;
                const void* vp = *reinterpret_cast<const void* const*>(c);  // CAST-OK: vslot (the vptr picks the stage)
                if (vp == vt.character) {
                    const ActionStack& st = static_cast<const CharacterComponent*>(c)->actionStack;
                    const int32_t cur = st.current;
                    if (cur >= 0 && cur < st.actions._size && st.actions._data) {
                        const char* a = reinterpret_cast<const char*>(st.actions._data[cur].Get());  // CAST-OK: crash_walk (a prefetch names a line, never a value)
                        if (a) {
                            __builtin_prefetch(a);
                            if constexpr (kPrefetchCovMoveTo) {
                                __builtin_prefetch(a + offsetof(MoveTo, destination) + 0xB);
                                __builtin_prefetch(a + offsetof(MoveTo, pathIndex));
                                __builtin_prefetch(a + offsetof(MoveTo, field_0x474));
                            } else {
                                __builtin_prefetch(a + 0x1F);
                            }
                        }
                    }
                } else if (vp == vt.model) {
                    const Graphics::MeshInstance& mi = static_cast<const ModelComponent*>(c)->meshInstance;
                    if (mi._animation._layers._data) __builtin_prefetch(mi._animation._layers._data);
                    if (mi._nodes._data) __builtin_prefetch(mi._nodes._data);
                } else {
                    // [PREFETCHCOV] the other arms (empty when all of them are off).
                    if constexpr (kPrefetchCovParticle) {
                        if (vp == cv.particle) {
                            PrefetchCovParticleInstances(static_cast<const ParticleComponent*>(c));
                            return;
                        }
                    }
                }
            });
    }
}

// The walk's per-component tick: the faithful one exactly as before, or a batched one.
// kWalk is a Ext::WalkBatch::Walk (walk_batch.h).
template <int kWalk>
static inline auto MakeWalkTick(const System::Time* time) {
    if constexpr (kWalk != Ext::WalkBatch::kWalkFaithful) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKBATCH.md, WALKGRID.md): ext.walk_batch (class A) / ext.walk_batch_grid (class B) -- a
        // CharacterComponent (told by its vptr, as the walk prefetch does) gets
        // Ext::WalkBatch::TickCharacter (kWalkPlace) or Ext::WalkBatchGrid::TickCharacter (kWalkNoPlace), which
        // take MoveTo::Execute's walking branch with a short step and run
        // CharacterComponent::Tick for everything else (src/ext/walk_batch/walk_batch.h); every
        // other component gets the faithful tick. Out of line on purpose: this TU's faithful
        // code stays as it was.
        const void* const charVt = Object::ClassVptr<CharacterComponent>::Get();
        return [time, charVt](Component* c) {
            if (*reinterpret_cast<const void* const*>(c) == charVt) {  // CAST-OK: vslot (the vptr names the class)
                if constexpr (kWalk == Ext::WalkBatch::kWalkNoPlace)
                    Ext::WalkBatchGrid::TickCharacter(static_cast<CharacterComponent*>(c), time);
                else
                    Ext::WalkBatch::TickCharacter(static_cast<CharacterComponent*>(c), time);
            } else {
                TickOneComponent(c, time);
            }
        };
    } else {
        return [time](Component* c) { TickOneComponent(c, time); };
    }
}

// Returns true when the snapshot walk ran (so the tree walk is skipped).
// kWalk (a Ext::WalkBatch::Walk): kWalkPlace is the instantiation EntityList::UpdateWalkBatch runs
// (ext.walk_batch), kWalkNoPlace EntityList::UpdateWalkBatchGrid's (ext.walk_batch_grid);
// kWalkFaithful is the faithful walk (EntityList::Update), the body as it was, with no test in
// its loop.
template <int kWalk>
static bool WalkUpdateTreeSnapshot(EntityList* self, const System::Time* time, bool prof) {
    if (!kTreeSnapshot) return false;
    if (s_walkDirty || s_walkOwner != self || s_walkCountAtBuild != self->updateTree._count) {
        if (!RebuildWalkSnapshot<kWalk == Ext::WalkBatch::kWalkFaithful>(self)) return false;   // corrupt: fall back to the tree walk
    }
    const size_t n = s_walkSnap.size();
    ComponentEntity* const* arr = s_walkSnap.data();
    TickList::Entry* tl = s_tickList.data();
    const bool cached = s_tickSorted;
    auto tick = MakeWalkTick<kWalk>(time);
    // BANISHED_STRESS_COMPPROF=4: the WALKCACHE shadow (TickList::Cached<true>) checks
    // every component the cached walk skips against retail's read of it.
    auto report = [](bool ok, int64_t detail) { StressHarness_Verify("TickList", ok, (long long)detail); };
    const bool verify = g_verifyOn != 0;
    WalkVptrs vt{nullptr, nullptr};
    if constexpr (kWalkPrefetch == 2)
        vt = {Object::ClassVptr<CharacterComponent>::Get(), Object::ClassVptr<ModelComponent>::Get()};
    WalkCovVptrs cv{nullptr};   // [PREFETCHCOV] set only when the arm is on
    if constexpr (kWalkPrefetch == 2 && kPrefetchCovParticle) cv.particle = Object::ClassVptr<ParticleComponent>::Get();
    for (size_t i = 0; i < n; ++i) {
        if constexpr (kWalkPrefetch == 2) WalkPrefetchStaged(arr, tl, n, i, cached, vt, cv);
        if constexpr (kWalkPrefetch == 1) {
        // Three-deep prefetch pipeline: entity header (i+3) -> its component IBP array
        // (i+2, header already resident) -> the component objects' flag words (i+1,
        // array already resident). Each level only touches memory the previous level
        // pulled in, so no prefetch ever waits on a miss. With a built tick list
        // (WALKCACHE) only the listed components are fetched: the others are not read.
        if (i + 3 < n) __builtin_prefetch(&arr[i + 3]->_components);
        if (i + 2 < n) {
            const Object::InternalPtr<Component>* comps = arr[i + 2]->_components._data;
            if (comps) __builtin_prefetch(comps);
        }
        if (i + 1 < n) {
            const ComponentEntity* pe1 = arr[i + 1];
            const Object::InternalPtr<Component>* comps = pe1->_components._data;
            int cc = pe1->_components._size;
            if (comps && cc > 0) {
                if (cached && tl[i + 1].state == TickList::kCached) {
                    const TickList::Entry& e1 = tl[i + 1];
                    for (int k = 0; k < e1.count; ++k) {
                        if (e1.idx[k] >= cc) break;   // a stale key; the visit rebuilds it
                        const Component* c = comps[e1.idx[k]].Get();
                        if (c) __builtin_prefetch(&c->_flags);
                    }
                } else {
                    if (cc > 6) cc = 6;
                    for (int k = 0; k < cc; ++k) {
                        const Component* c = comps[k].Get();
                        if (c) __builtin_prefetch(&c->_flags);
                    }
                }
            }
        }
        }   // kWalkPrefetch == 1
        ComponentEntity* entity = arr[i];
        Ext::RenderCulling::g_walkIndex = static_cast<int64_t>(i);   // [PerfX PERFXTBL] the ticks' side-table slot
        if (!((entity->_entityFlags >> 10) & 1)) {
            if (!cached)      entity->TickComponents(time);
            else if (verify)  TickList::Cached<true>(entity, tl[i], tick, report);
            else              TickList::Cached<false>(entity, tl[i], tick, report);
        }
        if (prof) ++g_elProfTreeNodes;
    }
    Ext::RenderCulling::g_walkIndex = -1;   // [PerfX PERFXTBL] outside the walk: binary search
    return true;
}

// ============================================================
// 0x1400c3f90 — EntityList::EntityList
// ============================================================
// The member initialisers are retail's stores in its order: +0x00 = 0; the
// master Array's two zero qwords (+0x08 / +0x10); the five bucket Arrays
// (0x14003accc over 0x140008080, the same two qwords each); the two Sets'
// root qword and count dword (+0x68 / +0x70, +0x78 / +0x80); the staging and
// removal Arrays; then the body: the +0xE0 byte, the five process counts (the
// +0xB8 store is retail's qword of 2, H7: two stores) and the cursors.
EntityList::EntityList() {
    accumulatedTime._ticks = 0;            // +0x00 (retail: before the Arrays)
    shuttingDown = false;                  // +0xE0
    processCount[0] = 0x32;                // +0xA8 50
    processCount[1] = 0xf;                 // +0xAC 15
    processCount[2] = 8;                   // +0xB0
    processCount[3] = 5;                   // +0xB4
    processCount[4] = 2;                   // +0xB8 (retail's qword store of 2: with +0xBC)
    cursor[0] = 0;                         // +0xBC
    cursor[1] = 0;                         // +0xC0 (retail: two qword stores of 0)
    cursor[2] = 0;                         // +0xC4
    cursor[3] = 0;                         // +0xC8
    cursor[4] = 0;                         // +0xCC
}

// ============================================================
// 0x1400c3e60 — EntityList::~EntityList
// ============================================================
// The body is the Shutdown call (0x1400c3e7d); the members are destroyed after
// it in reverse declaration order: removalList, stagingPostUpdate,
// stagingUpdate (~Array: MemFree, two zero qwords), postUpdateTree,
// updateTree (Set Clear: both subtrees, the root, root = 0, count = 0), the
// buckets 4..0 (0x14003ad30 with 0x140001740), masterArray.
EntityList::~EntityList() {
    Shutdown();   // 0x1400c3d30
}

template <int kWalk>
[[gnu::always_inline]] inline void EntityList::UpdateImpl(const System::Time* time) {
    // Step 0 (SA DEVIATION — one-frame removal deferral): drain the removal list
    // that was BUILT LAST FRAME, at the TOP of this frame's update, instead of at
    // the bottom of the same frame it was built (retail drains at the end — the
    // 0x1400c4090 `param_1[0x1b]` block after the component loop).
    //
    // WHY: an entity is marked-for-removal (0x400) + pushed onto removalList during
    // its own component tick (destroy SM state-5 -> Destroy_MarkAndPushRemoval).
    // Retail relies on the open info dialog's STRONG datasource ref to keep the
    // entity's memory alive (marked-but-alive) until the dialog closes, so retail's
    // unguarded per-frame dialog slots (e.g. ResidenceUI::Slot7 0x1400B51F0) only
    // ever read valid memory. In standalone the dialog controllers don't hold that
    // strong ref, so an end-of-frame drain FREES the entity in the same frame it is
    // marked — and this frame's later UI pump (GameUpdateLoop step 9) + EditTool
    // tick (step ~984) then read freed memory (ACCESS_VIOLATION in IsDerivedFrom,
    // ResidenceUI_Slot7 <- freed entity, 2026-07-21).
    //
    // Deferring the free by one frame restores the "marked but alive" window every
    // SA_DialogEntityAlive guard and the EditTool close-on-entity-death path (Task
    // #1) depend on: frame N marks; frame N's UI pump + EditTool tick see 0x400 ->
    // guards bail and the tool closes the panel; frame N+1 drains + frees with no
    // dialog left pointing at it. Marked entities are inert meanwhile — every update
    // loop above already skips bit-10 (0x400) entities.
    int64_t pq0, pq1;
    const bool prof = g_compProfOn != 0;
    if (prof) Platform::PerfCounter(&pq0);
    DrainRemovals();
    if (prof) { Platform::PerfCounter(&pq1); g_elProfDrainTicks += pq1 - pq0; pq0 = pq1; }

    // Step 1: Accumulate time
    accumulatedTime._ticks += time->_ticks;

    // Step 2: PreUpdate — process staging arrays into update tree
    PreUpdate();
    if (prof) { Platform::PerfCounter(&pq1); g_elProfPreTicks += pq1 - pq0; pq0 = pq1; }

    // Step 3: In-order traversal of update tree (+0x68)
    EntityTreeNode* treeRoot = updateTree._root;
    // [ext.walk_batch / ext.walk_batch_grid] kWalk: the batched snapshot walks. The tree walk
    // below (the snapshot's corrupt-tree fallback) is retail's in every instantiation.
    if (treeRoot && WalkUpdateTreeSnapshot<kWalk>(this, time, prof)) {
        treeRoot = nullptr;   // [PerfX] flat snapshot walk ran -- identical order, no tree chase
    }
    if (treeRoot) {
        g_treeCorrupt = false;

        // Find leftmost node (in-order start)
        EntityTreeNode* node = TreeMinimum(treeRoot);
        EntityTreeNode* prev = nullptr;

        while (node && !g_treeCorrupt) {
            // CheckPtr on the node (it's an IBP)

            // Guard the node's entity-IBP key against a freelist link (UAF node).
            ComponentEntity* entity = node->element;
            if (PtrCorrupt(entity)) {
                FlagTreeCorrupt("entityIBP", node, entity);
                break;
            }

            // Prefetch the in-order successor's entity header + component array before
            // ticking this one (PERF 2026-09-04: the walk is a pointer chase — node ->
            // entity -> component IBP array -> component -> vtable — and the entity
            // miss showed as the hottest self-time line). The real successor is still
            // computed AFTER the handler, exactly as retail does, so a node the handler
            // inserts as our successor is not skipped; this is a hint only.
            if (g_treePrefetch) {
                EntityTreeNode* pf = TreeSuccessor(node);
                if (pf && !PtrCorrupt(pf->element)) {
                    const ComponentEntity* pe = pf->element;
                    __builtin_prefetch(&pe->_components);
                    const Object::InternalPtr<Component>* comps = pe->_components._data;
                    if (comps) __builtin_prefetch(comps);
                }
            }

            // Check entity not deleted (bit 10 of flags at entity+0x40)
            if (!((entity->_entityFlags >> 10) & 1)) {
                // Call tree node handler (inline for Update, calls vtable +0x40)
                entity->TickComponents(time);
            }

            // Move to in-order successor
            prev = node;
            node = TreeSuccessor(node);
            if (prof) ++g_elProfTreeNodes;
        }

        if (g_treeCorrupt) ReportTreeCorrupt("Update", this, prev);
    }
    if (prof) { Platform::PerfCounter(&pq1); g_elProfTreeTicks += pq1 - pq0; pq0 = pq1; }

    // Step 4: 5-bucket round-robin
    g_samplerTag = 8;   // [Sampler] sub-tag: bucket (slot-9) ticks; restored to 4 at the end
    struct BucketTagReset { ~BucketTagReset() { g_samplerTag = 4; } } bucketTagReset;
    int64_t currentTime = accumulatedTime._ticks;

    for (int bucket = 0; bucket < 5; bucket++) {
        UpdateBucket* bkt = &buckets[bucket];
        int32_t count = bkt->_size;
        if (count == 0) continue;

        uint32_t toProcess = processCount[bucket];
        uint32_t cursorVal = cursor[bucket];
        if ((int32_t)cursorVal >= count) cursorVal = 0;

        uint32_t startIdx = cursorVal;
        uint32_t idx = startIdx;
        uint32_t processed = 0;

        if ((int32_t)toProcess > 0) {
            do {
                // Re-read bucket data pointer each iteration (may be reallocated)
                Object::InternalPtr<ComponentEntity>* arrBase = bkt->_data;

                // Read entity IBP and CheckPtr
                ComponentEntity* entIBP = arrBase[idx].Get();
                if (prof) ++g_elProfBucketVisits;

                // Check not deleted (bit 10)
                if (!((entIBP->_entityFlags >> 10) & 1)) {
                    // +0x20 is the entity's last-update System::Time (Serialize below
                    // writes it through Time::Serialize)
                    int64_t delta = currentTime - entIBP->_lastUpdateTime._ticks;

                    if (delta > 0) {
                        if (prof) ++g_elProfBucketTicked;
                        ComponentEntity* ent = entIBP;

                        // Check entity has "needs bucket update" flag (bit 3)
                        // and is not deleted (bit 10)
                        if ((ent->_entityFlags & 0x8) != 0 &&
                            (ent->_entityFlags & 0x400) == 0) {

                            int32_t compCount = ent->_components._size;
                            System::Time deltaTime = { delta };

                            for (int64_t ci = 0; ci < compCount; ci++) {
                                // Read component IBP and CheckPtr
                                Component* comp = ent->_components[ci].Get();

                                // Component flags at byte +0x20, bit 3 = "needs Update tick"
                                // Verified from disassembly: MOV ECX,[RAX+0x20]; SHR ECX,3
                                if ((comp->_flags >> 3) & 1) {

                                    // Component slot 9 (+0x48) Update, by name.
                                    if (g_compProfOn) {
                                        // [STRESS] per-component-type Update cost. Keyed by
                                        // vtable, so the only per-call cost is two QPC reads.
                                        const int slot = CompProf_Begin(comp);
                                        const long long q0 = CompProf_Now();
                                        comp->Update(&deltaTime);
                                        CompProf_End(slot, CompProf_Now() - q0);
                                    } else {
                                        comp->Update(&deltaTime);
                                    }
                                }

                                // Reload self pointer (matches original: param_1 = local_res8)
                                // The entity list pointer is stored in GameUpdateLoop and
                                // component updates may cause reallocation. In the original,
                                // param_1 is reloaded from a local. We just re-read current time.
                                currentTime = accumulatedTime._ticks;

                                // Check if entity deleted during component update
                                if (ent->_entityFlags & 0x400) break;
                            }
                        }

                        // Update entity timestamp
                        entIBP->_lastUpdateTime._ticks = currentTime;
                    }
                }

                // Advance cursor with wrap-around
                idx++;
                processed++;
                cursorVal++;
                // Re-read count (array may have changed size)
                count = bkt->_size;
                if ((int64_t)idx >= count) {
                    idx = 0;
                    cursorVal = 0;
                }
            } while (idx != startIdx && (int32_t)processed < (int32_t)toProcess);
        }

        // Save cursor position
        cursor[bucket] = cursorVal;
    }
    if (prof) { Platform::PerfCounter(&pq1); g_elProfBucketTicks += pq1 - pq0; }

    // Step 5: removal drain DEFERRED to the TOP of the NEXT EntityList_Update (see
    // EntityList_DrainRemovals call + rationale at the head of this function). What
    // state-5 pushed onto removalList this frame stays marked-but-alive through this
    // frame's UI pump + EditTool tick, and is freed at the next frame's Step 0.
}

// 0x1400c4090. The body is UpdateImpl<kWalkFaithful> (defined above these three, so it is
// inlined here); UpdateImpl<kWalkPlace> is ext.walk_batch's (UpdateWalkBatch) and
// UpdateImpl<kWalkNoPlace> ext.walk_batch_grid's (UpdateWalkBatchGrid): the same body whose
// snapshot walk offers each character's tick to Ext::WalkBatch::TickCharacter / TickCharacterGrid.
// GameUpdateLoop::Update picks one per sim iteration (Ext::WalkBatch::Select).
void EntityList::Update(const System::Time* time) {
    UpdateImpl<Ext::WalkBatch::kWalkFaithful>(time);
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKBATCH.md): ext.walk_batch (class A), src/ext/walk_batch/walk_batch.h.
void EntityList::UpdateWalkBatch(const System::Time* time) {
    UpdateImpl<Ext::WalkBatch::kWalkPlace>(time);
}

// EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKGRID.md): ext.walk_batch_grid (class B), src/ext/walk_batch_grid/walk_batch_grid.h.
void EntityList::UpdateWalkBatchGrid(const System::Time* time) {
    UpdateImpl<Ext::WalkBatch::kWalkNoPlace>(time);
}

// The retail step-5 body (0x1400c4090 tail), relocated so it drains the PRIOR
// frame's removalList. Faithful otherwise: PreUpdate + PostUpdateStaging then
// RemoveEntity per pending IBP, then clear the list.
// Close any still-open info dialog whose data-source entity is marked-for-removal
// (0x400) this drain, BEFORE the entity is freed below. A PINNED info panel is driven
// by the UI pump, NOT by EditTool, so EditTool's close-on-death (edittool.cpp:544) can
// never reach it; next frame the panel would read its dangling +0x28, and because the
// freed entity's +0x40 no longer has the 0x400 bit it FALSE-POSITIVES the per-panel
// guard and dereferences freed memory. Confirmed crash 2026-07-22 (pin a house panel,
// demolish it -> ResidenceUI_Slot7 -> IsDerivedFrom(freed)). We close during the marked
// window (entity still ALIVE here — reading data-source+0x40 is safe) so the free frame
// has no dialog pointing at it. This is what retail's strong data-source ref achieves;
// the standalone dialog holds only a weak ref, so we close explicitly. Fixes every
// entity panel (pinned or not), not just ResidenceUI.
static void EntityList_CloseDialogsForDyingEntities() {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw)
        return;
    UI::UserInterface* ui = gw->m_userInterface;
    if (!ui) return;

    // EntityUI is the shared frame controller on every entity info dialog; its +0x28 is
    // the data-source entity (raw .rdata type-id is null in standalone -> resolve by name).
    const uint32_t s_tidEntityUI = EntityUI::TypeId();   // caller batch CB6: was a lazy by-name cache
    if (s_tidEntityUI == 0 || s_tidEntityUI == 0xFFFFFFFFu) return;

    // Collect first, close after — fn_UI_CloseDialog mutates the list we're walking.
    UI::DialogInstance* toClose[16];
    int nClose = 0;

    // B.3.2: UI::UserInterface::_dialogs IS a Collection::LinkedList<DialogInstancePtr>
    // at +0x000, and retail's UI::UserInterface::GetFirstDialog (0x180031150) is just
    // `*(void**)retval = _dialogs._head`. Walking the container by name replaces the
    // raw head-fetch + `node + 0x10` next-pointer arithmetic; the node layout it read
    // (element +0x00, prev +0x08, next +0x10) is exactly LinkedList<T>::Node.
    auto& dialogs = ui->_dialogs;
    for (auto it = dialogs.GetFirst(); it && nClose < 16; ++it) {
        // node[0] = DialogInstance*
        UI::DialogInstance* dialog = *it;
        if (dialog) {
            Object::InternalPtr<UI::DialogController> ctrlRef = dialog->GetController(s_tidEntityUI);
            UI::DialogController* ctrl = ctrlRef.Get();
            if (ctrl != nullptr) {
                Object::Base* ds = ctrl->_dataSource;   // +0x28
                // The data source's +0x40 entity flags, read with no type check (standalone-only code)
                // (unchecked: the tree's EntityUI controllers hold a ComponentEntity; B10's check is not made here, as before)
                if (ds && ((static_cast<const ComponentEntity*>(ds)->_entityFlags >> 10) & 1))
                    toClose[nClose++] = dialog;
            }
        }
    }

    for (int i = 0; i < nClose; i++) {
        UI::UserInterface* dlgUI = toClose[i]->_ui;   // +0x10
        if (!dlgUI) dlgUI = ui;
        Object::InternalPtr<UI::DialogInstance> dialogRef(toClose[i]);
        dlgUI->CloseDialog(&dialogRef);
        printf("[EntityList] closed info dialog %p on dying (marked-for-removal) entity\n",
               static_cast<void*>(toClose[i]));
    }
}

// Sibling of the dialog-close above, same crash class (weak ref to a dying entity),
// same marked-window timing. The active world tool (EditTool) keeps the selected entity
// in an InternalBasePtr at +0x30 that its per-frame outline-follow (Impl_HighlightFollow,
// tool_base_render.cpp) dereferences. That ref is NOT generation-invalidated when the
// entity is freed, so the frame after a selected building is demolished the follow reads
// freed memory (Sub7080 -> GetTypeId on a garbage component ptr — confirmed crash on the
// LIVE_DESTROY path 2026-07-23). EditTool's own dead-ref cleanup keys off its dialog ref
// (+0x28) and also runs AFTER the follow, so it cannot save it. Clear the tool's selection
// here, during the marked window (entity still ALIVE — reading +0x40 is safe), so next
// frame there is nothing dangling to follow. Reaches the tool the same way the tool tick
// does: EntityUI == GameWorld+0x120, active-tool IBP at EntityUI+0x28 (PlayerToolManager::Update 0x1401026a0).
static void EntityList_ClearToolSelectionForDyingEntities() {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) return;
    PlayerToolManager* toolManager = gw->m_toolManager;
    if (!toolManager) return;
    if (toolManager->currentTool == nullptr) return;   // +0x28
    Tool* currentTool = toolManager->currentTool.Get();
    if (!currentTool) return;

    const uint32_t s_tidEditTool = EditTool::TypeId();   // caller batch CB6: was a lazy by-name cache
    if (s_tidEditTool == 0 || s_tidEditTool == 0xFFFFFFFFu) return;
    if (!::Object::IsDerivedFromNullSafe(currentTool, s_tidEditTool)) return;
    EditTool* tool = static_cast<EditTool*>(currentTool);   // after the type check (B10)

    // Selected-entity IBP at EditTool+0x30 (the outline-follow target).
    if (tool->lastEntityRef == 0) return;
    uintptr_t ent = reinterpret_cast<uintptr_t>(tool->lastEntityRef);   // CAST-OK: ptr_int (the tree's address-range probe below)
    if (ent < 0x10000 || ent > 0x00007FFFFFFFFFFFULL) return;
    if (((tool->lastEntityRef->_entityFlags >> 10) & 1) == 0) return;   // ComponentEntity +0x40

    // Drop both the dialog ref (+0x28) and the selected-entity ref (+0x30), exactly as
    // EditTool_CloseInfoDialog does — leaving +0x30 set would keep the follow chasing it.
    tool->entityRef = 0;
    tool->lastEntityRef = 0;
    printf("[EntityList] cleared EditTool selection on dying (marked-for-removal) "
           "entity %p\n", reinterpret_cast<void*>(ent));
}

// ============================================================
// [diag] BANISHED_DRAIN_AUDIT — "why does a destroyed entity leave a LIVE node in
// Graphics::Scene?" (the open question from the wild-food seed-crash wave).
//
// Nothing in the free path removes scene geometry: RemoveEntity only unlinks the
// entity from the buckets/trees and DeletePtr's the last strong ref. The ONLY
// unlink is MarkForRemoval's (0x1400c2ea0) +0x38 broadcast, fired at MARK time —
// so any re-registration between the mark and the free ships a scene node that
// points into memory the drain is about to recycle.
//
// Two things are measured on every entity the drain is about to free:
//   markStillSet — is bit 0x400 still set at free time? A CLEARED bit means the
//                  entity was un-marked after the mark (NotifyEntityChanged
//                  0x1400c3470 ZEROES entity+0x40 and rebuilds it from the
//                  component flag words, which do not carry 0x400) and was
//                  therefore ticked again like a live entity.
//   liveModel    — a ModelComponent-derived component whose modelId (+0x16c) is
//                  >= 0, i.e. it is STILL registered in Graphics::Scene as the
//                  allocation goes back to the heap. This is the dangling node.

// Audit + LAST-CHANCE SCENE UNLINK for one entity the drain is about to free.
//
// ROOT CAUSE this closes (measured 2026-08-14, PREREG A/B arm): an entity can be
// marked-for-removal BEFORE its own registration has run, so the mark-time +0x38
// broadcast finds modelId == -1 and unlinks nothing; the registration OnAdded pass
// (0x1400c2f60, component slot 6) then runs ModelComponent::Draw unconditionally —
// it has no notion of the 0x400 removal bit — and registers geometry that lives
// inside the allocation this drain is about to recycle. 44 of 109 freed entities in
// the repro arm were freed exactly like that; Graphics::Scene::Clean then walks a
// recycled node (RAX = two packed floats) and faults.
//
// Retail never needs a net here because it drains at the end of the same frame and
// (in the correct order) nothing registers after a mark. Our drain is already a
// deliberate SA deviation (see the one-frame deferral above), so the net lives here
// rather than as a guard inside a byte-faithful function: re-fire the component's
// own +0x38 handler, which is a no-op unless modelId >= 0 — i.e. unless something
// really did re-register after the mark.
//
// Row 792, LEFT (D.4 ROWS): the net goes with the deferral, not before it. Dropping it
// alone brings back the measured Scene::Clean fault. The deferral goes when the entity
// info dialogs hold retail's strong data-source reference (EntityUI, Dialog/entityui*.cpp)
// and the drain moves back to the end of the same frame (0x1400c4090's tail).
static void AuditDyingEntity(ComponentEntity* entity) {
    if (reinterpret_cast<uintptr_t>(entity) < 0x10000) return;  // CAST-OK: ptr_int

    const uint32_t s_tidModel = ModelComponent::TypeId();   // caller batch CB6: was a lazy by-name cache


    int models = 0, live = 0;
    int   n   = entity->_components._size;
    Object::InternalPtr<Component>* arr = entity->_components._data;
    if (s_tidModel && s_tidModel != 0xFFFFFFFFu) {
        for (int i = 0; i < n && arr; ++i) {
            Component* c = arr[i].Get();
            if (reinterpret_cast<uintptr_t>(c) < 0x10000) continue;  // CAST-OK: ptr_int
            if (!::Object::IsDerivedFromNullSafe(c, s_tidModel)) continue;
            ModelComponent* model = static_cast<ModelComponent*>(c);
            ++models;
            if (model->modelId < 0) continue;
            ++live;
            // Unlink it (vtable +0x38 = the removed-from-world handler; on
            // ModelComponent that is Scene::RemoveObject + modelId = -1). Dispatched
            // virtually so derived overrides (NaturalResourceComponent) still apply.
            // Caller batch CB6: slot 7 `OnRemove` of the proved ModelComponent (the
            // IsDerivedFrom test above); a compiler table's slot is never null.
            model->OnRemove();
        }
    }

    // The same net for a ParticleComponent (B4 H5 P1): its OnAdded (slot 6, 0x140052f70)
    // starts the autoStart emitters (StartEmitter 0x140052a50 -> Scene::AddObject) with
    // the same blindness to the 0x400 mark, and its dtor 0x140053310 now DELETES every
    // emitter instance (retail does; the tree leaked them before) without unregistering
    // it -- retail relies on the mark-time OnRemove 0x140052e80. Without this, an emitter
    // registered after the mark would leave Graphics::Scene a node pointing at the freed
    // instance. Re-fire the component's own OnRemove only when an emitter is still in
    // the scene (sceneId >= 0), as the model arm does on modelId.
    const uint32_t tidParticle = ParticleComponent::TypeId();
    for (int i = 0; i < n && arr; ++i) {
        Component* c = arr[i].Get();
        if (!::Object::IsDerivedFromNullSafe(c, tidParticle)) continue;
        ParticleComponent* particle = static_cast<ParticleComponent*>(c);
        for (int j = 0; j < particle->emitters._size; ++j) {
            if (particle->emitters[j].sceneId >= 0) {
                particle->OnRemove();   // slot 7
                break;
            }
        }
    }
}

void EntityList::DrainRemovals() {
    int32_t removalCount = removalList._size;
    if (removalCount <= 0) return;

    // Close dialogs + clear tool selection bound to the entities we're about to free
    // (see helpers above) — both are weak refs the free does not invalidate.
    EntityList_CloseDialogsForDyingEntities();
    EntityList_ClearToolSelectionForDyingEntities();

    PreUpdate();
    PostUpdateStaging();

    for (int64_t i = 0; i < removalCount; i++) {
        ComponentEntity* entityIBP = removalList._data[i];
        AuditDyingEntity(entityIBP);
        RemoveEntity(&entityIBP);
    }

    // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
    // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
    removalList.SetSize(0);

}

// ============================================================
// EntityList::PreUpdate (0x1400c3bd0)
// ============================================================
// Processes the staging update array (+0x88/+0x90):
// For each staged entity IBP, checks if entity has flag bit 2 set and is not deleted.
//   - If yes: inserts into update tree (+0x68) via BST insert
//   - If no: searches update tree for the entity and removes it via BST remove
// Then clears the staging array.
//
// Decompiled from 0x1400c3bd0.
void EntityList::PreUpdate() {
    int32_t stagingCount = stagingUpdate._size;
    if (stagingCount <= 0) {
        // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
        // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
        stagingUpdate.SetSize(0);
        return;
    }

    for (int i = 0; i < stagingCount; i++) {
        ComponentEntity** ibpPtr = &stagingUpdate._data[i];

        uint32_t entityFlags = (*ibpPtr)->_entityFlags;
        bool hasUpdateFlag = (entityFlags & 0x4) != 0;
        bool isDeleted = ((entityFlags >> 10) & 1) != 0;

        if (hasUpdateFlag && !isDeleted) {
            // Insert into update tree
            EntityTreeNode* outBuf = nullptr;
            WorkBST_RegisterWork_1a30(&updateTree, &outBuf, ibpPtr);
            EntityList_MarkUpdateTreeDirty();
        } else {
            // Search for entity in update tree, remove if found
            EntityTreeNode* node = updateTree._root;

            while (node != nullptr) {
                ComponentEntity* ibpVal = *ibpPtr;

                if (ibpVal < node->element) {
                    node = node->left;
                } else {
                    if (ibpVal <= node->element) {
                        // Found — remove from tree
                        // Validate first: the AVL delete derefs node->parent
                        // unguarded (faithful to retail 0x14001a6e0).
                        if (!StagingNodeRemovable(node)) {
                            ReportStagingCorrupt("PreUpdate", this, node);
                            break;
                        }
                        WorkBST_BSTRemove_a6e0(&updateTree, node);
                        EntityList_MarkUpdateTreeDirty();
                        break;
                    }
                    node = node->right;
                }
            }
        }
    }

    // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
    // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
    stagingUpdate.SetSize(0);
}

// ============================================================
// EntityList::PostUpdateStaging (0x1400c3ab0) — static helper
// ============================================================
// Same pattern as PreUpdate but for the post-update tree (+0x78)
// and staging array (+0x98/+0xa0), checking flag bit 4 (0x10).
//
// Decompiled from 0x1400c3ab0.
void EntityList::PostUpdateStaging() {
    int32_t stagingCount = stagingPostUpdate._size;
    if (stagingCount <= 0) {
        // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
        // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
        stagingPostUpdate.SetSize(0);
        return;
    }

    for (int i = 0; i < stagingCount; i++) {
        ComponentEntity** ibpPtr = &stagingPostUpdate._data[i];

        uint32_t entityFlags = (*ibpPtr)->_entityFlags;
        bool hasPostUpdateFlag = (entityFlags & 0x10) != 0;
        bool isDeleted = ((entityFlags >> 10) & 1) != 0;

        if (hasPostUpdateFlag && !isDeleted) {
            // Insert into post-update tree
            EntityTreeNode* outBuf = nullptr;
            WorkBST_RegisterWork_1a30(&postUpdateTree, &outBuf, ibpPtr);
        } else {
            // Search for entity in post-update tree, remove if found
            EntityTreeNode* node = postUpdateTree._root;

            while (node != nullptr) {
                ComponentEntity* ibpVal = *ibpPtr;

                if (ibpVal < node->element) {
                    node = node->left;
                } else {
                    if (ibpVal <= node->element) {
                        // Found — remove from tree
                        // Same guard as PreUpdate. This is the path that actually
                        // crashed on the 2026-09-04 tutorial run.
                        if (!StagingNodeRemovable(node)) {
                            ReportStagingCorrupt("PostUpdate", this, node);
                            break;
                        }
                        WorkBST_BSTRemove_a6e0(&postUpdateTree, node);
                        break;
                    }
                    node = node->right;
                }
            }
        }
    }

    // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
    // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
    stagingPostUpdate.SetSize(0);
}

// ============================================================
// EntityList::PostUpdate (0x1400c43d0)
// ============================================================
// Processes post-update staging array, then does in-order tree walk
// of the post-update tree (+0x78), calling PostUpdateNodeHandler per entity.
//
// Decompiled from 0x1400c43d0.
void EntityList::PostUpdate() {
    // Step 1: Process staging post-update array
    PostUpdateStaging();

    // Step 2: In-order traversal of post-update tree (+0x78)
    EntityTreeNode* treeRoot = postUpdateTree._root;
    if (!treeRoot) return;

    g_treeCorrupt = false;

    // Find leftmost node (in-order start)
    EntityTreeNode* node = TreeMinimum(treeRoot);
    EntityTreeNode* prev = nullptr;

    while (node && !g_treeCorrupt) {
        // CheckPtr on the node

        // Guard the node's entity-IBP key: a freed-but-still-linked node has a
        // freelist link written over +0x00, so dereferencing it faults.
        ComponentEntity* entity = node->element;
        if (PtrCorrupt(entity)) {
            FlagTreeCorrupt("entityIBP", node, entity);
            break;
        }

        // Check entity not deleted (bit 10 of flags at entity+0x40)
        if (!((entity->_entityFlags >> 10) & 1)) {
            entity->PostUpdateComponents();
        }

        // Move to in-order successor
        prev = node;
        node = TreeSuccessor(node);
    }

    if (g_treeCorrupt) ReportTreeCorrupt("PostUpdate", this, prev);
}

// ============================================================
// EntityList::RemoveEntity (0x1400c38e0)
// ============================================================
// Removes an entity from its bucket, the master array, and both trees.
// Also calls InternalBasePtr::DeletePtr to release the entity reference.
//
// Entity metadata layout (on the entity itself):
//   +0x18: int32 — index in master array
//   +0x1C: packed field — low 24 bits = index in bucket, high 8 bits include bucket ID
//   +0x1F: int8 — bucket index (0-5, where 6 means "not in any bucket")
//
// Decompiled from 0x1400c38e0.
void EntityList::RemoveEntity(ComponentEntity** entityIBP) {
    ComponentEntity* entity = *entityIBP;


    // Read bucket index from entity+0x1F (the category byte of _handle)
    int8_t bucketIdx = static_cast<int8_t>(entity->_handle >> 24);

    // Step 1: Remove from bucket (if in one; bucket 6 means "none")
    if (bucketIdx != 6) {
        // The entity's bucket: buckets[bucketIdx] (retail `param_1 + 0x18 + idx * 0x10`)
        UpdateBucket* bucket = &buckets[bucketIdx];

        // Last element index in bucket
        uint32_t lastIdx = bucket->_size - 1;

        entity = *entityIBP;

        // Entity's index within bucket (low 24 bits of +0x1C)
        uint32_t entityBucketIdx = (entity->_handle << 8) >> 8;

        if (lastIdx != entityBucketIdx) {
            // Swap last element into removed element's slot
            Object::InternalPtr<ComponentEntity>* bucketData = bucket->_data;
            bucketData[entityBucketIdx] = bucketData[lastIdx];

            // Update swapped entity's bucket index field
            ComponentEntity* swappedEntity = bucket->_data[entityBucketIdx].Get();

            // Update low 24 bits of +0x1C with new index
            swappedEntity->_handle = swappedEntity->_handle ^ ((swappedEntity->_handle ^ entityBucketIdx) & 0xFFFFFF);
        }

        // Shrink bucket array.
        // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
        // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
        bucket->SetSize((int)lastIdx);
    }

    // Step 2: Remove from master array (+0x08)
    int32_t lastMasterIdx = masterArray._size - 1;

    entity = *entityIBP;
    int32_t entityMasterIdx = entity->_entityId;

    if (lastMasterIdx != entityMasterIdx) {
        // Swap last element into removed element's slot
        Object::InternalPtr<ComponentEntity>* masterData = masterArray._data;
        masterData[entityMasterIdx] = masterData[lastMasterIdx];

        // Update swapped entity's master array index
        masterData[entityMasterIdx]->_entityId = entityMasterIdx;
    }

    // Shrink master array.
    // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
    // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
    masterArray.SetSize(lastMasterIdx);

    // Step 3: Search and remove from update tree (+0x68)
    EntityTreeNode* updateNode = updateTree._root;
    while (updateNode != nullptr) {
        if (*entityIBP < updateNode->element) {
            updateNode = updateNode->left;
        } else {
            if (*entityIBP <= updateNode->element) {
                break;  // Found
            }
            updateNode = updateNode->right;
        }
    }

    // Step 4: Search and remove from post-update tree (+0x78)
    EntityTreeNode* postUpdateNode = postUpdateTree._root;
    while (postUpdateNode != nullptr) {
        if (*entityIBP < postUpdateNode->element) {
            postUpdateNode = postUpdateNode->left;
        } else {
            if (*entityIBP <= postUpdateNode->element) {
                break;  // Found
            }
            postUpdateNode = postUpdateNode->right;
        }
    }

    // Remove found nodes from trees
    if (updateNode != nullptr) {
        WorkBST_BSTRemove_a6e0(&updateTree, updateNode);
        EntityList_MarkUpdateTreeDirty();
    }
    if (postUpdateNode != nullptr) {
        WorkBST_BSTRemove_a6e0(&postUpdateTree, postUpdateNode);
    }

    // Step 5: Delete the entity IBP reference. Object::InternalBasePtr::DeletePtr
    // (0x1800c0150) on the slot: delete the object through its virtual dtor, then
    // null the slot.
    if (ComponentEntity* dying = *entityIBP) {
        delete dying;
        *entityIBP = nullptr;
    }
}

// ============================================================
// EntityList::Serialize (0x1400c44a0)
// ============================================================
// Save/load the entity list. Steps:
//   1. Construct Serializer from stream
//   2. Flush PreUpdate and PostUpdateStaging
//   3. On save: process removal list
//   4. Serialize time (System::Time::Serialize)
//   5. Serialize master array count, then each IBP
//   6. After load: verify each IBP is a ComponentEntity (clear if not)
//   7. Serialize bucket data via helper
//   8. Serialize processCount[5] and cursor[5] arrays
//   9. Per-entity metadata: master index, bucket index, bucket ID, time
//
// Decompiled from 0x1400c44a0.

// Steps 2 and 3 of EntityList::Serialize (0x1400c44a0), straight-line in retail between the
// Serializer's construction and accumulatedTime; a member (NOT RETAIL as a function) so
// ext.background_save can run it on the main thread before the rest of the serialise moves
// to a worker. It writes no stream bytes, and a second call finds every list empty (PreUpdate
// / PostUpdateStaging only SetSize(0) an empty staging array), so Serialize's own call is then
// a no-op.
void EntityList::FlushForSerialize(IO::Stream& stream) {
    // Step 2: Flush staging arrays
    PreUpdate();
    PostUpdateStaging();

    // Step 3: On save (stream+0x08 == 1), process removal list
    if (static_cast<int>(stream.GetDirection()) == 1) {   // IO::Stream::GetDirection, retail 0x180006200
        int32_t removalCount = removalList._size;
        if (removalCount > 0) {
            for (int64_t i = 0; i < removalCount; i++) {
                ComponentEntity* localIBP = removalList._data[i];
                RemoveEntity(&localIBP);
            }
            // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8
            // align 8, new tail zeroed. Element is the entity InternalBasePtr.
            removalList.SetSize(0);
        }
    }
}

void EntityList::Serialize(IO::Stream& stream) {
    // Step 1: Construct Serializer (version 0; destroyed at the end, as retail)
    IO::Serializer serializer(stream, 0);

    // Steps 2 and 3: flush the staging arrays; on save, process the removal list
    FlushForSerialize(stream);

    accumulatedTime.Serialize(serializer);

    // Step 5: Serialize master array count
    int32_t masterCount = masterArray._size;
    int32_t serializedCount = masterCount;
    serializer.Serialize(serializedCount, nullptr);

    // On load (mode == 0): resize master array to deserialized count
    if (static_cast<int>(serializer.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
        // new tail zeroed. Element is the 8-byte entity InternalBasePtr (byte-copy).
        masterArray.SetSize(serializedCount);
    }

    // Serialize each entity IBP in master array
    Object::InternalPtr<ComponentEntity>* ibpIter = masterArray._data;
    int64_t ibpCount = (int64_t)serializedCount;
    if (ibpCount > 0) {
        for (int64_t j = 0; j < ibpCount; j++) {
            Object::InternalPtr<ComponentEntity>* currentIBP = &ibpIter[j];
            currentIBP->Object::InternalBasePtr::Serialize(serializer);   // IAT 0x140152aa0 (0x1400c4587)

            // After loading, verify it's a ComponentEntity
            if (currentIBP->Get() != nullptr) {
                bool isDerived = ::Object::IsDerivedFromNullSafe(currentIBP->Get(), ComponentEntity::TypeId());
                if (!isDerived) {
                    *currentIBP = Object::InternalPtr<ComponentEntity>();
                }
            }
        }
    }

    // Step 6: Serialize bucket trees via helper (5 buckets starting at +0x18)
    serializer.Serialize(5, buckets, nullptr);   // 0x1400c45b9 -> 0x140021ba0, IO::Serializer::Serialize<UpdateBucket> (MSVC's n = 5 clone)

    // Step 7: Serialize processCount[5] and cursor[5] as bulk arrays
    // Retail (0x1400c45d0 / 0x1400c45e8) calls dispatch slot 0x140151278 =
    // IO::Serializer::Serialize(int count, int* data, name) — the INT-array
    // overload, 20 bytes per call. We were calling slot 0x140152400
    // (fn_Serializer_SerializeBulk), the BYTE overload, which read only 5 bytes
    // each and left the stream 30 bytes short for every load — silently
    // desyncing Geometry and everything after it. The offsets prove the width:
    // processCount is at +0xA8 and cursor at +0xBC, exactly 5 ints apart.
    serializer.Serialize(5, processCount, nullptr);
    serializer.Serialize(5, cursor, nullptr);

    // Step 8: Per-entity metadata serialization
    int32_t entityIdx = 0;
    if (masterArray._size > 0) {
        do {
            // Read master array index from entity+0x18
            int32_t masterIdxField = masterArray._data[entityIdx]->_entityId;

            // Read bucket index (low 24 bits of entity+0x1C)
            int32_t bucketIdxField = static_cast<int32_t>((masterArray._data[entityIdx]->_handle << 8) >> 8);

            // Read bucket ID from entity+0x1F (the category byte of _handle)
            int32_t bucketId = (int32_t)static_cast<int8_t>(masterArray._data[entityIdx]->_handle >> 24);

            // Serialize the three metadata fields
            serializer.Serialize(masterIdxField, nullptr);
            serializer.Serialize(bucketIdxField, nullptr);
            serializer.Serialize(bucketId, nullptr);

            // Serialize entity's timestamp at +0x20 (System::Time)
            masterArray._data[entityIdx]->_lastUpdateTime.Serialize(serializer);

            // Write back deserialized metadata to entity
            masterArray._data[entityIdx]->_entityId = masterIdxField;

            ComponentEntity* ent = masterArray._data[entityIdx].Get();
            ent->_handle = ent->_handle ^ ((static_cast<uint32_t>(bucketIdxField) ^ ent->_handle) & 0xFFFFFF);

            // The category byte +0x1F: retail stores the BYTE, the
            // +0x1C dword's byte [3] (ComponentEntity::_handleBytes).
            masterArray._data[entityIdx]->_handleBytes[3] = (int8_t)bucketId;

            entityIdx++;
        } while (entityIdx < masterArray._size);
    }

    // Step 9: the Serializer's destructor runs here
}

