// ============================================================================
// src/engine/video/device_startup.cpp — Wave 9 lane VID2
//   Video::Device::{Startup, PlatformStartup, Shutdown, PlatformShutdown,
//   ValidateConfiguration, CreateVideoInterface}. (The seven unexported
//   Collection::Array helpers ValidateConfiguration reaches moved to
//   device_enum_types.cpp in ENDGAME B5 DEV-B; "PORTED HERE" below on an
//   Array<> row is historical — the body lives there now.)
// ============================================================================
//
// THE POINT OF THIS FILE
// ----------------------
// `?PlatformStartup@Device@Video@@AEAA_NXZ` is the next cutover boot wall after
// the IO chain. Our ported `Engine::Startup` calls it at
// src/engine/engine/engine_lifecycle.cpp:719 and a null there returns 0 with
// `g_startupFailAt == 11` — the boot ends, quietly, without a crash to
// diagnose.
//
// `?Startup@` (0x1800f7270) and `?Shutdown@` (0x1800f7280) are ONE-INSTRUCTION
// JMP THUNKS onto `PlatformStartup` (0x1800f8260) and `PlatformShutdown`
// (0x1800f83f0). Verified in the disassembly, not taken on trust:
//
//     1800f7270: JMP 0x1800f8260
//     1800f7280: JMP 0x1800f83f0
//
// so one body closes two rows each. Their live callers are
// src/game/Dialog/optionsdialog.cpp:1138/1168 (applying video settings).
//
// THREE RECORDED BLOCKERS FOR THIS CLUSTER WERE FALSE, AND ALL THREE WERE
// RE-CHECKED AGAINST THE DISASSEMBLY RATHER THAN INHERITED
// ---------------------------------------------------------------------------
//  1. WAVE9_LANE_VID.md: "loops the renderer interfaces through
//     CreateVideoInterface (which LoadLibrary's a plugin DLL and calls its
//     CreateDeviceData)". **False.** `?CreateVideoInterface@Device@Video@@QEAA_NH@Z`
//     (0x1800f7930) is thirty instructions and contains no `LoadLibrary`. It
//     tests three ALREADY-POPULATED pointers in `Device::dllSlots[idx]` and
//     calls the slot's `createDeviceData`. The `LoadLibraryW` lives in
//     `InitializeInterfaces` (0x1800f79f0), driven by `EnumerateVideoModes`,
//     which `src/app/video_interface.cpp` already detours.
//  2. Same doc: "calls the PRIVATE no-argument ValidateConfiguration". **False.**
//     It is exported — `?ValidateConfiguration@Device@Video@@QEAA_NXZ`, ordinal
//     4533. (Wave 6 lane B ported the EIGHT-ARGUMENT overload, ordinal 4532;
//     that part was right, and the two are different functions.)
//  3. Same doc: "with the DLL gone, Engine::Startup no longer runs; nothing in
//     our tree starts the video device". **Stale.** `Engine::Startup` is ported
//     (Wave 9 lane ENG) and line 719 IS that call.
//
// TRANSITIVE CLOSURE, COUNTED BEFORE ANY BODY WAS WRITTEN (lane PICK's rule)
// ---------------------------------------------------------------------------
//   PlatformStartup 0x1800f8260
//     -> Application::_singleton [0x1801fc678]        (DATA, minted)
//     -> ValidateConfiguration   0x1800f8830   PORTED HERE
//          -> GetDefaultResolution 0x1800f7540  PORTED (device_window.cpp)
//          -> memset               0x1800881b0  = the CRT memset; std::memset
//          -> Array<DisplayMode>::operator=  0x18000fcd0  PORTED HERE
//               -> Array<DisplayMode>::~Array 0x180010830 PORTED HERE
//                    -> element destroy       0x180010cf0 PORTED HERE
//               -> Array<RefreshRate>::operator= 0x18000fbf0 PORTED HERE
//                    -> Array<RefreshRate>::~Array 0x180010760 PORTED HERE
//          -> Array<bool>::operator=          0x180008b00  PORTED HERE
//               -> Array<bool>::~Array        0x180009560  PORTED HERE
//     -> Error::SetText          0x1800c5fa0   ALREADY PORTED (Wave 2)
//     -> CreateVideoInterface    0x1800f7930   PORTED HERE
//     -> Application::RegisterMessageCallback 0x18009fa60 PORTED
//                                   (src/engine/application/message_callbacks.cpp)
//     -> &SizeWindow 0x1800f7d20 / &MoveWindow 0x1800f7d90  PORTED
//                                   (device_window.cpp) — STORED, so a null
//                                   here is a null Application later calls
//   PlatformShutdown 0x1800f83f0
//     -> Collection::Set erase   0x18000aba0   PORTED (collection/set_avl.cpp)
//     -> the DeviceData vtable   (ours, src/render/dx11/)
//
//   MemAlloc 0x1800c7a50 / MemFree 0x1800c7ad0 / Free 0x1800c6c00 — registered
//   by attempt 14's RegisterAllocatorAddresses().
//
// **Closure size: 13 new bodies here + 3 in the _win twin + 5 in the Set
// cluster = 21, and it CLOSES.** Porting only the four assigned rows would have
// created 17 new unregistered address doors where there are none today, which
// is exactly the trade lane PICK refused.
//
// ORACLE
// ------
// `ValidateConfiguration` and the seven Array helpers get a lockstep synthetic
// probe (device_startup_probe.cpp). `PlatformStartup` / `PlatformShutdown` do
// NOT and cannot: they create and destroy the one D3D11 device, so running ours
// and then theirs makes a second swap chain against a window that already has
// one. What they get instead is **live exercise on the DLL-present boot** —
// with this port registered, `Engine::Startup`'s `RT_EXPORT_ONCE` resolves to
// OUR `PlatformStartup`, so every DLL-present run boots the video device
// through this file. A wrong transcription does not report a divergence; it
// fails to render.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "system/MemoryAllocator.h"
#include "system/Error.h"
#include "video/Device.h"
#include "video/validate_configuration.h"   // the refresh / multisample clamps (review F-130, F-156)
#include "video/CompanionData.h"   // ENDGAME B.3.5: Video::DeviceData
#include "application/Application.h"  // ENDGAME B.3.7 callers (K5)
#include "../collection/set_avl.h"
#include "platform_window.h"   // Phase D.4 SDL W2: the message ids

using Video::Configuration;
using Video::Device;
using Adapter           = Video::Device::Adapter;
using DisplayMode       = Video::Device::DisplayMode;
using GraphicsInterface = Video::Device::GraphicsInterface;
using InterfaceSlot     = Video::Device::InterfaceSlot;
using RefreshRate       = Video::Device::RefreshRate;


// ENDGAME B5 DEV-B: the seven `Collection::Array<T>` helpers ValidateConfiguration
// reaches (RtVideo_ArrayRefreshDtor / BoolDtor / ModeElemDestroy / ModeDtor /
// RefreshAssign / BoolAssign / ModeAssign, retail 0x180010760 / 0x180009560 /
// 0x180010cf0 / 0x180010830 / 0x18000fbf0 / 0x180008b00 / 0x18000fcd0) and their
// GrowFor rule MOVED, unchanged, to device_enum_types.cpp beside the other six
// Array helpers: Video::Device::Adapter's copy constructor and destructor call
// them, and that file links into the host unit tests where this one cannot.
// They are declared in Device.h and called from here by name.

// ============================================================================
// ?ValidateConfiguration@Device@Video@@QEAA_NXZ   retail 0x1800f8830, ord 4533
// ============================================================================
// Clamps `Video::Configuration` against the adapter the configuration selects,
// then copies that adapter's mode and multisample lists into the Device.
//
// NOT the eight-argument `?ValidateConfiguration@Device@Video@@QEAAXAEAH0000000@Z`
// (0x1800f7370, ordinal 4532) that Wave 6 lane B ported. Different function,
// adjacent ordinal — exactly the kind of pair that
// reference_ghidra_mcp_address_param_silently_ignored.md exists for.
//
// EARLY OUT: `interfaceCount == 0` returns with AL holding that count, i.e.
// FALSE, and no field is touched (0x1800f8852 jumps straight to the epilogue).
// The return value on that path is not an explicit `XOR AL,AL` — it is the
// count still sitting in EAX. Written as `return false` because the count is
// zero on that path by construction.
//
// SECOND PASS IS NOT REDUNDANT. The function walks the adapter's mode list
// TWICE: once against the requested width/height to pick the mode and clamp the
// refresh rate, then again against the CLAMPED values to find the final indices.
// A single pass would give the same answer whenever the first lookup hit, and a
// different one whenever `GetDefaultResolution` had to substitute — which is
// exactly the case a probe on a well-formed adapter never reaches. Both passes
// are written out.
//
// UNGUARDED INDEXING, twice, both retail's:
//   * `adapter->multisampleData[msIdx]` at 0x1800f89ff is not bounds-checked
//     against `multisampleCount`; the clamp that follows only maps a ZERO
//     ENTRY to index 0, it does not constrain the index itself. The index is
//     settings.cfg's VideoMultisample, so the port bounds it (NOT RETAIL,
//     review F-156, validate_configuration.h); likewise the refresh-ratio
//     divide skips a zero denominator (review F-130).
//   * the second pass indexes `modeData[modeIdx]` with the result of a search
//     that can return -1 if the clamped size is somehow still absent.
// Left unguarded per CLAUDE.md. If either faults, something upstream built an
// adapter list our `EnumerateVideoModes` path should have built differently,
// and that is the thing to find.
// ============================================================================
// ---------------------------------------------------------------------------
// `Device::GetDefaultResolution`, `SizeWindow` and `MoveWindow` are defined in
// device_window.cpp, the window half (B.4 R-VIDEO: the members themselves;
// the three `extern "C"` C bodies and their one-line forwards are gone).
// ---------------------------------------------------------------------------

bool Video::Device::ValidateConfiguration() {
    const int32_t ifaceTotal = interfaceCount;
    if (ifaceTotal == 0) return false;              // 0x1800f8852

    Configuration* cfg = Configuration::_singleton;

    int32_t ifaceIdx   = cfg->interfaceIndex;
    int32_t adapterIdx = cfg->adapterIndex;
    int32_t width      = cfg->width;
    int32_t height     = cfg->height;
    int32_t refreshN   = cfg->refreshNumerator;
    int32_t refreshD   = cfg->refreshDenominator;
    int32_t msIdx      = cfg->multisample;
    int32_t filter     = cfg->textureFilter;

    // 0x1800f8893: clamp the interface index into range.
    if (ifaceIdx < 0 || ifaceIdx >= ifaceTotal) ifaceIdx = 0;

    GraphicsInterface* iface = interfaceData + ifaceIdx;

    // 0x1800f88b8: clamp the adapter index against THAT interface's list.
    if (adapterIdx < 0 || adapterIdx >= iface->adapterCount) adapterIdx = 0;

    Adapter* adapter = iface->adapterData + adapterIdx;

    // ---- pass 1: find the requested mode ---------------------------------
    // The stack temp retail builds here is a DisplayMode{width, height, {}}
    // whose only purpose is the ~Array call at 0x1800f8928 — a side-effect-free
    // destruction of an array that was never filled. It is kept because the
    // call is observable to the allocator (it is a no-op on a null pointer, so
    // in practice it is not), and because deleting a written-then-released
    // local is how lane FIND's `dirAlias` finding says a side-effect carrier
    // gets lost. Here it provably carries nothing: MemFree(nullptr).
    int32_t modeIdx = -1;
    {
        const int32_t n = adapter->modeCount;
        for (int32_t i = 0; i < n; ++i) {
            if (adapter->modeData[i].width == width &&
                adapter->modeData[i].height == height) { modeIdx = i; break; }
        }
        DisplayMode tmp;
        tmp.refreshData     = nullptr;
        tmp.refreshCount    = 0;
        tmp.refreshCapacity = 0;
        RtVideo_ArrayRefreshDtor(tmp.refreshData, tmp.refreshCount,
                                 tmp.refreshCapacity);   // 0x1800f8928
    }

    if (modeIdx == -1) {
        // 0x1800f8945: no match — ask the desktop what it is running and take
        // that mode's dimensions.
        modeIdx = Video::Device::GetDefaultResolution(*adapter);
        // `this` is passed because retail passes it (RCX = this at
        // 0x1800f8942); the callee never reads it — 0x1800f7540's first act is
        // `MOV RDI,RDX`, and RCX is destroyed by the memset call two
        // instructions later.
        width  = adapter->modeData[modeIdx].width;
        height = adapter->modeData[modeIdx].height;
    }

    // ---- clamp the refresh rate against that mode -------------------------
    {
        const DisplayMode* mode = adapter->modeData + modeIdx;
        const int32_t n = mode->refreshCount;

        int32_t found = -1;
        for (int32_t i = 0; i < n; ++i) {
            if (mode->refreshData[i].numerator == refreshN &&
                mode->refreshData[i].denominator == refreshD) { found = i; break; }
        }

        if (found == -1) {
            // 0x1800f89a4: no exact match — fall back to the first entry whose
            // integer RATIO matches. Note this is INTEGER division on both
            // sides (`CDQ; IDIV`), so 59.94Hz (60000/1001) and 60Hz (60/1) both
            // reduce to 59 and 60 respectively and do NOT match, while
            // 120000/1001 and 119/1 both reduce to 119 and DO. Retail's
            // arithmetic; the point of a faithful port is that ours picks the
            // same surprising entry.
            const int32_t pick =
                Video::ValidateConfig::PickRefreshByRatio(mode->refreshData, n, refreshN, refreshD);
            refreshN = mode->refreshData[pick].numerator;
            refreshD = mode->refreshData[pick].denominator;
        }
    }

    // 0x1800f89f3: a multisample index whose adapter entry is FALSE falls back
    // to 0. Retail does not range-check the index; the port does (banner).
    msIdx = Video::ValidateConfig::ClampMultisample(adapter->multisampleData, adapter->multisampleCount,
                                                    msIdx);

    // 0x1800f8a07: clamp the texture filter to the adapter's maximum.
    if (filter > adapter->maxFilterLevel) filter = adapter->maxFilterLevel;

    cfg->interfaceIndex     = ifaceIdx;
    cfg->adapterIndex       = adapterIdx;
    cfg->width              = width;
    cfg->height             = height;
    cfg->refreshNumerator   = refreshN;
    cfg->refreshDenominator = refreshD;
    cfg->multisample        = msIdx;
    cfg->textureFilter      = filter;

    // ---- pass 2: re-look-up with the clamped values ------------------------
    // Retail RECOMPUTES the adapter pointer from the interface array here
    // (0x1800f8a38) rather than reusing the one above. Nothing can have moved
    // it, but "nothing can have moved it" is a claim about code this lane does
    // not own, so the re-read stands (lane PICK §5).
    adapter = (interfaceData + ifaceIdx)->adapterData + adapterIdx;

    int32_t finalModeIdx = -1;
    {
        const int32_t n = adapter->modeCount;
        for (int32_t i = 0; i < n; ++i) {
            if (adapter->modeData[i].width == width &&
                adapter->modeData[i].height == height) { finalModeIdx = i; break; }
        }
        DisplayMode tmp;
        tmp.refreshData     = nullptr;
        tmp.refreshCount    = 0;
        tmp.refreshCapacity = 0;
        RtVideo_ArrayRefreshDtor(tmp.refreshData, tmp.refreshCount,
                                 tmp.refreshCapacity);   // 0x1800f8a90
    }

    int32_t finalRefreshIdx = -1;
    {
        const DisplayMode* mode = adapter->modeData + finalModeIdx;
        const int32_t n = mode->refreshCount;
        const int32_t wantN = cfg->refreshNumerator;    // re-read from the cfg
        const int32_t wantD = cfg->refreshDenominator;  // (0x1800f8aa4)
        for (int32_t i = 0; i < n; ++i) {
            if (mode->refreshData[i].numerator == wantN &&
                mode->refreshData[i].denominator == wantD) { finalRefreshIdx = i; break; }
        }
    }

    // ---- clamp the three detail levels ------------------------------------
    // Each is "negative -> 0, above the cap -> the cap, otherwise untouched".
    // The caps differ per field and are literals in the code: 2, 3, 2.
    if (cfg->shadowScalar     < 0) cfg->shadowScalar     = 0;
    else if (cfg->shadowScalar     > 2) cfg->shadowScalar     = 2;
    if (cfg->shaderDetail     < 0) cfg->shaderDetail     = 0;
    else if (cfg->shaderDetail     > 3) cfg->shaderDetail     = 3;
    if (cfg->reflectionDetail < 0) cfg->reflectionDetail = 0;
    else if (cfg->reflectionDetail > 2) cfg->reflectionDetail = 2;

    dxgiAdapterIndex = adapter->dxgiAdapterIndex;  // 0x1800f8b26
    dxgiOutputIndex  = adapter->dxgiOutputIndex;   // 0x1800f8b2c
    displayModeIndex = finalModeIdx;               // 0x1800f8b2f
    refreshRateIndex = finalRefreshIdx;            // 0x1800f8b32

    RtVideo_ArrayModeAssign(modeData, modeCount, modeCapacity,
                            adapter->modeData, adapter->modeCount);          // 0x1800f8b3d
    RtVideo_ArrayBoolAssign(multisampleData, multisampleCount, multisampleCap,
                            adapter->multisampleData,
                            adapter->multisampleCount);                      // 0x1800f8b4a

    maxAnisotropy = adapter->maxFilterLevel;  // 0x1800f8b52
    return true;
}

// ============================================================================
// ?CreateVideoInterface@Device@Video@@QEAA_NH@Z   retail 0x1800f7930, ord 2140
// ============================================================================
// Swap the active renderer backend to `index`.
//
// NO `LoadLibrary` ANYWHERE IN IT — see the banner. It requires all THREE
// pointers in the slot to be non-null (module, getDeviceName, createDeviceData;
// tested in the order +0xb0, +0xc0, +0xb8) and fails with an Error::SetText if
// any is missing.
//
// The teardown of the previous `DeviceData` is vtable slot 0x18 (Destroy) then
// vtable slot 0 with flag 1 (the deleting dtor) — the same two-step every other
// companion release in this tree uses — and the field is nulled between them so
// a re-entrant read cannot see a half-destroyed object.
//
// It returns TRUE even when `createDeviceData` returns null (0x1800f79b7 sets
// AL unconditionally). PlatformStartup catches that separately at 0x1800f838f
// by re-reading the field. Faithful.
// ============================================================================
bool Video::Device::CreateVideoInterface(int index) {
    InterfaceSlot* slot = dllSlots + index;

    if (slot->module == nullptr ||
        slot->createDeviceData == nullptr ||
        slot->getDeviceName == nullptr) {
        // 0x1800f79c9 — the shipped string at 0x180158920. The message text is
        // not reproduced: it lives in the DLL's .rdata and at cutover there is
        // nothing at that address. Passing our own literal would change what
        // the player sees; passing the DLL's pointer would fault. This is the
        // one place in the file where our standalone genuinely differs from
        // retail, and it is stated rather than hidden.
        ::System::Error::SetText("Video::Device::CreateVideoInterface: the selected renderer "
                     "plugin did not export the expected entry points.", false);
        return false;
    }

    // ENDGAME B.3.5: the two hand-inlined vtable dispatches are virtual calls on
    // block A's real `Video::DeviceData` — slot 3 `Shutdown` (+0x18) and slot 0
    // (+0x00) with the delete flag, `delete` since B5 DEV-A.
    if (Video::DeviceData* dd = deviceData) {
        dd->Shutdown();
        dd = deviceData;                                     // 0x1800f7980 re-read
        if (dd)
            delete dd;   // B5 DEV-A (D10): slot 0, flags 1
        deviceData = nullptr;
    }

    activeInterface = index;                                 // 0x1800f79a4

    deviceData = slot->createDeviceData(this);
    return true;                                             // 0x1800f79b7
}

// ============================================================================
// ?PlatformStartup@Device@Video@@AEAA_NXZ   retail 0x1800f8260, ord 3405
// ?Startup@Device@Video@@QEAA_NXZ           retail 0x1800f7270, ord 4091 (thunk)
// ============================================================================
// Bring the video device up, trying every renderer interface in turn.
//
// THE RETRY LOOP IS THE INTERESTING PART, and it is not a plain for-loop:
//
//   * it starts at the CONFIGURED interface index, clamped to 0..2 by an
//     UNSIGNED compare (`CMP EBX,2 / CMOVA EBX,1` — a NEGATIVE configured index
//     is above 2 unsigned, so it becomes 1, not 0);
//   * it walks DOWNWARD and WRAPS to 2 at -1, so from index 1 the order is
//     1, 0, 2 — not 1, 2, 0;
//   * a three-byte stack array records which indices have been tried, and the
//     loop stops when it reaches one already marked. The initial `tried` test
//     happens BEFORE the first iteration, so a pre-marked entry would exit
//     immediately — it cannot, because the array is zeroed two instructions
//     earlier, but the test is where retail put it.
//
// `ValidateConfiguration`'s result is CHECKED the first time (failure is fatal)
// and IGNORED inside the loop (0x1800f82f9). Both kept.
//
// The two message callbacks are registered only on the success path, and their
// ids are 5 (WM_SIZE) and 0x232 (WM_EXITSIZEMOVE).
// ============================================================================
bool Video::Device::PlatformStartup() {
    // 0x1800f8266: the HWND comes from Application::_singleton[0].
    windowHandle = ::Application::Application::_singleton->_windowHandle;

    if (!Video::Device::ValidateConfiguration()) {
        // 0x1800f8283 — the shipped string at 0x1801589b0; see
        // CreateVideoInterface for why the text is ours.
        ::System::Error::SetText("Video::Device::PlatformStartup: no usable video "
                     "configuration.", false);
        return false;
    }

    Configuration* cfg = Configuration::_singleton;

    int32_t idx = cfg->interfaceIndex;
    if (static_cast<uint32_t>(idx) > 2u) idx = 1;    // 0x1800f82c5 CMOVA

    uint8_t tried[3] = { 0, 0, 0 };
    int64_t cursor = idx;

    if (tried[cursor] != 0) {                        // 0x1800f82e2, before the loop
        ::System::Error::SetText("Video::Device::PlatformStartup: no renderer interface "
                     "could be started.", false);
        return false;
    }

    for (;;) {
        tried[cursor] = 1;                           // 0x1800f82f3
        cfg->interfaceIndex = idx;                   // 0x1800f82f6

        (void)Video::Device::ValidateConfiguration();      // result IGNORED

        if (Video::Device::CreateVideoInterface(idx)) {
            // ENDGAME B.3.5: three hand-inlined `DeviceData` vtable dispatches
            // became virtual calls on block A's real class — slot 2 `CreateDevice`
            // (+0x10), slot 3 `Shutdown` (+0x18) and slot 0 (`delete`, B5 DEV-A).
            Video::DeviceData* dd = deviceData;

            if (dd->CreateDevice()) {
                // ---- success path (0x1800f838f) --------------------------
                if (deviceData == nullptr) break;         // re-read, then fail
                const int32_t active = activeInterface;
                if (active < 0) break;
                cfg->interfaceIndex = active;

                ::Application::Application* app = ::Application::Application::_singleton;
                // ENDGAME B.3.7 callers (K5): members of the real class (the
                // callback Set lives in src/engine/application/message_callbacks.cpp).
                app->RegisterMessageCallback(Platform::Window::kMsgSize, &Video::Device::SizeWindow);           // 5
                app->RegisterMessageCallback(Platform::Window::kMsgExitSizeMove, &Video::Device::MoveWindow);    // 0x232
                return true;
            }

            // ---- Create failed: tear the companion down and try the next ---
            deviceData->Shutdown();
            if (Video::DeviceData* d2 = deviceData)
                delete d2;   // B5 DEV-A (D10): slot 0, flags 1
            deviceData = nullptr;
        }

        // 0x1800f8345: step DOWN, wrapping to 2.
        --cursor;
        --idx;
        if (idx < 0) { idx = 2; cursor = 2; }
        if (tried[cursor] != 0) break;
    }

    // 0x1800f8360 — every interface tried, or the success path bailed.
    ::System::Error::SetText("Video::Device::PlatformStartup: no renderer interface could "
                 "be started.", false);
    return false;
}

bool Video::Device::Startup() {
    // 0x1800f7270: JMP 0x1800f8260. One instruction, no frame, no argument
    // shuffle — so this really is the same function under a second name.
    return Video::Device::PlatformStartup();
}

// ============================================================================
// ?PlatformShutdown@Device@Video@@AEAAXXZ   retail 0x1800f83f0, ord 3396
// ?Shutdown@Device@Video@@QEAAXXZ           retail 0x1800f7280, ord 4044 (thunk)
// ============================================================================
// Remove the two message callbacks, then release the DeviceData.
//
// The two removals are `UnregisterMessageCallback` INLINED — retail open-codes
// the descent and tail-calls the erase (0x18000aba0) rather than calling
// 0x18009fa20. Written out here the same way, against the same ported erase, so
// the shape matches; calling our own UnregisterMessageCallback would be
// equivalent today and would hide a future divergence between the two copies.
//
// It re-reads `Application::_singleton` between the two removals (0x1800f842f),
// which is preserved.
// ============================================================================
namespace {
// The inlined descent-and-erase, spelled once because retail spells it twice.
//
// The compare is UNSIGNED and the operand order is INVERTED relative to
// `UnregisterMessageCallback`: retail loads the node's key into EAX and does
// `CMP EAX,<id> / JA left` (0x1800f8412), where `UnregisterMessageCallback`
// does `CMP <id>,EAX / JC left` (0x18009fa31). The two are the same ordering
// written from opposite sides, and both are transcribed as written rather than
// normalised into one helper shared with the Application file.
void UnregisterInline(rtsetavl::Set* set, unsigned id) {
    for (rtsetavl::Node* n = set->root; n; ) {
        const unsigned key = rtsetavl::ElementOf< ::Application::Application::MessageCallbackEntry>(n).message;
        if (key > id)       n = n->left;                       // JA
        else if (key >= id) { rtsetavl::Erase(set, n); return; }  // JNC -> equal
        else                n = n->right;
    }
}
}  // namespace

void Video::Device::PlatformShutdown() {
    // 0x1800f83f6 / 0x1800f842f: the singleton is read, used, and RE-READ
    // between the two removals. Preserved — see buffers_lock.cpp's note on the
    // same pattern in Device::Unlock.
    // B.4 (R-ENG): `Application::_messageCallbacks` is the rtsetavl::Set at +0x18.
    {
        ::Application::Application* app = ::Application::Application::_singleton;
        UnregisterInline(&app->_messageCallbacks, Platform::Window::kMsgSize);           // WM_SIZE
    }
    {
        ::Application::Application* app = ::Application::Application::_singleton;  // 0x1800f842f
        UnregisterInline(&app->_messageCallbacks, Platform::Window::kMsgExitSizeMove);   // WM_EXITSIZEMOVE
    }

    // 0x1800f8464: release the DeviceData — Shutdown (vtable +0x18, slot 3), then
    // the deleting dtor (vtable +0x00, flag 1), with the field re-read between and
    // nulled after. The same two-step CreateVideoInterface uses. ENDGAME B.3.5:
    // both are virtual calls on block A's real `Video::DeviceData` now.
    if (Video::DeviceData* dd = deviceData) {
        dd->Shutdown();
        dd = deviceData;                                       // 0x1800f8476
        if (dd)
            delete dd;   // B5 DEV-A (D10): slot 0, flags 1
        deviceData = nullptr;                                  // 0x1800f848c
    }
}

void Video::Device::Shutdown() {
    // 0x1800f7280: JMP 0x1800f83f0.
    Video::Device::PlatformShutdown();
}

// ============================================================================
// Retail names and addresses of this file's exported bodies (checked against
// the shipped export directory with pefile before any body was written):
//
//   ?PlatformStartup@Device@Video@@AEAA_NXZ          0x1800f8260  ord 3405
//   ?Startup@Device@Video@@QEAA_NXZ                  0x1800f7270  ord 4091
//   ?PlatformShutdown@Device@Video@@AEAAXXZ          0x1800f83f0  ord 3396
//   ?Shutdown@Device@Video@@QEAAXXZ                  0x1800f7280  ord 4044
//   ?ValidateConfiguration@Device@Video@@QEAA_NXZ    0x1800f8830  ord 4533
//   ?CreateVideoInterface@Device@Video@@QEAA_NH@Z    0x1800f7930  ord 2140
//   ?SizeWindow@Device@Video@@SAHPEAUHWND__@@I_K_J@Z 0x1800f7d20  ord 4064
//   ?MoveWindow@Device@Video@@SAHPEAUHWND__@@I_K_J@Z 0x1800f7d90  ord 3235
//   ?GetDefaultResolution@Device@Video@@QEBAHAEBUAdapter@12@@Z
//                                                    0x1800f7540  ord 2559
//
// Run identity. B3R W2-RT: this was RtVideoDeviceStartupRegister(), which
// registered nothing any more. The one line it printed is kept, printed from
// this translation unit (its __DATE__ / __TIME__ are this file's), and
// RuntimeInit() (src/app/runtime_init.cpp) calls it once at boot.
// ============================================================================
void RtVideoDeviceStartupIdentity() {
    // ---- RUN IDENTITY -----------------------------------------------------
    // Four lanes independently found that the on-disk md5 says which exe is ON
    // DISK, not which one RAN: six lanes deploy to one game directory and a
    // concurrent deploy between the copy and the launch produces a complete,
    // plausible log from somebody else's binary. One lane got a whole summary
    // that way.
    //
    // So this line is emitted by THIS FILE, which no other lane has. Its
    // timestamp is this translation unit's compile time, so it changes whenever
    // this lane rebuilds. Every measurement in WAVE9_LANE_VID2.md is gated on
    // finding this exact string in the run's own log — an md5 check before and
    // after is necessary but not sufficient, because it cannot see a swap that
    // happened and was undone.
    printf("[LaneVID2] identity: device_startup.cpp built " __DATE__ " " __TIME__ "\n");
}
