#pragma once
// ============================================================================
// src/render/metal/create_on_demand/lazy_create.h -- ext.create_on_demand, the Metal side
// (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL. The Metal twin of src/render/dx11/create_on_demand/lazy_create.h, over the same
// backend-neutral table, Locator, Redecode and pending levels (src/render/common/create_on_demand/).
// With the row on:
//   - a STATIC buffer (not dynamic, size > 0, no buffer yet) read from a packed resource file by the
//     buffer's Serialize load arm is not read: its Locator is recorded, the stream skips the payload,
//     and the owner keeps the size / dynamic it just read (Create's two stores);
//   - a kind-2 texture whose level bytes the engine's level reader skipped (Video::ImageBuffer::
//     _extLevelSource, pending_levels.h) gets its descriptor and its SAMPLER in ImageBufferData::Create
//     as before; only the texture is recorded.
// The object is made at its first use: Ensure(this) at the top of every slot that reads its handle
// (Bind, Lock, SetData, Update, Read, the save arm); the fast path is the handle's own nil test. A made
// object unused for ext.create_on_demand_idle_min minutes is given back (the member nilled, the record
// idle again) and re-made at its next use; each such slot holds a UseScope for its body, so no eviction
// meets a use. The materialization re-decodes the payload from the package and runs the load's create through
// seeded_create.h: ext.compress_ao's conversion, then ext.share_gpu_data's create-or-share. Publish
// writes the companion's strong member and its `shared` flag. Destructors, re-creates and Attach*
// Forget(this) first.
//
// DEVICE. DeviceData::Shutdown (exit, a device rebuild) calls Pause first: no materialization runs
// against the device going away; CreateDevice's success calls Resume. A materialization that finds no
// device returns Retry: the record stays for the next use on the next device.
// Off (the row, /profile:faithful): one relaxed load per site, the load's path.
// ============================================================================

#import <Metal/Metal.h>

#include <atomic>
#include <cstdint>

#include "create_on_demand/lazy_table.h"
#include "seeded_create.h"

namespace IO {
class Stream;
}

namespace VideoMetal {
namespace CreateOnDemand {

// The table while the row is on (set once at boot, before the first load), else null.
extern std::atomic<::CreateOnDemand::LazyTable*> g_table;

inline bool Requested() {
    return g_table.load(std::memory_order_relaxed) != nullptr;
}

// A companion's strong handle member, which a prefetch worker's Publish and a sweep's Evict store
// while uses and the gather test it with no lock (review F-170; lazy_table.h LoadPublished). `field`
// is the member's address. Made: an acquire load of its bits, non-nil.
inline bool Made(const void* field) {
    return ::CreateOnDemand::LoadPublished(*static_cast<void* const*>(field)) != nullptr;
}

// Stores `value` in the strong member at `field` with release: the member's reference moves in with
// the bits, and the one it held before is released.
inline void StorePublished(void* field, id value) {
    void* const raw = (__bridge_retained void*)value;
    void* const old = __atomic_exchange_n(static_cast<void**>(field), raw, __ATOMIC_ACQ_REL);
    id released = (__bridge_transfer id)old;
    (void)released;
}

// Before a use of a companion whose handle is nil: make it now (or wait for the worker making it).
inline void Ensure(const void* obj) {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) {
        t->Ensure(obj);
    }
}

// Before a destructor, a re-create or an Attach*: no deferred create of `obj` runs after this.
inline void Forget(const void* obj) {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) {
        t->Forget(obj);
    }
}

// The buffer Serialize load arm, before it reads the payload: record the create of a static `size`-byte
// vertex / index buffer (written to `*out` and `*outShared` when made) and skip its payload in `stream`.
// False: nothing done, the load reads the payload.
bool DeferBuffer(const void* obj, IO::Stream* stream, bool index, uint32_t size, id<MTLBuffer> __strong* out,
                 bool* outShared);

// ImageBufferData::Create of a kind-2 texture: true when the engine's level reader skipped this image's
// level bytes (it left their Locators pending for `owner`).
bool HasPendingLevels(const void* owner);

// The texture's deferred create: `shape` and `desc` as Create built them (the descriptor is copied).
// Takes the pending Locators. Writes `*outTexture` and `*outShared` when made.
void DeferTexture(const void* obj, const TextureShape& shape, MTLTextureDescriptor* desc,
                  id<MTLTexture> __strong* outTexture, bool* outShared);

void Pause();
void Resume();

// The loader bracket's close (DeviceData::Slot4(false)): one summary line when anything changed.
void LoadEnd();
// After a sweep gave objects back (the table's sweep hook): the same summary.
void AfterSweep(int64_t evicted);

// Every slot that reads a companion's handle holds one for its whole body: no eviction runs while it
// does (the table's UseGate), and the object's use is stamped. Nothing while the row is off.
class UseScope {
public:
    explicit UseScope(const void* obj) : _table(g_table.load(std::memory_order_relaxed)) {
        if (_table == nullptr) {
            return;
        }
        _table->Enter();
        _table->Use(obj);
    }
    ~UseScope() {
        if (_table != nullptr) {
            _table->Leave();
        }
    }
    UseScope(const UseScope&) = delete;
    UseScope& operator=(const UseScope&) = delete;

private:
    ::CreateOnDemand::LazyTable* const _table;
};

}  // namespace CreateOnDemand
}  // namespace VideoMetal
