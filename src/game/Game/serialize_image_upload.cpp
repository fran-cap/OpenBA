// src/game/Game/serialize_image_upload.cpp
//
// M3.5 Path B session 7 — image-buffer heightmap upload helper.
//
// One exe-internal helper:
//   sub_c780  @ 0x1400ac780  — TradeWork_impl_sub_c780  (heightmap → texture upload)
//
// Called from Geometry::Serialize step 14 (the final IsLoading-only block, see
// docs/history/notes/M35_geom_serialize_findings.md lines 102-110) AFTER the VertexBuffer /
// IndexBuffer / heightmap deserialization is complete. Its job is to take the
// freshly-loaded heightmap raw bytes at self+0x140 and turn them into a
// Video::ImageBuffer at self+0x148 that downstream rendering can sample.
//
// Body shape (verified via Ghidra disassembly @ 0x1400ac780, ~30 instructions):
//   1. img = MemAlloc(0x68, 8)                         // ImageBuffer size
//   2. ImageBuffer::ImageBuffer(img)                   // Runtime DLL ctor
//   3. *(void**)img = 0x14015c8c0                      // the exe's copy of the
//                                                      // class's OWN table: 2+3
//                                                      // = `new ImageBuffer()`
//   4. self+0x148 = img                                // store on self
//   5. img->vtbl[0x48 / 8](img,                        // index 9: Create
//          0,           // ImageType
//          2,           // Usage
//          5,           // Format
//          0,           // mipLevels (unsigned)
//          width,       // (= self+0xC0)
//          width,       // height (square)
//          2,           // FilterMode
//          1, 1, 1,     // AddressMode x3
//          0)           // Array<Array<byte>>* mipData (null)
//   6. Device::Lock( *Device::_singleton )
//   7. ImageBuffer::CopyToTexture(img, self+0x140, width, width)
//   8. Device::Unlock( *Device::_singleton )
//
// The slot+0x48 dispatch is `Create` (?Create@ImageBuffer@Video@@UEAA_N
//   W4ImageType@12@W4Usage@12@W4Format@12@IHHW4FilterMode@12@W4AddressMode@12@
//   44PEAV?$Array@V?$Array@E@Collection@@@Collection@@@Z`). Argument shape
// matches the established NoiseUI / ModDialog usage (see
// src/game/Dialog/noiseui.cpp lines 246-257; src/game/Dialog/moddialog.cpp
// lines 326-327) exactly — including the 12 args via stack overflow on Win64.
//
// Standalone-safety:
// - If the MemoryAllocator or Video::Device singleton is not up, the 0x68-byte
//   MemAlloc fails or the grid size is not positive, the helper one-shot logs
//   and returns early — no crash, no upload.
// - The vtable at 0x14015c8c0 is exe-resident and is Video::ImageBuffer's own
//   (slot 0 the exe scalar deleting dtor, 1-9 thunks onto the class's members):
//   ENDGAME B.3.9 EXA constructs the real class.
//
// Called from WorldGeometry::Serialize (geometry_serialize.cpp),
// WorldGeometry::OnVideoRestart (geometry_draw.cpp) and worldgen.cpp.

// ENDGAME B.3.5 Video/Graphics caller batch: Video::ImageBuffer is a real class
// and the hand-inlined vtable +0x48 dispatch below is its slot 9 (`Create`).
// ImageBuffer.h pulls no Volume.h, so include order against <windows.h> is free.
#include "video/ImageBuffer.h"
#include "video/Device.h"       // ENDGAME B.3.5 batch K1 — Video::Device::{Lock,Unlock}
#include <new>
#include "Game/exe_bodies.h"
#include "Game/WorldGeometry.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include <cstdint>
#include <cstdio>

namespace {

// ============================================================
// Vtable +0x48 dispatch — ImageBuffer::Create signature
// ============================================================
//
// Original disasm @ 0x14003aa20 is `JMP qword ptr [0x140151a88]`. The
// Runtime export it resolves to is:
//   ?Create@ImageBuffer@Video@@UEAA_NW4ImageType@12@W4Usage@12@W4Format@12@
//      IHHW4FilterMode@12@W4AddressMode@12@44PEAV?$Array@V?$Array@E@
//      Collection@@@Collection@@@Z
//
// Eleven int32 / pointer args after `self`. The exe pushes them through
// shadow-space + stack overflow as observed at 0x1400ac7df-0x1400ac818:
//   [RSP+0x58] = 0  (Array<Array<byte>>* mipData)
//   [RSP+0x50] = 1  (AddressMode)
//   [RSP+0x48] = 1  (AddressMode)
//   [RSP+0x40] = 1  (AddressMode)
//   [RSP+0x38] = 2  (FilterMode)
//   [RSP+0x30] = w  (height)
//   [RSP+0x28] = w  (width — packed as uint at +0x24 too)
//   [RSP+0x20] = 0  (mipLevels)
//   R9d        = 5  (Format)
//   R8d        = 2  (Usage)
//   EDX        = 0  (ImageType)
//   RCX        = self
//
} // namespace

// ENDGAME B.3.5: the hand-inlined slot read and its typedef are GONE — +0x48 is
// slot 9 of `Video::ImageBuffer` (B35_RULES 2.3 item 5), so this is a virtual
// call on a PROVED receiver (0x68 bytes from MemAlloc, run through
// `??0ImageBuffer@Video@@`, then given the exe's table).
//
// B.4 GAME-b: the receiver is WorldGeometry (settled from retail's callers, see
// WorldGeometryElements.h), so the body is WorldGeometry::UploadHeightImage and
// reads _gridSize (+0xC0), _heightBytes (+0x140) and _heightImage (+0x148) by
// name. The 2026-05-01 black-screen triage dumps of the ImageBuffer's raw qwords
// (post-ctor, post-Create, final +0x60) went with it: they read the Runtime
// class by offset.

namespace {
constexpr int kImageBufferSize  = 0x68;     // sizeof(Video::ImageBuffer)
constexpr int kImageBufferAlign = 8;
} // namespace

// ============================================================
// 0x1400ac780 — WorldGeometry::UploadHeightImage
// ============================================================
//
// Allocates the 0x68-byte height ImageBuffer, stores it at _heightImage, creates
// it (gridSize x gridSize, format 5) and copies the raw heightmap bytes into it.
void WorldGeometry::UploadHeightImage() {
    if (!System::MemoryAllocator::_singleton) {
        static bool s_warned = false;
        if (!s_warned) {
            printf("[sub_c780] MemAlloc/singleton unavailable — skipping image upload "
                   "(self=%p)\n", (void*)this);
            fflush(stdout);
            s_warned = true;
        }
        return;
    }

    if (!::Video::Device::_singleton) {
        static bool s_warned = false;
        if (!s_warned) {
            printf("[sub_c780] Video::Device singleton not yet ready — "
                   "skipping image upload\n");
            fflush(stdout);
            s_warned = true;
        }
        return;
    }

    // (1) Allocate a 0x68-byte ImageBuffer with 8-byte alignment.
    void* mem = System::MemoryAllocator::MemAlloc(kImageBufferSize, kImageBufferAlign);
    if (!mem) {
        static bool s_warned = false;
        if (!s_warned) {
            printf("[sub_c780] MemAlloc(0x68) failed — skipping image upload\n");
            fflush(stdout);
            s_warned = true;
        }
        return;
    }

    // (2)+(3) Construct the real class: retail's imported ctor followed by the
    // exe's copy of the class's own table (ENDGAME B.3.9 EXA).
    Video::ImageBuffer* img = ::new (mem) Video::ImageBuffer();   // B.3.9 EXA: the real class (exe table 0x14015c8c0 is its own)

    // (4) Store on the geometry.
    _heightImage = img;

    // (5) The heightmap edge length (width == height, square texture).
    int width = _gridSize;
    if (width <= 0) {
        static bool s_warned = false;
        if (!s_warned) {
            printf("[sub_c780] non-positive width %d at self+0xC0 — "
                   "skipping Create/CopyToTexture\n", width);
            fflush(stdout);
            s_warned = true;
        }
        return;
    }

    // (6) Slot 9 of Video::ImageBuffer — `Create` — which is what the exe's
    //     hand-inlined vtable +0x48 dispatch was.
    [[maybe_unused]] bool createOk = img->Create(
             /*type      */ 0,
             /*usage     */ 2,
             /*format    */ 5,
             /*flags     */ 0u,
             /*width     */ width,
             /*height    */ width,
             /*filter    */ 2,
             /*addressU  */ 1,
             /*addressV  */ 1,
             /*addressW  */ 1,
             /*levels    */ nullptr);
    printf("[sub_c780] post-Create img=%p createOk=%d width=%d\n",
           (void*)img, (int)createOk, width);

    // (7) Lock the device, copy heightmap bytes -> texture, unlock.
    ::Video::Device::_singleton->Lock();

    uint8_t* heightmapBytes = _heightBytes;
    printf("[sub_c780] CopyToTexture(img=%p, bytes=%p, w=%d, h=%d)\n",
           (void*)img, (void*)heightmapBytes, width, width);
    if (heightmapBytes) {
        [[maybe_unused]] const uint8_t* hb = heightmapBytes;
        printf("    first 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x\n",
               hb[0], hb[1], hb[2], hb[3], hb[4], hb[5], hb[6], hb[7],
               hb[8], hb[9], hb[10], hb[11], hb[12], hb[13], hb[14], hb[15]);
    }
    fflush(stdout);
    img->CopyToTexture(heightmapBytes, width, width);

    // Re-fetch — the original reads `[0x140151a80]` then `[RAX]` again for the
    // Unlock call.
    ::Video::Device::_singleton->Unlock();

    printf("[sub_c780] DONE img=%p\n", (void*)img);
    fflush(stdout);
}

// ============================================================
// The C entry point
// ============================================================
// B4 X-G2-3: Reimpl_TradeWork_sub_c780 (the C entry of WorldGeometry::UploadHeightImage) had
// no caller left: deleted.
