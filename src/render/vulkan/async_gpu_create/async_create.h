#pragma once
// ============================================================================
// src/render/vulkan/async_gpu_create/async_create.h -- ext.async_gpu_create, the Vulkan side
// (docs/extensions/handins/ASYNCGPUCREATE.md, issue #13)
// ============================================================================
// NOT RETAIL (P4). The Vulkan twin of src/render/dx11/async_gpu_create/async_create.h. Retail creates
// every resource synchronously in its Serialize, on the loader thread. With the row on, the loader
// bracket (DeviceData::Slot4(true) ... Slot4(false), on the loader thread) DEFERS that thread's
// data-seeded static creates to worker threads:
//   - everything engine-visible is decided on the loader exactly as the synchronous path decides it (the
//     owner's size / dynamic, the image's shape and samplers, ext.texture_quality's drop);
//   - a job owns a copy of its seed bytes and runs the synchronous path's create (seeded_create.h: the
//     compress_ao conversion and the share_gpu_data create-or-share run on the worker too), then its
//     publish step writes the companion's fields, before the object counts as done;
//   - Await(obj) before every use of a companion's GPU object (bind, lock, set data, update, read, a
//     save, a re-create, the destructor) waits for that object's own job;
//   - Slot4(false) waits for every job of the load (End), so a finished load has created everything it
//     would have created synchronously. DeviceData::Shutdown drains first.
// Vulkan object creation is free-threaded and the backend's allocator and upload batch take their own
// locks (resources.h), so every device has the worker path (no DriverConcurrentCreates question).
// Off (the row, any thread outside the bracket): the synchronous path, plus one acquire load per use.
// ============================================================================
#include <atomic>
#include <cstdint>
#include <functional>

namespace VideoVulkan {
namespace AsyncCreate {

// Jobs enqueued and not yet finished, across all threads. Await's fast path reads only this.
extern std::atomic<int64_t> g_pending;

// The loader bracket (DeviceData::Slot4). Begin arms deferral for the CALLING thread when the row is
// requested; End waits for every job and disarms it.
void Begin();
void End();

// True on the thread inside an armed bracket: its seeded static creates may be deferred.
bool Deferring();

// Wait for every job enqueued before the call (any thread). Shutdown / device lost.
void Drain();

// Wait for `obj`'s own job, if it has one pending. Cheap when nothing is pending.
void AwaitSlow(const void* obj);
inline void Await(const void* obj) {
    if (g_pending.load(std::memory_order_acquire) != 0) AwaitSlow(obj);
}

// A deferred create for `obj`: `run` (on a worker) makes the object from the seed bytes it owns and
// writes the companion's fields; `bytes` is the seed byte count, for the back-pressure count.
void Enqueue(const void* obj, uint64_t bytes, std::function<void()> run);

}  // namespace AsyncCreate
}  // namespace VideoVulkan
