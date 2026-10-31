// ============================================================================
// tests/unit/ext_town_screenshot_test.cpp -- ext.town_screenshot
// ============================================================================
// The registry row (class B, OFF) and the pure part of the screenshot
// (src/ext/town_screenshot/screenshot_math.h): the isometric basis, the picture's footprint, the tile grid
// and the mask that leaves the corners outside the map transparent. The camera, the readback and the page
// need the running game.
// ============================================================================
#include "unit.h"
#include "ext/town_screenshot/screenshot_math.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cmath>
#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

bool Near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

UNIT_TEST(ext_town_screenshot_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("town_screenshot");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::B);   // the window's pages belong to B / C rows; a screenshot changes no play
    CHECK(!e->defaultOn);
    CHECK(Ext::SettingsKey(*e) == "ext.town_screenshot");
    CHECK(!ResolveText("").ext.town_screenshot);
    CHECK(ResolveText("ext.town_screenshot=1\n").ext.town_screenshot);
    CHECK(!ResolveText("ext.town_screenshot=0\n").ext.town_screenshot);
    CHECK(!ResolveText("profile=faithful\n").ext.town_screenshot);
}

UNIT_TEST(ext_town_screenshot_choices) {
    using namespace Ext::TownScreenshot;
    CHECK(QualityPixelsPerCell(0) == 4.0f && QualityPixelsPerCell(1) == 8.0f);
    CHECK(QualityPixelsPerCell(2) == 12.0f && QualityPixelsPerCell(3) == 16.0f);
    CHECK(QualityPixelsPerCell(-1) == 4.0f && QualityPixelsPerCell(9) == 16.0f);   // clamped
    CHECK(AnglePitch(0) == 30.0f);
    CHECK(Near(AnglePitch(1), 35.26439f, 1e-4f));
    CHECK(TurnYaw(0) == 45.0f && TurnYaw(1) == 135.0f && TurnYaw(3) == 315.0f);
    CHECK(TurnYaw(4) == 45.0f);   // wraps
}

// The camera's three axes are unit length, perpendicular, and the camera looks down.
UNIT_TEST(ext_town_screenshot_basis_is_orthonormal) {
    using namespace Ext::TownScreenshot;
    for (int turn = 0; turn < kTurnCount; ++turn) {
        for (int angle = 0; angle < kAngleCount; ++angle) {
            const Basis b = MakeBasis(AnglePitch(angle), TurnYaw(turn));
            CHECK(Near(b.rx * b.rx + b.ry * b.ry + b.rz * b.rz, 1.0f));
            CHECK(Near(b.ux * b.ux + b.uy * b.uy + b.uz * b.uz, 1.0f));
            CHECK(Near(b.fx * b.fx + b.fy * b.fy + b.fz * b.fz, 1.0f));
            CHECK(Near(b.rx * b.ux + b.ry * b.uy + b.rz * b.uz, 0.0f));
            CHECK(Near(b.rx * b.fx + b.ry * b.fy + b.rz * b.fz, 0.0f));
            CHECK(Near(b.ux * b.fx + b.uy * b.fy + b.uz * b.fz, 0.0f));
            CHECK(b.fz < 0.0f);   // looking down
            CHECK(b.uz > 0.0f);   // screen up is up in the world
        }
    }
}

// A flat square map seen at the diagonal: as wide as its diagonal, and as tall as the diagonal times
// sin(pitch).
UNIT_TEST(ext_town_screenshot_footprint_of_a_flat_map) {
    using namespace Ext::TownScreenshot;
    const Basis b = MakeBasis(30.0f, 45.0f);
    const Footprint f = ComputeFootprint(b, 100, 0.0f, 0.0f, 2.0f);
    const float diagonal = 100.0f * 1.41421356f;
    CHECK(Near(f.umax - f.umin, diagonal, 0.01f));
    CHECK(Near(f.vmax - f.vmin, diagonal * 0.5f, 0.01f));   // sin 30 = 0.5
    CHECK(f.width == static_cast<int>(std::ceil(diagonal * 2.0f)));
    CHECK(f.height == static_cast<int>(std::ceil(diagonal)));
    // height adds to the picture's height: z * cos(pitch) above the ground
    const Footprint tall = ComputeFootprint(b, 100, 0.0f, 40.0f, 2.0f);
    CHECK(Near(tall.vmax - tall.vmin, diagonal * 0.5f + 40.0f * std::cos(30.0f * kDegToRad), 0.01f));
}

UNIT_TEST(ext_town_screenshot_pixels_per_cell_is_capped_by_the_picture_size) {
    using namespace Ext::TownScreenshot;
    const Basis b = MakeBasis(30.0f, 45.0f);
    // plenty of room: the wanted value stands
    CHECK(ChoosePixelsPerCell(b, 256, 0.0f, 40.0f, 8.0f, 1000000000) == 8.0f);
    // a 640 map at 16 pixels to the cell is far past 64 million pixels, so it is brought down until it fits
    const int64_t cap = 64000000;
    const float ppc = ChoosePixelsPerCell(b, 640, 0.0f, 40.0f, 16.0f, cap);
    CHECK(ppc < 16.0f && ppc >= 1.0f);
    const Footprint f = ComputeFootprint(b, 640, 0.0f, 40.0f, ppc);
    CHECK(static_cast<int64_t>(f.width) * f.height <= cap);
    // and not by more than one step (5 percent) than it had to
    const Footprint over = ComputeFootprint(b, 640, 0.0f, 40.0f, ppc / 0.95f);
    CHECK(static_cast<int64_t>(over.width) * over.height > cap);
    // never below 1
    CHECK(ChoosePixelsPerCell(b, 640, 0.0f, 40.0f, 16.0f, 10) == 1.0f);
}

UNIT_TEST(ext_town_screenshot_tiles_cover_the_picture_without_gaps) {
    using namespace Ext::TownScreenshot;
    const TileGrid g = MakeGrid(2173, 1505, 1920, 1017, 64);
    CHECK(g.payloadW == 1792 && g.payloadH == 889);
    CHECK(g.cols == 2 && g.rows == 2 && g.count() == 4);
    CHECK(g.cols * g.payloadW >= 2173 && (g.cols - 1) * g.payloadW < 2173);
    CHECK(g.rows * g.payloadH >= 1505 && (g.rows - 1) * g.payloadH < 1505);
    // a picture that is exactly one payload wide is one column
    CHECK(MakeGrid(1792, 889, 1920, 1017, 64).count() == 1);
    CHECK(MakeGrid(1793, 889, 1920, 1017, 64).cols == 2);
    // a screen smaller than the margins still makes progress
    CHECK(MakeGrid(100, 100, 100, 100, 64).payloadW == 1);
}

// The tile centre puts the tile's middle on the right part of the picture: the world point it looks at
// projects back to the same (u, v).
UNIT_TEST(ext_town_screenshot_tile_centre_round_trips_through_the_camera_axes) {
    using namespace Ext::TownScreenshot;
    const Basis b = MakeBasis(30.0f, 135.0f);
    const float ppc = 8.0f;
    const Footprint f = ComputeFootprint(b, 256, -2.0f, 45.0f, ppc);
    const TileGrid g = MakeGrid(f.width, f.height, 1920, 1017, 64);
    for (int row = 0; row < g.rows; ++row) {
        for (int col = 0; col < g.cols; ++col) {
            float cu = 0.0f;
            float cv = 0.0f;
            TileCentre(f, g, ppc, col, row, 1920, 1017, cu, cv);
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            TileLookAt(b, cu, cv, x, y, z);
            CHECK(Near(x * b.rx + y * b.ry + z * b.rz, cu, 1e-2f));
            CHECK(Near(x * b.ux + y * b.uy + z * b.uz, cv, 1e-2f));
            // the tile's payload starts on the picture's pixel col * payloadW
            const float leftU = cu - 1920.0f / (2.0f * ppc) + 64.0f / ppc;
            CHECK(Near(leftU, f.umin + static_cast<float>(col * g.payloadW) / ppc, 1e-2f));
            const float topV = cv + 1017.0f / (2.0f * ppc) - 64.0f / ppc;
            CHECK(Near(topV, f.vmax - static_cast<float>(row * g.payloadH) / ppc, 1e-2f));
        }
    }
}

// The mask: the middle of the picture is map, the corners of its rectangle are not, and a flat map's mask is
// the diamond the square makes.
UNIT_TEST(ext_town_screenshot_mask_leaves_the_corners_out) {
    using namespace Ext::TownScreenshot;
    const Basis b = MakeBasis(30.0f, 45.0f);
    const float ppc = 2.0f;
    const Footprint f = ComputeFootprint(b, 100, 0.0f, 0.0f, ppc);
    CHECK(PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, f.width / 2, f.height / 2));
    CHECK(!PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, 1, 1));
    CHECK(!PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, f.width - 2, 1));
    CHECK(!PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, 1, f.height - 2));
    CHECK(!PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, f.width - 2, f.height - 2));
    // the diamond's four tips (a pixel in from each edge) are inside
    CHECK(PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, f.width / 2, 2));
    CHECK(PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, f.width / 2, f.height - 3));
    CHECK(PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, 2, f.height / 2));
    CHECK(PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, f.width - 3, f.height / 2));
    // with room above the ground (trees and mountains), more of the rectangle shows map
    const Footprint tall = ComputeFootprint(b, 100, 0.0f, 40.0f, ppc);
    int flatInside = 0;
    int tallInside = 0;
    for (int py = 0; py < f.height; py += 3) {
        for (int px = 0; px < f.width; px += 3) {
            if (PixelInsideMap(b, f, ppc, 100, 0.0f, 0.0f, px, py)) ++flatInside;
        }
    }
    for (int py = 0; py < tall.height; py += 3) {
        for (int px = 0; px < tall.width; px += 3) {
            if (PixelInsideMap(b, tall, ppc, 100, 0.0f, 40.0f, px, py)) ++tallInside;
        }
    }
    // the flat diamond is half its rectangle; the tall hexagon more than that
    const float flatSamples = static_cast<float>(((f.width + 2) / 3) * ((f.height + 2) / 3));
    const float tallSamples = static_cast<float>(((tall.width + 2) / 3) * ((tall.height + 2) / 3));
    const float flatShare = static_cast<float>(flatInside) / flatSamples;
    const float tallShare = static_cast<float>(tallInside) / tallSamples;
    CHECK(Near(flatShare, 0.5f, 0.03f));
    CHECK(tallShare > flatShare);
}

// A view straight down makes the whole picture map.
UNIT_TEST(ext_town_screenshot_mask_looking_straight_down) {
    using namespace Ext::TownScreenshot;
    const Basis b = MakeBasis(90.0f, 0.0f);
    const Footprint f = ComputeFootprint(b, 50, 0.0f, 10.0f, 4.0f);
    CHECK(PixelInsideMap(b, f, 4.0f, 50, 0.0f, 10.0f, 0, 0));
    CHECK(PixelInsideMap(b, f, 4.0f, 50, 0.0f, 10.0f, f.width - 1, f.height - 1));
}
