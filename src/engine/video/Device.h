#pragma once
// ============================================================================
// src/engine/video/Device.h — Wave 6 lane B, promoted to real C++ classes by
// ENDGAME B.3.5 block D2.
//   Video::Device and its five nested value types, Video::Configuration and
//   Video::RenderBuffer.
// ============================================================================
//
// Every offset below is transcribed from a retail body decompiled at its real
// address in Runtime-x64.dll; the function that pins each one is named next to
// it. Where a field is only pinned by "the ctor zeroes this qword" the comment
// says so, because that is weaker evidence than a read with a known meaning.
//
// ---------------------------------------------------------------------------
// B.3.5 BLOCK D2 — WHAT CHANGED, AND WHAT DELIBERATELY DID NOT
// ---------------------------------------------------------------------------
// `Video::Device`, `Video::Device::{RefreshRate,DisplayMode,Adapter,
// GraphicsInterface,InterfaceSlot}`, `Video::Configuration` and
// `Video::RenderBuffer` are now REAL C++ classes under their retail names, in
// `namespace Video`, with their exported members declared as members and the
// registered doors delegating to them with a qualified non-virtual call.
//
// ONE DEFINITION, NOT TWO (B34B_RULES B4b-1, and the brief's "where class and
// POD are field-identical prefer ONE definition and an alias"). The old PODs
// lived in `namespace RtVideo` and were FIELD-IDENTICAL to the retail classes,
// so there is nothing to duplicate: `RtVideo::Device` & co. are now ALIASES of
// the real classes. `src/engine/engine/singleton_ctors.cpp`,
// `src/engine/graphics/sunlight.cpp`, `src/engine/save/saveload_draw.cpp`,
// `src/engine/video/companion_data.cpp` and
// `src/engine/video/context_lifecycle.cpp` — five files in four other owners'
// sets — keep compiling untouched because of those aliases. Nested types get an
// alias too: `RtVideo::Adapter` is `Video::Device::Adapter`, which is where
// retail's `??0Adapter@Device@Video@@` says it lives.
//
// NONE OF THESE CLASSES TAKES A REAL CONSTRUCTOR OR DESTRUCTOR, and each
// abstention has its own reason recorded at the class:
//   * `Device`        — RULE B5-6. It owns a `CRITICAL_SECTION` at +0x60 whose
//                       owner-tid and depth words are read at PINNED offsets by
//                       dx11 and by four `Platform*` bodies;
//                       `InitializeCriticalSection` is retail's `Device::Startup`,
//                       not a constructor, and this header must stay
//                       `<windows.h>`-free (PLAN §4.1 decision 1).
//   * `RenderBuffer`, `IndexBuffer`, `VertexBuffer` (Buffers.h) — their
//                       ctors/dtors maintain the `_firstRenderBuffer` /
//                       `_firstBuffer` intrusive lists under the Device lock,
//                       and the list head is an `RT_DATA_SLOT` read, not a C++
//                       static (RULE B4b-4, B35_RULES §9).
//   * `Configuration` — `??0Configuration@Video@@QEAA@XZ` (0x1800f6f10) is
//                       UNPORTED; retail constructs the singleton from a static
//                       initializer, never through an allocation (measured this
//                       pass, see the `sizeof` note at the class). Its dtor door
//                       does not even read `this`: it only nulls the singleton
//                       cell, and `Engine::Shutdown` does the `MemFree`
//                       separately. A real dtor would be a second opinion about
//                       a teardown that already happens elsewhere (B34B_RULES
//                       §1.4's `System::Compression` precedent).
//   * `DisplayMode` / `RefreshRate` — retail exports NO default `DisplayMode`
//                       constructor (only `@HH@Z` and the copy ctor), while
//                       `video_mode_sort.cpp:169` default-constructs one as a
//                       swap temporary exactly as retail leaves it
//                       uninitialised — a declared ctor set would forbid the
//                       faithful spelling. So their ctor/dtor DOORS stay free
//                       functions; every other exported member is a real member.
//   * `Adapter` / `GraphicsInterface` — ENDGAME B5 DEV-B (B5_RULES D6) gave
//                       them retail's ctor / copy ctor / dtor as REAL MEMBERS
//                       and a `System::String name`; see the two classes. The
//                       Array element-destroy loops that open-code their
//                       teardown (`RtVideo_ArrayAdapterElemDestroy`,
//                       `RtVideo_ArrayInterfaceElemDestroy`) release the name
//                       through the String member's destructor; the one-
//                       instruction "spelling B" difference is unobservable
//                       (device_enum_types.cpp says why).
// The ctor/dtor/operator-new/operator-delete/operator= exports are tabulated
// per class below with their mangled name, retail address and the door symbol
// that answers them, so no other block has to go looking.
//
// Every exported member of these classes is DECLARED here whether or not it is
// ported (B.3.5 brief). An unported one carries `// NOT PORTED` and has no
// definition anywhere: taking its address or calling it is a link error, which
// is the loud failure we want.
//
// AGREEMENT WITH src/app/video_interface.cpp
// ---------------------------------------------
// That file writes DIRECTLY into the live Device object at hardcoded offsets
// (`kSlot0 = 0xb0`, `kSlotStride = 0x18`, module/+0x00, GetDeviceName/+0x08,
// CreateDeviceData/+0x10) to pre-fill the DX11 plugin slot. `Device::dllSlots`
// below is that same array, at the same offset, with the same stride and the
// same member order, and the static_asserts pin it. `src/app/main.cpp`
// (+0xf8 active interface index, +0x100 DeviceData*) and `src/render/dx11/*.cpp`
// (+0x100) agree too. Nothing in this header contradicts a hardcoded offset
// anywhere else in the tree — see docs/history/runtime_port/WAVE6_LANE_B.md §3.
//
// TWO ARRAYS, NOT ONE. Video::Device carries an `Array<GraphicsInterface>` at
// +0x40 (what GetInterface/GetInterfaceCount serve) AND a separate raw
// three-element array of DLL plugin slots at +0xb0. Both have stride 0x18 and
// it is easy to conflate them: the first is the ENUMERATED renderers the
// options dialog lists, the second is the LOADED plugin modules. They are
// unrelated memory.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "platform_sync.h"   // Platform::LockStorage: the +0x60 lock bytes

// `System::String` is INCLUDED (ENDGAME B5 DEV-B): `Adapter::name` and
// `GraphicsInterface::name` are `System::String` members, which needs the
// complete type. `Device::SetCursor` takes an `Object::ExternalPtr<ImageBuffer>`
// by value as a declaration only, so that one stays an incomplete type and this
// header does not drag `object/External.h` into every includer.
#include "../system/String.h"
namespace Object { template <class T> class ExternalPtr; }

namespace Video {

// Owned by other blocks' headers; pointers and references only here.
class Context;            // src/engine/video/Context.h   (b35-D1)
class ImageBuffer;        // src/engine/video/ImageBuffer.h (b35-C)
class DeviceData;         // src/engine/video/CompanionData.h (b35-A)
class RenderBufferData;   // src/engine/video/CompanionData.h (b35-A)
class RenderBuffer;       // declared in full further down this file
class ConstantBuffer;
class ConstantBufferData;
class ContextData;
class ImageBufferData;
class IndexBuffer;
class IndexBufferData;
class PixelProgram;
class PixelProgramData;
class RenderState;
class RenderStateData;
class VertexBuffer;
class VertexBufferData;
class VertexLayout;
class VertexLayoutData;
class VertexProgram;
class VertexProgramData;
class VideoProgram;
class VideoProgramData;

// ----------------------------------------------------------------------------
// Video::Device — 0x108 bytes, NON-POLYMORPHIC (no `??_7Device@Video@@6B@` and
// no `?_typeInfo@Device@Video@@` in the retail export table, B35_RULES §2.1).
//
// Pinned by ??0Device@Video@@QEAA@W4DeviceType@01@@Z (0x1800f7160), which
// writes every field below except the two critical-section bookkeeping words at
// +0x88/+0x8c (Lock() seeds those), plus the four getters Wave 6 lane B ported,
// ApplyChangedSettings (0x1800f8110), ValidateConfiguration (0x1800f7370),
// Startup (0x1800f7270) and RenderBuffer::PlatformConstruct (0x1800f4320).
//
// The critical section is declared as an opaque byte block on purpose — RULE
// B5-6. This header is included from src/engine/, which PLAN §4.1 decision 1
// keeps free of <windows.h>, and the owner-tid/depth words at +0x88/+0x8c are
// read at those exact offsets by src/render/dx11/contextdata.cpp and by the four
// `{Image,Index,Vertex,Render}Buffer::Platform{Construct,Destruct}` bodies. It
// is NOT a `std::mutex` and NOT a scoped guard; the hand-rolled recursive
// acquire lives in src/engine/video/buffers_lock.cpp.
//
// LIFECYCLE EXPORTS — DOORS, NOT MEMBERS (RULE B5-6):
//   ??0Device@Video@@QEAA@W4DeviceType@01@@Z   0x1800f7160  UNPORTED
//   ??0Device@Video@@QEAA@AEBV01@@Z            0x18000ced0  UNPORTED (copy ctor)
//   ??1Device@Video@@QEAA@XZ                   0x1800f7290  Video_Device_Dtor
//                                                           (video_resources.cpp)
//   ??4Device@Video@@QEAAAEAV01@AEBV01@@Z      0x18000d0b0  UNPORTED (operator=)
//   ??2Device@Video@@SAPEAX_K@Z                0x18000c9b0  UNPORTED (operator new)
//   ??2Device@Video@@SAPEAX_KPEAX@Z            0x18000ca20  UNPORTED (placement new)
//   ??3Device@Video@@SAXPEAX@Z                 0x18000ca30  UNPORTED (operator delete)
//
// STATIC DATA — `?_singleton@Device@Video@@0PEAV12@EA` @ 0x1801fe7a8 is the
// class static `Device::_singleton` (ENDGAME B.3.9 VID, B39_RULES D7): the
// minted row is redirected onto it, so the name door, the address door and the
// member are the same bytes while foreign readers still reach it by name.
// ----------------------------------------------------------------------------
class Device {
public:
    // ========================================================================
    // The five nested value types. Retail's manglings put them INSIDE Device:
    // `??0Adapter@Device@Video@@`, `?FindOrAddMode@Adapter@Device@Video@@`,
    // `?GetInterface@Device@Video@@QEBAAEBUGraphicsInterface@12@H@Z`. `Adapter`
    // and `GraphicsInterface` are `struct` in the manglings (`@U`),
    // `DisplayMode` and `RefreshRate` are `class` (`@V`); `InterfaceSlot` has no
    // retail export at all and is our name for the plugin-slot record
    // `InitializeInterfaces` walks.
    // ========================================================================

    // ------------------------------------------------------------------------
    // Video::Device::RefreshRate — 8 bytes
    // Pinned by Video::Device::ValidateConfiguration (0x1800f7370), which walks a
    // display mode's refresh list with `piVar2 += 2` over int pairs, and by
    // ??0RefreshRate@Device@Video@@QEAA@HH@Z (0x18000d270).
    //
    // DOORS, NOT MEMBERS:
    //   ??0RefreshRate@Device@Video@@QEAA@HH@Z            0x18000d270  UNPORTED
    //   ??4RefreshRate@Device@Video@@QEAAAEAV012@AEBV012@@Z 0x18000cb70 UNPORTED
    // ------------------------------------------------------------------------
    class RefreshRate {
    public:
        // ?GetNumerator@RefreshRate@Device@Video@@QEBAHXZ    0x18000d2d0  NOT PORTED
        int  GetNumerator() const;
        // ?GetDenominator@RefreshRate@Device@Video@@QEBAHXZ  0x18000d2e0  NOT PORTED
        int  GetDenominator() const;
        // ?GetIntRefresh@RefreshRate@Device@Video@@QEBAHXZ   0x18000d2f0  NOT PORTED
        int  GetIntRefresh() const;
        // ??8RefreshRate@Device@Video@@QEBA_NAEBV012@@Z      0x18000d2b0  NOT PORTED
        bool operator==(const RefreshRate& other) const;
        // ??MRefreshRate@Device@Video@@QEBA_NAEBV012@@Z      0x18000d280  NOT PORTED
        bool operator<(const RefreshRate& other) const;

        int32_t numerator;    // +0x00
        int32_t denominator;  // +0x04
    };

    // ------------------------------------------------------------------------
    // Video::Device::DisplayMode — 0x18 bytes
    // ??0DisplayMode@Device@Video@@QEAA@HH@Z (0x18000d300) writes width, height and
    // zeroes +0x08 and +0x10; ValidateConfiguration strides the mode list by 0x18
    // and reads +0x00/+0x04 as width/height, +0x08 as the refresh array data and
    // +0x10 as its count. That {data, size, capacity} triple is the standard
    // 16-byte Collection::Array (src/engine/collection/Collection.h).
    //
    // DOORS, NOT MEMBERS — and note retail exports NO DEFAULT CONSTRUCTOR, which
    // is why `video_mode_sort.cpp` may keep declaring an uninitialised swap
    // temporary:
    //   ??0DisplayMode@Device@Video@@QEAA@HH@Z              0x18000d300  UNPORTED
    //   ??0DisplayMode@Device@Video@@QEAA@AEBV012@@Z        0x18000cba0  UNPORTED
    //   ??1DisplayMode@Device@Video@@QEAA@XZ                0x18000cb80  UNPORTED
    //   ??4DisplayMode@Device@Video@@QEAAAEAV012@AEBV012@@Z 0x18000cbe0  UNPORTED
    // ------------------------------------------------------------------------
    class DisplayMode {
    public:
        // ?GetWidth@DisplayMode@Device@Video@@QEBAHXZ   0x18000d370  NOT PORTED
        int  GetWidth() const;
        // ?GetHeight@DisplayMode@Device@Video@@QEBAHXZ  0x18000d380  NOT PORTED
        int  GetHeight() const;
        // ?FindRefresh@DisplayMode@Device@Video@@QEBAHAEBVRefreshRate@23@@Z
        //                                               0x1800f6fe0  NOT PORTED
        int  FindRefresh(const RefreshRate& rate) const;
        // ?FindOrAddRefresh@DisplayMode@Device@Video@@QEAAXAEBVRefreshRate@23@@Z
        //                                               0x1800f6f90  NOT PORTED
        void FindOrAddRefresh(const RefreshRate& rate);
        // ??8DisplayMode@Device@Video@@QEBA_NAEBV012@@Z 0x18000d350  NOT PORTED
        bool operator==(const DisplayMode& other) const;
        // ??MDisplayMode@Device@Video@@QEBA_NAEBV012@@Z 0x18000d330  NOT PORTED
        bool operator<(const DisplayMode& other) const;

        int32_t      width;             // +0x00  ValidateConfiguration, ApplyChangedSettings
        int32_t      height;            // +0x04  ditto
        RefreshRate* refreshData;       // +0x08  Array<RefreshRate>::_data
        int32_t      refreshCount;      // +0x10  Array<RefreshRate>::_size
        int32_t      refreshCapacity;   // +0x14  Array<RefreshRate>::_capacity (inferred from the Array shape)
    };

    // ------------------------------------------------------------------------
    // Video::Device::Adapter — 0x38 bytes
    // ??0Adapter@Device@Video@@QEAA@XZ (0x1800f7100) zeroes +0x00..+0x27, writes
    // -1 as a qword over +0x28..+0x2f and -1 as a dword at +0x30. ValidateConfiguration
    // reads the mode array at +0x08/+0x10, indexes a byte array through +0x18, and
    // clamps its 8th out-parameter against the int at +0x28. The 0x38 stride is
    // ValidateConfiguration's `*param_2 * 0x38`.
    //
    // LIFECYCLE — REAL MEMBERS since ENDGAME B5 DEV-B (B5_RULES D6); the
    // bodies are in src/engine/video/device_enum_types.cpp:
    //   ??0Adapter@Device@Video@@QEAA@XZ                 0x1800f7100  Adapter()
    //   ??0Adapter@Device@Video@@QEAA@AEBU012@@Z         0x18000cc70  Adapter(const Adapter&)
    //   ??1Adapter@Device@Video@@QEAA@XZ                 0x18000cc10  ~Adapter()
    //   ??4Adapter@Device@Video@@QEAAAEAU012@AEBU012@@Z  0x18000cd00  UNPORTED (= delete)
    // The `name` member's own ctor / copy / dtor ARE the String share / release
    // retail inlines into those three bodies (System::String 0x180006760 /
    // 0x180006cb0 / 0x180006c80, src/engine/system/string.cpp). Every live
    // instance sits in raw MemAlloc'd storage (Device+0x40, GraphicsInterface
    // +0x08) or is one of dx11's enumeration locals; the array growers
    // placement-new the copy ctor and call `~Adapter()` explicitly.
    // ------------------------------------------------------------------------
    struct Adapter {
        Adapter();                                      // 0x1800f7100
        Adapter(const Adapter& other);                  // 0x18000cc70
        ~Adapter();                                     // 0x18000cc10
        // ??4Adapter@Device@Video@@QEAAAEAU012@AEBU012@@Z 0x18000cd00 is
        // UNPORTED: deleted, so nothing gets the implicit memberwise one.
        Adapter& operator=(const Adapter&) = delete;

        // ?FindDisplayMode@Adapter@Device@Video@@QEBAHAEBVDisplayMode@23@@Z
        //                                                0x1800f7310  NOT PORTED
        int  FindDisplayMode(const DisplayMode& mode) const;
        // ?FindOrAddMode@Adapter@Device@Video@@QEAAXAEBVDisplayMode@23@AEBVRefreshRate@23@@Z
        //                                                0x1800f7020  device_enum_types.cpp
        void FindOrAddMode(const DisplayMode& mode, const RefreshRate& rate);

        System::String name;            // +0x00  ctor zeroes it; the adapter's display name
        DisplayMode* modeData;          // +0x08  Array<DisplayMode>::_data       (ValidateConfiguration)
        int32_t      modeCount;         // +0x10  Array<DisplayMode>::_size       (ValidateConfiguration)
        int32_t      modeCapacity;      // +0x14  Array<DisplayMode>::_capacity   (inferred from the Array shape)
        uint8_t*     multisampleData;   // +0x18  Array<bool>::_data              (ValidateConfiguration: *(char*)(idx + adapter[0x18]))
        int32_t      multisampleCount;  // +0x20  Array<bool>::_size              (inferred; ctor zeroes)
        int32_t      multisampleCap;    // +0x24  Array<bool>::_capacity          (inferred; ctor zeroes)
        int32_t      maxFilterLevel;    // +0x28  ctor = -1; ValidateConfiguration clamps out-param 8 to it
        // +0x2C / +0x30 — MEANING RECOVERED by ENDGAME B.3.5 block D2, from the
        // OTHER side of the seam. Wave 6 lane B could only say "ctor = -1,
        // meaning not recovered" from the Runtime bodies, which merely copy the
        // pair into Device+0x14/+0x18. `src/render/dx11/devicedata.cpp`'s
        // `DeviceData_EnumerateInterfaces` is the WRITER, and it writes the DXGI
        // enumeration coordinates: `rec.dxgiAdapterIndex = adapterIndex` from
        // `IDXGIFactory1::EnumAdapters1`'s loop counter and `rec.dxgiOutputIndex =
        // outputIndex` from `IDXGIAdapter1::EnumOutputs`'s. The shipped plugin's
        // slot 1 did the same, and `Device::PlatformStartup`'s later
        // `adapter->EnumOutputs(Device+kOutputIndex)` reads the copy back — which
        // is what makes the pair a coordinate rather than a flag. The -1 default
        // is "no adapter/output chosen yet".
        int32_t      dxgiAdapterIndex;  // +0x2C  ctor = -1 (same qword store as +0x28)
        int32_t      dxgiOutputIndex;   // +0x30  ctor = -1
        int32_t      pad34;           // +0x34  not written by the ctor; tail padding or an unrecovered field
    };

    // ------------------------------------------------------------------------
    // Video::Device::GraphicsInterface — 0x18 bytes
    // ??0GraphicsInterface@Device@Video@@QEAA@XZ (0x18000cd90) zeroes exactly three
    // qwords. GetInterface (0x18000d3c0) strides by 0x18. ValidateConfiguration
    // reads the adapter array at +0x08/+0x10, and
    // src/game/Dialog/optionsdialog.cpp (PopulateVideoComboBoxes) shows `name`.
    //
    // LIFECYCLE — REAL MEMBERS since ENDGAME B5 DEV-B (bodies in
    // device_enum_types.cpp; see Adapter above for the String member):
    //   ??0GraphicsInterface@Device@Video@@QEAA@XZ                0x18000cd90  GraphicsInterface()
    //   ??0GraphicsInterface@Device@Video@@QEAA@AEBU012@@Z        0x18000ce10  GraphicsInterface(const GraphicsInterface&)
    //   ??1GraphicsInterface@Device@Video@@QEAA@XZ                0x18000cdc0  ~GraphicsInterface()
    //   ??4GraphicsInterface@Device@Video@@QEAAAEAU012@AEBU012@@Z 0x18000ce60  UNPORTED (= delete)
    //
    // It has no other exported member.
    // ------------------------------------------------------------------------
    struct GraphicsInterface {
        GraphicsInterface();                                        // 0x18000cd90
        GraphicsInterface(const GraphicsInterface& other);          // 0x18000ce10
        ~GraphicsInterface();                                       // 0x18000cdc0
        // 0x18000ce60 (operator=) is UNPORTED: deleted, as Adapter's.
        GraphicsInterface& operator=(const GraphicsInterface&) = delete;

        System::String name;      // +0x00  the renderer name shown in the combo
        Adapter* adapterData;     // +0x08  Array<Adapter>::_data
        int32_t  adapterCount;    // +0x10  Array<Adapter>::_size
        int32_t  adapterCapacity; // +0x14  Array<Adapter>::_capacity (inferred from the Array shape)
    };

    // ------------------------------------------------------------------------
    // One loaded renderer plugin DLL — 0x18 bytes, three of them inline at
    // Device+0xb0 (DX9, DX11, GL). No retail export names this type; it is our
    // name for the record Video::Device::InitializeInterfaces walks.
    //
    // This IS the struct src/app/video_interface.cpp writes into by hand;
    // see this header's banner. Video::Device::InitializeInterfaces (0x1800f79f0)
    // is the body that defines it: it tests the module word for zero, LoadLibraryW's
    // GetDLLForInterface(idx), then stores GetProcAddress("CreateDeviceData") at
    // +0x10 and GetProcAddress("GetDeviceName") at +0x08 — note the reversed order
    // relative to the field order.
    // ------------------------------------------------------------------------
    // B.4 (R-VIDEO), H4: the two entry points are typed as the functions they
    // are — `CreateVideoInterface` calls `createDeviceData(this)` and stores the
    // result as the DeviceData* (0x1800f79aa..0x1800f79b0); `getDeviceName` is
    // the plugin's name export, only ever tested for null here.
    using GetDeviceNameFn    = const char* (*)();
    using CreateDeviceDataFn = DeviceData* (*)(Device* device);
    struct InterfaceSlot {
        void*              module;            // +0x00  HMODULE. Only ever tested against zero and FreeLibrary'd.
        GetDeviceNameFn    getDeviceName;     // +0x08
        CreateDeviceDataFn createDeviceData;  // +0x10
    };

    // ========================================================================
    // Video::Device's own exported members, in alphabetical order (the order
    // B35_SLOTS.md lists them in), each with its retail address and the file its
    // body lives in. `NOT PORTED` means declared here and defined nowhere: a
    // caller gets a link error rather than a silent null call.
    // ========================================================================

    // ---- lifecycle / platform ----------------------------------------------
    // ?Startup@Device@Video@@QEAA_NXZ            0x1800f7270  device_startup.cpp
    bool Startup();
    // ?Shutdown@Device@Video@@QEAAXXZ            0x1800f7280  device_startup.cpp
    void Shutdown();
    // ?PlatformStartup@Device@Video@@AEAA_NXZ    0x1800f8260  device_startup.cpp
    bool PlatformStartup();
    // ?PlatformShutdown@Device@Video@@AEAAXXZ    0x1800f83f0  device_startup.cpp
    void PlatformShutdown();
    // ?PlatformConstruct@Device@Video@@AEAAXXZ   0x1800f81e0  NOT PORTED
    void PlatformConstruct();
    // ?PlatformDestruct@Device@Video@@AEAAXXZ    0x1800f8220  NOT PORTED
    void PlatformDestruct();

    // ---- the recursive render lock (RULE B5-6) -----------------------------
    // ?Lock@Device@Video@@QEAAXXZ                0x18000ca90  dynamic_resources.cpp
    // ACQUIRE / RELEASE: clang's thread-safety analysis (system/thread_annotations.h) checks that a
    // function pairs them. ext.render_pipe hands the held lock from the main thread to its render
    // thread, which the analysis cannot follow: those functions say so where they do it.
    void Lock() ACQUIRE(lock);
    // ?Unlock@Device@Video@@QEAAXXZ              0x18000cac0  dynamic_resources.cpp
    void Unlock() RELEASE(lock);
    // ?VerifyLock@Device@Video@@QEAAXXZ          0x18000cae0  NOT PORTED
    void VerifyLock();

    // ---- the device-lost / resize sweep ------------------------------------
    // ?RecreateDynamic@Device@Video@@QEAAXXZ     0x1800f80b0  dynamic_resources.cpp
    void RecreateDynamic();
    // ?ReleaseDynamic@Device@Video@@QEAAXXZ      0x1800f80e0  dynamic_resources.cpp
    void ReleaseDynamic();
    // ?NotifyBackgroundLoad@Device@Video@@QEAAX_N@Z 0x1800f8810  NOT PORTED
    void NotifyBackgroundLoad(bool loading);

    // ---- renderer interfaces ------------------------------------------------
    // ?InitializeInterfaces@Device@Video@@QEAAXXZ 0x1800f79f0  video_interfaces_win.cpp
    // RULE B5-9: it keeps its DELETED LoadLibraryW arm (retail VA
    // 0x1800f7a44..0x1800f7ac9). A real class must not "restore" the faithful
    // body — every shipped video plugin statically imports Runtime-x64.dll, and
    // loading one maps the DLL this exe is required never to map.
    void InitializeInterfaces();
    // ?GetDLLForInterface@Device@Video@@QEBA?AVString@System@@H@Z  0x1800f7b20
    //   video_interfaces_win.cpp. Returns `System::String` BY VALUE — a hidden
    //   retslot door (B35_RULES §2.5); `this` is spilled at entry and never read.
    System::String GetDLLForInterface(int index) const;
    // ?CreateVideoInterface@Device@Video@@QEAA_NH@Z 0x1800f7930  device_startup.cpp
    bool CreateVideoInterface(int index);
    // ?GetInterfaceCount@Device@Video@@QEBAHXZ   0x18000d3b0  device.cpp
    int  GetInterfaceCount() const;
    // ?GetInterface@Device@Video@@QEBAAEBUGraphicsInterface@12@H@Z 0x18000d3c0  device.cpp
    const GraphicsInterface& GetInterface(int index) const;
    // ?GetInterfaceIndex@Device@Video@@QEBAHXZ   0x18000cb30  NOT PORTED
    int  GetInterfaceIndex() const;
    // ?GetInterfaceName@Device@Video@@QEBAPEBDH@Z 0x18000cb60  NOT PORTED
    const char* GetInterfaceName(int index) const;
    // ?IsInterfaceAvailable@Device@Video@@QEBA_NH@Z 0x18000cb40  NOT PORTED
    bool IsInterfaceAvailable(int index) const;

    // ---- display modes ------------------------------------------------------
    // ?EnumerateVideoModes@Device@Video@@QEAA_NXZ 0x1800f8610  enumerate_video_modes.cpp
    bool EnumerateVideoModes();
    // ?ReleaseVideoModes@Device@Video@@QEAAXXZ   0x1800f87b0  NOT PORTED
    void ReleaseVideoModes();
    // ?GetDisplayMode@Device@Video@@QEBA?AVDisplayMode@12@H@Z 0x18000d3e0
    //   device_enum_types.cpp. Returns `DisplayMode` BY VALUE (hidden retslot),
    //   and the returned mode is a DEEP copy — the caller owns its refresh list.
    DisplayMode GetDisplayMode(int index) const;
    // ?GetDisplayModeCount@Device@Video@@QEBAHXZ 0x18000d3d0  NOT PORTED
    int  GetDisplayModeCount() const;
    // ?GetCurrentDisplayMode@Device@Video@@QEBAHXZ 0x18000d440  NOT PORTED
    int  GetCurrentDisplayMode() const;
    // ?GetDefaultResolution@Device@Video@@QEBAHAEBUAdapter@12@@Z 0x1800f7540
    //   device_startup.cpp (member) -> device_window.cpp (EnumDisplaySettingsW)
    int  GetDefaultResolution(const Adapter& adapter) const;
    // ?GetDefaultRefreshRate@Device@Video@@QEAAHAEBVDisplayMode@12@H@Z 0x1800f7600  NOT PORTED
    int  GetDefaultRefreshRate(const DisplayMode& mode, int hz);
    // ?AdjustDisplay@Device@Video@@QEAAXXZ       0x1800f7e20
    //   device_enum_types.cpp (member) -> device_adjustdisplay.cpp
    void AdjustDisplay();
    // ?LogDisplay@Device@Video@@QEBAXXZ          0x1800f7640  NOT PORTED
    void LogDisplay() const;
    // ?CompareDisplayMode@Device@Video@@CAHPEBX0@Z 0x1800f7cf0  NOT PORTED
    //   (`CA` — a PRIVATE static, retail's qsort comparator.)
    static int CompareDisplayMode(const void* a, const void* b);

    // ---- configuration ------------------------------------------------------
    // ?ValidateConfiguration@Device@Video@@QEAAXAEAH0000000@Z 0x1800f7370  device.cpp
    //   The EIGHT-int& out-parameter overload. THE DOOR DECIDES THE OVERLOAD:
    //   this one is registered from device.cpp, the no-argument one from
    //   device_startup.cpp, and the two ordinals are adjacent (4532 / 4533).
    void ValidateConfiguration(int& renderer, int& adapterIdx,
                               int& width, int& height,
                               int& refreshNum, int& refreshDen,
                               int& antialias, int& filter);
    // ?ValidateConfiguration@Device@Video@@QEAA_NXZ 0x1800f8830  device_startup.cpp
    bool ValidateConfiguration();
    // ?ApplyChangedSettings@Device@Video@@QEAA_NXZ 0x1800f8110  device.cpp
    bool ApplyChangedSettings();
    // ?NeedsRestart@Device@Video@@QEAA_NXZ       0x18000cb00  NOT PORTED
    bool NeedsRestart();

    // ---- multisampling / capabilities ---------------------------------------
    // ?GetMultisampleModeCount@Device@Video@@QEBAHXZ 0x18000d450  device.cpp
    int  GetMultisampleModeCount() const;
    // ?IsMultisampleModeSupported@Device@Video@@QEBA_NH@Z 0x18000d460  device.cpp
    bool IsMultisampleModeSupported(int mode) const;
    // ?GetMaxAnisotropy@Device@Video@@QEBAHXZ    0x18000cb10  NOT PORTED
    int  GetMaxAnisotropy() const;
    // ?RequiresColorTarget@Device@Video@@QEBA_NXZ 0x1800f81a0  NOT PORTED
    bool RequiresColorTarget() const;
    // ?UsesZeroToOneClipSpace@Device@Video@@QEBA_NXZ 0x1800f8180  NOT PORTED
    bool UsesZeroToOneClipSpace() const;
    // ?GetCenterPixelOffset@Device@Video@@QEBAMXZ 0x1800f81c0  NOT PORTED
    float GetCenterPixelOffset() const;

    // ---- window / back buffer -----------------------------------------------
    // ?GetWidth@Device@Video@@QEBAHXZ            0x18000d390  NOT PORTED
    int  GetWidth() const;
    // ?GetHeight@Device@Video@@QEBAHXZ           0x18000d3a0  NOT PORTED
    int  GetHeight() const;
    // ?GetWindow@Device@Video@@QEAAPEAUHWND__@@XZ 0x18000caf0  NOT PORTED
    //   (declared `void*` rather than `HWND`: this header stays <windows.h>-free.)
    void* GetWindow();
    // ?SizeWindow@Device@Video@@SAHPEAUHWND__@@I_K_J@Z 0x1800f7d20
    //   device_startup.cpp (member) -> device_window.cpp. A WM_SIZE
    //   callback whose ADDRESS PlatformStartup hands to the Application.
    static int SizeWindow(void* hwnd, unsigned msg, uint64_t wparam, int64_t lparam);
    // ?MoveWindow@Device@Video@@SAHPEAUHWND__@@I_K_J@Z 0x1800f7d90  ditto (WM_EXITSIZEMOVE)
    static int MoveWindow(void* hwnd, unsigned msg, uint64_t wparam, int64_t lparam);
    // ?SetCursor@Device@Video@@QEAAXV?$ExternalPtr@VImageBuffer@Video@@@Object@@HH@Z
    //                                            0x1800f7350  NOT PORTED
    void SetCursor(Object::ExternalPtr<ImageBuffer> image, int hotX, int hotY);
    // ?SetVideoErrorFuncs@Device@Video@@QEAAXP6AXXZ0@Z 0x1800f7150  NOT PORTED
    void SetVideoErrorFuncs(void (*onLost)(), void (*onRestored)());

    // ---- the companion, and the eleven factory forwarders -------------------
    // Each of the eleven `Create*Data` members is a one-line forward into a
    // `Video::DeviceData` vtable slot by BYTE OFFSET (B35_RULES §1, and
    // CompanionData.h tabulates the slot per factory). None is ported: our tree
    // reaches those slots directly, by offset, from the owner's constructor.
    // ?GetData@Device@Video@@QEAAPEAVDeviceData@2@XZ 0x18000cb20  NOT PORTED
    DeviceData* GetData();
    // ?CreateContextData@Device@Video@@...        0x1800f85d0  NOT PORTED  (slot  9, +0x48)
    ContextData*        CreateContextData(Context* owner);
    // ?CreateImageBufferData@Device@Video@@...    0x1800f85b0  NOT PORTED  (slot 10, +0x50)
    ImageBufferData*    CreateImageBufferData(ImageBuffer* owner);
    // ?CreateConstantBufferData@Device@Video@@... 0x1800f85f0  NOT PORTED  (slot 11, +0x58)
    ConstantBufferData* CreateConstantBufferData(ConstantBuffer* owner);
    // ?CreateIndexBufferData@Device@Video@@...    0x1800f8590  NOT PORTED  (slot 12, +0x60)
    IndexBufferData*    CreateIndexBufferData(IndexBuffer* owner);
    // ?CreateVertexBufferData@Device@Video@@...   0x1800f84f0  NOT PORTED  (slot 13, +0x68)
    VertexBufferData*   CreateVertexBufferData(VertexBuffer* owner);
    // ?CreateVertexLayoutData@Device@Video@@...   0x1800f84d0  NOT PORTED  (slot 14, +0x70)
    VertexLayoutData*   CreateVertexLayoutData(VertexLayout* owner);
    // ?CreateVertexProgramData@Device@Video@@...  0x1800f84b0  NOT PORTED  (slot 15, +0x78)
    VertexProgramData*  CreateVertexProgramData(VertexProgram* owner);
    // ?CreatePixelProgramData@Device@Video@@...   0x1800f8570  NOT PORTED  (slot 16, +0x80)
    PixelProgramData*   CreatePixelProgramData(PixelProgram* owner);
    // ?CreateRenderStateData@Device@Video@@...    0x1800f8510  NOT PORTED  (slot 17, +0x88)
    RenderStateData*    CreateRenderStateData(RenderState* owner);
    // ?CreateVideoProgramData@Device@Video@@...   0x1800f8530  NOT PORTED  (slot 18, +0x90)
    VideoProgramData*   CreateVideoProgramData(VideoProgram* owner);
    // ?CreateRenderBufferData@Device@Video@@...   0x1800f8550  NOT PORTED  (slot 19, +0x98)
    RenderBufferData*   CreateRenderBufferData(RenderBuffer* owner);

    // ---- the singleton accessor ---------------------------------------------
    // ?Get@Device@Video@@SAPEAV12@XZ             0x18000d260  NOT PORTED
    //   Reads `_singleton` below.
    static Device* Get();

    // ?_singleton@Device@Video@@0PEAV12@EA (0x1801fe7a8) — the class static
    // (ENDGAME B.3.9 VID, B39_RULES D7); its minted row is RETIRED (B.3.9
    // close): no name or address door is left. Written by the ctor door
    // (singleton_ctors.cpp, type 0 only) and cleared by ~Device. PUBLIC:
    // foreign readers (dx11, UI, Graphics) read it directly.
    static Device* _singleton;

    // EXT SEAM (NOT RETAIL, src/ext/render_backend/render_backend.h): PlatformStartup's loop, after slot
    // `index`'s CreateDevice failed and its DeviceData was torn down, asks whether to try the same slot
    // again (the dual exe's second backend behind slot 1, selected by the call). Null by default: retail
    // steps to the next slot. A class static, so the layout below is untouched.
    static bool (*_extInterfaceRetry)(int index);

    // ========================================================================
    // Data — retail layout, unchanged from Wave 6 lane B.
    // ========================================================================
    int32_t           type;                // +0x00  DeviceType ctor argument; 0 == the singleton
    int32_t           backBufferWidth;     // +0x04  ApplyChangedSettings writes; RenderBuffer::Create reads
    int32_t           backBufferHeight;    // +0x08  ApplyChangedSettings writes; RenderBuffer::Create reads
    int32_t           displayModeIndex;    // +0x0C  ApplyChangedSettings writes on a width/height match
    // +0x10..+0x1B named by Wave 9 lane VID2 from
    // ?ValidateConfiguration@Device@Video@@QEAA_NXZ (0x1800f8830), the only
    // writer of all three; it writes them together at 0x1800f8b23..0x1800f8b32:
    //     [this+0x14] = adapter->dxgiAdapterIndex   (0x1800f8b26)
    //     [this+0x18] = adapter->dxgiOutputIndex    (0x1800f8b2c)
    //     [this+0x0c] = the matched display-mode index   (0x1800f8b2f)
    //     [this+0x10] = the matched refresh-rate index   (0x1800f8b32)
    // The two adapter fields were "meaning not recovered" until B.3.5 block D2
    // read the WRITER on dx11's side — see Adapter::dxgiAdapterIndex /
    // dxgiOutputIndex above. `src/render/dx11/devicedata.cpp` reads THESE two copies
    // back (its `Dev::kOutputIndex` is +0x18) to re-find the output it enumerated.
    int32_t           refreshRateIndex;    // +0x10  index into the chosen mode's refresh list, or -1
    int32_t           dxgiAdapterIndex;    // +0x14  copied verbatim from Adapter+0x2C
    int32_t           dxgiOutputIndex;     // +0x18  copied verbatim from Adapter+0x30
    int32_t           pad1C;               // +0x1C  ctor-zeroed; no writer found
    DisplayMode*      modeData;            // +0x20  Array<DisplayMode>::_data      (ApplyChangedSettings)
    int32_t           modeCount;           // +0x28  Array<DisplayMode>::_size      (ApplyChangedSettings)
    int32_t           modeCapacity;        // +0x2C  Array<DisplayMode>::_capacity  (inferred from the Array shape)
    uint8_t*          multisampleData;     // +0x30  Array<bool>::_data             (IsMultisampleModeSupported)
    int32_t           multisampleCount;    // +0x38  Array<bool>::_size             (GetMultisampleModeCount)
    int32_t           multisampleCap;      // +0x3C  Array<bool>::_capacity         (inferred from the Array shape)
    GraphicsInterface* interfaceData;      // +0x40  Array<GraphicsInterface>::_data (GetInterface)
    int32_t           interfaceCount;      // +0x48  Array<GraphicsInterface>::_size (GetInterfaceCount)
    int32_t           interfaceCapacity;   // +0x4C  Array<GraphicsInterface>::_capacity (inferred)
    // B.4 (X-RL): the two video callbacks the profile build's
    // `Video::Device::SetVideoErrorFuncs` (0x1800fd860, inlined by retail into
    // Engine::MainLoop) stores: [0] error +0x50, [1] restart +0x58. The ctor
    // zeroes both (singleton_ctors.cpp: two nullptr stores = retail's two qword
    // stores). B5 CTX (B5_RULES C4): named;
    // written by Engine (engine.cpp), read by dx11's ContextData::Finish either
    // side of a device reset (0x18000183c / 0x18000187d, both null-checked).
    void            (*onVideoError)();     // +0x50
    void            (*onVideoRestart)();   // +0x58
    Platform::LockStorage lock;            // +0x60  CRITICAL_SECTION (InitializeCriticalSection + spin 5000)
                                           //        RULE B5-6: opaque, never a std::mutex;
                                           //        built in place by Platform::MutexInit.
    uint32_t          lockOwnerThreadId GUARDED_BY(lock);    // +0x88  RenderBuffer::PlatformConstruct / ~RenderBuffer
    int32_t           lockRecursionCount GUARDED_BY(lock);   // +0x8C  ditto
    void*             windowHandle;        // +0x90  HWND, copied from Application::_singleton[0] by Startup
    int32_t           maxAnisotropy;       // +0x98  max anisotropic filter level; read by
                                           //        src/game/Dialog/optionsdialog.cpp:359 as
                                           //        `*(int*)(vidDev + 0x98)`. Not written by the
                                           //        ctor — adapter enumeration fills it.
    uint8_t           settingsChanged;     // +0x9C  ApplyChangedSettings sets it to 1
    uint8_t           adjustingDisplay;             // +0x9D  ctor zeroes it in the same 2-byte store
    uint8_t           pad9E[2];            // +0x9E..+0x9F
    void*             monitor;             // +0xA0  HMONITOR: AdjustDisplay hands it to GetMonitorInfoW
                                           //        (0x1800f7e68); the ctor zeroes it
    uint8_t           unusedMonitorPair[8];            // +0xA8  the ctor zeroes it; meaning not recovered
    InterfaceSlot     dllSlots[3];         // +0xB0  DX9, DX11, GL — src/app/video_interface.cpp writes slot 1 here
    int32_t           activeInterface;     // +0xF8  ctor = -1; Startup copies it back into Configuration::interfaceIndex
    int32_t           padFC;               // +0xFC
    DeviceData*       deviceData;          // +0x100 src/render/dx11/ reads this at exactly this offset
};

// ----------------------------------------------------------------------------
// Video::Configuration — the video settings singleton (0x1801fe7a0)
//
// Every offset is pinned twice: by ??0Configuration@Video@@QEAA@XZ (0x1800f6f10),
// which gives each field its default, and by Video::Configuration::Save
// (0x1800f6d10), which gives each field its persisted NAME. The two agree.
//
// `sizeof` — B35_RULES §13 item 1 asked this block to close the gap by finding
// "an allocation site or a MOV EDX in the retail DLL". MEASURED THIS PASS, and
// the answer is that NO SUCH INSTRUMENT EXISTS:
//   * `get_xrefs_to(0x1800f6f10)` — Ghidra's exhaustive index of direct
//     CALL/JMP references — returns exactly two referencers and NEITHER is a
//     call: the module entry point, and a 4-byte RVA `10 6F 0F 00` at
//     `0x1801b0c0c`. That RVA is NOT a static-initializer table entry, which was
//     this pass's first reading and is wrong: the 96 bytes around it
//     (0x1801b0bf0..0x1801b0c4f) are an unbroken run of unrelated `.text` RVAs
//     with no paired `this` column, i.e. a Control Flow Guard
//     `__guard_fids_table`, which lists an address merely because something
//     takes it as an indirect-call target. So there is NO `LEA RCX,[X]` + CALL
//     thunk anywhere in `.text`, the constructor is only ever reached
//     indirectly, and no static storage address X can be recovered statically to
//     bound the size against the next symbol.
//   * the ~60 referencers of the singleton cell 0x1801fe7a0 write it in exactly
//     two places, `0x1800f6f6b` inside the ctor (`MOV [0x1801fe7a0],RCX` — the
//     ctor storing its own `this`) and `0x1800f6f80` inside the dtor
//     (`MOV [0x1801fe7a0],0`). Neither allocates.
//   * `?Shutdown@Configuration@Video@@` (0x1800f6670) is one instruction,
//     `JMP 0x1800f6d10` — a tail jump into `Save`. It frees nothing, so there is
//     no `operator delete` size either.
//   * the ctor's own last store is `MOV byte ptr [RCX+0x42],0x0` at
//     `0x1800f6f64`, and no body anywhere in the DLL (`Save`,
//     `ValidateConfiguration`, `ApplyChangedSettings` were all read) touches a
//     byte above +0x42.
// So the class is at least 0x43 bytes, 0x44 with natural 4-byte alignment, and
// 0x44 stays an INFERENCE — a `static_assert(sizeof == 0x44)` here would be an
// assertion that cannot fail for the right reason, which is exactly the shape
// B35_RULES §1.3 calls a false witness. What IS asserted is every offset, that
// the class is at least 0x43 bytes, and that it did not grow past 0x44.
// `tests/unit/video_device_abi_test.cpp` restates the bound at runtime.
//
// LIFECYCLE EXPORTS — DOORS, NOT MEMBERS (see the header banner):
//   ??0Configuration@Video@@QEAA@XZ                 0x1800f6f10  UNPORTED
//   ??1Configuration@Video@@QEAA@XZ                 0x1800f6f80  Video_Configuration_Dtor
//                                                                (video_resources.cpp)
//   ??4Configuration@Video@@QEAAAEAV01@AEBV01@@Z    0x18000d520  UNPORTED
//
// STATIC DATA — `?_singleton@Configuration@Video@@0PEAV12@EA` @ 0x1801fe7a0
// is the class static `Configuration::_singleton` (ENDGAME B.3.9 VID, B39_RULES
// D7): the minted row is redirected onto it.
// ----------------------------------------------------------------------------
class Configuration {
public:
    // ?Startup@Configuration@Video@@QEAA_NXZ   0x1800f6680  device.cpp
    bool Startup();
    // ?Shutdown@Configuration@Video@@QEAAXXZ   0x1800f6670  NOT PORTED
    //   (retail: `JMP Save`. Declared, not defined — nothing calls it.)
    void Shutdown();
    // ?Save@Configuration@Video@@QEAAXXZ       0x1800f6d10  device.cpp
    void Save();

    // ?Get@Configuration@Video@@SAPEAV12@XZ    0x18000d590  NOT PORTED
    static Configuration* Get();

    // ?_singleton@Configuration@Video@@0PEAV12@EA (0x1801fe7a0) — the class
    // static (ENDGAME B.3.9 VID, B39_RULES D7). RtVideoPortInit redirects the
    // minted storage row onto it, so the name door, the address door and this
    // member are the same bytes. Written by the ctor door (singleton_ctors.cpp)
    // and cleared by Engine's platform shutdown. PUBLIC while foreign readers
    // remain.
    static Configuration* _singleton;

    // ---- the eighteen accessor pairs, 0x18000d5a0..0x18000d7f0 -------------
    // Sixteen-byte stubs, each a single `MOV EAX,[RCX+off]` / `MOVZX EAX,byte
    // [RCX+off]` / `MOV [RCX+off],EDX`. None is ported: every reader in this
    // tree touches the field, and an uncalled port is an unverified port. They
    // are declared so a caller batch can convert to a member call without
    // editing this header, and they are the THIRD independent pin on all
    // eighteen offsets (Save's key names and the ctor's defaults are the other
    // two; all three agree).
    bool GetShowSettings() const;      // 0x18000d5a0   NOT PORTED
    int  GetInterface() const;         // 0x18000d5b0   NOT PORTED
    int  GetAdapter() const;           // 0x18000d5c0   NOT PORTED
    int  GetWidth() const;             // 0x18000d5d0   NOT PORTED
    int  GetHeight() const;            // 0x18000d5e0   NOT PORTED
    int  GetWindowX() const;           // 0x18000d5f0   NOT PORTED
    int  GetWindowY() const;           // 0x18000d600   NOT PORTED
    int  GetWindowWidth() const;       // 0x18000d610   NOT PORTED
    int  GetWindowHeight() const;      // 0x18000d620   NOT PORTED
    int  GetMultisample() const;       // 0x18000d630   NOT PORTED
    bool IsFullscreen() const;         // 0x18000d640   NOT PORTED
    int  GetRefreshNumerator() const;  // 0x18000d650   NOT PORTED
    int  GetRefreshDenominator() const;// 0x18000d660   NOT PORTED
    bool IsVSync() const;              // 0x18000d670   NOT PORTED
    int  GetTextureFilter() const;     // 0x18000d680   NOT PORTED
    int  GetShadowMapScalar() const;   // 0x18000d690   NOT PORTED
    int  GetShaderDetail() const;      // 0x18000d6a0   NOT PORTED
    int  GetReflectionDetail() const;  // 0x18000d6b0   NOT PORTED
    // 0x18000d6c0 names +0x42, which this header calls `hideWindow`; Startup does
    // not read it and Save does not write it.
    bool HideWindow() const;           // 0x18000d6c0   NOT PORTED

    void SetShowSettings(bool v);      // 0x18000d6d0   NOT PORTED
    void SetInterface(int v);          // 0x18000d6e0   NOT PORTED
    void SetAdapter(int v);            // 0x18000d6f0   NOT PORTED
    void SetWidth(int v);              // 0x18000d700   NOT PORTED
    void SetHeight(int v);             // 0x18000d710   NOT PORTED
    void SetWindowX(int v);            // 0x18000d720   NOT PORTED
    void SetWindowY(int v);            // 0x18000d730   NOT PORTED
    void SetWindowWidth(int v);        // 0x18000d740   NOT PORTED
    void SetWindowHeight(int v);       // 0x18000d750   NOT PORTED
    void SetMultisample(int v);        // 0x18000d760   NOT PORTED
    void SetFullscreen(bool v);        // 0x18000d770   NOT PORTED
    void SetRefreshNumerator(int v);   // 0x18000d780   NOT PORTED
    void SetRefreshDenominator(int v); // 0x18000d790   NOT PORTED
    void SetVSync(bool v);             // 0x18000d7a0   NOT PORTED
    void SetTextureFilter(int v);      // 0x18000d7b0   NOT PORTED
    void SetShadowMapScalar(int v);    // 0x18000d7c0   NOT PORTED
    void SetShaderDetail(int v);       // 0x18000d7d0   NOT PORTED
    void SetReflectionDetail(int v);   // 0x18000d7e0   NOT PORTED
    void SetHideWindow(bool v);        // 0x18000d7f0   NOT PORTED

    // ---- data ---------------------------------------------------------------
    uint8_t  settingsValid;     // +0x00  ctor = 1; Save -> "Settings"          (bool)
    uint8_t  pad01[3];          // +0x01
    int32_t  interfaceIndex;    // +0x04  ctor = 1; Save -> "Interface"
    int32_t  adapterIndex;      // +0x08  ctor = 0; Save -> "Adapter"
    int32_t  width;             // +0x0C  ctor = -1; Save -> "Width"
    int32_t  height;            // +0x10  ctor = -1; Save -> "Height"
    int32_t  windowX;           // +0x14  ctor = -1; Save -> "WindowX"
    int32_t  windowY;           // +0x18  ctor = -1; Save -> "WindowY"
    int32_t  windowWidth;       // +0x1C  ctor = -1; Save -> "WindowWidth"
    int32_t  windowHeight;      // +0x20  ctor = -1; Save -> "WindowHeight"
    int32_t  multisample;       // +0x24  ctor = 1;  Save -> "Multisample"
    int32_t  textureFilter;     // +0x28  ctor = 0;  Save -> "TextureFilter"
    int32_t  shadowScalar;      // +0x2C  ctor = 1;  Save -> "ShadowScalar"; RenderBuffer::Create shifts by it
    int32_t  shaderDetail;      // +0x30  ctor = 2;  Save -> "ShaderDetail"
    int32_t  reflectionDetail;  // +0x34  ctor = 1;  Save -> "ReflectionDetail"
    int32_t  refreshNumerator;  // +0x38  ctor = 60; Save -> "RefreshN"
    int32_t  refreshDenominator;// +0x3C  ctor = 1;  Save -> "RefreshD"
    uint8_t  fullscreen;        // +0x40  ctor = 1;  Save -> "Fullscreen"      (bool)
    uint8_t  vsync;             // +0x41  ctor = 1;  Save -> "VSync"           (bool)
    uint8_t  hideWindow;           // +0x42  ctor = 0;  not persisted — the ctor's LAST store
};

// ----------------------------------------------------------------------------
// Video::RenderBuffer — 0x30 bytes
//
// The ctor (0x1800f3ee0) zeroes +0x00..+0x17 and calls PlatformConstruct
// (0x1800f4320), which fills the intrusive list links and the platform object.
// Create (0x1800f3f10) writes +0x00, +0x08, +0x10 and +0x14 then tail-calls
// slot 1 of the object at +0x28. The size is confirmed independently by
// src/game/Game/minimap_paint.cpp:193, which MemAllocs exactly 0x30 bytes for
// one and then calls the retail ctor on it.
//
// +0x18/+0x20 are next/prev of a CIRCULAR doubly linked list of every live
// RenderBuffer, whose head is `?_firstRenderBuffer@RenderBuffer@Video@@`
// (0x1801fe790, an `RT_DATA_SLOT` read — RULE B4b-4 / B35_RULES §9, three
// by-name doors). Maintaining that list is exactly why the ctor and the dtor
// stay DOORS and this class gets no real ctor/dtor.
//
// B.3.5 BLOCK D2, RULE B5-2.3: this class is now the SINGLE definition of that
// layout. `src/render/dx11/dx11_backend_access.h`'s `RenderBufferOwner` — which pinned
// only colour/depth and carried `static_assert(sizeof(RenderBufferOwner)==0x10)`
// on a 0x30-byte class, a size assert on a prefix view that cannot fail for the
// right reason — is deleted and its users include this header instead.
// `Context.h`'s `RenderBufferPrefix` is b35-D1's file and is theirs to collapse.
//
// LIFECYCLE EXPORTS — DOORS, NOT MEMBERS:
//   ??0RenderBuffer@Video@@QEAA@XZ                0x1800f3ee0  Video_RenderBuffer_Ctor
//   ??1RenderBuffer@Video@@QEAA@XZ                0x1800f4020  Video_RenderBuffer_Dtor
//                                                 (both video_resources.cpp)
//   ??4RenderBuffer@Video@@QEAAAEAV01@AEBV01@@Z   0x18001f3d0  UNPORTED
//   ??2RenderBuffer@Video@@SAPEAX_K@Z             0x18001f2e0  UNPORTED
//   ??2RenderBuffer@Video@@SAPEAX_KPEAX@Z         0x18001f350  UNPORTED
//   ??3RenderBuffer@Video@@SAXPEAX@Z              0x18001f360  UNPORTED
// ----------------------------------------------------------------------------
class RenderBuffer {
public:
    // ?Create@RenderBuffer@Video@@QEAA_NPEBVImageBuffer@2@0@Z 0x1800f3f10  device.cpp
    bool Create(const ImageBuffer* color, const ImageBuffer* depth);
    // ?Release@RenderBuffer@Video@@QEAAXXZ        0x1800f42d0  NOT PORTED
    void Release();
    // ?Recreate@RenderBuffer@Video@@QEAAXXZ       0x1800f4170  dynamic_resources.cpp
    void Recreate();
    // ?Bind@RenderBuffer@Video@@QEBAXAEAVContext@2@@Z 0x1800f4300  video_resources.cpp
    void Bind(Context& context) const;
    // ?Resolve@RenderBuffer@Video@@QEBAXAEAVContext@2@PEAV12@_N@Z 0x1800f4510  NOT PORTED
    void Resolve(Context& context, RenderBuffer* other, bool flag) const;

    // ?Width@RenderBuffer@Video@@QEBAHXZ          0x18001f410  NOT PORTED
    int  Width() const;
    // ?Height@RenderBuffer@Video@@QEBAHXZ         0x18001f420  NOT PORTED
    int  Height() const;
    // ?ColorBuffer@RenderBuffer@Video@@QEBAPEBVImageBuffer@2@XZ 0x18001f430  NOT PORTED
    const ImageBuffer* ColorBuffer() const;
    // ?DepthBuffer@RenderBuffer@Video@@QEBAPEBVImageBuffer@2@XZ 0x18001f440  NOT PORTED
    const ImageBuffer* DepthBuffer() const;
    // ?GetData@RenderBuffer@Video@@QEBAPEBVRenderBufferData@2@XZ 0x18001f3c0  NOT PORTED
    const RenderBufferData* GetData() const;

    // ---- the two class-static list sweeps (RULE B5-6's lock) ---------------
    // ?RecreateDynamic@RenderBuffer@Video@@SAXXZ  0x1800f4030  dynamic_resources.cpp
    static void RecreateDynamic();
    // ?ReleaseDynamic@RenderBuffer@Video@@SAXXZ   0x1800f40c0  dynamic_resources.cpp
    static void ReleaseDynamic();
    // ?_firstRenderBuffer@RenderBuffer@Video@@0PEAV12@EA (0x1801fe790) — the
    // list head (ENDGAME B.3.9 VID class static, B39_RULES D7;
    // dynamic_resources.cpp).
    static RenderBuffer* _firstRenderBuffer;

    // ---- the two class allocation operators --------------------------------
    // ENDGAME B.3.5 block VC. Both are STATIC members and neither changes a
    // triviality trait, so the `is_trivially_copyable<RenderBuffer>` assert below
    // still holds (and is what proves it). The mangled names are what attribute
    // them to this class rather than a name prefix.
    //   ??2RenderBuffer@Video@@SAPEAX_K@Z  0x18001f2e0  context_ctor.cpp
    static void* operator new(size_t size);
    //   ??3RenderBuffer@Video@@SAXPEAX@Z   0x18001f360  context_lifecycle.cpp
    static void  operator delete(void* p);
    //   ??2RenderBuffer@Video@@SAPEAX_KPEAX@Z 0x18001f350 — NOT PORTED
    static void* operator new(size_t size, void* storage);

    // ---- private platform half; DOORS in buffers.cpp -----------------------
    // ?PlatformConstruct@RenderBuffer@Video@@AEAAXXZ 0x1800f4320  buffers.cpp
    void PlatformConstruct();
    // ?PlatformDestruct@RenderBuffer@Video@@AEAAXXZ  0x1800f4400  buffers.cpp
    void PlatformDestruct();
    // ?PlatformCreate@RenderBuffer@Video@@AEAA_NXZ   0x1800f44f0  NOT PORTED
    bool PlatformCreate();

    // ---- data ---------------------------------------------------------------
    // B.4 (R-VIDEO), H4: the pointer members carry their retail types (the
    // accessors above and `Create`'s mangled parameters name them).
    const ImageBuffer* colorBuffer;   // +0x00  (Create writes)
    const ImageBuffer* depthBuffer;   // +0x08  (Create writes)
    int32_t  width;         // +0x10  (Create writes)
    int32_t  height;        // +0x14  (Create writes)
    RenderBuffer*      next;          // +0x18  PlatformConstruct / ~RenderBuffer
    RenderBuffer*      prev;          // +0x20  PlatformConstruct / ~RenderBuffer
    RenderBufferData*  platformData;  // +0x28  Create tail-calls its vtable slot 1
};

}  // namespace Video

// ============================================================================
// LAYOUT ASSERTIONS — after the classes, because `offsetof` needs a complete
// type. Every assert that was here before B.3.5 is kept verbatim; `sizeof` and
// `alignof` are added per the step-5 brief.
// ============================================================================

// ---- Video::Device::RefreshRate ----
static_assert(sizeof(Video::Device::RefreshRate) == 0x08, "RefreshRate is 8 bytes");
static_assert(alignof(Video::Device::RefreshRate) == 4, "RefreshRate aligns to 4");
static_assert(offsetof(Video::Device::RefreshRate, numerator) == 0x00, "RefreshRate::numerator");
static_assert(offsetof(Video::Device::RefreshRate, denominator) == 0x04, "RefreshRate::denominator");

// ---- Video::Device::DisplayMode ----
static_assert(sizeof(Video::Device::DisplayMode) == 0x18, "DisplayMode is 0x18 bytes (GetInterface/ValidateConfiguration stride)");
static_assert(alignof(Video::Device::DisplayMode) == 8, "DisplayMode aligns to 8");
static_assert(offsetof(Video::Device::DisplayMode, width) == 0x00, "DisplayMode::width");
static_assert(offsetof(Video::Device::DisplayMode, height) == 0x04, "DisplayMode::height");
static_assert(offsetof(Video::Device::DisplayMode, refreshData) == 0x08, "DisplayMode::refreshData");
static_assert(offsetof(Video::Device::DisplayMode, refreshCount) == 0x10, "DisplayMode::refreshCount");
static_assert(offsetof(Video::Device::DisplayMode, refreshCapacity) == 0x14, "DisplayMode::refreshCapacity");

// ---- Video::Device::Adapter ----
static_assert(sizeof(Video::Device::Adapter) == 0x38, "Adapter is 0x38 bytes (ValidateConfiguration stride)");
static_assert(alignof(Video::Device::Adapter) == 8, "Adapter aligns to 8");
static_assert(offsetof(Video::Device::Adapter, name) == 0x00, "Adapter::name");
static_assert(offsetof(Video::Device::Adapter, modeData) == 0x08, "Adapter::modeData");
static_assert(offsetof(Video::Device::Adapter, modeCount) == 0x10, "Adapter::modeCount");
static_assert(offsetof(Video::Device::Adapter, modeCapacity) == 0x14, "Adapter::modeCapacity");
static_assert(offsetof(Video::Device::Adapter, multisampleData) == 0x18, "Adapter::multisampleData");
static_assert(offsetof(Video::Device::Adapter, multisampleCount) == 0x20, "Adapter::multisampleCount");
static_assert(offsetof(Video::Device::Adapter, multisampleCap) == 0x24, "Adapter::multisampleCap");
static_assert(offsetof(Video::Device::Adapter, maxFilterLevel) == 0x28, "Adapter::maxFilterLevel");
static_assert(offsetof(Video::Device::Adapter, dxgiAdapterIndex) == 0x2C, "Adapter::dxgiAdapterIndex");
static_assert(offsetof(Video::Device::Adapter, dxgiOutputIndex) == 0x30, "Adapter::dxgiOutputIndex");
static_assert(offsetof(Video::Device::Adapter, pad34) == 0x34, "Adapter::pad34");
// ENDGAME B5 DEV-B: the name is the real one-pointer String, not its data pointer.
static_assert(std::is_same<decltype(Video::Device::Adapter::name), System::String>::value,
              "Adapter::name is a System::String member");
static_assert(!std::is_copy_assignable<Video::Device::Adapter>::value,
              "Adapter::operator= (0x18000cd00) is UNPORTED and deleted");
static_assert(std::is_same<decltype(Video::Device::GraphicsInterface::name), System::String>::value,
              "GraphicsInterface::name is a System::String member");
static_assert(!std::is_copy_assignable<Video::Device::GraphicsInterface>::value,
              "GraphicsInterface::operator= (0x18000ce60) is UNPORTED and deleted");

// ---- Video::Device::GraphicsInterface ----
static_assert(sizeof(Video::Device::GraphicsInterface) == 0x18, "GraphicsInterface is 0x18 bytes (GetInterface stride)");
static_assert(alignof(Video::Device::GraphicsInterface) == 8, "GraphicsInterface aligns to 8");
static_assert(offsetof(Video::Device::GraphicsInterface, name) == 0x00, "GraphicsInterface::name");
static_assert(offsetof(Video::Device::GraphicsInterface, adapterData) == 0x08, "GraphicsInterface::adapterData");
static_assert(offsetof(Video::Device::GraphicsInterface, adapterCount) == 0x10, "GraphicsInterface::adapterCount");
static_assert(offsetof(Video::Device::GraphicsInterface, adapterCapacity) == 0x14, "GraphicsInterface::adapterCapacity");

// ---- Video::Device::InterfaceSlot ----
static_assert(sizeof(Video::Device::InterfaceSlot) == 0x18, "InterfaceSlot is 0x18 bytes (InitializeInterfaces stride)");
static_assert(alignof(Video::Device::InterfaceSlot) == 8, "InterfaceSlot aligns to 8");
static_assert(offsetof(Video::Device::InterfaceSlot, module) == 0x00, "InterfaceSlot::module");
static_assert(offsetof(Video::Device::InterfaceSlot, getDeviceName) == 0x08, "InterfaceSlot::getDeviceName");
static_assert(offsetof(Video::Device::InterfaceSlot, createDeviceData) == 0x10, "InterfaceSlot::createDeviceData");

// ---- Video::Device ----
static_assert(sizeof(Video::Device) == 0x108, "Device is 0x108 bytes");
static_assert(offsetof(Video::Device, pad1C) == 0x1C, "Device::pad1C");
static_assert(offsetof(Video::Device, pad9E) == 0x9E, "Device::pad9E");
static_assert(offsetof(Video::Device, padFC) == 0xFC, "Device::padFC");
static_assert(alignof(Video::Device) == 8, "Device aligns to 8");
// RULE B5-6 in assert form: no vptr, no user ctor/dtor, so the object our ports
// and dx11 read by named member is still exactly retail's bytes.
static_assert(std::is_standard_layout<Video::Device>::value, "Device stays standard-layout (no vptr)");
static_assert(std::is_trivially_copyable<Video::Device>::value,
              "Device stays trivially copyable — RULE B5-6 gives it no real ctor/dtor");
static_assert(offsetof(Video::Device, type) == 0x00, "Device::type");
static_assert(offsetof(Video::Device, backBufferWidth) == 0x04, "Device::backBufferWidth");
static_assert(offsetof(Video::Device, backBufferHeight) == 0x08, "Device::backBufferHeight");
static_assert(offsetof(Video::Device, displayModeIndex) == 0x0C, "Device::displayModeIndex");
static_assert(offsetof(Video::Device, refreshRateIndex) == 0x10, "Device::refreshRateIndex");
static_assert(offsetof(Video::Device, dxgiAdapterIndex) == 0x14, "Device::dxgiAdapterIndex");
static_assert(offsetof(Video::Device, dxgiOutputIndex)  == 0x18,
              "Device::dxgiOutputIndex — must match src/render/dx11/devicedata.cpp Dev::kOutputIndex");
static_assert(offsetof(Video::Device, modeData) == 0x20, "Device::modeData");
static_assert(offsetof(Video::Device, modeCount) == 0x28, "Device::modeCount");
static_assert(offsetof(Video::Device, modeCapacity) == 0x2C, "Device::modeCapacity");
static_assert(offsetof(Video::Device, multisampleData) == 0x30, "Device::multisampleData");
static_assert(offsetof(Video::Device, multisampleCount) == 0x38, "Device::multisampleCount");
static_assert(offsetof(Video::Device, multisampleCap) == 0x3C, "Device::multisampleCap");
static_assert(offsetof(Video::Device, interfaceData) == 0x40, "Device::interfaceData");
static_assert(offsetof(Video::Device, interfaceCount) == 0x48, "Device::interfaceCount");
static_assert(offsetof(Video::Device, interfaceCapacity) == 0x4C, "Device::interfaceCapacity");
static_assert(offsetof(Video::Device, onVideoError) == 0x50, "Device::onVideoError @ +0x50");
static_assert(offsetof(Video::Device, onVideoRestart) == 0x58, "Device::onVideoRestart @ +0x58");
static_assert(offsetof(Video::Device, lock) == 0x60, "Device::lock");
static_assert(sizeof(Video::Device::lock) == 0x28, "Device::lock is an opaque CRITICAL_SECTION — RULE B5-6");
static_assert(offsetof(Video::Device, lockOwnerThreadId) == 0x88, "Device::lockOwnerThreadId");
static_assert(offsetof(Video::Device, lockRecursionCount) == 0x8C, "Device::lockRecursionCount");
static_assert(offsetof(Video::Device, windowHandle) == 0x90, "Device::windowHandle");
static_assert(offsetof(Video::Device, maxAnisotropy) == 0x98, "Device::maxAnisotropy — must match optionsdialog.cpp:359");
static_assert(offsetof(Video::Device, settingsChanged) == 0x9C, "Device::settingsChanged");
static_assert(offsetof(Video::Device, adjustingDisplay) == 0x9D, "Device::adjustingDisplay");
static_assert(offsetof(Video::Device, monitor) == 0xA0, "Device::monitor (B.4 R-VIDEO)");
static_assert(offsetof(Video::Device, unusedMonitorPair) == 0xA8, "Device::unusedMonitorPair");
static_assert(offsetof(Video::Device, dllSlots) == 0xB0, "Device::dllSlots — must match src/app/video_interface.cpp kSlot0");
static_assert(offsetof(Video::Device, activeInterface) == 0xF8, "Device::activeInterface — must match src/app/main.cpp:1068");
static_assert(offsetof(Video::Device, deviceData) == 0x100, "Device::deviceData — must match src/render/dx11/buffers.cpp:209");

// ---- Video::Configuration ----
// NO `sizeof == 0x44` ASSERT: 0x44 is INFERRED, not measured — see the note at
// the class. These two bracket it instead, from the ctor's last store.
static_assert(sizeof(Video::Configuration) >= 0x43,
              "Configuration must cover the ctor's last store at +0x42 (0x1800f6f64)");
static_assert(offsetof(Video::Configuration, pad01) == 0x1, "Configuration::pad01");
static_assert(sizeof(Video::Configuration) <= 0x44,
              "Configuration must not grow past natural alignment of its +0x42 byte");
static_assert(alignof(Video::Configuration) == 4, "Configuration aligns to 4");
static_assert(std::is_standard_layout<Video::Configuration>::value, "Configuration stays standard-layout");
static_assert(std::is_trivially_copyable<Video::Configuration>::value,
              "Configuration stays trivially copyable — no real ctor/dtor");
static_assert(offsetof(Video::Configuration, settingsValid) == 0x00, "Configuration::settingsValid");
static_assert(offsetof(Video::Configuration, interfaceIndex) == 0x04, "Configuration::interfaceIndex");
static_assert(offsetof(Video::Configuration, adapterIndex) == 0x08, "Configuration::adapterIndex");
static_assert(offsetof(Video::Configuration, width) == 0x0C, "Configuration::width");
static_assert(offsetof(Video::Configuration, height) == 0x10, "Configuration::height");
static_assert(offsetof(Video::Configuration, windowX) == 0x14, "Configuration::windowX");
static_assert(offsetof(Video::Configuration, windowY) == 0x18, "Configuration::windowY");
static_assert(offsetof(Video::Configuration, windowWidth) == 0x1C, "Configuration::windowWidth");
static_assert(offsetof(Video::Configuration, windowHeight) == 0x20, "Configuration::windowHeight");
static_assert(offsetof(Video::Configuration, multisample) == 0x24, "Configuration::multisample");
static_assert(offsetof(Video::Configuration, textureFilter) == 0x28, "Configuration::textureFilter");
static_assert(offsetof(Video::Configuration, shadowScalar) == 0x2C, "Configuration::shadowScalar");
static_assert(offsetof(Video::Configuration, shaderDetail) == 0x30, "Configuration::shaderDetail");
static_assert(offsetof(Video::Configuration, reflectionDetail) == 0x34, "Configuration::reflectionDetail");
static_assert(offsetof(Video::Configuration, refreshNumerator) == 0x38, "Configuration::refreshNumerator");
static_assert(offsetof(Video::Configuration, refreshDenominator) == 0x3C, "Configuration::refreshDenominator");
static_assert(offsetof(Video::Configuration, fullscreen) == 0x40, "Configuration::fullscreen");
static_assert(offsetof(Video::Configuration, vsync) == 0x41, "Configuration::vsync");
static_assert(offsetof(Video::Configuration, hideWindow) == 0x42, "Configuration::hideWindow");

// ---- Video::RenderBuffer ----
static_assert(sizeof(Video::RenderBuffer) == 0x30, "RenderBuffer is 0x30 bytes (minimap_paint.cpp:193 allocates 0x30)");
static_assert(alignof(Video::RenderBuffer) == 8, "RenderBuffer aligns to 8");
static_assert(std::is_standard_layout<Video::RenderBuffer>::value, "RenderBuffer stays standard-layout");
static_assert(std::is_trivially_copyable<Video::RenderBuffer>::value,
              "RenderBuffer stays trivially copyable — its ctor/dtor keep the intrusive list and stay doors");
static_assert(offsetof(Video::RenderBuffer, colorBuffer) == 0x00, "RenderBuffer::colorBuffer");
static_assert(offsetof(Video::RenderBuffer, depthBuffer) == 0x08, "RenderBuffer::depthBuffer");
static_assert(offsetof(Video::RenderBuffer, width) == 0x10, "RenderBuffer::width");
static_assert(offsetof(Video::RenderBuffer, height) == 0x14, "RenderBuffer::height");
static_assert(offsetof(Video::RenderBuffer, next) == 0x18, "RenderBuffer::next");
static_assert(offsetof(Video::RenderBuffer, prev) == 0x20, "RenderBuffer::prev");
static_assert(offsetof(Video::RenderBuffer, platformData) == 0x28, "RenderBuffer::platformData");

// ============================================================================
// The `RtVideo` POD spelling — now ALIASES of the real classes, not copies.
//
// B34B_RULES B4b-1 renames a POD out of the retail name when the two collide.
// Here they never collided (the PODs were already in `RtVideo`) AND they were
// field-identical, so the correct move is the other one the B.3.5 brief names:
// ONE definition, aliases for the old spellings. Every existing `RtVideo::X`
// user keeps compiling — including four files in other blocks' sets — and there
// is exactly one layout to get wrong instead of two.
// ============================================================================
namespace RtVideo {

using Device            = ::Video::Device;
using Configuration     = ::Video::Configuration;
using RenderBuffer      = ::Video::RenderBuffer;

// The nested ones keep their old unqualified `RtVideo::` spelling too. Retail's
// manglings put them inside `Device`, which is where they are now declared.
using RefreshRate       = ::Video::Device::RefreshRate;
using DisplayMode       = ::Video::Device::DisplayMode;
using Adapter           = ::Video::Device::Adapter;
using GraphicsInterface = ::Video::Device::GraphicsInterface;
using InterfaceSlot     = ::Video::Device::InterfaceSlot;

// (The ImageBuffer `_flags` bits RenderBuffer::Create tests are Video::ImageBuffer::Flags,
// ImageBuffer.h, with the rest of the set.)

}  // namespace RtVideo

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- video/companion_data.cpp
void* Video_DisplayMode_Ctor(Video::Device::DisplayMode* self, int width, int height);  // 0x18000d300
void Video_DisplayMode_Dtor(Video::Device::DisplayMode* self);  // 0x18000cb80
// ---- video/device_enum_types.cpp
// (ENDGAME B5 DEV-B: the six Adapter / GraphicsInterface lifecycle doors are the
// classes' constructors, copy constructors and destructors now.)
// All thirteen `Collection::Array<T>` helpers below live in device_enum_types.cpp
// (B5 DEV-B moved the seven that were in device_startup.cpp beside the rest, so
// the Adapter / GraphicsInterface lifecycle links into the host unit tests).
// The `Collection::Array<T>` instantiations over the unrolled {data, size,
// capacity} triples above (B.4 R-VIDEO). Each takes its array as the owner's
// three named fields BY REFERENCE; an `operator=` takes the source's data and
// size. They are template instances: they become `Collection::Array<T>` members
// when these triples become `Array<T>` members (H5 — a real Array would give
// Device/Adapter/GraphicsInterface/DisplayMode a destructor, and src/render/dx11/ and the
// unit tests read the triples' fields by name).
void RtVideo_ArrayRefreshDtor(Video::Device::RefreshRate*& data, int32_t& size,
                              int32_t& capacity);                               // 0x180010760
void RtVideo_ArrayRefreshAssign(Video::Device::RefreshRate*& data, int32_t& size,
                                int32_t& capacity,
                                Video::Device::RefreshRate* const& srcData,
                                const int32_t& srcSize);                         // 0x18000fbf0
void RtVideo_ArrayRefreshSetSize(Video::Device::RefreshRate*& data, int32_t& size,
                                 int32_t& capacity, int32_t n,
                                 const Video::Device::RefreshRate& fill);        // 0x180057340
void RtVideo_ArrayBoolDtor(uint8_t*& data, int32_t& size, int32_t& capacity);    // 0x180009560
void RtVideo_ArrayBoolAssign(uint8_t*& data, int32_t& size, int32_t& capacity,
                             uint8_t* const& srcData, const int32_t& srcSize);   // 0x180008b00
void RtVideo_ArrayModeElemDestroy(Video::Device::DisplayMode* data, int32_t count);  // 0x180010cf0
void RtVideo_ArrayModeDtor(Video::Device::DisplayMode*& data, int32_t& size,
                           int32_t& capacity);                                  // 0x180010830
void RtVideo_ArrayModeAssign(Video::Device::DisplayMode*& data, int32_t& size,
                             int32_t& capacity,
                             Video::Device::DisplayMode* const& srcData,
                             const int32_t& srcSize);                            // 0x18000fcd0
void RtVideo_ArrayModeSetSize(Video::Device::DisplayMode*& data, int32_t& size,
                              int32_t& capacity, int32_t n,
                              const Video::Device::DisplayMode& fill);           // 0x180057440
void RtVideo_ArrayAdapterElemDestroy(Video::Device::Adapter* data, int32_t count);  // 0x180010da0
void RtVideo_ArrayAdapterDtor(Video::Device::Adapter*& data, int32_t& size,
                              int32_t& capacity);                               // 0x180010900
void RtVideo_ArrayInterfaceElemDestroy(Video::Device::GraphicsInterface* data,
                                       int32_t count);                          // 0x180010f00
void RtVideo_ArrayAdapterAssign(Video::Device::Adapter*& data, int32_t& size,
                                int32_t& capacity,
                                Video::Device::Adapter* const& srcData,
                                const int32_t& srcSize);                         // 0x18000fe10
// ---- video/video_resources.cpp
Video::RenderBuffer* Video_RenderBuffer_Ctor(Video::RenderBuffer* self);  // 0x1800f3ee0
void Video_RenderBuffer_Dtor(Video::RenderBuffer* self);  // 0x1800f4020
// B3R RT-Core: the two destructor bodies Engine::Shutdown calls by name.
void Video_Configuration_Dtor(void* self);  // 0x1800f6f80  ??1Configuration@Video@@QEAA@XZ
void Video_Device_Dtor(RtVideo::Device* self);  // 0x1800f7290  ??1Device@Video@@QEAA@XZ
// ==== end B3R W0-P0c ====

// src/engine/video/buffers_lock.cpp -- Video::Device's recursive render lock.
// The first pair is the INLINE COPY the Platform{Construct,Destruct} bodies and
// SaveLoad carry: it takes the ADDRESS of `Device::_singleton` because retail's
// unlock re-reads the singleton. The second pair is the body of the exported
// `Device::Lock` / `Device::Unlock` (dynamic_resources.cpp), on the device
// handed in.
void RtVideoDeviceLock(Video::Device** deviceSingleton) ACQUIRE((*deviceSingleton)->lock);
void RtVideoDeviceUnlock(Video::Device** deviceSingleton) RELEASE((*deviceSingleton)->lock);
void RtVideoDeviceLockThis(Video::Device* device) ACQUIRE(device->lock);
// src/engine/video/video_resources_platform.cpp -- ??1Device's DeleteCriticalSection.
void RtVideoDeleteDeviceLock(Video::Device* device);
void RtVideoDeviceUnlockThis(Video::Device* device) RELEASE(device->lock);

// src/engine/video/device_startup.cpp -- prints the run-identity line (this
// translation unit's build stamp). Called once, from RuntimeInit()
// (src/app/runtime_init.cpp).
void RtVideoDeviceStartupIdentity();
