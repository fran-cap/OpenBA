// ============================================================================
// src/ext/custom_maps/bmap.cpp -- ext.custom_maps (class A), the .bmap file
// ============================================================================
// See bmap.h. Standard C++ only; no engine header.
// ============================================================================
#include "ext/custom_maps/bmap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace Ext {
namespace CustomMaps {

namespace {

constexpr uint32_t kMagic = 0x50414d42u;   // "BMAP" read as a little endian u32

constexpr uint32_t kFlagStart     = 1u;
constexpr uint32_t kFlagResources = 2u;
constexpr uint32_t kFlagMods      = 4u;
constexpr uint32_t kFlagProvenance = 8u;

// built once, on first use; the same for every game (not town state)
uint32_t g_crcTable[256];
bool g_crcTableBuilt = false;

const uint32_t* CrcTable() {
    uint32_t* const table = g_crcTable;
    if (!g_crcTableBuilt) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        g_crcTableBuilt = true;
    }
    return table;
}

// ---- SHA-256 (FIPS 180-4), for the content ID ----
class Sha256 {
public:
    Sha256() {
        static const uint32_t init[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::memcpy(m_h, init, sizeof(m_h));
    }

    void Add(const void* data, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        m_length += size;
        while (size != 0) {
            const size_t take = (64 - m_fill < size) ? 64 - m_fill : size;
            std::memcpy(m_block + m_fill, p, take);
            m_fill += take;
            p += take;
            size -= take;
            if (m_fill == 64) {
                Block();
                m_fill = 0;
            }
        }
    }

    void U16(uint16_t v) {
        const uint8_t b[2] = {static_cast<uint8_t>(v & 0xffu), static_cast<uint8_t>(v >> 8)};
        Add(b, 2);
    }

    void U32(uint32_t v) {
        const uint8_t b[4] = {static_cast<uint8_t>(v & 0xffu), static_cast<uint8_t>((v >> 8) & 0xffu),
                              static_cast<uint8_t>((v >> 16) & 0xffu), static_cast<uint8_t>(v >> 24)};
        Add(b, 4);
    }

    void Finish(uint8_t out[32]) {
        const uint64_t bits = m_length * 8u;
        const uint8_t  one  = 0x80;
        Add(&one, 1);
        const uint8_t zero = 0;
        while (m_fill != 56) {
            Add(&zero, 1);
        }
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) {
            len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        }
        Add(len, 8);
        for (int i = 0; i < 8; ++i) {
            out[i * 4]     = static_cast<uint8_t>(m_h[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(m_h[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(m_h[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(m_h[i]);
        }
    }

private:
    static uint32_t Rotr(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    void Block() {
        static const uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
            0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
            0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
            0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
            0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
            0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
            0xc67178f2u};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* q = m_block + i * 4;
            w[i] = (static_cast<uint32_t>(q[0]) << 24) | (static_cast<uint32_t>(q[1]) << 16) |
                   (static_cast<uint32_t>(q[2]) << 8) | static_cast<uint32_t>(q[3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i]              = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = m_h[0];
        uint32_t b = m_h[1];
        uint32_t c = m_h[2];
        uint32_t d = m_h[3];
        uint32_t e = m_h[4];
        uint32_t f = m_h[5];
        uint32_t g = m_h[6];
        uint32_t h = m_h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t sum1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            const uint32_t t1 = h + sum1 + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        m_h[0] += a;
        m_h[1] += b;
        m_h[2] += c;
        m_h[3] += d;
        m_h[4] += e;
        m_h[5] += f;
        m_h[6] += g;
        m_h[7] += h;
    }

    uint32_t m_h[8];
    uint8_t  m_block[64];
    size_t   m_fill   = 0;
    uint64_t m_length = 0;
};

void AddF32(Sha256& sha, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    sha.U32(bits);
}

// A bounds-checked cursor over the file image. Every read reports whether the bytes were there.
class Reader {
public:
    Reader(const uint8_t* data, size_t size)
        : m_data(data)
        , m_size(size) {
    }

    size_t Left() const {
        return m_size - m_pos;
    }

    bool Bytes(void* out, size_t count) {
        if (count > Left()) {
            return false;
        }
        if (count != 0) {
            std::memcpy(out, m_data + m_pos, count);
        }
        m_pos += count;
        return true;
    }

    bool U8(uint8_t& v) {
        return Bytes(&v, 1);
    }

    bool U16(uint16_t& v) {
        uint8_t b[2];
        if (!Bytes(b, 2)) {
            return false;
        }
        v = static_cast<uint16_t>(b[0] | (b[1] << 8));
        return true;
    }

    bool U32(uint32_t& v) {
        uint8_t b[4];
        if (!Bytes(b, 4)) {
            return false;
        }
        v = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
            (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }

    bool I64(int64_t& v) {
        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!U32(lo) || !U32(hi)) {
            return false;
        }
        v = static_cast<int64_t>((static_cast<uint64_t>(hi) << 32) | lo);
        return true;
    }

    bool F32(float& v) {
        uint32_t bits = 0;
        if (!U32(bits)) {
            return false;
        }
        std::memcpy(&v, &bits, sizeof(v));
        return true;
    }

    // A u16 length and that many bytes, refused over `cap`.
    bool Text(std::string& out, size_t cap, Status& why) {
        uint16_t length = 0;
        if (!U16(length)) {
            why = Status::Truncated;
            return false;
        }
        if (length > cap) {
            why = Status::BadText;
            return false;
        }
        if (length > Left()) {
            why = Status::Truncated;
            return false;
        }
        out.assign(reinterpret_cast<const char*>(m_data + m_pos), length);
        m_pos += length;
        return true;
    }

private:
    const uint8_t* m_data;
    size_t         m_size;
    size_t         m_pos = 0;
};

class Writer {
public:
    void U8(uint8_t v) {
        m_out.push_back(v);
    }

    void U16(uint16_t v) {
        m_out.push_back(static_cast<uint8_t>(v & 0xffu));
        m_out.push_back(static_cast<uint8_t>(v >> 8));
    }

    void U32(uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) {
            m_out.push_back(static_cast<uint8_t>((v >> shift) & 0xffu));
        }
    }

    void F32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }

    void Text(const std::string& s) {
        U16(static_cast<uint16_t>(s.size()));
        m_out.insert(m_out.end(), s.begin(), s.end());
    }

    void Bytes(const uint8_t* data, size_t count) {
        m_out.insert(m_out.end(), data, data + count);
    }

    std::vector<uint8_t>& Out() {
        return m_out;
    }

private:
    std::vector<uint8_t> m_out;
};

bool SizeAllowed(uint32_t mapSize) {
    return mapSize >= kMinMapSize && mapSize <= kMaxMapSize && (mapSize % 8u) == 0;
}

bool TemplatePathOk(const std::string& path) {
    if (path.empty() || path.size() > kMaxTemplatePath) {
        return false;
    }
    for (char c : path) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == ' ' || c == '.' || c == '/' || c == '\\' || c == '-' || c == ':';
        if (!ok) {
            return false;
        }
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    const size_t n = path.size();
    if (n < 5 || path[n - 5] == '/' || path[n - 5] == '\\') {   // there must be a name before ".rsc"
        return false;
    }
    const char* tail = path.c_str() + n - 4;
    const bool  rsc  = (tail[0] == '.') && (tail[1] == 'r' || tail[1] == 'R') &&
                       (tail[2] == 's' || tail[2] == 'S') && (tail[3] == 'c' || tail[3] == 'C');
    return rsc;
}

}  // namespace

const char* StatusText(Status status) {
    switch (status) {
    case Status::Ok:
        return "ok";
    case Status::TooShort:
        return "the file is too short to be a map";
    case Status::BadMagic:
        return "not a map file (wrong signature)";
    case Status::BadVersion:
        return "a map file version this game does not know";
    case Status::BadSize:
        return "the map size is not allowed";
    case Status::BadText:
        return "a name or note in the file is too long";
    case Status::Truncated:
        return "the file ends in the middle of a section";
    case Status::TooMany:
        return "the file lists more items than a map may hold";
    case Status::BadCrc:
        return "the file is damaged (checksum mismatch)";
    case Status::TrailingBytes:
        return "the file has data after its checksum";
    case Status::OutOfMemory:
        return "not enough memory to read the map";
    }
    return "unknown";
}

uint32_t Crc32(const uint8_t* data, size_t size) {
    const uint32_t* table = CrcTable();
    uint32_t        c     = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        c = table[(c ^ data[i]) & 0xffu] ^ (c >> 8);
    }
    return c ^ 0xffffffffu;
}

namespace {

Status ParseInto(const uint8_t* data, size_t size, Map& map) {
    if (size < 16) {
        return Status::TooShort;
    }
    Reader   r(data, size);
    uint32_t magic   = 0;
    uint32_t version = 0;
    r.U32(magic);
    if (magic != kMagic) {
        return Status::BadMagic;
    }
    r.U32(version);
    if (version != kVersion) {
        return Status::BadVersion;
    }
    uint32_t flags = 0;
    if (!r.U32(map.mapSize) || !r.U32(flags)) {
        return Status::Truncated;
    }
    if (!SizeAllowed(map.mapSize)) {
        return Status::BadSize;
    }
    Status why = Status::Ok;
    if (!r.Text(map.name, kMaxNameLength, why) || !r.Text(map.author, kMaxNameLength, why) ||
        !r.Text(map.note, kMaxNoteLength, why)) {
        return why;
    }

    // The checksum covers everything before it, so check it before trusting the long sections.
    if (size < 4) {
        return Status::TooShort;
    }
    const size_t body = size - 4;
    uint32_t     stored = 0;
    {
        Reader tail(data + body, 4);
        tail.U32(stored);
    }
    if (Crc32(data, body) != stored) {
        return Status::BadCrc;
    }

    const size_t heightCount = HeightCount(map.mapSize);
    if (heightCount * sizeof(int16_t) > r.Left()) {
        return Status::Truncated;
    }
    map.heights.resize(heightCount);
    for (size_t i = 0; i < heightCount; ++i) {
        uint16_t raw = 0;
        r.U16(raw);
        map.heights[i] = static_cast<int16_t>(raw);
    }

    uint32_t pathCount = 0;
    if (!r.U32(pathCount)) {
        return Status::Truncated;
    }
    if (pathCount > kMaxPaths) {
        return Status::TooMany;
    }
    map.paths.resize(pathCount);
    for (uint32_t p = 0; p < pathCount; ++p) {
        uint32_t pointCount = 0;
        if (!r.U32(pointCount)) {
            return Status::Truncated;
        }
        if (pointCount < kMinPathPoints || pointCount > kMaxPathPoints) {
            return Status::TooMany;
        }
        if (static_cast<size_t>(pointCount) * 12u > r.Left()) {
            return Status::Truncated;
        }
        map.paths[p].points.resize(pointCount);
        for (uint32_t i = 0; i < pointCount; ++i) {
            PathPoint& pt = map.paths[p].points[i];
            r.F32(pt.x);
            r.F32(pt.y);
            r.F32(pt.halfWidth);
        }
    }

    if ((flags & kFlagResources) != 0) {
        map.hasResources = true;
        uint32_t resourceCount = 0;
        if (!r.U32(resourceCount)) {
            return Status::Truncated;
        }
        if (resourceCount > kMaxResources) {
            return Status::TooMany;
        }
        // Each entry is at least 8 bytes, so a count the remaining data cannot hold is a lie.
        if (static_cast<size_t>(resourceCount) * 8u > r.Left()) {
            return Status::Truncated;
        }
        map.resources.resize(resourceCount);
        for (uint32_t i = 0; i < resourceCount; ++i) {
            Resource& res = map.resources[i];
            if (!r.Text(res.templatePath, kMaxTemplatePath, why)) {
                return why;
            }
            if (!r.U16(res.cellX) || !r.U16(res.cellY) || !r.U8(res.variant) || !r.U8(res.growth)) {
                return Status::Truncated;
            }
        }
    }

    if ((flags & kFlagMods) != 0) {
        uint16_t modCount = 0;
        if (!r.U16(modCount)) {
            return Status::Truncated;
        }
        if (modCount > kMaxMods) {
            return Status::TooMany;
        }
        // Each entry is at least 6 bytes (two empty texts and a count).
        if (static_cast<size_t>(modCount) * 6u > r.Left()) {
            return Status::Truncated;
        }
        map.mods.resize(modCount);
        for (uint16_t i = 0; i < modCount; ++i) {
            ModRef& mod = map.mods[i];
            if (!r.Text(mod.name, kMaxNameLength, why) || !r.Text(mod.author, kMaxNameLength, why)) {
                return why;
            }
            uint16_t templateCount = 0;
            if (!r.U16(templateCount)) {
                return Status::Truncated;
            }
            if (templateCount > kMaxModTemplates) {
                return Status::TooMany;
            }
            if (static_cast<size_t>(templateCount) * 2u > r.Left()) {
                return Status::Truncated;
            }
            mod.templates.resize(templateCount);
            for (uint16_t t = 0; t < templateCount; ++t) {
                if (!r.Text(mod.templates[t], kMaxTemplatePath, why)) {
                    return why;
                }
            }
        }
    }

    if ((flags & kFlagStart) != 0) {
        map.hasStart = true;
        if (!r.U16(map.startX) || !r.U16(map.startY)) {
            return Status::Truncated;
        }
    }

    if ((flags & kFlagProvenance) != 0) {
        map.hasProvenance = true;
        Provenance& pv    = map.provenance;
        if (!r.Bytes(pv.contentId, 32) || !r.Bytes(pv.originId, 32) || !r.Bytes(pv.parentId, 32) ||
            !r.I64(pv.created) ||
            !r.I64(pv.saved)) {
            return Status::Truncated;
        }
    }

    uint32_t thumbnailLength = 0;
    if (!r.U32(thumbnailLength)) {
        return Status::Truncated;
    }
    if (thumbnailLength > kMaxThumbnail) {
        return Status::TooMany;
    }
    if (thumbnailLength > r.Left()) {
        return Status::Truncated;
    }
    map.thumbnail.resize(thumbnailLength);
    r.Bytes(map.thumbnail.data(), thumbnailLength);

    // Only the checksum may remain.
    if (r.Left() != 4) {
        return r.Left() < 4 ? Status::Truncated : Status::TrailingBytes;
    }
    return Status::Ok;
}

}  // namespace

Status Parse(const uint8_t* data, size_t size, Map& out) {
    out = Map();
    if (data == nullptr) {
        return Status::TooShort;
    }
    Map    parsed;
    Status status = Status::Ok;
    try {
        status = ParseInto(data, size, parsed);
    } catch (const std::bad_alloc&) {
        status = Status::OutOfMemory;
    }
    if (status == Status::Ok) {
        out = std::move(parsed);
    }
    return status;
}

std::vector<uint8_t> Write(const Map& map) {
    if (!SizeAllowed(map.mapSize) || map.heights.size() != HeightCount(map.mapSize) ||
        map.name.size() > kMaxNameLength || map.author.size() > kMaxNameLength ||
        map.note.size() > kMaxNoteLength || map.paths.size() > kMaxPaths ||
        map.resources.size() > kMaxResources || map.thumbnail.size() > kMaxThumbnail ||
        map.mods.size() > kMaxMods) {
        return std::vector<uint8_t>();
    }
    for (const Path& path : map.paths) {
        if (path.points.size() < kMinPathPoints || path.points.size() > kMaxPathPoints) {
            return std::vector<uint8_t>();
        }
    }
    for (const Resource& res : map.resources) {
        if (res.templatePath.size() > kMaxTemplatePath) {
            return std::vector<uint8_t>();
        }
    }

    for (const ModRef& mod : map.mods) {
        if (mod.name.size() > kMaxNameLength || mod.author.size() > kMaxNameLength ||
            mod.templates.size() > kMaxModTemplates) {
            return std::vector<uint8_t>();
        }
        for (const std::string& path : mod.templates) {
            if (path.size() > kMaxTemplatePath) {
                return std::vector<uint8_t>();
            }
        }
    }

    Writer   w;
    uint32_t flags = 0;
    if (map.hasStart) {
        flags |= kFlagStart;
    }
    if (map.hasResources) {
        flags |= kFlagResources;
    }
    if (!map.mods.empty()) {
        flags |= kFlagMods;
    }
    if (map.hasProvenance) {
        flags |= kFlagProvenance;
    }
    w.U32(kMagic);
    w.U32(kVersion);
    w.U32(map.mapSize);
    w.U32(flags);
    w.Text(map.name);
    w.Text(map.author);
    w.Text(map.note);
    for (int16_t h : map.heights) {
        w.U16(static_cast<uint16_t>(h));
    }
    w.U32(static_cast<uint32_t>(map.paths.size()));
    for (const Path& path : map.paths) {
        w.U32(static_cast<uint32_t>(path.points.size()));
        for (const PathPoint& pt : path.points) {
            w.F32(pt.x);
            w.F32(pt.y);
            w.F32(pt.halfWidth);
        }
    }
    if (map.hasResources) {
        w.U32(static_cast<uint32_t>(map.resources.size()));
        for (const Resource& res : map.resources) {
            w.Text(res.templatePath);
            w.U16(res.cellX);
            w.U16(res.cellY);
            w.U8(res.variant);
            w.U8(res.growth);
        }
    }
    if (!map.mods.empty()) {
        w.U16(static_cast<uint16_t>(map.mods.size()));
        for (const ModRef& mod : map.mods) {
            w.Text(mod.name);
            w.Text(mod.author);
            w.U16(static_cast<uint16_t>(mod.templates.size()));
            for (const std::string& path : mod.templates) {
                w.Text(path);
            }
        }
    }
    if (map.hasStart) {
        w.U16(map.startX);
        w.U16(map.startY);
    }
    if (map.hasProvenance) {
        const Provenance& pv = map.provenance;
        w.Bytes(pv.contentId, 32);
        w.Bytes(pv.originId, 32);
        w.Bytes(pv.parentId, 32);
        w.U32(static_cast<uint32_t>(static_cast<uint64_t>(pv.created) & 0xffffffffu));
        w.U32(static_cast<uint32_t>(static_cast<uint64_t>(pv.created) >> 32));
        w.U32(static_cast<uint32_t>(static_cast<uint64_t>(pv.saved) & 0xffffffffu));
        w.U32(static_cast<uint32_t>(static_cast<uint64_t>(pv.saved) >> 32));
    }
    w.U32(static_cast<uint32_t>(map.thumbnail.size()));
    w.Bytes(map.thumbnail.data(), map.thumbnail.size());
    w.U32(Crc32(w.Out().data(), w.Out().size()));
    return std::move(w.Out());
}

void ContentId(const Map& map, uint8_t out[32]) {
    static const char terrainTag[16] = {'b', 'm', 'a', 'p', '-', 't', 'e', 'r', 'r', 'a', 'i', 'n', '-', 'v',
                                        '1', '\0'};
    static const char mapTag[12]     = {'b', 'm', 'a', 'p', '-', 'm', 'a', 'p', '-', 'v', '1', '\0'};
    uint8_t           terrain[32];
    {
        Sha256 sha;
        sha.Add(terrainTag, sizeof(terrainTag));
        sha.U32(map.mapSize);
        for (int16_t h : map.heights) {
            sha.U16(static_cast<uint16_t>(h));
        }
        sha.Finish(terrain);
    }
    Sha256 sha;
    sha.Add(mapTag, sizeof(mapTag));
    sha.Add(terrain, 32);
    sha.U32(static_cast<uint32_t>(map.paths.size()));
    for (const Path& path : map.paths) {
        sha.U32(static_cast<uint32_t>(path.points.size()));
        for (const PathPoint& pt : path.points) {
            AddF32(sha, pt.x);
            AddF32(sha, pt.y);
            AddF32(sha, pt.halfWidth);
        }
    }
    std::vector<const Resource*> sorted;
    for (const Resource& res : map.resources) {
        sorted.push_back(&res);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Resource* a, const Resource* b) {
        const int c = a->templatePath.compare(b->templatePath);
        if (c != 0) {
            return c < 0;
        }
        if (a->cellY != b->cellY) {
            return a->cellY < b->cellY;
        }
        return a->cellX < b->cellX;
    });
    sha.U32(static_cast<uint32_t>(sorted.size()));
    for (const Resource* res : sorted) {
        sha.U16(static_cast<uint16_t>(res->templatePath.size()));
        sha.Add(res->templatePath.data(), res->templatePath.size());
        sha.U16(res->cellX);
        sha.U16(res->cellY);
        const uint8_t tail[2] = {res->variant, res->growth};
        sha.Add(tail, 2);
    }
    if (map.hasStart) {
        const uint8_t one = 1;
        sha.Add(&one, 1);
        sha.U16(map.startX);
        sha.U16(map.startY);
    } else {
        const uint8_t zero = 0;
        sha.Add(&zero, 1);
    }
    sha.Finish(out);
}

std::string IdHex(const uint8_t id[32]) {
    static const char digits[] = "0123456789abcdef";
    std::string       s;
    for (int i = 0; i < 32; ++i) {
        s.push_back(digits[id[i] >> 4]);
        s.push_back(digits[id[i] & 15]);
    }
    return s;
}

bool BoatRouteExists(const float* xy, size_t count, size_t strideFloats, uint32_t mapSize) {
    if (xy == nullptr || count < 2 || strideFloats < 2) return false;
    const float half = static_cast<float>(mapSize) * 0.5f;
    for (size_t i = 0; i < count; ++i) {
        const float x = xy[i * strideFloats];
        const float y = xy[i * strideFloats + 1];
        // the route builder's own tile test
        if (x > -half - 1.0f && x < half && y > -half - 1.0f && y < half) return true;
    }
    return false;
}

std::vector<Issue> Validate(const Map& map) {
    std::vector<Issue> issues;
    auto add = [&issues](bool error, const std::string& text) {
        Issue issue;
        issue.error = error;
        issue.text  = text;
        issues.push_back(issue);
    };

    if (!SizeAllowed(map.mapSize)) {
        add(true, "the map size " + std::to_string(map.mapSize) + " is not allowed");
        return issues;
    }
    if (map.heights.size() != HeightCount(map.mapSize)) {
        add(true, "the height list does not match the map size");
        return issues;
    }

    const int side  = static_cast<int>(GridVertices(map.mapSize));
    const int quart = static_cast<int>(map.mapSize) / 2;
    const int lo    = quart;
    const int hi    = quart + static_cast<int>(map.mapSize);
    size_t    land  = 0;
    for (int row = lo; row <= hi; ++row) {
        for (int col = lo; col <= hi; ++col) {
            if (map.heights[static_cast<size_t>(row) * side + col] > kWaterLevel) {
                ++land;
            }
        }
    }
    const size_t playable = static_cast<size_t>(map.mapSize + 1) * static_cast<size_t>(map.mapSize + 1);
    if (land * 100 < playable) {
        add(true, "less than a hundredth of the play area is above the water");
    }

    const float limit = static_cast<float>(map.mapSize);
    const float half  = limit * 0.5f;
    for (size_t p = 0; p < map.paths.size(); ++p) {
        for (const PathPoint& pt : map.paths[p].points) {
            const bool finite = std::isfinite(pt.x) && std::isfinite(pt.y) && std::isfinite(pt.halfWidth);
            if (!finite || std::fabs(pt.x) > limit || std::fabs(pt.y) > limit) {
                add(true, "a point of water path " + std::to_string(p) + " lies outside the map");
                return issues;
            }
            if (pt.halfWidth < kMinHalfWidth || pt.halfWidth > kMaxHalfWidth) {
                add(true, "water path " + std::to_string(p) + " has a width the game cannot draw");
                return issues;
            }
        }
    }
    if (map.paths.empty()) {
        add(false, "the map has no water path, so merchant boats cannot sail");
    } else {
        // The boats' route builder: points outside the play area, then inside it, then outside again.
        const std::vector<PathPoint>& pts = map.paths[0].points;
        auto inside = [half](const PathPoint& pt) {
            return pt.x > -half - 1.0f && pt.x < half && pt.y > -half - 1.0f && pt.y < half;
        };
        bool crosses = !inside(pts.front()) && !inside(pts.back());
        bool within  = false;
        for (const PathPoint& pt : pts) {
            within = within || inside(pt);
        }
        if (!crosses || !within) {
            add(false, "the first water path must start outside the play area, cross it and end outside, "
                       "so merchant boats cannot sail");
        }
    }

    if (map.hasProvenance) {
        uint8_t id[32];
        ContentId(map, id);
        if (std::memcmp(id, map.provenance.contentId, 32) != 0) {
            add(false,
                "the map was changed after its ID was written, so its ID does not match its content "
                "(a tool edited the file without updating it)");
        }
    }

    const int cells = static_cast<int>(map.mapSize);
    if (map.hasResources) {
        for (size_t i = 0; i < map.resources.size(); ++i) {
            const Resource& res = map.resources[i];
            if (!TemplatePathOk(res.templatePath)) {
                add(true, "resource " + std::to_string(i) + " names a template the game cannot use");
                return issues;
            }
            if (static_cast<int>(res.cellX) >= cells || static_cast<int>(res.cellY) >= cells) {
                add(true, "resource " + std::to_string(i) + " stands outside the play area");
                return issues;
            }
        }
    }
    for (size_t m = 0; m < map.mods.size(); ++m) {
        for (const std::string& path : map.mods[m].templates) {
            if (!TemplatePathOk(path)) {
                add(true, "mod " + std::to_string(m) + " of the mod list names a template the game cannot "
                           "use");
                return issues;
            }
        }
    }
    if (map.hasStart) {
        if (static_cast<int>(map.startX) >= cells || static_cast<int>(map.startY) >= cells) {
            add(true, "the start cell lies outside the play area");
        } else {
            const size_t at = static_cast<size_t>(map.startY + quart) * side + (map.startX + quart);
            if (map.heights[at] <= kWaterLevel) {
                add(true, "the start cell is under water");
            }
        }
    }
    return issues;
}

std::vector<ModCheck> CheckMods(const Map& map, const std::function<bool(const std::string&)>& resolves) {
    std::vector<ModCheck> out;
    for (const ModRef& mod : map.mods) {
        if (mod.templates.empty()) {
            continue;
        }
        ModCheck check;
        check.name  = mod.name.empty() ? std::string("(unnamed mod)") : mod.name;
        check.total = mod.templates.size();
        for (const std::string& path : mod.templates) {
            if (!resolves(path)) {
                ++check.missing;
            }
        }
        out.push_back(check);
    }
    return out;
}

std::vector<ModCheck> CheckModsInstalled(const Map& map,
                                         const std::function<bool(const ModRef&)>& installed) {
    std::vector<ModCheck> out;
    for (const ModRef& mod : map.mods) {
        ModCheck check;
        check.name  = mod.name.empty() ? std::string("(unnamed mod)") : mod.name;
        check.total = mod.templates.empty() ? 1 : mod.templates.size();
        if (!installed(mod)) {
            check.missing = check.total;
        }
        out.push_back(check);
    }
    return out;
}

std::string ModsLine(const std::vector<ModCheck>& checks) {
    std::vector<std::string> names;
    for (const ModCheck& check : checks) {
        if (check.missing == 0) {
            continue;
        }
        const bool all = check.missing == check.total;
        names.push_back(all ? check.name
                            : check.name + " (" + std::to_string(check.missing) + " of " +
                                  std::to_string(check.total) + " missing)");
    }
    if (names.empty()) {
        return std::string();
    }
    std::string line = "Needs: " + names[0];
    if (names.size() == 2) {
        line += ", " + names[1];
    } else if (names.size() > 2) {
        line += ", " + names[1] + " and " + std::to_string(names.size() - 2) + " more";
    }
    return line;
}

}  // namespace CustomMaps
}  // namespace Ext
