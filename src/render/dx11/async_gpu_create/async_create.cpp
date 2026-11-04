// ============================================================================
// src/render/dx11/async_gpu_create/async_create.cpp -- ext.async_gpu_create, the DX11 side
// (docs/extensions/handins/ASYNCGPUCREATE.md). The contract is in async_create.h.
// ============================================================================
// NOT RETAIL.
//
// One FIFO of jobs, a small pool of worker threads made at the first deferred create and never
// destroyed (they sleep on the queue's condition variable), and the set of objects with a job
// pending. Every lock is this file's one mutex; no D3D11 call is made while it is held, and the
// workers take nothing else (not the device lock, not the Present mutex: a create never touches
// the immediate context, and the device is free-threaded, created with flags 0).
//
// Back-pressure: the loader blocks in Push while more than kMaxBytesInFlight of initial data
// waits, so the deferred copies never grow the working set by more than that.
// ============================================================================

#include "async_gpu_create/async_create.h"
#include "async_gpu_create/async_gpu_create_backend.h"

#include "dx11_backend_access.h"   // Dx11Device
#include "share_gpu_data/share_create.h"   // NOT RETAIL: ext.share_gpu_data (the create-or-share)
#include "compress_ao/bc4_create.h"        // NOT RETAIL: ext.compress_ao (the conversion on the worker)
#include "system/MemoryAllocator.h"
#include "system/annotated_mutex.h"   // Sys::Mutex / Sys::MutexLock (clang -Wthread-safety)

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

namespace VideoDX11 {
namespace AsyncCreate {

std::atomic<int64_t> g_pending{0};

namespace {

constexpr uint64_t kMaxBytesInFlight = 256ull << 20;

enum class JobKind : uint8_t { Buffer, Texture };

struct Job {
    JobKind       kind;
    uint64_t      seq;     // the Push order; Drain waits for every seq below its ticket
    const void*   obj;
    ID3D11Device* dev;
    uint64_t      bytes;   // initial data held, for the back-pressure count
    // Buffer
    D3D11_BUFFER_DESC bufferDesc;
    void*             bufferData;
    ID3D11Buffer**    outBuffer;
    // Texture
    D3D11_TEXTURE2D_DESC            texDesc;
    D3D11_SUBRESOURCE_DATA*         subres;
    int                             subresCount;
    uint8_t*                        texBytes;
    bool                            hasSrvDesc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D11_SAMPLER_DESC              samplerDesc;
    uint32_t                        bc4SourceFormat;   // non-zero: convert the R8 levels first
    ID3D11Texture2D**               outTexture;
    ID3D11ShaderResourceView**      outSrv;
    ID3D11SamplerState**            outSampler;
};

// NOT RETAIL (UB fix, review F-065): IMMORTAL, made at static initialisation and never destroyed. The
// workers are detached and stay parked in g_workCv.wait for the rest of the run; as namespace-scope
// objects, exit() destroyed this state under them, and destroying a condition variable that has
// blocked waiters is undefined ([thread.condition.condvar]; winpthreads is silent about it, glibc
// hangs in exit()). Same rule as src/platform/common/event_std.cpp.
// GUARDED_BY: clang's thread-safety analysis (system/thread_annotations.h), as the Vulkan twin. Only a
// clang build of this backend checks it (the Linux check builds Vulkan, so this file is GCC's alone).
Sys::Mutex& g_mutex = *new Sys::Mutex;
// a job was queued
std::condition_variable& g_workCv = *new std::condition_variable;
// a job finished (Await / Drain / back-pressure)
std::condition_variable& g_doneCv = *new std::condition_variable;
std::deque<Job>& g_queue GUARDED_BY(g_mutex) = *new std::deque<Job>;
// objects with a queued or running job
std::unordered_set<const void*>& g_pendingObjs GUARDED_BY(g_mutex) = *new std::unordered_set<const void*>;
// seqs of the queued and running jobs
std::set<uint64_t>& g_pendingSeqs GUARDED_BY(g_mutex) = *new std::set<uint64_t>;
uint64_t                        g_nextSeq GUARDED_BY(g_mutex)   = 0;
uint64_t                        g_bytesHeld GUARDED_BY(g_mutex) = 0;
int                             g_waiters GUARDED_BY(g_mutex)   = 0;
int                             g_workers GUARDED_BY(g_mutex)   = 0;

std::atomic<bool> g_requested{false};
std::atomic<int>  g_driverConcurrent{-1};   // -1 = not queried yet (no device seen)

thread_local bool t_deferring = false;

// The creates of one job, in ImageBufferData::Create's / GpuBuffer_Create's order, writing the same
// fields with what D3D11 writes into them. ext.share_gpu_data's create-or-share (a plain create when
// its row is off) runs here too, so its hashing stays off the loader.
void Run(Job& j) {
    if (j.kind == JobKind::Buffer) {
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = j.bufferData;
        ShareGpuData::CreateBuffer(j.dev, j.bufferDesc, &init, j.outBuffer);
        System::MemoryAllocator::MemFree(j.bufferData);
    } else {
        // The levels lie back to back in texBytes (CreateTextureDeferred's copy): each one's size is
        // the distance to the next, the last one's to the end of the block.
        std::vector<uint64_t> sizes(static_cast<size_t>(j.subresCount));
        for (int i = 0; i < j.subresCount; ++i) {
            const uint8_t* at   = static_cast<const uint8_t*>(j.subres[i].pSysMem);
            const uint8_t* next = i + 1 < j.subresCount ? static_cast<const uint8_t*>(j.subres[i + 1].pSysMem)
                                                        : j.texBytes + j.bytes;
            sizes[static_cast<size_t>(i)] = static_cast<uint64_t>(next - at);
        }
        // ext.compress_ao: the cache lookup or the encode, here instead of on the loader. `bc4` holds
        // the converted levels until the create has copied them.
        std::vector<uint8_t> bc4;
        if (j.bc4SourceFormat != 0)
            CompressAo::ConvertForCreate(&j.texDesc, j.subres, j.subresCount, &sizes, j.bc4SourceFormat, &bc4);
        ShareGpuData::CreateTexture(j.dev, j.texDesc, j.subres, j.subresCount, sizes.data(),
                                    j.hasSrvDesc ? &j.srvDesc : nullptr, j.outTexture, j.outSrv);
        j.dev->CreateSamplerState(&j.samplerDesc, j.outSampler);
        delete[] j.subres;
        delete[] j.texBytes;
    }
    j.dev->Release();
}

void WorkerMain() {
    for (;;) {
        Job j;
        {
            Sys::MutexLock lock(g_mutex);
            g_workCv.wait(lock.Native(), []() REQUIRES(g_mutex) { return !g_queue.empty(); });
            j = g_queue.front();
            g_queue.pop_front();
        }
        Run(j);
        {
            Sys::MutexLock lock(g_mutex);
            g_pendingObjs.erase(j.obj);
            g_pendingSeqs.erase(j.seq);
            g_bytesHeld -= j.bytes;
            g_pending.fetch_sub(1, std::memory_order_release);
            if (g_waiters != 0) g_doneCv.notify_all();
        }
    }
}

// Measured (handins/ASYNCGPUCREATE.md §3, MegaMod boot on a 16-thread CPU): ONE worker already
// keeps up with the loader (no back-pressure wait, no Await wait, nothing left at the bracket's
// close); two were the best of 1 / 2 / 3 by a hair. Two at most, one on a CPU with fewer than
// eight threads, so the creates never crowd the loader and decode_ahead's workers.
int WorkerCount() {
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, std::min(2, hw / 4));
}

void Push(Job& j) {
    Sys::MutexLock lock(g_mutex);
    if (g_workers == 0) {
        g_workers = WorkerCount();
        for (int i = 0; i < g_workers; ++i) {
            std::thread(WorkerMain).detach();
        }
        std::fprintf(stderr, "[AsyncGpuCreate] %d worker thread(s)\n", g_workers);
    }
    // Back-pressure: wait for room unless nothing is held (one oversized job always goes through).
    if (g_bytesHeld != 0 && g_bytesHeld + j.bytes > kMaxBytesInFlight) {
        ++g_waiters;
        g_doneCv.wait(lock.Native(),
                      [&]() REQUIRES(g_mutex) { return g_bytesHeld == 0 || g_bytesHeld + j.bytes <= kMaxBytesInFlight; });
        --g_waiters;
    }
    j.seq = g_nextSeq++;
    g_bytesHeld += j.bytes;
    g_pendingObjs.insert(j.obj);
    g_pendingSeqs.insert(j.seq);
    g_pending.fetch_add(1, std::memory_order_release);
    g_queue.push_back(j);
    g_workCv.notify_one();
}

}  // namespace

void Begin() {
    t_deferring = false;
    if (!g_requested.load(std::memory_order_acquire)) return;
    if (g_driverConcurrent.load(std::memory_order_acquire) < 0) {
        // The first armed bracket: ask the driver once. A device recreated after a loss is on the
        // same adapter and driver, so the answer stands for the run.
        ID3D11Device* const dev = Dx11Device();
        if (dev == nullptr) return;
        D3D11_FEATURE_DATA_THREADING th = {};
        const HRESULT hr = dev->CheckFeatureSupport(D3D11_FEATURE_THREADING, &th, sizeof(th));
        const int concurrent = (hr == 0 && th.DriverConcurrentCreates) ? 1 : 0;
        g_driverConcurrent.store(concurrent, std::memory_order_release);
        std::fprintf(stderr, "[AsyncGpuCreate] driver threading: hr=0x%08lx DriverConcurrentCreates=%d "
                             "DriverCommandLists=%d -> %s\n",
                     static_cast<unsigned long>(hr), th.DriverConcurrentCreates, th.DriverCommandLists,
                     concurrent ? "creates on workers" : "creates stay on the loader (serial fallback)");
    }
    t_deferring = g_driverConcurrent.load(std::memory_order_acquire) == 1;
}

void End() {
    if (t_deferring) Drain();
    t_deferring = false;
}

bool Deferring() {
    return t_deferring;
}

void Drain() {
    if (g_pending.load(std::memory_order_acquire) == 0) return;
    Sys::MutexLock lock(g_mutex);
    const uint64_t ticket = g_nextSeq;
    ++g_waiters;
    g_doneCv.wait(lock.Native(),
                  [&]() REQUIRES(g_mutex) { return g_pendingSeqs.empty() || *g_pendingSeqs.begin() >= ticket; });
    --g_waiters;
}

void AwaitSlow(const void* obj) {
    Sys::MutexLock lock(g_mutex);
    if (g_pendingObjs.find(obj) == g_pendingObjs.end()) return;
    ++g_waiters;
    g_doneCv.wait(lock.Native(), [&]() REQUIRES(g_mutex) { return g_pendingObjs.find(obj) == g_pendingObjs.end(); });
    --g_waiters;
}

void EnqueueBuffer(const void* obj, ID3D11Device* dev, const D3D11_BUFFER_DESC& desc, void* data,
                   ID3D11Buffer** out) {
    Job j = {};
    j.kind       = JobKind::Buffer;
    j.obj        = obj;
    j.dev        = dev;
    j.bytes      = desc.ByteWidth;
    j.bufferDesc = desc;
    j.bufferData = data;
    j.outBuffer  = out;
    dev->AddRef();
    Push(j);
}

void EnqueueTexture(const void* obj, ID3D11Device* dev, const D3D11_TEXTURE2D_DESC& desc,
                    D3D11_SUBRESOURCE_DATA* subres, int subresCount, uint8_t* bytes, uint64_t byteCount,
                    const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDesc, const D3D11_SAMPLER_DESC& samplerDesc,
                    uint32_t bc4SourceFormat, ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSrv,
                    ID3D11SamplerState** outSampler) {
    Job j = {};
    j.kind        = JobKind::Texture;
    j.obj         = obj;
    j.dev         = dev;
    j.bytes       = byteCount;
    j.texDesc     = desc;
    j.subres      = subres;
    j.subresCount = subres != nullptr ? subresCount : 0;
    j.texBytes    = bytes;
    j.hasSrvDesc  = srvDesc != nullptr;
    if (srvDesc != nullptr) j.srvDesc = *srvDesc;
    j.samplerDesc = samplerDesc;
    j.bc4SourceFormat = bc4SourceFormat;
    j.outTexture  = outTexture;
    j.outSrv      = outSrv;
    j.outSampler  = outSampler;
    dev->AddRef();
    Push(j);
}

}  // namespace AsyncCreate
}  // namespace VideoDX11

// ---- the backend-neutral interface (async_gpu_create_backend.h) -----------------------------------
namespace AsyncGpuCreateBackend {

void Request(bool on) {
    VideoDX11::AsyncCreate::g_requested.store(on, std::memory_order_release);
}

bool Supported() {
    return VideoDX11::AsyncCreate::g_driverConcurrent.load(std::memory_order_acquire) == 1;
}

}  // namespace AsyncGpuCreateBackend
