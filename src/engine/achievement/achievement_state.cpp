// ============================================================================
// src/engine/achievement/achievement_state.cpp — Wave 23, lane ACHIEVE
// The five Achievement lifetime/state members Wave 5 lane B left on the DLL
// ============================================================================
//
//   ??1AchievementManager@Achievement@@QEAA@XZ              0x18009eab0
//   ?RemoveAll@AchievementManager@Achievement@@QEAAXXZ      0x18009e9d0
//   ?ReadAchievements@AchievementManager@Achievement@@QEAAXXZ  0x18009eb90
//   ?SetAchievementMet@AchievementManager@Achievement@@QEAA_NH@Z 0x18009eca0
//   ?CreateInstance@Achievement@1@QEBA?AV?$InternalPtr@
//        VAchievementInstance@Achievement@@@Object@@XZ      0x18009df80
//
// Every address is in RETAIL Runtime-x64.dll (image base 0x180000000), read in
// Ghidra on 2026-09-11 as BOTH decompiled C and raw disassembly. Where the two
// disagree the disassembly wins (reference_ghidra_c_output_vs_disassembly.md).
//
// ----------------------------------------------------------------------------
// THE DESTRUCTOR, AND THE TRAP THE WAVE 23 BRIEF NAMED
// ----------------------------------------------------------------------------
// The brief warned that porting a dtor "exactly as prescribed" once swapped a
// body that destroys EVERY element for one that destroys only the first. The
// hazard here turns out to be the MIRROR of that, and it is worth writing down
// because the natural C++ instinct gets it wrong in the other direction:
//
//   ??1AchievementManager DESTROYS NO ELEMENTS AT ALL.
//
// Read at 0x18009eab0 the whole body is four effects — a null store into the
// singleton cell and three CALLs, each to a LEAF that frees one buffer:
//
//     MOV qword ptr [0x1801fc670],0x0      ; _singleton = nullptr, FIRST
//     ADD RCX,0x30 / CALL 0x180009700      ; ~Array(_updateTimes)   +0x30
//     LEA RCX,[RBX+0x18] / CALL 0x180008e80; ~Array(_instances)     +0x18
//     LEA RCX,[RBX+0x8]  / JMP  0x180009560; ~Array(_completed)     +0x08
//
// There is no loop and no vtable call anywhere in it. The AchievementInstance
// objects hanging off _instances are NOT deleted here — `Shutdown` calls
// `RemoveAll` for that, and a manager torn down without a prior Shutdown leaks
// them. That is retail's behaviour, it is load-bearing for the ordering below,
// and "helpfully" adding an element loop would DOUBLE-FREE every instance on
// the normal Shutdown-then-destruct path.
//
// Two more things a tidy-up would break:
//   * the singleton is nulled BEFORE the arrays are freed, not after. Anything
//     that reads `_singleton` from inside a free hook sees null, not a
//     half-destroyed object.
//   * the three arrays go in REVERSE declaration order (+0x30, +0x18, +0x08),
//     which is what MSVC emits and what an allocator trace records.
//
// All three callees (0x180009700 / 0x180008e80 / 0x180009560) are the SAME
// eleven-instruction leaf, byte-for-byte: `MemFree(p); _data = 0; {_size,
// _capacity} = 0`. Here they are the compiler-emitted member destructors of
// the three arrays (`rtcoll::ArrayDtorImpl`, the note after the includes).
//
// ----------------------------------------------------------------------------
// NO ADDED NULL GUARDS, NO ADDED BOUNDS CHECKS
// ----------------------------------------------------------------------------
// `SetAchievementMet` writes `_completed._data[index]` with NO bounds check and
// NO null check on `_data` — `MOVSXD RBX,EDX / MOV RAX,[RCX+8] / MOV byte ptr
// [RBX+RAX],1`, three instructions, no test. `ReadAchievements` derefs the same
// buffer. Both faults are the load-bearing kind achievement_manager.cpp's own
// banner describes: they are how a manager that never got `SetAchievements`
// announces itself. The ports below fault in exactly the same places.
//
// ----------------------------------------------------------------------------
// WHAT THESE TWO TOUCH THAT NOTHING ELSE IN THE NAMESPACE DOES
// ----------------------------------------------------------------------------
// `SetAchievementMet` calls `IO::Persistence::Set` + `WriteSettings`, which
// writes the player's REAL settings store (retail: the HKCU registry; this exe:
// <Documents>\Banished\settings.cfg). `ReadAchievements` reads it back.
// achievement_manager.cpp's banner cites that as the reason Wave 5 lane B left
// them alone. It is a reason to be careful with a probe, not a reason not to
// port: the write is retail's, byte for byte, and `tools/ach_reset.sh` exists
// to undo it.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <new>

#include "achievement/AchievementManager.h"
#include "io/Persistence.h"   // ENDGAME B.3.9 SS1: Persistence::_singleton
#include "system/String.h"

// ENDGAME B.3.7 block B3: the five bodies are MEMBERS now. The three by-address
// buffer frees the old destructor spelled out are the compiler-emitted member
// destructors of `_updateTimes`, `_instances`, `_completed` — each
// `rtcoll::ArrayDtorImpl` (free then zero; the specs have no element destroy),
// run in REVERSE DECLARATION ORDER, which is retail's +0x30, +0x18, +0x08.

// (ENDGAME B.3.9 ENG: CreateInstance's inlined construction is the class's
// real constructor, `::new (p) AchievementInstance()`; the ctor door that
// stamped the MINTED table is retired.)

namespace {


// IO::Persistence's members are called by name (B3R; they were read through
// the name door before).
// ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
::IO::Persistence* PersistenceSingleton() {
    return ::IO::Persistence::_singleton;
}

// The two string literals both bodies pass, read out of .rdata by the lane that
// ported them: section "Ach", key "%i" formatted with the achievement index.
constexpr const char kIndexFormat[] = "%i";
constexpr const char kSection[]     = "Ach";

}  // namespace

namespace Achievement {

// ============================================================================
// AchievementManager::~AchievementManager()   retail 0x18009eab0 (QEAA)
//
//     MOV qword [_singleton],0      ; FIRST, before any free
//     ~Array(_updateTimes) / ~Array(_instances) / ~Array(_completed)
//
// NO ELEMENT IS DESTROYED — see the banner: Shutdown's RemoveAll does that,
// and a loop here would double-free every instance.
// ============================================================================
AchievementManager::~AchievementManager() {
    _singleton = nullptr;
}

// ============================================================================
// void AchievementManager::RemoveAll()   retail 0x18009e9d0
//
// For each slot (bound re-read every pass): if non-null, slot 0 with flag 1 —
// the scalar DELETING destructor, i.e. `Object::Destroy` — then null it. Then
// free the buffer and zero the header (`rtcoll::MemFree`, then the three
// stores the old inline `FreeArrayBuffer` made).
// ============================================================================
void AchievementManager::RemoveAll() {
    for (int32_t i = 0; i < _instances._size; ++i) {
        Object::InternalPtr<AchievementInstance>& slot = _instances._data[i];
        AchievementInstance* inst = slot.Get();
        if (inst != nullptr) {
            delete inst;
            slot = Object::InternalPtr<AchievementInstance>();
        }
    }
    rtcoll::MemFree(_instances._data);
    _instances._data     = nullptr;
    _instances._size     = 0;
    _instances._capacity = 0;
}

// ============================================================================
// void AchievementManager::ReadAchievements()   retail 0x18009eb90
//
// For each completed flag: clear it, format the key, ask Persistence for the
// bool into the flag itself. `_completed._data` is dereferenced UNGUARDED.
// ============================================================================
void AchievementManager::ReadAchievements() {
    ::IO::Persistence* persistence = PersistenceSingleton();

    for (int32_t i = 0; i < _completed._size; ++i) {
        _completed._data[i] = false;

        System::String key = System::String::Format(kIndexFormat, i);

        persistence->Get(kSection, key._data, _completed._data[i]);   // 0x1800b8290
        // `key` releases at the bottom of the iteration — retail's 0x18009ec1a.
    }
}

// ============================================================================
// bool AchievementManager::SetAchievementMet(int)   retail 0x18009eca0
//
// No bounds check, no null check on `_completed._data`. Writes the player's
// REAL settings through Persistence::Set + WriteSettings. Always `MOV AL,1`.
// ============================================================================
bool AchievementManager::SetAchievementMet(int index) {
    _completed._data[index] = true;

    ::IO::Persistence* persistence = PersistenceSingleton();

    System::String key = System::String::Format(kIndexFormat, index);

    persistence->Set(kSection, key._data, _completed._data[index]);   // 0x1800b8380
    persistence->WriteSettings();                                      // 0x1800b88e0

    return true;   // `key` releases first — retail's 0x18009ed30
}

// ============================================================================
// InternalPtr<AchievementInstance> Achievement::CreateInstance() const
// ?CreateInstance@Achievement@1@QEBA?AV?$InternalPtr@VAchievementInstance@…
//                                                    retail 0x18009df80
//
//   p = MemAlloc(0x28, 8)
//   if (p) { ??0AchievementInstance inlined }        ; Internal ctor, stamp, zero
//   *ret = p
//   p->_achievement = this                           ; UNGUARDED — faults on a
//                                                    ; failed allocation
//   Array<ExternalPtr<Goal>> temp = _goals           ; 0x180003970, deep
//   p->_goalList.Initialize(temp)                    ; by value: callee destroys
//
// The allocation goes through the container allocator (`rtcoll::MemAlloc`,
// Collection.h), the same allocator retail's `MemAlloc(0x28, 8)` reaches.
// ============================================================================
Object::InternalPtr<AchievementInstance> Achievement::CreateInstance() const {
    auto* inst = static_cast<AchievementInstance*>(rtcoll::MemAlloc(0x28, 8));
    if (inst != nullptr)
        ::new (static_cast<void*>(inst)) AchievementInstance();

    Object::InternalPtr<AchievementInstance> result(inst);
    inst->_achievement = const_cast<Achievement*>(this);   // faithful: not guarded   // LEFT: audit WORK, left: retail's const CreateInstance stores `this` into the instance's non-const ExternalPtr<Achievement> (+0x20), which code/ readers (goaldialog, gameupdateloop) use non-const; a const pointee would change ExternalPtr<const T> across the readers; S4 ruling: no marker (FLOOR section 4 bars a marker on a this / self operand), the cast stays listed

    // The by-value temporary. `GoalArray::Assign` IS retail's deep
    // `Array<ExternalPtr<Goal>>::operator=` (0x180003970, the J4
    // `Array<T*>::operator=` in array_destroy.cpp): a fresh buffer, one
    // 8-byte copy per element. Initialize consumes AND tears it down, so the
    // scope-exit ~Array is `MemFree(nullptr)`, a no-op.
    GoalArray temp;
    temp.Assign(_goals);
    inst->_goalList.Initialize(&temp);

    return result;
}

}  // namespace Achievement
