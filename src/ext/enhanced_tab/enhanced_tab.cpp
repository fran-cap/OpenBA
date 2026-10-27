// src/ext/enhanced_tab/enhanced_tab.cpp -- the Options dialog's "Enhanced" tab (blocks AOMENU, FAA-MENU)
//
// NOT RETAIL. enhanced_tab.h has the contract.

#include "ext/enhanced_tab/enhanced_tab.h"

#include "ext/registry.h"

#include "ui/ElementDescription.h"
#include "ui/TabElement.h"
#include "object/External.h"
#include "system/Filename.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

constexpr const char* kButtonRsc = "Dialog/EnhancedOptions.rsc:buttonEnhanced";
constexpr const char* kPageRsc   = "Dialog/EnhancedOptions.rsc:enhancedOptions";

const void* g_dialog = nullptr;   // the one open Options dialog

UI::ElementDescription* LoadDescription(const char* path) {
    UI::ElementDescription* d =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<UI::ElementDescription>(
            System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(d, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[EnhancedTab] %s did not load (%p): no Enhanced tab\n", path,
                     static_cast<const void*>(d));
        return nullptr;
    }
    return d;
}

const Contribution* const* Contributions(int* n) {
    static const Contribution* const list[] = {
        &AmbientOcclusionEntry(), &BetterTreesEntry(), &MaxShadowResEntry(), &SeamlessLoopsEntry(),
        &TextureQualityEntry(), &RenderScaleEntry(), &FpsCapEntry(), &TouchModeEntry(), &TouchPointerEntry(),
        &DerivedCacheEntry(),
    };
    *n = static_cast<int>(sizeof(list) / sizeof(list[0]));
    return list;
}

}  // namespace

bool Offered() { return !GateProfileForced(); }

bool Open(const void* dialog, UI::TabElement* tabs) {
    g_dialog = nullptr;
    if (!Offered()) return false;
    UI::ElementDescription* button = LoadDescription(kButtonRsc);
    UI::ElementDescription* content = LoadDescription(kPageRsc);
    if (button == nullptr || content == nullptr) return false;

    Object::InternalPtr<UI::Element> page =
        tabs->AddTab(Object::ExternalPtr<UI::ElementDescription>(button),
                     Object::ExternalPtr<UI::ElementDescription>(content));
    UI::Element* root = page.Get();
    if (root == nullptr) return false;

    g_dialog = dialog;
    int n = 0;
    const Contribution* const* list = Contributions(&n);
    for (int i = 0; i < n; ++i) list[i]->open(root);
    std::fprintf(stderr, "[EnhancedTab] Enhanced tab added: tabs=%d entries=%d\n", tabs->_tabCount, n);
    return true;
}

void Apply(const void* dialog) {
    if (dialog == nullptr || dialog != g_dialog) return;
    int n = 0;
    const Contribution* const* list = Contributions(&n);
    for (int i = 0; i < n; ++i) list[i]->apply();
}

bool OnButton(const void* dialog, int32_t elementId) {
    if (dialog == nullptr || dialog != g_dialog) return false;
    int n = 0;
    const Contribution* const* list = Contributions(&n);
    for (int i = 0; i < n; ++i) {
        if (list[i]->button != nullptr && list[i]->button(elementId)) return true;
    }
    return false;
}

void Close(const void* dialog) {
    if (dialog == nullptr || dialog != g_dialog) return;
    g_dialog = nullptr;
    int n = 0;
    const Contribution* const* list = Contributions(&n);
    for (int i = 0; i < n; ++i) list[i]->close();
}

}  // namespace EnhancedTab
}  // namespace Ext
