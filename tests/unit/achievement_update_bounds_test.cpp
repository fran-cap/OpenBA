// ============================================================================
// tests/unit/achievement_update_bounds_test.cpp — review F-092 / F-093 / F-094
// AchievementManager::Update (retail 0x18009e6c0) against the state a save load
// leaves behind.
// ============================================================================
//
// SetAchievements sizes `_completed` and `_updateTimes` from the achievement
// list; Persist (retail 0x18009e910) sizes `_instances` from the int the save
// stores, and keeps `_lastAchievementUpdate` from the previous game. Retail
// bounds Update by `_instances._size` alone, so:
//   - a save with MORE instances than the list reads past `_completed` and
//     writes past `_updateTimes` (F-092, F-093);
//   - a save with FEWER instances than the old cursor indexes `_instances`
//     past its end and calls through what it finds there (F-094).
// Each test builds the manager's fields by hand over buffers wider than the
// sizes it claims, so a write past the logical end lands on a canary the test
// reads back instead of on the heap. The manager is raw storage: its ctor is a
// door defined nowhere, and the buffers are the test's own, never freed by an
// Array dtor.
// ============================================================================
#include "unit.h"

#include "achievement/AchievementManager.h"
#include "io/FileSystem.h"

#include <cstdint>
#include <cstring>

namespace {

using Achievement::AchievementInstance;
using Achievement::AchievementManager;

// A FileSystem with no user pack files mounted: Update's gate reads only
// `_userPackFilenames.size` (+0x38), and an unmodded game has zero.
struct NoPackFileSystem {
    alignas(IO::FileSystem) unsigned char bytes[sizeof(IO::FileSystem)];
    IO::FileSystem* saved = IO::FileSystem::_singleton;   // package_file_read_test points it at its own
    NoPackFileSystem() {
        std::memset(bytes, 0, sizeof(bytes));
        IO::FileSystem::_singleton = reinterpret_cast<IO::FileSystem*>(bytes);
    }
    ~NoPackFileSystem() { IO::FileSystem::_singleton = saved; }
};

struct RawManager {
    alignas(AchievementManager) unsigned char bytes[sizeof(AchievementManager)];
    RawManager() { std::memset(bytes, 0, sizeof(bytes)); }
    AchievementManager& Get() { return *reinterpret_cast<AchievementManager*>(bytes); }
};

}  // namespace

// F-092 / F-093: the save stores 2 instances against a 1-entry list.
// `_completed[1]` and `_updateTimes[1]` are past the arrays' ends; the canary
// in `_updateTimes` slot 1 must survive the tick.
UNIT_TEST(achievement_update_save_count_above_list_stays_in_bounds) {
    NoPackFileSystem fs;
    RawManager raw;
    AchievementManager& m = raw.Get();

    bool completed[4] = {true, true, true, true};          // [1..3] past the end
    System::Time times[4] = {};
    times[0]._ticks = 5;
    times[1]._ticks = 777;                                  // canary past the end
    Object::InternalPtr<AchievementInstance> instances[2];  // null: none is reached

    m._completed._data = completed;
    m._completed._size = 1;
    m._completed._capacity = 1;
    m._updateTimes._data = times;
    m._updateTimes._size = 1;
    m._updateTimes._capacity = 1;
    m._instances._data = instances;
    m._instances._size = 2;
    m._instances._capacity = 2;
    m._lastAchievementUpdate = 0;

    System::Time tick;
    tick._ticks = 10;
    m.Update(tick);

    CHECK_EQ(times[0]._ticks, int64_t{0});    // completed: the slot is zeroed
    CHECK_EQ(times[1]._ticks, int64_t{777});  // past the end: untouched
    CHECK_EQ(m._lastAchievementUpdate, 0);
}

// F-094: the save stores 1 instance against a 2-entry list while the cursor
// left by the previous game is 1. Retail reads `_completed[1]` (false), then
// updates `_instances._data[1]`, one past the end (here it reaches the
// AchievementInstance::Update link stub, which aborts the run).
UNIT_TEST(achievement_update_stale_cursor_after_smaller_save_stays_in_bounds) {
    NoPackFileSystem fs;
    RawManager raw;
    AchievementManager& m = raw.Get();

    bool completed[2] = {true, false};
    System::Time times[2] = {};
    Object::InternalPtr<AchievementInstance> instances[2];  // [1] past the end

    m._completed._data = completed;
    m._completed._size = 2;
    m._completed._capacity = 2;
    m._updateTimes._data = times;
    m._updateTimes._size = 2;
    m._updateTimes._capacity = 2;
    m._instances._data = instances;
    m._instances._size = 1;
    m._instances._capacity = 1;
    m._lastAchievementUpdate = 1;

    System::Time tick;
    tick._ticks = 10;
    m.Update(tick);

    CHECK_EQ(m._lastAchievementUpdate, 0);
    CHECK_EQ(times[1]._ticks, int64_t{0});
}

// A valid save (instance count == list size, cursor in range) keeps retail's
// path: the one not-yet-completed achievement accumulates, the cursor walks
// past completed slots and wraps, nothing is updated when all are complete.
UNIT_TEST(achievement_update_valid_counts_unchanged) {
    NoPackFileSystem fs;
    RawManager raw;
    AchievementManager& m = raw.Get();

    bool completed[3] = {true, true, true};
    System::Time times[3] = {};
    times[0]._ticks = 4;
    times[1]._ticks = 4;
    times[2]._ticks = 4;
    Object::InternalPtr<AchievementInstance> instances[3];

    m._completed._data = completed;
    m._completed._size = 3;
    m._completed._capacity = 3;
    m._updateTimes._data = times;
    m._updateTimes._size = 3;
    m._updateTimes._capacity = 3;
    m._instances._data = instances;
    m._instances._size = 3;
    m._instances._capacity = 3;
    m._lastAchievementUpdate = 2;

    System::Time tick;
    tick._ticks = 10;
    m.Update(tick);

    CHECK_EQ(times[0]._ticks, int64_t{0});
    CHECK_EQ(times[1]._ticks, int64_t{0});
    CHECK_EQ(times[2]._ticks, int64_t{0});
    // Retail's walk: cursor 2 -> 0 -> 1 -> 2, skipped 3 == count, return.
    CHECK_EQ(m._lastAchievementUpdate, 2);
}
