#pragma once
// ============================================================================
// src/engine/achievement/AchievementManager.h
// Achievement::Achievement / AchievementInstance / AchievementManager
// ============================================================================
// Wave 5, lane B pinned these layouts; ENDGAME B.3.7 block B3 turned them into
// real C++ classes under retail's names (B37_RULES.md §1, §2, rules B7-3,
// B7-5, B7-6, B7-13). The old `RtAchievement::` PODs and the lane-local
// `GoalListView` / `GoalInstanceView` are GONE: every user was inside this
// block's file set and converted with it (B7-6: `GoalListView` folds into
// `Achievement::GoalList`, Goal.h).
//
// Every offset below is pinned by a named instruction in RETAIL
// Runtime-x64.dll (image base 0x180000000). Runtime-x64-profile.dll is used for
// NAMES ONLY (its vtable offsets differ from retail's).
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "Goal.h"
#include "../collection/Collection.h"
#include "../object/External.h"
#include "../object/Internal.h"
#include "../system/String.h"
#include "../system/Value.h"

namespace UI { class StringTable; class SpriteSheet; }
namespace IO { class Stream; }

namespace Achievement {

class AchievementInstance;

// ============================================================================
// Achievement::Achievement — an Object::External subclass, 0x58 bytes. NINE
// slots. RULE B7-3: POLYMORPHIC (the inventory missed it — its vftable is
// mangled with the back-reference `??_7Achievement@0@6B@`, B7-11), and the exe
// builds `Scenario` on it (src/game/Game/scenario.cpp, a C++ subclass whose
// ctor chains this class's real one). ENDGAME B.3.9 ENG (B39_RULES D1/D2):
// CONCRETE -- retail's own slot 0 and public real ctor, the factory pair the
// class's own; the minted row, its RT_DELDTOR row, the ??0 / ??1 doors and the
// slot doors are retired.
//
// `??_7Achievement@0@6B@` @ 0x180154740 (B37_SLOTS.md §1):
//   [0] +0x00 0x180001680  vector deleting dtor (MOV EDX,0x58)
//   [1] +0x08 0x180001040  ?TypeName@Achievement@1@UEBAPEBDXZ
//   [2] +0x10 0x180001020  ?Type@Achievement@1@UEBAIXZ
//   [3] +0x18 0x18009e170  ?GetFileVersion@Achievement@1@UEBAHXZ      (`return 3`)
//   [4] +0x20 0x1800066e0  ?Compile@External@Object@@…  INHERITED
//   [5] +0x28 0x18009e070  ?Serialize@Achievement@1@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180001270  ?Reconstruct@Achievement@1@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180001370  ?Destruct@Achievement@1@UEAAXXZ
//   [8] +0x40 0x1800012f0  ?Construct@Achievement@1@UEAAPEAVExternal@Object@@XZ  (B5-16)
// Literal (B7-2): ?_ParentTypeName@Achievement@1@ 0x180001010 -> "Object::External".
//
// FIELDS, each by the one-instruction accessor that reads it, cross-checked
// against ?Serialize@Achievement@1@ (0x18009e070), which streams them in this
// order: ExternalPtr, String, String, ExternalPtr, String, then the goal
// array's count (+0x50) and body (+0x48).
// ============================================================================
class Achievement : public Object::External {
public:
    // ??0Achievement@0@QEAA@XZ 0x18009e240 — the REAL constructor, PUBLIC:
    // External() and the three stamps are the compiler's; then the seven qword
    // zero stores +0x20..+0x50 (the two ExternalPtrs, the three Strings, the
    // goal array's header).
    Achievement()
        : Object::External(), _stringTable(), _title(), _description(),
          _spriteSheet(), _spriteName(), _goals() {}

    // ---- the nine slots, in retail order -----------------------------------
    // [0] 0x180001680 — MSVC's VECTOR deleting destructor (MOV EDX,0x58), both
    //     arms as the retired RT_DELDTOR row ran them. Scenario declares its own.
    // ??1Achievement@0@UEAA@XZ  0x18009e180. Virtual (B3X W1). the exe's Scenario
    // chains it. Retail: re-stamp, ~Array(+0x48), release +0x40, +0x30, +0x28,
    // then ~External. The body is EMPTY: the compiler destroys the members in
    // REVERSE DECLARATION ORDER — `_goals`, `_spriteName`, `_description`,
    // `_title` (the two ExternalPtrs are trivial) — which is exactly retail's
    // order, then runs ~External().
    ~Achievement() override;
    // [1] 0x180001040 TypeName / [2] 0x180001020 Type, the static TypeId
    // 0x180001030, `_TypeName` 0x180001000 / `_ParentTypeName` 0x180001010
    // and the factory pair 0x180001130 / 0x1800011f0 (achievement_manager.cpp):
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/achievement/achievement_type_records.cpp.
    DECLARE_TYPE(Achievement)
public:
    int32_t     GetFileVersion() const override;              // [3] 0x18009e170
    //          Compile                                       // [4] External's, inherited
    bool        Serialize(IO::Stream& stream) override;             // [5] 0x18009e070 (IO::Stream&)
    Object::External* Reconstruct() override;                 // [6] 0x180001270
    void        Destruct() override;                          // [7] 0x180001370
    virtual Object::External* Construct();                    // [8] 0x1800012f0

    // ---- non-slot exported members (all `@Achievement@1@`, B7-11) ----------
    // The retslot ones return the prvalue.
    Object::ExternalPtr<UI::StringTable> GetStringTable() const { return _stringTable; }  // 0x180001380
    Object::ExternalPtr<UI::SpriteSheet> GetSpriteSheet() const { return _spriteSheet; }  // 0x180001390
    System::String GetSpriteName() const;                     // 0x1800013a0
    System::String GetTitle() const;                          // 0x1800013e0
    System::String GetDescription() const;                    // 0x180001420
    const GoalArray& GetGoalList() const { return _goals; }   // 0x180001460 — `LEA RAX,[RCX+0x48]`
    // ?CreateInstance@Achievement@1@QEBA?AV?$InternalPtr@VAchievementInstance@… 0x18009df80
    Object::InternalPtr<AchievementInstance> CreateInstance() const;

    static void* operator new(std::size_t bytes);             // 0x180001050 — not ported
    static void* operator new(std::size_t bytes, void* place);// 0x1800010c0 — not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                    // 0x1800010d0 — MemFree
    // 0x180001130 / 0x1800011f0 — retail's factory pair (B39_RULES D2):
    // MemAlloc(0x58, 8) + the ctor, and the null-tested in-place ctor
    // (achievement_manager.cpp). The class declares its own placement new:
    // placement construction spells `::new (p) Achievement()`. (Declared by
    // DECLARE_TYPE above.)
    Achievement(const Achievement& other);                    // 0x180001470 — not ported
    Achievement& operator=(const Achievement& other);         // 0x180001550 — not ported

public:
    Object::ExternalPtr<UI::StringTable> _stringTable;   // +0x20  GetStringTable 0x180001380
    System::String                       _title;         // +0x28  GetTitle       0x1800013e0
    System::String                       _description;   // +0x30  GetDescription 0x180001420
    Object::ExternalPtr<UI::SpriteSheet> _spriteSheet;   // +0x38  GetSpriteSheet 0x180001390
    System::String                       _spriteName;    // +0x40  GetSpriteName  0x1800013a0
    GoalArray                            _goals;         // +0x48  GetGoalList    0x180001460
};
static_assert(sizeof(Achievement) == 0x58, "Achievement::Achievement is 0x58 bytes (slot 0 MOV EDX,0x58; ctor's last store +0x50)");
static_assert(alignof(Achievement) == 8, "Achievement::Achievement aligns to 8");
static_assert(offsetof(Achievement, _stringTable) == 0x20, "Achievement::_stringTable @ +0x20");
static_assert(offsetof(Achievement, _title)       == 0x28, "Achievement::_title @ +0x28");
static_assert(offsetof(Achievement, _description) == 0x30, "Achievement::_description @ +0x30");
static_assert(offsetof(Achievement, _spriteSheet) == 0x38, "Achievement::_spriteSheet @ +0x38");
static_assert(offsetof(Achievement, _spriteName)  == 0x40, "Achievement::_spriteName @ +0x40");
static_assert(offsetof(Achievement, _goals)       == 0x48, "Achievement::_goals @ +0x48 (Serialize reads its count at +0x50)");

// ============================================================================
// Achievement::AchievementInstance — an Object::Internal subclass, 0x28 bytes.
// FOUR slots. The exe subclasses it in retail (table 0x14015e350, built in
// GameUpdateLoop's slot-6 Load ON THE LOADER THREAD); the standalone never
// registers that table, so the object keeps the Runtime one. CONCRETE since
// ENDGAME B.3.8 block RB (slot 0 is the exe's scalar deleting dtor), so LOOP
// can build it as `::new (p) Achievement::AchievementInstance()` (D8). ENDGAME
// B.3.9 ENG: the ctor door and the MINTED row are retired -- every instance
// (Achievement::CreateInstance's, LOOP's, the factory's) wears this class's
// table.
//
// `??_7AchievementInstance@Achievement@@6B@` @ 0x180154700:
//   [0] +0x00 0x180001a90  vector deleting dtor (MOV EDX,0x28; ~GoalList(+0x10), ~Internal)
//   [1] +0x08 0x1800017c0  TypeName   [2] +0x10 0x1800017a0  Type
//   [3] +0x18 0x18009e4a0  ?Persist@AchievementInstance@Achievement@@UEAAXAEAVStream@IO@@@Z
// Literal: 0x180001790 -> "Object::Internal".
//
// ??0AchievementInstance (0x18009e3b0): CALL ??0Internal; stamp 0x180154700;
// zero the GoalList (+0x10, two qwords) and +0x20. SetAchievement (0x18009e440)
// `MOV [RCX+0x20],RDX` — the ExternalPtr<Achievement> arrives BARE (B7-5);
// GetGoalList (0x180001960) `LEA RAX,[RCX+0x10]`. CreateInstance allocates 0x28.
// ============================================================================
class AchievementInstance : public Object::Internal {
public:
    // [0] CONCRETE (ENDGAME B.3.8 block RB, B38_RULES D1/D2): the exe
    // instantiates the class itself (its table copy 0x14015e350, built in
    // GameUpdateLoop's Load), whose slot 0 is the exe scalar deleting dtor
    // 0x140019d90: CALL [??1AchievementInstance]; if (flags & 1) MemFree. An
    // exe-built class keeps the SCALAR arms (B39_RULES D1.2); the DLL's vector
    // row 0x180001a90 is retired (ENDGAME B.3.9 ENG).
    // ??1AchievementInstance@Achievement@@UEAA@XZ  0x18009e400. Virtual (B3X W1).
    // Retail: re-stamp, ~GoalList(this+0x10), tail-jump ~Internal. EMPTY body:
    // the compiler destroys `_goalList` (a real GoalList — its ~GoalList
    // deletes every instance) and then runs ~Internal().
    ~AchievementInstance() override;
    // [1] 0x1800017c0 TypeName / [2] 0x1800017a0 Type, the static TypeId
    // 0x1800017b0, `_TypeName` 0x180001780 / `_ParentTypeName` 0x180001790 and
    // CreateNewObject 0x1800018b0: retail's macro (ENDGAME B.3.9 TI-REST); the
    // record is `DEFINE_TYPE`d in achievement_type_records.cpp. Retail's
    // record's in-place half is the SHARED ?CreateInPlaceObject@Internal@Object@@
    // (0x1800070c0); the macro's CreateInPlaceObject forwards to it there.
    DECLARE_TYPE(AchievementInstance)
public:
    void        Persist(IO::Stream& stream) override;               // [3] 0x18009e4a0 (IO::Stream&)

    // ?GetAchievement@AchievementInstance…QEBA?AV?$ExternalPtr@VAchievement@1@… 0x180001950
    Object::ExternalPtr<Achievement> GetAchievement() const { return _achievement; }
    const GoalList& GetGoalList() const { return _goalList; }  // 0x180001960
    bool HasFailed() const;                                   // 0x18009e2b0
    bool IsMet() const;                                       // 0x18009e2f0
    void Update(const System::Time& time);                    // 0x18009e340
    // ?SetAchievement@…QEAAXV?$ExternalPtr@VAchievement@1@@Object@@@Z 0x18009e440
    // — ExternalPtr by value arrives BARE in RDX (B7-5): a register value here.
    void SetAchievement(Object::ExternalPtr<Achievement> achievement);

    static void* operator new(std::size_t bytes);             // 0x1800017d0 — not ported
    static void* operator new(std::size_t bytes, void* place);// 0x180001840 — not ported
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                    // 0x180001850 — MemFree
    // 0x1800018b0 — MemAlloc(0x28, 8) + the ctor (achievement_manager.cpp,
    // B39_RULES D2; declared by DECLARE_TYPE); the in-place half is Internal's
    // shared one.
    AchievementInstance(const AchievementInstance& other);    // 0x180001970 — not ported
    AchievementInstance& operator=(const AchievementInstance& other);  // 0x1800019f0 — not ported

    // ??0AchievementInstance@Achievement@@QEAA@XZ 0x18009e3b0 — the REAL
    // constructor, PUBLIC: Internal() (CALL ??0Internal) and the stamp are the
    // compiler's; then the GoalList's two zero qwords (+0x10/+0x18) and the
    // Achievement pointer (+0x20).
    AchievementInstance() : Object::Internal(), _goalList(), _achievement() {}

public:
    GoalList                         _goalList;      // +0x10 .. +0x1f  held BY VALUE (B7-8)
    Object::ExternalPtr<Achievement> _achievement;   // +0x20
};
static_assert(sizeof(AchievementInstance) == 0x28,
              "AchievementInstance is 0x28 bytes (slot 0 MOV EDX,0x28; CreateInstance allocates 0x28)");
static_assert(alignof(AchievementInstance) == 8, "AchievementInstance aligns to 8");
static_assert(offsetof(AchievementInstance, _goalList)    == 0x10, "AchievementInstance::_goalList @ +0x10 (GetGoalList 0x180001960)");
static_assert(offsetof(AchievementInstance, _achievement) == 0x20, "AchievementInstance::_achievement @ +0x20 (SetAchievement 0x18009e440)");

// ============================================================================
// Achievement::AchievementManager — 0x48 bytes, a process singleton.
// NON-POLYMORPHIC (B37_SLOTS §7: the ctor 0x18009eb00 zeroes +0x00..+0x40 and
// stamps no vtable).
//
// Allocated by `Engine::Startup` (`MemAlloc(0x48)` at 0x1800dbe42) with the
// constructor INLINED there; the port's ctor body is the free function
// `RtAchievement_AchievementManager_Ctor` in src/engine/engine/engine_lifecycle.cpp;
// `_singleton` is the class static (achievement_manager.cpp). `PlatformStartup`
// is defined on the member in src/engine/engine/platform_startup_family.cpp.
//
// THREE OF THE SIX FIELD NAMES ARE THE ENGINE'S OWN, from the profile build's
// assert strings (`Achievement\AchievementManager.cpp`): `_achievements` (+0x00),
// `_completed` and `_lastAchievementUpdate` ("!_completed[_lastAchievementUpdate]").
// `_instances`, `_eventHandler` and `_updateTimes` are OUR names.
//
// FIELD PINS, one retail body each:
//   +0x00 GetAchievementCount (0x1800079b0) = this->_achievements->_count —
//         UNGUARDED: a manager that never had SetAchievements faults on a read
//         of 0x28, which is the recorded symptom and the only signal.
//   +0x08 IsAchievementAwarded (0x1800079c0) one byte of `_completed`.
//   +0x18 GetAchievementInstance (0x18009e8f0) `*(this+0x18)[i]`; +0x20 its _size.
//   +0x28 SetEventHandler (0x18009e6b0).
//   +0x30 Update (0x18009e6c0) `(*(this+0x30))[i] += time` — an
//         Array<System::Time> of ACCUMULATED per-achievement time.
//   +0x40 Update's rotating cursor.
// The profile build's COPY CONSTRUCTOR (0x180029a40) walks the whole object in
// exactly this shape: three deep-copied Arrays, two raw pointers, one int.
//
// THE THREE ARRAYS NAME THEIR RETAIL ROWS IN THEIR TYPES (0x180008b90
// kSpec_Bool, 0x180008d60 kSpec_PtrZero, 0x180008ff0 kSpec_U64Zero — none is
// trait-equivalent, achievement_manager.cpp says why for each), which is what
// lets SetSize be a plain member call instead of an `AsArray` bridge. Their
// destructors are each 0x1800c7ad0's free-then-zero with no element destroy,
// so the compiler-emitted member destruction IS ??1AchievementManager's tail.
// ============================================================================
class AchievementManager {
public:
    using CompletedArray = Collection::Array<bool, Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&rtcoll::kSpec_Bool>>;
    using InstanceArray  = Collection::Array<Object::InternalPtr<AchievementInstance>,
                                             Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    using TimeArray      = Collection::Array<System::Time, Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&rtcoll::kSpec_U64Zero>>;
    using EventHandler   = void (*)(int);

    // ??0AchievementManager@Achievement@@QEAA@XZ 0x18009eb00 — ported as the free
    // function `RtAchievement_AchievementManager_Ctor` (src/engine/engine/
    // engine_lifecycle.cpp). Declared, defined nowhere.
    AchievementManager();
    // ??1AchievementManager@Achievement@@QEAA@XZ 0x18009eab0 — real (see the body).
    ~AchievementManager();
    AchievementManager(const AchievementManager& other);            // 0x1800079e0 — not ported
    AchievementManager& operator=(const AchievementManager& other); // 0x180007a70 — not ported

    static AchievementManager* Get();                          // 0x1800079d0
    int  GetAchievementCount() const;                          // 0x1800079b0
    bool IsAchievementAwarded(int index) const;                // 0x1800079c0
    Object::ExternalPtr<Achievement> GetAchievement(int index) const;                  // 0x18009e8a0 retslot
    Object::InternalPtr<AchievementInstance> GetAchievementInstance(int index) const;  // 0x18009e8f0 retslot
    void SetEventHandler(EventHandler handler);                // 0x18009e6b0
    // ?SetAchievements@…QEAAXV?$ExternalPtr@VExternalList@Object@@@Object@@@Z
    // 0x18009e860 — the ExternalPtr arrives BARE in RDX (B7-5: `MOV [RCX],RDX`).
    void SetAchievements(Object::ExternalPtr<Object::ExternalList> list);
    void Reset();                                              // 0x18009e5c0
    void Update(const System::Time& time);                     // 0x18009e6c0
    void Persist(IO::Stream& stream);                          // 0x18009e910
    void RemoveAll();                                          // 0x18009e9d0
    void ReadAchievements();                                   // 0x18009eb90
    bool SetAchievementMet(int index);                         // 0x18009eca0
    bool Startup();                                            // 0x18009eaa0 — not ported (no caller)
    void Shutdown();                                           // 0x18009ea90 — not ported (no caller)

    // ?_singleton@AchievementManager@Achievement@@0PEAV12@EA (0x1801fc670) —
    // the class static (ENDGAME B.3.9 ENG, B39_RULES D7). Defined in
    // achievement_manager.cpp. Written by `RtAchievement_AchievementManager_Ctor`
    // (engine_lifecycle.cpp) and ~AchievementManager. PUBLIC while foreign
    // readers still reach it.
    static AchievementManager* _singleton;

    // The four private `AEAA` platform hooks (exported — this DLL exports its
    // privates). PUBLIC here, as
    // in Application.h: access control is not ABI (B.3.7 fragment).
    void PlatformConstruct();                                  // 0x18009eb50 — not ported
    void PlatformDestruct();                                   // 0x18009eb60 — not ported
    bool PlatformStartup();                                    // 0x18009eb70 — engine/platform_startup_family.cpp (member since the B.3.7 fragment)
    void PlatformShutdown();                                   // 0x18009eb80 — not ported

public:
    Object::ExternalList* _achievements;           // +0x00 `_achievements`
    CompletedArray        _completed;              // +0x08 `_completed`
    InstanceArray         _instances;              // +0x18 (our name)
    EventHandler          _eventHandler;           // +0x28 (our name)
    TimeArray             _updateTimes;            // +0x30 (our name)
    int32_t               _lastAchievementUpdate;  // +0x40 `_lastAchievementUpdate`
    int32_t               _pad44;                  // +0x44 never read or written
};
static_assert(sizeof(AchievementManager) == 0x48,
              "AchievementManager is 0x48 bytes (Engine::Startup MemAlloc(0x48) at 0x1800dbe42)");
static_assert(offsetof(AchievementManager, _pad44) == 0x44, "AchievementManager::_pad44");
static_assert(alignof(AchievementManager) == 8, "AchievementManager aligns to 8");
static_assert(offsetof(AchievementManager, _achievements)          == 0x00, "_achievements @ +0x00");
static_assert(offsetof(AchievementManager, _completed)             == 0x08, "_completed @ +0x08");
static_assert(offsetof(AchievementManager, _instances)             == 0x18, "_instances @ +0x18");
static_assert(offsetof(AchievementManager, _eventHandler)          == 0x28, "_eventHandler @ +0x28");
static_assert(offsetof(AchievementManager, _updateTimes)           == 0x30, "_updateTimes @ +0x30");
static_assert(offsetof(AchievementManager, _lastAchievementUpdate) == 0x40, "_lastAchievementUpdate @ +0x40");
static_assert(!std::is_polymorphic<AchievementManager>::value, "no vtable (ctor 0x18009eb00)");
static_assert(sizeof(bool) == 1, "_completed is one byte per achievement");

}  // namespace Achievement
