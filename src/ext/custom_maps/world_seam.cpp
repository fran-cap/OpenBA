// ============================================================================
// src/ext/custom_maps/world_seam.cpp -- ext.custom_maps (class A), the world creation seam
// ============================================================================
// See world_seam.h and custom_maps.h. The map's heights become the heightfield's float height map (the
// game's own int16 value over 256, so the quantize step that follows gives the file's value back), its
// water paths become hf->paths (the water mesh reads x, y and the half width in outerRadius; the other
// fields are the carve brushes of the generator's own stages, which a map does not run), and the coverage
// features become hf->waterFeatures.
// ============================================================================
#include "ext/custom_maps/world_seam.h"

#include "ext/custom_maps/custom_maps.h"
#include "ext/registry.h"
#include "Game/TerrainHeightfield.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace Ext {
namespace CustomMaps {

unsigned BuildSize(unsigned rowSize) {
    const Map* map = Pending();
    return (Ext().custom_maps && map != nullptr) ? static_cast<unsigned>(map->mapSize) : rowSize;
}

unsigned LoadedSize(unsigned rowSize) {
    const Map* map = Active();
    return (Ext().custom_maps && map != nullptr) ? static_cast<unsigned>(map->mapSize) : rowSize;
}

bool ApplyToHeightfield(TerrainHeightfield* hf) {
    SetCoverageStart(-1);
    ClearActive();   // a new game never inherits the last one's map
    const Map* map = Pending();
    if (hf == nullptr || map == nullptr || !Ext().custom_maps) {
        return false;
    }
    if (hf->gridSize != 2 * static_cast<int>(map->mapSize)) {
        std::fprintf(stderr,
                     "[CustomMaps] the map \"%s\" is %u cells, this game's is %d: generating instead\n",
                     map->name.c_str(), static_cast<unsigned>(map->mapSize), hf->gridSize / 2);
        std::fflush(stderr);
        return false;
    }

    const size_t count = HeightCount(map->mapSize);
    RawArraySetSize(hf->heightmap, static_cast<int>(count));
    if (hf->heightmap.data == nullptr) {
        std::fprintf(stderr, "[CustomMaps] no memory for the height map: generating instead\n");
        std::fflush(stderr);
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        hf->heightmap.data[i] = HeightOf(map->heights[i]);
    }

    for (const Path& path : map->paths) {
        std::vector<TerrainPathPoint> points(path.points.size());
        for (size_t i = 0; i < points.size(); ++i) {
            TerrainPathPoint& out = points[i];
            out.x             = path.points[i].x;
            out.y             = path.points[i].y;
            out.strength      = 0.0f;
            out.innerRadius   = 0.0f;
            out.innerFalloff  = 0.0f;
            out.outerRadius   = path.points[i].halfWidth;
            out.outerFalloff  = 0.0f;
        }
        // The list deep-copies the polyline into its own buffer (the generator's stages append the same way).
        RawArray<TerrainPathPoint> polyline;
        polyline.data     = points.data();
        polyline.size     = static_cast<int32_t>(points.size());
        polyline.capacity = static_cast<int32_t>(points.size());
        hf->paths.SetSizeFill(hf->paths._size + 1, &polyline);
    }

    const std::vector<Feature> features = CoverageFeatures(*map);
    SetCoverageStart(hf->waterFeatures._size);
    for (const Feature& f : features) {
        TerrainStamp stamp;
        stamp.x      = f.x;
        stamp.y      = f.y;
        stamp.radius = f.radius;
        hf->waterFeatures.SetSize(hf->waterFeatures._size + 1, stamp);
    }

    std::fprintf(stderr,
                 "[CustomMaps] world built from the map \"%s\" by \"%s\": size %u, %zu water path(s), "
                 "%zu water feature(s)\n",
                 map->name.c_str(), map->author.c_str(), static_cast<unsigned>(map->mapSize),
                 map->paths.size(), features.size());
    std::fflush(stderr);
    ActivatePending();
    return true;
}

}  // namespace CustomMaps
}  // namespace Ext
