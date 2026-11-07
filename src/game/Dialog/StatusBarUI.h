#pragma once
// ============================================================================
// src/game/Dialog/StatusBarUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "StatusBarUI", parent
//   "UI::DialogController", table 0x14015d810, 11 slots, sizeof 0x1d0.
//   create 0x1400140e0: MemAlloc(0x1d0, 8), ??0DialogController, stamp, fields.
//   [0] 0x140014130 scalar deleting dtor (~StatusBarUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x14003a984  JMP [0x140151770]: DialogController's (inherited)
//   [5] 0x14007a3d0  OnOpened
//   [6] 0x14007a3c0  RET 0
//   [7] 0x1400798d0  OnUpdateLayout
//   [8] 0x14003a96c  JMP [0x1401515f8]: DialogController's (inherited)
//   [9] 0x14007a3a0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"
#include "Dialog/RatingGroup.h"      // the hearts / stars groups (+0x108 / +0x128)

namespace UI { class Element; class TextElement; class NumberElement; }

class StatusBarUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(StatusBarUI)

public:
    StatusBarUI() = default;                             // 0x1400140e0 (ctor inlined in the factory)

    // 0x140014130 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    // Retail frees the icon arrays at +0x138, +0x128, +0x118, +0x108: the four
    // Array members' dtors, in that order.
    ~StatusBarUI() override = default;                            // [0] 0x140014130
    // [4] inherited: DialogController::OnCreated (table slot 0x14003a984 jumps to the import)
    void OnOpened() override;   // [5] 0x14007a3d0
    void OnClosed() override {}   // [6] 0x14007a3c0  RET 0 in retail
    void OnUpdateLayout() override;   // [7] 0x1400798d0
    // [8] inherited: DialogController::OnUpdate (table slot 0x14003a96c jumps to the import);
    // the refresh is slot 7, which UserInterface::Update's layout pass dispatches every frame
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14007a3a0

    // ---- fields: OnOpened 0x14007a3d0 finds each by its .rsc name --------
    UI::TextElement*   labelTitleParam = nullptr;     // +0x30 "labelTitleParam" (retail checks LabelElement)
    UI::TextElement*   textSeason = nullptr;          // +0x38 "textSeason": Month%i of its own table
    UI::NumberElement* numberYear = nullptr;          // +0x40 "numberYear"
    UI::TextElement*   textPopulation = nullptr;      // +0x48 "textPopulation": adults, students, children
    UI::NumberElement* numberTemperature = nullptr;   // +0x50 "numberTemperature"
    // +0x58..+0xA8: the stock counts, CitizenBehavior::professionResources[0..10]
    UI::NumberElement* numberWood = nullptr;          // +0x58 [0]
    UI::NumberElement* numberStone = nullptr;         // +0x60 [1]
    UI::NumberElement* numberIron = nullptr;          // +0x68 [2]
    UI::NumberElement* numberFireWood = nullptr;      // +0x70 [3]
    UI::NumberElement* numberTools = nullptr;         // +0x78 [4]
    UI::NumberElement* numberFood = nullptr;          // +0x80 [5]
    UI::NumberElement* numberHerbs = nullptr;         // +0x88 [6]
    UI::NumberElement* numberClothes = nullptr;       // +0x90 [7]
    UI::NumberElement* numberAlcohol = nullptr;       // +0x98 [8]
    UI::NumberElement* numberCoal = nullptr;          // +0xA0 [9]
    UI::NumberElement* numberTextile = nullptr;       // +0xA8 [10]
    UI::NumberElement* numberCustom[10] = {};         // +0xB0..+0xF8 "numberCustom0".."9": [11..20]
    uint32_t statusCitizenIndex = 0;  // +0x100 the next-citizen-with-status cycle (0x14007a120)
    // +0x104: a gap in retail (nothing reads or writes it).
    uint32_t _gap104 = 0;
    // +0x108 / +0x128: the hearts and stars icon groups (Dialog/RatingGroup.h, Init
    // 0x14006abf0): each the "whole" and the "half" icon of every child of the
    // group. ~StatusBarUI 0x140014130 frees +0x138, +0x128, +0x118, +0x108.
    RatingGroup hearts;                               // +0x108 (half +0x118)
    RatingGroup stars;                                // +0x128 (half +0x138)
    // +0x148..+0x170: the status icons, element ids 100..105; bit n of
    // StatusIconComponent's activeMask
    UI::Element*     imageStatusHome = nullptr;       // +0x148 bit 0
    UI::Element*     imageStatusFood = nullptr;       // +0x150 bit 1
    UI::Element*     imageStatusCold = nullptr;       // +0x158 bit 2
    UI::Element*     imageStatusJob = nullptr;        // +0x160 bit 3
    UI::Element*     imageStatusTool = nullptr;       // +0x168 bit 4
    UI::Element*     imageStatusDisease = nullptr;    // +0x170 bit 5
    // +0x178..+0x1A0: each status's citizen count (TextElement::SetParameter 0)
    UI::TextElement* numberStatusHome = nullptr;      // +0x178
    UI::TextElement* numberStatusFood = nullptr;      // +0x180
    UI::TextElement* numberStatusCold = nullptr;      // +0x188
    UI::TextElement* numberStatusJob = nullptr;       // +0x190
    UI::TextElement* numberStatusTool = nullptr;      // +0x198
    UI::TextElement* numberStatusDisease = nullptr;   // +0x1A0
    // +0x1A8..+0x1C8: WeatherSimulation::weatherType 0..3, 4 = snow
    UI::Element*     imageRain = nullptr;             // +0x1A8 0
    UI::Element*     imageCloudy = nullptr;           // +0x1B0 1
    UI::Element*     imagePartlySunny = nullptr;      // +0x1B8 2
    UI::Element*     imageSunny = nullptr;            // +0x1C0 3
    UI::Element*     imageSnow = nullptr;             // +0x1C8 4
};

static_assert(sizeof(StatusBarUI) == 0x1d0, "StatusBarUI is 0x1d0 (create 0x1400140e0 MemAlloc(0x1d0))");
static_assert(offsetof(StatusBarUI, numberStone) == 0x60, "StatusBarUI::numberStone");
static_assert(offsetof(StatusBarUI, numberIron) == 0x68, "StatusBarUI::numberIron");
static_assert(offsetof(StatusBarUI, numberFireWood) == 0x70, "StatusBarUI::numberFireWood");
static_assert(offsetof(StatusBarUI, numberTools) == 0x78, "StatusBarUI::numberTools");
static_assert(offsetof(StatusBarUI, numberFood) == 0x80, "StatusBarUI::numberFood");
static_assert(offsetof(StatusBarUI, numberHerbs) == 0x88, "StatusBarUI::numberHerbs");
static_assert(offsetof(StatusBarUI, numberClothes) == 0x90, "StatusBarUI::numberClothes");
static_assert(offsetof(StatusBarUI, numberAlcohol) == 0x98, "StatusBarUI::numberAlcohol");
static_assert(offsetof(StatusBarUI, numberCoal) == 0xA0, "StatusBarUI::numberCoal");
static_assert(offsetof(StatusBarUI, imageStatusFood) == 0x150, "StatusBarUI::imageStatusFood");
static_assert(offsetof(StatusBarUI, imageStatusCold) == 0x158, "StatusBarUI::imageStatusCold");
static_assert(offsetof(StatusBarUI, imageStatusJob) == 0x160, "StatusBarUI::imageStatusJob");
static_assert(offsetof(StatusBarUI, imageStatusTool) == 0x168, "StatusBarUI::imageStatusTool");
static_assert(offsetof(StatusBarUI, imageStatusDisease) == 0x170, "StatusBarUI::imageStatusDisease");
static_assert(offsetof(StatusBarUI, numberStatusFood) == 0x180, "StatusBarUI::numberStatusFood");
static_assert(offsetof(StatusBarUI, numberStatusCold) == 0x188, "StatusBarUI::numberStatusCold");
static_assert(offsetof(StatusBarUI, numberStatusJob) == 0x190, "StatusBarUI::numberStatusJob");
static_assert(offsetof(StatusBarUI, numberStatusTool) == 0x198, "StatusBarUI::numberStatusTool");
static_assert(offsetof(StatusBarUI, numberStatusDisease) == 0x1A0, "StatusBarUI::numberStatusDisease");
static_assert(offsetof(StatusBarUI, imageCloudy) == 0x1B0, "StatusBarUI::imageCloudy");
static_assert(offsetof(StatusBarUI, imagePartlySunny) == 0x1B8, "StatusBarUI::imagePartlySunny");
static_assert(offsetof(StatusBarUI, imageSunny) == 0x1C0, "StatusBarUI::imageSunny");
static_assert(offsetof(StatusBarUI, imageSnow) == 0x1C8, "StatusBarUI::imageSnow");
static_assert(alignof(StatusBarUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(StatusBarUI, labelTitleParam) == 0x30, "labelTitleParam @ 0x30");
static_assert(offsetof(StatusBarUI, statusCitizenIndex) == 0x100, "statusCitizenIndex @ 0x100");
static_assert(offsetof(StatusBarUI, textSeason) == 0x38, "textSeason @ 0x38");
static_assert(offsetof(StatusBarUI, numberYear) == 0x40, "numberYear @ 0x40");
static_assert(offsetof(StatusBarUI, textPopulation) == 0x48, "textPopulation @ 0x48");
static_assert(offsetof(StatusBarUI, numberTemperature) == 0x50, "numberTemperature @ 0x50");
static_assert(offsetof(StatusBarUI, numberWood) == 0x58, "numberWood @ 0x58");
static_assert(offsetof(StatusBarUI, numberTextile) == 0xA8, "numberTextile @ 0xA8");
static_assert(offsetof(StatusBarUI, numberCustom) == 0xB0, "numberCustom @ 0xB0");
static_assert(offsetof(StatusBarUI, _gap104) == 0x104, "the +0x104 gap");
static_assert(offsetof(StatusBarUI, hearts) == 0x108, "hearts @ 0x108 (0x1400798d0)");
static_assert(offsetof(StatusBarUI, stars) == 0x128, "stars @ 0x128 (0x1400798d0)");
static_assert(offsetof(StatusBarUI, imageStatusHome) == 0x148, "imageStatusHome @ 0x148");
static_assert(offsetof(StatusBarUI, numberStatusHome) == 0x178, "numberStatusHome @ 0x178");
static_assert(offsetof(StatusBarUI, imageRain) == 0x1A8, "imageRain @ 0x1A8");
