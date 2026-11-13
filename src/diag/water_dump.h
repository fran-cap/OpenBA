// src/diag/water_dump.h -- the water mesh and the heights under it, for an offline top-down view.
//
//   BANISHED_WATER_DUMP=<dir>  each rebuild of ext.better_water's water copy (a new game, a load, the
//                              terrain brush) writes <dir>/water_dump.bin: the int16 heightmap, retail's
//                              water vertices, the copy the extension draws, the indices and both range
//                              tables. tools/water_view/water_view.py draws it (README alongside).
//
// Read only. Off (one Opt() test) unless the flag is set. The dump is made where the copy is made, so
// the run needs ext.better_water on (the default; not /profile:faithful).
#pragma once

struct TerrainVertex;
class WorldGeometry;

namespace WaterDump {

// `drawn` is the copy the extension draws (as many vertices as retail's mesh); null writes retail's twice.
void Write(const WorldGeometry& geo, const TerrainVertex* drawn);

}  // namespace WaterDump
