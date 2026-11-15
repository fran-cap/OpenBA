// ============================================================================
// tests/unit/rb_achievement_abi_test.cpp — ENDGAME B.3.8 block RB, the four
// Achievement Runtime bases GOAL (Scenario, the Goal/GoalInstance leaves) and
// LOOP (the AchievementInstance GameUpdateLoop builds) sit on.
//
// B38_RULES D1 / RULE B8-1: each gets a REAL constructor, inline in the
// header (its member initialisers are the body). The destructors are real
// and empty (the compiler's member/base chain IS retail's body).
//
// PART A — RETAIL'S BYTES: a test subclass built over a 0xCD-poisoned buffer
//   carries exactly retail's writes (0x18009e240, 0x18009ed50, 0x18009ef00,
//   0x18009e3b0) and leaves the bytes retail leaves (GoalInstance +0x14).
// PART B — (retired in ENDGAME B.3.9 ENG with the ??0 doors; it compared the
//   door shape against the ctor.)
// PART C — AchievementInstance is CONCRETE: retail slot 0 is the exe's scalar
//   deleting dtor 0x140019d90 (~AchievementInstance, MemFree on flags & 1);
//   B3X W1: the compiler's pair, D1 at `_ZTV…[2]` and D0 at `[3]`.
//
// The virtuals and the out-of-line dtors / GoalList members below are
// MARKERS (their bodies in src/engine/achievement do not link here); the
// constructors themselves are the REAL bodies — they are inline.
// ============================================================================
#include "unit.h"
#include "abi.h"   // RT_ASM_LABEL
#include "dtor_check.h"

#include "achievement/AchievementManager.h"
#include "achievement/Goal.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace Object {
namespace detail {
extern InternalFactory* g_unitInternalFactory;
}  // namespace detail
}  // namespace Object

extern const void* const g_rbAiVtGroup[] RT_ASM_LABEL("_ZTVN11Achievement19AchievementInstanceE");

namespace {
struct FactoryScope {
    ::Object::InternalFactory  factory;
    ::Object::InternalFactory* saved;
    FactoryScope() : factory(), saved(::Object::detail::g_unitInternalFactory) {
        ::Object::detail::g_unitInternalFactory = &factory;
    }
    ~FactoryScope() { ::Object::detail::g_unitInternalFactory = saved; }
};
int g_aiDtorRuns = 0;
// ENDGAME B.3.9 ENG: the Goal / GoalInstance destructor markers RECORD each
// call, so the destructors can be seen running per element (B3X W1: under
// `delete` / `delete[]`).
int         g_dtorCount = 0;
const void* g_dtorOrder[8] = {};
void NoteDtor(const void* self) {
    if (g_dtorCount < 8) g_dtorOrder[g_dtorCount] = self;
    ++g_dtorCount;
}
}  // namespace

// ===========================================================================
// Markers (see the banner).
// ===========================================================================
namespace Achievement {
int32_t     Achievement::GetFileVersion() const { return 3; }
bool        Achievement::Serialize(IO::Stream&) { return true; }
Object::External* Achievement::Reconstruct() { return this; }
void        Achievement::Destruct() {}
Object::External* Achievement::Construct() { return this; }
Achievement::~Achievement() { NoteDtor(this); }

void        AchievementInstance::Persist(IO::Stream&) {}
AchievementInstance::~AchievementInstance() { ++g_aiDtorRuns; }

int32_t     Goal::GetFileVersion() const { return 0x10000; }
bool        Goal::Serialize(IO::Stream&) { return true; }
Object::Type* Goal::GetInstanceType(Object::Type* ret) const { return ret; }
Goal::~Goal() { NoteDtor(this); }

void        GoalInstance::Persist(IO::Stream&) {}
void        GoalInstance::Startup() {}
void        GoalInstance::Update(const System::Time&, const GoalList&) {}
Goal::GoalState GoalInstance::CheckGoalState() const { return Goal::kNotMet; }
GoalInstance::~GoalInstance() { NoteDtor(this); }

GoalList::GoalList() {}
GoalList::~GoalList() {}
}  // namespace Achievement

namespace {

using namespace ::Achievement;

struct RbScenario final : ::Achievement::Achievement {
};
struct RbGoal final : Goal {
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
    GoalState CheckGoalState() const override { return kNotMet; }
};
struct RbGoalInstance final : GoalInstance {
};

template <class T> T* BuildIn(unsigned char* buf) {
    std::memset(buf, 0xCD, sizeof(T));
    return ::new (static_cast<void*>(buf)) T;
}
uint64_t Q(const void* p, size_t off) {
    uint64_t v; std::memcpy(&v, static_cast<const unsigned char*>(p) + off, 8); return v;
}
uint32_t D(const void* p, size_t off) {
    uint32_t v; std::memcpy(&v, static_cast<const unsigned char*>(p) + off, 4); return v;
}

// ===========================================================================
// PART A + B
// ===========================================================================
// ENDGAME B.3.9 ENG: the ??0 doors are retired; the ctors are the only bodies.
UNIT_TEST(rb_achievement_ctor_is_retail_0x18009e240) {
    alignas(8) unsigned char a[sizeof(RbScenario)];
    RbScenario* s = BuildIn<RbScenario>(a);
    for (size_t off = 0x08; off < 0x58; off += 8) CHECK_EQ(Q(s, off), (uint64_t)0);
    s->~RbScenario();
}

// ENDGAME B.3.9 ENG: the ??0Goal / ??0GoalInstance doors are retired (the
// ctors are the only bodies); the door-shape halves of these two tests went
// with them.
UNIT_TEST(rb_goal_ctor_is_retail_0x18009ed50) {
    alignas(8) unsigned char a[sizeof(RbGoal)];
    RbGoal* g = BuildIn<RbGoal>(a);
    for (size_t off = 0x08; off < 0x30; off += 8) CHECK_EQ(Q(g, off), (uint64_t)0);
    g->~RbGoal();
}

UNIT_TEST(rb_goalinstance_ctor_is_retail_0x18009ef00) {
    FactoryScope scope;
    alignas(8) unsigned char a[sizeof(RbGoalInstance)];
    RbGoalInstance* g = BuildIn<RbGoalInstance>(a);
    CHECK_EQ(D(g, 0x10), 0u);             // _flags (a DWORD store)
    CHECK_EQ(D(g, 0x14), 0xCDCDCDCDu);    // +0x14 NEVER written
    CHECK_EQ(Q(g, 0x18), (uint64_t)0);    // _goal
    g->~RbGoalInstance();
}

// ---------------------------------------------------------------------------
// ENDGAME B.3.9 ENG (B39_RULES D1, rule B9-16): Goal / GoalInstance's own
// slot 0 -- retail's VECTOR deleting destructors (element 0x30 / 0x20:
// Goal 0x180001d70, GoalInstance 0x1800020c0). B3X W1: the pair ~T() D1/D0;
// the vector arm has no Itanium counterpart, `delete[]` replaces it.
// ---------------------------------------------------------------------------
// A Goal leaf with no destructor of its own, so ~Goal is the body that runs.
// (Not final: dtor_check::Recording<GoalLeaf> derives from it.)
struct GoalLeaf : Goal {
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
    GoalState CheckGoalState() const override { return kNotMet; }
};

// Retail flags 1 is `delete p`: T's D0 runs the chain once and frees through
// the class-scope operator delete (MemFree; InternalMemFree == free here). The
// free is proven on a Recording<T> stand-in. Retail flags 3 (the vector arm)
// becomes `delete[]` of a `new T[3]`: three destructors, downward.
template <class T, int kSize>
void CheckBothArms() {
    static_assert(sizeof(T) == kSize, "the row's element size");
    static_assert(dtor_check::HasClassDelete<T>::value, "D0 frees through a class-scope operator delete");
    void* m = Object::detail::InternalMemAlloc(kSize, 8);
    CHECK(m != nullptr);
    T* p = ::new (m) T();
    g_dtorCount = 0;
    delete p;
    CHECK_EQ(g_dtorCount, 1);
    CHECK(g_dtorOrder[0] == m);
    T* r = dtor_check::NewOnMalloc<dtor_check::Recording<T>>();
    g_dtorCount = 0; dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_dtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));
    CHECK_ARRAY_DELETE(T, g_dtorCount, g_dtorOrder);
}

UNIT_TEST(b39_eng_goal_deletingdtor_frees_both_arms) {
    CHECK_EQ(sizeof(GoalLeaf), (size_t)0x30);
    CheckBothArms<GoalLeaf, 0x30>();
}

UNIT_TEST(b39_eng_goalinstance_deletingdtor_frees_both_arms) {
    FactoryScope scope;
    CheckBothArms<GoalInstance, 0x20>();
}

// GoalInstance is concrete now: two instances share the compiler's table.
UNIT_TEST(b39_eng_goalinstance_ctor_is_the_class_table) {
    FactoryScope scope;
    alignas(8) unsigned char a[0x20], b[0x20];
    GoalInstance* x = ::new (static_cast<void*>(a)) GoalInstance();
    GoalInstance* y = ::new (static_cast<void*>(b)) GoalInstance();
    RbGoalInstance probe;
    CHECK(*reinterpret_cast<const void* const*>(x) == *reinterpret_cast<const void* const*>(y));
    CHECK(*reinterpret_cast<const void* const*>(x) != *reinterpret_cast<const void* const*>(&probe));
    g_dtorCount = 0;
    x->~GoalInstance();     // retail slot 0, flags 0
    y->~GoalInstance();
    CHECK_EQ(g_dtorCount, 2);
}

UNIT_TEST(rb_achievementinstance_ctor_is_retail_0x18009e3b0) {
    FactoryScope scope;
    alignas(8) unsigned char a[sizeof(AchievementInstance)];
    std::memset(a, 0xCD, sizeof a);
    AchievementInstance* ai = ::new (static_cast<void*>(a)) AchievementInstance();
    CHECK(*reinterpret_cast<const void* const*>(ai) == &g_rbAiVtGroup[2]);
    for (size_t off = 0x10; off < 0x28; off += 8) CHECK_EQ(Q(ai, off), (uint64_t)0);
    ai->~AchievementInstance();   // destruct only (retail flags 0: no free of a stack buffer)
}

UNIT_TEST(rb_achievementinstance_slot0_is_the_exe_scalar_deleting_dtor) {
    FactoryScope scope;
    g_aiDtorRuns = 0;
    void* mem = Object::detail::InternalMemAlloc(sizeof(AchievementInstance), 8);
    AchievementInstance* ai = ::new (mem) AchievementInstance();
    // B3X W1, raw ON PURPOSE (pins the new two-slot order): retail's slot 0
    // with flags 1 is now D0 at `_ZTV…[3]` (the address point is [2]: D1 at
    // [2], D0 at [3]); it returns nothing. ~AchievementInstance, then the
    // class's MemFree operator delete (InternalMemFree == free, matching mem).
    using DtorEntry = void (*)(void*);
    CHECK(g_rbAiVtGroup[2] != g_rbAiVtGroup[3]);
    DtorEntry d0 = reinterpret_cast<DtorEntry>(const_cast<void*>(g_rbAiVtGroup[3]));
    d0(ai);
    CHECK_EQ(g_aiDtorRuns, 1);
    // Retail flags 0 is D1 at `[2]`: the destructor, no free (a stack buffer).
    alignas(8) unsigned char buf[sizeof(AchievementInstance)];
    AchievementInstance* st = ::new (static_cast<void*>(buf)) AchievementInstance();
    DtorEntry d1 = reinterpret_cast<DtorEntry>(const_cast<void*>(g_rbAiVtGroup[2]));
    d1(st);
    CHECK_EQ(g_aiDtorRuns, 2);
}

UNIT_TEST(rb_achievement_bases_shape) {
    CHECK(!std::is_abstract<::Achievement::Achievement>::value);   // ENDGAME B.3.9 ENG
    // B3X W1: every destructor is public and virtual (retail slot 0 = D1 + D0).
    CHECK(std::is_destructible<::Achievement::Achievement>::value);
    CHECK(std::has_virtual_destructor<::Achievement::Achievement>::value);
    CHECK(std::is_abstract<Goal>::value);                // slots 6-8 pure, as retail
    CHECK(!std::is_abstract<GoalInstance>::value);       // ENDGAME B.3.9 ENG
    CHECK(std::is_destructible<Goal>::value);
    CHECK(std::is_destructible<GoalInstance>::value);
    CHECK(std::has_virtual_destructor<GoalInstance>::value);
    CHECK(!std::is_abstract<AchievementInstance>::value);
    CHECK(std::is_destructible<AchievementInstance>::value);
    CHECK(std::has_virtual_destructor<AchievementInstance>::value);
    CHECK(std::has_virtual_destructor<Goal>::value);
    CHECK_EQ(sizeof(RbScenario), (size_t)0x58);
    CHECK_EQ(sizeof(RbGoal), (size_t)0x30);
    CHECK_EQ(sizeof(RbGoalInstance), (size_t)0x20);
}

// ENDGAME B.3.9 ENG: Achievement's own slot 0 -- retail's VECTOR deleting
// destructor 0x180001680 (element 0x58), both arms; two instances share the
// class table. B3X W1: flags 0 is `~Achievement()`, flags 1 `delete`, the
// vector arm `delete[]`.
UNIT_TEST(b39_eng_achievement_deletingdtor_frees_both_arms) {
    using Ach = ::Achievement::Achievement;
    alignas(8) unsigned char x[0x58], y[0x58];
    Ach* a1 = ::new (static_cast<void*>(x)) Ach();
    Ach* a2 = ::new (static_cast<void*>(y)) Ach();
    RbScenario probe;
    CHECK(*reinterpret_cast<const void* const*>(a1) == *reinterpret_cast<const void* const*>(a2));
    CHECK(*reinterpret_cast<const void* const*>(a1) != *reinterpret_cast<const void* const*>(&probe));
    g_dtorCount = 0;
    a1->~Ach();
    a2->~Ach();
    CHECK_EQ(g_dtorCount, 2);

    CheckBothArms<Ach, 0x58>();
}

}  // namespace
