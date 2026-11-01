#pragma once
// ============================================================================
// src/ext/show_grid/show_grid_math.h -- ext.show_grid's pure decisions (standard C++, no engine)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/SHOWGRID.md. Which tile edges get a line, how thick and what
// color, and the triangles of those lines. The engine side (show_grid.cpp) finds the cells the screen
// looks at, supplies the terrain heights and draws the triangles through the game's tile overlay
// material.
// ============================================================================

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Ext {
namespace ShowGrid {

// A line is a thin strip lying on the terrain along a tile edge. The strip is textured with the SOLID part of
// the overlay atlas (Terrain/Selection.rsc, the column the placement overlay uses for a filled square), so it
// draws as a plain line of its vertex color. The atlas has eight columns of 0.125; the probe build showed
// column 5 is solid white.
constexpr float kSolidU = (5.0f + 0.5f) * 0.125f;
constexpr float kSolidV = 0.5f;

// the strip floats this far (tile units) above the ground, so its sides clear a slope
constexpr float kLift       = 0.03f;
// a stronger, thicker line on every tile edge whose number is a multiple of this
constexpr int   kBoldEvery  = 10;
constexpr int   kMargin     = 1;        // tiles drawn beyond the ones the screen's corners look at
// a view that needs more is zoomed out too far for a grid to help: none is drawn
constexpr int   kMaxTiles   = 10000;

// An inclusive box of map cells.
struct Box {
    int col0 = 0;
    int row0 = 0;
    int col1 = -1;
    int row1 = -1;
    bool Valid() const { return col1 >= col0 && row1 >= row0; }
    int Tiles() const { return Valid() ? (col1 - col0 + 1) * (row1 - row0 + 1) : 0; }
};

// The box around the `n` cells (col, row) the screen's sample points hit, `kMargin` wider, inside a map
// `mapSize` cells a side. Not valid when there are no cells or the box has more than `kMaxTiles` tiles.
inline Box BoxOf(const int* cols, const int* rows, int n, int mapSize) {
    Box none;
    if (n <= 0 || mapSize <= 0) return none;
    int c0 = cols[0];
    int c1 = cols[0];
    int r0 = rows[0];
    int r1 = rows[0];
    for (int i = 1; i < n; ++i) {
        c0 = std::min(c0, cols[i]);
        c1 = std::max(c1, cols[i]);
        r0 = std::min(r0, rows[i]);
        r1 = std::max(r1, rows[i]);
    }
    Box b;
    b.col0 = std::max(0, c0 - kMargin);
    b.row0 = std::max(0, r0 - kMargin);
    b.col1 = std::min(mapSize - 1, c1 + kMargin);
    b.row1 = std::min(mapSize - 1, r1 + kMargin);
    if (!b.Valid() || b.Tiles() > kMaxTiles) return none;
    return b;
}

inline bool SameBox(const Box& a, const Box& b) {
    return a.col0 == b.col0 && a.row0 == b.row0 && a.col1 == b.col1 && a.row1 == b.row1;
}

// What the player chose (settings.cfg: ext.show_grid_opacity, _shade, _width).
struct LineStyle {
    int opacity = 75;   // percent, the ordinary lines' alpha
    int shade   = 100;  // percent brightness: with no saturation 100 is white and 0 black
    int hue     = 0;    // degrees on the color wheel, 0 to 359 (0 red, 120 green, 240 blue)
    int saturation = 0; // percent: 0 is grey (white at full shade), 100 the pure hue
    int width   = 6;    // hundredths of a tile: the ordinary lines' full width

    bool operator==(const LineStyle& o) const {
        return opacity == o.opacity && shade == o.shade && width == o.width && hue == o.hue &&
               saturation == o.saturation;
    }
    bool operator!=(const LineStyle& o) const { return !(*this == o); }
};

// A tile edge line whose number (the column or row of the vertex line, 0 at the map's corner) is a multiple
// of kBoldEvery is a bold one.
inline bool IsBoldLine(int lineNumber) {
    return lineNumber % kBoldEvery == 0;
}

inline uint32_t AlphaOf(int opacityPercent, bool bold) {
    const int percent = std::min(100, std::max(0, opacityPercent));
    int alpha = percent * 255 / 100;
    if (bold) alpha += (255 - alpha) * 6 / 10;   // 60% of the way to fully opaque
    return static_cast<uint32_t>(alpha);
}

// ARGB of a line.
inline uint32_t LineColor(const LineStyle& style, bool bold) {
    // HSV: the hue and saturation of the style, the shade as the value
    const float v = static_cast<float>(std::min(100, std::max(0, style.shade))) * 0.01f;
    const float s = static_cast<float>(std::min(100, std::max(0, style.saturation))) * 0.01f;
    const int h = ((style.hue % 360) + 360) % 360;
    const float sector = static_cast<float>(h) / 60.0f;
    const int i = static_cast<int>(sector);
    const float f = sector - static_cast<float>(i);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    float r = v;
    float g = t;
    float b = p;
    if (i == 1) {
        r = q;
        g = v;
        b = p;
    } else if (i == 2) {
        r = p;
        g = v;
        b = t;
    } else if (i == 3) {
        r = p;
        g = q;
        b = v;
    } else if (i == 4) {
        r = t;
        g = p;
        b = v;
    } else if (i == 5) {
        r = v;
        g = p;
        b = q;
    }
    const auto byte = [](float c) {
        return static_cast<uint32_t>(c * 255.0f + 0.001f) & 0xFFu;
    };
    return (AlphaOf(style.opacity, bold) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

// Half the full width of a line in tile units: a bold line is two thirds wider.
inline float HalfWidth(const LineStyle& style, bool bold) {
    const int hundredths = std::min(30, std::max(1, style.width));
    const float full = static_cast<float>(hundredths) * 0.01f;
    return (bold ? full * 5.0f / 3.0f : full) * 0.5f;
}

// The overlay renderer's vertex (retail 0x1401008c0 streams 0x18 bytes: position, uv, ARGB).
struct LineVertex {
    float    x;
    float    y;
    float    z;
    float    u;
    float    v;
    uint32_t color;
};
static_assert(sizeof(LineVertex) == 0x18, "the overlay vertex is 0x18 bytes");

// A number that changes when the ground under `box` changes: the lines are built again when it does (a loaded
// town with the same view, or terrain that was edited, would otherwise keep the old town's lines).
// `rawHeight(vc, vr)` is the height at vertex (vc, vr) as the heightmap stores it, over the same vertices
// BuildLines reads.
template <class RawHeightFn>
uint64_t HeightHash(const Box& box, RawHeightFn rawHeight) {
    uint64_t hash = 1469598103934665603ull;   // FNV-1a
    for (int r = box.row0; r <= box.row1 + 1; ++r) {
        for (int c = box.col0; c <= box.col1 + 1; ++c) {
            hash ^= static_cast<uint64_t>(static_cast<uint32_t>(rawHeight(c, r)));
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

// The triangles of every tile edge of `box`: six vertices a segment, a segment being one tile of a line. The
// lines run along the vertex columns col0 .. col1 + 1 and the vertex rows row0 .. row1 + 1 (a tile's
// corners). `originX` and `originY` are the world position of vertex 0 and `height(vc, vr)` the ground's
// height at vertex (vc, vr); a tile is one world unit.
template <class HeightFn>
std::vector<LineVertex> BuildLines(const Box& box, float originX, float originY, const LineStyle& style,
                                   HeightFn height) {
    std::vector<LineVertex> out;
    if (!box.Valid()) return out;
    const int cols = box.col1 - box.col0 + 1;
    const int rows = box.row1 - box.row0 + 1;
    out.reserve(static_cast<size_t>((cols + 1) * rows + (rows + 1) * cols) * 6u);
    // one strip from vertex a to vertex b, `bold` or not; (dx, dy) the unit direction of the line
    auto strip = [&](int ac, int ar, int bc, int br, float dx, float dy, bool bold) {
        const float hw = HalfWidth(style, bold);
        const float ox = -dy * hw;
        const float oy = dx * hw;
        const float ax = originX + static_cast<float>(ac);
        const float ay = originY + static_cast<float>(ar);
        const float bx = originX + static_cast<float>(bc);
        const float by = originY + static_cast<float>(br);
        const float az = height(ac, ar) + kLift;
        const float bz = height(bc, br) + kLift;
        const uint32_t color = LineColor(style, bold);
        const float px[6] = {ax + ox, bx - ox, ax - ox, ax + ox, bx + ox, bx - ox};
        const float py[6] = {ay + oy, by - oy, ay - oy, ay + oy, by + oy, by - oy};
        const float pz[6] = {az, bz, az, az, bz, bz};
        for (int k = 0; k < 6; ++k) {
            LineVertex v;
            v.x = px[k];
            v.y = py[k];
            v.z = pz[k];
            v.u = kSolidU;
            v.v = kSolidV;
            v.color = color;
            out.push_back(v);
        }
    };
    // the lines that run along the rows' direction
    for (int c = box.col0; c <= box.col1 + 1; ++c) {
        const bool bold = IsBoldLine(c);
        for (int r = box.row0; r <= box.row1; ++r) strip(c, r, c, r + 1, 0.0f, 1.0f, bold);
    }
    // the lines that run along the columns' direction
    for (int r = box.row0; r <= box.row1 + 1; ++r) {
        const bool bold = IsBoldLine(r);
        for (int c = box.col0; c <= box.col1; ++c) strip(c, r, c + 1, r, 1.0f, 0.0f, bold);
    }
    return out;
}

}  // namespace ShowGrid
}  // namespace Ext
