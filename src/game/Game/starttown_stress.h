// src/game/Game/starttown_stress.h
// BANISHED_STRESS: the stress town's siting, exposed (perf/README.md "What the town is").
//
// StartTown_StressSpawnStructures (starttown.cpp) builds the stress town with these
// helpers, and keeps the siting state alive for the rest of the world so the harness's
// housing pass (HousePass in src/diag/stress_harness.cpp, tools/stress_run.sh --houses)
// places its houses by the SAME rules: the same templates (wood / stone alternating),
// the same instant-build placement (StartTown_SpawnStructures' placeStructure), the same
// occupancy bitmap, learned door cells and whole-map pocket flood. Harness code only:
// nothing here runs in a normal game (the state exists only under BANISHED_STRESS or
// BANISHED_SHOWROOM).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ComponentEntity;
class Map;

namespace StressTown {

struct Building { const char* path; const char* tag; int fw, fh; };

// Why the last Place() placed nothing.
enum class Fail { None, Unresolved, NoSite, PlaceNull };

class Siting {
public:
    // Seeds the occupancy bitmap from every footprint StartTown recorded so far and floods
    // the reachable cells from the town cell (cx, cy).
    Siting(Map* game, int cx, int cy);

    Map*   game;
    int    mapW;
    size_t cells;
    int    cx, cy;
    int    townCell;
    bool   allowPockets;   // BANISHED_STRESS_ALLOW_POCKETS (the old walled-in layout)

    std::vector<uint8_t> occ;        // 1 = a footprint (door notches cleared)
    std::vector<int8_t>  workable;   // memo of the work classes' workability test, -1 = unknown
    std::vector<uint8_t> reach;      // cells reachable from the town over free ground
    std::vector<uint8_t> reachB;     // scratch flood for the pocket test
    int reachCount = 0;

    // Free ring kept around every footprint (1 = the walk lane; the showroom widens it).
    int margin = 1;
    // Ring search: centre and limit (the founding spawn moves them for the spread layout).
    int anchorX, anchorY, ringMax, maxR;
    int maxUsedR = 0;
    // Where the founding houses were sited, and the next house template (alternating).
    int houseAnchorX, houseAnchorY, houseRingMax;
    int houseKind = 0;

    // Counters. The first group is the founding [Stress::place] DONE / reject lines'.
    int reachRejects = 0, pocketRejects = 0;
    int rejDoor = 0, rejPocket = 0, diagN = 0;
    int doorWalled = 0;
    int failed = 0;
    int rejUnworkable = 0;   // a footprint cell fails the workability test
    int rejLive = 0;         // housing pass only: a resource or a character on a footprint cell

    // The footprint and entity the last successful Place() picked, and why the last one failed.
    int lastX = 0, lastY = 0, lastW = 0, lastH = 0;
    ComponentEntity* lastEnt = nullptr;
    Fail lastFail = Fail::None;

    // [Stress::place] lines from Place(); the housing pass turns them off and counts instead.
    bool log = true;

    void Mark(int x, int y, int w, int h);
    bool CellWorkable(int cell);
    bool CellWalkable(int cell);
    // Whole-map flood from the town cell over free walkable cells, with (ftx, fty, fw, fh) blocked
    // except its door cell (doorDx/doorDy >= 0). Returns the number of cells reached.
    int  FloodTown(std::vector<uint8_t>& out, int ftx, int fty, int fw, int fh, int doorDx, int doorDy);
    // true = placing this footprint would cut a reachable cell off or leave its door unreachable.
    // w == 0: verify an ALREADY-placed footprint: only the door test.
    bool CreatesPocket(int tx, int ty, int w, int h, int doorDx, int doorDy);
    bool Fits(int tx, int ty, int w, int h, int doorDx, int doorDy);
    bool DoorFor(const char* path, int* dx, int* dy) const;
    // Resolve, size, site (ring search from the anchor), place COMPLETED, record and learn the door.
    // Returns the entity, or null (lastFail says why; `failed` counts it).
    ComponentEntity* Place(const Building& b);

    // ── the housing pass (stress_harness.cpp HousePass) ──
    // Brings the state up to date with the live world: marks footprints recorded since (the
    // deferred hunter lodge), forgets the workability memo, re-floods the reachable cells and
    // marks the cells a character stands on. Turns on the live-cell test (`checkLive`).
    void BeginPass();
    // The next house of the founding alternation, sited around the founding houses' anchor
    // with the walk-lane margin. Null when nothing was placed (lastFail).
    ComponentEntity* PlaceHouse();

private:
    struct DoorOff { std::string path; int dx, dy; };
    std::vector<DoorOff> doorOffs;
    size_t occSynced = 0;            // StartTown's footprint list entries already marked
    bool checkLive = false;
    std::vector<uint8_t> live;       // cells a character stood on at BeginPass
};

// The current world's siting state, or null (no stress town / showroom was built).
Siting* Current();

}  // namespace StressTown
