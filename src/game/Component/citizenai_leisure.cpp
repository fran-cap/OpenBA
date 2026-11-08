// src/game/Component/citizenai_leisure.cpp
//
// Brick 3b handler wave (Leisure) — faithful 1:1 port of the deepest single
// CitizenAIComponent state handler plus its 3 private helpers:
//
//   0x14010a560  CitizenAI_State_Leisure           (state id 12, ~931 instr)
//   0x1400899d0  CitizenAI_State_Leisure_sub_99d0  (private; tavern/social find-target)
//   0x14002fd50  CitizenAI_State_Leisure_sub_fd50  (private; FindComponent scan, type 0x1c42c0)
//   0x14004a720  CitizenAI_helper_a720             (private; "is this HappinessComponent usable?" predicate)
//
// Leisure makes the citizen do something recreational: it picks a destination tile
// (an InteractComponent-bearing building, e.g. a tavern, or a random nearby walkable
// cell), walks the citizen there, and pushes a PlayAnimation action sequence. Returning
// 1 ("pushed") commits the state; the "no destination" tail returns 0 (low byte clear).
// Transcribed from the Ghidra decompile, offset-by-offset.
//
// B.4 W1 COMP-b: Leisure is a CitizenAIComponent member; `citizen` is the sibling
//   CitizenComponent (its _description = CitizenDescription, IsDerivedFrom 0x1c7190; home;
//   character->modelComponent whose transform +0x158/+0x15c/+0x160 = world pos, raw).
//   Success epilogue writes lastWorkId = -1, statusFlags |= 4, lastErrand = None and
//   leisureTimer = 0, then returns 1.
//
// GameUpdateLoop: m_game (+0xf8) = the Map (size, cells: MapCell stride 0x30),
//   m_random (+0xd8), m_citizenList (+0xf0) = CitizenBehavior (its happiness list).
// The two candidate sets: Component/LeisureCandidateSet.h (B.4 P2 CAI-5).
//
// ALREADY-REGISTERED exe sub-callees (resolved via ResolveAddr; bodies NOT duplicated):
//   0x14009f3e0 CharacterComponent_impl_f3e0 (GridToWorld; writes Vector3, XMM1=2nd ret) [reg]
//   0x14001a8c0 Pf_AvlRebalanceA_a8c0       (AVL retrace; (root, node))               [reg]
//   0x140004680 FUN_140004680               (FindComponent MapComponent, type 0x1c79e0)[reg]
//   0x140014c30 FUN_140014c30               (recursive BST free; (root, node))         [reg]
//   0x14009c480 FUN_14009c480 / Pf_ZoneRefresh (zone refresh; (grid,?,?,int))          [reg]
//   0x1400a00f0 FUN_1400a00f0               (cell reachability test; (grid,mask,from,to))[reg]
//   0x1400ba6b0 PlayAnimation_Action_Ctor4  (push PlayAnimation; 5-arg)                [reg]
//   0x140002ed0 Entity_FindComponent_Position (FindComponent; (entity,outIBP))         [reg]
//   0x140122e20 Entity_impl_2e20            (resolve entity world pos -> Vector3)       [reg]
//   0x140087fb0 Entity_impl_7fb0            (entity anchor cell index; (ibp,outVec?))   [reg]
//   0x1401158d0 CitizenComponent_CheckWorkplaceValid ((citizen, &entIBP))              [reg]
//   0x140021a30 CitizenAI_impl_1a30 (AVL/BST insert; (tree,outNode,keyIBP))            [reg]*
//
//   * COLLISION NOTE: 0x140021a30 is registered under the (misleading) name
//     WorkBST_RegisterWork_1a30(uintptr_t*, void*, void*). Verified by decompile that the
//     function AT 0x140021a30 IS CitizenAI_impl_1a30 — a key-ordered AVL insert that
//     reads the key from *param_3 and calls Pf_AvlRebalanceA_a8c0. The 3-arg signature
//     (tree, outNode, key) matches Leisure's use exactly. We call it via ResolveAddr.
//
// SELF-CONTAINED-BRICK type-ids (UNFILLED 0xFFFFFFFF in standalone; resolved via
//   Object::GetTypeIdByName, scan-result derefs null-guarded). TypeNames recovered from
//   each global's *_Register[Type] DATA writer:
//     DWORD_1401c7190 -> "CitizenDescription"  (CitizenDescription_Register)
//     DAT_1401c42f8   -> "HappinessDescription"(HappinessDescription_Register)
//     DAT_1401c42c0   -> "HappinessComponent"  (HappinessComponent_RegisterType)
//     DWORD_1401c79e0 -> "MapComponent"        (MapComponent_RegisterType)
//     DAT_1401c7900   -> "InteractComponent"   (InteractComponent_RegisterType)
//
// .rdata float consts (real exe bit-patterns read via read_memory; hardcoded — pure
// scalars, no indirection):
//     DAT_140172100 = 0.25  (RandFloat threshold: chance to clear the candidate cell)
//     DAT_140172114 = 0.5   (cell-centre bias subtracted from grid -> world)
//     DAT_14017210c = 0.33  (random animation-offset disk radius scale)
//     DAT_1401721b0 = 10.0  (animation-time base, added to RandFloatTol*ageMult)
//     g_pAgeMultiplier (0x140172198) = 5.0 (animation-time age multiplier)
//


#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map, MapCell (m_game)
#include "Entity/CitizenBehavior.h"   // the happiness list (m_citizenList)
#include "Component/MapComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/InteractComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/HappinessDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/HappinessComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/ComponentDescription.h"
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/LeisureCandidateSet.h"   // LeisureHomeNode, LeisureCellNode
#include "Action/PathAvl.h"                  // AvlRebalance<AvlNode28> (0x14001a8c0)
#include "Entity/ComponentEntity.h"
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
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

namespace {

// --- registered exe sub-callees ---

static uint32_t s_tid_CitizenDescription   = 0;  // DWORD_1401c7190
static uint32_t s_tid_HappinessDescription  = 0;  // DAT_1401c42f8
static uint32_t s_tid_HappinessComponent    = 0;  // DAT_1401c42c0
static uint32_t s_tid_MapComponent          = 0;  // DWORD_1401c79e0
static uint32_t s_tid_InteractComponent     = 0;  // DAT_1401c7900

// .rdata float consts (real exe bit-patterns).
static const float kRandThresh   = 0.25f;  // DAT_140172100
static const float kCellBias     = 0.5f;   // DAT_140172114
static const float kDiskScale    = 0.33f;  // DAT_14017210c
static const float kAnimTimeBase = 10.0f;  // DAT_1401721b0
static const float kAgeMult      = 5.0f;   // g_pAgeMultiplier (0x140172198)

static bool s_init = false;
static void EnsureInit() {
    if (s_init) return;
    if (s_tid_CitizenDescription == 0)   s_tid_CitizenDescription   = CitizenDescription::TypeId();
    if (s_tid_HappinessDescription == 0) s_tid_HappinessDescription = HappinessDescription::TypeId();
    if (s_tid_HappinessComponent == 0)   s_tid_HappinessComponent   = HappinessComponent::TypeId();
    if (s_tid_MapComponent == 0)         s_tid_MapComponent         = MapComponent::TypeId();
    if (s_tid_InteractComponent == 0)    s_tid_InteractComponent    = InteractComponent::TypeId();
    s_init = true;
}

static inline Map* MapGrid() { return GameUpdateLoop::_singleton->m_game; }
static inline System::Random* Rng() { return GameUpdateLoop::_singleton->m_random; }

// Virtual GetTypeId: original is `(**(code**)(**slot + 0x10))()` where slot is &entry,
// *slot is the object, **slot is its vtable, vtable+0x10 (index 2) is GetTypeId, called
// with the object as `this`. Returns the runtime type id of the object.
// B6 row 240: retail calls [[slot]+0x10] with no null test (0x14010ac70 / 0x14010ac7c,
// 0x14002fd90 / 0x14002fd9c); the tree's 0xffffffff for a null object is gone.
static inline uint32_t VGetTypeId(const Object::Base* obj) {
    return obj->Type();   // slot 2
}

// The object an element of a scanned array holds.
static inline Component* ElemObj(const Object::InternalPtr<Component>& e) { return e.Get(); }
static inline Description* ElemObj(Description* e) { return e; }

// Scan the array [arr, count) for the first entry whose runtime type id == wantTid;
// returns the IsDerivedFrom-narrowed object or null. Mirrors the inline scan loops in
// the original (the array and the count are read once, by the caller).
template <class E>
static auto ScanArrAt(const E* arr, int count, uint32_t wantTid) -> decltype(ElemObj(*arr)) {
    for (int i = 0; i < count; ++i) {
        if (VGetTypeId(ElemObj(arr[i])) == wantTid) {
            auto obj = ElemObj(arr[i]);
            bool ok = obj->IsDerivedFrom(wantTid);
            return ok ? obj : nullptr;
        }
    }
    return nullptr;
}

// The entity's component array (+0x30, count +0x38): the bottom
// InteractComponent / Happiness / Map scans.
// B6 row 240: no null-entity / null-description return (0x14010ac53 reads [RBX+0x38],
// 0x14010abca [RAX+0x28] with no test).
static Component* ScanArr(const ComponentEntity* node, uint32_t wantTid) {
    int count = node->_components._size;
    return ScanArrAt(node->_components._data, count, wantTid);
}
// The entity description's sub-description array (+0x28, count +0x30)
// (LAB_14010abbc validated-target radius lookup).
static Description* ScanType5(const ComponentEntity* node, uint32_t wantTid) {
    const ComponentDescription* type = node->_description;
    int count = type->_subDescCount;
    return ScanArrAt(type->_subDescs, count, wantTid);
}

} // namespace

// ============================================================
// 0x14004a720 — CitizenAI_helper_a720
// "Is this HappinessComponent usable?" — its description's type (+0x20) must not be 5
// and its registered flag (+0x28) must be set.
// ============================================================
static char CitizenAI_helper_a720(HappinessComponent* param_1) {
    // B6 row 240: 0x14004a73a IsDerivedFrom, 0x14004a744 CMOVNZ RDX,RBX over RDX = 0,
    // 0x14004a748 CMP dword ptr [RDX+0x20],5 with no test, then 0x14004a74e the +0x28 byte.
    Description* obj = param_1->_description;
    HappinessDescription* narrowed = obj->IsDerivedFrom(s_tid_HappinessDescription)
        ? static_cast<HappinessDescription*>(obj) : nullptr;
    if (narrowed->_happinessType != 5 && param_1->registered)
        return 1;
    return 0;
}

// ============================================================
// 0x14002fd50 — CitizenAI_State_Leisure_sub_fd50
// FindComponent scan: walk the entity's component array (+0x30) [count @ +0x38],
// find the first child whose type id == HappinessComponent (DAT_1401c42c0), store the
// IsDerivedFrom-narrowed result (an InternalPtr) at param_2, return param_2.
// ============================================================
static HappinessComponent** CitizenAI_State_Leisure_sub_fd50(ComponentEntity* param_1,
                                                            HappinessComponent** param_2) {
    HappinessComponent* result = nullptr;
    int count = param_1->_components._size;
    const Object::InternalPtr<Component>* arrBase = param_1->_components._data;
    int idx = 0;
    while (idx < count) {
        if (VGetTypeId(arrBase[idx].Get()) == s_tid_HappinessComponent) {
            Component* obj = arrBase[idx].Get();
            if (obj->IsDerivedFrom(s_tid_HappinessComponent))   // 0x14002fdf5
                result = static_cast<HappinessComponent*>(obj);
            break;
        }
        ++idx;
    }
    *param_2 = result;   // the InternalBasePtr ctor: `mov [rcx],rdx`
    return param_2;
}

// ============================================================
// 0x1400899d0 — CitizenAI_State_Leisure_sub_99d0
// Social/tavern find-target. Iterates the town's happiness components (CitizenBehavior
// +0x50, count @ +0x58); for each, finds its entity's InteractComponent (DAT_1401c7900),
// resolves its world pos -> grid cell, measures the path cost, and accumulates the
// reachable, usable candidates into a grown array, then picks one at random.
//
// Writes the chosen component's entity (+0x10) into *param_2, or a null
// InternalBasePtr if no candidate qualifies. Returns param_2.
//   param_3 = source grid cell (citizen pos), param_4 = max distance (in cells, *2 budget)
// ============================================================
// Path cost 0x14009f810 = Map::PathDistance, sqrtf 0x140132010 = WorkImpl_Sqrtf_132010.
static ComponentEntity** CitizenAI_State_Leisure_sub_99d0(CitizenBehavior* param_1, ComponentEntity** param_2,
                                                         uint32_t param_3, int param_4) {

    Map* mapGrid = MapGrid();
    uint32_t outCap = 0;          // (local_50 high dword)
    int matchCount = 0;           // pvVar18 / pvVar19 (entries written)
    Object::InternalPtr<HappinessComponent>* curArr = nullptr;   // pvVar21 / pvVar6 (current array ptr)

    int listCount = param_1->happinessComponents._size;
    if (listCount > 0) {
        int idx = 0;
        do {
            const Object::InternalPtr<HappinessComponent>* slot = &param_1->happinessComponents._data[idx];
            ComponentEntity* entObj = (*slot)->_entity.Get();

            // find the entity's InteractComponent (scan its component array @ entObj+0x30)
            // (the scan's IsDerivedFrom(InteractComponent) result, typed)
            InteractComponent* interact = nullptr;
            interact = static_cast<InteractComponent*>(ScanArr(entObj, s_tid_InteractComponent));
            InteractComponent* interactIBP[2] = {nullptr, nullptr};
            interactIBP[0] = interact;   // the InternalBasePtr ctor: `mov [rcx],rdx`

            // resolve world pos -> clamped grid cell
            Vector3Slot posVec;
            interactIBP[0]->NodePosition(&posVec.v, 0, 0, nullptr);   // 0x140122e20
            int w = mapGrid->size;
            int col = static_cast<int>(posVec.v.x + static_cast<float>(w >> 1));
            int row = static_cast<int>(posVec.v.y + static_cast<float>(w >> 1));
            if (col < 0) col = 0; if (w <= col) col = w - 1;
            if (row < 0) row = 0; if (w <= row) row = w - 1;
            uint32_t destCell = static_cast<uint32_t>(w * row + col);

            // structure-path cost (or manhattan-ish euclidean fallback if unreachable)
            int cost = mapGrid->PathDistance(param_3, destCell, '\x01');
            if (cost < 0) {
                int wgrid = mapGrid->size;
                int dx = static_cast<int>(param_3) % wgrid - static_cast<int>(destCell) % wgrid;
                int dy = static_cast<int>(param_3) / wgrid - static_cast<int>(destCell) / wgrid;
                float d = WorkImpl_Sqrtf_132010(static_cast<float>(dy * dy + dx * dx));
                cost = static_cast<int>(d) + wgrid;
            }

            if (cost < param_4 * 2
                && (static_cast<char>(mapGrid->ValidateCell(PathBits::Walk, param_3, destCell)) != '\0')) {
                HappinessComponent* node = param_1->happinessComponents._data[idx].Get();
                Description* prim = node->_description;
                HappinessDescription* narrowed = Object::IsDerivedFromNullSafe(prim, s_tid_HappinessDescription)
                    ? static_cast<HappinessDescription*>(prim) : nullptr;
                if ((narrowed == nullptr || narrowed->_happinessType != 5)
                    && node->registered) {
                    Object::InternalPtr<HappinessComponent>* listBase = param_1->happinessComponents._data;
                    uint32_t oldN = static_cast<uint32_t>(matchCount);
                    uint32_t newN = oldN + 1;
                    // grow array geometrically when needed
                    if (static_cast<int>(outCap) < static_cast<int>(newN)) {
                        uint32_t grow = (oldN != 0) ? newN * 3 : newN;
                        outCap = grow;
                        Object::InternalPtr<HappinessComponent>* fresh = static_cast<Object::InternalPtr<HappinessComponent>*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int>(grow)) << 3), 8));
                        // copy existing entries into the fresh array
                        if (fresh) {
                            int copy = (curArr == fresh) ? matchCount : 0;
                            for (int k = copy; k < static_cast<int>(newN); ++k) {
                                Object::InternalPtr<HappinessComponent>* dst = fresh + k;
                                Object::InternalPtr<HappinessComponent>* src = (k < static_cast<int>(oldN)) ? curArr + k : listBase + idx;
                                if (dst) { *dst = nullptr; *dst = *src; }
                            }
                        }
                        if (curArr != fresh && curArr != nullptr) System::MemoryAllocator::MemFree(curArr);
                        curArr = fresh;
                    } else {
                        // in-place append of the current entity into curArr[oldN]
                        Object::InternalPtr<HappinessComponent>* dst = curArr + static_cast<int>(oldN);
                        Object::InternalPtr<HappinessComponent>* srcList = listBase + idx;
                        if (dst) { *dst = nullptr; *dst = *srcList; }
                    }
                    matchCount = static_cast<int>(newN);
                }
            }

            ++idx;
        } while (idx < listCount);

        if (matchCount != 0) {
            uint32_t pick = Rng()->Rand(static_cast<uint32_t>(matchCount));
            HappinessComponent* node = curArr[pick].Get();
            *param_2 = nullptr;
            *param_2 = node->_entity.Get();
            if (curArr) System::MemoryAllocator::MemFree(curArr);
            return param_2;
        }
    }
    new (param_2) ::Object::InternalBasePtr(nullptr);
    if (curArr) System::MemoryAllocator::MemFree(curArr);
    return param_2;
}

// ============================================================
// 0x14010a560 — CitizenAI_State_Leisure  (state id 12)
// Faithful goto-structured transliteration of the ~931-instruction original.
// Local-name map to the Ghidra decompile:
//   self=local_90 (param_1+0x38), pvVar26=CitizenDescription/scan-result, piVar5=map,
//   pRVar6/local_88=rng, uVar37=srcCell/local_res10, uVar14=homeCell, local_res20=target,
//   iVar27=blockRadius, local_a0/9c/98=animDest, homeBox=homeBst {root, count},
//   local_c8/c0=cellBst/count, happinessMode=local_res10 low byte.
// ============================================================
uint64_t CitizenAIComponent::State_Leisure() {
    EnsureInit();

    Map* piVar5 = MapGrid();
    System::Random* pRVar6 = GameUpdateLoop::_singleton->m_random;

    // pvVar26 = CitizenDescription on the citizen (its _description, IsDerivedFrom 0x1c7190).
    Description* descRaw = citizen->_description;
    CitizenDescription* pvVar26 = descRaw->IsDerivedFrom(s_tid_CitizenDescription)   // 0x14010a5ae
        ? static_cast<CitizenDescription*>(descRaw) : nullptr;

    // citizen ModelComponent transform translation -> grid cell (uVar37=srcCell).
    ModelComponent* model = citizen->character->modelComponent.Get();
    int w0 = piVar5->size;
    int colC = static_cast<int>(static_cast<float>(w0 >> 1) + model->transform.translation.x);
    int rowC = static_cast<int>(static_cast<float>(w0 >> 1) + model->transform.translation.y);
    if (colC < 0) colC = 0; if (w0 <= colC) colC = w0 - 1;
    if (rowC < 0) rowC = 0; if (w0 <= rowC) rowC = w0 - 1;
    uint32_t uVar37 = static_cast<uint32_t>(w0 * rowC + colC);    // srcCell / local_res10

    // homeCell (uVar14) from the home anchor or -1.
    uint32_t uVar14;
    if (citizen->home == nullptr) {
        uVar14 = 0xffffffff;
    } else {
        ComponentEntity* homeIBP = citizen->home.Get();
        uVar14 = EntityAnchorCell(&homeIBP, nullptr);
    }

    int64_t local_a8 = static_cast<int64_t>(static_cast<int>(uVar37));   // srcCell index (retail: its byte offset, *0x30)
    ComponentEntity* local_res20 = piVar5->cells.GetData()[local_a8].blocker.Get();      // target entity

    {
        float fVar39 = pRVar6->RandFloat();
        if (fVar39 < kRandThresh) {
            ComponentEntity* nullIBP = nullptr;
            new (&nullIBP) ::Object::InternalBasePtr(nullptr);
            local_res20 = nullIBP;
        }
    }

    int iVar27 = 0;             // blockRadius (set per path before LAB_14010ae8a)
    int iVar30 = 0, iVar13 = 0, iVar24 = 0; // box origin col, row, height
    int local_res18_w = 0;      // box width (low dword of local_res18)
    float local_a0 = 0, local_9c = 0, local_98 = 0;  // animDest vec
    bool happinessMode = false; // local_res10 low byte

    if (local_res20 == nullptr) goto LAB_14010a7e0;

    // ---- validated-target path: target must have a usable HappinessComponent + be reachable ----
    {
        HappinessComponent* happyIBP = nullptr;
        CitizenAI_State_Leisure_sub_fd50(local_res20, &happyIBP);
        if (happyIBP == nullptr) goto LAB_14010a7e0;

        HappinessComponent* happyIBP2 = nullptr;
        HappinessComponent** pIVar17 = CitizenAI_State_Leisure_sub_fd50(local_res20, &happyIBP2);
        char cVar12 = CitizenAI_helper_a720(*pIVar17);
        if (cVar12 == '\0') goto LAB_14010a7e0;

        InteractComponent* posIBP = local_res20->FindComponent<InteractComponent>();   // 0x140002ed0
        InteractComponent** pp = &posIBP;
        Vector3Slot dpos;
        (*pp)->NodePosition(&dpos.v, 0, 0, nullptr);   // 0x14010a778, no test
        int w = piVar5->size;
        int tcol = static_cast<int>(static_cast<float>(w >> 1) + dpos.v.x);
        int trow = static_cast<int>(static_cast<float>(w >> 1) + dpos.v.y);
        if (tcol < 0) tcol = 0; if (w <= tcol) tcol = w - 1;
        if (trow < 0) trow = 0; if (w <= trow) trow = w - 1;
        uint32_t uVar35 = static_cast<uint32_t>(w * trow + tcol);
        uint8_t reach = piVar5->ValidateCell(PathBits::Walk, uVar37, uVar35);
        if (static_cast<char>(reach) == '\0') goto LAB_14010a7e0;
        goto LAB_14010a83d;   // target validated
    }

LAB_14010a7e0:
    // social find-target (sub_99d0) at the CitizenDescription leisure radius (pvVar26+0xbc),
    // first from srcCell then homeCell.
    {
        // 0x14010a7e7 / 0x14010a81c MOV R9D,[RSI+0xbc]: RSI is the narrowed (or 0)
        // CitizenDescription, read with no test (B6 row 240).
        uint32_t radius = pvVar26->idleRange;
        ComponentEntity* findBuf = nullptr;
        ComponentEntity** pr = CitizenAI_State_Leisure_sub_99d0(GameUpdateLoop::_singleton->m_citizenList,
                                                                &findBuf, uVar37, static_cast<int>(radius));
        local_res20 = *pr;
        if (local_res20 == nullptr) {
            if (static_cast<int>(uVar14) >= 0) {
                uint32_t radius2 = pvVar26->idleRange;
                ComponentEntity* findBuf2 = nullptr;
                ComponentEntity** pr2 = CitizenAI_State_Leisure_sub_99d0(GameUpdateLoop::_singleton->m_citizenList,
                                                                         &findBuf2, uVar14, static_cast<int>(radius2));
                local_res20 = *pr2;
                goto LAB_14010a83d;
            }
            goto LAB_14010a846;
        }
    }

LAB_14010a83d:
    if (local_res20 != nullptr) goto LAB_14010abbc;

LAB_14010a846:
    // home-box BST: insert reachable, work-valid cells in a 13x13 box around srcCell, then
    // (rand%count)-th in-order becomes local_res20. iVar27 ends = 6 for the bloom.
    {
        // ONE {root, count} record (retail [RBP-0x79] / [RBP-0x71], zeroed at 0x14010a846 /
        // 0x14010a84a): the insert 0x140021a30 counts into it itself.
        Collection::Set<ComponentEntity*> homeBox;
        homeBox._root = nullptr; homeBox._count = 0;
        int baseCol = static_cast<int>(uVar37) % piVar5->size;
        int boxRow = static_cast<int>(uVar37) / piVar5->size - 6;
        for (int rows = 0xd; rows != 0; --rows) {
            for (int dxo = -6; dxo < 7; ++dxo) {
                int cx = dxo + baseCol;
                int w = piVar5->size;
                if (cx >= 0 && cx < w && boxRow >= 0 && boxRow < w) {
                    MapCell* plVar20 = &piVar5->cells.GetData()[static_cast<int64_t>(w * boxRow + dxo + baseCol)];
                    ComponentEntity* cand = nullptr;     // local_res18
                    if (plVar20->blocker == nullptr) {
                        if (plVar20->occupant == nullptr) { new (&cand) ::Object::InternalBasePtr(
                            nullptr); }
                        else cand = plVar20->occupant.Get();
                    } else cand = plVar20->blocker.Get();
                    if (cand != nullptr) {
                        MapComponent* mapBuf = nullptr;     // local_b0
                        mapBuf = cand->FindComponent<MapComponent>();   // 0x140004680
                        if (mapBuf != nullptr) {
                            // local_b8 = cand
                            char ok = citizen->CanReach(cand);
                            if (ok != '\0') {
                                LeisureHomeNode* outNode = nullptr;   // &local_a0 (out-node, result unused)
                                // 0x14010a935 `LEA RCX,[RBP-0x79]; CALL 0x140021a30`: the insert's own
                                // `INC [RSI+8]` is the only count (0x14010a93e is `INC EBX`), so a
                                // duplicate key (the same building on several cells) is not counted.
                                WorkBST_RegisterWork_1a30(&homeBox, &outNode, &cand);
                            }
                        }
                    }
                }
            }
            ++boxRow;
        }
        // also consider the citizen's own home if reachable (manual key-ordered insert).
        uVar37 = static_cast<uint32_t>(w0 * rowC + colC);   // uVar37 = (uint)local_res10 = srcCell
        iVar27 = 6;
        if (citizen->home != nullptr) {
            char ok = citizen->CanReach(citizen->home.Get());   // local_res10 = home
            if (ok != '\0') {
                ComponentEntity* key = citizen->home.Get();
                if (homeBox._root == nullptr) {
                    LeisureHomeNode* nd = static_cast<LeisureHomeNode*>(System::MemoryAllocator::MemAlloc(sizeof(LeisureHomeNode), 8));
                    if (nd) { nd->element = key; nd->parent = nullptr; nd->left = nullptr; nd->right = nullptr; nd->balance = 0; nd->height = 0; }
                    homeBox._count = homeBox._count + 1;
                    homeBox._root = nd;
                } else {
                    ComponentEntity* keyVal = key;   // compared as ulonglong (pointer key)
                    LeisureHomeNode* cur = homeBox._root; LeisureHomeNode* ins = cur; bool done = false;
                    do {
                        ins = cur;
                        if (cur->element <= keyVal) {
                            if (keyVal <= cur->element) { done = true; break; }  // present
                            cur = cur->right;
                            if (cur == nullptr) {
                                LeisureHomeNode* nd = static_cast<LeisureHomeNode*>(System::MemoryAllocator::MemAlloc(sizeof(LeisureHomeNode), 8));
                                if (nd) { nd->element = keyVal; nd->parent = ins; nd->left = nullptr; nd->right = nullptr; nd->balance = 0; nd->height = 0; }
                                ins->right = nd;
                                AvlRebalance(&homeBox._root, &ins);   // 0x14001a8c0
                                homeBox._count = homeBox._count + 1; done = true; break;
                            }
                        } else {
                            cur = cur->left;
                            if (cur == nullptr) {
                                LeisureHomeNode* nd = static_cast<LeisureHomeNode*>(System::MemoryAllocator::MemAlloc(sizeof(LeisureHomeNode), 8));
                                if (nd) { nd->element = keyVal; nd->parent = ins; nd->left = nullptr; nd->right = nullptr; nd->balance = 0; nd->height = 0; }
                                ins->left = nd;
                                AvlRebalance(&homeBox._root, &ins);   // 0x14001a8c0
                                homeBox._count = homeBox._count + 1; done = true; break;
                            }
                        }
                    } while (!done);
                }
            }
        }
        // pick (rand%count)-th in-order node; its key (+0x00) is local_res20.
        if (homeBox._count > 0) {
            uint32_t pick = pRVar6->Rand(static_cast<uint32_t>(homeBox._count));
            LeisureHomeNode* node = homeBox._root; LeisureHomeNode* chosen = homeBox._root;
            while (node != nullptr) { chosen = node; node = node->left; }
            for (uint32_t s = pick; s > 0; --s) {
                if (chosen != nullptr) {
                    LeisureHomeNode* right = chosen->right;
                    if (right == nullptr) {
                        LeisureHomeNode* parent = chosen->parent;
                        if (parent != nullptr && parent->left != chosen) {
                            while (parent != nullptr && parent->right == chosen) {
                                chosen = parent; parent = parent->parent;
                            }
                        }
                        chosen = chosen->parent;
                    } else {
                        do { chosen = right; right = chosen->left; } while (right != nullptr);
                    }
                }
            }
            local_res20 = (chosen != nullptr) ? chosen->element : nullptr;
        }
        if (homeBox._root != nullptr) {
            { WorkBase_TreeNodeFree(&homeBox._root, homeBox._root->left);
                             WorkBase_TreeNodeFree(&homeBox._root, homeBox._root->right); }
            System::MemoryAllocator::MemFree(homeBox._root);
        }
        homeBox._root = nullptr; homeBox._count = 0;
        goto LAB_after_target;   // local_res20 set (or still null) -> bound decision
    }

LAB_14010abbc:
    // validated/found target: scan its description's sub-descriptions for
    // HappinessDescription (0x1c42f8); iVar27 = its radius (+0x2c).
    {
        Description* pvr = ScanType5(local_res20, s_tid_HappinessDescription);
        // 0x14010ac30 MOV R12D,[RCX+0x2c]: a miss leaves RCX = 0 (0x14010ac2c) and the read
        // has no test (B6 row 240).
        iVar27 = static_cast<HappinessDescription*>(pvr)->_idleRange;
    }

LAB_after_target:
    // ---- decide box bounds (LAB_14010ae8a feeder) ----
    happinessMode = false;   // local_res10 low byte = 0
    if (local_res20 == nullptr) {
        iVar13 = static_cast<int>(uVar37) / piVar5->size;   // box row
        iVar30 = static_cast<int>(uVar37) % piVar5->size;   // box col
        iVar24 = 1;                                    // box height
        local_res18_w = 1;                             // box width
        // iVar27 keeps the value from the path that reached here (6 from home-box, or the
        // validated-target radius). For a pure-null target with no home-box build iVar27=6.
        goto LAB_14010ae8a;
    }

    // local_res20 != 0: scan its components for HappinessComponent (0x1c42c0) -> local_res18.
    {
        HappinessComponent* local_res18 = static_cast<HappinessComponent*>(ScanArr(local_res20, s_tid_HappinessComponent));
        if (local_res18 == nullptr) {
            happinessMode = false;   // LAB_14010adba
        } else {
            HappinessComponent* local_res10n = static_cast<HappinessComponent*>(ScanArr(local_res20, s_tid_HappinessComponent));
            // LAB_14010ad7a: +0x18 = HappinessDescription (0x1c42f8); if +0x20==5 || !registered -> false.
            HappinessComponent* hnode = local_res10n;
            Description* hdRaw = hnode->_description;
            // 0x14010ad9b..0x14010ada4: CMOVNZ RCX,RBX over RCX = 0, then [RCX+0x20] with no
            // test (B6 row 240).
            HappinessDescription* hd = hdRaw->IsDerivedFrom(s_tid_HappinessDescription)
                ? static_cast<HappinessDescription*>(hdRaw) : nullptr;
            if (!(hd->_happinessType == 5
                  || !hnode->registered)) {
                happinessMode = true;   // local_res10 low byte = 1
            }
        }
        // third scan: MapComponent (0x1c79e0) -> local_b8; read bounds shorts (+0x28..+0x2e).
        MapComponent* local_b8 = static_cast<MapComponent*>(ScanArr(local_res20, s_tid_MapComponent));
        MapComponent* mnode = local_b8;
        iVar30          = static_cast<int>(mnode->tileX0);
        iVar13          = static_cast<int>(mnode->tileY0);
        local_res18_w   = static_cast<int>(mnode->tileX1);
        iVar24          = static_cast<int>(mnode->tileY1);
    }

LAB_14010ae8a:
    {
        iVar13 = iVar13 - iVar27;                       // box row origin
        int iVar36 = local_res18_w + iVar27 * 2;        // box width
        uint32_t boxH = static_cast<uint32_t>(iVar24 + iVar27 * 2);  // box height
        LeisureCellNode* local_c8 = nullptr; uint32_t local_c0 = 0;  // cell BST

        for (int rr = static_cast<int>(boxH); rr > 0; --rr) {
            for (int cc = 0; cc < iVar36; ++cc) {
                int iVar16 = cc + (iVar30 - iVar27);
                int w = piVar5->size;
                if (iVar16 >= 0 && iVar16 < w && iVar13 >= 0 && iVar13 < w) {
                    int cellIdx = w * iVar13 + cc + (iVar30 - iVar27);
                    int64_t lv = static_cast<int64_t>(cellIdx);
                    if (piVar5->cells.GetData()[lv].terrainType < PathType::Occasional) {   // a road or Normal
                        piVar5->FillZones();   // 0x14009c480 (RCX only)
                        MapCell* arrBase = piVar5->cells.GetData();
                        uint32_t mNew = 1u << (static_cast<uint8_t>(arrBase[lv].terrainType) & 0x1f);
                        uint32_t mSrc = 1u << (static_cast<uint8_t>(arrBase[local_a8].terrainType) & 0x1f);
                        uint32_t both = mSrc | mNew;
                        uint32_t mask = both | Bits(PathBits::Walk);
                        bool match = false;
                        if ((mask & mSrc) != 0 && (mask & mNew) != 0) {
                            if ((both & Bits(PathBits::Zoneless)) != 0) match = true;
                            else {
                                short z0 = arrBase[local_a8].zone;
                                short z1 = arrBase[lv].zone;
                                if (z0 != 0 && z1 != 0 && z0 == z1) match = true;
                            }
                        }
                        if (match) {
                            if (local_c8 == nullptr) {
                                LeisureCellNode* nd = static_cast<LeisureCellNode*>(System::MemoryAllocator::MemAlloc(sizeof(LeisureCellNode), 8));
                                if (nd) { nd->key = cellIdx; nd->parent = nullptr; nd->left = nullptr; nd->right = nullptr; nd->balance = 0; nd->height = 0; }
                                local_c8 = nd;
                                local_c0 = local_c0 + 1;
                            } else {
                                LeisureCellNode* cur = local_c8; LeisureCellNode* ins = cur; bool done = false;
                                do {
                                    ins = cur;
                                    if (cellIdx < cur->key) {
                                        cur = cur->left;
                                        if (cur == nullptr) {
                                            LeisureCellNode* nd = static_cast<LeisureCellNode*>(System::MemoryAllocator::MemAlloc(sizeof(LeisureCellNode), 8));
                                            if (nd) { nd->key = cellIdx; nd->parent = ins; nd->left = nullptr; nd->right = nullptr; nd->balance = 0; nd->height = 0; }
                                            ins->left = nd;
                                            AvlRebalance(&local_c8, &ins);   // 0x14001a8c0
                                            local_c0 = local_c0 + 1; done = true; break;
                                        }
                                    } else if (cellIdx <= cur->key) {
                                        done = true; break;  // present
                                    } else {
                                        cur = cur->right;
                                        if (cur == nullptr) {
                                            LeisureCellNode* nd = static_cast<LeisureCellNode*>(System::MemoryAllocator::MemAlloc(sizeof(LeisureCellNode), 8));
                                            if (nd) { nd->key = cellIdx; nd->parent = ins; nd->left = nullptr; nd->right = nullptr; nd->balance = 0; nd->height = 0; }
                                            ins->right = nd;
                                            AvlRebalance(&local_c8, &ins);   // 0x14001a8c0
                                            local_c0 = local_c0 + 1; done = true; break;
                                        }
                                    }
                                } while (!done);
                            }
                        }
                    }
                }
            }
            ++iVar13;
        }

        uint64_t uVar41 = 0;
        if (local_c0 != 0) {
            uint32_t pick = pRVar6->Rand(static_cast<uint32_t>(local_c0));
            LeisureCellNode* node = local_c8; LeisureCellNode* puVar21 = local_c8;
            while (node != nullptr) { puVar21 = node; node = node->left; }
            for (uint32_t s = pick; s > 0; --s) {
                if (puVar21 != nullptr) {
                    LeisureCellNode* right = puVar21->right;
                    if (right == nullptr) {
                        LeisureCellNode* parent = puVar21->parent;
                        if (parent != nullptr && parent->left != puVar21) {
                            while (parent != nullptr && parent->right == puVar21) {
                                puVar21 = parent; parent = parent->parent;
                            }
                        }
                        puVar21 = puVar21->parent;
                    } else {
                        do { puVar21 = right; right = puVar21->left; } while (right != nullptr);
                    }
                }
            }
            int chosenCell = puVar21 ? puVar21->key : 0;

            // cell -> world centre.
            int iv = piVar5->size;
            float fVar39c = static_cast<float>(iv >> 1) - kCellBias;
            float inXY[2] = { static_cast<float>(chosenCell % iv) - fVar39c,
                              static_cast<float>(chosenCell / iv) - fVar39c };
            uVar41 = 0;
            {
                float out3[4] = {0, 0, 0, 0};
                piVar5->GetTerrainPos(out3, inXY, '\0', nullptr);
                local_a0 = out3[0]; local_9c = out3[1]; local_98 = out3[2];
            }
            // random offset within a disk.
            float fVar39 = pRVar6->RandFloatTol() * kDiskScale;
            float fVar40 = pRVar6->RandFloatTol();
            float off[4] = { fVar40 * kDiskScale, fVar39, 0.0f, 0.0f };
            float dest[4] = { local_a0 + off[0], local_9c + off[1], local_98 + off[2], 0.0f };

            // entity to anim toward (local_res18): if happinessMode use target else null IBP.
            char cVar12 = happinessMode ? 1 : 0;
            ComponentEntity* entIBP;
            if (cVar12 == '\0') { entIBP = nullptr; new (&entIBP) ::Object::InternalBasePtr(
                nullptr); }
            else entIBP = local_res20;
            ComponentEntity* local_res18 = entIBP;

            CitizenComponent* local_a8h = citizen.Get();   // holder citizen (PlayAnimation self IBP)
            float fVar39t = pRVar6->RandFloatTol();
            CitizenAI_PlayAnimationCtor4_ba6b0(&local_a8h, &local_res18, dest,
                                       fVar39t * kAgeMult + kAnimTimeBase,
                                       static_cast<uint32_t>(static_cast<unsigned char>(cVar12)));

            citizen->lastWorkId = -1;
            citizen->statusFlags |= Bits(CitizenStatus::TaskDone);
            citizen->lastErrand = CitizenErrand::None;
            leisureTimer._ticks = 0;
            uVar41 = 1;
        }

        if (local_c8 != nullptr) {
            { WorkBase_TreeNodeFree(&local_c8, local_c8->left);
                             WorkBase_TreeNodeFree(&local_c8, local_c8->right); }
            System::MemoryAllocator::MemFree(local_c8);
        }
        return uVar41;
    }
}
