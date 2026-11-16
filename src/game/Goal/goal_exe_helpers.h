#pragma once
// Declarations for Goal-module exe-helper reimplementations.
// See src/game/Goal/goal_exe_helpers.cpp.

#include <cstddef>
#include <cstdint>
#include "Entity/CitizenBehavior.h"   // the entity-bucket trees' owner
#include "system/String.h"
#include "collection/Collection.h"

namespace Object { class External; }
class ComponentEntity;
class ComponentDescription;
class Profession;

// The by-name External reference retail's goals and tutorial steps hold:
// Object/NamedExternalRef.h (B4 S4 RESOLVE: the one record every holder shares).
#include "Object/NamedExternalRef.h"

// The element row of Array<NamedExternalRef> (goal_exe_helpers.cpp): retail's
// resize 0x14001b340 and destroy 0x14001b030 (the same two bodies
// MapGeneratorRows::kSpec_ExternalRef stands for).
namespace GoalRows {
extern const rtcoll::ArraySpec kSpec_GoalRefEntry;   // exe 0x14001b340 / 0x14001b030
}  // namespace GoalRows

// Collection::Array<NamedExternalRef> as the goals hold it. B4 Y GAMEA (H5): its
// ctor is the three zero stores (BuildingGoal's create 0x140025dc4 / 0x140025dc8),
// its dtor is ClearCallback (each element's ~String, MemFree, zero stores): the
// 0x14001b030 the owner's dtor calls (BuildingGoal 0x140025eab).
using NamedExternalRefArray = Collection::Array<NamedExternalRef, Collection::DefaultAllocator,
                                                Collection::ExplicitSpec<&GoalRows::kSpec_GoalRefEntry>>;
static_assert(sizeof(NamedExternalRefArray) == 0x10, "Array header is 0x10");

// The CitizenBehavior entity-bucket trees (+0x100 keyed by Profession, +0x160 keyed by
// ComponentDescription) are CitizenBehavior.h's ProfessionBucket / TemplateBucket
// (CitizenBucketNode<Key>, 0x38 bytes); the trees are workplacesByProfession / entitiesByTemplate.

// ENDGAME B.3.8 block GOAL: `Goal_FillInheritedVtableSlots` (the by-name fill
// of slots 4 / 7 / 9 into the hand-built Goal tables) is gone with those
// tables — the Goal leaves are real classes, and Compile / GetInstanceType are
// inherited from Achievement::Goal, Destruct is each leaf's own member.
