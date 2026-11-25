// ============================================================================
// tests/unit/ext_citizen_skins_test.cpp -- ext.citizen_skins: the strip reader, the chain, the program patch,
// the sets
// ============================================================================
// A compiled Texture resource is read at its top level (BC1, BC3, BGRA8, grey; anything else refused, and a
// size taken from the file is bounded first); strips join side by side into one BGRA8 chain with the engine's
// level ladder; the vertex program's one `l(0.125)` becomes 1 / (8 N) with the DXBC checksum made again, and
// a blob that is not that program is left alone; which packages' strips become sets (equal pixels once, a
// missing half from the first package, other sizes out, the limits); a citizen's set is a function of its
// name and the sets (pinned values, a set taken out moves only its own citizens); the registry row (class A,
// off). Every platform.
// ============================================================================
#include "unit.h"

#include "better_trees/dxbc.h"
#include "ext/citizen_skins/skin_math.h"
#include "ext/registry.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Ext::CitizenSkins;

namespace {

void PutU32(std::vector<uint8_t>& d, uint32_t v) {
    const size_t at = d.size();
    d.resize(at + 4);
    std::memcpy(d.data() + at, &v, 4);
}

void PutStr(std::vector<uint8_t>& d, const char* s) {
    const size_t n = std::strlen(s) + 1;
    PutU32(d, 0x80000000u | static_cast<uint32_t>(n));
    d.insert(d.end(), s, s + n);
}

// A compiled Texture resource, as the compiler writes one: header, template reference, nested block, ints,
// one level.
std::vector<uint8_t> Crs(int w, int h, int format, const std::vector<uint8_t>& level0, bool typeRef = true) {
    std::vector<uint8_t> d;
    PutU32(d, 0xaaaaaaaau);
    for (int i = 0; i < 6; ++i) PutU32(d, static_cast<uint32_t>(i));
    PutStr(d, "Models\\Citizen\\MaterialInstance\\MaleCitizen.png");
    d.insert(d.end(), 8, 0);
    if (typeRef) {
        PutStr(d, "Texture\\ModelTexture.rsc");
        d.insert(d.end(), 8, 0);
    }
    PutU32(d, 0xaaaaaaaau);
    PutU32(d, 0);
    PutU32(d, 0);
    PutU32(d, static_cast<uint32_t>(w));
    PutU32(d, static_cast<uint32_t>(h));
    PutU32(d, 0);
    PutU32(d, 2);
    PutU32(d, static_cast<uint32_t>(format));
    for (int i = 0; i < 5; ++i) PutU32(d, 1);
    PutStr(d, "Models\\Citizen\\MaterialInstance\\MaleCitizen.png");
    PutU32(d, 1);   // one level
    PutU32(d, static_cast<uint32_t>(level0.size()));
    d.insert(d.end(), level0.begin(), level0.end());
    PutU32(d, 0xbbbbbbbbu);
    PutU32(d, 0xbbbbbbbbu);
    return d;
}

// A BC1 block of one colour (both references the same RGB565, every index 0).
std::vector<uint8_t> Bc1Solid(uint16_t rgb565) {
    std::vector<uint8_t> b(8, 0);
    std::memcpy(b.data(), &rgb565, 2);
    std::memcpy(b.data() + 2, &rgb565, 2);
    return b;
}

Strip Solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Strip s;
    s.width = w;
    s.height = h;
    s.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (size_t i = 0; i < s.rgba.size(); i += 4) {
        s.rgba[i] = r;
        s.rgba[i + 1] = g;
        s.rgba[i + 2] = b;
        s.rgba[i + 3] = 255;
    }
    return s;
}

// A vertex-program-shaped container: one SHEX chunk holding `eighths` `mul r0.x, r0.x, l(0.125)` operands.
std::vector<uint8_t> Program(int eighths) {
    Dxbc::Container c;
    Dxbc::Chunk shex;
    shex.fourcc = Dxbc::FourCC('S', 'H', 'E', 'X');
    std::vector<uint8_t>& d = shex.data;
    PutU32(d, 0x00010040);   // version token
    PutU32(d, 4 + 4 * static_cast<uint32_t>(eighths) + 2);
    for (int i = 0; i < eighths; ++i) {
        PutU32(d, 0x00000038);   // some opcode word
        PutU32(d, 0x00004001);   // a one-component literal ...
        PutU32(d, 0x3e000000);   // ... 0.125
        PutU32(d, 0x00100012);
    }
    PutU32(d, 0x0100003e);   // ret
    PutU32(d, 0x00000000);
    c.chunks.push_back(shex);
    return Dxbc::Emit(c);
}

// The literal the program multiplies the texture coordinate by (0 when there is none).
float LiteralOf(const std::vector<uint8_t>& blob) {
    for (size_t i = 0; i + 8 <= blob.size(); i += 4) {
        uint32_t tok;
        std::memcpy(&tok, blob.data() + i, 4);
        if (tok == 0x00004001) {
            float value;
            std::memcpy(&value, blob.data() + i + 4, 4);
            return value;
        }
    }
    return 0.0f;
}

// A package's pair as ChooseSets takes it: strips of one colour each (the colour is what tells two apart).
PackSkin PackOf(uint8_t colour, int maleWidth = 8, int femaleWidth = 8, bool male = true,
                bool female = true) {
    PackSkin p;
    if (male) {
        p.male = Solid(maleWidth, 1, colour, 0, 0);
        p.hasMale = true;
        p.hashMale = StripHash(p.male);
    }
    if (female) {
        p.female = Solid(femaleWidth, 1, 0, colour, 0);
        p.hasFemale = true;
        p.hashFemale = StripHash(p.female);
    }
    return p;
}

}  // namespace

UNIT_TEST(ext_citizen_skins_reads_a_bc1_strip) {
    // 8 x 4: two blocks, red then blue.
    std::vector<uint8_t> px = Bc1Solid(0xF800);
    const std::vector<uint8_t> blue = Bc1Solid(0x001F);
    px.insert(px.end(), blue.begin(), blue.end());
    const std::vector<uint8_t> file = Crs(8, 4, 0, px);
    Strip s;
    std::string why;
    CHECK(ReadTextureLevel0(file.data(), file.size(), &s, &why));
    CHECK(s.width == 8 && s.height == 4);
    if (s.rgba.size() != 8 * 4 * 4) return;
    CHECK(s.rgba[0] == 255 && s.rgba[1] == 0 && s.rgba[2] == 0 && s.rgba[3] == 255);   // pixel (0,0): red
    const size_t blueAt = 4 * 4;                                                       // pixel (4,0): blue
    CHECK(s.rgba[blueAt] == 0 && s.rgba[blueAt + 1] == 0 && s.rgba[blueAt + 2] == 255);
}

UNIT_TEST(ext_citizen_skins_reads_the_other_formats_and_refuses_the_rest) {
    // BGRA8: stored B, G, R, A; read as R, G, B, A.
    const std::vector<uint8_t> bgra = {10, 20, 30, 40, 11, 21, 31, 41};
    // no template reference: the nested block follows at once
    const std::vector<uint8_t> f5 = Crs(2, 1, 5, bgra, false);
    Strip s;
    CHECK(ReadTextureLevel0(f5.data(), f5.size(), &s, nullptr));
    CHECK(s.rgba.size() == 8 && s.rgba[0] == 30 && s.rgba[1] == 20 && s.rgba[2] == 10 && s.rgba[3] == 40);
    // 8-bit grey.
    const std::vector<uint8_t> f4 = Crs(2, 1, 4, {7, 9});
    CHECK(ReadTextureLevel0(f4.data(), f4.size(), &s, nullptr));
    CHECK(s.rgba.size() == 8 && s.rgba[0] == 7 && s.rgba[4] == 9 && s.rgba[7] == 255);
    // BC3: alpha ramp from 255 down (a0 = 255 > a1 = 0, every index 0 -> 255), colour red.
    std::vector<uint8_t> bc3 = {255, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<uint8_t> red = Bc1Solid(0xF800);
    bc3.insert(bc3.end(), red.begin(), red.end());
    const std::vector<uint8_t> f1 = Crs(4, 4, 1, bc3);
    CHECK(ReadTextureLevel0(f1.data(), f1.size(), &s, nullptr));
    CHECK(s.rgba.size() == 64 && s.rgba[0] == 255 && s.rgba[1] == 0 && s.rgba[3] == 255);
    // A format this does not know: refused, with a reason.
    std::string why;
    const std::vector<uint8_t> f9 = Crs(2, 1, 9, bgra);
    CHECK(!ReadTextureLevel0(f9.data(), f9.size(), &s, &why));
    CHECK(!why.empty());
    // A level whose declared size is not the format's, a cut file, garbage.
    const std::vector<uint8_t> shortLevel = Crs(8, 4, 0, std::vector<uint8_t>(8, 0));
    CHECK(!ReadTextureLevel0(shortLevel.data(), shortLevel.size(), &s, nullptr));
    CHECK(!ReadTextureLevel0(f5.data(), f5.size() - 14, &s, nullptr));
    const uint8_t junk[40] = {1, 2, 3};
    CHECK(!ReadTextureLevel0(junk, sizeof(junk), &s, nullptr));
}

UNIT_TEST(ext_citizen_skins_chain_puts_the_strips_side_by_side) {
    const Strip a = Solid(16, 2, 255, 0, 0);
    const Strip b = Solid(16, 2, 0, 255, 0);
    const Strip c = Solid(16, 2, 0, 0, 255);
    Chain chain;
    std::string why;
    CHECK(BuildChain({&a, &b, &c}, &chain, &why));
    CHECK(chain.width == 48 && chain.height == 2);
    // The ladder of Video::ImageBuffer::GetLevelCount: 48x2, 24x1, 12x1, 6x1, 3x1, 1x1.
    CHECK(chain.levels.size() == 6);
    if (chain.levels.size() != 6) return;
    CHECK(chain.levels[0].size() == 48 * 2 * 4);
    CHECK(chain.levels[1].size() == 24 * 1 * 4);
    CHECK(chain.levels[5].size() == 1 * 1 * 4);
    // BGRA: the first strip's red is B 0, G 0, R 255; the second's green; the third's blue.
    const std::vector<uint8_t>& l0 = chain.levels[0];
    CHECK(l0[0] == 0 && l0[1] == 0 && l0[2] == 255 && l0[3] == 255);
    CHECK(l0[16 * 4] == 0 && l0[16 * 4 + 1] == 255 && l0[16 * 4 + 2] == 0);
    CHECK(l0[32 * 4] == 255 && l0[32 * 4 + 1] == 0 && l0[32 * 4 + 2] == 0);
    // A level is its box-filtered parent: a solid strip stays solid until the strips meet.
    CHECK(chain.levels[1][0] == 0 && chain.levels[1][2] == 255);
    // One strip alone, mixed sizes, nothing.
    CHECK(BuildChain({&a}, &chain, nullptr) && chain.width == 16);
    const Strip odd = Solid(8, 2, 0, 0, 0);
    CHECK(!BuildChain({&a, &odd}, &chain, &why));
    CHECK(!BuildChain({}, &chain, &why));
}

UNIT_TEST(ext_citizen_skins_a_slot_lands_on_its_cell_through_the_patched_literal) {
    // The program computes u' = (u + slot) * literal, literal = what the patch wrote. For every set count,
    // cell k of set s must start at pixel s * 1024 + k * 128 of the joined texture.
    for (int n = 1; n <= kMaxSets; ++n) {
        std::vector<uint8_t> blob = Program(1);
        CHECK(PatchVertexBlob(&blob, n) == 1);
        const double literal = LiteralOf(blob);
        for (int set = 0; set < n; ++set) {
            for (int variant = 0; variant < kCells; ++variant) {
                const int slot = SlotFor(set, variant);
                CHECK(slot == set * 8 + variant);
                const double pixel = (0.0 + slot) * literal * (1024.0 * n);   // the left edge of the cell
                const double want = static_cast<double>(set * 1024 + variant * 128);
                CHECK(pixel > want - 0.05 && pixel < want + 0.05);
            }
        }
    }
    CHECK(SlotFor(2, 9) == 17);   // a variant past 7 cannot spill into the next set
}

UNIT_TEST(ext_citizen_skins_patches_the_one_eighth) {
    const std::vector<uint8_t> original = Program(1);
    CHECK(CountEighths(original.data(), original.size()) == 1);
    std::vector<uint8_t> blob = original;
    CHECK(PatchVertexBlob(&blob, 3) == 1);
    // The size is the same, the literal is 1/24, the checksum is a real one, and nothing else moved.
    CHECK(blob.size() == original.size());
    size_t differing = 0;
    for (size_t i = 0; i < blob.size(); ++i) differing += blob[i] != original[i] ? 1 : 0;
    CHECK(differing > 0 && differing <= 4 + 16);
    uint32_t sum[4];
    Dxbc::Checksum(blob.data(), blob.size(), sum);
    CHECK(std::memcmp(sum, blob.data() + 4, 16) == 0);
    CHECK(CountEighths(blob.data(), blob.size()) == 0);
    Dxbc::Container parsed;
    std::string err;
    CHECK(Dxbc::Parse(blob.data(), blob.size(), &parsed, &err));
    float value = 0.0f;
    bool found = false;
    for (size_t i = 0; i + 8 <= blob.size(); i += 4) {
        uint32_t tok;
        std::memcpy(&tok, blob.data() + i, 4);
        if (tok == 0x00004001) {
            std::memcpy(&value, blob.data() + i + 4, 4);
            found = true;
        }
    }
    CHECK(found && value == 1.0f / 24.0f);
    // One set is the identity.
    std::vector<uint8_t> one = original;
    CHECK(PatchVertexBlob(&one, 1) == 1);
    CHECK(one == original);
}

UNIT_TEST(ext_citizen_skins_leaves_other_programs_alone) {
    // No literal, two literals, a broken checksum, not a container, too many sets: untouched, 0.
    std::vector<uint8_t> none = Program(0);
    const std::vector<uint8_t> noneBefore = none;
    CHECK(PatchVertexBlob(&none, 2) == 0 && none == noneBefore);
    std::vector<uint8_t> two = Program(2);
    const std::vector<uint8_t> twoBefore = two;
    CHECK(PatchVertexBlob(&two, 2) == 0 && two == twoBefore);
    std::vector<uint8_t> bad = Program(1);
    bad[10] ^= 0xff;   // a byte of the stored checksum: it no longer matches the container
    const std::vector<uint8_t> badBefore = bad;
    CHECK(PatchVertexBlob(&bad, 2) == 0 && bad == badBefore);
    std::vector<uint8_t> junk(64, 7);
    CHECK(PatchVertexBlob(&junk, 2) == 0);
    CHECK(CountEighths(junk.data(), junk.size()) == -1);
    std::vector<uint8_t> fine = Program(1);
    CHECK(PatchVertexBlob(&fine, 0) == 0);
    CHECK(PatchVertexBlob(&fine, kMaxSets + 1) == 0);
    CHECK(PatchVertexBlob(nullptr, 2) == 0);
}

namespace {

// Set identities as the glue makes them: from two pixel hashes, so no two sets share one.
std::vector<uint64_t> Ids(int n, uint64_t seed = 0) {
    std::vector<uint64_t> ids;
    for (int i = 0; i < n; ++i) {
        ids.push_back(SetIdentity(seed + 1000u + static_cast<uint64_t>(i) * 7919u, 31u + i));
    }
    return ids;
}

uint64_t IdFor(const std::string& name, const std::vector<uint64_t>& ids) {
    return ids[static_cast<size_t>(SetForName(name.c_str(), ids.data(), static_cast<int>(ids.size())))];
}

}  // namespace

UNIT_TEST(ext_citizen_skins_a_citizen_keeps_its_set) {
    // Stable, in range, null-safe, and spread over the sets.
    const std::vector<uint64_t> eight = Ids(8);
    CHECK(SetForName("Anna", eight.data(), 1) == 0);
    CHECK(SetForName("Anna", nullptr, 4) == 0);
    CHECK(SetForName(nullptr, eight.data(), 4) >= 0 && SetForName(nullptr, eight.data(), 4) < 4);
    CHECK(SetForName("Anna", eight.data(), 5) == SetForName("Anna", eight.data(), 5));
    int count[8] = {};
    for (int i = 0; i < 4000; ++i) {
        const std::string name = "Citizen " + std::to_string(i);
        const int s = SetForName(name.c_str(), eight.data(), 8);
        CHECK(s >= 0 && s < 8);
        if (s >= 0 && s < 8) ++count[s];
    }
    for (int s = 0; s < 8; ++s) CHECK(count[s] > 350 && count[s] < 650);   // 500 each, within a loose band
    // and over the most sets there can be
    const std::vector<uint64_t> sixteen = Ids(kMaxSets);
    int many[kMaxSets] = {};
    for (int i = 0; i < 8000; ++i) {
        const int s = SetForName(("Citizen " + std::to_string(i)).c_str(), sixteen.data(), kMaxSets);
        CHECK(s >= 0 && s < kMaxSets);
        if (s >= 0 && s < kMaxSets) ++many[s];
    }
    for (int s = 0; s < kMaxSets; ++s) CHECK(many[s] > 350 && many[s] < 650);   // 500 each
}

UNIT_TEST(ext_citizen_skins_a_removed_set_moves_only_its_own_citizens) {
    // Six sets, then the mod of one is taken out (from the middle, the front and the back): every citizen who
    // did not wear it keeps the very same set, and the ones who did spread over what is left.
    const std::vector<uint64_t> full = Ids(6);
    for (size_t gone = 0; gone < full.size(); ++gone) {
        std::vector<uint64_t> less = full;
        less.erase(less.begin() + static_cast<long>(gone));
        int movedFromOthers = 0, onTheGone = 0, spread[5] = {};
        for (int i = 0; i < 3000; ++i) {
            const std::string name = "Villager " + std::to_string(i);
            const uint64_t before = IdFor(name, full);
            const uint64_t after = IdFor(name, less);
            if (before == full[gone]) {
                ++onTheGone;
                CHECK(after != full[gone]);
                for (size_t k = 0; k < less.size(); ++k) {
                    if (less[k] == after) ++spread[k];
                }
            } else if (after != before) {
                ++movedFromOthers;
            }
        }
        CHECK(movedFromOthers == 0);                       // nobody else changed
        CHECK(onTheGone > 350 && onTheGone < 650);         // about 1 in 6 wore it
        for (int k = 0; k < 5; ++k) CHECK(spread[k] > 0);  // and they went to all the others
    }
}

UNIT_TEST(ext_citizen_skins_an_added_set_only_attracts_its_share) {
    const std::vector<uint64_t> five = Ids(5);
    std::vector<uint64_t> six = five;
    six.push_back(SetIdentity(424242u, 17u));
    int moved = 0, toNew = 0;
    for (int i = 0; i < 3000; ++i) {
        const std::string name = "Villager " + std::to_string(i);
        const uint64_t before = IdFor(name, five);
        const uint64_t after = IdFor(name, six);
        if (after != before) {
            ++moved;
            // a citizen who moves moves to the new set, never between old ones
            CHECK(after == six.back());
            ++toNew;
        }
    }
    CHECK(moved == toNew);
    CHECK(moved > 350 && moved < 650);      // about 1 in 6
}

UNIT_TEST(ext_citizen_skins_the_order_of_the_sets_does_not_matter) {
    // The same sets in another order give the same set to the same citizen (a mod moved up or down the list).
    const std::vector<uint64_t> ids = Ids(7);
    std::vector<uint64_t> reversed(ids.rbegin(), ids.rend());
    std::vector<uint64_t> rotated(ids.begin() + 3, ids.end());
    rotated.insert(rotated.end(), ids.begin(), ids.begin() + 3);
    for (int i = 0; i < 2000; ++i) {
        const std::string name = "Citizen " + std::to_string(i);
        const uint64_t want = IdFor(name, ids);
        CHECK(IdFor(name, reversed) == want);
        CHECK(IdFor(name, rotated) == want);
    }
}

UNIT_TEST(ext_citizen_skins_a_set_has_one_identity_from_its_pixels) {
    CHECK(SetIdentity(1, 2) == SetIdentity(1, 2));
    CHECK(SetIdentity(1, 2) != SetIdentity(2, 1));     // male and female are not interchangeable
    CHECK(SetIdentity(1, 2) != SetIdentity(1, 3));
    CHECK(SetIdentity(0, 0) != SetIdentity(0, 1));
}

UNIT_TEST(ext_citizen_skins_names_the_plain_and_the_numbered_pairs) {
    bool f = true;
    const char* base = "Models\\Citizen\\MaterialInstance\\CitizenTexture";
    // the plain names retail reads: the mod's own pair
    CHECK(ParseStripName("Models\\Citizen\\MaterialInstance\\CitizenTexture_resource.crs", &f) == 0 && !f);
    CHECK(ParseStripName("Models\\Citizen\\MaterialInstance\\CitizenTextureF_resource.crs", &f) == 0 && f);
    // numbered extras, either gender, any case, either slash
    CHECK(ParseStripName((std::string(base) + "2_resource.crs").c_str(), &f) == 2 && !f);
    CHECK(ParseStripName((std::string(base) + "F2_resource.crs").c_str(), &f) == 2 && f);
    CHECK(ParseStripName((std::string(base) + "F17_resource.crs").c_str(), &f) == 17 && f);
    CHECK(ParseStripName((std::string(base) + "99_resource.crs").c_str(), &f) == 99);
    CHECK(ParseStripName("MODELS/CITIZEN/MATERIALINSTANCE/CITIZENTEXTUREF3_RESOURCE.CRS", &f) == 3 && f);
    // not strips: another entry, a number that is not one, a rsc, a different folder or name
    CHECK(ParseStripName((std::string(base) + "1_resource.crs").c_str(), &f) == -1);
    CHECK(ParseStripName((std::string(base) + "0_resource.crs").c_str(), &f) == -1);
    CHECK(ParseStripName((std::string(base) + "02_resource.crs").c_str(), &f) == -1);
    CHECK(ParseStripName((std::string(base) + "100_resource.crs").c_str(), &f) == -1);
    CHECK(ParseStripName((std::string(base) + "2x_resource.crs").c_str(), &f) == -1);
    CHECK(ParseStripName((std::string(base) + "FF2_resource.crs").c_str(), &f) == -1);
    CHECK(ParseStripName((std::string(base) + "2.rsc").c_str(), &f) == -1);
    CHECK(ParseStripName((std::string(base) + "2_resource.crs.bak").c_str(), &f) == -1);
    CHECK(ParseStripName("Models\\Citizen\\MaterialInstance\\MaleNomadTexture_resource.crs", &f) == -1);
    CHECK(ParseStripName("Models\\Citizen\\CitizenTexture2_resource.crs", &f) == -1);
    CHECK(ParseStripName("CitizenTexture2_resource.crs", &f) == -1);
    CHECK(ParseStripName("", &f) == -1 && ParseStripName(nullptr, &f) == -1);
    // the gender flag may be left out
    CHECK(ParseStripName((std::string(base) + "F4_resource.crs").c_str(), nullptr) == 4);
}

UNIT_TEST(ext_citizen_skins_bc3_colours_are_always_four_colour) {
    // A BC3 colour block with c0 <= c1 is still the four-colour mode (BC1 would read it as three colours and
    // clear black): c0 = black, c1 = red, every index 3 -> two thirds of the way to red.
    std::vector<uint8_t> blk = {255, 0, 0, 0, 0, 0, 0, 0};   // alpha: a0 = 255, every index 0
    const uint8_t colour[8] = {0x00, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF};
    blk.insert(blk.end(), colour, colour + 8);
    const std::vector<uint8_t> file = Crs(4, 4, 1, blk);
    Strip s;
    CHECK(ReadTextureLevel0(file.data(), file.size(), &s, nullptr));
    CHECK(s.rgba.size() == 64 && s.rgba[0] == 170 && s.rgba[1] == 0 && s.rgba[2] == 0 && s.rgba[3] == 255);
    // BC1 itself keeps the three-colour mode: index 3 is clear black.
    std::vector<uint8_t> bc1(colour, colour + 8);
    const std::vector<uint8_t> file1 = Crs(4, 4, 0, bc1);
    Strip s1;
    CHECK(ReadTextureLevel0(file1.data(), file1.size(), &s1, nullptr));
    CHECK(s1.rgba.size() == 64 && s1.rgba[0] == 0 && s1.rgba[3] == 0);
}

UNIT_TEST(ext_citizen_skins_a_files_size_is_bounded_before_anything_is_decoded) {
    // A strip over 2 M pixels is refused (2048 x 2048 is 4 M), and nothing is allocated for it.
    Strip big;
    std::string why;
    const std::vector<uint8_t> file = Crs(2048, 2048, 5, std::vector<uint8_t>(2048u * 2048u * 4u, 0), false);
    CHECK(!ReadTextureLevel0(file.data(), file.size(), &big, &why));
    CHECK(!why.empty() && big.rgba.empty());
    // The size the first strip had can be required: another size is refused too.
    const std::vector<uint8_t> small = Crs(2, 1, 5, {1, 2, 3, 4, 5, 6, 7, 8}, false);
    Strip s;
    CHECK(ReadTextureLevel0(small.data(), small.size(), &s, nullptr, 2, 1));
    Strip other;
    CHECK(!ReadTextureLevel0(small.data(), small.size(), &other, &why, 4, 1) && other.rgba.empty());
    CHECK(!why.empty());
    // The joined image is never wider than D3D11 allows.
    const Strip a = Solid(8192, 1, 1, 2, 3);
    Chain chain;
    CHECK(BuildChain({&a, &a}, &chain, nullptr) && chain.width == 16384);
    CHECK(!BuildChain({&a, &a, &a}, &chain, &why));
}

UNIT_TEST(ext_citizen_skins_choose_counts_equal_pixels_once) {
    // vanilla (1), a mod (2), the same mod again, another (3), a copy of vanilla: three sets, in that order.
    std::vector<PackSkin> packs = {PackOf(1), PackOf(2), PackOf(2), PackOf(3), PackOf(1)};
    std::vector<std::string> notes;
    const Sets sets = ChooseSets(packs, &notes);
    CHECK(sets.male.size() == 3 && sets.female.size() == 3 && sets.ids.size() == 3);
    if (sets.male.size() != 3) return;
    CHECK(sets.male[0] == &packs[0].male && sets.male[1] == &packs[1].male && sets.male[2] == &packs[3].male);
    CHECK(sets.ids[0] == SetIdentity(packs[0].hashMale, packs[0].hashFemale));
    CHECK(sets.ids[0] != sets.ids[1] && sets.ids[1] != sets.ids[2] && sets.ids[0] != sets.ids[2]);
    CHECK(notes.empty());
    // a package that has the same pixels but moved up the list does not change the sets' identities
    std::vector<PackSkin> moved = {PackOf(1), PackOf(3), PackOf(2)};
    const Sets sets2 = ChooseSets(moved, nullptr);
    CHECK(sets2.ids.size() == 3 && sets2.ids[1] == sets.ids[2] && sets2.ids[2] == sets.ids[1]);
}

UNIT_TEST(ext_citizen_skins_choose_gives_a_missing_half_the_first_packages) {
    std::vector<PackSkin> packs = {PackOf(1), PackOf(2, 8, 8, true, false), PackOf(3, 8, 8, false, true)};
    const Sets sets = ChooseSets(packs, nullptr);
    CHECK(sets.male.size() == 3);
    if (sets.male.size() != 3) return;
    CHECK(sets.male[1] == &packs[1].male && sets.female[1] == &packs[0].female);   // a male-only mod
    CHECK(sets.male[2] == &packs[0].male && sets.female[2] == &packs[2].female);   // a female-only mod
    // The first package must have both, or there is no vanilla to build on: no sets, and a note says why.
    std::vector<PackSkin> noBase = {PackOf(1, 8, 8, true, false), PackOf(2)};
    std::vector<std::string> notes;
    CHECK(ChooseSets(noBase, &notes).male.empty());
    CHECK(notes.size() == 1);
    CHECK(ChooseSets({}, nullptr).male.empty());
}

UNIT_TEST(ext_citizen_skins_choose_leaves_out_other_sizes_and_stops_at_the_limits) {
    // another size than the first package's is left out, with a note
    std::vector<PackSkin> packs = {PackOf(1), PackOf(2, 16, 16), PackOf(3, 8, 16), PackOf(4)};
    std::vector<std::string> notes;
    const Sets sets = ChooseSets(packs, &notes);
    CHECK(sets.male.size() == 2 && notes.size() == 2);
    // the most sets there are
    std::vector<PackSkin> many;
    for (int c = 1; c <= 40; ++c) many.push_back(PackOf(static_cast<uint8_t>(c)));
    CHECK(static_cast<int>(ChooseSets(many, nullptr).male.size()) == kMaxSets);
    // the joined image is never wider than 16384: strips 4096 wide allow 4 sets, whichever gender is wider
    std::vector<PackSkin> wideMale;
    std::vector<PackSkin> wideFemale;
    for (int c = 1; c <= 10; ++c) {
        wideMale.push_back(PackOf(static_cast<uint8_t>(c), 4096, 1024));
        wideFemale.push_back(PackOf(static_cast<uint8_t>(c), 1024, 4096));
    }
    CHECK(ChooseSets(wideMale, nullptr).male.size() == 4);
    CHECK(ChooseSets(wideFemale, nullptr).male.size() == 4);
}

// The strip hash feeds every set identity, so every citizen's look: its start is the FNV-1a offset basis
// (14695981039346656037), and a change of it after a release would reshuffle every village.
UNIT_TEST(ext_citizen_skins_the_strip_hash_is_fnv1a_and_pinned) {
    Strip s;
    s.width = 1;
    s.height = 1;
    s.rgba = {1, 2, 3, 4};
    CHECK(StripHash(s) == 0xbe7a5e765165785cull);
    Strip empty;
    CHECK(StripHash(empty) == 14695981039346656037ull);   // no bytes, no size: the basis itself
}

UNIT_TEST(ext_citizen_skins_the_pick_is_pinned) {
    // Pinned values: if the hash ever changed, every village's look would change with it. (Computed apart
    // from the code with the same arithmetic.)
    CHECK(SetIdentity(1, 2) == 0xf2826f98653e9e57ull);
    CHECK(SetIdentity(0, 0) == 0x48218226ff3cd4bfull);
    const uint64_t ids[5] = {SetIdentity(1, 2), SetIdentity(3, 4), SetIdentity(5, 6), SetIdentity(7, 8),
                             SetIdentity(9, 10)};
    const struct {
        const char* name;
        int set;           // of the five
        int withoutOne;    // the same citizen with the second set taken out (its index in the five)
    } want[] = {{"Anna", 0, 0}, {"Bert", 0, 0}, {"Carla", 2, 2}, {"Dmitri", 1, 2}, {"Eve", 1, 4},
                {"Fritz", 4, 4}, {"Gustav", 2, 2}, {"Hanna", 0, 0}, {"Ivo", 1, 0}, {"Jana", 0, 0},
                {"Karl", 0, 0}, {"Lena", 4, 4}};
    const uint64_t four[4] = {ids[0], ids[2], ids[3], ids[4]};
    const int fourIndex[4] = {0, 2, 3, 4};
    for (const auto& w : want) {
        CHECK(SetForName(w.name, ids, 5) == w.set);
        CHECK(fourIndex[SetForName(w.name, four, 4)] == w.withoutOne);
        CHECK((w.set == 1) == (w.set != w.withoutOne));   // only the citizens of the removed set moved
    }
}

UNIT_TEST(ext_citizen_skins_row_is_class_a_default_off) {
    const Ext::Entry* row = Ext::FindEntry("citizen_skins");
    CHECK(row != nullptr);
    if (row == nullptr) return;
    CHECK(row->cls == Ext::Class::A);
    CHECK(!row->defaultOn);
}
