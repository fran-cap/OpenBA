#pragma once
// ============================================================================
// src/render/vulkan/create_on_demand/lazy_create.h -- ext.create_on_demand, the Vulkan side
// (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL (P4). The Vulkan twin of src/render/dx11/create_on_demand/lazy_create.h and the Metal one,
// over the same backend-neutral table, Locator, Redecode and pending levels
// (src/render/common/create_on_demand/). With the row on:
//   - a STATIC buffer read from a packed resource file by the buffers' Serialize load arm is not read:
//     its Locator is recorded, the stream skips the payload, and the owner keeps the size / dynamic it
//     just read;
//   - a kind-2 texture whose level bytes the engine's level reader skipped (Video::ImageBuffer::
//     _extLevelSource, pending_levels.h) gets its shape and its SAMPLERS in ImageBufferData::Create as
//     before; only the image is recorded.
// The object is made at its first use: Ensure(this) in every slot that reads its GPU object (Bind,
// Update, Read, the save arm), and again by the draw itself (draw.cpp), which holds the table's use
// gate while it reads the bound objects: here a Bind only records the companion and the draw reads its
// handle later, so an idle eviction between the two must not find a half-read handle (D3D11 holds a
// reference on what is bound; Vulkan does not). The materialization re-decodes the payload from the
// package and runs the load's create (seeded_create.h: ext.compress_ao's conversion, then
// ext.share_gpu_data's create-or-share); Publish writes the companion's fields. An evicted object is
// retired (the frames in flight finish with it first) and re-made at its next use.
//
// DEVICE. DeviceData::Shutdown calls Pause first: no materialization runs against the device going away;
// CreateDevice's success calls Resume. A materialization that finds no device returns Retry.
// Off (the row, /profile:faithful): one relaxed load per site.
// ============================================================================
#include "create_on_demand/lazy_table.h"
#include "seeded_create.h"

#include <atomic>
#include <cstdint>

namespace IO {
class Stream;
}

namespace VideoVulkan {

struct BufferStore;
class ImageBufferData;

namespace CreateOnDemand {

// The table while the row is on (set once at boot, before the first load), else null.
extern std::atomic<::CreateOnDemand::LazyTable*> g_table;

inline bool Requested() {
    return g_table.load(std::memory_order_relaxed) != nullptr;
}

// Before a use of a companion whose object is not made: make it now (or wait for the worker making it).
inline void Ensure(const void* obj) {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) t->Ensure(obj);
}

// Before a destructor or a re-create: no deferred create of `obj` runs after this.
inline void Forget(const void* obj) {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) t->Forget(obj);
}

// The buffer Serialize load arm, before it reads the payload: record the create of a static `size`-byte
// vertex / index buffer into `store` and skip its payload in `stream`. False: nothing done, the load
// reads the payload.
bool DeferBuffer(const void* obj, IO::Stream* stream, bool index, uint32_t size, VkBufferUsageFlags usage,
                 BufferStore* store);

// ImageBufferData::Create of a kind-2 texture: true when the engine's level reader skipped this image's
// level bytes (it left their Locators pending for `owner`).
bool HasPendingLevels(const void* owner);

// The image's deferred create, `shape` as Create decided it (ext.texture_quality's drop applied: the
// pending Locators are the kept levels'). Takes the pending Locators.
void DeferTexture(ImageBufferData* image, const TextureShape& shape);

void Pause();
void Resume();

// The loader bracket's close (DeviceData::Slot4(false)): one summary line when anything changed.
void LoadEnd();

// P13 (docs/android/P13_MEM.md §9, ruled 2026-10-28): on a device that transcodes BC to ETC2, the
// loader bracket's close waits here for the prefetch queue (the textures and buffers the load's mesh
// instances queued) before the load ends, so a transcode the derived cache does not hold yet runs on
// the workers during the load, never in the frame that first draws the texture. Returns at once when
// the row is off or the table is paused; logs the wait when it took a millisecond or more.
void FinishPrefetches();

// Every slot that reads a companion's object holds one for its whole body: no eviction meanwhile, and
// the use is stamped. Nothing while the row is off.
class UseScope {
public:
    explicit UseScope(const void* obj) : _table(g_table.load(std::memory_order_relaxed)) {
        if (_table == nullptr) return;
        _table->Enter();
        _table->Use(obj);
    }
    ~UseScope() {
        if (_table != nullptr) _table->Leave();
    }
    UseScope(const UseScope&) = delete;
    UseScope& operator=(const UseScope&) = delete;

private:
    ::CreateOnDemand::LazyTable* const _table;
};

// The draw's (draw.cpp IssueDraw): the gate held for the whole draw; Touch stamps a bound companion and
// makes it when it is not made (`made` false). One relaxed load per draw while the row is off.
class DrawScope {
public:
    DrawScope() : _table(g_table.load(std::memory_order_relaxed)) {
        if (_table != nullptr) _table->Enter();
    }
    ~DrawScope() {
        if (_table != nullptr) _table->Leave();
    }
    bool Active() const { return _table != nullptr; }
    void Touch(const void* obj, bool made) {
        if (_table == nullptr || obj == nullptr) return;
        _table->Use(obj);
        if (!made) _table->Ensure(obj);
    }
    DrawScope(const DrawScope&) = delete;
    DrawScope& operator=(const DrawScope&) = delete;

private:
    ::CreateOnDemand::LazyTable* const _table;
};

}  // namespace CreateOnDemand
}  // namespace VideoVulkan
