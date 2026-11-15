// ============================================================================
// src/ext/rock_seat/rock_seat.h -- fix.rock_seat: rocks and ore drawn seated on the slope
// ============================================================================
// NOT RETAIL. docs/extensions/handins/ROCKSEAT.md. The math is rock_seat_math.h.
//
// RENDER ONLY. ModelComponent::transform (+0x148) -- what retail serializes,
// and the only position of a rock anything else reads -- is never changed. The
// seated transform goes only into the model's MeshInstance (the Geometry
// transform the scene draws, culls and shadows with), which no Serialize writes
// (MeshInstance::Serialize streams the animation state and the materials) and
// no sim code reads. So a save, the map grid, the pathing, the harvesting and
// the minimap are retail's with the fix on.
//
// WHICH MODELS. An entity whose NaturalResourceDescription is not animated
// (_animated, the falling-tree animation) and is mined with the pick axe (_toolType ==
// kToolPickAxe): NaturalResourceRock and NaturalResourceOre in the shipped
// data, and any mod template made the same way. Trees, bushes, herbs and crops
// are left retail.
//
// WHEN. Every site that puts ModelComponent::transform into the mesh for a
// placed natural resource (PlaceAtCell, the start town's copy, ModelComponent::
// OnAdded), and once after a load (SeatAll: on a load the entities are read
// before the terrain, so OnAdded falls back to retail's transform there). Never
// per frame.
// ============================================================================
#pragma once

class ModelComponent;
class EntityList;

namespace Ext {
namespace RockSeat {

// ToolType PickAxe (Citizen.rsc's _tools order: Axe 0, PickAxe 1, Scythe 2, ...).
constexpr unsigned kToolPickAxe = 1;

// The fix's lever: the model's mesh gets the seated transform when the model is
// a rock and the terrain is loaded, else retail's SetTransform(model->transform).
void SetMeshTransform(ModelComponent* model);

// After a load: every rock's mesh re-seated on the loaded terrain.
void SeatAll(EntityList* list);

}  // namespace RockSeat
}  // namespace Ext
