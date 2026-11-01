// ============================================================================
// src/ext/registry.h -- the fixes and extensions registry (docs/extensions/DESIGN.md §3)
// ============================================================================
// NOT RETAIL. The one table of every non-faithful behaviour the exe can switch
// on, and the two read-once structs code tests them through:
//
//   if (Ext::Fix().icon_slide) { corrected } else { retail }       (a fix's lever)
//   if (Ext::Ext().<id>)       { the extension's body }            (an extension)
//
// WHERE THE VALUES COME FROM. settings.cfg (IO::Persistence's map), read ONCE
// at boot by Ext::InitFromSettings (registry_boot.cpp), after
// Persistence::ReadSettings:
//   fix.<id>=0|1     one per fix         absent or not 0/1 = the entry's default
//   ext.<id>=0|1     one per extension   absent or not 0/1 = the entry's default
//   profile=<name>   optional; overrides every per-entry key:
//                      faithful  every fix and every extension OFF (retail)
//                      all       every fix and every extension ON
//                    anything else (or absent) = the per-entry keys.
// The command line's `/profile:<name>` (System::Configuration, retail's own
// `/key:value` parser) overrides the settings key for that one run. It is
// how the gate scripts force the faithful profile (tools/build.sh run,
// tools/resave_gate/resave_gate.sh) without touching anyone's settings.cfg
// and without an environment flag. A run forced faithful from the command
// line also leaves the main menu exactly retail's (no Extensions button):
// see Ext::GateProfileForced.
//
// CLASSES (DESIGN.md §2). A fix has no class and toggles live. An extension's
// class is part of its entry and decides what a toggle costs:
//   A  live, any time
//   B  sim-divergent: locked for the game's lifetime
//   C  format-extending: locked for the game's lifetime (+ appended save chunk)
// The lock is taken by Ext::LockForGame at new game / load (the EXT SEAM in
// GameUpdateLoop::Load) and released with that loop. While a game holds the
// lock, Ext() keeps the B / C values the game started with; the dialog's
// toggles change only the REQUESTED value (settings.cfg), which the next new
// game / load picks up.
//
// This header and registry.cpp are standard C++ only (the unit tests link
// them on their own). The engine glue -- settings map, command line, the
// write-back -- is registry_boot.cpp.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Ext {

enum class Tier : uint8_t { Fix, Extension };

// `None` is a fix's class: fixes carry none (DESIGN.md §2).
enum class Class : uint8_t { None, A, B, C };

// One bool per registered fix. The names ARE the ids (settings key `fix.<id>`).
// The initialisers are the boot-less defaults; the table's `defaultOn` is the
// one InitFromSettings applies.
struct Fixes {
    bool icon_slide     = true;   // docs/retail_bugs.md §1
    bool residence_goto = true;   // docs/retail_bugs.md §2
    bool tavern_counter = true;   // docs/retail_bugs.md §3
    bool start_barn_height = true;   // docs/retail_bugs.md §4
    bool ui_scale_layout   = true;   // docs/retail_bugs.md §5 (engine seam UI::UserInterface::_extFixScaleLayout)
    bool rock_seat         = true;   // docs/retail_bugs.md §7, src/ext/rock_seat/ (docs/extensions/handins/ROCKSEAT.md): render only
    bool terrain_resort    = true;   // docs/retail_bugs.md §8, src/ext/terrain_resort/ (docs/extensions/handins/TERRAINRESORT.md)
    bool large_packages    = true;   // docs/retail_bugs.md §9, src/ext/large_packages/ (docs/extensions/handins/LARGEPKM.md): engine seam IO::FileSystem::_extPackagesMounted
    bool boot_device_lost  = true;   // docs/retail_bugs.md §10 (engine seam Engine::MainLoop::_extFixBootDeviceLost, backend seam BootDeviceLostBackend::SetFixFlag)
    bool slider_drag_clamp = true;   // docs/retail_bugs.md §12 (engine seam UI::SliderElement::_extFixDragClamp; docs/extensions/handins/SLIDERCLAMP.md): UI only
    bool dialog_position_drift = true;   // docs/retail_bugs.md §13, src/ext/dialog_position_drift/ (docs/extensions/handins/DIALOGPOSITIONDRIFT.md): UI only, at the _extDialogPlace seam's kExtPlaceUpperLeft
    bool outline_with_panel = true;  // docs/retail_bugs.md §16, src/ext/outline_with_panel/ (docs/extensions/handins/OUTLINEWITHPANEL.md): render only, engine seam Graphics::EdgeMeshInstance::_extHoldDraw
    bool alloc_tiebreak    = true;   // docs/retail_bugs.md §14, src/ext/alloc_tiebreak/ (docs/extensions/handins/ALLOCTIEBREAK.md): sim-visible, ties in the job / house allocators
    bool foundation_piles  = true;   // docs/retail_bugs.md §17, src/ext/foundation_piles/ (docs/extensions/handins/FOUNDATIONPILES.md): sim-visible, piles a full town cannot store leave a site's footprint
    bool path_query_first  = true;   // docs/retail_bugs.md §18, src/ext/path_query_first/ (docs/extensions/handins/PATHQUERYFIRST.md): sim-visible timing, the synchronous path query jumps the worker queue
    bool crop_planting_exit = true;  // docs/retail_bugs.md §11, src/ext/crop_planting_exit/: sim-visible, a crop field leaves planting when every slot is done or the rest are out of reach
    bool firefighter_wells = true;   // docs/retail_bugs.md §19, src/ext/firefighter_wells/: the Firefighter achievement unlocks at the 20 wells its description gives (the data says 40)
};

// One bool per registered extension (settings key `ext.<id>`). Each extension
// adds its field here and its row to the table in registry.cpp. The initialisers
// stay false even for class A: nothing runs before the boot resolves the table,
// whose defaultOn (class A ON) is what a fresh settings.cfg gets.
struct Extensions {
    bool eye_camera = false;   // class A: src/ext/eye_camera/ (docs/extensions/handins/EYECAM.md)
    bool no_fog = false;       // class A: src/ext/no_fog/ (docs/extensions/handins/NOFOG.md)
    bool show_grid = false;    // class A: src/ext/show_grid/ (docs/extensions/handins/SHOWGRID.md)
    bool tool_names = false;   // class C: src/ext/tool_names/ (docs/extensions/handins/TOOLNAMES.md)
    bool better_camera = false; // class B: src/ext/better_camera/ (docs/extensions/handins/BETTERCAM.md): zoom range 6 .. 200 + steady tilt
    bool high_limits = false;   // class B: src/ext/high_limits/ (docs/extensions/handins/HIGHLIMITS.md)
    bool sharp_ui   = false;   // class A: src/ext/sharp_ui/ (docs/extensions/handins/SHARPUI.md)
    bool fast_paths = false;   // class B: src/ext/fast_paths/ (docs/extensions/handins/FASTPATHS.md)
    bool path_workers = false; // class B: src/ext/path_workers/ (docs/extensions/handins/PATHWORKERS.md)
    bool render_culling = false; // class A: src/ext/render_culling/ (docs/extensions/handins/PERFXEXT.md)
    bool anim_batch     = false; // class B: src/ext/anim_batch/ (docs/extensions/handins/PERFXEXT.md)
    bool draw_gather    = false; // class A: src/ext/draw_gather/ (docs/extensions/handins/DRAWGATHER.md)
    bool draw_batch     = false; // class A: src/ext/draw_batch/ (docs/extensions/handins/DRAWBATCH.md)
    bool walk_batch     = false; // class A: src/ext/walk_batch/ (docs/extensions/handins/WALKBATCH.md)
    bool walk_batch_grid = false; // class B: src/ext/walk_batch_grid/ (docs/extensions/handins/WALKGRID.md)
    bool render_pipe    = false; // class A: src/ext/render_pipe/ (docs/extensions/handins/RENDERPIPE0.md)
    bool gather_threads = false; // class A: src/ext/gather_threads/ (docs/extensions/handins/CULLPAR.md); needs draw_gather
    bool quick_screens  = false; // class A: src/ext/quick_screens/ (docs/extensions/handins/SPLASH.md)
    bool ambient_occlusion = false; // class A: src/ext/ambient_occlusion/ (docs/extensions/AO.md)
    bool max_shadow_res = false; // class A: src/ext/max_shadow_res/ (the Enhanced tab row) + the Graphics::SunLight seam (docs/extensions/handins/FAA-SHADOW.md)
    bool seamless_loops = false; // class A, next start: src/ext/seamless_loops/ (docs/extensions/handins/SEAMLESS.md)
    bool better_water = false; // class A, live: src/ext/better_water/, seam WorldGeometry's water-range draw (docs/extensions/handins/BETTERWATER.md)
    bool better_trees = false; // class A: src/ext/better_trees/ (docs/extensions/handins/BETTER-TREES.md): smooth leaf edges + stable tree shading + edge dapple
    // class C: src/ext/restricted_foods/ (docs/extensions/handins/RESTRICTEDFOODS.md); the foods checked on the
    // Foods page are not eaten
    bool restricted_foods = false;
    // class C: src/ext/item_priority/ (docs/extensions/handins/ITEMPRIO.md); the town's ranked item lists
    bool item_priority = false;
    // class B: src/ext/terrain_edit/ (docs/extensions/handins/TERRAINEDIT.md); a brush tool that edits the
    // terrain under the mouse
    bool terrain_edit = false;
    // class B: src/ext/town_screenshot/ (docs/extensions/handins/TOWNSCREENSHOT.md); a picture of the whole map from an isometric view
    bool town_screenshot = false;
    // class C: src/ext/family_tree/ (docs/extensions/handins/FAMILYTREE.md); the town's births, marriages and
    // deaths
    bool family_tree = false;
    bool spread_warmth = false; // class B: src/ext/spread_warmth/ (docs/extensions/handins/HITCH_PFBURST.md)
    bool async_save_compress = false; // class A: src/ext/async_save_compress/, seam IO::CompressedStream::_extAsyncCompress (docs/extensions/handins/HITCH_SAVE.md)
    bool drain_split = false; // class B: src/ext/drain_split/ (docs/extensions/handins/HITCH_DRAINSPLIT.md)
    bool background_save = false; // class A: src/ext/background_save/ (docs/extensions/handins/HITCH_SAVESNAP.md)
    bool terrain_batch = false; // class A: src/ext/terrain_batch/, seam WorldGeometry::_extTerrainBatch (docs/extensions/handins/TERRAINBATCH.md)
    bool decode_ahead = false; // class A, next start: src/ext/decode_ahead/, seam IO::CompressedStream::_extDecodeAhead (docs/extensions/handins/DECODEAHEAD.md)
    bool load_progress = false; // class A, next start: src/ext/load_progress/, seams LoaderLoop::_extFrame + UpdateLoop::_extLoadProgress (docs/extensions/handins/LOADPROGRESS.md)
    bool async_gpu_create = false; // class A, next load: src/ext/async_gpu_create/, backend seam AsyncGpuCreateBackend (docs/extensions/handins/ASYNCGPUCREATE.md)
    bool compress_ao = false; // class A, next start: src/ext/compress_ao/, backend seam CompressAoBackend + the derived cache src/ext/derived_cache/ (docs/extensions/handins/COMPRESSAO.md)
    bool share_gpu_data = false; // class A, next start: src/ext/share_gpu_data/, backend seam ShareGpuDataBackend (docs/extensions/handins/SHAREGPUDATA.md)
    bool package_cache = false; // class A, live at each mount: src/ext/package_cache/, seams IO::FileSystem::_extIndexRestore / _extIndexBuilt + the derived cache (docs/extensions/handins/PKGCACHE.md)
    bool create_on_demand = false; // class A, next start: src/ext/create_on_demand/, backend seam CreateOnDemandBackend + seams IO::FileSystem::_extHandleClosing, Video::ImageBuffer::_extLevelSource, Graphics::MeshInstance::_extMeshAssigned (docs/extensions/handins/CREATEONDEMAND.md)
    bool texture_quality = false; // class A, next start: src/ext/texture_quality/, backend seam TextureQualityBackend + the level readers (docs/extensions/handins/TEXQUALITY.md)
    bool menu_entry = false; // class A, live at each menu entry: src/ext/menu_entry/, seams IO::Modification::_extModCached / _extModStored / _extOverridesRestore / _extOverridesBuilt / _extRemountSkip / _extRemountDone + IO::FileSystem::_extPackagesClosed (docs/extensions/handins/MENUENTRY.md)
    bool stream_cache = false; // class A, next start: src/ext/stream_cache/, seams Object::ExternalFactory::_extLoadBegin/_extLoadEnd, IO::CompressedStream::_extReadTap, Engine::LoaderLoop::_extLoadDone, CreateOnDemand::g_altLocate/g_altSkip + the derived cache (docs/extensions/handins/STREAMCACHE.md)
    bool visit_replay = false; // class A, next start: src/ext/visit_replay/, seam Object::ExternalFactory::_extVisitReplay (docs/extensions/handins/VISITREPLAY.md)
    bool find_fastpath = false; // class A, next start: src/ext/find_fastpath/, seam Object::ExternalFactory::_extFindFast (docs/extensions/handins/VISITREPLAY.md)
    bool heap_slack = false; // class A, next start: src/ext/heap_slack/, seam System::MemoryAllocator::_extCommitSeam (docs/extensions/handins/HEAPSLACK.md)
    bool phone_menu = false; // class A, live: src/ext/phone_menu/, seam UI::UserInterface::_extDialogPlace + the Options UI-scale slider (docs/extensions/handins/PHONEMENU.md)
    bool fps_cap = false; // class A, live: src/ext/fps_cap/, FrameLimiter::SetSettingsCap (src/app/frame_limiter.h; docs/android/P8_INPUT_OPTIONS.md)
    bool touch_mode = false; // class A, live: src/ext/touch_mode/, seam UI::UserInterface::_extDialogScale + ext.phone_menu's place hook + PlayerToolManager::UpdateCamera (docs/android/TOUCH_MODE.md)
    bool touch_pointer = false; // class A, live: src/ext/touch_pointer/, seams Input::Device::_extAfterUpdate + Platform::Touch::SetSink + ext.touch_mode's camera hold and place hook (docs/android/P11_TOUCH_POINTER.md)
#ifdef BANISHED_UNIT_TEST
    // tests/unit/ext_registry_test.cpp only: one extension per class, so the
    // lock semantics are tested before a real extension exists. Not in the exe.
    bool unit_a = false;
    bool unit_b = false;
    bool unit_c = false;
#endif
};

// ---------------------------------------------------------------------------
// VALUED KEYS (block AOMENU, docs/extensions/AO.md §3 "Settings")
// ---------------------------------------------------------------------------
// An extension's setting that is a number, not a toggle: `ext.<id>=<int>` in
// settings.cfg, beside the bool rows. Each key belongs to one extension row
// (`owner`), has a closed range and a default, and is class A (no lock: a value
// never diverges the sim or the save; the owner row's class says what the
// feature itself costs). The initialisers are the boot-less defaults and must
// equal the table's `defaultValue` (the unit tests check it).
struct Values {
    int32_t ao_quality = 2;     // ext.ao_quality  1..3   (owner ambient_occlusion): Low / Medium / High
    int32_t ao_opacity = 20;    // ext.ao_opacity  0..30 percent (owner ambient_occlusion): the apply's strength; the user's look 2026-10-14: 20 is the max that reads well, 30 the ceiling
    int32_t foliage_aa_mode = 2; // ext.foliage_aa_mode 1..2 (owner better_trees): 1 smooth edges, 2 + distance fill (FOLIAGE_AA.md §1 ruling 4)
    int32_t max_shadow_size = 4096; // ext.max_shadow_size 4096 | 8192 (owner max_shadow_res): the sun map at High (FAA-SHADOW2)
    int32_t rock_seat_mode = 2; // ext.rock_seat_mode 1..2 (owner fix rock_seat): 1 sink to the lowest ground under the rock, 2 tilt to the slope (the default, ROCKSEAT.md)
    int32_t create_on_demand_idle_min = 0; // ext.create_on_demand_idle_min 0..120 (owner create_on_demand): minutes unused before a made object is given back, 0 = never, the default until re-makes are prefetched (CREATEONDEMAND.md section 7)
    int32_t texture_quality_drop = 0; // ext.texture_quality_drop 0..1 (owner texture_quality): the top mip levels a qualifying texture is made without, 0 = Full (the default: the row or profile=all alone drops nothing), 1 = Half; an older 2 (Quarter, removed by ruling) clamps to 1; read once at boot (TEXQUALITY.md)
    int32_t fps_cap_rate = 60; // ext.fps_cap_rate 30 | 60 | 90 | 120 (owner fps_cap): the menu's and the game's frame-rate ceiling while the row is on; 60 is the phone's ruled default (docs/android/P8_INPUT_OPTIONS.md)
    int32_t touch_mode_toolbar_scale = 200; // ext.touch_mode_toolbar_scale 100 | 125 | 150 | 175 | 200 (owner touch_mode): the in-game toolbar's own scale in percent, shrunk to fit beside the control cluster (docs/android/TOUCH_MODE.md)
    // ext.show_grid_shade 0..100 (owner show_grid): the lines' grey in percent, 100 = white, 0 = black
    int32_t show_grid_shade = 100;
    // ext.show_grid_width 1..30 (owner show_grid): the lines' width in hundredths of a tile (a bold line is
    // two thirds wider)
    int32_t show_grid_width = 6;
    int32_t show_grid_hue = 0;        // ext.show_grid_hue 0..359 (owner show_grid): the lines' hue in degrees
    // ext.show_grid_saturation 0..100 (owner show_grid): 0 grey or white, 100 the pure hue
    int32_t show_grid_saturation = 0;
    // ext.show_grid_opacity 10..100 (owner show_grid): how strong the grid lines are, in percent; the
    // bold lines are stronger still
    int32_t show_grid_opacity = 75;
    int32_t touch_mode_zoom_min = 10; // ext.touch_mode_zoom_min 6..20 (owner touch_mode): the camera's near zoom limit in distance units while the row is on, retail's 20 otherwise (docs/android/P11D_ZOOM.md)
    int32_t touch_pointer_lift = 160; // ext.touch_pointer_lift 0 | 80 | 120 | 160 | 200 | 240 (owner touch_pointer): the aim point's height above the finger, device pixels (docs/android/P11_TOUCH_POINTER.md)
    int32_t touch_pointer_pick_radius = 50; // ext.touch_pointer_pick_radius 0 | 25 | 50 | 75 | 100 (owner touch_pointer): the pick snap's radius, device pixels
    int32_t touch_pointer_tap_twice = 0; // ext.touch_pointer_tap_twice 0..1 (owner touch_pointer): a second tap near the first places the aimed building
    int32_t derived_cache_mb = 4096; // ext.derived_cache_mb 256..65536 (owner compress_ao, the cache's first user): the derived-data cache's cap in MB (COMPRESSAO.md)
    int32_t natural_tree_growth = 1; // ext.natural_tree_growth 0..1 (owner better_trees): per-tree autumn colour, leaf fall, regrowth and green variation while the row is on (NATURAL_GROWTH.md)
    int32_t better_water_mode = 1; // ext.better_water_mode 1..2 (owner better_water): 1 Plain (the shore pass), 2 Fancy (+ the shore strip on the bank side); Plain until Fancy is verified (BETTERWATER.md section 7)
};

// All three together: what the tables' `slot` accessors index.
struct State {
    Fixes      fix;
    Extensions ext;
    Values     val;
};

struct ValueKey {
    const char* id;             // settings key suffix: `ext.<id>` ([a-z0-9_]+, unique across BOTH tables)
    const char* owner;          // the Entry id this value belongs to
    int32_t     minValue;
    int32_t     maxValue;
    int32_t     defaultValue;   // minValue <= defaultValue <= maxValue
    int32_t& (*slot)(State& s); // the key's int in a State
    // A DISCRETE key (null for a range key): the only values it takes, each in
    // [minValue, maxValue], defaultValue among them. Any other number -- in range
    // or not -- is replaced by defaultValue (ClampValue), not clamped.
    const int32_t* choices;
    int32_t        choiceCount;
};

struct Entry {
    const char* id;             // settings key suffix: `fix.<id>` / `ext.<id>`
    Tier        tier;
    Class       cls;            // Class::None for a fix
    bool        defaultOn;      // fixes and class A default ON, class B / C OFF (DESIGN.md §2)
    const char* displayName;    // the dialog's row title
    const char* description;    // the dialog's row text
    int32_t     formatVersion;  // class B and C: >= 1 (pseudo-mod ModInfo +0x18, the save manifest); 0 otherwise
    bool& (*slot)(State& s);    // the entry's bool in a State
};

// The table, in menu order: every fix, then every extension.
int EntryCount();
const Entry& EntryAt(int index);
const Entry* FindEntry(const char* id);   // by id alone; null when unknown

// The valued keys, in table order (registry.cpp kValueKeys).
int ValueKeyCount();
const ValueKey& ValueKeyAt(int index);
const ValueKey* FindValueKey(const char* id);   // null when unknown
std::string SettingsKey(const ValueKey& k);      // "ext.<id>"

// `v` forced into [minValue, maxValue]; for a discrete key (`choices`), `v` when
// it is one of the choices, else defaultValue.
int32_t ClampValue(const ValueKey& k, int64_t v);

// A decimal integer: an optional '-' and 1..9 digits, nothing else (no blanks,
// no '+', no hex, no fraction). In range or not, a well-formed number is
// CLAMPED into the key's range (`ok` true; a discrete key's non-choice = its
// default, see ClampValue); anything else is malformed (`ok` false, the key's
// default returned) -- the rule ParseToggle applies to bools.
int32_t ParseValue(const ValueKey& k, const std::string& text, bool& ok);

// "fix." / "ext." for the entry's tier.
const char* KeyPrefix(Tier tier);
std::string SettingsKey(const Entry& e);  // e.g. "fix.icon_slide"

// "A" / "B" / "C", "" for a fix.
const char* ClassLetter(Class cls);
bool IsLockedClass(Class cls);            // B or C

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------
enum class Profile : uint8_t { PerEntry, Faithful, All };

// "faithful" / "all" (exact, lower case); anything else, null or "" = PerEntry.
Profile ParseProfile(const char* text);
const char* ProfileName(Profile p);

// ---------------------------------------------------------------------------
// Loading (pure: the unit tests drive these)
// ---------------------------------------------------------------------------
// The settings source: true + `value` when `key` is present.
using Lookup = bool (*)(void* ctx, const char* key, std::string& value);

// "1" = on, "0" = off; anything else is not a toggle value (`ok` false).
bool ParseToggle(const std::string& value, bool& ok);

struct LoadReport {
    Profile profile          = Profile::PerEntry;
    bool    profileFromCommandLine = false;
    int     malformed        = 0;   // registered keys whose value was not 0/1 (default kept)
    int     malformedValues  = 0;   // valued keys whose value was not a number (default kept)
    int     clampedValues    = 0;   // valued keys whose number was out of range or not canonical ("007", "-0"): clamped / normalised
};

// The values `lookup` gives every registered entry, under the profile:
// `commandLineProfile` (null / "" = not given) wins over the settings key
// `profile`, which wins over the per-entry keys. Keys of ids not in the table
// are never asked for, so an unknown `fix.<x>` / `ext.<x>` is ignored.
// Valued keys: under PerEntry each is parsed with ParseValue (absent =
// default); the faithful and all profiles leave EVERY valued key at its
// default (a profile is a statement about the toggles; a number has no
// "retail" or "on" value, and the default is what the feature ships at).
State Resolve(Lookup lookup, void* ctx, const char* commandLineProfile, LoadReport* report);

// ---------------------------------------------------------------------------
// The live state
// ---------------------------------------------------------------------------
// What code reads. Valid from boot (InitFromSettings) on; before that, and in
// the unit tests until Install, the structs' initialisers.
const Fixes&      Fix();
const Extensions& Ext();
const Values&     Val();

// A valued key's live value, and the menu's write: clamps, stores it as both
// requested and active (class A: never locked) and returns the stored value.
// (Persisting it is SetValueAndRecord's job, registry_boot.cpp.)
int32_t Value(const ValueKey& k);
int32_t SetValue(const ValueKey& k, int64_t v);

// The value the player asked for (settings / the dialog), which differs from
// the active one only for a B / C extension while a game holds the lock.
bool Requested(const Entry& e);
bool Active(const Entry& e);

// Replace both the requested and the active state (boot; the unit tests).
// Clears the lock.
void Install(const State& requested, Profile profile, bool profileFromCommandLine);

Profile CurrentProfile();
// True when the profile came from the command line (`/profile:...`): the gate.
bool GateProfileForced();

// The dialog's toggle: records `on` as requested. A fix, a class A extension,
// or any extension while no game holds the lock, takes effect at once; a
// B / C extension under the lock waits for the next LockForGame. Returns
// whether the active value changed now. (Persisting it is SetAndPersist's
// job, registry_boot.cpp.)
bool SetRequested(const Entry& e, bool on);

// ---------------------------------------------------------------------------
// The game lock (B / C)
// ---------------------------------------------------------------------------
// `owner` is the game that takes it (the GameUpdateLoop); UnlockForGame from
// any other owner is ignored, so a reload whose new loop locks before the old
// one is destroyed keeps the new lock.
void LockForGame(const void* owner);
// A load whose save carries a B / C manifest (savetail.h, the SaveRestore load
// arm): the game's B / C set is EXACTLY `ids` -- each listed one on, every
// other B / C extension off -- whatever the player requested. Requested
// values are untouched (the next game starts from them again). Returns false
// and changes nothing when an id is not a registered class B / C extension.
bool LockForGameWithSet(const void* owner, const std::vector<std::string>& ids);
// An in-game B / C change the player confirmed (ExtensionsDialog::ReloadGame). The reload's
// save-restore writes the running game's set into the manifest, but the reloaded game must
// start with the new request. KeepRequestOnNextLock marks the next LockForGame as that reload;
// LockKeepsRequest() is true while the lock it took is held, and the load arm
// (ExtSave_ReadTail) then keeps the request instead of locking to the save's set. Any other
// lock, Install or the owner's unlock clears it.
void KeepRequestOnNextLock();
bool LockKeepsRequest();
void UnlockForGame(const void* owner);
bool Locked();
// Advances on every Install, LockForGame, LockForGameWithSet (success) and UnlockForGame (by its
// owner): a value read during one game equals a later read only while that game still holds the
// same lock. Per-game state kept outside the game's objects compares it to know it is stale
// (ext.drain_split's owed AssignCitizens, HITCH_DRAINSPLIT.md).
uint32_t LockEpoch();
// Locked AND the entry is B / C: "locked for this game" in the dialog.
bool EntryLocked(const Entry& e);

// ---------------------------------------------------------------------------
// EXT SEAM (NOT RETAIL, docs/extensions/handins/SAVETAIL.md, EXTSAVE.md): the save's mod list, every active class B / C extension as a pseudo-mod, at save write
// ---------------------------------------------------------------------------
// Every ACTIVE class B / C extension, as the pseudo-mod the save must carry
// (DESIGN.md §2 as amended by SAVETAIL, main 1a180d42f): ModInfo name = the
// display name, path = `ext:<id>`, +0x10 = 0 (the package CRC, never compared
// for a missing mod), +0x14 = 0x010007ff, +0x18 = the extension's
// formatVersion (the field retail's modsMissing modal displays). Every B and
// C entry carries a formatVersion >= 1. Ids are [a-z0-9_]+ (never a `.`, so
// `ext:<id>` cannot name a file); ValidId checks one, the unit tests check
// the whole table.
//
// The engine's writer (Save::SaveLoad::SaveMetaData, src/engine/save/
// saveload_meta.cpp) must not include src/ext, so the seam is the
// null-by-default function pointer Save::SaveLoad::_extExtraMods, which
// InitFromSettings installs (block EXTSAVE, handins/EXTSAVE.md):
// SaveModsProvider() below decides -- null under the faithful profile and
// while no B / C extension is registered, so the engine body runs
// byte-identical when nothing is enabled. The block-1 manifest is savetail.h.
constexpr uint32_t kPseudoModField14 = 0x010007ffu;

struct PseudoMod {
    std::string path;           // "ext:<id>"
    std::string name;           // the display name
    int32_t     field10;        // ModInfo +0x10: 0
    uint32_t    field14;        // ModInfo +0x14: kPseudoModField14
    int32_t     formatVersion;  // ModInfo +0x18
};
int PseudoModsForSave(PseudoMod* out, int capacity);   // returns the count (all, even past capacity)

// The engine-facing form: plain pointers, no std types. `path` / `name` stay
// valid until the next call (the registry owns them).
struct PseudoModRecord {
    const char* path;
    const char* name;
    int32_t     field10;
    uint32_t    field14;
    int32_t     formatVersion;
};
using ExtraModsProvider = int (*)(PseudoModRecord* out, int capacity);   // count, as above
int ProvideExtraModsForSave(PseudoModRecord* out, int capacity);
// What to install as the engine's provider at boot (after InitFromSettings):
// ProvideExtraModsForSave, or null under the faithful profile or when no
// B / C extension is registered.
ExtraModsProvider SaveModsProvider();

// [a-z0-9_]+
bool ValidId(const char* id);

// The load notice (SaveLoadDialog::DoLoad, block EXTSAVE). A save's mod-list
// entry is a pseudo-mod when its path starts with `ext:`; such entries never
// reach Save::SaveLoad::CheckMod. PseudoModLoadStatus is the CheckMod-shaped
// verdict DoLoad turns into retail's modal: 0 = this build has the extension
// (a class B / C entry), the player has it requested, and the saved format
// version is not newer than ours; 1 (modsMissing) otherwise.
constexpr const char* kPseudoModPrefix = "ext:";
bool IsPseudoModPath(const char* path);
int PseudoModLoadStatus(const char* path, int32_t savedFormatVersion);

// ---------------------------------------------------------------------------
// Engine glue (registry_boot.cpp; the exe only, not the unit tests)
// ---------------------------------------------------------------------------
// Boot: Resolve over IO::Persistence's settings map and the command line's
// `/profile:<name>`, then Install. Called once from Game::RunGameMain after
// Engine::Startup (which ran Persistence::ReadSettings). One `[Ext]` line per
// entry goes to stderr (standalone_error.log).
void InitFromSettings();

// The dialog's write-back: SetRequested, then the entry's `fix.<id>` /
// `ext.<id>` into the settings map (Persistence::Set, "%i"). The caller
// flushes once with PersistSettings (Persistence::WriteSettings).
bool SetAndRecord(const Entry& e, bool on);
// The valued key's write-back: SetValue, then `ext.<id>` = "%i" into the
// settings map. Returns the stored (clamped) value. The caller flushes.
int32_t SetValueAndRecord(const ValueKey& k, int64_t v);
// "Max shadow resolution" as one choice (FAA-SHADOW2; the Enhanced tab's combo and the tweak
// overlay): 0 = the row ext.max_shadow_res off (ext.max_shadow_size kept), 4096 / 8192 = the size
// key, then the row on. Main thread only (each record requests the sun map's rebuild). The caller
// flushes. Returns the choice now in effect (0, 4096 or 8192).
int32_t SetMaxShadowResAndRecord(int32_t size);
// The same choice read back: 0 while the row is off (requested), else ext.max_shadow_size.
int32_t MaxShadowResChoice();
// "Texture quality" as one choice (TEXQUALITY; the Enhanced tab's combo): 0 = the row
// ext.texture_quality off (ext.texture_quality_drop kept), 1 = the drop key (clamped to its range,
// 0..1 since Quarter was removed by ruling), then the row on. Recorded only: the drop is read once at
// boot (next start). The caller flushes. Returns the requested choice now (0 or 1).
int32_t SetTextureQualityAndRecord(int32_t drop);
// The same choice read back: 0 while the row is off (requested), else ext.texture_quality_drop.
int32_t TextureQualityChoice();
// "Frame rate cap" as one choice (the Enhanced tab's combo): 0 = the row ext.fps_cap off
// (ext.fps_cap_rate kept), 30 / 60 / 90 / 120 = the rate key, then the row on. Live: the frame
// limiter follows at once (Ext::FpsCap::Sync). The caller flushes. Returns the choice now in effect.
int32_t SetFpsCapAndRecord(int32_t fps);
// The same choice read back: 0 while the row is off (requested), else ext.fps_cap_rate.
int32_t FpsCapChoice();
// "Touch controls" as one choice (the Enhanced tab's combo): 0 = the row ext.touch_mode off
// (ext.touch_mode_toolbar_scale kept), 100 / 125 / 150 / 175 / 200 = the toolbar scale key, then the
// row on. Live. The caller flushes. Returns the choice now in effect.
int32_t SetTouchModeAndRecord(int32_t percent);
// The same choice read back: 0 while the row is off (requested), else ext.touch_mode_toolbar_scale.
int32_t TouchModeChoice();
void PersistSettings();

}  // namespace Ext
