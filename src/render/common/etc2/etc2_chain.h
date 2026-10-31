#pragma once
// ============================================================================
// src/render/common/etc2/etc2_chain.h -- one texture's BC1 / BC3 levels to ETC2, through the cache
// ============================================================================
// NOT RETAIL (docs/android/P13_MEM.md). Backend-neutral: a backend on a device without
// textureCompressionBC that samples ETC2 calls Transcode with a texture's BC levels and creates the
// texture in the format Transcode names, from `out` (bc_to_etc2.h: the same block grid and bytes per
// block as the BC levels).
//
// THE CACHE (`cache` non-null: the exe's shared derived-data cache, src/ext/derived_cache/; none
// under the faithful profile). The chain is first looked up under its content key: producer 'ETC2',
// kEncoderVersion, shape {width, height, levels, source format}, the levels' total size and their
// chained Hash64. A hit is the stored chain; a miss is transcoded and queued for storing. Bump
// kEncoderVersion whenever the encoder's output changes (src/render/common/etc2/etc2.cpp,
// bc_to_etc2.cpp): every old entry then misses and ages out through the cap.
//
// THE ENCODE. Bands of 8 block rows of every level over System::ParallelFor (the engine's fork-join
// pool, the caller included; each block is encoded on its own, so the output is the same whatever the
// split).
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace DerivedCache {
class Store;
}
}  // namespace Ext

namespace Etc2Chain {

constexpr uint32_t kEncoderVersion = 1;

// What the levels became.
enum class Target : uint8_t { Rgb8, Rgb8A1, Rgba8 };

struct Level {
    const uint8_t* data;
    size_t         size;
    int            width;    // the level's pixels
    int            height;
};

// `count` levels of a width x height texture in BC1 (`sourceFormat` 0) or BC3 (1, 2). Returns false
// (nothing done) when a level holds fewer bytes than its blocks. Else `out` holds the ETC2 levels back
// to back, level i at [offsets[i], offsets[i + 1]) (`offsets` count + 1 entries), and `target` the
// format: BC1 -> Rgb8, or Rgb8A1 when a texel of any level is transparent; BC3 -> Rgba8.
bool Transcode(const Level* levels, int count, int width, int height, int sourceFormat, Ext::DerivedCache::Store* cache,
               std::vector<uint8_t>* out, std::vector<size_t>* offsets, Target* target);

// The load's end (the backend's loader bracket): when anything was transcoded since the last, one
// summary line.
void LoadEnd();

}  // namespace Etc2Chain
