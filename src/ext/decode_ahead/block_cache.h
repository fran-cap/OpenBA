#pragma once
// src/ext/decode_ahead/block_cache.h -- ext.decode_ahead's worker pool + decoded-block cache
//
// NOT RETAIL (docs/extensions/handins/DECODEAHEAD.md). Standard C++ only (no engine header), so
// tests/unit/ext_decode_ahead_test.cpp drives it with its own decoder.
//
// A job is one compressed block of one stream: its bytes (`src`, `csize`, read-only memory that
// stays valid until the owner is dropped), the size it must decode to (`usize`) and the block's
// identity (owner, index, offset). Workers decode jobs in submission order into the cache's own
// buffers; Take hands one block back, waiting while it is still being decoded. A block is handed
// back only when the decoder produced exactly `usize` bytes; anything else (a damaged block) is
// reported as "no block" and the caller decodes it itself, so a damaged block takes retail's path.
// Drop(owner) forgets an owner's blocks: queued ones are removed, a running one is waited for.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace Ext { namespace DecodeAhead {

class BlockCache {
public:
    // System::Compression::Decompress's shape: bytes written, dst capacity `dstCap`.
    using DecodeFn = int (*)(const void* src, uint32_t srcLen, void* dst, uint32_t dstCap);

    static constexpr uint32_t kBlock = 0x8000;   // RtIo::kCompressedStreamWindowSize
    static constexpr uint32_t kSlack = 64;       // the decoder may read a little past srcEnd

    BlockCache(int workers, int maxJobs, DecodeFn decode);
    ~BlockCache();
    BlockCache(const BlockCache&) = delete;
    BlockCache& operator=(const BlockCache&) = delete;

    // Queue block `index` of `owner` unless it is already held or every job slot is in use.
    // True when it was queued.
    bool Submit(const void* owner, int32_t index, uint32_t offset, const uint8_t* src,
                uint16_t csize, uint16_t usize);

    // Block `index` of `owner`, copied into `dst` (usize bytes), when a job with that identity
    // (offset, src, csize) exists and decoded to exactly usize bytes; the job is freed either way.
    // Returns the bytes copied, or -1 when there is no such block.
    int Take(const void* owner, int32_t index, uint32_t offset, const uint8_t* src, uint16_t csize,
             uint8_t* dst);

    // Forget every block of `owner`.
    void Drop(const void* owner);

    struct Stats {
        int64_t submitted = 0;
        int64_t taken = 0;       // handed back to the caller
        int64_t waited = 0;      // Take found the job still running or queued
        int64_t rejected = 0;    // Take found a job that did not decode to usize (or a stale identity)
        int64_t dropped = 0;     // freed unused (Drop)
        int64_t full = 0;        // Submit refused: no free slot
    };
    Stats GetStats();

private:
    enum class State { Free, Queued, Running, Done };
    struct Job {
        State          state = State::Free;
        const void*    owner = nullptr;
        int32_t        index = 0;
        uint32_t       offset = 0;
        const uint8_t* src = nullptr;
        uint16_t       csize = 0;
        uint16_t       usize = 0;
        int            result = 0;
        std::vector<uint8_t> out;
    };

    Job* Find(const void* owner, int32_t index);
    void WorkerMain();

    DecodeFn                 decode_;
    std::mutex               mu_;
    std::condition_variable  work_;    // a job was queued, or stop
    std::condition_variable  done_;    // a job finished
    std::vector<Job>         jobs_;
    std::deque<Job*>         queue_;
    std::vector<std::thread> threads_;
    bool                     stop_ = false;
    Stats                    stats_;
};

}}  // namespace Ext::DecodeAhead
