// ============================================================================
// src/engine/video/device.cpp — Wave 6 lane B
//   Video::Device + Video::Configuration + Video::RenderBuffer +
//   Video::RenderStateData
// ============================================================================
//
// Ports 9 of the 15 FUNCTIONS this lane owns (the 17th and 16th rows of
// docs/history/runtime_port/wave6_lanes.txt are DATA symbols, see below). Layouts and
// the evidence for every offset are in src/engine/video/Device.h; the
// per-function write-up, the oracle arguments and the six hold-backs are in
// docs/history/runtime_port/WAVE6_LANE_B.md.
//
//   | Export                                    | Retail      | Ord  | Oracle              |
//   |-------------------------------------------|-------------|------|---------------------|
//   | ?GetInterfaceCount@Device@Video@@...      | 0x18000d3b0 | 2729 | FULL scalar         |
//   | ?GetInterface@Device@Video@@...           | 0x18000d3c0 | 2728 | FULL scalar (ptr)   |
//   | ?GetMultisampleModeCount@Device@Video@@...| 0x18000d450 | 2803 | FULL scalar         |
//   | ?IsMultisampleModeSupported@Device@...    | 0x18000d460 | 3145 | FULL scalar         |
//   | ?ValidateConfiguration@Device@Video@@...  | 0x1800f7370 | 4532 | FULL (scratch args) |
//   | ?ApplyChangedSettings@Device@Video@@...   | 0x1800f8110 | 1604 | FULL (scratch this) |
//   | ?Create@RenderBuffer@Video@@...           | 0x1800f3f10 | 1882 | FULL (scratch this) |
//   | ??1RenderStateData@Video@@UEAA@XZ         | 0x1800f8d70 |  662 | RETIRED (B.3.9 VID) |
//   | ?Save@Configuration@Video@@QEAAXXZ        | 0x1800f6d10 | 3605 | poison + readback   |
//
// HELD BACK (reasons in full in WAVE6_LANE_B.md §2):
//   ?Startup@Device@Video@@QEAA_NXZ            0x1800f7270  ord 4091
//   ?Shutdown@Device@Video@@QEAAXXZ            0x1800f7280  ord 4044
//   ?RecreateDynamic@Device@Video@@QEAAXXZ     0x1800f80b0  ord 3522
//   ?ReleaseDynamic@Device@Video@@QEAAXXZ      0x1800f80e0  ord 3536
//   ??0RenderBuffer@Video@@QEAA@XZ             0x1800f3ee0  ord  346
//   ??1RenderBuffer@Video@@QEAA@XZ             0x1800f4020  ord  659
//
// NOT FUNCTIONS, so not registrable before cutover (PLAN §2.3 / the Wave 4
// FileSystem::_singleton precedent):
//   ?_singleton@Configuration@Video@@0PEAV12@EA  0x1801fe7a0  ord 4804
//   ?_singleton@Device@Video@@0PEAV12@EA         0x1801fe7a8  ord 4807
// They are DATA exports. `RegisterRuntimeExport` installs a function pointer
// that ResolveExport hands back to a caller which then TAKES THE ADDRESS of the
// variable — registering one would hand out a pointer to our code as if it were
// the Device slot. Both stay on the DLL and both are counted as UNPORTED.
//
// All 17 mangled names were checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE any body was written
// (reference_resolveexport_phantom_names.md: a name the DLL does not contain
// resolves to NULL in silence, the port still registers, and the census still
// counts it). The retail addresses and ordinals above are that query's output.
//
// WHAT ELSE STAYS IN THE DLL, AND WHY
// -----------------------------------
// `Video::Device` is a singleton our own code already reaches into by hand:
// src/app/video_interface.cpp DETOURS Video::Device::InitializeInterfaces
// and writes the DX11 plugin slot directly at Device+0xb0, and src/render/dx11/ reads
// Device+0x100. Every one of those offsets is asserted in
// src/engine/video/Device.h, so this port and those writers agree by
// construction. None of the nine bodies below allocates, frees, stamps a
// vtable, or changes any layout — they read the Device and write only fields
// retail writes — so the "two implementations on one live object" hazard of
// WAVE6.md's opening section does not arise for this lane.
//
// PLATFORM. Nothing here includes <windows.h>. The Win32 surfaces of this
// class — the Device critical section at +0x60, LoadLibraryW in
// InitializeInterfaces, the Application message callbacks in Startup — all sit
// inside functions this lane holds back, so no `device_win.cpp` twin was needed
// (PLAN §4.1 decision 1).
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "video/Device.h"
#include "video/validate_configuration.h"   // the refresh / multisample clamps (review F-130, F-156)
#include "video/ImageBuffer.h"
#include "video/CompanionData.h"   // ENDGAME B.3.5: Video::RenderBufferData
#include "io/Persistence.h"        // ENDGAME B.3.9 SS1: Persistence::_singleton

using Video::Configuration;
using Video::Device;
using Video::RenderBuffer;
using Adapter           = Video::Device::Adapter;
using DisplayMode       = Video::Device::DisplayMode;
using GraphicsInterface = Video::Device::GraphicsInterface;
using RefreshRate       = Video::Device::RefreshRate;

// ----------------------------------------------------------------------------
// B.4 (R-VIDEO): the three singleton-slot accessors are gone — the bodies read
// the class statics `Device::_singleton`, `Configuration::_singleton` and
// `IO::Persistence::_singleton` by name. `Configuration::Startup` calls the two
// `IO::Persistence::Get` members (retail 0x1800b81e0 int, 0x1800b8290 bool)
// directly instead of their `RtIo_Persistence_Get*` forwarders.
//
// CONTRACT THIS FILE DEPENDS ON: both Gets write through `out` only on a hit
// and leave the caller's value untouched on a miss. That is retail's behaviour
// and it is what makes an absent registry key leave the ctor default in place.
// ----------------------------------------------------------------------------

// ?_singleton@Device@Video@@0PEAV12@EA (0x1801fe7a8) — the class static
// (ENDGAME B.3.9 VID, B39_RULES D7; the minted row is redirected onto it).
Video::Device* Video::Device::_singleton = nullptr;
// ?_singleton@Configuration@Video@@0PEAV12@EA (0x1801fe7a0) — likewise.
Video::Configuration* Video::Configuration::_singleton = nullptr;

// ============================================================================
// Video::Device::GetInterfaceCount  —  retail 0x18000d3b0, ordinal 2729
//
//   1800d3b0  MOV EAX, dword ptr [RCX + 0x48]
//   1800d3b4  RET
// ============================================================================
int Video::Device::GetInterfaceCount() const {
    return interfaceCount;
}

// ============================================================================
// Video::Device::GetInterface  —  retail 0x18000d3c0, ordinal 2728
//
// Returns a reference, i.e. an interior pointer into the Array's storage. No
// bounds check: retail indexes straight off Array::_data, and
// src/game/Dialog/optionsdialog.cpp relies on that (it feeds the result of
// GetInterfaceCount straight back in). A guard here would not be faithful and
// would not help — see CLAUDE.md "Don't 'fix' a faithful crash with a null
// check".
//
// The MEMBER returns the reference retail's `AEBU` mangling says it returns; the
// DOOR keeps the pointer its callers have always received, which is the same
// eight bytes on this ABI.
// ============================================================================
const Video::Device::GraphicsInterface& Video::Device::GetInterface(int index) const {
    return interfaceData[index];
}

// ============================================================================
// Video::Device::GetMultisampleModeCount  —  retail 0x18000d450, ordinal 2803
// ============================================================================
int Video::Device::GetMultisampleModeCount() const {
    return multisampleCount;
}

// ============================================================================
// Video::Device::IsMultisampleModeSupported  —  retail 0x18000d460, ordinal 3145
//
//   return (bool)((char*)self->multisampleData)[mode];
//
// Retail loads ONE BYTE and returns it as bool, so a stored value other than
// 0/1 would propagate. Reproduced with uint8_t + `!= 0` — MSVC's `bool` return
// is the low byte either way, and every producer of this array stores 0 or 1.
// ============================================================================
bool Video::Device::IsMultisampleModeSupported(int mode) const {
    return multisampleData[mode] != 0;
}

// ============================================================================
// Video::Device::ValidateConfiguration  —  retail 0x1800f7370, ordinal 4532
//
// Clamps a renderer/adapter/resolution/refresh/antialias/filter combination to
// something the hardware actually offers. Reads only `this`; writes only
// through the eight int& out-parameters, which is what makes it fully diffable.
//
// TWO RETAIL ODDITIES, both reproduced:
//
//  1. The stack local at 0x1800f7417 (`FUN_180010760(&local_28)`) is an
//     inlined Collection::Array destructor on a local that was zeroed at
//     0x1800f73f6 and never filled. FUN_180010760 begins `if (*p == 0) goto
//     clear`, so on a zero pair it frees nothing. It is a dead
//     scope-exit call from an inlined helper, not part of the algorithm, and it
//     is omitted here rather than emulated: emulating it would mean calling the
//     DLL allocator to free a null pointer.
//
//  2. The first refresh-rate loop compares the match index against -1 (
//     `if (iVar3 != -1)`), but the index it compares starts at 0 and only ever
//     increments, so the test is always true. That is a compiler artifact of a
//     `matchIndex != -1` guard written against a variable the optimiser proved
//     non-negative. The consequence is real and is kept: an EXACT refresh match
//     leaves *refreshNum / *refreshDen ALONE and skips the ratio fallback
//     entirely.
//
// CALLER NOTE: src/game/Dialog/optionsdialog.cpp:315 passes `&savedAdapter`
// as BOTH the 2nd and the 8th argument (retail does the same at exe
// 0x140072eb0). The diff harness below therefore rebuilds the caller's aliasing
// exactly rather than assuming eight distinct slots — with eight distinct
// scratch ints, the original would see a different world from ours and report
// a divergence that is an artifact of the harness.
// ============================================================================
// ----------------------------------------------------------------------------
// Video::Device::GetDefaultResolution — retail 0x1800f7540, ordinal 2559.
//
// ValidateConfiguration falls back to it when the requested resolution is not
// in the adapter's list. It is EXPORTED and public, so it is reachable by name;
// it is simply not one of the 17 functions this lane owns, and nothing in our
// tree calls it directly. Called at the DLL exactly as retail's own body does,
// the shape Wave 5 lane D used for Picking::Scene's internals and Wave 4 lane C
// used for Map_Find / Map_Insert.
//
// WAVE 9 LANE V2, 2026-09-07 — the door was wrong and the stated reason for it
// does not survive being checked. It read "the raw DLL door (not ResolveExport) on
// purpose: this is the DLL's implementation, unaffected by whether some later
// wave registers a port of it, which keeps this lane's oracle comparing like
// with like." But this helper has exactly ONE caller
// (ValidateConfigurationBody's resolution fallback), and that caller is the
// LIVE ported body, not the oracle — so it is a REAL DEPENDENCY, and pinning it
// to the DLL was pinning the live path, not an oracle. If a later wave ports
// GetDefaultResolution, the live path SHOULD pick that up; the diff harness
// further down still calls retail's ValidateConfiguration whole, which is where
// like-for-like actually comes from.
//
// The concrete hazard is cutover: the raw DLL door returns NULL with
// Runtime-x64.dll absent, so raw here is a null call on the fallback path.
// ENDGAME B.3.5 block D2: `GetDefaultResolution` IS ported and registered today
// (src/engine/video/device_startup.cpp, by name AND by address), and it is a
// member of this very class — so the resolution fallback below is now a DIRECT
// MEMBER CALL instead of an `RT_EXPORT_ONCE` lookup. That is the step-5 rule for
// a runtime-internal caller inside the block's own file set, and it makes the
// dependency a link-time fact rather than a name resolved at run time. The
// argument order is unchanged: retail passes `this` in RCX even though
// 0x1800f7540's first instruction is `MOV RDI,RDX` and RCX is dead from there.
// ----------------------------------------------------------------------------

void Video::Device::ValidateConfiguration(int& renderer, int& adapterIdx,
                                          int& width, int& height,
                                          int& refreshNum, int& refreshDen,
                                          int& antialias, int& filter) {
    if (renderer < 0 || interfaceCount <= renderer)
        renderer = 0;
    const GraphicsInterface* iface = interfaceData + renderer;

    if (adapterIdx < 0 || iface->adapterCount <= adapterIdx)
        adapterIdx = 0;
    const Adapter* adapter = iface->adapterData + adapterIdx;

    // Find the display mode matching the requested width/height.
    int modeIndex = -1;
    for (int i = 0; i < adapter->modeCount; ++i) {
        const DisplayMode* m = adapter->modeData + i;
        if (width == m->width && height == m->height) { modeIndex = i; break; }
    }
    if (modeIndex == -1) {
        modeIndex = Video::Device::GetDefaultResolution(*adapter);
        width  = adapter->modeData[modeIndex].width;
        height = adapter->modeData[modeIndex].height;
    }

    const DisplayMode* mode = adapter->modeData + modeIndex;

    // Exact refresh match wins outright and leaves the pair untouched
    // (see oddity 2 in the banner).
    bool exact = false;
    for (int i = 0; i < mode->refreshCount; ++i) {
        const RefreshRate* r = mode->refreshData + i;
        if (r->numerator == refreshNum && r->denominator == refreshDen) { exact = true; break; }
    }
    if (!exact) {
        // Otherwise take the first entry with the same integer Hz, else entry 0.
        const int pick = Video::ValidateConfig::PickRefreshByRatio(mode->refreshData, mode->refreshCount,
                                                                   refreshNum, refreshDen);
        refreshNum = mode->refreshData[pick].numerator;
        refreshDen = mode->refreshData[pick].denominator;
    }

    antialias = Video::ValidateConfig::ClampMultisample(adapter->multisampleData, adapter->multisampleCount,
                                                        antialias);
    if (adapter->maxFilterLevel < filter)
        filter = adapter->maxFilterLevel;
}

// ============================================================================
// Video::Device::ApplyChangedSettings  —  retail 0x1800f8110, ordinal 1604
//
// Copies the resolution the player chose out of Video::Configuration into the
// Device's back-buffer dimensions, re-finds the matching display mode index,
// raises the "settings changed" byte and returns true unconditionally
// (`MOV AL,1` at 0x1800f816a — there is no failure path).
//
// Which pair it copies depends on Configuration::fullscreen: fullscreen takes
// width/height (+0x0c/+0x10), windowed takes windowWidth/windowHeight
// (+0x1c/+0x20). Retail re-tests the fullscreen byte for each of the two,
// which is a codegen artifact; one test is equivalent because nothing between
// them writes it.
//
// The mode search does NOT break on a match and does NOT reset the index when
// nothing matches: displayModeIndex keeps whatever it had. Both are retail.
// ============================================================================
bool Video::Device::ApplyChangedSettings() {
    const Configuration* cfg = Configuration::_singleton;

    const int width  = cfg->fullscreen ? cfg->width  : cfg->windowWidth;
    backBufferWidth = width;
    const int height = cfg->fullscreen ? cfg->height : cfg->windowHeight;
    backBufferHeight = height;

    for (int i = 0; i < modeCount; ++i) {
        const DisplayMode* m = modeData + i;
        if (width == m->width && height == m->height)
            displayModeIndex = i;
    }

    settingsChanged = 1;

    return true;
}

// ============================================================================
// Video::RenderBuffer::Create  —  retail 0x1800f3f10, ordinal 1882
//
// Records the colour and depth surfaces, derives the buffer's dimensions from
// whichever of them is non-null (colour wins), and tail-calls slot 1 of the
// platform object at +0x28 — which in this process is
// VideoDX11 RenderBufferData::Stub1, `mov al,1; ret`
// (src/render/dx11/smallclasses.cpp:12).
//
// The dimension rule is the same for width and height, driven by three bits of
// ImageBuffer::flags (+0x34) that src/engine/video/ImageBuffer.h did not
// previously document:
//
//   bit 8  -> the back buffer's size          (Device+0x04 / +0x08)
//   bit 9  -> half the back buffer's size     (signed /2, not >>1)
//   bit 6  -> the image's own size, shifted right by Configuration::shadowScalar
//   none   -> the image's own size
//
// Retail emits the width and height paths as two separately-scheduled copies
// of that rule with a shared tail; they compute the same thing.
// ============================================================================
namespace {

int RenderBufferExtent(uint32_t flags,
                       int32_t ownExtent, int32_t backBufferExtent,
                       const Configuration* cfg) {
    if (flags & Video::ImageBuffer::kViewportSize)     return backBufferExtent;
    if (flags & Video::ImageBuffer::kHalfViewportSize) return backBufferExtent / 2;
    int v = ownExtent;
    if (flags & Video::ImageBuffer::kShadowMap)
        v >>= (cfg->shadowScalar & 31);   // retail: SAR EDX,CL with CL = scalar & 0x1f
    return v;
}

}  // namespace

// ENDGAME B.3.5 block D2: the hand-inlined `(*(void***)platformData)[1]` tail
// call is a virtual call on `Video::RenderBufferData`'s slot 1. B.4 (R-VIDEO):
// the two attachments and `platformData` are typed fields and the ImageBuffer
// descriptor is read through the real class (`_flags`, `_width`, `_height`).
bool Video::RenderBuffer::Create(const ImageBuffer* color, const ImageBuffer* depth) {
    colorBuffer = color;
    depthBuffer = depth;

    const Device*        dev = Device::_singleton;
    const Configuration* cfg = Configuration::_singleton;
    const ImageBuffer*   src = color ? color : depth;
    const uint32_t       flags = src->_flags;

    width  = RenderBufferExtent(flags, src->_width,  dev->backBufferWidth,  cfg);
    height = RenderBufferExtent(flags, src->_height, dev->backBufferHeight, cfg);

    return platformData->Slot1();
}

// (The `??1RenderStateData@Video@@UEAA@XZ` door, retail 0x1800f8d70, is GONE:
// ENDGAME B.3.9 VID retired it with the RT_DELDTOR row that was its one caller.
// src/render/dx11/renderstatedata.cpp calls `Video::RenderStateData::~RenderStateData()`
// directly, RULE B5-3.)

// ============================================================================
// Video::Configuration::Save  —  retail 0x1800f6d10, ordinal 3605
//
// Eighteen IO::Persistence::Set calls under the section "Video". Each key
// string below was read out of the retail body's LEA operands, so the names are
// the DLL's own, not reconstructed.
//
// THIS IS NOT A REGISTRY WRITE. The wave brief for this lane said Save "writes
// the real HKCU registry" and should be held back on the Wave 4
// Persistence::WriteSettings precedent. That is wrong, and the correction is
// what makes this function portable: IO::Persistence::Set (0x1800b80c0 /
// 0x1800b8380) is an in-memory Collection::Set insert — Wave 4 lane C ported
// both overloads for exactly that reason and held back only ReadSettings /
// WriteSettings, which are the registry pair. Save touches no registry key;
// IO::Persistence::WriteSettings, called from elsewhere at shutdown, does.
//
// Set is IO::Persistence::Set, the member, called directly (the int and bool
// overloads are picked by the field's type, as retail's two call targets are).
// ============================================================================
namespace {

constexpr const char* kSectionVideo = "Video";

// Retail's 18 stores, in retail's order. The field is named by a pointer to
// member (B.4 R-VIDEO: it was a byte offset); an int field has `intField`, a
// bool one `boolField`, and which is set picks the Set overload. The offsets in
// the comments are the ones retail's LEA operands carry.
struct SaveField {
    const char* key;
    int32_t Configuration::* intField;
    uint8_t Configuration::* boolField;
};

constexpr SaveField kSaveFields[] = {
    { "Interface",        &Configuration::interfaceIndex,     nullptr },   // +0x04
    { "Adapter",          &Configuration::adapterIndex,       nullptr },   // +0x08
    { "Width",            &Configuration::width,              nullptr },   // +0x0C
    { "Height",           &Configuration::height,             nullptr },   // +0x10
    { "WindowWidth",      &Configuration::windowWidth,        nullptr },   // +0x1C
    { "WindowHeight",     &Configuration::windowHeight,       nullptr },   // +0x20
    { "WindowX",          &Configuration::windowX,            nullptr },   // +0x14
    { "WindowY",          &Configuration::windowY,            nullptr },   // +0x18
    { "RefreshN",         &Configuration::refreshNumerator,   nullptr },   // +0x38
    { "RefreshD",         &Configuration::refreshDenominator, nullptr },   // +0x3C
    { "Multisample",      &Configuration::multisample,        nullptr },   // +0x24
    { "Fullscreen",       nullptr, &Configuration::fullscreen },           // +0x40
    { "VSync",            nullptr, &Configuration::vsync },                // +0x41
    { "TextureFilter",    &Configuration::textureFilter,      nullptr },   // +0x28
    { "ShadowScalar",     &Configuration::shadowScalar,       nullptr },   // +0x2C
    { "ShaderDetail",     &Configuration::shaderDetail,       nullptr },   // +0x30
    { "ReflectionDetail", &Configuration::reflectionDetail,   nullptr },   // +0x34
    { "Settings",         nullptr, &Configuration::settingsValid },        // +0x00
};
constexpr int kSaveFieldCount = (int)(sizeof(kSaveFields) / sizeof(kSaveFields[0]));

}  // namespace

void Video::Configuration::Save() {
    IO::Persistence* store = IO::Persistence::_singleton;

    for (int i = 0; i < kSaveFieldCount; ++i) {
        const SaveField& f = kSaveFields[i];
        if (f.boolField)
            store->Set(kSectionVideo, f.key, (this->*f.boolField) != 0);
        else
            store->Set(kSectionVideo, f.key, static_cast<int>(this->*f.intField));
    }
}

// ============================================================================
// bool Video::Configuration::Startup()
// retail 0x1800f6680, ord 4087 — the exact INVERSE of Save above, and the
// second of Engine::Startup's two Video::Configuration calls
// (src/engine/engine/engine_lifecycle.cpp:653, which is where the cutover
// residue reports this name).
//
// Retail reads the SAME 18 keys, from the SAME "Video" section, in the SAME
// order as Save writes them, so this body iterates `kSaveFields` rather than
// carrying a second table. That is not a tidy-up: it makes the load/store
// agreement compiler-enforced instead of comment-asserted, which is the one
// property a table typed twice cannot have. The order was read off the
// disassembly independently and then checked against kSaveFields entry by
// entry — all 18 agree in key, type and offset.
//
// Every one of the 18 offsets is pinned a THIRD time, independently of both
// this function and Save, by Configuration's own exported accessors at
// 0x18000d5a0..0x18000d6c0 — sixteen-byte stubs that are each a single
// `MOV EAX,[RCX+off]` / `MOVZX EAX,byte [RCX+off]`. They agree with
// Device.h's struct on all 18. (They also name +0x42, which Device.h calls
// `field42`: 0x18000d6c0 is ?HideWindow@Configuration@Video@@QEBA_NXZ. Startup
// does not read it and Save does not write it, so it stays out of the table —
// reported, not renamed, since Device.h is not this lane's file.)
//
// RETAIL INLINES the int reads rather than calling ?Get@Persistence@IO@@…AEAH:
// String::Format("%s%s", section, key) -> Map_Find (0x18002ace0) -> on a hit,
// atoi of the node's value (0x18007f034, which is a bare
// `XOR EDX,EDX; LEA R8D,[RDX+0xa]; JMP strtol`). That is instruction-for-
// instruction what the ported IO::Persistence::Get(int&) already does, so this
// body calls the port. The bool reads are NOT inlined in retail — it makes a
// real CALL to 0x1800b8290 — and that address is `RtIo_Persistence_GetBool`.
// Both are called as C++ symbols, not through a resolver, for the reason lane
// VID gives for ImageBuffer::Width: the dependency is then checked by the
// oracle those functions already carry.
//
// FAITHFUL DETAILS:
//  * A key that is ABSENT leaves the field at whatever the ctor put there.
//    Retail achieves that by only storing inside the `if (node)`; ours by
//    Get's documented contract of writing *out only on a hit. This is why the
//    oracle below must RESTORE the object before replaying retail — see there.
//  * The persistence singleton pointer is read and used UNGUARDED, as retail
//    does (0x1800f66a3 `MOV RDI,[0x1801fc6b8]`, then `MOV RCX,RDI` into the
//    find with no test). No null check is added.
//  * The return value is unconditionally `true` (0x1800f6ce4 `MOV AL,0x1`).
//    Nothing here can fail, including a completely empty registry.
// ============================================================================
bool Video::Configuration::Startup() {
    const IO::Persistence* persistence = IO::Persistence::_singleton;

    int found = 0;
    for (int i = 0; i < kSaveFieldCount; ++i) {
        const SaveField& f = kSaveFields[i];
        if (f.boolField) {
            uint8_t& field = this->*f.boolField;
            bool v = (field != 0);
            if (persistence->Get(kSectionVideo, f.key, v)) ++found;
            field = static_cast<uint8_t>(v ? 1 : 0);
        } else {
            int32_t& field = this->*f.intField;
            int v = field;
            if (persistence->Get(kSectionVideo, f.key, v)) ++found;
            field = v;
        }
    }

    return true;   // 0x1800f6ce4: MOV AL,0x1 — unconditional
}
