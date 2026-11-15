// ============================================================================
// src/engine/achievement/achievement_type_records.cpp — the Achievement type
// records (ENDGAME B.3.9 TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// The four `?_typeInfo@…` records of the namespace were MINTED until this
// block; the classes' own records, built at CRT init and head-inserted into
// `Object::BaseTypeInfo::_firstTypeInfo`, are the only ones now:
//
//   ?_typeInfo@Achievement@1@0VBaseTypeInfo@Object@@A            0x1801f9898
//   ?_typeInfo@AchievementInstance@Achievement@@0V…               0x1801f98d0
//   ?_typeInfo@Goal@Achievement@@0V…                              0x1801f9908
//   ?_typeInfo@GoalInstance@Achievement@@0V…                      0x1801f9940
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`; saves carry the
// achievement/goal ids), copied from the minted rows and retail's `_TypeName`
// / `_ParentTypeName` bodies, never respelt:
//   Achievement          parent "Object::External"   (0x180001010)
//   AchievementInstance  parent "Object::Internal"   (0x180001790)
//   Goal                 parent "Object::External"   (0x180001b30)
//   GoalInstance         parent "Object::Internal"   (0x180001e30)
// ext nullptr for all four (the records' +0x18 in retail's .data).
//
// FACTORY PAIRS, as retail's records carry them: Achievement's and Goal's own
// statics (Goal's are the ABSTRACT class's `XOR EAX,EAX` pair, goal.cpp);
// AchievementInstance / GoalInstance carry their own CreateNewObject and, for
// the in-place half, the SHARED `?CreateInPlaceObject@Internal@Object@@`
// (0x1800070c0, `XOR EAX,EAX; RET` — Object::Internal::CreateInPlaceObject).
// DECLARE_TYPE declares a `CreateInPlaceObject` in every class, so the two
// Internal subclasses define theirs below as that shared body: same result
// (null), no second transcription.
//
// Its own translation unit so the unit harness links it without the classes'
// other bodies (tests/unit/b39_ti_rest_test.cpp).
// ============================================================================
#include "achievement/AchievementManager.h"
#include "achievement/Goal.h"
#include "object/Internal.h"

// The alias is load-bearing: the record's initialiser (`Class::_TypeName()`,
// `&Class::CreateNewObject`) is looked up in the CLASS's scope, where an
// unqualified `Achievement` finds the class `Achievement::Achievement` before
// the namespace (and `::Achievement::X` would glue onto the declarator's
// `::Object::BaseTypeInfo`). The name literal is unaffected: DEFINE_TYPE keeps
// only the last component of `#Class`.
namespace AchievementNs = ::Achievement;

DEFINE_TYPE(AchievementNs::Achievement,         "Object::External", nullptr)
DEFINE_TYPE(AchievementNs::AchievementInstance, "Object::Internal", nullptr)
DEFINE_TYPE(AchievementNs::Goal,                "Object::External", nullptr)
DEFINE_TYPE(AchievementNs::GoalInstance,        "Object::Internal", nullptr)

// The shared in-place half (see the banner).
::Object::Base* Achievement::AchievementInstance::CreateInPlaceObject(void* storage) {
    return ::Object::Internal::CreateInPlaceObject(storage);
}
::Object::Base* Achievement::GoalInstance::CreateInPlaceObject(void* storage) {
    return ::Object::Internal::CreateInPlaceObject(storage);
}
