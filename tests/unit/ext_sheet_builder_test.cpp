// tests/unit/ext_sheet_builder_test.cpp -- block RSCSPRITE (KITFREE.md §3, §5).
//
// 1. The packer: the baked Extensions bitmaps fit the .rsc's sheet (512 x 512 since
//    2026-10-18; 256 x 256 held 28 and refused the 29th),
//    every sprite's own pixels are its source (RGBA -> BGRA), the pad ring
//    replicates its edge, no two sprites overlap, the records are the kit's form.
// 2. The cross-check against the kit (KITFREE.md §5): when the kit's compiled
//    build/ext_resources/WinData/Dialog/ExtensionsSprites*.crs exist (tools/
//    ext_resources.sh), the UI::SpriteSheet we fill is written in the
//    BinaryWriter's record format (mirrored here: the ported writer family and
//    SpriteSheet::Serialize are slot PROBES in this host build, see sources.cmake)
//    and compared BYTE FOR BYTE with the kit's object body, the four uv floats
//    of each record excepted (placement is the one thing allowed to differ); the
//    material's and the texture's records are parsed and compared field by field
//    (their Serialize is not replayable on a host: MaterialInstance resolves a
//    real Material, ImageBuffer recreates a device texture), and every sprite's
//    pixels in the kit's atlas equal ours. Skipped (not passed) without the
//    files. The CRC the test prints is the CRC the exe's provider logs for the
//    SAME object written by the real IO::BinaryWriter (`[ExtRes] sprites: ...
//    sheet N bytes crc X`): equal CRCs = the mirror is the writer's format.
#include "unit.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ext/resources/ext_sprites_data.h"
#include "ext/resources/sheet_builder.h"
#include "ext/registry.h"   // the rows whose icons the sheet must carry
#include "object/External.h"
#include "system/Crc.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "ui/SpriteSheet.h"

using Ext::Resources::SheetBitmap;
using Ext::Resources::SheetLayout;
using Ext::Resources::SheetRecord;
using Ext::Resources::SheetSpec;

namespace {

SheetSpec ExtSpec() {
    SheetSpec s;
    s.sheetWidth      = Ext::Resources::SpritesData::kSheetWidth;
    s.sheetHeight     = Ext::Resources::SpritesData::kSheetHeight;
    s.padForFiltering = Ext::Resources::SpritesData::kPadForFiltering;
    s.materialName    = Ext::Resources::SpritesData::kMaterialName;
    s.imageName       = Ext::Resources::SpritesData::kImageName;
    return s;
}

const SheetBitmap* Bitmaps() { return Ext::Resources::SpritesData::kBitmaps; }
int BitmapCount() { return Ext::Resources::SpritesData::kBitmapCount; }

const uint8_t* Texel(const SheetLayout& l, int x, int y) {
    return &l.bgra[(static_cast<size_t>(y) * l.width + x) * 4];
}
bool SameTexel(const uint8_t* bgra, const uint8_t* rgba) {
    return bgra[0] == rgba[2] && bgra[1] == rgba[1] && bgra[2] == rgba[0] && bgra[3] == rgba[3];
}

// ---- the kit's .crs: the compressed-stream container and the LZF blocks ------
std::vector<uint8_t> ReadFile(const char* path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return out;
    std::fseek(f, 0, SEEK_END);
    const int64_t n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0 && std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    std::fclose(f);
    return out;
}

bool Lzf(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expect) {
    const size_t start = out.size();
    size_t i = 0;
    while (i < n) {
        const uint8_t c = src[i++];
        if (c < 32) {
            out.insert(out.end(), src + i, src + i + c + 1);
            i += c + 1;
        } else {
            size_t len = c >> 5;
            if (len == 7) len += src[i++];
            const size_t off = (static_cast<size_t>(c & 0x1f) << 8) + src[i++] + 1;
            if (off > out.size()) return false;
            for (size_t k = 0; k < len + 2; ++k) out.push_back(out[out.size() - off]);
        }
    }
    return out.size() - start == expect;
}

bool DecodeCrs(const char* path, std::vector<uint8_t>& out) {
    const std::vector<uint8_t> d = ReadFile(path);
    if (d.size() < 4) return false;
    uint32_t count;
    std::memcpy(&count, d.data(), 4);
    for (uint32_t b = 0; b < count; ++b) {
        uint32_t offset, packed;
        std::memcpy(&offset, d.data() + 4 + 8 * b, 4);
        std::memcpy(&packed, d.data() + 8 + 8 * b, 4);
        const size_t comp = packed & 0xffff;
        size_t uncomp = packed >> 16;
        if (uncomp == 0) uncomp = 0x10000;
        if (offset + comp > d.size()) return false;
        if (comp == uncomp) out.insert(out.end(), d.data() + offset, d.data() + offset + comp);
        else if (!Lzf(d.data() + offset, comp, out, uncomp)) return false;
    }
    return true;
}

// A cursor over BinaryWriter records (the kit's decoded bytes, or ours).
struct Cur {
    const std::vector<uint8_t>* d;
    size_t o = 0;
    bool ok = true;
    uint32_t U32() {
        uint32_t v = 0;
        if (o + 4 > d->size()) { ok = false; return 0; }
        std::memcpy(&v, d->data() + o, 4); o += 4; return v;
    }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    float F32() { float f = 0; const uint32_t u = U32(); std::memcpy(&f, &u, 4); return f; }
    uint8_t U8() { if (o >= d->size()) { ok = false; return 0; } return (*d)[o++]; }
    // String::Serialize's record: u32 length | 0x80000000, then the bytes (nul included).
    std::string Str() {
        const uint32_t n = U32();
        if (!(n & 0x80000000u)) { ok = false; return std::string(); }
        const size_t len = n & 0x7fffffffu;
        if (o + len > d->size()) { ok = false; return std::string(); }
        std::string s(reinterpret_cast<const char*>(d->data() + o), len);
        o += len;
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }
    // The BinaryWriter frame: aaaaaaaa, length, version. Returns the version.
    int32_t Frame() {
        const uint32_t m = U32();
        if (m != 0xaaaaaaaau) ok = false;
        (void)U32();
        return I32();
    }
    // The compiled-external header: magic, type, version, the dependency list.
    void Header(uint32_t& typeId, int32_t& fileVersion) {
        (void)Frame();
        const uint32_t magic = U32();
        if (magic != 0x5412a91fu) ok = false;
        typeId = U32();
        fileVersion = I32();
        const int32_t deps = I32();
        for (int32_t i = 0; i < deps && ok; ++i) { (void)Str(); (void)U32(); (void)U32(); }
    }
};

// The BinaryWriter's record format, mirrored (Serializer.h: the frame is
// aaaaaaaa / length / version ... bbbbbbbb with the length patched to cover
// the version dword and everything after it; text_codec.cpp: a String is
// u32 (length | 0x80000000) + the bytes with their NUL, an empty String is
// the length 0 | 0x80000000 and no bytes; external_ptr_serialize.cpp: an
// ExternalPtr is one bool and, when set, the object's path).
struct Rec {
    std::vector<uint8_t> b;
    size_t lenAt = 0;
    void U32(uint32_t v) { const size_t o = b.size(); b.resize(o + 4); std::memcpy(&b[o], &v, 4); }
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void F32(float f) { uint32_t u; std::memcpy(&u, &f, 4); U32(u); }
    void Bool(bool v) { b.push_back(v ? 1 : 0); }
    void Str(const System::String& s) {
        const char* p = s._data;
        const size_t n = p ? std::strlen(p) + 1 : 0;
        U32(static_cast<uint32_t>(n) | 0x80000000u);
        if (n) b.insert(b.end(), p, p + n);
    }
    void ExtPtr(const Object::External* e) {
        Bool(e != nullptr);
        if (e != nullptr) Str(e->_path);
    }
    void Begin(int32_t version) { U32(0xaaaaaaaau); lenAt = b.size(); U32(0xffffffffu); I32(version); }
    void End() {
        const uint32_t len = static_cast<uint32_t>(b.size() - (lenAt + 4));
        U32(0xbbbbbbbbu);
        std::memcpy(&b[lenAt], &len, 4);
    }
};

// UI::SpriteSheet::Serialize's member order (spritesheet_vtable.cpp, retail
// 0x18011d730 + the 0x180078ad0 record walker) over the mirrored records.
std::vector<uint8_t> WriteSheet(const UI::SpriteSheet& s) {
    Rec r;
    r.Begin(0);
    r.Str(s.m_groupName);     // +0x40 first
    r.Str(s.m_textureName);   // +0x38
    r.I32(s.m_field48);
    r.I32(s.m_field4C);
    r.Bool(s.m_field50);
    r.I32(s.m_sprites._size);
    for (int32_t i = 0; i < s.m_sprites._size; ++i) {
        const UI::SpriteInfo& e = s.m_sprites._data[i];
        r.Str(e.name);
        r.Str(e.group);
        r.I32(e.x); r.I32(e.y); r.I32(e.width); r.I32(e.height);
        r.I32(e._reserved18);   // fifth of the five ints
        r.F32(e.u0); r.F32(e.v0); r.F32(e.u1); r.F32(e.v1);
    }
    r.ExtPtr(reinterpret_cast<const Object::External*>(s.m_field30));
    r.End();
    return r.b;
}

constexpr const char* kKitSheet    = "build/ext_resources/WinData/Dialog/ExtensionsSprites_resource.crs";
constexpr const char* kKitMaterial = "build/ext_resources/WinData/Dialog/ExtensionsSpritesMaterial_resource.crs";
constexpr const char* kKitTexture  = "build/ext_resources/WinData/Dialog/ExtensionsSpritesTexture_resource.crs";

// The host has no seated MemoryAllocator (its `_singleton` is null): the
// engine objects the cross-check builds allocate through the instrument seat,
// armed with the CRT heap for the test's duration (allocator_seat_test.cpp
// documents the seat's contract; malloc's 16-byte alignment covers 4 and 8).
void* SeatAlloc(uint64_t size, int) { return std::malloc(static_cast<size_t>(size)); }
void  SeatFree(void* p) { std::free(p); }
struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree  = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree  = nullptr;
    }
};

// A stand-in for the objects an ExternalPtr names on the write path: only
// the path is read, so any External with the path will do.
UI::SpriteSheet* PathStandIn(const char* path) {
    UI::SpriteSheet* s = ::new (System::MemoryAllocator::MemAlloc(sizeof(UI::SpriteSheet), 8)) UI::SpriteSheet();
    s->_path = System::Filename(path);
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
UNIT_TEST(ext_sheet_pack_fits_pads_and_records) {
    SheetLayout l;
    // The size the .rsc names (and the bake copied); 512 x 512 since 2026-10-18, 512 x 1024 since
    // 2026-11-04 (the 116th sprite did not fit).
    const int W = Ext::Resources::SpritesData::kSheetWidth;
    const int H = Ext::Resources::SpritesData::kSheetHeight;
    CHECK_EQ(W, 512);
    CHECK_EQ(H, 1024);
    CHECK(Ext::Resources::PackSheet(ExtSpec(), Bitmaps(), BitmapCount(), l));
    CHECK_EQ(l.width, W);
    CHECK_EQ(l.height, H);
    CHECK(l.padded);
    CHECK_EQ(static_cast<int>(l.places.size()), BitmapCount());
    CHECK_EQ(l.bgra.size(), static_cast<size_t>(W) * H * 4);
    if (l.places.size() != static_cast<size_t>(BitmapCount())) return;

    std::vector<uint8_t> owner(static_cast<size_t>(W) * H, 0);   // 0 = free, else bitmap index + 1
    for (int i = 0; i < BitmapCount(); ++i) {
        const SheetBitmap& b = Bitmaps()[i];
        const int x = l.places[static_cast<size_t>(i)].x, y = l.places[static_cast<size_t>(i)].y;
        CHECK(x >= 1 && y >= 1 && x + b.width + 1 <= W && y + b.height + 1 <= H);
        bool pixels = true, ring = true, overlap = false;
        for (int sy = -1; sy <= b.height; ++sy) {
            for (int sx = -1; sx <= b.width; ++sx) {
                const int cx = sx < 0 ? 0 : (sx >= b.width ? b.width - 1 : sx);
                const int cy = sy < 0 ? 0 : (sy >= b.height ? b.height - 1 : sy);
                const uint8_t* src = b.rgba + (static_cast<size_t>(cy) * b.width + cx) * 4;
                const bool same = SameTexel(Texel(l, x + sx, y + sy), src);
                if (sx >= 0 && sy >= 0 && sx < b.width && sy < b.height) pixels = pixels && same;
                else ring = ring && same;
                uint8_t& o = owner[static_cast<size_t>(y + sy) * W + (x + sx)];
                if (o != 0) overlap = true;
                o = static_cast<uint8_t>(i + 1);
            }
        }
        CHECK(pixels);
        CHECK(ring);
        CHECK(!overlap);
        const SheetRecord r = Ext::Resources::RecordFor(l, b, i);
        CHECK_EQ(r.x, 1);
        CHECK_EQ(r.y, 1);
        CHECK_EQ(r.reserved, 1);
        CHECK_EQ(r.width, b.width);
        CHECK_EQ(r.height, b.height);
        CHECK_EQ(r.u0 * static_cast<float>(W), static_cast<float>(x));
        CHECK_EQ(r.v0 * static_cast<float>(H), static_cast<float>(y));
        CHECK_EQ(r.u1 * static_cast<float>(W), static_cast<float>(x + b.width));
        CHECK_EQ(r.v1 * static_cast<float>(H), static_cast<float>(y + b.height));
    }
    // A sheet too small is refused, not truncated.
    SheetSpec small = ExtSpec();
    small.sheetWidth = 64; small.sheetHeight = 64;
    SheetLayout l2;
    CHECK(!Ext::Resources::PackSheet(small, Bitmaps(), BitmapCount(), l2));
    CHECK(l2.bgra.empty());
    // Without padding the records carry zeros and the first sprite sits at (0, 0).
    SheetSpec nopad = ExtSpec();
    nopad.padForFiltering = false;
    SheetLayout l3;
    CHECK(Ext::Resources::PackSheet(nopad, Bitmaps(), BitmapCount(), l3));
    const SheetRecord r3 = Ext::Resources::RecordFor(l3, Bitmaps()[6], 6);   // the first 48 x 48
    CHECK_EQ(r3.x, 0); CHECK_EQ(r3.reserved, 0); CHECK_EQ(r3.u0, 0.0f); CHECK_EQ(r3.v0, 0.0f);
}

// Every registry row has its icon (the Extensions dialog's ExtensionsEntry_<id>, tools/ext_icons/): a row
// added without one draws no picture (fps_cap and phone_menu had none until P9). The unit tests' own rows
// (unit_a / unit_b / unit_c, registry.cpp under BANISHED_UNIT_TEST) are not in the exe and have none.
UNIT_TEST(ext_sheet_every_entry_has_an_icon) {
    for (int i = 0; i < Ext::EntryCount(); ++i) {
        const Ext::Entry& e = Ext::EntryAt(i);
        if (std::strncmp(e.id, "unit_", 5) == 0) continue;
        const std::string name = std::string("ExtensionsEntry_") + e.id;
        bool found = false;
        for (int b = 0; b < BitmapCount(); ++b) {
            if (name == Bitmaps()[b].name) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::fprintf(stderr, "  no icon for registry row %s\n", e.id);
        }
        CHECK(found);
    }
}

// ---------------------------------------------------------------------------
UNIT_TEST(ext_sheet_kit_crosscheck) {
    std::vector<uint8_t> kit;
    if (!DecodeCrs(kKitSheet, kit))
        UNIT_SKIP("no kit output: run tools/ext_resources.sh (needs ToolKit/) for the cross-check");
    HeapSeat seat;

    // ---- our sheet, in the writer's record format ------------------------------
    SheetLayout l;
    CHECK(Ext::Resources::PackSheet(ExtSpec(), Bitmaps(), BitmapCount(), l));
    UI::SpriteSheet* standInMaterial = PathStandIn("Dialog/ExtensionsSpritesMaterial.rsc:resource");
    UI::SpriteSheet* sheet = PathStandIn("Dialog/ExtensionsSprites.rsc:resource");
    Ext::Resources::FillSpriteSheet(*sheet, ExtSpec(), Bitmaps(), BitmapCount(), l,
                                    reinterpret_cast<Graphics::MaterialInstance*>(standInMaterial));
    const std::vector<uint8_t> ours = WriteSheet(*sheet);
    std::printf("  our sheet record: %zu bytes, crc %08x (the exe logs the real writer's for this object)\n",
                ours.size(), System::CRC::Compute(ours.data(), static_cast<int>(ours.size()), 0xffffffffu));

    // ---- the kit's object body ------------------------------------------------
    Cur k{ &kit };
    uint32_t typeId; int32_t fileVersion;
    k.Header(typeId, fileVersion);
    CHECK(k.ok);
    CHECK_EQ(fileVersion, 6);   // UI::SpriteSheet::GetFileVersion (a slot probe on the host)
    std::printf("  kit header: type %08x version %d (the exe logs UI::SpriteSheet::TypeId())\n", typeId, fileVersion);
    const size_t bodyStart = k.o;
    // The file ends with the LOADER's frame trailer (RtObject_External_Load's
    // own Serializer wraps the header + the object): four bb bytes after the
    // object's own; they are the container's, not the object's.
    CHECK(kit.size() >= bodyStart + 4);
    const uint8_t bb[4] = { 0xbb, 0xbb, 0xbb, 0xbb };
    CHECK(std::memcmp(kit.data() + kit.size() - 4, bb, 4) == 0);
    std::vector<uint8_t> theirs(kit.begin() + static_cast<std::ptrdiff_t>(bodyStart), kit.end() - 4);

    // Blank the uv floats of every record in both streams (the records are
    // parsed from each stream on its own, so a structural difference shows up
    // as a length or a byte mismatch, never as a mis-blanked field).
    auto blankUvs = [](std::vector<uint8_t>& buf, int& count) -> bool {
        Cur c{ &buf };
        (void)c.Frame();
        (void)c.Str(); (void)c.Str();            // _materialName, _imageName
        (void)c.I32(); (void)c.I32(); (void)c.U8();   // width, height, pad
        count = c.I32();
        for (int i = 0; i < count && c.ok; ++i) {
            (void)c.Str(); (void)c.Str();
            for (int j = 0; j < 5; ++j) (void)c.I32();
            if (c.o + 16 > buf.size()) { c.ok = false; break; }
            std::memset(buf.data() + c.o, 0, 16);
            c.o += 16;
        }
        return c.ok;
    };
    int ourCount = 0, kitCount = 0;
    std::vector<uint8_t> oursBlank = ours, theirsBlank = theirs;
    CHECK(blankUvs(oursBlank, ourCount));
    CHECK(blankUvs(theirsBlank, kitCount));
    CHECK_EQ(ourCount, BitmapCount());
    CHECK_EQ(kitCount, BitmapCount());
    CHECK_EQ(oursBlank.size(), theirsBlank.size());
    size_t firstDiff = oursBlank.size();
    const size_t n = oursBlank.size() < theirsBlank.size() ? oursBlank.size() : theirsBlank.size();
    for (size_t i = 0; i < n; ++i) if (oursBlank[i] != theirsBlank[i]) { firstDiff = i; break; }
    if (firstDiff != oursBlank.size() || oursBlank.size() != theirsBlank.size()) {
        std::printf("  sheet bytes differ at %zu (ours %zu bytes, kit %zu): ours %02x kit %02x\n",
                    firstDiff, oursBlank.size(), theirsBlank.size(),
                    firstDiff < oursBlank.size() ? oursBlank[firstDiff] : 0,
                    firstDiff < theirsBlank.size() ? theirsBlank[firstDiff] : 0);
        CHECK(false);
    } else {
        std::printf("  sheet record == the kit's (%zu bytes, uv excepted)\n", ours.size());
    }

    // ---- the kit's placement vs ours: every sprite's pixels in the kit's atlas
    // equal the same sprite's pixels in ours (and the source), ring included.
    std::vector<uint8_t> tex;
    if (DecodeCrs(kKitTexture, tex)) {
        Cur t{ &tex };
        uint32_t tType; int32_t tVer;
        t.Header(tType, tVer);
        CHECK_EQ(tVer, 7);
        (void)t.Frame();
        const int32_t w = t.I32(), h = t.I32();
        const int32_t dim = t.I32(), kind = t.I32(), fmt = t.I32();
        const uint32_t flags = t.U32();
        const int32_t aU = t.I32(), aV = t.I32(), aW = t.I32(), filt = t.I32();
        const std::string srcName = t.Str();
        const int32_t levels = t.I32();
        const int32_t blobSize = t.I32();
        CHECK(t.ok);
        const SheetSpec s = ExtSpec();
        CHECK_EQ(w, l.width); CHECK_EQ(h, l.height);
        CHECK_EQ(dim, 0); CHECK_EQ(kind, s.imageKind); CHECK_EQ(fmt, s.imageFormat);
        CHECK_EQ(flags, s.imageFlags);
        CHECK_EQ(aU, s.imageAddress); CHECK_EQ(aV, s.imageAddress); CHECK_EQ(aW, s.imageAddress);
        CHECK_EQ(filt, s.imageFilter);
        CHECK_STREQ(srcName.c_str(), System::Filename(s.imageName)._data);
        CHECK_EQ(levels, 1);
        CHECK_EQ(static_cast<size_t>(blobSize), l.bgra.size());
        std::printf("  kit texture: type %08x, %d x %d, kind %d format %d flags %u address %d/%d/%d filter %d, "
                    "\"%s\", %d level(s) of %d bytes\n", tType, w, h, kind, fmt, flags, aU, aV, aW, filt,
                    srcName.c_str(), levels, blobSize);
        if (t.ok && static_cast<size_t>(blobSize) == l.bgra.size() && t.o + l.bgra.size() <= tex.size()) {
            const uint8_t* kitPix = tex.data() + t.o;
            // The kit's record for sprite i gives its rectangle in the kit's atlas.
            Cur c{ &kit };
            c.o = bodyStart;
            (void)c.Frame(); (void)c.Str(); (void)c.Str(); (void)c.I32(); (void)c.I32(); (void)c.U8();
            const int32_t cnt = c.I32();
            int mismatched = 0;
            for (int i = 0; i < cnt && i < BitmapCount(); ++i) {
                const std::string nm = c.Str(); (void)c.Str();
                for (int j = 0; j < 5; ++j) (void)c.I32();
                const float u0 = c.F32(), v0 = c.F32(); (void)c.F32(); (void)c.F32();
                const int kx = static_cast<int>(u0 * w + 0.5f), ky = static_cast<int>(v0 * h + 0.5f);
                const SheetBitmap& b = Bitmaps()[i];
                const int ox = l.places[static_cast<size_t>(i)].x, oy = l.places[static_cast<size_t>(i)].y;
                bool same = std::strcmp(nm.c_str(), b.name) == 0;
                for (int sy = -1; sy <= b.height && same; ++sy)
                    for (int sx = -1; sx <= b.width && same; ++sx)
                        same = std::memcmp(kitPix + (static_cast<size_t>(ky + sy) * w + kx + sx) * 4,
                                           Texel(l, ox + sx, oy + sy), 4) == 0;
                if (!same) { ++mismatched; std::printf("  sprite %d (%s) differs from the kit's atlas\n", i, b.name); }
            }
            CHECK_EQ(mismatched, 0);
            if (mismatched == 0) std::printf("  every sprite's pixels + ring == the kit's atlas (%d sprites)\n", cnt);
        }
    } else {
        std::printf("  (no %s: the texture record was not cross-checked)\n", kKitTexture);
    }

    // ---- the material: the kit's records vs what FillMaterialInstance writes ---
    std::vector<uint8_t> mat;
    if (DecodeCrs(kKitMaterial, mat)) {
        Cur m{ &mat };
        uint32_t mType; int32_t mVer;
        m.Header(mType, mVer);
        (void)m.Frame();
        const uint8_t basePresent = m.U8();
        const std::string basePath = m.Str();
        const int32_t textures = m.I32();
        const std::string slot = m.Str();
        const uint8_t texPresent = m.U8();
        const std::string texPath = m.Str();
        const int32_t constants = m.I32();
        CHECK(m.ok);
        CHECK_EQ(mVer, 4);
        CHECK_EQ(basePresent, 1);
        CHECK_STREQ(basePath.c_str(), (std::string(System::Filename("Material/UI/uiMaterial.rsc")._data) + ":resource").c_str());
        CHECK_EQ(textures, 1);
        CHECK_STREQ(slot.c_str(), "diffuse");
        CHECK_EQ(texPresent, 1);
        CHECK_STREQ(texPath.c_str(), System::Filename("Dialog/ExtensionsSpritesTexture.rsc:resource")._data);
        CHECK_EQ(constants, 0);
        std::printf("  kit material: type %08x, base \"%s\", %d texture(s) [\"%s\" -> \"%s\"], %d constant(s)\n",
                    mType, basePath.c_str(), textures, slot.c_str(), texPath.c_str(), constants);
    } else {
        std::printf("  (no %s: the material record was not cross-checked)\n", kKitMaterial);
    }
}
