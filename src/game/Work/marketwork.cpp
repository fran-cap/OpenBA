// src/game/Work/marketwork.cpp
// Clean C++ reimplementation of MarketWork -- all methods.
// Source: Game/Work/MarketWork.cpp (from profile build debug strings)
//
// MarketWork (0x58 bytes) -- Market trading work action.
// Inherits: Object::Internal -> Work (0x48) -> MarketWork
// Vtable: 0x140161710
//
// CRITICAL DIFFERENCE from HerbalistWork/HuntWork/PastureWork:
// MarketWork uses a DIFFERENT BST node layout with 0x38-byte nodes:
//   +0x00: key (InternalBasePtr)
//   +0x08: data pointer (allocated separately, freed in dtor)
//   +0x10: data2
//   +0x18: parent
//   +0x20: left child
//   +0x28: right child
// vs the simpler BST used by other Work classes:
//   +0x00: key, +0x08: parent, +0x10: left, +0x18: right
//
// This means BST traversal, removal, cleanup, and serialization
// use different offsets and different helper functions.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Work/MarketWork.h"
#include "object/TypeInfo.h"
#include "Tutorial/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "system/MemoryAllocator.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "Work/exe_bodies.h"
#include "Action/PathAvl.h"        // AvlTreeHead (the version-0 migration set header)
#include "Action/exe_bodies.h"      // B3R: bodies called by name
#include "Component/exe_bodies.h"   // B3R: bodies called by name
#include "Player/exe_bodies.h"      // B3R: 0x140014b00 / 0x140014b40
#include "Work/marketwork_bst_a9f0.h"
#include "Work/marketwork_bst_aaa0.h"
#include "Work/marketwork_bst_afb0.h"
#include "Work/marketwork_bst_b290.h"
#include "Work/marketwork_bst_b680.h"
#include "Work/marketwork_bst_ba70.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/MarketComponent.h"   // ENDGAME B.3.8 CB3: MarketComponent::TypeId()
#include "Component/MarketDescription.h"   // ENDGAME B.3.8 CB3: MarketDescription::TypeId()
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.8 CB3: RawMaterialDescription::TypeId()
#include "Component/StorageComponent.h"   // ENDGAME B.3.8 CB3: StorageComponent::TypeId()
#include "Component/ConsumeProduceComponent.h"   // ENDGAME B.3.8 CB3: ConsumeProduceComponent::TypeId()
#include "Component/InteractComponent.h"   // ENDGAME B.3.8 CB3: InteractComponent::TypeId()
#include "Component/InteractDescription.h"   // ENDGAME B.3.8 CB3: InteractDescription::TypeId()
#include "Component/MapComponent.h"   // ENDGAME B.3.8 CB3: MapComponent::TypeId()
#include "Component/StorageLocationComponent.h"   // ENDGAME B.3.8 CB3: StorageLocationComponent::TypeId()
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"
#include "collection/Collection.h"
#include "Action/PathSearch.h"   // RawArray (PickBestDropTarget)
#include "Action/PickupStoredRawMaterial.h"   // AddMaterial 0x1400ca230 on the path result

// ============================================================
// Sub-functions, called by name (B3R; the module exe_bodies.h headers)
// ============================================================
//   0x1400296b0  simple BST serialize (v0 format)
//   0x14002b290  MarketBst_b290 -- market BST serialize (v1 format)
//   0x14002a9f0  MarketBst_a9f0 -- insert with data; FOUR args (tree, outIbp, key, vec),
//                as the retail call in MarketWork::Serialize (0x1400f1d70) passes them
//   0x14002aaa0  MarketBst_aaa0 -- ERASE BY KEY (not an insert); slots 6 and 7 thunk to it
//   0x14002ac40  erase by node (unlink, destroy via sub_aee0, AVL retrace)
//   0x14002af40  market tree cleanup (recursive; frees node + data); two args
//   0x1400f7100  validate work entity     0x1400c5400  cancel all work items
//   0x140002e10  find sub-entity          0x140014c30  simple BST node free
// Allocation goes through System::MemoryAllocator.
//
// The Work BASE vtable is 0x140161a80, byte-verified in retail: MarketWork_Ctor
// disassembles to `0x1400f1f8d: LEA RAX,[0x140161a80]` followed by MOV [RBX],RAX.
// This was previously 0x1401612e0, which is not a vtable at all: it is an INTERIOR
// address of FireWork's vtable (starts 0x1401612a0, 13 slots, ends 0x140161308 at the
// "FishingWork" name string) — i.e. slot 8. Ghidra reports ZERO xrefs to 0x1401612e0.


// ============================================================
// MarketWork::Slot6 -- retail 0x1400eff60 (Ghidra: "MarketWork_Draw", a misnomer)
// ============================================================
// Retail body is a 1-line thunk:
//     MarketWork_sub_aaa0(param_1 + 0x48, param_2); return 0;
// i.e. ERASE the entry keyed by param_2, and return false. It is NOT an "add" —
// the old comment here said "Add entity to BST" purely because sub_aaa0 was
// mis-named "insert". Verified 2026-08-22 by decompiling 0x1400eff60.
// (The slot's key parameter is void* in Work.h: LEFT.)
//
int MarketWork::OnComplete(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    MarketBst_aaa0(&_claims, param2);
    return false;
}


// ============================================================
// MarketWork::Slot7 -- retail 0x1400eff50 (Ghidra: "MarketWork_OnCreate", a misnomer)
// ============================================================
// ⚠ FIXED 2026-08-22. Retail slot 7 is:
//     MarketWork_sub_aaa0(param_1 + 0x48, param_2); return;
// — the SAME erase-by-key helper slot 6 calls, differing only in return type
// (void vs bool). This port previously called sub_ac40 (erase-by-NODE) instead,
// which would have handed an InternalBasePtr* to a function that dereferences its
// second argument as a fully-formed 0x38-byte tree node: reading +0x18/+0x20/+0x28
// off a key would have unlinked garbage and corrupted the tree.
// Verified by decompiling 0x1400eff50 and 0x1400eff60 — both call 0x14002aaa0.
//
int MarketWork::OnCancel(CitizenComponent** param2, ComponentEntity** /*arg2*/, int /*arg3*/) {
    MarketBst_aaa0(&_claims, param2);
    return 0;
}


// ============================================================
// MarketWork::Slot8 / CancelAll -- retail 0x1400f1ca0
// ============================================================
// Loops while the claim count (+0x50, signed: JLE / JG) > 0: the leftmost node's
// citizen, its CharacterComponent (+0x278), the action list (+0x48) is cleared.
//
int MarketWork::Cleanup() {
    int count = _claims.count;

    while (count > 0) {
        // Find leftmost node in the claim tree
        ClaimNode* node = _claims.root;
        ClaimNode* leftmost = node;
        while (node != nullptr) {
            leftmost = node;
            node = node->left;
        }

        // Cancel the work item: the claiming citizen's action list
        CitizenComponent* citizen = leftmost->element.citizen.Get();
        citizen->character->actionStack.Clear();

        count = _claims.count;
    }
    return 0;
}


// ============================================================
// MarketWork::Slot9 / RemoveInvalid -- retail 0x1400efd20
// ============================================================
// Iterates the claim tree, validates each entry via FUN_1400f7100,
// collects invalid ones, removes them from tree.
// Returns true if all entries are valid (none removed).
// Uses MarketWork_sub_ac40 for the removal (not CitizenAI_impl_a6e0).
//
int MarketWork::Validate() {
    ClaimNode* root = _claims.root;
    CitizenComponent** removeArr = nullptr;
    uint32_t removeCount = 0;
    uint32_t removeCapacity = 0;

    if (root == nullptr) {
        return true;  // Empty tree -- nothing to validate
    }

    // Find leftmost node (minimum)
    ClaimNode* node = root;
    while (node->left != nullptr) {
        node = node->left;
    }

    // In-order traversal
    while (node != nullptr) {
        CitizenComponent* entityIBP = node->element.citizen.Get();

        // Validate this entity
        bool stillValid = ValidateTask(nullptr, &entityIBP, nullptr);

        if (!stillValid) {
            // Add to removal list -- grow array if needed
            uint32_t newCount = removeCount + 1;
            if (newCount != removeCount) {
                CitizenComponent** newArr = removeArr;
                if ((int)removeCapacity < (int)newCount) {
                    uint32_t newCap = newCount;
                    if (removeCount != 0) {
                        newCap = newCount * 3;
                    }
                    removeCapacity = newCap;
                    newArr = static_cast<CitizenComponent**>(System::MemoryAllocator::MemAlloc((int64_t)newCap * 8, 8));
                }

                // Copy existing entries if buffer changed
                uint32_t copyStart = 0;
                if (removeArr == newArr) {
                    copyStart = removeCount;
                }
                for (uint32_t i = copyStart; i < newCount; i++) {
                    CitizenComponent* val;
                    if (i < removeCount) {
                        val = removeArr[i];
                    } else {
                        val = entityIBP;
                    }
                    newArr[i] = val;
                }
                if (removeArr != newArr) {
                    System::MemoryAllocator::MemFree(removeArr);
                }
                removeArr = newArr;
                removeCount = newCount;
            }
        }

        // In-order successor
        ClaimNode* rightChild = node->right;
        if (rightChild != nullptr) {
            // Go right, then all the way left
            node = rightChild;
            while (node->left != nullptr) {
                node = node->left;
            }
        } else {
            // Go up until we come from a left child
            ClaimNode* parent = node->parent;
            if (parent != nullptr && parent->left != node) {
                // We came from the right -- keep going up
                while (parent != nullptr && parent->right == node) {
                    node = parent;
                    parent = parent->parent;
                }
                node = parent;
            } else {
                node = parent;
            }
        }
    }

    // Remove all invalid entries from the tree (the key compare is unsigned)
    if (removeCount > 0) {
        for (uint32_t i = 0; i < removeCount; i++) {
            // Find and remove the node with this key
            ClaimNode* searchNode = _claims.root;
            while (searchNode != nullptr) {
                if (removeArr[i] < searchNode->element.citizen.Get()) {
                    searchNode = searchNode->left;
                } else {
                    if (removeArr[i] <= searchNode->element.citizen.Get()) {
                        MarketBst_ac40(&_claims, searchNode);
                        break;
                    }
                    searchNode = searchNode->right;
                }
            }
        }
    }


    System::MemoryAllocator::MemFree(removeArr);
    return removeCount == 0;
}


// ============================================================
// MarketWork::Slot10 / GetWorkEntity -- retail 0x1400f1d10
// ============================================================
// The entity's (+0x10) WorkPlaceComponent (FindComponent 0x140002e10), its
// profession (+0x48) into the return slot. IDENTICAL pattern to other Work classes.
//
Profession** MarketWork::GetTarget(Profession** outPtr) {
    WorkPlaceComponent* workPlace = _entity->FindComponent<WorkPlaceComponent>();
    *outPtr = workPlace->profession;
    return outPtr;
}


// ============================================================
// MarketWork::Serialize -- retail 0x1400f1d70
// ============================================================
// Work::Serialize + IO::Serializer(version=1) with version migration.
//
// Version 0 (old format):
//   Deserialize into temp simple BST, then re-insert each entry
//   into self+0x48 using MarketWork_sub_a9f0 (which allocates
//   the 0x38-byte nodes with data fields).
//   Then clean up the temp simple BST.
//
// Version 1+ (new format):
//   Call MarketWork_helper_b290 directly on self+0x48.
//
void MarketWork::Persist(IO::Stream& stream) {
    // Call Work::Serialize base
    Work::Persist(stream);   // 0x1400f7400, a direct call in retail — QUALIFIED here

    // Create serializer with version=1
    IO::Serializer serializer(stream, 1);

    int version = serializer.GetVersion();


    if (version == 0) {
        // OLD format: deserialize into temp simple BST, then migrate
        // (Set<InternalPtr<CitizenComponent>>, the T = 8 node, held as its raw header)
        using SimpleNode = Collection::Set<CitizenComponent*>::Node;
        AvlTreeHead<SimpleNode> tempBST = {};  // root=0, count=0

        // Deserialize using simple BST format
        WorkBST_BST_Serialize_296b0(&tempBST, &serializer);

        // In-order traverse temp BST
        // For each entry, re-insert into MarketWork's BST using market insert
        SimpleNode* tempRoot = tempBST.root;
        if (tempRoot != nullptr) {
            // Find leftmost in temp
            SimpleNode* node = tempRoot;
            while (node->left != nullptr) {
                node = node->left;
            }

            // In-order traversal of simple BST
            while (node != nullptr) {
                // Re-insert this key into MarketWork's BST.
                // Retail (0x1400f1d70) passes FOUR args:
                //   out  = a 2-qword stack InternalBasePtr receiving the inserted node
                //   key  = the v0 node itself (its +0x00 IS the key a9f0 reads)
                //   vec  = a zeroed 16-byte payload-array descriptor {data=0, count/cap=0},
                //          i.e. the migrated entry starts with an EMPTY payload; retail then
                //          "frees" it with MemFree(nullptr), a deliberate no-op on the
                //          always-null data pointer of that temporary.
                ClaimNode* outIbp = nullptr;
                CarryList vecDesc = {nullptr, 0, 0};
                MarketBst_a9f0(&_claims, &outIbp, &node->element, &vecDesc);
                vecDesc.data = nullptr;
                vecDesc.count = 0;
                vecDesc.capacity = 0;

                // In-order successor
                SimpleNode* rightChild = node->right;
                if (rightChild != nullptr) {
                    node = rightChild;
                    while (node->left != nullptr) {
                        node = node->left;
                    }
                } else {
                    SimpleNode* parent = node->parent;
                    if (parent != nullptr && parent->left != node) {
                        while (parent != nullptr && parent->right == node) {
                            node = parent;
                            parent = parent->parent;
                        }
                        node = parent;
                    } else {
                        node = parent;
                    }
                }
            }

            // Clean up temp simple BST
            // Free left subtree, then right subtree, then root
            WorkBase_TreeNodeFree(&tempBST, tempRoot->left);
            WorkBase_TreeNodeFree(&tempBST, tempRoot->right);
            System::MemoryAllocator::MemFree(tempRoot);
        }
    } else {
        // NEW format: call MarketWork_helper_b290 directly
        MarketBst_b290(&_claims, &serializer);
    }
}


// ============================================================
// MarketWork::Execute (slot 4) -- STAGE 1 dependencies
// ============================================================
// Retail 0x1400eff80 is ~1408 decompiled lines and is being landed in stages.
// STAGE 1 is the entry block, 0x1400eff80 .. 0x1400f021f: the guards, the two
// cheap "return 1" rejections and the "citizen is already carrying, go deliver"
// arm that returns 2. Everything past 0x1400f021f (the demand array, the two
// qsorts, the candidate scoring and the FindPath/TransferToCarrier claim) is
// NOT ported yet and is represented by a single "keep walking" return 0.
//
// Ghidra names this MarketWork_Serialize. It is not serialization.
//
// Sub-functions, called by name (B3R; the module exe_bodies.h headers):
//   0x14012e5c0 CalcMaxWorkers   0x140006270 FindComponent_Storage  0x14012cf10 Storage count
//   0x140115e60 LongTermPlan     0x140116540 FreeCarryCapacity      0x14002afb0 MarketBst_afb0
//   0x140014b40 AVL retrace      0x140014b00 tree free              0x14008d590 PickDropTarget
//   0x140122e20 EntityResolvePos 0x140123070 RecursiveChildSum      0x14002b680 / 0x14002ba70 qsorts
//   0x1400bf790 FindPath         0x140087fb0 EntityAnchorCell       0x14009c480 ZoneRefresh
//   0x14009f810 IsDstReachable   0x140132010 sqrtf                  0x14012d5d0 FindBestAdjacentCell
//   0x1400ca230 TransferToCarrier
// DAT_1401c3dd8 is g_pGameWorldSingleton (engine_funcs.h).
//
// ENDGAME B.3.9 D5: every type-id cell Slot4 reads (DAT_1401c44f0 = MarketComponent,
// DAT_1401c4528 = MarketDescription, and the rest below) is read BY CELL through the
// class's own TypeId() -- the old MW_TypeId() wrapper (read the exe .rdata cell,
// fall back to the class static if the cell was 0 or the pre-Startup sentinel
// 0xFFFFFFFF) is gone: the class static IS that cell's value once Factory::Startup
// has run, and nothing here runs before Startup.

// Retail inlines FindComponent<T> at every component scan of slot 4 (eight copies);
// those are ComponentEntity::FindComponent<T>() here.
//
// The other inlined scan shape in this function: a ComponentDescription's
// sub-description array (+0x28 data, +0x30 count) -- no InternalBasePtr, hence no
// CheckPtr (verified at 0x1400f0450). Retail reads it for RawMaterialDescription.
static RawMaterialDescription* MW_FindRawMaterial(ComponentDescription* resource) {
    const uint32_t typeId = RawMaterialDescription::TypeId();
    const int count = resource->_subDescCount;
    for (int i = 0; i < count; ++i) {
        Description* child = resource->_subDescs[i];
        if (child->Type() == typeId) {
            child = resource->_subDescs[i];
            return Object::IsDerivedFromNullSafe(child, typeId)
                ? static_cast<RawMaterialDescription*>(child) : nullptr;
        }
    }
    return nullptr;
}


// ---- STAGE 2 dependencies -------------------------------------------------
// 0x14002afb0 grows the 0x20-byte demand-record array (ported as MarketBst_afb0).
// 0x140014b40 is the SHARED AVL retrace and 0x140014b00 the shared recursive
// free; both operate on the same +0x18/+0x20/+0x28/+0x30 node shape.
//
// ⚠ 0x14002a9f0 is deliberately NOT used by stage 2. It inserts into MarketWork's
// OWN tree at self+0x48, and retail calls it exactly twice, both times as the
// final act of a successful claim (0x1400f0e49 and 0x1400f1bd4). The tree stage 2
// builds is a function-local one whose inserts retail fully inlines -- three
// separate MemAlloc(0x38,8) sites at 0x1400f02d1 / 0x1400f0363 / 0x1400f03a6.
// Routing stage 2 through a9f0 would write the town's demand nodes into the live
// claim registry.
//
// The demand records and their header are MarketWork::Demand / DemandList (MarketWork.h).

// The function-local supply/demand tree. Same 0x38-byte node shape as
// MarketWork's own tree (a Set of a 0x18 element), but +0x08..+0x17 are three
// accumulators instead of a payload array. Retail zeroes that span with two
// 8-byte stores (here the four named fields, in address order).
struct MW_Supply {
    ComponentDescription* resource;  // +0x00 the key: a town resource type
    int32_t accumA;                  // +0x08 loop A, weighted (see below)
    int32_t accumC;                  // +0x0c loop C, town-wide available
    int32_t accumB;                  // +0x10 loop B, in-transit quantity
    int32_t _pad14;                  // +0x14 zeroed with accumB
};
using MW_SupplyNode = Collection::Set<MW_Supply>::Node;
static_assert(sizeof(MW_SupplyNode) == 0x38, "supply node is 0x38 (MemAlloc 0x1400f02d1)");
static_assert(offsetof(MW_SupplyNode, left) == 0x20, "supply node left @ +0x20");
static_assert(offsetof(MW_SupplyNode, balance) == 0x30, "supply node balance @ +0x30");
// The tree header {root, int32 count} the shared retrace 0x140014b40 takes in RCX.
struct MW_SupplyTree {
    MW_SupplyNode* root;
    int32_t        count;
    int32_t        _pad0C;
};

// The stage 5 take list is MarketWork::Take / TakeList (MarketWork.h).

// Retail inlines this descent at all three accumulation sites (0x1400f04c0,
// 0x1400f0607, 0x1400f08e8).
// ⚠ On a miss -- and on an empty tree -- retail leaves the cursor NULL and then
// unconditionally executes ADD [cursor+off],reg. That deref is load-bearing: it
// relies on every resource type reachable from a storage list also being present
// in the town resource registry the tree was built from. Callers below reproduce
// the unguarded write; do not add a null check here. (Keys compare unsigned.)
static MW_SupplyNode* MW_TreeFind(MW_SupplyNode* root, ComponentDescription* key) {
    MW_SupplyNode* cur = root;
    while (cur != nullptr) {
        ComponentDescription* k = cur->element.resource;
        if (key < k)      cur = cur->left;
        else if (k < key) cur = cur->right;
        else              return cur;
    }
    return nullptr;
}

// DIAGNOSTIC ONLY, printed right before the faithful null deref described above fires:
// WHICH storage entity holds WHICH material type that the town resource registry
// (townMgr+0x110) never learned about. Retail's invariant is "every type in any storage
// list was registered"; when it breaks here the culprit is whatever deposited the
// material without RawMaterialComponent_RegisterRawMaterial, not this walk. First seen
// in the BANISHED_STRESS town (2026-09-03) at loop C, frame ~8400.
static void MW_ReportTreeMiss(const char* site, int entityIdx, const void* entityIBP,
                              ComponentDescription* resType, RawMaterialDescription* rawMat,
                              CitizenBehavior* town) {
    char path[160] = "?";
    if (resType) {
        // the hidden return slot GetPath constructs into
        System::Filename fnPath = resType->GetPath();
        const char* s = fnPath._data;
        if (s) {
            size_t i = 0;
            for (; i + 1 < sizeof(path) && s[i] >= 0x20 && s[i] < 0x7f; i++) path[i] = s[i];
            path[i] = '\0';
        }
    }
    const int regCount = town->knownRawMaterials._size;
    fprintf(stderr, "[MarketWork::Execute] TREE MISS at %s: storage entity[%d]=%p holds resType=%p "
            "(%s) rawMatDesc=%p flags=0x%x -- not among the %d types in townMgr+0x110; "
            "about to fault faithfully\n",
            site, entityIdx, entityIBP, static_cast<void*>(resType),
            path, static_cast<void*>(rawMat),
            rawMat ? rawMat->_flags : 0,
            regCount);
    fflush(stderr);
}

// Retail's shared epilogue at 0x1400f1c15: free the local tree (two recursive
// subtree frees plus the root) and the record array, then return. Stage 2 runs
// it on its own exit path because retail runs it on EVERY exit path from
// 0x1400f021f onward -- skipping it would leak a tree and an array per call.
// (0x140014b00 is Avl38FreeSubtree, Player/exe_bodies.h; was its TradeWork_helper_14b00 forwarder.)
static void MW_Stage2Teardown(MW_SupplyTree* tree, MarketWork::DemandList* records) {
    MW_SupplyNode* root = tree->root;
    if (root != nullptr) {
        Avl38FreeSubtree(tree, root->left);
        Avl38FreeSubtree(tree, root->right);
        System::MemoryAllocator::MemFree(root);
    }
    tree->root = nullptr;
    tree->count = 0;
    System::MemoryAllocator::MemFree(records->data);
}


// ============================================================
// MarketWork::Execute -- slot 4, retail 0x1400eff80
// ============================================================
// ⚠ THIS IS THE REAL SLOT 4 — vtable index 4 (+0x20), retail 0x1400eff80.
// A second body, MarketWork_Slot4_Impl, used to sit above claiming the same retail
// address while being dispatched by nothing. It was deleted 2026-08-22: two ports of
// one retail function drift apart and the dead copy accumulates stale notes (its
// header still described the BST block as "BLOCKED", long after that stopped being
// true). See reference_one_retail_fn_ported_twice_diverges.
//
// PORTING SPEC for whoever lands the 1408-line body, recovered from the ONLY call
// site — the citizen idle claim-walk, retail 0x1400f7b60 (ported as Helper_sub_7b60,
// citizenai_housing_idle.cpp:506, vcall at :527):
//
//     uVar2 = (**(code **)(*(longlong *)*plVar4 + 0x20))(work, local_res18, param_1 + 0x130);
//     if (uVar2 == 2) return 1;                       // CLAIMED -> stop the walk
//     if (uVar2 == 1) work[+0x2c] = sched[+8];        // visited-this-tick stamp
//
// so the true signature is THREE args, not the four the deleted copy declared:
//     int Execute(MarketWork* self, InternalBasePtr* citizenHolder, void* ctx /*sched+0x130*/)
// and the return is a TRI-STATE: 2 = claimed (walk stops), 1 = examined-but-declined
// (stamp it, keep walking), 0 = keep walking without stamping. Slot 9 (Validate) is
// called immediately BEFORE this on the same object, unconditionally.
//
// Retail has NO unconditional per-tick call of slot 4 anywhere; it is purely
// claim-driven, so it fires only when a citizen whose profession key (+0x1f0) matches
// this workplace's WPC+0x48 enters AI state Idle.
// ---- STAGE 1 (retail 0x1400eff80 .. 0x1400f021f) --------------------------
int MarketWork::Execute(CitizenComponent** citizenSlot, System::Time* ctx) {
    (void)ctx;   // sched+0x130; retail slot 4 never reads param_3 -- it is only a
                 // decompiler artefact of a caller-saved stack slot reused as a temp.


    // --- 0x1400effd0..0x1400efffc: hauler-slot cap ------------------------
    // EDI = MarketWork+0x50, the claim count = citizens already claimed by this
    // market. self+0x18 is the market's WorkComponent; its +0x30 is a lazily-filled
    // cache of "how many haulers may work here at once", computed by CalcMaxWorkers
    // (0x14012e5c0). Full -> 1 = examined but declined. (The tree has two ports of 0x14012e5c0 that differ; the one
    // called here takes the component as an integer: LEFT.)
    const int claimed = _claims.count;
    WorkComponent* workComp = _workComponent.Get();
    if (workComp->workState == 0) {
        workComp->workState = workComp->CalcMaxWorkers();
    }
    const int maxHaulers = workComp->workState;
    if (maxHaulers <= claimed) {
        return 1;
    }

    // --- 0x1400f0001..0x1400f005c: citizen is already carrying ------------
    // *citizenHolder is the CitizenComponent; its +0x10 is the citizen Entity.
    // If that entity's own StorageComponent holds anything, the citizen must go
    // dump it first: push AI state LongTermPlan and report 2 = CLAIMED, which
    // stops the caller's claim-walk.
    CitizenComponent* citizen = *citizenSlot;
    StorageComponent* carriedStorage = citizen->_entity->FindComponent<StorageComponent>();
    const int carried = carriedStorage->QueryCount();
    if (carried > 0) {
        (*citizenSlot)->LongTermPlan();
        return 2;
    }

    // --- 0x1400f0061..0x1400f0118: the market's MarketDescription ---------
    // self+0x10 is the market Entity: its MarketComponent (retail keeps a strong ref
    // for the rest of the body), then the component's Description narrowed to
    // MarketDescription.
    MarketComponent* marketComp = _entity->FindComponent<MarketComponent>();
    Description* descRaw = marketComp->_description;
    MarketDescription* marketDesc = Object::IsDerivedFromNullSafe(descRaw, MarketDescription::TypeId())
        ? static_cast<MarketDescription*>(descRaw) : nullptr;

    // --- 0x1400f011c..0x1400f0132: the market's own StorageComponent ------
    StorageComponent* marketStorage = _entity->FindComponent<StorageComponent>();

    // --- 0x1400f0132..0x1400f01c5: the citizen's terrain cell index -------
    // The map's size is the edge length; the citizen's CharacterComponent (+0x278)
    // -> ModelComponent (+0x68) carries the world position (transform translation
    // +0x158/+0x15c). Both axes are biased by dim/2, truncated and clamped, then
    // folded into a single cell index. Stage 1 computes it because retail does, and
    // because the value is load-bearing for stage 3/4 (it is the origin handed to
    // the resource query and to FindPath).
    Map* map = GameUpdateLoop::_singleton->m_game;
    const Math::Vector3& pos = (*citizenSlot)->character->modelComponent->transform.translation;

    const int dim = map->size;
    const float half = static_cast<float>(dim >> 1);
    int cellX = static_cast<int>(half + pos.x);
    int cellY = static_cast<int>(half + pos.y);
    if (cellX < 0)    cellX = 0;
    if (cellX >= dim) cellX = dim - 1;
    if (cellY < 0)    cellY = 0;
    if (cellY >= dim) cellY = dim - 1;
    const int cellIdx = cellY * dim + cellX;

    // --- 0x1400f01c8..0x1400f01da: how much the citizen can still carry ---
    // (0x140116540 takes the citizen as an integer: LEFT.)
    const int freeCarry = (*citizenSlot)->FreeStorageCapacity();

    // --- 0x1400f01dd..0x1400f0219: market already full enough -------------
    // fill = QueryCount(marketStorage) / (float)capacity.
    // COMISS/JNC: if fill >= MarketDescription::_fillRatio the market does not want
    // any more stock at all -> 1 = examined but declined.
    const float stocked = static_cast<float>(
        marketStorage->QueryCount());
    const float capacity = static_cast<float>(marketStorage->capacity);
    const float fill = stocked / capacity;
    if (marketDesc->_fillRatio <= fill) {
        return 1;
    }

    // ======================================================================
    // STAGE 2 -- demand records + town supply/demand tree
    // retail 0x1400f021f .. 0x1400f096b
    // ======================================================================

    // --- 0x1400f021f..0x1400f0267: the demand record array ----------------
    // One 0x20-byte record per entry in MarketDescription's per-resource array.
    // afb0 grows the array; retail then seeds only record+0x00 from each
    // description entry's mask, because afb0's default-init leaves +0x00 alone.
    DemandList records = {nullptr, 0, 0};
    MarketBst_afb0(&records, marketDesc->_ratioCount);
    const MarketRatio* descEntries = marketDesc->_ratios;
    for (int i = 0; i < records.count; ++i) {
        records.data[i].resourceMask = descEntries[i].resourceMask;
    }
    Demand* const rec = records.data;

    // --- 0x1400f0267..0x1400f0406: one tree node per town resource type ---
    // The town (CitizenBehavior): entity array +0x00 / count +0x08, and the
    // resource-type registry +0x110 / count +0x118.
    const int marketEntryCount = marketStorage->storageSlots._size;
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;

    MW_SupplyTree tree = {nullptr, 0, 0};
    {
        const int typeCount = town->knownRawMaterials._size;
        for (int i = 0; i < typeCount; ++i) {
            ComponentDescription* key = town->knownRawMaterials._data[i];

            MW_SupplyNode* root = tree.root;
            if (root == nullptr) {
                // 0x1400f02c9: first node.
                MW_SupplyNode* n = static_cast<MW_SupplyNode*>(System::MemoryAllocator::MemAlloc(sizeof(MW_SupplyNode), 8));
                MW_SupplyNode* stored = nullptr;
                if (n != nullptr) {
                    n->element.resource = key;
                    n->element.accumA   = 0;   // +0x08/+0x0c: one qword store in retail
                    n->element.accumC   = 0;
                    n->element.accumB   = 0;   // +0x10/+0x14: one qword store in retail
                    n->element._pad14   = 0;
                    n->parent  = nullptr;
                    n->left    = nullptr;
                    n->right   = nullptr;
                    n->balance = 0;            // +0x30: one dword store in retail
                    n->height  = 0;
                    stored = n;
                }
                tree.root = stored;
            } else {
                // 0x1400f030f: descend to the insertion point. A key that is
                // already present is a no-op AND skips the count increment
                // (retail jumps straight to 0x1400f03f2).
                MW_SupplyNode* cur = root;
                MW_SupplyNode* parent = cur;
                bool duplicate = false;
                bool goLeft = false;
                for (;;) {
                    parent = cur;
                    ComponentDescription* k = cur->element.resource;
                    if (key < k) {
                        MW_SupplyNode* l = cur->left;
                        if (l == nullptr) { goLeft = true; break; }
                        cur = l;
                    } else if (k < key) {
                        MW_SupplyNode* r = cur->right;
                        if (r == nullptr) { goLeft = false; break; }
                        cur = r;
                    } else {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                MW_SupplyNode* n = static_cast<MW_SupplyNode*>(System::MemoryAllocator::MemAlloc(sizeof(MW_SupplyNode), 8));
                MW_SupplyNode* stored = nullptr;
                if (n != nullptr) {
                    n->element.resource = key;
                    n->element.accumA   = 0;
                    n->element.accumC   = 0;
                    n->element.accumB   = 0;
                    n->element._pad14   = 0;
                    n->parent  = parent;
                    n->left    = nullptr;
                    n->right   = nullptr;
                    n->balance = 0;
                    n->height  = 0;
                    stored = n;
                }
                if (goLeft) parent->left = stored;
                else        parent->right = stored;

                // 0x1400f03ea: shared AVL retrace, cursor starts at the PARENT
                // (retail keeps the descent cursor in a stack slot and hands its
                // address over, so the retrace can walk and rewrite it). The retrace
                // (Avl38Retrace, Player/exe_bodies.h).
                MW_SupplyNode* cursor = parent;
                Avl38Retrace(&tree.root, &cursor);   // 0x140014b40
            }
            // 0x1400f03ef
            tree.count += 1;
        }
    }
    MW_SupplyNode* const treeRoot = tree.root;

    // --- 0x1400f0406..0x1400f0525: LOOP A, what this market already holds --
    // StorageComponent items: {resource, count, reserve}.
    // available = count - reserve; unitSize = RawMaterialDescription::_weight.
    // ⚠ The tree accumulator is available * (unitSize * available) -- QUADRATIC.
    // That is not a decompiler artefact: 0x1400f04df is IMUL R10D,R9D where R10D
    // is the available count and R9D is already unitSize*available. The record
    // accumulator on the very next lines gets the plain unitSize*available. The
    // asymmetry is retail's, and it is what makes a market that already stocks a
    // resource outrank the town's supply of it super-linearly.
    for (int i = 0; i < marketEntryCount; ++i) {
        const StorageComponent::Item& entry = marketStorage->storageSlots[i];
        ComponentDescription* resType = entry._resource;

        RawMaterialDescription* rawMat = MW_FindRawMaterial(resType);
        const int available = entry._count - entry._reserve;
        const int32_t mass = rawMat->_weight * available;

        MW_SupplyNode* node = MW_TreeFind(treeRoot, resType);
        if (!node) MW_ReportTreeMiss("loopA(market)", -1, marketStorage, resType, rawMat, town);
        node->element.accumA += available * mass;

        const uint32_t mask = rawMat->_flags;
        for (int r = 0; r < records.count; ++r) {
            if ((rec[r].resourceMask & mask) != 0) rec[r].inMarket += mass;
        }
    }

    // --- 0x1400f0525..0x1400f06c3: LOOP B, what haulers are already bringing
    // In-order walk of MarketWork's OWN claim tree. Each claim's carry list is
    // {resource, count}. Note the tree accumulator here takes the RAW qty while the
    // records take unitSize*qty -- checked at 0x1400f0626 (ADD [RAX+0x10],R8D)
    // versus 0x1400f0648 (ADD [RDX],R9D).
    {
        ClaimNode* node = _claims.root;
        if (node != nullptr) {
            while (node->left != nullptr)
                node = node->left;
        }
        while (node != nullptr) {
            const int n = node->element.carries.count;
            for (int i = 0; i < n; ++i) {
                const Carry& elem = node->element.carries.data[i];
                ComponentDescription* resType = elem.resource;
                const int32_t   qty     = elem.count;

                RawMaterialDescription* rawMat = MW_FindRawMaterial(resType);
                const int32_t mass = rawMat->_weight * qty;

                MW_SupplyNode* tn = MW_TreeFind(treeRoot, resType);
                if (!tn) MW_ReportTreeMiss("loopB(haulers)", -1, nullptr, resType, rawMat, town);
                tn->element.accumB += qty;

                const uint32_t mask = rawMat->_flags;
                for (int r = 0; r < records.count; ++r) {
                    if ((rec[r].resourceMask & mask) != 0) rec[r].inTransit += mass;
                }
            }
            // in-order successor -- same shape as Slot9's walk.
            ClaimNode* right = node->right;
            if (right != nullptr) {
                node = right;
                while (node->left != nullptr)
                    node = node->left;
            } else {
                ClaimNode* parent = node->parent;
                if (parent != nullptr && parent->left != node) {
                    while (parent != nullptr && parent->right == node) {
                        node = parent;
                        parent = parent->parent;
                    }
                }
                node = parent;
            }
        }
    }

    // --- 0x1400f06c3..0x1400f096b: LOOP C, what the town is holding -------
    // Every town entity that has a StorageComponent but NO ConsumeProduceComponent
    // (a producer that eats its own output is not a source) contributes its
    // available stock. Tree and records both take the plain unitSize*available
    // here (0x1400f0907 / 0x1400f0928).
    {
        const int entityCount = town->storageLocations._size;
        for (int e = 0; e < entityCount; ++e) {
            ComponentEntity* entity = town->storageLocations._data[e].Get();
            StorageComponent* storage = entity->FindComponent<StorageComponent>();

            entity = town->storageLocations._data[e].Get();
            ConsumeProduceComponent* consumeProduce = entity->FindComponent<ConsumeProduceComponent>();

            if (consumeProduce != nullptr) continue;   // 0x1400f0812: has a consumer -> skip

            const int n = storage->storageSlots._size;
            for (int i = 0; i < n; ++i) {
                const StorageComponent::Item& entry = storage->storageSlots[i];
                ComponentDescription* resType = entry._resource;

                RawMaterialDescription* rawMat = MW_FindRawMaterial(resType);
                const int available = entry._count - entry._reserve;
                const int32_t mass = available * rawMat->_weight;

                MW_SupplyNode* tn = MW_TreeFind(treeRoot, resType);
                if (!tn) MW_ReportTreeMiss("loopC(town)", e, entity, resType, rawMat, town);
                tn->element.accumC += mass;

                const uint32_t mask = rawMat->_flags;
                for (int r = 0; r < records.count; ++r) {
                    if ((rec[r].resourceMask & mask) != 0) rec[r].inTown += mass;
                }
            }
        }
    }

    // ======================================================================
    // STAGE 3 -- ratio pass, surplus shed (the arm that returns 2), qsort
    // retail 0x1400f096b .. 0x1400f0e8a
    // ======================================================================

    // --- 0x1400f096b..0x1400f09eb: the ratio pass -------------------------
    // The market capacity float from stage 1 is still live in XMM10 across all
    // of stage 2 in retail; here it becomes the denominator.
    // 0x1400f096b MOVSS XMM8,[0x14017214c] -- that .rdata single is byte-verified
    // 1.0f (00 00 80 3F): kRdata_14017214c.
    const float kOne = kRdata_14017214c;
    const float invCapacity = kOne / capacity;            // 0x1400f0978 DIVSS XMM6,XMM10
    for (int r = 0; r < records.count; ++r) {
        const float want = descEntries[r].ratio;
        // (inTransit + inMarket) as a SIGNED int32 before the float convert
        // (CVTDQ2PS at 0x1400f09a5), then scaled by 1/capacity.
        const float stocked =
            static_cast<float>(static_cast<int32_t>(rec[r].inTransit + rec[r].inMarket)) * invCapacity;
        rec[r].stockedFrac  = stocked;             // +0x10
        rec[r].satisfaction = stocked / want;      // +0x14 -- the b680 sort key
        rec[r].shortfall    = static_cast<int32_t>(capacity * want)
                            - static_cast<int32_t>(rec[r].inTransit)
                            - static_cast<int32_t>(rec[r].inMarket);   // +0x1c
    }

    // --- 0x1400f09eb..0x1400f0be0: shed a per-resource SURPLUS ------------
    // ⚠ CORRECTION to my own earlier stage map: this arm is NOT "pick up a loose
    // resource off the ground". It fires when the market holds MORE THAN TWICE
    // its target share of a resource, and it hands the excess to the citizen to
    // carry AWAY. 0x14008d590 with its char flag = 1 calls
    // StorageComponent_impl_cd80(storage, resType, count) at 0x14008da0e -- an
    // "will you ACCEPT n of this?" test -- so it returns the nearest OTHER entity
    // that will take the goods, excluding this market. Verified by decompiling
    // 0x14008d590 end to end.
    //
    // The two magic floats are exe .rdata singles, read at 0x140172174 and
    // 0x140172158: 2.0 (the trigger) and 1.25 (how much to keep).
    constexpr float kSurplusTrigger = 2.0f;    // DAT_140172174
    constexpr float kSurplusKeep    = 1.25f;   // DAT_140172158

    for (int r = 0; r < records.count; ++r) {
        const float want = descEntries[r].ratio;
        if (!(want * kSurplusTrigger <
              static_cast<float>(static_cast<int32_t>(rec[r].inMarket)) * invCapacity))
            continue;

        // 0x1400f0a3x: how much of this resource the market keeps.
        const int keep = static_cast<int32_t>(want * kSurplusKeep * capacity);

        for (int i = 0; i < marketEntryCount; ++i) {
            const StorageComponent::Item& entry = marketStorage->storageSlots[i];
            ComponentDescription* resType = entry._resource;

            RawMaterialDescription* rawMat = MW_FindRawMaterial(resType);
            if ((rawMat->_flags & rec[r].resourceMask) == 0)
                continue;

            // 0x1400f0b02 / 0x1400f0b0c: two signed IDIVs by the unit size.
            const int unitSize   = rawMat->_weight;
            const int surplus    = (static_cast<int32_t>(rec[r].inMarket) - keep) / unitSize;
            const int entryAvail = entry._count - entry._reserve;
            int take = freeCarry / unitSize;
            if (entryAvail < take) take = entryAvail;   // CMOVL  0x1400f0b1e
            if (take > surplus)    take = surplus;      // CMOVG  0x1400f0b25
            if (take <= 0) continue;

            // 0x1400f0b2e: destPos is a 12-byte Vector3 out-param -- three dwords,
            // written by 0x14008d590 via param_7[0..2]. Deliberately a SEPARATE
            // local from the a9f0 payload descriptor below: retail reuses one
            // stack slot for both and re-zeroes 16 bytes before the a9f0 call, so
            // aliasing them here would leave 4 bytes of position data in the
            // descriptor's capacity field. (0x14008d590 takes the town, the resource
            // and the position by name.)
            float destPos[3] = {0.0f, 0.0f, 0.0f};
            Object::InternalPtr<ComponentEntity> exclude = _entity;  // this market
            ComponentEntity* found = nullptr;
            town->PickBestDropTarget(
                               &found,
                               static_cast<uint32_t>(cellIdx), resType, take, '\x01',
                               destPos, &exclude);   // 0x14008d590
            if (found == nullptr) continue;   // 0x1400f0b86: nobody will take it, keep scanning

            // ============ THE CLAIM (retail returns 2 from here) ============
            // 0x1400f0be3: a random interact point on the MARKET, weighted by the
            // InteractDescription's use-node count plus each part's recursive sum.
            InteractComponent* interact = _entity->FindComponent<InteractComponent>();
            Description* icDescRaw = interact->_description;
            InteractDescription* icDesc = Object::IsDerivedFromNullSafe(icDescRaw, InteractDescription::TypeId())
                ? static_cast<InteractDescription*>(icDescRaw) : nullptr;
            uint32_t weight = static_cast<uint32_t>(icDesc->_useNodes.count);
            const int icChildren = interact->useLocations._size;
            for (int k = 0; k < icChildren; ++k) {
                weight += interact->useLocations[k].Get()->NodeCount(0);   // 0x140123070
            }
            const uint32_t roll = GameUpdateLoop::_singleton->m_random->Rand(weight);
            Math::Vector3 marketPos;
            interact->NodePosition(&marketPos, 0, static_cast<int>(roll), nullptr);   // 0x140122e20

            // 0x1400f0d57..0x1400f0ddc: build the citizen's action chain.
            // Arg 8 is the resource TYPE ENTITY POINTER, full 64 bits -- see the
            // engine_funcs.h note; it was truncated to 32 once before and crashed.
            ComponentEntity* pathDest = found;                    // where to deliver
            ComponentEntity* pathSrc = _entity.Get();                  // this market
            CitizenComponent* pathWalker = *citizenSlot;
            ::Object::InternalPtr<Action> pathOut;   // retail discards this handle on this arm
            const int pathType = _workId;
            WorkAct_GatherRawMatCtor_bf790(&pathOut, &pathWalker, pathType, &pathDest, destPos,
                          &pathSrc, &marketPos.x, resType, take,
                          0, 1, '\0', 0, 1);

            // 0x1400f0de1..0x1400f0e2a: stamp the citizen.
            const int pathTypeAfter = _workId;
            CitizenComponent* citizen3 = *citizenSlot;
            Profession* tgtVal = nullptr;
            GetTarget(&tgtVal);                     // 0x1400f0e03 MOV RAX,[RAX]
            citizen3->lastWorkId   = pathTypeAfter;
            citizen3->statusFlags |= Bits(CitizenStatus::TaskDone);
            citizen3->lastErrand   = CitizenErrand::None;
            if (tgtVal == citizen3->profession && pathTypeAfter >= 0) {
                citizen3->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
            }

            // 0x1400f0e2a..0x1400f0e51: record the claim in MarketWork's OWN tree.
            // ⚠ The payload descriptor is ZEROED on this arm -- 0x1400f0e2a writes
            // two qwords of 0 into the slot before LEA R9. It is the S5 arm at
            // 0x1400f1bd4 that passes a real take list (&local_100). Passing a real
            // payload here would be wrong. The MemFree(nullptr) that follows at
            // 0x1400f0e51 is retail's deliberate no-op on that always-null pointer,
            // exactly as in the v0 save-migration path in Serialize above.
            CarryList vecDesc = {nullptr, 0, 0};
            ClaimNode* claimNode = nullptr;
            MarketBst_a9f0(&_claims, &claimNode, citizenSlot, &vecDesc);
            System::MemoryAllocator::MemFree(nullptr);
            vecDesc.data = nullptr;
            vecDesc.count = 0;
            vecDesc.capacity = 0;


            MW_Stage2Teardown(&tree, &records);
            return 2;
        }
    }

    // --- 0x1400f0e6e..0x1400f0e82: sort the demand records ----------------
    // Ascending by the float at +0x14 (stocked fraction / want), so the LEAST
    // satisfied resource ends up first. Stage 4 consumes this order.
    if (records.count > 1) {
        MarketBst_b680(records.data, records.count);
    }

    // ======================================================================
    // STAGE 4 -- pick the building to fetch from.  retail 0x1400f0e87..0x1400f13a3
    // ======================================================================
    // 0x1400f0e87/0x1400f0e97 also seed the epilogue's default return value of 1,
    // which is what this function yields when no candidate wins.
    int  result     = 1;      // uVar29 / R10D
    int  bestIndex  = -1;     // local_160, 0x1400f0e87 MOV dword [RBP-0x68],0xffffffff
    float bestScore = 0.0f;   // 0x1400f0e8e XORPS XMM6,XMM6
    const int townEntityCount = town->storageLocations._size;

    for (int e = 0; e < townEntityCount; ++e) {
        Object::InternalPtr<ComponentEntity>* const townEntities = town->storageLocations._data;

        StorageComponent* candStorage = townEntities[e]->FindComponent<StorageComponent>();
        MapComponent* candMap = townEntities[e]->FindComponent<MapComponent>();
        (void)candMap;

        // 0x1400f1013: the candidate's anchor cell index.
        ComponentEntity* anchorIBP = townEntities[e].Get();
        const int candCell = EntityAnchorCell(&anchorIBP, nullptr);

        ConsumeProduceComponent* candCP = townEntities[e]->FindComponent<ConsumeProduceComponent>();

        // 0x1400f10bc: a building that consumes its own stock is not a source.
        // (Ghidra renders the other side of this branch as an empty
        // `else { LAB_1400f135b: }` -- there is no else body, it is the loop
        // increment falling through.)
        if (candCP != nullptr) continue;

        // 0x1400f10ce: recompute the region ids at cell+0x26. ⚠ ONE argument --
        // RCX only. Ghidra prints a 4-arg prototype; the extra three are stale
        // registers, and the body takes only param_1.
        map->FillZones();   // 0x14009c480

        // --- 0x1400f10d3..0x1400f1156: the passability / region test ---------
        // Cell records live at map->cells, stride 0x30 (LEA r,[r+r*2]; SHL 4).
        const MapCell& cellSrc = map->cells.GetData()[cellIdx];
        const MapCell& cellDst = map->cells.GetData()[candCell];
        const uint32_t bitDst = Bits(PathBitOf(cellDst.terrainType));
        const uint32_t bitSrc = Bits(PathBitOf(cellSrc.terrainType));
        const uint32_t mask   = (bitSrc | bitDst) | Bits(PathBits::Walk);
        // ⚠ These two tests are PROVABLY DEAD in retail: `mask` is OR'd with both
        // bits before being TESTed against each of them, so TEST is always
        // non-zero and neither JZ is ever taken (0x1400f1119 / 0x1400f1121).
        // Kept because they are what the listing does and they cost nothing --
        // do not "clean them up", they are the evidence.
        if ((mask & bitSrc) == 0) continue;
        if ((mask & bitDst) == 0) continue;
        // 0x1400f112a: 0xe1f contributes no bits to 0x1e0, so this is non-zero
        // only when one of the two cells' own layer bit lands in 5..8. When it
        // does, the pair is accepted outright; otherwise both cells must carry a
        // non-zero zone and the two zones must match.
        if ((mask & Bits(PathBits::Zoneless)) == 0) {
            const uint16_t regionSrc = cellSrc.zone;
            if (regionSrc == 0) continue;
            const uint16_t regionDst = cellDst.zone;
            if (regionDst == 0) continue;
            if (regionSrc != regionDst) continue;
        }

        // --- 0x1400f1156..0x1400f127f: how much this candidate could supply ---
        // record+0x18 is scratch, zeroed per candidate then filled with the
        // candidate's own stock of each demand resource.
        const int candEntries = candStorage->storageSlots._size;
        for (int r = 0; r < records.count; ++r) rec[r].stage4Scratch = 0;

        for (int i = 0; i < candEntries; ++i) {
            const StorageComponent::Item& centry = candStorage->storageSlots[i];
            RawMaterialDescription* crm = MW_FindRawMaterial(centry._resource);
            const uint32_t cmass = static_cast<uint32_t>(
                (centry._count - centry._reserve) * crm->_weight);
            const uint32_t cmask = crm->_flags;
            for (int r = 0; r < records.count; ++r) {
                if ((rec[r].resourceMask & cmask) != 0) rec[r].stage4Scratch += cmass;
            }
        }

        // --- 0x1400f127f..0x1400f1355: distance-weighted score ----------------
        // f810 is the real path cost; a negative result means "no path known", and
        // retail falls back to map-edge + euclidean cell distance.
        int dist = map->PathDistance(static_cast<uint32_t>(candCell),
                                     static_cast<uint32_t>(cellIdx), '\x01');   // 0x14009f810
        if (dist < 0) {
            const int dim2 = map->size;
            const int dx = (candCell % dim2) - (cellIdx % dim2);
            const int dy = (candCell / dim2) - (cellIdx / dim2);
            dist = static_cast<int>(WorkImpl_Sqrtf_132010(static_cast<float>(dy * dy + dx * dx))) + dim2;
        }
        float distWeight = kOne;
        if (dist > 0) distWeight = kOne / static_cast<float>(dist);

        // Rank weight 1/(r+1) over the b680-SORTED record order: the least
        // satisfied resource is index 0 and counts fullest. This is the only
        // consumer of that sort, so a wrong b680 shows up here as a wrong winner.
        float value = 0.0f;
        for (int r = 0; r < records.count; ++r) {
            uint32_t amount = static_cast<uint32_t>(freeCarry);
            if (static_cast<int32_t>(rec[r].stage4Scratch) < freeCarry)
                amount = rec[r].stage4Scratch;
            value += (kOne - rec[r].satisfaction) *
                     static_cast<float>(static_cast<int32_t>(amount)) *
                     (kOne / (static_cast<float>(r) + kOne));
        }
        if (bestScore < value * distWeight) {
            bestIndex = e;
            bestScore = value * distWeight;
        }
    }

    // ======================================================================
    // STAGE 5 -- take list, path, transfer, claim.  retail 0x1400f13a3..0x1400f1c15
    // ======================================================================
    if (bestIndex >= 0) {   // 0x1400f0e8a-era check: local_160 < 0 -> straight to epilogue
        ComponentEntity* const winner = town->storageLocations._data[bestIndex].Get();
        StorageComponent* winStorage = winner->FindComponent<StorageComponent>();

        // --- 0x1400f1425..0x1400f16a0: build the take list ------------------
        // 0x18-byte records: resource type, rank (the ba70 sort key, SIGNED
        // int32), available, demand-record index.
        TakeList takeList = {nullptr, 0, 0};
        const int winEntries = winStorage->storageSlots._size;

        for (int i = 0; i < winEntries; ++i) {
            const StorageComponent::Item& wentry = winStorage->storageSlots[i];
            ComponentDescription* wresType = wentry._resource;
            RawMaterialDescription* wrm = MW_FindRawMaterial(wresType);
            const uint32_t wmask = wrm->_flags;

            // First demand record this resource satisfies AND that the market is
            // not already double-stocked on. ⚠ RETAIL QUIRK, ported as-is: the
            // `want` here is read from MarketDescription entry [r] using the
            // record's CURRENT index, but the records were reordered by b680 at
            // 0x1400f0e82. So after the sort this pairs record[r] with the
            // description entry that was at position r before sorting. That is
            // what the listing does (the desc offset advances by 8 in lockstep
            // with the record pointer advancing by 0x20); it is not a port bug.
            int matched = -1;
            for (int r = 0; r < records.count; ++r) {
                if ((rec[r].resourceMask & wmask) == 0) continue;
                const float lim = descEntries[r].ratio * kSurplusTrigger;
                if (rec[r].stockedFrac < lim) matched = r;
                break;   // 0x1400f1660: first mask hit ends the scan either way
            }
            if (matched < 0) continue;

            MW_SupplyNode* tn = MW_TreeFind(treeRoot, wresType);
            // 0x1400f159f/0x1400f15a2: node accumB (in-transit qty) + accumA
            // (the quadratic market-weighted term from loop A), added unsigned.
            const int32_t rank = static_cast<int32_t>(
                static_cast<uint32_t>(tn->element.accumB) +
                static_cast<uint32_t>(tn->element.accumA));
            const int32_t avail = wentry._count - wentry._reserve;

            const int32_t oldCount = takeList.count;
            const int32_t newCount = oldCount + 1;
            Take* oldData = takeList.data;
            takeList.count = newCount;
            if (takeList.capacity < newCount) {
                int32_t cap = newCount;
                if (oldCount != 0) cap = newCount * 3;
                takeList.capacity = cap;
                takeList.data = static_cast<Take*>(
                    System::MemoryAllocator::MemAlloc(static_cast<int64_t>(cap) * 0x18, 8));
            }
            int32_t start = (oldData == takeList.data) ? oldCount : 0;
            for (int32_t k = start; k < newCount; ++k) {
                Take* dst = &takeList.data[k];
                if (k < oldCount) {
                    *dst = oldData[k];
                } else {
                    dst->resource = wresType;
                    dst->rank     = rank;
                    dst->avail    = avail;
                    dst->record   = matched;
                    dst->_pad14   = 0;
                }
            }
            if (oldData != takeList.data) System::MemoryAllocator::MemFree(oldData);
        }

        // 0x1400f16a0: ascending by the signed int32 at +0x08.
        if (takeList.count > 1) MarketBst_ba70(takeList.data, takeList.count);

        // --- 0x1400f16a5..0x1400f18c0: two interact points -------------------
        InteractComponent* mic = _entity->FindComponent<InteractComponent>();
        Description* micDescRaw = mic->_description;
        InteractDescription* micDesc = Object::IsDerivedFromNullSafe(micDescRaw, InteractDescription::TypeId())
            ? static_cast<InteractDescription*>(micDescRaw) : nullptr;
        uint32_t weight = static_cast<uint32_t>(micDesc->_useNodes.count);
        const int micChildren = mic->useLocations._size;
        for (int k = 0; k < micChildren; ++k) {
            weight += mic->useLocations[k].Get()->NodeCount(0);   // 0x140123070
        }
        const uint32_t roll2 = GameUpdateLoop::_singleton->m_random->Rand(weight);
        Math::Vector3 marketPos2;
        mic->NodePosition(&marketPos2, 0, static_cast<int>(roll2), nullptr);   // 0x140122e20

        float winnerPos[3] = {0.0f, 0.0f, 0.0f};
        StorageLocationComponent* winLoc = winner->FindComponent<StorageLocationComponent>();
        winLoc->FindBestAdjacentCell(static_cast<uint32_t>(cellIdx), winnerPos);   // 0x14012d5d0 (the forwarder dropped roll2)

        // --- 0x1400f18c0..0x1400f1a80: how much of each to actually carry ----
        // {resource type, count} records. This IS the payload a9f0 stores on the
        // claim node, unlike the S3 arm's zeroed descriptor.
        CarryList carry = {nullptr, 0, 0};
        int carryRemaining = freeCarry;
        for (int t = 0; t < takeList.count; ++t) {
            const Take& te = takeList.data[t];
            ComponentDescription* tType = te.resource;
            const int32_t   tAvail = te.avail;
            const int32_t   tRec   = te.record;

            RawMaterialDescription* trm = MW_FindRawMaterial(tType);
            const int unit = trm->_weight;
            int take = carryRemaining / unit;
            if (tAvail < take) take = tAvail;
            const int byDemand = rec[tRec].shortfall / unit;
            if (byDemand < take) take = byDemand;
            if (take <= 0) continue;

            const int32_t oldCount = carry.count;
            const int32_t newCount = oldCount + 1;
            Carry* oldData = carry.data;
            carry.count = newCount;
            if (carry.capacity < newCount) {
                int32_t cap = newCount;
                if (oldCount != 0) cap = newCount * 3;
                carry.capacity = cap;
                carry.data = static_cast<Carry*>(
                    System::MemoryAllocator::MemAlloc(static_cast<int64_t>(cap) * 0x10, 8));
            }
            int32_t start = (oldData == carry.data) ? oldCount : 0;
            for (int32_t k = start; k < newCount; ++k) {
                Carry* dst = &carry.data[k];
                if (dst == nullptr) continue;   // retail's own per-element guard
                if (k < oldCount) {
                    *dst = oldData[k];
                } else {
                    dst->resource = tType;
                    dst->count    = take;
                    dst->_pad0C   = 0;
                }
            }
            if (oldData != carry.data) System::MemoryAllocator::MemFree(oldData);

            // 0x1400f1a5x: consume both budgets in MASS, not in units.
            const int mass = take * unit;
            carryRemaining -= mass;
            rec[tRec].shortfall -= mass;
        }

        // --- 0x1400f1a80..0x1400f1bd9: the claim ------------------------------
        if (carry.count != 0) {
            // Retail builds each FindPath argument as InternalBasePtr() + Copy
            // (0x140152ac0 / 0x140152ab8: an 8-byte copy, not refcounted).
            ComponentEntity* pathWinner = winner;
            ComponentEntity* pathMarket = _entity.Get();
            CitizenComponent* pathCitizen = *citizenSlot;

            // Note the direction: SRC is the winner (pick up there), DEST is the
            // market (drop off here) -- the mirror of the S3 shed arm. Args 8/9 are
            // 0 and -1 here, not a material template and a count.
            ::Object::InternalPtr<Action> pathOut2;
            const int pathType2 = _workId;
            WorkAct_GatherRawMatCtor_bf790(&pathOut2, &pathCitizen, pathType2, &pathMarket, &marketPos2.x,
                          &pathWinner, winnerPos, nullptr, -1, 0, 1, '\0', 0, 1);

            CitizenComponent* citizen5 = *citizenSlot;
            Profession* tgtVal2 = nullptr;
            GetTarget(&tgtVal2);
            citizen5->lastWorkId   = pathType2;
            citizen5->statusFlags |= Bits(CitizenStatus::TaskDone);
            citizen5->lastErrand   = CitizenErrand::None;
            if (tgtVal2 == citizen5->profession && pathType2 >= 0) {
                citizen5->happinessFlags |= CitizenComponent::kHappinessWorkedOwnProfession;
            }

            // 0x1400ca230 (was its ActChild_TransferToCarrier_ca230 forwarder): the path
            // result is the PickupStoredRawMaterial 0x1400bf790 built.
            for (int k = 0; k < carry.count; ++k) {
                const Carry& ce = carry.data[k];
                static_cast<PickupStoredRawMaterial*>(pathOut2.Get())->AddMaterial(ce.resource, ce.count);
            }

            ClaimNode* claimNode = nullptr;
            MarketBst_a9f0(&_claims, &claimNode, citizenSlot, &carry);
            result = 2;

        }

        // 0x1400f1be8/0x1400f1bfe: both S5 arrays are freed here, before the
        // shared epilogue. The carry array is freed even on the claiming path --
        // a9f0 copies the payload into the node, it does not take ownership.
        System::MemoryAllocator::MemFree(carry.data);
        System::MemoryAllocator::MemFree(takeList.data);
    }


    MW_Stage2Teardown(&tree, &records);
    return result;
}


// ============================================================
// MarketWork::DtorBody (called from ScalarDtor)
// ============================================================
// Sets vtables down chain, frees tree nodes at +0x48 using
// FUN_14002af40 (which also frees each node's carry list), then
// calls Work -> Internal dtor chain.
//
// CRITICAL: Uses FUN_14002af40 for tree cleanup, NOT the simple
// TreeNodeCleanup used by HerbalistWork. FUN_14002af40 recurses
// via left(+0x20) and right(+0x28), frees data at +0x08.
//
// MarketWork dtor (inlined in retail slot 0 0x14002a910). The stamps and the `~Internal` call are the compiler's chain.
MarketWork::~MarketWork() {
    // Free the claim tree using market-specific cleanup
    ClaimNode* bstRoot = _claims.root;
    if (bstRoot != nullptr) {
        // FUN_14002af40 recursively frees left, right, then the node's carry list,
        // zeros it, frees the node
        WorkLeaf_MarketTreeCleanup_2af40(&_claims.root, bstRoot->left);
        WorkLeaf_MarketTreeCleanup_2af40(&_claims.root, bstRoot->right);
        // Free root node's carry list and the root itself
        Carry* rootData = bstRoot->element.carries.data;
        if (rootData) {
            System::MemoryAllocator::MemFree(rootData);
        }
        System::MemoryAllocator::MemFree(bstRoot);
    }

    // Zero out the tree root and count
    _claims.root = nullptr;
    _claims.count = 0;

}



// ============================================================
// MarketWork::Slot11 -- returns (this->_field50 > 0), retail 0x140029860
// ============================================================
// 9 bytes, too small to hook.
int MarketWork::GetPriority() {
    return _claims.count > 0 ? 1 : 0;
}



// Ctor 0x1400f1f70: `??0Internal`, the Work base fields, the leaf fields zeroed (the
// two stamps are the compiler's).
MarketWork::MarketWork() : Work(), _claims{nullptr, 0} {
}

// ============================================================
// Reflection, factory and the seam (ENDGAME B.3.8, B38_RULES D4/D5)
// ============================================================
// Before: a hand-built 13-slot `g_MarketWork_Vtable` (slot 1 null, slot 2 a lazily
// cached id), MarketWork_Ctor_Impl (the ctor with both stamps written by hand) and
// REGISTER_TYPE over MarketWork_Factory / _CreateInPlace, plus two vtable-pointer
// statics for the dtor's stamps.

// create 0x14002a8d0: MemAlloc(sizeof, 8), then the ctor.
::Object::Base* MarketWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MarketWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) MarketWork());
}
// Retail's createInPlace is the imported Internal::CreateInPlaceObject (returns null);
// the tree's in-place entry built the object, and that is kept (B8-13).
::Object::Base* MarketWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) MarketWork());
}


#include "Object/factory_registration.h"

DEFINE_EXE_TYPE(MarketWork, "Work")
