// src/game/Game/eventlog_achievement.cpp
//
// OnAchievementAwarded (exe 0x140096960) — the achievement award TOAST.
//
// This is the `void (*)(int)` callback retail installs with
// Achievement::AchievementManager::SetEventHandler at GameUpdateLoop::Load step 30
// (call site 0x14009752e; the LEA that loads the handler address is 0x140097524).
// Runtime's AchievementManager invokes it with the awarded achievement's INDEX the
// moment the award is recorded, and all it does is push one event-log entry — i.e.
// the on-screen announcement. With the handler left null the award is still recorded
// and still written to the registry; only the toast is lost. That was the state of
// PLAN.md §1.2 item 6 ("Achievement award toast unported").
//
// Retail body (disasm-verified, 0x140096960 .. 0x140096aea, 97 instructions):
//   RSI = eventLog = *(g_pGameWorldSingleton /*0x1401c3dd8*/) + 0x138
//   ExternalPtr<Achievement> ap;
//   AchievementManager::GetAchievement(**0x1401514f0, &ap, id)          [0x140151540]
//   System::String titleKey;  Achievement::GetTitle(ap, &titleKey)      [0x140151598]
//   UI::String uiName(*(ap+0x20) /*ExternalPtr<StringTable>*/, titleKey)[0x140151300]
//   ~titleKey                                                           [0x140152af0]
//   System::String titleText = (System::String)uiName                   [0x140151308]
//   InternalBasePtr nullEntity(nullptr)                                 [0x140152a90]
//   ... then the AddEventWithName body INLINED with baked-in constants:
//       entry+0x00 colour   = 0xFFEA7517   (what severity 4 maps to)
//       entry+0x10 type     = 0x19
//       entry+0x14 severity = 4
//       entry+0x18 tile     = EventLog_TileIndex(nullEntity)  -> -1
//       entry+0x1c icon     = 9            (what IconIndex(0x19) returns)
//       entry+0x20 popup    = -1           (what PopupIndex(0x19) returns)
//       EventArray_Grow(eventLog+0x18, count+1, entry)                  [0x140019b70]
//   FormatIndexed args: {0}=season {1}=year {2}=titleText — i.e. exactly the
//   AddEventWithName (0x140095750) shape. Every derived value above matches what
//   EventLog_AddEventCore already computes for (0x19, 4, null entity), so we route
//   through the shared core rather than transcribing a 4th copy of the append —
//   see the "AddEvent family core" note in eventlog.cpp on how the copies drift.
//   There is no speed-drop tail (0x19 is outside the 0xb/0xc/0x1e/0x1f set), and
//   retail's tail-free body agrees.
//
// NOTE the UI::String temporary is never destructed in retail either (it holds an
// ExternalPtr + an index, no owned heap String). Kept faithful.

#include "ui/UserInterface.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Game/EventLog.h"         // m_eventNotifier's class: EventLog::AddEventWithName
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/String.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/StringTable.h"    // ENDGAME B.3.6 UI callers (K4)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K2)
#include <new>

extern "C" void EventLog_OnAchievementAwarded(int achievementId) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) return;
    EventLog* eventLog = gameWorld->m_eventNotifier;
    if (!eventLog) return;

    // AchievementManager singleton — exe import slot 0x1401514f0 holds the address
    // of Runtime's ?_singleton@AchievementManager@Achievement@@0PEAV12@EA, so the
    // manager is ::Achievement::AchievementManager::_singleton. Retail derefs it unguarded; we only ever
    // reach here from Runtime's own handler call, so it is always live — the guard
    // below only covers the case where the slot never resolved at all.
    const Achievement::AchievementManager* mgr = ::Achievement::AchievementManager::_singleton;
    if (!mgr) return;

    // GetAchievement returns ExternalPtr<Achievement> BY VALUE (a retslot in
    // retail); the ExternalPtr is one raw pointer and holds no reference.
    const Object::ExternalPtr<Achievement::Achievement> achievementPtr =
        mgr->GetAchievement(achievementId);
    Achievement::Achievement* achievement = achievementPtr.Get();
    if (achievement == nullptr) {
        return;
    }

    System::String titleKey;  // the achievement's title KEY
    ::new (&titleKey) System::String(achievement->GetTitle());

    // UI::String(ExternalPtr<StringTable> = *(achievement+0x20), key), then
    // operator System::String() — the LOOKED-UP display text. This is the
    // NAME-LOOKUP UI::String ctor (0x140151300), not the index one.
    // NOT RETAIL (review F-064, undefined behaviour): placement new of a UI::String needs
    // its 8-byte alignment; retail's local was typed.
    alignas(UI::String) uint8_t uiNameBuf[24] = {0};
    System::String titleText;
    const UI::String* uiName =
        ::new (uiNameBuf) UI::String(achievement->_stringTable, titleKey);   // Achievement+0x20
    titleKey = System::String();   // retail releases the key here
    ::new (&titleText) System::String(uiName->operator System::String());


    ::Object::InternalBasePtr nullEntity(nullptr);

    eventLog->AddEventWithName(/*eventType*/ 0x19, /*severity*/ 4,
                               &nullEntity, &titleText);   // 0x140095750
}
