// Phase 4 (lane L1) — the three GPU-buffer resource classes of VideoDX11-x64.dll:
// VertexBufferData, IndexBufferData and ConstantBufferData. 18 vtable slots.
//
//   VertexBufferData    vtable 0x180018830, 7 slots
//   IndexBufferData     vtable 0x1800187c8, 7 slots
//   ConstantBufferData  vtable 0x1800185d0, 4 slots
//
// B5 BUF (the dx11 seam): all three are real C++ subclasses of their abstract
// `Video::XData` bases (src/render/dx11/VideoDX11Data.h); every slot below is a member
// declared `override`, and the compiler builds all three vtables (retail slot
// N >= 1 is compiler slot N + 1, which nothing indexes any more). The 18 seam
// registrations, the three live-vtable re-seats and the three qualified
// base-destructor calls are gone: the destructors' vptr store, the
// chain to `??1XData@Video@@` and the conditional MemFree are the compiler's
// (its D0 + the base's class-scope `operator delete`, B5 W0,
// src/engine/video/companion_operators.cpp).
//
// Why the three live in ONE translation unit
// ------------------------------------------
// Slots 3 and 4 of VertexBufferData and IndexBufferData are *the same two
// addresses* — 0x180003290 (Lock) and 0x180003340 (Unlock) appear verbatim in
// both vtables. The original has one copy of the code; so does this file (one
// template helper over the three shared fields, called from both classes'
// members). The two classes are byte-identical in layout (both 0x20,
// ID3D11Buffer* at +0x10, write cursor at +0x18, locked flag at +0x1c), and
// VideoDX11Data.h carries static_asserts that pin that equivalence so a future
// edit to one class cannot silently make the two members diverge from retail's
// one body.
//
// ConstantBufferData rides along because it is the third member of the
// "buffer upload path" and shares the private array helper at 0x180001550.
//
// How every function here reaches D3D11
// -------------------------------------
// None of these classes caches the device or the context (only ContextData
// does). Each call walks the same chain the original walks:
//
//     Video::Device::_singleton   (Runtime 0x1801fe7a8, a Video::Device*)
//       -> [+0x100]  active Video::DeviceData*   (our DeviceData struct)
//       -> [+0x20]   ID3D11Device*
//       -> [+0x28]   ID3D11DeviceContext*
//
// In the shipped DLL this reads the import slot at VideoDX11 0x1800134b8,
// which the loader fills with the address of that same Runtime variable — see
// "The one global" in decomp/videodx11.md. We resolve the Runtime variable
// directly, which is the same object with one less indirection to get wrong.
// B5 BUF: through the shared `Dx11Device()` / `Dx11Context()` walk in
// dx11_backend_access.h (this file's private third copy of it is gone; same
// chain, same loads, no cache).
//
// ConstantBufferData::Create is the one exception: it takes the Video::Device*
// from its *owner* (Video::ConstantBuffer+0x38) rather than from the singleton.
// Reproduced as written; the two are the same object in practice.
//
// COM calls
// ---------
// Written against <d3d11.h> rather than hand-rolled vtable offsets, but every
// method used was checked against the offset the disassembly calls through,
// using the declaration order of ID3D11DeviceContextVtbl / ID3D11DeviceVtbl:
//
//     [ctx+0x38] = index  7 = VSSetConstantBuffers
//     [ctx+0x70] = index 14 = Map
//     [ctx+0x78] = index 15 = Unmap
//     [ctx+0x80] = index 16 = PSSetConstantBuffers
//     [ctx+0x90] = index 18 = IASetVertexBuffers
//     [ctx+0x98] = index 19 = IASetIndexBuffer
//     [ctx+0x178]= index 47 = CopyResource
//     [ctx+0x180]= index 48 = UpdateSubresource
//     [dev+0x18] = index  3 = ID3D11Device::CreateBuffer
//     [obj+0x10] = index  2 = IUnknown::Release
//
// HRESULT checks are reproduced literally: the original compares `(int)hr == 0`
// (S_OK exactly), not SUCCEEDED(hr). So does this file.
//
// Structured exception handling
// -----------------------------
// The two SetData slots (0x1800031c0 / 0x180003c60) carry an MSVC SEH state
// cookie (`mov [rsp-0x38], -2`) whose unwind funclet tail-calls the
// Video::Device::Unlock thunk at 0x180003060 — i.e. the original guarantees the
// device lock is released if the D3D11 call unwinds. GCC cannot reproduce MSVC
// SEH, and none of the calls between Lock and Unlock can throw (they are COM
// vtable calls into d3d11.dll), so this file uses a plain Lock/…/Unlock pair.
// That is the one deliberate behavioural difference in the file, and it is only
// observable on a path that cannot occur.
//
// Verification
// ------------
// All 18 slots are live and unconditional. They were verified in-game and
// RenderDoc-diffed against the shipped DLL — see src/render/dx11/README.md §"Per-draw
// pipeline-state equality".

#include "VideoDX11Data.h"
#include "dx11_backend_access.h"
#include "async_gpu_create/async_create.h"   // NOT RETAIL: ext.async_gpu_create
#include "share_gpu_data/share_create.h"     // NOT RETAIL: ext.share_gpu_data
#include "create_on_demand/lazy_create.h"    // NOT RETAIL: ext.create_on_demand
// ENDGAME B.3.5 block D2, RULE B5-3: the three real Runtime abstract bases (B5 BUF:
// now the C++ bases of the three classes below).
#include "video/CompanionData.h"
// RULE B5-2: the real `Video::{Index,Vertex}Buffer` (the two owners).
#include "video/Buffers.h"
// ENDGAME B.3.5 block VC: `Video::ConstantBuffer` (which replaces this file's fifth
// view of it) and `IO::Serializer` (which replaces `SerializerStreamMode`'s
// hand-read offsets — B35_RULES §5.5).
#include "video/ConstantBuffer.h"
#include "video/Device.h"
#include "io/Serializer.h"

#include "system/MemoryAllocator.h"   // B.3R DX: MemAlloc / MemFree, direct

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

namespace VideoDX11 {
namespace {

// ============================================================
// Runtime-x64.dll dependencies — DIRECT REFERENCES (ENDGAME B.3R, block DX)
//
// This was `RuntimeDeps g_deps`, filled once by `EnsureDeps()` (a plain-bool
// guard, reached from both the loader and the render thread) through
// `Exp<T>(mangled)` = `ResolveExport`, plus the address of
// Video::Device::_singleton and the three live vtables; every slot forwarded to
// the original DLL's slot when `g_deps.ok` was false. Both names bound to a body in
// this build, the address of a class static is never null and the seam is
// installed before any of these slots is reachable, so `ok` was always true:
// the struct, `Exp<>`, `EnsureDeps()`, its guards and their forwarding arms are
// GONE, and no cache replaces them (B3R_RULES R2, R8):
//   ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z -> System::MemoryAllocator::MemAlloc
//   ?MemFree@MemoryAllocator@System@@SAXPEAX@Z    -> System::MemoryAllocator::MemFree
//   Video::Device::_singleton                     -> the class static, read live
// The three vtable bases went with B5 BUF: the compiler stores them now.
// ============================================================

// ENDGAME B.3.5 block J converted `Video::Device::Lock` / `Unlock` to member
// calls. B5 BUF: `Device::_singleton` is the typed class static, so the bridge
// cast this helper used to name is gone.
inline ::Video::Device* VideoDeviceObject() {
    return ::Video::Device::_singleton;
}

// ============================================================
// Owner-side classes (Runtime-x64.dll)
//
// Video::VertexBuffer / Video::IndexBuffer — ENDGAME B.3.5 block D2, RULE B5-2
// collapsed the two-field prefix view `GpuBufferOwner` into an alias of
// `Video::IndexBuffer` used for BOTH owners (retail's slots 1, 2, 3 and 6 are ONE
// body each, shared between the two owner classes precisely because the two
// are field-identical). B5 BUF: `_owner` is typed per class, so the shared
// bodies below are TEMPLATES over the owner type and each instantiation reads its
// own class's named `size` / `dynamic` — the alias, and the cross-type cast it
// needed, are gone. `tests/unit/video_device_abi_test.cpp` still restates the
// field identity at runtime.
//
// Video::ConstantBuffer — ENDGAME B.3.5 block VC, RULE B5-2's duplicate-view
// collapse. This was the FIFTH view of this one class: a 0x40-byte prefix with its
// own eight `offsetof` asserts repeating the Runtime-side ones, and no `sizeof`
// assert because only the leading fields were thought known.
// `Video::ConstantBuffer` is real now (src/engine/video/ConstantBuffer.h,
// 0x48 bytes, every field named and asserted, with `ConstantInfo` as a NESTED
// STRUCT carrying the retail name), so the name is an ALIAS instead of a sixth
// description of the layout.
//
// THE FIELD NAMES THE VIEW CHOSE AND WHAT THEY REALLY ARE — each pair is the same
// offset, so this is a rename and not a re-aim:
//   slotIndex     +0x14 -> _uploadIndex     (?GetUploadIndex@ 0x1800182c0; it IS
//                                            the D3D11 StartSlot, so the dx11-side
//                                            name was right about the meaning)
//   bindToVertex  +0x18 -> _bindToVertex
//   banked        +0x19 -> _banked
//   registerCount +0x1c -> _registerCount
//   bankStride    +0x20 -> _bankStride
//   entries       +0x28 -> _constantData    (`ConstantInfo*`, not `void*`)
//   entryCount    +0x30 -> _constantCount
//   device        +0x38 -> _device
//
// The three `kConstantEntry*` byte constants go with it: the element is
// `Video::ConstantBuffer::ConstantInfo` and `size` / `offset` are its named
// fields. The asserts below keep the facts dx11 depends on checked against the
// SHARED definition, so if the two sides ever disagreed this file stops compiling
// instead of reading the wrong field.
// ============================================================
using ConstantBufferOwner = ::Video::ConstantBuffer;
using ConstantEntry       = ::Video::ConstantBuffer::ConstantInfo;
static_assert(offsetof(ConstantBufferOwner, _uploadIndex)   == 0x14, "ConstantBuffer::_uploadIndex");
static_assert(offsetof(ConstantBufferOwner, _bindToVertex)  == 0x18, "ConstantBuffer::_bindToVertex");
static_assert(offsetof(ConstantBufferOwner, _banked)        == 0x19, "ConstantBuffer::_banked");
static_assert(offsetof(ConstantBufferOwner, _registerCount) == 0x1c, "ConstantBuffer::_registerCount");
static_assert(offsetof(ConstantBufferOwner, _bankStride)    == 0x20, "ConstantBuffer::_bankStride");
static_assert(offsetof(ConstantBufferOwner, _constantData)  == 0x28, "ConstantBuffer::_constantData");
static_assert(offsetof(ConstantBufferOwner, _constantCount) == 0x30, "ConstantBuffer::_constantCount");
static_assert(offsetof(ConstantBufferOwner, _device)        == 0x38, "ConstantBuffer::_device");
static_assert(sizeof(ConstantEntry) == 0x18,
              "the entry stride dx11 used to walk by hand as kConstantEntryStride");
static_assert(offsetof(::Video::IndexBuffer, size)    == 0x00, "IndexBuffer::size");
static_assert(offsetof(::Video::IndexBuffer, dynamic) == 0x04, "IndexBuffer::dynamic");
static_assert(offsetof(::Video::VertexBuffer, size)    == 0x00, "VertexBuffer::size");
static_assert(offsetof(::Video::VertexBuffer, dynamic) == 0x04, "VertexBuffer::dynamic");

// IO::Serializer+0x00 is the IO::Stream* it is attached to; +0x08 on that
// stream is an int the DLL compares against 0 and 1.
//
// NOT PROVEN — the enum's name and its full value set are unknown. All that is
// established, from the two Serialize slots themselves, is:
//   0        -> allocate a scratch block, read the payload into it, and rebuild
//               the GPU buffer from it (a load).
//   1        -> CopyResource the live GPU buffer into a STAGING buffer, then map
//               and hand the payload to the serializer (a save).
//   anything -> same as 1 but WITHOUT the CopyResource, so whatever is mapped is
//   else        undefined memory. Most plausibly a size-counting pass, but that
//               is a guess and is not relied on here.
// ENDGAME B.3.4 callers batch H LEFT THIS ONE ALONE, because "this translation
// unit includes no runtime/types header at all, so converting it would mean
// pulling src/engine/io/Serializer.h into the VideoDX11 replacement for one
// expression — a module-boundary decision, not a caller batch's".
//
// B35_RULES §5.5 SETTLES THAT DECISION and block VC acts on it: dx11 now includes
// seven runtime/types headers, this file four of them, so the boundary argument no
// longer holds. The body is `IO::Serializer::GetDirection()` (retail 0x180006210,
// `MOV RAX,[RCX] / MOV EAX,[RAX+8] / RET` — exactly the two loads that were
// written out here), and that retires a SIXTH duplicate offset view.
//
// THE RETURN TYPE STAYS `int32_t` and the two comparisons below stay against 0 and
// 1 rather than against `IO::kReading` / `IO::kWriting`, deliberately: the enum's
// full value set is NOT PROVEN — the case list above has a third behaviour for
// "anything else" — and naming only two of an unknown set would assert more than
// is known.
//
// B5 BUF: the slot's argument is typed `IO::Serializer*` (CompanionData.h), so
// the `void*` bridge cast this helper carried is gone.
inline int32_t SerializerStreamMode(::IO::Serializer* serializer) {
    return static_cast<int32_t>(serializer->GetDirection());
}

// ============================================================
// 0x180001550 — generic Array<T*>::SetLength(int)
//
// A shared utility, not a vtable slot. Only ConstantBufferData slot 1 calls it.
// Reproduced exactly, including the three properties that matter:
//   * growth is x3 but ONLY on a +1 append from a non-empty array; any other
//     resize allocates exactly newLen slots,
//   * capacity never shrinks, so the array is not reallocated when shrinking,
//   * NEW slots are left UNINITIALIZED. ConstantBufferData::Create relies on
//     CreateBuffer filling every one of them; if a CreateBuffer fails partway,
//     the tail of the array holds garbage that the destructor will later
//     Release(). That is an original-code hazard, faithfully preserved.
//
// The array is ConstantBufferData's {buffers, bufferCount, bufferCapacity}
// triple (+0x18 / +0x20 / +0x24 — the {data, count, capacity} shape retail's
// helper takes by address). B5 BUF: `buffers` is typed `ID3D11Buffer**`, so the
// three fields are passed by reference instead of through a `PtrArray` view of
// the object (the reinterpret_cast that view needed is gone).
// ============================================================

void ArraySetLength(ID3D11Buffer**& data, int32_t& count, int32_t& capacity, int newLen) {
    const int oldLen = count;
    if (newLen == oldLen) return;

    ID3D11Buffer** oldData = data;
    count = newLen;

    if (capacity < newLen) {
        capacity = newLen;
        if (newLen == oldLen + 1 && oldLen != 0) capacity = newLen * 3;
        data = static_cast<ID3D11Buffer**>(System::MemoryAllocator::MemAlloc(
            static_cast<unsigned long long>(static_cast<int64_t>(capacity) << 3), 8));
    }

    // When no reallocation happened this loop is empty by construction
    // (start == oldLen and the body requires i < oldLen).
    // The original also tests `&data[i] != nullptr` here — the address of an
    // array element, so the test is dead. Omitted.
    int i = (oldData == data) ? oldLen : 0;
    for (; i < newLen; ++i) {
        if (i < oldLen) {
            data[i] = oldData[i];
        }
    }

    if (oldData != data) System::MemoryAllocator::MemFree(oldData);
}

// 0x180001520 — free-and-null the same array shape. Reached ONLY from MSVC EH
// unwind funclets, which GCC has no way to install and which cannot fire here
// (nothing between the allocation and its owner's constructor throws).
// Deliberately not ported; recorded so the roster stays complete.

// ============================================================
// Shared: VertexBufferData / IndexBufferData slots 3 and 4
//
// 0x180003290 and 0x180003340 are the same two functions in BOTH vtables. One
// implementation each, a template over the companion class, called from both
// classes' `Lock` / `Unlock` members below. The asserts in VideoDX11Data.h prove
// the two classes have the identical layout, which is what retail's single body
// relies on.
// ============================================================

// 0x180003290 — Lock(sizeBytes, &outByteOffset) -> mapped pointer, or null.
//
// A classic dynamic ring buffer: NO_OVERWRITE while the write cursor still has
// room, DISCARD (and rewind to 0) once it does not.
template <class Data>
void* BufferData_Lock(Data* self, int32_t sizeBytes, int32_t* outByteOffset) {
    const auto* owner = self->_owner;

    D3D11_MAP mapType;
    if (self->writeCursor + sizeBytes > owner->size) {
        mapType = D3D11_MAP_WRITE_DISCARD;         // 4
        self->writeCursor = 0;
    } else {
        mapType = D3D11_MAP_WRITE_NO_OVERWRITE;    // 5
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = Dx11Context()->Map(self->buffer, 0, mapType, 0, &mapped);
    if (hr != 0) return nullptr;

    *outByteOffset = self->writeCursor;
    self->locked   = 1;
    // Advance by the request rounded up to an even byte count. The original
    // emits the signed-division idiom for `(n + 1) / 2 * 2`, not an AND mask;
    // for the non-negative sizes this is ever called with the two agree.
    self->writeCursor += ((sizeBytes + 1) / 2) * 2;

    return reinterpret_cast<uint8_t*>(mapped.pData) + *outByteOffset;   // CAST-OK: gpu_map
}

// 0x180003340 — Unlock()
template <class Data>
void BufferData_Unlock(Data* self) {
    Dx11Context()->Unmap(self->buffer, 0);
    self->locked = 0;
}

// ============================================================
// Shared: the Create body behind VertexBufferData slot 1 (0x180003b90) and
// IndexBufferData slot 1 (0x1800030f0).
//
// The two are byte-identical apart from BindFlags, so the shape is factored
// here and the two slots below differ only in that one constant. Both write the
// requested size and dynamic flag back onto the OWNER before doing anything
// else — Create is also the setter.
// ============================================================

// `dynamic` is deliberately uint8_t, not bool: the original stores the raw
// incoming byte on the owner and then compares it against 0. Declaring it bool
// would let the compiler assume 0/1 and test only bit 0.
// The descriptor both Create paths build: retail's synchronous one below, and ext.async_gpu_create's
// deferred one (GpuBuffer_CreateDeferred), which must hand the worker the very same descriptor.
inline D3D11_BUFFER_DESC GpuBuffer_Desc(uint32_t sizeBytes, bool dynamic, const void* initialData,
                                        UINT bindFlags) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeBytes;
    if (dynamic) {
        desc.Usage          = D3D11_USAGE_DYNAMIC;                       // 2
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;                    // 0x10000
    } else {
        // IMMUTABLE when seeded with data, DEFAULT when not.
        desc.Usage          = initialData ? D3D11_USAGE_IMMUTABLE        // 1
                                          : D3D11_USAGE_DEFAULT;         // 0
        desc.CPUAccessFlags = 0;
    }
    desc.BindFlags              = bindFlags;
    desc.MiscFlags              = 0;
    desc.StructureByteStride    = 0;
    return desc;
}

template <class Owner>
bool GpuBuffer_Create(Owner* owner, ID3D11Buffer** outBuffer,
                      uint32_t sizeBytes, uint8_t dynamicByte, const void* initialData,
                      UINT bindFlags) {
    owner->size    = static_cast<int32_t>(sizeBytes);
    owner->dynamic = dynamicByte;

    if (owner->size == 0) return true;

    const bool dynamic = (dynamicByte != 0);

    const D3D11_BUFFER_DESC desc = GpuBuffer_Desc(sizeBytes, dynamic, initialData, bindFlags);

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem          = initialData;
    init.SysMemPitch      = 0;
    init.SysMemSlicePitch = 0;

    // NOT RETAIL (ext.share_gpu_data, docs/extensions/handins/SHAREGPUDATA.md): an IMMUTABLE seeded
    // buffer equal to one already made shares it; anything else, and the row off, is this CreateBuffer.
    HRESULT hr = ShareGpuData::CreateBuffer(
        Dx11Device(), desc, initialData ? &init : nullptr, outBuffer);

    return hr == 0 && *outBuffer != nullptr;
}

// NOT RETAIL (ext.async_gpu_create, docs/extensions/handins/ASYNCGPUCREATE.md): the load arm's
// Create for a static, data-seeded buffer, with the CreateBuffer itself on a worker. The owner's two
// stores and the descriptor are GpuBuffer_Create's; the worker writes `buffer` with what CreateBuffer
// writes (null on failure, as the synchronous arm leaves it). The job owns `scratch` and frees it.
// The load arm discards Create's bool, so there is no answer to give back.
template <class Data> UINT GpuBuffer_BindFlags();
template <> UINT GpuBuffer_BindFlags<VertexBufferData>() { return D3D11_BIND_VERTEX_BUFFER; }
template <> UINT GpuBuffer_BindFlags<IndexBufferData>()  { return D3D11_BIND_INDEX_BUFFER; }

template <class Data>
void GpuBuffer_CreateDeferred(Data* self, uint32_t sizeBytes, void* scratch) {
    auto* owner = self->_owner;
    AsyncCreate::Await(self);   // a re-create of a buffer whose first create is still queued
    owner->size    = static_cast<int32_t>(sizeBytes);
    owner->dynamic = 0;
    const D3D11_BUFFER_DESC desc = GpuBuffer_Desc(sizeBytes, false, scratch, GpuBuffer_BindFlags<Data>());
    AsyncCreate::EnqueueBuffer(self, Dx11Device(), desc, scratch, &self->buffer);
}

// ============================================================
// Shared: the Serialize body behind VertexBufferData slot 6 (0x180003d90) and
// IndexBufferData slot 6 (0x1800033b0). Identical apart from which Create they
// call back into on the load path (B5 BUF: `self->Create`, the companion's own
// slot-1 member — the class is `final`, so it is the same direct call the old
// function pointer made).
// ============================================================

template <class Data>
void GpuBuffer_Serialize(Data* self, ::IO::Serializer* serializer) {
    // B5 BUF: this was `encoding->vtable[0](encoding)`, called blind as "which
    // virtual this is has NOT been identified". It has since been:
    // src/engine/io/Serializer.h declares `IO::Encoding`'s slot 0 (+0x00) as
    // `IsBinary() const`, so the same slot is now called by name. The DLL uses it
    // as an "is this encoding one I can serialize raw bytes through" gate.
    if (!serializer->GetEncoding().IsBinary()) return;

    // NOT RETAIL (ext.async_gpu_create): the save arm reads `buffer`; a deferred create finishes first.
    AsyncCreate::Await(self);
    // NOT RETAIL (ext.create_on_demand): no eviction while this reads `buffer`, and a buffer still
    // waiting for its first use is made first.
    CreateOnDemand::UseScope use(self);
    if (::CreateOnDemand::LoadPublished(self->buffer) == nullptr && SerializerStreamMode(serializer) != 0)
        CreateOnDemand::Ensure(self);

    auto* owner = self->_owner;

    // Order matters: the dynamic flag first, then the size.
    serializer->Serialize(owner->dynamic, nullptr);
    serializer->Serialize(owner->size, nullptr);

    const int32_t sizeBytes = owner->size;
    if (sizeBytes == 0) return;

    if (SerializerStreamMode(serializer) == 0) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand -- a
        // static buffer read from a packed resource file is not read: its create (the descriptor
        // GpuBuffer_Create builds for seeded data) waits for its first use, and the stream skips the
        // payload. The owner already holds the size / dynamic Create would store.
        if (CreateOnDemand::Requested() && owner->dynamic == 0 && self->buffer == nullptr) {
            const D3D11_BUFFER_DESC desc = GpuBuffer_Desc(static_cast<uint32_t>(owner->size), false,
                                                          /*seeded: IMMUTABLE*/ owner, GpuBuffer_BindFlags<Data>());
            if (CreateOnDemand::DeferBuffer(self, serializer->_stream, desc, &self->buffer)) return;
        }
        // Load: read the payload into scratch memory and rebuild the buffer.
        void* scratch = System::MemoryAllocator::MemAlloc(static_cast<unsigned long long>(sizeBytes), 4);
        serializer->SerializeBytes(owner->size, scratch, nullptr);
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/ASYNCGPUCREATE.md): inside an armed loader
        // bracket a static buffer's CreateBuffer goes to a worker, which owns and frees `scratch`.
        if (AsyncCreate::Deferring() && owner->dynamic == 0) {
            GpuBuffer_CreateDeferred(self, static_cast<uint32_t>(owner->size), scratch);
            return;
        }
        self->Create(static_cast<uint32_t>(owner->size), owner->dynamic, scratch);
        System::MemoryAllocator::MemFree(scratch);
        return;
    }

    // Save (and the unidentified third mode): pull the contents back through a
    // STAGING buffer.
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = static_cast<UINT>(sizeBytes);
    desc.Usage          = D3D11_USAGE_STAGING;                                   // 3
    desc.BindFlags      = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;        // 0x30000
    desc.MiscFlags      = 0;
    desc.StructureByteStride = 0;

    ID3D11Buffer* staging = nullptr;

    VideoDeviceObject()->Lock();
    // NOT RETAIL: the Present lock (dx11_backend_access.h), for the staging
    // round trip through the immediate context below.
    std::unique_lock<std::recursive_mutex> presentLock(Dx11PresentMutex());

    ID3D11Device*        dev = Dx11Device();
    ID3D11DeviceContext* ctx = Dx11Context();

    // The original ignores this HRESULT and then unconditionally maps, unmaps
    // and Releases `staging`. On a failed CreateBuffer that is a null
    // dereference. Reproduced rather than "fixed" — silently diverging here
    // would hide a real failure mode from the RenderDoc/A-B comparison.
    dev->CreateBuffer(&desc, nullptr, &staging);

    if (SerializerStreamMode(serializer) == 1) {
        ctx->CopyResource(staging, self->buffer);
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(staging, 0, D3D11_MAP_READ_WRITE, 0, &mapped);                      // 3
    serializer->SerializeBytes(owner->size, mapped.pData, nullptr);
    ctx->Unmap(staging, 0);
    staging->Release();

    presentLock.unlock();
    VideoDeviceObject()->Unlock();
}

// ============================================================
// Shared: the SetData body behind VertexBufferData slot 2 (0x180003c60) and
// IndexBufferData slot 2 (0x1800031c0).
//
// !! The two are byte-identical EXCEPT for one instruction:
//        VertexBufferData  xor r8d, r8d   -> UpdateSubresource DstSubresource = 0
//        IndexBufferData   mov r8d, edi   -> UpdateSubresource DstSubresource = offsetBytes
//    Passing a byte offset as a subresource index is meaningless for a buffer
//    (buffers have exactly one subresource, index 0) and is almost certainly a
//    copy-paste bug in the shipped DLL. It is reproduced verbatim: the D3D11
//    debug layer will complain about it under the original DLL too, and making
//    ours "correct" would make an A/B capture diverge for the wrong reason.
//    See the dstSubresource parameter at each call site below.
// ============================================================

template <class Owner>
bool GpuBuffer_SetData(const Owner* owner, ID3D11Buffer* buffer,
                       int32_t sizeBytes, int32_t offsetBytes, const void* src,
                       UINT dstSubresource) {
    if (owner->size == 0) return true;

    VideoDeviceObject()->Lock();

    ID3D11DeviceContext* ctx = Dx11Context();

    D3D11_BOX box;
    box.left   = static_cast<UINT>(offsetBytes);
    box.top    = 0;
    box.front  = 0;
    box.right  = static_cast<UINT>(sizeBytes + offsetBytes);
    box.bottom = 1;
    box.back   = 1;

    {
        // NOT RETAIL: the Present lock (dx11_backend_access.h). The caller may
        // be the loader thread while the main thread presents.
        std::lock_guard<std::recursive_mutex> presentLock(Dx11PresentMutex());
        ctx->UpdateSubresource(buffer, dstSubresource, &box,
                               src, static_cast<UINT>(sizeBytes), 0);
    }

    VideoDeviceObject()->Unlock();
    return true;
}

}  // namespace

// ============================================================
// VertexBufferData — vtable 0x180018830
// ============================================================

// 0x180003b20 — slot 0: scalar deleting destructor.
//
// B5 BUF: the vptr re-seat, the chain to ??1VertexBufferData@Video@@UEAA@XZ and
// the `flags & 1` MemFree are the compiler's (D0 + the base's operator delete).
// What is left is the body's own work, in retail order: Release the buffer if
// there is one, then clear the three fields.
VertexBufferData::~VertexBufferData() {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): never free under a running create
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): nor under a materialization
    ShareGpuData::DropBuffer(buffer);   // NOT RETAIL (ext.share_gpu_data): one user less, before the Release
    if (buffer) buffer->Release();
    buffer      = nullptr;
    writeCursor = 0;
    locked      = 0;
}

// 0x180003b90 — slot 1: Create(sizeBytes, dynamic, initialData) -> bool.
// BindFlags = D3D11_BIND_VERTEX_BUFFER (1).
bool VertexBufferData::Create(uint32_t sizeBytes, uint8_t dynamicByte, const void* data) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    return GpuBuffer_Create(_owner, &buffer,
                            sizeBytes, dynamicByte, data, D3D11_BIND_VERTEX_BUFFER);
}

// 0x180003c60 — slot 2: SetData(sizeBytes, offsetBytes, src) -> bool.
// DstSubresource is a hard 0 here (contrast IndexBufferData slot 2).
bool VertexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    // NOT RETAIL (ext.create_on_demand): an acquire load, a worker may publish the handle meanwhile
    if (::CreateOnDemand::LoadPublished(buffer) == nullptr) CreateOnDemand::Ensure(this);
    return GpuBuffer_SetData(_owner, buffer,
                             sizeBytes, offsetBytes, src, /*dstSubresource=*/0);
}

// 0x180003290 — slot 3, SHARED with IndexBufferData slot 3 (one retail body).
void* VertexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    // NOT RETAIL (ext.create_on_demand): an acquire load, a worker may publish the handle meanwhile
    if (::CreateOnDemand::LoadPublished(buffer) == nullptr) CreateOnDemand::Ensure(this);
    return BufferData_Lock(this, sizeBytes, outByteOffset);
}

// 0x180003340 — slot 4, SHARED with IndexBufferData slot 4 (one retail body).
void VertexBufferData::Unlock() {
    BufferData_Unlock(this);
}

// 0x180003d30 — slot 5: Bind(?, startSlot, offsetBytes, strideBytes).
//
// The second parameter (RDX) is never read by the original — it is overwritten
// with the Video::Device pointer three instructions in. Almost certainly the
// Video::Context* every other Bind in the DLL is handed and ignores.
void VertexBufferData::Bind(void* /*context, unused*/, uint32_t startSlot,
                            uint32_t offsetBytes, uint32_t strideBytes) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): one relaxed load when idle
    // NOT RETAIL (ext.create_on_demand): the first use of a deferred buffer makes it; a made one
    // pays the null test. The scope stamps the use and keeps an eviction away until the bind is done.
    CreateOnDemand::UseScope use(this);
    if (::CreateOnDemand::LoadPublished(buffer) == nullptr) CreateOnDemand::Ensure(this);
    // The original passes the addresses of its own R9 home slot (offset) and of
    // stack argument 5 (stride); taking the address of our parameters is the
    // same observable call.
    UINT stride = strideBytes;
    UINT offset = offsetBytes;
    Dx11Context()->IASetVertexBuffers(startSlot, 1, &buffer, &stride, &offset);
}

// 0x180003d90 — slot 6: Serialize(IO::Serializer*).
void VertexBufferData::Serialize(::IO::Serializer* serializer) {
    GpuBuffer_Serialize(this, serializer);
}

// ============================================================
// IndexBufferData — vtable 0x1800187c8
// ============================================================

// 0x180003080 — slot 0: scalar deleting destructor. Same shape as
// VertexBufferData's (B5 BUF: the re-seat, base chain and MemFree are the
// compiler's).
IndexBufferData::~IndexBufferData() {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): never free under a running create
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): nor under a materialization
    ShareGpuData::DropBuffer(buffer);   // NOT RETAIL (ext.share_gpu_data): one user less, before the Release
    if (buffer) buffer->Release();
    buffer      = nullptr;
    writeCursor = 0;
    locked      = 0;
}

// 0x1800030f0 — slot 1: Create(sizeBytes, dynamic, initialData) -> bool.
// BindFlags = D3D11_BIND_INDEX_BUFFER (2) — the ONLY difference from the
// VertexBufferData version.
bool IndexBufferData::Create(uint32_t sizeBytes, uint8_t dynamicByte, const void* data) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    return GpuBuffer_Create(_owner, &buffer,
                            sizeBytes, dynamicByte, data, D3D11_BIND_INDEX_BUFFER);
}

// 0x1800031c0 — slot 2: SetData(sizeBytes, offsetBytes, src) -> bool.
//
// DstSubresource = offsetBytes. See the warning on GpuBuffer_SetData: this is
// the shipped DLL's behaviour, bug and all.
bool IndexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    // NOT RETAIL (ext.create_on_demand): an acquire load, a worker may publish the handle meanwhile
    if (::CreateOnDemand::LoadPublished(buffer) == nullptr) CreateOnDemand::Ensure(this);
    return GpuBuffer_SetData(_owner, buffer,
                             sizeBytes, offsetBytes, src,
                             /*dstSubresource=*/static_cast<UINT>(offsetBytes));
}

// 0x180003290 — slot 3, SHARED with VertexBufferData slot 3 (one retail body).
void* IndexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    // NOT RETAIL (ext.create_on_demand): an acquire load, a worker may publish the handle meanwhile
    if (::CreateOnDemand::LoadPublished(buffer) == nullptr) CreateOnDemand::Ensure(this);
    return BufferData_Lock(this, sizeBytes, outByteOffset);
}

// 0x180003340 — slot 4, SHARED with VertexBufferData slot 4 (one retail body).
void IndexBufferData::Unlock() {
    BufferData_Unlock(this);
}

// 0x180003380 — slot 5: Bind().
//
// Not a Ghidra function: a 45-byte body sitting immediately after Unlock
// (0x180003340) that the analyser folded into its predecessor. Decoded by hand
// from `read_memory(0x180003380, 64)`:
//   48 8b 05 ..   mov  rax, [rip+0x10131]   ; -> Video::Device::_singleton
//   4c 8b d1      mov  r10, rcx             ; this
//   45 33 c9      xor  r9d, r9d             ; Offset = 0
//   48 8b 10      mov  rdx, [rax]
//   45 8d 41 39   lea  r8d, [r9+0x39]       ; Format = 0x39 = DXGI_FORMAT_R16_UINT
//   ...
//   48 ff a0 98.. jmp  [rax+0x98]           ; IASetIndexBuffer
//
// The hard-coded R16_UINT is what proves Banished indices are always 16-bit.
// ENDGAME B.3.5 block J: the SLOT takes a second argument this body never reads.
// `?BindResources@Context@Video@@`'s index-buffer arm sets RDX to the Context
// (0x1800ef1e2) before dispatching here, so the parameter is spelled out now --
// `Video::IndexBufferData::Bind(void*)` in src/engine/video/CompanionData.h,
// corrected this pass from a no-argument declaration. Ignoring it is retail's
// behaviour; not declaring it was this file's.
void IndexBufferData::Bind(void* /*context, never read*/) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): one relaxed load when idle
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): stamp, no eviction meanwhile
    // NOT RETAIL (ext.create_on_demand): an acquire load, a worker may publish the handle meanwhile
    if (::CreateOnDemand::LoadPublished(buffer) == nullptr) CreateOnDemand::Ensure(this);
    Dx11Context()->IASetIndexBuffer(buffer, DXGI_FORMAT_R16_UINT, 0);
}

// 0x1800033b0 — slot 6: Serialize(IO::Serializer*).
void IndexBufferData::Serialize(::IO::Serializer* serializer) {
    GpuBuffer_Serialize(this, serializer);
}

// ============================================================
// ConstantBufferData — vtable 0x1800185d0
// ============================================================

// Factory 0x180005730's stores after the base ctor and the vptr: the -1 sentinel
// ("no rung bound yet", 0x180005758) and four zeroes. Then — NOT retail — the
// object joins the reset registry so DeviceData_Resize can drop its "already
// bound" cache when it calls ClearState(); see the note above
// Dx11InvalidateConstantBufferBindings below.
ConstantBufferData::ConstantBufferData(::Video::ConstantBuffer* owner)
    : ::Video::ConstantBufferData(owner) {
    activeIndex    = -1;          // sentinel: no rung bound yet
    pad14          = 0;
    buffers        = nullptr;
    bufferCount    = 0;
    bufferCapacity = 0;
    Dx11TrackConstantBuffer(this);
}

// 0x180001240 — slot 0: scalar deleting destructor, which calls
// 0x180001270 — ~ConstantBufferData (not a vtable slot) and then MemFrees on
// `flags & 1`.
//
// B5 BUF: the vptr store, the chain to ??1ConstantBufferData@Video@@UEAA@XZ and
// the conditional MemFree are the compiler's. The body keeps the rest in its
// order: untrack from the reset registry (ours, not retail), Release every
// buffer in [0, count), MemFree the block, clear the triple. The compiler's vptr
// store now precedes the untrack instead of following it; the untrack makes no
// virtual call, so nothing can observe the difference.
ConstantBufferData::~ConstantBufferData() {
    Dx11UntrackConstantBuffer(this);
    for (int i = 0; i < bufferCount; ++i) {
        ID3D11Buffer* b = buffers[i];
        if (b) b->Release();
    }
    // Unconditional — MemFree(nullptr) is the allocator's problem, as in the
    // original, which does not null-check either.
    System::MemoryAllocator::MemFree(buffers);
    buffers = nullptr;
    // One qword store clears count and capacity together, exactly as compiled.
    bufferCount    = 0;
    bufferCapacity = 0;
}

// 0x180001300 — slot 1: Create() -> bool.
//
// Two jobs:
//   1. Walk the owner's entry table and stamp each entry's byte offset
//      (entry[i].[0x14]) as the running sum of the preceding entry sizes
//      (entry[i].[0x10]). This is a layout pass, not a GPU operation.
//   2. Allocate the ID3D11Buffer(s). Un-banked owners get exactly one buffer of
//      the full size. Banked owners get a LADDER: buffer i has size
//      bankStride*(i+1), so a partial update can bind the smallest buffer that
//      still holds the data — that is what slot 2's `size/bankStride - 1` index
//      selects.
//
// Note this slot takes its ID3D11Device from the OWNER (Video::ConstantBuffer
// +0x38 -> Video::Device -> +0x100 -> +0x20), not from the module singleton.
bool ConstantBufferData::Create() {
    ConstantBufferOwner* owner = _owner;

    // 1. offsets. (The original re-loads owner->entries and owner->entryCount
    // on every iteration; neither can change, so the loop is hoisted here.)
    int32_t running = 0;
    for (int i = 0; i < owner->_constantCount; ++i) {
        ConstantEntry& entry = owner->_constantData[i];
        entry.offset = running;
        running += entry.size;
    }

    const int32_t totalBytes = owner->_registerCount << 4;

    // `Video::Device::deviceData` (+0x100) is the named member now, typed
    // `Video::DeviceData*`; B5 DEV-A made dx11's `DeviceData` a real subclass, so
    // the step to it is the one downcast dx11 makes (Dx11DeviceDataOf).
    DeviceData* dd = Dx11DeviceDataOf(owner->_device);
    ID3D11Device* dev = dd->d3dDevice;
    // No device yet -> nothing to build, and the original reports success.
    if (!dev) return true;

    HRESULT hr;
    if (!owner->_banked) {
        ArraySetLength(buffers, bufferCount, bufferCapacity, 1);

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth           = static_cast<UINT>(totalBytes);
        desc.Usage               = D3D11_USAGE_DEFAULT;               // 0
        desc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;        // 4
        desc.CPUAccessFlags      = 0;
        desc.MiscFlags           = 0;
        desc.StructureByteStride = 0;

        hr = dev->CreateBuffer(&desc, nullptr, buffers);
    } else {
        ArraySetLength(buffers, bufferCount, bufferCapacity, totalBytes / owner->_bankStride);

        // Seeded to a failure value so an empty ladder reports false, matching
        // the `or eax, 0xffffffff` before the loop.
        hr = -1;
        for (int i = 0; i < bufferCount; ++i) {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth           = static_cast<UINT>(owner->_bankStride * (i + 1));
            desc.Usage               = D3D11_USAGE_DEFAULT;
            desc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags      = 0;
            desc.MiscFlags           = 0;
            desc.StructureByteStride = 0;

            hr = dev->CreateBuffer(&desc, nullptr, buffers + i);
            if (hr != 0) break;
        }
    }
    return hr == 0;
}

// 0x180001460 — slot 2: Bind/Update(?, srcData, ?, sizeBytes).
//
// Parameters 2 (RDX) and 4 (R9) are never read by the original. The payload
// pointer arrives in R8 and the byte count as stack argument 5.
//
// Picks the ladder rung that fits, re-binds only when the rung changed (that
// `activeIndex != idx` guard is the whole point of the class), then pushes the
// payload with a full-resource UpdateSubresource.
// ENDGAME B.3.5 block J: the fourth argument is an INT, not a pointer. Retail's
// owner-side trampoline is
// `?Bind@ConstantBuffer@Video@@QEAAXAEAVContext@2@PEAXHH@Z` = (Context&, void*,
// int, int), and all five dispatches in `?BindResources@Context@Video@@` build it
// with `XOR R9D,R9D` -- a DWORD zero. `Video::ConstantBufferData::Bind` in
// CompanionData.h is corrected to match; neither ignored argument is read here.
void ConstantBufferData::Bind(void* /*context, never read*/, const void* srcData,
                              int32_t /*first, never read*/, int32_t sizeBytes) {
    ConstantBufferOwner* owner = _owner;
    ID3D11DeviceContext* ctx = Dx11Context();

    const int32_t index = owner->_banked ? (sizeBytes / owner->_bankStride - 1) : 0;

    if (activeIndex != index) {
        activeIndex = index;
        ID3D11Buffer** slot = buffers + index;
        if (owner->_bindToVertex == 0) {
            ctx->PSSetConstantBuffers(static_cast<UINT>(owner->_uploadIndex), 1, slot);
        } else {
            ctx->VSSetConstantBuffers(static_cast<UINT>(owner->_uploadIndex), 1, slot);
        }
    }

    ctx->UpdateSubresource(buffers[activeIndex], 0, nullptr, srcData, 0, 0);
}

// 0x180001450 — slot 3: `return owner->_uploadIndex + 1`.
//
// Not a Ghidra function; a 10-byte tail after ~ConstantBufferData. Bytes:
//   48 8b 41 08   mov eax-path: rax = [rcx+8]     ; owner
//   8b 40 14      mov eax, [rax+0x14]             ; owner->_uploadIndex
//   ff c0         inc eax
//   c3            ret
//
// The Runtime-side name is unknown. Given +0x14 is the StartSlot handed to
// VS/PSSetConstantBuffers, "index + 1" reads as a required-slot-count query,
// but that is inference, not evidence.
int32_t ConstantBufferData::SlotIndexPlusOne() {
    return _owner->_uploadIndex + 1;
}

// ============================================================
// Constant-buffer binding registry — see dx11_backend_access.h.
//
// DELIBERATE DEVIATION from the shipped DLL. ConstantBufferData::Bind's
// `activeIndex != index` guard (0x180001460) caches "this rung is already bound
// to my shader slot". DeviceData::Resize (0x180004340) calls
// ID3D11DeviceContext::ClearState(), which drops every constant-buffer binding
// on the floor while leaving `activeIndex` alone — so the guard skips the
// rebind on every subsequent frame and the shaders read a NULL constant buffer
// from then on. Zeroed transform matrices collapse every vertex, nothing
// rasterises, and the window stays black until the process exits. Measured:
// after a resize the same ConstantBufferData objects come back with
// activeIndex == index == 0 and VS/PSSetConstantBuffers is never called again.
//
// The shipped DLL has the same hole; it only stays hidden while something keeps
// changing the rung (a differently sized update) and re-binding as a side
// effect. Resetting the cache where the bindings are actually dropped is the
// narrowest correct fix, and it costs one rebind per buffer per device reset
// rather than a rebind per update, so the guard keeps doing its job.
//
// Touched on create, on destroy, and once per device reset — never per draw.
namespace {
std::mutex&                       CbRegistryLock() { static std::mutex m; return m; }
std::vector<ConstantBufferData*>& CbRegistry()     { static std::vector<ConstantBufferData*> v; return v; }
}  // namespace

void Dx11TrackConstantBuffer(ConstantBufferData* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lock(CbRegistryLock());
    CbRegistry().push_back(p);
}

void Dx11UntrackConstantBuffer(ConstantBufferData* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lock(CbRegistryLock());
    std::vector<ConstantBufferData*>& v = CbRegistry();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == p) { v[i] = v.back(); v.pop_back(); return; }
    }
}

// DeviceData_Resize runs on the render thread (ContextData::BeginCommands) and on the
// window-message thread (DeviceData_OnActivate), and BeginCommands reads the counter on the
// render thread, so it is atomic. Relaxed is enough: it only has to be seen to change.
namespace { std::atomic<unsigned> g_clearStateGeneration{0}; }
void     Dx11NoteClearState()        { g_clearStateGeneration.fetch_add(1, std::memory_order_relaxed); }
unsigned Dx11ClearStateGeneration()  { return g_clearStateGeneration.load(std::memory_order_relaxed); }

void Dx11InvalidateConstantBufferBindings() {
    std::lock_guard<std::mutex> lock(CbRegistryLock());
    for (ConstantBufferData* p : CbRegistry())
        p->activeIndex = -1;   // the factory's "no rung bound yet" sentinel
}

}  // namespace VideoDX11
