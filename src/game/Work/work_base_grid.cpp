// src/game/Work/work_base_grid.cpp
// Shared Work grid / work-assignment helpers (hand-ported + Ghidra-verified).
//
// Called by name from many src/game/Work/*.cpp. Faithful 1:1 ports.
#include "Work/exe_bodies.h"
//
// NOTE: this replaces an earlier parallel-agent draft that was NOT faithful (the draft is
// kept under docs/history/notes/wave2_drafts/work_base_grid.cpp.draft). Each function here was verified
// against the Ghidra decompile.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode (the id map's node)
#include "Work/Work.h"   // B.3.x W0: the work tree payload by name
#include "Component/AutoPickupComponent.h"
#include "Entity/ComponentEntity.h"
#include "collection/Collection.h"
#include <cstdint>

// ============================================================
// FUN_14009ccf0 — WorldGrid_FindCell
// ============================================================
// Scan the 3x3 block around srcCell for the passable cell nearest destCell. The original
// iterates with the COLUMN tied to the outer loop and the ROW tied to the inner loop (a
// transposed traversal); ties resolve to the first-visited minimum, so the exact order is
// preserved here. The Map's cells, terrain type (+0x29).
// Caller: int fn(Map* map, int srcCell, int destCell, uint32_t passFlags)
int Map::FindCell(int srcCell, int destCell, PathBits passFlags) {
    int gridW = size;
    int best = -1;
    int bestDist = 0;
    MapCell* cellArr = cells.GetData();

    int col = srcCell % gridW - 1;                 // iVar9, advances with the outer loop
    for (int rowD = -1; rowD <= 1; rowD++, col++) {
        int rowBase = (srcCell / gridW - 1) * gridW;   // iVar6, advances with the inner loop
        for (int colD = -1; colD < 2; colD++, rowBase += gridW) {
            if (!(rowD != 0 || colD != 0)) continue;   // skip srcCell itself
            if (col < 0) continue;
            int row = colD + srcCell / gridW;
            if (row < 0 || col >= gridW || row >= gridW) continue;
            int cand = col + rowBase;
            PathType passByte = cellArr[static_cast<uint32_t>(cand)].terrainType;
            if (!HasType(passFlags, passByte)) continue;
            int dRow = cand / gridW - destCell / gridW;
            int dCol = cand % gridW - destCell % gridW;
            int dist = dRow * dRow + dCol * dCol;
            if (best < 0 || dist < bestDist) { best = cand; bestDist = dist; }
        }
    }
    return best;
}

// The scheduler's id map (WorkScheduler::trees[0]): WorkSchedulerIdNode
// (Game/WorkSchedulerNodes.h: key @+0, the work @+8, left @+0x18, right @+0x20).

// ============================================================
// FUN_1400f8620 — TryAssignWorkplace
// ============================================================
// Look up cellId in the scheduler's id map (trees[0]). If the matched work's stamp
// (Work +0x2c) already equals the scheduler's step count (+8), it's a no-op. Otherwise
// fire Validate [9] and Execute [4](citizen, &scheduler->time); 2 = accepted; 1 =
// assignable -> when `flag`, fire Validate then Slot5; on success return low-byte 1,
// else stamp the step count. RCX is the WorkScheduler (a member of it; its header is
// GAME-a's, so this stays free).
// RCX = the WorkScheduler, RDX = the citizen reference (an InternalPtr<CitizenComponent>
// slot: 0x1400f86b9 `MOV RAX,[R14]` copies it into the Execute argument), R8D = the cell id, R9B = flag.
uint64_t WorkScheduler::TryAssignWorkplace(CitizenComponent** citizenRef, int cellId, char flag) {
    WorkSchedulerIdNode* node = trees[0].root;
    for (;;) {
        for (;;) {
            if (node == nullptr) return 0;
            if (node->key <= cellId) break;
            node = node->left;
        }
        if (cellId <= node->key) break;
        node = node->right;
    }
    if (node == nullptr) return 0;

    Work* work = node->work.Get();   // IBP
    uint32_t mgrGen = stepCount;
    if (static_cast<uint32_t>(work->_lastRejectStep) == mgrGen)
        return static_cast<uint64_t>(mgrGen & 0xffffff00u);

    // [9] Validate, RCX only (0x1400f86ad); [4] Execute(&citizen, scheduler+0x130) (0x1400f86d3).
    work->Validate();

    CitizenComponent* citizenCopy = *citizenRef;
    uint64_t uVar2 = static_cast<uint32_t>(work->Execute(&citizenCopy, &time));

    if (static_cast<int>(uVar2) == 2)
        return (uVar2 & 0xffffffffffffff00ull) | 1;

    if (static_cast<int>(uVar2) == 1) {
        if (flag != '\0') {
            work->Validate();   // [9] (0x1400f86f4)
            citizenCopy = *citizenRef;
            // [5] Slot5(&citizen, scheduler+0x130) (0x1400f871a); the caller reads AL.
            uint64_t assignR = static_cast<uint32_t>(work->Slot5(&citizenCopy, &time));
            if (static_cast<char>(assignR) != '\0')
                return (assignR & 0xffffffffffffff00ull) | 1;
        }
        uVar2 = mgrGen;
        work->_lastRejectStep = static_cast<int32_t>(mgrGen);
    }
    return uVar2 & 0xffffffffffffff00ull;
}

// ============================================================
// FUN_1400e3db0 — AutoPickup_CheckAssignments (Work::TryAutoPickup)
// ============================================================
// Find the AutoPickupComponent on this work's entity (0x140022520). If enabled
// (+0x28), walk its cell-index list (+0x30, count +0x38): for each id, look it up in
// the scheduler's id map (*g_pGameWorldSingleton + 0x118, trees[0]). If found, try to
// assign the citizen (TryAssignWorkplace) — return 2 on success, else keep scanning;
// if NOT found, compact the id out of the list and re-examine the shifted-in slot.
// RCX is a Work (only its +0x10 entity is read): the Work leaves' Execute calls it
// with `this`.
int Work::TryAutoPickup(CitizenComponent** citizenRef) {
    AutoPickupComponent* apComp = _entity->FindComponent<AutoPickupComponent>();   // 0x140022520
    // B6 (14d G22): 0x1400e3de0 / 0x1400e3de5 read the component's +0x28 with no test;
    // 0x1400e3df5 / 0x1400e3e06 read [0x1401c3dd8]+0x118 with no test.
    if (!apComp->isEnabled) return 0;
    WorkScheduler* mgr = GameUpdateLoop::_singleton->m_tutorial;

    int i = 0;
    if (apComp->pileWorkIds._size <= 0) return 0;

    do {
        WorkSchedulerIdNode* node = mgr->trees[0].root;
        int cellId = apComp->pileWorkIds._data[i];
        bool keepElement = false;
        while (node != nullptr) {
            if (cellId < node->key) {
                node = node->left;
            } else {
                if (cellId <= node->key) {   // found
                    CitizenComponent* citizenCopy = *citizenRef;
                    uint64_t r = mgr->TryAssignWorkplace(&citizenCopy,
                                                                   cellId, '\x01');
                    if (static_cast<char>(r) != '\0') return 2;
                    keepElement = true;
                    break;
                }
                node = node->right;
            }
        }
        if (!keepElement) {
            // not found in the map -> compact arr[i] out of the list
            int count = apComp->pileWorkIds._size;
            int shiftIdx = i;
            int j = i;
            while (j < count) {
                j++;
                if (j < count) {
                    int32_t* p = apComp->pileWorkIds._data + shiftIdx;
                    p[0] = p[1];
                }
                shiftIdx += 1;
            }
            apComp->pileWorkIds._size = count - 1;
            i -= 1;
        }
        i += 1;
    } while (i < apComp->pileWorkIds._size);

    return 0;
}
