// ============================================================================
// src/ext/trader_request/panel_row.cpp -- ext.trader_request's row on the trading post window's Orders tab
// ============================================================================
// docs/extensions/handins/TRADERREQUEST.md section 4. The trading post window (TradeUI, tradeui.cpp) gets one
// more row on its Orders tab: the text "Request trader (price mult)", a combo of the post's traders with
// "Any trader (1x)" first and each trader as "Food (2x)", and a combo of the mode (next visit / every visit). The text is our own description
// (TraderRequestPanel.rsc:ribbonTraderRequestRow); the two combos are copies of the tab's order combo
// ("comboOrder"), made from its description, so they look exactly like it in the retail window or a mod's.
// A vertical column (ribbonTraderRequestColumn) takes the slot of the order combo's parent and holds that
// parent, then our row, as ext.item_limits' row sits under a workshop's Product row. In the retail window
// that parent is the tab's whole page (the combo and the goods table), so the row is the page's last line,
// under the goods table.
//
// The row shows the post's request (trader_request.h): "Any trader" = none. Picking a trader makes the
// request with the mode the second combo shows; picking "Any trader" removes it; changing the mode changes a
// request that exists. The row follows the list at every layout, so a "next visit" request an arrival used
// up goes back to "Any trader" while the window is open. Unlike the order combo, the row works with no
// trader docked: a request is for a boat that has not arrived yet.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/ComboElement.h"
#include "ui/RibbonElement.h"
#include "ui/TextElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/Dialog.h"
#include "ui/Sprite.h"
#include "ui/String.h"
#include "ui/StringTable.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/String.h"

#include "Component/TradeComponent.h"
#include "Component/TradeDescription.h"
#include "Dialog/TradeUI.h"
#include "Entity/ComponentEntity.h"

#include "ext/toolbar/toolbar.h"   // OurString
#include "ext/trader_request/trader_request.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace TraderRequest {

namespace {

// Clear of the window's own ids (2000.. 8000+ for its table rows, 0x4E20..0x4E23, 199) and of
// ext.item_limits' 0x7E10 / 0x7E11.
constexpr int32_t kTraderId = 0x7E20;
constexpr int32_t kModeId = 0x7E21;
constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kRowRsc = "Dialog/TraderRequestPanel.rsc:ribbonTraderRequestRow";
constexpr const char* kColumnRsc = "Dialog/TraderRequestPanel.rsc:ribbonTraderRequestColumn";

struct Row {
    UI::ComboElement* trader = nullptr;    // item 0 "Any trader", then the description's traders in order
    UI::ComboElement* mode = nullptr;      // item 0 the next visit, item 1 every visit
    std::vector<std::string> traders;      // TradeMerchant::name per trader item (item i + 1)
};

// Each open window with a row. The elements are owned by the window's element tree, so raw pointers are
// enough; an entry is used only from its own live window's calls and replaced when a window is made at the
// same address again.
std::unordered_map<const TradeUI*, Row> g_rows;

const UI::Dialog* WindowDialog() {
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) return nullptr;
    return static_cast<const UI::Dialog*>(ext);
}

UI::ElementDescription* Template(const char* rsc) {
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(rsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) return nullptr;
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

// Frees an element made by CreateInstance that never joined the window's tree (a parent would own it).
void Discard(Object::InternalPtr<UI::Element>& elem) {
    elem.DeletePtr();
}

// Appends `row` to `holder`. A ribbon goes through AddItem, which grows its extents with its children.
void Append(UI::Element* holder, Object::InternalPtr<UI::Element>* row) {
    if (::Object::IsDerivedFromNullSafe(holder, UI::RibbonElement::TypeId())) {
        static_cast<UI::RibbonElement*>(holder)->AddItem(row);
    } else {
        holder->AddChild(row);
    }
}

// Puts `row` on its own line under `above` (the order combo's row): a vertical column takes that row's slot
// in `holder` (same alignment and stretch, so it sits where the row sat) and holds that row, then `row`.
// The holder owns the column and the column owns both rows. False (nothing changed) when the slot is not
// found. (ext.item_limits' AddUnder, panel_row.cpp there.)
bool AddUnder(UI::Element* holder, UI::Element* above, const UI::Dialog* window,
              Object::InternalPtr<UI::Element>* row) {
    UI::ElementDescription* columnDesc = Template(kColumnRsc);
    if (columnDesc == nullptr) return false;
    UI::Element** slots = holder->_children.GetData();
    const int32_t count = holder->_children.GetSize();
    int32_t slot = -1;
    for (int32_t i = 0; i < count; ++i) {
        if (slots[i] == above) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return false;
    Object::InternalPtr<UI::Element> columnElem;
    columnDesc->CreateInstance(&columnElem, window);
    UI::Element* column = columnElem.Get();
    if (!::Object::IsDerivedFromNullSafe(column, UI::RibbonElement::TypeId())) {
        Discard(columnElem);
        return false;
    }
    const uint32_t stretch = UI::kElementFlagAlignArg1 | UI::kElementFlagAlignArg2;
    column->_alignment = above->_alignment;
    column->_flags = (column->_flags & ~stretch) | (above->_flags & stretch);
    slots[slot] = column;
    column->_parent = holder;
    Object::InternalPtr<UI::Element> aboveElem(above);
    auto* ribbon = static_cast<UI::RibbonElement*>(column);
    ribbon->AddItem(&aboveElem);
    ribbon->AddItem(row);
    holder->SetNeedsSizeCalc();
    return true;
}

System::String OurText(const char* key) {
    const UI::String s = Toolbar::OurString(key);
    if (!s.IsValid()) {
        return System::String(key);
    }
    return static_cast<System::String>(s);
}

TradeComponent* PostOf(const TradeUI* panel) {
    Object::Base* source = panel->_dataSource;
    if (!::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())) return nullptr;
    return static_cast<ComponentEntity*>(source)->FindComponent<TradeComponent>();
}

int32_t PostIdOf(const TradeUI* panel) {
    Object::Base* source = panel->_dataSource;
    if (!::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())) return -1;
    return static_cast<ComponentEntity*>(source)->_entityId;
}

const TradeDescription* DescriptionOf(const TradeComponent* post) {
    Description* desc = post != nullptr ? post->_description : nullptr;
    if (!::Object::IsDerivedFromNullSafe(desc, TradeDescription::TypeId())) return nullptr;
    return static_cast<const TradeDescription*>(desc);
}

// A copy of the order combo with `id`, emptied of the order combo's own items.
UI::ComboElement* MakeCombo(const UI::ComboElement* like, const UI::Dialog* window, int32_t id,
                            Object::InternalPtr<UI::Element>& elem) {
    like->_description->CreateInstance(&elem, window);
    UI::Element* root = elem.Get();
    if (!::Object::IsDerivedFromNullSafe(root, UI::ComboElement::TypeId())) {
        Discard(elem);
        return nullptr;
    }
    auto* combo = static_cast<UI::ComboElement*>(root);
    combo->RemoveAllItems();
    combo->SetElementId(id);
    combo->Enable(true);
    combo->SetToolTip(Toolbar::OurString("TraderRequestTip"));
    return combo;
}

void Select(UI::ComboElement* combo, int32_t item) {
    if (combo->_openDialog == nullptr && combo->GetCurrentItem() != item) {
        combo->SetCurrentItem(item);
    }
}

// Shows the post's request: the trader's item (or "Any trader") and, for a request, its mode.
void Refresh(const TradeUI* panel, Row& row) {
    Request request;
    if (!RequestOf(PostIdOf(panel), request)) {
        Select(row.trader, 0);
        return;
    }
    int32_t item = 0;   // a trader this game does not have reads as "Any trader": retail's roll stands
    for (std::size_t i = 0; i < row.traders.size(); ++i) {
        if (row.traders[i] == request.trader) {
            item = static_cast<int32_t>(i) + 1;
            break;
        }
    }
    Select(row.trader, item);
    Select(row.mode, request.mode == kModeAlways ? 1 : 0);
}

}  // namespace

void PanelOpenedSlow(TradeUI* panel) {
    g_rows.erase(panel);
    const TradeDescription* desc = DescriptionOf(PostOf(panel));
    if (PostIdOf(panel) < 0) {
        // A request is keyed by the post's entity id: a post without one could not keep it.
        std::fprintf(stderr, "[TraderRequest] the trading post has no entity id: no request row\n");
        return;
    }
    UI::ComboElement* order = panel->_comboOrder;
    UI::Element* orderRow = order != nullptr ? order->_parent : nullptr;
    UI::Element* holder = orderRow != nullptr ? orderRow->_parent : nullptr;
    if (desc == nullptr || holder == nullptr || order->_description == nullptr) {
        static int logged = 0;
        if (logged < 4) {
            ++logged;
            std::fprintf(stderr, "[TraderRequest] the trading post window has no order combo row (comboOrder "
                         "%p): no request row\n", static_cast<void*>(order));
        }
        return;
    }
    const UI::Dialog* window = WindowDialog();
    UI::ElementDescription* rowDesc = Template(kRowRsc);
    if (window == nullptr || rowDesc == nullptr) {
        std::fprintf(stderr, "[TraderRequest] %s / %s did not load: no request row\n", kWindowRsc, kRowRsc);
        return;
    }
    Object::InternalPtr<UI::Element> rowElem;
    rowDesc->CreateInstance(&rowElem, window);
    UI::Element* rowRoot = rowElem.Get();
    if (rowRoot == nullptr) {
        return;
    }
    Object::InternalPtr<UI::Element> traderElem;
    Object::InternalPtr<UI::Element> modeElem;
    Row row;
    row.trader = MakeCombo(order, window, kTraderId, traderElem);
    row.mode = MakeCombo(order, window, kModeId, modeElem);
    if (row.trader == nullptr || row.mode == nullptr) {
        Discard(rowElem);
        Discard(traderElem);
        Discard(modeElem);
        return;
    }
    row.trader->AddItem(UI::Sprite(), OurText("TraderRequestAny"), nullptr);
    for (int32_t i = 0; i < desc->_merchantCount; ++i) {
        const TradeMerchant& m = desc->_merchants[i];
        const UI::String shown(Object::ExternalPtr<UI::StringTable>(m.stringTable), m.name);
        const System::String display = static_cast<System::String>(shown);
        const std::string label = ComboLabel(display._data != nullptr ? std::string(display._data) : std::string());
        row.trader->AddItem(UI::Sprite(), System::String(label.c_str()), nullptr);
        row.traders.push_back(m.name._data != nullptr ? std::string(m.name._data) : std::string());
    }
    row.mode->AddItem(UI::Sprite(), OurText("TraderRequestOnce"), nullptr);
    row.mode->AddItem(UI::Sprite(), OurText("TraderRequestAlways"), nullptr);
    row.trader->SetCurrentItem(0);
    row.mode->SetCurrentItem(0);
    Append(rowRoot, &traderElem);
    Append(rowRoot, &modeElem);
    if (!AddUnder(holder, orderRow, window, &rowElem)) {
        std::fprintf(stderr, "[TraderRequest] %s did not load or the order combo's row is not in its holder: "
                     "the row goes at the holder's end\n", kColumnRsc);
        Append(holder, &rowElem);
    }
    Refresh(panel, row);
    g_rows[panel] = row;
}

void PanelLayoutSlow(TradeUI* panel) {
    const auto it = g_rows.find(panel);
    if (it == g_rows.end()) return;
    Refresh(panel, it->second);
}

bool PanelEventSlow(TradeUI* panel, UI::EventId msg, int32_t id) {
    if (id != kTraderId && id != kModeId) return false;
    const auto it = g_rows.find(panel);
    if (it == g_rows.end()) return false;
    if (msg != UI::EventId::SelectionChanged) return true;
    const Row& row = it->second;
    const int32_t post = PostIdOf(panel);
    const int32_t item = row.trader->GetCurrentItem();
    if (item < 1 || static_cast<std::size_t>(item) > row.traders.size()) {
        ClearRequest(post);
        std::fprintf(stderr, "[TraderRequest] post %d: any trader\n", post);
        return true;
    }
    const uint8_t mode = row.mode->GetCurrentItem() == 1 ? kModeAlways : kModeOnce;
    const std::string& trader = row.traders[static_cast<std::size_t>(item) - 1];
    const bool changed = SetRequest(post, trader, mode);
    std::fprintf(stderr, "[TraderRequest] post %d: requests \"%s\" (%s)%s\n", post, trader.c_str(),
                 mode == kModeOnce ? "next visit" : "every visit", changed ? "" : ", unchanged");
    return true;
}

void PanelsForget() {
    g_rows.clear();
}

}  // namespace TraderRequest
}  // namespace Ext
