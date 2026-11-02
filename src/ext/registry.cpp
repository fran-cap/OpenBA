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
// class B / C default OFF: DESIGN.md section 2, ruled 2026-10-18), in the
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
      "Dialogs no longer flash to a corner when the UI scale changes, and pop-up notices follow the "
      "UI scale.",
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
    // SAVEFAILURENOTICE (docs/retail_bugs.md section 29): a save that does not reach the disk (read-only
    // folder, Controlled Folder Access, antivirus, full disk) opens a message box naming the save folder,
    // where retail's UpdateSave (0x1800c2a80) never reads the save job's status; and the job writes a
    // temporary file and moves it over the save, so a failed write keeps the previous save. No sim change.
    { "save_failure_notice", Tier::Fix, Class::None, true,
      "Tell when a save fails",
      "A save that cannot be written (a read-only or protected save folder, an antivirus block, a full "
      "disk) shows a message instead of failing silently, and the previous save of that name is kept.",
      0, [](State& s) -> bool& { return s.fix.save_failure_notice; } },
    // BOATREACHNOTICE (docs/retail_bugs.md section 30): when a merchant boat's two water legs to a trading
    // post's dock come back empty (BoatComponent::SetDestination 0x1400456e0 drops them without a word),
    // the event log says so, at the post's tile. Where the boat sails is unchanged. The line is an ordinary
    // event-log entry (text, a save keeps it like any other).
    { "boat_reach_notice", Tier::Fix, Class::None, true,
      "Tell when boats cannot reach a dock",
      "When a merchant's boat finds no water route to a trading post's dock, the event log says so, so a "
      "post that never trades is no longer a mystery.",
      0, [](State& s) -> bool& { return s.fix.boat_reach_notice; } },

    // Extensions. Each one adds a field to Ext::Extensions and a row here:
    // Tier::Extension, its Class, default true for class A / false for B or C, and for class B or C its
    // formatVersion (>= 1). The id is [a-z0-9_]+ (the unit tests check every row).
    // Class A but OFF by default (the user, 2026-10-18): it changes how the game is played,
    // so it stays the player's decision.
    { "eye_camera", Tier::Extension, Class::A, false,
      "Eye-level camera",
      "Follow a citizen at eye level, looking where they look.",
      0, [](State& s) -> bool& { return s.ext.eye_camera; } },

    // BETTERCAM (docs/extensions/handins/BETTERCAM.md): the two camera fixes of Banished Game Tweaks, in source.
    // Class B (PR review): with the row on, the zoom target and the camera distance, which retail serializes,
    // can hold values outside retail's range, so a save records the extension.
    { "better_camera", Tier::Extension, Class::B, false,
      "Better camera",
      "Zoom in much closer and out much farther, and keep the camera's tilt when zoomed out "
      "instead of flattening to top-down.",
      1, [](State& s) -> bool& { return s.ext.better_camera; } },
    // HIGHLIMITS (docs/extensions/handins/HIGHLIMITS.md): the limit spinners of the Town Hall, the Limits
    // window and the production buildings take values up to 999,999,999 and 9 typed digits instead of
    // 999,999 and 6. Class B: no serialized state, but a limit above 999,999 is a value the faithful
    // spinner cannot show, so it is locked for the game and recorded in the save.
    { "high_limits", Tier::Extension, Class::B, false,
      "Higher limit values",
      "Lets the limit boxes in the Town Hall, the Limits window and the production buildings take values "
      "up to 999,999,999 (retail stops at 999,999). Play diverges.",
      1, [](State& s) -> bool& { return s.ext.high_limits; } },

    { "sharp_ui", Tier::Extension, Class::A, true,
      "Sharp UI",
      "Crisp text and sprites at any UI scale. Layout is unchanged.",
      0, [](State& s) -> bool& { return s.ext.sharp_ui; } },

    { "fast_paths", Tier::Extension, Class::B, false,
      "Faster pathfinding",
      "Same-length routes for far less work. Ties can differ, so play diverges.",
      1, [](State& s) -> bool& { return s.ext.fast_paths; } },

    { "path_workers", Tier::Extension, Class::B, false,
      "Parallel pathfinding",
      "Finds paths on several threads. Same paths, but play diverges.",
      1, [](State& s) -> bool& { return s.ext.path_workers; } },

    { "render_culling", Tier::Extension, Class::A, true,
      "Skip unseen work",
      "Off-screen citizens, icons and effects pause until you look.",
      0, [](State& s) -> bool& { return s.ext.render_culling; } },

    { "anim_batch", Tier::Extension, Class::B, false,
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
    { "walk_batch_grid", Tier::Extension, Class::B, false,
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
    // FAA-SHADOW (docs/extensions/FOLIAGE_AA.md section 7): the sun map at 4096 when the retail
    // Shadow Resolution combo is at High; Medium / Low keep their retail sizes.
    { "max_shadow_res", Tier::Extension, Class::A, true,
      "Max shadow resolution",
      "Shadow Resolution High builds a 4096 shadow map instead of 2048.",
      0, [](State& s) -> bool& { return s.ext.max_shadow_res; } },
    // RESTRICTEDFOODS (docs/extensions/handins/RESTRICTEDFOODS.md): the foods checked on the Foods page stay
    // Edible for storage, markets and recipes but are not eaten by citizens. Class C: the checked list is the
    // town's own state, recorded in the save's tail (src/ext/restricted_foods/save_state.cpp).
    { "restricted_foods", Tier::Extension, Class::C, false,
      "Restricted foods",
      "Lets you stop citizens eating foods you choose, from the Foods page in a game. Play diverges; the "
      "choice is recorded in the save.",
      1, [](State& s) -> bool& { return s.ext.restricted_foods; } },
    // ITEMPRIO (docs/extensions/handins/ITEMPRIO.md): the town ranks the tools, fuels, clothing and
    // health items its citizens fetch from a storage; a citizen takes the best-ranked item. Class C:
    // the ranked lists are the town's own state, recorded in the save's tail
    // (src/ext/item_priority/save_state.cpp).
    { "item_priority", Tier::Extension, Class::C, false,
      "Item priority",
      "Lets you rank the tools, fuels, clothing and health items citizens take first, from the "
      "Priority page in a game. Play diverges; the choice is recorded in the save.",
      2, [](State& s) -> bool& { return s.ext.item_priority; } },

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
    // HITCH_PFBURST (docs/extensions/handins/HITCH_PFBURST.md): a per-frame budget of the cold
    // citizens' warm-house scans (one region path search per residence), so a cold spell at high
    // speed spreads its scans over a few frames instead of one 10-20 ms frame.
    { "spread_warmth", Tier::Extension, Class::B, false,
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
    { "drain_split", Tier::Extension, Class::B, false,
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
    // NOFOG (docs/extensions/handins/NOFOG.md): the fog is taken off the finished lighting each frame, so
    // the far map stays clear when the camera is zoomed out. Class A: a picture-only change, no sim effect,
    // nothing saved, live. OFF by default: how the map looks is the player's choice (like eye_camera).
    { "no_fog", Tier::Extension, Class::A, false,
      "No fog",
      "Removes the fog that hazes the far map when the camera is zoomed far out.",
      0, [](State& s) -> bool& { return s.ext.no_fog; } },
    // SHOWGRID (docs/extensions/handins/SHOWGRID.md): a grid of tile outlines over the map, for planning with
    // the game paused. Class A: a picture-only change, no sim effect, nothing saved, live. OFF by default:
    // how the map looks is the player's choice (like no_fog).
    { "show_grid", Tier::Extension, Class::A, false,
      "Show grid",
      "Draws a grid over the map so you can see where the tiles are while planning where to build.",
      0, [](State& s) -> bool& { return s.ext.show_grid; } },
    // TOOLNAMES (docs/extensions/handins/TOOLNAMES.md): the citizen panel names the tool the citizen carries
    // (the item's own name) instead of retail's "default tool / other tool" words. Class C: which tool each
    // citizen carries is the town's own state, recorded in the save's tail so a tool keeps its name across a
    // load (src/ext/tool_names/tool_names.cpp). No sim effect. OFF by default like the other class C rows.
    { "tool_names", Tier::Extension, Class::C, false,
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
constexpr int32_t kTouchPointerLifts[] = { 0, 80, 120, 160, 200, 240 };
constexpr int32_t kTouchPointerPickRadii[] = { 0, 25, 50, 75, 100 };

const ValueKey kValueKeys[] = {
    { "ao_quality", "ambient_occlusion", 1, 3, 2,
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
    // NATURAL_GROWTH.md: Better trees' natural tree growth (the per-tree autumn colour, leaf fall, spring
    // regrowth and green variation), on whenever the row is; the Enhanced tab's second Better trees combo.
    { "natural_tree_growth", "better_trees", 0, 1, 1,
      [](State& s) -> int32_t& { return s.val.natural_tree_growth; }, nullptr, 0 },
    // BETTERWATER.md section 7: Better water's level, the Enhanced tab's "Better water" Plain / Fancy (Off is
    // the row). Fancy adds the shore strip on the bank side of the waterline.
    { "better_water_mode", "better_water", 1, 2, 1,
      [](State& s) -> int32_t& { return s.val.better_water_mode; }, nullptr, 0 },
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
    g_profile = profile;
    g_profileFromCommandLine = profileFromCommandLine;
    g_lockOwner = nullptr;
    ++g_lockEpoch;
    g_keepRequestNext = false;
    g_lockKeepsRequest = false;
}

Profile CurrentProfile() { return g_profile; }

bool GateProfileForced() { return g_profileFromCommandLine && g_profile == Profile::Faithful; }

bool SetRequested(const Entry& e, bool on) {
    e.slot(g_requested) = on;
    if (EntryLocked(e)) return false;   // waits for the next LockForGame
    const bool changed = e.slot(g_active) != on;
    e.slot(g_active) = on;
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
            out[n].field10 = 0;
            out[n].field14 = kPseudoModField14;
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
        out[i].field10 = held[i].field10;
        out[i].field14 = held[i].field14;
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

bool ValidId(const char* id) {
    if (id == nullptr || id[0] == '\0') return false;
    for (const char* p = id; *p; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

}  // namespace Ext
