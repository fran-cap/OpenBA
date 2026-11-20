// ============================================================================
// src/ext/item_limits/pasture_rows.cpp -- ext.item_limits' rows on the pasture panel
// ============================================================================
// docs/extensions/handins/ITEMLIMITS.md section 4. A pasture's panel (PastureUI, pastureui.cpp) gets one row
// per item its livestock gives: what a slaughter yields (the AnimalDescription's raw materials: meat, leather,
// a mod's feathers ...) and what the herd drops over time (the LivestockDescription's byproduct: wool, milk,
// eggs, a mod's fertilizer ...). Each row is the workshop panel's row (panel_common.h): a check box,
// "<item> limit" and a copy of the panel's category limit spinner. The rows sit in a column under the
// panel's animal-count slider, which takes the slider's slot, so the panel grows by their height.
//
// The rows follow the livestock the panel's combo selects: when it changes, a new column of rows replaces the
// old one. A panel with no category limit spinner, or a pasture with no livestock yet, gets no rows.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/ComboElement.h"
#include "ui/SliderElement.h"
#include "ui/SpinnerElement.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/Dialog.h"
#include "ui/String.h"
#include "ui/StringTable.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"

#include "Action/exe_bodies.h"   // ResourceCategory
#include "Component/AnimalDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/LivestockDescription.h"
#include "Component/PastureComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Dialog/PastureUI.h"
#include "Entity/ComponentEntity.h"

#include "ext/item_limits/item_limits.h"
#include "ext/item_limits/panel_common.h"

#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace ItemLimits {

namespace {

// Row i's check box is kFirstId + 2i, its spinner the next id: clear of the panel's own ids (the combo's 500,
// the slider's 501, the category spinner's 0x3039) and of the workshop and gatherer rows' 0x7E10..0x7E2F and
// their item picker's 0x7E30 (panel_row.cpp).
constexpr int32_t kFirstId = 0x7E40;
constexpr int kMaxRows = 8;

struct Rows {
    const void* livestock = nullptr;   // the livestock template the rows were built for
    UI::Element* column = nullptr;     // the column holding the rows (owned by the panel's tree)
    std::vector<PanelRows::Row> rows;
};

// Each open pasture panel with rows (as the workshop panel's g_rows: raw pointers into the panel's tree).
std::unordered_map<const PastureUI*, Rows> g_pastures;
// Pasture panels opened before their category limit spinner was found: the first layout builds their rows.
std::unordered_set<const PastureUI*> g_pending;

PastureComponent* PastureOf(const PastureUI* panel) {
    Object::Base* source = panel->_dataSource;
    if (!::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())) return nullptr;
    return static_cast<ComponentEntity*>(source)->FindComponent<PastureComponent>();
}

// What a livestock gives: its slaughter yield, then its byproduct, each item once (FirstOfEach).
std::vector<const ComponentDescription*> ProductsOf(const ComponentDescription* livestock) {
    std::vector<const ComponentDescription*> out;
    if (livestock == nullptr) return out;
    std::vector<const void*> items;
    const AnimalDescription* animal = livestock->FindDescription<AnimalDescription>();
    if (animal != nullptr) {
        for (int32_t i = 0; i < animal->_rawMaterialCount; ++i) {
            items.push_back(PanelRows::AsItem(animal->_rawMaterials[i]));
        }
    }
    const LivestockDescription* herd = livestock->FindDescription<LivestockDescription>();
    if (herd != nullptr) {
        items.push_back(PanelRows::AsItem(herd->_byproduct));
    }
    for (int32_t i : FirstOfEach(items, kMaxRows)) {
        out.push_back(static_cast<const ComponentDescription*>(items[static_cast<size_t>(i)]));
    }
    return out;
}

// Fills `column` (a new, empty column) with a row per item `livestock` gives.
void Fill(UI::SpinnerElement* category, const ComponentDescription* livestock, UI::Element* column,
          std::vector<PanelRows::Row>& out) {
    const UI::Dialog* window = PanelRows::WindowDialog();
    for (const ComponentDescription* product : ProductsOf(livestock)) {
        if (static_cast<int>(out.size()) >= kMaxRows) break;
        const int32_t checkId = kFirstId + 2 * static_cast<int32_t>(out.size());
        PanelRows::Row row;
        Object::InternalPtr<UI::Element> rowElem;
        if (!PanelRows::MakeRow(category, window, checkId, checkId + 1, row, &rowElem)) break;
        PanelRows::Append(column, &rowElem);
        const RawMaterialDescription* material = product->FindDescription<RawMaterialDescription>();
        UI::String name(Object::ExternalPtr<UI::StringTable>(material->_stringTable), material->_name);
        PanelRows::PointAt(row, product, System::String(name), PanelRows::CategoryLimit(ResourceCategory(product)));
        out.push_back(row);
    }
}

// Puts a column of rows for `livestock` (empty for none) in the place of the panel's current one, or the
// first one under the slider. `rows` follows what is in the panel: `rows.livestock` changes only once the new
// column is placed, so a column that did not load is tried again at the next layout.
void Rebuild(PastureUI* panel, UI::SpinnerElement* category, const ComponentDescription* livestock, Rows& rows) {
    Object::InternalPtr<UI::Element> column;
    if (!PanelRows::MakeColumn(PanelRows::WindowDialog(), &column)) {
        return;
    }
    std::vector<PanelRows::Row> fresh;
    if (livestock != nullptr) {
        Fill(category, livestock, column.Get(), fresh);
    }
    bool placed = false;
    if (rows.column != nullptr) {
        placed = PanelRows::Replace(rows.column, &column);   // frees the old column and its rows
    } else {
        UI::Element* target = panel->slider;
        UI::Element* holder = target->_parent;
        placed = PanelRows::AddUnder(holder, target, PanelRows::WindowDialog(), &column);
        if (!placed) {
            PanelRows::Append(holder, &column);
            placed = true;
        }
    }
    if (!placed) {
        // The old column stays (Replace found no slot for it): its rows keep following the old livestock and
        // the next layout tries again.
        PanelRows::Discard(column);
        return;
    }
    rows.livestock = livestock;
    rows.column = column.Get();
    rows.rows.swap(fresh);
}

// Builds the panel's rows when its category limit spinner and its slider row are there; else returns false,
// or with `last` logs the panel and gives it no rows.
bool Open(PastureUI* panel, bool last) {
    UI::SpinnerElement* category = PanelRows::CategorySpinner(panel->_dialog);
    UI::Element* holder = panel->slider != nullptr ? panel->slider->_parent : nullptr;
    if (category == nullptr || holder == nullptr) {
        if (!last) return false;
        static int logged = 0;
        if (logged < 4) {
            ++logged;
            std::fprintf(stderr, "[ItemLimits] the pasture panel has no category limit spinner (editLimit %p) or "
                         "no slider row (%p): no item limit rows\n", static_cast<void*>(category),
                         static_cast<void*>(holder));
        }
        return true;
    }
    const PastureComponent* pasture = PastureOf(panel);
    Rows& rows = g_pastures[panel];
    Rebuild(panel, category, pasture != nullptr ? pasture->livestockType : nullptr, rows);
    return true;
}

}  // namespace

// The category limit's spinner belongs to the panel's ResourceLimitUI, a controller that may come after
// PastureUI's: when OnCreated does not find it, the first layout looks again.
void PastureOpenedSlow(PastureUI* panel) {
    g_pastures.erase(panel);
    g_pending.erase(panel);
    if (!Open(panel, false)) {
        g_pending.insert(panel);
    }
}

void PastureLayoutSlow(PastureUI* panel) {
    if (g_pending.erase(panel) != 0) {
        Open(panel, true);
        return;
    }
    const auto it = g_pastures.find(panel);
    if (it == g_pastures.end()) return;
    Rows& rows = it->second;
    const PastureComponent* pasture = PastureOf(panel);
    const ComponentDescription* livestock = pasture != nullptr ? pasture->livestockType : nullptr;
    if (livestock != rows.livestock) {
        // The combo picked another livestock: a new column of rows in the old one's place.
        UI::SpinnerElement* category = PanelRows::CategorySpinner(panel->_dialog);
        if (category != nullptr) {
            Rebuild(panel, category, livestock, rows);
        }
        return;
    }
    for (PanelRows::Row& row : rows.rows) {
        PanelRows::Follow(row);   // another panel (or a load) may have changed an item's limit
    }
}

bool PastureEventSlow(PastureUI* panel, UI::EventId msg, int32_t id) {
    if (id < kFirstId || id >= kFirstId + 2 * kMaxRows) return false;
    const auto it = g_pastures.find(panel);
    if (it == g_pastures.end()) return false;
    const size_t index = static_cast<size_t>((id - kFirstId) / 2);
    if (index < it->second.rows.size()) {
        PanelRows::Apply(it->second.rows[index], msg, ((id - kFirstId) % 2) == 0);
    }
    return true;
}

namespace PanelRows {

void ForgetPastureRows() {
    g_pastures.clear();
    g_pending.clear();
}

}  // namespace PanelRows

}  // namespace ItemLimits
}  // namespace Ext
