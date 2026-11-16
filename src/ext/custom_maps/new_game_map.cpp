// ============================================================================
// src/ext/custom_maps/new_game_map.cpp -- ext.custom_maps: the New Game dialog's "Custom map" row
// ============================================================================
// See new_game_map.h. The elements are Dialog/NewGameMap.rsc:buttonLoadMap, :buttonClearMap and :textMapName.
// ============================================================================
#include "ext/custom_maps/new_game_map.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "Component/ComponentDescription.h"
#include "Dialog/NewGameDialog.h"
#include "ext/custom_maps/custom_maps.h"
#include "ext/custom_maps/map_list.h"
#include "ext/custom_maps/map_picker.h"
#include "ext/custom_maps/mod_check.h"
#include "ext/registry.h"
#include "ext/resources/text_provider.h"
#include "object/External.h"
#include "platform_pick_file.h"
#include "platform_settings.h"
#include "object/Internal.h"
#include "system/Filename.h"
#include "system/String.h"
#include "ui/ComboElement.h"
#include "ui/ElementDescription.h"
#include "ui/TableElement.h"
#include "ui/Element.h"
#include "ui/TextElement.h"

namespace Ext {
namespace CustomMaps {

namespace {

constexpr const char* kResource = "Dialog/NewGameMap.rsc";
// the fallback block, under the description box
constexpr const char* kRowObject = "Dialog/NewGameMap.rsc:rowCustomMap";
// the table row: first column
constexpr const char* kLabelObject = "Dialog/NewGameMap.rsc:textCustomMap";
// the table row: second column (the two buttons)
constexpr const char* kCellObject = "Dialog/NewGameMap.rsc:cellCustomMap";
// the table's second row: an empty first cell, then the picked map's name and its mods line
constexpr const char* kBlankObject = "Dialog/NewGameMap.rsc:cellCustomMapBlank";
constexpr const char* kNameCellObject = "Dialog/NewGameMap.rsc:cellCustomMapName";

// -1: not checked yet; 0: unusable; 1: every object loads (the resources do not change during a run)
int g_resourceState = -1;

// Whether every object of our resource loads: a hole in the tree would be read through by the dialog's code,
// so a hole means the game's own dialog opens.
bool ResourceUsable() {
    int& state = g_resourceState;
    if (state >= 0) return state == 1;
    state = 0;
    ::Object::ExternalFactory* const factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) return false;
    const std::string prefix = std::string(kResource) + ":";
    int checked = 0;
    int missing = 0;
    bool sawRoot = false;
    for (int i = 0, n = ::Ext::Resources::TextObjectCount(); i < n; ++i) {
        std::string path;
        if (!::Ext::Resources::TextObjectAt(i, path) || path.compare(0, prefix.size(), prefix) != 0) continue;
        ++checked;
        if (path == prefix + "rowCustomMap") sawRoot = true;
        if (factory->FindOrLoad(System::Filename(path.c_str())) == nullptr) {
            std::fprintf(stderr, "[CustomMaps] New Game: %s did not load\n", path.c_str());
            ++missing;
        }
    }
    state = (sawRoot && checked > 0 && missing == 0) ? 1 : 0;
    std::fprintf(stderr, "[CustomMaps] New Game: %d object(s) of %s checked, %d missing: %s\n", checked,
                 kResource, missing,
                 state == 1 ? "the Custom map row is added" : "the New Game dialog is left as it is");
    return state == 1;
}

UI::Element* Find(NewGameDialog* dialog, const char* name) {
    return dialog == nullptr ? nullptr : dialog->FindElement(System::String(name)).Get();
}

// One of the row's text lines: parameter 0 of the named text element; `hideWhenEmpty` takes the line out of
// the layout when it has nothing to say.
void SetLine(NewGameDialog* dialog, const char* element, const std::string& text, bool hideWhenEmpty) {
    UI::Element* e = Find(dialog, element);
    if (e != nullptr && ::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        static_cast<UI::TextElement*>(e)->SetParameter(0, System::String(text.c_str()));
        if (hideWhenEmpty) e->Hide(text.empty());
    }
}

// The map's name and size (or "None", or what is wrong with a file), and, on a line of its own, the
// mods it needs.
void ShowName(NewGameDialog* dialog, const std::string& text) { SetLine(dialog, "textMapName", text, false); }
void ShowMods(NewGameDialog* dialog, const std::string& text) { SetLine(dialog, "textMapMods", text, true); }

// A picked map brings its own size (world_seam.h BuildSize), so the Map size row has no say while a
// map is picked.
void LockSize(NewGameDialog* dialog, const Map* map) {
    if (dialog == nullptr || dialog->comboTerrainSize == nullptr) return;
    dialog->comboTerrainSize->Enable(map == nullptr);
}

// The mods the picked map lists that are not (fully) loaded, as one short line; "" when nothing is missing or
// the map lists none. Worked out when the map is picked: the mods load once at start, so the answer holds
// until the map is cleared.
std::string g_modsLine;

void ShowPending(NewGameDialog* dialog) {
    const Map* map = Pending();
    if (map == nullptr) {
        ShowName(dialog, "None");
        ShowMods(dialog, "");
    } else {
        const std::string name = map->name.empty() ? std::string("(no name)") : map->name;
        ShowName(dialog, name + ", " + SizeText(map->mapSize));
        ShowMods(dialog, g_modsLine);
    }
    LockSize(dialog, map);
}

// The game's own user folder, <Documents>\Banished (where its saves and screenshots go), when it exists; else
// an empty path.
std::filesystem::path BanishedDir() {
    std::string base;
    if (const char* user = Platform::UserDirOverride()) {
        base = user;
    } else if (Platform::DocumentsDir(base)) {
        base += "\\Banished";
    }
    if (base.empty()) {
        return std::filesystem::path();
    }
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::u8path(base);
    return std::filesystem::is_directory(root, ec) ? root : std::filesystem::path();
}

// Where the maps are kept: <Documents>\Banished\Maps when that folder exists, else the game's own folder (the
// map editor saves to whichever the player chose first, and its Save dialog remembers it); an empty path when
// there is no game folder.
std::filesystem::path MapsDir() {
    const std::filesystem::path root = BanishedDir();
    if (root.empty()) {
        return root;
    }
    std::error_code ec;
    const std::filesystem::path maps = root / "Maps";
    return std::filesystem::is_directory(maps, ec) ? maps : root;
}

// The system's file picker, opened in the maps folder. False when the player cancels.
bool PickFile(std::filesystem::path& out) {
    std::string chosen;
    if (!Platform::PickOpenFile("Load custom map", "Banished maps (*.bmap)", "*.bmap", MapsDir().u8string(),
                                chosen)) {
        return false;
    }
    out = std::filesystem::u8path(chosen);
    return true;
}

// Reads, checks and picks `path` for the next new game; the row shows the result (or what is wrong
// with the file).
void UseFile(NewGameDialog* dialog, const std::filesystem::path& path) {
    Map map;
    std::string why;
    if (!ReadMapFile(path, map, why)) {
        std::fprintf(stderr, "[CustomMaps] New Game: %s: %s\n", path.string().c_str(), why.c_str());
        ShowName(dialog, "Cannot use that file: " + why);
        return;
    }
    std::fprintf(stderr, "[CustomMaps] New Game: picked \"%s\" (%u cells, %zu path(s))\n", map.name.c_str(),
                 static_cast<unsigned>(map.mapSize), map.paths.size());
    // By name, from the game's own mod list (mod_check.h): no resource is loaded from the menu.
    const std::vector<ModCheck> checks = CheckModsInstalled(map, ModIsEnabled);
    g_modsLine = ModsLine(checks);
    for (const ModCheck& check : checks) {
        std::fprintf(stderr, "[CustomMaps] New Game: mod \"%s\": %s\n", check.name.c_str(),
                     check.missing == 0 ? "enabled" : "not enabled (the New Game row names it)");
    }
    SetPending(std::move(map));
    ShowPending(dialog);
}

}  // namespace

// One of our elements, built from its description in the dialog's own resource; null when it does not
// load or build.
Object::InternalPtr<UI::Element> BuildElement(NewGameDialog* dialog, const char* path) {
    Object::InternalPtr<UI::Element> made;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    const ::Object::External* res = factory != nullptr ? factory->FindOrLoad(System::Filename(path))
        : nullptr;
    if (::Object::IsDerivedFromNullSafe(res, UI::ElementDescription::TypeId())) {
        static_cast<const UI::ElementDescription*>(res)->CreateInstance(&made, dialog->_dialog);
    }
    return made;
}

// The row as two more rows of the dialog's two-column table (tableNew, the same element in the game's dialog
// and in MegaMod's), so they line up with every other row whatever the column widths are. Row one: the label
// in the first column and the two buttons in the second, a label and its control on one row, as every other
// row has them. Row two: an empty cell and, under the buttons, the picked map's name and its mods line. The
// table's cells are Element::_children, row-major (`_columnCount * row + col`); TableElement::SetSize would
// delete the cells it holds, so the array grows by two rows here instead, its new slots cleared, and the
// four cells go in through SetChild. False, with the table untouched, when the dialog has no such table or
// any element does not build (the caller then adds the block under the description box).
bool AddTableRow(NewGameDialog* dialog) {
    UI::Element* found = Find(dialog, "tableNew");
    if (found == nullptr || !::Object::IsDerivedFromNullSafe(found, UI::TableElement::TypeId())) {
        return false;
    }
    UI::TableElement* table = static_cast<UI::TableElement*>(found);
    const int cols = table->_columnCount;
    const int rows = table->_rowCount;
    if (cols != 2 || rows < 1 || table->_children.GetSize() != cols * rows) {
        return false;
    }
    Object::InternalPtr<UI::Element> label = BuildElement(dialog, kLabelObject);
    Object::InternalPtr<UI::Element> buttons = BuildElement(dialog, kCellObject);
    Object::InternalPtr<UI::Element> blank = BuildElement(dialog, kBlankObject);
    Object::InternalPtr<UI::Element> name = BuildElement(dialog, kNameCellObject);
    if (label.Get() == nullptr || buttons.Get() == nullptr || blank.Get() == nullptr ||
        name.Get() == nullptr) {
        return false;
    }
    const int added = 2;
    table->_rowCount = rows + added;
    table->_rowSizes.SetSize(rows + added);
    table->_children.SetSize((rows + added) * cols);
    // the new slots, empty until SetChild
    for (int i = rows * cols; i < (rows + added) * cols; ++i) {
        table->_children.GetData()[i] = nullptr;
    }
    table->SetChild(&label, rows * cols + 0);
    table->SetChild(&buttons, rows * cols + 1);
    table->SetChild(&blank, rows * cols + 2);
    table->SetChild(&name, rows * cols + 3);
    return true;
}

void NewGameMapOpened(NewGameDialog* dialog) {
    if (dialog == nullptr || !::Ext::Ext().custom_maps || !ResourceUsable()) return;
    if (AddTableRow(dialog)) {
        UI::Element* load  = Find(dialog, "buttonLoadMap");
        UI::Element* clear = Find(dialog, "buttonClearMap");
        if (load == nullptr) return;
        load->SetElementId(kLoadMapButtonId);
        if (clear != nullptr) clear->SetElementId(kClearMapButtonId);
        ShowPending(dialog);
        std::fprintf(stderr, "[CustomMaps] New Game: Custom map row in the dialog's table (%s)\n",
                     Pending() != nullptr ? "a map is picked" : "none picked");
        return;
    }
    // No usable table: the row goes into the dialog in use (the game's, or a mod's rebuilt one such as
    // MegaMod's), at the end of the ribbon that holds the description box: that ribbon is the same element in
    // both, and the rest of the dialog is left as its resource has it.
    UI::Element* description = Find(dialog, "textDescription");
    UI::Element* ribbon = description != nullptr ? description->_parent : nullptr;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    const ::Object::External* rowRes = factory != nullptr ? factory->FindOrLoad(System::Filename(kRowObject))
        : nullptr;
    if (ribbon == nullptr || !::Object::IsDerivedFromNullSafe(rowRes, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr,
                     "[CustomMaps] New Game: no place for the Custom map row (description %p, ribbon %p, "
                     "row %p)\n",
                     static_cast<void*>(description), static_cast<void*>(ribbon),
                     static_cast<const void*>(rowRes));
        return;
    }
    Object::InternalPtr<UI::Element> row;
    static_cast<const UI::ElementDescription*>(rowRes)->CreateInstance(&row, dialog->_dialog);
    if (row.Get() == nullptr) {
        std::fprintf(stderr, "[CustomMaps] New Game: the Custom map row did not build\n");
        return;
    }
    ribbon->AddChild(&row);
    UI::Element* load  = Find(dialog, "buttonLoadMap");
    UI::Element* clear = Find(dialog, "buttonClearMap");
    if (load == nullptr) return;
    load->SetElementId(kLoadMapButtonId);
    if (clear != nullptr) clear->SetElementId(kClearMapButtonId);
    ShowPending(dialog);
    std::fprintf(stderr, "[CustomMaps] New Game: Custom map row (%s)\n",
                 Pending() != nullptr ? "a map is picked" : "none picked");
}

bool NewGameMapButton(NewGameDialog* dialog, int32_t id) {
    if (id != kLoadMapButtonId && id != kClearMapButtonId) return false;
    if (id == kClearMapButtonId) {
        ClearPending();
        g_modsLine.clear();
        ShowPending(dialog);
        return true;
    }
    // The maps folder when it holds maps: the Load screen's look (map_picker.h). Else, or when the picker
    // cannot open, the system's file picker.
    if (Platform::CanPickFile()) {
        const std::filesystem::path folder = MapsDir();
        if (!folder.empty()) {
            std::vector<ListedMap> maps = ScanMaps(folder);
            std::fprintf(stderr, "[CustomMaps] New Game: %zu map(s) in %s\n", maps.size(),
                         folder.string().c_str());
            if (!maps.empty() && OpenMapPicker(dialog, std::move(maps))) return true;
        }
    }
    BrowseForMap(dialog);
    return true;
}

void MapChosen(NewGameDialog* dialog, const std::filesystem::path& file) {
    if (dialog == nullptr) return;
    UseFile(dialog, file);
}

bool BrowseForMap(NewGameDialog* dialog) {
    if (dialog == nullptr) return false;
    if (!Platform::CanPickFile()) {
        ShowName(dialog, "Not available on this platform yet");
        return false;
    }
    std::filesystem::path path;
    if (!PickFile(path)) return false;   // cancelled: nothing changes
    UseFile(dialog, path);
    return true;
}

}  // namespace CustomMaps
}  // namespace Ext
