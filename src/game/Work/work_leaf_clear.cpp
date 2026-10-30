// src/game/Work/work_leaf_clear.cpp
// Work-gap leaf batch: closure-safe exe sub-helpers referenced from
// src/game/Work/{cleararea,harvestarea,plantarea}.cpp.
//
// Like the other work_base_*.cpp files, these are exe helpers resolved via
#include "Work/exe_bodies.h"
// ResolveAddr(Module::Exe, 0xADDR) from the Work class reimpls. They resolve to 0
// (silently skipped) unless registered. This file provides faithful 1:1 ports so the call
// sites fire real logic in the standalone exe. A single registration point lives
// in standalone (see lead-integrated reg_*.cpp).
//
// Each function is a 1:1 port of the decompiled exe helper; do not "improve"
// behavior. Struct field offsets match the decompile EXACTLY (this[0x18] = byte
// offset +0x18, BST node children left=+0x18 / right=+0x20, etc.).
//
// CLOSURE-SAFETY: every exe-code sub-callee below is either already registered
// (snapshot-verified), a sibling ported in this file, or a Runtime engine export
// reachable through engine_funcs.h globals. The three big Action-construction
// ctors that several of these call sites ALSO reference (PerformWork ctor
// 0x1400bffd0, ShowTool ctor4 0x1400c1530, ShowTool ctor3 0x1400bdd20) are
// DEFERRED — their sub-callee closure (action copy-ctors 0x1400c59d0 / 0x1400c63b0
// / 0x1400ca520, Entity_impl 0x1400c29a0, ShowTool_Action_Ctor 0x140042890) is
// unregistered, so porting them now would build a half-null/drifted action and
// silently corrupt — strictly worse than the safe no-op skip.
//
// Bodies are unconditional so this compiles as-is in the standalone exe.

#include <new>
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Action/PathAvl.h"   // AvlRebalance (0x140013480)
#include "Work/GridTile.h"      // GridTile (0x14009fc00 / 0x14009d7c0)
#include "Player/exe_bodies.h"   // B3R: PlayerPath_FindComponent_3ea0
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include "rt_libm.h"   // RtCrt_Qsort_18007f050: the MSVC CRT qsort (the exe's 0x14003ae10)
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Entity/ComponentEntity.h"       // ENDGAME B.3.9 D5: ComponentEntity::TypeId()
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include "Component/CropFieldComponent.h"
#include "Component/StateMachineComponent.h"
#include "Work/Work.h"          // WorkEntityAssignment / WorkEntitySetNode
#include "Game/Map.h"
#include "collection/Collection.h"

// 0x140003ea0 — entity-resolve behind an action IBP: PlayerPath_FindComponent_3ea0
// (Player/exe_bodies.h), called by name (B3R).

// ============================================================
// Shared exe sub-callees, called by name (B3R)
// ============================================================
// 0x14001ca60 — BST single-node remove + rebalance: WorkCellBST_Remove_ca60 (Work/exe_bodies.h).
// 0x140013480 — BST rebalance/fixup after insert: AvlRebalance (Action/PathAvl.h).

// ============================================================
// 0x14011b390 — PlantArea_helper_b390
// ============================================================
// RCX is the field's CropFieldComponent (its +0x10 entity, +0x58 slot grid; the
// address sits among CropFieldComponent's bodies): a member of that class.
// Reads the entity's StateMachineComponent state byte (the pending state, or the
// input when none is pending). If state == 0x09 AND the field has slots, every slot
// whose state is 0 (empty) becomes 2 (mark the plant slot pending).
// Closure: 0x140003ea0 (FindComponent<StateMachineComponent>). SAFE.
void CropFieldComponent::MarkPlantSlots() {
    StateMachineComponent* machine = _entity->FindComponent<StateMachineComponent>();   // 0x140003ea0

    char state = machine->stateMachine.pendingState;
    if (state < 0) state = machine->stateMachine.currentInput;

    int count = slots._size;
    if (state == '\t' && count > 0) {
        CropSlot* slots = this->slots._data;
        for (int i = 0; i < count; i++) {
            if (slots[i].state == 0) slots[i].state = 2;
        }
    }
}


// ============================================================
// ClearArea's entity -> citizen set (WorkEntitySetNode, Work/Work.h): the exe's
// inlined Collection::Set over WorkEntityAssignment, keyed on the entity pointer
// (compared unsigned). LEFT (the three entry points): the callers (ClearArea,
// WORK-a) hand the set and the record as void*; each body views them once.
// ============================================================
namespace {
using EntitySet = Collection::Set<WorkEntityAssignment>;
}  // namespace

// ============================================================
// 0x1400298a0 — ClearArea_helper_98a0
// ============================================================
// Search-and-remove for the entity-keyed set (the +0x48 entity tree). param_1 =
// the set; param_2 = InternalBasePtr* whose dereferenced 8-byte value is the search
// key. On match calls the shared single-node remove (0x14001ca60: the same link
// layout).
// ABI: void fn(longlong* treeRootSlot, InternalBasePtr* key)
// Closure: 0x14001ca60 (reg) + InternalBasePtr_CheckPtr (engine). SAFE.
void WorkLeaf_BstRemoveByIBP_298a0(EntitySet* set, ComponentEntity* const* key) {
    WorkEntitySetNode* node = set->_root;
    if (node == nullptr) return;
    do {
        ComponentEntity* keyVal = *key;
        if (keyVal < node->element.entity.Get()) {
            node = node->left;       // left (+0x18)
        } else {
            if (*key <= node->element.entity.Get()) {
                WorkCellBST_Remove_ca60(set, node);   // 0x14001ca60
                return;
            }
            node = node->right;      // right (+0x20)
        }
    } while (node != nullptr);
}

// ============================================================
// 0x140029b40 — ClearArea_helper_9920_sub_9b40 (inlined leaf)
// ============================================================
// Allocates + inits a 0x30-byte node copying the 16-byte pair from `pair` and
// storing the parent. Only callee is MemAlloc (engine). Ported inline so 9920's
// closure is self-contained.
static WorkEntitySetNode* WorkLeaf_BstNewNode_9b40(const WorkEntityAssignment* pair, WorkEntitySetNode* parent) {
    WorkEntitySetNode* n = static_cast<WorkEntitySetNode*>(
        System::MemoryAllocator::MemAlloc(0x30, 8));
    if (n == nullptr) return nullptr;
    n->element.entity = nullptr;  n->element.entity = pair->entity;     // key / IBP entity (+0x00)
    n->element.citizen = nullptr; n->element.citizen = pair->citizen;   // IBP work ref     (+0x08)
    n->parent = parent;              // parent  (+0x10)
    n->left = nullptr;               // left  (+0x18)
    n->right = nullptr;              // right (+0x20)
    n->balance = 0;                  // color/balance (+0x28): one dword store in retail
    n->height = 0;
    return n;
}

// ============================================================
// 0x140029920 — ClearArea_helper_9920
// ============================================================
// Insert (entity-keyed) into the +0x48 entity set, returning the inserted /
// existing node through param_2. On empty tree it allocates the root directly;
// otherwise it walks to a leaf, allocates a child via 9b40, then rebalances via
// 0x140013480. The count is incremented on every insert that adds a node.
// ABI: undefined8* fn(longlong* treeRootSlot, undefined8* outNode, InternalBasePtr* ins)
// Closure: 0x140029b40 (inlined) + 0x140013480 (reg) + MemAlloc/CheckPtr (engine). SAFE.
WorkEntitySetNode** WorkLeaf_BstInsertByIBP_29920(EntitySet* set, WorkEntitySetNode** outNode, const WorkEntityAssignment* ins) {
    // Retail's ClearArea::Execute hands its reused Profession* stack slot here; the
    // tree's caller holds a typed node local.
    WorkEntitySetNode** out = outNode;
    WorkEntitySetNode* node = set->_root;
    WorkEntitySetNode* result;

    if (node == nullptr) {
        // Empty tree — allocate root directly (matches exe's open-coded alloc).
        WorkEntitySetNode* n = static_cast<WorkEntitySetNode*>(
            System::MemoryAllocator::MemAlloc(0x30, 8));
        result = nullptr;
        if (n != nullptr) {
            n->element.entity = nullptr;  n->element.entity = ins->entity;
            n->element.citizen = nullptr; n->element.citizen = ins->citizen;
            n->parent = nullptr;
            n->left = nullptr;
            n->right = nullptr;
            n->balance = 0;
            n->height = 0;
            result = n;
        }
        set->_root = result;
        *out = result;
    } else {
        ComponentEntity* insKey = ins->entity.Get();
        WorkEntitySetNode* newChildSlotOwner = nullptr;  // = local_res8 / pivot for rebalance
        for (;;) {
            // descend-left while key < node key (re-reading key each step via CheckPtr)
            while (true) {
                newChildSlotOwner = node;
                insKey = ins->entity.Get();
                if (insKey >= node->element.entity.Get()) break;
                WorkEntitySetNode* left = node->left;  // +0x18
                if (left == nullptr) {
                    result = WorkLeaf_BstNewNode_9b40(ins, node);
                    node->left = result;
                    goto inserted;
                }
                node = left;
            }
            if (ins->entity.Get() <= node->element.entity.Get()) {
                // duplicate found — return existing node; early return, NO insert,
                // NO rebalance, and NO count increment (matches exe early `return`).
                *out = node;
                return outNode;
            }
            WorkEntitySetNode* right = node->right;     // +0x20
            if (right == nullptr) {
                result = WorkLeaf_BstNewNode_9b40(ins, node);
                node->right = result;
                goto inserted;
            }
            node = right;
        }
    inserted:
        AvlRebalance(&set->_root, &newChildSlotOwner);   // 0x140013480
        *out = result;
    }
    set->_count = set->_count + 1;
    return outNode;
}

// ============================================================
// 0x140029c10 — ClearArea_helper_9c10
// ============================================================
// Serialize the +0x48 entity set (pairs of InternalPtrs: a ComponentEntity and a
// CitizenComponent). param_1 = the set, param_2 = IO::Serializer*. Writing branch
// (serializer mode != 0): in-order traverse, serialize each pair (with IsDerivedFrom
// type recheck nulling mismatches). Reading branch (mode == 0): read count, then
// loop reading two IBPs per entry and insert via 9920 (sibling).
// ABI: void fn(longlong* treeRootSlot, Serializer* ser)
// Closure: 9920 (sibling) + Serialize(int)/IBP::Serialize/IsDerivedFrom + type
//          IDs (all engine globals). SAFE.
void WorkLeaf_BstSerializeIBPpair_29c10(EntitySet* set, IO::Serializer* ser) {

    int count = set->_count;
    ser->Serialize(count, nullptr);

    // serializer mode lives at *(*ser + 8); 0 => reading.
    int mode = static_cast<int>(ser->GetDirection());   // IO::Serializer::GetDirection, retail 0x180006210

    // ENDGAME B.3.9 D5: read directly through the class's own TypeId().
    const uint32_t tidEntity = ComponentEntity::TypeId();       // DAT_1401c5d70
    const uint32_t tidCitizenAI = CitizenComponent::TypeId();   // DAT_1401c7158

    // LEFT (both branches): the pair's two members are InternalPtrs retail streams
    // through InternalBasePtr::Serialize; they are declared T*.
    if (mode == 0) {
        // ---- reading ----
        if (count <= 0) return;
        int i = 0;
        do {
            WorkEntityAssignment pair;
            pair.entity = nullptr;
            pair.citizen = nullptr;
            pair.entity.InternalBasePtr::Serialize(*ser);
            if (pair.entity.Get() != nullptr && !Object::IsDerivedFromNullSafe(pair.entity.Get(), tidEntity)) {
                pair.entity = nullptr;
            }
            pair.citizen.InternalBasePtr::Serialize(*ser);
            if (pair.citizen.Get() != nullptr && !Object::IsDerivedFromNullSafe(pair.citizen.Get(), tidCitizenAI)) {
                pair.citizen = nullptr;
            }
            // pair {entity, citizen} laid out contiguously for the insert key.
            WorkEntitySetNode* outNode = nullptr;
            WorkLeaf_BstInsertByIBP_29920(set, &outNode, &pair);
            i++;
        } while (i < count);
    } else {
        // ---- writing ---- (in-order traversal, serialize each pair)
        WorkEntitySetNode* root = set->_root;
        if (root == nullptr) return;
        // find leftmost
        WorkEntitySetNode* node = root;
        while (node->left != nullptr) {
            node = node->left;
        }
        while (node != nullptr) {
            // IBP A at node+0x00
            node->element.entity.InternalBasePtr::Serialize(*ser);
            ComponentEntity* a = node->element.entity.Get();
            if (a != nullptr && !Object::IsDerivedFromNullSafe(a, tidEntity)) {
                node->element.entity = nullptr;
            }
            // IBP B at node+0x08
            node->element.citizen.InternalBasePtr::Serialize(*ser);
            CitizenComponent* b = node->element.citizen.Get();
            if (b != nullptr && !Object::IsDerivedFromNullSafe(b, tidCitizenAI)) {
                node->element.citizen = nullptr;
            }
            // in-order successor
            WorkEntitySetNode* right = node->right;
            if (right != nullptr) {
                node = right;
                while (node->left != nullptr) {
                    node = node->left;
                }
            } else {
                WorkEntitySetNode* parent = node->parent;
                if (parent != nullptr && parent->left != node) {
                    while (parent != nullptr && parent->right == node) {
                        node = parent;
                        parent = node->parent;
                    }
                    node = parent;
                } else {
                    node = parent;
                }
            }
        }
    }
}

// The 16-byte tile record GetTilesInArea (0x14009fc00) pushes and
// SortTilesByDistance (0x14009d7c0) sorts is GridTile (Work/GridTile.h); the
// callers' arrays are RawArray<GridTile>.
using GridTileArray = RawArray<GridTile>;

// ============================================================
// 0x14009d7c0 — WorldGrid::SortTilesByDistance (FUN_14009d7c0)
// ============================================================
// For each tile, compute squared grid distance from refCell (param_3) into the
// tile's distance, then qsort ascending by that field. Grid width is the int at
// *param_1. Comparator (exe stub @0x14009d7b0): (int)a[+4] - (int)b[+4].
// ABI: void fn(int* mapGrid, longlong* tileArray, int refCell)
// Closure: qsort (the exe's MSVC CRT qsort 0x14003ae10, hash-identical to the Runtime's
// 0x18007f050 that RtCrt_Qsort_18007f050 ports) + inline comparator. SAFE.
namespace {
int WorkLeaf_TileDistCompare(const void* a, const void* b) {   // LEFT: qsort's comparator signature (int (*)(const void*, const void*)); each points at a 0x10-byte tile record
    int da = static_cast<const GridTile*>(a)->distance;   // CAST-OK: crt
    int db = static_cast<const GridTile*>(b)->distance;   // CAST-OK: crt
    return da - db;   // MOV EAX,[RCX+4]; SUB EAX,[RDX+4]; RET
}
}
void Map::SortTilesByDistance(GridTileArray* arr, int refCell) const {
    int gridW = size;
    int count = arr->size;
    GridTile* base = arr->data;
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            GridTile& e = base[i];
            int tileIdx = e.cell;
            int dy = tileIdx / gridW - refCell / gridW;
            int dx = tileIdx % gridW - refCell % gridW;
            e.distance = dy * dy + dx * dx;
        }
    }
    RtCrt_Qsort_18007f050(base, static_cast<size_t>(count), 0x10, &WorkLeaf_TileDistCompare);   // JMP 0x14003ae10
}

// ============================================================
// 0x14009fc00 — WorldGrid::GetTilesInArea (FUN_14009fc00)
// ============================================================
// Scan the rectangle [x..x+w) x [y..y+h) of the grid, pushing tiles into the array
// param_6. For every cell with an occupant push {cellIdx, 0, occupant}. If param_7
// (collect-empty) is set, for cells whose terrain type lacks bits 0/1 AND whose
// +0x2A byte == 0xFF AND that are not in the Map's +0xE8 set, push {cellIdx, 0, null}.
// ABI: void fn(int* grid, int x, int y, int w, int h, longlong* outArr, char collectEmpty)
// Closure: MemAlloc/MemFree/InternalBasePtr_Ctor (engine). SAFE.
//
// Dynamic-array push (16-byte stride, grow x3 on first realloc past the seed,
// copy-then-free) ported verbatim from the exe's open-coded inline grow.
namespace {
// push one tile `rec` into array `arr`. Faithful to the exe inline.
void WorkLeaf_TileArrPush(GridTileArray* arr, const GridTile* rec) {
    int oldCount = arr->size;
    int newCount = oldCount + 1;
    if (newCount == oldCount) return;            // overflow guard (exe: iVar10!=iVar11)
    GridTile* oldPtr = arr->data;
    arr->size = newCount;
    if (arr->capacity < newCount) {
        arr->capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) {
            arr->capacity = newCount * 3;
        }
        arr->data = static_cast<GridTile*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(arr->capacity) << 4, 8));
    }
    int copyStart = (oldPtr == arr->data) ? oldCount : 0;
    if (copyStart < newCount) {
        const GridTile* srcOld = oldPtr + copyStart;
        for (int idx = copyStart; idx < newCount; idx++) {
            GridTile* dst = arr->data + idx;
            if (dst != nullptr) {
                const GridTile* s = (idx < oldCount) ? srcOld : rec;
                dst->cell = s->cell;
                dst->distance = s->distance;
                dst->entity = nullptr;
                dst->entity = s->entity;
            }
            srcOld += 1;
        }
    }
    if (oldPtr != arr->data) {
        System::MemoryAllocator::MemFree(oldPtr);
    }
}
}

void Map::GetTilesInArea(int x, int y, int w, int h,
                                   GridTileArray* arr, char collectEmpty) const {
    int gridW = size;
    MapCell* cells = this->cells._data;
    int rowEnd = h + y;
    if (y >= rowEnd) return;

    int row = y;
    do {
        for (int col = x; col < x + w; col++) {
            int cellIdx = gridW * row + col;
            const MapCell& cell = cells[cellIdx];
            ComponentEntity* entity = cell.occupant.Get();
            if (entity != nullptr) {
                GridTile rec;
                rec.cell = cellIdx;
                rec.distance = 0;
                rec.entity = entity;
                WorkLeaf_TileArrPush(arr, &rec);
            }

            if (collectEmpty != '\0') {
                uint8_t passByte = cell.terrainType;
                uint32_t mask = 1u << (passByte & 0x1f);
                bool emit;
                if ((mask & 2) == 0 && (mask & 1) == 0 &&
                    cell.field_0x2A == 0xFF) {
                    // Pass/flag condition HOLDS: walk the Map's +0xE8 set (int keys).
                    // Exe: found (cellIdx <= *node) -> EMIT; exhausted -> SKIP.
                    const AvlNode28* bnode = field_0xE8.root;   // key +0x00, left +0x10, right +0x18
                    emit = false;
                    while (bnode != nullptr) {
                        if (cellIdx < bnode->key) {
                            bnode = bnode->left;                          // left  +0x10
                        } else if (cellIdx <= bnode->key) {
                            emit = true;                                  // found -> emit
                            break;
                        } else {
                            bnode = bnode->right;                         // right +0x18
                        }
                    }
                } else {
                    // Pass/flag condition FAILS -> emit (LAB_14009fdce direct).
                    emit = true;
                }
                if (emit) {
                    GridTile rec;
                    rec.cell = cellIdx;
                    rec.distance = 0;
                    rec.entity = nullptr;   // the InternalBasePtr ctor with null
                    WorkLeaf_TileArrPush(arr, &rec);
                }
            }
        }
        row++;
    } while (row < rowEnd);
}
