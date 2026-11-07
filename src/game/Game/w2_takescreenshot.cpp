// src/game/Game/w2_takescreenshot.cpp
//
// Game::GameUpdateLoop::TakeScreenshot  —  port of exe 0x140095da0.
//
// Registered at 0x140095da0 by standalone/reg_w2_screenshot.cpp, which is what
// makes `fn_QuickSave` in src/game/Game/gameupdateloop.cpp non-null. That
// pointer is misnamed: 0x140095da0 is NOT a quicksave handler, it is the
// screenshot grab that GameUpdateLoop::Draw runs when either the quicksave
// (key 0x21) or the quickload (0x22) flag is set. Retail's lifecycle is
// Update sets +0xC0/+0xC1, Draw tests them, this function clears both.
//
// ---------------------------------------------------------------------------
// Retail body (Ghidra 0x140095da0, disassembly cross-read instruction by
// instruction — the decompiler's arg order for Create is misleading):
//
//   RSI  = *(void**)Video::Device::_singleton        ; slot 0x140151a80
//   RBP  = (*(void**)Engine::MainLoop::_singleton)[0x10]   ; slot 0x140151440
//                                                   ; = the Video::Context*
//   this = MemAlloc(0x88, 8)                        ; STATIC 2-arg MemAlloc
//   if (this) { FrameCopyImageBuffer::ctor(this); *(void**)this = &vt_14015e370; }
//   else this = 0;                                  ; then dereferenced anyway
//   (*(vt+0x48))(this, 0, 4, 5, 0x100, RSI[4], RSI[8], 1,1,1,1, nullptr);
//   (*(vt+0x50))(this, RBP, 1);
//   base = Filename(IO::FileSystem::GetSaveDirectory(fs)
//                   + "\\Screenshots\\Screenshot");
//   Filename path;  int n = 0;
//   do { path = base + String::Format("%i.%s", n++, "bmp"); }
//   while (fs->Exists(path));
//   px = MemAlloc((int64)(RSI[8] * RSI[4]) << 2, 4);
//   this->CopyFromTexture(px, RSI[4], RSI[8]);
//   ImageBuffer::SaveImageToFile(path, RSI[4], RSI[8], px);   ; STATIC
//   MemFree(px);
//   (*(vt+0x00))(this, 1);                          ; ~FrameCopyImageBuffer + MemFree
//   *(uint16_t*)(self + 0xC0) = 0;                  ; clears BOTH flags
//
// Device+4 = screen width, Device+8 = screen height (same pair read by
// toolbardialog.cpp:1822 for its centering math).
//
// ---------------------------------------------------------------------------
// Two deliberate, documented departures from a literal transcription:
//
// 1. The vptr overwrite (`*(void**)this = &PTR_FUN_14015e370`) is NOT
//    replicated. That exe table is 12 slots; slot 0 is exe code (0x140019d50,
//    verified: `~FrameCopyImageBuffer(this); if (flag&1) MemFree(this);`) and
//    slots 1-11 are import thunks straight back to the Runtime's own virtuals
//    — i.e. it is a copy of the Runtime's vtable with slot 0 relinked. So the
//    ctor-installed Runtime vptr already dispatches every virtual identically,
//    and leaving it alone is strictly safer: anything inside Create/Generate
//    that dispatches on its own vptr keeps working, which a partially
//    reconstructed exe table could not guarantee.
//    Consequently Create/Generate are called through their exported names
//    (they ARE the functions the exe's +0x48/+0x50 thunks point at — verified
//    with tools/dispatch_slot_map.py on 0x1401520b0 / 0x1401520b8), and the
//    teardown does exactly what slot 0 does: dtor, then MemFree.
//
// 2. (Gone with B3R.) The body once resolved its Runtime methods by export name
//    behind one up-front resolve gate; they are called by name now, so there is
//    no gate and, like retail, no null to test.
// ---------------------------------------------------------------------------

// ENDGAME B.3.5 Video/Graphics caller batch: Video::ImageBuffer is a real class
// and `SaveImageToFile` is its class static. ImageBuffer.h pulls no Volume.h, so
// it is safe after resolve.h's <windows.h> here.
// ENDGAME B.3.5 batch K1 adds FrameCopyImageBuffer.h for Create/Generate. Its chain
// (GeneratedImageBuffer.h -> ImageBuffer.h) reaches no Volume.h either, so the
// after-resolve.h placement this file already uses still holds.
#include "video/ImageBuffer.h"
#include "graphics/FrameCopyImageBuffer.h"
#include "system/String.h"
#include "graphics/Camera.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "io/FileSystem.h"      // ENDGAME B.3.4a closing 4/4
#include "system/Filename.h"
#include <new>
#include "video/Device.h"       // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "engine/MainLoop.h"    // ENDGAME B.3.9 SS2: Engine::MainLoop::_singleton

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"

namespace {

// --- Runtime primitives, all named PE imports of the retail exe -------------
// Called by name (B3R): the STATIC System::MemoryAllocator::MemAlloc / MemFree pair
// (0x140152b60 / 0x140152b68; ECX=size, EDX=alignment), and the System::String /
// System::Filename members the ten export doors were registered to (the RtSystem_*
// shims of src/engine/system/{string,filename}.cpp construct into the caller's raw
// slot with placement new; the calls below do the same):
//   ??HSystem@@YA?AVString@0@AEBV10@PEBD@Z   operator+(String, char const*)   0x140151a08
//   ??HSystem@@YA?AVString@0@AEBV10@0@Z      operator+(String, String)        0x140151a10
//   ?Format@String@System@@SA?AV12@PEBDZZ    String::Format (static varargs)  0x140151560
//   ??1String@System@@QEAA@XZ                ~String                          0x140152af0
//   ??0Filename@System@@QEAA@XZ              Filename()                       0x140151d40
//   ??0Filename@System@@QEAA@AEBVString@1@@Z Filename(const String&)          0x140151d68
//   ??4Filename@System@@QEAAAEAV01@AEBVString@1@@Z  Filename::operator=(String) 0x1401520c8
//   ??1Filename@System@@QEAA@XZ              ~Filename                        0x1401529e8
// ?SaveImageToFile@ImageBuffer@Video@@SA_NAEBVFilename@System@@HHPEAI@Z is a class
// static of the real `Video::ImageBuffer` and the call site below spells it so.

}  // namespace

// ============================================================================
// GameUpdateLoop::TakeScreenshot — exe 0x140095da0 (Draw calls it by name).
// ============================================================================

void GameUpdateLoop::TakeScreenshot() {

    ::Video::Device*    device   = ::Video::Device::_singleton;
    ::Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
    void* videoCtx = mainLoop ? mainLoop->_context : nullptr;      // MainLoop +0x10
    const IO::FileSystem* fileSys = ::IO::FileSystem::_singleton;

    const int width  = device->backBufferWidth;    // Device +0x04
    const int height = device->backBufferHeight;   // Device +0x08

    // --- Grab the back buffer into a FrameCopyImageBuffer ---------------------
    Graphics::FrameCopyImageBuffer* frameCopy =
        static_cast<Graphics::FrameCopyImageBuffer*>(System::MemoryAllocator::MemAlloc(0x88, 8));
    if (frameCopy) {
        // ENDGAME B.3.9 GFX (B9-13): the real class's ctor, not the ??0 door.
        // Retail's exe-side table copy 0x14015e370 is what the object then
        // wears; ours wears the class's compiler table, whose slot 0 is that
        // copy's scalar deleting dtor (FrameCopyImageBuffer.h).
        ::new (frameCopy) Graphics::FrameCopyImageBuffer();
    }

    // Retail's literal arg list: (imageType=0, usage=4, format=5, flags=0x100,
    // width, height, filterMode=1, addressMode 1,1,1, initialData=nullptr).
    // Both are VIRTUALS the class overrides ([9] 0x1800a3b70 and [10] 0x1800a3b20);
    // the receiver wears the Runtime vptr the ctor installed (departure (1)).
    frameCopy->Create(0, 4, 5, Video::ImageBuffer::kViewportSize, width, height, 1, 1, 1, 1, nullptr);
    static_cast<const Graphics::FrameCopyImageBuffer*>(frameCopy)->Generate(videoCtx, 1);

    // --- <saveDir>\Screenshots\Screenshot<N>.bmp, first N that does not exist --
    // The construction / release order is retail's: saveDir, joined, base, then
    // ~joined, ~saveDir (the lambda's scope); path; per probe suffix, full, then
    // ~full, ~suffix; at the end ~path, ~base.
    System::Filename base = [fileSys]() {
        System::Filename saveDir(fileSys->GetSaveDirectory());
        System::String joined(saveDir.AsString() + "\\Screenshots\\Screenshot");   // exe 0x14015e520
        return System::Filename(joined);
    }();

    System::Filename path;

    int index = 0;
    do {
        // exe 0x14015e238 = "%i.%s", exe 0x14015e518 = "bmp" (62 6d 70 00) —
        // BMP, not PNG.
        System::String suffix(System::String::Format("%i.%s", index, "bmp"));
        System::String full(base.AsString() + suffix);
        path = full;
        ++index;
    } while (fileSys->Exists(path));

    // --- Read the pixels back and write the file -----------------------------
    unsigned int* pixels = static_cast<unsigned int*>(
        System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(height * width)) << 2, 4));
    // ?CopyFromTexture@ImageBuffer@Video@@QEAA_NPEAXHH@Z: FrameCopyImageBuffer
    // derives from Video::ImageBuffer (GeneratedImageBuffer.h's chain).
    frameCopy->CopyFromTexture(pixels, width, height);
    // ?SaveImageToFile@ImageBuffer@Video@@SA_NAEBVFilename@System@@HHPEAI@Z is STATIC.
    Video::ImageBuffer::SaveImageToFile(path, width, height, pixels);
    System::MemoryAllocator::MemFree(pixels);


    // --- Teardown: exactly what the exe vtable's slot 0 (0x140019d50) does ----
    // ENDGAME B.3.9 GFX: that slot 0 IS ~FrameCopyImageBuffer then MemFree.
    delete frameCopy;

    // The ONLY clearer of m_quickSavePressed / m_quickLoadPressed. Retail stores
    // a single 16-bit zero over both bytes at +0xC0 (H7: the two named stores).
    m_quickSavePressed = 0;
    m_quickLoadPressed = 0;
}   // ~path, ~base
