#pragma once
// ============================================================================
// src/ext/map_overlays/map_overlays.h -- ext.map_overlays (class C, default OFF): the game-side seams
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md. SimCity-style data views in the map window: one tab
// per view beside retail's map, a resource / kind picker, a coarse-to-fine scale slider, a legend, and
// "Show overlay on terrain". The history views (production, consumption, traffic) are saved (class C).
//
// SELECTION (DESIGN.md hook rule 4). Every game seam tests `g_on` once, above any loop; with the row off it
// pays one load and a not-taken branch. g_on is set by BeginFrame from the game's lock.
// Ownership of the files in this directory: MAPOVERLAYS.md §6.
// ============================================================================
#include <cstdint>
#include <vector>
#include "ext/map_overlays/overlay_core.h"

namespace Ext { namespace SaveTail { struct StateCodec; } }
namespace Video { class Context; }
namespace Graphics { class Camera; }
class RawMaterialDescription;
class MapUI;
class ComponentDescription;
class ComponentEntity;

namespace Ext {
namespace MapOverlays {

// ---- state (map_overlays_town.cpp) -------------------------------------------------------------------------
extern bool g_on;                    // the row is active for this game

History& Town();                     // the town's history (saved)
const SaveTail::StateCodec& Codec(); // save_state.cpp

// GameUpdateLoop::Update, once per frame: reads the lock into g_on, the map size and the calendar month
// (History::AdvanceTo), and resolves the material pointers to table indices (cached per pointer).
void BeginFrame();

// ---- the flow seams (called from src/game only when g_on) ---------------------------------------------------
// `worldX` / `worldZ` are world coordinates of the site (the building's or the home's position); the
// tile is computed here. `count` is in items, as the inventory counts them.
void OnProduced(const RawMaterialDescription* material, float worldX, float worldZ, int count);
void OnConsumed(const RawMaterialDescription* material, float worldX, float worldZ, int count);
// The same for the seams that hold an entity and an item template (what StorageComponent stores): the site is
// the entity's MapComponent footprint centre, the material item->FindDescription<RawMaterialDescription>();
// nothing is counted when either is missing.
void OnProducedAt(const ComponentDescription* item, const ComponentEntity* site, int count);
void OnConsumedAt(const ComponentDescription* item, const ComponentEntity* site, int count);
// A citizen entered a new tile (the movement seam; MAPOVERLAYS.md §4 for the cost rule).
void OnStep(int tileX, int tileZ);

// ---- the view (overlay_view.cpp) ---------------------------------------------------------------------------
const Selection& CurrentSelection();
void SetSelection(const Selection& s);   // the UI; marks the image stale

// The colourised, blurred overlay for the current selection: size*size RGBA, 0xAABBGGRR. That is NOT the
// minimap images' order (B8G8R8A8, a uint32 reads 0xAARRGGBB) nor the terrain vertex colour's (ARGB): the
// renderers convert. Alpha 0 where the value is 0. Rebuilt lazily: at most every kRebuildFrames frames
// while a view is showing, at once after SetSelection. `version` changes whenever the pixels change, so the
// renderers upload only then. Empty (size 0) for View::Map or with g_on false.
constexpr int kRebuildFrames = 60;
const std::vector<uint32_t>& Image(int& size, uint32_t& version);
// The legend for the current image: the value at full colour, in the view's unit (items per period,
// steps, percent, count).
float LegendMax();

// Fills the instant views' grids (overlay_views.cpp; walks the entity lists).
bool BuildInstantTiles(const Selection& s, int size, std::vector<float>& out);

// ---- the renderers (overlay_render.cpp) --------------------------------------------------------------------
// The minimap: the overlay drawn over MapUI's map image while an overlay tab is active. Called by the map
// window's controller (map_window.cpp) every UI update while the window is open; a View::Map selection
// (Image() size 0) puts retail's sprite back on imageOverlay.
void MapWindowUpdate(MapUI& map);
// Before the MapUI is destroyed: retail's sprite back on imageOverlay before ~MapUI deletes the sheets.
void MapWindowClosed(MapUI& map);
// Main thread, every frame, at the end of BeginFrame: snapshots Image() for the terrain draw, which may run
// on the render thread (ext.render_pipe) and never calls Image() itself.
void RenderFrame();
// The terrain: from Graphics::Renderer's ground draw (src/ext/ground_draw.h), between the opaque and the
// blended lists of the main scene, with that scene's camera, while the row is on.
void DrawOnTerrain(Video::Context& context, const Graphics::Camera& camera);

// A new game or a load: selection back to View::Map, onTerrain off; the history comes from the save (or empty).
void OnGameStart();

}  // namespace MapOverlays
}  // namespace Ext
