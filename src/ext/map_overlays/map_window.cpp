// ============================================================================
// src/ext/map_overlays/map_window.cpp -- ext.map_overlays: the overlay tabs in retail's map window
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §1, §2; src/ext/ui/Dialog/MapOverlays.rsc; map_window.h.
//
// RETAIL'S WINDOW, BY REFERENCE. The map window is the UtilityMap tool's: ToolUI (StandardDialog.rsc's
// controller) reads the entity's UIDescription and injects MapUI with Dialog/Map.rsc:content at userGroup0
// (UIDescription::AddControllers, toolui.cpp). Nothing of that is copied, edited or moved. While the row is
// on, the seam after AddControllers calls AddMapWindowTabs, which injects the overlay tabs by the same
// mechanism: DialogInstance::AddController with type 0 (element only, retail's own "no controller" arm)
// puts tabMapOverlays at userGroup1, the next group of StandardDialog's page, under the map. A controller of
// ours (not a reflected type: built here, never by the factory) is placed in the dialog's controller list,
// so the dialog updates it every frame and deletes it with the window.
//
// The map keeps its place and shows in every tab: an overlay is drawn over MapUI's own image (the render
// side, MapWindowUpdate), so a tab only adds its controls under the map. The first tab, Map, has an empty
// page and selects View::Map: retail's map with no overlay (MapWindowUpdate puts retail's sprite back).
//
// THE CONTROLS are read once a frame (no element events are needed, so none of our ids reaches ToolUI's or
// MapUI's handlers; they still get ids of their own, away from ToolUI's 0 / 1 and MapUI's 1000). Every change
// becomes one SetSelection. Per view the window remembers what was picked (category, item, kind, months);
// the scale and the terrain check box are one setting for every view.
//
// THE RESOURCE PICKER. Categories are RawMaterialDescription::_flags masks: All, Food (the edible bit) and
// Fuel (the fuel bit), then the trading post's resource groups (TradeDescription::_pairs: retail's own flag
// masks, sprites and names, read from the running game, so the bit values are the game's and not guessed
// here), then Other: the bits of the running game's materials that none of those name (a mod's flags). The
// item combo lists the materials of the picked category, each with its own sprite: every loaded
// RawMaterialDescription (the factory walk ext.restricted_foods uses), so a mod's material appears.
//
// Ownership: MAPOVERLAYS.md §6 (the UI agent).
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "ui/ComboElement.h"
#include "ui/SliderElement.h"
#include "ui/TabElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogController.h"
#include "ui/DialogControllerConfig.h"
#include "ui/DialogInstance.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "Component/RawMaterialDescription.h"
#include "Component/TradeDescription.h"
#include "Dialog/MapUI.h"

#include "ext/map_overlays/map_overlays.h"
#include "ext/map_overlays/map_window.h"
#include "ext/map_overlays/overlay_math.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace Ext {
namespace MapOverlays {

namespace {

constexpr const char* kTabsRsc      = "Dialog/MapOverlays.rsc:tabMapOverlays";
constexpr const char* kTabButtonRsc = "Dialog/MapOverlays.rsc:tabButtonMapOverlays";
constexpr const char* kInsertAt     = "userGroup1";   // StandardDialog.rsc: the group under userGroup0 (the map)

constexpr int32_t kIdBase = 7300;      // our elements' ids: kIdBase + 16 * tab + n (ToolUI uses 0 / 1, MapUI 1000)
constexpr int kScaleSteps = 10;        // the scale slider: 0 (Fine, Selection.scale 0) .. 10 (Coarse, 1)
constexpr int kViewCount = static_cast<int>(View::Count);

// What a tab shows, in View order (overlay_core.h): its page, its caption and tooltip keys.
enum class PageKind : uint8_t { Map, Resource, Kind, Traffic };
struct TabSpec {
    View view;
    PageKind page;
    const char* caption;
    const char* tip;
};
constexpr TabSpec kTabs[kViewCount] = {
    { View::Map,         PageKind::Map,      "MapOverlaysTabMap",         "MapOverlaysTabMapTip" },
    { View::Production,  PageKind::Resource, "MapOverlaysTabProduction",  "MapOverlaysTabProductionTip" },
    { View::Consumption, PageKind::Resource, "MapOverlaysTabConsumption", "MapOverlaysTabConsumptionTip" },
    { View::Stock,       PageKind::Resource, "MapOverlaysTabStock",       "MapOverlaysTabStockTip" },
    { View::Services,    PageKind::Kind,     "MapOverlaysTabServices",    "MapOverlaysTabServicesTip" },
    { View::Traffic,     PageKind::Traffic,  "MapOverlaysTabTraffic",     "MapOverlaysTabTrafficTip" },
    { View::Citizens,    PageKind::Kind,     "MapOverlaysTabCitizens",    "MapOverlaysTabCitizensTip" },
    { View::Natural,     PageKind::Kind,     "MapOverlaysTabNatural",     "MapOverlaysTabNaturalTip" },
};

const char* PageRsc(PageKind kind) {
    switch (kind) {
    case PageKind::Resource:
        return "Dialog/MapOverlays.rsc:pageMapOverlaysResource";
    case PageKind::Kind:
        return "Dialog/MapOverlays.rsc:pageMapOverlaysKind";
    case PageKind::Traffic:
        return "Dialog/MapOverlays.rsc:pageMapOverlaysTraffic";
    case PageKind::Map:
    default:
        return "Dialog/MapOverlays.rsc:pageMapOverlaysMap";
    }
}

// The kind combos' items, in the enums' order (overlay_core.h).
constexpr const char* kServiceKeys[] = {
    "MapOverlaysServiceMarket", "MapOverlaysServiceWell", "MapOverlaysServiceHerbalist",
    "MapOverlaysServiceChapel", "MapOverlaysServiceSchool", "MapOverlaysServiceTavern",
    "MapOverlaysServiceTownHall",
};
constexpr const char* kCitizenKeys[] = {
    "MapOverlaysCitizenHappiness", "MapOverlaysCitizenHealth", "MapOverlaysCitizenFood",
    "MapOverlaysCitizenWarmth", "MapOverlaysCitizenAge",
};
constexpr const char* kNaturalKeys[] = {
    "MapOverlaysNaturalTrees", "MapOverlaysNaturalStone", "MapOverlaysNaturalIron",
    "MapOverlaysNaturalWildlife", "MapOverlaysNaturalFish",
};
static_assert(sizeof(kServiceKeys) / sizeof(kServiceKeys[0]) == static_cast<size_t>(ServiceKind::Count), "");
static_assert(sizeof(kCitizenKeys) / sizeof(kCitizenKeys[0]) == static_cast<size_t>(CitizenMeasure::Count), "");
static_assert(sizeof(kNaturalKeys) / sizeof(kNaturalKeys[0]) == static_cast<size_t>(NaturalKind::Count), "");

// What the window remembers per view while the game runs (a new town starts from the defaults).
struct ViewMemory {
    uint32_t categoryFlags = 0;
    std::string resourcePath;
    uint8_t kind = 0;
    int months = kMonths;
};
std::array<ViewMemory, kViewCount> g_memory;
uint32_t g_memoryTown = ~0u;   // the lock epoch g_memory belongs to

// ---- resources ---------------------------------------------------------------------------------------------

UI::ElementDescription* LoadDescription(const char* path) {
    if (::Object::ExternalFactory::_singleton == nullptr) {
        return nullptr;
    }
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[MapOverlays] %s did not load as an ElementDescription (%p)\n", path,
                     static_cast<const void*>(ext));
        return nullptr;
    }
    // AddTab / AddController take the description as an ExternalPtr (non-const); the factory hands out const.
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

// A text of our string table, for a combo item (AddItem takes the text itself, not a key).
System::String OurText(const char* key) {
    const UI::String s = Toolbar::OurString(key);
    if (!s.IsValid()) {
        return System::String(key);
    }
    return static_cast<System::String>(s);
}

// "RabbitMeat" -> "Rabbit Meat": the fallback when a material's own string table does not name it.
std::string PrettyWords(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        const char p = i ? in[i - 1] : 0;
        const bool up = std::isupper(static_cast<unsigned char>(c)) != 0;
        const bool prevLowOrDigit =
            std::islower(static_cast<unsigned char>(p)) || std::isdigit(static_cast<unsigned char>(p));
        if (i && up && prevLowOrDigit) {
            out.push_back(' ');
        }
        out.push_back(c == '_' ? ' ' : c);
    }
    return out;
}

// A text of a game string table by name; empty when the table does not have it.
std::string TableText(const UI::StringTable* table, const System::String& name) {
    if (table == nullptr || !::Object::IsDerivedFromNullSafe(table, UI::StringTable::TypeId())) {
        return std::string();
    }
    if (name._data == nullptr || name._data[0] == 0) {
        return std::string();
    }
    const int32_t index = table->GetIndexFromName(name);
    if (index < 0) {
        return std::string();
    }
    const System::String text = table->GetText(index);
    return text._data != nullptr ? std::string(text._data) : std::string();
}

// One material of the running game.
struct Material {
    const RawMaterialDescription* mat = nullptr;
    std::string path;      // its template path: what Selection::resourcePath names
    std::string display;   // the name the game shows, else the internal name in words
    uint32_t flags = 0;
};

// The material's icon: the small one the game's lists use, else the full-size one.
UI::Sprite MaterialSprite(const RawMaterialDescription* m) {
    if (m == nullptr || m->_spriteSheet == nullptr) {
        return UI::Sprite();
    }
    const System::String* name = nullptr;
    if (m->_nameSmall._data != nullptr && m->_nameSmall._data[0] != 0) {
        name = &m->_nameSmall;
    } else if (m->_name._data != nullptr && m->_name._data[0] != 0) {
        name = &m->_name;
    }
    if (name == nullptr) {
        return UI::Sprite();
    }
    UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(m->_spriteSheet), *name);
    return sprite.IsValid() ? sprite : UI::Sprite();
}

// Every loaded RawMaterialDescription, once per template path (case-insensitive), by display name.
std::vector<Material> CollectMaterials() {
    std::vector<Material> out;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return out;
    }
    const uint32_t tid = RawMaterialDescription::TypeId();
    std::vector<std::string> seen;
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 20000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        const RawMaterialDescription* m = static_cast<const RawMaterialDescription*>(prev);
        if (m->_name._data == nullptr || m->_name._data[0] == 0) {
            continue;
        }
        const System::Filename path = prev->GetPath();
        if (path._data == nullptr || path._data[0] == 0) {
            continue;
        }
        Material entry;
        entry.mat = m;
        entry.path = path._data;
        const std::string key = Toolbar::Lower(entry.path.c_str());
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        entry.flags = m->_flags;
        entry.display = TableText(m->_stringTable, m->_name);
        if (entry.display.empty()) {
            entry.display = PrettyWords(m->_name._data);
        }
        out.push_back(entry);
    }
    std::sort(out.begin(), out.end(), [](const Material& a, const Material& b) {
        return Toolbar::Lower(a.display.c_str()) < Toolbar::Lower(b.display.c_str());
    });
    return out;
}

// One category of the resource picker.
struct Category {
    uint32_t flags = 0;    // 0 = every material
    System::String text;
    UI::Sprite sprite;
};

// The first material of `flags` (by name): the icon of a category that has none of its own.
UI::Sprite FirstSprite(const std::vector<Material>& materials, uint32_t flags) {
    for (const Material& m : materials) {
        if (flags != 0 && (m.flags & flags) == 0) {
            continue;
        }
        UI::Sprite s = MaterialSprite(m.mat);
        if (s.IsValid()) {
            return s;
        }
    }
    return UI::Sprite();
}

// The trading post's resource groups (the first TradeDescription with any): retail's own flag masks, icons
// and names.
const TradeDescription* FindTradeGroups() {
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return nullptr;
    }
    const uint32_t tid = TradeDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 1000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        const TradeDescription* t = static_cast<const TradeDescription*>(prev);
        if (t->_pairs != nullptr && t->_pairCount > 0) {
            return t;
        }
    }
    return nullptr;
}

std::vector<Category> CollectCategories(const std::vector<Material>& materials) {
    std::vector<Category> out;
    Category all;
    all.text = OurText("MapOverlaysAllGoods");
    all.sprite = FirstSprite(materials, 0);
    out.push_back(all);

    Category food;
    food.flags = RawMaterialFlags::Edible;
    food.text = OurText("MapOverlaysFood");
    food.sprite = FirstSprite(materials, food.flags);
    out.push_back(food);

    Category fuel;
    fuel.flags = RawMaterialFlags::Fuel;
    fuel.text = OurText("MapOverlaysFuel");
    fuel.sprite = FirstSprite(materials, fuel.flags);
    out.push_back(fuel);

    uint32_t named = food.flags | fuel.flags;
    if (const TradeDescription* trade = FindTradeGroups()) {
        for (int32_t i = 0; i < trade->_pairCount; ++i) {
            const TradeGoodsCategory& group = trade->_pairs[i];
            if (group.rawMaterialFlags == 0) {
                continue;
            }
            Category c;
            c.flags = group.rawMaterialFlags;
            const std::string text = TableText(trade->_ref28, group.textName);
            c.text = System::String(text.empty() ? "?" : text.c_str());
            if (trade->_ref20 != nullptr && group.spriteName._data != nullptr) {
                c.sprite = UI::Sprite(Object::ExternalPtr<UI::SpriteSheet>(trade->_ref20), group.spriteName);
            }
            if (!c.sprite.IsValid()) {
                c.sprite = FirstSprite(materials, c.flags);
            }
            named |= c.flags;
            out.push_back(c);
        }
    } else {
        std::fprintf(stderr, "[MapOverlays] no trading post resource groups: the picker shows All, Food, Fuel\n");
    }

    // Other: the bits the running game's materials carry that no category above names (a mod's flags).
    uint32_t other = 0;
    for (const Material& m : materials) {
        other |= m.flags & ~named;
    }
    if (other != 0) {
        Category c;
        c.flags = other;
        c.text = OurText("MapOverlaysOther");
        c.sprite = FirstSprite(materials, other);
        out.push_back(c);
    }
    return out;
}

// ---- the legend --------------------------------------------------------------------------------------------

// Ramp() is 0xAABBGGRR (the overlay's pixel order); an element colour is 0xAARRGGBB, here opaque.
uint32_t SwatchColour(float t) {
    const uint32_t c = Ramp(t);
    const uint32_t r = c & 0xffu;
    const uint32_t g = (c >> 8) & 0xffu;
    const uint32_t b = (c >> 16) & 0xffu;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

const char* UnitKey(View view, uint8_t kind) {
    switch (view) {
    case View::Production:
    case View::Consumption:
    case View::Stock:
        return "MapOverlaysUnitItems";
    case View::Traffic:
        return "MapOverlaysUnitSteps";
    case View::Citizens:
        return kind == static_cast<uint8_t>(CitizenMeasure::Age) ? "MapOverlaysUnitCount" : "MapOverlaysUnitPercent";
    default:
        return "MapOverlaysUnitCount";
    }
}

std::string FormatValue(float v) {
    char buf[32];
    if (v >= 10.0f || v == 0.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(v));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    }
    return std::string(buf);
}

bool SameSelection(const Selection& a, const Selection& b) {
    return a.view == b.view && a.categoryFlags == b.categoryFlags && a.resourcePath == b.resourcePath &&
           a.kind == b.kind && a.months == b.months && a.scale == b.scale && a.onTerrain == b.onTerrain;
}

int RoundSlider(const UI::SliderElement* slider, int lo, int hi) {
    const int v = static_cast<int>(std::floor(slider->_value + 0.5f));
    return std::max(lo, std::min(hi, v));
}

// ---- the controller ----------------------------------------------------------------------------------------

// One tab's page and the controls in it (null when the page has none of that kind).
struct Page {
    const TabSpec* spec = nullptr;
    UI::Element* root = nullptr;
    UI::ComboElement* category = nullptr;
    UI::ComboElement* resource = nullptr;
    UI::ComboElement* kind = nullptr;
    UI::Element* monthsRow = nullptr;
    UI::SliderElement* months = nullptr;
    UI::SliderElement* scale = nullptr;
    UI::TextElement* legendMax = nullptr;
    UI::CheckElement* terrain = nullptr;
    std::vector<int> resourceItems;   // resource combo item -> index into the materials (item 0: -1, all)
    int32_t categoryItem = -1;        // the items last read
    int32_t resourceItem = -1;
    int32_t kindItem = -1;
};

class MapWindowController final : public UI::DialogController {
public:
    explicit MapWindowController(MapUI* map) : _map(map) {
        _dataSource = nullptr;
    }

    // The dialog deletes its controllers in list order, and this one sits before the MapUI (Attach), so the
    // MapUI is still whole here: retail's sprite goes back on imageOverlay before ~MapUI deletes the sheets.
    ~MapWindowController() override {
        if (_map != nullptr) {
            MapWindowClosed(*_map);
        }
    }

    bool Build(UI::TabElement* tabs);
    void OnUpdate(const System::Time& time) override;

private:
    void BuildPage(Page& page, int tab);
    void FillResources(Page& page, uint32_t categoryFlags, const std::string& selectPath);
    void ShowTab(int tab);
    void ReadPage(Page& page, Selection& s);
    void UpdateLegend(Page& page, const Selection& s);

    MapUI* _map = nullptr;            // the window's MapUI (deleted after this controller)
    UI::TabElement* _tabs = nullptr;  // owned by the dialog's element tree
    std::vector<Page> _pages;
    std::vector<Material> _materials;
    std::vector<Category> _categories;
    int32_t _shownTab = -1;
    float _legendValue = -1.0f;       // the legend text last written, and for which view / kind
    int _legendView = -1;
    int _legendKind = -1;
};

bool MapWindowController::Build(UI::TabElement* tabs) {
    _tabs = tabs;
    UI::ElementDescription* button = LoadDescription(kTabButtonRsc);
    if (button == nullptr) {
        return false;
    }
    _materials = CollectMaterials();
    _categories = CollectCategories(_materials);

    for (int tab = 0; tab < kViewCount; ++tab) {
        const TabSpec& spec = kTabs[tab];
        UI::ElementDescription* content = LoadDescription(PageRsc(spec.page));
        if (content == nullptr) {
            return false;
        }
        Object::InternalPtr<UI::Element> root =
            tabs->AddTab(Object::ExternalPtr<UI::ElementDescription>(button),
                         Object::ExternalPtr<UI::ElementDescription>(content));
        if (root.Get() == nullptr) {
            std::fprintf(stderr, "[MapOverlays] AddTab failed for tab %d\n", tab);
            return false;
        }
        // The tab's button is the child just before its page (TabElement.h: button at 3 + 2i).
        const int index = tabs->_tabCount - 1;
        UI::Element** kids = tabs->_children.GetData();
        const int nKids = tabs->_children.GetSize();
        if (3 + 2 * index < nKids && kids[3 + 2 * index] != nullptr) {
            UI::Element* tabButton = kids[3 + 2 * index];
            tabButton->SetElementId(kIdBase + 16 * tab);
            tabButton->SetToolTip(Toolbar::OurString(spec.tip));
            const int nb = tabButton->ChildCount();
            for (int k = 0; k < nb; ++k) {
                Toolbar::SetTableText(tabButton->Child(k).Get(), spec.caption);
            }
        }
        Page page;
        page.spec = &spec;
        page.root = root.Get();
        _pages.push_back(page);
        BuildPage(_pages.back(), tab);
    }

    // The window opens on the view that shows (a reopened window keeps it; a new town starts on Map).
    const int current = static_cast<int>(CurrentSelection().view);
    tabs->SetActiveTab(current >= 0 && current < kViewCount ? current : 0);
    _shownTab = -1;   // OnUpdate shows it
    std::fprintf(stderr, "[MapOverlays] map window: %d tabs, %d materials, %d categories\n", kViewCount,
                 static_cast<int>(_materials.size()), static_cast<int>(_categories.size()));
    return true;
}

void MapWindowController::BuildPage(Page& page, int tab) {
    UI::Element* root = page.root;
    const int32_t ids = kIdBase + 16 * tab;
    const ViewMemory& memory = g_memory[static_cast<size_t>(tab)];

    page.category = FindElement<UI::ComboElement>(root, "comboMapOverlaysCategory");
    page.resource = FindElement<UI::ComboElement>(root, "comboMapOverlaysResource");
    page.kind = FindElement<UI::ComboElement>(root, "comboMapOverlaysKind");
    page.monthsRow = root->FindElement(System::String("rowMapOverlaysMonths")).Get();
    page.months = FindElement<UI::SliderElement>(root, "sliderMapOverlaysMonths");
    page.scale = FindElement<UI::SliderElement>(root, "sliderMapOverlaysScale");
    page.legendMax = FindElement<UI::TextElement>(root, "textMapOverlaysLegendMax");
    page.terrain = FindElement<UI::CheckElement>(root, "checkMapOverlaysTerrain");

    if (page.category != nullptr) {
        page.category->SetElementId(ids + 1);
        page.category->RemoveAllItems();
        int32_t select = 0;
        for (size_t i = 0; i < _categories.size(); ++i) {
            const Category& c = _categories[i];
            page.category->AddItem(c.sprite, c.text, nullptr);
            if (c.flags == memory.categoryFlags) {
                select = static_cast<int32_t>(i);
            }
        }
        page.category->SetCurrentItem(select);
        page.categoryItem = page.category->GetCurrentItem();
        const uint32_t flags = _categories.empty() ? 0 : _categories[static_cast<size_t>(select)].flags;
        FillResources(page, flags, memory.resourcePath);
    }
    if (page.kind != nullptr) {
        page.kind->SetElementId(ids + 3);
        page.kind->RemoveAllItems();
        const char* const* keys = kServiceKeys;
        int count = static_cast<int>(ServiceKind::Count);
        if (page.spec->view == View::Citizens) {
            keys = kCitizenKeys;
            count = static_cast<int>(CitizenMeasure::Count);
        } else if (page.spec->view == View::Natural) {
            keys = kNaturalKeys;
            count = static_cast<int>(NaturalKind::Count);
        }
        for (int i = 0; i < count; ++i) {
            page.kind->AddItem(UI::Sprite(), OurText(keys[i]), nullptr);
        }
        page.kind->SetCurrentItem(memory.kind < count ? memory.kind : 0);
        page.kindItem = page.kind->GetCurrentItem();
    }
    // Stock is an instant view: no months.
    if (page.monthsRow != nullptr && page.spec->view == View::Stock) {
        page.monthsRow->SetFlagsOnOff(UI::kElementFlagHidden, 0);
    }
    if (page.months != nullptr) {
        page.months->SetElementId(ids + 4);
        page.months->_minimum = 1.0f;
        page.months->_maximum = static_cast<float>(kMonths);
        page.months->_steps = kMonths - 1;
        page.months->_value = static_cast<float>(std::max(1, std::min(kMonths, memory.months)));
    }
    if (page.scale != nullptr) {
        page.scale->SetElementId(ids + 5);
        page.scale->_minimum = 0.0f;
        page.scale->_maximum = static_cast<float>(kScaleSteps);
        page.scale->_steps = kScaleSteps;
    }
    if (page.terrain != nullptr) {
        page.terrain->SetElementId(ids + 6);
    }
    // The ramp's swatches, 0 excluded (transparent): the colour at the top of each tenth.
    if (UI::Element* ramp = root->FindElement(System::String("ribbonMapOverlaysRamp")).Get()) {
        const int n = ramp->ChildCount();
        for (int i = 0; i < n; ++i) {
            UI::Element* swatch = ramp->Child(i).Get();
            if (swatch != nullptr) {
                swatch->SetColor(SwatchColour(static_cast<float>(i + 1) / static_cast<float>(n)));
            }
        }
    }
}

// The item combo: "All of them", then the category's materials by name, each with its own icon.
void MapWindowController::FillResources(Page& page, uint32_t categoryFlags, const std::string& selectPath) {
    if (page.resource == nullptr) {
        return;
    }
    page.resource->SetElementId(kIdBase + 16 * static_cast<int32_t>(page.spec->view) + 2);
    page.resource->RemoveAllItems();
    page.resourceItems.clear();
    page.resource->AddItem(UI::Sprite(), OurText("MapOverlaysAllOf"), nullptr);
    page.resourceItems.push_back(-1);
    int32_t select = 0;
    const std::string want = Toolbar::Lower(selectPath.c_str());
    for (size_t i = 0; i < _materials.size(); ++i) {
        const Material& m = _materials[i];
        if (categoryFlags != 0 && (m.flags & categoryFlags) == 0) {
            continue;
        }
        if (!want.empty() && Toolbar::Lower(m.path.c_str()) == want) {
            select = static_cast<int32_t>(page.resourceItems.size());
        }
        page.resource->AddItem(MaterialSprite(m.mat), System::String(m.display.c_str()), nullptr);
        page.resourceItems.push_back(static_cast<int>(i));
    }
    page.resource->SetCurrentItem(select);
    page.resourceItem = page.resource->GetCurrentItem();
}

// A tab came to the front: its view is selected, and the shared controls show the shared settings.
void MapWindowController::ShowTab(int tab) {
    _shownTab = tab;
    _legendValue = -1.0f;
    if (tab < 0 || tab >= static_cast<int>(_pages.size())) {
        return;
    }
    Page& page = _pages[static_cast<size_t>(tab)];
    const Selection& current = CurrentSelection();
    if (page.scale != nullptr) {
        page.scale->_value = current.scale * static_cast<float>(kScaleSteps);
    }
    if (page.terrain != nullptr) {
        page.terrain->_checked = current.onTerrain ? 1 : 0;
    }
}

// The page's controls into `s` (the view's memory follows them).
void MapWindowController::ReadPage(Page& page, Selection& s) {
    ViewMemory& memory = g_memory[static_cast<size_t>(page.spec->view)];
    if (page.category != nullptr) {
        const int32_t item = page.category->GetCurrentItem();
        if (item != page.categoryItem && item >= 0 && item < static_cast<int32_t>(_categories.size())) {
            page.categoryItem = item;
            memory.categoryFlags = _categories[static_cast<size_t>(item)].flags;
            memory.resourcePath.clear();
            FillResources(page, memory.categoryFlags, memory.resourcePath);
        }
    }
    if (page.resource != nullptr) {
        const int32_t item = page.resource->GetCurrentItem();
        if (item != page.resourceItem && item >= 0 && item < static_cast<int32_t>(page.resourceItems.size())) {
            page.resourceItem = item;
            const int m = page.resourceItems[static_cast<size_t>(item)];
            memory.resourcePath = m < 0 ? std::string() : _materials[static_cast<size_t>(m)].path;
        }
    }
    if (page.kind != nullptr) {
        const int32_t item = page.kind->GetCurrentItem();
        if (item != page.kindItem && item >= 0) {
            page.kindItem = item;
            memory.kind = static_cast<uint8_t>(item);
        }
    }
    if (page.months != nullptr && page.spec->view != View::Stock) {
        memory.months = RoundSlider(page.months, 1, kMonths);
    }
    s.view = page.spec->view;
    s.categoryFlags = memory.categoryFlags;
    s.resourcePath = memory.resourcePath;
    s.kind = memory.kind;
    s.months = memory.months;
    if (page.scale != nullptr) {
        s.scale = static_cast<float>(RoundSlider(page.scale, 0, kScaleSteps)) / static_cast<float>(kScaleSteps);
    }
    if (page.terrain != nullptr) {
        s.onTerrain = page.terrain->_checked != 0;
    }
}

// The value at full colour, in the view's unit; written only when it changes.
void MapWindowController::UpdateLegend(Page& page, const Selection& s) {
    if (page.legendMax == nullptr) {
        return;
    }
    const float value = LegendMax();
    const int view = static_cast<int>(s.view);
    if (value == _legendValue && view == _legendView && static_cast<int>(s.kind) == _legendKind) {
        return;
    }
    _legendValue = value;
    _legendView = view;
    _legendKind = static_cast<int>(s.kind);
    Toolbar::SetTableText(page.legendMax, UnitKey(s.view, s.kind));
    page.legendMax->SetParameter(0, System::String(FormatValue(value).c_str()));
}

void MapWindowController::OnUpdate(const System::Time& /*time*/) {
    if (_tabs == nullptr || _map == nullptr) {
        return;
    }
    if (g_memoryTown != LockEpoch()) {
        // a new town while the window stayed open: the memory of the last one does not apply
        g_memoryTown = LockEpoch();
        g_memory = std::array<ViewMemory, kViewCount>();
    }
    const int32_t tab = _tabs->GetActiveTab();
    if (tab != _shownTab) {
        ShowTab(tab);
    }
    if (tab >= 0 && tab < static_cast<int32_t>(_pages.size())) {
        Page& page = _pages[static_cast<size_t>(tab)];
        Selection s = CurrentSelection();
        ReadPage(page, s);
        if (!SameSelection(s, CurrentSelection())) {
            SetSelection(s);
        }
        MapWindowUpdate(*_map);
        UpdateLegend(page, s);
    } else {
        MapWindowUpdate(*_map);
    }
}

}  // namespace

void AddMapWindowTabs(UI::DialogInstance* dialog, Object::Base* dataSource) {
    if (dialog == nullptr) {
        return;
    }
    // Only the map window: the one StandardDialog window whose UIDescription added a MapUI.
    UI::DialogController* found = dialog->GetController(MapUI::TypeId()).Get();
    if (found == nullptr) {
        return;
    }
    MapUI* map = static_cast<MapUI*>(found);
    if (g_memoryTown != LockEpoch()) {
        g_memoryTown = LockEpoch();
        g_memory = std::array<ViewMemory, kViewCount>();
    }

    // The element, the way UIDescription::AddControllers injects Map.rsc:content: AddController with type 0 is
    // retail's "element only" arm (dialog_create.cpp).
    UI::ElementDescription* tabsDesc = LoadDescription(kTabsRsc);
    if (tabsDesc == nullptr) {
        return;
    }
    dialog->AddController(0, dataSource, Object::ExternalPtr<UI::DialogControllerConfig>(),
                          Object::ExternalPtr<UI::ElementDescription>(tabsDesc), System::String(kInsertAt));
    UI::Element* tabsElement = dialog->FindElement(System::String("tabMapOverlays")).Get();
    if (!::Object::IsDerivedFromNullSafe(tabsElement, UI::TabElement::TypeId())) {
        std::fprintf(stderr, "[MapOverlays] the map window has no %s group for the tabs (%p): retail's window\n",
                     kInsertAt, static_cast<void*>(tabsElement));
        return;
    }

    void* mem = System::MemoryAllocator::MemAlloc(sizeof(MapWindowController), 8);
    if (mem == nullptr) {
        return;
    }
    MapWindowController* controller = ::new (mem) MapWindowController(map);
    controller->_dialog = dialog;
    controller->_dataSource = dataSource;
    controller->_container = tabsElement;
    if (!controller->Build(static_cast<UI::TabElement*>(tabsElement))) {
        std::fprintf(stderr, "[MapOverlays] the overlay tabs did not build: the map window has the map only\n");
    }

    // Into the dialog's list just before the MapUI: updated with the window, deleted before the MapUI.
    Collection::Array<UI::DialogController*>& list = dialog->_controllers;
    const int32_t n = list.GetSize();
    list.SetSize(n + 1, controller);
    UI::DialogController** data = list.GetData();
    int32_t at = n;
    for (int32_t i = 0; i < n; ++i) {
        if (data[i] == map) {
            at = i;
            break;
        }
    }
    for (int32_t i = n; i > at; --i) {
        data[i] = data[i - 1];
    }
    data[at] = controller;
}

}  // namespace MapOverlays
}  // namespace Ext
