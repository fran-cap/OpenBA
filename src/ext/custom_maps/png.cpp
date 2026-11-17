// ============================================================================
// src/ext/custom_maps/png.cpp -- ext.custom_maps: the PNG reader (png.h)
// ============================================================================
// The DEFLATE decoder is a C++ rewrite that follows the structure of zlib's contrib/puff/puff.c, version 2.3
// (21 Jan 2013): canonical Huffman codes decoded a bit at a time from per-length counts. It is small and does
// not need speed for a thumbnail a few tens of KB long. It is not a copy of puff.c; the licence of puff.c
// (THIRD_PARTY_NOTICES.md lists it) is the notice of puff.h, reproduced here:
//
//   Copyright (C) 2002-2013 Mark Adler, all rights reserved
//
//   This software is provided 'as-is', without any express or implied warranty. In no event will the author
//   be held liable for any damages arising from the use of this software.
//
//   Permission is granted to anyone to use this software for any purpose, including commercial applications,
//   and to alter it and redistribute it freely, subject to the following restrictions:
//
//   1. The origin of this software must not be misrepresented; you must not claim that you wrote the
//      original software. If you use this software in a product, an acknowledgment in the product
//      documentation would be appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the
//      original software.
//   3. This notice may not be removed or altered from any source distribution.
//
//   Mark Adler    madler@alumni.caltech.edu
// ============================================================================
#include "ext/custom_maps/png.h"

#include <cstring>

namespace Ext {
namespace CustomMaps {

namespace {

constexpr int kMaxBits   = 15;
constexpr int kMaxLCodes = 286;
constexpr int kMaxDCodes = 30;
constexpr int kFixLCodes = 288;

struct Stream {
    const uint8_t* in     = nullptr;
    size_t         size   = 0;
    size_t         pos    = 0;
    uint32_t       bitBuf = 0;
    int            bitCnt = 0;
    std::vector<uint8_t>* out = nullptr;
    size_t         maxOut = 0;
    bool           bad    = false;
};

// `need` bits (at most 13), the first one read in the low bit.
uint32_t Bits(Stream& s, int need) {
    uint32_t val = s.bitBuf;
    while (s.bitCnt < need) {
        if (s.pos >= s.size) {
            s.bad = true;
            return 0;
        }
        val |= static_cast<uint32_t>(s.in[s.pos++]) << s.bitCnt;
        s.bitCnt += 8;
    }
    s.bitBuf = val >> need;
    s.bitCnt -= need;
    return val & ((1u << need) - 1u);
}

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[kFixLCodes];
};

// Builds the decoding tables for `n` code lengths. Zero when the code is complete, negative when it is
// over-subscribed, positive when it is incomplete.
int Construct(Huffman& h, const short* length, int n) {
    for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
    for (int symbol = 0; symbol < n; ++symbol) ++h.count[length[symbol]];
    if (h.count[0] == n) return 0;   // no codes: complete, though decoding with it fails
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
    for (int symbol = 0; symbol < n; ++symbol) {
        if (length[symbol] != 0) h.symbol[offs[length[symbol]]++] = static_cast<short>(symbol);
    }
    return left;
}

// The next symbol, or a negative number on a bad code or the end of the data.
int Decode(Stream& s, const Huffman& h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= static_cast<int>(Bits(s, 1));
        if (s.bad) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

bool Emit(Stream& s, uint8_t byte) {
    if (s.out->size() >= s.maxOut) return false;
    s.out->push_back(byte);
    return true;
}

bool Codes(Stream& s, const Huffman& lencode, const Huffman& distcode) {
    static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
                                    16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                   7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int symbol = Decode(s, lencode);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (!Emit(s, static_cast<uint8_t>(symbol))) return false;
        } else if (symbol == 256) {
            return true;
        } else {
            symbol -= 257;
            if (symbol >= 29) return false;
            const int len = lens[symbol] + static_cast<int>(Bits(s, lext[symbol]));
            const int dsym = Decode(s, distcode);
            if (dsym < 0 || dsym >= 30) return false;
            const size_t dist = static_cast<size_t>(dists[dsym]) + Bits(s, dext[dsym]);
            if (s.bad || dist > s.out->size()) return false;
            for (int i = 0; i < len; ++i) {
                if (!Emit(s, (*s.out)[s.out->size() - dist])) return false;
            }
        }
        if (s.bad) return false;
    }
}

bool Stored(Stream& s) {
    s.bitBuf = 0;   // drop the rest of the current byte
    s.bitCnt = 0;
    if (s.pos + 4 > s.size) return false;
    const uint32_t len  = s.in[s.pos] | (static_cast<uint32_t>(s.in[s.pos + 1]) << 8);
    const uint32_t nlen = s.in[s.pos + 2] | (static_cast<uint32_t>(s.in[s.pos + 3]) << 8);
    s.pos += 4;
    if (len != (~nlen & 0xffffu) || s.pos + len > s.size) return false;
    for (uint32_t i = 0; i < len; ++i) {
        if (!Emit(s, s.in[s.pos + i])) return false;
    }
    s.pos += len;
    return true;
}

// the fixed codes of DEFLATE block type 1, built once on first use (the same for every stream)
bool g_fixedBuilt = false;
Huffman g_fixedLength;
Huffman g_fixedDistance;

bool Fixed(Stream& s) {
    Huffman& lencode = g_fixedLength;
    Huffman& distcode = g_fixedDistance;
    if (!g_fixedBuilt) {
        short lengths[kFixLCodes];
        int symbol = 0;
        for (; symbol < 144; ++symbol) lengths[symbol] = 8;
        for (; symbol < 256; ++symbol) lengths[symbol] = 9;
        for (; symbol < 280; ++symbol) lengths[symbol] = 7;
        for (; symbol < kFixLCodes; ++symbol) lengths[symbol] = 8;
        Construct(lencode, lengths, kFixLCodes);
        for (symbol = 0; symbol < kMaxDCodes; ++symbol) lengths[symbol] = 5;
        Construct(distcode, lengths, kMaxDCodes);
        g_fixedBuilt = true;
    }
    return Codes(s, lencode, distcode);
}

bool Dynamic(Stream& s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen  = static_cast<int>(Bits(s, 5)) + 257;
    const int ndist = static_cast<int>(Bits(s, 5)) + 1;
    const int ncode = static_cast<int>(Bits(s, 4)) + 4;
    if (s.bad || nlen > kMaxLCodes || ndist > kMaxDCodes) return false;
    short lengths[kMaxLCodes + kMaxDCodes];
    int index = 0;
    for (; index < ncode; ++index) lengths[order[index]] = static_cast<short>(Bits(s, 3));
    for (; index < 19; ++index) lengths[order[index]] = 0;
    if (s.bad) return false;
    Huffman lencode, distcode;
    if (Construct(lencode, lengths, 19) != 0) return false;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = Decode(s, lencode);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[index++] = static_cast<short>(symbol);
        } else {
            int len = 0;
            int repeat;
            if (symbol == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                repeat = 3 + static_cast<int>(Bits(s, 2));
            } else if (symbol == 17) {
                repeat = 3 + static_cast<int>(Bits(s, 3));
            } else {
                repeat = 11 + static_cast<int>(Bits(s, 7));
            }
            if (s.bad || index + repeat > nlen + ndist) return false;
            while (repeat-- > 0) lengths[index++] = static_cast<short>(len);
        }
    }
    if (lengths[256] == 0) return false;   // no end-of-block code
    int err = Construct(lencode, lengths, nlen);
    if (err != 0 && (err < 0 || nlen - lencode.count[0] != 1)) return false;
    err = Construct(distcode, lengths + nlen, ndist);
    if (err != 0 && (err < 0 || ndist - distcode.count[0] != 1)) return false;
    return Codes(s, lencode, distcode);
}

uint32_t Be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

int Paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

}  // namespace

bool Inflate(const uint8_t* data, size_t size, size_t maxOut, std::vector<uint8_t>& out) {
    out.clear();
    if (data == nullptr) return false;
    Stream s;
    s.in = data;
    s.size = size;
    s.out = &out;
    s.maxOut = maxOut;
    bool last;
    do {
        last = Bits(s, 1) != 0;
        const uint32_t type = Bits(s, 2);
        if (s.bad) break;
        bool ok = false;
        if (type == 0) ok = Stored(s);
        else if (type == 1) ok = Fixed(s);
        else if (type == 2) ok = Dynamic(s);
        if (!ok || s.bad) {
            out.clear();
            return false;
        }
    } while (!last);
    if (s.bad) {
        out.clear();
        return false;
    }
    return true;
}

bool ZlibInflate(const uint8_t* data, size_t size, size_t maxOut, std::vector<uint8_t>& out) {
    out.clear();
    if (data == nullptr || size < 2) return false;
    if ((data[0] & 0x0f) != 8 || ((static_cast<uint32_t>(data[0]) << 8) | data[1]) % 31u != 0 ||
        (data[1] & 0x20) != 0) return false;
    return Inflate(data + 2, size - 2, maxOut, out);
}

bool DecodePng(const uint8_t* data, size_t size, Image& out) {
    out = Image();
    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (data == nullptr || size < 8 || std::memcmp(data, kSignature, 8) != 0) return false;

    uint32_t width = 0;
    uint32_t height = 0;
    int depth = 0;
    int colorType = -1;
    bool haveHeader = false;
    bool sawEnd = false;
    std::vector<uint8_t> idat, palette, paletteAlpha;
    size_t pos = 8;
    while (pos + 8 <= size && !sawEnd) {
        const uint32_t len = Be32(data + pos);
        const uint8_t* type = data + pos + 4;
        // the data and the CRC must be there
        if (len > size - pos - 8 || size - pos - 8 - len < 4) return false;
        const uint8_t* body = data + pos + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13 || haveHeader) return false;
            width = Be32(body);
            height = Be32(body + 4);
            depth = body[8];
            colorType = body[9];
            // compression, filter, and no interlace
            if (body[10] != 0 || body[11] != 0 || body[12] != 0) return false;
            haveHeader = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            if (len % 3 != 0 || len > 768) return false;
            palette.assign(body, body + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (colorType == 3) paletteAlpha.assign(body, body + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            if (idat.size() + len > 8u * 1024u * 1024u) return false;
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            sawEnd = true;
        }
        pos += 12 + static_cast<size_t>(len);
    }
    if (!haveHeader || idat.empty()) return false;
    if (width == 0 || height == 0 || width > kMaxPngSide || height > kMaxPngSide ||
        static_cast<size_t>(width) * height > kMaxPngPixels) {
        return false;
    }
    int channels = 0;
    // Each color type allows its own bit depths: gray 1 to 16, palette 1 to 8, the others 8 and 16.
    const bool depthIsPowerOfTwo = depth == 1 || depth == 2 || depth == 4 || depth == 8;
    const bool byteDepth = depth == 8 || depth == 16;
    switch (colorType) {
        case 0:
            channels = 1;
            if (!depthIsPowerOfTwo && depth != 16) {
                return false;
            }
            break;
        case 2:
            channels = 3;
            if (!byteDepth) {
                return false;
            }
            break;
        case 3:
            channels = 1;
            if (!depthIsPowerOfTwo) {
                return false;
            }
            break;
        case 4:
            channels = 2;
            if (!byteDepth) {
                return false;
            }
            break;
        case 6:
            channels = 4;
            if (!byteDepth) {
                return false;
            }
            break;
        default:
            return false;
    }
    if (colorType == 3 && palette.empty()) return false;

    const size_t bitsPerPixel = static_cast<size_t>(channels) * static_cast<size_t>(depth);
    const size_t bpp = bitsPerPixel < 8 ? 1 : bitsPerPixel / 8;
    const size_t rowBytes = (static_cast<size_t>(width) * bitsPerPixel + 7) / 8;
    const size_t rawSize = (rowBytes + 1) * height;
    std::vector<uint8_t> raw;
    if (!ZlibInflate(idat.data(), idat.size(), rawSize, raw) || raw.size() != rawSize) return false;

    // Undo the filters, in place: each row's bytes follow its filter byte.
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = raw.data() + static_cast<size_t>(y) * (rowBytes + 1);
        const uint8_t filter = row[0];
        uint8_t* cur = row + 1;
        // the previous row's bytes (its filter byte is just before cur)
        const uint8_t* prev = y > 0 ? row - rowBytes : nullptr;
        if (filter > 4) return false;
        for (size_t i = 0; i < rowBytes; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0;
            const int b = prev != nullptr ? prev[i] : 0;
            const int c = (prev != nullptr && i >= bpp) ? prev[i - bpp] : 0;
            int add = 0;
            switch (filter) {
                case 1:
                    add = a;
                    break;
                case 2:
                    add = b;
                    break;
                case 3:
                    add = (a + b) / 2;
                    break;
                case 4:
                    add = Paeth(a, b, c);
                    break;
                default:
                    break;
            }
            cur[i] = static_cast<uint8_t>(cur[i] + add);
        }
    }

    out.width = width;
    out.height = height;
    out.bgra.assign(static_cast<size_t>(width) * height * 4u, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = raw.data() + static_cast<size_t>(y) * (rowBytes + 1) + 1;
        uint8_t* dst = out.bgra.data() + static_cast<size_t>(y) * width * 4u;
        // The sample `index` (a pixel's channel counted along the row), scaled to 8 bits for depths 1 to
        // 8 and 16.
        auto sample = [&](size_t index) -> int {
            if (depth == 8) return row[index];
            if (depth == 16) return row[index * 2];
            const size_t bit = index * static_cast<size_t>(depth);
            const int shift = 8 - depth - static_cast<int>(bit % 8);
            return (row[bit / 8] >> shift) & ((1 << depth) - 1);
        };
        for (uint32_t x = 0; x < width; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 255;
            const size_t base = static_cast<size_t>(x) * static_cast<size_t>(channels);
            if (colorType == 3) {
                const size_t entry = static_cast<size_t>(sample(base));
                if (entry * 3 + 2 >= palette.size()) {
                    out = Image();
                    return false;
                }
                r = palette[entry * 3];
                g = palette[entry * 3 + 1];
                b = palette[entry * 3 + 2];
                if (entry < paletteAlpha.size()) a = paletteAlpha[entry];
            } else if (colorType == 0 || colorType == 4) {
                int v = sample(base);
                if (colorType == 0 && depth < 8) v = v * 255 / ((1 << depth) - 1);
                r = g = b = v;
                if (colorType == 4) a = sample(base + 1);
            } else {
                r = sample(base);
                g = sample(base + 1);
                b = sample(base + 2);
                if (colorType == 6) a = sample(base + 3);
            }
            dst[x * 4 + 0] = static_cast<uint8_t>(b);
            dst[x * 4 + 1] = static_cast<uint8_t>(g);
            dst[x * 4 + 2] = static_cast<uint8_t>(r);
            dst[x * 4 + 3] = static_cast<uint8_t>(a);
        }
    }
    return true;
}

Image FitCover(const Image& source, uint32_t width, uint32_t height) {
    Image out;
    if (source.width == 0 || source.height == 0 || width == 0 || height == 0 ||
        source.bgra.size() != static_cast<size_t>(source.width) * source.height * 4u ||
        static_cast<size_t>(width) * height > kMaxPngPixels) {
        return out;
    }
    // The part of the source that covers the frame: the frame's aspect, as large as fits, centered.
    const double sw = source.width;
    const double sh = source.height;
    const double scaleX = static_cast<double>(width) / sw;
    const double scaleY = static_cast<double>(height) / sh;
    const double scale = scaleX > scaleY ? scaleX : scaleY;
    const double cropW = width / scale;
    const double cropH = height / scale;
    const double x0 = (sw - cropW) / 2.0;
    const double y0 = (sh - cropH) / 2.0;
    out.width = width;
    out.height = height;
    out.bgra.resize(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        double fy = y0 + (y + 0.5) * cropH / height - 0.5;
        if (fy < 0.0) fy = 0.0;
        if (fy > sh - 1.0) fy = sh - 1.0;
        const uint32_t iy = static_cast<uint32_t>(fy);
        const uint32_t iy1 = iy + 1 < source.height ? iy + 1 : iy;
        const double ty = fy - iy;
        for (uint32_t x = 0; x < width; ++x) {
            double fx = x0 + (x + 0.5) * cropW / width - 0.5;
            if (fx < 0.0) fx = 0.0;
            if (fx > sw - 1.0) fx = sw - 1.0;
            const uint32_t ix = static_cast<uint32_t>(fx);
            const uint32_t ix1 = ix + 1 < source.width ? ix + 1 : ix;
            const double tx = fx - ix;
            for (int c = 0; c < 4; ++c) {
                const auto at = [&](uint32_t px, uint32_t py) {
                    const size_t at4 = (static_cast<size_t>(py) * source.width + px) * 4u;
                    return static_cast<double>(source.bgra[at4 + c]);
                };
                const double top = at(ix, iy) * (1.0 - tx) + at(ix1, iy) * tx;
                const double bottom = at(ix, iy1) * (1.0 - tx) + at(ix1, iy1) * tx;
                const double v = top * (1.0 - ty) + bottom * ty;
                out.bgra[(static_cast<size_t>(y) * width + x) * 4u + c] = static_cast<uint8_t>(v + 0.5);
            }
        }
    }
    return out;
}

}  // namespace CustomMaps
}  // namespace Ext
