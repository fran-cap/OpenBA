// ============================================================================
// tests/fuzz/stream_cache_fuzz.cpp -- Ext::StreamCache::Bundle::AddChunk over arbitrary chunks
// ============================================================================
// ext.stream_cache reads its record chunks back from the derived cache (a file in the user's
// Documents folder: a local cache, so a planted or damaged file reaches this parser). P0_MAP §8.
// Input: byte 0 = how many chunks (1..4), the rest split evenly into that many chunks; each chunk
// goes through ReadBack (findChunk serves them in order, findMarker serves none) exactly as
// stream_cache.cpp feeds them. Then, on what the bundle kept, the consumers' operations: Find per
// record, RecordView::Read over every segment and one byte past it, LiveBytes, ShouldConsolidate,
// Consolidate, and a re-parse of the consolidated chunk (it must be accepted: an oracle, trap()).
// ============================================================================
#include "ext/stream_cache/stream_record.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Ext::StreamCache;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1 || size > 0x20000)
        return 0;
    const uint32_t n = 1 + (data[0] & 3);
    const size_t body = size - 1;
    std::vector<std::vector<uint8_t>> chunks;
    for (uint32_t i = 0; i < n; ++i) {
        const size_t b = body * i / n;
        const size_t e = body * (i + 1) / n;
        chunks.emplace_back(data + 1 + b, data + 1 + e);
        // MASK (finding: AddChunk reserves the raw u32 record count before any check, an OOM
        // abort on a 16-byte chunk): a count no chunk of this size can hold is rejected here so
        // the fuzzer reaches the record parser.
        if (ChunkRecordCount(chunks.back()) > chunks.back().size() / kRecordHeader)
            return 0;
    }

    Bundle bundle;
    const FindFn findChunk = [&](uint32_t ordinal, std::vector<uint8_t>& out) {
        if (ordinal >= chunks.size())
            return false;
        out = chunks[ordinal];
        return true;
    };
    const FindFn findMarker = [](uint32_t, std::vector<uint8_t>&) { return false; };
    ReadBack(bundle, findChunk, findMarker);

    // The consumers: every record the index holds, by its entry id.
    const std::vector<uint8_t> merged = bundle.Consolidate();
    if (!merged.empty()) {
        const uint32_t count = ChunkRecordCount(merged);
        // Walk the consolidated chunk's records to learn the ids (the bundle has no iterator).
        Bundle again;
        if (!again.AddChunk(merged))
            __builtin_trap();   // the consolidated chunk of accepted records must parse
        if (again.Records() != bundle.Records() || again.LiveBytes() != bundle.LiveBytes())
            __builtin_trap();
        size_t at = kChunkHeader;
        std::vector<uint8_t> buf;
        for (uint32_t i = 0; i < count; ++i) {
            EntryId id;
            std::memcpy(&id.offset, merged.data() + at, 8);
            std::memcpy(&id.size, merged.data() + at + 8, 4);
            const RecordView* v = bundle.Find(id);
            if (v == nullptr)
                __builtin_trap();
            for (uint32_t s = 0; s < v->segCount; ++s) {
                const Segment seg = v->segs[s];
                buf.resize(static_cast<size_t>(seg.len) + 1);
                if (!v->Read(seg.pos, seg.len, buf.data()))
                    __builtin_trap();
                v->Read(seg.pos, seg.len + 1, buf.data());
                v->Read(seg.pos + seg.len, 1, buf.data());
            }
            // Re-record the view (the widen path: Recorder::Seed + Note + Finish).
            Recorder rec;
            rec.Seed(*v);
            if (v->segCount > 0) {
                const Segment last = v->segs[v->segCount - 1];
                std::vector<uint8_t> extra(16, 0xAB);
                rec.Note(last.pos + last.len, 16, extra.data());
            }
            std::vector<uint8_t> out;
            BeginChunk(out);
            rec.Finish(out);
            const size_t blockBytes = static_cast<size_t>(v->blockCount) * 8;
            const size_t segBytes = static_cast<size_t>(v->segCount) * 8;
            const size_t padded = (static_cast<size_t>(v->byteCount) + 7) & ~static_cast<size_t>(7);
            at += kRecordHeader + blockBytes + segBytes + padded;
        }
    }
    ShouldConsolidate(bundle);
    return 0;
}
