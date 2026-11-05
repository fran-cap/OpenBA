// src/ext/ambient_occlusion/enhanced_entry.cpp -- AO's rows of the Enhanced tab (block AOMENU)
//
// NOT RETAIL. The combo "Ambient occlusion" (Off / Lowest / Low / Medium / High = Params::quality
// 0 / 4 / 1 / 2 / 3: Lowest is stored as 4 so a stored 1..3 keeps its meaning, issue #253) and the
// slider "AO strength" (0..30 = Params::opacity); enhanced_tab.h has the contract.

#include "ext/ambient_occlusion/params.h"
#include "ext/enhanced_tab/enhanced_tab.h"

#include "ao/ao_backend.h"   // src/render/common/ao: AoBackend::Supported

#include "ui/ComboElement.h"
#include "ui/SliderElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement*  g_quality = nullptr;
UI::SliderElement* g_opacity = nullptr;

// The combo's rows (EnhancedOptions.rsc aoQualityLevels) <-> Params::quality.
constexpr int kQualityForItem[] = { 0, 4, 1, 2, 3 };
constexpr int kItemCount = static_cast<int>(sizeof(kQualityForItem) / sizeof(kQualityForItem[0]));

int ItemForQuality(int quality) {
    for (int i = 0; i < kItemCount; ++i)
        if (kQualityForItem[i] == quality) return i;
    return 0;
}

int QualityForItem(int item) { return item >= 0 && item < kItemCount ? kQualityForItem[item] : 0; }

void Open(UI::Element* page) {
    g_quality = FindElement<UI::ComboElement>(page, "comboAoQuality");
    g_opacity = FindElement<UI::SliderElement>(page, "sliderAoOpacity");
    const AmbientOcclusion::Params& now = AmbientOcclusion::Current();
    if (g_quality != nullptr) g_quality->SetCurrentItem(ItemForQuality(now.quality));
    if (g_opacity != nullptr) {
        g_opacity->_minimum = 0.0f;      // like the UI scale slider (OptionsDialog::OnOpened)
        g_opacity->_maximum = 30.0f;     // ext.ao_opacity's range (registry.cpp kValueKeys; the user's ruling 2026-10-14)
        g_opacity->_value = static_cast<float>(now.opacity);
    }
    // P8 round 2: where the backend cannot run the pass (AoBackend::Supported false: the device's
    // features, or a pipeline its driver refused) both rows show disabled rather than doing nothing.
    const bool supported = AoBackend::Supported();
    if (!supported) {
        if (g_quality != nullptr) g_quality->Enable(false);
        if (g_opacity != nullptr) g_opacity->Enable(false);
    }
    std::fprintf(stderr, "[AmbientOcclusion] combo=%p slider=%p quality=%d opacity=%d%s\n",
                 static_cast<void*>(g_quality), static_cast<void*>(g_opacity), now.quality, now.opacity,
                 supported ? "" : " (not supported on this device: disabled)");
}

void Apply() {
    AmbientOcclusion::Params p = AmbientOcclusion::Current();
    if (g_quality != nullptr) p.quality = QualityForItem(g_quality->_currentItem);
    if (g_opacity != nullptr) p.opacity = static_cast<int>(g_opacity->_value + 0.5f);
    AmbientOcclusion::Set(p);
    const AmbientOcclusion::Params& now = AmbientOcclusion::Current();
    std::fprintf(stderr, "[AmbientOcclusion] Apply: quality=%d opacity=%d\n", now.quality, now.opacity);
}

void Close() { g_quality = nullptr; g_opacity = nullptr; }

}  // namespace

const Contribution& AmbientOcclusionEntry() {
    static const Contribution c = { "ambient_occlusion", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext
