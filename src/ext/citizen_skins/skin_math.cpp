// src/ext/citizen_skins/skin_math.cpp -- ext.citizen_skins: the engine-free half (see skin_math.h)

#include "ext/citizen_skins/skin_math.h"

#include "better_trees/dxbc.h"

#include <algorithm>
#include <cstring>

namespace Ext {
namespace CitizenSkins {

namespace {

// ---- a compiled resource's bytes (the reader rsc_decode.py's Cursor is) -----------------------------------

struct Cursor {
    const uint8_t* d;
    size_t n;
    size_t pos = 0;
    bool ok = true;

    bool Need(size_t k) {
        if (!ok || k > n || pos > n - k) ok = false;
        return ok;
    }
    uint32_t U32() {
        if (!Need(4)) return 0;
        uint32_t v;
        std::memcpy(&v, d + pos, 4);
        pos += 4;
        return v;
    }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    void Skip(size_t k) {
        if (Need(k)) pos += k;
    }
    // A length-prefixed string without the reference flag: bit 31 of the field says 1 byte per character,
    // else 2; the length counts the trailing null. Returns the text as bytes (the low byte of each
    // character).
    std::string Str() {
        const uint32_t field = U32();
        const uint32_t len = field & 0x7fffffffu;
        const bool narrow = (field & 0x80000000u) != 0;
        std::string s;
        if (!ok || len > 4096) {
            ok = false;
            return s;
        }
        const size_t bytes = narrow ? len : static_cast<size_t>(len) * 2;
        if (!Need(bytes)) return s;
        for (uint32_t i = 0; i < len; ++i) {
            const char c = static_cast<char>(d[pos + (narrow ? i : i * 2)]);
            if (c != 0) s.push_back(c);
        }
        pos += bytes;
        return s;
    }
};

constexpr uint32_t kBlock = 0xaaaaaaaau;

constexpr int kFormatBc1 = 0;
constexpr int kFormatBc3a = 1;
constexpr int kFormatBc3b = 2;
constexpr int kFormatGrey = 4;
constexpr int kFormatBgra = 5;

size_t LevelBytes(int format, int w, int h) {
    const size_t bw = static_cast<size_t>(std::max(1, (w + 3) / 4));
    const size_t bh = static_cast<size_t>(std::max(1, (h + 3) / 4));
    switch (format) {
        case kFormatBc1: return bw * bh * 8;
        case kFormatBc3a:
        case kFormatBc3b: return bw * bh * 16;
        case kFormatGrey: return static_cast<size_t>(w) * static_cast<size_t>(h);
        case kFormatBgra: return static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        default: return 0;
    }
}

void Rgb565(uint16_t v, int* r, int* g, int* b) {
    *r = ((v >> 11) & 0x1f) * 255 / 31;
    *g = ((v >> 5) & 0x3f) * 255 / 63;
    *b = (v & 0x1f) * 255 / 31;
}

// One BC1 colour block (8 bytes) to 16 RGBA pixels, row-major 4 x 4. BC1 reads c0 <= c1 as the three-colour
// mode with a clear black entry; the colour half of a BC3 block never does (`fourColours`).
void Bc1Block(const uint8_t* blk, uint8_t out[64], bool fourColours) {
    uint16_t c0;
    uint16_t c1;
    std::memcpy(&c0, blk, 2);
    std::memcpy(&c1, blk + 2, 2);
    uint32_t bits;
    std::memcpy(&bits, blk + 4, 4);
    int pal[4][4];
    Rgb565(c0, &pal[0][0], &pal[0][1], &pal[0][2]);
    Rgb565(c1, &pal[1][0], &pal[1][1], &pal[1][2]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || fourColours) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
            pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
            pal[3][k] = 0;
        }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    for (int i = 0; i < 16; ++i) {
        const int idx = static_cast<int>((bits >> (2 * i)) & 3u);
        for (int k = 0; k < 4; ++k) out[i * 4 + k] = static_cast<uint8_t>(pal[idx][k]);
    }
}

void Bc3Alpha(const uint8_t* blk, uint8_t out[16]) {
    const int a0 = blk[0];
    const int a1 = blk[1];
    int ramp[8] = {a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) ramp[1 + i] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (int i = 1; i < 5; ++i) ramp[1 + i] = ((5 - i) * a0 + i * a1) / 5;
        ramp[6] = 0;
        ramp[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(blk[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = static_cast<uint8_t>(ramp[(bits >> (3 * i)) & 7u]);
}

void Put(uint8_t* rgba, int width, int height, int bx, int by, const uint8_t px[64]) {
    for (int y = 0; y < 4; ++y) {
        const int yy = by * 4 + y;
        if (yy >= height) break;
        for (int x = 0; x < 4; ++x) {
            const int xx = bx * 4 + x;
            if (xx >= width) break;
            std::memcpy(rgba + (static_cast<size_t>(yy) * static_cast<size_t>(width) +
                                static_cast<size_t>(xx)) * 4,
                        px + (y * 4 + x) * 4, 4);
        }
    }
}

uint32_t Rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

void DecodeBc1(const uint8_t* blocks, int width, int height, uint8_t* rgba) {
    const int bw = std::max(1, (width + 3) / 4);
    const int bh = std::max(1, (height + 3) / 4);
    uint8_t px[64];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            Bc1Block(blocks + (static_cast<size_t>(by) * static_cast<size_t>(bw) +
                               static_cast<size_t>(bx)) * 8, px, false);
            Put(rgba, width, height, bx, by, px);
        }
    }
}

void DecodeBc3(const uint8_t* blocks, int width, int height, uint8_t* rgba) {
    const int bw = std::max(1, (width + 3) / 4);
    const int bh = std::max(1, (height + 3) / 4);
    uint8_t px[64];
    uint8_t alpha[16];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* blk =
                blocks + (static_cast<size_t>(by) * static_cast<size_t>(bw) + static_cast<size_t>(bx)) * 16;
            Bc3Alpha(blk, alpha);
            Bc1Block(blk + 8, px, true);
            for (int i = 0; i < 16; ++i) px[i * 4 + 3] = alpha[i];
            Put(rgba, width, height, bx, by, px);
        }
    }
}

bool ReadTextureLevel0(const uint8_t* data, size_t size, Strip* out, std::string* why, int expectWidth,
                       int expectHeight) {
    auto fail = [&](const char* msg) {
        if (why != nullptr) *why = msg;
        return false;
    };
    Cursor c{data, size};
    for (int i = 0; i < 7; ++i) c.U32();   // the object header
    c.Str();                               // _imageName (the build-time source path)
    c.Skip(8);
    if (!c.ok) return fail("short header");
    // A template reference (Texture\ModelTexture.rsc and so on) unless the nested block follows at once.
    uint32_t next = 0;
    if (c.Need(4)) std::memcpy(&next, data + c.pos, 4);
    if (next != kBlock) {
        const std::string typeRef = c.Str();
        c.Skip(8);
        if (typeRef == "Texture\\ModelAlphaTexture.rsc" || typeRef == "Texture\\TerrainAlphaTexture.rsc") {
            c.Str();
            c.Skip(8);
        }
    }
    if (c.U32() != kBlock) return fail("no nested block");
    c.U32();   // inner length
    c.U32();   // zero
    const int width = c.I32();
    const int height = c.I32();
    c.I32();
    c.I32();
    const int format = c.I32();
    for (int i = 0; i < 5; ++i) c.I32();   // 3 more header ints and 2 whose meaning is not needed
    c.Str();                               // _imageName again
    const int mips = c.I32();
    if (!c.ok) return fail("short texture header");
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || mips < 1 || mips > 20) {
        return fail("odd size");
    }
    // The size comes from the file: bound it before anything is allocated or decoded.
    if (static_cast<int64_t>(width) * static_cast<int64_t>(height) > kMaxStripPixels) {
        return fail("a strip over 2 million pixels");
    }
    if (expectWidth > 0 && expectHeight > 0 && (width != expectWidth || height != expectHeight)) {
        return fail("not the first strip's size");
    }
    const size_t want = LevelBytes(format, width, height);
    if (want == 0) return fail("unknown pixel format");
    const int32_t declared = c.I32();
    if (!c.ok || declared < 0 || static_cast<size_t>(declared) != want) {
        return fail("level 0 size is not the format's");
    }
    if (!c.Need(want)) return fail("level 0 is cut off");
    const uint8_t* px = data + c.pos;

    out->width = width;
    out->height = height;
    out->rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 255);
    switch (format) {
        case kFormatBc1:
            DecodeBc1(px, width, height, out->rgba.data());
            break;
        case kFormatBc3a:
        case kFormatBc3b:
            DecodeBc3(px, width, height, out->rgba.data());
            break;
        case kFormatGrey:
            for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
                out->rgba[i * 4 + 0] = out->rgba[i * 4 + 1] = out->rgba[i * 4 + 2] = px[i];
            }
            break;
        default:   // BGRA8
            for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
                out->rgba[i * 4 + 0] = px[i * 4 + 2];
                out->rgba[i * 4 + 1] = px[i * 4 + 1];
                out->rgba[i * 4 + 2] = px[i * 4 + 0];
                out->rgba[i * 4 + 3] = px[i * 4 + 3];
            }
            break;
    }
    return true;
}

bool BuildChain(const std::vector<const Strip*>& strips, Chain* out, std::string* why) {
    auto fail = [&](const char* msg) {
        if (why != nullptr) *why = msg;
        return false;
    };
    if (strips.empty() || strips[0] == nullptr) return fail("no strips");
    const int sw = strips[0]->width;
    const int sh = strips[0]->height;
    for (const Strip* s : strips) {
        if (s == nullptr || s->width != sw || s->height != sh || s->width <= 0 || s->height <= 0 ||
            s->rgba.size() != static_cast<size_t>(sw) * static_cast<size_t>(sh) * 4) {
            return fail("the strips are not all one size");
        }
    }
    const int n = static_cast<int>(strips.size());
    if (static_cast<int64_t>(sw) * n > kMaxTextureSide ||
        static_cast<int64_t>(sw) * n * static_cast<int64_t>(sh) > kMaxChainPixels) {
        return fail("the joined image would be too large");
    }
    const int w = sw * n;
    const int h = sh;
    out->width = w;
    out->height = h;
    out->levels.clear();
    std::vector<uint8_t> level0(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int s = 0; s < n; ++s) {
            const uint8_t* src = strips[static_cast<size_t>(s)]->rgba.data() +
                                 static_cast<size_t>(y) * static_cast<size_t>(sw) * 4;
            uint8_t* dst = level0.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) +
                                            static_cast<size_t>(s) * static_cast<size_t>(sw)) * 4;
            for (int x = 0; x < sw; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];   // BGRA
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = 255;
            }
        }
    }
    out->levels.push_back(std::move(level0));
    int cw = w;
    int ch = h;
    while (cw > 1 || ch > 1) {
        const int nw = std::max(1, cw >> 1);
        const int nh = std::max(1, ch >> 1);
        const std::vector<uint8_t>& prev = out->levels.back();
        std::vector<uint8_t> next(static_cast<size_t>(nw) * static_cast<size_t>(nh) * 4);
        for (int y = 0; y < nh; ++y) {
            const int y0 = std::min(2 * y, ch - 1);
            const int y1 = std::min(2 * y + 1, ch - 1);
            for (int x = 0; x < nw; ++x) {
                const int x0 = std::min(2 * x, cw - 1);
                const int x1 = std::min(2 * x + 1, cw - 1);
                const uint8_t* p00 =
                    &prev[(static_cast<size_t>(y0) * static_cast<size_t>(cw) + static_cast<size_t>(x0)) * 4];
                const uint8_t* p10 =
                    &prev[(static_cast<size_t>(y0) * static_cast<size_t>(cw) + static_cast<size_t>(x1)) * 4];
                const uint8_t* p01 =
                    &prev[(static_cast<size_t>(y1) * static_cast<size_t>(cw) + static_cast<size_t>(x0)) * 4];
                const uint8_t* p11 =
                    &prev[(static_cast<size_t>(y1) * static_cast<size_t>(cw) + static_cast<size_t>(x1)) * 4];
                uint8_t* d =
                    &next[(static_cast<size_t>(y) * static_cast<size_t>(nw) + static_cast<size_t>(x)) * 4];
                for (int k = 0; k < 4; ++k) {
                    d[k] = static_cast<uint8_t>((p00[k] + p10[k] + p01[k] + p11[k] + 2) >> 2);
                }
            }
        }
        out->levels.push_back(std::move(next));
        cw = nw;
        ch = nh;
    }
    return true;
}

// ---- the vertex program -----------------------------------------------------------------------------------

namespace {

constexpr uint32_t kImmediate32Scalar = 0x00004001u;   // the operand token of a one-component literal
constexpr uint32_t kEighth = 0x3e000000u;              // 0.125f

// The offsets (from the blob's start) of every `immediate 0.125` operand's value.
std::vector<size_t> FindEighths(const uint8_t* blob, size_t size) {
    std::vector<size_t> at;
    for (size_t i = 0; i + 8 <= size; i += 4) {
        if (Rd32(blob + i) == kImmediate32Scalar && Rd32(blob + i + 4) == kEighth) at.push_back(i + 4);
    }
    return at;
}

bool SumMatches(const uint8_t* blob, size_t size) {
    if (size < 32 || std::memcmp(blob, "DXBC", 4) != 0) return false;
    uint32_t sum[4];
    Dxbc::Checksum(blob, size, sum);
    return std::memcmp(sum, blob + 4, 16) == 0;
}

}  // namespace

int CountEighths(const uint8_t* blob, size_t size) {
    if (blob == nullptr || size < 32 || std::memcmp(blob, "DXBC", 4) != 0) return -1;
    return static_cast<int>(FindEighths(blob, size).size());
}

int PatchVertexBlob(std::vector<uint8_t>* blob, int sets) {
    if (blob == nullptr || sets < 1 || sets > kMaxSets) return 0;
    if (!SumMatches(blob->data(), blob->size())) return 0;
    const std::vector<size_t> at = FindEighths(blob->data(), blob->size());
    if (at.size() != 1) return 0;
    const float value = 1.0f / static_cast<float>(kCells * sets);
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    std::memcpy(blob->data() + at[0], &bits, 4);
    uint32_t sum[4];
    Dxbc::Checksum(blob->data(), blob->size(), sum);
    std::memcpy(blob->data() + 4, sum, 16);
    return 1;
}

namespace {

// A 64-bit finalizer (splitmix64's): every input bit reaches every output bit.
uint64_t Mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

}  // namespace

int ParseStripName(const char* entry, bool* female) {
    static const char kPrefix[] = "models\\citizen\\materialinstance\\citizentexture";
    static const char kSuffix[] = "_resource.crs";
    if (entry == nullptr) return -1;
    std::string s;
    for (const char* p = entry; *p != 0 && s.size() < 256; ++p) {
        char c = *p == '/' ? '\\' : *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        s += c;
    }
    const size_t prefix = sizeof(kPrefix) - 1;
    const size_t suffix = sizeof(kSuffix) - 1;
    if (s.size() < prefix + suffix || s.compare(0, prefix, kPrefix) != 0) return -1;
    size_t i = prefix;
    bool f = false;
    if (s[i] == 'f') {
        f = true;
        ++i;
    }
    const size_t firstDigit = i;
    int number = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        number = number * 10 + (s[i] - '0');
        ++i;
        if (i - firstDigit > 2) return -1;
    }
    if (s.size() - i != suffix || s.compare(i, suffix, kSuffix) != 0) return -1;
    // 0, 1 and 07 are not numbers here: the plain names are the first pair, 2 is the next
    if (i > firstDigit && (s[firstDigit] == '0' || number < 2)) return -1;
    if (female != nullptr) *female = f;
    return number;
}

uint64_t SetIdentity(uint64_t maleHash, uint64_t femaleHash) {
    return Mix64(maleHash ^ Mix64(femaleHash + 0x9e3779b97f4a7c15ull));
}

int SetForName(const char* name, const uint64_t* ids, int sets) {
    if (sets <= 1 || ids == nullptr) return 0;
    uint64_t h = 14695981039346656037ull;   // FNV-1a over the name
    if (name != nullptr) {
        for (const char* p = name; *p != 0; ++p) {
            h ^= static_cast<uint8_t>(*p);
            h *= 1099511628211ull;
        }
    }
    h = Mix64(h);
    int best = 0;
    uint64_t bestScore = Mix64(h ^ Mix64(ids[0]));
    for (int i = 1; i < sets; ++i) {
        const uint64_t score = Mix64(h ^ Mix64(ids[i]));
        if (score > bestScore) {   // a tie goes to the earlier set
            best = i;
            bestScore = score;
        }
    }
    return best;
}

uint64_t StripHash(const Strip& s) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : s.rgba) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h ^ (static_cast<uint64_t>(s.width) << 32) ^ static_cast<uint64_t>(s.height);
}

Sets ChooseSets(const std::vector<PackSkin>& packs, std::vector<std::string>* notes) {
    Sets s;
    auto note = [&](const std::string& m) {
        if (notes != nullptr) notes->push_back(m);
    };
    if (packs.empty()) return s;
    const PackSkin& base = packs[0];
    if (!base.hasMale || !base.hasFemale) {
        note("the first package with a citizen texture has only one of the two: off");
        return s;
    }
    s.male.push_back(&base.male);
    s.female.push_back(&base.female);
    s.ids.push_back(SetIdentity(base.hashMale, base.hashFemale));
    std::vector<std::pair<uint64_t, uint64_t>> seen;
    seen.emplace_back(base.hashMale, base.hashFemale);
    // as many sets as the limit and the joined image's width (a strip's width times the sets) allow
    const int widest = std::max(base.male.width, base.female.width);
    const int limit = widest > 0 ? std::max(1, std::min(kMaxSets, kMaxTextureSide / widest)) : 1;
    for (size_t k = 1; k < packs.size() && static_cast<int>(s.male.size()) < limit; ++k) {
        const PackSkin& p = packs[k];
        const Strip* m = p.hasMale ? &p.male : &base.male;
        const Strip* f = p.hasFemale ? &p.female : &base.female;
        const uint64_t hm = p.hasMale ? p.hashMale : base.hashMale;
        const uint64_t hf = p.hasFemale ? p.hashFemale : base.hashFemale;
        if (m->width != base.male.width || m->height != base.male.height || f->width != base.female.width ||
            f->height != base.female.height) {
            note("set " + std::to_string(k) + ": its strips are not the first package's size: left out");
            continue;
        }
        bool dup = false;
        for (const auto& h : seen) dup = dup || (h.first == hm && h.second == hf);
        if (dup) continue;
        seen.emplace_back(hm, hf);
        s.male.push_back(m);
        s.female.push_back(f);
        s.ids.push_back(SetIdentity(hm, hf));
    }
    return s;
}

}  // namespace CitizenSkins
}  // namespace Ext
