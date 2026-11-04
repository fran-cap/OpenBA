#pragma once
// ============================================================================
// src/game/Game/Map.h — ENDGAME B.4 W0-H2
//
// The 0xF8-byte world map object at GameUpdateLoop+0xF8 (m_game): the tile
// grid, the map's own Picking::Scene, the pathfinder / region objects the
// tree seeds into it. Load builds it with MemAlloc(0xf8, 8) + ctor
// 0x14009ee70; serialized by 0x14009f100.
//
// CLASS NAME NOT SETTLED. The tree calls it "Game" (m_game); the profile
// build lists `Game\Map.cpp` and the tree's own comments call its functions
// Map::CreateNaturalResource / Map_CalcDistance. `Map` is used until the
// profile exe (Application-x64-profile.exe, not open in Ghidra) confirms it.
// No vtable. Load placement-news it on MemAlloc(0xf8, 8): the ctor 0x14009ee70
// (members pickingScene, cells, cellQueue constructed where retail builds them).
//
// FIELDS (retail ctor 0x14009ee70 unless noted; names only where retail's
// behaviour gives one, else a typed guess):
//   +0x00 int32  size        Serialize(int&) first (0x14009f100); squared
//                            as the cell count (size * size) by the loader
//   +0x08 Picking::Scene     embedded, Picking::Scene::Scene(+0x08): a real
//                            member (behaviour: B4 wave Y)
//   +0x80 cell array         Collection::Array<MapCell> (a real member, behaviour:
//                            B4 wave Y): data +0x80, count +0x88 (Serialize(int&)
//                            + resize 0x14001aa90), capacity +0x8C
//   +0x90 int16[9]           Serialize(9, short*) (0x14009f100); ctor zero
//   +0xA2 uint8_t            ctor 1; the loader sets it to 1
//   +0x68 / +0x70            the pathfinder worker and the coarse region graph
//                            (typed by B.4 ACT-a)
//   +0xA8..+0xE8             ONE pooled cell queue (Action/PathSearch.h), the
//                            flood fill 0x14009c170's work list (B.4 ACT-a);
//                            the ctor's qword store of 8 at +0xD0 is the queue
//                            pool's growCount (8) and stride (0)
//   +0xE8                    a Set header AvlTreeHead<AvlNode28>: root (children
//                            at node+0x10 / +0x18, int key at +0) and count +0xF0
//   every other qword / dword: zeroed by the ctor, meaning not settled.
//
// MapCell (B.4 GAME-a): the element of the cell array at +0x80, stride 0x30,
// align 8. Stride and fields from retail's readers:
//   resize 0x14001aa90 (copies +0x00..+0x24 and +0x26..+0x2D; init +0x18 = -1,
//     +0x2A = 0xFF), bulk serializer 0x14001ac10 (IBP, IBP, ExternalPtr,
//     Serialize(9, uchar*) at +0x1C, ushort +0x26, five uchar +0x28..+0x2C,
//     signed char +0x2D);
//   +0x00 blocker   the footprint rasterizer 0x14009d360 stores the entity for a
//                   non-'.' footprint cell and tells the path worker the cell
//                   is blocked (0x1400a6f00's argument `entity != 0`)
//   +0x08 occupant  0x14009dc20 stores the placed entity per footprint cell
//   +0x18 regionId  init -1 (0x14001aa90); the route query 0x14012cd80
//                   compares two cells' ids
//   +0x26 zone      path-region word; cleared and rebuilt by 0x14009c480
//   +0x28 footprintCount  0x14009d360 adds +1 / -1 for a '.' footprint cell;
//                   0x14009f100's load pass reads 0xFF as "unset"
//   +0x29 terrainType     the classify helper 0x14009e2c0 (2, 6, 7, 8, 10);
//                   tested as a bit index (1 << (t & 0x1F)) by the walk masks
//   +0x2D floorCount      signed: the Map query 0x14009f3e0 ray-picks the
//                   floor when > 0 (0x14009f4e9 CMP; JLE)
//   the rest keep typed guess names.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <array>
#include "Action/PathSearch.h"   // MapCellQueue, RegionGraph (B.4 ACT-a)
#include "picking/Scene.h"       // -Iruntime/types: Picking::Scene (by value at +0x08)
#include "collection/Collection.h" // Collection::Array<MapCell> (by value at +0x80)

class ComponentEntity;
class PathfinderThread;
class ComponentDescription;      // Component/ComponentDescription.h
class CreatePlacedDescription;   // Component/CreatePlacedDescription.h
struct GridTile;                 // Work/GridTile.h
namespace IO { class Stream; }
namespace System { class String; }

struct MapCell {
    Object::InternalPtr<ComponentEntity> blocker;    // +0x00 (0x14009d360)
    Object::InternalPtr<ComponentEntity> occupant;   // +0x08 (0x14009dc20)
    ComponentDescription* field_0x10;   // +0x10 ExternalPtr<ComponentDescription> (serialized by 0x140002340; typed B8 RT2)
    int32_t  regionId;               // +0x18 init -1; not serialized, copied on resize
    std::array<uint8_t, 9> field_0x1C;   // +0x1C Serialize(9, uchar*) (0x14001aca0); +0x22 read by 0x140115be0;
                                     //       indexed by influence state (0x14009f990); resize copies it whole
    uint8_t  _pad25;                 // +0x25 written by neither resize arm
    uint16_t zone;                   // +0x26 path-region word (Serialize ushort)
    uint8_t  footprintCount;         // +0x28 (Serialize uchar)
    uint8_t  terrainType;            // +0x29 (Serialize uchar)
    uint8_t  field_0x2A;             // +0x2A init 0xFF (Serialize uchar); the tree: road batch id
    uint8_t  field_0x2B;             // +0x2B (Serialize uchar); the tree: road blend / orientation
    uint8_t  field_0x2C;             // +0x2C (Serialize uchar); the tree: growth level (/255)
    int8_t   floorCount;             // +0x2D (Serialize signed char)
    uint8_t  _pad2E[2];              // +0x2E written by neither resize arm
};

static_assert(sizeof(MapCell) == 0x30, "MapCell stride 0x30 (0x14001aa90 / 0x14001ac10)");
static_assert(alignof(MapCell) == 8, "MapCell is qword-aligned");
static_assert(offsetof(MapCell, blocker) == 0x00, "MapCell::blocker @ +0x00");
static_assert(offsetof(MapCell, occupant) == 0x08, "MapCell::occupant @ +0x08");
static_assert(offsetof(MapCell, field_0x10) == 0x10, "MapCell +0x10");
static_assert(offsetof(MapCell, regionId) == 0x18, "MapCell::regionId @ +0x18");
static_assert(offsetof(MapCell, field_0x1C) == 0x1C, "MapCell +0x1C");
static_assert(sizeof(std::array<uint8_t, 9>) == 9, "MapCell +0x1C is nine bytes (+0x1C..+0x24)");
static_assert(offsetof(MapCell, _pad25) == 0x25, "MapCell +0x25");
static_assert(offsetof(MapCell, zone) == 0x26, "MapCell::zone @ +0x26");
static_assert(offsetof(MapCell, footprintCount) == 0x28, "MapCell::footprintCount @ +0x28");
static_assert(offsetof(MapCell, terrainType) == 0x29, "MapCell::terrainType @ +0x29");
static_assert(offsetof(MapCell, field_0x2A) == 0x2A, "MapCell +0x2A");
static_assert(offsetof(MapCell, field_0x2B) == 0x2B, "MapCell +0x2B");
static_assert(offsetof(MapCell, field_0x2C) == 0x2C, "MapCell +0x2C");
static_assert(offsetof(MapCell, floorCount) == 0x2D, "MapCell::floorCount @ +0x2D");
static_assert(offsetof(MapCell, _pad2E) == 0x2E, "MapCell +0x2E");

// The cell Array's element row (0x14001aa90's two arms: the partial assign and
// the patterned init; defined in gamegrid_serialize.cpp). Map::cells is a
// Collection::Array over it: retail's ctor zeroes the header, its dtor inlines
// the trivial-element ~Array (MemFree, then data / size+capacity zeroed).
extern const rtcoll::ArraySpec kSpec_MapCell;
using MapCellArray = Collection::Array<MapCell, Collection::DefaultAllocator,
                                       Collection::ExplicitSpec<&kSpec_MapCell>>;

class Map {
public:
    Map();                       // 0x14009ee70 (defined in gameupdateloop_load.cpp)
    ~Map();                      // 0x14009ed60 (keystone_a_gamedtor.cpp); Shutdown MemFrees the map after it
    void Serialize(IO::Stream& stream);   // 0x14009f100 (gamegrid_serialize.cpp)
    // NOT RETAIL (review F-026): retail has no such check. Does a loaded map record's cell
    // array (`count` cells) cover its edge `size`? Serialize refuses the load when it does not.
    // Retail keeps the saved `size` whatever the count: Serialize's walks (and the version < 2
    // fix-up's writes) and every later cell index the game forms run over size * size cells,
    // an edge past 46340 overflows the int32 product, and an edge of 0 reaches the game's
    // divisions by `size` (action_base_cnr.cpp and others). A valid save holds exactly
    // size * size cells (MapTileGrid_Build's cells.SetSize(dim * dim)) with a positive edge.
    // A count ABOVE size * size is accepted (no walk reaches the extra cells); the finding's
    // `count != size * size` would refuse it as well.
    static bool CellsCoverSize(int32_t size, int32_t count) {
        return size > 0 && static_cast<int64_t>(size) * size <= count;
    }
    void SortTilesByDistance(RawArray<GridTile>* tiles, int refCell) const;   // 0x14009d7c0 (Work/work_leaf_clear.cpp)

    int32_t  size;               // +0x00 map edge in cells (Serialize int&)
    uint8_t  _pad04[4];          // +0x04 not written by the ctor
    // +0x08: retail's ctor 0x14009ee70 runs ??0Scene@Picking@@ (import
    // 0x140151ff8) on it first; its dtor 0x14009ed60 tail-JMPs to ~Scene after
    // freeing everything else (cells and the cell queue in the body): the
    // compiler's member destruction after ~Map's body.
    Picking::Scene pickingScene; // +0x08
    PathfinderThread* pathfinder;  // +0x68 ctor 0; the worker (e8c0 MemAlloc(0x1f8) + 0x1400a67f0;
                                 //       0x140046b2c hands it to 0x1400a6fe0)
    RegionGraph* regionGraph;    // +0x70 ctor 0; the coarse region graph (e8c0 MemAlloc(0x108) +
                                 //       0x1400a8670; 0x140046b1b hands it to 0x1400a8c90)
    RegionPairCache* regionPairCache;   // +0x78 ctor 0; the region pair cache (Action/PathSearch.h:
                                 //       MemAlloc(0x38) in the e8c0 tail; Update hands it to
                                 //       0x1400a5d10, the dtor 0x14009ed60 frees it)
    // +0x80: Collection::Array<MapCell> (count Serialize int&, resize 0x14001aa90).
    // Retail's ctor zeroes it second-to-last of the four leading pointers (qword
    // +0x80, qword +0x88); its dtor frees it after cellQueue (0x14009ee2c..ee40):
    // the compiler's member destruction after ~Map's body.
    MapCellArray cells;          // +0x80
    int16_t  field_0x90[9];      // +0x90 Serialize(9, short*)
    uint8_t  field_0xA2;         // +0xA2 ctor 1; the zones-stale flag, a raw byte (B.4 H8): retail ORs
                                 //       a byte into it (known_port_issues row 311); every
                                 //       writer in the tree stores 0 / 1
    uint8_t  _padA3[5];          // +0xA3
    MapCellQueue cellQueue;      // +0xA8 the zone flood fill's pooled work queue (0x14009c170:
                                 //       tail +0xA8, head +0xB0, count +0xB8, node pool +0xC0).
                                 //       A real member: retail's ctor builds it inline after
                                 //       +0xA2 (0x14009eeb4..eee3), its dtor runs 0x14001a5f0 on
                                 //       it after the body (0x14009ee26). Name unconfirmed.
    // +0xE8: a Set header {root, count} (ctor: qword 0 at +0xE8, dword 0 at +0xF0;
    // +0xF4 unwritten): 0x28 nodes keyed by cell (0x14009c640). The dtor's body
    // frees it (0x140014c30 on both children, MemFree the root, root / count
    // zeroed): a trivial POD here, released by hand where retail does.
    AvlTreeHead<AvlNode28> field_0xE8;   // +0xE8

    void RemoveFloor(uint32_t floorId, int x, int y, int w, int h);   // 0x14009cba0 (defined in Component/floorcomponent.cpp)
    void StampInfluence(int state, int cx, int cy, int r, short delta);  // 0x14009f990 (defined in Component/happinesscomponent.cpp)
    bool HasIdentityInRect(int x, int y, int w, int h, const class ComponentDescription* identity) const;  // 0x14009fb60 (Component/diseasecomponent.cpp)
    void StampDanger(int xMin, int yMin, int xMax, int yMax, short delta);   // 0x14009fa80 (Component/firecomponent_ignite.cpp)
    void FillZone(uint32_t cell, uint16_t zone);          // 0x14009c170 (Action/action_pf_astar.cpp, B.4 ACT-a)
    int  NearestRegionId(int cell) const;                 // 0x14009d0a0 (Action/action_pf_geom.cpp, B.4 ACT-a)
    int  PathDistance(uint32_t from, uint32_t to, char useCache);   // 0x14009f810 (Action/action_pf_roots_route.cpp, B.4 ACT-a)
    void SetMovementCosts(PathfinderThread* pathfinder, int profile);   // 0x14009e550 (Action/action_base_move.cpp, B.4 ACT-a)
    PathTicket* RequestPath(PathTicket* out, uint64_t passFlags, uint32_t destCell, uint32_t srcCell);   // 0x14009cfb0 (Action/action_base_move.cpp)
    void FillZones();                                     // 0x14009c480 (Action/action_pf_roots_road.cpp, B.4 ACT-a)
    int  FindPlacementCell(int cell, char terrainCheck, ComponentEntity* const* allow, uint32_t walkMask);   // 0x14009bd40 (Action/action_pf_roots_misc.cpp, B.4 ACT-a)
    void SetTerrainType(int cell, uint32_t terrainType);  // 0x14009f2f0 (Action/action_pf_roots_road.cpp, B.4 ACT-a)
    void ResetTerrainHeights(int x0, int y0, int x1, int y1);   // 0x14009e0e0 (Action/action_pf_roots_road.cpp)
    void FinishRoadCell(int cell, uint8_t orientation, uint32_t terrainType);   // 0x14009c640 (Action/action_pf_roots_road.cpp)
    uint64_t CheckCellTerrain(int cellIndex) const;   // 0x14009cb00 (Action/action_base_cnr.cpp)
    void StampFootprint(const Object::InternalPtr<ComponentEntity>* owner, int x, int y, unsigned int w, int h, const System::String* footprint, int rotation);   // 0x14009d360 (Action/keystone_a_zone.cpp)
    int FindRoadPath(unsigned int passableTypes, unsigned int start, uint64_t goal, void* unused5, int32_t* path, uint8_t manhattan, uint8_t respectBlocked);   // 0x14009ce00 (Action/keystone_a_zone.cpp)
    int ClassifyTerrain(int cellIndex);   // 0x14009e2c0 (Component/keystone_a_map.cpp)
    float* GetTerrainPos(float* outPos3, const float* inXY2, char flag, float* outTri);   // 0x14009f3e0 (Game/mapquery.cpp)
    uint64_t RemovePendingRoad(int param_2);   // 0x14009c700 (Player/keystone_a_removalcalcpath.cpp)
    bool IsGroundClear(int x, int y, int w, int h) const;   // 0x14009d860 (Game/maparea.cpp)
    bool IsAreaEmpty(int x, int y, int w, int h) const;     // 0x14009dac0 (Game/maparea.cpp)
    ComponentEntity** EntityAtCell(ComponentEntity** retIBP, int cellIndex) const;   // 0x1400a0280 (Player/tool_base_edit.cpp)
    bool TestPlacement(int param_2, int param_3, uint32_t param_4, float param_5, float param_6, float param_7, char param_8, char param_9, const ComponentDescription* param_10);   // 0x14009f5d0 (Player/tool_base_path.cpp)
    uint64_t FootprintValidity(int param_2, int param_3, int param_4, int param_5, unsigned int param_6, const CreatePlacedDescription* param_7, const System::String* param_8, float param_9, float param_10, float param_11, char param_12, const System::String* param_13, int param_14, const ComponentDescription* param_15);   // 0x14009d580 (Player/tool_base_tile.cpp)
    int FindCell(int srcCell, int destCell, uint32_t passFlags);   // 0x14009ccf0 (Work/work_base_grid.cpp)
    int CalcDistance(uint32_t cellA, uint32_t cellB);   // 0x140008830 (Work/work_base_reimpl.cpp)
    void GetTilesInArea(int x, int y, int w, int h, RawArray<GridTile>* outArr, char collectEmpty) const;   // 0x14009fc00 (Work/work_leaf_clear.cpp)
    uint64_t ValidateCell(uint32_t pathType, uint32_t destCell, int srcCell);   // 0x1400a00f0 (Work/work_base_reimpl.cpp)
    uint64_t StampCellRefs(int x, int y, int w, int h, ComponentDescription* ref);   // 0x14009f770 (Component/pasturecomponent_states.cpp)
    void PlaceRoadCell(uint32_t cellIdx, uint8_t roadType, uint64_t endCell, uint32_t mapDescVal, ComponentDescription* roadDescExtra);   // 0x14009c960 (Player/roadtool_calcpath.cpp)
};

// [STILLTERRAIN -- NOT RETAIL, faithful perf work; Component/still_terrain.h] true when
// map->GetTerrainPos(out, {inX, inZ}, flag 0, ...) may take its floor branch (the ray cast
// into Map +0x08's picking scene): the same canonical-grid, cell-index and floorCount test,
// read only, no side effect. False = the query is {inX, inZ, heightmap height}. (Game/mapquery.cpp)
bool MapQuery_MayReachFloor(const Map* map, float inX, float inZ);

static_assert(sizeof(Map) == 0xF8, "Map is 0xF8 (Load MemAlloc(0xf8, 8))");
static_assert(alignof(Map) == 8, "Map is qword-aligned");
static_assert(offsetof(Map, size) == 0x00, "Map::size @ +0x00");
static_assert(offsetof(Map, _pad04) == 0x04, "Map +0x04");
static_assert(offsetof(Map, pickingScene) == 0x08, "Map::pickingScene @ +0x08");
static_assert(offsetof(Map, pathfinder) == 0x68, "Map::pathfinder @ +0x68");
static_assert(offsetof(Map, regionGraph) == 0x70, "Map::regionGraph @ +0x70");
static_assert(offsetof(Map, regionPairCache) == 0x78, "Map::regionPairCache @ +0x78");
static_assert(offsetof(Map, cells) == 0x80, "Map::cells @ +0x80");
static_assert(sizeof(MapCellArray) == 0x10, "Map::cells is the 0x10 Array header (+0x80..+0x90)");
static_assert(offsetof(Map, field_0x90) == 0x90, "Map +0x90");
static_assert(offsetof(Map, field_0xA2) == 0xA2, "Map +0xA2");
static_assert(offsetof(Map, _padA3) == 0xA3, "Map +0xA3");
static_assert(offsetof(Map, cellQueue) == 0xA8, "Map::cellQueue @ +0xA8");
static_assert(offsetof(Map, cellQueue) + offsetof(MapCellQueue, pool) +
              offsetof(PathNodePool, growCount) == 0xD0, "Map +0xD0 (ctor 8) is the queue pool's growCount");
static_assert(offsetof(Map, field_0xE8) == 0xE8, "Map +0xE8");
static_assert(offsetof(Map, field_0xE8) + offsetof(AvlTreeHead<AvlNode28>, count) == 0xF0, "Map +0xF0 (the tree's count)");
static_assert(std::is_trivially_destructible<AvlTreeHead<AvlNode28>>::value, "the +0xE8 header is a POD");
