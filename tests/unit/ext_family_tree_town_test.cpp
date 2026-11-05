// tests/unit/ext_family_tree_town_test.cpp -- ext.family_tree's game side. Review F-260: NowStamp reads a
// save-derived Calendar (year, yearTime) and AgeYears a save-derived citizen age with no range check. A year
// above 178956970 overflowed `year * 12` (int32), and a yearTime or an age past INT_MAX was a float->int
// conversion out of range (UB) before the clamp. Review F-264: the seams follow the row of the game that
// holds the lock from the moment it takes it, not the last frame of the game before.
#include "unit.h"
#include "ext/family_tree/family_tree.h"
#include "Component/CitizenComponent.h"
#include "Game/Calendar.h"
#include "Game/GameUpdateLoop.h"
#include "ext/registry.h"
#include "ext/savetail.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

// family_tree_town.cpp's two outside references that the unit tree does not link (no panel or camera here).
#include "Entity/ComponentEntity.h"
#include "Game/PlayerToolManager.h"
void Ext::FamilyTree::CitizenButtonsForget() {
}
// PlayerToolManager::CenterOnEntity: statusbar_icon_race_test.cpp defines the unit's one stand-in.

namespace {
struct LoopWithCalendar {
    alignas(16) uint8_t loopBytes[sizeof(GameUpdateLoop)];
    Calendar cal;
    GameUpdateLoop* saved;
    LoopWithCalendar(int32_t year, int64_t yearTicks) : saved(GameUpdateLoop::_singleton) {
        std::memset(loopBytes, 0, sizeof(loopBytes));
        std::memset(&cal, 0, sizeof(cal));
        cal.year = year;
        cal.yearTime._ticks = yearTicks;
        GameUpdateLoop* loop = reinterpret_cast<GameUpdateLoop*>(loopBytes);
        loop->m_dayCounter = &cal;
        GameUpdateLoop::_singleton = loop;
    }
    ~LoopWithCalendar() { GameUpdateLoop::_singleton = saved; }
};
}  // namespace

// Calendar::year 0x7fffff00 from a modified save: year * 12 is signed int32 overflow (UB). The stamp of
// a positive year must not come back negative: it saturates.
UNIT_TEST(ext_family_tree_nowstamp_huge_year) {
    LoopWithCalendar w(0x7fffff00, 0);
    const int32_t stamp = Ext::FamilyTree::NowStamp();
    std::printf("    NowStamp(year=0x7fffff00, yearTime=0) = %d\n", stamp);
    CHECK_EQ(stamp, INT32_MAX);   // saturated
}

// yearTime of INT64_MAX ticks (~9.2e12 s): the month is computed as a float far past INT_MAX and cast to
// int before the 0..11 clamp (UB). Clamped, the month is 11: year 1 -> stamp 1 * 12 + 11 = 23.
UNIT_TEST(ext_family_tree_nowstamp_huge_yeartime) {
    LoopWithCalendar w(1, INT64_MAX);
    const int32_t stamp = Ext::FamilyTree::NowStamp();
    std::printf("    NowStamp(year=1, yearTime=INT64_MAX ticks) = %d\n", stamp);
    CHECK_EQ(stamp, 23);
}

// yearTime of INT64_MIN ticks: the same cast far below INT_MIN. Clamped, the month is 0.
UNIT_TEST(ext_family_tree_nowstamp_negative_yeartime) {
    LoopWithCalendar w(1, INT64_MIN);
    CHECK_EQ(Ext::FamilyTree::NowStamp(), 12);
}

namespace {
// A citizen as the save left it: only the fields the family tree reads (name, gender, age) are set.
struct SavedCitizen {
    alignas(16) uint8_t bytes[sizeof(CitizenComponent)];
    explicit SavedCitizen(float age) {
        std::memset(bytes, 0, sizeof(bytes));
        Citizen()->age = age;
    }
    CitizenComponent* Citizen() { return reinterpret_cast<CitizenComponent*>(bytes); }
};

// The age a citizen who arrives (no parent) is recorded with.
int32_t JoinedAgeOf(float age) {
    LoopWithCalendar w(1, 0);
    Ext::FamilyTree::Town().Clear();
    SavedCitizen c(age);
    Ext::FamilyTree::OnCreatedSlow(c.Citizen(), nullptr);
    const uint32_t id = Ext::FamilyTree::PersonOfCitizen(c.Citizen());
    const Ext::FamilyTree::Person* p = Ext::FamilyTree::Town().Find(id);
    const int32_t joinedAge = p != nullptr ? p->joinedAge : -1;
    // unlinks the citizen again
    Ext::FamilyTree::OnRemovedSlow(c.Citizen());
    Ext::FamilyTree::Town().Clear();
    return joinedAge;
}
}  // namespace

// An age of 1e30 (x5 = 5e30 years) is past INT_MAX: the cast was UB, and on x86 it gave INT_MIN, read as 0.
// Clamped, the age is the largest an int holds. NaN reads as 0, and an ordinary age is unchanged.
UNIT_TEST(ext_family_tree_ageyears_out_of_range) {
    const int32_t huge = JoinedAgeOf(1e30f);
    std::printf("    joinedAge(age=1e30) = %d\n", huge);
    CHECK_EQ(huge, INT32_MAX);
    CHECK_EQ(JoinedAgeOf(std::numeric_limits<float>::quiet_NaN()), 0);
    CHECK_EQ(JoinedAgeOf(-3.0f), 0);
    CHECK_EQ(JoinedAgeOf(4.1f), 20);
}

namespace {
// What GameUpdateLoop::Load runs before the start town's citizens are made (gameupdateloop_load.cpp: the
// class lock, then every class C codec's reset), and ~GameUpdateLoop when the game ends.
void TakeLock(const void* game) {
    Ext::LockForGame(game);
    Ext::SaveTail::ResetAllState();
}
}  // namespace

// Review F-264: game A plays with the row on; back at the menu the player turns it off and starts game B. The
// start town's citizens of B are made before B's first frame, and the seams must already be off; in reverse,
// a game with the row on records its founders at creation.
UNIT_TEST(ext_family_tree_seams_follow_the_row_of_the_game_that_took_the_lock) {
    const Ext::Entry* row = Ext::FindEntry("family_tree");
    CHECK(row != nullptr);
    if (row == nullptr) return;
    const int gameA = 0;
    const int gameB = 0;
    Ext::SetRequested(*row, true);
    TakeLock(&gameA);
    Ext::FamilyTree::BeginFrame();
    CHECK(Ext::FamilyTree::g_on);
    Ext::UnlockForGame(&gameA);
    // row off, game B: its founders are made before its first frame
    Ext::SetRequested(*row, false);
    TakeLock(&gameB);
    SavedCitizen founderB(4.0f);
    Ext::FamilyTree::OnCreated(founderB.Citizen(), nullptr);
    CHECK(!Ext::FamilyTree::g_on);
    CHECK_EQ(Ext::FamilyTree::Town().Size(), 0u);
    CHECK_EQ(Ext::FamilyTree::PersonOfCitizen(founderB.Citizen()), 0u);
    Ext::FamilyTree::BeginFrame();
    Ext::UnlockForGame(&gameB);
    // row on again, game A: its founders are recorded when they are made
    Ext::SetRequested(*row, true);
    TakeLock(&gameA);
    SavedCitizen founderA(4.0f);
    Ext::FamilyTree::OnCreated(founderA.Citizen(), nullptr);
    CHECK(Ext::FamilyTree::g_on);
    CHECK_EQ(Ext::FamilyTree::Town().Size(), 1u);
    CHECK(Ext::FamilyTree::PersonOfCitizen(founderA.Citizen()) != 0u);
    // the game ends: no game, no row
    Ext::UnlockForGame(&gameA);
    CHECK(!Ext::FamilyTree::g_on);
    CHECK_EQ(Ext::FamilyTree::PersonOfCitizen(founderA.Citizen()), 0u);
    Ext::SetRequested(*row, false);
    Ext::FamilyTree::Town().Clear();
}
