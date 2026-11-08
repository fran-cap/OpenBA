// ============================================================================
// src/engine/video/buffers.cpp — Wave 6 lane C — Video::ImageBuffer +
// Video::ImageBufferData + Video::IndexBuffer + Video::VertexBuffer
// ============================================================================
//
// Scope: the 22 functions in docs/history/runtime_port/wave6_lanes.txt under
// "=== LANE C ===". Every mangled name below was checked against the real
// export table of C:\GOG Games\Banished\Runtime-x64.dll with
// tools/pe_exports.py BEFORE this file was written (all 22 present — see the
// per-function table in docs/history/runtime_port/WAVE6_LANE_C.md).
//
// SHAPE OF THIS LANE, IN ONE PARAGRAPH: `IndexBuffer`/`VertexBuffer` carry no
// vtable of their own — `Create`/`Set` are two-instruction tail-call
// trampolines into a platform buffer object living at `this+0x08`
// (dx11::IndexBufferData / VertexBufferData — src/render/dx11/VideoDX11Data.h,
// NOT this lane's file). `ImageBuffer::CopyToTexture`/`CopyFromTexture`/
// `Create` are the same shape, dispatching through `this+0x60`
// (dx11::ImageBufferData, src/render/dx11/imagebufferdata.cpp). Since VideoDX11 is
// ALREADY OURS (PLAN.md's "Why this wave is different" note), every one of
// those trampolines reaches a vtable this project already owns on both sides
// of the port — see the per-function oracle notes below for what that means
// for diffing.
//
// The remaining functions are the true leaves: nine pure accessors with a
// full differential oracle, three constructors, two destructors, and one
// held-back function (SaveImageToFile — see its own comment).

#include "video/ImageBuffer.h"
#include "video/Buffers.h"
#include "video/Device.h"   // Wave 9 lane W: the lock and +0x100
#include "video/CompanionData.h"  // ENDGAME B.3.5: Video::{Index,Vertex}BufferData
#include "system/MemoryAllocator.h"

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <new>       // ENDGAME B.3.9 VS: placement new (the factory pair)

namespace {

// ENDGAME B.3.5 block VC — `ObjectExternalDtor()` IS GONE. It was an
// `RT_EXPORT_ONCE(DtorFn, "??1External@Object@@UEAA@XZ")` (binding spelling 3)
// that `~ImageBuffer` used to reach its own base destructor with, next to a
// hand-written stamp of `??_7CompiledExternal@Object@@6B@`. Both are the
// DESTRUCTOR CHAIN now: `Video::ImageBuffer` really derives from
// `Object::CompiledExternal`, so `~CompiledExternal()` and then `~External()`
// run after the member body with no resolver on the path — the same retirement
// `Video::VertexLayout` made in b35-B, and `Object::External`'s own banner
// (`src/engine/object/external_class.cpp:128-135`) is the settled rule for why
// the two base vptr stores are the compiler's and not hand-written on a
// converted object.

// ENDGAME B.3.9 VS: `ImageBufferVtable()` -- the by-name read of the minted
// `??_7ImageBuffer@Video@@6B@` the ctor door stamped -- is GONE with the door and
// the minted row. Every ImageBuffer is built by the real constructor and wears
// the compiler's table (B39_RULES D1).

// B.4 (R-VIDEO): the singleton-slot and list-head accessors that handed out
// `void**` views of `Device::_singleton`, `Configuration::_singleton` and the
// four class-static list heads are gone: every body below names the typed class
// static. The Device lock (buffers_lock.cpp, declared in Device.h) takes the
// ADDRESS of `Device::_singleton` because retail's unlock RE-READS it.

// The three offsets buffers_lock.cpp used to hardcode are its field names now;
// the asserts stay as the layout's statement of the lock's shape.
static_assert(offsetof(Video::Device, lock) == 0x60, "Device::lock");
static_assert(offsetof(Video::Device, lockOwnerThreadId) == 0x88, "Device::lockOwnerThreadId");
static_assert(offsetof(Video::Device, lockRecursionCount) == 0x8C, "Device::lockRecursionCount");

// The intrusive circular doubly-linked list every Platform{Construct,Destruct}
// pair maintains, at the two link fields that class uses (+0x50/+0x58 for
// ImageBuffer, +0x10/+0x18 for Index/VertexBuffer, +0x18/+0x20 for
// RenderBuffer). `next` is the field written with the list head; `prev` is the
// one written with the head's own prev. B.4 (R-VIDEO): the two fields are
// pointer-to-members of the class, so the links are written by name.
//
// TRANSCRIBED, not reasoned about: all four decompiles agree instruction for
// instruction, including the two oddities. In Link, the last write is spelled
// `(*(this->next))->prev = this` rather than `head->prev = this` — the same
// address by construction, kept because retail reloads `this->next` to get it.
// In Unlink, the `if (*first == self) *first = nullptr` AFTER `*first` was
// already advanced to `self->next` is not dead: for a one-element list
// `self->next == self`, so the advance puts `self` back and this clears it.
template <class T>
void ListLink(T* self, T*& first, T* T::*next, T* T::*prev) {
    if (first == nullptr) {
        first = self;
        self->*next = self;
        self->*prev = self;
    } else {
        T* tail = first->*prev;
        self->*prev = tail;
        self->*next = first;
        tail->*next = self;
        (self->*next)->*prev = self;
    }
}

// A self whose links are null is not on the list: a genuine early-out.
template <class T>
void ListUnlink(T* self, T*& first, T* T::*next, T* T::*prev) {
    T* p = self->*prev;
    T* n = self->*next;
    if (p == nullptr || n == nullptr)
        return;
    if (first == self)
        first = n;
    p->*next = n;
    n->*prev = p;
    self->*prev = nullptr;
    self->*next = nullptr;
    if (first == self)
        first = nullptr;
}

// Wave 9 lane GFX2. Read off 0x1800f4381..0x1800f43bd: retail loads the tail as
// `[head + 0x20]` and writes `this + 0x18 = head`, which is ListLink's
// `prev = +0x20` / `next = +0x18`. Independently cross-checked against the
// class's own list WALKER: dynamic_resources.cpp records that all four walkers
// step the `prev` field (ImageBuffer +0x58, Index/Vertex +0x18, RenderBuffer
// +0x20), and +0x20 is exactly what this assignment calls `prev`. (Buffers.h
// names the Index/Vertex +0x10 `listPrev` and +0x18 `listNext`; in ListLink's
// terms +0x10 is `next` and +0x18 `prev`. The offsets are retail's, the names
// are guesses and are not changed here.)
static_assert(offsetof(Video::ImageBuffer, _nextSurface) == 0x50, "ImageBuffer list next");
static_assert(offsetof(Video::ImageBuffer, _prevSurface) == 0x58, "ImageBuffer list prev");
static_assert(offsetof(Video::IndexBuffer, listPrev) == 0x10, "IndexBuffer list next");
static_assert(offsetof(Video::IndexBuffer, listNext) == 0x18, "IndexBuffer list prev");
static_assert(offsetof(Video::VertexBuffer, listPrev) == 0x10, "VertexBuffer list next");
static_assert(offsetof(Video::VertexBuffer, listNext) == 0x18, "VertexBuffer list prev");
static_assert(offsetof(Video::RenderBuffer, next) == 0x18, "RenderBuffer list next");
static_assert(offsetof(Video::RenderBuffer, prev) == 0x20, "RenderBuffer list prev");

// The companion fields the Platform bodies store to (the four descriptors'
// `companionOff` before B.4).
static_assert(offsetof(Video::ImageBuffer, _data)          == 0x60, "ImageBuffer companion");
static_assert(offsetof(Video::IndexBuffer, platformData)   == 0x08, "IndexBuffer companion");
static_assert(offsetof(Video::VertexBuffer, platformData)  == 0x08, "VertexBuffer companion");
static_assert(offsetof(Video::RenderBuffer, platformData)  == 0x28, "RenderBuffer companion");

}  // namespace

// ============================================================================
// ??0ImageBuffer@Video@@QEAA@XZ (0x1800f2580) / ??1ImageBuffer@Video@@UEAA@XZ
// (0x1800f2610) -- ENDGAME B.3.9 VS: BOTH DOORS ARE GONE.
//
// `Video_ImageBuffer_Ctor` stamped the minted `??_7ImageBuffer@Video@@6B@` and ran
// the shared body; `Video_ImageBuffer_Dtor` was the `~Class` the RT_DELDTOR row
// (0x18001ed20) resolved by address. Every builder now writes
// `::new (p) Video::ImageBuffer()` (Runtime: context_ctor, imagebuffer_vtable's
// Construct / Reconstruct, the Graphics and Save builders; exe: EXA), the row is
// retired, and nothing reaches either door by name or by address. The bodies
// are the members at `namespace Video` scope below.
// ============================================================================

// ============================================================================
// Wave 9 lane W — Video::{Image,Index,Vertex}Buffer::Platform{Construct,Destruct}
//
//   0x1800f2f40  ?PlatformConstruct@ImageBuffer@Video@@AEAAXXZ    ord 3337
//   0x1800f3020  ?PlatformDestruct@ImageBuffer@Video@@AEAAXXZ     ord 3365
//   0x1800f3680  ?PlatformConstruct@IndexBuffer@Video@@QEAAXXZ    ord 3338
//   0x1800f3760  ?PlatformDestruct@IndexBuffer@Video@@QEAAXXZ     ord 3366
//   0x1800f57e0  ?PlatformConstruct@VertexBuffer@Video@@QEAAXXZ   ord 3345
//   0x1800f58c0  ?PlatformDestruct@VertexBuffer@Video@@QEAAXXZ    ord 3373
//
// ALL SIX ARE THE SAME FUNCTION with three parameters — the companion factory
// slot on Video::Device's DeviceData vtable, the two link fields, and which
// class-static list head. Construct: make the dx11 companion
// (`dd = Device::_singleton->deviceData; companion = dd ? dd->Create…Data(this)
// : nullptr`), store it, take Device's lock, splice `this` into the class's
// circular list, drop the lock. Destruct: release the companion through its
// slot-0 deleting destructor with flag 1, take the lock, unsplice, drop the
// lock.
//
// THE COMPANION FACTORY IS ALREADY OURS. Device+0x100 is dx11::DeviceData
// (src/render/dx11/devicedata.cpp) and slots 10/12/13/19 (+0x50/+0x60/+0x68/+0x98) are
// its Image/Index/Vertex/Render companion factories. B.4 (R-VIDEO): each is the
// virtual call on `Video::DeviceData` it always dispatched as, by name.
//
// The ONE place the classes differ in Destruct: Index/VertexBuffer write
// `this+8 = 0` after releasing, ImageBuffer and RenderBuffer leave the freed
// pointer in place. That asymmetry is in the shipped bytes of all four and is
// reproduced rather than tidied.
// ============================================================================

// ============================================================================
// ENDGAME B.3.5 block VC — THE `Video::ImageBuffer` MEMBER BODIES.
// ============================================================================
namespace Video {

// ??1ImageBuffer@Video@@UEAA@XZ — 0x1800f2610, ordinal 608.
//
// Retail's four steps, and where each one is now:
//   1. stamp ImageBuffer's own vtable          — the compiler's entry store
//   2. PlatformDestruct                        — the member call
//   3. release the `_sourceName` String by hand — below; `_sourceName` keeps the
//      `void*` type precisely so the compiler does NOT release it a second time
//   4. stamp CompiledExternal's vtable, then tail-jump ~External
//                                              — the DESTRUCTOR CHAIN
// Steps 2 and 3 are `ReleaseFields`, kept out of line only so the unit tests
// can link recording stand-ins.
ImageBuffer::~ImageBuffer() { ReleaseFields(); }

// ??0ImageBuffer 0x1800f2580 after the inlined External stores and the stamp
// (both the compiler's, in `ImageBuffer()`).
void ImageBuffer::InitFields() {
    _width = 0;
    _height = 0;
    _dimension = 0;
    _kind = 2;         // default/none
    _format = 0;
    _flags = 0;
    _addressU = 0;
    _addressV = 0;
    _addressW = 0;
    _filter = 1;       // LINEAR
    // +0x48 = 0 is the System::String member's own ctor, run before this (B4 X-RH).
    // +0x50/+0x58 are not written by the ctor.
    PlatformConstruct();
}

// ?CreateNewObject@ImageBuffer@Video@@SAPEAVBase@Object@@XZ  0x18001e9f0 (read,
// Runtime-x64.dll): the class `operator new` inlined (Enter(alloc+0x640);
// Allocate(0x68, 8); Leave), the null branch, then `??0ImageBuffer` -- ENDGAME
// B.3.9 VS, the class's own pair (B39_RULES D2); the FACTREST row is deleted.
Object::Base* ImageBuffer::CreateNewObject() {
    void* p = ImageBuffer::operator new(sizeof(ImageBuffer));
    if (p == nullptr) return nullptr;
    return ::new (p) ImageBuffer();
}
// ?CreateInPlaceObject@ImageBuffer@Video@@SAPEAVBase@Object@@PEAX@Z  0x18001ea70
Object::Base* ImageBuffer::CreateInPlaceObject(void* storage) {
    return storage ? ::new (storage) ImageBuffer() : nullptr;
}

// ??1ImageBuffer 0x1800f2610 step 2 (see above). Step 3, the release of
// `_sourceName` (0x1800f2636..0x1800f2658), is the System::String member's
// destructor, which the compiler runs after ~ImageBuffer's body and before
// ~External (B4 X-RH).
void ImageBuffer::ReleaseFields() {
    PlatformDestruct();
}

// ?Width@ImageBuffer@Video@@QEBAHXZ  0x1800f2530
// Bit 8 = the live device width, bit 9 = half of it, otherwise the stored value
// right-shifted by `Configuration::shadowScalar & 0x1f` when bit 6 is set.
// Both singletons are read at the point retail reads them.
int32_t ImageBuffer::Width() const {
    if ((_flags & kViewportSize) != 0)
        return Device::_singleton->backBufferWidth;
    if ((_flags & kHalfViewportSize) != 0)
        return Device::_singleton->backBufferWidth / 2;
    int32_t v = _width;
    if ((_flags & kShadowMap) != 0) {
        const uint32_t shift =
            static_cast<uint32_t>(Configuration::_singleton->shadowScalar) & 0x1f;
        v >>= shift;
    }
    return v;
}

// ?Height@ImageBuffer@Video@@QEBAHXZ  0x1800f24e0 — the same ladder on +8.
int32_t ImageBuffer::Height() const {
    if ((_flags & kViewportSize) != 0)
        return Device::_singleton->backBufferHeight;
    if ((_flags & kHalfViewportSize) != 0)
        return Device::_singleton->backBufferHeight / 2;
    int32_t v = _height;
    if ((_flags & kShadowMap) != 0) {
        const uint32_t shift =
            static_cast<uint32_t>(Configuration::_singleton->shadowScalar) & 0x1f;
        v >>= shift;
    }
    return v;
}

// ?GetLevelCount@ImageBuffer@Video@@QEBAHXZ  0x1800f2420 — the mip ladder, which
// halves BOTH dimensions with a floor of 1 and counts until both reach 1.
int32_t ImageBuffer::GetLevelCount() const {
    int32_t count = 1;
    if (HasMipMaps()) {
        int32_t w = _width;
        int32_t h = _height;
        while (w > 1 || h > 1) {
            h >>= 1; if (h == 0) h = 1;
            w >>= 1; if (w == 0) w = 1;
            ++count;
        }
    }
    return count;
}

// The five `_kind` tests and the format test, each one `CMP` against retail's
// own constant. `IsRenderBuffer` is `_kind & ~0b101 == 0`, i.e. `_kind` in
// {0,1,4,5} — deliberately NOT the same set as `dynamic_resources.cpp`'s
// per-element guard, which also admits 3.
bool ImageBuffer::IsCompressed() const   { return _format < 3; }
bool ImageBuffer::HasMipMaps() const     { return (_flags & kMipmaps) != 0; }
bool ImageBuffer::IsDepthBuffer() const  { return _kind == 1; }
bool ImageBuffer::IsDynamic() const      { return _kind == 3; }
bool ImageBuffer::IsGenerated() const    { return _kind == 4; }
bool ImageBuffer::IsRendered() const     { return _kind == 5; }
bool ImageBuffer::IsRenderBuffer() const {
    return (static_cast<uint32_t>(_kind) & 0xfffffffau) == 0;
}

// ?CopyToTexture@ / ?CopyFromTexture@ — 0x1800f3380 / 0x1800f33a0, two-
// instruction tail-call trampolines in retail: `this = this->_data; jmp
// [vtable + N]`. Slots 5 and 6 of `Video::ImageBufferData`, i.e. +0x28 and +0x30.
// Both land in src/render/dx11/imagebufferdata.cpp, which is ours on both sides of the
// port, so there is nothing on the far side to diverge from.
bool ImageBuffer::CopyToTexture(void* dst, int32_t width, int32_t height) {
    return _data->Update(dst, width, height);
}

bool ImageBuffer::CopyFromTexture(void* src, int32_t width, int32_t height) {
    return _data->Read(src, width, height);
}

// slot 9 — ?Create@ImageBuffer@Video@@UEAA_N…  0x1800f2680.
// Ten descriptor field stores in retail's order (note it interleaves
// dimension/width and kind/height), then a genuine two-argument call into
// `Video::ImageBufferData` slot 3.
bool ImageBuffer::Create(ImageType type, Usage usage, Format format, uint32_t flags,
                         int32_t width, int32_t height, FilterMode filter,
                         AddressMode addressU, AddressMode addressV,
                         AddressMode addressW, LevelArray* levels) {
    _dimension = type;
    _width     = width;
    _kind      = usage;
    _height    = height;
    _format    = format;
    _flags     = flags;
    _filter    = filter;
    _addressU  = addressU;
    _addressV  = addressV;
    _addressW  = addressW;

    return _data->Create(levels);
}

// ?PlatformConstruct@ImageBuffer@Video@@AEAAXXZ  0x1800f2f40, ordinal 3337
// ?PlatformDestruct@ImageBuffer@Video@@AEAAXXZ   0x1800f3020, ordinal 3365
// The ImageBuffer arm does NOT null the companion field after releasing it.
void ImageBuffer::PlatformConstruct() {
    DeviceData* dd = Device::_singleton->deviceData;
    _data = dd ? dd->CreateImageBufferData(this) : nullptr;       // slot 10, +0x50
    RtVideoDeviceLock(&Device::_singleton);
    ListLink(this, _firstSurface, &ImageBuffer::_nextSurface, &ImageBuffer::_prevSurface);
    RtVideoDeviceUnlock(&Device::_singleton);
}

void ImageBuffer::PlatformDestruct() {
    // B5 RES (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
    // virtual destructor, then the base's class-scope operator delete (MemFree).
    if (ImageBufferData* companion = _data)
        delete companion;
    RtVideoDeviceLock(&Device::_singleton);
    ListUnlink(this, _firstSurface, &ImageBuffer::_nextSurface, &ImageBuffer::_prevSurface);
    RtVideoDeviceUnlock(&Device::_singleton);
}

}  // namespace Video

// ENDGAME B.3.5 block D2 — the four buffer `Platform*` bodies are members of the
// real `Video::IndexBuffer` / `Video::VertexBuffer`.
void Video::IndexBuffer::PlatformConstruct() {
    DeviceData* dd = Device::_singleton->deviceData;
    platformData = dd ? dd->CreateIndexBufferData(this) : nullptr;   // slot 12, +0x60
    RtVideoDeviceLock(&Device::_singleton);
    ListLink(this, _firstBuffer, &IndexBuffer::listPrev, &IndexBuffer::listNext);
    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::IndexBuffer::PlatformDestruct() {
    // B5 BUF (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
    // virtual destructor, then the base's class-scope operator delete (MemFree).
    if (IndexBufferData* companion = platformData)
        delete companion;
    platformData = nullptr;
    RtVideoDeviceLock(&Device::_singleton);
    ListUnlink(this, _firstBuffer, &IndexBuffer::listPrev, &IndexBuffer::listNext);
    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::VertexBuffer::PlatformConstruct() {
    DeviceData* dd = Device::_singleton->deviceData;
    platformData = dd ? dd->CreateVertexBufferData(this) : nullptr;  // slot 13, +0x68
    RtVideoDeviceLock(&Device::_singleton);
    ListLink(this, _firstBuffer, &VertexBuffer::listPrev, &VertexBuffer::listNext);
    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::VertexBuffer::PlatformDestruct() {
    // B5 BUF (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
    // virtual destructor, then the base's class-scope operator delete (MemFree).
    if (VertexBufferData* companion = platformData)
        delete companion;
    platformData = nullptr;
    RtVideoDeviceLock(&Device::_singleton);
    ListUnlink(this, _firstBuffer, &VertexBuffer::listPrev, &VertexBuffer::listNext);
    RtVideoDeviceUnlock(&Device::_singleton);
}

// ============================================================================
// Wave 9 lane GFX2 — Video::RenderBuffer::Platform{Construct,Destruct}
//
//   0x1800f4320  ?PlatformConstruct@RenderBuffer@Video@@AEAAXXZ   ord 3341
//   0x1800f4400  ?PlatformDestruct@RenderBuffer@Video@@AEAAXXZ    ord 3369
//
// The FOURTH instantiation of the family above. Slot 0x98 == DeviceData slot
// 19, `CreateRenderBufferData`. 0x1800f4400 releases the companion at +0x28
// and leaves the freed pointer there, the ImageBuffer asymmetry, not the
// Index/Vertex one. `Graphics::SunLight`'s constructor (0x1801081f0) and its
// `Set` rebuild paths build the very first RenderBuffer in the process, so the
// FIRST-IN-LIST branch of ListLink runs here.
// ============================================================================

void Video::RenderBuffer::PlatformConstruct() {
    DeviceData* dd = Device::_singleton->deviceData;
    platformData = dd ? dd->CreateRenderBufferData(this) : nullptr;  // slot 19, +0x98
    RtVideoDeviceLock(&Device::_singleton);
    ListLink(this, _firstRenderBuffer, &RenderBuffer::next, &RenderBuffer::prev);
    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::RenderBuffer::PlatformDestruct() {
    // B5 PILOT (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
    // virtual destructor, then the base's class-scope operator delete (MemFree).
    // +0x28 is still NOT nulled afterwards (the ImageBuffer asymmetry above).
    if (RenderBufferData* companion = platformData)
        delete companion;
    RtVideoDeviceLock(&Device::_singleton);
    ListUnlink(this, _firstRenderBuffer, &RenderBuffer::next, &RenderBuffer::prev);
    RtVideoDeviceUnlock(&Device::_singleton);
}

// ============================================================================
// Video::IndexBuffer / Video::VertexBuffer — ctors zero the two owned fields
// then hand off to PlatformConstruct. Create/Set are pure trampolines into the
// platform buffer object at +0x08 (dx11::IndexBufferData / VertexBufferData).
// ============================================================================

// ENDGAME B.3.5 block D2. The two constructors stay DOORS and do NOT become real
// `IndexBuffer()` / `VertexBuffer()`: the `PlatformConstruct` they tail into
// publishes `this` into the `_firstBuffer` intrusive list (RULE B4b-4 /
// B35_RULES §9). What converted inside them is the hand-off, a member call.
void Video_IndexBuffer_Ctor(IndexBuffer* self) {
    self->size = 0;
    self->dynamic = false;
    self->Video::IndexBuffer::PlatformConstruct();
}

void Video_VertexBuffer_Ctor(VertexBuffer* self) {
    self->size = 0;
    self->dynamic = false;
    self->Video::VertexBuffer::PlatformConstruct();
}

// B.4 wave Y (RT-GFX): the two exported destructors are real members.
//   ??1IndexBuffer@Video@@QEAA@XZ   0x1800f3480  `JMP 0x1800f3760` (PlatformDestruct)
//   ??1VertexBuffer@Video@@QEAA@XZ  0x1800f55e0  `JMP 0x1800f58c0` (PlatformDestruct)
// Each is the one tail jump: the destructor IS PlatformDestruct. A holder's
// implicit member destruction now runs it, so no holder calls PlatformDestruct
// on a typed member by hand any more (EdgeMesh, WorldGeometry). The erased
// `Array<DetailLevel>` rows and Context's heap buffers keep their explicit
// PlatformDestruct, as retail's bodies call it there.
Video::IndexBuffer::~IndexBuffer()   { PlatformDestruct(); }
Video::VertexBuffer::~VertexBuffer() { PlatformDestruct(); }

// Create/Set are pure trampolines into the companion at +0x08, slots 1 and 2 of
// `Video::{Index,Vertex}BufferData` (`Create` and `SetData`, +0x08 and +0x10).
bool Video::IndexBuffer::Create(int count, bool isDynamic, void* data) {
    return platformData->Create(static_cast<uint32_t>(count),
                                static_cast<uint8_t>(isDynamic ? 1 : 0), data);
}

// ENDGAME B.3.5 closing pass: the two ints are `(sizeBytes, offsetBytes)`, the
// retail trampoline's and dx11's slot-2 consumer's reading (Buffers.h).
bool Video::IndexBuffer::Set(int sizeBytes, int offsetBytes, void* data) {
    return platformData->SetData(sizeBytes, offsetBytes, data);
}

bool Video::VertexBuffer::Create(int count, bool isDynamic, void* data) {
    return platformData->Create(static_cast<uint32_t>(count),
                                static_cast<uint8_t>(isDynamic ? 1 : 0), data);
}

bool Video::VertexBuffer::Set(int sizeBytes, int offsetBytes, void* data) {
    return platformData->SetData(sizeBytes, offsetBytes, data);
}

// ============================================================================
// HELD BACK: Video::ImageBuffer::SaveImageToFile (static) — 0x1800f2840,
// ordinal 3608. One caller (src/game/Game/w2_takescreenshot.cpp, the
// screenshot path).
//
// Not ported. Reasons:
//   1. File I/O side effect (writes a .bmp to disk) — not diffable by the
//      RT_DIFF pure/idempotent-replay patterns this lane's other functions
//      use; running "ours" and "theirs" back to back would write the file
//      twice, and there is no private-scratch variant of a filesystem write.
//   2. The decompile (see docs/history/runtime_port/WAVE6_LANE_C.md) is not a small
//      leaf: it builds paths via System::Filename::GetPathDirectory /
//      GetPathFilename / AdjustDelimiters and System::operator+ on
//      refcounted Strings, opens the file through IO::FileSystem::CreatePath
//      and IO::BinaryFileStream/IO::FileStream::Create, and hand-assembles a
//      24-bit BMP header before writing rows back-to-front through
//      WriteFile. That is Wave 4 (IO) and System::String territory, not
//      this lane's ImageBuffer/IndexBuffer/VertexBuffer scope, and
//      reimplementing it here would mean either depending on those other
//      lanes' unowned internals or duplicating them.
//   3. Low traffic and low risk on the DLL: it runs once per screenshot, on
//      a user-initiated action, not in a hot path or on every frame.
//
// fn_ImageBuffer_* in engine_funcs.h does not resolve this export at all —
// w2_takescreenshot.cpp calls a different, already-ported capture path — so
// leaving this on the DLL costs nothing beyond the census counting it as
// "called, not ported."
// ============================================================================
