// ============================================================================
// src/ext/stream_cache/segment_stream.cpp -- ext.stream_cache: the record-backed stream
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). The contract is in the header.
// ============================================================================
#include "ext/stream_cache/segment_stream.h"

#include "io/Serializer.h"
#include "system/string_internal.h"   // RtSystem::RtMemAlloc / RtMemFree: the encoding, as every stream makes it

namespace Ext {
namespace StreamCache {

SegmentStream::SegmentStream(const RecordView* record, Platform::FileHandle package, int64_t entry,
                             OpenFallbackFn openFallback, void* context)
    : IO::Stream(), _record(record), _package(package), _entry(entry), _openFallback(openFallback),
      _context(context) {
    // What every reading stream's constructor does (MemoryStream, CompressedStream): a
    // BinaryEncoding of its own, freed by ~Stream.
    void* encMem = RtSystem::RtMemAlloc(8, 4);
    IO::Encoding* enc = nullptr;
    if (encMem) enc = ::new (encMem) IO::BinaryEncoding();
    if (_encoding) RtSystem::RtMemFree(_encoding);
    _encoding  = enc;
    _direction = IO::kReading;
}

SegmentStream::~SegmentStream() = default;

bool SegmentStream::Write(int n, const void* src) {
    (void)n;
    (void)src;
    return false;
}

bool SegmentStream::EnterFallback() {
    IO::CompressedStream* const cs = _openFallback != nullptr ? _openFallback(*this, _context) : nullptr;
    if (cs == nullptr) {
        _failed = true;
        return false;
    }
    // Where the next read lands. The position alone: CompressedStream::Read starts every read with
    // SetWindow(_position), so the block is decoded exactly when retail would decode it.
    cs->_position = _pos;
    _fallback = cs;
    return true;
}

bool SegmentStream::Read(int n, void* dst) {
    if (_fallback != nullptr) return _fallback->Read(n, dst);
    if (n < 0) return false;
    if (n == 0) return true;
    if (_record->Read(static_cast<uint32_t>(_pos), static_cast<uint32_t>(n), static_cast<uint8_t*>(dst))) {
        _pos += n;
        _consumed += static_cast<uint32_t>(n);
        return true;
    }
    if (!EnterFallback()) return false;
    return _fallback->Read(n, dst);
}

bool SegmentStream::SetCurrentPos(int pos) {
    if (_fallback != nullptr) return _fallback->SetCurrentPos(pos);
    _pos = pos;
    return true;
}

uint32_t SegmentStream::GetCurrentPos() const {
    if (_fallback != nullptr) return static_cast<uint32_t>(_fallback->_position);   // what its GetCurrentPos answers
    return static_cast<uint32_t>(_pos);
}

uint32_t SegmentStream::GetSize() const {
    if (_fallback != nullptr) return _fallback->GetSize();
    return _record->total;
}

void SegmentStream::Skip(int32_t len) {
    if (_fallback != nullptr) {
        _fallback->_position += len;   // CreateOnDemand::Skip's shape over the compressed stream
        return;
    }
    _pos += len;
}

bool SegmentStream::SkipWithin(int32_t len) {
    // Hostile-cache hardening (review F-234): ext.texture_quality's SkipIfWithin hands over a size it
    // read from the record's own bytes. Bounded by the record's total, which AddChunk keeps within
    // INT32_MAX, so the Skip add cannot overflow; a valid size always lies within the stream.
    if (len < 0) return false;
    if (static_cast<int64_t>(GetCurrentPos()) + len > static_cast<int64_t>(_record->total)) return false;
    Skip(len);
    return true;
}

}  // namespace StreamCache
}  // namespace Ext
