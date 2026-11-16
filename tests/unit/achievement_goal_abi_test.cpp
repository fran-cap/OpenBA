// ============================================================================
// tests/unit/achievement_goal_abi_test.cpp — ENDGAME B.3.7 block B3
// The ABI claims `Achievement::{Goal, GoalInstance, GoalList, Achievement,
// AchievementInstance, AchievementManager}` make (B37_RULES.md B7-1, B7-4,
// B7-5, B7-8), measured on the SHIPPED HEADERS.
// ============================================================================
//
// PART A — LAYOUT is the headers' static_asserts (Goal.h, AchievementManager.h);
//   the class-shape rules are here.
//
// PART B — SLOT ORDER OF THE REAL CLASSES, with no link dependency. A pointer
//   to a VIRTUAL member function is, in the Itanium C++ ABI GCC uses on this
//   target, `{ 1 + vtable byte offset, this-adjustment }`. So `&Goal::Serialize`
//   READS the slot the header gives `Serialize` without instantiating, calling
//   or linking anything — this is the header under test, not a copy of it.
//   (graphics_geometry_abi_test.cpp used a mirrored probe for this; the PMF
//   reading is stronger because it cannot drift from the header.)
//
// PART C — RULE B7-4, the one retslot VIRTUAL: `Goal::GetInstanceType`
//   (slot 9, 0x18009eeb0) returns `Object::Type` BY VALUE. MSVC passes `this`
//   in RCX and the caller's storage in RDX; GCC's ms_abi puts the hidden slot
//   FIRST. PART C pins the declared shape — the out-parameter spelled in
//   retail order, which is what GoalList::Initialize's `CALL [RAX+0x48]` and
//   the exe's TimeGoal / FinishTutorialGoal slot-9 bodies call.
//
// PART D — RULE B7-5, hidden-pointer ARGUMENTS: `GoalList::AreGoalsMetExcept`
//   (InternalPtr by value, `MOV RDX,[RDX]`) and `GoalList::Initialize` (Array
//   by value, `MOV EDX,[RDX+0x8]`) receive a POINTER in RDX. Their declared
//   types are pinned.
//
// WHAT IS *NOT* PROVEN HERE: the bodies, which are verified by the real run
// (AchievementManager::Update's bounds are achievement_update_bounds_test.cpp).
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include "achievement/Goal.h"
#include "achievement/AchievementManager.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace {

using Achievement::Goal;
using Achievement::GoalInstance;
using Achievement::GoalList;
using AchClass = Achievement::Achievement;
using Achievement::AchievementInstance;
using Achievement::AchievementManager;

// ===========================================================================
// PART A — LAYOUT: every sizeof / alignof / offsetof is Goal.h's and
// AchievementManager.h's own static_asserts; only the class-shape rules are here.
// ===========================================================================

UNIT_TEST(achievement_class_shape_rules) {
    // B7-13 / ENDGAME B.1 decision 2: polymorphic, abstract; B3X W1: public virtual dtor (retail slot 0 = D1 + D0).
    CHECK(std::is_polymorphic<Goal>::value);
    CHECK(std::is_abstract<Goal>::value);
    CHECK(!std::is_abstract<GoalInstance>::value);        // concrete since B.3.9 ENG
    CHECK(!std::is_abstract<AchClass>::value);            // concrete since B.3.9 ENG
    CHECK(!std::is_abstract<AchievementInstance>::value);   // concrete since B.3.8 RB (exe table copy 0x14015e350)
    CHECK(std::has_virtual_destructor<Goal>::value);
    CHECK(std::has_virtual_destructor<GoalInstance>::value);
    CHECK(std::has_virtual_destructor<AchClass>::value);
    CHECK(std::has_virtual_destructor<AchievementInstance>::value);
    // B7-8: GoalList is a value type with a DEEP copy and an owning dtor.
    CHECK(!std::is_polymorphic<GoalList>::value);
    CHECK(!std::is_trivially_copyable<GoalList>::value);
    CHECK(!std::is_trivially_destructible<GoalList>::value);
    CHECK(!std::is_polymorphic<AchievementManager>::value);
    // The bases retail derives from, at offset 0 (no multiple inheritance).
    CHECK((std::is_base_of<Object::External, Goal>::value));
    CHECK((std::is_base_of<Object::Internal, GoalInstance>::value));
    CHECK((std::is_base_of<Object::External, AchClass>::value));
    CHECK((std::is_base_of<Object::Internal, AchievementInstance>::value));
    // Object::Type is a value written into a raw hidden slot.
    CHECK(std::is_trivially_copyable<Object::Type>::value);
}

// ===========================================================================
// PART B — SLOT ORDER OF THE REAL CLASSES (B37_SLOTS.md §1)
// ===========================================================================

UNIT_TEST(achievement_goal_slots_are_retail_order) {
    // ??_7Goal@Achievement@@6B@ 0x1801546b0, ten slots.
    CHECK_DTOR_SLOT(Goal);
    CHECK_SLOT(Goal, TypeName,        0x08/8);
    CHECK_SLOT(Goal, Type,            0x10/8);
    CHECK_SLOT(Goal, GetFileVersion,  0x18/8);
    CHECK_SLOT(Goal, Compile,         0x20/8);   // External's, inherited
    CHECK_SLOT(Goal, Serialize,       0x28/8);
    CHECK_SLOT(Goal, Reconstruct,     0x30/8);   // pure, inherited
    CHECK_SLOT(Goal, Destruct,        0x38/8);   // pure, inherited
    CHECK_SLOT(Goal, CheckGoalState,  0x40/8);   // pure, Goal's own
    CHECK_SLOT(Goal, GetInstanceType, 0x48/8);   // Goal.h's old kGetInstanceType
}

UNIT_TEST(achievement_goalinstance_slots_are_retail_order) {
    // ??_7GoalInstance@Achievement@@6B@ 0x180154678, seven slots (B7-1.8).
    CHECK_DTOR_SLOT(GoalInstance);
    CHECK_SLOT(GoalInstance, TypeName,       0x08/8);
    CHECK_SLOT(GoalInstance, Type,           0x10/8);
    CHECK_SLOT(GoalInstance, Persist,        0x18/8);
    CHECK_SLOT(GoalInstance, Startup,        0x20/8);
    CHECK_SLOT(GoalInstance, Update,         0x28/8);
    CHECK_SLOT(GoalInstance, CheckGoalState, 0x30/8);
}

UNIT_TEST(achievement_achievement_slots_are_retail_order) {
    // ??_7Achievement@0@6B@ 0x180154740, nine slots.
    CHECK_DTOR_SLOT(AchClass);
    CHECK_SLOT(AchClass, TypeName,       0x08/8);
    CHECK_SLOT(AchClass, Type,           0x10/8);
    CHECK_SLOT(AchClass, GetFileVersion, 0x18/8);
    CHECK_SLOT(AchClass, Compile,        0x20/8);
    CHECK_SLOT(AchClass, Serialize,      0x28/8);
    CHECK_SLOT(AchClass, Reconstruct,    0x30/8);
    CHECK_SLOT(AchClass, Destruct,       0x38/8);
    CHECK_SLOT(AchClass, Construct,      0x40/8);
    // ??_7AchievementInstance@Achievement@@6B@ 0x180154700, four slots.
    CHECK_DTOR_SLOT(AchievementInstance);
    CHECK_SLOT(AchievementInstance, TypeName, 0x08/8);
    CHECK_SLOT(AchievementInstance, Type,     0x10/8);
    CHECK_SLOT(AchievementInstance, Persist,  0x18/8);
}

// ===========================================================================
// PART C — RULE B7-4: slot 9 carries a HIDDEN RETURN SLOT, in retail order
// ===========================================================================

// The declared type IS the retail-order shape — `(this, ret)`, result `ret`.
static_assert(std::is_same<decltype(&Goal::GetInstanceType),
                           Object::Type* (Goal::*)(Object::Type*) const>::value,
              "B7-4: GetInstanceType spells the hidden return slot explicitly");

// A real-type runtime fact the retired slot-9 probe also exercised: Object::Type
// is the raw id (??BType -- operator unsigned int), written into a hidden slot.
UNIT_TEST(achievement_object_type_is_its_raw_id) {
    Object::Type slot; slot._type = 0x1234abcdu;
    CHECK_EQ(static_cast<uint32_t>(slot), 0x1234abcdu);
}

// ===========================================================================
// PART D — RULE B7-5: hidden-pointer ARGUMENTS, and the bare-register ones
// ===========================================================================

static_assert(std::is_same<decltype(&GoalList::AreGoalsMetExcept),
                           bool (GoalList::*)(const Object::InternalPtr<GoalInstance>*) const>::value,
              "B7-5: InternalPtr by value arrives by HIDDEN POINTER (MOV RDX,[RDX])");
static_assert(std::is_same<decltype(&GoalList::Initialize),
                           void (GoalList::*)(Achievement::GoalArray*)>::value,
              "B7-5: Array by value arrives by HIDDEN POINTER (MOV EDX,[RDX+0x8])");
static_assert(std::is_same<decltype(&AchievementInstance::SetAchievement),
                           void (AchievementInstance::*)(Object::ExternalPtr<AchClass>)>::value,
              "B7-5: ExternalPtr by value is a BARE register (MOV [RCX+0x20],RDX)");
static_assert(std::is_same<decltype(&AchievementManager::SetAchievements),
                           void (AchievementManager::*)(Object::ExternalPtr<Object::ExternalList>)>::value,
              "B7-5: ExternalPtr by value is a BARE register (MOV [RCX],RDX)");
static_assert(std::is_trivially_copyable<Object::ExternalPtr<AchClass>>::value &&
              sizeof(Object::ExternalPtr<AchClass>) == 8,
              "an 8-byte trivially copyable class travels in one register");

// The real-type facts the hidden-pointer callers stand on: an InternalPtr copy
// hands back its pointer, and a GoalArray temp's count is what GetSize reads.
UNIT_TEST(achievement_hidden_pointer_argument_types) {
    Object::InternalPtr<GoalInstance> except(reinterpret_cast<GoalInstance*>(uintptr_t(0x5000)));
    CHECK(except.Get() == reinterpret_cast<GoalInstance*>(uintptr_t(0x5000)));
    Achievement::GoalArray temp;
    temp._size = 7;   // the count field at +0x08 is what retail reads first
    CHECK_EQ(temp.GetSize(), 7);
    temp._size = 0;   // nothing was allocated: leave the header empty for ~Array
}

}  // namespace
