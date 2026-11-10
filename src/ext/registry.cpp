// ============================================================================
// src/ext/registry.cpp -- the fixes and extensions table and its state
// ============================================================================
// NOT RETAIL. registry.h has the contract. Standard C++ only: the unit tests
// (tests/unit/ext_registry_test.cpp) link this file alone.
// ============================================================================

#include "ext/registry.h"
#include "ext/savetail.h"   // ResetAllState: UnlockForGame (class C state goes with the game)

#include <cstring>

namespace Ext {

namespace {

// ---------------------------------------------------------------------------
// THE TABLE. Fixes first (default ON), then extensions (class A default ON,
// class B / C default OFF but the rows ruled ON for 1.0.3: DESIGN.md section 2), in the
// order the dialog lists them. A fix's description says what the player saw
// in retail; docs/retail_bugs.md carries the mechanism and the lever.
// ---------------------------------------------------------------------------
const Entry kEntries[] = {
    { "icon_slide", Tier::Fix, Class::None, true,
      "Status icons appear in place",
      "New status icons no longer slide in from the centre of the map.",
      0, [](State& s) -> bool& { return s.fix.icon_slide; } },

    { "residence_goto", Tier::Fix, Class::None, true,
      "House 'go to' button",
      "Centres on the resident, not on nothing or the wrong citizen.",
      0, [](State& s) -> bool& { return s.fix.residence_goto; } },

    { "tavern_counter", Tier::Fix, Class::None, true,
      "Tavern statistics",
      "Counts the good actually consumed, not the next one in storage.",
      0, [](State& s) -> bool& { return s.fix.tavern_counter; } },

    { "start_barn_height", Tier::Fix, Class::None, true,
      "Grounded starting buildings",
      "Starting buildings sit on the flattened ground instead of floating.",
      0, [](State& s) -> bool& { return s.fix.start_barn_height; } },

    { "ui_scale_layout", Tier::Fix, Class::None, true,
      "Steady dialogs on UI rescale",
      "Dialogs no longer flash to a corner when the UI scale changes, pop-up notices follow the "
      "UI scale, and the name and cost of a building being placed show above it at any UI scale.",
      0, [](State& s) -> bool& { return s.fix.ui_scale_layout; } },

    // ROCKSEAT (docs/extensions/handins/ROCKSEAT.md, docs/retail_bugs.md section 7): rocks and ore
    // are drawn seated on the slope (the valued key rock_seat_mode below picks how). Render only:
    // the serialized transform, the sim and the save are retail's.
    { "rock_seat", Tier::Fix, Class::None, true,
      "Rocks sit on the ground",
      "Rocks and iron deposits on a slope no longer float on their downhill side, so their shadows "
      "meet them.",
      0, [](State& s) -> bool& { return s.fix.rock_seat; } },

    // TERRAINRESORT (docs/extensions/handins/TERRAINRESORT.md, docs/retail_bugs.md section 8): a cell
    // sorted "too steep" is re-sorted once flattening has made the ground under it buildable, after
    // every flatten and once on a load; a tree, ore or goods pile cleared from steep ground leaves its
    // cell buildable.
    // Sim-visible: the healed cells take buildings and trees.
    { "terrain_resort", Tier::Fix, Class::None, true,
      "No red squares on flat ground",
      "Ground stays buildable after a tree on a hill edge is cut, and ground flattened by a road or "
      "building next to it can be built on again, instead of staying marked as too steep.",
      0, [](State& s) -> bool& { return s.fix.terrain_resort; } },
    // LARGEPKM (docs/extensions/handins/LARGEPKM.md, docs/retail_bugs.md section 9): a mod package
    // larger than 2 GiB is read with 64-bit offsets. Only which bytes a packed read fetches changes;
    // packages up to 2 GiB, the mod list and the save are retail's. Applies at the next mount.
    { "large_packages", Tier::Fix, Class::None, true,
      "Mods larger than 2 GB",
      "A mod package larger than 2 GB loads instead of crashing the game. Takes effect at the next "
      "start.",
      0, [](State& s) -> bool& { return s.fix.large_packages; } },

    // docs/retail_bugs.md section 10 (known_port_issues rows 1332 / 1334): a graphics device lost
    // during start-up recovers. Two seams: MainLoop's video error / restart pair skips the save-list
    // walks while there is no save list yet, and the renderer backend runs the device-lost recovery
    // when a swap-chain reset fails on a removed device (retail recovers only from Present).
    // Off (the faithful profile): retail's spin or crash.
    { "boot_device_lost", Tier::Fix, Class::None, true,
      "Graphics reset during start-up",
      "A graphics driver reset while the game is starting up no longer crashes it.",
      0, [](State& s) -> bool& { return s.fix.boot_device_lost; } },

    // SLIDERCLAMP (docs/extensions/handins/SLIDERCLAMP.md, docs/retail_bugs.md section 12): a slider
    // or scroll bar dragged with the pointer off its 32-pixel band keeps following the pointer and
    // stops at its ends, and a release there keeps the value. Retail snaps back to where the drag
    // began. UI only. One row, two seams (SliderElement, ScrollElement).
    { "slider_drag_clamp", Tier::Fix, Class::None, true,
      "Sliders follow the mouse",
      "A slider or scroll bar being dragged keeps following the mouse and stops at its ends, instead "
      "of jumping back when the pointer strays off it.",
      0, [](State& s) -> bool& { return s.fix.slider_drag_clamp; } },

    // DIALOGPOSITIONDRIFT (docs/extensions/handins/DIALOGPOSITIONDRIFT.md, docs/retail_bugs.md section 13): a dialog moved
    // after its layout (an entity's panel above its building) is moved from where the layout put it, not
    // from a stored position the layout's pull-back at the screen's edge left stale. UI only.
    { "dialog_position_drift", Tier::Fix, Class::None, true,
      "Steady panels at the edge",
      "A building or citizen panel opened near the bottom of the screen no longer jumps for a moment "
      "when it first appears.",
      0, [](State& s) -> bool& { return s.fix.dialog_position_drift; } },

    // OUTLINEWITHPANEL (docs/extensions/handins/OUTLINEWITHPANEL.md, docs/retail_bugs.md section 16): on
    // the frame a click opens an entity's panel, its new outline is held back until the panel is laid
    // out, the next frame. Render only. Engine seam Graphics::EdgeMeshInstance::_extHoldDraw.
    { "outline_with_panel", Tier::Fix, Class::None, true,
      "Outline with its panel",
      "A selected building's or citizen's outline appears together with its panel, instead of one "
      "frame before it.",
      0, [](State& s) -> bool& { return s.fix.outline_with_panel; } },

    // ALLOCTIEBREAK (docs/extensions/handins/ALLOCTIEBREAK.md, docs/retail_bugs.md section 14): the
    // employment allocator and the housing re-balancer break ties between equal candidates by entity id
    // and profession-list index instead of by heap address. Sim-visible: which citizen takes a job, and
    // which empty house a family moves to, among equals. Nothing new is saved.
    { "alloc_tiebreak", Tier::Fix, Class::None, true,
      "Repeatable job and house choices",
      "When several citizens or houses are equally good for a job or a move, the choice no longer "
      "depends on memory layout, so a town plays out the same way each time a save is loaded.",
      0, [](State& s) -> bool& { return s.fix.alloc_tiebreak; } },

    // FOUNDATIONPILES (docs/extensions/handins/FOUNDATIONPILES.md, docs/retail_bugs.md section 17): a
    // building site whose footprint holds only piles that no storage will take has them carried to free
    // ground just outside it (ClearArea phase 3), so it can be built; retail waits until the piles rot.
    // Sim-visible: which cells the piles lie on. Nothing new is saved: the move is retail's own actions.
    { "foundation_piles", Tier::Fix, Class::None, true,
      "Building sites clear with full storage",
      "When every store is full, logs and stone lying where a building is to go are carried just "
      "outside its footprint, so the building can go ahead instead of waiting for them to rot.",
      0, [](State& s) -> bool& { return s.fix.foundation_piles; } },

    // PATHQUERYFIRST (docs/extensions/handins/PATHQUERYFIRST.md, docs/retail_bugs.md section 18): the
    // synchronous path query (a merchant boat's water route, the road tool's cost query) runs at the head of
    // the pathfinder's queue instead of behind every queued citizen search. Sim-visible timing: those
    // citizens get their paths a few ticks later. Nothing new is saved.
    { "path_query_first", Tier::Fix, Class::None, true,
      "No freeze when a merchant arrives",
      "In a large town a merchant boat or a road being drawn no longer freezes the game while every "
      "citizen route queued before it is worked out.",
      0, [](State& s) -> bool& { return s.fix.path_query_first; } },

    // CROPPLANTINGEXIT (docs/retail_bugs.md section 11): a crop field in planting (state 9) moves on when
    // every slot ends done (the re-clear request state 9's transition table drops in retail) and when the
    // only slots left to plant are out of the workers' reach (PlantArea skips them and never ends
    // planting). Sim-visible. Nothing new is saved: the states and slot marks are retail's own.
    { "crop_planting_exit", Tier::Fix, Class::None, true,
      "Fields finish planting",
      "A crop field no longer stays in planting forever when farmers cannot reach one of its rows, or when "
      "every crop was lost before planting finished; it goes on to tending and harvest.",
      0, [](State& s) -> bool& { return s.fix.crop_planting_exit; } },

    // FIREFIGHTERWELLS (docs/retail_bugs.md section 20): the Firefighter achievement's goal in the game data
    // compares the count of finished wells with 40, while its description reads "Build 20 wells"; with the
    // row on that one goal compares with 20. Changes when an achievement is awarded, nothing in the sim.
    { "firefighter_wells", Tier::Fix, Class::None, true,
      "Firefighter at 20 wells",
      "The Firefighter achievement is awarded at the 20 wells its description asks for; the game's own "
      "data asked for 40.",
      0, [](State& s) -> bool& { return s.fix.firefighter_wells; } },
    // UNRESOLVEDSEEDS (docs/retail_bugs.md section 21): the town's available-agriculture list keeps no
    // null entry: the start town skips a start-condition crop / tree / animal that does not resolve, and
    // a load drops an entry whose resource no longer loads. Retail's crop field, orchard and pasture
    // panels fault on the null. Sim-visible only where retail would have crashed (or, at the start town,
    // spawned a herd from nothing). Nothing new is saved.
    { "unresolved_seeds", Tier::Fix, Class::None, true,
      "No crash on fields with a missing crop",
      "A crop, tree or animal that a starting condition or a save names but no installed mod provides "
      "is left out of the town's list, so selecting a crop field, orchard or pasture no longer crashes.",
      0, [](State& s) -> bool& { return s.fix.unresolved_seeds; } },
    // COMMUTECOST (docs/retail_bugs.md section 6): PathGraph::RegionPairCost's not-found arm (0x1400d6060)
    // returns the commute cost it just computed and stored instead of 0, so the employment allocator no
    // longer rates a region pair's first, never-measured commute as free. Sim-visible: which citizen takes
    // a job when a home, workplace or region changed while the pairs drained. Nothing new is saved.
    { "commute_cost", Tier::Fix, Class::None, true,
      "Jobs near home after building",
      "A citizen is no longer sent to a far workplace because the walk to a newly built home or "
      "workplace had not been measured yet and was counted as no walk at all.",
      0, [](State& s) -> bool& { return s.fix.commute_cost; } },
    // NOMADARRIVAL (docs/retail_bugs.md section 22): NomadComponent::TickWalkIn (0x14004f9c0) counts an
    // idle nomad as arrived wherever it stands, so a group whose walk to the town hall fails becomes
    // citizens where it is stuck. With the row on, arrival means near the meeting point; otherwise the
    // nomad walks again once, then leaves the group and goes back to the map edge. Sim-visible only where
    // a walk failed. Nothing new is saved (the retry mark is not saved; after a load it retries again).
    { "nomad_arrival", Tier::Fix, Class::None, true,
      "Nomads must reach the town hall",
      "Nomads who cannot walk to the town hall (cut off by a river, for example) no longer count as "
      "arrived where they are stuck; they try once more, then leave, instead of becoming citizens who "
      "cannot reach the town.",
      0, [](State& s) -> bool& { return s.fix.nomad_arrival; } },
    // MODSPERSIST (docs/retail_bugs.md section 23): retail writes the mod list to settings.cfg only at a
    // clean shutdown, so a crash after a Mods change (often the reload itself) loses it; and a change made
    // in a game is used for that game, then dropped at the next menu after every changed resource was
    // reloaded twice. With the row on, confirming Mods writes the settings at once; in a game the pause
    // menu's Mods button first shows a Yes / No warning (Dialog/Extensions.rsc:modsInGame), and a confirmed
    // change is put in the settings too, so the next menu keeps it. Not sim-visible. Nothing new is saved.
    { "mods_persist", Tier::Fix, Class::None, true,
      "Mod changes saved at once",
      "A change in the Mods window is saved as soon as you confirm it, so a crash no longer brings back "
      "the old list, and a change made during a game is kept after it. In a game the Mods button warns "
      "first: turning off a mod the town uses can damage it.",
      0, [](State& s) -> bool& { return s.fix.mods_persist; } },
    // SAVEFAILURENOTICE (docs/retail_bugs.md section 27): a save that does not reach the disk (read-only
    // folder, Controlled Folder Access, antivirus, full disk) opens a message box naming the save folder,
    // where retail's UpdateSave (0x1800c2a80) never reads the save job's status; and the job writes a
    // temporary file and moves it over the save, so a failed write keeps the previous save. No sim change.
    { "save_failure_notice", Tier::Fix, Class::None, true,
      "Tell when a save fails",
      "A save that cannot be written (a read-only or protected save folder, an antivirus block, a full "
      "disk) shows a message instead of failing silently, and the previous save of that name is kept.",
      0, [](State& s) -> bool& { return s.fix.save_failure_notice; } },
    // BOATREACHNOTICE (docs/retail_bugs.md section 28): when a merchant boat's two water legs to a trading
    // post's dock come back empty (BoatComponent::SetDestination 0x1400456e0 drops them without a word),
    // the event log says so, at the post's tile. Where the boat sails is unchanged. The line is an ordinary
    // event-log entry (text, a save keeps it like any other).
    { "boat_reach_notice", Tier::Fix, Class::None, true,
      "Tell when boats cannot reach a dock",
      "When a merchant's boat finds no water route to a trading post's dock, the event log says so, so a "
      "post that never trades is no longer a mystery.",
      0, [](State& s) -> bool& { return s.fix.boat_reach_notice; } },

    // CANCELREFUND (docs/retail_bugs.md section 26): the materials DestroyComponent::ScatterContents
    // (0x14011c530) cannot lay on a cancelled site's (or a demolished building's) own footprint are laid in
    // piles on the free cells around it, found by the search a citizen's dropped load uses (0x14009bd40),
    // instead of being discarded. Sim-visible: more piles, more material. Nothing new is saved: the piles
    // are ordinary piles.
    { "cancel_refund", Tier::Fix, Class::None, true,
      "Cancelled buildings return their materials",
      "Materials delivered to a building site that is cancelled are left in piles around it instead of "
      "most of them vanishing when they do not fit on the site's own ground.",
      0, [](State& s) -> bool& { return s.fix.cancel_refund; } },
    // STRANDEDWADE (docs/retail_bugs.md section 19): Map::RequestPath (0x14009cfb0) refuses every walk
    // between two land zones, while a walk with a water or mountain endpoint may cross into any zone; a
    // citizen left on cut-off land asks for the same refused walk until he starves or freezes. With the
    // row on, a citizen on land outside his anchor's zone (home; the home he just lost; the nearest town
    // hall; the nearest storage) whose walk toward it was refused on the zone mismatch makes one walk
    // with retail's wade / climb bypass (pass flags 0xe1f | 0x1e0) there. Sim-visible only for such a
    // citizen.
    // Nothing is saved (a load starts every citizen unstranded).
    { "stranded_wade", Tier::Fix, Class::None, true,
      "Stranded citizens wade home",
      "A citizen stuck on land cut off from the town (across a stream, on a finished dock or bridge "
      "piece) wades or climbs back home (or to the town hall or a storage building), instead of standing there until he "
      "starves or freezes.",
      0, [](State& s) -> bool& { return s.fix.stranded_wade; } },

    // AUTOTRADEFREEGOODS (docs/retail_bugs.md section 24): TradeComponent::AutoTrade (0x140059ad0) pays for an
    // auto-purchase by selling the post's goods unit by unit until the price is covered; a good the docked
    // merchant prices 0 never covers it, so retail hands over the whole stack. With the row on such a good
    // is passed over. Sim-visible: what the post keeps. Nothing new is saved.
    { "autotrade_free_goods", Tier::Fix, Class::None, true,
      "Auto-trade keeps unwanted goods",
      "A trading post that trades automatically no longer gives a merchant whole stacks of goods he "
      "does not buy, for nothing, while paying for what it bought.",
      0, [](State& s) -> bool& { return s.fix.autotrade_free_goods; } },

    // INPUTFETCHROOM (docs/retail_bugs.md section 25): ConsumeProduce::Execute's input fetch (0x1400e7840)
    // asks for a full carry (-1), which the workplace's all-or-nothing CanAdd (0x14012cd80) refuses when it
    // has less room than that, so the load goes back to a barn and the worker comes for it again. With the
    // row on the fetch asks for what the workplace and the worker can take, and skips the input when that is
    // nothing. Sim-visible: what a worker carries and where. Nothing new is saved.
    { "input_fetch_room", Tier::Fix, Class::None, true,
      "Workers fetch what fits",
      "A brewer, smith or other production worker fetches only as much of an input as the building has "
      "room for, instead of hauling a full load to a nearly full building and carrying it back again.",
      0, [](State& s) -> bool& { return s.fix.input_fetch_room; } },

    // DEBUGSPAWNORIGIN (docs/retail_bugs.md section 29): the debug panel's "spawn citizens" button
    // (Game_SpawnCitizens 0x140060380) places its four citizens on the use nodes of the town's storage
    // buildings and repeats its pass until four are placed; a town whose storages have no use node (only
    // stockpiles) loops for ever and the game hangs. With the row on, a pass that places nobody places the
    // rest at the free land cell nearest the world origin (the map centre). Debug panel only; nothing new
    // is saved (the citizens are ordinary citizens).
    { "debug_spawn_origin", Tier::Fix, Class::None, true,
      "Debug spawn does not hang",
      "The debug panel's spawn-citizens button no longer freezes the game in a town whose storage has no "
      "place to put them (only stockpiles); the new citizens appear near the middle of the map instead.",
      0, [](State& s) -> bool& { return s.fix.debug_spawn_origin; } },

    // STATUSBAREMPTYTOWN (review S-006): the status bar's problem-icon click (StatusBarUI's handler
    // 0x14007a120) walks the town's citizens with no count test. A click queued in one frame is
    // dispatched in the next, after its sim tick; when that tick removed the last citizen, the walk
    // reads the list's slot 0 past its end: the removed citizen (freed in retail by the same frame's
    // entity drain). With the row on, the click does nothing in a town with no citizen. UI only;
    // nothing new is saved.
    { "statusbar_empty_town", Tier::Fix, Class::None, true,
      "Status icons safe when the town empties",
      "Clicking a status-bar problem icon just as the last citizen leaves or dies no longer jumps to "
      "the citizen who is gone (in the original game, a read of freed memory).",
      0, [](State& s) -> bool& { return s.fix.statusbar_empty_town; } },
    // MINIMAPOUTLINECLIP (docs/retail_bugs.md section 31): FrustumOutlineElement::Draw clips each side of the
    // minimap's camera outline to the minimap rect before AddLine, so the sprite cull (which reads a line's
    // endpoints as a rectangle's corners) no longer drops a side drawn toward a corner off the map. UI only.
    { "minimap_outline_clip", Tier::Fix, Class::None, true,
      "Whole camera outline on the map",
      "The yellow outline of the camera's view on the map keeps all four sides when part of the view lies "
      "past the edge of the map, instead of losing one or two.",
      0, [](State& s) -> bool& { return s.fix.minimap_outline_clip; } },
    // STREAMFREEWAIT (review S-027): Audio::Device::OnSoundBufferDeleted (0x1800de0e0) frees a streamed
    // buffer's voice and returns, and ~SoundBuffer frees the seek table the stream worker may still be
    // reading between its ReadFile and its next lock (Stream::Run 0x1800a0f60): a use after free. With
    // the row on, the stream's pass over that channel is stopped and waited for first, Stream::Play's
    // own drain. Engine seam Audio::Device::_extFixStreamFreeWait. Nothing in the sim; nothing saved.
    { "stream_free_wait", Tier::Fix, Class::None, true,
      "No crash when streamed music is unloaded",
      "When a streamed sound such as music is unloaded while it plays, the game first lets the music "
      "streaming thread stop, so that thread never reads the freed sound.",
      0, [](State& s) -> bool& { return s.fix.stream_free_wait; } },
    // DECALFOGORDER (docs/retail_bugs.md section 32): the terrain decal pixel programs (Decal.ps and its
    // heirs) fog the decal colour, then tint it toward the underwater colour, so a decal below the water
    // line (a bridge's, a dock's) shows as a dark band through fog. With the row on, the installed
    // programs are rewritten at load to tint first, then fog, as the model programs do
    // (src/render/common/decal_fog/; never a shipped copy, DESIGN.md rule 7). Render only; nothing saved.
    { "decal_fog_order", Tier::Fix, Class::None, true,
      "Bridges fade into fog",
      "The dark ground marking under a bridge or dock over water fades into fog like everything else, "
      "instead of showing as a sharp dark band through it. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.fix.decal_fog_order; } },
    // SHADOWZOOMPULSE (docs/retail_bugs.md section 33): the sun box is sized to the view on every frame, so
    // a zoom samples the shadow map on a new grid each frame and every shadow edge moves at once. With the
    // row on the box's size steps on a ladder (Graphics::ShadowZoom::SteadyHalf). Render only; nothing saved.
    { "shadow_zoom_pulse", Tier::Fix, Class::None, true,
      "Steady shadows while zooming",
      "Shadows no longer flicker and pulse while the camera zooms in or out.",
      0, [](State& s) -> bool& { return s.fix.shadow_zoom_pulse; } },

    // Extensions. Each one adds a field to Ext::Extensions and a row here:
    // Tier::Extension, its Class, default true for class A / false for B or C, and for class B or C its
    // formatVersion (>= 1). The id is [a-z0-9_]+ (the unit tests check every row).
    // Class A but OFF by default (the user, 2026-10-18): it changes how the game is played,
    // so it stays the player's decision.
    { "eye_camera", Tier::Extension, Class::A, false,
      "Eye-level camera",
      "Follow a citizen at eye level, looking where they look.",
      0, [](State& s) -> bool& { return s.ext.eye_camera; } },

    // BETTERCAM (docs/extensions/handins/BETTERCAM.md): Banished Game Tweaks' wider zoom range, in source. The
    // steady tilt it also carried is ext.camera_angle's slider since 2026-11-08 (a file with better_camera=1 and no
    // camera_angle key is migrated to the slider's steady point, registry_boot.cpp). Class B (PR review): with the
    // row on, the zoom target and the camera distance, which retail serializes, can hold values outside retail's
    // range, so a save records the extension. ON by default on a desktop (ruled 2026-11-08 for 1.0.3); the Android
    // first run seeds it off (app_android.cpp kPhoneSettings: the touch zoom has its own near limit).
    { "better_camera", Tier::Extension, Class::B, true,
      "Wider zoom",
      "Zoom in much closer and out much farther (6 to 200 instead of 20 to 130). Takes effect at the next new "
      "game or load.",
      1, [](State& s) -> bool& { return s.ext.better_camera; } },
    // CAMERAANGLE (docs/extensions/handins/CAMERAANGLE.md): how far the camera tilts toward top-down as it zooms
    // out, ext.camera_tilt 0..100 (the Enhanced tab's "Camera angle" slider, 50 = this row off = retail). Class A:
    // the pitch target is recomputed every frame, nothing saved, live. ON by default on a desktop (ruled 2026-11-08):
    // with ext.camera_tilt at its default 50 that is retail's pitch, the slider in place; the Android first run seeds
    // it off.
    { "camera_angle", Tier::Extension, Class::A, true,
      "Camera angle",
      "Changes how far the camera tilts toward top-down as you zoom out: steeper, or the same tilt at every "
      "zoom and a little flatter. Set it in Options > Enhanced > Camera angle.",
      0, [](State& s) -> bool& { return s.ext.camera_angle; } },
    // RECIPESKIP (docs/extensions/handins/RECIPESKIP.md): a workshop set to every recipe stops, in retail, as
    // soon as ONE recipe's material is at its limit. With the row on that recipe is left out of the draw and
    // the building only stops when every recipe is at its limit. Class B: no serialized state, but the
    // town's production differs from retail's, so it is locked for the game and recorded in the save.
    { "recipe_skip", Tier::Extension, Class::B, true,
      "Skip recipes at their limit",
      "A workshop that makes every recipe in turn keeps working on the others when one recipe's limit "
      "is reached, instead of stopping.",
      1, [](State& s) -> bool& { return s.ext.recipe_skip; } },
    // HIGHLIMITS (docs/extensions/handins/HIGHLIMITS.md): the limit spinners of the Town Hall, the Limits
    // window and the production buildings take values up to 999,999,999 and 9 typed digits instead of
    // 999,999 and 6. Class B: no serialized state, but a limit above 999,999 is a value the faithful
    // spinner cannot show, so it is locked for the game and recorded in the save.
    { "high_limits", Tier::Extension, Class::B, true,
      "Higher limit values",
      "Lets the limit boxes in the Town Hall, the Limits window and the production buildings take values "
      "up to 999,999,999 (retail stops at 999,999). Play diverges.",
      1, [](State& s) -> bool& { return s.ext.high_limits; } },

    { "sharp_ui", Tier::Extension, Class::A, true,
      "Sharp UI",
      "Crisp text at any UI scale; with icons (the Enhanced tab's Sharp UI), crisp menu sprites too. "
      "Layout is unchanged.",
      0, [](State& s) -> bool& { return s.ext.sharp_ui; } },

    { "fast_paths", Tier::Extension, Class::B, true,
      "Faster pathfinding",
      "Same-length routes for far less work. Ties can differ, so play diverges.",
      1, [](State& s) -> bool& { return s.ext.fast_paths; } },

    { "path_workers", Tier::Extension, Class::B, true,
      "Parallel pathfinding",
      "Finds paths on several threads. Same paths, but play diverges.",
      1, [](State& s) -> bool& { return s.ext.path_workers; } },

    { "render_culling", Tier::Extension, Class::A, true,
      "Skip unseen work",
      "Off-screen citizens, icons and effects pause until you look.",
      0, [](State& s) -> bool& { return s.ext.render_culling; } },

    { "anim_batch", Tier::Extension, Class::B, true,
      "Batched animation",
      "Above 1x speed, animations step once per frame. Play diverges.",
      1, [](State& s) -> bool& { return s.ext.anim_batch; } },

    { "draw_gather", Tier::Extension, Class::A, true,
      "Faster scene gathering",
      "Skips out-of-view objects without touching them. Same picture.",
      0, [](State& s) -> bool& { return s.ext.draw_gather; } },
    // DRAWBATCH (docs/extensions/handins/DRAWBATCH.md): consecutive decals, roads and smoke
    // emitters of one material drawn as one dynamic draw; the same vertex bytes in the same order.
    { "draw_batch", Tier::Extension, Class::A, true,
      "Batched small draws",
      "Draws runs of decals, roads and smoke in one go. Same picture.",
      0, [](State& s) -> bool& { return s.ext.draw_batch; } },
    { "walk_batch", Tier::Extension, Class::A, true,
      "Batched walking",
      "Above 1x speed, walking steps take a shortcut. Same positions.",
      0, [](State& s) -> bool& { return s.ext.walk_batch; } },

    // WALKGRID: walk_batch's measured class B variant (WALKBATCH.md sections 3, 11, 13). On, it
    // batches the walk by itself (walk_batch need not be on) and also skips Place.
    { "walk_batch_grid", Tier::Extension, Class::B, true,
      "Batched walking, faster",
      "Batched walking; walkers seen a frame late. Diverges from the original.",
      1, [](State& s) -> bool& { return s.ext.walk_batch_grid; } },

    // RENDERPIPE (THREADRESEARCH.md section 1.5, THREADPLAN.md section 1 rows 1-2): the frame's
    // emit one frame behind the gather. RENDERPIPE-0 runs it on the main thread in that order;
    // RENDERPIPE-1 moves it onto its own thread.
    { "render_pipe", Tier::Extension, Class::A, true,
      "Render thread",
      "Renders on a second thread. Higher fps, one frame more latency.",
      0, [](State& s) -> bool& { return s.ext.render_pipe; } },

    // CULLPAR (docs/extensions/handins/CULLPAR.md): the draw gather's walk split over helper
    // threads; the helper count is Cpu::CullHelpers (src/platform/common/cpu/roster.h), 0 on 4 cores.
    { "gather_threads", Tier::Extension, Class::A, true,
      "Parallel culling",
      "Faster scene gathering split over threads (5+ cores).",
      0, [](State& s) -> bool& { return s.ext.gather_threads; } },

    // SPLASH (docs/extensions/handins/SPLASH.md; LOADPLAN.md section 1 rulings 1 and 3): retail's
    // minimum display / operation times end once the work they cover is done. The fades stay.
    { "quick_screens", Tier::Extension, Class::A, true,
      "Quick loading screens",
      "Loading and save screens close as soon as the work is done.",
      0, [](State& s) -> bool& { return s.ext.quick_screens; } },

    // AO (docs/extensions/AO.md): ASSAO multiplied into the finished opaque colour. Its quality and
    // strength are the valued keys ao_quality / ao_opacity below (the Options dialog's tab).
    { "ambient_occlusion", Tier::Extension, Class::A, true,
      "Ambient occlusion",
      "Screen-space ambient occlusion on the opaque scene (ASSAO).",
      0, [](State& s) -> bool& { return s.ext.ambient_occlusion; } },

    // BETTERWATER (docs/extensions/handins/BETTERWATER.md): a shore edge on the water -- a shallow tint and
    // a thin broken foam line at the bank, drawn as a second pass over each water range with our own
    // pixel program. Class A: drawing only, nothing saved, live; ON by default as class A is (DESIGN.md ง2).
    { "better_water", Tier::Extension, Class::A, true,
      "Better water",
      "A soft shore edge on rivers and lakes: shallow water at the bank and a thin line of foam at the "
      "waterline.",
      0, [](State& s) -> bool& { return s.ext.better_water; } },

    // BETTER_TREES (docs/extensions/handins/BETTER-TREES.md; the history in FOLIAGE_AA.md and
    // TREE_SHADOW.md): one row for the tree look. The alpha-tested leaf programs are rewritten at load
    // from the user's own shaders (alpha to coverage + a sharpened alpha), the billboard canopies look
    // the sun shadow up from the sun-facing card, and the cut is dappled per leaf. The foliage mode is
    // the valued key foliage_aa_mode below.
    { "better_trees", Tier::Extension, Class::A, true,
      "Better trees",
      "Smooth leaf edges (MSAA), tree shading that no longer turns with the view, leaf-scale shadow dapple.",
      0, [](State& s) -> bool& { return s.ext.better_trees; } },
    // BOATLANE (docs/extensions/handins/BOATLANE.md): the trade boats sail a lane through the middle of the
    // water, a smooth curve, instead of retail's grid steps along the cheapest bank; the same lane
    // ext.winter_ice keeps open in the ice. Class B: a boat's route and so its travel time change. OFF by
    // default, the B / C rule (the user's ruling, 2026-11-06; extension defaults to be revisited next
    // release).
    { "boat_lane", Tier::Extension, Class::B, true,
      "Boat lane",
      "Trade boats sail a smooth lane down the middle of rivers and lakes instead of hugging the cheapest "
      "bank in grid steps.",
      1, [](State& s) -> bool& { return s.ext.boat_lane; } },
    // WINTERICE (docs/extensions/handins/WINTERICE.md): water freezes in winter -- ice from the banks inward
    // while it is below 32 F, the shallow sides thawing first in spring, open water round buildings in the
    // water and along the trade boats' channel. Drawn as one more pass over each water range with our own
    // pixel program. Class A: drawing only, nothing saved; ON by default (the standing class A rule).
    { "winter_ice", Tier::Extension, Class::A, true,
      "Winter ice",
      "Lakes and rivers freeze over in cold winters: ice grows from the banks inward and melts from the "
      "sides in spring. The trade boats' channel stays open.",
      0, [](State& s) -> bool& { return s.ext.winter_ice; } },
    // WALKONICE (docs/extensions/handins/WINTERICE.md section 5): while a lake is frozen solid from bank to bank
    // its cells are open to walkers (never a river, never the open water round a dock or along the boat
    // channel). Class C: the ice is the town's own state, kept in the save's tail
    // (src/ext/winter_ice/ice_town.cpp), so the lake opens on the same day after a reload. Owner winter_ice.
    { "walk_on_ice", Tier::Extension, Class::C, true,
      "Walk on ice",
      "Citizens and animals can cross a lake while it is frozen solid in winter. Rivers stay a barrier. Play "
      "diverges; the ice is recorded in the save.",
      1, [](State& s) -> bool& { return s.ext.walk_on_ice; } },
    // BETTERCLOUDS (docs/extensions/handins/BETTERCLOUDS.md): the sky draws with a pixel program of ours,
    // procedural clouds drifting down the wind over the painted sky blurred; the mode below adds Fancy.
    // Class A: drawing only, nothing saved, live; ON by default (the standing class A rule).
    { "better_clouds", Tier::Extension, Class::A, true,
      "Better clouds",
      "Clouds drift across the sky and its reflection in the water with the wind, thicker and darker "
      "in winter and in bad weather. They stop when the game is paused.",
      0, [](State& s) -> bool& { return s.ext.better_clouds; } },
    // FANCYTERRAIN (src/ext/fancy_terrain/fancy_terrain.h): the terrain shades itself from a horizon map of
    // the heightmap (sun hidden by a ridge, less sky in a valley, a warm bounce), multiplied into the opaque
    // scene at the ground-draw seam. Class A: drawing only, nothing saved, live; ON by default.
    { "fancy_terrain", Tier::Extension, Class::A, true,
      "Fancy terrain",
      "Hills and cliffs shade the land around them: the sun is hidden behind ridges, valleys and the foot "
      "of a cliff see less sky, and shaded slopes keep a warm light. Terrain edits update it.",
      0, [](State& s) -> bool& { return s.ext.fancy_terrain; } },
    // DOF (docs/extensions/DOF.md section 3.1): a half-resolution depth of field after the main scene's lists
    // (the scene-draw seam), focused on the camera's target. Class A: drawing only, nothing saved, live. The row
    // ships ON; the blur itself is the valued key ext.depth_blur (the Enhanced tab's "Depth blur"), OFF by default.
    { "depth_of_field", Tier::Extension, Class::A, true,
      "Depth blur",
      "What is far from the point the camera looks at is softly blurred, like a photograph with a shallow "
      "depth of field: the far hills, the sky and anything close to the camera.",
      0, [](State& s) -> bool& { return s.ext.depth_of_field; } },
    // LEAF_LOD (docs/extensions/LEAF_LOD.md): zoomed out, every leafy tree draws a share of its leaf cards,
    // each a little larger, from reduced copies of the installed meshes built at run time. Class A: drawing
    // only, nothing saved, live. On the Experimental tab while the look is being judged.
    { "leaf_lod", Tier::Extension, Class::A, true,
      "Lighter far trees",
      "When the camera is zoomed out, leafy trees are drawn with fewer, larger leaves. Faster over forests.",
      0, [](State& s) -> bool& { return s.ext.leaf_lod; } },
    // FAA-SHADOW (docs/extensions/FOLIAGE_AA.md section 7): the sun map at 4096 when the retail
    // Shadow Resolution combo is at High; Medium / Low keep their retail sizes.
    { "max_shadow_res", Tier::Extension, Class::A, true,
      "Max shadow resolution",
      "Shadow Resolution High builds a 4096 shadow map instead of 2048.",
      0, [](State& s) -> bool& { return s.ext.max_shadow_res; } },
    // RESTRICTEDFOODS (docs/extensions/handins/RESTRICTEDFOODS.md): the foods checked on the Foods page stay
    // Edible for storage, markets and recipes but are not eaten by citizens. Class C: the checked list is the
    // town's own state, recorded in the save's tail (src/ext/restricted_foods/save_state.cpp).
    { "restricted_foods", Tier::Extension, Class::C, true,
      "Restricted foods",
      "Lets you stop citizens eating foods you choose, from the Foods page in a game. Play diverges; the "
      "choice is recorded in the save.",
      1, [](State& s) -> bool& { return s.ext.restricted_foods; } },
    // ITEMPRIO (docs/extensions/handins/ITEMPRIO.md): the town ranks the tools, fuels, clothing and
    // health items its citizens fetch from a storage; a citizen takes the best-ranked item. Class C:
    // the ranked lists are the town's own state, recorded in the save's tail
    // (src/ext/item_priority/save_state.cpp).
    { "item_priority", Tier::Extension, Class::C, true,
      "Item priority",
      "Lets you rank the tools, fuels, clothing and health items citizens take first, from the "
      "Priority page in a game. Play diverges; the choice is recorded in the save.",
      2, [](State& s) -> bool& { return s.ext.item_priority; } },
    // ITEMLIMITS (docs/extensions/handins/ITEMLIMITS.md): a limit for one item beside its category's, set in
    // the panel of a workshop that makes it; every workshop making the item stops at it. Class C: the limits
    // are the town's own state, recorded in the save's tail (src/ext/item_limits/save_state.cpp).
    { "item_limits", Tier::Extension, Class::C, false,
      "Item limits",
      "Lets you give one item, such as steel tools, its own limit beside its category's, from the panel "
      "of a workshop that makes it. Every workshop making that item stops at the limit. Play diverges; "
      "the limits are recorded in the save.",
      1, [](State& s) -> bool& { return s.ext.item_limits; } },

    // TERRAINEDIT (docs/extensions/handins/TERRAINEDIT.md): a brush tool, started from the Terrain page of
    // the extension window, that edits the terrain under the mouse. Class B: play diverges (the map is no
    // longer the generated one) and no state of its own is recorded, because the save already carries the
    // heights and the cell types the brush writes.
    { "terrain_edit", Tier::Extension, Class::B, false,
      "Terrain editor",
      "A brush, started from the Terrain page in a game, that reshapes the ground under the mouse. "
      "Play diverges.",
      1, [](State& s) -> bool& { return s.ext.terrain_edit; } },
    // TOWNSCREENSHOT (docs/extensions/handins/TOWNSCREENSHOT.md): one picture of the whole map from an isometric view,
    // taken from the Town Screenshot page of the extension window. Class B only because the window's pages belong to
    // B / C rows (the window is recorded and locked with the game): a screenshot changes nothing about how a town
    // plays, and no state of its own is recorded.
    { "town_screenshot", Tier::Extension, Class::B, false,
      "Town Screenshot",
      "Takes one picture of the whole map from above at an isometric angle, from the Town Screenshot page in a "
      "game. The game pauses while it works.",
      1, [](State& s) -> bool& { return s.ext.town_screenshot; } },
    // FAMILYTREE (docs/extensions/handins/FAMILYTREE.md): the town's births, marriages and deaths are written
    // down, so the Family page can show who is related to whom, living or dead. Class C: the record is the
    // town's own state, kept in the save's tail (src/ext/family_tree/family_tree.cpp).
    { "family_tree", Tier::Extension, Class::C, false,
      "Family tree",
      "Records who is born to whom, who marries and who dies, and shows each citizen's family on the Family "
      "page in a game. The record is kept in the save.",
      2, [](State& s) -> bool& { return s.ext.family_tree; } },
    // MAPOVERLAYS (docs/extensions/handins/MAPOVERLAYS.md): SimCity-style data views beside the map window's
    // map (production, consumption, stock, services, traffic, citizens, natural resources). Class C: the last
    // year's production, consumption and traffic history is the town's own state, kept in the save's tail
    // (src/ext/map_overlays/save_state.cpp).
    { "map_overlays", Tier::Extension, Class::C, true,
      "Map data views",
      "Adds views to the map window that show where goods are produced, consumed and stored, where services "
      "reach and where citizens walk. The last year's history is kept in the save.",
      1, [](State& s) -> bool& { return s.ext.map_overlays; } },
    // HITCH_PFBURST (docs/extensions/handins/HITCH_PFBURST.md): a per-frame budget of the cold
    // citizens' warm-house scans (one region path search per residence), so a cold spell at high
    // speed spreads its scans over a few frames instead of one 10-20 ms frame.
    { "spread_warmth", Tier::Extension, Class::B, true,
      "Spread warmth searches",
      "At high speed, cold citizens pick a warm house a few frames later. Play diverges.",
      1, [](State& s) -> bool& { return s.ext.spread_warmth; } },
    // HITCH_SAVE (docs/extensions/handins/HITCH_SAVE.md): the save's 32 KB windows are compressed
    // on a worker thread, in retail's order, while the main thread is still writing the game
    // state. The save file is byte-identical; the save frame loses the compression time.
    { "async_save_compress", Tier::Extension, Class::A, true,
      "Background save compression",
      "Saves compress on a worker thread while the game state is written. Shorter save stutter, "
      "identical save files.",
      0, [](State& s) -> bool& { return s.ext.async_save_compress; } },
    // HITCH_DRAINSPLIT (docs/extensions/handins/HITCH_DRAINSPLIT.md): the PathGraph drain's two
    // halves in two frames -- ReassignWorkers in the drain tick, AssignCitizens in the first sim
    // tick of the next frame -- so the five-minute job pass is not one frame's work at high speed.
    { "drain_split", Tier::Extension, Class::B, true,
      "Split the job reassignment",
      "At high speed, the five-minute job assignment runs one frame after the housing pass. "
      "Play diverges.",
      1, [](State& s) -> bool& { return s.ext.drain_split; } },
    // HITCH_SAVESNAP (docs/extensions/handins/HITCH_SAVESNAP.md): the save's game state is
    // serialised on a worker while the sim is frozen; frames and the camera keep going. The save
    // file is byte-identical (the same world state, nothing changes it while the worker runs).
    { "background_save", Tier::Extension, Class::A, true,
      "Background saving",
      "Saves are written on a worker thread while the town holds still for a moment; the screen "
      "and the camera keep moving. Identical save files.",
      0, [](State& s) -> bool& { return s.ext.background_save; } },
    // TERRAINBATCH (docs/extensions/handins/TERRAINBATCH.md; gpuframe/ideas_passes.md ranks 2 and 4):
    // each grid row's run of visible terrain chunks is one draw (same vertices, indices, order and
    // state as the chunk draws), and the shadow pass skips the chunks whose shadow cannot reach the
    // view. The picture is unchanged.
    { "terrain_batch", Tier::Extension, Class::A, true,
      "Batched terrain drawing",
      "Draws each row of visible terrain in one call and skips shadow work for terrain whose shadow "
      "cannot reach the screen. Same picture.",
      0, [](State& s) -> bool& { return s.ext.terrain_batch; } },
    // SEAMLESS (docs/extensions/handins/SEAMLESS.md, CHATTER-DIAG.md): every looping ambience's decoded
    // PCM loses its codec tail and has its end folded into its start, at load. The seam
    // Audio::SoundBuffer::_extLoopSeam is installed at boot, so a toggle takes effect at the next start.
    { "seamless_loops", Tier::Extension, Class::A, true,
      "Seamless ambient loops",
      "Blends the end of each looping ambient sound into its start, removing the dropout at the loop "
      "point. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.seamless_loops; } },
    // DECODEAHEAD (docs/extensions/handins/DECODEAHEAD.md, issue #13): while a resource loads, the
    // next compressed blocks of its file are decoded on worker threads. The same bytes, the same load
    // order; the seam IO::CompressedStream::_extDecodeAhead is installed at boot (next start).
    { "decode_ahead", Tier::Extension, Class::A, true,
      "Parallel resource decoding",
      "Decodes the next parts of a resource on other cores while it loads, so large mods load "
      "faster. Same game data. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.decode_ahead; } },
    // LOADPROGRESS (docs/extensions/handins/LOADPROGRESS.md, issue #13): while the boot loads the
    // game data and the enabled mods' lists, the black loader frames show the game's own loading
    // screen and which mod is loading. Only with a mod enabled; read at the next start.
    { "load_progress", Tier::Extension, Class::A, true,
      "Mod loading screen",
      "While mods load at start-up, shows the loading screen and which mod is loading instead of a "
      "black window. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.load_progress; } },
    // ASYNCGPUCREATE (docs/extensions/handins/ASYNCGPUCREATE.md, issue #13): while the loader reads a
    // resource file, the GPU buffers and textures it makes are created on worker threads. The same
    // resources, all made before the load ends; the backend reads the request at the next load.
    { "async_gpu_create", Tier::Extension, Class::A, true,
      "Parallel graphics loading",
      "Creates the graphics resources of loading content on other cores, so large mods load faster. "
      "Same game data. Takes effect at the next load.",
      0, [](State& s) -> bool& { return s.ext.async_gpu_create; } },
    // COMPRESSAO (docs/extensions/handins/COMPRESSAO.md, issue #13): the baked AO maps (2D R8 textures
    // from the resource files) are created as BC4, half the memory, the same look; the encode is kept
    // in the derived-data cache in the user's documents folder. Read once at boot (next start).
    { "compress_ao", Tier::Extension, Class::A, true,
      "Compressed AO maps",
      "Keeps the models' baked shading maps compressed: half the memory, the same look. Prepared once "
      "after a mod change, then reused. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.compress_ao; } },
    // SHAREGPUDATA (docs/extensions/handins/SHAREGPUDATA.md, issue #13): immutable vertex / index
    // buffers and file-seeded textures with the same descriptor and the same bytes are created once
    // and shared by every engine object that loads them. Read once at boot (next start).
    { "share_gpu_data", Tier::Extension, Class::A, true,
      "Shared graphics data",
      "Keeps one copy of graphics data that several models or mods load identically: less memory, "
      "the same look. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.share_gpu_data; } },
    // CREATEONDEMAND (docs/extensions/handins/CREATEONDEMAND.md, issue #13): static vertex / index
    // buffers and file-seeded textures are created at their first use (or when the world places a
    // mesh that uses them) instead of at load; their bytes are read again from the mapped package.
    // Read once at boot (next start). ON by default (the user ruled 2026-10-23).
    { "create_on_demand", Tier::Extension, Class::A, true,
      "Load graphics on demand",
      "Prepares a model's graphics when the game first shows it instead of at start-up: much less "
      "memory with large mods. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.create_on_demand; } },
    // STREAMCACHE (docs/extensions/handins/STREAMCACHE.md, issue #13): the part of each compiled
    // resource a load reads (all of a small one; a mesh's or texture's header and size fields when
    // create_on_demand skips the payloads) is kept decoded in the derived-data cache and read from
    // there at the next start, instead of through the compressed stream. Read once at boot (next
    // start). ON by default, like the other derived-cache rows.
    { "stream_cache", Tier::Extension, Class::A, true,
      "Cache decoded resources",
      "Keeps the small part of every resource file the game reads at start-up decoded on disk "
      "(in the cache folder), so the next start skips the decompression. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.stream_cache; } },
    // PKGCACHE (docs/extensions/handins/PKGCACHE.md, issue #13): the merged directory of the mounted
    // packages, built at every mount by one search and one insert per package entry, is kept in the
    // derived-data cache and built back node for node at the next mount of the same packages. Read
    // at each mount (live).
    { "package_cache", Tier::Extension, Class::A, true,
      "Remembered mod file lists",
      "Keeps the combined file list of the mounted mods in the cache folder, so starting the game or "
      "reloading mods skips rebuilding it. Same game data.",
      0, [](State& s) -> bool& { return s.ext.package_cache; } },
    // MENUENTRY (docs/extensions/handins/MENUENTRY.md, issue #13): entering the main menu rescans
    // every .pkm and then remounts every package, even when nothing changed. The scan takes each
    // unchanged .pkm's record (same name, size, write time) from the last scan, and the remount is
    // skipped when what it would mount is what is mounted. Read at each menu entry (live).
    { "menu_entry", Tier::Extension, Class::A, true,
      "Quick return to the menu",
      "Skips rescanning and remounting the mod files when returning to the main menu if none of "
      "them changed. Same game data.",
      0, [](State& s) -> bool& { return s.ext.menu_entry; } },
    // VISITREPLAY (docs/extensions/handins/VISITREPLAY.md, issue #13): each loader bracket's first
    // touch of a resource re-walks what it references; the rows that walk reaches are recorded and
    // the next bracket reaches the same rows from the record. Read once at boot (next start).
    { "visit_replay", Tier::Extension, Class::A, true,
      "Remembered resource links",
      "Remembers which resources each resource uses, so the loading after the menu, a new game or a "
      "load skips walking them again. Same game data. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.visit_replay; } },
    // VISITREPLAY section B4: FindOrLoad's hit arm answered from a request (or a save's reference
    // id) that already named a row. Read once at boot (next start).
    { "find_fastpath", Tier::Extension, Class::A, true,
      "Faster resource lookups",
      "Remembers where each resource a load asks for already is, so loading a large mod set or a save "
      "skips looking it up again. Same game data. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.find_fastpath; } },
    // HEAPSLACK (docs/extensions/handins/HEAPSLACK.md, issue #13): the allocator keeps the pages it
    // would decommit at the top of a range (up to 64 MB in all) and zeroes them itself when retail
    // would have committed them fresh. Same bytes in every allocation. Read once at boot (next start).
    // ON by default: a MegaMod start without the derived cache 15.75 -> 14.35 s to the menu; warm
    // starts and the stress town are unchanged.
    { "heap_slack", Tier::Extension, Class::A, true,
      "Fewer memory round trips",
      "Keeps recently freed memory ready instead of handing it back to the system and asking for it "
      "again, so large mod sets load faster. Same game data. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.heap_slack; } },
    // NOFOG (docs/extensions/handins/NOFOG.md): the fog is pushed back on the finished lighting each frame,
    // as far as ext.fog_distance says (100, the default: taken off), so the far map stays clear when the
    // camera is zoomed out. The Enhanced tab's "Fog distance" slider sets both (0 = this row off). Class A: a
    // picture-only change, no sim effect, nothing saved, live. ON by default on a desktop (ruled 2026-11-08 for
    // 1.0.3) with ext.fog_distance at its default 0, which is retail's fog: the slider in place, nothing changed
    // until the player moves it. The Android first run seeds it off.
    { "no_fog", Tier::Extension, Class::A, true,
      "Fog distance",
      "Pushes back the fog that hazes the far map when the camera is zoomed far out, up to no fog at all. "
      "Set how far in Options > Enhanced > Fog distance.",
      0, [](State& s) -> bool& { return s.ext.no_fog; } },
    // SHOWGRID (docs/extensions/handins/SHOWGRID.md): a grid of tile outlines over the map, for planning with
    // the game paused. Class A: a picture-only change, no sim effect, nothing saved, live. OFF by default:
    // how the map looks is the player's choice (like no_fog).
    { "show_grid", Tier::Extension, Class::A, false,
      "Show grid",
      "Draws a grid over the map so you can see where the tiles are while planning where to build.",
      0, [](State& s) -> bool& { return s.ext.show_grid; } },
    // FPSCOUNTER (docs/extensions/handins/FPSCOUNTER.md): the frame rate the game draws at (current, average,
    // minimum, maximum) on an FPS page of the extension window. Class A: it only counts frames, nothing saved,
    // live. OFF by default: the page brings the toolbar's extension button, which is the player's call.
    { "fps_counter", Tier::Extension, Class::A, false,
      "FPS counter",
      "Adds an FPS page to the extension window (the toolbar's Reports group) with the current, average, "
      "minimum and maximum frame rate.",
      0, [](State& s) -> bool& { return s.ext.fps_counter; } },
    // BUILDINGFINDER (docs/extensions/handins/BUILDINGFINDER.md): a Find Buildings page in the extension
    // window: a search box over every building the toolbar can place (the ones the town cannot build now
    // too), and a favorites list the player keeps in a text file beside settings.cfg, shown in its own small
    // window. Class A: it keeps no state in the game and nothing in the simulation reads it. OFF by default:
    // it adds a tab to the extension window.
    { "building_finder", Tier::Extension, Class::A, false,
      "Building finder",
      "Adds a Find Buildings page to the extension window: search every building by name, and keep a list of "
      "favorites you can start placing with one click.",
      0, [](State& s) -> bool& { return s.ext.building_finder; } },
    // DYNAMICPROFESSIONS (docs/extensions/handins/DYNAMICPROFESSIONS.md): the toolbar's Professions button
    // opens a window that lists only the professions the town can use (the laborer and the builder always,
    // then every profession with a workplace built or a citizen in it), in place of retail's list of all of
    // them. Class A: it only reads the town and moves citizens between professions with retail's own calls;
    // nothing is kept.
    // OFF by default: it replaces a window the player knows.
    { "dynamic_professions", Tier::Extension, Class::A, false,
      "Dynamic professions",
      "Replaces the Professions window with one that lists only the professions your town can use: the "
      "laborer and builder, and any profession with a workplace built or a citizen working at it.",
      0, [](State& s) -> bool& { return s.ext.dynamic_professions; } },
    // TOOLNAMES (docs/extensions/handins/TOOLNAMES.md): the citizen panel names the tool the citizen carries
    // (the item's own name) instead of retail's "default tool / other tool" words. Class C: which tool each
    // citizen carries is the town's own state, recorded in the save's tail so a tool keeps its name across a
    // load (src/ext/tool_names/tool_names.cpp). No sim effect. ON by default (ruled 2026-11-08: modded tools are the common case).
    { "tool_names", Tier::Extension, Class::C, true,
      "Tool names",
      "The citizen panel shows the name of the tool a citizen carries, so modded tools are not all shown "
      "as iron or steel. The tool each citizen carries is recorded in the save.",
      1, [](State& s) -> bool& { return s.ext.tool_names; } },
    // TEXQUALITY (docs/extensions/handins/TEXQUALITY.md): the large mipmapped world textures are made
    // without their ext.texture_quality_drop largest levels (half or quarter resolution), so far less
    // video memory and less decoding at load. Retail has no such setting. Read once at boot (next start).
    // OFF by default: how sharp the textures are is the player's choice.
    { "texture_quality", Tier::Extension, Class::A, false,
      "Reduced texture quality",
      "Loads the large world textures at half or quarter resolution (the Enhanced tab's Texture quality): "
      "much less video memory, softer textures. Takes effect at the next start.",
      0, [](State& s) -> bool& { return s.ext.texture_quality; } },
    // PHONEMENU (docs/extensions/handins/PHONEMENU.md): the front end fitted to a phone held in
    // landscape -- the menu backdrop moved so the logo is whole on a screen wider than 16:9, the menu
    // dialogs and the in-game interface laid out inside a 4.5 % safe area (rounded corners, the camera
    // cut-out), and the UI scale slider down to 25 %. Class A: layout only, nothing saved, live. OFF by
    // default on a desktop; the Android first run turns it on (src/platform/android/app_android.cpp).
    { "phone_menu", Tier::Extension, Class::A, false,
      "Phone screen fit",
      "Keeps the menus and the game's interface clear of rounded screen corners and camera cut-outs, "
      "shows the whole Banished logo on wide screens, and lets the UI scale go down to 25% (a much larger "
      "interface).",
      0, [](State& s) -> bool& { return s.ext.phone_menu; } },
    // FPSCAP (docs/android/P8_INPUT_OPTIONS.md): a frame-rate ceiling for the menu and the game
    // (src/app/frame_limiter.h), at ext.fps_cap_rate. Class A: frames only; the sim's fixed step runs
    // every tick the elapsed time owes, nothing is saved, live. OFF by default on a desktop (the game
    // stays uncapped, as retail); the Android first run turns it on at 60 (ruled 2026-10-25).
    { "fps_cap", Tier::Extension, Class::A, false,
      "Frame rate cap",
      "Limits the frame rate in the menu and in game (the Enhanced tab's Frame rate cap): less power and "
      "heat, the simulation unchanged.",
      0, [](State& s) -> bool& { return s.ext.fps_cap; } },
    // TOUCHMODE (docs/android/TOUCH_MODE.md): a camera pad and rotate / zoom buttons bottom-left, the
    // in-game toolbar at its own scale along the bottom (ext.touch_mode_toolbar_scale, shrunk to fit).
    // Class A: input and layout only, nothing saved, live. OFF by default on a desktop; the Android
    // first run turns it on (ruled 2026-10-26).
    { "touch_mode", Tier::Extension, Class::A, false,
      "Touch controls",
      "A camera pad and zoom / rotate buttons at the bottom left, and the toolbar enlarged for fingers "
      "along the bottom; the camera zooms in closer. For phones and touch screens.",
      0, [](State& s) -> bool& { return s.ext.touch_mode; } },
    // P11 (docs/android/P11_TOUCH_POINTER.md): fingers drive the game's own tools: one finger aims a placement
    // or a road, an action bar places / rotates / cancels, two fingers pan, pinch-zoom and spin, a tap picks
    // the nearest thing. Class A: input only, nothing saved, live. OFF by default on a desktop; the Android
    // first run turns it on (ruled 2026-10-26).
    { "touch_pointer", Tier::Extension, Class::A, false,
      "Touch pointer",
      "Fingers aim buildings and roads before they are placed (an action bar places, rotates and cancels), "
      "two fingers move, zoom and turn the view, and a tap selects the nearest person or building. For "
      "phones and touch screens.",
      0, [](State& s) -> bool& { return s.ext.touch_pointer; } },
    // Issue #253 (graphics/stagger_hold.h): the sun's shadow map and the water's reflection may each keep
    // last frame's image on a near-still view, rendering at least every ext.staggered_passes_shadow /
    // _reflection frames (2: one frame held at most, the two passes on alternate frames) and at once on a
    // turn, a move past a small tolerance, a zoom, a settings change or a target rebuild. Class A: frames
    // only, nothing saved, live. ON by default on every platform (ruled 2026-11-08 for 1.0.3; it was OFF on a
    // desktop): a held frame lags moving shadows and reflections by a frame, ruled acceptable. The Android
    // first run also seeds it on (src/platform/android/app_android.cpp kPhoneSettings, ruled 2026-11-05).
    { "staggered_passes", Tier::Extension, Class::A, true,
      "Staggered shadow and reflection",
      "Renders the shadows and the water's reflection every other frame while the view is still, on "
      "alternate frames: a higher frame rate on slower devices; moving shadows and reflections may "
      "trail by a frame.",
      0, [](State& s) -> bool& { return s.ext.staggered_passes; } },
    // FULLSCREENMODE (src/ext/fullscreen_mode/fullscreen_mode.h): the Options dialog's Video tab shows one
    // "Fullscreen" combo Off / Exclusive / Borderless in place of retail's checkbox, and the DX11 renderer
    // gains a borderless fullscreen (a windowed swap chain in a popup over the monitor, at the desktop's
    // size; alt-tab leaves it on screen). Class A: the window only, nothing saved in a town. ON by default:
    // with ext.fullscreen_borderless at 0 the combo's Exclusive is retail's checkbox ticked, so nothing changes
    // until the player picks Borderless. Offered only by a renderer with an exclusive mode (DX11).
    { "fullscreen_mode", Tier::Extension, Class::A, true,
      "Fullscreen mode",
      "The Video tab's Fullscreen setting becomes Off / Exclusive / Borderless. Borderless covers the "
      "screen at the desktop's resolution and stays on screen when you switch to another window.",
      0, [](State& s) -> bool& { return s.ext.fullscreen_mode; } },
    // RENDERBACKEND (src/ext/render_backend/render_backend.h): the dual Windows exe carries DirectX 11 and
    // Vulkan behind retail's one renderer slot; the row lists both in the Video tab's Renderer combo and
    // starts the one ext.render_api names, falling back to DirectX 11 when Vulkan cannot start. Class A:
    // the renderer only, nothing saved in a town. Off: retail's DirectX 11. A one-backend build ignores it.
    // DEFAULT ON (maintainer ruling 2026-11-08, for 1.0.3; it was off in the table so a current player stayed
    // on DirectX 11): every settings.cfg without the key now starts the renderer ext.render_api names, Vulkan by
    // default. A fresh install still has both keys written at its first start (Ext::RenderBackend::
    // SeedFreshInstall), which now only makes them explicit. The DirectX 11 fallback is on in every case, and
    // a fallback is SAVED: the backend that started is written to ext.render_api (and the row turned on)
    // so the next launch starts on it (Ext::RenderBackend::SaveFallback). docs/extensions/handins/RENDERBACKEND.md.
    { "render_backend", Tier::Extension, Class::A, true,
      "Renderer choice",
      "The Video tab's Renderer lists DirectX 11 and Vulkan; the game restarts its graphics on the one you "
      "pick. If Vulkan cannot start on this computer, DirectX 11 is used.",
      0, [](State& s) -> bool& { return s.ext.render_backend; } },
    // WORKCAND (docs/extensions/handins/WORKCAND.md): the idle work search's candidates (the plain-cost
    // builder 0x1400f8000 of the walk 0x1400f79b0) in a sorted flat array instead of a transient AVL of one
    // MemAlloc node per job: the same jobs visited in the same order (retail's cost / work id order and
    // duplicate rule; the COMPPROF=4 shadow compares the two). Class B: the arena allocations it skips move
    // later heap addresses, and EntityList's update tree orders entities by address, so a town diverges
    // from retail's as it does between two installs (known_port_issues row 1280). Default ON (ruled
    // 2026-11-09); the open ruling is whether it is faithful perf work with no row (WORKCAND.md section 2).
    { "work_cand", Tier::Extension, Class::B, true,
      "Faster job search",
      "Idle citizens look through the open jobs without building a list item by item. The same jobs in "
      "the same order; play diverges.",
      1, [](State& s) -> bool& { return s.ext.work_cand; } },

#ifdef BANISHED_UNIT_TEST
    // The unit tests' three classes (registry.h). Not compiled into the exe.
    { "unit_a", Tier::Extension, Class::A, false, "Unit A", "test only", 0,
      [](State& s) -> bool& { return s.ext.unit_a; } },
    { "unit_b", Tier::Extension, Class::B, false, "Unit B", "test only", 1,
      [](State& s) -> bool& { return s.ext.unit_b; } },
    { "unit_c", Tier::Extension, Class::C, false, "Unit C", "test only", 3,
      [](State& s) -> bool& { return s.ext.unit_c; } },
#endif
};

constexpr int kEntryCount = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));

// THE VALUED KEYS (registry.h "VALUED KEYS"). Each names its owner row; the
// ranges are the menu's (AO.md ยง3: quality 1..3 = Low / Medium / High, Off is
// the owner row's bool; opacity 0..30 percent, default 20: the user's look of 2026-10-14 put
// the usable maximum at 20 and anything past 30 "iffy").
constexpr int32_t kMaxShadowSizes[] = { 4096, 8192 };
constexpr int32_t kFpsCapRates[] = { 30, 60, 90, 120 };
constexpr int32_t kTouchToolbarScales[] = { 100, 125, 150, 175, 200 };
constexpr int32_t kTouchPadScales[] = { 0, 50, 60, 75, 100, 125 };
constexpr int32_t kTouchActionBarScales[] = { 25, 30, 35, 50, 60, 75, 100, 125 };
constexpr int32_t kTouchCameraButtonScales[] = { 0, 15, 20, 25, 35, 50, 75, 100, 125 };
constexpr int32_t kPhoneSafeMargins[] = { 0, 2000, 3375, 4500, 6000 };
constexpr int32_t kTouchPointerLifts[] = { 0, 80, 120, 160, 200, 240 };
constexpr int32_t kTouchPointerPickRadii[] = { 0, 25, 50, 75, 100 };
constexpr int32_t kStaggerShadowEvery[] = { 1, 2, 3, 4 };
constexpr int32_t kStaggerReflectionEvery[] = { 1, 2, 4, 8 };

const ValueKey kValueKeys[] = {
    // 4 = ASSAO Lowest (half density, the phone tier; issue #253), appended after High so a stored
    // 1..3 keeps its meaning. The Enhanced tab lists it second (Off / Lowest / Low / Medium / High).
    { "ao_quality", "ambient_occlusion", 1, 4, 2,
      [](State& s) -> int32_t& { return s.val.ao_quality; }, nullptr, 0 },
    { "ao_opacity", "ambient_occlusion", 0, 30, 20,
      [](State& s) -> int32_t& { return s.val.ao_opacity; }, nullptr, 0 },
    // FOLIAGE_AA ยง1 ruling 4: the Enhanced tab's Better trees combo is Off / On / On + distance fill;
    // Off is the owner row's bool, 1 = smooth edges (On), 2 = smooth edges + distance fill (the
    // default when the row is on).
    { "foliage_aa_mode", "better_trees", 1, 2, 2,
      [](State& s) -> int32_t& { return s.val.foliage_aa_mode; }, nullptr, 0 },
    // FAA-SHADOW2: the sun map's raw size at High while the row max_shadow_res is on (the combo's
    // Off is the row's bool). Discrete: 4096 or 8192, anything else = 4096. Medium / Low keep
    // retail's 1024 / 512 whatever this says (src/engine/graphics/sunlight.cpp SunMapRawSize).
    { "max_shadow_size", "max_shadow_res", 4096, 8192, 4096,
      [](State& s) -> int32_t& { return s.val.max_shadow_size; }, kMaxShadowSizes, 2 },
    // SHADOWZOOM (docs/extensions/handins/SHADOWZOOM.md section 6): while the row is on, the shadow casters
    // are drawn into the share of the map the view can show (src/engine/graphics/shadow_zoom.h): the full
    // map up close, half of it along each axis when zoomed out at the default tilt. No rebuild; live.
    { "max_shadow_zoom", "max_shadow_res", 0, 1, 1,
      [](State& s) -> int32_t& { return s.val.max_shadow_zoom; }, nullptr, 0 },
    // ROCKSEAT: how fix.rock_seat seats a rock (src/ext/rock_seat/rock_seat_math.h): 1 sinks it to the
    // lowest ground under its bottom ring, 2 tilts it to the slope and seats it (the default: sink
    // buries small rocks on a slope, ROCKSEAT.md section 4). The only valued key owned by a fix; the
    // fix's bool is the off switch.
    { "rock_seat_mode", "rock_seat", 1, 2, 2,
      [](State& s) -> int32_t& { return s.val.rock_seat_mode; }, nullptr, 0 },
    // COMPRESSAO: the derived-data cache's size cap in MB (src/ext/derived_cache/derived_cache.h "THE
    // CAP"). Owned by compress_ao, the cache's first user; a later user shares the one cache and the
    // one cap. 4096 holds a MegaMod 7-part boot's AO maps (~1.04 GB) four times over.
    { "derived_cache_mb", "compress_ao", 256, 65536, 4096,
      [](State& s) -> int32_t& { return s.val.derived_cache_mb; }, nullptr, 0 },
    // CREATEONDEMAND section 7: minutes a made object may go unused before it is given back (released,
    // re-made from its package at the next use); 0 = never. Read once at boot. Default 0 until re-makes
    // are prefetched: a re-made object is otherwise created on the render thread in the frame that draws it.
    { "create_on_demand_idle_min", "create_on_demand", 0, 120, 0,
      [](State& s) -> int32_t& { return s.val.create_on_demand_idle_min; }, nullptr, 0 },
    // TEXQUALITY: the top mip levels a qualifying texture is made without while the row texture_quality is
    // on: 0 = Full (the default, so the row or profile=all alone drops nothing; ruled by the user),
    // 1 = half resolution (the combo's Half). Read once at boot. Quarter (2) was removed by the user's
    // ruling (too blurry): the range is 0..1, so a 2 from an older settings.cfg CLAMPS to 1 (ClampValue,
    // a range key). The code below the key still takes any drop N (texture_quality.h).
    { "texture_quality_drop", "texture_quality", 0, 1, 0,
      [](State& s) -> int32_t& { return s.val.texture_quality_drop; }, nullptr, 0 },
    // FPSCAP: the ceiling while the row fps_cap is on, one of the combo's rates; 60 is the phone's ruled
    // default. Any other number reads as 60 (a discrete key).
    { "fps_cap_rate", "fps_cap", 30, 120, 60,
      [](State& s) -> int32_t& { return s.val.fps_cap_rate; }, kFpsCapRates, 4 },
    // FULLSCREENMODE: what VideoFullscreen=1 means while the row fullscreen_mode is on: 0 = exclusive (retail's),
    // 1 = borderless. A range key.
    { "fullscreen_borderless", "fullscreen_mode", 0, 1, 0,
      [](State& s) -> int32_t& { return s.val.fullscreen_borderless; }, nullptr, 0 },
    // RENDERBACKEND: the renderer the dual exe starts while the row render_backend is on: 0 = DirectX 11,
    // 1 = Vulkan (the default: the row on means Vulkan first). A range key.
    { "render_api", "render_backend", 0, 1, 1,
      [](State& s) -> int32_t& { return s.val.render_api; }, nullptr, 0 },
    // SHOWGRID: the grid lines' hue in degrees (read every frame, so a settings.cfg edit shows at the next
    // start). A range key.
    { "show_grid_hue", "show_grid", 0, 359, 0,
      [](State& s) -> int32_t& { return s.val.show_grid_hue; }, nullptr, 0 },
    { "show_grid_saturation", "show_grid", 0, 100, 0,
      [](State& s) -> int32_t& { return s.val.show_grid_saturation; }, nullptr, 0 },
    { "show_grid_opacity", "show_grid", 10, 100, 75,
      [](State& s) -> int32_t& { return s.val.show_grid_opacity; }, nullptr, 0 },
    { "show_grid_shade", "show_grid", 0, 100, 100,
      [](State& s) -> int32_t& { return s.val.show_grid_shade; }, nullptr, 0 },
    { "show_grid_width", "show_grid", 1, 30, 6,
      [](State& s) -> int32_t& { return s.val.show_grid_width; }, nullptr, 0 },
    // TOUCHMODE: the toolbar's own scale in percent while the row touch_mode is on, one of the combo's
    // sizes; 200 is the ruled "about 2x" (shrunk to fit beside the controls). Any other number reads as 200.
    { "touch_mode_toolbar_scale", "touch_mode", 100, 200, 200,
      [](State& s) -> int32_t& { return s.val.touch_mode_toolbar_scale; }, kTouchToolbarScales, 5 },
    // Issue #96: the camera pad cluster's own size in percent (touch_math.h LayoutCluster's scale), 0 = the
    // cluster hidden (the gestures alone; the toolbar then takes the bottom from the left margin). 100 is the
    // size the cluster always had, so a settings.cfg without the key looks as before. Any other number reads as 100.
    { "touch_mode_pad_scale", "touch_mode", 0, 125, 100,
      [](State& s) -> int32_t& { return s.val.touch_mode_pad_scale; }, kTouchPadScales, 6 },
    // Issue #96: ext.touch_pointer's placement bar in percent of the cluster's 100 % cell (the bar always took
    // the cluster's cell), independent of the pad's scale. 100 = as before. Any other number reads as 100.
    { "touch_mode_action_bar_scale", "touch_mode", 25, 125, 100,
      [](State& s) -> int32_t& { return s.val.touch_mode_action_bar_scale; }, kTouchActionBarScales, 8 },
    // Issue #96 (the reporter's follow-up): the corner camera buttons, rotate left / right and zoom in / out in a
    // column in the upper right (touch_math.h LayoutCornerButtons), in percent of the pad's own button at pad
    // 100 %; 0 = none (no dialog, nothing to hit). Independent of the pad's key. Off by default, so nothing changes
    // for anyone who does not pick it. Any other number reads as 0.
    { "touch_mode_camera_buttons", "touch_mode", 0, 125, 0,
      [](State& s) -> int32_t& { return s.val.touch_mode_camera_buttons; }, kTouchCameraButtonScales, 9 },
    // P11d (docs/android/P11D_ZOOM.md): the camera's near zoom limit in distance units while the row touch_mode is
    // on (every zoom input: the buttons, the pinch, keys, the wheel); retail's is 20. A range key, settings.cfg only.
    { "touch_mode_zoom_min", "touch_mode", 6, 20, 10,
      [](State& s) -> int32_t& { return s.val.touch_mode_zoom_min; }, nullptr, 0 },
    // P11 TOUCHPOINTER: the aim point's height above the finger in device pixels while ext.touch_pointer aims a
    // tool (0 = at the finger); 160 is the default, to be tuned on the phone (TOUCH_PLACEMENT_IDEAS.md ง5).
    { "touch_pointer_lift", "touch_pointer", 0, 240, 160,
      [](State& s) -> int32_t& { return s.val.touch_pointer_lift; }, kTouchPointerLifts, 6 },
    // The pick snap's radius in device pixels (0 = retail's single ray at the finger); ruled about 50.
    { "touch_pointer_pick_radius", "touch_pointer", 0, 100, 50,
      [](State& s) -> int32_t& { return s.val.touch_pointer_pick_radius; }, kTouchPointerPickRadii, 5 },
    // Tap twice to place: a second tap near the first places the aimed building (ruled a setting, off).
    { "touch_pointer_tap_twice", "touch_pointer", 0, 1, 0,
      [](State& s) -> int32_t& { return s.val.touch_pointer_tap_twice; }, nullptr, 0 },
    // Issue #96 (handins/PHONEMENU.md "Safe area"): the safe-area inset while the row phone_menu is on, in
    // thousandths of a percent of the screen height: 3375 = 3.375 %, the owner's P11c ruling (phone_math.h
    // kSafeMarginFraction) and the default, so phones are unchanged; 0 = no inset (a tablet: panels to the
    // edges); 4500 = P7's 4.5 %. Every reader goes through phone_math.h SafeMargin. Any other number reads as 3375.
    { "phone_menu_safe_margin", "phone_menu", 0, 6000, 3375,
      [](State& s) -> int32_t& { return s.val.phone_menu_safe_margin; }, kPhoneSafeMargins, 5 },
    // NATURAL_GROWTH.md: Better trees' natural tree growth (the per-tree autumn colour, leaf fall, spring
    // regrowth and green variation), on whenever the row is; the Enhanced tab's second Better trees combo.
    { "natural_tree_growth", "better_trees", 0, 1, 1,
      [](State& s) -> int32_t& { return s.val.natural_tree_growth; }, nullptr, 0 },
    // BETTERCLOUDS.md: Better clouds' level, the Enhanced tab's "Better clouds" On / Fancy (Off is the row).
    // Fancy adds the clouds' shadows on the ground.
    { "better_clouds_mode", "better_clouds", 1, 2, 1,
      [](State& s) -> int32_t& { return s.val.better_clouds_mode; }, nullptr, 0 },
    // DOF: the Enhanced tab's "Depth blur" while the row depth_of_field is on: 0 = Off (the default), 1 = On.
    { "depth_blur", "depth_of_field", 0, 1, 0,
      [](State& s) -> int32_t& { return s.val.depth_blur; }, nullptr, 0 },
    // BETTERWATER.md section 7: Better water's level, the Enhanced tab's "Better water" Plain / Fancy (Off is
    // the row). Fancy adds the shore strip on the bank side of the waterline.
    { "better_water_mode", "better_water", 1, 2, 1,
      [](State& s) -> int32_t& { return s.val.better_water_mode; }, nullptr, 0 },
    // NOFOG.md: the Enhanced tab's "Fog distance" slider. 0 = retail's fog (the default since the row ships ON,
    // ruled 2026-11-08), 1..99 push fog near and far out by 100 / (100 - d), 100 = no fog. A settings.cfg with
    // ext.no_fog=1 and no ext.fog_distance key (the row's no-fog meaning before the key) is migrated to 100 at
    // boot (registry_boot.cpp).
    { "fog_distance", "no_fog", 0, 100, 0,
      [](State& s) -> int32_t& { return s.val.fog_distance; }, nullptr, 0 },
    // CAMERAANGLE.md: the Enhanced tab's "Camera angle" slider while the row camera_angle is on (the slider's 50 is
    // the row off). camera_angle.h has the curve: 0 top-down zoomed out, 50 retail, 90 steady, 100 a touch flatter.
    { "camera_tilt", "camera_angle", 0, 100, 50,
      [](State& s) -> int32_t& { return s.val.camera_tilt; }, nullptr, 0 },
    // SHARP_SPRITES.md: Sharp UI's level, the Enhanced tab's "Sharp UI" Text / Text + icons (Off is the row).
    // 0 = the text only (scale-matched fonts, whole-pixel quads); 1 = also the magnified sprite sheets.
    { "sharp_ui_icons", "sharp_ui", 0, 1, 0,
      [](State& s) -> int32_t& { return s.val.sharp_ui_icons; }, nullptr, 0 },
    // Issue #253: the most frames the shadow map / the reflection may go between renders while the row
    // staggered_passes is on (1 = every frame, as retail). 2 is the ruled default; settings.cfg only. Any
    // other number reads as 2 (discrete keys).
    { "staggered_passes_shadow", "staggered_passes", 1, 4, 2,
      [](State& s) -> int32_t& { return s.val.staggered_passes_shadow; }, kStaggerShadowEvery, 4 },
    { "staggered_passes_reflection", "staggered_passes", 1, 8, 2,
      [](State& s) -> int32_t& { return s.val.staggered_passes_reflection; }, kStaggerReflectionEvery, 4 },
};

constexpr int kValueKeyCount = static_cast<int>(sizeof(kValueKeys) / sizeof(kValueKeys[0]));

// The two states. `g_requested` is what settings.cfg / the dialog asked for;
// `g_active` is what code reads (Fix() / Ext()). They differ only for a B / C
// extension while a game holds the lock.
State       g_requested;
State       g_active;
Profile     g_profile = Profile::PerEntry;
bool        g_profileFromCommandLine = false;
const void* g_lockOwner = nullptr;
uint32_t g_lockEpoch = 0;   // LockEpoch(): bumped by Install / LockForGame* / UnlockForGame
bool g_keepRequestNext = false;   // KeepRequestOnNextLock(): the next LockForGame is an in-game B / C change
bool g_lockKeepsRequest = false;  // LockKeepsRequest(): the lock held now is that reload's
bool g_installed = false;         // Installed(): Install has run

}  // namespace

int EntryCount() { return kEntryCount; }

const Entry& EntryAt(int index) { return kEntries[index]; }

const Entry* FindEntry(const char* id) {
    if (id == nullptr) return nullptr;
    for (const Entry& e : kEntries)
        if (std::strcmp(e.id, id) == 0) return &e;
    return nullptr;
}

int ValueKeyCount() { return kValueKeyCount; }

const ValueKey& ValueKeyAt(int index) { return kValueKeys[index]; }

const ValueKey* FindValueKey(const char* id) {
    if (id == nullptr) return nullptr;
    for (const ValueKey& k : kValueKeys)
        if (std::strcmp(k.id, id) == 0) return &k;
    return nullptr;
}

std::string SettingsKey(const ValueKey& k) { return std::string(KeyPrefix(Tier::Extension)) + k.id; }

int32_t ClampValue(const ValueKey& k, int64_t v) {
    if (k.choices != nullptr) {
        for (int32_t i = 0; i < k.choiceCount; ++i)
            if (v == k.choices[i]) return k.choices[i];
        return k.defaultValue;
    }
    if (v < k.minValue) return k.minValue;
    if (v > k.maxValue) return k.maxValue;
    return static_cast<int32_t>(v);
}

int32_t ParseValue(const ValueKey& k, const std::string& text, bool& ok) {
    ok = false;
    size_t i = 0;
    bool negative = false;
    if (i < text.size() && text[i] == '-') { negative = true; ++i; }
    const size_t digits = text.size() - i;
    if (digits == 0 || digits > 9) return k.defaultValue;   // 9 digits never overflow int64 math below
    int64_t v = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return k.defaultValue;
        v = v * 10 + (c - '0');
    }
    ok = true;
    return ClampValue(k, negative ? -v : v);
}

const char* KeyPrefix(Tier tier) { return tier == Tier::Fix ? "fix." : "ext."; }

std::string SettingsKey(const Entry& e) { return std::string(KeyPrefix(e.tier)) + e.id; }

const char* ClassLetter(Class cls) {
    switch (cls) {
    case Class::A: return "A";
    case Class::B: return "B";
    case Class::C: return "C";
    case Class::None: break;
    }
    return "";
}

bool IsLockedClass(Class cls) { return cls == Class::B || cls == Class::C; }

Profile ParseProfile(const char* text) {
    if (text == nullptr) return Profile::PerEntry;
    if (std::strcmp(text, "faithful") == 0) return Profile::Faithful;
    if (std::strcmp(text, "all") == 0) return Profile::All;
    return Profile::PerEntry;
}

const char* ProfileName(Profile p) {
    switch (p) {
    case Profile::Faithful: return "faithful";
    case Profile::All:      return "all";
    case Profile::PerEntry: break;
    }
    return "per-entry";
}

bool ParseToggle(const std::string& value, bool& ok) {
    ok = true;
    if (value == "1") return true;
    if (value == "0") return false;
    ok = false;
    return false;
}

State Resolve(Lookup lookup, void* ctx, const char* commandLineProfile, LoadReport* report) {
    LoadReport local;
    LoadReport& r = report ? *report : local;
    r = LoadReport();

    // The profile: the command line first, then the settings key.
    if (commandLineProfile != nullptr && commandLineProfile[0] != '\0') {
        r.profile = ParseProfile(commandLineProfile);
        r.profileFromCommandLine = true;
    } else {
        std::string value;
        if (lookup != nullptr && lookup(ctx, "profile", value))
            r.profile = ParseProfile(value.c_str());
    }

    State s;
    for (const Entry& e : kEntries) {
        bool on = e.defaultOn;
        if (r.profile == Profile::Faithful) {
            on = false;
        } else if (r.profile == Profile::All) {
            on = true;
        } else {
            std::string value;
            if (lookup != nullptr && lookup(ctx, SettingsKey(e).c_str(), value)) {
                bool ok = false;
                const bool parsed = ParseToggle(value, ok);
                if (ok) on = parsed;
                else ++r.malformed;
            }
        }
        e.slot(s) = on;
    }
    for (const ValueKey& k : kValueKeys) {
        int32_t v = k.defaultValue;
        if (r.profile == Profile::PerEntry) {
            std::string value;
            if (lookup != nullptr && lookup(ctx, SettingsKey(k).c_str(), value)) {
                bool ok = false;
                v = ParseValue(k, value, ok);
                if (!ok) ++r.malformedValues;
                else if (value != std::to_string(v)) ++r.clampedValues;
            }
        }
        k.slot(s) = v;
    }
    return s;
}

const Fixes&      Fix() { return g_active.fix; }

namespace {
LiveFixes g_live;

// After every write of g_active's fix rows (main thread).
void PublishLiveFixes() {
    g_live.save_failure_notice.store(g_active.fix.save_failure_notice, std::memory_order_relaxed);
    g_live.boot_device_lost.store(g_active.fix.boot_device_lost, std::memory_order_relaxed);
    g_live.decal_fog_order.store(g_active.fix.decal_fog_order, std::memory_order_relaxed);
}
}  // namespace

const LiveFixes& LiveFix() { return g_live; }
const Extensions& Ext() { return g_active.ext; }
const Values&     Val() { return g_active.val; }

int32_t Value(const ValueKey& k) { return k.slot(g_active); }

int32_t SetValue(const ValueKey& k, int64_t v) {
    const int32_t c = ClampValue(k, v);
    k.slot(g_requested) = c;
    k.slot(g_active) = c;
    return c;
}

bool Requested(const Entry& e) { return e.slot(g_requested); }
bool Active(const Entry& e)    { return e.slot(g_active); }

void Install(const State& requested, Profile profile, bool profileFromCommandLine) {
    g_requested = requested;
    g_active = requested;
    PublishLiveFixes();
    g_profile = profile;
    g_profileFromCommandLine = profileFromCommandLine;
    g_lockOwner = nullptr;
    ++g_lockEpoch;
    g_keepRequestNext = false;
    g_lockKeepsRequest = false;
    g_installed = true;
}

bool Installed() { return g_installed; }

Profile CurrentProfile() { return g_profile; }

bool GateProfileForced() { return g_profileFromCommandLine && g_profile == Profile::Faithful; }

bool SetRequested(const Entry& e, bool on) {
    e.slot(g_requested) = on;
    if (EntryLocked(e)) return false;   // waits for the next LockForGame
    const bool changed = e.slot(g_active) != on;
    e.slot(g_active) = on;
    PublishLiveFixes();
    return changed;
}

void LockForGame(const void* owner) {
    // The game starts with what the player asked for; from here on a B / C
    // toggle only changes the request.
    for (const Entry& e : kEntries)
        if (e.tier == Tier::Extension && IsLockedClass(e.cls))
            e.slot(g_active) = e.slot(g_requested);
    g_lockOwner = owner;
    ++g_lockEpoch;
    g_lockKeepsRequest = g_keepRequestNext;   // one shot: only the reload that asked for it
    g_keepRequestNext = false;
}

bool LockForGameWithSet(const void* owner, const std::vector<std::string>& ids) {
    for (const std::string& id : ids) {
        const Entry* e = FindEntry(id.c_str());
        if (e == nullptr || e->tier != Tier::Extension || !IsLockedClass(e->cls)) return false;
    }
    // The save decides: exactly its B / C set, the request notwithstanding.
    for (const Entry& e : kEntries) {
        if (e.tier != Tier::Extension || !IsLockedClass(e.cls)) continue;
        bool on = false;
        for (const std::string& id : ids)
            if (id == e.id) { on = true; break; }
        e.slot(g_active) = on;
    }
    g_lockOwner = owner;
    ++g_lockEpoch;
    g_lockKeepsRequest = false;
    return true;
}

void KeepRequestOnNextLock() { g_keepRequestNext = true; }

bool LockKeepsRequest() { return Locked() && g_lockKeepsRequest; }

void UnlockForGame(const void* owner) {
    if (owner == nullptr || owner != g_lockOwner) return;
    g_lockOwner = nullptr;
    ++g_lockEpoch;
    g_lockKeepsRequest = false;
    // A class C extension's per-town state goes with the town that held the lock: only the owner's unlock
    // gets here, so a reload whose new loop already took the lock keeps the state it decoded.
    SaveTail::ResetAllState();
    // Back at the menu nothing holds the lock: the active state is the request.
    for (const Entry& e : kEntries)
        e.slot(g_active) = e.slot(g_requested);
    PublishLiveFixes();
}

bool Locked() { return g_lockOwner != nullptr; }

uint32_t LockEpoch() { return g_lockEpoch; }

bool EntryLocked(const Entry& e) {
    return Locked() && e.tier == Tier::Extension && IsLockedClass(e.cls);
}

int PseudoModsForSave(PseudoMod* out, int capacity) {
    int n = 0;
    for (const Entry& e : kEntries) {
        if (e.tier != Tier::Extension || !IsLockedClass(e.cls)) continue;
        if (!e.slot(g_active)) continue;
        if (out != nullptr && n < capacity) {
            out[n].path = std::string("ext:") + e.id;
            out[n].name = e.displayName;
            out[n].versionMajor = 0;
            out[n].gameVersion = kPseudoModField14;
            out[n].formatVersion = e.formatVersion;
        }
        ++n;
    }
    return n;
}

int ProvideExtraModsForSave(PseudoModRecord* out, int capacity) {
    // The registry owns the strings until the next call.
    static PseudoMod held[kEntryCount > 0 ? kEntryCount : 1];
    const int n = PseudoModsForSave(held, kEntryCount);
    for (int i = 0; out != nullptr && i < n && i < capacity; ++i) {
        out[i].path = held[i].path.c_str();
        out[i].name = held[i].name.c_str();
        out[i].versionMajor = held[i].versionMajor;
        out[i].gameVersion = held[i].gameVersion;
        out[i].formatVersion = held[i].formatVersion;
    }
    return n;
}

ExtraModsProvider SaveModsProvider() {
    if (g_profile == Profile::Faithful) return nullptr;
    for (const Entry& e : kEntries)
        if (e.tier == Tier::Extension && IsLockedClass(e.cls))
            return &ProvideExtraModsForSave;
    return nullptr;
}

bool IsPseudoModPath(const char* path) {
    return path != nullptr &&
           std::strncmp(path, kPseudoModPrefix, std::strlen(kPseudoModPrefix)) == 0;
}

int PseudoModLoadStatus(const char* path, int32_t savedFormatVersion) {
    if (!IsPseudoModPath(path)) return 1;
    const Entry* e = FindEntry(path + std::strlen(kPseudoModPrefix));
    if (e == nullptr || e->tier != Tier::Extension || !IsLockedClass(e->cls)) return 1;
    if (savedFormatVersion > e->formatVersion) return 1;
    if (!Requested(*e)) return 1;
    return 0;
}

// The Extensions dialog's Experimental tab (registry.h Experimental).
bool Experimental(const Entry& e) {
    static const char* const kExperimental[] = { "touch_mode", "touch_pointer", "phone_menu", "staggered_passes",
                                                         "leaf_lod" };
    for (const char* id : kExperimental)
        if (std::strcmp(e.id, id) == 0) return true;
    return false;
}

bool ValidId(const char* id) {
    if (id == nullptr || id[0] == '\0') return false;
    for (const char* p = id; *p; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

}  // namespace Ext
