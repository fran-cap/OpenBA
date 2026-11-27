// ============================================================================
// tests/unit/ext_custom_maps_test.cpp -- ext.custom_maps: the .bmap file
// (docs/extensions/handins/CUSTOMMAPS.md)
// ============================================================================
// src/ext/custom_maps/bmap.{h,cpp} and custom_maps.{h,cpp}, standard C++. What is pinned:
//   * the checksum is the zlib CRC-32 (the check value of "123456789");
//   * a map written and parsed again is the same map in every field, and writing twice gives the same bytes;
//   * the optional parts (resources, start, thumbnail, paths) are told apart from empty ones;
//   * a damaged, short, mis-signed, mis-versioned or oversized file is refused with its own status, and
//     a hostile count (a path, point, resource or thumbnail count far larger than the data) is refused
//     without reading or allocating what it claims;
//   * Write refuses what Parse would refuse;
//   * Validate: each rule, error or warning as the game uses it;
//   * the water coverage: every underwater vertex lies in some feature's box, no cell is in two boxes, the
//     features come in tile order, a dry map has none;
//   * the size row lookup, the pending map and the coverage start state;
//   * the PNG reader (png.h): the three DEFLATE block kinds, all five row filters, RGB / RGBA / palette
//     (with tRNS) / gray at 1 and 16 bits against files zlib wrote, and every damaged or unsupported
//     file refused;
//   * the map list (map_list.h): newest first, only .bmap files of the folder itself, a damaged file
//     listed as such.
// ============================================================================
#include "unit.h"
#include "ext/custom_maps/bmap.h"
#include "ext/custom_maps/custom_maps.h"
#include "ext/custom_maps/map_list.h"
#include "ext/custom_maps/png.h"
#include "ext/better_water/river_mouth.h"
#include "ext/water_origin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace CM = Ext::CustomMaps;

namespace {

constexpr uint32_t kSize = 64;   // the smallest allowed size keeps the files small

// Land everywhere (height 1.0), with a river that starts west of the play area, crosses it and leaves
// to the east, the way the boats' route builder wants path 0.
CM::Map ValidMap() {
    CM::Map map;
    map.mapSize = kSize;
    map.name    = "Test valley";
    map.author  = "unit test";
    map.note    = "a map for the tests";
    map.heights.assign(CM::HeightCount(kSize), 256);
    CM::Path river;
    for (int i = 0; i < 6; ++i) {
        CM::PathPoint pt;
        pt.x         = -static_cast<float>(kSize) + static_cast<float>(i) * 25.6f;
        pt.y         = 3.0f;
        pt.halfWidth = 4.0f;
        river.points.push_back(pt);
    }
    map.paths.push_back(river);
    return map;
}

CM::Map FullMap() {
    CM::Map map = ValidMap();
    map.hasResources = true;
    CM::Resource a;
    a.templatePath = "Template/NaturalResourceRock.rsc";
    a.cellX        = 10;
    a.cellY        = 20;
    a.variant      = 3;
    a.growth       = 200;
    CM::Resource b;
    b.templatePath = "Template\\NaturalResourceOak.rsc";
    b.cellX        = static_cast<uint16_t>(kSize - 1);
    b.cellY        = 0;
    map.resources.push_back(a);
    map.resources.push_back(b);
    map.hasStart = true;
    map.startX   = 32;
    map.startY   = 33;
    map.thumbnail.assign({0x89, 'P', 'N', 'G', 1, 2, 3});
    CM::ModRef rocks;
    rocks.name      = "Rock Pack";
    rocks.author    = "someone";
    rocks.templates = {"Template/NaturalResourceRock.rsc"};
    CM::ModRef trees;
    trees.name      = "Tree Pack";
    trees.templates = {"Template\\NaturalResourceOak.rsc", "Template/NaturalResourcePine.rsc"};
    map.mods.push_back(rocks);
    map.mods.push_back(trees);
    return map;
}

bool SameMap(const CM::Map& a, const CM::Map& b) {
    if (a.mapSize != b.mapSize || a.name != b.name || a.author != b.author || a.note != b.note ||
        a.heights != b.heights || a.paths.size() != b.paths.size() || a.hasResources != b.hasResources ||
        a.resources.size() != b.resources.size() || a.hasStart != b.hasStart || a.startX != b.startX ||
        a.startY != b.startY || a.thumbnail != b.thumbnail || a.mods.size() != b.mods.size() ||
        a.hasProvenance != b.hasProvenance ||
        (a.hasProvenance && (std::memcmp(a.provenance.contentId, b.provenance.contentId, 32) != 0 ||
                             std::memcmp(a.provenance.originId, b.provenance.originId, 32) != 0 ||
                             std::memcmp(a.provenance.parentId, b.provenance.parentId, 32) != 0 ||
                             a.provenance.created != b.provenance.created ||
                                 a.provenance.saved != b.provenance.saved))) {
        return false;
    }
    for (size_t i = 0; i < a.mods.size(); ++i) {
        if (a.mods[i].name != b.mods[i].name || a.mods[i].author != b.mods[i].author ||
            a.mods[i].templates != b.mods[i].templates) {
            return false;
        }
    }
    for (size_t p = 0; p < a.paths.size(); ++p) {
        if (a.paths[p].points.size() != b.paths[p].points.size()) {
            return false;
        }
        for (size_t i = 0; i < a.paths[p].points.size(); ++i) {
            const CM::PathPoint& x = a.paths[p].points[i];
            const CM::PathPoint& y = b.paths[p].points[i];
            if (x.x != y.x || x.y != y.y || x.halfWidth != y.halfWidth) {
                return false;
            }
        }
    }
    for (size_t i = 0; i < a.resources.size(); ++i) {
        const CM::Resource& x = a.resources[i];
        const CM::Resource& y = b.resources[i];
        if (x.templatePath != y.templatePath || x.cellX != y.cellX || x.cellY != y.cellY ||
            x.variant != y.variant || x.growth != y.growth) {
            return false;
        }
    }
    return true;
}

void SetCrc(std::vector<uint8_t>& file) {
    const size_t body = file.size() - 4;
    const uint32_t crc = CM::Crc32(file.data(), body);
    for (int i = 0; i < 4; ++i) {
        file[body + i] = static_cast<uint8_t>((crc >> (8 * i)) & 0xffu);
    }
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xffu));
    }
}

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

// A file with the real header and heights and a caller-chosen tail, checksum included, so a test can
// say anything after the heights (a count far larger than the bytes that follow).
std::vector<uint8_t> RawFile(uint32_t flags, const std::vector<uint8_t>& tail) {
    std::vector<uint8_t> f;
    PutU32(f, 0x50414d42u);
    PutU32(f, CM::kVersion);
    PutU32(f, kSize);
    PutU32(f, flags);
    PutU16(f, 0);
    PutU16(f, 0);
    PutU16(f, 0);
    f.resize(f.size() + CM::HeightCount(kSize) * 2, 0);
    f.insert(f.end(), tail.begin(), tail.end());
    PutU32(f, 0);
    SetCrc(f);
    return f;
}

bool HasText(const std::vector<CM::Issue>& issues, const char* part, bool error) {
    for (const CM::Issue& issue : issues) {
        if (issue.error == error && issue.text.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// The map the editor's own fingerprint code (map editor, index.html) was run on: heights ((i * 37) % 1000) -
// 500, one path of three points, three resources and a start. The three expected values below come from that
// code, so the two programs agree on the canonical bytes.
CM::Map IdMap() {
    CM::Map map;
    map.mapSize = kSize;
    map.heights.resize(CM::HeightCount(kSize));
    for (size_t i = 0; i < map.heights.size(); ++i) {
        map.heights[i] = static_cast<int16_t>((static_cast<int>(i) * 37) % 1000 - 500);
    }
    CM::Path path;
    for (int i = 0; i < 3; ++i) {
        CM::PathPoint pt;
        pt.x         = -64.0f + 64.0f * static_cast<float>(i);
        pt.y         = 3.0f;
        pt.halfWidth = 4.0f;
        path.points.push_back(pt);
    }
    map.paths.push_back(path);
    const struct {
        const char* t;
        uint16_t    x, y;
        uint8_t     v, g;
    } res[3] = {{"Template/NaturalResourceRock.rsc", 10, 20, 3, 250}, {"Template/Aaa.rsc", 5, 5, 1, 2},
                {"Template/NaturalResourceRock.rsc", 10, 3, 3, 250}};
    map.hasResources = true;
    for (const auto& r : res) {
        CM::Resource e;
        e.templatePath = r.t;
        e.cellX        = r.x;
        e.cellY        = r.y;
        e.variant      = r.v;
        e.growth       = r.g;
        map.resources.push_back(e);
    }
    map.hasStart = true;
    map.startX   = 7;
    map.startY   = 9;
    return map;
}

std::string IdOf(const CM::Map& map) {
    uint8_t id[32];
    CM::ContentId(map, id);
    return CM::IdHex(id);
}

}  // namespace

UNIT_TEST(ext_custom_maps_the_content_id_matches_the_editors) {
    CM::Map map = IdMap();
    CHECK(IdOf(map) == "896cc09a3f65c8b42aae5cf46568fa1c647366e16c977f1e6a65353f6c77e3d6");
    CM::Map noStart = map;
    noStart.hasStart = false;
    CHECK(IdOf(noStart) == "fe5dc8bcca6b26c419d96533035ac4cc2fe4002f102e4c1dd707151cafc5ab41");
    CM::Map noRes = map;
    noRes.resources.clear();
    CHECK(IdOf(noRes) == "836eaa67c44ad0b0ff2922f53029a522a5a7304a58b4d66cdc29d0c227c767c2");
}

UNIT_TEST(ext_custom_maps_the_content_id_ignores_what_is_not_content) {
    CM::Map map = IdMap();
    const std::string id = IdOf(map);
    map.name   = "another name";
    map.author = "somebody else";
    map.note   = "a different note";
    map.thumbnail.assign(10, 7);
    CM::ModRef mod;
    mod.name = "Rock Pack";
    map.mods.push_back(mod);
    CHECK(IdOf(map) == id);
    // the order resources are listed in does not matter
    std::reverse(map.resources.begin(), map.resources.end());
    CHECK(IdOf(map) == id);
    map.heights[100] += 1;
    CHECK(IdOf(map) != id);
    map = IdMap();
    map.resources[0].growth = 249;
    CHECK(IdOf(map) != id);
    map = IdMap();
    map.paths[0].points[1].halfWidth = 4.5f;
    CHECK(IdOf(map) != id);
    map = IdMap();
    map.startX = 8;
    CHECK(IdOf(map) != id);
}

UNIT_TEST(ext_custom_maps_the_provenance_block_round_trips_and_is_checked) {
    CM::Map map = IdMap();
    map.hasProvenance = true;
    uint8_t id[32];
    CM::ContentId(map, id);
    std::memcpy(map.provenance.contentId, id, 32);
    std::memset(map.provenance.originId, 0xab, 32);
    std::memset(map.provenance.parentId, 0, 32);
    map.provenance.created = 1700000000;
    map.provenance.saved   = 1700000100;
    const std::vector<uint8_t> file = CM::Write(map);
    CM::Map back;
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Ok);
    CHECK(back.hasProvenance);
    CHECK(SameMap(map, back));
    CHECK(file == CM::Write(back));
    CHECK(!HasText(CM::Validate(back), "ID does not match", false));
    // a map without the block stays without it
    CM::Map plain = IdMap();
    const std::vector<uint8_t> plainFile = CM::Write(plain);
    CM::Map plainBack;
    CHECK(CM::Parse(plainFile.data(), plainFile.size(), plainBack) == CM::Status::Ok);
    CHECK(!plainBack.hasProvenance);
    CHECK(plainFile.size() + 112 == file.size());
    // the content changed without the ID: a warning, and the map is still usable
    back.heights[200] += 1;
    CHECK(HasText(CM::Validate(back), "ID does not match", false));
    CHECK(!HasText(CM::Validate(back), "ID does not match", true));
    // a block cut short is refused
    std::vector<uint8_t> cut = RawFile(8u, std::vector<uint8_t>(50, 0));
    CM::Map bad;
    CHECK(CM::Parse(cut.data(), cut.size(), bad) != CM::Status::Ok);
}

UNIT_TEST(ext_custom_maps_crc32_is_the_zlib_one) {
    const char* check = "123456789";
    CHECK_EQ(CM::Crc32(reinterpret_cast<const uint8_t*>(check), 9), 0xcbf43926u);
    CHECK_EQ(CM::Crc32(reinterpret_cast<const uint8_t*>(check), 0), 0u);
}

UNIT_TEST(ext_custom_maps_height_count_is_the_generators) {
    CHECK_EQ(CM::HeightCount(256), 513u * 513u);
    CHECK_EQ(CM::HeightCount(384), 769u * 769u);
    CHECK_EQ(CM::HeightCount(512), 1025u * 1025u);
}

UNIT_TEST(ext_custom_maps_round_trip_keeps_every_field) {
    const CM::Map        map  = FullMap();
    const std::vector<uint8_t> file = CM::Write(map);
    CHECK(!file.empty());
    CM::Map back;
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Ok);
    CHECK(SameMap(map, back));
    CHECK(file == CM::Write(back));   // the same bytes again
    CHECK(file == CM::Write(map));
}

UNIT_TEST(ext_custom_maps_round_trip_minimal_map) {
    CM::Map map = ValidMap();
    map.paths.clear();
    const std::vector<uint8_t> file = CM::Write(map);
    CM::Map back;
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Ok);
    CHECK(SameMap(map, back));
    CHECK(!back.hasResources);
    CHECK(!back.hasStart);
    CHECK(back.thumbnail.empty());
    CHECK(back.paths.empty());
}

UNIT_TEST(ext_custom_maps_an_empty_resource_list_is_not_an_absent_one) {
    CM::Map none = ValidMap();
    CM::Map empty = ValidMap();
    empty.hasResources = true;
    CM::Map backNone;
    CM::Map backEmpty;
    const std::vector<uint8_t> a = CM::Write(none);
    const std::vector<uint8_t> b = CM::Write(empty);
    CHECK(CM::Parse(a.data(), a.size(), backNone) == CM::Status::Ok);
    CHECK(CM::Parse(b.data(), b.size(), backEmpty) == CM::Status::Ok);
    CHECK(!backNone.hasResources);
    CHECK(backEmpty.hasResources);
    CHECK(backEmpty.resources.empty());
    CHECK(a != b);
}

UNIT_TEST(ext_custom_maps_the_mod_list_round_trips_and_is_optional) {
    const CM::Map full = FullMap();
    const std::vector<uint8_t> file = CM::Write(full);
    CM::Map back;
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Ok);
    CHECK_EQ(back.mods.size(), static_cast<size_t>(2));
    CHECK(back.mods[0].name == "Rock Pack");
    CHECK(back.mods[0].author == "someone");
    CHECK(back.mods[1].author.empty());
    CHECK_EQ(back.mods[1].templates.size(), static_cast<size_t>(2));
    CHECK(SameMap(full, back));

    // A map with no list writes no section: its bytes differ from the same map with one, and it parses with
    // none.
    CM::Map plain = full;
    plain.mods.clear();
    const std::vector<uint8_t> a = CM::Write(plain);
    CM::Map backPlain;
    CHECK(CM::Parse(a.data(), a.size(), backPlain) == CM::Status::Ok);
    CHECK(backPlain.mods.empty());
    CHECK(a != file);
    CHECK(a.size() < file.size());

    // The list is information only: a mod with no template and a map without resources are both fine.
    CM::Map odd = ValidMap();
    CM::ModRef empty;
    empty.name = "Nothing used";
    odd.mods.push_back(empty);
    const std::vector<uint8_t> b = CM::Write(odd);
    CM::Map backOdd;
    CHECK(CM::Parse(b.data(), b.size(), backOdd) == CM::Status::Ok);
    CHECK(SameMap(odd, backOdd));
    CHECK(!CM::HasError(CM::Validate(odd)));
}

UNIT_TEST(ext_custom_maps_a_hostile_mod_list_is_refused) {
    // The mod section sits after the resource section, so these files carry no paths and no resources:
    // path count 0, then the mod count.
    std::vector<uint8_t> tooMany;
    PutU32(tooMany, 0);
    PutU16(tooMany, static_cast<uint16_t>(CM::kMaxMods + 1));
    CM::Map back;
    std::vector<uint8_t> f = RawFile(4u, tooMany);
    CHECK(CM::Parse(f.data(), f.size(), back) == CM::Status::TooMany);

    std::vector<uint8_t> lie;
    PutU32(lie, 0);
    PutU16(lie, static_cast<uint16_t>(CM::kMaxMods));   // 64 entries claimed, none there
    f = RawFile(4u, lie);
    CHECK(CM::Parse(f.data(), f.size(), back) == CM::Status::Truncated);

    std::vector<uint8_t> manyTemplates;
    PutU32(manyTemplates, 0);
    PutU16(manyTemplates, 1);
    PutU16(manyTemplates, 0);   // name
    PutU16(manyTemplates, 0);   // author
    PutU16(manyTemplates, static_cast<uint16_t>(CM::kMaxModTemplates + 1));
    f = RawFile(4u, manyTemplates);
    CHECK(CM::Parse(f.data(), f.size(), back) == CM::Status::TooMany);
    CHECK(back.mods.empty());

    CM::Map tooLong = FullMap();
    tooLong.mods[0].name.assign(CM::kMaxNameLength + 1, 'x');
    CHECK(CM::Write(tooLong).empty());
    CM::Map tooManyMods = FullMap();
    tooManyMods.mods.assign(CM::kMaxMods + 1, CM::ModRef());
    CHECK(CM::Write(tooManyMods).empty());
    CM::Map badTemplate = FullMap();
    badTemplate.mods[0].templates[0] = "../evil.rsc";
    CHECK(HasText(CM::Validate(badTemplate), "mod list", true));
}

UNIT_TEST(ext_custom_maps_the_new_game_row_names_the_missing_mods) {
    const CM::Map map = FullMap();   // Rock Pack: 1 template, Tree Pack: 2
    auto everything = [](const std::string&) { return true; };
    auto nothing    = [](const std::string&) { return false; };
    auto noOak      = [](const std::string& path) { return path.find("Oak") == std::string::npos; };

    CHECK(CM::ModsLine(CM::CheckMods(map, everything)).empty());
    CHECK(CM::ModsLine(CM::CheckMods(map, nothing)) == "Needs: Rock Pack, Tree Pack");
    CHECK(CM::ModsLine(CM::CheckMods(map, noOak)) == "Needs: Tree Pack (1 of 2 missing)");

    const std::vector<CM::ModCheck> checks = CM::CheckMods(map, noOak);
    CHECK_EQ(checks.size(), static_cast<size_t>(2));
    CHECK_EQ(checks[0].missing, static_cast<size_t>(0));
    CHECK_EQ(checks[1].total, static_cast<size_t>(2));

    CM::Map many = map;
    for (int i = 0; i < 3; ++i) {
        CM::ModRef extra;
        extra.name      = "Extra " + std::to_string(i);
        extra.templates = {"Template/NaturalResourceExtra.rsc"};
        many.mods.push_back(extra);
    }
    CHECK(CM::ModsLine(CM::CheckMods(many, nothing)) == "Needs: Rock Pack, Tree Pack and 3 more");

    CM::Map none = map;
    none.mods.clear();
    CHECK(CM::CheckMods(none, nothing).empty());
    CHECK(CM::ModsLine(CM::CheckMods(none, nothing)).empty());

    CM::Map unnamed = map;
    unnamed.mods.resize(1);
    unnamed.mods[0].name.clear();
    CHECK(CM::ModsLine(CM::CheckMods(unnamed, nothing)) == "Needs: (unnamed mod)");
}

UNIT_TEST(ext_custom_maps_heights_survive_the_extremes) {
    CM::Map map = ValidMap();
    map.heights[0]  = std::numeric_limits<int16_t>::min();
    map.heights[1]  = std::numeric_limits<int16_t>::max();
    map.heights[2]  = -1;
    map.heights[3]  = 0;
    const std::vector<uint8_t> file = CM::Write(map);
    CM::Map back;
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Ok);
    CHECK_EQ(back.heights[0], std::numeric_limits<int16_t>::min());
    CHECK_EQ(back.heights[1], std::numeric_limits<int16_t>::max());
    CHECK_EQ(back.heights[2], static_cast<int16_t>(-1));
    CHECK_EQ(back.heights[3], static_cast<int16_t>(0));
}

UNIT_TEST(ext_custom_maps_each_damaged_byte_is_refused) {
    const std::vector<uint8_t> file = CM::Write(FullMap());
    // A byte flipped in the header, the text, the heights, the paths, the resources, the tail and
    // the checksum itself: the file is refused each time and `back` stays empty.
    const size_t spots[] = {20, 30, 100, file.size() / 2, file.size() - 60, file.size() - 20,
                            file.size() - 8, file.size() - 1};
    for (size_t at : spots) {
        std::vector<uint8_t> bad = file;
        bad[at] ^= 0x40;
        CM::Map back;
        const CM::Status status = CM::Parse(bad.data(), bad.size(), back);
        CHECK(status != CM::Status::Ok);
        CHECK(back.heights.empty());
        CHECK(back.name.empty());
    }
}

UNIT_TEST(ext_custom_maps_header_faults_have_their_own_status) {
    const std::vector<uint8_t> file = CM::Write(ValidMap());
    CM::Map back;

    std::vector<uint8_t> magic = file;
    magic[0] = 'X';
    CHECK(CM::Parse(magic.data(), magic.size(), back) == CM::Status::BadMagic);

    std::vector<uint8_t> version = file;
    version[4] = 2;
    CHECK(CM::Parse(version.data(), version.size(), back) == CM::Status::BadVersion);

    std::vector<uint8_t> size = file;
    size[8] = 100;   // 100 is not a multiple of 8
    CHECK(CM::Parse(size.data(), size.size(), back) == CM::Status::BadSize);
    size[8] = 0;
    size[9] = 8;     // 2048 is over the cap
    CHECK(CM::Parse(size.data(), size.size(), back) == CM::Status::BadSize);

    CHECK(CM::Parse(file.data(), 10, back) == CM::Status::TooShort);
    CHECK(CM::Parse(nullptr, 100, back) == CM::Status::TooShort);
    CHECK(CM::Parse(file.data(), 0, back) == CM::Status::TooShort);
}

UNIT_TEST(ext_custom_maps_data_after_the_checksum_is_refused) {
    std::vector<uint8_t> file = CM::Write(ValidMap());
    file.insert(file.end() - 4, 0x55);   // one stray byte before the checksum, checksum recomputed
    SetCrc(file);
    CM::Map back;
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::TrailingBytes);
}

UNIT_TEST(ext_custom_maps_every_prefix_is_refused_and_never_crashes) {
    const std::vector<uint8_t> file = CM::Write(FullMap());
    CM::Map back;
    for (size_t n = 0; n < file.size(); ++n) {
        const bool dense = n < 200 || n + 200 > file.size();
        if (!dense && (n % 97) != 0) {
            continue;
        }
        CHECK(CM::Parse(file.data(), n, back) != CM::Status::Ok);
    }
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Ok);
}

UNIT_TEST(ext_custom_maps_hostile_counts_are_refused_without_allocating_them) {
    CM::Map back;

    std::vector<uint8_t> manyPaths;
    PutU32(manyPaths, 0xffffffffu);
    std::vector<uint8_t> file = RawFile(0, manyPaths);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::TooMany);

    std::vector<uint8_t> longPath;
    PutU32(longPath, 1);
    PutU32(longPath, 0x7fffffffu);
    file = RawFile(0, longPath);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::TooMany);

    std::vector<uint8_t> shortPath;
    PutU32(shortPath, 1);
    PutU32(shortPath, 1);
    file = RawFile(0, shortPath);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::TooMany);

    std::vector<uint8_t> liePath;   // 4096 points claimed, none present
    PutU32(liePath, 1);
    PutU32(liePath, CM::kMaxPathPoints);
    file = RawFile(0, liePath);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Truncated);

    std::vector<uint8_t> manyResources;   // 2 flag bit
    PutU32(manyResources, 0);
    PutU32(manyResources, 0xffffffffu);
    file = RawFile(2, manyResources);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::TooMany);

    std::vector<uint8_t> lieResources;   // under the cap but more than the data can hold
    PutU32(lieResources, 0);
    PutU32(lieResources, CM::kMaxResources);
    file = RawFile(2, lieResources);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Truncated);

    std::vector<uint8_t> bigThumb;
    PutU32(bigThumb, 0);
    PutU32(bigThumb, 0xffffffffu);
    file = RawFile(0, bigThumb);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::TooMany);

    std::vector<uint8_t> lieThumb;   // under the cap but not there
    PutU32(lieThumb, 0);
    PutU32(lieThumb, CM::kMaxThumbnail);
    file = RawFile(0, lieThumb);
    CHECK(CM::Parse(file.data(), file.size(), back) == CM::Status::Truncated);

    CHECK(back.heights.empty());
    CHECK(back.paths.empty());
    CHECK(back.resources.empty());
}

UNIT_TEST(ext_custom_maps_a_long_text_is_refused) {
    std::vector<uint8_t> f;
    PutU32(f, 0x50414d42u);
    PutU32(f, CM::kVersion);
    PutU32(f, kSize);
    PutU32(f, 0);
    PutU16(f, 65);   // a name over the cap
    f.resize(f.size() + 65, 'a');
    PutU32(f, 0);
    SetCrc(f);
    CM::Map back;
    CHECK(CM::Parse(f.data(), f.size(), back) == CM::Status::BadText);

    CM::Map long_name = ValidMap();
    long_name.name.assign(65, 'n');
    CHECK(CM::Write(long_name).empty());
    long_name.name.assign(64, 'n');
    CHECK(!CM::Write(long_name).empty());
}

UNIT_TEST(ext_custom_maps_write_refuses_what_parse_would_refuse) {
    CM::Map badSize = ValidMap();
    badSize.mapSize = 100;
    CHECK(CM::Write(badSize).empty());

    CM::Map wrongHeights = ValidMap();
    wrongHeights.heights.pop_back();
    CHECK(CM::Write(wrongHeights).empty());

    CM::Map tooManyPaths = ValidMap();
    const CM::Path onePath = tooManyPaths.paths[0];   // a copy: assign must not read an element it frees
    tooManyPaths.paths.assign(CM::kMaxPaths + 1, onePath);
    CHECK(CM::Write(tooManyPaths).empty());

    CM::Map shortPath = ValidMap();
    shortPath.paths[0].points.resize(1);
    CHECK(CM::Write(shortPath).empty());

    CM::Map bigThumbnail = ValidMap();
    bigThumbnail.thumbnail.assign(CM::kMaxThumbnail + 1, 1);
    CHECK(CM::Write(bigThumbnail).empty());

    CM::Map longTemplate = FullMap();
    longTemplate.resources[0].templatePath.assign(CM::kMaxTemplatePath + 1, 'a');
    CHECK(CM::Write(longTemplate).empty());
}

UNIT_TEST(ext_custom_maps_a_valid_map_has_no_issue) {
    CHECK(CM::Validate(ValidMap()).empty());
    CHECK(CM::Validate(FullMap()).empty());
}

UNIT_TEST(ext_custom_maps_validate_size_and_height_list) {
    CM::Map badSize = ValidMap();
    badSize.mapSize = 100;
    CHECK(HasText(CM::Validate(badSize), "not allowed", true));
    CM::Map wrong = ValidMap();
    wrong.heights.pop_back();
    CHECK(HasText(CM::Validate(wrong), "does not match", true));
}

UNIT_TEST(ext_custom_maps_validate_needs_land) {
    CM::Map water = ValidMap();
    water.heights.assign(water.heights.size(), -512);
    CHECK(HasText(CM::Validate(water), "above the water", true));

    // Water outside the play area does not count: only the middle square is looked at.
    CM::Map ringed = ValidMap();
    const int side = static_cast<int>(CM::GridVertices(kSize));
    for (int row = 0; row < side; ++row) {
        for (int col = 0; col < side; ++col) {
            const int  from   = static_cast<int>(kSize) / 2;
            const int  to     = static_cast<int>(kSize) * 3 / 2;
            const bool inside = row >= from && row <= to && col >= from && col <= to;
            if (!inside) {
                ringed.heights[static_cast<size_t>(row) * side + col] = -1000;
            }
        }
    }
    CHECK(CM::Validate(ringed).empty());

    // Exactly the water level counts as water; one step above is land.
    CM::Map level = ValidMap();
    level.heights.assign(level.heights.size(), CM::kWaterLevel);
    CHECK(HasText(CM::Validate(level), "above the water", true));
    level.heights.assign(level.heights.size(), static_cast<int16_t>(CM::kWaterLevel + 1));
    CHECK(!CM::HasError(CM::Validate(level)));
}

UNIT_TEST(ext_custom_maps_validate_paths) {
    CM::Map none = ValidMap();
    none.paths.clear();
    std::vector<CM::Issue> issues = CM::Validate(none);
    CHECK(HasText(issues, "no water path", false));
    CHECK(!CM::HasError(issues));

    // Path 0 must start outside the play area (|x| or |y| of at least half the map size), cross it and
    // end outside: the boats' route builder. Anything else is a warning, not an error.
    CM::Map inland = ValidMap();
    for (CM::PathPoint& pt : inland.paths[0].points) {
        pt.x = pt.x * 0.25f;
        pt.y = 0.0f;
    }
    issues = CM::Validate(inland);
    CHECK(HasText(issues, "start outside the play area", false));
    CHECK(!CM::HasError(issues));

    CM::Map deadEnd = ValidMap();
    deadEnd.paths[0].points.resize(4);   // starts outside, ends inside
    CHECK(HasText(CM::Validate(deadEnd), "start outside the play area", false));

    CM::Map startsInside = ValidMap();
    startsInside.paths[0].points.erase(startsInside.paths[0].points.begin(),
                                       startsInside.paths[0].points.begin() + 3);   // starts inside
    CHECK(HasText(CM::Validate(startsInside), "start outside the play area", false));

    CM::Map outsideOnly = ValidMap();
    for (CM::PathPoint& pt : outsideOnly.paths[0].points) {
        pt.y = 60.0f;   // never inside the square
    }
    CHECK(HasText(CM::Validate(outsideOnly), "start outside the play area", false));

    // North to south works as well as west to east; only path 0 is looked at.
    CM::Map vertical = ValidMap();
    for (CM::PathPoint& pt : vertical.paths[0].points) {
        std::swap(pt.x, pt.y);
    }
    CHECK(CM::Validate(vertical).empty());
    CM::Path channel;
    CM::PathPoint a;
    a.halfWidth = 1.5f;
    CM::PathPoint b = a;
    b.x = 5.0f;
    channel.points.push_back(a);
    channel.points.push_back(b);
    vertical.paths.push_back(channel);   // a short inland channel as the second path
    CHECK(CM::Validate(vertical).empty());

    CM::Map outside = ValidMap();
    outside.paths[0].points[2].x = static_cast<float>(kSize) + 1.0f;
    CHECK(HasText(CM::Validate(outside), "outside the map", true));

    CM::Map notANumber = ValidMap();
    notANumber.paths[0].points[1].y = std::numeric_limits<float>::quiet_NaN();
    CHECK(HasText(CM::Validate(notANumber), "outside the map", true));

    CM::Map infinite = ValidMap();
    infinite.paths[0].points[1].halfWidth = std::numeric_limits<float>::infinity();
    CHECK(CM::HasError(CM::Validate(infinite)));

    CM::Map thin = ValidMap();
    thin.paths[0].points[0].halfWidth = CM::kMinHalfWidth - 0.01f;
    CHECK(HasText(CM::Validate(thin), "width", true));
    thin.paths[0].points[0].halfWidth = CM::kMinHalfWidth;
    CHECK(!CM::HasError(CM::Validate(thin)));

    CM::Map wide = ValidMap();
    wide.paths[0].points[0].halfWidth = CM::kMaxHalfWidth + 0.01f;
    CHECK(HasText(CM::Validate(wide), "width", true));
}

UNIT_TEST(ext_custom_maps_validate_resources) {
    CM::Map outside = FullMap();
    outside.resources[0].cellX = static_cast<uint16_t>(kSize);
    CHECK(HasText(CM::Validate(outside), "outside the play area", true));

    const char* bad[] = {"", "Template/Rock", "Template/../Rock.rsc", "Template/Rock.png", "Tem\nplate.rsc",
                         "Template/Rock.rscx", ".rsc"};
    for (const char* name : bad) {
        CM::Map map = FullMap();
        map.resources[0].templatePath = name;
        CHECK(HasText(CM::Validate(map), "template", true));
    }
    const char* good[] = {"Template/Rock.rsc", "Template\\Rock.rsc", "A.RSC", "Models/Natural Tree-2.rsc"};
    for (const char* name : good) {
        CM::Map map = FullMap();
        map.resources[0].templatePath = name;
        CHECK(!CM::HasError(CM::Validate(map)));
    }

    // Without the flag the list is not looked at.
    CM::Map absent = FullMap();
    absent.resources[0].templatePath = "../../etc/passwd";
    absent.hasResources = false;
    CHECK(!CM::HasError(CM::Validate(absent)));
}

UNIT_TEST(ext_custom_maps_validate_start) {
    CM::Map outside = FullMap();
    outside.startX = static_cast<uint16_t>(kSize);
    CHECK(HasText(CM::Validate(outside), "start cell lies outside", true));

    CM::Map wet = FullMap();
    const int side  = static_cast<int>(CM::GridVertices(kSize));
    const int quart = static_cast<int>(kSize) / 2;
    wet.heights[static_cast<size_t>(wet.startY + quart) * side + (wet.startX + quart)] = CM::kWaterLevel;
    CHECK(HasText(CM::Validate(wet), "under water", true));
    wet.heights[static_cast<size_t>(wet.startY + quart) * side + (wet.startX + quart)] = CM::kWaterLevel + 1;
    CHECK(!CM::HasError(CM::Validate(wet)));

    // A neighbour in the water does not matter; the start is its own vertex.
    CM::Map neighbour = FullMap();
    const size_t row    = static_cast<size_t>(neighbour.startY + quart);
    const size_t beside = row * side + (neighbour.startX + quart + 1);
    neighbour.heights[beside] = -500;
    CHECK(!CM::HasError(CM::Validate(neighbour)));

    CM::Map absent = FullMap();
    absent.hasStart = false;
    absent.startX   = 9999;
    CHECK(!CM::HasError(CM::Validate(absent)));
}

UNIT_TEST(ext_custom_maps_status_text_names_every_status) {
    for (int i = 0; i <= static_cast<int>(CM::Status::OutOfMemory); ++i) {
        const char* text = CM::StatusText(static_cast<CM::Status>(i));
        CHECK(text != nullptr);
        CHECK(std::strlen(text) > 1);
        CHECK(std::strcmp(text, "unknown") != 0);
    }
}

namespace {

// A map whose heights are land except for the vertices `wet` names (world cells, the centre at 0, 0).
CM::Map WetMap(uint32_t mapSize, const std::vector<std::pair<int, int>>& wet) {
    CM::Map map;
    map.mapSize = mapSize;
    map.heights.assign(CM::HeightCount(mapSize), 256);
    const int half = static_cast<int>(mapSize);
    const int side = 2 * half + 1;
    for (const std::pair<int, int>& v : wet) {
        map.heights[static_cast<size_t>(v.second + half) * side + (v.first + half)] = -300;
    }
    return map;
}

bool InBox(const CM::Feature& f, int x, int y) {
    const float r = f.radius + static_cast<float>(CM::kRadMargin);
    return static_cast<float>(x) >= f.x - r && static_cast<float>(x) <= f.x + r &&
           static_cast<float>(y) >= f.y - r && static_cast<float>(y) <= f.y + r;
}

}  // namespace

UNIT_TEST(ext_custom_maps_height_of_is_the_games_unit) {
    CHECK_EQ(CM::HeightOf(256), 1.0f);
    CHECK_EQ(CM::HeightOf(-192), CM::kUnderwater);
    CHECK_EQ(CM::HeightOf(0), 0.0f);
    CHECK_EQ(CM::HeightOf(std::numeric_limits<int16_t>::min()), -128.0f);
    CHECK_EQ(CM::kTileSide, 200);
}

UNIT_TEST(ext_custom_maps_a_dry_map_needs_no_water_features) {
    CHECK(CM::CoverageFeatures(WetMap(256, {})).empty());
    CM::Map wrongSize = WetMap(64, {});
    wrongSize.heights.pop_back();
    CHECK(CM::CoverageFeatures(wrongSize).empty());
    CHECK(CM::CoverageFeatures(CM::Map()).empty());
}

UNIT_TEST(ext_custom_maps_the_underwater_threshold_is_the_builders) {
    CM::Map map = WetMap(64, {});
    map.heights[0] = CM::kUnderwaterRaw + 1;   // just above -0.75: land to the builder
    CHECK(CM::CoverageFeatures(map).empty());
    map.heights[0] = CM::kUnderwaterRaw;       // -0.75: water
    CHECK_EQ(CM::CoverageFeatures(map).size(), static_cast<size_t>(1));
}

UNIT_TEST(ext_custom_maps_one_tile_covers_the_smallest_map) {
    const std::vector<CM::Feature> features = CM::CoverageFeatures(WetMap(64, {{0, 0}}));
    CHECK_EQ(features.size(), static_cast<size_t>(1));
    CHECK_EQ(features[0].x, 36.0f);   // the tile starts at -64 and is 200 wide
    CHECK_EQ(features[0].y, 36.0f);
    CHECK_EQ(features[0].radius, 98.0f);
}

UNIT_TEST(ext_custom_maps_tiles_follow_the_grid_in_row_major_order) {
    // Size 256: the grid runs -256 .. 256, tiles start at -256, -56 and 144 on each axis.
    const std::vector<CM::Feature> two = CM::CoverageFeatures(WetMap(256, {{-250, -250}, {150, 150}}));
    CHECK_EQ(two.size(), static_cast<size_t>(2));
    CHECK_EQ(two[0].x, -156.0f);
    CHECK_EQ(two[0].y, -156.0f);
    CHECK_EQ(two[1].x, 244.0f);
    CHECK_EQ(two[1].y, 244.0f);

    const std::vector<CM::Feature> row =
        CM::CoverageFeatures(WetMap(256, {{150, -250}, {-250, 150}, {-250, -250}}));
    CHECK_EQ(row.size(), static_cast<size_t>(3));
    // Rows first, then columns: (-156, -156), (244, -156), (-156, 244).
    CHECK_EQ(row[0].y, -156.0f);
    CHECK_EQ(row[0].x, -156.0f);
    CHECK_EQ(row[1].y, -156.0f);
    CHECK_EQ(row[1].x, 244.0f);
    CHECK_EQ(row[2].y, 244.0f);
    CHECK_EQ(row[2].x, -156.0f);
}

UNIT_TEST(ext_custom_maps_a_vertex_on_a_tile_edge_wets_both_tiles) {
    // World x = -56 is the corner between the cells of the first and second tile of size 256.
    const std::vector<CM::Feature> features = CM::CoverageFeatures(WetMap(256, {{-56, -250}}));
    CHECK_EQ(features.size(), static_cast<size_t>(2));
    CHECK_EQ(features[0].x, -156.0f);
    CHECK_EQ(features[1].x, 44.0f);
    // One step inside the first tile touches only it.
    CHECK_EQ(CM::CoverageFeatures(WetMap(256, {{-57, -250}})).size(), static_cast<size_t>(1));
}

UNIT_TEST(ext_custom_maps_every_underwater_vertex_is_covered_and_no_cell_twice) {
    for (uint32_t mapSize : {64u, 256u, 384u}) {
        // Scattered wet vertices from a fixed generator, plus the four corners of the grid.
        std::vector<std::pair<int, int>> wet;
        uint32_t state = 12345u + mapSize;
        const int half = static_cast<int>(mapSize);
        for (int i = 0; i < 40; ++i) {
            state = state * 1664525u + 1013904223u;
            const int x = static_cast<int>((state >> 8) % static_cast<uint32_t>(2 * half + 1)) - half;
            state = state * 1664525u + 1013904223u;
            const int y = static_cast<int>((state >> 8) % static_cast<uint32_t>(2 * half + 1)) - half;
            wet.push_back({x, y});
        }
        wet.push_back({-half, -half});
        wet.push_back({half, half});
        wet.push_back({-half, half});
        wet.push_back({half, -half});
        const CM::Map map = WetMap(mapSize, wet);
        const std::vector<CM::Feature> features = CM::CoverageFeatures(map);
        CHECK(!features.empty());
        for (const std::pair<int, int>& v : wet) {
            bool covered = false;
            for (const CM::Feature& f : features) {
                covered = covered || InBox(f, v.first, v.second);
            }
            CHECK(covered);
        }
        // The builder meshes the cells col in [x - r2, x + r2): a cell is in one box only.
        const float side = static_cast<float>(CM::kTileSide);
        for (size_t a = 0; a < features.size(); ++a) {
            for (size_t b = a + 1; b < features.size(); ++b) {
                const bool sameX = std::fabs(features[a].x - features[b].x) < side;
                const bool sameY = std::fabs(features[a].y - features[b].y) < side;
                CHECK(!(sameX && sameY));
            }
        }
    }
}

UNIT_TEST(ext_custom_maps_a_feature_box_is_the_builders_tile) {
    // The builder scans (int)(x - (r + 2)) up to (int)(x + (r + 2)) exclusive: 200 cells.
    const std::vector<CM::Feature> features = CM::CoverageFeatures(WetMap(256, {{0, 0}}));
    CHECK_EQ(features.size(), static_cast<size_t>(1));
    const float r2 = features[0].radius + static_cast<float>(CM::kRadMargin);
    CHECK_EQ(static_cast<int>(features[0].x + r2) - static_cast<int>(features[0].x - r2), CM::kTileSide);
}

UNIT_TEST(ext_custom_maps_size_row_lookup) {
    const uint32_t rows[] = {256, 384, 512};
    CHECK_EQ(CM::SizeRowFor(256, rows, 3), 0);
    CHECK_EQ(CM::SizeRowFor(512, rows, 3), 2);
    CHECK_EQ(CM::SizeRowFor(128, rows, 3), -1);
    CHECK_EQ(CM::SizeRowFor(256, rows, 0), -1);
}

UNIT_TEST(ext_custom_maps_the_pending_map_is_held_and_cleared) {
    CM::ClearPending();
    CHECK(CM::Pending() == nullptr);
    CM::SetPending(FullMap());
    CHECK(CM::Pending() != nullptr);
    CHECK(SameMap(*CM::Pending(), FullMap()));
    CM::Map other = ValidMap();
    other.name = "second";
    CM::SetPending(std::move(other));   // a second pick replaces the first
    CHECK(CM::Pending() != nullptr);
    CHECK(CM::Pending()->name == "second");
    CM::ClearPending();
    CHECK(CM::Pending() == nullptr);
    CM::ClearPending();                 // clearing nothing is fine
}

UNIT_TEST(ext_custom_maps_activating_moves_the_pending_map_and_the_next_game_empties_it) {
    CM::ClearPending();
    CM::ClearActive();
    CHECK(CM::Active() == nullptr);
    CM::SetPending(FullMap());
    CM::ActivatePending();
    CHECK(CM::Pending() == nullptr);          // the pick is used up by the game it started
    CHECK(CM::Active() != nullptr);
    CHECK(SameMap(*CM::Active(), FullMap()));
    CM::ActivatePending();                    // nothing picked: the active map is emptied, not kept
    CHECK(CM::Active() == nullptr);
    CM::SetPending(FullMap());
    CM::ActivatePending();
    CM::ClearActive();
    CHECK(CM::Active() == nullptr);
    CM::ClearActive();                        // clearing nothing is fine
}

UNIT_TEST(ext_custom_maps_coverage_start_state) {
    CM::SetCoverageStart(-1);
    CHECK(!CM::IsCoverageFeature(0));
    CHECK(!CM::IsCoverageFeature(5));
    CM::SetCoverageStart(0);
    CHECK(CM::IsCoverageFeature(0));
    CHECK(CM::IsCoverageFeature(35));
    CHECK_EQ(CM::CoverageZ(), -CM::kBaseZ);
    CM::SetCoverageStart(3);
    CHECK(!CM::IsCoverageFeature(2));
    CHECK(CM::IsCoverageFeature(3));
    CHECK_EQ(CM::CoverageZ(), 3 - CM::kBaseZ);
    CM::SetCoverageStart(-1);
    CHECK(!CM::IsCoverageFeature(0));
}


// ---------------------------------------------------------------------------------------------------------------------
// The PNG reader. The files below were written by Python's zlib (row filters 0..4 in turn, the IDAT split in
// two chunks): 24 x 16 RGB with block types 2 (dynamic Huffman), 1 (fixed) and 0 (stored), 24 x 16 RGBA, a
// 4-bit palette with tRNS, 1-bit gray, 16-bit gray + alpha.
// ---------------------------------------------------------------------------------------------------------------------
namespace {

// rgb_dynamic 756 bytes
const uint8_t k_rgb_dynamic[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x18,
    0x00,0x00,0x00,0x10,0x08,0x02,0x00,0x00,0x00,0x83,0x46,0x28,0xc2,0x00,0x00,0x01,0x57,0x49,0x44,0x41,
    0x54,0x78,0xda,0xad,0xd2,0x4b,0x48,0x54,0x51,0x18,0x07,0xf0,0x73,0xbd,0xf3,0x9f,0xcf,0x39,0x93,0x73,
    0x1c,0xe7,0x8e,0x76,0x33,0xaf,0xc9,0xcd,0x1c,0xb3,0x89,0x9a,0xa2,0xe9,0x21,0xe4,0x44,0x4d,0x0f,0x49,
    0x8d,0xd2,0xc0,0x9e,0x94,0x2d,0xa6,0x42,0xaa,0x59,0x64,0x81,0xf6,0xa2,0x6c,0x61,0x65,0x94,0x06,0x5a,
    0x48,0x8f,0x4d,0x05,0xd1,0x83,0x40,0x37,0x15,0x14,0x54,0x1b,0x37,0xad,0x5b,0x14,0x91,0x44,0x11,0x5d,
    0x82,0xd9,0x7c,0x9b,0xe4,0x44,0x60,0xb4,0xe9,0x05,0x87,0x3f,0x7f,0xbe,0xc5,0xb7,0xf8,0xce,0x4f,0x08,
    0x21,0xc8,0x40,0xc8,0x27,0xa3,0x01,0x35,0xb5,0xd0,0x72,0x6d,0x7b,0xe6,0x74,0x67,0xee,0x5c,0x77,0x61,
    0x5d,0x6c,0xe9,0xba,0x78,0xba,0x2d,0xb1,0xf6,0x50,0x72,0x43,0x6f,0xed,0xa6,0x9b,0xa9,0x1d,0xcf,0xd2,
    0x99,0x37,0xf5,0xfb,0x44,0xd3,0xc1,0x8a,0xe6,0xae,0xe5,0xad,0x27,0x33,0xdb,0x7a,0x7a,0xdb,0x2e,0x8c,
    0x64,0x06,0xde,0xb5,0x5f,0x0d,0x67,0x0d,0xd3,0x00,0x19,0x4c,0xa6,0x22,0x30,0x91,0xa2,0x00,0x53,0x50,
    0x51,0x01,0x93,0x52,0x14,0x66,0x8a,0x28,0x8a,0x32,0x95,0x28,0xb2,0x99,0x4a,0x15,0x95,0x31,0x95,0x2b,
    0xaa,0x60,0x72,0x15,0x55,0x32,0x55,0x29,0xaa,0x66,0xaa,0x51,0x14,0xe7,0xbc,0xf1,0x45,0xa6,0xa1,0x4c,
    0xc3,0x33,0x0d,0xfe,0xa7,0xee,0xcf,0x93,0x30,0x18,0x79,0x02,0x26,0xc3,0x67,0x01,0x0c,0xbf,0x00,0x31,
    0xf2,0x2d,0x04,0x18,0x52,0x20,0xc8,0x98,0x64,0xa1,0x80,0x11,0x12,0x50,0x8c,0x5e,0x0b,0x61,0x46,0x91,
    0x40,0x84,0x61,0x59,0x88,0x32,0x8a,0x05,0x2e,0xb1,0x4f,0x6f,0x95,0x3a,0xc7,0xdf,0xeb,0x1f,0x05,0x7f,
    0x3a,0x17,0x0a,0x76,0xb1,0xdf,0x2a,0x0b,0xb8,0xd3,0x43,0x4e,0x4d,0x14,0x09,0x47,0x2c,0xaa,0x56,0x75,
    0x0b,0xe4,0xca,0x15,0xa9,0x86,0x96,0xda,0xe6,0x4c,0xfd,0xe6,0xae,0xf4,0xce,0xbe,0xf8,0xee,0x3b,0xb1,
    0xfd,0x2f,0x93,0x1d,0xef,0x13,0x47,0x90,0x39,0x35,0xa3,0xed,0xcc,0xea,0xec,0xc5,0xf6,0xf6,0xc1,0xbe,
    0xe6,0x6b,0x8f,0x9a,0x18,0x64,0xa2,0x5e,0x00,0x00,0x01,0x58,0x49,0x44,0x41,0x54,0x6e,0x7e,0xd8,0x76,
    0xb7,0xb8,0xd5,0x28,0xf2,0x3b,0xe3,0xbf,0x46,0xa6,0x47,0x00,0x91,0x47,0x01,0x50,0xd0,0xa3,0x02,0x90,
    0x4a,0x51,0x18,0x14,0xf1,0x28,0x0a,0x2a,0xf1,0xc8,0x06,0x95,0x7a,0x54,0x06,0x2a,0x4f,0x51,0x05,0xc8,
    0xf5,0xa8,0x12,0x54,0xe5,0x51,0x35,0xa8,0xc6,0xa3,0x38,0xbe,0x1f,0x9b,0xff,0x43,0x86,0x50,0x02,0x43,
    0x22,0x4f,0xc1,0xb4,0xe0,0xb3,0x01,0x07,0x7e,0x17,0x14,0x43,0xfe,0x43,0x04,0xde,0x42,0x8e,0x21,0xf8,
    0x09,0x93,0x3c,0x14,0xe4,0x10,0x62,0x28,0x81,0xde,0x38,0xc2,0x12,0x45,0x0a,0x11,0x0b,0x96,0x8d,0xa8,
    0x83,0x62,0x17,0x97,0x62,0xbe,0x1f,0x5b,0xe5,0x2f,0xf9,0x67,0x73,0xe1,0xc8,0x44,0x65,0x30,0x39,0x2b,
    0x14,0x9b,0x17,0x89,0x2f,0x9e,0x92,0x4e,0xb9,0xf5,0xab,0x66,0xd7,0x36,0x2e,0x49,0xb5,0xac,0x91,0x5b,
    0x36,0xa9,0xb6,0x76,0xb1,0xe7,0x04,0x0e,0x0c,0x38,0x87,0x1e,0xb8,0x47,0x47,0xad,0xee,0x8f,0xf6,0x59,
    0x79,0xbc,0xaf,0xa6,0xfb,0x72,0x43,0xc7,0xf5,0x6c,0xe7,0xad,0x81,0xfe,0x7b,0x4f,0x07,0x87,0x3f,0xf7,
    0x3c,0x2e,0x3d,0x6f,0x54,0x04,0x93,0x5a,0x36,0x6b,0xd9,0xae,0x96,0xcd,0x5a,0xf6,0x2b,0x2d,0x9b,0xb5,
    0x6c,0x57,0xcb,0x66,0x2d,0x3b,0xab,0x65,0xb3,0x96,0xed,0x6a,0xd9,0x3c,0x51,0xb6,0x6d,0x1a,0xae,0x76,
    0x31,0xf6,0xb3,0xda,0xdf,0x9e,0x47,0xc9,0xd5,0xb2,0xa5,0x96,0x9d,0xd3,0xb2,0xa5,0x96,0x9d,0xd3,0xb2,
    0xa5,0x96,0x9d,0xd3,0xb2,0xa5,0x96,0x9d,0xd6,0xb2,0xa5,0x96,0x9d,0xd3,0xb2,0xe5,0x44,0xd9,0xde,0x04,
    0xb5,0x7f,0xd3,0x45,0x5c,0xd5,0xcf,0x2f,0x4c,0x2f,0x89,0xa4,0x96,0x4d,0xae,0x5d,0x5d,0x9e,0x6c,0x8a,
    0x25,0x36,0xce,0x8b,0x6f,0xad,0x8b,0xed,0x6a,0x74,0xf7,0x6e,0x77,0xb2,0x59,0xfb,0xf0,0x69,0xeb,0xd8,
    0x90,0x3a,0x3d,0x2c,0xcf,0xbd,0x42,0xff,0x17,0x71,0x45,0x0d,0xde,0x98,0xd3,0x7f,0x7b,0xfd,0xf9,0xfb,
    0x1d,0x3d,0x23,0x43,0xdd,0x4f,0x5e,0x1c,0x7f,0xfe,0xb5,0x73,0x74,0x5a,0xc7,0x37,0x40,0x9b,0xfa,0xa3,
    0x16,0x75,0x7e,0x2e,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
// rgb_fixed 821 bytes
const uint8_t k_rgb_fixed[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x18,
    0x00,0x00,0x00,0x10,0x08,0x02,0x00,0x00,0x00,0x83,0x46,0x28,0xc2,0x00,0x00,0x01,0x78,0x49,0x44,0x41,
    0x54,0x78,0x01,0x63,0x60,0x60,0x60,0x60,0x67,0x64,0xe5,0x63,0xe1,0x12,0xe5,0xe4,0x97,0x11,0x10,0x51,
    0x96,0x94,0xd4,0x52,0x91,0x33,0x34,0x54,0xb6,0x70,0xd0,0xb0,0x0f,0xd4,0x75,0x4b,0x31,0xf2,0xad,0x34,
    0x0f,0x99,0x60,0x13,0xbd,0xd2,0x31,0xe9,0x88,0x5b,0xe6,0x43,0xef,0x02,0x86,0x80,0x72,0xc5,0xd0,0x3a,
    0x97,0xa8,0xd6,0xcc,0xf8,0x9e,0x09,0x29,0x93,0x77,0x66,0xce,0x7a,0x92,0xb7,0x50,0xb0,0x98,0x91,0x99,
    0x91,0x95,0x9d,0xf1,0x37,0x3b,0x33,0x3f,0x3b,0xeb,0x6f,0x76,0x76,0x7e,0x76,0xce,0xdf,0xec,0xdc,0xfc,
    0xec,0xbc,0xbf,0xd9,0xf9,0xf9,0xd9,0x05,0x7f,0xb3,0x0b,0xf3,0xb3,0x8b,0xfe,0x66,0x17,0xe7,0x67,0x97,
    0xfc,0xcd,0x2e,0xcd,0xcf,0x2e,0xfb,0x9b,0x5d,0x9e,0x9f,0x5d,0xf1,0x37,0xbb,0x32,0x3f,0xbb,0xea,0x6f,
    0x76,0x75,0x7e,0x76,0xcd,0xdf,0xec,0xda,0xfc,0xec,0xba,0xbf,0x99,0x80,0x06,0x31,0x33,0xf2,0x33,0x33,
    0x7e,0x64,0x66,0xfc,0x4d,0x11,0x9b,0x8d,0x89,0x8b,0x95,0xf1,0x37,0x2b,0x13,0x03,0x2b,0xf3,0x6f,0x56,
    0x16,0x11,0x56,0xd6,0xdf,0xac,0x6c,0x0c,0xac,0xec,0xbf,0x59,0x39,0x44,0x58,0x39,0x7f,0xb3,0x72,0x31,
    0xb0,0x72,0xff,0x66,0xe5,0x11,0x61,0xe5,0xfd,0xcd,0xca,0xc7,0xc0,0xca,0xff,0x9b,0x75,0x82,0x08,0xab,
    0xe0,0x6f,0x56,0x21,0x06,0x56,0xe1,0xdf,0xac,0x22,0x22,0xac,0xa2,0xbf,0x59,0xc5,0x18,0x58,0xa7,0xff,
    0x66,0x01,0x9b,0xca,0x05,0x26,0x81,0xe8,0x2e,0x8c,0xc1,0x4a,0xaa,0x38,0x03,0x3f,0xab,0xa4,0x18,0x9b,
    0x88,0x2c,0xa7,0xb2,0x0a,0x9f,0x9c,0xb6,0x28,0xab,0x91,0x1c,0x83,0xa5,0x26,0xbf,0x83,0x19,0x97,0xbb,
    0xab,0xa3,0x5f,0x98,0x4d,0x68,0xa6,0x77,0x4c,0x9d,0x5b,0xf2,0x54,0xdd,0xac,0x75,0x1a,0x85,0x27,0xcd,
    0x2b,0x9e,0x19,0xd5,0xb3,0x66,0xb6,0xa9,0xa5,0xf4,0x7a,0x16,0x4f,0xc9,0xcb,0x9b,0x3d,0x35,0x74,0xd1,
    0xbe,0x80,0x95,0x2f,0xe3,0x37,0x88,0x45,0x31,0x0a,0xb1,0xc9,0x01,0x63,0x8d,0x9d,0xf9,0x23,0x3b,0x2b,
    0x2b,0x3b,0xfb,0x47,0x76,0x4e,0x56,0x76,0xee,0x8f,0xec,0xbc,0xac,0xec,0xfc,0x8e,0xec,0x82,0xac,0xec,
    0xc2,0x1f,0xd9,0x45,0x59,0xd9,0xc5,0x3f,0xb2,0x4b,0xb2,0xb2,0x4b,0x7f,0x64,0x97,0x65,0x46,0x4c,0x54,
    0x34,0x00,0x00,0x01,0x78,0x49,0x44,0x41,0x54,0x65,0x97,0x77,0x64,0x57,0x64,0x65,0x57,0xfe,0xc8,0xae,
    0xca,0xca,0xae,0xfe,0x91,0x5d,0x93,0x95,0x5d,0xfb,0x23,0xbb,0x2e,0x2b,0x24,0xb0,0x7f,0x53,0x81,0xe4,
    0x63,0x15,0x67,0x65,0xe4,0x62,0x65,0xe2,0x67,0x65,0x16,0x61,0x65,0x91,0x64,0x65,0x95,0x63,0x65,0x53,
    0x66,0x65,0xd7,0x60,0xe5,0xd8,0xca,0xca,0xf9,0x88,0x95,0xeb,0x39,0x2b,0xf7,0x1b,0x56,0x9e,0x8f,0xac,
    0xbc,0xdf,0x58,0xf9,0x7e,0xb3,0xf2,0x33,0xb0,0x4e,0xd0,0x65,0x15,0xe4,0x62,0x15,0xe2,0x67,0x15,0x16,
    0x61,0x15,0x91,0x64,0x15,0x95,0x63,0x15,0x53,0x66,0x9d,0xae,0xc1,0x02,0x33,0x95,0x0b,0x83,0x24,0x4d,
    0x9c,0x41,0x8e,0xcb,0x48,0x95,0xdb,0x5c,0x87,0x4f,0xc3,0x58,0x58,0xd7,0x4a,0xca,0xcd,0x51,0xd9,0xdb,
    0x43,0xcf,0xc6,0xdf,0xda,0x31,0xcc,0x8b,0x2b,0x36,0x9a,0x3f,0x25,0x8f,0x21,0xbb,0x99,0xb5,0x68,0x96,
    0x5c,0xe5,0x66,0xe5,0x86,0x73,0x22,0xed,0xaf,0x25,0xfb,0xb8,0x9a,0xa6,0x6a,0xb7,0xcf,0xf1,0xab,0x58,
    0x5c,0x5c,0xbb,0x6a,0xd6,0xb4,0x8d,0x87,0x67,0xef,0x78,0xd7,0xb3,0x5f,0x7a,0x22,0xa3,0x22,0xb7,0x39,
    0x38,0x65,0xff,0x06,0xa7,0x6c,0x65,0x70,0xca,0xfe,0x0d,0x4e,0xd9,0x97,0xc1,0x29,0xfb,0x37,0x38,0x65,
    0x2b,0x83,0x53,0xf6,0x6f,0x70,0xca,0x2e,0x06,0xa7,0xec,0xdf,0xe0,0x94,0xad,0x0c,0x4e,0xd9,0xbf,0x91,
    0x53,0xb6,0x24,0x33,0xa3,0x32,0x38,0x5d,0x3c,0x47,0x4d,0xb5,0x44,0x8b,0x8b,0xb2,0x2b,0x83,0x53,0x36,
    0x17,0x38,0x65,0x7f,0x03,0xa7,0x6c,0x2e,0x70,0xca,0xfe,0x06,0x4e,0xd9,0x5c,0xe0,0x94,0xfd,0x0d,0x9c,
    0xb2,0xb9,0xc0,0x29,0xdb,0x0d,0x9c,0xb2,0xb9,0xc0,0x29,0xfb,0x1b,0x38,0x65,0x73,0x21,0xa7,0xec,0x8f,
    0x48,0xa9,0x96,0x1c,0x36,0x83,0x2e,0xbf,0xb7,0x89,0x80,0x9b,0xb5,0xb0,0xa3,0x93,0x84,0x8d,0xa7,0xbc,
    0x79,0x80,0x86,0x51,0xb8,0xb1,0x6e,0x9c,0x83,0x46,0xaa,0xbf,0x72,0x4e,0x82,0x5c,0x71,0xb1,0x64,0x55,
    0x87,0x48,0xe3,0x7c,0xfe,0x8e,0x1d,0x5c,0xfd,0x97,0x59,0xa7,0x7d,0x60,0x98,0xcb,0x3f,0x7b,0x89,0xc1,
    0xb4,0xd5,0xc1,0x13,0x37,0x55,0xf4,0xec,0x9c,0xdf,0x7e,0xe0,0x44,0xd3,0xf1,0xcf,0xb5,0xe7,0x14,0x2a,
    0x00,0x40,0x9b,0xfa,0xa3,0x00,0x4f,0x8a,0x38,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,
    0x82};
// rgb_stored 1248 bytes
const uint8_t k_rgb_stored[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x18,
    0x00,0x00,0x00,0x10,0x08,0x02,0x00,0x00,0x00,0x83,0x46,0x28,0xc2,0x00,0x00,0x02,0x4d,0x49,0x44,0x41,
    0x54,0x78,0x01,0x01,0x90,0x04,0x6f,0xfb,0x00,0x00,0x00,0x00,0x07,0x01,0x05,0x0e,0x04,0x0a,0x15,0x09,
    0x0f,0x1c,0x10,0x14,0x23,0x19,0x19,0x2a,0x24,0x1e,0x31,0x31,0x23,0x38,0x40,0x28,0x3f,0x51,0x2d,0x46,
    0x64,0x32,0x4d,0x79,0x37,0x54,0x90,0x3c,0x5b,0xa9,0x41,0x62,0xc4,0x46,0x69,0xe1,0x4b,0x70,0x00,0x50,
    0x77,0x21,0x55,0x7e,0x44,0x5a,0x85,0x69,0x5f,0x8c,0x90,0x64,0x93,0xb9,0x69,0x9a,0xe4,0x6e,0xa1,0x11,
    0x73,0x01,0x03,0x01,0x05,0x07,0x01,0xfb,0x07,0x03,0x0f,0x07,0x05,0xfb,0x07,0x07,0x0f,0x07,0x09,0xfb,
    0x07,0x0b,0x0f,0x07,0x0d,0xfb,0x07,0x0f,0x0f,0x07,0x11,0xfb,0x07,0x13,0x0f,0x07,0x15,0xfb,0x07,0x17,
    0x0f,0x07,0x19,0xfb,0x07,0x1b,0x0f,0x07,0x1d,0xfb,0x07,0x1f,0x0f,0x07,0x21,0xfb,0x07,0x23,0x0f,0x07,
    0x25,0xfb,0x07,0x27,0x0f,0x07,0x29,0xfb,0x07,0x2b,0x0f,0x07,0x2d,0xfb,0x02,0x03,0x01,0x05,0x03,0x01,
    0x0f,0x03,0x01,0xf1,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0x0f,0x03,0x01,0xf1,0x03,0x01,0xfb,0x03,
    0x01,0x05,0x03,0x01,0x0f,0x03,0x01,0xf1,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0x0f,0x03,0x01,0xf1,
    0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0x0f,0x03,0x01,0xf1,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,
    0x0f,0x03,0x01,0xf1,0x03,0x01,0xfb,0x03,0x06,0x02,0x0a,0x05,0x01,0xfb,0x05,0x02,0x00,0x05,0x03,0xfb,
    0x05,0x04,0x14,0x05,0x05,0xfb,0x05,0x06,0x00,0x05,0x07,0xfb,0x05,0x08,0x14,0x05,0x09,0xfb,0x05,0x0a,
    0x00,0x05,0x0b,0xfb,0x05,0x0c,0x14,0x05,0x0d,0xfb,0x05,0x0e,0x00,0x05,0x0f,0xfb,0x05,0x90,0x14,0x05,
    0x11,0xfb,0x05,0x12,0x00,0x05,0x13,0xfb,0x05,0x14,0x14,0x05,0x15,0xfb,0x05,0x16,0x00,0x05,0x97,0xfb,
    0x04,0x03,0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0xdd,0x03,0x01,0x05,0x03,
    0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0xdd,
    0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0x05,0x03,0x01,
    0x05,0x03,0x01,0xdd,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x05,0x00,0x0f,0x05,0x19,0x16,0x06,0x14,
    0x1d,0x09,0x23,0x24,0x0e,0x1e,0x2b,0x15,0x05,0x32,0x1e,0x00,0x39,0x29,0x0f,0x40,0x36,0x0a,0x47,0x45,
    0x41,0x4e,0x56,0x3c,0x55,0x69,0x4b,0x5c,0x7e,0x46,0x63,0x95,0x2d,0x6a,0xae,0x28,0x71,0xc9,0x37,0x78,
    0xe6,0x32,0x7f,0x05,0x69,0x86,0x26,0x64,0x8d,0x49,0x73,0x94,0x6e,0x6e,0x9b,0x95,0x55,0xa2,0xbe,0x50,
    0xa9,0xe9,0x5f,0xb0,0x16,0x5a,0x01,0x12,0x06,0x1e,0x07,0x01,0x05,0x07,0x03,0xf1,0x07,0x05,0x05,0x07,
    0x07,0xf1,0x07,0x09,0x05,0x07,0x0b,0xf1,0x07,0x0d,0x05,0x07,0x0f,0x41,0x07,0x11,0x05,0x07,0x13,0xf1,
    0x07,0x15,0x05,0x07,0x17,0xf1,0x07,0x19,0x05,0x07,0x1b,0xf1,0x07,0x1d,0x05,0x07,0x1f,0x41,0x07,0x21,
    0x05,0x07,0x23,0xf1,0x07,0x25,0x05,0x07,0x27,0xf1,0x07,0x29,0x05,0x07,0x2b,0xf1,0x07,0x2d,0x05,0x02,
    0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,
    0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,
    0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,
    0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0xc7,0xd3,0xfe,0xe3,0x00,0x00,0x02,0x4e,0x49,0x44,
    0x41,0x54,0x01,0xfb,0x03,0x0e,0x05,0x17,0x05,0x01,0x0a,0x05,0x02,0x0f,0x05,0x03,0x14,0x05,0x04,0x19,
    0x05,0x05,0x1e,0x05,0x06,0x23,0x05,0x07,0x28,0x05,0x08,0xb5,0x05,0x09,0xe2,0x05,0x0a,0xe7,0x05,0x0b,
    0xec,0x05,0x0c,0xf1,0x05,0x0d,0xf6,0x05,0x0e,0xfb,0x05,0x0f,0x00,0x05,0x90,0x2d,0x05,0x11,0x0a,0x05,
    0x12,0x0f,0x05,0x13,0x14,0x05,0x14,0x19,0x05,0x15,0x1e,0x05,0x16,0x23,0x05,0x97,0x28,0x04,0x03,0x01,
    0x05,0x03,0x01,0xfb,0x03,0x01,0x0a,0x03,0x01,0xfb,0x03,0x01,0x0a,0x03,0x01,0xfb,0x03,0x01,0x0a,0x03,
    0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x0a,0x03,0x01,0xfb,0x03,0x01,0x0a,0x03,0x01,0xfb,
    0x03,0x01,0x0a,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0xfb,0x03,0x01,0x0a,0x03,0x01,0xfb,0x03,0x01,
    0x0a,0x03,0x01,0xfb,0x03,0x01,0x0a,0x03,0x01,0xfb,0x00,0x1e,0x0a,0x32,0x25,0x0b,0x37,0x2c,0x0e,0x28,
    0x33,0x13,0x2d,0x3a,0x1a,0x46,0x41,0x23,0x4b,0x48,0x2e,0x3c,0x4f,0x3b,0x41,0x56,0x4a,0x0a,0x5d,0x5b,
    0x0f,0x64,0x6e,0x00,0x6b,0x83,0x05,0x72,0x9a,0x1e,0x79,0xb3,0x23,0x80,0xce,0x14,0x87,0xeb,0x19,0x8e,
    0x0a,0x82,0x95,0x2b,0x87,0x9c,0x4e,0x78,0xa3,0x73,0x7d,0xaa,0x9a,0x96,0xb1,0xc3,0x9b,0xb8,0xee,0x8c,
    0xbf,0x1b,0x91,0x01,0x21,0x0b,0x37,0x07,0x01,0xfb,0x07,0x03,0xfb,0x07,0x05,0xfb,0x07,0x07,0x23,0x07,
    0x09,0xfb,0x07,0x0b,0xfb,0x07,0x0d,0xfb,0x07,0x0f,0xd3,0x07,0x11,0xfb,0x07,0x13,0xfb,0x07,0x15,0xfb,
    0x07,0x17,0x23,0x07,0x19,0xfb,0x07,0x1b,0xfb,0x07,0x1d,0xfb,0x07,0x1f,0x73,0x07,0x21,0xfb,0x07,0x23,
    0xfb,0x07,0x25,0xfb,0x07,0x27,0x23,0x07,0x29,0xfb,0x07,0x2b,0xfb,0x07,0x2d,0xfb,0x02,0x03,0x01,0x05,
    0x03,0x01,0x0f,0x03,0x01,0x19,0x03,0x01,0x23,0x03,0x01,0xdd,0x03,0x01,0xe7,0x03,0x01,0xf1,0x03,0x01,
    0xfb,0x03,0x01,0x05,0x03,0x01,0x0f,0x03,0x01,0x19,0x03,0x01,0x23,0x03,0x01,0xdd,0x03,0x01,0xe7,0x03,
    0x01,0xf1,0x03,0x01,0xfb,0x03,0x01,0x05,0x03,0x01,0x0f,0x03,0x01,0x19,0x03,0x01,0x23,0x03,0x01,0xdd,
    0x03,0x01,0xe7,0x03,0x01,0xf1,0x03,0x01,0xfb,0x03,0x15,0x07,0x23,0x05,0x01,0xfb,0x05,0x02,0x0a,0x05,
    0x03,0xfb,0x05,0x04,0xf6,0x05,0x05,0xfb,0x05,0x06,0x0a,0x05,0x07,0xfb,0x05,0x08,0xf6,0x05,0x09,0xfb,
    0x05,0x0a,0x0a,0x05,0x0b,0xfb,0x05,0x0c,0xf6,0x05,0x0d,0xfb,0x05,0x0e,0x0a,0x05,0x0f,0xfb,0x05,0x90,
    0x46,0x05,0x11,0xfb,0x05,0x12,0x0a,0x05,0x13,0xfb,0x05,0x14,0xf6,0x05,0x15,0xfb,0x05,0x16,0x0a,0x05,
    0x97,0xfb,0x04,0x03,0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0xf1,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,
    0x0a,0x03,0x01,0xf1,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0xf1,0x03,0x01,0x05,0x03,
    0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0xf1,0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0xf1,
    0x03,0x01,0x05,0x03,0x01,0x05,0x03,0x01,0x0a,0x03,0x01,0xf1,0x03,0x01,0x05,0x00,0x2d,0x0f,0x4b,0x34,
    0x10,0x46,0x3b,0x13,0x41,0x42,0x18,0x3c,0x49,0x1f,0x37,0x50,0x28,0x32,0x57,0x33,0x2d,0x5e,0x40,0x28,
    0x65,0x4f,0x23,0x6c,0x60,0x1e,0x73,0x73,0x19,0x7a,0x88,0x14,0x81,0x9f,0x0f,0x88,0xb8,0x0a,0x8f,0xd3,
    0x05,0x96,0xf0,0x00,0x9d,0x0f,0x9b,0xa4,0x30,0x96,0xab,0x53,0x91,0xb2,0x78,0x8c,0xb9,0x9f,0x87,0xc0,
    0xc8,0x82,0xc7,0xf3,0x7d,0xce,0x20,0x78,0x40,0x9b,0xfa,0xa3,0xc3,0x4f,0xb0,0x22,0x00,0x00,0x00,0x00,
    0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
// rgba 886 bytes
const uint8_t k_rgba[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x18,
    0x00,0x00,0x00,0x10,0x08,0x06,0x00,0x00,0x00,0x0c,0x24,0xbf,0x95,0x00,0x00,0x01,0x98,0x49,0x44,0x41,
    0x54,0x78,0xda,0xb5,0x93,0x5b,0x48,0x14,0x51,0x1c,0xc6,0x67,0x76,0xf7,0xcc,0xb4,0x67,0x72,0x47,0xdd,
    0x8b,0x4d,0xe6,0xea,0x32,0x5a,0x6b,0xdb,0x46,0x6e,0xd1,0x56,0x2e,0xe4,0x46,0x6d,0x99,0x69,0x76,0x31,
    0x43,0x2d,0xc3,0x8c,0xda,0x2e,0x4b,0xb5,0x0f,0xad,0x81,0x76,0x57,0x03,0x2b,0xa5,0x34,0xd2,0x4a,0x2a,
    0x7b,0xb0,0xa0,0x7b,0xd0,0x05,0x52,0xe8,0x5e,0x10,0x45,0xd4,0x4b,0x05,0x15,0x91,0x44,0x11,0x6d,0xc1,
    0xf6,0x30,0x05,0x7d,0xe2,0x91,0x22,0xea,0xa5,0xcb,0xc0,0x9f,0xdf,0x7c,0x1c,0xfe,0x9c,0x87,0xef,0xfc,
    0x38,0x0e,0x9f,0xc8,0x13,0xc9,0x64,0xa0,0x36,0xab,0x51,0x76,0x0c,0x89,0xb7,0x8c,0x50,0x15,0xc5,0x3b,
    0x3c,0xdd,0x3e,0x29,0x2b,0x4b,0xcd,0x1f,0x97,0xe3,0x9c,0x3f,0x71,0x96,0x7b,0x71,0xa0,0xd2,0x13,0xca,
    0xaf,0xf2,0x56,0xcd,0x6d,0xf4,0x6d,0x2d,0xed,0xf4,0xef,0xaa,0xb8,0x16,0x68,0x0d,0xbe,0xc8,0x3b,0xba,
    0x9a,0x2b,0x3c,0xb5,0xce,0x51,0x74,0xb9,0x66,0x72,0xc9,0x8d,0x6d,0xc1,0xf2,0x07,0x0d,0x8d,0x95,0x4f,
    0x77,0x5f,0x0c,0xf6,0xb4,0xbe,0x0a,0x7d,0x3c,0x94,0x10,0xfe,0xca,0xeb,0x79,0xc2,0x8b,0xbc,0x26,0x89,
    0x7a,0x59,0x12,0x09,0x28,0x82,0x46,0x50,0x02,0xe3,0x40,0x19,0x4c,0x00,0xcd,0xa0,0x15,0x4c,0x02,0x15,
    0x30,0x19,0x4c,0x01,0x53,0x41,0x07,0xa8,0x82,0x19,0xe0,0x30,0x30,0x13,0x74,0x81,0x6e,0x4d,0xd2,0xf5,
    0x5e,0xa0,0xe7,0x65,0x4c,0x14,0xa3,0xf1,0xff,0x3c,0x0b,0x3a,0xaa,0x23,0xbc,0x26,0x10,0x1d,0x27,0x10,
    0x3d,0x68,0xb0,0x08,0x84,0x80,0x02,0xb2,0x08,0x0e,0x40,0x36,0x82,0x14,0x59,0x02,0x07,0x22,0xc7,0x81,
    0x26,0x64,0x19,0x6c,0x44,0x4e,0x00,0x13,0x91,0xcd,0xa0,0x05,0xd9,0x0a,0xda,0x90,0xf7,0x6a,0x82,0xa1,
    0xef,0x46,0xca,0x6e,0xee,0x9d,0x67,0x3f,0xfc,0xf7,0xcf,0x9f,0x9f,0x73,0x32,0x51,0x88,0x4d,0xb0,0xc4,
    0xa7,0x18,0xd5,0xe4,0x74,0x93,0x7d,0xa8,0xcb,0x4a,0xb2,0x3c,0x76,0xce,0x37,0x3e,0x53,0x9e,0x9a,0x33,
    0x96,0xce,0x9e,0x3a,0xc5,0xbf,0xa0,0x60,0x9e,0x6f,0x59,0x51,0x30,0x2f,0x5c,0x56,0x13,0xa8,0x59,0xdc,
    0xec,0xde,0xbe,0xfc,0x84,0x73,0xcf,0x9a,0xdb,0xde,0xf6,0xc8,0x6b,0xcf,0xb1,0x0d,0x24,0x78,0xbe,0x76,
    0x68,0x65,0xf7,0x8e,0xdc,0xf0,0xdd,0x3d,0xa1,0xd0,0xe3,0xb6,0xe6,0xa2,0x17,0x87,0xaf,0x14,0xbe,0xeb,
    0x7c,0x53,0xfe,0xf9,0x94,0xad,0x44,0xc7,0x27,0x2a,0x24,0xe8,0x0d,0x00,0x00,0x01,0x99,0x49,0x44,0x41,
    0x54,0x0a,0x76,0xa1,0xf7,0x15,0x89,0xfa,0x28,0x4a,0x06,0x45,0xd0,0x08,0x4a,0x60,0x1c,0x28,0xfb,0x51,
    0x32,0x68,0x46,0xb6,0x82,0x49,0xa0,0x02,0x26,0x83,0x29,0x60,0x2a,0xce,0x1d,0xa0,0x8a,0x9c,0x01,0x0e,
    0x03,0x33,0x41,0x17,0xe8,0x26,0xfd,0x25,0xf7,0x97,0xf3,0x1f,0x68,0x22,0x49,0x84,0xf0,0x14,0x25,0xcb,
    0x28,0x19,0x05,0x19,0x14,0x94,0x6c,0x47,0xc9,0x2a,0x4a,0x76,0xa2,0xe4,0xf3,0x28,0xf9,0x25,0x4a,0xee,
    0x41,0xc9,0xef,0x50,0x72,0x14,0x25,0xc7,0x50,0x32,0x8a,0x94,0x51,0x64,0xa3,0x1b,0x25,0x63,0x3f,0x11,
    0xfb,0x66,0xec,0x5b,0xb0,0x6f,0xc5,0xbe,0x0d,0xfb,0x7b,0x9d,0xb5,0x86,0xef,0x37,0xd2,0xdf,0xf0,0xef,
    0xce,0x39,0x3b,0xf5,0xd0,0x0c,0xc9,0x6b,0x1d,0x61,0x72,0xa6,0x8d,0x36,0xbb,0x5d,0x13,0x06,0x07,0xc6,
    0xfa,0xd5,0x3c,0xff,0xb4,0x91,0xbe,0x19,0x33,0xb3,0xfd,0xc5,0xf3,0xa6,0xd3,0x8a,0x05,0xa5,0xf2,0xaa,
    0xca,0x10,0x17,0x59,0xb1,0x85,0x6c,0x59,0xdb,0x6a,0xdf,0x59,0x75,0x56,0xdd,0xb7,0xf1,0x9e,0xa5,0xa3,
    0xee,0xad,0x72,0x72,0x27,0xdd,0x7c,0xa9,0xd9,0x55,0x77,0x7d,0x7f,0x41,0xe4,0xfe,0x91,0x70,0xf5,0x93,
    0x63,0xad,0x2d,0xaf,0x4f,0x5f,0x6d,0x8b,0x5e,0x78,0xdf,0xf0,0xa5,0x2b,0xb9,0x49,0xe4,0x1d,0x92,0x57,
    0xea,0x33,0x59,0x63,0x26,0xab,0xcc,0x64,0x8d,0x99,0xfc,0x90,0x99,0xac,0x31,0x93,0x55,0x66,0xb2,0xc6,
    0x4c,0x0e,0x33,0x93,0x35,0x66,0xb2,0xca,0x4c,0xd6,0x7e,0x36,0x59,0xc1,0xa8,0xec,0x1d,0xf7,0xfc,0xc2,
    0xcc,0x3f,0x3c,0xb7,0x8a,0xaa,0xd8,0x67,0x32,0x65,0x26,0xc7,0x98,0xc9,0x94,0x99,0x1c,0x63,0x26,0x53,
    0x66,0x72,0x8c,0x99,0x4c,0x99,0xc9,0x01,0x66,0x32,0x65,0x26,0xc7,0x98,0xc9,0xb4,0xd7,0xe4,0xda,0x1f,
    0x4c,0x8e,0xfe,0x64,0xe6,0x3f,0xc9,0x12,0xe7,0x96,0xf3,0xe4,0x31,0xf1,0x81,0xc1,0xd9,0x66,0x7f,0xc6,
    0xa4,0x41,0xbe,0x51,0xb9,0xa9,0xde,0xec,0x42,0xa7,0x27,0x50,0x3c,0xda,0x3d,0x6b,0x61,0x8e,0xb3,0x6c,
    0xc9,0x4c,0x75,0xe9,0xca,0x45,0xf6,0xb5,0xe1,0xb0,0x52,0xbd,0xbe,0xde,0x52,0xbf,0xa9,0x5d,0xde,0x5d,
    0x7f,0x81,0x1e,0xdc,0xf5,0x90,0x74,0xb6,0x7c,0xe0,0xce,0x1d,0x90,0xdb,0xba,0x3a,0x46,0xb5,0xdc,0x39,
    0x3e,0xa7,0xe9,0xd1,0x99,0x48,0xc3,0xf3,0x8b,0xed,0x75,0x6f,0xbb,0x6f,0x6d,0x8e,0xdd,0xfc,0x54,0xcd,
    0xdf,0x4b,0x8b,0x0c,0xfc,0x06,0xd0,0xf2,0x2f,0x41,0x66,0xe9,0xdf,0xeb,0x00,0x00,0x00,0x00,0x49,0x45,
    0x4e,0x44,0xae,0x42,0x60,0x82};
// pal4 123 bytes
const uint8_t k_pal4[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x05,
    0x00,0x00,0x00,0x02,0x04,0x03,0x00,0x00,0x00,0x62,0x44,0x0b,0x6e,0x00,0x00,0x00,0x0c,0x50,0x4c,0x54,
    0x45,0xff,0x00,0x00,0x00,0xff,0x00,0x00,0x00,0xff,0x0a,0x14,0x1e,0x22,0x88,0x29,0x04,0x00,0x00,0x00,
    0x02,0x74,0x52,0x4e,0x53,0xff,0x80,0x08,0x0f,0xb3,0x6a,0x00,0x00,0x00,0x08,0x49,0x44,0x41,0x54,0x78,
    0xda,0x63,0x60,0x54,0x16,0x60,0x34,0x91,0x91,0x59,0x35,0x00,0x00,0x00,0x08,0x49,0x44,0x41,0x54,0xba,
    0x27,0x00,0x00,0x03,0x97,0x01,0x56,0xd2,0x9e,0x54,0xe1,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,
    0x42,0x60,0x82};
// gray1 83 bytes
const uint8_t k_gray1[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x0a,
    0x00,0x00,0x00,0x02,0x01,0x00,0x00,0x00,0x00,0x49,0x1a,0x70,0x7d,0x00,0x00,0x00,0x07,0x49,0x44,0x41,
    0x54,0x78,0xda,0x63,0xd8,0x74,0x80,0x51,0x6c,0x11,0xdf,0x6f,0x00,0x00,0x00,0x07,0x49,0x44,0x41,0x54,
    0x5f,0x10,0x00,0x06,0xf2,0x01,0xb4,0xd9,0x21,0xfe,0x57,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,
    0x42,0x60,0x82};
// ga16 90 bytes
const uint8_t k_ga16[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x03,
    0x00,0x00,0x00,0x01,0x10,0x04,0x00,0x00,0x00,0xe1,0x79,0x00,0x7c,0x00,0x00,0x00,0x0a,0x49,0x44,0x41,
    0x54,0x78,0xda,0x63,0x68,0x60,0xfc,0xcf,0x24,0xc0,0xac,0x22,0x8b,0xee,0x84,0x00,0x00,0x00,0x0b,0x49,
    0x44,0x41,0x54,0xc0,0xf2,0x8f,0x95,0x91,0x0d,0x00,0x15,0x98,0x02,0xc4,0xc8,0x6c,0x7f,0x05,0x00,0x00,
    0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};

struct Rgb { int r, g, b; };
Rgb RgbAt(int x, int y) { return Rgb{(x * 7 + y * 3) & 255, (x * x + y) & 255, ((x ^ y) * 5) & 255}; }

CM::Image Decode(const uint8_t* data, size_t size, bool& ok) {
    CM::Image image;
    ok = CM::DecodePng(data, size, image);
    return image;
}

bool PixelIs(const CM::Image& image, uint32_t x, uint32_t y, int r, int g, int b, int a) {
    const uint8_t* p = &image.bgra[(static_cast<size_t>(y) * image.width + x) * 4];
    return p[0] == b && p[1] == g && p[2] == r && p[3] == a;
}

void CheckRgbImage(const uint8_t* data, size_t size, bool alpha) {
    bool ok = false;
    const CM::Image image = Decode(data, size, ok);
    CHECK(ok);
    CHECK_EQ(image.width, 24u);
    CHECK_EQ(image.height, 16u);
    CHECK_EQ(image.bgra.size(), 24u * 16u * 4u);
    bool all = true;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 24; ++x) {
            const Rgb c = RgbAt(x, y);
            all = all && PixelIs(image, x, y, c.r, c.g, c.b, alpha ? ((x * 11 + y) & 255) : 255);
        }
    }
    CHECK(all);
}

}  // namespace

UNIT_TEST(ext_custom_maps_png_decodes_every_block_kind) {
    CheckRgbImage(k_rgb_dynamic, sizeof(k_rgb_dynamic), false);   // block type 2
    CheckRgbImage(k_rgb_fixed, sizeof(k_rgb_fixed), false);       // block type 1
    CheckRgbImage(k_rgb_stored, sizeof(k_rgb_stored), false);     // block type 0
}

UNIT_TEST(ext_custom_maps_png_decodes_rgba) {
    CheckRgbImage(k_rgba, sizeof(k_rgba), true);
}

UNIT_TEST(ext_custom_maps_png_decodes_palette_gray_and_sixteen_bit) {
    bool ok = false;
    // indices 0 1 2 3 1 / 3 2 1 0 2; tRNS gives entries 0, 1 alpha 255, 128
    CM::Image image = Decode(k_pal4, sizeof(k_pal4), ok);
    CHECK(ok && image.width == 5 && image.height == 2);
    CHECK(PixelIs(image, 0, 0, 255, 0, 0, 255));
    CHECK(PixelIs(image, 1, 0, 0, 255, 0, 128));
    CHECK(PixelIs(image, 2, 0, 0, 0, 255, 255));
    CHECK(PixelIs(image, 3, 0, 10, 20, 30, 255));
    CHECK(PixelIs(image, 4, 0, 0, 255, 0, 128));
    CHECK(PixelIs(image, 0, 1, 10, 20, 30, 255));
    CHECK(PixelIs(image, 4, 1, 0, 0, 255, 255));

    image = Decode(k_gray1, sizeof(k_gray1), ok);           // 1 0 1 1 0 0 1 0 1 1 / 0 0 1 0 1 1 1 1 0 1
    CHECK(ok && image.width == 10 && image.height == 2);
    const int row0[10] = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1};
    const int row1[10] = {0, 0, 1, 0, 1, 1, 1, 1, 0, 1};
    bool all = true;
    for (uint32_t x = 0; x < 10; ++x) {
        all = all && PixelIs(image, x, 0, row0[x] * 255, row0[x] * 255, row0[x] * 255, 255);
        all = all && PixelIs(image, x, 1, row1[x] * 255, row1[x] * 255, row1[x] * 255, 255);
    }
    CHECK(all);

    image = Decode(k_ga16, sizeof(k_ga16), ok);             // the high byte of each 16-bit sample
    CHECK(ok && image.width == 3 && image.height == 1);
    CHECK(PixelIs(image, 0, 0, 0x80, 0x80, 0x80, 0xff));
    CHECK(PixelIs(image, 1, 0, 0x10, 0x10, 0x10, 0x20));
    CHECK(PixelIs(image, 2, 0, 0xfe, 0xfe, 0xfe, 0x01));
}

UNIT_TEST(ext_custom_maps_png_refuses_damaged_and_unsupported_files) {
    CM::Image image;
    CHECK(!CM::DecodePng(nullptr, 0, image));
    // shorter than the signature
    CHECK(!CM::DecodePng(k_rgb_dynamic, 7, image));
    const std::vector<uint8_t> png(k_rgb_dynamic, k_rgb_dynamic + sizeof(k_rgb_dynamic));
    // IHDR starts at 8 (length, "IHDR") so its data starts at 16: width 16..19, height 20..23, depth 24, type
    // 25, then three bytes.
    const size_t ihdr = 16;

    std::vector<uint8_t> bad = png;
    bad[1] = 'X';                                                                    // signature
    CHECK(!CM::DecodePng(bad.data(), bad.size(), image) && image.bgra.empty());

    // cut inside a chunk / before IEND
    for (size_t cut : {size_t(20), size_t(40), png.size() / 2, png.size() - 13}) {
        bad.assign(png.begin(), png.begin() + static_cast<std::ptrdiff_t>(cut));
        CHECK(!CM::DecodePng(bad.data(), bad.size(), image));
    }

    bad = png;
    // interlace: Adam7 is not read
    bad[ihdr + 12] = 1;
    CHECK(!CM::DecodePng(bad.data(), bad.size(), image));
    bad = png;
    // colour type 7 does not exist
    bad[ihdr + 9] = 7;
    CHECK(!CM::DecodePng(bad.data(), bad.size(), image));
    bad = png;
    // 3-bit RGB does not exist
    bad[ihdr + 8] = 3;
    CHECK(!CM::DecodePng(bad.data(), bad.size(), image));
    bad = png;
    bad[ihdr + 0] = bad[ihdr + 1] = bad[ihdr + 2] = bad[ihdr + 3] = 0;               // width 0
    CHECK(!CM::DecodePng(bad.data(), bad.size(), image));
    bad = png;
    // width 1048576: over the side cap
    bad[ihdr + 0] = 0; bad[ihdr + 1] = 0x10; bad[ihdr + 2] = 0; bad[ihdr + 3] = 0;
    CHECK(!CM::DecodePng(bad.data(), bad.size(), image));

    // A flipped byte inside the compressed data: some flips are refused, none reads past the data or crashes.
    size_t flipped = 0, refused = 0;
    for (size_t at = 8 + 25 + 8 + 4; at + 20 < png.size(); at += 7) {
        bad = png;
        bad[at] ^= 0x5a;
        ++flipped;
        if (!CM::DecodePng(bad.data(), bad.size(), image)) ++refused;
    }
    CHECK(flipped > 20);
    CHECK(refused > 0);
}

UNIT_TEST(ext_custom_maps_fit_cover_crops_to_the_frame_and_never_stretches) {
    // A 4 x 2 image: left half red, right half blue. Covering a 2 x 2 frame takes the middle 2 x 2: one red
    // column, one blue.
    CM::Image src;
    src.width = 4;
    src.height = 2;
    for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            // BGRA
            const uint8_t px[4] = {static_cast<uint8_t>(x < 2 ? 0 : 255), 0,
                                   static_cast<uint8_t>(x < 2 ? 255 : 0), 255};
            src.bgra.insert(src.bgra.end(), px, px + 4);
        }
    }
    CM::Image same = CM::FitCover(src, 4, 2);                       // the same size: unchanged
    CHECK(same.width == 4 && same.height == 2 && same.bgra == src.bgra);
    CM::Image square = CM::FitCover(src, 2, 2);
    CHECK(square.width == 2 && square.height == 2 && square.bgra.size() == 16u);
    CHECK(PixelIs(square, 0, 0, 255, 0, 0, 255));                    // red (R = 255)
    CHECK(PixelIs(square, 1, 0, 0, 0, 255, 255));                    // blue
    CHECK(PixelIs(square, 0, 1, 255, 0, 0, 255) && PixelIs(square, 1, 1, 0, 0, 255, 255));
    // scaled up: the corners keep their colors
    CM::Image up = CM::FitCover(src, 8, 4);
    CHECK(up.width == 8 && up.height == 4);
    CHECK(PixelIs(up, 0, 0, 255, 0, 0, 255) && PixelIs(up, 7, 3, 0, 0, 255, 255));
    CHECK(CM::FitCover(CM::Image(), 4, 4).bgra.empty());
    CHECK(CM::FitCover(src, 0, 4).bgra.empty());
    CM::Image broken = src;
    broken.bgra.pop_back();
    CHECK(CM::FitCover(broken, 4, 4).bgra.empty());
}

UNIT_TEST(ext_custom_maps_inflate_limits) {
    // zlib.compress(b"A" * 1000, 9)
    const uint8_t zbytes[] = {0x78, 0xda, 0x73, 0x74, 0x1c, 0x05, 0xa3, 0x60, 0x14, 0x0c, 0x77, 0x00, 0x00,
                              0x89, 0x0c, 0xfd, 0xe9};
    std::vector<uint8_t> out;
    CHECK(CM::ZlibInflate(zbytes, sizeof(zbytes), 1000, out));
    CHECK_EQ(out.size(), 1000u);
    CHECK(out.size() == 1000 && out[0] == 'A' && out[999] == 'A');
    CHECK(!CM::ZlibInflate(zbytes, sizeof(zbytes), 999, out));          // would be longer than allowed
    CHECK(out.empty());
    CHECK(!CM::ZlibInflate(zbytes, 5, 1000, out));                       // truncated
    const uint8_t badHeader[] = {0x79, 0xda, 0x03, 0x00};                // not a deflate method
    CHECK(!CM::ZlibInflate(badHeader, sizeof(badHeader), 10, out));
    // a preset dictionary is not supported
    const uint8_t dictHeader[] = {0x78, 0xbb, 0x03, 0x00};
    CHECK(!CM::ZlibInflate(dictHeader, sizeof(dictHeader), 10, out));
    CHECK(!CM::Inflate(nullptr, 0, 10, out));
}

// ---------------------------------------------------------------------------------------------------------------------
// The map list.
// ---------------------------------------------------------------------------------------------------------------------
namespace {

void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void AgeFile(const std::filesystem::path& path, int hours) {
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(path, now - std::chrono::hours(hours));
}

}  // namespace

// ReadMapFile tests the file's size before it reads the file: a file past kMaxMapFile is refused with the size
// reason, and a missing file, a file of another kind and a good file each get their own answer.
UNIT_TEST(ext_custom_maps_read_map_file_tests_the_size_first) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "openba_custom_maps_read_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    CM::Map good = ValidMap();
    good.name = "Readable";
    WriteBytes(dir / "good.bmap", CM::Write(good));
    WriteBytes(dir / "junk.bmap", std::vector<uint8_t>{'B', 'M', 'A', 'P', 1, 0});
    {
        std::ofstream big(dir / "big.bmap", std::ios::binary);
    }
    fs::resize_file(dir / "big.bmap", CM::kMaxMapFile + 1, ec);
    CHECK(!ec);

    CM::Map out;
    std::string why;
    CHECK(CM::ReadMapFile(dir / "good.bmap", out, why));
    CHECK(out.name == "Readable" && out.mapSize == kSize);
    CHECK(!CM::ReadMapFile(dir / "big.bmap", out, why));
    CHECK(why == "the file is too large");
    CHECK(!CM::ReadMapFile(dir / "junk.bmap", out, why));
    CHECK(why.find("not a valid map file") == 0);
    CHECK(!CM::ReadMapFile(dir / "missing.bmap", out, why));
    CHECK(why == "cannot open the file");
    fs::remove_all(dir, ec);
}

UNIT_TEST(ext_custom_maps_list_scan_newest_first) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "openba_custom_maps_list_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "sub");

    CM::Map older = ValidMap();
    older.name = "Older Valley";
    older.thumbnail.assign(k_gray1, k_gray1 + sizeof(k_gray1));
    CM::Map newer = ValidMap();
    newer.name = "Newer Valley";
    CM::Map unnamed = ValidMap();
    unnamed.name.clear();
    WriteBytes(dir / "older.bmap", CM::Write(older));
    WriteBytes(dir / "NEWER.BMAP", CM::Write(newer));              // the extension is matched without case
    WriteBytes(dir / "plain.bmap", CM::Write(unnamed));
    WriteBytes(dir / "broken.bmap", std::vector<uint8_t>{'B', 'M', 'A', 'P', 1, 0});
    WriteBytes(dir / "notes.txt", std::vector<uint8_t>{1, 2, 3});
    WriteBytes(dir / "sub" / "inner.bmap", CM::Write(newer));      // a subfolder is not listed
    AgeFile(dir / "NEWER.BMAP", 1);
    AgeFile(dir / "older.bmap", 30);
    AgeFile(dir / "plain.bmap", 60);
    AgeFile(dir / "broken.bmap", 90);

    const std::vector<CM::ListedMap> maps = CM::ScanMaps(dir);
    CHECK_EQ(maps.size(), 4u);
    if (maps.size() == 4) {
        CHECK(maps[0].valid && maps[0].name == "Newer Valley" && maps[0].mapSize == kSize);
        CHECK(maps[1].valid && maps[1].name == "Older Valley" && maps[1].thumbnail.size() == sizeof(k_gray1));
        CHECK(maps[2].valid && maps[2].name == "plain");           // no name in the file: the file's name
        CHECK(!maps[3].valid && maps[3].name == "broken" && !maps[3].problem.empty() &&
              maps[3].thumbnail.empty());
        CHECK(maps[0].modified > maps[1].modified && maps[1].modified > maps[2].modified);
    }
    CHECK(CM::ScanMaps(dir / "missing").empty());
    CHECK(CM::ScanMaps(fs::path()).empty());
    fs::remove_all(dir, ec);
}

UNIT_TEST(ext_custom_maps_list_sort_is_stable_for_equal_times) {
    std::vector<CM::ListedMap> maps(3);
    maps[0].name = "b"; maps[0].path = "b.bmap"; maps[0].modified = 100;
    maps[1].name = "a"; maps[1].path = "a.bmap"; maps[1].modified = 100;
    maps[2].name = "z"; maps[2].path = "z.bmap"; maps[2].modified = 200;
    CM::SortNewestFirst(maps);
    CHECK(maps[0].name == "z" && maps[1].name == "a" && maps[2].name == "b");
}

UNIT_TEST(ext_custom_maps_boat_route_exists) {
    // The world's polyline 0 as the route builder reads it: x, y pairs in cells, the map centre at 0, 0 (a
    // 64-cell map: the play area is x and y from -33 up to, not including, 32).
    const float good[] = {-80, 0, -40, 0, 0, 0, 40, 0, 80, 0};
    CHECK(CM::BoatRouteExists(good, 5, 2, 64));
    const float outside[] = {-80, 0, -70, 5, -60, 9};                  // never enters the play area
    CHECK(!CM::BoatRouteExists(outside, 3, 2, 64));
    const float one[] = {0, 0};
    CHECK(!CM::BoatRouteExists(one, 1, 2, 64));                        // a single point is no route
    // an empty polyline: a map with no water path
    CHECK(!CM::BoatRouteExists(good, 0, 2, 64));
    CHECK(!CM::BoatRouteExists(nullptr, 5, 2, 64));
    CHECK(!CM::BoatRouteExists(good, 5, 1, 64));                       // a stride that holds no y
    // two points four floats apart (padding between): stride 4
    const float strided[] = {-80, 0, 99, 99, 0, 0, 99, 99};
    CHECK(CM::BoatRouteExists(strided, 2, 4, 64));
    // x must be above -half - 1 = -33, so -33 is outside
    const float edge[] = {-80, 0, -33, 0};
    CHECK(!CM::BoatRouteExists(edge, 2, 2, 64));
    const float edgeIn[] = {-80, 0, -32.5f, 0};
    CHECK(CM::BoatRouteExists(edgeIn, 2, 2, 64));
    const float farEdge[] = {-80, 0, 32, 0};                           // x < half: 32 is outside
    CHECK(!CM::BoatRouteExists(farEdge, 2, 2, 64));
}

UNIT_TEST(ext_custom_maps_mods_are_checked_by_name_without_a_template_lookup) {
    CM::Map map = ValidMap();
    CM::ModRef a, b, c;
    a.name = "MegaMod 10"; a.templates = {"Template/A.rsc", "Template/B.rsc"};
    b.name = "Other Mod";  b.templates = {"Template/C.rsc"};
    c.name = "";           // a mod with no name and no templates still counts
    map.mods = {a, b, c};
    int asked = 0;
    // MegaMod is enabled, the others are not
    std::vector<CM::ModCheck> checks = CM::CheckModsInstalled(map, [&](const CM::ModRef& mod) {
        ++asked;
        return mod.name == "MegaMod 10";
    });
    CHECK_EQ(asked, 3);
    CHECK_EQ(checks.size(), 3u);
    if (checks.size() == 3) {
        CHECK(checks[0].name == "MegaMod 10" && checks[0].total == 2 && checks[0].missing == 0);
        CHECK(checks[1].name == "Other Mod" && checks[1].total == 1 && checks[1].missing == 1);
        CHECK(checks[2].name == "(unnamed mod)" && checks[2].total == 1 && checks[2].missing == 1);
    }
    CHECK(CM::ModsLine(checks) == "Needs: Other Mod and (unnamed mod)" ||
          CM::ModsLine(checks) == "Needs: Other Mod, (unnamed mod)");
    // all enabled: nothing to say; a map that lists no mod: nothing to say
    checks = CM::CheckModsInstalled(map, [](const CM::ModRef&) { return true; });
    CHECK(CM::ModsLine(checks).empty());
    map.mods.clear();
    CHECK(CM::CheckModsInstalled(map, [](const CM::ModRef&) { return false; }).empty());
}

UNIT_TEST(ext_custom_maps_row_texts) {
    CHECK(CM::SizeText(512) == "512 x 512");
    CHECK(CM::SizeText(64) == "64 x 64");
    CHECK(CM::ModsText({}, 30) == "No mods listed");
    CHECK(CM::ModsText({"MegaMod"}, 30) == "Needs: MegaMod");
    CHECK(CM::ModsText({"MegaMod", "Colonial"}, 40) == "Needs: MegaMod, Colonial");
    // the second does not fit
    CHECK(CM::ModsText({"MegaMod", "Colonial Charter", "Extra"}, 30) == "Needs: MegaMod +2");
    CHECK(CM::ModsText({"A", "B", "C"}, 30) == "Needs: A, B, C");
    // one name: cut, never dropped
    CHECK(CM::ModsText({"A Mod With A Very Long Name Indeed Yes"}, 20) == "Needs: A Mod With A ...");
    CHECK(CM::CutText("short", 26) == "short");
    CHECK(CM::CutText("abcdefghij", 4) == "abcd...");
    // 'e acute' is 2 bytes: a cut inside it backs up
    CHECK(CM::CutText(std::string("ab") + "Ã©" + "cd", 3) == "ab...");
}

UNIT_TEST(ext_custom_maps_a_picked_uri_gets_a_safe_file_name) {
    // Android's document picker: the name is the URI's last part, %XX decoded, after the volume's "primary:"
    CHECK(CM::PickedFileName("content://com.android.externalstorage.documents/document/"
                             "primary%3ADownload%2FIsland%20map.bmap", "x") == "Island map.bmap");
    // the downloads provider names a number, not a file: the map's own name is used
    CHECK(CM::PickedFileName("content://com.android.providers.downloads.documents/document/msf%3A1234",
                             "Caldera") == "Caldera.bmap");
    // a case other than lower is kept
    CHECK(CM::PickedFileName("content://p/document/A.BMAP", "x") == "A.BMAP");
    // never a path, never hidden, never empty
    CHECK(CM::PickedFileName("content://p/document/%2E%2E%2Fevil.bmap", "x") == "evil.bmap");
    CHECK(CM::PickedFileName("content://p/document/.bmap", "") == "map.bmap");
    CHECK(CM::PickedFileName("content://p/document/1", "a/b:c") == "a_b_c.bmap");
    CHECK(CM::PickedFileName("content://p/document/1", "..hidden ") == "hidden.bmap");
    // %25 decodes to '%', a stray '%' with no two hex digits after it stays; '%' is then no file-name character
    CHECK(CM::PickedFileName("content://p/document/100%25%2.bmap", "x") == "100__2.bmap");
}

UNIT_TEST(ext_custom_maps_list_keeps_author_and_mod_names) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "openba_custom_maps_list_author_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    CM::Map map = ValidMap();
    map.name = "Named";
    map.author = "Some Author";
    CM::ModRef a, b;
    a.name = "MegaMod"; a.templates = {"Template/NaturalResourceTree.rsc"};
    b.name = "Other"; b.templates = {"Template/NaturalResourceRock.rsc"};
    map.mods = {a, b};
    WriteBytes(dir / "m.bmap", CM::Write(map));
    const std::vector<CM::ListedMap> maps = CM::ScanMaps(dir);
    CHECK_EQ(maps.size(), 1u);
    if (maps.size() == 1) {
        CHECK(maps[0].valid && maps[0].author == "Some Author");
        CHECK(maps[0].mods.size() == 2 && maps[0].mods[0] == "MegaMod" && maps[0].mods[1] == "Other");
    }
    fs::remove_all(dir, ec);
}

UNIT_TEST(ext_custom_maps_date_text) {
    CHECK(CM::DateText(0).empty());
    CHECK(CM::DateText(-5).empty());
    const std::string text = CM::DateText(1794524100);   // some day in November 2026, any zone
    CHECK_EQ(text.size(), 19u);                          // "11/12/2026 11:15 PM"
    CHECK(text.size() == 19 && text[2] == '/' && text[5] == '/' && text[10] == ' ' && text[13] == ':' &&
          text[16] == ' ');
}

// ---- the origin byte of a coverage feature's water ----
// water_contour_mesh.cpp marks the water it lays for a map file's coverage features with CoverageWaterOrigin,
// and ext.better_water's river-mouth fade leaves a range so marked alone. The mesh builder needs the running
// game, so this test pins both ends: the byte, and what the fade does with a tile made of it.
namespace {

namespace RM = Ext::BetterWater::RiverMouth;

constexpr int kCoverageStride = 32;
constexpr int kCoverageHalf = 16;

// A tile of full-weight water of the given origin.
std::vector<TerrainVertex> CoverageTile(uint8_t origin) {
    std::vector<TerrainVertex> mesh;
    for (int row = 0; row <= 4; ++row) {
        for (int col = 0; col <= 10; ++col) {
            TerrainVertex v{};
            v.position[0] = static_cast<int16_t>(col * 48);
            v.position[1] = static_cast<int16_t>(row * 48);
            v.normal[0] = 0x7f;
            v.normal[1] = 0x7f;
            v.normal[2] = 0xff;   // a coverage feature's full weight
            v.normal[3] = origin;
            mesh.push_back(v);
        }
    }
    return mesh;
}

// Wet (-1) in rows 1..3 for every column >= 1, dry (+1) elsewhere: a channel cut into the tile's side,
// which the fade would darken.
std::vector<int16_t> CoverageHeights() {
    std::vector<int16_t> heights(kCoverageStride * kCoverageStride, static_cast<int16_t>(256));
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col < kCoverageHalf; ++col) {
            const int at = (row + kCoverageHalf) * kCoverageStride + (col + kCoverageHalf);
            heights[at] = -256;
        }
    }
    return heights;
}

int CoverageFade(const std::vector<TerrainVertex>& mesh, std::vector<TerrainVertex>& out) {
    const std::vector<int16_t> heights = CoverageHeights();
    const RM::Range tile{0, static_cast<int32_t>(mesh.size())};
    return RM::Fade(mesh.data(), static_cast<int32_t>(mesh.size()), &tile, 1, heights.data(), kCoverageStride,
                    kCoverageHalf, RM::kFadeTiles, out);
}

}  // namespace

UNIT_TEST(ext_custom_maps_coverage_water_origin_byte_is_marked_and_the_river_mouth_fade_leaves_it_alone) {
    const int marked = CM::CoverageWaterOrigin(true);
    const int plain = CM::CoverageWaterOrigin(false);
    CHECK_EQ(marked, static_cast<int>(Ext::WaterOrigin::kCustomMaps));
    CHECK_EQ(plain, static_cast<int>(Ext::WaterOrigin::kRetail));
    CHECK(marked != static_cast<int>(Ext::WaterOrigin::kTerrainEdit));

    // a tile of coverage water: not one vertex changes
    std::vector<TerrainVertex> out;
    CHECK(CoverageFade(CoverageTile(CM::CoverageWaterOrigin(true)), out) == 0);
    for (const TerrainVertex& v : out) CHECK_EQ(static_cast<int>(v.normal[2]), 0xff);

    // the same tile written as any other water fades toward the channel: the origin byte protects it
    CHECK(CoverageFade(CoverageTile(CM::CoverageWaterOrigin(false)), out) > 0);
}
