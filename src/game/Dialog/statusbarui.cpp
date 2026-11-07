// src/game/Dialog/statusbarui.cpp
// Clean reimplementation of StatusBarUI methods.
//
// StatusBarUI (0x1D0 bytes): Main HUD bar showing date, population,
// resources, weather, citizen status indicators.
//
// Vtable layout (reimpl names -> actual function):
//   +0x28 (Draw/Slot5):     0x14007a3d0 — element finding/init (319 lines)
//   +0x38 (Startup/Slot7):  0x1400798d0 — main HUD update (299 lines)
//   +0x48 (OnRemove/Slot9): 0x14007a3a0 — status icon click handler (reimplemented)
//
// Original source: C:\Projects\Buildy\code\Game\Dialog/StatusBarUI.h
//                  Dialog\StatusBarUI.cpp

#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/NumberElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/RibbonElement.h"   // the hearts / stars groups' narrowing (0x140151720)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/ui_panels.h"
#include "Dialog/citizen_icon_helpers.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers: System::Time
#include "Object/factory_registration.h"
#include <cstddef>
#include "Component/CitizenDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Component/StatusIconComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/CitizenComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/Calendar.h"
#include "Game/WeatherSimulation.h"
#include "Game/PlayerToolManager.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "ext/registry.h"              // Ext::Fix().statusbar_empty_town

// CitizenUI_UpdateIconDisplay_Impl is in the shared header Dialog/citizen_icon_helpers.h;
// the icon group's Init (0x14006abf0) is TownHall_GroupRatingInit (Dialog/RatingGroup.h).
// (removed local copies — see git history for originals)

// ============================================================
// Constants from exe .rdata
// ============================================================
static constexpr float kMaxHealth       = 1.0f;   // DAT_14017214c
static constexpr float kSecondsPerYear  = 3600.0f; // DAT_14017225c
static constexpr float kMonthsPerYear   = 12.0f;   // DAT_1401721b8
static constexpr float kFreezingF       = 32.0f;   // DAT_1401721d0
static constexpr float kFtoC_Num        = 5.0f;    // DAT_140172198
static constexpr float kFtoC_Den        = 9.0f;    // DAT_1401721ac

// ============================================================
// Helper: the dialog's element of that name, as OnOpened 0x14007a3d0 stores
// most of its finds: DialogInstance::FindElement and the result taken as it is
// (`MOV RAX,[RAX]; MOV [R15+x],RAX`, no IsDerivedFrom).
// ============================================================
template <class T>
static T* FindNamed(StatusBarUI* self, const System::String& name) {
    return static_cast<T*>(self->_dialog->FindElement(name).Get());
}
template <class T>
static T* FindNamed(StatusBarUI* self, const char* name) {
    return FindNamed<T>(self, System::String(name));
}

// ============================================================
// StatusBarUI_Draw (0x14007a3d0, vtable +0x28):
// Element initialization — finds all named UI elements
// ============================================================

// [5] 0x14007a3d0
void StatusBarUI::OnOpened() {
    // --- Date, population, temperature ---
    textSeason        = FindNamed<UI::TextElement>(this, "textSeason");
    textPopulation    = FindNamed<UI::TextElement>(this, "textPopulation");
    numberYear        = FindNamed<UI::NumberElement>(this, "numberYear");
    numberTemperature = FindNamed<UI::NumberElement>(this, "numberTemperature");

    // --- Resource counts ---
    numberWood     = FindNamed<UI::NumberElement>(this, "numberWood");
    numberStone    = FindNamed<UI::NumberElement>(this, "numberStone");
    numberIron     = FindNamed<UI::NumberElement>(this, "numberIron");
    numberFireWood = FindNamed<UI::NumberElement>(this, "numberFireWood");
    numberTools    = FindNamed<UI::NumberElement>(this, "numberTools");
    numberFood     = FindNamed<UI::NumberElement>(this, "numberFood");
    numberHerbs    = FindNamed<UI::NumberElement>(this, "numberHerbs");
    numberClothes  = FindNamed<UI::NumberElement>(this, "numberClothes");
    numberAlcohol  = FindNamed<UI::NumberElement>(this, "numberAlcohol");
    numberCoal     = FindNamed<UI::NumberElement>(this, "numberCoal");
    numberTextile  = FindNamed<UI::NumberElement>(this, "numberTextile");

    // --- Custom resource counters (numberCustom0..9) ---
    for (int i = 0; i < 10; i++) {
        System::String fmtStr = System::String::Format("numberCustom%i", (int)i);
        numberCustom[i] = FindNamed<UI::NumberElement>(this, fmtStr);
    }

    // --- Status icons (imageStatus*) ---
    imageStatusHome    = FindNamed<UI::Element>(this, "imageStatusHome");
    imageStatusFood    = FindNamed<UI::Element>(this, "imageStatusFood");
    imageStatusCold    = FindNamed<UI::Element>(this, "imageStatusCold");
    imageStatusJob     = FindNamed<UI::Element>(this, "imageStatusJob");
    imageStatusTool    = FindNamed<UI::Element>(this, "imageStatusTool");
    imageStatusDisease = FindNamed<UI::Element>(this, "imageStatusDisease");

    // Set element IDs 100-105; each icon raises element events (flag 0x10000)
    UI::Element* const statusIcons[6] = { imageStatusHome, imageStatusFood, imageStatusCold,
                                          imageStatusJob, imageStatusTool, imageStatusDisease };
    for (int i = 0; i < 6; i++) {
        statusIcons[i]->SetElementId(100 + i);
        statusIcons[i]->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);
    }

    // --- Status numbers (numberStatus*) ---
    numberStatusHome    = FindNamed<UI::TextElement>(this, "numberStatusHome");
    numberStatusFood    = FindNamed<UI::TextElement>(this, "numberStatusFood");
    numberStatusCold    = FindNamed<UI::TextElement>(this, "numberStatusCold");
    numberStatusJob     = FindNamed<UI::TextElement>(this, "numberStatusJob");
    numberStatusTool    = FindNamed<UI::TextElement>(this, "numberStatusTool");
    numberStatusDisease = FindNamed<UI::TextElement>(this, "numberStatusDisease");

    // --- Weather icons ---
    imageSunny       = FindNamed<UI::Element>(this, "imageSunny");
    imagePartlySunny = FindNamed<UI::Element>(this, "imagePartlySunny");
    imageCloudy      = FindNamed<UI::Element>(this, "imageCloudy");
    imageRain        = FindNamed<UI::Element>(this, "imageRain");
    imageSnow        = FindNamed<UI::Element>(this, "imageSnow");

    // --- Hearts/Stars icon groups ---
    // Each group is narrowed to RibbonElement (IsDerivedFrom through the import slot
    // 0x140151720 + CMOVNZ, 0x14007acf1..0x14007ad08 / 0x14007ad6e..0x14007ad85) and its
    // InternalPtr handed by address to the group's Init 0x14006abf0 (RCX = +0x108 / +0x128).
    {
        UI::Element* found = FindNamed<UI::Element>(this, "groupHearts");
        UI::Element* group = ::Object::IsDerivedFromNullSafe(found, UI::RibbonElement::TypeId()) ? found : nullptr;
        TownHall_GroupRatingInit(&hearts, &group);   // 0x14006abf0
    }
    {
        UI::Element* found = FindNamed<UI::Element>(this, "groupStars");
        UI::Element* group = ::Object::IsDerivedFromNullSafe(found, UI::RibbonElement::TypeId()) ? found : nullptr;
        TownHall_GroupRatingInit(&stars, &group);    // 0x14006abf0
    }

    // --- Title label: narrowed to TextElement (import slot 0x140151508 + CMOVNZ,
    // 0x14007adeb..0x14007adff) into +0x30 ---
    {
        UI::Element* found = FindNamed<UI::Element>(this, "labelTitleParam");
        labelTitleParam = ::Object::IsDerivedFromNullSafe(found, UI::TextElement::TypeId())
            ? static_cast<UI::TextElement*>(found) : nullptr;
    }

    // --- Set initial title text from map name (SetParameter unguarded, 0x14007ae48) ---
    System::String mapName(GameUpdateLoop::_singleton->m_saveName);   // FUN_140011f90: the String copy
    labelTitleParam->SetParameter(0, mapName);
}

// ============================================================
// StatusBarUI_Startup (0x1400798d0, vtable +0x38):
// Main HUD update — called every frame
// ============================================================
// Iterates all citizens to count status problems (homeless, hungry,
// cold, jobless, toolless, diseased), accumulates health/happiness,
// updates population text, resource counts, weather icons,
// temperature, date, and status indicators.

// [7] 0x1400798d0
void StatusBarUI::OnUpdateLayout() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    // The town (+0xF0)
    const CitizenBehavior* town = game->m_citizenList;
    int citizenCount = town->citizens._size;

    // Accumulators matching decompilation locals
    uint32_t statusBitfield = 0;     // local_res10[0]
    uint32_t adultCount = 0;         // local_res18 — age >= adultAge, not at school
    uint32_t studentCount = 0;       // local_res20 — at school
    uint32_t childCount = 0;         // local_d0 — under adultAge, not at school
    float healthSum = 0.0f;          // fVar28
    float happySum = 0.0f;           // fVar29
    uint32_t statusCounts[6] = {0};  // local_b8[0..5]

    // Iterate all citizens
    for (int i = 0; i < citizenCount; i++) {
        const CitizenComponent* citizen = town->citizens._data[i].Get();

        if (citizen->age < citizen->description->adultAge || citizen->school != nullptr) {
            if (citizen->school == nullptr) {
                childCount++;
            } else {
                studentCount++;
            }
        } else {
            adultCount++;
        }

        // The citizen's StatusIconComponent (retail dereferences it unguarded)
        const StatusIconComponent* icons = citizen->_entity->FindComponent<StatusIconComponent>();

        uint32_t status = icons->_geometry.activeMask;
        statusBitfield |= status;

        // Count individual status bits
        statusCounts[0] += (status & 1);           // home
        statusCounts[1] += ((status >> 1) & 1);    // food
        statusCounts[2] += ((status >> 2) & 1);    // cold
        statusCounts[3] += ((status >> 3) & 1);    // job
        statusCounts[4] += ((status >> 4) & 1);    // tool
        statusCounts[5] += ((status >> 5) & 1);    // disease

        healthSum += citizen->health;
        happySum += citizen->happiness;
    }

    // --- Update hearts (health) icon display ---
    float maxHappy = kMaxHealth;  // default if no citizens
    if (citizenCount == 0) {
        CitizenUI_UpdateIconDisplay_Impl(hearts.whole, hearts.half, 0.0f, kMaxHealth);
        happySum = 0.0f;
    } else {
        // The first citizen's description (Component::_description, checked)
        const Description* desc = town->citizens._data[0]->_description;
        const CitizenDescription* citizenDesc =
            ::Object::IsDerivedFromNullSafe(desc, CitizenDescription::TypeId())
                ? static_cast<const CitizenDescription*>(desc) : nullptr;
        CitizenUI_UpdateIconDisplay_Impl(hearts.whole, hearts.half,
                                        healthSum / (float)citizenCount,
                                        citizenDesc->maxHealth);

        // Retail reads the first citizen's description a second time
        desc = town->citizens._data[0]->_description;
        citizenDesc = ::Object::IsDerivedFromNullSafe(desc, CitizenDescription::TypeId())
                          ? static_cast<const CitizenDescription*>(desc) : nullptr;
        happySum = happySum / (float)citizenCount;
        maxHappy = citizenDesc->maxHappiness;
    }

    // --- Update stars (happiness) icon display ---
    CitizenUI_UpdateIconDisplay_Impl(stars.whole, stars.half, happySum, maxHappy);

    // --- Update status icon visibility and counts ---
    UI::Element* const statusIcons[6] = { imageStatusHome, imageStatusFood, imageStatusCold,
                                          imageStatusJob, imageStatusTool, imageStatusDisease };
    UI::TextElement* const statusNumbers[6] = { numberStatusHome, numberStatusFood, numberStatusCold,
                                                numberStatusJob, numberStatusTool, numberStatusDisease };
    uint32_t mask = 1;
    for (int k = 0; k < 6; k++) {
        bool hasStatus = (statusBitfield & mask) != 0;
        uint32_t hideFlag = hasStatus ? 0 : 1;
        uint32_t showFlag = hasStatus ? 1 : 0;

        statusIcons[k]->SetFlagsOnOff(hideFlag, showFlag);
        statusNumbers[k]->SetFlagsOnOff(hideFlag, showFlag);

        // Set count text as parameter 0
        System::String countStr = System::String::Format("%i", (int)statusCounts[k]);
        statusNumbers[k]->SetParameter(0, countStr);

        mask = (mask << 1) | (uint32_t)((int32_t)mask < 0);  // rotate left
    }

    // --- Update population text (3 params: adults, students, children) ---
    {
        System::String str = System::String::Format("%i", (int)adultCount);
        textPopulation->SetParameter(0, str);
    }
    {
        System::String str = System::String::Format("%i", (int)studentCount);
        textPopulation->SetParameter(1, str);
    }
    {
        System::String str = System::String::Format("%i", (int)childCount);
        textPopulation->SetParameter(2, str);
    }

    // --- Update year ---
    const Calendar* calendar = game->m_dayCounter;
    numberYear->SetValue(static_cast<int32_t>(calendar->year));

    // --- Update month/season ---
    float seconds = calendar->yearTime.AsSeconds();
    uint32_t monthIdx = (uint32_t)((seconds / kSecondsPerYear) * kMonthsPerYear);
    if ((int)monthIdx > 11) monthIdx = 0;

    {
        System::String monthKey = System::String::Format("Month%i", (int)monthIdx);

        // A UI::String of textSeason's own string table (+0xd8) and the key, then
        // SetText. B7 LX (row 994 G30): retail 0x1400798d0 tests neither textSeason
        // nor its table (the UI::String ctor on [+0x38]+0xd8, then
        // TextElement::SetText on [+0x38]); the guard that stood here is gone.
        UI::String month(textSeason->_text.GetStringTable(), monthKey);
        textSeason->SetText(&month);
    }

    // --- Update temperature ---
    const WeatherSimulation* weather = game->m_weather;
    float rawTemp = weather->temperature;
    float displayTemp = rawTemp;

    // Celsius mode (GameSettings::showCelsius): retail reads the settings byte with
    // no test of the settings pointer (`g_pGameSettingsPtr[0x15]`).
    const GameSettings* settings = g_gameSettings;
    if (settings->showCelsius != 0) {
        displayTemp = ((rawTemp - kFreezingF) * kFtoC_Num) / kFtoC_Den;
    }
    numberTemperature->SetValue(static_cast<int32_t>((int)displayTemp));

    // --- Update weather icons ---
    // Weather check uses displayTemp (after conversion), compared to freezing F
    int weatherType = static_cast<int32_t>(weather->weatherType);
    if (displayTemp <= kFreezingF && weatherType == static_cast<int32_t>(WeatherType::Rain)) {
        weatherType = 4;  // snow
    }

    // rain(0), cloudy(1), partly(2), sunny(3), snow(4)
    UI::Element* const weatherIcons[5] = { imageRain, imageCloudy, imagePartlySunny, imageSunny, imageSnow };
    for (int weatherIdx = 0; weatherIdx < 5; weatherIdx++) {
        weatherIcons[weatherIdx]->SetFlagsOnOff((uint32_t)(weatherIdx != weatherType),
                                                (uint32_t)(weatherIdx == weatherType));
    }

    // --- Update resource counts: the town's stock, one entry per resource ---
    const ProfessionResourceEntry* stock = town->professionResources._data;

    // Match exact decompilation order: pre-read into locals
    int valFood     = stock[5].currentCount;
    int valFireWood = stock[3].currentCount;
    int valTools    = stock[4].currentCount;
    int valWood     = stock[0].currentCount;
    int valStone    = stock[1].currentCount;
    int valIron     = stock[2].currentCount;
    int valHerbs    = stock[6].currentCount;
    int valClothes  = stock[7].currentCount;
    int valAlcohol  = stock[8].currentCount;
    int valCoal     = stock[9].currentCount;
    int valTextile  = stock[10].currentCount;

    numberWood->SetValue(static_cast<int32_t>(valWood));
    numberStone->SetValue(static_cast<int32_t>(valStone));
    numberIron->SetValue(static_cast<int32_t>(valIron));
    numberTools->SetValue(static_cast<int32_t>(valTools));
    numberFireWood->SetValue(static_cast<int32_t>(valFireWood));
    numberFood->SetValue(static_cast<int32_t>(valFood));
    numberHerbs->SetValue(static_cast<int32_t>(valHerbs));
    numberClothes->SetValue(static_cast<int32_t>(valClothes));
    numberAlcohol->SetValue(static_cast<int32_t>(valAlcohol));
    numberCoal->SetValue(static_cast<int32_t>(valCoal));

    // Textile — may be null
    if (numberTextile != nullptr) {
        numberTextile->SetValue(static_cast<int32_t>(valTextile));
    }

    // Custom resources (numberCustom0..9): stock entries 11..20
    // (retail: lVar25 from 0x160 to <0x2A0 step 0x20, re-reading the stock each time)
    for (int k = 0; k < 10; k++) {
        if (numberCustom[k] != nullptr) {
            int value = town->professionResources._data[11 + k].currentCount;
            numberCustom[k]->SetValue(static_cast<int32_t>(value));
        }
    }

    // --- Update title text (map name) ---
    System::String mapName(game->m_saveName);   // FUN_140011f90: the String copy
    if (labelTitleParam)
        labelTitleParam->SetParameter(0, mapName);
}

// ============================================================
// StatusBarUI_StatusIconHandler_Impl (was fn_StatusBarUI_StatusIconHandler, FUN_14007a120)
//
// Cycles through citizens to find the next one with the given status problem.
// Uses self->statusCitizenIndex (+0x100) to remember position between clicks.
// On match, navigates camera to the citizen (opens CitizenUI).
//
// iconIndex: 0=home, 1=food, 2=cold, 3=job, 4=tool, 5=disease
// Bitmask: 1 << iconIndex tested against StatusIconComponent's activeMask (+0xF0).
// ============================================================
static void StatusBarUI_StatusIconHandler_Impl(StatusBarUI* self, uint8_t iconIndex) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;

    // Bit 10 of the game flags = locked/paused
    if ((game->m_gameFlags >> 10) & 1) return;

    // The town's citizens (no test of the town or of the count: 0x14007a120)
    const CitizenBehavior* town = game->m_citizenList;
    const int citizenCount = town->citizens._size;
    // FIX statusbar_empty_town (review S-006, NOT RETAIL): retail 0x14007a120 has no count test.
    // A click queued in frame N is dispatched in frame N+1 by UpdateEvents, after that frame's
    // sim tick; when the tick removed the last citizen, the walk below wraps the index to 0 and
    // reads citizens._data[0] past _size: the removed citizen, freed in retail by the same
    // frame's EntityList drain (use after free), and in the port centred on as it dies. With the
    // row on, a click in a town with no citizen does nothing.
    if (Ext::Fix().statusbar_empty_town && citizenCount <= 0)
        return;
    const uint32_t bitMask = 1u << (iconIndex & 0x1F);
    int checked = 0;

    // Advance the cycle (+0x100); a negative index restarts at 0, otherwise the walk
    // enters at its bound test.
    self->statusCitizenIndex++;
    if (static_cast<int32_t>(self->statusCitizenIndex) >= 0) goto bound_test;

    for (;;) {
        self->statusCitizenIndex = 0;
        do {
            {
                const CitizenComponent* citizen =
                    town->citizens._data[static_cast<int32_t>(self->statusCitizenIndex)].Get();
                // The citizen's StatusIconComponent, read unguarded (+0xf0)
                const StatusIconComponent* icons = citizen->_entity->FindComponent<StatusIconComponent>();

                if ((icons->_geometry.activeMask & bitMask) != 0 || citizenCount <= checked) {
                    // Found a match (or exhausted all citizens): retail finds the
                    // component again and returns when the bit is not set
                    ComponentEntity* navEntity =
                        town->citizens._data[static_cast<int32_t>(self->statusCitizenIndex)]->_entity.Get();
                    const StatusIconComponent* navIcons = navEntity->FindComponent<StatusIconComponent>();
                    if ((navIcons->_geometry.activeMask & bitMask) == 0) return;

                    // Navigate to the citizen entity (0x140103040)
                    ComponentEntity* entityRef =
                        town->citizens._data[static_cast<int32_t>(self->statusCitizenIndex)]->_entity.Get();
                    UI::DialogInstance* dialogRef = self->_dialog;
                    game->m_toolManager->CenterOnEntity(&dialogRef, &entityRef);
                    return;
                }
            }
            self->statusCitizenIndex++;
            checked++;
bound_test:
            ;
        } while (static_cast<int32_t>(self->statusCitizenIndex) < citizenCount);
    }
}

// ============================================================
// StatusBarUI_OnRemove (0x14007a3a0, vtable +0x48):
// Status icon click handler.
//
// Called when an element is activated (eventType == 2 = element click).
// Status icons imageStatusHome..imageStatusDisease have element IDs 100-105
// (set during Draw/Startup initialization).
//
// On each click of icon N, cycles to the NEXT citizen whose StatusIconComponent
// bits include bit N (1 << N), increments self->statusCitizenIndex,
// navigates camera and opens CitizenUI for that citizen.
//
// The actual citizen search + navigation is done by the shared helper at
// 0x14007a120 (fn_StatusBarUI_StatusIconHandler), which:
//   1. Checks game state flag (bails if paused/locked)
//   2. Increments and wraps self->statusCitizenIndex within citizen count
//   3. Iterates citizens starting at that index, finds StatusIconComponent
//   4. Tests status & (1 << iconIndex)
//   5. On match: calls FUN_140103040 (navigate camera, open CitizenUI)
// ============================================================

// [9] 0x14007a3a0
void StatusBarUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId eventType, int32_t eventId,
                                 int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    // Click events (2) on element ids 100..106: `param_4 - 100U < 7` (0x14007a3a0);
    // the six status icons carry 100..105 (OnOpened). Icon 0 = home (bit 0), ...
    if (eventType == UI::EventId::LeftPress && static_cast<uint32_t>(eventId - 100) < 7) {
        StatusBarUI_StatusIconHandler_Impl(this, static_cast<uint8_t>(eventId + 0x9c));
    }
}

// ============================================================================
// STANDALONE promotion — real SA vtable for StatusBarUI.
// ============================================================================
// The toolbar "town status" utility dialog (Template/UtilityStatusBar.rsc ->
// StandardDialog.rsc:resourceUserTitle + Dialog/StatusBar.rsc:content, toolbar
// group 4 — same submenu as the Map dialog) is driven by a StatusBarUI
// controller. In standalone this type was a UI_DIALOG_STUB (all slots no-op,
// registration REMOVED from ui_dialog_stub_factories.cpp), so the dialog opened
// with every field on its .rsc default: title untitled, all statistics zero.
//
// Retail vtable (exe @ 0x14015d810):
//   idx5 (+0x28) = 0x14007a3d0  element discovery      -> StatusBarUI_Draw
//   idx7 (+0x38) = 0x1400798d0  full stats refresh      -> StatusBarUI_Startup
//   idx9 (+0x48) = 0x14007a3a0  status-icon click       -> StatusBarUI_OnRemove
//
// Slot 5 is dispatched by DialogInstance::OnOpened when the dialog is added
// (UserInterface::AddDialog), slot 7 by DialogInstance::ComputeLayout from
// UserInterface::Update's per-frame layout pass: the retail refresh.
//
// Data sources (all live after the town-registry passes in starttown.cpp):
// town citizen list townMgr+0xa0/+0xa8 (population/status/hearts/stars),
// resource stock townMgr+0x170, game clock gw+0x128, weather gw+0x130, and
// the town name String at gw+0x28 (title parameter).


// create 0x1400140e0: MemAlloc(0x1d0, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* StatusBarUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StatusBarUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StatusBarUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* StatusBarUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StatusBarUI());
}

DEFINE_EXE_TYPE(StatusBarUI, "UI::DialogController")

