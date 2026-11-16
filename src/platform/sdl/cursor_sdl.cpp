// ============================================================================
// src/platform/sdl/cursor_sdl.cpp -- Platform::Cursor over SDL3
// ============================================================================
// Phase D.4 SDL step W5 (plan §A.1; RENDER-3 put the four calls behind Platform::Cursor).
//
// RETAIL (VideoDX11::DeviceData, Video-x64.dll):
//   slot 8 SetImageBuffer (0x180005450): if (cursor) DestroyCursor(cursor);
//       cursor = CreateCursor(Application::GetInstance(), hotX, hotY, 32, 32, and, xor);
//       SetCursor(cursor);
//   slot 3 Shutdown (0x1800051c0):      if (cursor) DestroyCursor(cursor);   // not nulled
//   WM_SETCURSOR callback (0x1800053f0): SetCursor(dd->cursor); ShowCursor(TRUE);
// and Application::PlatformStartup (0x18009fc10): UpdateWindow(hwnd); SetCursor(NULL);
//
// WHAT SDL DOES DIFFERENTLY, AND WHY IT IS ACCEPTED
//  * Create: SDL makes cursors from ARGB surfaces (SDL_CreateColorCursor). The AND / XOR
//    planes are converted by src/platform/common/cursor_planes.cpp; the hotspot and the
//    32 x 32 size are passed through. `instance` is Win32's module handle and is not
//    used. An inverting pixel (AND = 1, XOR = 1; retail's builder never makes one) is
//    written as SDL's own SDL_CreateCursor writes it: 0x00FFFFFF on Windows, which SDL's
//    Windows backend turns back into an inverting pixel, opaque black elsewhere.
//  * Set: SDL_SetCursor. SDL's window procedure answers every client-area WM_SETCURSOR
//    with the cursor SDL last applied, so once the game cursor is set SDL itself keeps
//    re-applying it (the SDL-B hand-in's flicker, SDL's arrow drawn before the synthesised
//    callback re-set the game cursor, is gone).
//    Set(nullptr) is Win32's SetCursor(NULL): NO cursor image. SDL_SetCursor(NULL) means
//    "redraw the current cursor" instead, so a null handle selects a fully transparent
//    cursor made here once (SDL keeps its own blank cursor private). Retail reaches
//    SetCursor(NULL) at window creation and in the WM_SETCURSOR callback until slot 8
//    has built the game cursor (DeviceData::cursor is null until then), so the OS arrow
//    never shows over the client area during boot: the same holds here.
//  * Show: SDL_ShowCursor / SDL_HideCursor. ShowCursor is a display COUNTER in Win32 and
//    a flag in SDL; the only call in the tree is Show(true), so the two agree.
//  * Destroy: SDL_DestroyCursor. Slot 3 leaves DeviceData::cursor dangling after its
//    DestroyCursor (retail; kept, the caller stays unguarded). Win32's DestroyCursor
//    fails harmlessly on a dead handle, and SetCursor of one shows nothing. The handle
//    slot below keeps that OS-side behaviour: a destroyed handle's slot holds no
//    cursor, so Destroy does nothing and Set selects "no cursor" (SDL_DestroyCursor and
//    SDL_SetCursor would also ignore a freed SDL_Cursor*: both look it up in SDL's list
//    of live cursors first, SDL_mouse.c). The three slot-3 callers in
//    src/engine/video/device_startup.cpp (CreateVideoInterface, the failed-create arm of
//    PlatformStartup, PlatformShutdown) delete the DeviceData right after Shutdown; the DX11
//    device-lost recoveries (src/render/dx11/contextdata.cpp, render_pipe_finish.cpp) run
//    slot 3 and then slot 2 on the same DeviceData.
//  * The handle: platform_window.h's CursorHandle is a pointer to Platform::CursorObject,
//    which this backend defines as a slot holding the SDL_Cursor*. Destroy clears the
//    slot and returns it to a free list, so a stale handle reaches a slot whose cursor is
//    null (or, once reused, a live one: the same address reuse an HCURSOR or a freed
//    SDL_Cursor* allows). No pointer is reinterpreted (castmetric).
// ============================================================================

#include "platform_window.h"
#include "cursor_planes.h"
#include "cursor_image.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_platform_defines.h>
#include <SDL3/SDL_surface.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

// The opaque handle's object (platform_window.h declares it; only this backend defines it).
struct Platform::CursorObject {
    SDL_Cursor* sdl;
};

namespace Platform {
namespace Cursor {

namespace {

#if defined(SDL_PLATFORM_WIN32)
constexpr uint32_t kArgbInverted = 0x00FFFFFFu;   // SDL_CreateCursor's Windows value
#else
constexpr uint32_t kArgbInverted = kArgbBlack;    // SDL_CreateCursor's value elsewhere
#endif

// The slots. A deque keeps every slot's address for the life of the process; Destroy
// returns a slot to the free list. Main thread only (slot 8, slot 3 and the WM_SETCURSOR
// callback all run there).
std::deque<CursorObject>   g_slots;
std::vector<CursorObject*> g_freeSlots;

CursorHandle Wrap(SDL_Cursor* c) {
    if (c == nullptr) return nullptr;
    CursorObject* slot;
    if (!g_freeSlots.empty()) {
        slot = g_freeSlots.back();
        g_freeSlots.pop_back();
    } else {
        g_slots.push_back(CursorObject{nullptr});
        slot = &g_slots.back();
    }
    slot->sdl = c;
    return slot;
}

SDL_Cursor* MakeCursor(int32_t hotX, int32_t hotY, int32_t width, int32_t height,
                       const void* andPlane, const void* xorPlane) {
    // The planes as bytes (Win32's layout: WORD-aligned rows, MSB = leftmost pixel).
    const size_t planeBytes = static_cast<size_t>(PlaneStride(width)) * static_cast<size_t>(height);
    std::vector<uint8_t> andBytes(planeBytes);
    std::vector<uint8_t> xorBytes(planeBytes);
    std::memcpy(andBytes.data(), andPlane, planeBytes);
    std::memcpy(xorBytes.data(), xorPlane, planeBytes);

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    PlanesToArgb(andBytes.data(), xorBytes.data(), width, height, kArgbInverted,
                 pixels.data(), width);

    // SDL_CreateColorCursor copies the pixels, so a surface over our buffer is enough.
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888,
                                                 pixels.data(), width * 4);
    if (surface == nullptr) return nullptr;
    // Retina: a 32 x 32 point cursor needs 64 x 64 pixels or macOS scales the 1x image up
    // and blurs it. SDL picks the alternate whose scale matches the display (Cocoa: both
    // become representations of one NSImage). Failure to add it leaves the 1x image.
    std::vector<uint32_t> big(static_cast<size_t>(width) * height * 4);
    SDL_Surface* surface2 = nullptr;
    if (width > 0 && height > 0) {
        UpscaleNearest(pixels.data(), width, height, 2, big.data(), width * 2);
        surface2 = SDL_CreateSurfaceFrom(width * 2, height * 2, SDL_PIXELFORMAT_ARGB8888,
                                         big.data(), width * 2 * 4);
        if (surface2 != nullptr) SDL_AddSurfaceAlternateImage(surface, surface2);
    }
    SDL_Cursor* c = SDL_CreateColorCursor(surface, hotX, hotY);
    SDL_DestroySurface(surface);    // also releases the alternate image list
    SDL_DestroySurface(surface2);
    return c;
}

// Win32's "no cursor" (SetCursor(NULL)): a 32 x 32 cursor whose AND plane is all ones
// and XOR plane all zeros, i.e. every pixel transparent. Made on first use, kept for
// the process (SDL frees it in SDL_Quit with the rest of its cursors).
SDL_Cursor* NoCursor() {
    static SDL_Cursor* blank = nullptr;
    if (blank == nullptr) {
        uint8_t andPlane[32 * 4];
        uint8_t xorPlane[32 * 4];
        std::memset(andPlane, 0xFF, sizeof(andPlane));
        std::memset(xorPlane, 0x00, sizeof(xorPlane));
        blank = MakeCursor(0, 0, 32, 32, andPlane, xorPlane);
    }
    return blank;
}

}  // namespace

CursorHandle Create(void* /*instance*/, int32_t hotX, int32_t hotY, int32_t width,
                    int32_t height, const void* andPlane, const void* xorPlane) {
    SDL_Cursor* c = MakeCursor(hotX, hotY, width, height, andPlane, xorPlane);
    if (c == nullptr)
        std::fprintf(stderr, "[SDL] SDL_CreateColorCursor failed: %s\n", SDL_GetError());
    return Wrap(c);
}

void Set(CursorHandle cursor) {
    SDL_Cursor* c = (cursor != nullptr) ? cursor->sdl : nullptr;
    // A null handle, or a destroyed one, is Win32's SetCursor(NULL) / SetCursor(<dead>):
    // no cursor image.
    SDL_SetCursor(c != nullptr ? c : NoCursor());
}

void Show(bool show) {
    if (show) SDL_ShowCursor();
    else      SDL_HideCursor();
}

// The callers are the Vulkan and Metal backends (src/render/vulkan/device.cpp, src/render/metal/backend.mm).
void SetFromTexels(const uint32_t* texels, int32_t hotX, int32_t hotY) {
    static CursorHandle current = nullptr;
    uint8_t andPlane[4 * 32];
    uint8_t xorPlane[4 * 32];
    TexelsToPlanes(texels, 32, 32, 32, andPlane, xorPlane);
    CursorHandle built = Create(nullptr, hotX, hotY, 32, 32, andPlane, xorPlane);
    if (built == nullptr) return;
    Set(built);                      // SDL keeps drawing the new cursor before the old is freed
    if (current != nullptr) Destroy(current);
    current = built;
}

void Destroy(CursorHandle cursor) {
    if (cursor == nullptr || cursor->sdl == nullptr) return;   // DestroyCursor(NULL / dead): fails
    SDL_DestroyCursor(cursor->sdl);
    cursor->sdl = nullptr;
    g_freeSlots.push_back(cursor);
}

}  // namespace Cursor
}  // namespace Platform
