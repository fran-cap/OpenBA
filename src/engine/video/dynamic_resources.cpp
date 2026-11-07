// ============================================================================
// src/engine/video/dynamic_resources.cpp — Wave 9 lane VID
//   The device-lost / device-restore resource sweep:
//   Video::Device::{Lock,Unlock,ReleaseDynamic,RecreateDynamic} and the eight
//   class-static list walkers they forward to, plus Video::RenderBuffer::Recreate.
// ============================================================================
//
// WHY THIS FILE EXISTS, AND WHY IT IS NOT "THE DEVICE-LOST PATH"
// -------------------------------------------------------------
// `?ReleaseDynamic@Device@Video@@QEAAXXZ` and `?RecreateDynamic@Device@Video@@QEAAXXZ`
// look like they matter only when the GPU device is lost. They do not. Both
// names sit in the import tables of `src/render/dx11/contextdata.cpp` and
// `src/render/dx11/devicedata.cpp`, and BOTH of those tables are ALL-OR-NOTHING:
//
//     const Imports* GetImports() { ... return g_imOk ? &g_im : nullptr; }
//     void ContextData_Finish(ContextData* self) {
//         const Imports* im = GetImports();
//         if (!im) { Orig<void (*)(ContextData*)>(3)(self); return; }
//
// `g_imOk` is false if ANY ONE of the twenty entries resolved null. At cutover
// `ResolveExport` misses the port registry for an unported name and falls
// through to `the raw DLL door`, which has no module handle and returns
// null — so ONE unported Runtime export in that table disables EVERY
// `ContextData` slot, and there is no shipped body to fall back to:
// VideoDX11-x64.dll is never loaded. That is a dead renderer on the first
// draw, not on device loss.
//
// So these four `Video::Device` members are per-frame infrastructure. Their
// Wave 6 hold-back reason ("running ours and theirs both releases twice and
// recreates twice; there is nothing to compare") is a statement about the
// ORACLE, and it is still true — but it was never a statement about whether
// the port is needed, and at cutover there is no second implementation for the
// double-execution hazard to arise from.
//
// WHAT THE EIGHT STATICS ARE
// --------------------------
//   RecreateDynamic: IndexBuffer::RecreateDynamic(); VertexBuffer::RecreateDynamic();
//                    ImageBuffer::RecreateDynamic();  RenderBuffer::RecreateDynamic();
//   ReleaseDynamic:  RenderBuffer::ReleaseDynamic();  ImageBuffer::ReleaseDynamic();
//                    VertexBuffer::ReleaseDynamic();  IndexBuffer::ReleaseDynamic();
//
// Each walks its class's process-global intrusive list under Video::Device's
// recursive lock and destroys or recreates the dx11 companion object hanging
// off every live element. Wave 6 lane B's secondary reason for holding the two
// forwarders — "three of the four callees belong to lane C, porting the
// forwarder first would hard-code call-by-address into three classes another
// lane is reversing" — is answered by porting the callees too, which is what
// this file does. No address door is used for any of the nine callees.
//
// A STALE BLOCKER, CHECKED AND FALSE
// ----------------------------------
// WAVE6_LANE_B.md §2.4 says the RenderBuffer list head "_firstRenderBuffer" is
// "a DLL-internal variable with no export". It is EXPORTED:
//
//     ord 4787  0x1801fe790  ?_firstRenderBuffer@RenderBuffer@Video@@0PEAV12@EA
//
// read out of the shipped export directory with pefile. Lane C2 minted the
// other three sibling heads (`_firstSurface`, and both `_firstBuffer`s) and
// skipped this one, so the fourth head had no storage after cutover. This lane
// adds the missing mint row rather than working around it.
//
// EVERY MANGLED NAME BELOW WAS CHECKED AGAINST THE SHIPPED EXPORT DIRECTORY OF
// C:\GOG Games\Banished\Runtime-x64.dll WITH pefile BEFORE ANY BODY WAS
// WRITTEN (reference_resolveexport_phantom_names.md). Retail addresses and
// ordinals:
//
//   ?Lock@Device@Video@@QEAAXXZ                    0x18000ca90  ord 3214
//   ?Unlock@Device@Video@@QEAAXXZ                  0x18000cac0  ord 4462
//   ?RecreateDynamic@Device@Video@@QEAAXXZ         0x1800f80b0  ord 3522
//   ?ReleaseDynamic@Device@Video@@QEAAXXZ          0x1800f80e0  ord 3536
//   ?RecreateDynamic@ImageBuffer@Video@@SAXXZ      0x1800f2d90  ord 3523
//   ?ReleaseDynamic@ImageBuffer@Video@@SAXXZ       0x1800f2e80  ord 3537
//   ?RecreateDynamic@IndexBuffer@Video@@SAXXZ      0x1800f3500  ord 3524
//   ?ReleaseDynamic@IndexBuffer@Video@@SAXXZ       0x1800f35d0  ord 3538
//   ?RecreateDynamic@VertexBuffer@Video@@SAXXZ     0x1800f5660  ord 3526
//   ?ReleaseDynamic@VertexBuffer@Video@@SAXXZ      0x1800f5730  ord 3540
//   ?RecreateDynamic@RenderBuffer@Video@@SAXXZ     0x1800f4030  ord 3525
//   ?ReleaseDynamic@RenderBuffer@Video@@SAXXZ      0x1800f40c0  ord 3539
//   ?Recreate@RenderBuffer@Video@@QEAAXXZ          0x1800f4170  ord 3521
//
// THERE IS NO ORACLE FOR ANY OF THE TWELVE WALKERS/FORWARDERS, AND THIS FILE
// SAYS SO RATHER THAN PRETENDING OTHERWISE.
// Wave 6's argument stands unchanged: running ours and then theirs releases
// every live GPU companion twice and recreates it twice, and the second pass
// operates on freed objects. A `the diff oracle` has nothing to assert either —
// the observable effect is entirely inside `src/render/dx11/`. So no `the diff oracle`
// appears below for those bodies. What IS verified:
//
//   * `Device::Lock` / `Device::Unlock` are byte-identical in shape to the
//     inline copies already living in `buffers_lock.cpp`, which Wave 9 lane W
//     validated against retail through the empty-list probe. Their two new
//     `this`-taking twins are transcribed from 0x18000ca90 / 0x18000cac0
//     instruction by instruction (the store ORDER differs from the inline copy
//     and is preserved — see buffers_lock.cpp).
//   * `RenderBuffer::Recreate`'s width/height arithmetic is not transcribed a
//     second time. It is EXACTLY `ImageBuffer::Width()` / `Height()`, which are
//     already ported AND carry a FULL scalar oracle
//     (`Video_ImageBuffer_Width` / `_Height`, buffers.cpp:429/450). Calling
//     them rather than re-deriving the bit8/bit9/bit6 ladder means the
//     equivalence is enforced by the compiler instead of asserted in a comment,
//     and it puts the one piece of real arithmetic in this file under an oracle
//     that already runs. Retail inlines that ladder, so under
//     `the diff-oracle flag` this shows up as extra `Video::ImageBuffer::Width`
//     call counts; that is a measurement artefact, not a behaviour change.
//
// PLATFORM. Nothing here includes <windows.h>. The Device lock lives in
// buffers_lock.cpp per PLAN §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "video/Buffers.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"
#include "graphics/stagger_hold.h"   // ext.staggered_passes: a rebuild invalidates held targets (NOT RETAIL)

using Video::Device;
using Video::RenderBuffer;

// The Win32 half of Video::Device's recursive render lock (buffers_lock.cpp,
// declared in Device.h): the two singleton-reading forms are Wave 9 lane W's
// inline copies; the two `This` forms are the bodies of the exported
// Device::Lock / Device::Unlock.

// ENDGAME B.3.5 block VC — the two `Video_ImageBuffer_{Width,Height}` free
// declarations are GONE. `Video::ImageBuffer::Width()` / `Height()` are MEMBERS
// (bodies in buffers.cpp), so `RenderBuffer::Recreate` below calls them on the
// attachment directly.

namespace {

// ---------------------------------------------------------------------------
// B.4 (R-VIDEO): the five `void**` / typed-slot accessors over the class
// statics (`Device::_singleton` and the four list heads) are gone; the bodies
// name the statics. Each sweep takes the lock on the ADDRESS of
// `Device::_singleton` (retail's unlock re-reads it) and re-reads its list head
// through the static exactly where retail re-reads it.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The intrusive list link every walker steps through.
//
// The offsets are retail's, read off the four disassemblies, and they are NOT
// the same across the four classes:
//
//   ImageBuffer   +0x58   (0x1800f2e35  MOV RBX,[RBX+0x58])   `_field58`
//   IndexBuffer   +0x18   (0x1800f3584  MOV RBX,[RBX+0x18])   `listNext`
//   VertexBuffer  +0x18   (0x1800f56e4  MOV RBX,[RBX+0x18])   `listNext`
//   RenderBuffer  +0x20   (0x1800f4078  MOV RBX,[RBX+0x20])   `prev`
//
// B.4 (R-VIDEO): each walker reads its class's field by name. The names are
// the headers' (guesses); buffers.cpp's ListLink calls +0x10 / +0x18 of the
// buffers `next` / `prev`, so Buffers.h's `listNext` is ListLink's `prev`.
// The list is circular, so both directions terminate; the OFFSET is retail's.
// ---------------------------------------------------------------------------
static_assert(offsetof(Video::ImageBuffer, _field58) == 0x58, "ImageBuffer walker link");
static_assert(offsetof(Video::IndexBuffer, listNext) == 0x18, "IndexBuffer walker link");
static_assert(offsetof(Video::VertexBuffer, listNext) == 0x18, "VertexBuffer walker link");
static_assert(offsetof(Video::RenderBuffer, prev) == 0x20, "RenderBuffer walker link");

// The one shape all four ReleaseDynamic walkers share for a single element:
// destroy the companion through its deleting destructor (vtable slot 0, flag 1)
// and null the field. Guarded on the companion, exactly as retail is.
// B5 (the dx11 seam, B5_RULES D10): this was the `ReleaseCompanion<>` template
// over `DeletingDtor(1)`. Every companion now wears the compiler's table, so
// slot 0 with flag 1 is `delete`; the four walkers spell the three statements
// inline (PILOT: RenderBuffer, BUF: Index/VertexBuffer, RES: ImageBuffer — the
// template's last user) and the template is gone.

// ImageBuffer's per-element guard, from 0x1800f2dd0 and 0x1800f2ec8:
//
//     EAX = kind;  CMP EAX,1;  JBE take
//     ADD EAX,-3;  CMP EAX,2;  JA  skip          (unsigned)
//
// i.e. kind in {0, 1, 3, 4, 5}. Note this is NOT ImageBuffer::IsRenderBuffer()
// ({0,1,4,5}) — the dynamic kind 3 is included here and excluded there, so the
// existing accessor deliberately is not reused.
bool ImageBufferIsDynamicResource(const Video::ImageBuffer* p) {
    const int32_t kind = p->_kind;
    if (kind <= 1)
        return true;
    return static_cast<uint32_t>(kind - 3) <= 2u;
}

}  // namespace

// ---------------------------------------------------------------------------
// ENDGAME B.3.9 VID (B39_RULES D7): the four list heads are class statics, all
// four in one commit because device-lost recovery walks them together
// (Release*/Recreate* above and below, the Platform{Construct,Destruct} links in
// buffers.cpp). Their mint rows retired with their last readers.
// ---------------------------------------------------------------------------
Video::ImageBuffer*  Video::ImageBuffer::_firstSurface       = nullptr;   // 0x1801fe780
Video::IndexBuffer*  Video::IndexBuffer::_firstBuffer        = nullptr;   // 0x1801fe788
Video::RenderBuffer* Video::RenderBuffer::_firstRenderBuffer = nullptr;   // 0x1801fe790
Video::VertexBuffer* Video::VertexBuffer::_firstBuffer       = nullptr;   // 0x1801fe798

// ============================================================================
// Video::Device::Lock  —  retail 0x18000ca90, ordinal 3214
// Video::Device::Unlock —  retail 0x18000cac0, ordinal 4462
//
// Thin `this`-taking wrappers over the Win32 half. They are NOT the same as the
// inline copies the four PlatformConstruct/PlatformDestruct bodies carry: those
// load `Device::_singleton` themselves, these operate on the passed `this`, and
// retail's exported Lock stores the depth BEFORE the owner id while the inline
// copy stores the owner first. buffers_lock.cpp keeps both orders.
// ============================================================================
// ENDGAME B.3.5 block D2: real members of `Video::Device`. RULE B5-6 — the lock
// itself stays the opaque `uint8_t lock[0x28]` at +0x60 with the hand-rolled
// recursive acquire in `buffers_lock.cpp`; these two do NOT become a `std::mutex`
// or a scoped guard, because src/render/dx11/contextdata.cpp and the four `Platform*`
// bodies read the owner-tid and depth words at pinned +0x88/+0x8c.
void Video::Device::Lock() {
    RtVideoDeviceLockThis(this);
}

void Video::Device::Unlock() {
    RtVideoDeviceUnlockThis(this);
}

// ============================================================================
// Video::ImageBuffer::RecreateDynamic  —  retail 0x1800f2d90, ordinal 3523
//
// Walks _firstSurface under the Device lock. For each element whose `kind` is a
// dynamic-resource kind: build a fresh companion through DeviceData slot 10
// (`CreateImageBufferData`, +0x50), store it at ImageBuffer+0x60, then poke it
// once through one of three companion slots selected by ImageBuffer::flags bits
// 4 and 5.
//
// THE UNGUARDED COMPANION DEREF IS RETAIL'S. When `Device::deviceData` is null
// the shipped body stores a null companion at +0x60 and then dispatches through
// it (0x1800f2df3 XOR EAX,EAX -> 0x1800f2e00 store -> 0x1800f2e14 CALL [RDX+8]).
// Per CLAUDE.md that deref stays unguarded: retail relies on the device data
// being there whenever any ImageBuffer is on this list, and a guard here would
// hide an upstream ordering defect rather than fix it.
//
// The head is RE-READ from memory on every iteration (0x1800f2e39 compares
// against `[_firstSurface]`, not a cached register), so the loop below re-reads
// it too.
// ============================================================================
// ENDGAME B.3.5 block VC: +0x08/+0x10/+0x18 are `Video::ImageBufferData`'s
// slots 1, 2 and 3 (`AttachColorBuffer`, `AttachDepthBuffer`, `Create`). The
// third arm passing `0` for `levels` is retail's `CALL [RDX+0x18]` with a
// zeroed argument register, i.e. "rebuild with no initial pixel data".
void Video::ImageBuffer::RecreateDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    Video::ImageBuffer* p = _firstSurface;
    if (p != nullptr) {
        do {
            if (ImageBufferIsDynamicResource(p)) {
                DeviceData* dd = Device::_singleton->deviceData;
                Video::ImageBufferData* companion =
                    dd ? dd->CreateImageBufferData(p) : nullptr;   // XOR EAX,EAX
                p->_data = companion;

                // Unguarded past retail's own test — see the banner.
                const uint32_t flags = p->_flags;
                if ((flags & Video::ImageBuffer::kAliasedColor) != 0) {
                    companion->AttachColorBuffer();
                } else if ((flags & Video::ImageBuffer::kAliasedDepth) != 0) {
                    companion->AttachDepthBuffer();
                } else {
                    companion->Create(nullptr);
                }
            }
            p = p->_field58;
        } while (p != _firstSurface);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

// ============================================================================
// Video::ImageBuffer::ReleaseDynamic  —  retail 0x1800f2e80, ordinal 3537
//
// THE STALE HEAD IS DELIBERATE. Retail loads the head into RCX once
// (0x1800f2eb7), reloads it ONLY inside the taken branch (0x1800f2eef), and
// compares the walk against that possibly-stale RCX (0x1800f2efa). An element
// that fails the kind test therefore does not refresh the terminator. That is
// reproduced literally: `terminator` below is updated in exactly the same place.
// ============================================================================
void Video::ImageBuffer::ReleaseDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    Video::ImageBuffer* terminator = _firstSurface;
    if (terminator != nullptr) {
        Video::ImageBuffer* p = terminator;
        do {
            if (ImageBufferIsDynamicResource(p)) {
                // B5 RES (B5_RULES D10): ImageBufferData wears the compiler's
                // table, so its slot-0 call with flags 1 is `delete` (virtual
                // dtor, then the base's operator delete = MemFree); guarded on
                // the companion and the field nulled, as ReleaseCompanion does.
                if (ImageBufferData* companion = p->_data)
                    delete companion;
                p->_data = nullptr;
                terminator = _firstSurface;
            }
            p = p->_field58;
        } while (p != terminator);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

// ============================================================================
// Video::IndexBuffer::RecreateDynamic   —  retail 0x1800f3500, ordinal 3524
// Video::VertexBuffer::RecreateDynamic  —  retail 0x1800f5660, ordinal 3526
//
// One shape, two heads, two DeviceData factory slots (12 `CreateIndexBufferData`
// +0x60, 13 `CreateVertexBufferData` +0x68). The per-element guard is the
// `dynamic` bool at +0x04, and after the companion is stored at +0x08 retail
// calls companion slot 1 (+0x08, `Create`) with (self->size, /*dynamic*/ true,
// /*data*/ nullptr) — the buffer's Create, with `dynamic` hardcoded true and no
// initial data.
//
// The head reload sits inside the taken branch here too (0x1800f357d /
// 0x1800f56dd), so the terminator goes stale for a skipped element exactly as
// in ReleaseDynamic above.
// ============================================================================
void Video::IndexBuffer::RecreateDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    IndexBuffer* terminator = _firstBuffer;
    if (terminator != nullptr) {
        IndexBuffer* p = terminator;
        do {
            if (p->dynamic) {
                DeviceData* dd = Device::_singleton->deviceData;
                IndexBufferData* companion = dd ? dd->CreateIndexBufferData(p) : nullptr;
                p->platformData = companion;
                // Unguarded, as retail is — see the ImageBuffer note.
                companion->Create(static_cast<uint32_t>(p->size), 1, nullptr);
                terminator = _firstBuffer;
            }
            p = p->listNext;
        } while (p != terminator);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::IndexBuffer::ReleaseDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    IndexBuffer* terminator = _firstBuffer;
    if (terminator != nullptr) {
        IndexBuffer* p = terminator;
        do {
            if (p->dynamic) {
                // B5 BUF (B5_RULES D10): IndexBufferData wears the compiler's table,
                // so its slot-0 call with flags 1 is `delete` (virtual dtor, then
                // the base's operator delete = MemFree); guarded on the companion
                // and the field nulled (the former ReleaseCompanion<> template's
                // steps; the template went with its last user, ImageBuffer).
                if (IndexBufferData* companion = p->platformData)
                    delete companion;
                p->platformData = nullptr;
                terminator = _firstBuffer;
            }
            p = p->listNext;
        } while (p != terminator);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::VertexBuffer::RecreateDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    VertexBuffer* terminator = _firstBuffer;
    if (terminator != nullptr) {
        VertexBuffer* p = terminator;
        do {
            if (p->dynamic) {
                DeviceData* dd = Device::_singleton->deviceData;
                VertexBufferData* companion = dd ? dd->CreateVertexBufferData(p) : nullptr;
                p->platformData = companion;
                // Unguarded, as retail is — see the ImageBuffer note.
                companion->Create(static_cast<uint32_t>(p->size), 1, nullptr);
                terminator = _firstBuffer;
            }
            p = p->listNext;
        } while (p != terminator);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::VertexBuffer::ReleaseDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    VertexBuffer* terminator = _firstBuffer;
    if (terminator != nullptr) {
        VertexBuffer* p = terminator;
        do {
            if (p->dynamic) {
                // B5 BUF (B5_RULES D10): VertexBufferData wears the compiler's table,
                // so its slot-0 call with flags 1 is `delete` (virtual dtor, then
                // the base's operator delete = MemFree); guarded on the companion
                // and the field nulled (the former ReleaseCompanion<> template's
                // steps; the template went with its last user, ImageBuffer).
                if (VertexBufferData* companion = p->platformData)
                    delete companion;
                p->platformData = nullptr;
                terminator = _firstBuffer;
            }
            p = p->listNext;
        } while (p != terminator);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

// ============================================================================
// Video::RenderBuffer::Recreate  —  retail 0x1800f4170, ordinal 3521
//
// Rebuild one RenderBuffer's companion (DeviceData slot 19,
// `CreateRenderBufferData`, +0x98), poke it through companion slot 1 (+0x08),
// then recompute the render buffer's cached width/height from whichever
// attachment it has.
//
// The width/height ladder is `ImageBuffer::Width()` / `Height()` verbatim —
// bit8 = device backbuffer size, bit9 = half of it, otherwise the buffer's own
// size shifted right by Configuration::shadowScalar when bit6 is set — applied
// to the colour attachment if there is one and to the depth attachment
// otherwise. Rather than transcribe that ladder a fourth time this calls the
// two ported accessors; see the file banner for why.
//
// `depthBuffer` is dereferenced without a null check when `colorBuffer` is
// null, which is retail (0x1800f41d3 MOV RCX,[RBX+8] then 0x1800f41d7 reads
// [RCX+0x34] unconditionally). A RenderBuffer with neither attachment never
// reaches this function, because nothing puts one on the list.
// ============================================================================
void Video::RenderBuffer::Recreate() {
    DeviceData* dd = Device::_singleton->deviceData;
    RenderBufferData* companion = dd ? dd->CreateRenderBufferData(this) : nullptr;
    platformData = companion;
    // Unguarded, as retail is (0x1800f41a0 reads [RAX] with no test).
    (void)companion->Slot1();

    const Video::ImageBuffer* source = colorBuffer;
    if (source == nullptr)
        source = depthBuffer;

    width  = source->Video::ImageBuffer::Width();
    height = source->Video::ImageBuffer::Height();
}

// ============================================================================
// Video::RenderBuffer::RecreateDynamic  —  retail 0x1800f4030, ordinal 3525
// Video::RenderBuffer::ReleaseDynamic   —  retail 0x1800f40c0, ordinal 3539
//
// No per-element guard on either: every RenderBuffer on the list is swept.
// Both re-read the head from memory on every iteration (0x1800f407c and
// 0x1800f4120 compare against `[_firstRenderBuffer]`, not a cached register),
// unlike the other three classes — so both loops below re-read it too.
// ============================================================================
void Video::RenderBuffer::RecreateDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    RenderBuffer* p = _firstRenderBuffer;
    if (p != nullptr) {
        do {
            p->Video::RenderBuffer::Recreate();
            p = p->prev;
        } while (p != _firstRenderBuffer);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

void Video::RenderBuffer::ReleaseDynamic() {
    RtVideoDeviceLock(&Device::_singleton);

    RenderBuffer* p = _firstRenderBuffer;
    if (p != nullptr) {
        do {
            // B5 PILOT (B5_RULES D10): RenderBufferData wears the compiler's
            // table, so its slot-0 call with flags 1 is `delete` (virtual dtor,
            // then the base's operator delete = MemFree); guarded on the
            // companion and the field nulled, as ReleaseCompanion does.
            if (RenderBufferData* companion = p->platformData)
                delete companion;
            p->platformData = nullptr;
            p = p->prev;
        } while (p != _firstRenderBuffer);
    }

    RtVideoDeviceUnlock(&Device::_singleton);
}

// ============================================================================
// Video::Device::RecreateDynamic  —  retail 0x1800f80b0, ordinal 3522
// Video::Device::ReleaseDynamic   —  retail 0x1800f80e0, ordinal 3536
//
// The four-call forwarders. ORDER IS LOAD-BEARING and is retail's: recreate
// index, vertex, image, render; release render, image, vertex, index — the
// exact reverse, because a RenderBuffer's companion refers to the ImageBuffer
// companions it is built from.
//
// Note both take NO `this`. They are `QEAAXXZ` — non-static members with a
// `this` in RCX that neither body reads (0x1800f80b0 goes straight to
// `SUB RSP,0x28` and never touches RCX).
// ============================================================================
void Video::Device::RecreateDynamic() {
    Video::IndexBuffer::RecreateDynamic();
    Video::VertexBuffer::RecreateDynamic();
    Video::ImageBuffer::RecreateDynamic();
    Video::RenderBuffer::RecreateDynamic();
    // NOT RETAIL (ext.staggered_passes, graphics/stagger_hold.h): the render targets were
    // re-made empty, so a held shadow map or reflection renders at its next use.
    Graphics::StaggerHold::Invalidate();
}

void Video::Device::ReleaseDynamic() {
    Video::RenderBuffer::ReleaseDynamic();
    Video::ImageBuffer::ReleaseDynamic();
    Video::VertexBuffer::ReleaseDynamic();
    Video::IndexBuffer::ReleaseDynamic();
}
