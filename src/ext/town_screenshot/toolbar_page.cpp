// ============================================================================
// src/ext/town_screenshot/toolbar_page.cpp -- ext.town_screenshot's page in the extension window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TOWNSCREENSHOT.md, src/ext/ui/Dialog/TownScreenshotPage.rsc. Three drop downs (the
// quality, the angle and the turn), a button that starts the screenshot, one that stops it, and a line that
// says what is happening. The screenshot itself is town_screenshot.cpp; the page only starts it and shows its
// status line.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/ComboElement.h"
#include "ui/CheckElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"

#include "ext/town_screenshot/town_screenshot.h"
#include "ext/town_screenshot/screenshot_math.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Ext {
namespace TownScreenshot {

namespace {

// Local element ids (the host adds the page's idBase).
constexpr int32_t kTakeId = 10;
constexpr int32_t kCancelId = 11;
constexpr int32_t kQualityId = 12;
constexpr int32_t kAngleId = 13;
constexpr int32_t kTurnId = 14;
constexpr int32_t kWeatherId = 15;
constexpr int32_t kSnowId = 16;
constexpr int32_t kViewId = 17;

Object::InternalPtr<UI::Element> g_page;
UI::ComboElement* g_quality = nullptr;
UI::ComboElement* g_angle = nullptr;
UI::ComboElement* g_turn = nullptr;
UI::CheckElement* g_weather = nullptr;
UI::CheckElement* g_snow = nullptr;
char g_shown[160] = "";   // the status line as last set, so the text is only touched when it changes

const char* const kQualityNames[kQualityCount] = {
    "Draft (4 pixels to the cell)", "Standard (8)", "High (12)", "Maximum (16)",
};
const char* const kAngleNames[kAngleCount] = {
    "Classic (30 degrees)", "True isometric (35 degrees)",
};
const char* const kTurnNames[kTurnCount] = {
    "Not turned", "Quarter turn", "Half turn", "Three-quarter turn",
};

void Fill(UI::ComboElement* combo, const char* const* names, int count, int current) {
    if (combo == nullptr) return;
    combo->RemoveAllItems();
    for (int i = 0; i < count; ++i) {
        combo->AddItem(UI::Sprite(), System::String(names[i]), nullptr);
    }
    combo->SetCurrentItem(current);
}

// The status line into the caption.
void ShowStatus() {
    UI::Element* page = g_page.Get();
    if (page == nullptr) return;
    UI::Element* e = page->FindElement(System::String("textTownScreenshotStatus")).Get();
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) return;
    const char* text = Status();
    if (std::strcmp(text, g_shown) == 0) return;
    std::snprintf(g_shown, sizeof g_shown, "%s", text);
    static_cast<UI::TextElement*>(e)->SetParameter(0, System::String(text));
}

void PageOpen(UI::Element* page, int32_t idBase) {
    g_page = Object::InternalPtr<UI::Element>(page);
    g_shown[0] = '\0';
    SetStatusHook(&ShowStatus);
    static const struct {
        const char* name;
        int32_t id;
    } kButtons[] = {
        { "buttonTownScreenshotTake", kTakeId },
        { "buttonTownScreenshotView", kViewId },
        { "buttonTownScreenshotCancel", kCancelId },
    };
    for (const auto& b : kButtons) {
        UI::ButtonElement* button = FindElement<UI::ButtonElement>(page, b.name);
        if (button != nullptr) button->SetElementId(idBase + b.id);
    }
    g_quality = FindElement<UI::ComboElement>(page, "comboTownScreenshotQuality");
    g_angle = FindElement<UI::ComboElement>(page, "comboTownScreenshotAngle");
    g_turn = FindElement<UI::ComboElement>(page, "comboTownScreenshotTurn");
    if (g_quality != nullptr) g_quality->SetElementId(idBase + kQualityId);
    if (g_angle != nullptr) g_angle->SetElementId(idBase + kAngleId);
    if (g_turn != nullptr) g_turn->SetElementId(idBase + kTurnId);
    g_weather = FindElement<UI::CheckElement>(page, "checkTownScreenshotWeather");
    if (g_weather != nullptr) {
        g_weather->SetElementId(idBase + kWeatherId);
        g_weather->_checked = Choose().ignoreWeather ? 1 : 0;
    }
    g_snow = FindElement<UI::CheckElement>(page, "checkTownScreenshotSnow");
    if (g_snow != nullptr) {
        g_snow->SetElementId(idBase + kSnowId);
        g_snow->_checked = Choose().hideSnow ? 1 : 0;
    }
    const Choices& c = Choose();
    Fill(g_quality, kQualityNames, kQualityCount, c.quality);
    Fill(g_angle, kAngleNames, kAngleCount, c.angle);
    Fill(g_turn, kTurnNames, kTurnCount, c.turn);
    ShowStatus();
}

// The three drop downs into the choices. Read when a screenshot starts as well as when a drop down is used,
// so the choice stands whichever way the window host reports a pick.
void ReadChoices() {
    Choices& c = Choose();
    if (g_quality != nullptr && g_quality->_currentItem >= 0) c.quality = g_quality->_currentItem;
    if (g_angle != nullptr && g_angle->_currentItem >= 0) c.angle = g_angle->_currentItem;
    if (g_turn != nullptr && g_turn->_currentItem >= 0) c.turn = g_turn->_currentItem;
    if (g_weather != nullptr) c.ignoreWeather = g_weather->_checked != 0;
    if (g_snow != nullptr) c.hideSnow = g_snow->_checked != 0;
}

bool PageChanged(UI::Element* /*page*/, int32_t id) {
    Choices& c = Choose();
    if (id == kTakeId) {
        ReadChoices();
        Start();
    } else if (id == kViewId) {
        TakeViewScreenshot();
    } else if (id == kCancelId) {
        Cancel();
    } else if (id == kQualityId && g_quality != nullptr && g_quality->_currentItem >= 0) {
        c.quality = g_quality->_currentItem;
    } else if (id == kAngleId && g_angle != nullptr && g_angle->_currentItem >= 0) {
        c.angle = g_angle->_currentItem;
    } else if (id == kTurnId && g_turn != nullptr && g_turn->_currentItem >= 0) {
        c.turn = g_turn->_currentItem;
    } else if (id == kWeatherId && g_weather != nullptr) {
        c.ignoreWeather = g_weather->_checked != 0;
    } else if (id == kSnowId && g_snow != nullptr) {
        c.hideSnow = g_snow->_checked != 0;
    } else {
        return false;
    }
    ShowStatus();
    return true;
}

// Every frame the page shows: the status line goes back to the progress once a refusal's few seconds are up.
void PageTick() {
    ShowStatus();
}

void PageClose() {
    SetStatusHook(nullptr);
    g_page = Object::InternalPtr<UI::Element>();
    g_quality = nullptr;
    g_angle = nullptr;
    g_turn = nullptr;
    g_weather = nullptr;
    g_snow = nullptr;
}

}  // namespace

}  // namespace TownScreenshot

namespace Toolbar {

const Contribution& TownScreenshotPage() {
    static const Contribution c = { "town_screenshot", "TownScreenshotTab",
                                    "Dialog/TownScreenshotPage.rsc:pageTownScreenshot",
                                    TownScreenshot::PageOpen, TownScreenshot::PageChanged,
                                    TownScreenshot::PageClose, TownScreenshot::PageTick };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
