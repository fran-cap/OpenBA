#pragma once
// ============================================================================
// src/ext/terrain_edit/terrain_edit_math.h -- ext.terrain_edit, the pure part (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. Standard C++ only: the brush's disc and its falloff, so
// tests/unit/ext_terrain_edit_test.cpp can check them without the game.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <vector>

#include "ext/terrain_edit/plant_math.h"

namespace Ext {
namespace TerrainEdit {

constexpr float kMinRadius = 1.0f;     // cells
constexpr float kMaxRadius = 24.0f;    // cells
// world units per painted frame at the centre (retail heights are z / 256 in int16)
constexpr float kRaiseStep = 0.04f;

struct Cell {
    int col;
    int row;
    float weight;   // 1 at the centre, 0 at the edge: the share of the step this cell takes
};

// The brush's weight at `dist` cells from the centre for a brush of `radius`: a smooth bump, 0 outside.
inline float Falloff(float dist, float radius) {
    if (radius <= 0.0f || dist >= radius) return 0.0f;
    const float t = 1.0f - dist / radius;
    return t * t * (3.0f - 2.0f * t);
}

// The cells of a disc of `radius` around (col, row), clipped to a size x size grid.
inline std::vector<Cell> Disc(int col, int row, float radius, int size) {
    std::vector<Cell> out;
    if (size <= 0) return out;
    if (radius < kMinRadius) radius = kMinRadius;
    if (radius > kMaxRadius) radius = kMaxRadius;
    const int r = static_cast<int>(std::ceil(radius));
    for (int dr = -r; dr <= r; ++dr) {
        for (int dc = -r; dc <= r; ++dc) {
            const int c = col + dc;
            const int w = row + dr;
            if (c < 0 || w < 0 || c >= size || w >= size) continue;
            const float d = std::sqrt(static_cast<float>(dc * dc + dr * dr));
            const float f = Falloff(d, radius);
            if (f <= 0.0f && !(dc == 0 && dr == 0)) continue;
            out.push_back(Cell{ c, w, f });
        }
    }
    return out;
}

// A raise of `delta` world units as the int16 height words the game stores (z * 256), clamped to its range.
inline int RaisedWord(int word, float delta) {
    int64_t v = static_cast<int64_t>(word) + static_cast<int64_t>(std::llround(delta * 256.0f));
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return static_cast<int>(v);
}

// ---- brush modes ---------------------------------------------------------------------------------
// The game classifies a cell from its height (Map::ClassifyTerrain, keystone_a_map.cpp): below -1.4 deep
// water, below -0.25 shallow water, -0.25 to 0.75 flat ground that can be built on (when the slope allows),
// up to 2.0 steep ground, from 2.0 a mountain. The modes keep to that: flatten stays in the buildable band
// and the mountain brush aims above the mountain line. Lower goes down to the lake floor, and a cell that
// falls under the water line becomes water; the page draws that water again when the stroke ends
// (brush_tool.cpp, terrain_water.h), because the game builds its water mesh only once, at world generation.
enum class Mode : int { Raise = 0, Lower = 1, Flatten = 2, Mountain = 3, Smooth = 4, Plant = 5 };
constexpr int kModeCount = 6;

constexpr float kDefaultRadius   = 6.0f;
constexpr float kRadiusStep      = 2.0f;
constexpr float kMinStrength     = 0.5f;
constexpr float kMaxStrength     = 4.0f;
constexpr float kStrengthStep    = 0.5f;
// no brush takes ground above this: the chunks are drawn up to +16
constexpr float kHeightCeiling   = 15.0f;
// lowering stops here: well under the game's deep-water line (-1.4)
constexpr float kLowerFloor      = -3.0f;
constexpr float kFlattenMin      = -0.2f;   // flatten aims inside the buildable band (-0.25 to 0.75)
constexpr float kFlattenMax      = 0.7f;
// The game's terrain chunks are drawn between heights of -16 and +16 (geometry_draw.cpp), and the ground it
// generates reaches mountains of that order, so the brush's peaks run from about 7 to 15; the ceiling keeps
// the noise from pushing a peak out of the chunks' range (kHeightCeiling, above).
constexpr float kMountainBase      = 5.5f;   // a mountain's peak at strength 0
// the peak rises this much for each unit of strength (6.7 to 15.1)
constexpr float kMountainPerStrength = 2.4f;
// the slope's curve: above 1 it is a cone with a pointed top, not a dome
constexpr float kMountainPower     = 1.6f;
// how far the noise may push the height, as a share of the cone's height there
constexpr float kMountainRugged    = 0.3f;
constexpr float kMountainGrain     = 4.0f;   // vertices per lattice step of the noise
// the share of the way to the neighbours' average a vertex moves per painted frame
constexpr float kSmoothRate      = 0.35f;
// the share of the way to the target a vertex moves per painted frame at the centre
constexpr float kBlendRate       = 0.12f;

struct BrushState {
    Mode  mode     = Mode::Raise;
    float radius   = kDefaultRadius;
    float strength = 1.0f;
    // the resource tool: how full it leaves the disc (plant_math.h, PlantTarget)
    float density  = kDefaultDensity;
};

inline float ClampFloat(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// One step of the size / strength controls: `direction` is +1 or -1; the result stays inside the limits.
inline float SteppedRadius(float radius, int direction) {
    return ClampFloat(radius + kRadiusStep * static_cast<float>(direction), kMinRadius, kMaxRadius);
}

inline float SteppedStrength(float strength, int direction) {
    return ClampFloat(strength + kStrengthStep * static_cast<float>(direction), kMinStrength, kMaxStrength);
}

// The height flatten aims for when a stroke starts at a vertex of height `startWord` (a height word): that
// height, held inside the buildable band.
inline float FlattenTarget(int startWord) {
    return ClampFloat(static_cast<float>(startWord) / 256.0f, kFlattenMin, kFlattenMax);
}

// A deterministic value in [-1, 1] for a lattice point.
inline float LatticeValue(int ix, int iy, uint32_t seed) {
    uint32_t h = seed ^ (static_cast<uint32_t>(ix) * 0x9e3779b1u) ^ (static_cast<uint32_t>(iy) * 0x85ebca6bu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffffu) / 8388607.5f - 1.0f;
}

// Smooth value noise in [-1, 1] at (x, y), in lattice units.
inline float ValueNoise(float x, float y, uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    float tx = x - fx;
    float ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = LatticeValue(ix, iy, seed);
    const float b = LatticeValue(ix + 1, iy, seed);
    const float c = LatticeValue(ix, iy + 1, seed);
    const float d = LatticeValue(ix + 1, iy + 1, seed);
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

// The height (world units) the mountain brush aims for at a vertex `dist` cells from the brush centre: a
// cone with a pointed top whose peak grows with `strength`, roughened by noise that is fixed for a stroke
// (`seed`) so the peaks look natural and stay where they are while the button is held. 0 at and beyond the
// brush's radius.
inline float MountainHeight(float dist, float radius, float strength, int vx, int vy, uint32_t seed) {
    if (radius <= 0.0f || dist >= radius) return 0.0f;
    const float t = 1.0f - dist / radius;
    const float peak = kMountainBase + kMountainPerStrength * strength;
    const float cone = peak * std::pow(t, kMountainPower);
    const float n = ValueNoise(static_cast<float>(vx) / kMountainGrain,
        static_cast<float>(vy) / kMountainGrain, seed);
    const float height = cone * (1.0f + kMountainRugged * n);
    return height > kHeightCeiling ? kHeightCeiling : height;
}

// The height word a vertex of `word` takes in one painted frame. `weight` is the disc's falloff at the
// vertex (0 to 1), `strength` the page's multiplier and `target` the height flatten (one for the stroke),
// mountain (MountainHeight at the vertex) or smooth (the average of the vertex's neighbours) aims for, in
// world units; raise and lower ignore it.
inline int NextWord(int word, Mode mode, float weight, float strength, float target) {
    const float amount = weight * strength;
    switch (mode) {
        case Mode::Raise: {
            const int ceilingWord = static_cast<int>(std::llround(kHeightCeiling * 256.0f));
            if (word >= ceilingWord) return word;   // already at or above the ceiling: leave it, never lower
            const int raised = RaisedWord(word, kRaiseStep * amount);
            return raised > ceilingWord ? ceilingWord : raised;
        }
        case Mode::Lower: {
            const int floorWord = static_cast<int>(std::llround(kLowerFloor * 256.0f));
            if (word <= floorWord) return word;   // already at or below the floor: leave it, never raise
            const int lowered = RaisedWord(word, -kRaiseStep * amount);
            return lowered < floorWord ? floorWord : lowered;
        }
        case Mode::Smooth: {
            const int targetWord = static_cast<int>(std::llround(target * 256.0f));
            float share = kSmoothRate * amount;
            if (share > 1.0f) share = 1.0f;
            const double gap = static_cast<double>(targetWord - word);
            const int moved = static_cast<int>(std::llround(gap * static_cast<double>(share)));
            return RaisedWord(word, static_cast<float>(moved) / 256.0f);
        }
        case Mode::Plant:
            return word;   // the resource tool writes no height
        case Mode::Flatten:
        case Mode::Mountain: {
            const int targetWord = static_cast<int>(std::llround(target * 256.0f));
            if (mode == Mode::Mountain && word >= targetWord) return word;   // already higher: not lowered
            float share = kBlendRate * amount;
            if (share > 1.0f) share = 1.0f;
            const double gap = static_cast<double>(targetWord - word);
            const int moved = static_cast<int>(std::llround(gap * static_cast<double>(share)));
            return RaisedWord(word, static_cast<float>(moved) / 256.0f);
        }
    }
    return word;
}

}  // namespace TerrainEdit
}  // namespace Ext
