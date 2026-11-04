// ============================================================================
// src/ext/fps_counter/toolbar_page.cpp -- ext.fps_counter's page in the extension window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/FPSCOUNTER.md, src/ext/ui/Dialog/FpsCounterPage.rsc. Five lines of figures (the
// current, average, minimum and maximum frame rate, and how long they cover) and a Reset button. The frames are
// counted in fps_counter.cpp whether or not the window is open; the page only shows the sums, re-reading them
// each frame it shows and touching a text only when its figure changed.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"

#include "ext/fps_counter/fps_counter.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Ext {
namespace FpsCounter {

namespace {

// Local element ids (the host adds the page's idBase).
constexpr int32_t kResetId = 10;

enum Figure { kCurrent, kAverage, kMinimum, kMaximum, kMeasured, kFigureCount };

const char* const kFigureNames[kFigureCount] = {
    "textFpsCounterCurrentValue", "textFpsCounterAverageValue", "textFpsCounterMinimumValue",
    "textFpsCounterMaximumValue", "textFpsCounterMeasuredValue",
};

UI::TextElement* g_figures[kFigureCount] = {};
char g_shown[kFigureCount][32] = {};   // each figure as last set, so a text is only touched when it changes

void SetFigure(int i, const char* text) {
    if (g_figures[i] == nullptr || std::strcmp(text, g_shown[i]) == 0) return;
    std::snprintf(g_shown[i], sizeof g_shown[i], "%s", text);
    g_figures[i]->SetParameter(0, System::String(text));
}

void FormatRate(char* out, size_t size, bool measured, double rate) {
    if (measured) {
        std::snprintf(out, size, "%.1f", rate);
    } else {
        std::snprintf(out, size, "-");
    }
}

void Show() {
    const Stats& s = Current();
    const int64_t frequency = Frequency();
    const bool measured = s.windows > 0;
    char text[32];
    FormatRate(text, sizeof text, measured, s.current);
    SetFigure(kCurrent, text);
    FormatRate(text, sizeof text, measured, s.Average(frequency));
    SetFigure(kAverage, text);
    FormatRate(text, sizeof text, measured, s.minimum);
    SetFigure(kMinimum, text);
    FormatRate(text, sizeof text, measured, s.maximum);
    SetFigure(kMaximum, text);
    const int64_t seconds = s.Seconds(frequency);
    if (seconds >= 3600) {
        std::snprintf(text, sizeof text, "%lld h %02lld min", static_cast<long long>(seconds / 3600),
                      static_cast<long long>((seconds / 60) % 60));
    } else if (seconds >= 60) {
        std::snprintf(text, sizeof text, "%lld min %02lld s", static_cast<long long>(seconds / 60),
                      static_cast<long long>(seconds % 60));
    } else {
        std::snprintf(text, sizeof text, "%lld s", static_cast<long long>(seconds));
    }
    SetFigure(kMeasured, text);
}

void PageOpen(UI::Element* page, int32_t idBase) {
    for (int i = 0; i < kFigureCount; ++i) {
        g_figures[i] = FindElement<UI::TextElement>(page, kFigureNames[i]);
        g_shown[i][0] = '\0';   // never a figure (none is empty): the first Show sets every text
    }
    UI::ButtonElement* reset = FindElement<UI::ButtonElement>(page, "buttonFpsCounterReset");
    if (reset != nullptr) reset->SetElementId(idBase + kResetId);
    Show();
}

bool PageChanged(UI::Element* /*page*/, int32_t id) {
    if (id != kResetId) return false;
    Reset();
    Show();
    return true;
}

// Every frame the page shows.
void PageTick() {
    Show();
}

void PageClose() {
    for (int i = 0; i < kFigureCount; ++i) g_figures[i] = nullptr;
}

}  // namespace

}  // namespace FpsCounter

namespace Toolbar {

const Contribution& FpsCounterPage() {
    static const Contribution c = { "fps_counter", "FpsCounterTab", "Dialog/FpsCounterPage.rsc:pageFpsCounter",
                                    FpsCounter::PageOpen, FpsCounter::PageChanged, FpsCounter::PageClose,
                                    FpsCounter::PageTick };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
