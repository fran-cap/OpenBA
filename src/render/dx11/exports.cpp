// The two VideoDX11-x64.dll exports — the entire plugin ABI.
//
//     Ordinal 1  CreateDeviceData  0x1800011a0  Video::DeviceData* (Video::Device*)
//     Ordinal 2  GetDeviceName     0x180001230  const char* ()  -> "DirectX 11"
//
// These are the last two pieces of shipped renderer code that ran in the
// process. They are not reachable from any vtable: Runtime's
// Video::Device::InitializeInterfaces GetProcAddress'es them into
// Video::Device+0xd8 / +0xd0 and Video::Device::CreateVideoInterface calls
// through those fields, both inside a single Engine::Startup call. Runtime
// reaches both through `Video::Device::dllSlots[1]`, which
// src/app/video_interface.cpp fills from inside the ported
// InitializeInterfaces (no DLL is loaded, no detour exists).
//
// CreateDeviceData runs on EVERY boot: it allocates the `VideoDX11::DeviceData`
// subclass, which wears the compiler's vtable (B5).

#include "VideoDX11Data.h"     // VideoDX11::DeviceData
#include "VideoDX11Plugin.h"   // the declarations of the two exports

namespace VideoDX11 {

// ============================================================
// Imports — DIRECT REFERENCES (ENDGAME B.3R, block DX)
//
// This was `ExportImports`: two pointers resolved at first use through
// `Dx11ResolveRuntimeImport`, with a readiness test. Both names bound to a
// body in this build, so the test was always true; the struct, the test and
// the null arm below are GONE (B3R_RULES R2, R8):
//
//   IAT 0x1800134c0  ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z
//                    -> System::MemoryAllocator::MemAlloc
//   IAT 0x180013220  ??0DeviceData@Video@@QEAA@PEAVDevice@1@@Z
//                    -> the `Video::DeviceData(owner)` base constructor of
//                       `new VideoDX11::DeviceData(device)` (B5 DEV-A)
// ============================================================

// ============================================================
// 0x1800011a0 — CreateDeviceData(Video::Device* device) -> Video::DeviceData*
//
//     ECX = 0x70, EDX = 4   (LEA ECX,[RDX+0x6c] after MOV EDX,4)
//     p = System::MemoryAllocator::MemAlloc(0x70, 4)
//     if (!p) return NULL                         ; JZ over everything
//     Video::DeviceData::DeviceData(p, device)    ; Runtime base ctor, fills
//                                                 ; vptr and +0x08 owner
//     p->vptr = 0x180018bb0                       ; overwrite with the DX11 one
//     zero the DX11-private tail
//     return p
//
// Two details the field-by-field initialisation preserves and a memset would
// not:
//   * +0x40 is written as a single BYTE (MOV byte [RBX+0x40],AL). +0x41..+0x47
//     keep whatever the base ctor left there.
//   * +0x58 is written as a QWORD -1 (MOV qword [RBX+0x58],-1), which sets
//     BOTH cursorHotspotX and cursorHotspotY to -1 — not just the first.
//
// B5 DEV-A (the dx11 seam): `VideoDX11::DeviceData` is a real subclass of
// `Video::DeviceData`. The base's class-scope operator new is retail's
// MemAlloc(0x70, 4) (src/engine/video/companion_operators.cpp), the base ctor
// and the vptr store are the compiler's, and DeviceData's constructor
// (devicedata.cpp) writes exactly the stores listed above. Retail's
// `if (!p) return NULL` is not reproduced (B5_RULES D2). The return is the
// BASE type, so it is `Video::Device::CreateDeviceDataFn` with no cast.
// ============================================================
::Video::DeviceData* Dx11_CreateDeviceData(::Video::Device* device) {
    return new DeviceData(device);
}

// ============================================================
// 0x180001230 — GetDeviceName() -> "DirectX 11"
//
// Our own literal. An earlier revision returned the shipped DLL's .rdata string
// at 0x1800185c0 to keep the returned POINTER identical as well as the bytes;
// that is not available in Standalone mode, where the DLL is never mapped.
// Nothing compares the address — Runtime copies the result into a System::String
// — so the bytes are the whole of the contract.
//
// The retired Dx11FuncsInit used to cross-check against 0x1800185c0 when the DLL happened to
// be loaded, which is where a divergence would show up.
// ============================================================
const char* Dx11_GetDeviceName() {
    return "DirectX 11";
}

}  // namespace VideoDX11

// Runtime reaches both through Video::Device+0xd8 / +0xd0, which
// src/app/video_interface.cpp fills (through Render::DeviceName /
// Render::CreateDeviceData, render_backend_dx11.cpp) from inside the ported
// InitializeInterfaces; no DLL is loaded.
