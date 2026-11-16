// src/game/Game/minimap_upload.cpp
// GUI Keystone F — map-render bring-up.
//
// The minimap subsystem lives at gameWorld+0x148. Its struct:
//   +0x00  byte   dirty flag
//   +0x04  int    width (== map grid edge, e.g. 256)
//   +0x08  ImageBuffer*  terrain GPU texture   (built here)
//   +0x10  ImageBuffer*  overlay GPU texture   (built here)
//   +0x18  uint32*       terrain raw pixel array (width*width, ARGB)
//   +0x20  int           terrain array count/cap
//   +0x28  uint32*       overlay raw pixel array (width*width, ARGB)
//   +0x30  int           overlay array count/cap
//
// In the retail game the two GPU ImageBuffers are created + uploaded on the
// save/load round-trip's LOAD half — Game::Serialize (0x140095a90) calls the
// minimap Serialize (FUN_1400a57f0 @ 0x1400a57f0) whose IsLoading tail runs:
//     FUN_1400a4e80(mapObj)                       // create both ImageBuffers
//     Device::Lock;  CopyToTexture(+0x08, +0x18, w, w);  Device::Unlock
//     Device::Lock;  CopyToTexture(+0x10, +0x28, w, w);  Device::Unlock
// The standalone builds the world DIRECTLY (no serialize round-trip), so the
// buffers never get created and MapUI::SetupImage reads null → the minimap
// can't render. This helper ports FUN_1400a4e80's buffer creation + the
// CopyToTexture upload tail so the raw pixel arrays reach the GPU.
//
// Buffer-creation args verified against FUN_1400a4e80 disasm:
//     Create(imageType=0, usage=2, format=5, mipLevels=0, w, h,
//            filterMode=1, addrU=1, addrV=1, addrW=1, mipData=null)
// (Note filterMode=1 here vs 2 in TradeWork_sub_c780 — matched to the minimap.)
//
// The buffer is a real Video::ImageBuffer (the exe's table 0x14015c8c0 is the
// class's own); CopyToTexture and Device Lock/Unlock are members;
// see src/game/Game/serialize_image_upload.cpp for the sibling single-buffer
// upload this mirrors.

#include "Game/Minimap.h"
#include "video/ImageBuffer.h"
#include "video/Device.h"
#include <new>
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "options.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int kImageBufferSize  = 0x68;
constexpr int kImageBufferAlign = 8;

// Allocate + Create one ImageBuffer, store it in *dst, then upload the raw pixel
// array into it. Returns the buffer (or null).
Video::ImageBuffer* CreateAndUploadOne(Video::ImageBuffer** dst, uint32_t* pixels, int width,
                                       [[maybe_unused]] const char* tag, bool trace) {
    void* mem = System::MemoryAllocator::MemAlloc(kImageBufferSize, kImageBufferAlign);
    if (!mem) { if (trace) printf("[Minimap] %s: MemAlloc(0x68) failed\n", tag); return nullptr; }

    Video::ImageBuffer* img = ::new (mem) Video::ImageBuffer();   // B.3.9 EXA: the real class (exe table 0x14015c8c0 is its own)
    *dst = img;

    [[maybe_unused]] bool ok = img->Create(
        /*imageType*/0, /*usage*/2, /*format*/5, /*mip*/0u,
        width, width, /*filterMode*/1, 1, 1, 1, nullptr);

    // Upload the raw pixel array into the texture.
    ::Video::Device::_singleton->Lock();
    if (pixels)
        img->CopyToTexture(pixels, width, width);
    ::Video::Device::_singleton->Unlock();

    if (trace)
        printf("[Minimap] %s: img=%p createOk=%d pixels=%p w=%d uploaded\n",
               tag, (void*)img, (int)ok, (void*)pixels, width);
    return img;
}

}  // namespace

// ============================================================
// Minimap::CreateAndUpload — create + upload both minimap ImageBuffers.
// ============================================================
// Each call creates and uploads two new buffers into terrainImage / overlayImage
// (matches the retail Serialize which re-creates on every load). Safe no-op if the
// allocator or the device is missing or the size is invalid.
void Minimap::CreateAndUpload() {
    static bool s_trace = Opt().mapTrace != nullptr;

    // ENDGAME B.3.5 batch K1: the CopyToTexture / Device::Lock / Device::Unlock terms
    // went with their call sites; B.3.9 EXA took the ImageBuffer ctor-door and
    // exe-table terms (the buffer is constructed as the real class).
    if (!System::MemoryAllocator::_singleton ||
        !::Video::Device::_singleton) {
        static bool warned = false;
        if (!warned) {
            printf("[Minimap] primitives unresolved — skipping upload "
                   "(alloc=%p dev=%p)\n",
                   (void*)System::MemoryAllocator::_singleton,
                   (void*)::Video::Device::_singleton);
            fflush(stdout);
            warned = true;
        }
        return;
    }

    int width = size;
    if (width <= 0) {
        if (s_trace) printf("[Minimap] width<=0 (%d) — skipping\n", width);
        return;
    }

    // Terrain: terrainPixels -> terrainImage. Overlay: overlayPixels -> overlayImage.
    CreateAndUploadOne(&terrainImage, terrainPixels._data, width, "terrain", s_trace);
    CreateAndUploadOne(&overlayImage, overlayPixels._data, width, "overlay", s_trace);
}

// B4 FORWARD: 2 callers (Minimap::Serialize; src/diag/tool_harness.cpp). The
// null test is the old body's.
extern "C" void Minimap_CreateAndUpload(Minimap* mapObj) {
    if (!mapObj) return;
    mapObj->CreateAndUpload();
}

// ============================================================
// 0x1400a59a0 — Minimap::OnVideoRestart (read in Ghidra)
// ============================================================
// The device-restored half of GameUpdateLoop::OnVideoError's two inlined
// ImageBuffer::Release calls: new companions for the overlay image, then the
// terrain image (the imported Recreate, 0x1401524a8, overlay first as Release
// was), then each image re-uploaded from its retained pixel array under its own
// Device lock: terrain (+0x08 from +0x18) first, then overlay (+0x10 from +0x28),
// size x size. Both images are dereferenced unguarded, as retail does: in a
// running game both exist (the terrain image from the first Draw's PaintTerrain).
void Minimap::OnVideoRestart() {
    overlayImage->Recreate();   // +0x10
    terrainImage->Recreate();   // +0x08
    ::Video::Device::_singleton->Lock();
    terrainImage->CopyToTexture(terrainPixels._data, size, size);
    ::Video::Device::_singleton->Unlock();
    ::Video::Device::_singleton->Lock();
    overlayImage->CopyToTexture(overlayPixels._data, size, size);
    ::Video::Device::_singleton->Unlock();
}
