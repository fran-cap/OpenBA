// ============================================================================
// tests/unit/ext_better_clouds_test.cpp -- ext.better_clouds: the noise, the motion, the programs
// ============================================================================
// The baked noise tiles and is equalised (src/ext/better_clouds/cloud_noise.h); the motion runs at the
// game's speed capped at 3x, stands still while paused and carries every octave down the one wind
// (cloud_params.h); the constants carry the sun; the generated sky programs are valid ps_4_0 containers
// that fit the installed sky's vertex outputs, and the fit test refuses one that does not (sky_fit.h);
// the registry row (class A, ON) and its mode key. Every platform.
// ============================================================================
#include "unit.h"

#include "better_trees/dxbc.h"
#include "better_clouds/cloud_shadow_ps.h"
#include "better_clouds/cloud_sky_cheap_ps.h"
#include "better_clouds/cloud_sky_ps.h"
#include "ext/better_clouds/cloud_noise.h"
#include "ext/better_clouds/cloud_params.h"
#include "ext/better_clouds/sky_fit.h"
#include "ext/registry.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace Ext::BetterClouds;

namespace {

void Put(std::vector<uint8_t>& d, uint32_t v) {
    const size_t at = d.size();
    d.resize(at + 4);
    std::memcpy(d.data() + at, &v, 4);
}

// A container with one signature chunk.
std::vector<uint8_t> SigBlob(uint32_t fourcc, const std::vector<SigElement>& elements) {
    std::vector<uint8_t> d;
    Put(d, static_cast<uint32_t>(elements.size()));
    Put(d, 8);
    const uint32_t nameBase = static_cast<uint32_t>(8 + elements.size() * 24);
    std::vector<uint8_t> names;
    for (const SigElement& e : elements) {
        Put(d, nameBase + static_cast<uint32_t>(names.size()));
        Put(d, e.semanticIndex);
        Put(d, e.systemValue);
        Put(d, e.componentType);
        Put(d, e.reg);
        d.push_back(e.mask);
        d.push_back(e.rwMask);
        d.push_back(0);
        d.push_back(0);
        names.insert(names.end(), e.name.begin(), e.name.end());
        names.push_back(0);
    }
    d.insert(d.end(), names.begin(), names.end());
    Dxbc::Container c;
    Dxbc::Chunk chunk;
    chunk.fourcc = fourcc;
    chunk.data = d;
    c.chunks.push_back(chunk);
    return Dxbc::Emit(c);
}

SigElement El(const char* name, uint32_t index, uint32_t sv, uint32_t reg, uint8_t mask) {
    SigElement e;
    e.name = name;
    e.semanticIndex = index;
    e.systemValue = sv;
    e.componentType = 3;
    e.reg = reg;
    e.mask = mask;
    return e;
}

// The installed sky vertex program's outputs, as the modding kit's SkyMaterial.srsl describes them:
// the clip position and a float3 cube-map direction (the engine numbers interpolants from 1).
std::vector<uint8_t> InstalledVs() {
    return SigBlob(Dxbc::FourCC('O', 'S', 'G', 'N'), {El("SV_Position", 0, 1, 0, 0xF), El("TEXCOORD", 1, 0, 1, 0x7)});
}

double Offset(const Motion& m, int o, int axis) { return m.off[o][axis]; }

}  // namespace

UNIT_TEST(ext_better_clouds_noise_tiles_and_is_equalised) {
    const int n = 64;   // small: the same code at a quarter of the shipped size
    const std::vector<uint8_t> t = BakeNoise(n);
    CHECK(t.size() == static_cast<size_t>(n * n * 4));
    // Equalised: every channel spans 0..255 with a mean near the middle.
    for (int c = 0; c < 4; ++c) {
        int lo = 255;
        int hi = 0;
        double sum = 0.0;
        for (int i = 0; i < n * n; ++i) {
            const int v = t[static_cast<size_t>(i) * 4 + c];
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sum += v;
        }
        CHECK(lo == 0);
        CHECK(hi == 255);
        CHECK(std::fabs(sum / (n * n) - 127.5) < 2.0);
    }
    // Tiles: the step across the wrap is no bigger than the steps inside the tile.
    for (int c = 0; c < 4; ++c) {
        double inner = 0.0;
        double seam = 0.0;
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x + 1 < n; ++x) {
                inner += std::abs(t[(static_cast<size_t>(y) * n + x + 1) * 4 + c] - t[(static_cast<size_t>(y) * n + x) * 4 + c]);
            }
            seam += std::abs(t[(static_cast<size_t>(y) * n + 0) * 4 + c] - t[(static_cast<size_t>(y) * n + n - 1) * 4 + c]);
        }
        inner /= static_cast<double>(n * (n - 1));
        seam /= static_cast<double>(n);
        CHECK(seam < inner * 2.0 + 4.0);
    }
    // Deterministic.
    CHECK(BakeNoise(n) == t);
}

UNIT_TEST(ext_better_clouds_motion_speed_cap_and_pause) {
    Motion a;
    Motion b;
    Motion c;
    a.Advance(1.0, 3);
    b.Advance(1.0, 10);   // capped at 3x
    c.Advance(1.0, 0);    // paused
    for (int o = 0; o < 4; ++o) {
        CHECK(Offset(a, o, 0) == Offset(b, o, 0));
        CHECK(Offset(a, o, 1) == Offset(b, o, 1));
        CHECK(Offset(c, o, 0) == 0.0);
        CHECK(Offset(c, o, 1) == 0.0);
    }
    Motion d;
    d.Advance(1.0, 1);
    CHECK(std::fabs((1.0 - Offset(d, 0, 0)) * 3.0 - (1.0 - Offset(a, 0, 0))) < 1e-9);   // 3x moves three times as far
}

// Every octave moves down the one wind: un-rotating an octave's offset (off = -R W F) gives the same
// direction for all of them, along +wind.
UNIT_TEST(ext_better_clouds_motion_one_direction) {
    Motion m;
    m.Advance(0.5, 1);   // small: no wrap yet
    for (int o = 0; o < 4; ++o) {
        const Octave& k = kOctaves[o];
        double ox = Offset(m, o, 0);
        double oy = Offset(m, o, 1);
        if (ox > 0.5) ox -= 1.0;
        if (oy > 0.5) oy -= 1.0;
        // W = -R^T off / F
        const double wx = -(k.cosA * ox + k.sinA * oy) / k.freq;
        const double wy = -(-k.sinA * ox + k.cosA * oy) / k.freq;
        const double len = std::sqrt(wx * wx + wy * wy);
        CHECK(len > 0.0);
        CHECK(std::fabs(wx / len - kWindX) < 1e-6);
        CHECK(std::fabs(wy / len - kWindY) < 1e-6);
    }
}

UNIT_TEST(ext_better_clouds_motion_stays_wrapped) {
    Motion m;
    for (int i = 0; i < 200000; ++i) m.Advance(1.0 / 60.0, 3);   // about an hour at 3x
    for (int o = 0; o < 4; ++o) {
        for (int a = 0; a < 2; ++a) {
            CHECK(Offset(m, o, a) >= 0.0);
            CHECK(Offset(m, o, a) < 1.0);
        }
    }
}

UNIT_TEST(ext_better_clouds_constants_carry_the_sun_and_the_look) {
    Motion m;
    Look look;
    float c[16];
    Constants(m, look, 3.0f, 0.0f, 4.0f, c);   // up and to +x
    CHECK(std::fabs(c[8] - 0.6f) < 1e-5f);
    CHECK(c[9] == 0.0f);
    CHECK(std::fabs(c[10] - 0.8f) < 1e-5f);
    CHECK(c[11] == look.scale);
    CHECK(c[12] == look.white);
    CHECK(c[13] == look.shade);
    CHECK(c[14] == look.skyLift);
    CHECK(c[15] == look.coverage);
    Constants(m, look, 0.0f, 0.0f, 0.0f, c);   // no sun at all: straight up
    CHECK(c[8] == 0.0f && c[9] == 0.0f && c[10] == 1.0f);
}

// Morph 0: every octave travels the same world distance (rigid clouds); 1: the octaves' own speeds.
UNIT_TEST(ext_better_clouds_motion_morph) {
    Motion rigid;
    rigid.Advance(0.25, 1, 1.0, 0.0);
    double len[4];
    for (int o = 0; o < 4; ++o) {
        const Octave& k = kOctaves[o];
        double ox = Offset(rigid, o, 0);
        double oy = Offset(rigid, o, 1);
        if (ox > 0.5) ox -= 1.0;
        if (oy > 0.5) oy -= 1.0;
        len[o] = std::sqrt(ox * ox + oy * oy) / k.freq;   // world travel, un-scaled by the octave
    }
    for (int o = 1; o < 4; ++o) CHECK(std::fabs(len[o] - len[0]) < len[0] * 2e-3);   // the rotations are kept to 3 decimals
    Motion full;
    Motion dflt;
    full.Advance(0.25, 1, 1.0, 1.0);
    dflt.Advance(0.25, 1);   // the default morph argument is the full spread
    for (int o = 0; o < 4; ++o) CHECK(Offset(full, o, 0) == Offset(dflt, o, 0));
}

// The wind scale (the overlay's speed) scales the travel; 0 stops it.
UNIT_TEST(ext_better_clouds_motion_wind_scale) {
    Motion a;
    Motion b;
    Motion z;
    a.Advance(0.5, 1, 1.0);
    b.Advance(0.25, 1, 2.0);
    z.Advance(0.5, 1, 0.0);
    for (int o = 0; o < 4; ++o) {
        CHECK(std::fabs(Offset(a, o, 0) - Offset(b, o, 0)) < 1e-12);
        CHECK(Offset(z, o, 0) == 0.0);
    }
}

// Fancy's shadow terms: the sky's coverage, weaker under an overcast sky and a low sun, gone at night;
// the fade follows the fog, and no fog keeps the shadows to any distance.
UNIT_TEST(ext_better_clouds_shadow_terms) {
    const Look look;
    const ShadowTerms clear = ShadowTermsFor(look, 0.0f, 0.0f, 1.0f, -100.0f, 0.01f);
    CHECK(std::fabs(clear.coverage - (0.50f + look.coverage)) < 1e-5f);
    CHECK(std::fabs(clear.strength - look.shadowStrength) < 1e-5f);
    CHECK(std::fabs(clear.fadeFrom - 100.0f) < 1e-3f);
    CHECK(std::fabs(clear.fadeTo - 200.0f) < 1e-3f);
    const ShadowTerms storm = ShadowTermsFor(look, 0.0f, 1.0f, 1.0f, -100.0f, 0.01f);
    CHECK(storm.coverage > clear.coverage);
    CHECK(storm.coverage <= kShadowCoverageMax);   // gaps of sun remain
    CHECK(storm.strength < clear.strength * 0.25f);
    // Winter alone (the snow) keeps its shadows.
    const ShadowTerms winter = ShadowTermsFor(look, 1.0f, 0.0f, 1.0f, -100.0f, 0.01f);
    CHECK(std::fabs(winter.strength - clear.strength) < 1e-5f);
    CHECK(winter.coverage > clear.coverage);
    const ShadowTerms night = ShadowTermsFor(look, 0.0f, 0.0f, -0.2f, -100.0f, 0.01f);
    CHECK(night.strength == 0.0f);
    const ShadowTerms low = ShadowTermsFor(look, 0.0f, 0.0f, 0.05f, -100.0f, 0.01f);
    CHECK(low.strength > 0.0f && low.strength < clear.strength);
    const ShadowTerms noFog = ShadowTermsFor(look, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    CHECK(noFog.fadeFrom >= 1.0e5f);
    CHECK(noFog.fadeTo > noFog.fadeFrom);
}

// The shadow program is a ps_4_0 container (the backend creates it directly, no signature to fit).
UNIT_TEST(ext_better_clouds_shadow_program_is_ps_4_0) {
    Dxbc::Container c;
    std::string err;
    CHECK(Dxbc::Parse(g_bc_PSCloudShadow, sizeof(g_bc_PSCloudShadow), &c, &err));
    const Dxbc::Chunk* shdr = c.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    CHECK(shdr != nullptr);
    if (shdr == nullptr) return;
    uint32_t version = 0;
    std::memcpy(&version, shdr->data.data(), 4);
    CHECK((version >> 16) == 0);
    CHECK(((version >> 4) & 0xF) == 4);
}

UNIT_TEST(ext_better_clouds_programs_are_ps_4_0_and_fit_the_sky) {
    const uint8_t* blobs[2] = {g_bc_PSCloudSky, g_bc_PSCloudSkyCheap};
    const size_t sizes[2] = {sizeof(g_bc_PSCloudSky), sizeof(g_bc_PSCloudSkyCheap)};
    const std::vector<uint8_t> vs = InstalledVs();
    for (int i = 0; i < 2; ++i) {
        Dxbc::Container c;
        std::string err;
        CHECK(Dxbc::Parse(blobs[i], sizes[i], &c, &err));
        const Dxbc::Chunk* shdr = c.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
        CHECK(shdr != nullptr);
        if (shdr == nullptr) continue;
        uint32_t version = 0;
        std::memcpy(&version, shdr->data.data(), 4);
        CHECK((version >> 16) == 0);          // pixel
        CHECK(((version >> 4) & 0xF) == 4);   // ps_4_0
        std::string why;
        CHECK(PixelFitsVertex(blobs[i], sizes[i], vs.data(), vs.size(), &why));
        CHECK(why.empty());
    }
}

UNIT_TEST(ext_better_clouds_refuses_a_sky_it_does_not_fit) {
    std::string why;
    // The direction in another register.
    const std::vector<uint8_t> moved =
        SigBlob(Dxbc::FourCC('O', 'S', 'G', 'N'), {El("SV_Position", 0, 1, 0, 0xF), El("TEXCOORD", 1, 0, 2, 0x7)});
    CHECK(!PixelFitsVertex(g_bc_PSCloudSky, sizeof(g_bc_PSCloudSky), moved.data(), moved.size(), &why));
    // Only two components of it.
    const std::vector<uint8_t> narrow =
        SigBlob(Dxbc::FourCC('O', 'S', 'G', 'N'), {El("SV_Position", 0, 1, 0, 0xF), El("TEXCOORD", 1, 0, 1, 0x3)});
    CHECK(!PixelFitsVertex(g_bc_PSCloudSky, sizeof(g_bc_PSCloudSky), narrow.data(), narrow.size(), &why));
    // Not written at all.
    const std::vector<uint8_t> none = SigBlob(Dxbc::FourCC('O', 'S', 'G', 'N'), {El("SV_Position", 0, 1, 0, 0xF)});
    CHECK(!PixelFitsVertex(g_bc_PSCloudSky, sizeof(g_bc_PSCloudSky), none.data(), none.size(), &why));
    CHECK(why.find("TEXCOORD1") != std::string::npos);
    // Garbage.
    const uint8_t junk[16] = {1, 2, 3};
    CHECK(!PixelFitsVertex(g_bc_PSCloudSky, sizeof(g_bc_PSCloudSky), junk, sizeof(junk), &why));
}

UNIT_TEST(ext_better_clouds_row_is_class_a_default_on) {
    const Ext::Entry* row = Ext::FindEntry("better_clouds");
    CHECK(row != nullptr);
    if (row == nullptr) return;
    CHECK(row->cls == Ext::Class::A);
    CHECK(row->defaultOn);   // class A ON, the standing rule
    const Ext::ValueKey* mode = Ext::FindValueKey("better_clouds_mode");
    CHECK(mode != nullptr);
}
