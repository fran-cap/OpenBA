// ============================================================================
// src/ext/river_channel/river_channel.cpp -- ext.river_channel (river_channel.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RIVERCHANNEL.md.
// ============================================================================
#include "ext/river_channel/river_channel.h"
#include "ext/river_channel/river_channel_math.h"

#include "Game/TerrainHeightfield.h"

#include <cstdio>
#include <vector>

namespace Ext {
namespace RiverChannel {

void Apply(TerrainHeightfield* hf) {
    const int32_t edge = hf->gridSize + 1;
    float* heights = hf->heightmap.data;
    if (heights == nullptr || hf->heightmap.size < edge * edge) return;
    const std::vector<float> before(heights, heights + static_cast<size_t>(edge) * static_cast<size_t>(edge));
    std::vector<Point> points;
    int lowered = 0;
    int paths = 0;
    for (int32_t i = 0; i < hf->paths._size; ++i) {
        const RawArray<TerrainPathPoint>& path = hf->paths._data[i];
        if (path.data == nullptr || path.size < 2) continue;
        points.clear();
        for (int32_t j = 0; j < path.size; ++j) {
            points.push_back(Point{path.data[j].x, path.data[j].y});
        }
        lowered += CarvePath(heights, before.data(), edge, hf->gridSize >> 1, points.data(),
                             static_cast<int32_t>(points.size()));
        ++paths;
    }
    std::fprintf(stderr, "[RiverChannel] %d heights lowered along %d river path(s)\n", lowered, paths);
}

}  // namespace RiverChannel
}  // namespace Ext
