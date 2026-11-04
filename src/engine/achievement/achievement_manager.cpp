// ============================================================================
// src/engine/achievement/achievement_manager.cpp — Wave 5, lane B
// Achievement::Achievement / AchievementInstance / AchievementManager
// ============================================================================
//
// Ports 18 of the 19 CODE exports our tree calls on these three classes
// (docs/history/runtime_port/WAVE5_LANE_B.md has the per-function table). Retail
// addresses are in Runtime-x64.dll, image base 0x180000000; profile addresses
// are in Runtime-x64-profile.dll, the ToolKit build, which is the source of
// NAMES only (PLAN.md §6 step 1). Both were read in Ghidra on 2026-09-06.
//
// HELD BACK by Wave 5, deliberately, with the reasoning in the wave doc:
//   ?Persist@AchievementManager@Achievement@@QEAAXAEAVStream@IO@@@Z (0x18009e910)
//     -- it inlines an IO::Serializer construction, and Wave 4 lane A measured
//       that a Serializer ctor CONSUMES OR EMITS A 12-BYTE STREAM HEADER
//       (src/engine/io/Serializer.h). So it has no runnable oracle of any
//       kind: a second construction desyncs the save.
//
// *** THAT HOLDBACK IS DISCHARGED. WAVE 23 LANE ACHIEVE, 2026-09-11. ***
// The blocker was real when written and stopped being true in Wave 9, when lane
// S built the save-stream emission oracle: it ran each side against its OWN
// INDEPENDENT SYNTHETIC SINK, so there is no shared stream to desync and each
// side constructs its own codec. Lane P discharged the IDENTICAL blocker, in
// the identical words, for Goal::Serialize and GoalInstance::Persist
// (src/engine/achievement/goal_serialize.cpp). Persist is now ported in
// src/engine/achievement/achievement_serialize.cpp, under a PREFIX oracle
// (stopAfter = 1) because its element loop mutates the global Object::Factory.
// The paragraph above is kept rather than deleted: it is the evidence for
// PLAN rule "re-check a stated blocker before acting on it", and this one
// survived fourteen waves because nobody re-read it.
//   ?_singleton@AchievementManager@Achievement@@0PEAV12@EA and the two
//   ?_typeInfo@… symbols are DATA exports; they cannot be registered before
//   cutover (same finding as Wave 4 lane D's FileSystem::_singleton).
//
// STATE. Only nine of these functions are pure reads; the other nine mutate
// the live singleton, a live instance, or the Windows registry.
//
// NO ADDED NULL GUARDS. Retail derefs `_achievements` in GetAchievementCount,
// `_completed._data` in Update and `_instances._data` in Reset without
// checking any of them, and those faults are load-bearing: they are how a
// manager that never got SetAchievements announces itself
// (reference_achievementmanager_has_no_list_until_setachievements.md, whose
// "faults on a READ of 0x28" is re-derived and confirmed in the header). The
// ports below fault in exactly the same places, now at an address inside
// OpenBA.exe rather than inside Runtime-x64.dll.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>          // placement new into the retslot doors

#include "achievement/AchievementManager.h"
#include "io/FileSystem.h"
#include "object/External.h"   // Object::ExternalPtr<T>, ExternalList
#include "object/Internal.h"   // Object::InternalPtr<T>

// ENDGAME B.3.7 block B3: every body below is a MEMBER of
// `Achievement::AchievementManager`, `AchievementInstance` or `Achievement`
// (src/engine/achievement/AchievementManager.h); every registered door is a
// one-line QUALIFIED delegation. ENDGAME B.3.9 ENG: the two CONSTRUCTOR doors
// (`??0Achievement`, `??0AchievementInstance`), which stamped the MINTED
// tables, are gone with those rows -- the classes' real constructors are the
// only bodies, and the factory pairs are the classes' own statics below.
//
// The callers inside this file that used to reach sibling members through
// ResolveExport name doors (`ReadAchievements`, `SetAchievementMet`,
// `CreateInstance`, `GoalList::Initialize`, `Base::IsDerivedFrom`,
// `~External`) and by address (`Array<ExternalPtr<Goal>>::operator=`
// 0x180003970) now call the members directly.

namespace {

// (`?_singleton@AchievementManager@Achievement@@0PEAV12@EA` is the class
// static `AchievementManager::_singleton` since ENDGAME B.3.9 ENG.)

// ?_typeInfo@Achievement@1@0VBaseTypeInfo@Object@@A  (0x1801f9898). The FIRST
// DWORD is the type id — GetAchievement's own `MOV EDX,dword ptr [0x1801f9898]`
// — i.e. retail's static accessor `Achievement::TypeId()` (0x180001030).
uint32_t AchievementTypeId() { return Achievement::Achievement::TypeId(); }

}  // namespace

namespace Achievement {

// ############################################################################
// #  AchievementManager
// ############################################################################

// static AchievementManager* Get()  retail 0x1800079d0 — `return _singleton;`
AchievementManager* AchievementManager::_singleton = nullptr;   // 0x1801fc670

AchievementManager* AchievementManager::Get() {
    return _singleton;
}

// int GetAchievementCount() const  retail 0x1800079b0 — `_achievements->_count`,
// UNGUARDED: a manager with no SetAchievements faults on a read of 0x28, the
// recorded symptom and the only signal that the list is missing.
int AchievementManager::GetAchievementCount() const {
    return _achievements->_count;
}

// bool IsAchievementAwarded(int) const  retail 0x1800079c0 — one byte of
// `_completed`, no bounds check.
bool AchievementManager::IsAchievementAwarded(int index) const {
    return _completed._data[index];
}

// ExternalPtr<Achievement> GetAchievement(int) const  retail 0x18009e8a0
// (door: RCX=this, RDX=retval, R8D=index). `_resources[index]` if it derives
// from Achievement, else null — Base::IsDerivedFrom (0x1800c0390).
Object::ExternalPtr<Achievement> AchievementManager::GetAchievement(int index) const {
    Object::External* obj = _achievements->_resources[index];
    return Object::ExternalPtr<Achievement>(
        obj->IsDerivedFrom(AchievementTypeId()) ? static_cast<Achievement*>(obj) : nullptr);
}

// InternalPtr<AchievementInstance> GetAchievementInstance(int) const
// retail 0x18009e8f0 — one move, no bounds check, no handle validation.
Object::InternalPtr<AchievementInstance> AchievementManager::GetAchievementInstance(int index) const {
    return _instances._data[index];
}

// void SetEventHandler(void (*)(int))  retail 0x18009e6b0 — one store to +0x28.
void AchievementManager::SetEventHandler(EventHandler handler) {
    _eventHandler = handler;
}

// ============================================================================
// void SetAchievements(ExternalPtr<ExternalList>)  retail 0x18009e860
//     _achievements = list;                         ; `MOV [RCX],RDX` — BARE (B7-5)
//     _updateTimes.SetSize(list->_count);           ; 0x180008ff0 kSpec_U64Zero
//     _completed.SetSize(_achievements->_count);    ; 0x180008b90 kSpec_Bool, RE-READ
//     ReadAchievements();
// It does NOT size `_instances` — that is Reset's job on a new game and
// Persist's on a load.
// ============================================================================
void AchievementManager::SetAchievements(Object::ExternalPtr<Object::ExternalList> list) {
    _achievements = list.Get();
    _updateTimes.SetSize(_achievements->_count);
    _completed.SetSize(_achievements->_count);
    ReadAchievements();
}

// ============================================================================
// void Reset()  retail 0x18009e5c0 — THE INSTANCE BUILDER.
// Sizes `_instances` to the achievement count (0x180008d60, kSpec_PtrZero) and
// fills every slot with Achievement::CreateInstance. The per-element type test
// is retail's INLINED Base::IsDerivedFrom — the member here. A missing
// registration still faults rather than yielding null (retail derefs the
// found node unconditionally, and so does the port).
// ============================================================================
void AchievementManager::Reset() {
    _instances.SetSize(_achievements->_count);

    const uint32_t achievementTypeId = AchievementTypeId();
    for (int i = 0; i < _instances._size; ++i) {
        Object::External* obj = _achievements->_resources[i];
        Achievement* achievement =
            obj->IsDerivedFrom(achievementTypeId) ? static_cast<Achievement*>(obj) : nullptr;
        _instances._data[i] = achievement->CreateInstance();
    }
}

// void Update(const System::Time&)  retail 0x18009e6c0 -- in achievement_update.cpp
// (split out for the host unit test, review F-092..F-094).

// ############################################################################
// #  AchievementInstance
// ############################################################################

// ??1AchievementInstance@Achievement@@UEAA@XZ  0x18009e400 — EMPTY: the
// compiler emits the re-stamp, `~GoalList()` on `_goalList` and `~Internal()`.
AchievementInstance::~AchievementInstance() {}

// ============================================================================
// bool IsMet() const  retail 0x18009e2f0
// An EMPTY goal list is NOT met (retail's explicit `count == 0` test).
// Otherwise every goal must have bit 0 set and bit 1 clear — tested in that
// order, as retail's two branches do. Elements dereferenced UNGUARDED.
// ============================================================================
bool AchievementInstance::IsMet() const {
    const int count = _goalList._goals._size;
    if (count == 0) return false;
    for (int i = 0; i < count; ++i) {
        const uint32_t flags = _goalList._goals._data[i]->_flags;
        if ((flags & GoalInstance::kFlagMet) == 0)    return false;
        if ((flags & GoalInstance::kFlagFailed) != 0) return false;
    }
    return true;
}

// bool HasFailed() const  retail 0x18009e2b0 — true if ANY goal has bit 1.
bool AchievementInstance::HasFailed() const {
    const int count = _goalList._goals._size;
    for (int i = 0; i < count; ++i) {
        if ((_goalList._goals._data[i]->_flags & GoalInstance::kFlagFailed) != 0)
            return true;
    }
    return false;
}

// ============================================================================
// void Update(const System::Time&)  retail 0x18009e340
// Forwards the tick to every goal instance through GoalInstance SLOT 5 —
// `(this, const Time&, const GoalList&)`, passing THIS INSTANCE'S OWN list
// (`LEA R8,[RDI+0x10]`). A VIRTUAL call, on purpose: retail dispatches, and
// the instance is an exe subclass (TimeGoalInstance, FinishTutorialInstance).
// ============================================================================
void AchievementInstance::Update(const System::Time& time) {
    const int count = _goalList._goals._size;
    for (int i = 0; i < count; ++i)
        _goalList._goals._data[i]->Update(time, _goalList);
}

// ============================================================================
// void SetAchievement(ExternalPtr<Achievement>)  retail 0x18009e440
//     MOV [RCX+0x20],RDX            ; store FIRST (the argument is BARE, B7-5)
//     temp = achievement->_goals    ; 0x180003970, a deep copy into a fresh buffer
//     _goalList.Initialize(temp)    ; by value — the CALLEE tears `temp` down
// A null achievement faults at `achievement + 0x48`, as retail does.
// ============================================================================
void AchievementInstance::SetAchievement(Object::ExternalPtr<Achievement> achievement) {
    _achievement = achievement;

    GoalArray temp;
    temp.Assign(achievement->_goals);
    _goalList.Initialize(&temp);
}

// ############################################################################
// #  Achievement (the External resource)
// ############################################################################

// ??1Achievement@0@UEAA@XZ  0x18009e180 — EMPTY: see the header. The
// compiler's member destruction order (`_goals`, `_spriteName`,
// `_description`, `_title`) is retail's, then ~External().
Achievement::~Achievement() {}

// ?CreateNewObject@Achievement@1@SAPEAVBase@Object@@XZ  0x180001130
// The FACTREST row this replaces: MemAlloc of 0x58 bytes, align 8 (the locked
// `?MemAlloc@MemoryAllocator@System@@` Object::detail::InternalMemAlloc
// reaches); retail's null branch; then the inlined constructor.
Object::Base* Achievement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Achievement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) Achievement();
}
// ?CreateInPlaceObject@Achievement@1@SAPEAVBase@Object@@PEAX@Z  0x1800011f0
Object::Base* Achievement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) Achievement();
}

// ?CreateNewObject@AchievementInstance@Achievement@@SAPEAVBase@Object@@XZ
// 0x1800018b0 — MemAlloc(0x28, 8); retail's null branch; the inlined ctor
// (CALL ??0Internal, the stamp, three zero qwords). The in-place half is the
// shared `?CreateInPlaceObject@Internal@Object@@`.
Object::Base* AchievementInstance::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AchievementInstance), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) AchievementInstance();
}

// int GetFileVersion() const  retail 0x18009e170 — `return 3;` Scenario's
// override ORs 0x600 onto it (Scenario_Slot3, exe 0x1400a9460).
int32_t Achievement::GetFileVersion() const { return 3; }

// String GetTitle() / GetDescription() / GetSpriteName() const
// retail 0x1800013e0 / 0x180001420 / 0x1800013a0 — the same three-instruction
// body on a different field: the String copy constructor (an ADDREF).
System::String Achievement::GetTitle() const       { return _title; }
System::String Achievement::GetDescription() const { return _description; }
System::String Achievement::GetSpriteName() const  { return _spriteName; }

}  // namespace Achievement
