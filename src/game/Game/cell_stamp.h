// src/game/Game/cell_stamp.h -- [NON-RETAIL bookkeeping, no retail read or write changes]
// AMBMEMO (docs/extensions/handins/AMBMEMO.md): a write clock over the map cell array.
//
// The map (Game+0x80) is dim*dim MapCell records of 0x30 bytes. This keeps one 64-bit
// stamp per 16x16 block of cells: every store to a cell's blocker (+0x00), occupant
// (+0x08) or terrain type (+0x29) calls NoteWrite(cellPtr) right after, which sets that
// block's stamp to a fresh value of a global clock. A reader that cached something
// computed from a rectangle of cells records Now() after its scan; the rectangle is
// unchanged since then iff RectStamp(rect) <= that value.
//
// The reader is AmbientEnvironmentComponent::Update's biome scan (the memo there). The
// writers, each a single-cell store (the same list the terrain sidecar keeps, plus the
// blocker / occupant stores):
//   terrain +0x29  Map::SetTerrainType (action_pf_roots_road.cpp; dc20, dee0, the start
//                  town un-mark and the road writers all go through it),
//                  cemeterycomponent.cpp, citizen_lifeevent_handler.cpp,
//                  pasturecomponent.cpp, pasturecomponent_states.cpp,
//                  quarrycomponent_deform.cpp, keystone_a_removalcalcpath.cpp,
//                  resourceproducecomponent_states.cpp
//   blocker +0x00  Map::StampFootprint (keystone_a_zone.cpp, 0x14009d360)
//   occupant +0x08 MapComponent_overlay_dc20 / CreateNaturalResource_impl_dee0
//                  (keystone_a_map.cpp), StartTown_UnmarkResourceCellGrid (starttown.cpp),
//                  cemeterycomponent.cpp, citizen_lifeevent_handler.cpp,
//                  pasturecomponent.cpp, pasturecomponent_states.cpp,
//                  quarrycomponent_deform.cpp, resourceproducecomponent_states.cpp
//   both           the cell stream's type-check nulls (gamegrid_serialize.cpp)
// Bulk rewrites (worldgen classify maptilegrid.cpp, the stream load gamegrid_serialize.cpp,
// the world teardown) call Invalidate(); a new cell-array base or dim rebuilds the table
// on the next RectStamp, with every block at a fresh clock value (so every cached answer
// misses once). The COMPPROF=4 shadow `AmbientMemo` proves the list complete.
#pragma once
#include <cstdint>

struct MapCell;   // Game/Map.h: the 0x30-byte cell record

namespace CellStamp {

constexpr int      kCellStride  = 0x30;   // sizeof(MapCell), asserted where Map.h is in scope
constexpr int      kBlockShift  = 4;      // 16x16 cells per stamp
constexpr uint64_t kUncacheable = ~0ull;  // RectStamp of a rect that is not inside the grid

extern const char* g_base;      // cell array the table was built for (null = not built)
extern int         g_dim;       // its edge
extern int         g_blocksW;   // blocks per row
extern uint64_t*   g_blocks;    // g_blocksW * g_blocksW stamps
extern uint64_t    g_clock;     // last value handed out

// Call right AFTER storing cells[idx]'s blocker, occupant or terrain type.
inline void NoteIndex(int idx) {
    if (!g_blocks || idx < 0 || idx >= g_dim * g_dim) return;
    const int y = idx / g_dim, x = idx - y * g_dim;
    g_blocks[(y >> kBlockShift) * g_blocksW + (x >> kBlockShift)] = ++g_clock;
}
inline void NoteWrite(const MapCell* cellPtr) {
    if (!g_blocks || !cellPtr) return;
    const char* p = reinterpret_cast<const char*>(cellPtr);   // CAST-OK: elem_stride
    if (p < g_base) return;
    const long long off = p - g_base;
    NoteIndex(static_cast<int>(off / kCellStride));
}

// The current clock: what a reader records right after its scan.
inline uint64_t Now() { return g_clock; }

// The newest stamp over the cells [x, x+w) x [y, y+h) of the `dim`-edged array at `cells`,
// (re)building the table for that array first if it is not the one tracked. Returns
// kUncacheable when the rect is empty or not inside the grid (the reader then always scans).
uint64_t RectStamp(const MapCell* cells, int dim, int x, int y, int w, int h);

// Bulk rewrite / grid freed: forget the table (rebuilt on the next RectStamp).
void Invalidate();

}  // namespace CellStamp
