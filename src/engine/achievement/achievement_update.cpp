// ============================================================================
// src/engine/achievement/achievement_update.cpp
// Achievement::AchievementManager::Update, retail 0x18009e6c0
// ============================================================================
//
// Split out of achievement_manager.cpp (review F-092..F-094) so the host unit
// test (tests/unit/achievement_update_bounds_test.cpp) links the real body
// without the rest of that file, whose members other unit tests stub with
// markers of their own (rb_achievement_abi_test.cpp, b39_ti_rest_test.cpp).
// ============================================================================

#include "achievement/AchievementManager.h"
#include "io/FileSystem.h"

namespace {

// ?_singleton@FileSystem@IO@@0PEAV12@EA (0x1801fc6a8) -- the class static now
// (ENDGAME B.3.9 SS1, B39_RULES D7): read fresh, no lazy cache.
const ::IO::FileSystem* FileSystemSingleton() {
    return ::IO::FileSystem::_singleton;
}

// AchievementManager::Update's gate, inlined in retail as `CMP dword
// [RAX+0x38],0 / JG done` — `IO::FileSystem::AreUserPackFilesEnabled`
// (0x1800acfc0): the user (mod) pack-file COUNT is positive.
bool UserPackFilesEnabled(const ::IO::FileSystem* fs) {
    return fs->_userPackFilenames.size > 0;
}

}  // namespace

namespace Achievement {

// ============================================================================
// void Update(const System::Time&)  retail 0x18009e6c0 / profile 0x180028cd0
//
// The rotating updater. Once per tick:
//   0. If any user pack file (mod) is mounted, do nothing at all.
//   1. Accumulate the delta into _updateTimes[i] for every not-yet-completed
//      achievement; hard-zero the slot of every completed one.
//   2. Walk the cursor past completed achievements; if it skips all, return.
//   3. Update the ONE instance at the cursor with its accumulated time, zero
//      that slot, and if the instance is now met, award it and fire the handler.
//   4. Advance the cursor, wrapping (on a RE-READ `_instances._size`).
// Retail INLINES AchievementInstance::Update and ::IsMet; the members are
// called. The unguarded `_completed._data[_lastAchievementUpdate]` read is
// retail's and faults when SetAchievements never ran.
//
// NOT RETAIL (review F-092, F-093, F-094): two bounds retail lacks. Persist
// (0x18009e910) sizes `_instances` from the int the save stores, while
// SetAchievements sized `_completed` and `_updateTimes` from the achievement
// list, and nothing reconciles them; Persist also keeps `_lastAchievementUpdate`
// from the previous game. Retail bounds every loop by `_instances._size`
// (`CMP dword ptr [RCX + 0x20],R8D` at 0x18009e6e8), so a save with more
// instances than the list reads past `_completed` and writes past
// `_updateTimes`, and one with fewer than the old cursor indexes `_instances`
// past its end (`MOV RDI,qword ptr [RCX + RAX*0x1]` at 0x18009e779) and calls
// through what it finds: heap out-of-bounds access, undefined behaviour. The
// bound is the smaller of the two sizes and a cursor past it restarts at 0. A
// valid save stores one instance per achievement and leaves the cursor in
// range, so neither changes anything for it.
// ============================================================================
void AchievementManager::Update(const System::Time& time) {
    if (UserPackFilesEnabled(FileSystemSingleton())) return;

    int count = _instances._size;
    if (count > _completed._size) count = _completed._size;   // NOT RETAIL: F-092 / F-093
    if (count > 0 && _lastAchievementUpdate >= count)         // NOT RETAIL: F-094
        _lastAchievementUpdate = 0;
    for (int i = 0; i < count; ++i) {
        if (!_completed._data[i]) _updateTimes._data[i]._ticks += time._ticks;
        else                      _updateTimes._data[i]._ticks = 0;
    }

    int skipped = 0;
    while (_completed._data[_lastAchievementUpdate]) {
        if (skipped >= count) break;
        ++_lastAchievementUpdate;
        ++skipped;
        if (_lastAchievementUpdate >= count) _lastAchievementUpdate = 0;
    }
    if (skipped == count) return;

    int index = _lastAchievementUpdate;
    _instances._data[index]->Update(_updateTimes._data[index]);
    _updateTimes._data[index]._ticks = 0;

    index = _lastAchievementUpdate;
    AchievementInstance* inst = _instances._data[index].Get();
    if (inst->IsMet()) {
        if (!_completed._data[index] && SetAchievementMet(index)) {
            if (_eventHandler) _eventHandler(_lastAchievementUpdate);
        }
    }

    ++_lastAchievementUpdate;
    // NOT RETAIL: the re-read `_instances._size` wrap, also bounded by `_completed`.
    if (_lastAchievementUpdate >= _instances._size || _lastAchievementUpdate >= _completed._size)
        _lastAchievementUpdate = 0;
}

}  // namespace Achievement
