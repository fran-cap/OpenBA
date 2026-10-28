// src/game/Component/citizenai_growup.cpp
//
// Brick 3b handler wave 4 — faithful 1:1 ports of two CitizenAIComponent state
// handlers plus one private find-helper:
//
//   0x14010c890  CitizenAI_State_GrowUp        (state id 4)
//   0x140115e60  CitizenAI_State_LongTermPlan  (state id 13)  — SHARED: also reached by
//                Gather/Gather2/GrowUp via ResolveAddr(0x140115e60).
//   0x14008f230  Entity_impl_f230 (private GrowUp helper; ported here, not registered
//                separately as a reused symbol — but DOES get a RegisterExeAddr so the
//                handler's `ResolveAddr(0x14008f230)` resolves to it).
//
// ─────────────────────────────────────────────────────────────────────────────
// SELF / CHAIN (verified against the Ghidra decompile + disassembly):
//   param_1 (GrowUp) = CitizenAIComponent. self = param_1+0x38 = sibling CitizenComponent
//     IBP. citizen = *self. citizen+0x238 = workplace/residence entity, citizen+0x278 =
//     CharacterComponent (+0x68 = ModelComponent, whose transform +0x158/+0x15c/+0x160 =
//     world pos), citizen+0x18 = controller-comp IBP.
//   LongTermPlan's param_1 IS the CitizenComponent (the CITIZEN, not the AI component) —
//     GrowUp calls it as LongTermPlan(*(param_1+0x38)). param_1+0x10 = entity, +0x278 =
//     CharacterComponent, +0x30/+0x54/+0xb0 = the action-state flag triplet, +0x278 then
//     +0x48 = the action stack object run by the executor.
//   g_pGameWorldSingleton (DAT_1401c3dd8): +0xf0 = entity-list pair, +0xf8 = map-grid
//     (int* dim at [0]), +0xd8 = Random.
//
//   GrowUp success epilogue (both branches): *self → +0x30=-1, +0x54|=4, +0xb0=0,
//     stamp Time(DAT_1401721fc * desc_walktime) into citizen+0x60, return 1.
//   GrowUp early/adult branch (age > _maxAge 15.0): tail-call LongTermPlan, set
//     param_1+0x2c = 4 (next-state), return 1.
//   GrowUp food-shortage branch (QueryCount < 1): Time(DAT_1401721fc) → citizen+0x60,
//     run dd30 find-best-food-source, scan its storage occupants for the right slot,
//     and if found push a PerformWork(Ctor11) chain (eat) → return 1, else return 0.
//
// ─────────────────────────────────────────────────────────────────────────────
// Registered exe sub-callees (resolved via ResolveAddr; bodies NOT duplicated):
//   0x140006270  Entity_FindComponent_Storage  (reg_work_base WorkBase_FindComponent_6270)
//   0x14012cf10  StorageComponent_QueryCount    (reg_component_people)
//   0x14008dd30  CitizenAI_State_sub_dd30       (reg_citizenai_findtarget)
//   0x140087fb0  Entity_impl_7fb0               (reg_pf_road Pf_EntityAnchorCell_7fb0)
//   0x1400bc790  PerformWork_Action_Ctor11      (reg_citizenai_actionctors)
//   0x14008d590  FUN_14008d590  (find-storage-with-resource)   (reg elsewhere)
//   0x1400c1070  PerformWork_Action_Ctor4       (reg_citizenai_actionctors)
//   0x14012cd80  StorageComponent_impl_cd80     (reg elsewhere)
//   0x1400c4c50  CitizenAI_ActionExecutor_c4c50 (reg_citizenai_executor)
//   --- f230 closure (all registered) ---
//   0x14009f3e0  CharacterComponent_impl_f3e0 == fn_MapGetTerrainPos (mapquery)
//   0x140002ed0  Entity_FindComponent_Position  (reg_work_base WorkBase_FindComponent_2ed0)
//   0x140122e20  Entity_impl_2e20               (reg_pf_road Pf_EntityResolvePos_2e20)
//   0x140008830  FUN_140008830 (cell distance)  (reg_work_base WorkBase_CalcDistance_8830)
//   0x1400a00f0  FUN_1400a00f0 (cell validate)  (reg_work_base WorkBase_ValidateCell_a00f0)
//
// Engine dispatch slots used (all already wired by their owning reimpl batches; resolved
//   here via ResolveSlot for portability):
//   [0x140152ab0] fn_InternalBasePtr_CheckPtr   [0x140152a90] fn_InternalBasePtr_Ctor
//   [0x140152a98] fn_IsDerivedFrom              [0x140152b60] MemAlloc  [0x140152b68] MemFree
//   [0x140152b00] System::Time::Time(Time*,float)  [0x140151320] Math::Vector3::Vector3(Scalar&)
//   (All of the above are also exposed as engine_funcs.h externs; this file uses those
//    externs where they exist and ResolveSlot for the Vector3-from-Scalar ctor.)
//
// SELF-CONTAINED-BRICK type-ids (the DAT_1401c7xxx globals are UNFILLED 0xFFFFFFFF in the
//   standalone build → resolved here via Object::GetTypeIdByName, derived from each global's
//   *_Register/_RegisterType [DATA] writer; scan-result derefs are null-guarded):
//     DWORD_1401c7cb8 -> "RawMaterialDescription"  (RawMaterialDescription_Register@14014f7b5;
//                        it is a *Description*, used by GrowUp's storage-slot scan + cd80 + d590)
//     DWORD_1401c7e78 -> "StorageComponent"        (LongTermPlan's component-type scan filter)
//     DWORD_1401c80e0 -> "WorkPlaceComponent"      (WorkPlaceComponent_RegisterType@14014feb5;
//                        f230's nearest-workplace scan filter)
//
// .rdata float const: DAT_1401721fc (GrowUp food-cooldown / walk-time scale). Read from the
//   exe via ResolveAddr; null-guarded fallback 0.0f.
//


#include <new>
#include "math/Math.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
// EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
// ext.item_priority (class C), the item pick and the finder tiers
#include "ext/item_priority/item_priority.h"
#include "Component/WorkPlaceComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/StorageComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/RawMaterialDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"         // the healer array (m_citizenList)
#include "Component/ComponentDescription.h" // the storage item's sub-descriptions
#include "Component/HealerComponent.h"
#include "Component/InteractComponent.h"
#include "Component/ModelComponent.h"       // the model translation
#include "Game/Map.h"                       // Map::size (m_game)
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

namespace {

// --- registered exe sub-callees (incl. the f230 closure): called by name ---
// --- Math::Vector3::Vector3(const Scalar&) (import slot 0x140151320): Math::Vector3's own ---

static uint32_t s_tid_RawMaterialDescription = 0;  // DWORD_1401c7cb8
static uint32_t s_tid_StorageComponent       = 0;  // DWORD_1401c7e78
static uint32_t s_tid_WorkPlaceComponent     = 0;  // DWORD_1401c80e0
static uint32_t s_tid_CitizenDescription     = 0;  // DWORD_1401c7190 (GrowUp cooldown desc)

static const float s_walkScale = kRdata_1401721fc;  // DAT_1401721fc (75.0)

// Resolves no door: it only caches the type ids.
static bool s_init = false;
static void EnsureInit() {
    if (s_init) return;
    if (s_tid_RawMaterialDescription == 0) s_tid_RawMaterialDescription = RawMaterialDescription::TypeId();
    if (s_tid_StorageComponent == 0)       s_tid_StorageComponent       = StorageComponent::TypeId();
    if (s_tid_WorkPlaceComponent == 0)     s_tid_WorkPlaceComponent     = WorkPlaceComponent::TypeId();
    if (s_tid_CitizenDescription == 0)     s_tid_CitizenDescription     = CitizenDescription::TypeId();
    s_init = true;
}

static inline Map* MapGrid() { return GameUpdateLoop::_singleton->m_game; }
static inline CitizenBehavior* ListPair() { return GameUpdateLoop::_singleton->m_citizenList; }

// model world pos (translation x / y) -> clamped grid cell (row*w + col).
static uint32_t ModelCell(const ModelComponent* model, const Map* mapGrid) {
    int w = mapGrid->size;
    int col = static_cast<int>(model->transform.translation.x + static_cast<float>(w >> 1));
    int row = static_cast<int>(model->transform.translation.y + static_cast<float>(w >> 1));
    if (col < 0) col = 0; if (w <= col) col = w - 1;
    if (row < 0) row = 0; if (w <= row) row = w - 1;
    return static_cast<uint32_t>(w * row + col);
}

// The object an element of a scanned array holds.
static inline Component* ElemObj(const Object::InternalPtr<Component>& e) { return e.Get(); }
static inline Description* ElemObj(Description* e) { return e; }

// Scan a node's object array for the first entry whose GetTypeId() matches typeId,
// returning the derived object (null-guarded). Mirrors the inline
// GetTypeId()/IsDerivedFrom scan repeated throughout these handlers.
//
// THE ARRAY/COUNT PAIR IS NOT THE SAME AT EVERY CALL SITE — it depends on what kind of
// node the caller passes, and retail spells both out:
//
//   +0x30 array / +0x38 count  — a component ENTITY, e.g. inside 0x140115e60:
//       if (0 < *(int *)(lVar4 + 0x38)) {                            // count = +0x38
//         puVar1 = (undefined8*)(*(longlong*)(lVar4 + 0x30) + i*8);  // array = +0x30
//     (the ENT_COMP_ARR/ENT_COMP_CNT pair starttown.cpp names.)
//
//   +0x28 array / +0x30 count  — a storage ROW node, e.g. inside 0x14010c890:
//       if (0 < *(int *)(lVar4 + 0x30)) {                            // count = +0x30
//         ... *(longlong *)(lVar4 + 0x28) + idx*8                    // array = +0x28
//
// HISTORY — this helper was born reading +0x28/+0x30 for everyone, which crashed the
// 0x140115e60 (LongTermPlan) caller. The 2026-08-14 "fix" flipped it to +0x30/+0x38 for
// everyone, which fixed that caller and silently broke the two 0x14010c890 (GrowUp) row
// scans in the other direction. Both edits shared the same false premise: that one pair
// fits every call site. It does not, so the pair is now the caller's to state.
//
// The GrowUp breakage stayed hidden for ten days because it needs a citizen actually
// reaching the eat arm with a stocked storage. Its signature is an ACCESS_VIOLATION
// reading 0x00000005_00000005 — reading 8 bytes at row+0x30 when the row keeps a
// count/capacity int32 PAIR there, so a 5-item/5-capacity row becomes a "pointer".
//
// Retail also CheckPtrs EVERY element slot before dereferencing it — these are
// InternalBasePtrs, and a stale one is exactly what a raw deref turns into a crash.
//
// B.4 P2: the two pairs are the two node classes: ComponentEntity::_components /
// _components._size (+0x30 / +0x38) and ComponentDescription::_subDescs / _subDescCount
// (+0x28 / +0x30). The array and the count are read once, the array first.
//
// B6 row 280: read in State_GrowUp 0x14010caf0-0x14010cb01 `MOV RAX,[R15+0x28]; MOV RCX,[RAX+R14*8];
// MOV RAX,[RCX]; CALL [RAX+0x10]`: no test of the array, the element or its vptr (the tree's three
// skips are gone).
template <class E>
static auto ScanArray(const E* base, int count, uint32_t typeId) -> decltype(ElemObj(*base)) {
    for (int i = 0; i < count; ++i) {
        auto obj = ElemObj(base[i]);
        // vtable+0x10 = GetTypeId()
        uint32_t tid = obj->Type();   // slot 2
        if (tid == typeId) {
            bool ok = Object::IsDerivedFromNullSafe(obj, typeId);
            return ok ? obj : nullptr;
        }
    }
    return nullptr;
}
static Component* ScanForType(const ComponentEntity* node, uint32_t typeId) {
    if (node == nullptr) return nullptr;
    return ScanArray(node->_components._data, node->_components._size, typeId);
}
static Description* ScanForType(const ComponentDescription* node, uint32_t typeId) {
    if (node == nullptr) return nullptr;
    return ScanArray(node->_subDescs, node->_subDescCount, typeId);
}

} // namespace

// ============================================================
// 0x14008f230 — Entity_impl_f230  (private GrowUp helper)
//   Finds the nearest reachable WorkPlaceComponent-bearing entity to a grid cell.
//   param_1 = the CitizenBehavior (GameUpdateLoop::m_citizenList; its healer array
//             +0x70 / count +0x78). param_2 = out InternalBasePtr (best entity).
//   param_3 = cell index (clamped row*w+col origin). param_4 = out Vector3 (best world pos).
// ============================================================
extern "C" void* CitizenAI_Entity_impl_f230(const CitizenBehavior* param_1, ComponentEntity** param_2, uint32_t param_3, float* param_4) {
    EnsureInit();
    if (param_1->healers._size == 0) {
        new (param_2) ::Object::InternalBasePtr(nullptr);
        return param_2;
    }
    Map* map = MapGrid();
    Map* mapGrid = map;
    int w = map->size;
    // worldPos = ((cell % w) - (w>>1 - DAT_140172114), (cell / w) - ..., 0) — but the only
    // use is feeding f3e0 (terrain sample) which the original immediately overwrites with a
    // fresh per-candidate position; the seed call's result is discarded (local_80 zeroed
    // right after). We faithfully zero the running pos and rely on the per-candidate path.
    float bias = static_cast<float>(w >> 1) - kRdata_140172114;   // DAT_140172114 = 0.5
    float seedXY[3] = {
        static_cast<float>(static_cast<int>(param_3) % w) - bias,
        static_cast<float>(static_cast<int>(param_3) / w) - bias,
        0.0f
    };
    float seedTri[4] = {0, 0, 0, 0};
    map->GetTerrainPos(seedTri, seedXY, '\0', nullptr);

    // running best
    float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f;
    int   bestCost = 0xffff;          // local_res8
    ComponentEntity* bestEntity = nullptr;   // lVar12

    int n = param_1->healers._size;
    const Object::InternalPtr<HealerComponent>* arr = param_1->healers._data;
    for (int idx = 0; idx < n; ++idx) {
        ComponentEntity* entity = arr[idx]->_entity.Get();   // *plVar11 + 0x10
        // scan entity's components for a WorkPlaceComponent (_components._size, _components)
        int64_t compArrCount = entity->_components._size;
        const Object::InternalPtr<Component>* compArr = entity->_components._data;
        WorkPlaceComponent* wp = nullptr;
        for (int64_t j = 0; j < compArrCount; ++j) {
            Component* obj = compArr[j].Get();
            uint32_t tid = obj->Type();   // slot 2
            if (tid == s_tid_WorkPlaceComponent) {
                bool ok = Object::IsDerivedFromNullSafe(obj, s_tid_WorkPlaceComponent);
                wp = ok ? static_cast<WorkPlaceComponent*>(obj) : nullptr;
                break;
            }
        }
        WorkPlaceComponent* wpIBP = wp;  // local_98
        // workerCount (+0x60) != 0 → this workplace accepts a worker. A scan miss leaves a
        // null InternalPtr and 0x14008f3d1 `CMP [RAX+0x60],0` reads it unguarded (B6 row 850).
        if (wpIBP->workers._size != 0) {
            // 0x14008f3db-0x14008f40f: FindComponent<InteractComponent> 0x140002ed0 into
            // [RBP-0x31], then InteractNodePosition 0x140122e20 UNCONDITIONALLY into the
            // unconstructed slot [RBP-0x11] (kind 0, index 0, heading null).
            InteractComponent* interact = entity->FindComponent<InteractComponent>();   // 0x140002ed0
            Vector3Slot wposSlot;
            interact->NodePosition(&wposSlot.v, 0, 0, nullptr);
            const Math::Vector3& wpos = wposSlot.v;
            int dim = map->size;
            int ix = static_cast<int>(wpos.x + static_cast<float>(dim >> 1));
            int iy = static_cast<int>(wpos.y + static_cast<float>(dim >> 1));
            if (ix < 0) ix = 0; if (dim <= ix) ix = dim - 1;
            if (iy < 0) iy = 0; if (dim <= iy) iy = dim - 1;
            uint32_t cell = static_cast<uint32_t>(dim * iy + ix);
            uint64_t reachable = mapGrid->ValidateCell(0xe1f, param_3, cell);
            if (static_cast<char>(reachable) != '\0') {
                int cost = mapGrid->CalcDistance(param_3, cell);
                if (cost < bestCost) {
                    bestX = wpos.x; bestY = wpos.y; bestZ = wpos.z;
                    bestEntity = arr[idx]->_entity.Get();
                    bestCost = cost;
                }
            }
        }
    }
    param_4[0] = bestX; param_4[1] = bestY; param_4[2] = bestZ;
    *param_2 = bestEntity;
    return param_2;
}

// ============================================================
// 0x140115e60 — CitizenComponent::LongTermPlan  (AI state id 13; tree: "CitizenAI_State_LongTermPlan")
//   `this` = the CitizenComponent. Builds two parallel dynamic arrays of
//   (storage-entity, world-pos) candidates by scanning the rows of the citizen's own
//   (carry) StorageComponent for matching RawMaterialDescription items, then pushes a
//   PerformWork(Ctor4) chain per candidate and runs the executor.
// ============================================================
void CitizenComponent::LongTermPlan() {
    EnsureInit();
    CitizenBehavior* listPair = ListPair();
    Map* mapGrid = MapGrid();

    // citizen storage component (local_res20)
    StorageComponent* citStor = _entity->FindComponent<StorageComponent>();

    // dynamic array A: entity IBPs (stride 8, alloc newCap << 3). base/cap at local_70/local_68.
    ComponentEntity** arrA = nullptr;  int arrACount = 0; int arrACap = 0;  // local_70 / (int)local_68 / local_68._4_4_
    // dynamic array B: float[3] world positions (stride 0xc). base/cap at local_60/local_58.
    float*   arrB = nullptr;  int arrBCount = 0; int arrBCap = 0;          // local_60 / (int)local_58 / local_58._4_4_

    // citizen home cell (model pos) — uVar15 / local_res18
    const ModelComponent* model = character->modelComponent.Get();
    uint32_t homeCell = ModelCell(model, mapGrid);

    // iterate the citizen-storage rows (slotCount, storageSlots: StorageComponent::Item,
    // 0x10 bytes: _resource, _count).
    int rowCount = (citStor != nullptr) ? citStor->storageSlots._size : 0;
    bool committed = false;        // matched cd80 → break to commit
    for (int row = 0; row < rowCount; ++row) {
        const StorageComponent::Item* rowBase = citStor->storageSlots._data + row;
        int occ = rowBase->_count;
        if (occ == 0) {
            // empty row: nothing to add here; advance.
            continue;
        }
        // search the already-collected array A for a storage whose held resource satisfies
        // this row (StorageComponent_impl_cd80). If satisfied, commit.
        bool satisfied = false;
        for (int k = 0; k < arrACount; ++k) {
            // CheckPtr BEFORE the read — retail is `CheckPtr(pfVar21); lVar4 = *pfVar21;`.
            // Reading first and refreshing after uses the pre-refresh pointer, which is
            // the stale value CheckPtr exists to replace.
            ComponentEntity* entry = arrA[k];
            // find that entry's StorageComponent
            Component* sc = ScanForType(entry, s_tid_StorageComponent);
            Component* scIBP = sc;
            const ComponentDescription* descComp = rowBase->_resource;   // row entity (RawMaterialDescription)
            int amount = rowBase->_count;
            // 0x14012cd80 (was its Pf_StorageCanAccept_cd80 forwarder); sc was found by the
            // StorageComponent type id.
            uint32_t hit = (scIBP) ? static_cast<StorageComponent*>(scIBP)->CanAdd(descComp, amount) : 0;
            if (static_cast<char>(hit) != '\0') { satisfied = true; break; }
        }
        if (satisfied) { committed = true; break; }

        // not satisfied by current set: ask d590 for a new storage candidate.
        float    outVec[3] = {0, 0, 0};      // local_80
        Object::InternalPtr<ComponentEntity> matchIBP = nullptr;   // local_a8
        ComponentEntity* outBest  = nullptr;   // local_98
        // 0x14008d590 (was its Pf_PickBestDropTarget_8d590 forwarder)
        listPair->PickBestDropTarget(&outBest, homeCell,
                           rowBase->_resource, rowBase->_count, '\x01', outVec, &matchIBP);
        if (outBest == nullptr) {
            // d590 found nothing for this row → treat as committed-with-empty (the original
            // jumps to the no-more-rows path that sets up the action push from whatever was
            // collected). We simply continue; arrays retain prior entries.
            continue;
        }

        // grow array A (append outBest as an entity-IBP pair).
        {
            int newCount = arrACount + 1;
            if (newCount != arrACount) {
                if (arrACap < newCount) {
                    int newCap = newCount;
                    if (arrACount != 0) newCap = newCount * 3;
                    arrACap = newCap;
                    ComponentEntity** nb = static_cast<ComponentEntity**>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(newCap) << 3, 8));
                    // copy old
                    // 8-byte stride (one int64 per entry, alloc is newCap << 3) — the
                    // original's pfVar14 += 2 over a float pointer.
                    for (int c = 0; c < arrACount; ++c) {
                        nb[c] = arrA[c];
                    }
                    if (arrA) System::MemoryAllocator::MemFree(arrA);
                    arrA = nb;
                }
                arrA[arrACount] = outBest;
                arrACount = newCount;
            }
        }
        // grow array B (append outVec as float[3]).
        {
            int newCount = arrBCount + 1;
            if (newCount != arrBCount) {
                if (arrBCap < newCount) {
                    int newCap = newCount;
                    if (arrBCount != 0) newCap = newCount * 3;
                    arrBCap = newCap;
                    float* nb = static_cast<float*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(newCap) * 0xc, 4));
                    for (int c = 0; c < arrBCount; ++c) {
                        nb[c * 3 + 0] = arrB[c * 3 + 0];
                        nb[c * 3 + 1] = arrB[c * 3 + 1];
                        nb[c * 3 + 2] = arrB[c * 3 + 2];
                    }
                    if (arrB) System::MemoryAllocator::MemFree(arrB);
                    arrB = nb;
                }
                arrB[arrBCount * 3 + 0] = outVec[0];
                arrB[arrBCount * 3 + 1] = outVec[1];
                arrB[arrBCount * 3 + 2] = outVec[2];
                arrBCount = newCount;
            }
        }
    }

    // commit: if any candidate collected (arrACount != 0 — the original's `if (uVar16 != 0)`),
    // push a PerformWork(Ctor4) per candidate and run the executor.
    if (arrACount != 0) {
        ComponentEntity* firstIBP = nullptr;   // local_90
        new (&firstIBP) ::Object::InternalBasePtr(nullptr);
        CitizenComponent* selfIBP = this;   // local_98 (the InternalBasePtr ctor: one store)

        // Vector3((Scalar&)0) — the first push uses a zero-vector built from local_res10 (=0).
        Math::Scalar zeroScalar(0.0f);
        float vecBuf[3] = {0, 0, 0};   // Vector3 retbuf (local_80)
        ::new (static_cast<void*>(vecBuf)) Math::Vector3(zeroScalar);   // the splat ctor

        CitizenAI_PerformWorkCtor4_c1070(&selfIBP, &firstIBP, vecBuf, homeCell, 1, 1);

        // remaining candidates (the original loops uVar16 times over arrA/arrB).
        for (int i = 0; i < arrACount; ++i) {
            ComponentEntity* entIBP = arrA[i];  // local_res8 = *pfVar11
            CitizenComponent* citizenIBP = this;   // local_res10
            float* destB = &arrB[i * 3];
            CitizenAI_PerformWorkCtor4_c1070(&citizenIBP, &entIBP, destB, homeCell, 0, 0);
        }

        // commit the action-state flags on the citizen and run the executor.
        field_0x30 = -1;
        statusFlags |= 4;
        field_0xB0 = 0;

        System::Time timeStamp{};
        RtSystem_Time_Ctor(&timeStamp, 0.0f);
        CharacterComponent* ch = character.Get();
        if (-1 < ch->actionStack.current) {
            ch->actionStack.Execute(&timeStamp);
        }
        (void)committed;
    }

    // free the dynamic arrays (LAB_140116387 epilogue).
    if (arrB) System::MemoryAllocator::MemFree(arrB);
    arrB = nullptr; arrBCount = 0;
    if (arrA) System::MemoryAllocator::MemFree(arrA);
    arrA = nullptr; arrACount = 0;
}

// ============================================================
// 0x14010c890 — CitizenAIComponent::State_GrowUp  (state id 4)
// ============================================================
uint64_t CitizenAIComponent::State_GrowUp() {
    EnsureInit();
    CitizenBehavior* listPair = ListPair();
    Map* mapGrid = MapGrid();

    float destA[4] = {0, 0, 0, 0};   // local_a0
    float destB[4] = {0, 0, 0, 0};   // local_90

    // the citizen's ModelComponent (translation.z = walk-time src, local_70)
    const ModelComponent* model = citizen->character->modelComponent.Get();
    // model+0x160 (local_70) is read by the original but only the desc->foodInterval*scale
    // path is used for the cooldown below; the raw value is not consumed. home cell from model pos:
    uint32_t homeCell = ModelCell(model, mapGrid);

    // the citizen's own StorageComponent → QueryCount (carrying anything?)
    StorageComponent* own = citizen->_entity->FindComponent<StorageComponent>();
    int count = (own) ? own->QueryCount() : 0;

    if (count >= 1) {
        // Citizen's own storage is non-empty (QueryCount >= 1) → defer to LongTermPlan
        // (the haul-to-storage leg) and queue next state 4. (decompile `else`).
        citizen->LongTermPlan();
        queuedState = 4;
        return 1;  // low byte 1
    }

    // FOOD-SHORTAGE branch (count < 1): stamp the cooldown into timers[4],
    // find the best food source via dd30, then scan its storage for an eatable item.
    System::Time cd{};
    RtSystem_Time_Ctor(&cd, s_walkScale);
    timers[4] = cd;

    ComponentEntity* resEntity = citizen->home.Get();
    uint32_t bGoal;
    if (resEntity == nullptr) {
        bGoal = 0xffffffff;
    } else {
        ComponentEntity* resIBP = resEntity;
        bGoal = EntityAnchorCell(&resIBP, nullptr);
    }

    ComponentEntity* found = nullptr;          // local_b0
    ComponentDescription* outComp = nullptr;   // local_res18
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
    // with item priority active for Health (category 3) the finder runs once per rank, best first, so a
    // better-ranked item in a farther storage wins over a worse one in the nearest (otherwise one search).
    Ext::ItemPriority::FindTiered(Ext::ItemPriority::kHealth, [&]() -> bool {
        found = nullptr;
        outComp = nullptr;
        listPair->FindBestTarget(&found, homeCell, bGoal, &outComp, destA, 3);
        return found != nullptr;
    });
    if (found == nullptr) {
        return 0;   // no source → low byte 0
    }

    // found a source entity. find its StorageComponent (local_b8): 0x140006270 (was its
    // WorkBase_FindComponent_6270 forwarder).
    ComponentEntity* target = found;
    StorageComponent* tStorNode = target->FindComponent<StorageComponent>();
    if (tStorNode == nullptr) {
        return 0;
    }

    // scan the source storage's occupant rows for an eatable RawMaterialDescription item
    // that still has stock (count[+8] - reserved[+0xc] > 0 and the RawMaterialDescription's
    // +0x5c bit13 "edible" flag is set), picking one (random if multiple, biased by +0x58).
    int rowCount = tStorNode->storageSlots._size;
    int eligible = 0;          // (uint)local_res18
    int preferred = -1;        // uVar21 (the +0x58-flagged candidate index, or random)
    const StorageComponent::Item* rowArrBase = tStorNode->storageSlots._data;   // local_b8+0x28
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
    // this fetch picks the item itself (the finder only named the storage), so item priority narrows it here:
    // only the rows of the best rank among the Health items the storage holds stay eligible. Tested once,
    // above the loops.
    const bool prio = Ext::ItemPriority::Active(Ext::ItemPriority::kHealth);
    int bestRank = 0x7fffffff;
    if (prio) {
        for (int row = 0; row < rowCount; ++row) {
            const Description* d = ScanForType(rowArrBase[row]._resource, s_tid_RawMaterialDescription);
            if (!d) {
                continue;
            }
            const RawMaterialDescription* rm = static_cast<const RawMaterialDescription*>(d);
            if (((rm->_flags >> 0xd) & 1) == 0 || rowArrBase[row]._count - rowArrBase[row]._reserve <= 0) {
                continue;
            }
            const int r = Ext::ItemPriority::RankOfMat(Ext::ItemPriority::kHealth, rm);
            if (r < bestRank) {
                bestRank = r;
            }
        }
    }
    for (int row = 0; row < rowCount; ++row) {
        const ComponentDescription* rowEnt = rowArrBase[row]._resource;
        // storage ROW node: the item's ComponentDescription, sub-descriptions +0x28 / count
        // +0x30 (retail 0x14010c890), NOT the +0x30/+0x38 entity pair — see the ScanForType header.
        const Description* rmDesc = ScanForType(rowEnt, s_tid_RawMaterialDescription);
        if (!rmDesc) continue;
        const RawMaterialDescription* rm = static_cast<const RawMaterialDescription*>(rmDesc);
        uint32_t flag = rm->_flags >> 0xd;
        if ((flag & 1) == 0) continue;
        int stock = rowArrBase[row]._count - rowArrBase[row]._reserve;
        if (stock <= 0) continue;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md): only the best rank
        if (prio && !Ext::ItemPriority::Eligible(Ext::ItemPriority::kHealth, rm, bestRank)) {
            continue;
        }
        if (rm->_unknown58 != '\0') {
            preferred = eligible;   // remember the flagged candidate's ordinal
        }
        ++eligible;
    }
    if (eligible == 0) {
        return 0;
    }

    int chosen = preferred;
    if (chosen < 0) {
        // System::Random::Rand(m_random, eligible)
        chosen = GameUpdateLoop::_singleton->m_random->Rand(static_cast<uint32_t>(eligible));
    }

    // second pass: locate the chosen eatable row, build the eat (Ctor11) action.
    int ordinal = 0;
    for (int row = 0; row < rowCount; ++row) {
        const ComponentDescription* rowEnt = rowArrBase[row]._resource;
        // storage ROW node: the item's ComponentDescription, sub-descriptions +0x28 / count
        // +0x30 (retail 0x14010c890), NOT the +0x30/+0x38 entity pair — see the ScanForType header.
        const Description* rmDesc = ScanForType(rowEnt, s_tid_RawMaterialDescription);
        if (!rmDesc) continue;
        const RawMaterialDescription* rm = static_cast<const RawMaterialDescription*>(rmDesc);
        uint32_t flag = rm->_flags >> 0xd;
        if ((flag & 1) == 0) continue;
        int stock = rowArrBase[row]._count - rowArrBase[row]._reserve;
        if (stock <= 0) continue;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md): the same test as pass 1
        if (prio && !Ext::ItemPriority::Eligible(Ext::ItemPriority::kHealth, rm, bestRank)) {
            continue;
        }
        if (ordinal == chosen) {
            ComponentDescription* resCountObj = rowArrBase[row]._resource;
            if (resCountObj != nullptr) {
                // f230 → nearest workplace world pos into destB, entity into local_60.
                ComponentEntity* bestWp[2] = {nullptr, nullptr};   // local_60
                CitizenAI_Entity_impl_f230(listPair, bestWp, homeCell, destB);
                if (bestWp[0] != nullptr) {
                    // Ctor11 holder = the CitizenComponent wrapped
                    // (disasm: local_res8 := param_1; then local_res8 := *(local_res8+0x38)).
                    CitizenComponent* holderCitizen = citizen.Get();
                    ComponentEntity* srcIBP = found;       // local_res18 = lVar4 = local_b0 (food source entity)
                    ComponentEntity* dstIBP = bestWp[0];   // local_res10 (workplace entity from f230)
                    CitizenAI_PerformWorkCtor11_bc790(&holderCitizen, resCountObj, &srcIBP, destA, &dstIBP, destB);

                    // success epilogue: flag triplet on the citizen.
                    CitizenComponent* cz = citizen.Get();
                    cz->statusFlags |= 4;
                    cz->field_0x30 = -1;
                    cz->field_0xB0 = 0;

                    // walk-time-scaled cooldown: desc = cz->_description, require
                    // CitizenDescription (DWORD_1401c7190); wt = foodInterval * DAT_1401721fc.
                    // Stamp → timers[4].
                    Description* cdesc = cz->_description;
                    float wt = 0.0f;
                    if (cdesc && Object::IsDerivedFromNullSafe(cdesc, s_tid_CitizenDescription)) {
                        wt = static_cast<const CitizenDescription*>(cdesc)->foodInterval;
                    }
                    System::Time cd2{};
                    RtSystem_Time_Ctor(&cd2, wt * s_walkScale);
                    timers[4] = cd2;

                    return 1;  // low byte 1
                }
            }
            break;
        }
        ++ordinal;
    }

    return 0;
}

