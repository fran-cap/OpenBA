// src/diag/water_dump.cpp -- the water mesh dump (water_dump.h).
#include "water_dump.h"
#include "options.h"
#include "err_line.h"
#include "Game/WorldGeometry.h"
#include <cstdint>
#include <cstdio>
#include <string>

namespace WaterDump {

// The file: "BWD1", eight int32 (vertex count, index count, river ranges, lake ranges, height row stride,
// half grid, 0, 0), retail's vertices, the drawn vertices, the indices, the river ranges, the lake ranges
// (four int32 each), stride * stride int16 heights. tools/water_view/water_view.py reads it.
void Write(const WorldGeometry& geo, const TerrainVertex* drawn) {
    const char* dir = Opt().waterDump;
    if (dir == nullptr || dir[0] == '\0') return;
    const std::string path = std::string(dir) + "/water_dump.bin";
    FILE* out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) {
        ErrLine("[WaterDump] cannot write %s\n", path.c_str());
        return;
    }
    const int32_t vertexCount = geo._contourVertices.size;
    const int32_t indexCount = geo._contourIndices.size;
    const int32_t riverCount = geo._contourRangesA.size;
    const int32_t lakeCount = geo._contourRangesB.size;
    const int32_t stride = geo._heightRowStride;
    const int32_t head[8] = {vertexCount, indexCount, riverCount, lakeCount, stride, geo._gridSize >> 1, 0, 0};
    std::fwrite("BWD1", 1, 4, out);
    std::fwrite(head, sizeof(head), 1, out);
    const TerrainVertex* retail = geo._contourVertices.data;
    std::fwrite(retail, sizeof(TerrainVertex), static_cast<size_t>(vertexCount), out);
    std::fwrite(drawn != nullptr ? drawn : retail, sizeof(TerrainVertex), static_cast<size_t>(vertexCount), out);
    std::fwrite(geo._contourIndices.data, sizeof(uint16_t), static_cast<size_t>(indexCount), out);
    for (int32_t i = 0; i < riverCount; ++i) {
        std::fwrite(geo._contourRangesA.data[i].values, sizeof(int32_t), 4, out);
    }
    for (int32_t i = 0; i < lakeCount; ++i) {
        std::fwrite(geo._contourRangesB.data[i].values, sizeof(int32_t), 4, out);
    }
    std::fwrite(geo._heights.data, sizeof(int16_t), static_cast<size_t>(stride) * static_cast<size_t>(stride), out);
    std::fclose(out);
    ErrLine("[WaterDump] %s: %d vertices, %d indices, %d river + %d lake ranges, heights %d x %d\n", path.c_str(),
            vertexCount, indexCount, riverCount, lakeCount, stride, stride);
}

}  // namespace WaterDump
