// src/game/Player/roadtool_calcpath.cpp
// RoadTool COMMIT path — the half of road placement that actually builds road.
//
// DragTool_OnKey dispatches vtable slot 13 (+0x68) on drag-release to commit whatever
// the drag previewed. RoadTool's slot 13 was a Tool_Ret0 stub, so a road drag always
// reported "invalid" (error sound, nothing placed) even though slot 14 (CalcCost) had
// already priced a valid path. This file ports slot 13 and the three exe helpers it
// needs; every deeper leaf was already ported + registered by earlier waves.
//
// Ported here (all verified against the Application-x64.exe disassembly):
//   0x140104d30  RoadTool::CalcPath            — slot 13, validate + place the run
//   0x14009c960  RoadTool_PlaceRoadSegment     — one cell: BuildRoad work + queue + BST
//   0x1400acd90  RoadTool_DecalStore           — record the road's decal by road type
//   0x140026990  RoadTool_FindMapDescription   — find MapDescription on the descriptor
//
// Leaves (every one is called by name since B3R; see the list below):
//   0x14009ce00 road A* (keystone_a_zone)      0x1400af370 road-mesh rebuild (pf_road)
//   0x1400e4b50 BuildRoad 8-arg ctor           0x1400f72e0 Work base ctor (reg_work_base)
//   0x1400f88a0 work-scheduler insert          0x14001a4f0 AVL insert (reg_pf_avl)
//   0x14009c700 road-cell removal handler      0x140001bb0/0x1400021d0 array resizers
//
// Map-grid layout used below (mapGrid = gameWorld+0xf8):
//   +0x00 gridSize · +0x80 terrain cell array (stride 0x30) · +0xe8 pending-road BST root
// Terrain cell (stride 0x30):
//   +0x00 occupying entity ptr · +0x28 road-adjacency count · +0x29 terrain type byte
//   +0x2a road type (0xff = none) · +0x2b road state (0x80 = queued)

#include "Player/roadtool_funcs.h"
#include "Player/dragtool.h"   // Tool + DragTool (ENDGAME B.3.8 TOOL)
#include <new>
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // RoadTool_sub_ce00 (0x14009ce00), called by name
#include "Player/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Work/buildroad_funcs.h"
#include "Work/BuildRoad.h"   // BuildRoad_CtorParams_Impl returns the BuildRoad (a Work)
#include "graphics/Material.h"   // Graphics::MaterialInstance is-an Object::External (the decal store)
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include "Object/factory_registration.h"
#include "Component/DecalDescription.h"
#include "Component/MapDescription.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/ComponentDescription.h"
#include "Component/CreateRoadDescription.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "Game/PlayerToolManager.h"
#include "Game/WorkScheduler.h"   // WorkScheduler::Insert (0x1400f88a0)
#include "Action/PathAvl.h"
#include "cell_trace.h"   // red square: BANISHED_CELL_TRACE (src/diag/cell_trace.h)

namespace {

// ---- leaves, called by name ---------------------------------------------------
//   0x1400e4b50 BuildRoad_CtorParams_Impl (Work/buildroad_funcs.h)
//   0x1400f88a0 CitizenAI_WorkScheduler_Insert (Component/exe_bodies.h)
//   0x14001a4f0 AvlInsert28 (Action/PathAvl.h)
//   0x14009c700 RemovalCalc_helper_c700 (Player/exe_bodies.h) -- road-cell removal
//   0x140001bb0 / 0x1400021d0 Reimpl_Array_Resize_Stride08 / _Stride04
//               (Game/exe_bodies.h)
//   0x14009ce00 RoadTool_sub_ce00 (Action/exe_bodies.h) -- the road A*; returns the
//               path length

// Shared description scan: walk the ComponentDescription's sub-descriptions (+0x28
// ptr / +0x30 count), match Type() (vtable +0x10) against T's id, confirm with
// IsDerivedFrom. The exact shape both 0x140026990 and CalcPath's inline
// DecalDescription loop use — including the `break` on first type-id match.
template <class T>
T* FindDescription(const ComponentDescription* entity) {
    const uint32_t typeId = T::TypeId();
    if (!entity || !typeId) return nullptr;
    int count = entity->_subDescCount;
    Description** comps = entity->_subDescs;
    if (!comps) return nullptr;
    for (int i = 0; i < count; ++i) {
        Description* comp = comps[i];
        if (!comp) continue;
        if (comp->Type() == typeId) {
            return Object::IsDerivedFromNullSafe(comp, typeId) ? static_cast<T*>(comp) : nullptr;
        }
    }
    return nullptr;
}

// The pending-road BST at Map +0xE8 (AvlNode28: key +0x00, left +0x10, right +0x18).
bool PendingRoad(const Map* map, int cellIdx) {
    const AvlNode28* node = map->field_0xE8.root;
    while (node) {
        int key = node->key;
        if (cellIdx < key)      node = node->left;
        else if (cellIdx > key) node = node->right;
        else return true;
    }
    return false;
}

}  // namespace

// ============================================================================
// 0x1400acd90 — record the decal's material for a road type in the terrain decal
// system (WorldGeometry, GameUpdateLoop +0x110): the External* array at +0x210
// (count +0x218) with a parallel int array at +0x220 (count +0x228); both grow to
// `index+1` and the new int slots are zeroed.
// ============================================================================
void (*WorldGeometry::_extMaterialTaken)(const Graphics::MaterialInstance* material) = nullptr;

void WorldGeometry::StoreBatchMaterial(int index, Graphics::MaterialInstance* decal) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): the batch material's textures
    // are queued for their first-use creates (WorldGeometry.h has the contract).
    if (_extMaterialTaken != nullptr) _extMaterialTaken(decal);
    RawArray<Graphics::MaterialInstance*>& refs = _batchMaterials;   // +0x210
    Collection::Array<int32_t>& ints = _batchCounters;     // +0x220
    int oldCount = refs.size;   // +0x218
    int needed   = index + 1;
    if (oldCount < needed) {
        Reimpl_Array_Resize_Stride08(&refs, needed);
        ints.SetSize(needed);   // 0x1400021d0
        // Zero the freshly grown tail of the int array. Retail bounds this by the int
        // array's own dword at +0x228 (0x1400acdd8 / 0x1400ace01: CMP EBX,[RDI+0x228]) -- the
        // low dword of its {data, size|capacity} header, i.e. its size -- NOT by `needed`.
        int intCount = ints._size;   // +0x228
        int32_t* intBase = ints._data;
        if (intBase) {
            for (int i = oldCount; i < intCount; ++i)
                intBase[i] = 0;
        }
    }
    Graphics::MaterialInstance** ptrBase = refs.data;
    if (ptrBase) ptrBase[index] = decal;
}

// ============================================================================
// 0x14009c960 — place ONE road cell.
// Queues a BuildRoad work item for the cell (citizens build it), records it in the
// pending-road BST, and stamps the cell's road type + queued state. The leading
// `roadType < existing` test is what makes re-drawing the same or a lower-tier road
// over an existing one a no-op (0xff = "no road" sorts above every real type).
// ============================================================================
void Map::PlaceRoadCell(uint32_t cellIdx,
                                                uint8_t roadType, uint64_t /*endCell*/,
                                                uint32_t mapDescVal, ComponentDescription* roadDescExtra) {
    MapCell* cell = &cells.GetData()[(int)cellIdx];   // 0x14009c992: [RCX+0x80], no test

    // Only upgrade: bail unless the new road type outranks what is already there.
    if (!(roadType < cell->field_0x2A)) return;

    // If this cell already has a pending road entry, clear it first.
    if (PendingRoad(this, (int)cellIdx)) {
        RemovePendingRoad((int)cellIdx);
    }

    // Bump the cell's road-adjacency counter, but only for a cell that is plain ground
    // (terrain bits 0/1 clear), currently roadless (0x2a == 0xff), and not already
    // pending in the BST.
    {
        const MapCell* tb = &cells.GetData()[(int)cellIdx];
        uint32_t terrainFlag = Bits(PathBitOf(tb->terrainType));
        if ((terrainFlag & Bits(PathBits::Fast)) == 0 && (terrainFlag & Bits(PathBits::Faster)) == 0 &&
            tb->field_0x2A == MapCell::kNoRoadBatch) {
            bool pending = PendingRoad(this, (int)cellIdx);
            if (!pending) {
                CellTrace::NoteCount(this, cell, cell->footprintCount, 1, "PlaceRoadCell");
                cell->footprintCount = static_cast<uint8_t>(cell->footprintCount + 1);
            }
        }
    }

    // Build the BuildRoad work item (0x70 bytes) and hand it to the work scheduler.
    ComponentEntity* ownerIbp = nullptr;   // retail seeds a null InternalBasePtr
    Work* work = nullptr;
    BuildRoad* mem = static_cast<BuildRoad*>(System::MemoryAllocator::MemAlloc(0x70, 8));
    if (mem) {
        new (&ownerIbp) ::Object::InternalBasePtr(nullptr);   // the IBP ctor 0x140152a90
        work = new (mem) BuildRoad(&ownerIbp, cellIdx,   // 0x1400e4b50
                                   mapDescVal, roadDescExtra,
                                   cell->field_0x2A,      // existing road type
                                   cell->field_0x2B,      // existing road state
                                   static_cast<uint8_t>(cell->terrainType));    // terrain type
    }

    // 0x14009ca33 tests only MemAlloc's result (to skip the ctor); the object or null
    // is wrapped (0x14009ca93) and posted, the BST insert and the stamp follow: no test.
    Object::InternalPtr<Work> workIbp(work);

    // Post to the work scheduler (GameUpdateLoop +0x118). code 0x2000 = the road
    // category; retail passes fromExisting='\0' and prio=-1 (the prio is dead here).
    // 0x14009caaf-0x14009cabd: the loop and the scheduler read with no test.
    GameUpdateLoop::_singleton->m_tutorial->Insert(&workIbp, 0x2000, '\0', -1);   // 0x1400f88a0

    // Record the cell in the pending-road BST (0x14001a4f0; the inserted node comes
    // back through the out slot, which retail points at the dead owner IBP local).
    {
        int32_t key = static_cast<int32_t>(cellIdx);
        AvlNode28* inserted = nullptr;
        AvlInsert28(&field_0xE8, &inserted, &key);   // Map +0xE8, the Set header
    }

    cell->field_0x2A = roadType;
    cell->field_0x2B = 0x80;   // queued-for-construction marker
}

// ============================================================================
// 0x140104d30 — RoadTool::CalcPath (vtable slot 13, +0x68).
// Returns true when the road run was committed. Retail's failure paths all return a
// value whose low byte is 0, which DragTool_OnKey reads as false (error sound).
// ============================================================================
bool RoadTool::CalcPath(int startCell, int endCell) {
    if ((int)startCell < 0 || (int)endCell < 0) return false;

    // Sort so sc <= ec.
    uint32_t sc = startCell, ec = endCell;
    if ((int)ec < (int)sc) { sc = endCell; ec = startCell; }

    if (GameUpdateLoop::_singleton == nullptr) return false;
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    Map* mapGrid = gameWorld->m_game;
    if (!mapGrid) return false;
    const MapCell* terrainBase = mapGrid->cells.GetData();
    if (!terrainBase) return false;

    // Both endpoints must be road-legal terrain and unoccupied.
    const MapCell* cellA = &terrainBase[(int)sc];
    const MapCell* cellB = &terrainBase[(int)ec];
    uint32_t flagA = Bits(PathBitOf(cellA->terrainType));
    uint32_t flagB = Bits(PathBitOf(cellB->terrainType));
    if ((flagA & Bits(PathBits::NoRoad)) != 0 || (flagB & Bits(PathBits::NoRoad)) != 0) return false;
    if (cellA->blocker != nullptr || cellB->blocker != nullptr) return false;

    // Honour the owner's fixed-endpoint constraint (used by scripted/tutorial roads).
    const PlayerToolManager* controller = dialogPtr;
    if (controller && controller->field_0x80) {
        int gridSize = mapGrid->size;
        if (gridSize != 0) {
            int a = controller->field_0x98;
            int b = controller->field_0x9C;
            uint32_t ca = (uint32_t)(gridSize * (a / gridSize) + a % gridSize);
            uint32_t cb = (uint32_t)(gridSize * (b / gridSize) + b % gridSize);
            if ((ca != sc || cb != ec) && (ca != ec || cb != sc)) return false;
        }
    }

    // Run the road A* over the span (same call CalcCost makes, mask 0xa17).
    int pathBuf[2048];
    int pathLen = 0;
    pathLen = mapGrid->FindRoadPath(PathBits::RoadRoute, sc, ec, nullptr, pathBuf, modifierFlag, 1);
    if (pathLen == 0) {
        return false;
    }

    const ComponentDescription* descEntity = descriptionPtr;
    const CreateRoadDescription* roadDesc  = this->roadDesc;
    if (!descEntity || !roadDesc) return false;

    // The decal the finished road paints, and the MapDescription value each segment gets.
    const DecalDescription* decalComp = FindDescription<DecalDescription>(descEntity);
    if (!decalComp) {
        return false;
    }
    Graphics::MaterialInstance* decal = decalComp->_materials[0];   // the first ExternalPtr of the +0x28 array

    MapDescription* mapDesc = nullptr;
    mapDesc = descEntity->FindDescription<MapDescription>();   // 0x140026990
    if (!mapDesc) {
        return false;
    }
    uint32_t mapDescVal = mapDesc->_cellType;
    // +0x20 is the bool CreateRoadDescription serializes; retail reads its byte (0 or 1).
    const uint8_t roadType       = static_cast<uint8_t>(roadDesc->_b20);
    ComponentDescription* const roadDescExtra = roadDesc->_created;

    WorldGeometry* decalMgr = gameWorld->m_worldGeometry;
    decalMgr->StoreBatchMaterial((int)roadType, decal);

    for (int i = 0; i < pathLen; ++i) {
        mapGrid->PlaceRoadCell((uint32_t)pathBuf[i], roadType,
                                        (uint64_t)ec, mapDescVal, roadDescExtra);
    }

    // Retail 0x140104f7f (the call right after the PlaceRoadCell loop, 0x140104f61): the road mesh is built
    // again once every cell is placed. A queued cell (state 0x80) is drawn by the mesh, translucent, until a
    // builder finishes it; without this call the drawn road stays invisible until the first cell is finished
    // (FinishRoadCell rebuilds the mesh).
    decalMgr->RebuildRoadMesh(mapGrid);   // 0x1400af370

    return true;
}