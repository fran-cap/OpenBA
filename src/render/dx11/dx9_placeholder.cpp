// ============================================================
// The DX9 PLACEHOLDER slot
//
// The ported InitializeInterfaces loads no plugin DLL (VideoDX9-x64.dll imports
// Runtime-x64.dll), so slot 0 stays empty. Retail then appends nothing for it and
// the Device's GraphicsInterface array holds ONE entry (ours, DX11) while the
// configuration's interface index -- a SLOT index, 1 -- stays 1: the Options
// dialog's PopulateResolutionCombo (retail 0x140073ec0) does
// GetInterface(cfgIndex) with no bounds check and reads past the array. Retail
// never meets this because both plugin DLLs always load and each appends one
// entry, so array index == slot index. This placeholder keeps that invariant:
// it appends a "DirectX 9" interface listing the SAME adapters and modes the
// DX11 walk finds (which is what the real plugin reports on a machine that has
// both). Its Create() returns false, so choosing it in Options leaves the
// fallback loop in Device::PlatformStartup to step back to DX11. Filled only by
// VideoInterfaceFillDx9Placeholder (src/app/video_interface.cpp), which the
// ported InitializeInterfaces calls when slot 0 is empty (it loads no DLL).
//
// B5 DEV-A (B5_RULES D5, C3): a real C++ subclass of `Video::DeviceData`. It was
// a 16-byte POD wearing a four-entry hand-built vtable array, which
// left retail slots 4..19 dangling past the table's end; now those sixteen slots
// INHERIT the base's `_purecall` bodies, and the three it implements are
// overrides. Runtime creates one through the slot's factory and destroys it in
// `Video::Device::EnumerateVideoModes` (src/engine/video/enumerate_video_modes.cpp)
// through the base pointer, so it must wear a table whose slot 0 destroys THIS
// object. Allocation is the base's class-scope `operator new` (MemAlloc(n, 4),
// src/engine/video/companion_operators.cpp), freed by the base's own slot 0 --
// the old POD used the global `new` / `delete` pair.
// ============================================================

#include "VideoDX11Plugin.h"
#include "video/CompanionData.h"   // src/engine: Video::DeviceData

namespace VideoDX11 {
namespace {

class Dx9PlaceholderDeviceData final : public ::Video::DeviceData {
public:
    explicit Dx9PlaceholderDeviceData(::Video::Device* owner)
        : ::Video::DeviceData(owner) {}

    // [1] The same adapters and modes DX11 lists, under the DX9 name: what the
    // real plugin reports on this machine. A zero-adapter entry was tried first
    // and selecting it in Options died in UserInterface::Draw.
    bool EnumerateInterfaces() override {
        return Dx11EnumerateDxgiInterfaces(_owner, "DirectX 9");
    }
    // [2] Never creates a device: Device::PlatformStartup's fallback loop then
    // steps back to the DX11 slot.
    bool CreateDevice() override { return false; }
    // [3] Nothing was created, so nothing is torn down.
    void Shutdown() override {}
};
static_assert(sizeof(Dx9PlaceholderDeviceData) == 0x10,
              "the placeholder is the base's two pointers and nothing else");

}  // namespace

::Video::DeviceData* Dx9Placeholder_CreateDeviceData(::Video::Device* device) {
    return new Dx9PlaceholderDeviceData(device);
}

const char* Dx9Placeholder_GetDeviceName() { return "DirectX 9"; }

}  // namespace VideoDX11
