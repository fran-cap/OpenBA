// ============================================================================
// src/engine/video/enumerate_video_modes.cpp — Wave 9 lane EVM
//   ?EnumerateVideoModes@Device@Video@@QEAA_NXZ  retail 0x1800f8610  ord 2319
// ============================================================================
//
// `Engine::Startup` calls it (src/engine/engine/engine_lifecycle.cpp) and
// **discards the result** — `(void)videoDevice->EnumerateVideoModes()` — because
// retail does not test it either. So the `_N` return is not load-bearing for
// control flow; what has to be right is the SIDE EFFECT, and a later consumer
// faults on that some distance from the actual defect.
//
// THE BODY IS TWO HALVES
// ----------------------
//   1. plugin orchestration: for each of the three interface slots, create the
//      backend's DeviceData, call ONE virtual on it, destroy it. The virtual is
//      what appends to `Device::interfaceData` — this function never writes
//      that array itself.
//   2. a three-level sort pass over what half 1 produced.
//
// WHAT IT WRITES INTO THE DEVICE, WHICH IS THE QUESTION THE BRIEF ASKED
// ---------------------------------------------------------------------
// Exactly two regions change across the call:
//   * +0x40/+0x48/+0x4C — the `Array<GraphicsInterface>` header, filled
//     indirectly by the plugin virtual. `_data` is a heap pointer: different
//     between any two runs by construction.
//   * +0xb0..+0xf7 — the three `InterfaceSlot`s, written by
//     `InitializeInterfaces`: a module handle and two entry points each.
//
// THE SORT PASS IS THE ONLY REAL COMPUTATION — a pure permutation of buffers
// the caller owns. video_mode_sort.cpp transcribes the two introsorts.
//
// FOUR TRANSCRIBED DETAILS
// ------------------------
//  1. **The three slot tests are in a fixed order and are not equivalent to
//     "the slot is complete"**: module (+0x00), then createDeviceData (+0x10),
//     then getDeviceName (+0x08). A slot with a module and a create but no name
//     is skipped, and `getDeviceName` is never CALLED here — only tested.
//  2. **`create(self)`'s result is dereferenced UNGUARDED** (`MOV RDX,[RAX]` at
//     0x1800f8678) but the destructor call afterwards IS null-guarded
//     (`TEST RBX,RBX` at 0x1800f869e). Retail is inconsistent on purpose: the
//     unguarded deref is inside the SEH region, the guarded one is not.
//  3. **`Debug::_disableHandler` is raised around the virtual and lowered after
//     it**, on BOTH the normal and the exception path. See the limits note.
//  4. **The destructor is vtable slot 0 called with 1**, i.e. MSVC's scalar
//     deleting destructor with the "also free" flag set.
//
// THE `__except` THIS PORT DOES NOT REPRODUCE
// -------------------------------------------
// The 20 bytes at 0x1800f8683 that Ghidra leaves out of the listing are an
// exception landing pad. Decoded from the mapped image:
//     1800f8683  4C 8B A4 24 80 00 00 00   MOV R12,[RSP+0x80]   ; self
//     1800f868b  8B BC 24 98 00 00 00      MOV EDI,[RSP+0x98]   ; the loop index
//     1800f8692  48 8B 5C 24 28            MOV RBX,[RSP+0x28]   ; the DeviceData
// then it falls into the `_disableHandler = 0` path. So retail CATCHES a
// faulting driver enumeration, releases that DeviceData and CARRIES ON to the
// next slot — which is what raising `_disableHandler` across the call is for.
//
// This port raises and lowers the flag faithfully but does NOT swallow the
// fault, and that is a deliberate choice rather than a limitation to work
// around: MinGW has no usable `__try`/`__except` on x64, and reproducing the
// swallow would mean teaching our VEH to ignore access violations while the
// flag is set. During a port wave that would hide exactly the faults these
// lanes exist to find. Consequence, stated rather than discovered: on a video
// driver that faults during mode enumeration, retail skips that renderer and
// continues, and we die with a named crash. It is an ERROR-PATH difference and
// it is in the lane's honest limits.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "video/Device.h"
#include "video/CompanionData.h"   // ENDGAME B.3.5: Video::DeviceData
#include "system/Debug.h"          // B3R W0-SM M2: System::Debug::_disableHandler

using Adapter           = Video::Device::Adapter;
using DisplayMode       = Video::Device::DisplayMode;
using GraphicsInterface = Video::Device::GraphicsInterface;
using InterfaceSlot     = Video::Device::InterfaceSlot;

extern "C" void RtVideo_SortDisplayModes(DisplayMode* first, int32_t count);
extern "C" void RtVideo_SortRefreshRates(Video::Device::RefreshRate* first, int32_t count);

namespace {

// Device::dllSlots (+0xB0), three InterfaceSlots.
constexpr int kSlotCount = 3;
static_assert(sizeof(Video::Device::dllSlots) / sizeof(InterfaceSlot) == kSlotCount,
              "Device::dllSlots holds the three interface DLL slots");

}  // namespace

// ============================================================================
// ENDGAME B.3.5 block D2. `InitializeInterfaces` is a sibling member now, and the
// two hand-inlined `DeviceData` vtable dispatches became virtual calls on block
// A's real `Video::DeviceData` (slot 1 `EnumerateInterfaces`, slot 0 the
// virtual destructor via `delete`, B5 DEV-A). Every branch is untouched; the file
// has no `__try`/`__except` (see the banner), and the unguarded deref at
// 0x1800f8678 is what retail's SEH region exists for.
bool Video::Device::EnumerateVideoModes() {
    Video::Device::InitializeInterfaces();              // 0x1800f8627

    int created = 0, enumerated = 0;
    for (int i = 0; i < kSlotCount; ++i) {              // 0x1800f862e / 0x1800f8635
        InterfaceSlot* const s = &dllSlots[i];
        if (s->module == nullptr)           continue;   // 0x1800f8641
        const CreateDeviceDataFn create = s->createDeviceData;
        if (create == nullptr)              continue;   // 0x1800f8654
        if (s->getDeviceName == nullptr)    continue;   // 0x1800f8659, tested, never called

        DeviceData* data = create(this);                // 0x1800f8667
        ++created;

        System::Debug::_disableHandler = true;          // 0x1800f8671

        // 0x1800f8678: `MOV RDX,[RAX]` — UNGUARDED. Retail relies on the SEH
        // region here, not on a null test; see the banner. The virtual call keeps
        // that: a null receiver faults on the vptr load exactly as retail does.
        (void)data->EnumerateInterfaces();
        ++enumerated;

        System::Debug::_disableHandler = false;         // 0x1800f8697

        if (data != nullptr) {                          // 0x1800f869e — guarded HERE
            // 0x1800f86ab, the deleting flag. B5 DEV-A (B5_RULES D10, C3): slot 0
            // with flags 1 is `delete` -- the virtual destructor of whichever
            // plugin built it (DX11 or the DX9 placeholder), then the base's
            // class-scope operator delete (MemFree).
            delete data;
        }
    }

    // ---- the three-level sort pass, 0x1800f86b7..0x1800f878a ----------------
    // Level 1  Device+0x40   Array<GraphicsInterface>, stride 0x18, count +0x48
    // Level 2  gi+0x08       Array<Adapter>,           stride 0x38, count gi+0x10
    // Level 3  adapter+0x08  Array<DisplayMode>,       stride 0x18, count +0x10
    // Every stride and offset is the one src/engine/video/Device.h already
    // pins from other bodies, which is why this file asserts none of its own.
    int adaptersSeen = 0, modesSorted = 0, refreshSorted = 0;
    for (int32_t gi = 0; gi < interfaceCount; ++gi) {            // 0x1800f86c0
        GraphicsInterface* const iface = interfaceData + gi;
        for (int32_t ai = 0; ai < iface->adapterCount; ++ai) {   // 0x1800f86ec
            Adapter* const adapter = iface->adapterData + ai;
            ++adaptersSeen;

            if (adapter->modeCount > 1) {                        // 0x1800f870b
                RtVideo_SortDisplayModes(adapter->modeData, adapter->modeCount);
                ++modesSorted;
            }
            // Retail RE-READS `adapter->modeCount` for the inner loop bound
            // (0x1800f8723 and again at 0x1800f8755) rather than reusing the
            // value it just tested. Kept: the sort above can in principle be
            // handed a count the array does not have.
            for (int32_t mi = 0; mi < adapter->modeCount; ++mi) { // 0x1800f8730
                DisplayMode* const mode = adapter->modeData + mi;
                if (mode->refreshCount > 1) {                     // 0x1800f8738
                    RtVideo_SortRefreshRates(mode->refreshData, mode->refreshCount);
                    ++refreshSorted;
                }
            }
        }
    }

    return true;                                        // 0x1800f8790 MOV AL,1
}
