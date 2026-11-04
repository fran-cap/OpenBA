// ============================================================================
// src/render/common/create_on_demand/payload.cpp -- ext.create_on_demand's package mappings and
// payload re-decode (docs/extensions/handins/CREATEONDEMAND.md). The contract is in payload.h.
// ============================================================================
// NOT RETAIL.
// ============================================================================

#include "create_on_demand/payload.h"

#include "system/Compression.h"

#include <algorithm>
#include <cstring>

namespace CreateOnDemand {

namespace {

int32_t ReadI32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

struct BlockHeader {
    uint32_t sourceOffset;
    uint16_t compressedSize;   // 0 = stored
    uint16_t uncompressedSize;
};
static_assert(sizeof(BlockHeader) == 8, "RtIo::CompressedStreamHeader is 8 bytes");

}  // namespace

// ---- PackageMaps --------------------------------------------------------------------------------

PackageMap* PackageMaps::Acquire(Platform::FileHandle h) {
    if (h == nullptr || h == Platform::kInvalidFileHandle) return nullptr;
    std::lock_guard<std::mutex> lock(_mutex);
    for (PackageMap* m : _maps) {
        if (m->handle == h) {
            ++m->users;
            return m;
        }
    }
    PackageMap* const m = new PackageMap();
    if (!Platform::FileMapOpen(h, &m->map) || m->map.data == nullptr) {
        delete m;
        return nullptr;
    }
    m->handle = h;
    m->users  = 1;
    _maps.push_back(m);
    ++_opened;
    return m;
}

void PackageMaps::Release(PackageMap* m) {
    if (m == nullptr) return;
    std::lock_guard<std::mutex> lock(_mutex);
    if (--m->users > 0 || m->handle != nullptr) return;
    // Detached and unused: nothing can reach it any more.
    _maps.erase(std::find(_maps.begin(), _maps.end(), m));
    Platform::FileMapClose(&m->map);
    delete m;
}

void PackageMaps::Detach(Platform::FileHandle h) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (size_t i = 0; i < _maps.size(); ++i) {
        PackageMap* const m = _maps[i];
        if (m->handle != h) continue;
        m->handle = nullptr;
        if (m->users == 0) {
            _maps.erase(_maps.begin() + static_cast<std::ptrdiff_t>(i));
            Platform::FileMapClose(&m->map);
            delete m;
        }
        return;
    }
}

PackageMaps::Stats PackageMaps::GetStats() {
    std::lock_guard<std::mutex> lock(_mutex);
    Stats s;
    s.live   = static_cast<int64_t>(_maps.size());
    s.opened = _opened;
    for (const PackageMap* m : _maps) {
        if (m->handle == nullptr) ++s.detached;
    }
    return s;
}

PackageMaps::~PackageMaps() {
    for (PackageMap* m : _maps) {
        Platform::FileMapClose(&m->map);
        delete m;
    }
}

PackageMaps& Maps() {
    static PackageMaps maps;
    return maps;
}

// ---- Locator ------------------------------------------------------------------------------------

Locator::Locator(Locator&& other) noexcept
    : map(other.map), entry(other.entry), size(other.size), pos(other.pos), len(other.len) {
    other.map = nullptr;
}

Locator& Locator::operator=(Locator&& other) noexcept {
    if (this != &other) {
        Clear();
        map       = other.map;
        entry     = other.entry;
        size      = other.size;
        pos       = other.pos;
        len       = other.len;
        other.map = nullptr;
    }
    return *this;
}

Locator::~Locator() {
    Clear();
}

void Locator::Clear() {
    if (map != nullptr) Maps().Release(map);
    map = nullptr;
}

// ---- re-decode ----------------------------------------------------------------------------------

bool RangeInStream(const uint8_t* entry, uint64_t entrySize, int32_t pos, int32_t len) {
    if (entry == nullptr || entrySize < 4 || pos < 0 || len < 0) return false;
    const int32_t n = ReadI32(entry);
    if (n <= 0 || 4 + static_cast<uint64_t>(n) * 8 > entrySize) return false;
    // Every block but the last holds a full 32 KB, so the stream's length is the last block's end.
    BlockHeader last;
    std::memcpy(&last, entry + 4 + 8 * static_cast<uint64_t>(n - 1), 8);
    // A block never holds more than 32 KB: a larger last block would put the stream's end past the
    // directory's n blocks (hardening: the field is the package's).
    if (last.uncompressedSize > kBlockBytes) return false;
    const int64_t length = static_cast<int64_t>(n - 1) * kBlockBytes + last.uncompressedSize;
    return static_cast<int64_t>(pos) + len <= length;
}

bool RedecodeRange(const uint8_t* entry, uint64_t entrySize, int32_t pos, int32_t len, uint8_t* dst) {
    if (!RangeInStream(entry, entrySize, pos, len)) return false;
    if (len == 0) return true;
    const int32_t n = ReadI32(entry);
    uint8_t page[kBlockBytes];
    const int32_t first = pos >> 15;
    const int32_t last  = (pos + len - 1) >> 15;
    for (int32_t b = first; b <= last; ++b) {
        if (b >= n) return false;
        BlockHeader h;
        std::memcpy(&h, entry + 4 + 8 * static_cast<uint64_t>(b), 8);
        // The page holds one block: a stored block over 32 KB would overflow it (hardening).
        if (h.uncompressedSize > kBlockBytes) return false;
        const uint32_t stored = h.compressedSize != 0 ? h.compressedSize : h.uncompressedSize;
        if (static_cast<uint64_t>(h.sourceOffset) + stored > entrySize) return false;
        const uint8_t* const src = entry + h.sourceOffset;
        if (h.compressedSize != 0) {
            const int got = ::System::Compression::Decompress(src, h.compressedSize, page, kBlockBytes);
            if (got != h.uncompressedSize) return false;
        } else {
            std::memcpy(page, src, h.uncompressedSize);
        }
        const int32_t blockStart = b << 15;
        const int32_t from       = pos > blockStart ? pos - blockStart : 0;
        const int32_t to         = std::min<int32_t>(h.uncompressedSize, pos + len - blockStart);
        if (to < from) return false;
        std::memcpy(dst + (blockStart + from - pos), page + from, static_cast<size_t>(to - from));
    }
    return true;
}

bool Redecode(const Locator& loc, uint8_t* dst) {
    if (loc.map == nullptr || loc.entry + loc.size > loc.map->map.size) return false;
    return RedecodeRange(loc.map->map.data + loc.entry, loc.size, loc.pos, loc.len, dst);
}

}  // namespace CreateOnDemand
