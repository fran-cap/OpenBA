// ============================================================================
// src/engine/video/video_resources.cpp — Wave 18 lane VRES
//   The Video resource lifecycle: RenderBuffer ctor/dtor/Bind,
//   ImageBuffer::Recreate, and the Configuration / Device destructors.
// ============================================================================
//
// Six of lane VRES's seven rows. The seventh
// (?SaveImageToFile@ImageBuffer@Video@@) is in save_image_to_file.cpp;
// docs/history/runtime_port/WAVE18_LANE_VRES.md carries its verdict.
//
//   | Export                                   | Retail      | Ord  |
//   |------------------------------------------|-------------|------|
//   | ??0RenderBuffer@Video@@QEAA@XZ           | 0x1800f3ee0 |  346 |
//   | ??1RenderBuffer@Video@@QEAA@XZ           | 0x1800f4020 |  659 |
//   | ?Bind@RenderBuffer@Video@@QEBAX...       | 0x1800f4300 | 1656 |
//   | ?Recreate@ImageBuffer@Video@@QEAAXXZ     | 0x1800f33f0 | 3520 |
//   | ??1Configuration@Video@@QEAA@XZ          | 0x1800f6f80 |  557 |
//   | ??1Device@Video@@QEAA@XZ                 | 0x1800f7290 |  569 |
//
// Every address and ordinal above is the shipped export directory's own answer
// (pefile over C:\GOG Games\Banished\Runtime-x64.dll), read BEFORE any body was
// written — reference_resolveexport_phantom_names.md. Every body below was
// transcribed from `disassemble_function` at that address, not from Ghidra's
// decompiled C (reference_ghidra_c_output_vs_disassembly.md).
//
// ---------------------------------------------------------------------------
// THE TWO DESTRUCTORS, AND WHAT EACH ONE DESTROYS VS MERELY FREES
// ---------------------------------------------------------------------------
// A dtor is the highest-risk shape in this project: a prior lane's written
// recipe would have swapped a body that destroys every element for one that
// only frees the block (reference_a_written_recipe_can_be_wrong.md). So the
// distinction is drawn per array, from the disassembly, before the code:
//
// ??1Device@Video@@ owns THREE arrays and destroys them through three DIFFERENT
// retail helpers, which are NOT interchangeable:
//
//   +0x40 Array<GraphicsInterface>  -> 0x1800109d0  DESTROYS EVERY ELEMENT
//         Its element helper 0x180010f00 walks stride 0x18 and, per element,
//         (a) recurses into 0x180010da0 over the element's Adapter array,
//         (b) frees the Adapter buffer and clears its 16-byte header,
//         (c) releases the System::String at +0x00.
//         0x180010da0 in turn, per Adapter (stride 0x38), frees the
//         multisample buffer at +0x18, recurses into 0x180010cf0 over the
//         DisplayMode array to free each mode's RefreshRate buffer, frees the
//         DisplayMode buffer, and releases the Adapter's name String.
//         Writing this one as "free the block" would leak, per Device, one
//         String and one Array per interface, plus one String and three
//         Arrays per adapter, plus one Array per display mode. That is the
//         exact defect this comment exists to prevent.
//
//   +0x30 Array<bool>              -> 0x180009560  FREES THE BLOCK ONLY
//         No element loop at all, and that is CORRECT: `bool` has no
//         destructor. It is instruction-for-instruction lane Q's
//         `rtcoll::ArrayDtorImpl` (0x180009560 is one of the eleven addresses
//         that body already covers).
//
//   +0x20 Array<DisplayMode>       -> 0x180010830  DESTROYS EVERY ELEMENT
//         Element helper 0x180010cf0 = lane D2's BODY A with offs {0x08},
//         stride 0x18: free each mode's RefreshRate buffer, clear the header.
//
// ??1Configuration@Video@@ destroys NOTHING and frees NOTHING. Its entire body
// is two instructions: store 0 over `?_singleton@Configuration@Video@@`. It
// does not even read `this`. Engine::Shutdown does the MemFree separately
// (src/engine/engine/engine_lifecycle.cpp:845).
//
// ---------------------------------------------------------------------------
// THREE HELPER ADDRESSES lane D2 DID NOT ENUMERATE
// ---------------------------------------------------------------------------
// array_destroy.cpp's banner says the destroy-helper cluster is ELEVEN
// addresses and THREE bodies. Reached from ??1Device, it is fourteen:
// 0x180010cf0, 0x180010da0 and 0x180010f00 are three more, and all three are
// covered by the bodies and specs that file already exports:
//
//   0x180010cf0 == BODY A, offs {0x08}, stride 0x18   (== kD_InnerArr_18's row,
//                  a SECOND address for that spec; the banner lists only
//                  0x1800419b0)
//   0x180010da0 == a nested per-Adapter destroy: A{0x18}, recurse, A{0x08},
//                  B{0x00}
//   0x180010f00 == a nested per-GraphicsInterface destroy: recurse, A{0x08},
//                  B{0x00}
//
// The two nested ones are the shape lane D2 called "the eleventh, 0x18005c550,
// a RECURSIVE destroy that calls one of each body before freeing".
//
// PLATFORM. `??1Device` calls DeleteCriticalSection on the lock at Device+0x60.
// PLAN §4.1 decision 1 keeps src/engine/ free of <windows.h>, so that one call
// is `RtVideoDeleteDeviceLock` in video_resources_platform.cpp.
// ============================================================================

// B.4 (R-VIDEO): the three nested destroys are the typed helpers now. 0x180010cf0
// is `RtVideo_ArrayModeElemDestroy` (device_enum_types.cpp) and 0x180010da0 is
// `RtVideo_ArrayAdapterElemDestroy` (device_enum_types.cpp) -- the same retail
// addresses, ported once each on the named fields, spelling B of the String
// release included -- and 0x180010f00 is `RtVideo_ArrayInterfaceElemDestroy`,
// MOVED from this file into device_enum_types.cpp beside its two siblings. The
// by-offset `HdrAt` view and the `DestroySpec` rows it fed are gone.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "video/Device.h"
#include "video/ImageBuffer.h"
#include "video/CompanionData.h"   // ENDGAME B.3.5: Video::{Device,RenderBuffer}Data
#include "../collection/array_internal.h"

using Video::Configuration;
using Video::Device;
using Video::DeviceData;
using Video::RenderBuffer;

namespace {

// One `{data, size, capacity}` triple's `ArrayDtorImpl` (collection/Collection.h):
// MemFree the block (the allocator's own null test), then the three zero stores.
template <class T>
void FreeTriple(T*& data, int32_t& size, int32_t& capacity) {
    rtcoll::MemFree(data);
    data     = nullptr;
    size     = 0;
    capacity = 0;
}

}  // namespace

// ---------------------------------------------------------------------------

// ============================================================================
// ??0RenderBuffer@Video@@QEAA@XZ — 0x1800f3ee0, ordinal 346
// ============================================================================
//
// Twelve instructions. Three qword zero stores covering +0x00..+0x17, a CALL to
// PlatformConstruct, and `MOV RAX,RBX` — the ctor returns `this`, which MSVC
// requires.
//
// `MOV qword ptr [RCX+0x10],RAX` is ONE eight-byte store covering `width` AND
// `height`. Writing them as two ints is the same bytes; it is spelled as two
// named fields because that is what Device.h pins.
//
// No real `RenderBuffer::RenderBuffer()`: this free function is the ctor. Through
// PlatformConstruct it publishes `this` into the `_firstRenderBuffer` intrusive
// list (the class static in Device.h).
RenderBuffer* Video_RenderBuffer_Ctor(RenderBuffer* self) {
    self->colorBuffer = nullptr;                       // MOV [RCX],RAX
    self->depthBuffer = nullptr;                       // MOV [RCX+8],RAX
    self->width  = 0;                                  // MOV [RCX+0x10],RAX,
    self->height = 0;                                  //   one store, two fields
    self->Video::RenderBuffer::PlatformConstruct();
    return self;                                       // MOV RAX,RBX
}

// ============================================================================
// ??1RenderBuffer@Video@@QEAA@XZ — 0x1800f4020, ordinal 659
// ============================================================================
//
// ONE instruction: `JMP 0x1800f4400`. The destructor IS PlatformDestruct — the
// linker folded them, so retail's ~RenderBuffer is a tail-call alias and
// nothing else. What the destination does is unlink `this` from the circular
// `_firstRenderBuffer` list and release the RenderBufferData companion at +0x28
// — and it does NOT null +0x28 afterwards (buffers.cpp's "ImageBuffer
// asymmetry"). A free function, not a real `~RenderBuffer()`.
void Video_RenderBuffer_Dtor(RenderBuffer* self) {
    self->Video::RenderBuffer::PlatformDestruct();
}

// ============================================================================
// ?Bind@RenderBuffer@Video@@QEBAXAEAVContext@2@@Z — 0x1800f4300, ordinal 1656
// ============================================================================
//
//     MOV RCX,[RCX+0x28]   ; this->platformData
//     MOV RAX,[RCX]        ; its vptr
//     JMP [RAX+0x10]       ; slot 2, with RDX (the Context&) untouched
//
// The deref of `platformData` is unguarded in retail and is left unguarded
// here. A null there means PlatformConstruct never ran on this object, which is
// an upstream defect to find, not one to paper over.
void Video::RenderBuffer::Bind(Context& context) const {
    platformData->Bind(&context);   // Video::RenderBufferData slot 2
}

// ============================================================================
// ?Recreate@ImageBuffer@Video@@QEAAXXZ — 0x1800f33f0, ordinal 3520
// ============================================================================
//
// Device-lost / resize recovery for one ImageBuffer. Called from
// src/engine/save/saveload_update.cpp:236.
//
//     dd = Video::Device::_singleton->deviceData        ; +0x100
//     pd = dd ? dd->CreateImageBufferData(this) : null  ; slot 10, +0x50
//     this->_data = pd                                  ; +0x60
//     f = this->_flags                                  ; +0x34, read BEFORE
//     if (f & 0x10) tail  pd->AttachColorBuffer()
//     else if (f & 0x20) tail  pd->AttachDepthBuffer()
//     else               tail  pd->Create(nullptr)
//
// Note the two tests are `SHR ECX,4 / TEST CL,1` and `SHR EAX,5 / TEST AL,1` —
// bits 4 and 5, which ImageBuffer.h already documents as "device-reset-tracked
// (set by DX11 color/depth attach)". They are read from the SAME loaded value,
// so a flags write by the factory call could not affect the second test;
// reproduced by loading `_flags` once.
//
// FAITHFUL UNGUARDED DEREF. When `dd` is null, `pd` is null and retail
// dereferences it immediately. That is not a bug to guard
// (CLAUDE.md "Don't fix a faithful crash with a null check") — it is retail
// relying on Recreate never being called with no device, and a guard would skip
// the recreate rather than restore it.
void Video::ImageBuffer::Recreate() {
    Device* const dev = Device::_singleton;
    DeviceData* const dd = dev ? dev->deviceData : nullptr;

    ImageBufferData* pd = nullptr;
    if (dd) pd = dd->CreateImageBufferData(this);
    _data = pd;

    const uint32_t f = _flags;
    if ((f >> 4) & 1u) { pd->AttachColorBuffer(); return; }
    if ((f >> 5) & 1u) { pd->AttachDepthBuffer(); return; }
    (void)pd->Create(nullptr);
}

// ============================================================================
// ?Release@ImageBuffer@Video@@QEAAXXZ — 0x1800f33c0
// ============================================================================
//
//     MOV RBX,RCX ; MOV RCX,[RCX+0x60] ; TEST RCX,RCX ; JZ +
//     MOV RAX,[RCX] ; MOV EDX,1 ; CALL [RAX]       ; companion slot 0, flags 1
//   + MOV qword ptr [RBX+0x60],0
//
// Recreate's partner: the device-lost release of one image's companion, called
// by the exe's GameUpdateLoop::OnVideoError (0x14009599a / 0x1400959a9) for the
// two minimap images. `this` is dereferenced unguarded (retail's); the companion
// is guarded and the field always nulled. ImageBufferData wears the compiler's
// table, so slot 0 with flags 1 is `delete` (as ReleaseDynamic spells it).
void Video::ImageBuffer::Release() {
    if (ImageBufferData* companion = _data)
        delete companion;
    _data = nullptr;
}

// ============================================================================
// ??1Configuration@Video@@QEAA@XZ — 0x1800f6f80, ordinal 557
// ============================================================================
//
//     MOV qword ptr [0x1801fe7a0],0x0
//     RET
//
// That is the whole function. It does not read `this`, destroys nothing, frees
// nothing, and takes no branch: it clears the exported singleton pointer
// unconditionally, even for a Configuration that was never the singleton.
// Engine::Shutdown does the MemFree afterwards.
void Video_Configuration_Dtor(void* self) {
    (void)self;                        // retail never touches RCX
    Configuration::_singleton = nullptr;
}

// ============================================================================
// ??1Device@Video@@QEAA@XZ — 0x1800f7290, ordinal 569
// ============================================================================
//
// Six steps, in retail's order:
//
//   1. dd = this->deviceData (+0x100); if (dd) slot 0 with flags 1 — MSVC's
//      scalar deleting destructor with the delete flag, destroy AND free
//      (`delete dd` since B5 DEV-A).
//   2. this->deviceData = nullptr.
//   3. if (this->type == 0) `?_singleton@Device@Video@@` = nullptr.
//      `CMP dword [RBX],ECX / CMOVZ RAX,RCX` — a CMOV, so the singleton slot is
//      REWRITTEN EITHER WAY: with null when this is the type-0 device, with its
//      own current value when it is not. Reproduced as an unconditional store.
//   4. DeleteCriticalSection(&this->lock) — +0x60, via RtVideoDeleteDeviceLock.
//   5..6. The three arrays, in retail's order 0x40, 0x30, 0x20. NOTE the order
//      is NOT the offset order and is NOT reverse-declaration order; it is
//      transcribed, not tidied.
//
// WHAT IT DOES NOT TOUCH: `dllSlots` at +0xB0 (the renderer interface slots
// src/app/video_interface.cpp fills) is never read by this body — no
// FreeLibrary, no clear.
//
// This is a free function and not a real `~Device()` — RULE B5-6, because
// `InitializeCriticalSection` is retail's `Device::Startup`.
void Video_Device_Dtor(Device* self) {
    // ---- 1 & 2: the DeviceData companion -----------------------------------
    DeviceData* const dd = self->deviceData;
    // B5 DEV-A (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
    // virtual destructor, then the base's class-scope operator delete (MemFree).
    if (dd) delete dd;
    self->deviceData = nullptr;

    // ---- 3: the singleton slot (CMOV, so always stored) --------------------
    Device* keep = Device::_singleton;
    if (self->type == 0) keep = nullptr;
    Device::_singleton = keep;

    // ---- 4: the lock -------------------------------------------------------
    RtVideoDeleteDeviceLock(self);

    // ---- 5: Array<GraphicsInterface> @+0x40 — DESTROY, then free -----------
    RtVideo_ArrayInterfaceElemDestroy(self->interfaceData, self->interfaceCount);   // 0x180010f00
    FreeTriple(self->interfaceData, self->interfaceCount, self->interfaceCapacity);

    // ---- 5b: Array<bool> @+0x30 — FREE ONLY, bool has no destructor --------
    FreeTriple(self->multisampleData, self->multisampleCount, self->multisampleCap);

    // ---- 6: Array<DisplayMode> @+0x20 — DESTROY, then free -----------------
    RtVideo_ArrayModeElemDestroy(self->modeData, self->modeCount);   // 0x180010cf0
    FreeTriple(self->modeData, self->modeCount, self->modeCapacity);
}
