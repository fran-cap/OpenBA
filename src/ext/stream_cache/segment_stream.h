#pragma once
// ============================================================================
// src/ext/stream_cache/segment_stream.h -- ext.stream_cache: a resource read from its record
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). An IO::Stream the serializer
// reads a compiled resource from when the stream cache holds the resource's record
// (stream_record.h): positions are the DECOMPRESSED stream's, as CompressedStream's are, so every
// consumer -- the serializer, ext.create_on_demand's Locate / Skip -- sees the coordinates it saw
// over the compressed stream.
//
// A read outside the recorded segments (a record made by a build that read less) FALLS BACK: the
// opener hands over retail's CompressedStream on the same entry, it is positioned at the current
// decompressed position (SetCurrentPos decodes the block, as any Read there would) and the load
// goes on through it; every call is forwarded from then on. The bytes already consumed from the
// record are the ones that stream would have given (the record is those bytes), so the load is the
// same load. The fallback is counted (Extended) and the owner widens the record with what the rest
// of the load reads. An opener that fails marks the stream Failed (the load's reads then fail, as
// they would on a stream that cannot be opened).
//
// The stream carries the package handle and the entry offset the record was made from, which is
// what a Locator needs (create_on_demand's payloads are re-read from the mapped package exactly as
// they are when the compressed stream is read).
// ============================================================================
#include <cstdint>

#include "io/Stream.h"
#include "platform_file.h"
#include "ext/stream_cache/stream_record.h"

namespace Ext {
namespace StreamCache {

class SegmentStream final : public IO::Stream {
public:
    // Opens retail's compressed stream over the entry `self` serves; null when that fails.
    using OpenFallbackFn = IO::CompressedStream* (*)(SegmentStream& self, void* context);

    SegmentStream(const RecordView* record, Platform::FileHandle package, int64_t entry,
                  OpenFallbackFn openFallback, void* context);
    ~SegmentStream() override;
    SegmentStream(const SegmentStream&) = delete;
    SegmentStream& operator=(const SegmentStream&) = delete;

    bool     Write(int n, const void* src) override;
    bool     Read(int n, void* dst) override;
    bool     SetCurrentPos(int pos) override;
    uint32_t GetCurrentPos() const override;
    uint32_t GetSize() const override;

    // Past a payload the renderer reads later (CreateOnDemand::Skip's shape).
    void Skip(int32_t len);
    // Skip only when the `len` bytes lie ahead of the position within the record's total
    // (CreateOnDemand::SkipIfWithin's contract); false: nothing done. ext.create_on_demand's
    // g_altSkip over this stream.
    bool SkipWithin(int32_t len);

    const RecordView*     Record() const { return _record; }
    Platform::FileHandle  Package() const { return _package; }
    int64_t               Entry() const { return _entry; }
    uint32_t              Consumed() const { return _consumed; }   // bytes served from the record
    bool                  Extended() const { return _fallback != nullptr; }
    IO::CompressedStream* Fallback() const { return _fallback; }
    bool                  Failed() const { return _failed; }

private:
    bool EnterFallback();

    const RecordView*     _record;
    Platform::FileHandle  _package;
    int64_t               _entry;
    OpenFallbackFn        _openFallback;
    void*                 _context;
    IO::CompressedStream* _fallback = nullptr;
    int32_t               _pos      = 0;
    uint32_t              _consumed = 0;
    bool                  _failed   = false;
};

}  // namespace StreamCache
}  // namespace Ext
