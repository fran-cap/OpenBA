// Stop Runtime from loading VideoDX11-x64.dll, and hand it our renderer instead.
//
// This is the piece that makes Standalone mode a real replacement rather than a
// patch of the shipped DLL. Everything else in src/render/dx11/ replaces *code*; this
// replaces the DLL's role in Runtime's plugin protocol.
//
// The protocol
// ------------
// Video::Device::InitializeInterfaces (Runtime 0x1800f79f0) walks three
// interface slots on the Device singleton — DX9, DX11, GL at +0xb0, +0xc8,
// +0xe0, stride 0x18 — and for each one:
//
//     if (*(HMODULE*)(this + 0xb0 + idx*0x18) == 0) {        <-- the opening
//         LoadLibraryW(GetDLLForInterface(idx));
//         GetProcAddress("CreateDeviceData") -> slot + 0x10;
//         GetProcAddress("GetDeviceName")    -> slot + 0x08;
//         if (either is null) { FreeLibrary; zero all three; }
//     }
//
// **It skips a slot that is already populated.** So we do not have to
// reimplement it, intercept LoadLibrary, or ship a stub DLL: we only have to
// get there first. Video::Device::CreateVideoInterface (0x1800f7930) then
// requires all three words to be non-zero, sets Device+0xf8 = idx, and calls
// the CreateDeviceData pointer, storing the result at Device+0x100.
//
// Note what the HMODULE word is used for: nothing but a non-zero test, plus a
// FreeLibrary on the GetProcAddress-failed path. It is never called through.
//
// Why a detour rather than writing the fields directly
// ----------------------------------------------------
// The Device singleton does not exist before Engine::Startup — Runtime
// 0x1801fe7a8 still reads null when the address registry is being built — and
// InitializeInterfaces runs inside Startup. There is no window. So we
// fill slot 1 from inside InitializeInterfaces itself, which then skips DX11
// and handles DX9/GL exactly as before. Our ported
// ?InitializeInterfaces@Device@Video@@QEAAXXZ (0x1800f79f0,
// src/platform/win/video_interfaces_win.cpp) calls VideoInterfaceFillDx11Slot
// as its first statement; the old byte-patch detour on the DLL's own body went
// with the DLL (ENDGAME.md A.1/A.3).

// ENDGAME B.3.5 caller batch — INCLUDE ORDER IS LOAD-BEARING HERE. The runtime
// type headers come FIRST, before `resolve.h`: resolve.h pulls in <windows.h>,
// and <wingdi.h>'s global `Rectangle()` FUNCTION then hides `Volume.h`'s global
// `struct Rectangle`, which breaks any runtime/types header that reaches Volume.h
// (batch I3 hit exactly this in main.cpp and perfx.cpp). Do not
// reorder these two blocks.
#include "video/Device.h"    // Video::Device, Video::Device::InterfaceSlot

// B5 DEV-A: the two DLL exports (src/render/dx11/exports.cpp) and the cutover-only DX9
// placeholder (src/render/dx11/dx9_placeholder.cpp), declared by the plugin's header
// instead of a local extern block. Reached through the backend-neutral entry points
// (src/render/common/render_backend.h): the DX11 backend's are forwarders to those four
// (src/render/dx11/render_backend_dx11.cpp); another backend supplies its own.
#include "render_backend.h"
#include "ext/render_backend/render_backend.h"   // EXT SEAM (NOT RETAIL): Ext::RenderBackend::BootSelect

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>   // dlopen: the POSIX sentinel (Phase D.4 LINUX-1)
#endif
#include <cstdint>
#include <cstdio>

namespace {

// ENDGAME B.3.5 caller batch: the four raw-offset constexprs that used to sit
// here — `kSlot0 = 0xb0`, `kSlotStride = 0x18`, `kOffModule/kOffGetName/kOffCreate
// = 0x00/0x08/0x10` — are GONE. `src/engine/video/Device.h` declares exactly
// this record as `Video::Device::InterfaceSlot dllSlots[3]` at +0xB0 and asserts
// all five numbers (sizeof 0x18, alignof 8, and the three field offsets), and its
// banner names THIS FILE as the hand-writer it was documenting. The two fills
// below index the named array and write the named fields, so the compiler checks
// what the comments used to assert. Two of the three slots stay UNTOUCHED, which
// is RULE B5-9: `InitializeInterfaces` keeps its deleted `LoadLibraryW` arm and
// this pre-fill loads NOTHING — no `LoadLibraryW` of a plugin, no
// `GetProcAddress`, `loaded`/`rejected` still 0.
constexpr int kInterfaceDX9  = 0;
constexpr int kInterfaceDX11 = 1;

bool g_filled = false;

// The non-zero word Runtime tests, and would FreeLibrary on its error path.
// A fabricated value would make that FreeLibrary undefined; our own module
// handle, with an extra reference taken for the purpose, makes it a harmless
// refcount decrement on a module that is never going away.
// Review F-213: ONE reference per call, i.e. per filled slot. The Engine shutdown
// releases every populated slot (retail: one FreeLibrary per slot, each slot's
// plugin loaded once), and both slots hold this handle; a cached handle took one
// reference for two releases.
#if defined(_WIN32)
HMODULE SentinelModule() {
    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(GetModuleHandleW(nullptr), self, MAX_PATH);
    HMODULE h = LoadLibraryW(self);                   // +1 ref on OpenBA.exe
    if (!h) h = GetModuleHandleW(nullptr);            // still non-zero
    return h;
}
#else
// POSIX: dlopen(NULL) is the main program's handle with a reference taken, so the
// Platform::ReleaseLibrary (dlclose) that the shutdown makes on every slot is the
// same harmless refcount decrement.
void* SentinelModule() {
    return dlopen(nullptr, RTLD_NOW);
}
#endif

void FillDx11Slot(Video::Device* device);

}  // namespace

// Wave 9 lane ENGIO. The slot fill, reachable WITHOUT the detour.
//
// See the correction on InstallVideoInterfacePrefill below: at cutover the
// detour is the ONLY path that reaches this, and the detour cannot exist. Whoever
// ports Video::Device::InitializeInterfaces (0x1800F79F0) must call this from
// the ported body -- that is what turns the pre-fill from a DLL-present hack
// into the real thing.
void VideoInterfaceFillDx11Slot(Video::Device* device) { FillDx11Slot(device); }

// Cutover only: slot 0 (DX9) with the placeholder plugin, so the Device's
// interface array keeps one entry per slot exactly as it does on a retail
// machine. Called by the ported InitializeInterfaces after its LoadLibraryW of
// VideoDX9-x64.dll failed, and only with the Runtime DLL absent.
void VideoInterfaceFillDx9Placeholder(Video::Device* device) {
    // B5 DEV-A: the Device arrives typed and both entry points already have the
    // slot's exact types (render_backend.h), so the three dx11_seam casts that
    // stood here are gone; the HMODULE goes into the header's `void* module`.
    Video::Device::InterfaceSlot& slot = device->dllSlots[kInterfaceDX9];
    slot.module           = SentinelModule();
    slot.getDeviceName    = &Render::PlaceholderDeviceName;
    slot.createDeviceData = &Render::PlaceholderCreateDeviceData;
    printf("[VideoInterface] DX9 slot filled with the cutover PLACEHOLDER on Device %p "
           "(VideoDX9-x64.dll cannot load without Runtime-x64.dll)\n", static_cast<void*>(device));
}

namespace {

void FillDx11Slot(Video::Device* device) {
    // EXT SEAM (NOT RETAIL, src/ext/render_backend/render_backend.h): the dual exe picks the backend this
    // slot makes before the first listing of the adapters (once; a no-op in a one-backend build).
    Ext::RenderBackend::BootSelect();
    // B5 DEV-A: as the placeholder fill above -- typed Device, exact types.
    Video::Device::InterfaceSlot& slot = device->dllSlots[kInterfaceDX11];
    slot.module           = SentinelModule();
    slot.getDeviceName    = &Render::DeviceName;
    slot.createDeviceData = &Render::CreateDeviceData;

    g_filled = true;
    printf("[VideoInterface] DX11 slot pre-filled on Device %p: "
           "module=%p GetDeviceName=%p CreateDeviceData=%p — Runtime will not "
           "LoadLibrary VideoDX11-x64.dll\n",
           static_cast<void*>(device), slot.module,
           reinterpret_cast<void*>(&Render::DeviceName),
           reinterpret_cast<void*>(&Render::CreateDeviceData));
    fflush(stdout);
}

}  // namespace

// True once our pointers are actually on the Device. Reported by DumpVideoState.
bool VideoInterfacePrefilled() { return g_filled; }
