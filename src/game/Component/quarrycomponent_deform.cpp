// src/game/Component/quarrycomponent_deform.cpp
// The quarry pit — terrain heightmap deformation driven by production progress.
//
// The pit a quarry digs is a real carve of the terrain HEIGHTMAP, not a mesh or a
// decal, and it DEEPENS as the quarry produces. Template/Quarry.rsc:
//
//     QuarryDescription quarry {
//         PickingMesh _floorMesh = "Models/Buildings/Quarry/QuarryFloor.rsc";  // desc +0x20
//         float _maxDepth     = -7.0;                                          // desc +0x2c
//         float _initialDepth = -1.4;                                          // desc +0x28
//     }
//
// Two entry points drive it, both previously unreachable because QuarryComponent was
// spawned by COMPONENT_INERT_FACTORY (shared stub vtable — nothing in it ran):
//
//   slot [12] 0x1400550f0, gated on msg 4 (build complete)
//        -> carve once at _initialDepth
//   slot [15] 0x140054b80, the ConsumeProduce message sink
//        (Quarry.rsc: `ObjectType _messageType = QuarryComponent; int _messageId = 1;`
//         — every completed production cycle sends it)
//        -> re-carve at lerp(_initialDepth, _maxDepth, produced/_maximumProduction)
//           then convert the dug cells' terrain type to 5 (quarried rock)
//
// Same shape as the tunnel TerraformComponent carve, and it reuses that wave's
// heightmap idiom deliberately (see src/game/Component/terraformcomponent.cpp).
//
// Closure — every callee was already ported AND registered by an earlier wave:
//   0x14000b630 FindComponent<Decal>   (reg_destroy_sm)
//   0x14000b6f0 FindConsumeProduce     (reg_action_child_ctors2)
//   0x140003f60 FindComponent<Model>   (reg_action_cnr)
//   0x140004680 FindComponent<Map>     (reg_component_nature)
//   0x14009f3e0 MapGetTerrainPos       (mapquery)
//   0x1400aaa20 cell-quad ray          (reg_pf_road)   0x1400aa810 ray/tri (reg_pf_geom)
//   0x1400ab150 region recalc          (reg_component_gap_D)
//   0x1400a8c90 fine-cell dirty        (reg_pf_road)   0x1400a6fe0 pf cmd2 (reg_pf_astar)
//
// Decompiled from Application-x64.exe; the slope test and the cell-stamp tail are
// pinned against the disassembly (see the notes at each site).

#include "Component/QuarryComponent.h"
#include "Component/ConsumeProduceDescription.h"
#include "Component/QuarryDescription.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "Game/terrain_sidecar.h"
#include "Object/factory_registration.h"
#include "picking/PickingMesh.h"   // ENDGAME B.3.7 callers (K2)
#include "Action/exe_bodies.h"      // Pf_CellQuadRay_aa20
#include "Action/PathSearch.h"       // RegionGraph::MarkCellDirty
#include "Game/PathfinderThread.h"   // PathfinderThread::SetCellTerrain
#include "Component/ModelComponent.h"
#include "Component/MapComponent.h"
#include "Component/DecalComponent.h"
#include "Component/ConsumeProduceComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Game/WorldGeometry.h"
#include "Game/Map.h"
#include "Game/exe_constants.h"
#include "Component/terraformcomponent_funcs.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store

namespace {

float RdataF32(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

// .rdata constants:
//   0x140172100 = 0.25   vertex bias      0x140172114 = 0.5    cell-centre bias
//   0x140172230 = 256.0  heightmap scale  0x140172280 = -1.0   ray direction (down)
const float s_bias = kRdata_140172100, s_centreBias = kRdata_140172114;
const float s_heightScale = RdataF32(kRdata_140172230), s_rayDown = kRdata_140172280;
// Slot15's slope cutoff, retail .rdata 0x140172130 = 0x3f59999a = 0.85f (read 2026-10-11;
// loaded into XMM13 at 0x140054da4, tested `COMISS XMM13,[probe+8] / JBE`). worldgen.cpp's
// grass slope band reads the same float.
constexpr float s_slopeCut = 0.85f;
// Slot15's negative corner offset, retail .rdata 0x140172274 = 0xbe800000 = -0.25f (read
// 2026-10-11; XMM10 at 0x140054d80). The positive one is 0x140172100 = 0.25f (XMM9 at
// 0x140054d56), the s_bias constant above.
constexpr float s_cornerNeg = -0.25f;


// Every way the carve can silently do nothing gets named once. A deformation that
// never happens leaves no trace otherwise, and every failure mode here is "a pointer
// or type id resolved to zero" — the class of bug that reads as "the feature is
// broken" when it is really "the feature never ran".
void QuarryBail(const char* where, const char* why) {
    static int n = 0;
    if (n >= 12) return;
    ++n;
    std::fprintf(stderr, "[Quarry] %s SKIPPED — %s\n", where, why);
    std::fflush(stderr);
}


// Retail reaches the QuarryDescription through the owning entity's DESCRIPTION list
// (ComponentEntity::_description -> _subDescs) — NOT the component array. The two
// lists are one deref apart and picking the wrong one finds nothing.
QuarryDescription* QuarryFindDescription(ComponentEntity* entity) {
    if (!entity) return nullptr;
    const uint32_t tid = QuarryDescription::TypeId();
    if (tid == 0) return nullptr;
    const ComponentDescription* descList = entity->_description;
    if (!descList) return nullptr;
    const int n = descList->_subDescCount;
    Description** arr = descList->_subDescs;
    if (n <= 0 || !arr) return nullptr;
    for (int i = 0; i < n; ++i) {
        Description* d = arr[i];
        if (d && d->IsDerivedFrom(tid)) return static_cast<QuarryDescription*>(d);
    }
    return nullptr;
}

// The heightmap edit is bracketed by DecalComponent suspend/resume — the footprint
// decal is projected onto the terrain, so it has to be rebuilt after the ground moves.
// vtable +0x38 = suspend (index 7), +0x30 = resume (index 6).
DecalComponent* QuarryDecalSuspend(ComponentEntity* entity) {
    DecalComponent* decal = entity ? entity->FindComponent<DecalComponent>() : nullptr;   // 0x14000b630
    if (!decal) return nullptr;
    decal->OnRemove();   // slot 7
    return decal;
}
void QuarryDecalResume(DecalComponent* decal) {
    if (!decal) return;
    decal->OnAdded();    // slot 6
}

}  // namespace

// ============================================================
// QuarryComponent::ApplyDepth (0x1400549c0)
// ============================================================
// Walks the QuarryDescription's _floorMesh, transforms every vertex by the owner's
// ModelComponent placed transform, clamps each vertex Z up to `depth`, and writes the
// result into the terrain heightmap.
//
// The clamp is what makes the pit deepen: `depth` starts at _initialDepth (-1.4) and
// falls toward _maxDepth (-7.0), so as it drops, progressively more of the floor
// mesh's low geometry is allowed through and the floor sinks.
//
// As retail: the description is narrowed with IsDerivedFrom (null when it is not a
// QuarryDescription) and its +0x20 read untested; the ModelComponent, the world's
// terrain (+0x110), the mesh and the height array are all used with no test.
void QuarryComponent::ApplyDepth(float depth) {
    Description* descRaw = _description;
    QuarryDescription* desc = descRaw->IsDerivedFrom(QuarryDescription::TypeId())
        ? static_cast<QuarryDescription*>(descRaw) : nullptr;

    ModelComponent* model = _entity->FindComponent<ModelComponent>();   // 0x140003f60

    // Retail copy-constructs a Transform on the stack (??0Transform@Math@@QEAA@AEBV01@@Z,
    // slot 0x140151418) from the ModelComponent's transform.
    const Math::Transform xform(model->transform);

    const Picking::PickingMesh* mesh = desc->_floorMesh;   // +0x20
    WorldGeometry* terrain = GameUpdateLoop::_singleton->m_worldGeometry;   // +0x110

    int16_t* heightArray = terrain->_heights.data;   // +0xD8 the height array
    const int       stride      = terrain->_fieldD0;
    const float     half        = static_cast<float>(terrain->_gridSize >> 1);

    // Picking::PickingMesh::Vertices (import slot 0x140151478) — a reference return.
    const int vertCount = mesh->Vertices(0).GetSize();

    for (int i = 0; i < vertCount; ++i) {
        // Re-query per iteration exactly as retail does.
        const ::Vector3* data = mesh->Vertices(0).GetData();

        // Transform::operator*(const Vector3&) — slot 0x140151470. LEFT: the picking
        // mesh's element is the global engine Vector3, Math::Vector3's layout twin.
        const Math::Vector3 world =
            xform * data[i];   // ::Vector3 is Math::Vector3 (engine/types/common.h alias)

        float z = world.z;
        if (z < depth) z = depth;          // floor clamp — this IS the progressive dig

        const int mapX = static_cast<int>(world.x + s_bias + half);
        const int mapY = static_cast<int>(world.y + s_bias + half);

        heightArray[static_cast<int64_t>(stride) * mapY + mapX] =
            static_cast<int16_t>(static_cast<int>(z * s_heightScale));
        StillTerrain::Bump();   // [STILLTERRAIN] a heightmap word was written

        terrain->MarkChunksDirty(mapX, mapY);   // 0x140054b2e CALL 0x1400ab150, RCX the terrain (gameloop+0x110), no chunk-size test
    }

}

// ============================================================
// QuarryComponent slot [12] (0x1400550f0) — message sink
// ============================================================
// msg 4 = build complete: carve the pit once at _initialDepth.
void QuarryComponent::OnEvent(int msgId) {
    if (msgId != 4) return;
    ComponentEntity* entity = _entity.Get();

    QuarryDescription* desc = QuarryFindDescription(entity);
    if (!desc) { QuarryBail("Slot12(msg4)", "no QuarryDescription in the entity description list"); return; }

    const float initialDepth = desc->_initialDepth;

    DecalComponent* decal = QuarryDecalSuspend(entity);
    ApplyDepth(initialDepth);
    QuarryDecalResume(decal);

}

// ============================================================
// QuarryComponent slot [15] (0x140054b80) — ConsumeProduce message sink
// ============================================================
// Quarry.rsc wires this up: `_messageType = QuarryComponent; _messageId = 1;`, so every
// completed stone-production cycle lands here. Two halves:
//   1. re-carve the floor at lerp(_initialDepth, _maxDepth, produced/_maximumProduction)
//   2. walk the footprint and convert the dug cells' terrain type to 5 (quarried rock)
void QuarryComponent::OnMessage(int /*message*/, void* /*data*/, uint32_t /*arg0*/, uint32_t /*arg1*/) {
    ComponentEntity* entity = _entity.Get();

    // ---- production progress -> depth -------------------------------------------
    ConsumeProduceComponent* cp = entity ? entity->FindComponent<ConsumeProduceComponent>() : nullptr;   // 0x14000b6f0
    if (!cp) { QuarryBail("Slot15", "owner has no ConsumeProduceComponent"); return; }
    Description* cpDescRaw = cp->_description;
    const uint32_t tidCP = ConsumeProduceDescription::TypeId();
    ConsumeProduceDescription* cpDesc = (cpDescRaw && tidCP && cpDescRaw->IsDerivedFrom(tidCP))
                 ? static_cast<ConsumeProduceDescription*>(cpDescRaw) : nullptr;
    if (!cpDesc) { QuarryBail("Slot15", "ConsumeProduce +0x18 is not a ConsumeProduceDescription"); return; }

    QuarryDescription* qDesc = QuarryFindDescription(entity);
    if (!qDesc) { QuarryBail("Slot15", "no QuarryDescription in the entity description list"); return; }

    const int produced = cp->consumeState;   // +0x68: the production count this sink reads
    const int maxProd  = cpDesc->_i48;       // +0x48 _maximumProduction
    // Retail divides unguarded; _maximumProduction is authored non-zero on every quarry
    // template. Guarded only because a zero here would be OUR resolution failure.
    if (maxProd == 0) { QuarryBail("Slot15", "cpDesc+0x48 _maximumProduction is 0"); return; }

    const float t            = static_cast<float>(produced) / static_cast<float>(maxProd);
    const float initialDepth = qDesc->_initialDepth;
    const float maxDepth     = qDesc->_maxDepth;
    const float depth        = (1.0f - t) * initialDepth + t * maxDepth;

    DecalComponent* decal = QuarryDecalSuspend(entity);
    ApplyDepth(depth);
    QuarryDecalResume(decal);

    // ---- footprint -> terrain type 5 --------------------------------------------
    if (GameUpdateLoop::_singleton == nullptr) return;
    WorldGeometry* terrainGrid = GameUpdateLoop::_singleton->m_worldGeometry;
    Map* tileGrid = GameUpdateLoop::_singleton->m_game;
    if (!terrainGrid || !tileGrid) { QuarryBail("Slot15", "gameWorld terrain/tile grid null"); return; }

    MapComponent* map = entity ? entity->FindComponent<MapComponent>() : nullptr;   // 0x140004680
    if (!map) { QuarryBail("Slot15", "owner has no MapComponent (finder 0x140004680 returned null)"); return; }
    const int baseX = map->tileX0;
    const int baseY = map->tileY0;
    const int rectW = map->tileX1;
    const int rectH = map->tileY1;

    MapCell* tileData = tileGrid->cells.GetData();
    if (!tileData) { QuarryBail("Slot15", "tileGrid+0x80 cell array null"); return; }
    const float   halfTerrain = static_cast<float>(terrainGrid->_gridSize >> 1);

    int converted = 0;
    for (int row = 0; row < rectH; ++row) {
        const int absRow = baseY + row;
        for (int col = 0; col < rectW; ++col) {
            for (int k = 0; k < 4; ++k) {
                // The four quarter-cell corner offsets in retail's order (0x140054df4-
                // 0x140054e4a: XMM6 = x, XMM7 = y): (-0.25,-0.25) (-0.25,0.25)
                // (0.25,-0.25) (0.25,0.25).
                const float ox = (k == 2 || k == 3) ? s_bias : s_cornerNeg;
                const float oy = (k == 1 || k == 3) ? s_bias : s_cornerNeg;

                const int gw = tileGrid->size;
                if (gw <= 0) break;
                const int   cellIdx = gw * absRow + baseX + col;
                const float centre  = static_cast<float>(gw >> 1) - s_centreBias;

                float cellPos[3] = { static_cast<float>(cellIdx % gw) - centre,
                                     static_cast<float>(cellIdx / gw) - centre, 0.0f };
                float terrain[3] = { 0.0f, 0.0f, 0.0f };
                tileGrid->GetTerrainPos(terrain, cellPos, '\0', nullptr);

                const float px = terrain[0] + ox;
                const float py = terrain[1] + oy;

                const int cellX = static_cast<int>(px + halfTerrain);
                const int cellY = static_cast<int>(py + halfTerrain);

                float dir[3]    = { 0.0f, 0.0f, s_rayDown };      // straight down
                float origin[3] = { px, py, 200.0f };
                float maxT      = 200.0f;
                float outHit[3] = { 0.0f, 0.0f, 0.0f };
                float outT      = 0.0f;
                // p9 is a 3-float out-param and the SLOPE TEST reads p9[2], not the hit
                // point. Pinned to the disassembly: the args set p9 = RBP-0x30, and the
                // test at 0x140054fb9 is `COMISS XMM13,[RBP-0x28] / JBE`, i.e.
                // 0.85 > p9[2]. Retail zeroes p9[2] before every call, so a ray that
                // misses leaves 0.0 and still passes — replicated exactly; do not
                // "improve" this into a hit check.
                float probe[3]  = { 0.0f, 0.0f, 0.0f };

                terrainGrid->CellQuadRay(cellX, cellY,
                                    dir, origin, &maxT, outHit, &outT, probe);

                if (!(probe[2] < s_slopeCut)) continue;

                MapCell* cell = &tileData[static_cast<int64_t>(cellIdx)];
                const uint8_t  oldType = cell->terrainType;
                const uint32_t oldMask = 1u << (oldType & 0x1f);
                // newType 5 always lands in 0x1e0, so f2f0's two-sided boundary rule
                // reduces to "was the old type in 0xe1f" — which is what retail inlines.
                const uint8_t dirty = ((oldMask & 0xe1f) != 0) ? 1u : 0u;
                tileGrid->field_0xA2 |= dirty;
                cell->terrainType = 5;
                TerrainSidecar::NoteWrite(cell);   // [terrain sidecar] mirror
                CellStamp::NoteWrite(cell);   // [AMBMEMO] cell write clock

                if (dirty) {
                    RegionGraph* regionObj = tileGrid->regionGraph;
                    if (regionObj) regionObj->MarkCellDirty(cellIdx % gw, cellIdx / gw);   // 0x1400a8c90 (Pf_FineCellDirty_a8c90's body)
                }
                PathfinderThread* pfThread = tileGrid->pathfinder;
                if (pfThread)
                    pfThread->SetCellTerrain(static_cast<int>(cellIdx), 5);   // 0x1400a6fe0 (Pf_EnqueueCmd2_a6fe0's body)

                // MapCell::occupant (+0x08) is the ENTITY raster — the quarry claims the dug cell.
                cell->occupant = entity;
                CellStamp::NoteWrite(cell);   // [AMBMEMO] cell write clock
                ++converted;
            }
        }
    }

}
