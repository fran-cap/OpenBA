// ============================================================================
// src/ext/registry_boot.cpp -- the registry's engine glue (exe only)
// ============================================================================
// NOT RETAIL. registry.h has the contract. Three things touch the engine:
//   * the settings map: IO::Persistence::_singleton->_settingsMap, filled from
//     settings.cfg by Persistence::ReadSettings during Engine::Startup;
//   * the command line: System::Configuration::_singleton, retail's own
//     `/key:value` parser (Configuration::SetCommandLine, configuration.cpp),
//     read for the one key `profile`;
//   * the write-back: Persistence::Set (the "%i" spelling) + WriteSettings.
// No getenv, no BANISHED_* flag (CLAUDE.md, DIRECTION.md §2).
// ============================================================================

#include "ext/registry.h"
#include "ext/draw_gather/draw_gather.h"   // ext.draw_gather: its engine seams follow the toggle
#include "ext/draw_batch/draw_batch.h"     // ext.draw_batch: the Renderer::_extDrawItemList seam follows the toggle
#include "ext/background_save/background_save.h"   // ext.background_save: the Save::SaveLoad seam follows the toggle
#include "ext/render_pipe/render_pipe.h"   // ext.render_pipe: the MainLoop::_extFrame seam follows the toggle
#include "ext/ambient_occlusion/ambient_occlusion.h"      // ext.ambient_occlusion: the readable depth + the arm follow the toggle
#include "ext/better_trees/better_trees.h"   // ext.better_trees: the render-side seams follow the toggle
#include "ext/seamless_loops/seamless_loops.h"   // ext.seamless_loops: the SoundBuffer decode seam
#include "ext/large_packages/large_packages.h"   // fix.large_packages: the FileSystem mount seam
#include "ext/package_cache/package_cache.h"       // ext.package_cache: the FileSystem index seams
#include "ext/menu_entry/menu_entry.h"             // ext.menu_entry: the mod scan and remount seams
#include "ext/phone_menu/phone_menu.h"             // ext.phone_menu: the menu safe area and backdrop seam
#include "ext/fps_cap/fps_cap.h"                     // ext.fps_cap: the frame limiter's settings cap
#include "ext/touch_pointer/touch_pointer.h"         // ext.touch_pointer: the Input::Device::_extAfterUpdate seam
#include "ext/decode_ahead/decode_ahead.h"       // ext.decode_ahead: the CompressedStream decode seam
#include "ext/async_gpu_create/async_gpu_create.h"   // ext.async_gpu_create: the backend's request follows the toggle
#include "ext/compress_ao/compress_ao.h"   // ext.compress_ao: the backend's request + the derived cache, at boot
#include "ext/share_gpu_data/share_gpu_data.h"   // ext.share_gpu_data: the backend's request, at boot
#include "ext/create_on_demand/create_on_demand.h"   // ext.create_on_demand: the backend's request + seams, at boot
#include "ext/texture_quality/texture_quality.h"     // ext.texture_quality: the run's drop + the level reader, at boot
#include "ext/stream_cache/stream_cache.h"           // ext.stream_cache: the resource-record seams, at boot
#include "ext/visit_replay/visit_replay.h"         // ext.visit_replay: the ExternalFactory::_extVisitReplay seam, at boot
#include "ext/find_fastpath/find_fastpath.h"       // ext.find_fastpath: the ExternalFactory::_extFindFast seam, at boot
#include "ext/heap_slack/heap_slack.h"             // ext.heap_slack: the MemoryAllocator::_extCommitSeam seam, at boot
#include "ext/outline_with_panel/outline_with_panel.h"   // fix.outline_with_panel: the EdgeMeshInstance::_extHoldDraw seam
#include "ext/save_failure_notice/save_failure_notice.h"   // fix.save_failure_notice: the save job / SaveLoad seams
#include "ext/resources/text_provider.h"        // the Extensions dialog / Enhanced tab resources from the exe's text (KITFREE)
#include "rsc_text_check.h"                      // src/diag: the kit cross-check, /rsccheck:1 on the command line
#include "ext/resources/font_provider.h"         // KITFREE RSCFONT: ext.sharp_ui's font variants, provided at run time
#include "ext/resources/ext_sprites.h"           // KITFREE: the Extensions sprite sheet provider

#include <cstdio>
#include <string>

#include <cstddef>
#include <vector>

#include "audio/SoundBuffer.h"  // the ext.seamless_loops seam (DESIGN.md hook rule 5)
#include "graphics/SunLight.h"  // the ext.max_shadow_res seam (DESIGN.md hook rule 5)
#include "graphics/FrameCopyImageBuffer.h"   // the ext.better_water scene depth seam (DESIGN.md hook rule 5)
#include "ext/better_water/better_water.h"
#include "io/Persistence.h"
#include "save/SaveLoad.h"      // the pseudo-mod seam Save::SaveLoad::_extExtraMods (DESIGN.md hook rule 5)
#include "system/Configuration.h"
#include <cstring>
#include "system/String.h"
#include "ui/UserInterface.h"   // the fix.ui_scale_layout seam (DESIGN.md hook rule 5)
#include "ui/SliderElement.h"   // the fix.slider_drag_clamp seam (DESIGN.md hook rule 5)
#include "ui/ScrollElement.h"   // the fix.slider_drag_clamp seam, scroll-bar half
#include "engine/MainLoop.h"     // the fix.boot_device_lost seam (DESIGN.md hook rule 5)
#include "boot_device_lost/boot_device_lost_backend.h"   // fix.boot_device_lost: the backend half
#include "Game/WorldGeometry.h"  // the ext.terrain_batch seam (DESIGN.md hook rule 5)

namespace Ext {

namespace {

// Lookup over the live settings map: the raw value string.
bool PersistenceLookup(void* /*ctx*/, const char* key, std::string& value) {
    const ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (p == nullptr) return false;
    System::String k(key);
    const RtIo::SettingsNode* node = p->_settingsMap.Find(k._data);
    if (node == nullptr) return false;
    const char* v = node->element.value._data;
    value = v ? v : "";
    return true;
}

// The command line's `/profile:<name>`, or "" when absent.
std::string CommandLineProfile() {
    const System::Configuration* cfg = System::Configuration::_singleton;
    if (cfg == nullptr) return std::string();
    const System::String value = cfg->GetCommandLineString(System::String("profile"));
    return value._data ? std::string(value._data) : std::string();
}

// The engine's record and the registry's are the same five fields in the same
// order (registry.h EXT SEAM): asserted, so a field added on one side fails
// the build. The adapter copies field by field (no cast across the two types).
using EngineRecord = ::Save::SaveLoad::ExtraModRecord;
static_assert(sizeof(EngineRecord) == sizeof(PseudoModRecord), "pseudo-mod record size");
static_assert(offsetof(EngineRecord, path) == offsetof(PseudoModRecord, path), "path");
static_assert(offsetof(EngineRecord, name) == offsetof(PseudoModRecord, name), "name");
static_assert(offsetof(EngineRecord, field10) == offsetof(PseudoModRecord, field10), "+0x10");
static_assert(offsetof(EngineRecord, field14) == offsetof(PseudoModRecord, field14), "+0x14");
static_assert(offsetof(EngineRecord, formatVersion) == offsetof(PseudoModRecord, formatVersion),
              "+0x18");

int EngineExtraMods(EngineRecord* out, int capacity) {
    if (out == nullptr || capacity <= 0) return ProvideExtraModsForSave(nullptr, 0);
    std::vector<PseudoModRecord> recs(static_cast<size_t>(capacity));
    const int n = ProvideExtraModsForSave(recs.data(), capacity);
    for (int i = 0; i < n && i < capacity; ++i) {
        out[i].path          = recs[static_cast<size_t>(i)].path;
        out[i].name          = recs[static_cast<size_t>(i)].name;
        out[i].field10       = recs[static_cast<size_t>(i)].field10;
        out[i].field14       = recs[static_cast<size_t>(i)].field14;
        out[i].formatVersion = recs[static_cast<size_t>(i)].formatVersion;
    }
    return n;
}

}  // namespace

void InitFromSettings() {
    const std::string cmd = CommandLineProfile();
    LoadReport report;
    State requested = Resolve(&PersistenceLookup, nullptr, cmd.c_str(), &report);
    // Settings migration: ext.better_trees replaced ext.foliage_aa and ext.billboard_shadow. A file
    // that has neither a better_trees key but one of the old rows at 1 turns the new row on, and
    // writes it (the caller's flush persists it); the old keys are left in the file untouched.
    if (report.profile == Profile::PerEntry) {
        std::string v;
        if (!PersistenceLookup(nullptr, "ext.better_trees", v)) {
            std::string a, b;
            const bool oldOn = (PersistenceLookup(nullptr, "ext.foliage_aa", a) && a == "1") ||
                               (PersistenceLookup(nullptr, "ext.billboard_shadow", b) && b == "1");
            if (oldOn) {
                requested.ext.better_trees = true;
                if (::IO::Persistence* p = ::IO::Persistence::_singleton)
                    p->Set(KeyPrefix(Tier::Extension), "better_trees", 1);
                std::fprintf(stderr, "[Ext] migrated ext.foliage_aa / ext.billboard_shadow -> ext.better_trees=1\n");
            }
        }
    }
    Install(requested, report.profile, report.profileFromCommandLine);
    // Engine-side fix seams: null by default, pointed at the live bool here
    // (fixes are global and live: the pointer follows the menu toggle).
    ::UI::UserInterface::_extFixScaleLayout = &Fix().ui_scale_layout;
    ::UI::SliderElement::_extFixDragClamp = &Fix().slider_drag_clamp;
    ::UI::ScrollElement::_extFixDragClamp = &Fix().slider_drag_clamp;
    // fix.save_failure_notice: the save job's temporary file, and state 2's status read + notice.
    ::IO::PersistenceWorker::_extFixSaveTemp = &Fix().save_failure_notice;
    ::Save::SaveLoad::_extFixSaveResult = &Fix().save_failure_notice;
    ::Save::SaveLoad::_extSaveResult = &Ext::SaveFailureNotice::OnSaveResult;
    // fix.boot_device_lost: one row, two seams -- the engine's SaveLoad-walk skip and the
    // renderer backend's recovery of a reset that failed on a removed device.
    ::Engine::MainLoop::_extFixBootDeviceLost = &Fix().boot_device_lost;
    BootDeviceLostBackend::SetFixFlag(&Fix().boot_device_lost);
    // fix.large_packages: the IO::FileSystem::_extPackagesMounted seam (null under the faithful
    // profile). The boot's .pkg mount ran inside Engine::Startup, before this; every mods mount runs it.
    LargePackages::Sync();
    // fix.outline_with_panel: the Graphics::EdgeMeshInstance::_extHoldDraw seam (null under the
    // faithful profile); the hook reads the row each frame.
    OutlineWithPanel::Sync();
    // ext.package_cache: the IO::FileSystem::_extIndexRestore / _extIndexBuilt seams (null under the
    // faithful profile); the hooks read the row at each mount.
    PackageCache::Sync();
    // ext.menu_entry: the IO::Modification mod-scan / remount seams and IO::FileSystem::_extPackagesClosed
    // (null under the faithful profile); the hooks read the row at each scan and remount.
    MenuEntry::Sync();
    // The save's pseudo-mods (EXT SEAM, Save::SaveLoad::_extExtraMods). Installed
    // ONCE here: the profile is fixed for the run (settings key or /profile at
    // boot; nothing changes it later), so SaveModsProvider() is final now --
    // null under the faithful profile or while no B / C extension is registered.
    // A B / C toggle needs no re-install: the provider reports the ACTIVE set
    // and returns 0 records while none is on, so the engine appends nothing.
    ::Save::SaveLoad::_extExtraMods = SaveModsProvider() != nullptr ? &EngineExtraMods : nullptr;
    // ext.draw_gather (class A, live): the three Graphics::Scene seams (scene_ext.h).
    Ext::DrawGather::Sync();
    // ext.draw_batch (class A, live): the Graphics::Renderer::_extDrawItemList seam.
    Ext::DrawBatch::Sync();
    // ext.render_pipe (class A, live): the Engine::MainLoop::_extFrame seam.
    Ext::RenderPipe::Sync();
    // ext.phone_menu (class A, live): the UI::UserInterface::_extDialogPlace seam, and
    // _extDialogScale (its tall-dialog fit and ext.touch_mode's toolbar scale, TOUCH_MODE.md).
    Ext::PhoneMenu::Sync();
    // ext.phone_menu on a fixed surface (Android): VSync kept on (video_rows.cpp).
    Ext::PhoneMenu::KeepVSyncOnFixedSurface();
    // ext.fps_cap (class A, live): the frame limiter's settings cap (src/app/frame_limiter.h).
    Ext::FpsCap::Sync();
    // ext.touch_pointer (class A, live): the Input::Device::_extAfterUpdate seam (P11_TOUCH_POINTER.md).
    Ext::TouchPointer::Sync();
    // ext.ambient_occlusion (class A, live): the main depth's format + the per-frame arm.
    Ext::AmbientOcclusion::Sync();
    // ext.better_trees (class A, live): the VideoDX11::g_extFoliageAA seam + the mode, and the
    // billboard-shadow D3D stage (null seam while the row is off).
    BetterTrees::Sync();
    // ext.background_save (class A, live): the Save::SaveLoad::_extSaveStart / _extSavePoll seam.
    Ext::BackgroundSave::Sync();
    // ext.max_shadow_res (class A, live): the Graphics::SunLight seam follows the
    // row's live bool (a toggle rebuilds the map through Device::settingsChanged).
    // Installed once, like the save seam above: the profile is fixed for the run,
    // and under the faithful profile it stays null (the sun map pays one null test).
    ::Graphics::SunLight::_extMaxShadowRes =
        CurrentProfile() == Profile::Faithful ? nullptr : &Ext().max_shadow_res;
    // ext.max_shadow_size (FAA-SHADOW2): the size the row builds at High, 4096 or 8192 (the
    // registry's discrete key never holds another value). Same rule as the bool above.
    ::Graphics::SunLight::_extMaxShadowSize =
        CurrentProfile() == Profile::Faithful ? nullptr : &Val().max_shadow_size;
    // ext.terrain_batch (class A, live): the WorldGeometry seam follows the row's live bool. Installed
    // once, like the sun map's: under the faithful profile it stays null, so no run index buffer is
    // built and the terrain loop and draw pay one null test each.
    ::WorldGeometry::_extTerrainBatch =
        CurrentProfile() == Profile::Faithful ? nullptr : &Ext().terrain_batch;
    // ext.better_water (class A, live): the Graphics::FrameCopyImageBuffer::Generate seam, installed
    // once like the two above; the body itself follows the live level (it copies only while Fancy's
    // contact foam is on). Null under the faithful profile: the frame copy pays one null test.
    ::Graphics::FrameCopyImageBuffer::_extAfterCopy =
        CurrentProfile() == Profile::Faithful ? nullptr : &BetterWater::AfterFrameCopy;
    // ext.seamless_loops (class A, next start): the Audio::SoundBuffer::DecodeAtLoad seam, set ONCE
    // here from the boot value (it reads nothing live: a buffer already decoded keeps its PCM, so the
    // dialog's toggle is recorded for the next start). Null under the faithful profile (which turns the
    // row off) or with the row off: the decode pays one null test and its PCM is the plain decode.
    ::Audio::SoundBuffer::_extLoopSeam =
        Ext().seamless_loops ? &SeamlessLoops::ApplyToLoopingBuffer : nullptr;
    // ext.heap_slack (class A, next start): the System::MemoryAllocator::_extCommitSeam seam, set ONCE
    // here under the allocator lock. Null under the faithful profile or with the row off: a commit or
    // decommit pays one null test. Ranges already committed are taken over at their next mark move.
    HeapSlack::Sync();
    // ext.decode_ahead (class A, next start): the IO::CompressedStream::_extDecodeAhead seam, set ONCE
    // here (boot, before the loader's first resource). Null under the faithful profile or with the row
    // off: DecompressFromSource and Release pay one null test each.
    DecodeAhead::Sync();
    // ext.async_gpu_create (class A, next load): the backend's request, read when the loader opens a
    // load (src/render/common/async_gpu_create/). Before the boot's first load, which follows this.
    Ext::AsyncGpuCreate::Sync();
    // ext.compress_ao (class A, next start): opens the derived-data cache (never under the faithful
    // profile, whose row is off) and hands it with the row's value to the backend. Once, here: the
    // boot's first load follows, and the dialog's toggle is recorded for the next start.
    Ext::CompressAo::Boot();
    // ext.share_gpu_data (class A, next start): the backend's request, once, before the boot's first
    // load (src/render/common/share_gpu_data/). Off under the faithful profile (the row is off).
    Ext::ShareGpuData::Boot();
    // ext.create_on_demand (class A, next start): the backend's request and the engine seams, once,
    // before the boot's first load (src/render/common/create_on_demand/). Off under the faithful
    // profile (the row is off).
    Ext::CreateOnDemand::Boot();
    // ext.texture_quality (class A, next start): the run's drop, once, before the boot's first load;
    // after create_on_demand (whose level reader, when installed, drops the levels itself) and before
    // stream_cache (whose records are keyed on the drop). Off under the faithful profile.
    Ext::TextureQuality::Boot();
    // ext.stream_cache (class A, next start): the resource-record seams and the derived cache, once,
    // before the boot's first load; after create_on_demand, whose value the records are keyed on.
    // Off under the faithful profile (the row is off).
    Ext::StreamCache::Boot();
    // ext.visit_replay / ext.find_fastpath (class A, next start): the resource table's seams
    // (object/resource_ext.h), once, before the boot's first load. Off under the faithful profile.
    Ext::VisitReplay::Sync();
    Ext::FindFastpath::Sync();
    // The extension resources built in the exe (docs/extensions/KITFREE.md): the
    // providers register here, never under a COMMAND-LINE faithful profile
    // (`/profile:faithful`: the gate run, no Extensions button, no Enhanced tab,
    // no sharp_ui -- Ext::GateProfileForced), so the engine's load-miss seam stays
    // null there -- until a save carrying an `ext:` pseudo-mod is opened:
    // SaveLoadDialog::DoLoad then calls InitText itself (known_port_issues row
    // 1330). A faithful profile chosen in settings.cfg keeps the providers (the
    // user ruled 2026-10-18): the Extensions button and its dialog's profile note
    // are meant for exactly that case, and until then the button never loaded.
    if (!GateProfileForced()) {
        Resources::InitText();
        Resources::InitFonts();   // ext.sharp_ui's font variants (RSCFONT)
        Resources::InitSprites();  // the Extensions sprite sheet (RSCSPRITE)
        // /rsccheck:1 on the command line: write every provided object's bytes
        // for tools/ext_rsc_check.py (the kit's compiled .crs is the oracle).
        Diag::RscTextCheckIfArmed("standalone_rsccheck");
    }

    std::fprintf(stderr, "[Ext] profile=%s (%s)%s, %d entries, %d malformed value(s), "
                         "%d valued key(s) (%d malformed, %d clamped)\n",
                 ProfileName(report.profile),
                 report.profileFromCommandLine ? "command line /profile"
                                               : "settings.cfg",
                 GateProfileForced() ? " -- GATE: faithful forced, menu unchanged" : "",
                 EntryCount(), report.malformed,
                 ValueKeyCount(), report.malformedValues, report.clampedValues);
    for (int i = 0; i < EntryCount(); ++i) {
        const Entry& e = EntryAt(i);
        std::fprintf(stderr, "[Ext]   %s=%d%s%s\n", SettingsKey(e).c_str(),
                     Active(e) ? 1 : 0,
                     e.tier == Tier::Extension ? " class " : "",
                     ClassLetter(e.cls));
    }
    for (int i = 0; i < ValueKeyCount(); ++i) {
        const ValueKey& k = ValueKeyAt(i);
        std::fprintf(stderr, "[Ext]   %s=%d (%d..%d, of %s)\n", SettingsKey(k).c_str(),
                     Value(k), k.minValue, k.maxValue, k.owner);
    }
}

bool SetAndRecord(const Entry& e, bool on) {
    const bool changedNow = SetRequested(e, on);
    Ext::DrawGather::Sync();   // a class A toggle takes effect now (no-op for every other entry)
    Ext::DrawBatch::Sync();    // on / off: the DrawItemList seam is set or cleared now
    Ext::RenderPipe::Sync();   // on: installed now; off: drained and removed at the next Frame tail
    Ext::AmbientOcclusion::Sync();   // on / off: the depth rebuild is requested, the arm follows at once
    BetterTrees::Sync();        // on: the seams are set now; off: cleared at the next pixel / vertex bind
    Ext::BackgroundSave::Sync();     // on: the save seam is set now; off: cleared now, or after a running save
    Ext::AsyncGpuCreate::Sync();     // on / off: from the next load the loader opens
    Ext::FpsCap::Sync();             // on / off: the frame limiter's ceiling from the next frame
    // ext.max_shadow_res: the sun map is rebuilt through Device::settingsChanged, raised HERE (main thread,
    // between frames) so render_pipe runs the resize sequentially; see SunLight.h (FAA-MENU).
    if (changedNow && std::strcmp(e.id, "max_shadow_res") == 0) ::Graphics::SunLight::ExtRequestMaxShadowResSync();
    if (::IO::Persistence* p = ::IO::Persistence::_singleton)
        p->Set(KeyPrefix(e.tier), e.id, on ? 1 : 0);   // "fix.<id>" / "ext.<id>" = "%i"
    std::fprintf(stderr, "[Ext] %s requested=%d active=%d%s\n", SettingsKey(e).c_str(),
                 on ? 1 : 0, Active(e) ? 1 : 0,
                 EntryLocked(e) ? " (locked for this game)" : "");
    return changedNow;
}

int32_t SetValueAndRecord(const ValueKey& k, int64_t v) {
    const int32_t before = Value(k);
    const int32_t stored = SetValue(k, v);
    // ext.max_shadow_size: a new size rebuilds the sun map (when the row is on and the retail combo is at
    // High), requested HERE on the main thread for the same reason as the row's toggle above.
    if (stored != before && std::strcmp(k.id, "max_shadow_size") == 0)
        ::Graphics::SunLight::ExtRequestMaxShadowResSync();
    if (::IO::Persistence* p = ::IO::Persistence::_singleton)
        p->Set(KeyPrefix(Tier::Extension), k.id, static_cast<int>(stored));   // "ext.<id>" = "%i"
    if (std::strcmp(k.id, "fps_cap_rate") == 0) Ext::FpsCap::Sync();   // live: the next frame's ceiling
    std::fprintf(stderr, "[Ext] %s=%d\n", SettingsKey(k).c_str(), stored);
    return stored;
}

int32_t MaxShadowResChoice() {
    const Entry* row = FindEntry("max_shadow_res");
    const ValueKey* size = FindValueKey("max_shadow_size");
    return Requested(*row) ? Value(*size) : 0;
}

int32_t SetMaxShadowResAndRecord(int32_t size) {
    const Entry& row = *FindEntry("max_shadow_res");
    const ValueKey& key = *FindValueKey("max_shadow_size");
    if (size == 4096 || size == 8192) {
        // The size first: while the row is still off the key change rebuilds nothing; the row's
        // toggle (or, the row already on, the size change itself) requests exactly one rebuild.
        if (Value(key) != size) SetValueAndRecord(key, size);
        if (!Requested(row)) SetAndRecord(row, true);
    } else if (Requested(row)) {
        SetAndRecord(row, false);
    }
    return MaxShadowResChoice();
}

int32_t TextureQualityChoice() {
    const Entry* row = FindEntry("texture_quality");
    const ValueKey* drop = FindValueKey("texture_quality_drop");
    return Requested(*row) ? Value(*drop) : 0;
}

int32_t SetTextureQualityAndRecord(int32_t drop) {
    const Entry& row = *FindEntry("texture_quality");
    const ValueKey& key = *FindValueKey("texture_quality_drop");
    if (drop > 0) {
        const int32_t stored = ClampValue(key, drop);   // 0..1: Quarter was removed by ruling
        if (Value(key) != stored) SetValueAndRecord(key, stored);
        if (!Requested(row)) SetAndRecord(row, true);
    } else if (Requested(row)) {
        SetAndRecord(row, false);
    }
    return TextureQualityChoice();
}

int32_t FpsCapChoice() {
    const Entry* row = FindEntry("fps_cap");
    const ValueKey* rate = FindValueKey("fps_cap_rate");
    return Requested(*row) ? Value(*rate) : 0;
}

int32_t SetFpsCapAndRecord(int32_t fps) {
    const Entry& row = *FindEntry("fps_cap");
    const ValueKey& key = *FindValueKey("fps_cap_rate");
    if (fps > 0) {
        const int32_t stored = ClampValue(key, fps);   // one of 30 / 60 / 90 / 120, else 60
        if (Value(key) != stored) SetValueAndRecord(key, stored);
        if (!Requested(row)) SetAndRecord(row, true);
    } else if (Requested(row)) {
        SetAndRecord(row, false);
    }
    return FpsCapChoice();
}

int32_t TouchModeChoice() {
    const Entry* row = FindEntry("touch_mode");
    const ValueKey* scale = FindValueKey("touch_mode_toolbar_scale");
    return Requested(*row) ? Value(*scale) : 0;
}

int32_t SetTouchModeAndRecord(int32_t percent) {
    const Entry& row = *FindEntry("touch_mode");
    const ValueKey& key = *FindValueKey("touch_mode_toolbar_scale");
    if (percent > 0) {
        const int32_t stored = ClampValue(key, percent);   // one of 100 .. 200 in steps of 25, else 200
        if (Value(key) != stored) SetValueAndRecord(key, stored);
        if (!Requested(row)) SetAndRecord(row, true);
    } else if (Requested(row)) {
        SetAndRecord(row, false);
    }
    return TouchModeChoice();
}

void PersistSettings() {
    if (const ::IO::Persistence* p = ::IO::Persistence::_singleton)
        p->WriteSettings();
}

}  // namespace Ext
