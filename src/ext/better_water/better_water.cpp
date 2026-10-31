// ============================================================================
// src/ext/better_water/better_water.cpp -- ext.better_water's shore pass (docs/extensions/handins/BETTERWATER.md)
// ============================================================================
// NOT RETAIL. better_water.h has the design. This file owns the "twin" programs: for each water
// VideoProgram the game binds (River, Lake), one VideoProgram of ours with the same vertex layout,
// vertex program and render state and the shore pixel program instead of the water's. They are made
// through the engine's own Video classes, so every backend creates and binds them as it does a
// program read from a package (DX11 takes the DXBC, Vulkan converts it with dxbc-spirv, Metal with
// airconv): no backend code of our own.
//
// LIFETIME. The twins are made on first use, on the drawing thread, and live for the process. A
// twin remembers the water program it stands in for and the parts it copied (Twin, Matches): a world
// load or a device restart (MainLoop::OnVideoRestart) makes new water programs, nothing matches and a
// new twin is made in the next slot. A replaced twin is left alive (a backend may still hold it in
// cached state): two small objects per world load, sharing the one shore pixel program.
// ============================================================================

#include "ext/better_water/better_water.h"
#include "ext/better_water/shore_params.h"

#include "ext/registry.h"
#if BANISHED_TWEAK_OVERLAY
#include "ext/tweak/ext_tweaks.h"   // the debug overlay's live ShoreParams
#endif

#include "better_water/shore_overlay_lake_ps.h"    // src/render/common: g_bw_PSShoreLake (tools/water_shaders.py)
#include "better_water/shore_overlay_river_ps.h"   // src/render/common: g_bw_PSShoreRiver

#include "video/Context.h"
#include "video/ShaderProgram.h"
#include "video/VideoProgram.h"

#include <cstdio>
#include <cstring>

namespace Ext {
namespace BetterWater {

namespace {

// The shore pixel programs, one per Kind, shared by every twin of that kind. Made once; null after a
// failed create.
Video::PixelProgram* g_shorePixel[2] = {nullptr, nullptr};
bool g_shorePixelTried[2] = {false, false};

struct Twin {
    // The game's water program and the parts the twin was made from. A twin is reused only while
    // all four still match: a world load frees the water programs and makes new ones (often at the
    // same address), and a device restart gives them new companions.
    const Video::VideoProgram* water = nullptr;
    Kind kind = Kind::River;
    const void* waterData = nullptr;
    const void* layout = nullptr;
    const void* vertex = nullptr;
    const void* state = nullptr;
    Video::VideoProgram* twin = nullptr;
};

// River and Lake per world, the menu backdrop's pair and a mod's own water materials. When every
// slot is taken the oldest is replaced (round robin): its water program is gone by then.
constexpr int kMaxTwins = 8;
Twin g_twins[kMaxTwins];
int g_nextSlot = 0;

bool Matches(const Twin& t, const Video::VideoProgram* water, Kind kind) {
    return t.water == water && t.kind == kind && t.waterData == static_cast<const void*>(water->_data) &&
           t.layout == static_cast<const void*>(water->_vertexLayout.Get()) &&
           t.vertex == static_cast<const void*>(water->_vertexProgram.Get()) &&
           t.state == static_cast<const void*>(water->_renderState.Get());
}

Video::PixelProgram* ShorePixel(Kind kind) {
    const int k = static_cast<int>(kind);
    if (g_shorePixelTried[k]) return g_shorePixel[k];
    g_shorePixelTried[k] = true;
    const uint8_t* bytes = kind == Kind::Lake ? g_bw_PSShoreLake : g_bw_PSShoreRiver;
    const int size = kind == Kind::Lake ? static_cast<int>(sizeof(g_bw_PSShoreLake))
                                        : static_cast<int>(sizeof(g_bw_PSShoreRiver));

    // The factory's own create (what a package load runs): the class operator new is not ported.
    Video::PixelProgram* p = static_cast<Video::PixelProgram*>(Video::PixelProgram::CreateNewObject());
    if (p == nullptr || p->_data == nullptr) {
        std::fprintf(stderr, "[BetterWater] no device: the shore program was not made\n");
        return nullptr;
    }
    // One permutation: a backend binds variant 0 for every shader detail the program lacks.
    p->_byteCode.SetSize(1);
    Collection::Array<uint8_t>& blob = p->_byteCode[0];
    blob.SetSize(size);
    std::memcpy(blob._data, bytes, static_cast<size_t>(size));
    if (!p->PlatformCreate()) {
        std::fprintf(stderr, "[BetterWater] the device refused the shore program: no shore on this device\n");
        return nullptr;
    }
    std::fprintf(stderr, "[BetterWater] %s shore program made (%d bytes)\n",
                 kind == Kind::Lake ? "lake" : "river", size);
    g_shorePixel[k] = p;
    return p;
}

Video::VideoProgram* MakeTwin(const Video::VideoProgram* water, Video::PixelProgram* shore) {
    Video::VideoProgram* t = static_cast<Video::VideoProgram*>(Video::VideoProgram::CreateNewObject());
    if (t == nullptr || t->_data == nullptr) return nullptr;
    t->_vertexLayout  = water->_vertexLayout;
    t->_vertexProgram = water->_vertexProgram;
    t->_pixelProgram  = shore;
    t->_renderState   = water->_renderState;
    t->PlatformCreate();
    return t;
}

const Video::VideoProgram* TwinFor(const Video::VideoProgram* water, Kind kind) {
    Video::PixelProgram* shore = ShorePixel(kind);
    if (shore == nullptr) return nullptr;

    for (const Twin& t : g_twins) {
        if (Matches(t, water, kind)) return t.twin;
    }
    // A new water program (a world load, the menu backdrop, a device restart). The twin it replaces
    // is left alive: a backend may still hold it in cached state, and it is a few hundred bytes.
    Twin& t = g_twins[g_nextSlot];
    g_nextSlot = (g_nextSlot + 1) % kMaxTwins;
    t.water = water;
    t.kind = kind;
    t.waterData = water->_data;
    t.layout = water->_vertexLayout.Get();
    t.vertex = water->_vertexProgram.Get();
    t.state = water->_renderState.Get();
    t.twin = MakeTwin(water, shore);
    std::fprintf(stderr, "[BetterWater] twin made for water program %p -> %p\n",
                 static_cast<const void*>(water), static_cast<const void*>(t.twin));
    return t.twin;
}

}  // namespace

bool Enabled() { return Ext::Ext().better_water; }

ShoreParams CurrentParams() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::WaterValues().p;
#else
    return ShoreParams{};
#endif
}

void DrawShore(Video::Context& context, Kind kind, int32_t a, int32_t b, int32_t c, int32_t d) {
    // The water material bound its VideoProgram (VideoProgram::SetupShaders) before WorldGeometry's
    // draw ran; that is the program the twin stands in for.
    const Video::VideoProgram* water = context._pixelConstantStaging.videoProgram;
    if (water == nullptr) return;
    const Video::VideoProgram* twin = TwinFor(water, kind);
    if (twin == nullptr) return;

    // The look goes to the shore program as its MaterialConstants (b1): written into the context's
    // material staging for this draw only, and retail's put back after it (dirty bit 0x8000 uploads
    // the staging at the next draw), so the next draw item sees exactly what it would have.
    float saved[4][4];
    std::memcpy(saved, context._pixelConstantStaging.material, sizeof(saved));
    const ShoreParams params = CurrentParams();
    static_assert(sizeof(params) == sizeof(saved), "ShoreParams fills the material staging");
    std::memcpy(context._pixelConstantStaging.material, &params, sizeof(saved));
    context._dirty |= 0x8000u;

    twin->SetupShaders(context);
    context.DrawIndexed(0, a, b, c, d, 1);

    std::memcpy(context._pixelConstantStaging.material, saved, sizeof(saved));
    context._dirty |= 0x8000u;
    water->SetupShaders(context);
}

}  // namespace BetterWater
}  // namespace Ext
