// src/game/Game/terrain_sidecar.h -- [NON-RETAIL data structure, IDENTICAL reads]
// The terrain-type side-car (was PerfX's TERRAIN_SIDECAR, src/diag/perf_terrain.h; faithful
// perf work since PERFXEXT, docs/extensions/handins/PERFXEXT.md: unconditional, no toggle).
//
// The map cell array (Game+0x80) is dim*dim records of 0x30 bytes; the terrain TYPE is
// the byte at +0x29. The per-tick movement code reads exactly that one byte for the
// citizen's current cell (walk-speed / road test), so every citizen costs one cache
// line of a 7.8 MB array per iteration -- a miss in L1/L2 for all of them, and at
// 800x50 that is 40k random 48-byte-stride reads a frame. This keeps a 1-byte-per-cell
// mirror (256 KB on a 512 grid) that the hot readers use instead.
//
// Sync rules (the whole risk of a shadow copy is a stale byte):
//   * The mirror is keyed on the cell-array BASE POINTER and count: a new grid (new
//     game, load-time resize) rebuilds it on the next read.
//   * Every store to a cell's +0x29 calls NoteWrite(cellPtr) right after -- the eight
//     single-cell writers: action_pf_roots_road.cpp, cemeterycomponent.cpp,
//     citizen_lifeevent_handler.cpp (Cemetery_SpawnTombstone_6a90),
//     pasturecomponent_states.cpp, pasturecomponent.cpp (PastureComponent::OnEvent's
//     pointer-alias store), quarrycomponent_deform.cpp, keystone_a_removalcalcpath.cpp,
//     resourceproducecomponent_states.cpp
//     (the tombstone and the pasture-state writer were missing until PERFXEXT, the
//     pasture OnEvent writer until AMBMEMO). Bulk rewrites (worldgen classify maptilegrid.cpp, the stream load
//     gamegrid_serialize.cpp) call Invalidate(); CopyMapCell / ZeroMapCell only run inside
//     the array's own resize, which moves the base or the count.
//   * BANISHED_STRESS_COMPPROF=4 (g_verifyOn, src/diag/stress_harness.h) makes every hot
//     read compare the mirror with the real byte: shadow `TerrainSidecar`, expect 0
//     mismatches -- the proof that the writer list is complete. (Was
//     BANISHED_PERF_TERRAIN_VERIFY, retired with the PerfX flags.)
#pragma once
#include <cstdint>
#include "stress_harness.h"   // g_verifyOn: 1 only at COMPPROF level 4

struct MapCell;   // Game/Map.h: the 0x30-byte cell record, terrain type at +0x29

namespace TerrainSidecar {

extern uint8_t*       g_mirror;        // count bytes (null = not built)
extern const MapCell* g_mirrorBase;    // cell array the mirror was built from
extern int            g_mirrorCount;

// Slow path: (re)build for a new grid, or the verified read under COMPPROF=4.
uint8_t GetSlow(const MapCell* cells, int count, int idx);

// Hot read of cells[idx].terrainType. `cells`/`count` = the {data,count} header at Game+0x80.
inline uint8_t Get(const MapCell* cells, int count, int idx) {
    if (cells == g_mirrorBase && (unsigned)idx < (unsigned)g_mirrorCount && !g_verifyOn)
        return g_mirror[idx];
    return GetSlow(cells, count, idx);
}

// Call right AFTER storing cellPtr->terrainType; copies the new byte into the mirror.
void NoteWrite(const MapCell* cellPtr);
// Bulk rewrite / grid freed: forget the mirror (rebuilt lazily on the next read).
void Invalidate();
// World teardown: one line on stderr when the COMPPROF=4 shadow ran.
void Report();

}  // namespace TerrainSidecar
