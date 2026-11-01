// ============================================================================
// src/ext/show_grid/toolbar_page.cpp -- ext.show_grid's page in the extension window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/SHOWGRID.md, src/ext/ui/Dialog/ShowGridPage.rsc. A check box that shows or hides
// the grid (a runtime flag, ShowGrid::Shown(): not a value key, never written to settings.cfg; the row
// stays on, so the tab stays) and five sliders: the lines' opacity, shade, width, hue and color (the value
// keys show_grid_opacity, _shade, _width, _hue and _saturation). The check box and the sliders are read once
// a frame while the page shows (PageTick), so the host needs no slider event; a changed slider value is
// stored and written to settings.cfg as the Options dialog does.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/CheckElement.h"
#include "ui/SliderElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"

#include "ext/registry.h"
#include "ext/show_grid/show_grid.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <cmath>
#include <cstdint>

namespace Ext {
namespace ShowGrid {

namespace {

// Local element ids (the host adds the page's idBase).
constexpr int32_t kShownId = 10;

Object::InternalPtr<UI::Element> g_page;
UI::CheckElement* g_shownBox = nullptr;
UI::SliderElement* g_opacity = nullptr;
UI::SliderElement* g_shade = nullptr;
UI::SliderElement* g_hue = nullptr;
UI::SliderElement* g_saturation = nullptr;
UI::SliderElement* g_width = nullptr;
bool g_dirty = false;   // a value changed while the page was open: settings.cfg is written when it closes

const ValueKey* Key(const char* id) {
    return FindValueKey(id);
}

// A slider takes the key's range and its current value.
void Setup(UI::SliderElement* slider, const char* id) {
    const ValueKey* k = Key(id);
    if (slider == nullptr || k == nullptr) return;
    slider->_minimum = static_cast<float>(k->minValue);
    slider->_maximum = static_cast<float>(k->maxValue);
    slider->_value = static_cast<float>(Value(*k));
}

// The slider's whole-number value into the key, when it is not the stored one.
void Read(const UI::SliderElement* slider, const char* id) {
    const ValueKey* k = Key(id);
    if (slider == nullptr || k == nullptr) return;
    const int32_t wanted = static_cast<int32_t>(std::floor(slider->_value + 0.5f));
    if (wanted == Value(*k)) return;
    SetValueAndRecord(*k, wanted);
    g_dirty = true;
}

void ReadShown() {
    if (g_shownBox == nullptr) return;
    SetShown(g_shownBox->_checked != 0);
}

void PageOpen(UI::Element* page, int32_t idBase) {
    g_page = Object::InternalPtr<UI::Element>(page);
    g_dirty = false;
    g_shownBox = FindElement<UI::CheckElement>(page, "checkShowGridShown");
    if (g_shownBox != nullptr) {
        g_shownBox->SetElementId(idBase + kShownId);
        g_shownBox->_checked = Shown() ? 1 : 0;
    }
    g_opacity = FindElement<UI::SliderElement>(page, "sliderShowGridOpacity");
    g_shade = FindElement<UI::SliderElement>(page, "sliderShowGridShade");
    g_width = FindElement<UI::SliderElement>(page, "sliderShowGridWidth");
    g_hue = FindElement<UI::SliderElement>(page, "sliderShowGridHue");
    g_saturation = FindElement<UI::SliderElement>(page, "sliderShowGridSaturation");
    Setup(g_opacity, "show_grid_opacity");
    Setup(g_shade, "show_grid_shade");
    Setup(g_width, "show_grid_width");
    Setup(g_hue, "show_grid_hue");
    Setup(g_saturation, "show_grid_saturation");
}

bool PageChanged(UI::Element* /*page*/, int32_t id) {
    if (id != kShownId) return false;
    ReadShown();
    return true;
}

// Every frame the page shows.
void PageTick() {
    ReadShown();   // the same read as PageChanged's, in case the host reports a click another way
    Read(g_opacity, "show_grid_opacity");
    Read(g_shade, "show_grid_shade");
    Read(g_width, "show_grid_width");
    Read(g_hue, "show_grid_hue");
    Read(g_saturation, "show_grid_saturation");
}

void PageClose() {
    if (g_dirty) PersistSettings();
    g_dirty = false;
    g_page = Object::InternalPtr<UI::Element>();
    g_shownBox = nullptr;
    g_opacity = nullptr;
    g_shade = nullptr;
    g_width = nullptr;
    g_hue = nullptr;
    g_saturation = nullptr;
}

}  // namespace

}  // namespace ShowGrid

namespace Toolbar {

const Contribution& ShowGridPage() {
    static const Contribution c = { "show_grid", "ShowGridTab", "Dialog/ShowGridPage.rsc:pageShowGrid",
                                    ShowGrid::PageOpen, ShowGrid::PageChanged, ShowGrid::PageClose,
                                    ShowGrid::PageTick };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
