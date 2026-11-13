#pragma once
// ============================================================================
// src/ext/radius_all/radius_all.h -- ext.radius_all (class A, live, default ON)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/RADIUSALL.md). Retail draws a building's work circle (the terrain decal
// RadiusDescription::AddHighlight 0x140055430 makes) for one building at a time: the one whose panel is open
// (RadiusUI::OnOpened) and the one being placed (PlacementTool::Update). With the row on, a Shift+click on a
// building that has a circle (a long press of a finger with ext.touch_pointer) shows the circle of every
// building of its kind, and keeps them while the player selects or places other things; the same on a
// building of a kind already shown hides them. Several kinds can be shown at once.
//
// The circles are the game's own decals, made by the same AddHighlight call with the same material and
// radius, round the same centre cell. Nothing is kept: the kinds shown belong to the running game and are
// forgotten with it, and the circles are taken out before a save is written (WorldGeometry serialises its
// decals), then put back.
//
// Threads: all of it runs on the main thread (the tool manager's update, the input poll, the game update,
// the save's head).
// ============================================================================

class ComponentEntity;
namespace Input {
class KeyMap;
}

namespace Ext {
namespace RadiusAll {

// EditTool::OnKey (EXT SEAM), on a click that found `entity`: true when the click was a Shift+click (retail's
// Shift, GameKey::PathMethod) or the click of a long press, and the entity has a work circle: its kind's
// circles are switched and the tool does not select the entity. False otherwise (retail's selection).
bool Click(const ComponentEntity* entity, const Input::KeyMap* keyMap);

// ext.touch_pointer, a finger held still on the world with no tool: the click it sends next counts as a
// Shift+click for Click (for a moment: touch_pointer_math.h's latch shows it a few polls later).
void ArmLongPress();

// GameUpdateLoop::Update, after SaveLoad::Update (EXT SEAM), every frame: keeps one circle per building of
// the kinds shown (a building built or removed since), and drops them all when the row goes off.
void Frame();

// GameUpdateLoop::SerializeAllHead's save arm (EXT SEAM): takes this row's circles out of the terrain's decal
// list so the save holds none of them; the next Frame puts them back.
void BeforeSave();

}  // namespace RadiusAll
}  // namespace Ext
