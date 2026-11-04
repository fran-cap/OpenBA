// ============================================================================
// src/render/common/create_on_demand/stream_locate.cpp -- ext.create_on_demand: locate a payload in
// its package (docs/extensions/handins/CREATEONDEMAND.md). The contract is in stream_locate.h.
// ============================================================================
// NOT RETAIL.
// ============================================================================

#include "create_on_demand/stream_locate.h"

#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "io/Stream.h"

#include <cstring>

namespace CreateOnDemand {

AltLocateFn g_altLocate = nullptr;
AltSkipFn   g_altSkip   = nullptr;

bool Locate(IO::Stream* stream, int32_t len, Locator* out) {
    IO::CompressedStream* const cs = dynamic_cast<IO::CompressedStream*>(stream);
    if (cs == nullptr) return g_altLocate != nullptr && g_altLocate(stream, len, out);
    if (cs->GetDirection() != IO::kReading || len < 0) return false;
    IO::FileStream* const fs = dynamic_cast<IO::FileStream*>(cs->_source);
    IO::FileSystem* const sys = IO::FileSystem::_singleton;
    if (fs == nullptr || sys == nullptr) return false;
    Platform::FileHandle handle = nullptr;
    int64_t entry = 0;
    if (!sys->PackedEntryOrigin(fs->_file, &handle, &entry)) return false;
    const int32_t size = fs->_file.size;
    const int32_t n    = cs->_headerSize;
    if (size <= 0 || n <= 0) return false;

    PackageMap* const map = Maps().Acquire(handle);
    if (map == nullptr) return false;
    Locator loc;
    loc.map   = map;   // the reference is the Locator's from here on
    loc.entry = static_cast<uint64_t>(entry);
    loc.size  = static_cast<uint32_t>(size);
    loc.pos   = cs->_position;
    loc.len   = len;
    if (loc.entry + loc.size > map->map.size) return false;
    // The mapped entry is the stream being read: the same block count and the same directory.
    const uint8_t* const bytes = map->map.data + loc.entry;
    int32_t count = 0;
    std::memcpy(&count, bytes, 4);
    if (count != n || 4 + static_cast<uint64_t>(n) * 8 > loc.size) return false;
    if (std::memcmp(bytes + 4, cs->_headerData, static_cast<size_t>(n) * 8) != 0) return false;
    if (!RangeInStream(bytes, loc.size, loc.pos, loc.len)) return false;
    *out = std::move(loc);
    return true;
}

void Skip(IO::Stream* stream, int32_t len) {
    if (g_altSkip != nullptr && g_altSkip(stream, len)) return;
    IO::CompressedStream* const cs = static_cast<IO::CompressedStream*>(stream);
    cs->_position += len;
}

bool SkipIfWithin(IO::Stream* stream, int32_t len) {
    if (len < 0) return false;
    if (g_altSkip != nullptr && g_altSkip(stream, len)) return true;
    IO::CompressedStream* const cs = dynamic_cast<IO::CompressedStream*>(stream);
    if (cs == nullptr || cs->GetDirection() != IO::kReading) return false;
    const int32_t n = cs->_headerSize;
    if (n <= 0 || cs->_headerData == nullptr) return false;
    // Every block but the last holds a full 32 KB (RangeInStream's reading of the same directory, and
    // its bound: no block holds more).
    const int32_t lastSize = cs->_headerData[n - 1].uncompressedSize;
    if (lastSize > kBlockBytes) return false;
    const int64_t length = static_cast<int64_t>(n - 1) * kBlockBytes + lastSize;
    if (static_cast<int64_t>(cs->_position) + len > length) return false;
    cs->_position += len;
    return true;
}

}  // namespace CreateOnDemand
