// VideoDX11 DeviceData — vtable 0x180018bb0, slots 0/1/2/3/8 and the eleven
// resource factories 9..19 (lane L6). Slots 4-7 are the one-liner stubs and
// live in devicedata_stubs.cpp; this file does not touch them.
//
// This is the platform boundary itself: slot 2 is the only place in the process
// that calls D3D11CreateDevice / IDXGIFactory1::CreateSwapChain, and slot 3 is
// the only place that tears them down. Slots 2 and 3 are also the two functions
// that can leave the user with no window, so everything below is a transcription
// of the disassembly rather than a reconstruction of intent — including the
// parts that look like bugs (see "Original quirks reproduced" at the bottom).
//
// ---------------------------------------------------------------------------
// The two-globals trap, and a correction to decomp/videodx11.md
// ---------------------------------------------------------------------------
// Ghidra renders THREE different singletons as `_singleton_exref`:
//
//   VideoDX11 IAT 0x180013310  ?_singleton@Application@1@0PEAV11@EA
//   VideoDX11 IAT 0x180013480  ?_singleton@Configuration@Video@@0PEAV12@EA
//   VideoDX11 IAT 0x1800134b8  ?_singleton@Device@Video@@0PEAV12@EA
//
// decomp/videodx11.md conflated the first two with `Video::Device` in three
// places. All three are corrected here, each against the instruction:
//
//   * "DeviceData+0x40 is mirrored into Application::_singleton+0x40 on
//     shutdown" — it is mirrored into **Video::Configuration+0x40**
//     (0x18000520f loads 0x180013480, not 0x180013310).
//   * "DXGI_SWAP_CHAIN_DESC.Windowed = Device+0x40 == 0" — it is
//     **Configuration+0x40 == 0** (0x1800045af loads 0x180013480).
//     Device+0x40 is not a flag at all: it is the base pointer of the
//     Device::GraphicsInterface array, which slot 1 fills (0x180004dca).
//   * "ResolveMultisampleMode clamps Device+0x24" — it clamps
//     **Configuration+0x24** (0x18000451f loads 0x180013480).
//
// Getting any of these wrong reads a flag out of a pointer field.
//
// ---------------------------------------------------------------------------
// Video::Configuration field map (Runtime), recovered for this lane
// ---------------------------------------------------------------------------
// Every offset below is proven twice: by the registry key it is loaded from in
// Video::Configuration::Startup (Runtime 0x1800f6680, which does
// String::Format("%s%s", "Video", <suffix>) then a Persistence map lookup), and
// by the default the ctor writes (Runtime 0x1800f6f10).
//
//   off    registry key             ctor default
//   -----  -----------------------  ------------
//   +0x00  VideoSettings (bool)     true
//   +0x04  VideoInterface           1
//   +0x08  VideoAdapter             0
//   +0x0c  VideoWidth               -1
//   +0x10  VideoHeight              -1
//   +0x14  VideoWindowX             -1
//   +0x18  VideoWindowY             -1
//   +0x1c  VideoWindowWidth         -1      <- the "skip override" sentinel
//   +0x20  VideoWindowHeight        -1      <- the "skip override" sentinel
//   +0x24  VideoMultisample         1
//   +0x28  VideoTextureFilter       0
//   +0x2c  VideoShadowScalar        1
//   +0x30  VideoShaderDetail        2
//   +0x34  VideoReflectionDetail    1
//   +0x38  VideoRefreshN            60
//   +0x3c  VideoRefreshD            1
//   +0x40  VideoFullscreen (bool)   true
//   +0x41  VideoVSync (bool)        true    (L5 already uses this as SyncInterval)
//   +0x42  not persisted            false
//
// ---------------------------------------------------------------------------
// COM vtable indices used by this file
// ---------------------------------------------------------------------------
// Index counted from QueryInterface = 0, INCLUDING inherited methods. Every one
// was read off the call site's displacement in the disassembly and then checked
// against mingw-w64's d3d11.h / dxgi.h, not assumed from either alone.
//
//   interface           idx  byte   method                        call site
//   ------------------  ---  -----  ----------------------------  ----------
//   IUnknown              2  0x010  Release                       many
//   IDXGIFactory1        12  0x060  EnumAdapters1                 0x180004e72
//   IDXGIFactory          8  0x040  MakeWindowAssociation         0x18000507b
//   IDXGIFactory         10  0x050  CreateSwapChain               0x180005091
//   IDXGIAdapter          7  0x038  EnumOutputs                   0x180004ead
//   IDXGIAdapter1        10  0x050  GetDesc1                      0x180004760
//   IDXGIOutput           7  0x038  GetDesc                       0x180004f52
//   IDXGIOutput           8  0x040  GetDisplayModeList            0x18000491a
//   IDXGISwapChain       10  0x050  SetFullscreenState            0x180005271
//   IDXGISwapChain       13  0x068  ResizeBuffers                 0x180004493
//   IDXGISwapChain       14  0x070  ResizeTarget                  0x180004464
//   ID3D11Device          5  0x028  CreateTexture2D               0x180005332
//   ID3D11Device         29  0x0e8  CheckFormatSupport            0x18000500a
//   ID3D11Device         30  0x0f0  CheckMultisampleQualityLevels 0x180004bac
//   ID3D11DeviceContext  33  0x108  OMSetRenderTargets            0x18000438e
//   ID3D11DeviceContext 111  0x378  ClearState                    0x18000439b
//
// The IDXGIAdapter1 index 10 for GetDesc1 is the one worth spelling out:
// IDXGIAdapter contributes EnumOutputs(7)/GetDesc(8)/CheckInterfaceSupport(9)
// on top of IDXGIObject's 3..6, so GetDesc1 lands at 10 = +0x50. Slot 1 calls
// +0x50 on the object EnumAdapters1 produced, which is consistent.
//
// ---------------------------------------------------------------------------
// Runtime import IAT map
// ---------------------------------------------------------------------------
// VideoDX11's Runtime import thunks are one contiguous run. The base is
// 0x180013218: index N sits at 0x180013218 + N*8, in the order objdump -p
// prints the Runtime-x64.dll import name table. That base was DERIVED, not
// assumed — it is the unique value consistent with all six IAT addresses
// earlier lanes had already pinned independently (?Lock@Device = 0x180013408,
// ?_singleton@Configuration = 0x180013480, ??1ContextData = 0x180013470,
// ?_singleton@Device = 0x1800134b8, ?MemAlloc = 0x1800134c0,
// ?MemFree = 0x1800134d0), and it additionally predicts all eleven base-ctor
// slots this file needs, each of which matches the CALL displacement in the
// corresponding factory. That is 17 independent confirmations of one constant.

#include "VideoDX11Plugin.h"   // Dx11EnumerateDxgiInterfaces (defined here)
#include "stall_debug.h"
#include "boot_window.h"
#include "dx11_backend_access.h"
#include "async_gpu_create/async_create.h"   // NOT RETAIL: ext.async_gpu_create
#include "share_gpu_data/share_create.h"     // NOT RETAIL: ext.share_gpu_data
#include "create_on_demand/lazy_create.h"    // NOT RETAIL: ext.create_on_demand
#include "ext/fullscreen_mode/fullscreen_mode.h"   // NOT RETAIL: ext.fullscreen_mode's borderless fullscreen
#include "VideoDX11Data.h"
#include "d3d_debug.h"                       // NOT RETAIL: BANISHED_D3D_DEBUG
#include "options.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "system/String.h"   // -Iruntime/types
#include "system/Error.h"    // ENDGAME B.3.4b System callers: System::Error
#include "system/WideString.h"  // ENDGAME B.3.4b System callers: System::WideString
// ENDGAME B.3.5 block D2 / B35_FILEMAP §3f: the real `Video::Device` and its four
// nested value types, replacing this file's four local name-and-layout mirrors.
#include "video/Device.h"
#include "video/refresh_rate_at.h"   // Video::RefreshRateAt (review F-171)
// B5 DEV-A: the `Video::DeviceData` base `VideoDX11::DeviceData` derives from.
#include "video/CompanionData.h"
#include "application/Application.h"   // -Iruntime/types: ENDGAME B.3.7 callers (K5)
#include "platform_gfx.h"   // Platform::ResolveD3D11CreateDevice / ResolveCreateDXGIFactory1
#include "ao/ao_depth_views.h"   // NOT RETAIL: ext.ambient_occlusion's readable main depth (CreateDepthBuffer)
#if BANISHED_TWEAK_OVERLAY
#include "tweak_overlay.h"   // the debug tweak overlay (debug flavour only, src/diag)
#endif
#include "platform_window.h"   // Phase D.4 SDL W2: the message ids; RENDER-3: window state, show, cursor
#include "platform_thread.h"   // Platform::CurrentThreadId (TraceGeom)
#include <new>
#include <type_traits>

namespace VideoDX11 {
namespace {

// ============================================================
// Runtime-x64.dll imports — DIRECT REFERENCES (ENDGAME B.3R, block DX)
//
// This was the `Imports` struct: 21 function pointers filled at first use by a
// loop over a {field, mangled name, VideoDX11 IAT slot} table through
// `Dx11ResolveRuntimeImport`, with an all-or-nothing readiness test that made
// every slot below forward to the shipped implementation when any row was
// missing. Every one of the 21 names bound to a body in this build, so the test
// was always true: the struct, the table, the test and each slot's dead
// forwarding arm are GONE, and each use is a call of the body the NAME bound
// (B3R_RULES R2, R8):
//
//   retail IAT   mangled name                                  now
//   0x1800134c0  ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z System::MemoryAllocator::MemAlloc
//   0x1800134d0  ?MemFree@MemoryAllocator@System@@SAXPEAX@Z    System::MemoryAllocator::MemFree
//   0x180013478  ??0ContextData@Video@@…        (and the ten  the base ctor of each
//   …            other ??0XData@Video@@ ctors, slot 9..19)    `new XData(owner)` below
//   0x180013250  ??0Adapter@Device@Video@@QEAA@XZ              Adapter::Adapter()
//   0x1800132b8  ??0Adapter@Device@Video@@QEAA@AEBU012@@Z      Adapter::Adapter(const Adapter&)
//   0x1800134c8  ??1Adapter@Device@Video@@QEAA@XZ              Adapter::~Adapter()
//   0x1800132d0  ??0GraphicsInterface@Device@Video@@QEAA@XZ    GraphicsInterface::GraphicsInterface()
//   0x1800132b0  ??0GraphicsInterface@…@@QEAA@AEBU012@@Z       GraphicsInterface(const GraphicsInterface&)
//   0x1800132c8  ??1GraphicsInterface@Device@Video@@QEAA@XZ    GraphicsInterface::~GraphicsInterface()
//   0x180013308  ??0DisplayMode@Device@Video@@QEAA@HH@Z        Video_DisplayMode_Ctor
//   0x1800132f8  ??1DisplayMode@Device@Video@@QEAA@XZ          Video_DisplayMode_Dtor
//
// B5 DEV-B (B5_RULES D6): Adapter and GraphicsInterface carry retail's ctor /
// copy ctor / dtor as real members and a `System::String name`, so their four
// rows are C++ constructions and destructions below (locals, and placement-new
// plus explicit destructor calls on the growers' raw MemAlloc'd storage). The
// two DisplayMode rows stay C functions (Device.h records why). The eleven `??0XData`
// names bound to one-line doors whose whole body is `::new (self) XData(owner)`;
// B5: each factory is `new XData(owner)`, whose base constructor is that
// construction (the twelve `*Data` classes are real subclasses).
//
// The earlier removals (the Video::Device members, System::String /
// WideString, Application, ImageBuffer::CopyFromTexture and the DeviceData
// base dtor) are recorded in git history (ENDGAME B.3.1 / B.3.4b / B.3.5 J /
// B.3.7 / B.3.9).
// ============================================================

// ============================================================
// d3d11.dll / dxgi.dll entry points
//
// These are the only two non-COM graphics imports the DLL has. Where they come
// from is the platform's business since Phase D.4 RENDER §C.1: the
// GetModuleHandleW / LoadLibraryW / GetProcAddress body that was
// ResolveGfxImport here moved unchanged to src/platform/win/gfx_win.cpp, behind
// src/platform/common/platform_gfx.h (that file carries its history: the
// retail IAT, why LoadLibrary comes first standalone). Platform::GfxProc is
// FARPROC on Win64, so the one cast per entry point stays here.
//
// B5 DEV-A (D4): CreateDXGIFactory1's out-parameter is `void**` in dxgi.h, but
// both callers here pass IID_IDXGIFactory1 and nothing else, so the resolved
// pointer is typed as what it returns into: `IDXGIFactory1**`. The pointer is
// already the product of the cast below; typing it means the
// two call sites write straight into their IDXGIFactory1* (DeviceData::unk10 /
// the walk's local) with no `void**` cast of their own (the `com_iid` floor
// ceiling is full).
// ============================================================
typedef HRESULT (WINAPI *CreateDXGIFactory1_t)(REFIID, IDXGIFactory1**);
typedef HRESULT (WINAPI *D3D11CreateDevice_t)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                              const D3D_FEATURE_LEVEL*, UINT, UINT,
                                              ID3D11Device**, D3D_FEATURE_LEVEL*,
                                              ID3D11DeviceContext**);

CreateDXGIFactory1_t GetCreateDXGIFactory1() {
    static CreateDXGIFactory1_t fn = reinterpret_cast<CreateDXGIFactory1_t>(   // CAST-OK: win32
        Platform::ResolveCreateDXGIFactory1());   // GetProcAddress's FARPROC
    return fn;
}
D3D11CreateDevice_t GetD3D11CreateDevice() {
    static D3D11CreateDevice_t fn = reinterpret_cast<D3D11CreateDevice_t>(   // CAST-OK: win32
        Platform::ResolveD3D11CreateDevice());   // GetProcAddress's FARPROC
    return fn;
}

// IID_IDXGIFactory1, transcribed from the 16 bytes at VideoDX11 0x180018b98
// that slots 1 and 2 hand to CreateDXGIFactory1:
//   78 ae 0a 77  6f f2  ba 4d  a8 29 25 3c 83 d1 b3 87
// Spelled out rather than using the dxguid.lib symbol so this file does not add
// a library to the link line, and so the shipped bytes sit next to their use.
const GUID kIID_IDXGIFactory1 =
    { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };

// ============================================================
// Runtime-side fields this file reads and writes
//
// B5 DEV-A (B5_RULES D8): the `Dev::` / `Cfg::` byte-offset tables and the
// `Field<T>` accessor that read them are GONE. Every site names the member of
// the real `Video::Device` / `Video::Configuration` (src/engine/video/
// Device.h, which asserts each offset); the dx11-side proof of each stays here:
//
//   Device+0x04/+0x08  backBufferWidth/Height  the swap chain's back-buffer size.
//                      Written by slot 2 from the display-mode table and then
//                      again by Video::Device::AdjustDisplay (Runtime 0x1800f7e20)
//                      from GetClientRect; read by FillSwapChainDesc
//                      (0x1800045f6 / 0x1800045ff) and by the buffer creators.
//   Device+0x0c        displayModeIndex        index into the mode table at +0x20;
//                      0x180004edc scales it by 0x18 (`LEA RCX,[RAX+RAX*2]`, *8).
//   Device+0x10        refreshRateIndex        index into the current mode's
//                      refresh-rate array, stride 8 (0x180004609 / 0x18000461c).
//   Device+0x14/+0x18  dxgiAdapterIndex/       the DXGI adapter and output slot 2
//                      dxgiOutputIndex         re-enumerates with (0x180004e6f
//                                              EnumAdapters1, 0x180004eaa EnumOutputs).
//   Device+0x20        modeData                DisplayMode[], stride 0x18 (0x180004ee4).
//   Device+0x40/+0x48  interfaceData/Count     the GraphicsInterface array slot 1's
//                                              tail grows (0x180004dca-0x180004ddc).
//                                              NOT a bool — see the header note.
//   Device+0x90        windowHandle            the HWND: DXGI_SWAP_CHAIN_DESC::
//                                              OutputWindow (0x18000464e), ShowWindow,
//                                              IsWindowVisible, MakeWindowAssociation.
//   Device+0x9c        settingsChanged         "swap chain needs a rebuild". Set by
//                                              the resize helper before it does
//                                              anything and cleared only on its
//                                              success path; ContextData::
//                                              BeginCommands bails to the helper
//                                              while it is set (L5).
//   Device+0xa0        monitor                 HMONITOR from DXGI_OUTPUT_DESC::
//                                              Monitor (0x180004f5d).
//   Device+0x100       deviceData              the active Video::DeviceData*.
//   Configuration+0x1c/+0x20 windowWidth/Height  -1 = skip the override
//   Configuration+0x24 multisample, +0x40 fullscreen,
//   Configuration+0x42 field42                 not persisted; suppresses the ShowWindow
// ============================================================

// Device.h keeps the HWND as `void*` (that header is <windows.h>-free); this is
// the one place it becomes a window handle.
inline HWND WindowOf(const ::Video::Device* dev) {
    return static_cast<HWND>(dev->windowHandle);  // CAST-OK: win32
}

// ENDGAME B.3.9 VID: Video::Configuration::_singleton and
// Application::_singleton are the class statics; the import rows that resolved
// them by name are gone. B5 DEV-A: both typed (the B.3.7-era bridge casts at the
// RegisterMessageCallback / GetInstance sites went with the `void*`).
inline ::Video::Configuration*     ConfigOf() { return ::Video::Configuration::_singleton; }
inline ::Application::Application* AppOf()    { return ::Application::Application::_singleton; }

// Phase D.4 RENDER-3: DeviceData::cursor is a Platform::CursorHandle (retail's HCURSOR;
// a slot holding an SDL_Cursor* since SDL step W5, src/platform/sdl/cursor_sdl.cpp).

// ============================================================
// Runtime value types this file constructs on the stack
//
// ENDGAME B.3.5 block D2, B35_FILEMAP §3f — THE FOUR LOCAL MIRRORS ARE GONE.
// This file used to re-declare `DisplayMode`, `RefreshRate`, `Adapter` and
// `GraphicsInterface` as byte-for-byte name-and-layout duplicates of
// `src/engine/video/Device.h`'s, each with its own `static_assert`s repeating
// the Runtime-side ones. That is B34_RULES §1.5.2's duplicate-POD hazard four
// times over: two independent descriptions of one layout, either of which could
// drift. They are now ALIASES of the real nested classes
// `Video::Device::{DisplayMode,RefreshRate,Adapter,GraphicsInterface}`, so there
// is ONE description and dx11 gets the class's own field names. RULE B5-1.2 /
// B35_RULES §1.2: dx11 compiles with the shared include set, so this needs no
// Makefile change and is simply another `runtime/types` header included here.
//
// WHAT THIS FILE PROVED THAT THE RUNTIME SIDE COULD NOT, and it is now recorded
// in the header instead of here: `Adapter+0x2c`/`+0x30`, which Wave 6 lane B
// could only describe as "ctor = -1, meaning not recovered", are the DXGI
// enumeration coordinates. `DeviceData_EnumerateInterfaces` below is the WRITER
// (`EnumAdapters1`'s loop counter and `EnumOutputs`'s), and `dev->dxgiAdapterIndex`
// / `dev->dxgiOutputIndex` (Device+0x14/+0x18, the copies `ValidateConfiguration`
// makes) are where this file READS them back. Header fields renamed to
// `dxgiAdapterIndex` / `dxgiOutputIndex` accordingly.
//
// The sizes and every offset are still asserted — by Device.h, once, against the
// Runtime bodies AND against this file's strides (0x180004c43 for Adapter,
// 0x180005b00 for GraphicsInterface). The two strides agree with the Runtime
// side's, which is what made the collapse safe rather than hopeful.
// ============================================================

using DisplayMode       = ::Video::Device::DisplayMode;
using RefreshRate       = ::Video::Device::RefreshRate;
using Adapter           = ::Video::Device::Adapter;
using GraphicsInterface = ::Video::Device::GraphicsInterface;

// Re-stated here, from THIS file's own instruments, so the collapse is checked
// rather than assumed: if Device.h ever disagreed with the strides dx11 walks,
// the build breaks here.
static_assert(sizeof(Adapter) == 0x38,
              "Video::Device::Adapter stride — dx11's array grower at 0x180004c43");
static_assert(sizeof(GraphicsInterface) == 0x18,
              "GraphicsInterface stride — dx11 at 0x180005b00");
static_assert(sizeof(DisplayMode) == 0x18, "Video::Device::DisplayMode stride");
static_assert(sizeof(RefreshRate) == 8, "RefreshRate stride (Device+0x10 scales by 8)");

// The three OS/DXGI structure offsets slot 1 and slot 2 depend on. The original
// hard-codes them as displacements, so if the SDK headers ever disagreed the
// port would read the wrong field silently. Pinned here instead.
//
//   DXGI_OUTPUT_DESC::Monitor at +0x58   — slot 2 stores it to Device+0xa0
//                                          (`MOV RAX,[RBP+0x18]` where the desc
//                                          base is RBP-0x40, i.e. +0x58)
//   DXGI_OUTPUT_DESC::DeviceName at +0x0 — slot 1's " (Display n)" suffix
//                                          since D.4 RENDER §C.3
//
// Retail's slot 1 read the suffix from MONITORINFOEXW::szDevice (+0x28, `LEA
// RCX,[RBP+0x88]` against a struct based at RBP+0x60, cbSize = 0x68 written
// literally at 0x180004811) after GetMonitorInfoW. RENDER §C.3 reads
// DXGI_OUTPUT_DESC::DeviceName instead (the same GDI name), so the
// MONITORINFOEXW size assert that guarded the cbSize literal is gone with the
// call.
static_assert(offsetof(DXGI_OUTPUT_DESC, Monitor) == 0x58,
              "DXGI_OUTPUT_DESC::Monitor offset (0x180004f59)");
static_assert(offsetof(DXGI_OUTPUT_DESC, DeviceName) == 0 && sizeof(DXGI_OUTPUT_DESC::DeviceName) == 64,
              "DXGI_OUTPUT_DESC::DeviceName is WCHAR[32] at +0x0 (slot 1's name suffix)");
static_assert(sizeof(DXGI_SWAP_CHAIN_DESC) == 0x48,
              "DXGI_SWAP_CHAIN_DESC is the 9 qwords FillSwapChainDesc zeroes");
static_assert(sizeof(DXGI_MODE_DESC) == 0x1c,
              "DXGI_MODE_DESC stride — slot 1 allocates count*0x1c (0x180004941)");

// Forward decl — the WM_ACTIVATE handler and the resize helper are mutually
// referenced through RegisterMessageCallback.
int  DeviceData_Resize(DeviceData* self);

// (B5 DEV-A, B5_RULES D9: the `Orig` template, the "run the shipped DLL's
// slot" forwarder, is gone with its two arms — slots 1 and 2 return false.)

// ============================================================
// 0x180004510 — ResolveMultisampleMode() -> sampleCount
//
// Clamp Video::Configuration's multisample INDEX against the adapter's mode
// count, then walk downwards until the device reports the mode supported, write
// the (possibly lowered) index back into the configuration, and return
// index + 1 as the D3D11 sample count.
//
// Two faithfulness details:
//   * The downward walk is unbounded. If index 0 were unsupported it would run
//     off the bottom into negative indices. The shipped code has no floor and
//     neither does this; Video::Device::IsMultisampleModeSupported(0) is always
//     true for a device that got this far (1x MSAA), so the loop terminates.
//   * The write-back is guarded by a compare (0x180004575) that makes it a
//     no-op when nothing changed. Kept, because it is the difference between
//     touching and not touching the Configuration singleton.
// ============================================================
int ResolveMultisampleMode(DeviceData* self) {
    ::Video::Device* dev = self->_owner;
    ::Video::Configuration* cfg = ConfigOf();

    int index = cfg->multisample;
    if (index >= dev->GetMultisampleModeCount())
        index = dev->GetMultisampleModeCount() - 1;

    while (!dev->IsMultisampleModeSupported(index))
        --index;

    if (cfg->multisample != index)
        cfg->multisample = index;

    return index + 1;
}

// ============================================================
// 0x180004590 — FillSwapChainDesc(DXGI_SWAP_CHAIN_DESC*)
//
// The whole fullscreen story is these 72 bytes. Field by field, with the store
// that proves each:
//
//   BufferDesc.Width / .Height        Device+0x04 / +0x08          0x1800045f9 / 602
//   BufferDesc.RefreshRate            mode.rates[Device+0x10]      0x180004615 / 624
//   BufferDesc.Format                 0x57 = B8G8R8A8_UNORM        0x180004627
//   BufferDesc.ScanlineOrdering       0  (upper half of that qword store)
//   BufferDesc.Scaling                0                            0x180004631
//   SampleDesc                        {1, 0}                       0x180004634 (one qword)
//   BufferUsage                       0x20 = RENDER_TARGET_OUTPUT  0x18000463c
//   BufferCount                       2                            0x180004643
//   OutputWindow                      Device+0x90                  0x180004655
//   Windowed                          Configuration+0x40 == 0      0x18000465b-661
//   SwapEffect                        0 = DISCARD                  0x180004664
//   Flags                             2 = ALLOW_MODE_SWITCH        0x180004667
//
// The swap chain is single-sampled on purpose: MSAA lives on the +0x38 colour
// texture, which EndCommands resolves into the back buffer.
//
// The DisplayMode temporary is fetched for its refresh-rate table alone; the
// width/height in it are ignored here (Device+0x04/+0x08 already hold the
// post-AdjustDisplay client size).
// ============================================================
void FillSwapChainDesc(DeviceData* self, DXGI_SWAP_CHAIN_DESC* desc) {
    ::Video::Device* dev = self->_owner;
    ::Video::Configuration* cfg = ConfigOf();

    // ENDGAME B.3.5 block J, RULE B5-12 / CALLER_BATCH_BRIEF "by-value returns":
    // the door body moved to the call site. `Video::Device::GetDisplayMode` returns
    // the mode BY VALUE; retail's caller hands it a pointer to this very stack slot
    // and the door placement-constructs into it. Written as ONE placement-new of
    // the prvalue into that same uninitialised storage, which is what the door did,
    // and no copy (C++17 guarantees the prvalue is built in place).
    DisplayMode mode;
    ::new (static_cast<void*>(&mode))
        DisplayMode(dev->GetDisplayMode(dev->displayModeIndex));

    std::memset(desc, 0, sizeof(*desc));

    desc->BufferDesc.Width  = static_cast<UINT>(dev->backBufferWidth);
    desc->BufferDesc.Height = static_cast<UINT>(dev->backBufferHeight);

    // ENDGAME B.3.5: `mode` is the real `Video::Device::DisplayMode` now, so its
    // refresh array is already typed and the cast is gone.
    // Review F-171: RefreshRateAt bounds the index to this mode's list (refresh_rate_at.h).
    const RefreshRate rate = Video::RefreshRateAt(mode, dev->refreshRateIndex);
    desc->BufferDesc.RefreshRate.Numerator   = static_cast<UINT>(rate.numerator);
    desc->BufferDesc.RefreshRate.Denominator = static_cast<UINT>(rate.denominator);

    desc->BufferDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;   // 0x57
    desc->BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    desc->BufferDesc.Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;

    desc->SampleDesc.Count   = 1;
    desc->SampleDesc.Quality = 0;

    desc->BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;             // 0x20
    desc->BufferCount  = 2;
    desc->OutputWindow = WindowOf(dev);
    desc->Windowed     = (cfg->fullscreen == 0) ? TRUE : FALSE;
    // EXT SEAM (NOT RETAIL, src/ext/fullscreen_mode/fullscreen_mode.h): borderless is a windowed swap chain
    // in the fullscreen popup AdjustDisplay makes.
    if (Ext::FullscreenMode::Borderless()) desc->Windowed = TRUE;
    desc->SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;
    desc->Flags        = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;      // 2

    Video_DisplayMode_Dtor(&mode);
}

// ============================================================
// 0x1800052b0 / 0x180005350 — CreateColorBuffer / CreateDepthBuffer
//
// Identical except for three constants, so they share one body here exactly as
// the shipped code shares one shape. Both release-and-null the previous texture
// first, size themselves from Device+0x04/+0x08, and take their sample count
// from ResolveMultisampleMode.
//
//   colour  field +0x38  format 0x57 (B8G8R8A8_UNORM)   bind 0x20 (RENDER_TARGET)
//   depth   field +0x30  format 0x2d (D24_UNORM_S8_UINT) bind 0x40 (DEPTH_STENCIL)
//
// Neither checks the HRESULT beyond "== 0"; both return that as a bool. Note
// they are MULTISAMPLED while the swap chain is not — that is the whole reason
// EndCommands does a ResolveSubresource.
// ============================================================
bool CreateAttachment(DeviceData* self, ID3D11Texture2D** slot, DXGI_FORMAT format,
                      UINT bindFlags) {
    Dx11Release(*slot);
    *slot = nullptr;

    ::Video::Device* dev = self->_owner;

    D3D11_TEXTURE2D_DESC desc;
    desc.Width              = static_cast<UINT>(dev->backBufferWidth);
    desc.Height             = static_cast<UINT>(dev->backBufferHeight);
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = format;
    desc.SampleDesc.Count   = static_cast<UINT>(ResolveMultisampleMode(self));
    desc.SampleDesc.Quality = 0;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    desc.BindFlags          = bindFlags;
    desc.CPUAccessFlags     = 0;
    desc.MiscFlags          = 0;

    ID3D11Device* d3d = self->d3dDevice;
    HRESULT hr = d3d->CreateTexture2D(&desc, nullptr, slot);
    return hr == 0;
}

bool CreateColorBuffer(DeviceData* self) {
    return CreateAttachment(self, &self->colorBuffer, DXGI_FORMAT_B8G8R8A8_UNORM,
                            D3D11_BIND_RENDER_TARGET);
}
bool CreateDepthBuffer(DeviceData* self) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOCORE.md): ext.ambient_occlusion (AO.md §6.1): while
    // the extension asks for it, the main depth is created typeless and
    // shader-readable (AttachDepthBuffer gives it explicit views). The format is
    // fixed per build: a toggle reaches here through the rebuild path
    // (ao_backend.h RequestReadableDepth -> Device::settingsChanged).
    if (AoBackend::BuildReadableDepth())
        return CreateAttachment(self, &self->depthBuffer, DXGI_FORMAT_R24G8_TYPELESS,
                                D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
    return CreateAttachment(self, &self->depthBuffer, DXGI_FORMAT_D24_UNORM_S8_UINT,
                            D3D11_BIND_DEPTH_STENCIL);
}

// ============================================================
// Window-collapse hunt (temporary diagnostic)
//
// The window collapses to 136x39 about 1.6s into a run. 136x39 is exactly
// SM_CXMIN x SM_CYMIN on this desktop — the OS *minimum tracking size*, i.e.
// what Windows clamps a request to when a caller asks for a smaller (or 0x0)
// window. It is NOT the metrics of a minimized frame (SM_CXMINIMIZED is
// 160x28), and the WM_SIZE that follows carries type=0 (SIZE_RESTORED), so the
// window is never iconic. Something asks for a degenerate size and the OS
// rounds it up.
//
// CAUSE (proven 2026-07-24): PowerToys FancyZones, an EXTERNAL process. It is
// not a bug in this port or in the engine. FancyZones has no app exclusions
// configured and has openWindowOnActiveMonitor/appLastZone_moveWindows set, so
// it snaps the freshly created game window into a zone on another monitor. The
// cross-process SetWindowPos carries SWP_ASYNCWINDOWPOS, which is why it is
// invisible to every in-process hook and why the request is *posted*: it lands
// only when the main thread resumes pumping after the sync loader wait, making
// it look like the loader caused it. Adding OpenBA.exe to
// fancyzones_excluded_apps takes the collapse from 240 occurrences to 0 with no
// code change, and DeviceData_Resize is then never called at all.
//
// Everything on our side is downstream of that. This code and
// Video::Device::AdjustDisplay only *react*: AdjustDisplay unconditionally
// re-reads GetClientRect into Device+0x04/+0x08 and Configuration+0x1c/+0x20,
// so once an external actor has collapsed the window the bad size latches and
// each frame re-applies it. That latch is 100% original Runtime code, so retail
// Banished is vulnerable to exactly the same thing.
//
// The trace below is kept because it cleanly separates "someone else collapsed
// the window" from "we collapsed it": if entry already reports a degenerate
// client rect, the caller is external. Bounded to the first few resizes —
// once the swap chain is wedged BeginCommands retries every frame.
// ============================================================
namespace {
int g_resizeTraceCalls = 0;
constexpr int kResizeTraceMax = 6;

inline bool ResizeTraceOn() { return g_resizeTraceCalls <= kResizeTraceMax; }

// ------------------------------------------------------------------
// Degenerate-size guard (deliberate deviation from retail — see below)
//
// Retail latches whatever GetClientRect returns, with no floor. That was safe
// in 2013: nothing on a normal desktop reshaped a window behind the app's back.
// It is not safe now — FancyZones (2019) and every tiling/zone manager since do
// exactly that, and one collapsed frame poisons Device+0x04/+0x08 and
// Configuration+0x1c/+0x20 for the rest of the run, then gets persisted.
//
// The guard rejects impossible values rather than overriding real ones: no
// window this small can be a render target, and no user ever chose one. 64
// matches the sentinel repair in src/app/main.cpp (HarnessGuardUnattendedBoot).
//
// Kill-switch for A/B: BANISHED_WINDOW_GUARD=0 restores verbatim retail
// behaviour (window collapses and stays collapsed).
// ------------------------------------------------------------------
constexpr int kMinSaneWindowDim = 64;

inline bool DegenerateSize(int w, int h) {
    return w < kMinSaneWindowDim || h < kMinSaneWindowDim;
}

// Last size that could plausibly be a render target. The guard restores this
// when it catches a collapse, so it must only ever hold a sane pair.
int g_lastGoodW = 0;
int g_lastGoodH = 0;

inline void RememberGoodSize(int w, int h) {
    if (!DegenerateSize(w, h)) { g_lastGoodW = w; g_lastGoodH = h; }
}

// Best available sane size, in descending order of trustworthiness: what we
// last saw working, then the configured window override, then the display mode
// (which is always sane because it comes from the adapter's mode table).
void PickGoodSize(const ::Video::Configuration* cfg, const DisplayMode& mode, int* w, int* h) {
    if (!DegenerateSize(g_lastGoodW, g_lastGoodH)) {
        *w = g_lastGoodW; *h = g_lastGoodH; return;
    }
    const int cw = cfg->windowWidth;
    const int ch = cfg->windowHeight;
    if (!DegenerateSize(cw, ch)) { *w = cw; *h = ch; return; }
    *w = mode.width; *h = mode.height;
}

void TraceGeom(const char* stage, const ::Video::Device* dev) {
    if (!ResizeTraceOn()) return;
    void* hwnd = dev->windowHandle;
    ::Video::Configuration* cfg = ConfigOf();
    Platform::Rect wr{}, cr{};
    Platform::Window::GetWindowRect(hwnd, &wr);
    Platform::Window::GetClientRect(hwnd, &cr);
    fprintf(stderr,
            "[ResizeTrace #%d] %-18s win=(%d,%d %dx%d) client=%dx%d "
            "dev=%dx%d cfgWin=%dx%d vis=%d tid=%u\n",
            g_resizeTraceCalls, stage,
            wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
            cr.right - cr.left, cr.bottom - cr.top,
            dev->backBufferWidth, dev->backBufferHeight,
            cfg->windowWidth,
            cfg->windowHeight,
            Platform::Window::IsVisible(hwnd) ? 1 : 0, Platform::CurrentThreadId());
    fflush(stderr);
}

// Which monitor is the swap chain bound to? The collapsed window lands at
// (-2456,104), which is (104,104) on DISPLAY3 (desktop x=-2560). If the
// containing output is that monitor, a DXGI-side reposition explains both the
// move and the resize.
// ============================================================================
// DxgiWideText -- a DXGI WCHAR text as the 2-byte units System::WideString holds
// (Phase D.4 LINUX-1, plans/RENDER.md C.4: the ABI audit's wchar_t line).
// ============================================================================
// DXGI_ADAPTER_DESC1::Description (WCHAR[128]) and DXGI_OUTPUT_DESC::DeviceName (WCHAR[32])
// feed System::WideString, retail's UTF-16 type (char16_t). WCHAR is 2 bytes and get() is the
// reinterpret_cast it always was: the same pointer, nothing copied. (The 4 -> 2 narrowing the
// Linux build needed went with that build's dx11 route in Vulkan P5.)
template <size_t N>
class DxgiWideText {
public:
    explicit DxgiWideText(const WCHAR (&text)[N])
        : p_(reinterpret_cast<const char16_t*>(text)) {}  // CAST-OK: win32
    const char16_t* get() const { return p_; }

private:
    const char16_t* p_;
};

void TraceContainingOutput(IDXGISwapChain* sc) {
    static bool done = false;
    if (done) return;
    done = true;
    IDXGIOutput* out = nullptr;
    if (SUCCEEDED(sc->GetContainingOutput(&out)) && out) {
        DXGI_OUTPUT_DESC od{};
        if (SUCCEEDED(out->GetDesc(&od))) {
            fprintf(stderr, "[ResizeTrace] swapchain containing output '%ls' "
                    "desktop=(%ld,%ld)-(%ld,%ld) attached=%d\n",
                    od.DeviceName,
                    // (long): LONG's printf spelling
                    (long)od.DesktopCoordinates.left, (long)od.DesktopCoordinates.top,
                    (long)od.DesktopCoordinates.right, (long)od.DesktopCoordinates.bottom,
                    od.AttachedToDesktop ? 1 : 0);
            fflush(stderr);
        }
        out->Release();
    }
}
}  // namespace

// ============================================================
// 0x180004340 — the swap-chain resize / recreate helper
//
// Not a vtable slot: a plain function called from the WM_ACTIVATE handler
// (twice, one call per transition direction) and directly by
// ContextData::BeginCommands when Device+0x9c is set. Both callers run this
// body, which is a transcription of the shipped one.
//
// Sequence, with the early-out first:
//
//   Device+0x9c = 1                       <- set BEFORE the visibility test
//   if (!IsWindowVisible(Device+0x90)) return 0;   <- Device+0x9c STAYS SET
//   context->OMSetRenderTargets(0, NULL, NULL)
//   context->ClearState()
//   Video::Device::ReleaseDynamic(device)
//   Release depth (+0x30), Release colour (+0x38), null both
//   GetDisplayMode(device, &mode, Device+0x0c)
//   if (Configuration+0x40 && !DeviceData+0x40)   <- entering/staying fullscreen
//       Device+0x04/+0x08 = mode.width/height
//   Video::Device::AdjustDisplay(device)          <- moves/resizes the HWND and,
//                                                    in windowed mode, rewrites
//                                                    Device+0x04/+0x08 from
//                                                    GetClientRect
//   swapChain->ResizeTarget({Device+0x04, Device+0x08, mode.rates[Device+0x10],
//                            format 28, 0, 0})
//   swapChain->ResizeBuffers(1, Device+0x04, Device+0x08, 0x57, 2)
//   swapChain->SetFullscreenState(Configuration+0x40 != 0, NULL)
//   CreateColorBuffer() && CreateDepthBuffer()
//   Video::Device::RecreateDynamic(device)
//   Device+0x9c = 0
//   return 1
//
// Any failure returns 0 and LEAVES Device+0x9c set, which is what makes
// BeginCommands retry the whole thing on the next frame.
//
// The `format 28` in the ResizeTarget mode desc is DXGI_FORMAT_R8G8B8A8_UNORM,
// not the B8G8R8A8_UNORM (0x57) the swap chain actually uses. That mismatch is
// in the shipped code (`MOV dword ptr [RSP+0x60],0x1c` at 0x18000441d) and is
// reproduced verbatim — see the quirks list.
// ============================================================

int DeviceData_Resize(DeviceData* self) {
    ::Video::Device* dev = self->_owner;
    ++g_resizeTraceCalls;
    TraceGeom("entry", dev);
    dev->settingsChanged = 1;

    // NOT RETAIL: while the boot reveal is still pending (boot_window.h) the window has
    // never been shown, its client rect is valid, and the only way it gets shown is a
    // Draw -- which BeginCommands refuses while this flag is set. Early-outing here
    // deadlocked the boot the first time a reset was pending before the first frame
    // (ext.ambient_occlusion on in settings.cfg: the readable-depth rebuild, 2026-10-14:
    // no window in any flavour). The reset runs on the not-yet-revealed window instead.
    if (!Platform::Window::IsVisible(dev->windowHandle) && !BootWindow::IsPending())
        return 0;

    // NOT RETAIL: a minimized window has a 0x0 client rect, and IsWindowVisible()
    // is still TRUE for it — so retail (0x180004340 tests IsWindowVisible only)
    // cheerfully latches that 0x0 the first time the player minimizes. Treat it
    // like the not-visible case: leave kResetPending set and rebuild when the
    // window comes back. Doing this before the guard below also stops us fighting
    // a deliberate minimize.
    if (Platform::Window::IsIconic(dev->windowHandle)) {
        // Review S-052: retail's reset ends in SetFullscreenState(cfg->fullscreen)
        // (0x1800044af), which is how a focus loss (OnActivate: cfg->fullscreen = 0)
        // leaves exclusive mode. The rebuild waits for the restore; leaving exclusive
        // mode does not. The restore's reset sets it again from cfg->fullscreen.
        BOOL exclusive = FALSE;
        IDXGISwapChain* const sc = self->swapChain;
        if (ConfigOf()->fullscreen == 0 && sc != nullptr &&
            sc->GetFullscreenState(&exclusive, nullptr) == 0 && exclusive)
            sc->SetFullscreenState(FALSE, nullptr);
        return 0;
    }

    ID3D11DeviceContext* ctx = self->d3dContext;
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->ClearState();

    // ClearState() has just unbound every constant buffer. ConstantBufferData
    // caches which rung it has bound and would otherwise never re-bind, leaving
    // every shader reading a null constant buffer — a permanently black window
    // after any resize. Drop those caches here, where the bindings actually
    // died. Deliberate deviation from the shipped DLL; see the note above
    // Dx11InvalidateConstantBufferBindings in buffers.cpp.
    Dx11InvalidateConstantBufferBindings();
    Dx11NoteClearState();   // topology cache in ContextData, same reason

    dev->ReleaseDynamic();

    Dx11Release(self->depthBuffer);
    Dx11Release(self->colorBuffer);
    self->depthBuffer = nullptr;
    self->colorBuffer = nullptr;

    ::Video::Configuration* cfg = ConfigOf();

    // The same by-value shape as FillSwapChainDesc's — see the note there.
    DisplayMode mode;
    ::new (static_cast<void*>(&mode))
        DisplayMode(dev->GetDisplayMode(dev->displayModeIndex));

    // EXT SEAM (NOT RETAIL, src/ext/fullscreen_mode/fullscreen_mode.h): borderless leaves an exclusive
    // swap chain BEFORE AdjustDisplay places the popup (leaving exclusive mode restores the desktop's
    // mode and the window DXGI took over; retail's SetFullscreenState below comes after the resize), and
    // takes the monitor's size, not the display mode's: the desktop is never switched.
    const bool borderless = Ext::FullscreenMode::Borderless();
    if (borderless) {
        BOOL exclusive = FALSE;
        if (self->swapChain != nullptr &&
            self->swapChain->GetFullscreenState(&exclusive, nullptr) == 0 && exclusive)
            self->swapChain->SetFullscreenState(FALSE, nullptr);
    }

    if (cfg->fullscreen != 0 && self->wasFullscreen == 0) {
        dev->backBufferWidth  = mode.width;
        dev->backBufferHeight = mode.height;
        if (borderless)
            Ext::FullscreenMode::DesktopSize(dev->monitor, &dev->backBufferWidth, &dev->backBufferHeight);
    }

    // AdjustDisplay's windowed branch re-reads GetClientRect and writes the
    // result into BOTH Device+0x04/+0x08 and Video::Configuration+0x1c/+0x20,
    // with no floor. Left alone, one collapsed frame latches for the whole run
    // and then gets persisted. The guard brackets that call.
    //
    // Guard, first half. Device+0x04/+0x08 arrives here from the WM_SIZE path,
    // so an external collapse is already sitting in it before AdjustDisplay
    // runs. Replacing it with a sane size now means AdjustDisplay's SetWindowPos
    // pushes the window back open instead of nailing it shut, and the
    // GetClientRect it latches afterwards is the good value.
    if (DegenerateSize(dev->backBufferWidth,
                       dev->backBufferHeight)) {
        int gw = 0, gh = 0;
        PickGoodSize(cfg, mode, &gw, &gh);
        static bool reported = false;
        if (!reported) {
            reported = true;
            fprintf(stderr, "[WindowGuard] refusing collapsed size %dx%d -> "
                    "restoring %dx%d (an external window manager reshaped the "
                    "window; see the note above DeviceData_Resize)\n",
                    dev->backBufferWidth,
                    dev->backBufferHeight, gw, gh);
            fflush(stderr);
        }
        dev->backBufferWidth  = gw;
        dev->backBufferHeight = gh;
    }

    TraceGeom("pre-AdjustDisplay", dev);
    dev->AdjustDisplay();
    TraceGeom("post-AdjustDisplay", dev);

    // Guard, second half. If the external actor re-collapsed the window inside
    // that call, AdjustDisplay has just latched the bad size into both the
    // device and the configuration. Scrub both: the swap chain must never be
    // built at a zero dimension (CreateColorBuffer fails with "[0x0 4xAA]" and
    // Engine::Startup returns 0), and Configuration must never carry a value
    // that Video::Configuration::Save would persist. The window itself may stay
    // collapsed — we do not keep fighting for it — but the renderer survives
    // and the settings stay clean.
    {
        int gw = 0, gh = 0;
        PickGoodSize(cfg, mode, &gw, &gh);
        if (DegenerateSize(dev->backBufferWidth,
                           dev->backBufferHeight)) {
            dev->backBufferWidth  = gw;
            dev->backBufferHeight = gh;
        }
        if (DegenerateSize(cfg->windowWidth,
                           cfg->windowHeight)) {
            cfg->windowWidth  = gw;
            cfg->windowHeight = gh;
        }
    }

    // Whatever we ended up with, if it is sane it becomes the restore point.
    RememberGoodSize(dev->backBufferWidth,
                     dev->backBufferHeight);

    // Review F-171: RefreshRateAt bounds the index to this mode's list (refresh_rate_at.h).
    const RefreshRate rate = Video::RefreshRateAt(mode, dev->refreshRateIndex);

    DXGI_MODE_DESC target;
    target.Width                   = static_cast<UINT>(dev->backBufferWidth);
    target.Height                  = static_cast<UINT>(dev->backBufferHeight);
    target.RefreshRate.Numerator   = static_cast<UINT>(rate.numerator);
    target.RefreshRate.Denominator = static_cast<UINT>(rate.denominator);
    target.Format                  = static_cast<DXGI_FORMAT>(28);  // sic — see quirks
    target.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    target.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;

    IDXGISwapChain* sc = self->swapChain;

    bool ok = false;

    // ResizeTarget is called first and unconditionally in the shipped order, so
    // hoisting it out of the short-circuit chain preserves semantics exactly.
    TraceContainingOutput(sc);
    TraceGeom("pre-ResizeTarget", dev);
    const HRESULT hrTarget = sc->ResizeTarget(&target);
    TraceGeom("post-ResizeTarget", dev);
    if (ResizeTraceOn()) {
        fprintf(stderr, "[ResizeTrace #%d] ResizeTarget(%ux%u @ %u/%u fmt=%u) "
                "-> 0x%08lX\n", g_resizeTraceCalls, target.Width, target.Height,
                target.RefreshRate.Numerator, target.RefreshRate.Denominator,
                static_cast<unsigned>(target.Format),
                static_cast<unsigned long>(hrTarget));
        fflush(stderr);
    }

    if (hrTarget == 0 &&
        sc->ResizeBuffers(1,
                          static_cast<UINT>(dev->backBufferWidth),
                          static_cast<UINT>(dev->backBufferHeight),
                          DXGI_FORMAT_B8G8R8A8_UNORM,
                          DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) == 0 &&
        sc->SetFullscreenState(cfg->fullscreen != 0 && !borderless ? TRUE : FALSE,
                               nullptr) == 0 &&
        CreateColorBuffer(self) &&
        CreateDepthBuffer(self)) {
        dev->RecreateDynamic();
        dev->settingsChanged = 0;
        ok = true;
    }

    Video_DisplayMode_Dtor(&mode);
    return ok ? 1 : 0;
}

// ============================================================
// 0x1800042d0 — WM_ACTIVATE callback (registered as message id 6)
//
// The fullscreen save/restore across focus changes. Registered by slot 2 and
// unregistered by slot 3.
//
//   cfg = Video::Configuration::_singleton
//   dd  = Video::Device::_singleton->[0x100]        (the DeviceData)
//   if (cfg->fullscreen == 0 && dd->wasFullscreen == 0) return 1;   // nothing to do
//   if (LOWORD(wParam) == WA_INACTIVE) {
//       dd->wasFullscreen = 1;    cfg->fullscreen = 0;    Resize(dd);
//   } else if (dd->wasFullscreen) {
//       dd->wasFullscreen = 0;    cfg->fullscreen = 1;    Resize(dd);
//   }
//   return 1;
//
// So DeviceData+0x40 is not "is fullscreen" — it is "was fullscreen, restore it
// when focus comes back". Naming it `wasFullscreen` (which the type header
// already does) is exactly right.
//
// Note the outer guard: when the game is configured windowed AND has never been
// fullscreen, both flags are zero and this handler does nothing at all. It
// cannot resize a window that was never fullscreen.
//
// The callback signature comes from the Runtime export it is handed to:
//   ?RegisterMessageCallback@Application@1@QEAAXIP6AHPEAUHWND__@@I_K_J@Z@Z
//     -> void RegisterMessageCallback(unsigned, int (*)(HWND, unsigned,
//                                                       unsigned __int64, __int64))
// B5 DEV-A: declared exactly as `Application::MessageCallback` spells it (the
// HWND is that header's `void*`), so it registers without a cast.
// The original reads only the low word of wParam (`TEST R8W,R8W`).
// ============================================================
int DeviceData_OnActivate(void* /*hwnd*/, uint32_t /*msg*/,
                          uint64_t wParam, int64_t /*lParam*/) {
    ::Video::Configuration* cfg = ConfigOf();
    // Video::Device::_singleton->deviceData (+0x100), the class static (B.3.9 VID).
    DeviceData* dd = Dx11DeviceData();

    if (cfg->fullscreen == 0 && dd->wasFullscreen == 0)
        return 1;

    // EXT SEAM (NOT RETAIL, src/ext/fullscreen_mode/fullscreen_mode.h): a borderless window has no
    // exclusive mode to lose; it stays as it is when another window takes the focus.
    if (dd->wasFullscreen == 0 && Ext::FullscreenMode::Borderless())
        return 1;

    if (static_cast<unsigned short>(wParam) == 0) {      // WA_INACTIVE
        dd->wasFullscreen = 1;
        cfg->fullscreen = 0;
        DeviceData_Resize(dd);
        return 1;
    }

    if (dd->wasFullscreen != 0) {
        dd->wasFullscreen = 0;
        cfg->fullscreen = 1;
        DeviceData_Resize(dd);
    }
    return 1;
}

// ============================================================
// 0x1800053f0 — WM_SETCURSOR callback (registered as message id 0x20)
//
// Three instructions of substance: re-apply the cursor slot 8 built, and force
// it visible. Reads the DeviceData through Video::Device::_singleton rather
// than capturing it, because a message callback gets no user pointer.
// ============================================================
int DeviceData_OnSetCursor(void* /*hwnd*/, uint32_t /*msg*/,
                           uint64_t /*w*/, int64_t /*l*/) {
    // Video::Device::_singleton->deviceData (+0x100), the class static (B.3.9 VID).
    DeviceData* dd = Dx11DeviceData();

    Platform::Cursor::Set(dd->cursor);
    Platform::Cursor::Show(true);
    return 1;
}

}  // namespace

// ============================================================
// B5 DEV-A: the constructor — CreateDeviceData's (0x1800011a0) stores after
// MemAlloc(0x70, 4) + ??0DeviceData(device) + the vptr store, nothing more
// (B5_RULES section 5: no zeroing braces). The memory is the base's class-scope
// operator new, i.e. retail's MemAlloc(0x70, 4) (exports.cpp).
//
//   +0x10..+0x38  six qwords = 0
//   +0x40         a single BYTE 0 (`MOV byte [RBX+0x40],AL`): pad41 untouched
//   +0x48/+0x50   0
//   +0x58         ONE qword -1 (`MOV qword [RBX+0x58],-1`): both hotspots -1
//   +0x60..+0x6c  0
// ============================================================
DeviceData::DeviceData(::Video::Device* owner)
    : ::Video::DeviceData(owner),
      unk10(nullptr), swapChain(nullptr), d3dDevice(nullptr), d3dContext(nullptr),
      depthBuffer(nullptr), colorBuffer(nullptr),
      wasFullscreen(0),                 // byte only — pad41 untouched, as shipped
      cursor(nullptr), cursorImage(nullptr),
      cursorHotspotX(-1), cursorHotspotY(-1),   // the qword -1 at +0x58 covers both
      trackedImages(nullptr), trackedCount(0), trackedCapacity(0) {}

// ============================================================
// Slot 0 — 0x180004270  scalar deleting destructor
//
//   this->vptr = DeviceData vtable
//   MemFree(this->trackedImages)
//   this->trackedImages = NULL; this->trackedCount = this->trackedCapacity = 0
//   Video::DeviceData::~DeviceData(this)
//   if (flags & 1) MemFree(this)
//   return this
//
// The count/capacity clear is a SINGLE 8-byte store at +0x68 (0x1800042a7), so
// it zeroes both halves; writing them separately is equivalent and clearer.
//
// B5 DEV-A: the vptr re-seat, the base-destructor chain and the conditional
// MemFree are the compiler's (its D0 + the base's class-scope `operator delete`,
// src/engine/video/companion_operators.cpp); the body is the free and the
// three stores, in retail order.
//
// Note what is NOT freed here: the swap chain, device, context and buffers are
// slot 3's job, and the cursor is destroyed by slot 3 too. Calling slot 0
// without slot 3 leaks every COM object. That is the shipped contract.
// ============================================================
DeviceData::~DeviceData() {
    System::MemoryAllocator::MemFree(trackedImages);
    trackedImages   = nullptr;
    trackedCount    = 0;
    trackedCapacity = 0;
}

// ============================================================
// Slot 3 — 0x1800051c0  shutdown
//
//   UnregisterMessageCallback(app, 0x20)      // WM_SETCURSOR
//   UnregisterMessageCallback(app, 6)         // WM_ACTIVATE
//   if (cursor) DestroyCursor(cursor)
//   if (this->wasFullscreen) Configuration->fullscreen = true
//   Release(depth +0x30);  depth = NULL
//   Release(colour +0x38); colour = NULL
//   Release(context +0x28); context = NULL
//   if (swapChain) {
//       if (Configuration->fullscreen) swapChain->SetFullscreenState(FALSE, NULL)
//       Release(swapChain)
//   }
//   swapChain = NULL
//   Release(device +0x20); device = NULL
//
// Three things about this function are deliberate and reproduced:
//
//   * The wasFullscreen -> Configuration write happens FIRST, and the
//     SetFullscreenState guard then reads the value it just wrote. So quitting
//     while alt-tabbed out of fullscreen still restores VideoFullscreen=true in
//     the saved settings AND still leaves DXGI exclusive mode properly.
//   * +0x10 (the IDXGIFactory1) is NOT released here. Slot 2 already released
//     it as soon as the swap chain existed (0x1800050c2) and never nulled the
//     field, so DeviceData+0x10 is a dangling pointer for the whole session.
//     Releasing it here would be a double-release.
//   * +0x48 (the HCURSOR) is not nulled after DestroyCursor, so it too dangles.
//     Slot 8's identity check would then compare against a stale handle. Left
//     as-is; "fixing" it changes observable behaviour. (SDL step W5: every caller
//     of this slot deletes the DeviceData right after it, so slot 8 never meets
//     the stale handle; SDL ignores a destroyed cursor as Win32 does,
//     src/platform/sdl/cursor_sdl.cpp.)
// ============================================================
void DeviceData::Shutdown() {
    // NOT RETAIL (ext.async_gpu_create): no create is left running against the device going away.
    AsyncCreate::Drain();
    // NOT RETAIL (ext.create_on_demand): no first-use create runs against the device going away (the
    // rest wait for the next device).
    CreateOnDemand::Pause();
    // NOT RETAIL (ext.share_gpu_data): nothing made on this device is handed out again; the table's
    // references go (exit, both device-lost recoveries and a device rebuild all come through here).
    ShareGpuData::Clear();
#if BANISHED_TWEAK_OVERLAY
    // The debug tweak overlay releases its DX11 objects and its device / context references before
    // the device goes (device lost, a settings rebuild, exit); it re-inits on the next open frame.
    Diag::TweakOverlay::OnDeviceShutdown();
#endif
    ::Application::Application* app = AppOf();
    app->UnregisterMessageCallback(Platform::Window::kMsgSetCursor);   // 0x20
    app->UnregisterMessageCallback(Platform::Window::kMsgActivate);    // 6

    if (cursor)
        Platform::Cursor::Destroy(cursor);

    ::Video::Configuration* cfg = ConfigOf();
    if (wasFullscreen != 0)
        cfg->fullscreen = 1;

    Dx11Release(depthBuffer);
    depthBuffer = nullptr;

    Dx11Release(colorBuffer);
    colorBuffer = nullptr;

    Dx11Release(d3dContext);
    d3dContext = nullptr;

    if (swapChain) {
        IDXGISwapChain* sc = swapChain;
        if (cfg->fullscreen != 0)
            sc->SetFullscreenState(FALSE, nullptr);
        sc->Release();
    }
    swapChain = nullptr;

    Dx11Release(d3dDevice);
    d3dDevice = nullptr;
}

// ============================================================
// Slot 2 — 0x180004e10  create device + swap chain + buffers
//
// The riskiest function in the DLL: get it wrong and there is no window. Every
// step below is in the shipped order, with the shipped error strings (they are
// what the user sees when startup fails, so they are observable).
//
//  1. CreateDXGIFactory1(IID_IDXGIFactory1, &this->+0x10)
//  2. factory->EnumAdapters1(Device+0x14, &adapter)
//  3. adapter->EnumOutputs(Device+0x18, &output)
//  4. Device+0x04/+0x08 = modeTable[Device+0x0c].{width,height}
//  5. if (!Configuration->fullscreen) {
//         if (Configuration->windowWidth  >= 0) Device+0x04 = it
//         if (Configuration->windowHeight >= 0) Device+0x08 = it
//     }
//  6. if (!Configuration+0x42) ShowWindow(Device+0x90, SW_SHOWDEFAULT)
//  7. output->GetDesc(&od);  Device+0xa0 = od.Monitor
//  8. Video::Device::AdjustDisplay(device)
//  9. FillSwapChainDesc(&desc)
// 10. D3D11CreateDevice(adapter, UNKNOWN, NULL, 0, {11_0,10_1,10_0}, 3,
//                       D3D11_SDK_VERSION, &this->+0x20, &fl, &this->+0x28)
// 11. CheckFormatSupport(B8G8R8A8_UNORM) must have RENDER_TARGET (0x4000)
// 12. CheckFormatSupport(D24_UNORM_S8_UINT) must have DEPTH_STENCIL (0x10000)
// 13. factory->MakeWindowAssociation(Device+0x90, DXGI_MWA_NO_WINDOW_CHANGES)
// 14. factory->CreateSwapChain(d3dDevice, &desc, &this->+0x18)
// 15. Release adapter, output, factory
// 16. CreateColorBuffer() then CreateDepthBuffer()
// 17. RegisterMessageCallback(0x20, OnSetCursor)
//     RegisterMessageCallback(6,    OnActivate)
//
// Step 5 is the "-1 means don't override" sentinel: the test is a SIGN test
// (`TEST ECX,ECX; JS skip` at 0x180004f12 and 0x180004f21), and
// Video::Configuration's constructor really does default both fields to -1
// (Runtime 0x1800f6f10 stores 0xffffffffffffffff at +0x1c). Confirmed on both
// sides, not inferred from one.
//
// Step 8 is why the swap chain and the window always agree at creation time:
// AdjustDisplay physically SetWindowPos'es the HWND and then, in windowed mode,
// re-reads Device+0x04/+0x08 from GetClientRect. FillSwapChainDesc runs after
// it, so it sees the settled size.
//
// Failure handling, reproduced exactly:
//   * Only DXGI_ERROR_NOT_FOUND (0x887a0002) is treated as an enumeration
//     failure. Any OTHER non-zero HRESULT from EnumAdapters1/EnumOutputs falls
//     through and the code proceeds with whatever the out-pointer holds.
//   * Every failure path returns false WITHOUT releasing anything already
//     created. The factory, adapter, output, device and context all leak on a
//     failed startup. Reproduced: adding cleanup would change what a
//     post-failure DumpVideoState() sees.
// ============================================================
bool DeviceData::CreateDevice() {
    CreateDXGIFactory1_t createFactory = GetCreateDXGIFactory1();
    D3D11CreateDevice_t  createDevice  = GetD3D11CreateDevice();
    // B5 DEV-A (B5_RULES D9): this arm forwarded to the shipped DLL's slot 2
    // ("the import failed, run the original"). There is no original: the DLL is
    // never mapped, so the forward returned null into a call. Failing to resolve
    // dxgi.dll / d3d11.dll's entry point fails device creation instead, and
    // Device::PlatformStartup's fallback loop takes it from there.
    if (!createFactory || !createDevice) return false;

    ::Video::Device* dev = _owner;
    StallDebug::Mark("  DX11 CreateDevice: entry");

    if (createFactory(kIID_IDXGIFactory1, &unk10) != 0) {
        System::Error::SetText("VideoDX11: Couldn't create a dxgi factory!", false);
        return false;
    }
    IDXGIFactory1* factory = unk10;

    IDXGIAdapter1* adapter = nullptr;
    if (factory->EnumAdapters1(static_cast<UINT>(dev->dxgiAdapterIndex),
                               &adapter) == DXGI_ERROR_NOT_FOUND) {
        System::Error::SetText("VideoDX11: Unable to query adapter for dx11 interface!", false);
        return false;
    }

    IDXGIOutput* output = nullptr;
    if (adapter->EnumOutputs(static_cast<UINT>(dev->dxgiOutputIndex),
                             &output) == DXGI_ERROR_NOT_FOUND) {
        System::Error::SetText("VideoDX11: Unable to query output for dx11 display!", false);
        return false;
    }

    StallDebug::Mark("  DX11 DXGI factory + adapter + output enum");

    ::Video::Configuration* cfg = ConfigOf();

    // 4 — start from the selected display mode.
    const DisplayMode* modeTable = dev->modeData;
    const int modeIndex = dev->displayModeIndex;
    dev->backBufferWidth  = modeTable[modeIndex].width;
    dev->backBufferHeight = modeTable[modeIndex].height;

    // 5 — windowed override, skipped for any negative value.
    if (cfg->fullscreen == 0) {
        const int ww = cfg->windowWidth;
        if (ww >= 0) dev->backBufferWidth = ww;
        const int wh = cfg->windowHeight;
        if (wh >= 0) dev->backBufferHeight = wh;
    }

    // Arm the deferred reveal BEFORE step 6 reads the flag. This does not
    // change step 6's logic at all — it sets Configuration+0x42, which the
    // shipped branch below already honours, so CreateDevice takes the engine's
    // own skip-show path and src/app/boot_window.cpp reveals the window once
    // the splash has a frame to show. Without this the window is on screen and
    // empty for ~450ms every boot; see boot_window.h for the measurements.
    // No-op (and step 6 shows the window inline, as shipped) when
    // BANISHED_DEFER_WINDOW_SHOW=0.
    BootWindow::ArmDeferredShow(cfg, dev->windowHandle);

    // 6 — SW_SHOWDEFAULT (10) honours the STARTUPINFO the process was launched
    // with, so the resulting show-state is a property of the launcher, not of
    // this code. See the Task B note in the lane report.
    if (cfg->field42 == 0) {
        Platform::Window::ShowDefault(dev->windowHandle);
        // From here the engine window is ON SCREEN. Everything between this
        // point and the first splash Present is a visible, unpainted window,
        // so this mark is the start of the boot blank-window budget.
        StallDebug::Mark("  DX11 ShowWindow  <== WINDOW NOW VISIBLE");
    }

    // 7 — remember which monitor the chosen output is on; AdjustDisplay uses it
    // to position a fullscreen window.
    DXGI_OUTPUT_DESC od;
    output->GetDesc(&od);
    dev->monitor = od.Monitor;

    // EXT SEAM (NOT RETAIL, src/ext/fullscreen_mode/fullscreen_mode.h): a borderless boot covers the output's
    // desktop rect, whatever display mode step 4 picked.
    if (Ext::FullscreenMode::Borderless()) {
        const RECT& dr = od.DesktopCoordinates;
        if (dr.right > dr.left && dr.bottom > dr.top) {
            dev->backBufferWidth  = dr.right - dr.left;
            dev->backBufferHeight = dr.bottom - dr.top;
        }
    }

    // 8 — settle the window, which may rewrite Device+0x04/+0x08.
    dev->AdjustDisplay();
    StallDebug::Mark("  DX11 AdjustDisplay (window sized)");

    // 9
    DXGI_SWAP_CHAIN_DESC desc;
    FillSwapChainDesc(this, &desc);

    // 10
    const D3D_FEATURE_LEVEL levels[3] = {
        D3D_FEATURE_LEVEL_11_0,   // 0xb000
        D3D_FEATURE_LEVEL_10_1,   // 0xa100
        D3D_FEATURE_LEVEL_10_0,   // 0xa000
    };
    D3D_FEATURE_LEVEL granted = static_cast<D3D_FEATURE_LEVEL>(0);
    // NOT RETAIL: BANISHED_D3D_DEBUG asks for the debug layer (retail passes 0); without the
    // SDK layers installed that create fails, and the retail call below runs as before.
    bool created = false;
    if (Opt().d3dDebug) {
        created = createDevice(adapter, static_cast<D3D_DRIVER_TYPE>(0), nullptr, D3D11_CREATE_DEVICE_DEBUG,
                               levels, 3, D3D11_SDK_VERSION, &d3dDevice, &granted, &d3dContext) == 0;
        D3dDebug::Attach(created ? d3dDevice : nullptr);
    }
    if (!created &&
        createDevice(adapter, static_cast<D3D_DRIVER_TYPE>(0), nullptr, 0,
                     levels, 3, D3D11_SDK_VERSION,
                     &d3dDevice, &granted, &d3dContext) != 0) {
        System::Error::SetText("VideoDX11: Could not initialize the DX11 device! "
                         "D3D_FEATURE_LEVEL_10_0 is required!", false);
        return false;
    }

    StallDebug::Mark("  DX11 D3D11CreateDevice");

    ID3D11Device* d3d = d3dDevice;

    // 11
    UINT support = 0;
    d3d->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support);
    if ((support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0) {
        System::Error::SetText("VideoDX11: Graphics hardware doesn't support "
                         "DXGI_FORMAT_B8G8R8A8_UNORM as a render target format!", false);
        return false;
    }

    // 12
    d3d->CheckFormatSupport(DXGI_FORMAT_D24_UNORM_S8_UINT, &support);
    if ((support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) == 0) {
        System::Error::SetText("VideoDX11: Graphics hardware doesn't support "
                         "DXGI_FORMAT_D24_UNORM_S8_UINT as a depth stencil format!", false);
        return false;
    }

    // 13
    factory->MakeWindowAssociation(WindowOf(dev),
                                   DXGI_MWA_NO_WINDOW_CHANGES);

    // 14
    if (factory->CreateSwapChain(d3d, &desc, &swapChain) != 0) {
        System::Error::SetText("VideoDX11: Could not initialize the swap chain!", false);
        return false;
    }

    // 15 — adapter, output and factory are done with. The factory field is left
    // pointing at the released object (see slot 3's note).
    adapter->Release();
    output->Release();
    factory->Release();

    // 16
    if (!CreateColorBuffer(this)) {
        int aa = ResolveMultisampleMode(this);
        const System::String msg = System::String::Format(
            "VideoDX11: Failed to create color buffer with format "
            "DXGI_FORMAT_B8G8R8A8_UNORM [%ix%i %ixAA]",
            dev->backBufferWidth,
            dev->backBufferHeight, aa);
        System::Error::SetText(msg._data, false);
        return false;
    }
    if (!CreateDepthBuffer(this)) {
        int aa = ResolveMultisampleMode(this);
        const System::String msg = System::String::Format(
            "VideoDX11: Failed to create a valid depth buffer format "
            "DXGI_FORMAT_D24_UNORM_S8_UINT [%ix%i %AA]!",
            dev->backBufferWidth,
            dev->backBufferHeight,
            // NOT RETAIL (UB, vararg type): retail's text has `%A` (a hex double) where the
            // colour-buffer message has `%ixAA`, and passes the int; the int is passed as the
            // double `%A` reads. The format text is retail's.
            static_cast<double>(aa));
        System::Error::SetText(msg._data, false);
        return false;
    }

    StallDebug::Mark("  DX11 swapchain + color/depth buffers");

    // 17
    ::Application::Application* app = AppOf();
    app->RegisterMessageCallback(Platform::Window::kMsgSetCursor, &DeviceData_OnSetCursor);   // 0x20
    app->RegisterMessageCallback(Platform::Window::kMsgActivate, &DeviceData_OnActivate);      // 6
    // NOT RETAIL (ext.create_on_demand): objects waiting for their first use are made on this device.
    CreateOnDemand::Resume();
    return true;
}

// ============================================================
// Slot 8 — 0x180005450  SetImageBuffer(ImageBuffer*, int hotX, int hotY)
//
// Builds a cursor (retail: a Win32 HCURSOR; Platform::Cursor::Create) from a 32x32
// Video::ImageBuffer. No D3D11 at all.
//
// Early-out: if (imageBuffer, hotX, hotY) all match what is already cached in
// +0x50/+0x58/+0x5c, do nothing. Otherwise destroy the old cursor and rebuild.
//
// The pixel classification (0x180005520-0x180005549) is:
//
//   alpha == 0            -> set the bit in the AND plane  (transparent)
//   alpha != 0 && B != 0  -> set the bit in the XOR plane  (white)
//   alpha != 0 && B == 0  -> neither bit                   (black)
//
// It tests the BLUE channel, not luminance: `TEST DL,DL` on the low byte of a
// BGRA texel. A cursor drawn in pure red would come out black.
//
// Bit order: within each 32-pixel row the first pixel is bit 31 and the last is
// bit 0, then the row's dword is byte-reversed (0x180005553-0x1800055b4 is a
// hand-expanded bswap). Net effect: the leftmost pixel is the MSB of the first
// byte, which is what CreateCursor wants.
//
// The masks are 32 rows x 4 bytes = 128 bytes each, zeroed as one dword plus a
// 124-byte memset (0x1800054e2 / 0x1800054e6) — a 128-byte memset is the same
// thing.
//
// Note the source read is bracketed by Video::Device::Lock/Unlock, because
// CopyFromTexture stages through the immediate context.
// ============================================================
void DeviceData::SetImageBuffer(::Video::ImageBuffer* image, int hotX, int hotY) {
    // `image` is the ImageBuffer it always was: retail's Video::Device::SetCursor
    // passes an ExternalPtr<ImageBuffer> and the slot is typed since B5 CLOSE.

    if (image == cursorImage &&
        hotX == cursorHotspotX &&
        hotY == cursorHotspotY)
        return;

    if (cursor)
        Platform::Cursor::Destroy(cursor);

    ::Video::Device* dev = _owner;

    uint32_t pixels[32 * 32];
    dev->Lock();
    image->CopyFromTexture(pixels, 32, 32);
    dev->Unlock();

    uint32_t andPlane[32];
    uint32_t xorPlane[32];
    std::memset(andPlane, 0, sizeof(andPlane));
    std::memset(xorPlane, 0, sizeof(xorPlane));

    int flat = 0;
    for (int row = 0; row < 32; ++row) {
        for (int bit = 31; bit >= 0; --bit, ++flat) {
            const uint32_t texel = pixels[flat];
            const uint32_t mask  = 1u << bit;
            if ((texel & 0xff000000u) == 0)
                andPlane[row] |= mask;                 // transparent
            else if ((texel & 0xffu) != 0)
                xorPlane[row] |= mask;                 // white
        }
        // The hand-expanded byte reversal at 0x180005553.
        const uint32_t a = andPlane[row];
        andPlane[row] = (a >> 24) | ((a >> 8) & 0xff00u) |
                        ((a << 8) & 0xff0000u) | (a << 24);
        const uint32_t x = xorPlane[row];
        xorPlane[row] = (x >> 24) | ((x >> 8) & 0xff00u) |
                        ((x << 8) & 0xff0000u) | (x << 24);
    }

    // Application::GetInstance is the raw HINSTANCE, which CreateCursor takes.
    Platform::CursorHandle built = Platform::Cursor::Create(
        AppOf()->GetInstance(), hotX, hotY, 32, 32, andPlane, xorPlane);
    cursor = built;
    Platform::Cursor::Set(built);

    cursorImage    = image;
    cursorHotspotX = hotX;
    cursorHotspotY = hotY;
}

namespace {

// ============================================================
// 0x180005b00 — Collection::Array<GraphicsInterface>::SetLength(int)
//
// The generic grower, specialised for GraphicsInterface (stride 0x18). Growth
// rule, transcribed: when the request exceeds capacity, capacity becomes the
// request — EXCEPT when the request is exactly oldCount+1 and oldCount was
// non-zero, in which case it becomes request*3. So the first append allocates
// exactly 1 and every later append triples. Reproduced because the allocation
// sizes are observable in a heap trace.
//
// New elements are copy-constructed from `proto`; carried-over elements are
// copy-constructed from the old block, which is then destroyed and freed. When
// the block did NOT move, only the tail past the new length is destroyed.
// ============================================================
// B5 CLOSE: the {data, count, capacity} triple is `Video::Device`'s own named
// `interfaceData` / `interfaceCount` / `interfaceCapacity` (+0x40 / +0x48 / +0x4c).
void GfxInterfaceArraySetLength(::Video::Device* dev, int newCount,
                                const GraphicsInterface* proto) {
    GraphicsInterface** dataP = &dev->interfaceData;
    int32_t* countP = &dev->interfaceCount;
    int32_t* capP   = &dev->interfaceCapacity;

    const int oldCount = *countP;
    if (newCount == oldCount) return;

    GraphicsInterface* oldBase = *dataP;
    *countP = newCount;

    if (newCount > *capP) {
        int cap = newCount;
        *capP = cap;
        if (newCount == oldCount + 1 && oldCount != 0) {
            cap = newCount * 3;
            *capP = cap;
        }
        *dataP = static_cast<GraphicsInterface*>(
            System::MemoryAllocator::MemAlloc(static_cast<size_t>(cap) * sizeof(GraphicsInterface), 8));
    }

    GraphicsInterface* base = *dataP;
    int i = (oldBase == base) ? oldCount : 0;
    // NOT RETAIL (UB, null-pointer arithmetic): retail forms `base + i` and then tests it for
    // null (MSVC's placement-new check); with a failed MemAlloc that is null + i, which is
    // undefined and lets the compiler drop the test. The test is made once, on `base`: the same
    // answer for every element.
    if (base == nullptr)
        i = newCount;
    for (; i < newCount; ++i) {
        GraphicsInterface* dst = base + i;
        const GraphicsInterface* src = (i < oldCount) ? (oldBase + i) : proto;
        ::new (static_cast<void*>(dst)) GraphicsInterface(*src);   // 0x18000ce10
    }

    // The block is raw MemAlloc'd storage, so each element's destructor is
    // called explicitly (0x18000cdc0), in the shipped order, before the free.
    if (oldBase != base) {
        for (int j = 0; j < oldCount; ++j)
            (oldBase + j)->~GraphicsInterface();
        System::MemoryAllocator::MemFree(oldBase);
    } else {
        for (int j = newCount; j < oldCount; ++j)
            (oldBase + j)->~GraphicsInterface();
    }
}

// The same grower, inlined into slot 1 for Adapter (stride 0x38) with the
// identical growth rule. The shipped code has two separate expansions of the
// same template; they are byte-for-byte the same algorithm.
void AdapterArraySetLength(GraphicsInterface* gi, int newCount,
                           const Adapter* proto) {
    const int oldCount = gi->adapterCount;
    if (newCount == oldCount) return;

    Adapter* oldBase = gi->adapterData;
    gi->adapterCount = newCount;

    if (newCount > gi->adapterCapacity) {
        int cap = newCount;
        gi->adapterCapacity = cap;
        if (newCount == oldCount + 1 && oldCount != 0) {
            cap = newCount * 3;
            gi->adapterCapacity = cap;
        }
        gi->adapterData = static_cast<Adapter*>(
            System::MemoryAllocator::MemAlloc(static_cast<size_t>(cap) * sizeof(Adapter), 8));
    }

    Adapter* base = gi->adapterData;
    int i = (oldBase == base) ? oldCount : 0;
    // NOT RETAIL (UB, null-pointer arithmetic): as GfxInterfaceArraySetLength, the per-element
    // null test on `base + i` is made once on `base`.
    if (base == nullptr)
        i = newCount;
    for (; i < newCount; ++i) {
        Adapter* dst = base + i;
        const Adapter* src = (i < oldCount) ? (oldBase + i) : proto;
        ::new (static_cast<void*>(dst)) Adapter(*src);   // 0x18000cc70
    }

    if (oldBase != base) {
        for (int j = 0; j < oldCount; ++j)
            (oldBase + j)->~Adapter();                    // 0x18000cc10
        System::MemoryAllocator::MemFree(oldBase);
    } else {
        for (int j = newCount; j < oldCount; ++j)
            (oldBase + j)->~Adapter();
    }
}

// ============================================================
// Slot 1 — 0x180004690  enumerate adapters / outputs / display modes
//
// The largest function in the DLL (494 instructions). It builds ONE
// Video::Device::GraphicsInterface named "DirectX 11" holding one
// Video::Device::Adapter per (DXGI adapter, DXGI output) PAIR that has at least
// one usable display mode, and appends it to Device+0x40.
//
// Per output it records: a display name, the filtered mode list, a 32-entry
// MSAA support table, the max anisotropy, and the (adapterIndex, outputIndex)
// that slot 2 will later re-enumerate with.
//
//   for (a = 0; factory->EnumAdapters1(a, &ad) != NOT_FOUND; ++a) {
//       ad->GetDesc1(&d1)
//       for (o = 0; ad->EnumOutputs(o, &out) != NOT_FOUND; ++o) {
//           Adapter rec;                             // fresh each iteration
//           rec.name = WideString(d1.Description).AsUTF8()
//           rec.maxAnisotropy = 16
//           rec.adapterIndex = a; rec.outputIndex = o
//           out->GetDesc(&od)
//           GetMonitorInfoW(od.Monitor, &mi)         // MONITORINFOEXW, cbSize 0x68
//           rec.name += " (Display " + AsUTF8(&mi.szDevice[len-1]) + ")"
//                     // (port since D.4 RENDER §C.3: od.DeviceName, same string)
//           out->GetDisplayModeList(28, 0, &n, NULL)
//           modes = MemAlloc(n * 0x1c, 4)
//           out->GetDisplayModeList(28, 0, &n, modes)
//           for each mode with Width >= 800:
//               rec.FindOrAddMode(DisplayMode(w,h), RefreshRate{num,den})
//           out->Release()
//           if (rec.modeCount != 0) {
//               D3D11CreateDevice(ad, ...)           // probe device, discarded
//               resize rec.msaaSupported to 32 bytes
//               for (s = 1; s <= 32; ++s)
//                   rec.msaaSupported[s-1] =
//                       CheckMultisampleQualityLevels(0x57, s, &q), q != 0
//               ctx->Release(); dev->Release()
//               gi.adapters.SetLength(gi.adapterCount + 1)   // appends rec
//           }
//           MemFree(modes)
//       }
//       ad->Release()
//   }
//   factory->Release()
//   if (gi.adapterCount > 0)
//       Device.interfaces.SetLength(Device.interfaceCount + 1, &gi)
//   return gi.adapterCount > 0
//
// Notes on fidelity:
//
//   * The display-mode format is 28 (R8G8B8A8_UNORM), NOT the 0x57
//     (B8G8R8A8_UNORM) everything else in the DLL uses (`LEA EDX,[R8+0x1c]` at
//     0x180004916 with R8 = 0). Same mismatch as the resize helper's
//     ResizeTarget. Reproduced.
//   * The MSAA probe uses 0x57, so the two formats really are used side by side
//     in the same function.
//   * The 800-pixel floor is an unsigned compare (`CMP dword ptr [RBX],0x320;
//     JC`) against the WIDTH only; height is unfiltered.
//   * The name suffix takes `&szDevice[len-1]` (the port: `&od.DeviceName[len-1]`,
//     RENDER §C.3) — the LAST wide character of the
//     device name, i.e. the display number off the end of "\\.\DISPLAY3". That
//     is why the string reads " (Display 3)".
//   * The shipped code contains an inlined Array<DXGI_MODE_DESC>::SetLength
//     whose copy loop is provably dead (its guard is `index >= 0`, always true
//     for a loop counting up from zero) and whose MemFree argument is provably
//     the null old base. Both are omitted here; neither has an observable
//     effect, and writing them out would be transcribing decompiler noise
//     rather than behaviour.
//   * The probe D3D11CreateDevice's HRESULT failure path SKIPS the append but
//     still frees the mode list and destroys the record. Reproduced.
//   * On the probe-device path the original does NOT check the ID3D11Device
//     out-pointer for null before calling through it.
// ============================================================
// The adapter/output/mode walk, factored out of DeviceData_EnumerateInterfaces
// so the cutover-only DX9 placeholder (dx9_placeholder.cpp, through
// Dx11EnumerateDxgiInterfaces at the end of this file) can list the SAME adapters under
// its own name (on a retail machine the DX9 plugin enumerates the same GPUs and
// modes through d3d9). Byte-for-byte the shipped walk; only `dev` and the
// interface name are parameters now.
static bool EnumerateDxgiInterfaceInto(::Video::Device* dev, const char* interfaceName,
                                       CreateDXGIFactory1_t createFactory,
                                       D3D11CreateDevice_t  createDevice) {
    // B5 DEV-B: `gi` is constructed by ??0GraphicsInterface (0x18000cd90) and
    // destroyed by ??1GraphicsInterface (0x18000cdc0) at either return — the
    // two points the shipped code called it.
    GraphicsInterface gi;
    {
        const System::String name(interfaceName);
        gi.name = name;
    }

    IDXGIFactory1* factory = nullptr;
    if (createFactory(kIID_IDXGIFactory1, &factory) != 0)
        return false;                                     // ~GraphicsInterface(gi)

    for (UINT adapterIndex = 0; ; ++adapterIndex) {
        IDXGIAdapter1* dxgiAdapter = nullptr;
        if (factory->EnumAdapters1(adapterIndex, &dxgiAdapter) == DXGI_ERROR_NOT_FOUND)
            break;

        DXGI_ADAPTER_DESC1 adapterDesc;
        dxgiAdapter->GetDesc1(&adapterDesc);

        for (UINT outputIndex = 0; ; ++outputIndex) {
            // A fresh record per output, seeded from the adapter description.
            // B5 DEV-B: ??0Adapter (0x1800f7100) here; ??1Adapter (0x18000cc10)
            // runs at the `break` below and at the end of each iteration, the
            // two points the shipped code called it.
            Adapter rec;
            {
                // B5 CLOSE: a real `System::WideString` local; its destructor runs at
                // this scope's end, exactly where the shipped code called it.
                const DxgiWideText<128> description(adapterDesc.Description);  // RENDER C.4
                const System::WideString w(description.get());
                {
                    // ENDGAME B.3.5 block J: `?AsUTF8@WideString@System@@` is a
                    // real const member returning `System::String` by value, so
                    // the local is DIRECT-INITIALISED from the prvalue — one
                    // construction into the storage the door used to be handed as
                    // a retslot, instead of a default construction plus that
                    // write. The inner scope still releases it before the
                    // WideString, which is the shipped order.
                    const System::String utf8 = w.AsUTF8();
                    rec.name = utf8;
                }
            }                                   // w
            // ENDGAME B.3.5 block D2: the real class's field names. These three
            // stores are the EVIDENCE that Adapter+0x28/+0x2c/+0x30 are the
            // anisotropy cap and the two DXGI enumeration coordinates; Device.h
            // records that and renames its two "meaning not recovered" fields.
            rec.maxFilterLevel   = 0x10;
            rec.dxgiAdapterIndex = static_cast<int32_t>(adapterIndex);
            rec.dxgiOutputIndex  = static_cast<int32_t>(outputIndex);

            IDXGIOutput* output = nullptr;
            if (dxgiAdapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND)
                break;                                    // ~Adapter(rec)

            DXGI_OUTPUT_DESC outDesc;
            output->GetDesc(&outDesc);

            // Phase D.4 RENDER §C.3 (behaviour): retail asked GDI for the
            // monitor's device name here (0x180004804-0x180004823:
            //   LEA R8D,[RDX+0x68] / LEA RCX,[RBP+0x60] / CALL memset
            //   MOV dword ptr [RBP+0x60],0x68          ; mi.cbSize
            //   LEA RDX,[RBP+0x60]
            //   MOV RCX,qword ptr [RBP+0x128]          ; outDesc.Monitor
            //   CALL qword ptr [0x1800134e8]           ; GetMonitorInfoW
            // then StrLength / WideString on [RBP+0x88] = mi.szDevice).
            // The name is read from the DXGI_OUTPUT_DESC GetDesc just filled
            // instead: on Windows, Microsoft's DXGI fills DeviceName with the
            // same GDI name MONITORINFOEXW::szDevice carries ("\\.\DISPLAYn"
            // for the same HMONITOR). The one retail path this drops: a
            // failed GetMonitorInfoW left szDevice zeroed, so StrLength was 0
            // and the suffix read szDevice[-1] (the tail of mi.rcWork).
            // GetDesc always fills DeviceName.
            // ABI hook (RENDER §C.4, Phase D.4 LINUX-1): DeviceName is WCHAR[32];
            // this read and adapterDesc.Description above go through
            // DxgiWideText (a no-op cast to char16_t).

            // rec.name += " (Display " + last-char-of(DeviceName) + ")"
            // Temporary construction and destruction order is the shipped one
            // (0x180004829-0x1800048e8): prefix, wide, utf8, t1, t2, t3, then
            // destroyed t3, t2, t1, utf8, wide, prefix.
            {
                // The nested scopes below reproduce the shipped destruction
                // order exactly — t3, t2, t1, tailU, tailW, prefix — with each
                // System::String and WideString released by its own destructor
                // at the scope end where the shipped one called it.
                const System::String prefix(" (Display ");
                {
                    const DxgiWideText<32> deviceNameText(outDesc.DeviceName);  // RENDER C.4
                    const char16_t* const deviceName = deviceNameText.get();
                    const int len = System::WideString::StrLength(deviceName);
                    const System::WideString tailW(deviceName + (len - 1));
                    {
                        // Same shape as the adapter description above.
                        const System::String tailU = tailW.AsUTF8();
                        {
                            const System::String t1 = prefix + tailU;
                            const System::String t2 = t1 + ")";
                            // operator+= returns the new value BY VALUE and the
                            // caller discards the copy; that copy is retail's t3.
                            const System::String t3 =
                                (rec.name += t2);
                            (void)t3;
                        }                       // t3, t2, t1
                    }                           // tailU
                }                               // tailW
                                                // prefix
            }

            // Display modes. Format 28 is what the shipped code asks for.
            UINT modeCount = 0;
            DXGI_MODE_DESC* modes = nullptr;
            if (output->GetDisplayModeList(static_cast<DXGI_FORMAT>(28), 0,
                                           &modeCount, nullptr) == 0) {
                if (modeCount != 0)
                    modes = static_cast<DXGI_MODE_DESC*>(
                        System::MemoryAllocator::MemAlloc(static_cast<size_t>(modeCount) * sizeof(DXGI_MODE_DESC), 4));

                // NOT RETAIL (UB, null dereference): with a first count of 0 `modes` stays
                // null, the second call is then a count query, and a mode list that grew in
                // between (hot-plug, a mode change) returns n > 0 with S_OK; retail then reads
                // modes[0] through null. The loop runs only over a buffer; the call is as retail.
                if (output->GetDisplayModeList(static_cast<DXGI_FORMAT>(28), 0,
                                               &modeCount, modes) == 0 && modes != nullptr) {
                    for (UINT i = 0; i < modeCount; ++i) {
                        const DXGI_MODE_DESC& m = modes[i];
                        if (m.Width < 800) continue;
                        DisplayMode dm;
                        Video_DisplayMode_Ctor(&dm, static_cast<int>(m.Width),
                                            static_cast<int>(m.Height));
                        RefreshRate rr;
                        rr.numerator   = static_cast<int32_t>(m.RefreshRate.Numerator);
                        rr.denominator = static_cast<int32_t>(m.RefreshRate.Denominator);
                        rec.FindOrAddMode(dm, rr);
                        Video_DisplayMode_Dtor(&dm);
                    }
                }
            }
            output->Release();

            if (rec.modeCount != 0) {
                const D3D_FEATURE_LEVEL levels[3] = {
                    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                };
                ID3D11Device*        probeDev = nullptr;
                ID3D11DeviceContext* probeCtx = nullptr;
                D3D_FEATURE_LEVEL    granted  = static_cast<D3D_FEATURE_LEVEL>(0);

                if (createDevice(dxgiAdapter, static_cast<D3D_DRIVER_TYPE>(0), nullptr, 0,
                                 levels, 3, D3D11_SDK_VERSION,
                                 &probeDev, &granted, &probeCtx) == 0) {
                    // Grow the MSAA table to 32 bytes. Same growth rule as the
                    // other two arrays; from a fresh Adapter this is a plain
                    // MemAlloc(32, 1) with nothing to carry over.
                    if (rec.multisampleCount != 0x20) {
                        uint8_t* oldBase = rec.multisampleData;
                        const int oldCount = rec.multisampleCount;
                        rec.multisampleCount = 0x20;
                        if (rec.multisampleCap < 0x20) {
                            int cap = 0x20;
                            rec.multisampleCap = cap;
                            if (oldCount + 1 == 0x20 && oldCount != 0) {
                                cap = 0x60;
                                rec.multisampleCap = cap;
                            }
                            rec.multisampleData = static_cast<uint8_t*>(
                                System::MemoryAllocator::MemAlloc(static_cast<size_t>(cap), 1));
                        }
                        unsigned char* base = rec.multisampleData;
                        int i = (oldBase == rec.multisampleData) ? oldCount : 0;
                        for (; i < 0x20; ++i)
                            if (i < oldCount)
                                base[i] = oldBase[i];
                        if (oldBase != rec.multisampleData)
                            System::MemoryAllocator::MemFree(oldBase);
                    }

                    unsigned char* msaa = rec.multisampleData;
                    for (int sampleCount = 1; sampleCount <= 0x20; ++sampleCount) {
                        UINT quality = 0;
                        probeDev->CheckMultisampleQualityLevels(
                            DXGI_FORMAT_B8G8R8A8_UNORM,
                            static_cast<UINT>(sampleCount), &quality);
                        msaa[sampleCount - 1] = (quality != 0) ? 1 : 0;
                    }

                    probeCtx->Release();
                    probeDev->Release();

                    AdapterArraySetLength(&gi, gi.adapterCount + 1, &rec);
                }
            }

            System::MemoryAllocator::MemFree(modes);
        }                                                 // ~Adapter(rec)

        dxgiAdapter->Release();
    }

    factory->Release();

    const bool any = gi.adapterCount > 0;
    if (any) {
        // B5 DEV-A: Device+0x40 by name (`Dev::kInterfaces` is gone).
        GfxInterfaceArraySetLength(dev, dev->interfaceCount + 1, &gi);
    }

    return any;                                           // ~GraphicsInterface(gi)
}

}  // namespace

// Slot 1. B5 DEV-A (B5_RULES D9): the arm that forwarded to the shipped DLL's
// slot 1 when dxgi.dll / d3d11.dll's entry points were missing is `return false`
// inside Dx11EnumerateDxgiInterfaces (the end of this file): there is no
// original to run.
bool DeviceData::EnumerateInterfaces() {
    return Dx11EnumerateDxgiInterfaces(_owner, "DirectX 11");
}

// ============================================================
// Slots 9..19 — the eleven resource factories
//
// Every one is the same five steps:
//     p = MemAlloc(sizeof, 4)
//     if (!p) return NULL
//     Video::<Class>Data::<Class>Data(p, owner)     // the Runtime base ctor
//     p->vptr = <the DX11 vtable>                   // overwrite the base's
//     zero the DX11-private tail
//     return p
//
// The address <-> class pairing was verified TWICE, independently:
//   1. Ghidra's recovered signature for each factory names both the return type
//      and the Runtime base ctor it calls.
//   2. The CALL displacement in each factory resolves through the derived IAT
//      base (0x180013218) to exactly the matching ??0<Class>Data mangled name
//      in objdump's import listing.
// Both agree with the order src/render/dx11/README.md already recorded. Table:
//
//   slot  factory       size  vtable       base ctor IAT  class
//   ----  ------------  ----  -----------  -------------  ------------------
//    9    0x180005650   0x20  0x1800186f0  0x180013478    ContextData
//   10    0x1800056c0   0x38  0x180018790  0x1800133e8    ImageBufferData
//   11    0x180005730   0x28  0x1800185d0  0x180013490    ConstantBufferData
//   12    0x1800057a0   0x20  0x1800187c8  0x180013390    IndexBufferData
//   13    0x180005810   0x20  0x180018830  0x180013340    VertexBufferData
//   14    0x180005880   0x18  0x180018878  0x180013330    VertexLayoutData
//   15    0x1800058f0   0x30  0x180018890  0x180013320    VertexProgramData
//   16    0x180005960   0x30  0x180018800  0x180013360    PixelProgramData
//   17    0x1800059d0   0x28  0x180018818  0x180013350    RenderStateData
//   18    0x180005a40   0x10  0x1800185a8  0x1800134a0    VideoProgramData
//   19    0x180005aa0   0x10  0x180018588  0x1800134b0    RenderBufferData
//
// Two initialisation values are NOT zero and matter:
//   * ConstantBufferData+0x10 is set to -1 (0x180005758), the "no rung bound
//     yet" sentinel L1 documented.
//   * ContextData+0x10 is set to 0 as a DWORD, which is
//     D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED and therefore never equal to a
//     topology the class would set — L5 relies on that.
// Index/VertexBufferData write +0x18 as a dword and +0x1c as a single byte
// (0x1800057d8 region), leaving +0x1d..+0x1f untouched; that is reproduced by
// assigning the named fields rather than memsetting the tail.
//
// The `this` argument (RCX) is the DeviceData and is ignored by all eleven.
//
// B5 DEV-A: the eleven are `DeviceData` members overriding the base's slots
// 9..19 with its exact signatures (base-typed returns). A class that is a real
// subclass is `return new XData(owner);` — the base's class-scope operator new
// is retail's MemAlloc(size, 4) and the ctor chain is `??0XData` + the vptr
// store, now the compiler's (retail's `if (!p) return NULL` is not reproduced:
// B5_RULES D2). Since block RES landed all eleven are real subclasses, so the
// POD helper (MemAlloc + base ctor + hand-table vptr) and its bridge casts are
// gone.
//
// ENDGAME B.3R block DX: the base-ctor step was a call through the import row
// `??0XData@Video@@QEAA@PEAVX@1@@Z`, whose body was the Runtime door
// `Video_XData_Ctor` = `::new (self) Video::XData(owner)`; it is now the base
// constructor of each `new XData(owner)`. The readiness guard that forwarded
// each factory to the shipped slot when an import was missing is gone with the
// struct.
// ============================================================
::Video::ContextData* DeviceData::CreateContextData(::Video::Context* owner) {
    // B5 CTX: the base's operator new is retail's MemAlloc(0x20, 4); the ctor chain
    // is retail's `??0ContextData` + the vptr store (now the compiler's table) +
    // the three tail stores (ContextData's constructor). The `if (!p)` arm is
    // gone (B5_RULES D2: a null from MemAlloc is a crash either way).
    return new ContextData(owner);
}

::Video::ImageBufferData* DeviceData::CreateImageBufferData(::Video::ImageBuffer* owner) {
    // B5 RES: the base's operator new is retail's MemAlloc(0x38, 4); the ctor chain
    // is retail's `??0ImageBufferData` + the vptr store (now the compiler's table)
    // + the five null stores.
    return new ImageBufferData(owner);
}

::Video::ConstantBufferData* DeviceData::CreateConstantBufferData(::Video::ConstantBuffer* owner) {
    // B5 BUF: the base's operator new is retail's MemAlloc(0x28, 4); the ctor chain
    // is retail's `??0ConstantBufferData` + the vptr store (now the compiler's
    // table) + the -1 sentinel and the four zeroes, then (ours) the join to the
    // reset registry Dx11InvalidateConstantBufferBindings walks — buffers.cpp.
    return new ConstantBufferData(owner);
}

::Video::IndexBufferData* DeviceData::CreateIndexBufferData(::Video::IndexBuffer* owner) {
    // B5 BUF: MemAlloc(0x20, 4) + `??0IndexBufferData` + the vptr store + buffer,
    // writeCursor (dword) and locked (byte); +0x1d..+0x1f untouched.
    return new IndexBufferData(owner);
}

::Video::VertexBufferData* DeviceData::CreateVertexBufferData(::Video::VertexBuffer* owner) {
    // B5 BUF: MemAlloc(0x20, 4) + `??0VertexBufferData` + the same three stores.
    return new VertexBufferData(owner);
}

::Video::VertexLayoutData* DeviceData::CreateVertexLayoutData(::Video::VertexLayout* owner) {
    // B5 RES: the base's operator new is retail's MemAlloc(0x18, 4); the ctor chain
    // is retail's `??0VertexLayoutData` + the vptr store (now the compiler's table)
    // + `inputLayout = nullptr`.
    return new VertexLayoutData(owner);
}

::Video::VertexProgramData* DeviceData::CreateVertexProgramData(::Video::VertexProgram* owner) {
    // B5 RES: the base's operator new is retail's MemAlloc(0x30, 4); the ctor chain
    // is retail's `??0VertexProgramData` + the vptr store (now the compiler's table)
    // + the four `variants[i] = nullptr`.
    return new VertexProgramData(owner);
}

::Video::PixelProgramData* DeviceData::CreatePixelProgramData(::Video::PixelProgram* owner) {
    // B5 RES: the base's operator new is retail's MemAlloc(0x30, 4); the ctor chain
    // is retail's `??0PixelProgramData` + the vptr store (now the compiler's table)
    // + the four `variants[i] = nullptr`.
    return new PixelProgramData(owner);
}

::Video::RenderStateData* DeviceData::CreateRenderStateData(::Video::RenderState* owner) {
    // B5 RES: the base's operator new is retail's MemAlloc(0x28, 4); the ctor chain
    // is retail's `??0RenderStateData` + the vptr store (now the compiler's table)
    // + the three null stores.
    return new RenderStateData(owner);
}

::Video::VideoProgramData* DeviceData::CreateVideoProgramData(::Video::VideoProgram* owner) {
    // B5 PILOT: the base's operator new is retail's MemAlloc(0x10, 4); the ctor chain
    // is retail's `??0VideoProgramData` + the vptr store (now the compiler's table).
    return new VideoProgramData(owner);
}

::Video::RenderBufferData* DeviceData::CreateRenderBufferData(::Video::RenderBuffer* owner) {
    // B5 PILOT: the base's operator new is retail's MemAlloc(0x10, 4); the ctor chain
    // is retail's `??0RenderBufferData` + the vptr store (now the compiler's table).
    return new RenderBufferData(owner);
}

// The one function in this lane with a caller outside it. 0x180004340 is a near
// CALL from ContextData::BeginCommands, not a vtable slot, so it needs a
// declaration with external linkage — ContextData names it directly, and it can only do
// that through a declaration with external linkage. (B5 DEV-A: the slots are
// DeviceData members now; the helpers stay in the anonymous namespace.)
int Dx11_DeviceData_Resize(DeviceData* deviceData) {
    return DeviceData_Resize(deviceData);
}

// The slot-1 walk with external linkage (VideoDX11Plugin.h): DeviceData's own
// EnumerateInterfaces and the DX9 placeholder (dx9_placeholder.cpp) both run
// it, under their own interface name — one body, never a copy. B5 DEV-A (D9):
// the two graphics entry points failing to resolve returns false; there is no
// shipped DLL behind it to forward to.
bool Dx11EnumerateDxgiInterfaces(::Video::Device* device, const char* interfaceName) {
    CreateDXGIFactory1_t createFactory = GetCreateDXGIFactory1();
    D3D11CreateDevice_t  createDevice  = GetD3D11CreateDevice();
    if (!createFactory || !createDevice) return false;
    return EnumerateDxgiInterfaceInto(device, interfaceName, createFactory, createDevice);
}

}  // namespace VideoDX11

// ============================================================
// Original quirks reproduced (do not "fix" these)
//
//  1. Slot 1 and the resize helper ask DXGI for format 28
//     (R8G8B8A8_UNORM) when enumerating display modes and when calling
//     ResizeTarget, while every buffer the backend creates is 0x57
//     (B8G8R8A8_UNORM). Two different formats, same code paths.
//  2. Slot 2 releases the IDXGIFactory1 but never nulls DeviceData+0x10, so
//     that field dangles for the whole session; slot 3 correctly does not
//     release it again.
//  3. Slot 3 destroys the cursor without nulling DeviceData+0x48.
//  4. Slot 2's failure paths leak the factory, adapter, output, D3D11 device
//     and context.
//  5. Slot 2 and slot 1 treat ONLY DXGI_ERROR_NOT_FOUND as an enumeration
//     failure; any other non-zero HRESULT proceeds with an unvalidated pointer.
//  6. ResolveMultisampleMode's downward walk has no lower bound.
//  7. Slot 8 classifies cursor pixels off the BLUE channel alone.
//  7b. The depth-buffer failure message's format string is
//     "... [%ix%i %AA]!" — `%A` is a real printf conversion (hex float), so it
//     consumes the int sample count as a double. The colour-buffer message
//     next to it correctly says "%ixAA". Transcribed verbatim: it is a shipped
//     string on a shipped failure path.
//  8. Video::Device::AdjustDisplay (Runtime, not ours) builds its off-screen
//     probe rectangle with `bottom = rc.bottom + X` — the X offset, not Y.
//     Noted here because it affects where a restored window lands; not our code
//     to change.
//
// ============================================================
// B5 DEV-A: the sixteen DeviceData slot registrations that stood
// here (and slots 4-7's four in devicedata_stubs.cpp) are GONE: DeviceData is a
// real subclass and the compiler builds its table. Slots 1, 2 and 3 are the
// three that can end a session with no window; they were verified in-game and
// RenderDoc-diffed against the shipped DLL before graduating (src/render/dx11/README.md
// "Per-draw pipeline-state equality") and their bodies are unchanged here.
// ============================================================
