// src/game/Action/action_base_move.cpp
// Shared MoveTo / PerformWork / PlayAnimation base-class / ECS exe sub-helpers.
//
// The per-class Action .cpp files (moveto.cpp, performwork.cpp, playanimation.cpp,
// huntanimal.cpp) call these by name through Action/exe_bodies.h (B3R); the
// registration (RegisterActionMoveBatch in standalone/reg_action_move.cpp) stays for
// readers other blocks still have.
//
// Wave 3 (Action) — MoveTo / PerformWork / PlayAnimation sub-batch. Each function is
// a 1:1 port of the decompiled exe helper; do not "improve" behavior.
//
#include "Action/exe_bodies.h"

#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/PathfinderThread.h"
#include "Action/PathSearch.h"
#include "Game/exe_bodies.h"       // Reimpl_MapGetTerrainPos
#include "Game/exe_constants.h"   // kRdata_140172114
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "Action/saved_bounds.h"
#include "Object/factory_registration.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "math/Math.h"
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Work/Work.h"         // B.3.x W0: Work::GetTarget by name
#include "Component/CharacterComponent.h"   // ENDGAME B.3.8 CB3: CharacterComponent::TypeId()
#include "Component/CharacterDescription.h"
#include "Component/CitizenComponent.h"
#include "Component/ModelComponent.h"
#include "Action/MoveTo.h"
#include "Entity/ComponentEntity.h"
#include "Game/Profession.h"
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode (the id trees' node)
#include "stress_harness.h"   // PFBENCH: g_pfProfOn / PfProf_Taken (stress harness only)
#include "ext/map_overlays/map_overlays.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): ext.map_overlays
#include "ext/winter_ice/walk_type.h"   // EXT SEAM (NOT RETAIL, WINTERICE.md 5.2): ext.walk_on_ice, a frozen lake cell reads as type 9

// ============================================================
// Locally-resolved dispatch-slot / math helpers
// ============================================================
// ENDGAME B.3.1 step 2: the Math::Vector2 / Math::Vector3 helpers below used to be
// read from the exe's .rdata dispatch slots — 0x1401526b0 Math::Vector2::Normalize,
// 0x1401512c0 Math::Vector3::Normalize, 0x1401512d8 Math::Vector3::Heading. Those
// slots were the DLL boundary's shape; the calls are now direct members on the real
// Math:: classes (src/engine/math/Math.h), same bodies, same operand order.

// ============================================================
// FUN_14010a060 — PerformWork_helper_a060 (SetFacingDirection)
// ============================================================
// Sets a component's facing from a Vector3 direction. Clears a byte at +0x28,
// normalizes (x,y,0) into +0x2c..+0x34, stores Heading(dir) at +0x3c.
// Caller signature: void fn(void* comp, void* vector3)
void CharacterComponent::SetFacingDirection(const Math::Vector3& dir) {
    field_0x28 = false;
    Math::Vector3 tmp(dir.x, dir.y, 0.0f);
    Math::Vector3 n = tmp.Normalize();
    field_0x2C.x = n.x;
    field_0x2C.y = n.y;
    field_0x2C.z = n.z;
    field_0x3C = dir.Heading();
}


// ============================================================
// FUN_1400c8d20 — MoveTo_sub_8d20 (UpdateFacing toward next waypoint)
// ============================================================
// Computes the facing direction from the citizen's current world position toward
// the next path cell, normalizes it into the movement component (+0x2c..+0x34) and
// stores the Heading at +0x3c. Mirrors PerformWork_helper_a060 but derives the
// direction from grid geometry.
//   g_pGameWorldSingleton+0xf8 -> map grid (int*). grid[0] = width.
//   self+0x28 -> citizen IBP; that+0x68 -> movement sub-object; +0x158/+0x15c =
//     current world x/y; +0x160 carried through.
//   self+0x60 = current path index; self+0x68[idx] = target cell.
//   CharacterComponent_impl_f3e0 (0x14009f3e0, Reimpl_MapGetTerrainPos) = grid->world
//   conversion. DAT_140172114 = 0.5f (kRdata_140172114).
// Caller signature: void fn(void* self)
void MoveTo::UpdateFacing() {
    Map* grid = GameUpdateLoop::_singleton->m_game;    // +0xF8
    float halfCell = kRdata_140172114;   // 0x140172114

    // The character's model (character +0x68), whose transform translation (+0x148 + 0x10)
    // is the current world position.
    const CharacterComponent* character = this->character.Get();
    const ModelComponent* model = character->modelComponent.Get();

    float curX = model->transform.translation.x;   // +0x158
    float curY = model->transform.translation.y;   // +0x15C

    // Target cell index from the path array (self+0x68 + curIdx*4).
    int curIdx = pathIndex;
    int targetCell = path[curIdx];

    int gridW = grid->size;
    if (Ext::MapOverlays::g_on && curIdx > 1) {
        // ext.map_overlays: a footstep, the waypoint just reached (path[0] is the start cell, not entered)
        const int reached = path[curIdx - 1];
        Ext::MapOverlays::OnStep(reached % gridW, reached / gridW);
    }
    float center = static_cast<float>(gridW >> 1) - halfCell;
    float inGrid[3];
    inGrid[0] = static_cast<float>(targetCell % gridW) - center;  // local_58
    inGrid[1] = static_cast<float>(targetCell / gridW) - center;  // local_54
    inGrid[2] = 0.0f;

    float outWorld[3] = {0.0f, 0.0f, 0.0f};
    grid->GetTerrainPos(outWorld, inGrid, 0, nullptr);   // 0x14009f3e0

    // dir2 = (worldX - curX, worldY - curY), normalized.
    Math::Vector2 dir2in(outWorld[0] - curX, outWorld[1] - curY);
    Math::Vector2 dir2 = dir2in.Normalize();

    // Re-read the character, store the normalized 3D facing into it.
    CharacterComponent* character2 = this->character.Get();
    character2->field_0x28 = false;

    // local_58/54/50 = (dir2.x, dir2.y, 0) from the Vector2::Normalize result.
    Math::Vector3 dir3(dir2.x, dir2.y, 0.0f);
    Math::Vector3 n = dir3.Normalize();
    character2->field_0x2C.x = n.x;
    character2->field_0x2C.y = n.y;
    character2->field_0x2C.z = n.z;

    Math::Vector3 head3(dir2.x, dir2.y, 0.0f);
    character2->field_0x3C = head3.Heading();
}

// ============================================================
// FUN_14010a1f0 — AnimalComponent_impl_a1f0 (SetAnimation)
// ============================================================
// Sets an animation on the family/model component and computes a speed scalar.
//   self+0x78 -> sub-object (lVar1) read for +0x2c.
//   self+0x68 -> animation sub-object IBP (checked).
//   AnimalComponent_helper_50f0 (0x1401250f0): (RCX=animObj, EDX=animId, XMM2=0.0f,
//     XMM3=*(lVar1+0x2c), stack5=p3 float).  Verified in disassembly: XMM6=p3, XMM7=p4.
//   self+0x38 = *(animObj+0x13c) * p4  (speed multiplier).
// Caller signature: void fn(void* self, uint32_t animId, float p3, float p4)
void CharacterComponent::SetAnimation(uint32_t animId, float p3, float p4) {
    const CharacterDescription* desc = characterDescription;
    // 5th arg (p3) is a float passed on the stack in the original (MOVSS [RSP+0x20]).
    modelComponent.Get()->PlayAnimation(animId, 0.0f, desc->_turnFilter, p3);   // 0x1401250f0
    // ModelComponent +0x13c is the embedded MeshInstance's (+0x28) +0x114: _color[2]
    // (Mesh.h's guess name for the four floats the ctor seeds with 1.0).
    field_0x38 = modelComponent->meshInstance._color[2] * p4;
}


// ============================================================
// FUN_140116650 — PerformWork_helper_6650 (citizen break / task-due check)
// ============================================================
// Returns nonzero when the citizen needs a break / has a pending task.
//   gw+0xd0 bit 8: if set, returns 0 (paused/disabled state).
//   self+0x1f8 != 0: a task already pending -> returns 1.
//   self+0x30 (task id) >=0 : walk the AVL tree at gw+0x118->+0x30 keyed on int at
//     node+0 (left=node+6 ints i.e. +0x18, right=node+8 ints i.e. +0x20), value IBP at
//     node+2 ints i.e. +0x08. On the matching node, call value->vtable[+0x50](value,&out)
//     to fetch a record; read float at out+0x54; FUN_1400a9290(self+0x200, &thatFloat,
//     gw+0xd8 Random) decides. On success stash out at self+0x1f8 and return it (nonzero).
// Caller signature: uint8_t fn(void* entity)
uint8_t CitizenComponent::BreakCheck() {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;

    uint32_t flags = gw->m_gameFlags;
    if (((flags >> 8) & 1) != 0)
        return 0;

    if (pendingProfession != nullptr)
        return 1;

    int taskId = field_0x30;
    if (taskId < 0)
        return 0;

    // The scheduler's first id tree (WorkScheduler::trees[0], +0x30): int key at +0x00,
    // the Work at +0x08, children at +0x18 / +0x20.
    const WorkSchedulerIdNode* node = gw->m_tutorial->trees[0].root;
    Work* value = nullptr;       // local_res10
    bool haveValue = false;
    while (node != nullptr) {
        if (taskId < node->key) {
            node = node->left;
        } else if (taskId > node->key) {
            node = node->right;
        } else {
            value = node->work.Get();
            haveValue = true;
            break;
        }
    }
    if (!haveValue) {
        // InternalBasePtr ctor (&local_res10, 0): value stays null.
        value = nullptr;
    }

    if (value == nullptr)
        return 0;

    // value->vtable[+0x50](value, &out) -> the Profession in 'out'. The value is the
    // scheduler's work (filled by CitizenAI_impl_88a0 0x1400f88a0 with the posted Work
    // leaves).
    Profession* outRec = nullptr;
    value->GetTarget(&outRec);   // slot 10, retslot

    // Original copies 8 bytes (*(undefined8*)(rec+0x54)) — a9290 reads BOTH floats
    // (pair[0] = value, pair[1] = weight).
    float duePair[2];
    duePair[0] = outRec->skillMult1;
    duePair[1] = outRec->skillMult2;
    // LEFT: CitizenComponent::field_0x200 (block COMP-b's header) is declared int64_t, not
    // System::Time.
    bool ok = BreakTimeCheck(&field_0x200, duePair, gw->m_random);   // 0x1400a9290
    if (ok) {
        pendingProfession = outRec;
        return 1;
    }
    return 0;
}


// ============================================================
// FUN_14009e550 — MoveTo_helper_e550 (SetTerrainCosts by movement profile)
// ============================================================
// Programs the pathfinder's per-terrain step costs for one of 4 movement profiles
// (param_3 = 0..3), via FUN_1400a6a30(pathfinder, slot, cost, param4). Then calls
// RoadTool_sub_ce00_sub_68b0 to set a road-preference flag.
//   param_1 = map grid (longlong); +0x68 -> some sub-object for the final call.
//   param_2 = pathfinder (Thread*).
// Caller signature: void fn(void* mapGrid, void* pathfinder, int profile, void* param4)
//
// B.4 ACT-a: Map::SetMovementCosts. Retail reads RCX (the Map), RDX (the pathfinder) and
// R8D (the profile) only (0x14009e55a / 0x14009e55d / 0x14009e560); the tree's fourth
// argument was never read.
void Map::SetMovementCosts(PathfinderThread* pathfinder, int profile) {
    PathfinderThread* pf = pathfinder;
    uint8_t roadFlag;

    if (profile == 0) {
        pf->SetStepCost(0, 3);
        pf->SetStepCost(1, 3);
        pf->SetStepCost(2, 4);
        pf->SetStepCost(3, 8);
        pf->SetStepCost(4, 0x10);
        pf->SetStepCost(5, 0x10);
        pf->SetStepCost(6, 0x10);
        pf->SetStepCost(7, 0x10);
        pf->SetStepCost(8, 0x10);
        pf->SetStepCost(9, 3);
        pf->SetStepCost(10, 4);
        roadFlag = 1;
    } else if (profile == 1) {
        pf->SetStepCost(0, 0x10);
        pf->SetStepCost(1, 0x10);
        pf->SetStepCost(2, 4);
        pf->SetStepCost(3, 4);
        pf->SetStepCost(4, 0x10);
        pf->SetStepCost(5, 0x10);
        pf->SetStepCost(6, 0x10);
        pf->SetStepCost(7, 4);
        pf->SetStepCost(8, 4);
        pf->SetStepCost(9, 4);
        pf->SetStepCost(10, 4);
        roadFlag = 1;
    } else if (profile == 2) {
        for (int i = 0; i <= 10; i++)
            pf->SetStepCost(i, 1);
        roadFlag = 0;
    } else if (profile == 3) {
        pf->SetStepCost(0, 10);
        pf->SetStepCost(1, 10);
        pf->SetStepCost(2, 10);
        pf->SetStepCost(3, 10);
        pf->SetStepCost(4, 10);
        pf->SetStepCost(5, 10);
        pf->SetStepCost(6, 10);
        pf->SetStepCost(7, 5);
        pf->SetStepCost(8, 1);
        pf->SetStepCost(9, 5);
        pf->SetStepCost(10, 10);
        roadFlag = 1;
    } else {
        return;
    }

    this->pathfinder->SetSearchFlags(roadFlag, 0, 0);   // 0x1400a68b0 (DL, R8B = 0, R9B = 0)
}


// ============================================================
// FUN_1400a6d50 — MoveTo_helper_6d50 (CopyPathFromFinder)
// ============================================================
// PathfinderThread::TakePath: copies up to maxNodes path cells from the ticket's result
// slot into the caller's path array (retail re-reads the ticket and the slot table on
// every element), then frees the slot (token 0, ready 0, path emptied) and resets the
// ticket to {-1, 0}. Returns the number of cells copied (min(count, maxNodes)). No lock.
int PathfinderThread::TakePath(PathTicket* ticket, int32_t* path, int maxNodes) {
    if (g_pfProfOn) PfProf_Taken(ticket->slot);   // [PFBENCH] enqueue -> taken latency (stress harness only)
    int count = results.data[ticket->slot].path._size;
    if (maxNodes < count) count = maxNodes;

    if (count > 0) {
        for (int i = 0; i < count; i++)
            path[i] = results.data[ticket->slot].path._data[i];
    }

    results.data[ticket->slot].token = 0;
    results.data[ticket->slot].ready = 0;
    // 0x1400021d0 == Collection::Array<int32>::SetSize (1400a6df3); SetSize(0) never grows.
    results.data[ticket->slot].path.SetSize(0);

    ticket->slot = -1;                 // one qword store of {-1, 0} in retail (H7)
    ticket->token = 0;
    return count;
}


// ============================================================
// FUN_14009cfb0 — PathfinderQuery (cell-to-cell reachability + path-cost build)
// ============================================================
// Refreshes connectivity (FUN_14009c480), then validates that the two cells share a
// compatible terrain pass-type / road overlay; if so it programs the path query via
// FUN_1400a6b20 + TradeComponent_impl_6c10 (the actual A* setup) and returns the
// out-result buffer. On failure it writes the "no path" sentinel (out[0]=-1,out[1]=0).
//   param_1 = map grid (int*). cell records: array at grid+0x20 (int-units => *(grid+0x20)
//     read as longlong), stride 0x30, pass byte +0x29, region short +0x26.
//   grid+0x1a (int-units) => *(grid+0x1a) read as Thread* = pathfinder owner.
//   param_3 (uint64) = terrain pass flags. param_4 = destCell, param_5 = srcCell.
// Caller signature: void* fn(void* grid, void* outResult, uint64_t flags, uint32_t dest, uint32_t src)
//
// B.4 ACT-a: Map::RequestPath. The zone refresh 0x14009c480 is Map::FillZones; the two
// pathfinder calls are PathfinderThread::SetPassableTypes / ::RequestPath.
PathTicket* Map::RequestPath(PathTicket* out, uint64_t flags, uint32_t destCell, uint32_t srcCell) {
    FillZones();   // 0x14009c480 (RCX only)

    const MapCell* recSrc  = &cells.GetData()[static_cast<int>(srcCell)];
    const MapCell* recDest = &cells.GetData()[static_cast<int>(destCell)];

    uint32_t typeSrc  = Bits(PathBitOf(Ext::WalkOnIce::TypeOf(*recSrc)));
    uint32_t typeDest = Bits(PathBitOf(Ext::WalkOnIce::TypeOf(*recDest)));
    uint32_t mask = static_cast<uint32_t>(flags) | typeDest | typeSrc;

    bool reachable = (mask & typeDest) && (mask & typeSrc);
    int16_t rd = static_cast<int16_t>(recDest->zone);
    int16_t rs = static_cast<int16_t>(recSrc->zone);
    if (reachable && (mask & Bits(PathBits::Zoneless)) == 0) {
        if (rd == 0 || rs == 0 || rd != rs)
            reachable = false;
    }


    if (!reachable) {
        out->token = 0;
        out->slot = -1;
        return out;
    }

    if ((mask & 0x180u) != 0)
        mask |= 0x180u;

    // Retail leaves R8 / R9 holding the two cell records here; 0x1400a6b20 reads EDX only.
    pathfinder->SetPassableTypes(static_cast<int>(mask));   // 0x1400a6b20
    pathfinder->RequestPath(out, static_cast<int>(destCell), static_cast<int>(srcCell));   // 0x1400a6c10
    return out;
}


// ============================================================
// FUN_1400c8c10 — MoveTo_helper_8c10 (PathSetup: world pos -> cells -> query)
// ============================================================
// Converts the target world position (param_2) and the citizen's current world
// position (self+0x38/+0x3c) to clamped grid cells, and either marks "arrived"
// (same cell) or programs the terrain costs + pathfinder query for the move.
//   gw+0xf8 -> map grid (int*); grid[0] = width; center = width>>1 (float).
//   self+0x60 = path index (reset -1), +0x64 = arrived flag, +0x68 = arrived cell,
//   self+0x470 = movement profile, self+0x5c = terrain flags, self+0x20 = result store.
// RCX self, RDX worldPos: the caller's R8 / R9 are never read.
//   (returns a truthy nonzero; original returns (1 in low byte) | high garbage.)
bool MoveTo::PathSetup(const float* worldPos) {
    Map* grid = GameUpdateLoop::_singleton->m_game;
    int width = grid->size;
    float center = static_cast<float>(width >> 1);

    auto clamp = [&](float f) -> int {
        return ActionBounds::WorldToCellAxis(f, width);   // CVTTSS2SI, then [0, width-1]
    };

    int tx = clamp(center + worldPos[0]);
    int ty = clamp(worldPos[1] + center);
    uint32_t destCell = static_cast<uint32_t>(width * ty + tx);

    int cx = clamp(destination.x + center);   // +0x38
    int cy = clamp(destination.y + center);   // +0x3C
    uint32_t srcCell = static_cast<uint32_t>(width * cy + cx);

    pathIndex = -1;

    if (destCell == srcCell) {
        pathCount = 1;
        path[0] = static_cast<int32_t>(destCell);
        pathIndex = 0;
    } else {
        grid->SetMovementCosts(grid->pathfinder, movementProfile);   // 0x14009e550
        // Original writes the query result into a 2-int stack scratch (local_res8),
        // then copies the 8-byte result into self+0x20.
        PathTicket scratch = {0, 0};
        PathTicket* res = grid->RequestPath(&scratch, passFlags, destCell, srcCell);   // 0x14009cfb0
        ticket = *res;
    }
    return true;   // retail: AL = 1 (the one caller ignores it)
}

// ============================================================
// FUN_140092260 — Bridge/spatial entity query near a position (quadtree walk)
//   *** NOW REIMPLEMENTED — src/game/Action/action_pf_roots_misc.cpp ***
// ============================================================
// Formerly skipped because of its frame-exact ~9.5KB local quadtree-walker. The
// pathfinding wave ported the walker's builder (18dd0) and subdivider (18880) as
// proper registered functions operating on a passed walker pointer, so 92260 was
// reimplemented (Pf_SpatialProximityQuery_92260) with a same-size local buffer
// `alignas(16) uint8_t walker[0x2438]` and disasm-verified record offsets, calling
// the registered 18dd0/18880. Registered via RegisterPfRootsMiscBatch.
