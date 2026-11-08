// src/ext/render_backend/render_backend.cpp -- NOT RETAIL. ext.render_backend (render_backend.h has the
// contract)

#include "ext/render_backend/render_backend.h"

#include "ext/registry.h"
#include "ext/render_pipe/render_pipe.h"   // Ext::RenderPipe::SettleBeforeStepWrite

#include "engine/MainLoop.h"
#include "io/settings_store.h"   // IO::SettingsStore (SeedFreshInstall, before Engine::Startup)
#include "render/common/render_backend.h"   // Render::BackendCount / SelectBackend / ... (by path: this directory has its own render_backend.h)
#include "system/Error.h"   // System::Error::SetText / Display (SwitchNow with no backend left)
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "ui/ComboElement.h"
#include "ui/DialogInstance.h"
#include "ui/Sprite.h"
#include "video/Device.h"

#include "platform_window.h"   // Platform::Window::IsIconic (SwitchNow's deferral)

#include <cstdio>
#include <cstring>
#include <string>

namespace Ext {
namespace RenderBackend {

namespace {

// ext.render_api's values, by backend directory name (render_backend.h's table).
constexpr const char* kApiIds[] = {"dx11", "vulkan"};
constexpr int32_t kApiCount = 2;

// The interface slot the dual exe's backends share (Video::Device::dllSlots[1], retail's DX11 slot).
constexpr int kSharedSlot = 1;

const ValueKey& ApiKey() {
    static const ValueKey* k = FindValueKey("render_api");
    return *k;
}

// The row and the value: the installed registry once Ext::InitFromSettings ran, else settings.cfg and
// the command line resolved now (BootSelect runs inside Engine::Startup, before the install).
bool RowOn() {
    if (Installed()) return Ext().render_backend;
    static const State boot = ResolveFromSettings();
    return boot.ext.render_backend;
}

int32_t ApiValue() {
    if (Installed()) return Val().render_api;
    static const State boot = ResolveFromSettings();
    return boot.val.render_api;
}

// The backend ext.render_api names, or -1 when this exe does not carry it.
int BackendForApi(int32_t api) {
    if (api < 0 || api >= kApiCount) return -1;
    return Render::FindBackend(kApiIds[api]);
}

int32_t ApiForBackend(int backend) {
    const char* id = Render::BackendId(backend);
    if (id == nullptr) return -1;
    for (int32_t a = 0; a < kApiCount; ++a) {
        if (std::strcmp(id, kApiIds[a]) == 0) return a;
    }
    return -1;
}

// The backends whose CreateDevice failed since the last deliberate pick (bit per index): the fallback
// never returns to one of them.
uint32_t g_failed = 0;

// A boot that started another backend than the wanted one (BootSelect's probe, RetryInterface): saved by
// SaveFallback once the device is up.
bool g_bootFellBack = false;

// The backend SwitchNow was asked for while the window was minimised, -1 = none (RunPendingSwitch).
int g_pendingSwitch = -1;

bool g_listing = false;   // inside ListAdaptersAgain (ListingAdapters)

// The interface list (Video::Device +0x40) emptied and listed again by every slot's DeviceData, the
// active backend's in slot 1: Device::EnumerateVideoModes (0x1800f8610) appends one GraphicsInterface
// per slot to an empty list, which retail does once, inside Engine::Startup. Only while no device
// exists. The Device's own mode / multisample arrays are rebuilt by the next CreateDevice, and
// Device::PlatformStartup validates the configuration against the new list before it makes one.
void ListAdaptersAgain() {
    Video::Device* dev = Video::Device::_singleton;
    if (dev == nullptr) return;
    RtVideo_ArrayInterfaceElemDestroy(dev->interfaceData, dev->interfaceCount);
    System::MemoryAllocator::MemFree(dev->interfaceData);
    dev->interfaceData = nullptr;
    dev->interfaceCount = 0;
    dev->interfaceCapacity = 0;
    g_listing = true;
    dev->EnumerateVideoModes();
    g_listing = false;
    std::fprintf(stderr, "[RenderBackend] adapters listed again: %d interface(s), slot 1 = %s\n",
                 dev->interfaceCount,
                 dev->interfaceCount > kSharedSlot ? dev->interfaceData[kSharedSlot].name._data : "(none)");
}

// The next backend to try after `from` failed, in the exe's order: available and not failed since the
// last pick. -1 when none is left.
int NextBackend(int from) {
    const int n = Render::BackendCount();
    for (int step = 1; step < n; ++step) {
        const int b = (from + step) % n;
        if (g_failed & (1u << b)) continue;
        if (!Render::BackendAvailable(b)) {
            g_failed |= 1u << b;
            continue;
        }
        return b;
    }
    return -1;
}

// ---- the Options dialog's combo -------------------------------------------------------------------
const UI::DialogInstance* g_dialog = nullptr;   // the one open Options dialog (it is modal)
UI::ComboElement* g_combo = nullptr;
int32_t g_firstBackendItem = -1;   // the combo item of backend 0; backend b is item g_firstBackendItem + b
int g_picked = -1;                 // ReadVideoRow's backend, -1 = no change
int32_t g_committedFrom = -1;      // ext.render_api before CommitVideoRow, -1 = no commit

}  // namespace

bool Offered() {
    return Render::BackendCount() > 1 && RowOn();
}

void SeedFreshInstall() {
    if (Render::BackendCount() < 2) return;
    std::string path;
    IO::SettingsFile::Entries entries;
    if (!IO::SettingsStore::DefaultPath(path)) return;
    const IO::SettingsStore::Source source = IO::SettingsStore::Load(path, entries);
    if (source == IO::SettingsStore::Source::File) return;   // an existing player: the file decides
    if (source == IO::SettingsStore::Source::None) entries.clear();
    for (const IO::SettingsFile::Entry& e : entries) {
        if (e.key == "ext.render_backend") return;   // the import already says
    }
    // A retail registry import counts as a fresh install (ruling 2026-11-08): the file is written as the
    // imported set plus the two keys, which the engine's ReadSettings then reads instead of importing again.
    entries.push_back(IO::SettingsFile::Entry{"ext.render_api", "1"});
    entries.push_back(IO::SettingsFile::Entry{"ext.render_backend", "1"});
    const bool ok = IO::SettingsStore::Store(path, entries);
    std::fprintf(stderr, "[RenderBackend] fresh install (%s): %s ext.render_backend=1 ext.render_api=1 (Vulkan first)\n",
                 source == IO::SettingsStore::Source::Legacy ? "retail registry imported" : "no settings",
                 ok ? "settings.cfg written with" : "FAILED to write");
}

bool SaveFallback() {
    if (!g_bootFellBack) return false;
    g_bootFellBack = false;
    if (CurrentProfile() == Profile::Faithful) return true;   // the faithful profile always wants DirectX 11
    const int32_t api = ApiForBackend(Render::ActiveBackend());
    if (api < 0) return true;
    SetValueAndRecord(ApiKey(), api);
    if (!Ext().render_backend) SetAndRecord(*FindEntry("render_backend"), true);
    PersistSettings();
    std::fprintf(stderr, "[RenderBackend] fallback saved: ext.render_backend=1 ext.render_api=%d (%s), the next "
                         "launch starts on it\n",
                 api, Render::BackendId(Render::ActiveBackend()));
    return true;
}

void BootSelect() {
    static bool done = false;
    if (done) return;
    done = true;
    if (Render::BackendCount() < 2) return;
    Video::Device::_extInterfaceRetry = &RetryInterface;
    g_failed = 0;
    // The row off (or the faithful profile): retail's renderer.
    int wanted = Render::FindBackend("dx11");
    if (RowOn()) {
        const int b = BackendForApi(ApiValue());
        if (b >= 0) wanted = b;
    }
    if (wanted < 0) wanted = 0;
    int pick = wanted;
    if (!Render::BackendAvailable(pick)) {
        g_failed |= 1u << pick;
        pick = NextBackend(pick);
        if (pick < 0) pick = wanted;   // nothing else: let CreateDevice fail with the wanted one's error
    }
    Render::SelectBackend(pick);
    std::fprintf(stderr, "[RenderBackend] boot: row %s, wanted %s, starting %s%s\n", RowOn() ? "on" : "off",
                 Render::BackendId(wanted), Render::BackendId(pick),
                 pick != wanted ? " (FALLBACK: the wanted backend cannot start here; saved once the device is up)" : "");
    if (pick != wanted) g_bootFellBack = true;
}

bool RetryInterface(int index) {
    if (index != kSharedSlot || Render::BackendCount() < 2) return false;
    const int failed = Render::ActiveBackend();
    g_failed |= 1u << failed;
    const int next = NextBackend(failed);
    if (next < 0) {
        std::fprintf(stderr, "[RenderBackend] %s failed to start and no other backend is left\n",
                     Render::BackendId(failed));
        return false;
    }
    std::fprintf(stderr, "[RenderBackend] FALLBACK: %s failed to start (CreateDevice); trying %s\n",
                 Render::BackendId(failed), Render::BackendId(next));
    if (!Render::SelectBackend(next)) return false;
    g_bootFellBack = true;   // SaveFallback records it once the device is up
    ListAdaptersAgain();
    return true;
}

bool SwitchNow(int index) {
    if (index < 0 || index >= Render::BackendCount() || index == Render::ActiveBackend()) return false;
    Video::Device* vidDev = Video::Device::_singleton;
    Video::Configuration* vidCfg = Video::Configuration::_singleton;
    Engine::MainLoop* mainLoop = Engine::MainLoop::_singleton;
    if (vidDev == nullptr || vidCfg == nullptr) return false;
    if (Platform::Window::IsIconic(vidDev->windowHandle)) {
        if (g_pendingSwitch != index) {
            std::fprintf(stderr, "[RenderBackend] switch to %s deferred: the window is minimised\n",
                         Render::BackendId(index));
        }
        g_pendingSwitch = index;
        return false;
    }
    g_pendingSwitch = -1;
    const int from = Render::ActiveBackend();
    std::fprintf(stderr, "[RenderBackend] switch %s -> %s: the full video restart\n",
                 Render::BackendId(from), Render::BackendId(index));
    // ApplyVideoSettings' renderer-change arm (exe 0x140071e10), in its order.
    Ext::RenderPipe::SettleBeforeStepWrite();
    if (mainLoop != nullptr) mainLoop->OnVideoError();
    vidDev->ReleaseDynamic();
    vidDev->Shutdown();
    g_failed = 0;
    g_bootFellBack = false;
    Render::SelectBackend(index);
    ListAdaptersAgain();
    bool success = vidDev->Startup();
    if (!success) {
        // No backend started (the fallback included): the one that was running, again.
        std::fprintf(stderr, "[RenderBackend] switch to %s started no backend: %s again\n",
                     Render::BackendId(index), Render::BackendId(from));
        g_failed = 0;
        Render::SelectBackend(from);
        ListAdaptersAgain();
        success = vidDev->Startup();
    }
    if (!success) {
        // No device: the end retail gives a device it cannot make again (see SwitchNow in the header).
        std::fprintf(stderr, "[RenderBackend] switch: no backend could be started\n");
        System::Error::SetText("Video::Device: no renderer could be started after a renderer change.", true);
        System::Error::Display();
        return false;
    }
    vidDev->RecreateDynamic();
    if (mainLoop != nullptr) mainLoop->OnVideoRestart();
    vidCfg->Save();
    SaveFallback();   // the picked backend failed and another one started: keep the one that works
    std::fprintf(stderr, "[RenderBackend] switch done: OK, active %s%s\n", Render::BackendId(Render::ActiveBackend()),
                 Render::ActiveBackend() == index ? "" : " (the picked backend did not start)");
    return Render::ActiveBackend() == index;
}

bool ListingAdapters() {
    return g_listing;
}

void RunPendingSwitch() {
    if (g_pendingSwitch < 0) return;
    const Video::Device* vidDev = Video::Device::_singleton;
    if (vidDev == nullptr || Platform::Window::IsIconic(vidDev->windowHandle)) return;
    const int index = g_pendingSwitch;
    std::fprintf(stderr, "[RenderBackend] the window is restored: the deferred switch to %s runs\n",
                 Render::BackendId(index));
    g_pendingSwitch = -1;
    if (index != Render::ActiveBackend()) SwitchNow(index);
}

void OpenVideoRow(UI::DialogInstance* dialog, UI::ComboElement* comboRenderer) {
    g_dialog = nullptr;
    g_combo = nullptr;
    g_firstBackendItem = -1;
    g_picked = -1;
    g_committedFrom = -1;
    if (!Offered() || dialog == nullptr || comboRenderer == nullptr) return;
    const Video::Device* vidDev = Video::Device::_singleton;
    if (vidDev == nullptr || vidDev->interfaceCount <= kSharedSlot) return;
    // Slot 1's one item replaced by one per backend, and nothing else: the other slots hold the index
    // placeholder ("DirectX 9", whose CreateDevice always fails; dx9_placeholder.cpp), which the dual exe
    // does not offer (maintainer ruling 2026-11-08: only DirectX 11 and Vulkan are listed). Every backend
    // item carries slot 1's index as its data, so retail's handlers (the Renderer combo's SelectionChanged,
    // ApplyVideoSettings) still read interface 1.
    comboRenderer->RemoveAllItems();
    int32_t select = -1;
    for (int i = 0; i < vidDev->interfaceCount; ++i) {
        UI::Sprite sprite;
        void* itemData = reinterpret_cast<void*>(static_cast<intptr_t>(i));   // CAST-OK: ptr_int (retail's item data)
        if (i != kSharedSlot) continue;
        for (int b = 0; b < Render::BackendCount(); ++b) {
            System::String name;
            name.Set(Render::BackendName(b));
            const int32_t item = comboRenderer->AddItem(sprite, name, itemData);
            if (b == 0) g_firstBackendItem = item;
            if (b == Render::ActiveBackend()) select = item;   // the device runs on slot 1 (the placeholder never starts)
        }
    }
    if (select >= 0) comboRenderer->SetCurrentItem(select);
    g_dialog = dialog;
    g_combo = comboRenderer;
    std::fprintf(stderr, "[RenderBackend] Video tab: %d renderer items, %s current\n", comboRenderer->GetItemCount(),
                 Render::BackendId(Render::ActiveBackend()));
}

bool ReadVideoRow(const UI::DialogInstance* dialog) {
    g_picked = -1;
    if (dialog == nullptr || dialog != g_dialog) return false;
    const int32_t item = g_combo->GetCurrentItem();
    const int b = item - g_firstBackendItem;
    if (item < g_firstBackendItem || b >= Render::BackendCount()) return false;
    if (b == Render::ActiveBackend()) return false;
    g_picked = b;
    return true;
}

void CommitVideoRow(const UI::DialogInstance* dialog) {
    if (dialog == nullptr || dialog != g_dialog) return;
    g_committedFrom = -1;   // a second Apply in the same dialog rolls back only its own change
    if (g_picked < 0) return;
    const int picked = g_picked;
    g_picked = -1;
    g_failed = 0;
    if (!Render::SelectBackend(picked)) return;   // a device still exists: the restart did not run
    const int32_t api = ApiForBackend(picked);
    if (api >= 0 && api != Val().render_api) {
        g_committedFrom = Val().render_api;
        SetValueAndRecord(ApiKey(), api);
    }
    std::fprintf(stderr, "[RenderBackend] Apply: %s picked, ext.render_api=%d\n", Render::BackendId(picked),
                 Val().render_api);
    ListAdaptersAgain();
}

void RollbackVideoRow(const UI::DialogInstance* dialog) {
    if (dialog == nullptr || dialog != g_dialog || g_committedFrom < 0) return;
    SetValueAndRecord(ApiKey(), g_committedFrom);
    std::fprintf(stderr, "[RenderBackend] the video restart failed: ext.render_api=%d again\n", g_committedFrom);
    g_committedFrom = -1;
}

void CloseVideoRow(const UI::DialogInstance* dialog) {
    if (dialog == nullptr || dialog != g_dialog) return;
    g_dialog = nullptr;
    g_combo = nullptr;
    g_firstBackendItem = -1;
    g_picked = -1;
    g_committedFrom = -1;
}

}  // namespace RenderBackend
}  // namespace Ext
