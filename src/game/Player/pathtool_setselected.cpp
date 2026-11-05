// src/game/Player/pathtool_setselected.cpp
// Faithful 1:1 port of the PathTool "SetSelectedEntity" route-builder chain from
// Application-x64.exe. This is what fills the PathTool's point (+0x40) and
// segment (+0x50) arrays that PathTool::Draw renders — i.e. it computes the
// worker home<->workplace routes shown by the "Paths" report tool.
//
// Ported functions (B4 PLAYER: PathTool members, PathTool.h):
//   0x1400dd710  PathTool::SetSelected      SetSelectedEntity dispatcher
//   0x1400dd250  PathTool::ClearHighlight   clear old selection highlight
//   0x1400dd0f0  PathTool::SetHighlight     set new selection highlight
//   0x1400dd770  PathTool::BuildRoutes      the route builder (A* + AVL dedup)
//   0x1400de5d0  HomeDoorCell               the citizen's home door -> cell index
//   0x140026ba0  (the +0x30 route push)     Collection::Array<PathRoute>::SetSize
//   0x140007080  FindComponent<HighlightComponent>
//
// AVL dedup nodes are 0x28 bytes (PathSegmentNode, Player/PathTool.h: one layout for
// both the point tree — element {cell, 0} — and the segment tree — packed {min,max}):
//   +0x00 key (int or 8B), +0x08 parent, +0x10 left, +0x18 right,
//   +0x20 short balance, +0x22 short height.
// The rebalance/insert/flatten logic is byte-identical between the two trees, so it is
// factored into shared helpers (AvlRebalanceUp / *Tree_Insert / in-order flatten) — a
// faithful equivalent of the exe's 4x-inlined copies, transcribed from the disassembly.
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra); cross-checked vs disassembly.

#include "Player/PathTool.h"
#include "Component/HighlightComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/InteractComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Component/ResidenceComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/PathfinderThread.h"
#include "Action/PathAvl.h"        // AvlRotateLeft / AvlRotateRight (0x140014c70 / 0x140014da0)
#include "Action/PathSearch.h"     // PathTicket, PathResult
#include "collection/Collection.h"
#include <cstdint>
#include "application/Application.h"   // ENDGAME B.3.7 callers (K2)
#include "Player/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "system/MemoryAllocator.h"

namespace {

// ---- exe sub-callees, called by name ----
//   0x14004c4a0 HighlightRepaint_Impl (Player/exe_bodies.h) -- highlight refresh
//   0x140002e10 FindComponent<WorkPlaceComponent>, 0x140008090
//               FindComponent<ResidenceComponent> (Ghidra mislabels it
//               "FindComponent_Storage"), 0x140004740 FindComponent<MultiObjectComponent>
//               (the related-entity list), 0x140009790 FindComponent<CitizenComponent>
//   0x140087fb0 EntityAnchorCell, 0x14009cfb0 Map::RequestPath,
//   0x140014c70 / 0x140014da0 AvlRotateLeft / AvlRotateRight<PathSegmentNode>,
//   0x140122e20 InteractNodePosition (Action/exe_bodies.h)
//   0x1400021d0 Collection::Array<int32_t>::SetSize -- resize to 0 frees nothing,
//               it empties the result slot's cell list
//   0x140014c30 WorkBase_TreeNodeFree_14c30 (Work/exe_bodies.h)

// Allocate + init a fresh 0x28-byte AVL node with the given 8-byte element and
// parent (the point tree's element is {cell, 0}: its key zero-extended).
PathSegmentNode* AvlNodeAlloc(const PathSegment& key, PathSegmentNode* parent) {
    PathSegmentNode* n = static_cast<PathSegmentNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
    if (!n) return nullptr;
    n->seg.minCell = key.minCell;
    n->seg.maxCell = key.maxCell;
    n->parent  = parent;
    n->left    = nullptr;
    n->right   = nullptr;
    n->balance = 0;      // balance(short) + height(short): one dword store in the tree
    n->height  = 0;
    return n;
}

// Retrace/rebalance walking up from `node` (the node a child was just linked to).
// Faithful transcription of the exe's inlined rebalance: recompute height/balance,
// then AVL single/double rotate (14c70 = rotate-left, 14da0 = rotate-right).
void AvlRebalanceUp(PathSegmentNode** root, PathSegmentNode* node) {
    while (node != nullptr) {
        PathSegmentNode* parent = node->parent;
        PathSegmentNode* left   = node->left;
        PathSegmentNode* right  = node->right;
        short lh = left  ? (short)(left->height  + 1) : 0;
        short rh = right ? (short)(right->height + 1) : 0;
        short h  = (rh < lh) ? lh : rh;
        node->height = h;
        short bal = (short)(rh - lh);
        node->balance = bal;
        if (bal > 1 && right) {
            short rbal = right->balance;
            if (rbal < 1 && rbal < 0) { AvlRotateRight(root, right); }
            AvlRotateLeft(root, node);
        }
        // Second check re-reads balance from memory (a prior rotation may have changed it).
        short bal2 = node->balance;
        if (bal2 < -1) {
            left = node->left;
            if (left) {
                short lbal = left->balance;
                if (lbal > -1 && lbal > 0) { AvlRotateLeft(root, left); }
                AvlRotateRight(root, node);
            }
        }
        node = parent;
    }
}

// Insert `key` into the point AVL tree (int key, dedup on equal). count at *pCount.
void PointTree_Insert(PathSegmentNode** root, int* pCount, int key) {
    if (*root == nullptr) {
        PathSegmentNode* n = AvlNodeAlloc(PathSegment{key, 0}, nullptr);
        if (!n) return;
        *pCount += 1;
        *root = n;
        return;
    }
    PathSegmentNode* cur = *root;
    for (;;) {
        int nk = cur->seg.minCell;
        if (key < nk) {
            if (cur->left == nullptr) {
                PathSegmentNode* n = AvlNodeAlloc(PathSegment{key, 0}, cur);
                cur->left = n;
                AvlRebalanceUp(root, cur);
                *pCount += 1;
                return;
            }
            cur = cur->left;
        } else if (key > nk) {
            if (cur->right == nullptr) {
                PathSegmentNode* n = AvlNodeAlloc(PathSegment{key, 0}, cur);
                cur->right = n;
                AvlRebalanceUp(root, cur);
                *pCount += 1;
                return;
            }
            cur = cur->right;
        } else {
            return;  // equal → dedup
        }
    }
}

// Insert a {min,max} segment key into the segment AVL tree (dedup on equal pair).
void SegTree_Insert(PathSegmentNode** root, int* pCount, const PathSegment& seg) {
    if (*root == nullptr) {
        PathSegmentNode* n = AvlNodeAlloc(seg, nullptr);
        if (!n) return;
        *pCount += 1;
        *root = n;
        return;
    }
    PathSegmentNode* cur = *root;
    for (;;) {
        const PathSegment& nodeSeg = cur->seg;
        int nmin = nodeSeg.minCell;
        int nmax = nodeSeg.maxCell;
        bool goLeft = false, skip = false;
        if (seg.minCell < nmin) {
            goLeft = true;
        } else if (seg.minCell == nmin) {
            if (seg.maxCell < nmax) goLeft = true;
            else if (seg.maxCell <= nmax) skip = true;   // equal pair
        }
        if (skip) return;
        if (goLeft) {
            if (cur->left == nullptr) {
                PathSegmentNode* n = AvlNodeAlloc(seg, cur);
                cur->left = n;
                AvlRebalanceUp(root, cur);
                *pCount += 1;
                return;
            }
            cur = cur->left;
        } else {
            if (cur->right == nullptr) {
                PathSegmentNode* n = AvlNodeAlloc(seg, cur);
                cur->right = n;
                AvlRebalanceUp(root, cur);
                *pCount += 1;
                return;
            }
            cur = cur->right;
        }
    }
}

// In-order traversal helpers.
PathSegmentNode* InorderFirst(PathSegmentNode* n) { if (!n) return nullptr; while (n->left) n = n->left; return n; }
PathSegmentNode* InorderNext(PathSegmentNode* n) {
    if (n->right) { n = n->right; while (n->left) n = n->left; return n; }
    PathSegmentNode* p = n->parent;
    while (p && p->right == n) { n = p; p = p->parent; }
    return p;
}

// The three output lists of PathTool::SetSelected are PathTool::routes / points /
// segments (PathTool.h): Collection::Array<PathRoute> (stride 0x200c, align 4),
// Array<int32_t>, Array<PathSegment> (8 bytes at ALIGN 4, because this site
// allocates `fn_MemAlloc(..., cap << 3, 4)`). All three: byteCopy, init == nullptr
// (retail's empty else arm: the new tail keeps whatever the allocation held),
// destroy == nullptr. The two pushes always have newCount == oldCount + 1.

// ============================================================
// 0x1400de5d0 — the citizen's home (+0x238) door -> cell index. RCX is never read;
// RDX is the citizen's InternalPtr.
// ============================================================
int HomeDoorCell(CitizenComponent* const* citizen) {
    Map* map = GameUpdateLoop::_singleton->m_game;
    // 🐞 MISNAMED-FINDER TRAP (6th instance). The disasm at 0x1400de629 calls
    // 0x140002ed0 = FindComponent<InteractComponent> (type DWORD_1401c7900),
    // NOT the inline fn_FindComponent_Position in engine_funcs.h, which is
    // 0x14000d6c0 -> BuildComponent (DWORD_1401c7008). Both were spelled
    // "..._Position" and both return an IBP, so the wrong one compiled and ran.
    // It matters because 0x140122e20 casts the component's +0x18 description to
    // InteractDescription: an InteractComponent carries one, a BuildComponent
    // carries a BuildDescription (a SIBLING of InteractDescription, not a base —
    // factory_registrations.csv:232/273 both list parent "Description"). The cast
    // failed, our SA-GUARD returned a zero vector, and e5d0 mapped (0,0,0) to
    // cell (w>>1, w>>1) = the dead CENTRE of the map. Every route therefore started
    // at the map centre instead of the door -- and because the centre is the same
    // cell for every building, N workers collapsed into N identical src/dst pairs
    // (the "duplicate routes" report).
    InteractComponent* door = (*citizen)->home->FindComponent<InteractComponent>();   // 0x140002ed0
    Math::Vector3 vec;
    door->NodePosition(&vec, 0, 0, nullptr);
    int w = map->size;
    int col = (int)(vec.x + (float)(w >> 1));   // local_18 = x
    int row = (int)((float)(w >> 1) + vec.y);   // local_14 = y
    if (col < 0) col = 0;
    if (w <= col) col = w - 1;
    if (row < 0) row = 0;
    if (w <= row) row = w - 1;
    return row * w + col;
}

// One home -> workplace route of `citizen` appended to `routes` (the three
// identical inlined pushes of 0x1400dd770). `entry` is the caller's scratch
// record: only its first three ints are written, the tail is whatever it held.
void PushRoute(Collection::Array<PathRoute>& routes, PathRoute& entry, CitizenComponent* citizen) {
    ComponentEntity* workplace = citizen->workplace.Get();
    int anchorCell = EntityAnchorCell(&workplace, nullptr);
    CitizenComponent* copy = citizen;   // retail passes a COPY (local_2080), not the live slot
    int homeCell = HomeDoorCell(&copy);
    entry.srcCell   = homeCell;
    entry.dstCell   = anchorCell;
    entry.cellCount = 0;
    routes.SetSize(routes._size + 1, entry);   // 0x140026ba0
}

}  // namespace

// ============================================================
// 0x1400dd250 — clear the highlight flag (+0x11c) on the selected entity's related
// entities and refresh (c4a0).
// ============================================================
void PathTool::ClearHighlight() {
    if (selectedEntity == nullptr) return;
    MultiObjectComponent* list = selectedEntity->FindComponent<MultiObjectComponent>();   // 0x140004740
    if (list != nullptr) {
        int n = list->children._size;
        for (int i = 0; i < n; i++) {
            ComponentEntity* ent = list->children[i].Get();
            HighlightComponent* hi = ent->FindComponent<HighlightComponent>();   // 0x140007080
            if (hi != nullptr) {
                if (hi->highlightFlag != '\0') {
                    hi->highlightFlag = 0;
                    hi->Repaint();
                }
            }
        }
    }
    HighlightComponent* hi2 = selectedEntity->FindComponent<HighlightComponent>();   // 0x140007080
    if (hi2 != nullptr) {
        if (hi2->highlightFlag != '\0') {
            hi2->highlightFlag = 0;
            hi2->Repaint();
        }
    }
}

// ============================================================
// 0x1400dd0f0 — set the highlight flag (+0x11c = 1) on the related entities. Begins by
// clearing (d250), then sets. Same shape as d250 with flag value 1.
// ============================================================
void PathTool::SetHighlight() {
    ClearHighlight();
    if (selectedEntity == nullptr) return;
    MultiObjectComponent* list = selectedEntity->FindComponent<MultiObjectComponent>();   // 0x140004740
    if (list != nullptr) {
        int n = list->children._size;
        for (int i = 0; i < n; i++) {
            ComponentEntity* ent = list->children[i].Get();
            HighlightComponent* hi = ent->FindComponent<HighlightComponent>();   // 0x140007080
            if (hi != nullptr) {
                if (hi->highlightFlag != '\x01') {
                    hi->highlightFlag = 1;
                    hi->Repaint();
                }
            }
        }
    }
    HighlightComponent* hi2 = selectedEntity->FindComponent<HighlightComponent>();   // 0x140007080
    if (hi2 != nullptr) {
        if (hi2->highlightFlag != '\x01') {
            hi2->highlightFlag = 1;
            hi2->Repaint();
        }
    }
}

// ============================================================
// 0x1400dd770 — the route builder. Determines the home<->workplace cell pairs for the
// selected entity (citizen / workplace / residence), A*-routes each pair, and dedups the
// path cells (point tree) and segments (segment tree) into +0x40 / +0x50.
// ============================================================
void PathTool::BuildRoutes() {

    WorkPlaceComponent* workPlace = selectedEntity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    // ResidenceComponent (0x140008090) - its +0x50/+0x58 is the family list, each
    // family an array of citizens.
    ResidenceComponent* residence = selectedEntity->FindComponent<ResidenceComponent>();
    CitizenComponent* citizen = selectedEntity->FindComponent<CitizenComponent>();      // 0x140009790

    // Reset the three output arrays: each is "MemFree the buffer, zero _data, zero
    // the _size/_capacity qword" — the Collection::Array<T>::~Array body for a
    // trivially destructible T.
    // NOT RETAIL (spelling only): retail inlines that body; the port runs it as
    // DestroyAndFree, not an explicit `~Array()`, which would end the members'
    // lifetime ([basic.life]) before PushRoute and the SetSize calls below use them.
    routes.DestroyAndFree();
    points.DestroyAndFree();
    segments.DestroyAndFree();

    PathRoute entry;   // scratch route entry pushed via 6ba0 (garbage tail OK; count=0)

    // --- Determine the source route pairs. ---
    // (A) selected entity IS a citizen with both a home (+0x238) and a workplace (+0x248).
    if (citizen != nullptr) {
        if (citizen->home != nullptr) {
            if (citizen->workplace != nullptr) {
                PushRoute(routes, entry, citizen);
            }
        }
    }

    // (B) not a workplace with a profession → iterate the residence's families;
    // else iterate the workers.
    bool useResidence;
    if (workPlace == nullptr) { useResidence = true; }
    else { useResidence = (workPlace->profession == nullptr); }

    if (useResidence) {
        if (residence != nullptr) {
            int outer = residence->residentSlots._size;
            for (int oi = 0; oi < outer; oi++) {
                int inner = residence->residentSlots[oi]._size;
                for (int ii = 0; ii < inner; ii++) {
                    CitizenComponent* member = residence->residentSlots[oi]._data[ii].Get();
                    if (member->home != nullptr) {
                        if (member->workplace != nullptr) {
                            PushRoute(routes, entry, member);
                        }
                    }
                }
            }
        }
    } else {
        int workerCount = workPlace->workers._size;
        for (int wi = 0; wi < workerCount; wi++) {
            CitizenComponent* worker = workPlace->workers[wi].Get();
            if (worker->home != nullptr) {
                if (worker->workplace != nullptr) {
                    PushRoute(routes, entry, worker);
                }
            }
        }
    }

    // --- A*-route each pair, dedup cells/segments into the point/segment AVL trees. ---
    Map* map = GameUpdateLoop::_singleton->m_game;
    PathSegmentNode* pointTree = nullptr;  int pointCount = 0;
    PathSegmentNode* segTree   = nullptr;  int segCount   = 0;

    int routeCount = routes._size;
    for (int r = 0; r < routeCount; r++) {
        PathRoute& route = routes._data[r];
        uint32_t srcCell = route.srcCell;
        uint32_t dstCell = route.dstCell;

        // Async A* query: returns {slot@+0, token@+4}. Spin on the map's result table.
        PathTicket ticket = {0, 0};
        map->RequestPath(&ticket, 0xe1f, srcCell, dstCell);   // 0x14009cfb0
        int qIdx = ticket.slot;
        int qId  = ticket.token;
        if (qId == 0 || qIdx < 0) {
            continue;   // no query issued → skip this route
        }

        // Wait for the async worker to publish this query's cells. B6 row 77: retail spins
        // with no bound (0x1400ddc40-0x1400ddc76: re-read [map+0x68] each pass, test the slot,
        // else Application::YieldToThreads and JMP back); the tree's 20,000,000-spin bail is gone.
        PathfinderThread* holder;
        for (;;) {
            holder = map->pathfinder;   // 0x1400ddc40 MOV RDX,[RSI+0x68]
            int cap = holder->results.size;
            PathResult* table = holder->results.data;
            if (qIdx < cap &&
                table[qIdx].token == qId &&
                table[qIdx].ready != '\0') break;
            ::Application::Application::_singleton->YieldToThreads();   // 0x1400ddc70
        }

        // Copy the path cells (capped at 0x800) into routeEntry.pathCells, release the slot.
        PathResult& slot = holder->results.data[qIdx];
        int cellCount = slot.path._size;
        if (cellCount > 0x800) cellCount = 0x800;
        const int32_t* cellsPtr = slot.path._data;
        for (int c = 0; c < cellCount; c++)
            route.cells[c] = cellsPtr[c];
        slot.token = 0;      // clear queryId
        slot.ready = 0;      // clear ready flag
        slot.path.SetSize(0);   // 0x1400021d0: empty the slot's cell list
        route.cellCount = cellCount;  // routeEntry.pathCount


        // Insert each cell into the point tree; each consecutive pair into the segment tree.
        int pc = routes._data[r].cellCount;
        for (int i = 0; i < pc; i++) {
            int cell = routes._data[r].cells[i];
            PointTree_Insert(&pointTree, &pointCount, cell);
            if (i == 0) continue;   // no previous cell → no segment
            int prev = routes._data[r].cells[i - 1];
            int curc = routes._data[r].cells[i];
            PathSegment seg;
            if (prev < curc) { seg.minCell = prev; seg.maxCell = curc; }
            else             { seg.minCell = curc; seg.maxCell = prev; }
            SegTree_Insert(&segTree, &segCount, seg);
        }
    }

    // --- Flatten trees (in-order) into +0x40 (points) and +0x50 (segments). ---
    for (PathSegmentNode* n = InorderFirst(pointTree); n; n = InorderNext(n)) {
        points.SetSize(points._size + 1, n->seg.minCell);
    }
    for (PathSegmentNode* n = InorderFirst(segTree); n; n = InorderNext(n)) {
        segments.SetSize(segments._size + 1, n->seg);   // LEFT: POD AvlNode28: the tree reads AvlNode28's element as the 8-byte segment (AvlNodeAlloc in this file)
    }


    // --- Free the AVL trees (14c30 recursively frees a subtree). ---
    if (segTree) {
        WorkBase_TreeNodeFree(&segTree, segTree->left); WorkBase_TreeNodeFree(&segTree, segTree->right);
        System::MemoryAllocator::MemFree(segTree);
    }
    if (pointTree) {
        WorkBase_TreeNodeFree(&pointTree, pointTree->left); WorkBase_TreeNodeFree(&pointTree, pointTree->right);
        System::MemoryAllocator::MemFree(pointTree);
    }
}

// ============================================================
// 0x1400dd710 — SetSelectedEntity dispatcher.
// ============================================================
void PathTool::SetSelected(ComponentEntity* const* entity) {
    if (selectedEntity != *entity) {
        ClearHighlight();
        selectedEntity = *entity;
        SetHighlight();
        BuildRoutes();
    }
}

// ============================================================
// PathTool::Shutdown — slot 5 (+0x28), 0x1400de930: a bare JMP to 0x1400dd250.
// ============================================================
// Retail's PathTool vtable (0x140160680) slot +0x28 is 0x1400de930, which is a bare
// JMP to 0x1400dd250 — i.e. the tool's Shutdown IS "clear the selection highlight".
// Toolbar_ShowGroup calls that slot on the OUTGOING controller before deleting it
// (`(**(code **)(**(longlong **)(param_1 + 0x28) + 0x28))()` in FUN_140102b60), so it
// is what un-highlights the last selected building when the Paths tool is switched
// away or disabled. Our vtable had `Tool_Ret0` in that slot, so the highlight stayed
// lit on the last building forever. Reported live 2026-08-20.
void PathTool::Shutdown() {
    ClearHighlight();
}
