#pragma once
// ============================================================================
// src/render/dx11/async_gpu_create/async_create.h -- ext.async_gpu_create, the DX11 side
// (docs/extensions/handins/ASYNCGPUCREATE.md, issue #13)
// ============================================================================
// NOT RETAIL. Retail's VideoDX11 creates every resource synchronously in its Serialize, on the
// loader thread (buffers.cpp GpuBuffer_Serialize -> Create, imagebufferdata.cpp Create). A MegaMod
// boot makes ~160k immutable vertex / index buffers and ~18k textures that way: a third of the
// loader's time is the driver's CreateBuffer / CreateTexture2D (handins/ASYNCGPUCREATE.md §1).
//
// With the row on, the loader bracket (Engine::Loader::Run's DeviceData::Slot4(true) ... Slot4(false),
// on the loader thread) DEFERS the data-seeded immutable creates of that thread to worker threads:
//   - the descriptors are built on the loader exactly as the synchronous path builds them, and every
//     engine-visible field (the owner's size / dynamic, the image registry, the nulled views) is
//     written there too, in the same order;
//   - a job owns its initial data (the buffer's scratch block, a copy of the texture's level bytes)
//     and the device it was enqueued against (AddRef'd), and writes the D3D11 handles into the same
//     companion fields the synchronous call writes, with the same values on success and on failure;
//   - Await(obj) before every use of a companion's handles (bind, map, update, read, re-create,
//     destroy) waits for that object's own job, so nothing ever sees a half-made resource;
//   - Slot4(false) waits for every job of the load (End), so a finished load has created everything
//     it would have created synchronously. Shutdown and the device-lost path drain first.
// Off (the row, a driver without concurrent creates, any thread outside the bracket): retail's path,
// unchanged, plus one acquire atomic load per use (Await's fast path).
// ============================================================================

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstdint>

namespace VideoDX11 {
namespace AsyncCreate {

// Jobs enqueued and not yet finished, across all threads. Await's fast path reads only this.
extern std::atomic<int64_t> g_pending;

// The loader bracket (DeviceData::Slot4). Begin arms deferral for the CALLING thread when the row is
// requested and the driver reports concurrent creates; End waits for every job and disarms it.
void Begin();
void End();

// True on the thread inside an armed bracket: its seeded immutable creates may be deferred.
bool Deferring();

// Wait for every job enqueued before the call (any thread). Shutdown / device lost / teardown.
void Drain();

// Wait for `obj`'s own job, if it has one pending. Cheap when nothing is pending.
void AwaitSlow(const void* obj);
inline void Await(const void* obj) {
    if (g_pending.load(std::memory_order_acquire) != 0) AwaitSlow(obj);
}

// A deferred CreateBuffer. `data` is a System::MemoryAllocator block the job now owns (MemFree'd by
// the worker after the create); `*out` receives what CreateBuffer writes.
void EnqueueBuffer(const void* obj, ID3D11Device* dev, const D3D11_BUFFER_DESC& desc, void* data,
                   ID3D11Buffer** out);

// A deferred CreateTexture2D + CreateShaderResourceView + CreateSamplerState (the order of
// ImageBufferData::Create for a plain texture). `subres` points into `bytes`, a new[] block the job
// now owns, its `subresCount` levels back to back in order. `srvDesc` null = the default view. The SRV
// is made only after a successful texture; the sampler always, as the synchronous path does.
// `subres` is a new[] array the job owns too; `byteCount` is the size of `bytes` (the back-pressure
// count). `bc4SourceFormat` non-zero (ext.compress_ao): the levels are R8 of that engine format and the
// worker converts them to BC4 before the create (VideoDX11::CompressAo::ConvertForCreate).
void EnqueueTexture(const void* obj, ID3D11Device* dev, const D3D11_TEXTURE2D_DESC& desc,
                    D3D11_SUBRESOURCE_DATA* subres, int subresCount, uint8_t* bytes, uint64_t byteCount,
                    const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDesc, const D3D11_SAMPLER_DESC& samplerDesc,
                    uint32_t bc4SourceFormat,
                    ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSrv,
                    ID3D11SamplerState** outSampler);

}  // namespace AsyncCreate
}  // namespace VideoDX11
