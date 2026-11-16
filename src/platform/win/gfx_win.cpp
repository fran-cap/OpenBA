// ============================================================================
// src/platform/win/gfx_win.cpp — the graphics-library seam, Win32 (Phase D.4 RENDER §C.1)
// ============================================================================
// src/platform/common/platform_gfx.h declares the three functions; this file is
// their Windows body. ResolveGfxImport below is devicedata.cpp's helper of the
// same name, MOVED UNCHANGED (body, arguments and the stderr line), with its
// history:
//
// These are the only two non-COM graphics imports VideoDX11-x64.dll has. They
// used to be read out of VideoDX11's OWN import address table (the exact
// pointers the shipped code CALLs through), with GetProcAddress as a fallback.
// Standalone mode made the fallback the only path: the DLL is never loaded, so
// there is no IAT. The order is therefore inverted. LoadLibrary (not
// GetModuleHandle) is what makes this work standalone: with VideoDX11 absent,
// nothing else in the process has pulled d3d11.dll or dxgi.dll in yet, so
// asking for a handle to an already-loaded module returns null and the
// renderer never comes up.
//
// The first argument is the retail IAT slot each entry point was read from
// (VideoDX11 0x180013530 CreateDXGIFactory1, 0x180013520 D3D11CreateDevice).
// It is documentation only and nothing reads it (no door: nothing resolves a
// body by retail address).
// ============================================================================

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "platform_gfx.h"

// The header's opaque type is the SDK's FARPROC itself on Win64, so the
// GetProcAddress result is returned with no cast (platform_gfx.h).
static_assert(std::is_same<FARPROC, Platform::GfxProc>::value,
              "Platform::GfxProc must be FARPROC on Win64");

namespace {

FARPROC ResolveGfxImport(uintptr_t iatGhidra, const wchar_t* dll, const char* name) {
    // Primary: the system DLL, loading it if necessary. Same function either
    // way — the shipped DLL's IAT slot holds exactly what GetProcAddress
    // returns, since that is what the loader wrote into it.
    HMODULE m = GetModuleHandleW(dll);
    if (!m) m = LoadLibraryW(dll);
    if (m) {
        if (FARPROC fn = GetProcAddress(m, name))
            return fn;
    }

    // There is no second source: the old fallback read VideoDX11-x64.dll's own
    // IAT slot, and that DLL is never mapped (ENDGAME.md A.1).
    (void)iatGhidra;

    fprintf(stderr, "[Dx11] FAILED to resolve %ls!%s — the renderer cannot start\n",
            dll, name);
    return nullptr;
}

}  // namespace

namespace Platform {

GfxProc ResolveCreateDXGIFactory1() {
    return ResolveGfxImport(0x180013530, L"dxgi.dll", "CreateDXGIFactory1");
}

GfxProc ResolveD3D11CreateDevice() {
    return ResolveGfxImport(0x180013520, L"d3d11.dll", "D3D11CreateDevice");
}

// Engine::Shutdown 0x1800db5e8 / 0x1800db639: `CALL qword ptr [0x18014b118]`,
// kernel32!FreeLibrary (the IAT slot read out of Runtime-x64.dll's import directory with
// pefile). Moved unchanged from src/platform/win/engine_lifecycle_win.cpp (Phase D.4 SDL-5).
void ReleaseLibrary(void* module) {
    FreeLibrary(static_cast<HMODULE>(module));   // CAST-OK: win32
}

}  // namespace Platform
