// ============================================================================
// tests/unit/ext_texture_quality_test.cpp -- ext.texture_quality (block TEXQUALITY)
// ============================================================================
// The registry row (class A, default OFF, off under /profile:faithful, never a pseudo-mod) and its
// valued key ext.texture_quality_drop (0..1, default 0); the backend-neutral rule
// (src/render/common/texture_quality/texture_quality.h): which images qualify, and the pitch
// rebasing -- the subresources ImageBufferData::Create's faithful loop builds for every level, with
// the dropped ones taken off the front by DropLeading, are exactly the subresources the same loop
// builds for the smaller texture. docs/extensions/handins/TEXQUALITY.md.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "io/settings_file.h"
#include "texture_quality/texture_quality.h"

#include <cstdint>
#include <string>
#include <vector>

using TextureQuality::Classify;
using TextureQuality::DropFor;
using TextureQuality::DropLeading;
using TextureQuality::Verdict;

namespace {

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const IO::SettingsFile::Entries* entries = static_cast<const IO::SettingsFile::Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State Resolved(const char* text, const char* cmd = nullptr) {
    IO::SettingsFile::Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, cmd, nullptr);
}

// Video::ImageBuffer::GetLevelCount's ladder (buffers.cpp): halve both sides, floor 1, until both are 1.
int32_t LevelCount(int32_t w, int32_t h) {
    int32_t count = 1;
    while (w > 1 || h > 1) {
        h >>= 1;
        if (h == 0) h = 1;
        w >>= 1;
        if (w == 0) w = 1;
        ++count;
    }
    return count;
}

// The pitches ImageBufferData::Create's faithful loop gives `levels` subresources of a 2D texture
// `width` wide, `bytes` per pixel or per 4x4 block (imagebufferdata.cpp, the "initial data" loop).
std::vector<uint32_t> RetailPitches(int width, int levels, int bytes, bool compressed) {
    std::vector<uint32_t> out;
    int levelWidth = width;
    int levelIndex = 0;
    for (int i = 0; i < levels; ++i) {
        int rowUnits = levelWidth;
        if (compressed) rowUnits = levelWidth / 4;
        out.push_back(static_cast<uint32_t>(bytes * rowUnits));
        levelWidth >>= 1;
        if (compressed && levelWidth < 4) levelWidth = 4;
        if (++levelIndex == levels) {
            levelWidth = width;
            levelIndex = 0;
        }
    }
    return out;
}

// D3D11's own row pitch of level `mip` of a texture `width` wide (what the driver reads).
uint32_t D3dPitch(int width, int mip, int bytes, bool compressed) {
    int w = width >> mip;
    if (w < 1) w = 1;
    if (compressed) return static_cast<uint32_t>(bytes * ((w + 3) / 4));
    return static_cast<uint32_t>(bytes * w);
}

}  // namespace

UNIT_TEST(ext_texture_quality_row) {
    const Ext::Entry* e = Ext::FindEntry("texture_quality");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(!e->defaultOn);   // OFF by default: retail's textures until the player picks Half / Quarter
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.texture_quality");

    CHECK(!Resolved("").ext.texture_quality);
    CHECK(Resolved("ext.texture_quality=1\n").ext.texture_quality);
    CHECK(!Resolved("ext.texture_quality=1\nprofile=faithful\n").ext.texture_quality);
    CHECK(!Resolved("ext.texture_quality=1\n", "faithful").ext.texture_quality);   // the gate's /profile:faithful

    Ext::State on{};
    on.ext.texture_quality = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().texture_quality);
    CHECK(Ext::PseudoModsForSave(nullptr, 0) == 0);   // class A: never in the save
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_texture_quality_drop_key) {
    const Ext::ValueKey* k = Ext::FindValueKey("texture_quality_drop");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK_STREQ(k->owner, "texture_quality");
    CHECK_EQ(k->minValue, 0);
    CHECK_EQ(k->maxValue, 1);       // Full / Half: Quarter (2) removed by the user's ruling
    CHECK_EQ(k->defaultValue, 0);   // Full: the row or profile=all alone drops nothing (the user's ruling)
    CHECK(k->choices == nullptr);   // a range key: an older 2 clamps to 1, it does not fall back to 0
    CHECK_EQ(Ext::Values{}.texture_quality_drop, 0);
    CHECK_EQ(Resolved("").val.texture_quality_drop, 0);
    CHECK_EQ(Resolved("ext.texture_quality_drop=1\n").val.texture_quality_drop, 1);
    CHECK_EQ(Resolved("ext.texture_quality_drop=2\n").val.texture_quality_drop, 1);    // an older Quarter: Half
    CHECK_EQ(Resolved("ext.texture_quality_drop=7\n").val.texture_quality_drop, 1);    // clamped
    CHECK_EQ(Resolved("ext.texture_quality_drop=-1\n").val.texture_quality_drop, 0);   // clamped
    CHECK_EQ(Resolved("ext.texture_quality_drop=x\n").val.texture_quality_drop, 0);    // malformed: default
    // profile=all turns the row on and leaves the key at its default: Full.
    const Ext::State all = Resolved("ext.texture_quality_drop=2\nprofile=all\n");
    CHECK(all.ext.texture_quality);
    CHECK_EQ(all.val.texture_quality_drop, 0);
}

UNIT_TEST(ext_texture_quality_rule) {
    const int32_t n1024 = LevelCount(1024, 1024);
    CHECK_EQ(n1024, 11);
    // A qualifying world texture: usage 2, 2D, mipmapped, the whole chain in the file.
    CHECK(Classify(1, 2, 0, 1u, 1024, 1024, n1024, n1024) == Verdict::Drop);
    CHECK_EQ(DropFor(1, 2, 0, 1u, 1024, 1024, n1024, n1024), 1);
    CHECK_EQ(DropFor(2, 2, 0, 1u, 1024, 1024, n1024, n1024), 2);
    CHECK_EQ(DropFor(1, 2, 0, 1u, 2048, 512, LevelCount(2048, 512), LevelCount(2048, 512)), 1);
    // Off: nothing drops.
    CHECK(Classify(0, 2, 0, 1u, 1024, 1024, n1024, n1024) == Verdict::Off);
    CHECK_EQ(DropFor(0, 2, 0, 1u, 1024, 1024, n1024, n1024), 0);
    // Not a file-seeded texture (render targets, depth, dynamic, generated, rendered).
    for (int32_t kind : { 0, 1, 3, 4, 5 })
        CHECK(Classify(1, kind, 0, 1u, 1024, 1024, n1024, n1024) == Verdict::NotUsage2);
    // Cubes keep every level.
    CHECK(Classify(1, 2, 1, 1u, 1024, 1024, n1024, n1024) == Verdict::Cube);
    // Sprite sheets and fonts: not mipmapped (their pixel rects are 1:1 texels).
    CHECK(Classify(1, 2, 0, 0u, 1024, 1024, 1, 1) == Verdict::NotMipmapped);
    CHECK(Classify(1, 2, 0, 0x40u, 2048, 2048, 1, 1) == Verdict::NotMipmapped);
    // A file whose level count is not the ladder's.
    CHECK(Classify(1, 2, 0, 1u, 1024, 1024, n1024, n1024 - 1) == Verdict::LevelCount);
    // Small textures (and anything under 512 on either side).
    CHECK(Classify(1, 2, 0, 1u, 256, 256, LevelCount(256, 256), LevelCount(256, 256)) == Verdict::Small);
    CHECK(Classify(1, 2, 0, 1u, 1024, 256, LevelCount(1024, 256), LevelCount(1024, 256)) == Verdict::Small);
    CHECK(Classify(1, 2, 0, 1u, 16, 16, 5, 5) == Verdict::Small);
    CHECK(Classify(1, 2, 0, 1u, 8, 8, 4, 4) == Verdict::FewLevels);
    // Sides that do not halve into whole 4x4 blocks: 4 << drop must divide both.
    CHECK(Classify(1, 2, 0, 1u, 1000, 1000, LevelCount(1000, 1000), LevelCount(1000, 1000)) == Verdict::Drop);
    CHECK(Classify(2, 2, 0, 1u, 1000, 1000, LevelCount(1000, 1000), LevelCount(1000, 1000)) == Verdict::OddSize);
    CHECK(Classify(1, 2, 0, 1u, 1028, 1024, LevelCount(1028, 1024), LevelCount(1028, 1024)) == Verdict::OddSize);
    for (int v = 0; v < TextureQuality::kVerdictCount; ++v)
        CHECK(TextureQuality::VerdictName(static_cast<Verdict>(v))[0] != '?');
}

UNIT_TEST(ext_texture_quality_drop_leading) {
    int a[5] = { 10, 11, 12, 13, 14 };
    CHECK_EQ(DropLeading(a, 5, 0), 5);
    CHECK_EQ(a[0], 10);
    CHECK_EQ(DropLeading(a, 5, 2), 3);
    CHECK_EQ(a[0], 12);
    CHECK_EQ(a[1], 13);
    CHECK_EQ(a[2], 14);
    int b[2] = { 1, 2 };
    CHECK_EQ(DropLeading(b, 2, 2), 0);
    CHECK_EQ(DropLeading(b, 2, 5), 0);
    CHECK_EQ(DropLeading<int>(nullptr, 0, 1), 0);
}

// The pitch rebasing: the faithful loop's pitches for the full chain, minus the dropped front,
// equal the loop's pitches for the half / quarter texture, and both are D3D11's own row pitches.
UNIT_TEST(ext_texture_quality_pitch_rebasing) {
    struct Format {
        int  bytes;
        bool compressed;
    };
    const Format formats[] = { { 8, true }, { 16, true }, { 1, false }, { 4, false } };   // BC1, BC3, R8 / A8, BGRA8
    const int sizes[][2] = { { 512, 512 }, { 1024, 1024 }, { 2048, 2048 }, { 2048, 512 }, { 512, 2048 }, { 1000, 1000 } };
    int checked = 0;
    for (const Format& f : formats) {
        for (const auto& wh : sizes) {
            const int w = wh[0];
            const int h = wh[1];
            const int levels = LevelCount(w, h);
            for (int drop = 1; drop <= TextureQuality::kMaxDrop; ++drop) {
                if (DropFor(drop, 2, 0, 1u, w, h, levels, levels) != drop) continue;
                std::vector<uint32_t> full = RetailPitches(w, levels, f.bytes, f.compressed);
                const int kept = DropLeading(full.data(), static_cast<int>(full.size()), drop);
                CHECK_EQ(kept, levels - drop);
                CHECK_EQ(LevelCount(w >> drop, h >> drop), levels - drop);
                const std::vector<uint32_t> small = RetailPitches(w >> drop, levels - drop, f.bytes, f.compressed);
                // A power-of-two side: the loop's pitches are D3D11's own. (A side such as 1000 is not:
                // retail's BC row count truncates (125 px = 31 blocks, D3D says 32) for the full texture
                // as for the small one; nor are the levels of a tall texture past a 1-pixel width, where
                // an uncompressed row's halving reaches 0. The drop keeps retail's arithmetic, it does
                // not correct it.)
                const bool pot = (w & (w - 1)) == 0;
                for (int i = 0; i < kept; ++i) {
                    CHECK_EQ(full[static_cast<size_t>(i)], small[static_cast<size_t>(i)]);
                    if (pot && (w >> (drop + i)) >= 1)
                        CHECK_EQ(full[static_cast<size_t>(i)], D3dPitch(w >> drop, i, f.bytes, f.compressed));
                }
                ++checked;
            }
        }
    }
    CHECK(checked > 30);
}
