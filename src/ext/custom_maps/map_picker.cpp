// ============================================================================
// src/ext/custom_maps/map_picker.cpp -- ext.custom_maps: the Custom map picker (map_picker.h)
// ============================================================================
// The shape is SaveLoadDialog's (src/game/Dialog/saveloaddialog.cpp): a table of rows cloned from the
// dialog's template 0, each with labelSaveName / labelSaveDate / imageSaveIcon / borderSave / imageCorrupt /
// selectSave; a click on a thumbnail raises LeftPress with the element id i + 4 and selects row i; Cancel is
// id 0 and Load id 2. What differs: the rows come from the maps folder (map_list.h), there is no busy page,
// and the thumbnail is made here from the file's PNG. Standalone code, not a retail body: each failed step is
// logged as [CustomMaps] and leaves the New Game row as it was.
// ============================================================================
// The runtime type headers come first (saveloaddialog.cpp's note: Volume.h's Rectangle against <wingdi.h>).
#include "ui/TextElement.h"
#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "ui/TableElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "ui/Dialog.h"
#include "video/ImageBuffer.h"
#include "graphics/Material.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "system/Value.h"
#include "Game/game.h"
#include "Dialog/NewGameDialog.h"

#include "ext/custom_maps/map_picker.h"
#include "ext/custom_maps/new_game_map.h"
#include "ext/custom_maps/png.h"
#include "ext/resources/text_provider.h"
#include "platform_pick_file.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace Ext {
namespace CustomMaps {

namespace {

constexpr const char* kResource = "Dialog/CustomMapLoad.rsc";
// the Load screen's own thumbnail material
constexpr const char* kMaterial = "Material/UI/UIMaterialNoBlend.rsc";
constexpr int32_t kCancelId   = 0;
constexpr int32_t kLoadId     = 2;
constexpr int32_t kBrowseId   = 3;
// imageSaveIcon of row i has the id i + 4
constexpr int32_t kFirstRowId = 4;
// bytes of a name or author; a longer one is cut and ends in "..."
constexpr size_t  kMaxShownName = 26;
// bytes of the mods line before the names that do not fit become "+N"
constexpr size_t  kMaxShownMods = 30;
// ImageBuffer::Create's arguments (ext.winter_ice's, saveload_saveinfo.cpp's SetThumbnail): a 2D texture,
// default usage, BGRA8, linear filter, clamped addressing.
constexpr int32_t kUsageDefault = 2;
constexpr int32_t kFormatBGRA8  = 5;
constexpr int32_t kLinear       = 1;
constexpr int32_t kClamp        = 1;

// -1: not checked yet; 0: unusable; 1: every object loads (the resources do not change during a run)
int g_resourceState = -1;

// Whether every object of our resource loads (the check NewGameMapResource makes for its dialog): a hole in
// the tree would be read through by the dialog's code, so a hole means the picker does not open.
bool ResourceUsable() {
    int& state = g_resourceState;
    if (state >= 0) return state == 1;
    state = 0;
    ::Ext::Resources::InitText();   // idempotent
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
        if (path == prefix + "resource") sawRoot = true;
        if (factory->FindOrLoad(System::Filename(path.c_str())) == nullptr) {
            std::fprintf(stderr, "[CustomMaps] picker: %s did not load\n", path.c_str());
            ++missing;
        }
    }
    state = (sawRoot && checked > 0 && missing == 0) ? 1 : 0;
    std::fprintf(stderr, "[CustomMaps] picker: %d object(s) of %s checked, %d missing: %s\n", checked,
                 kResource, missing,
                 state == 1 ? "the picker is used" : "the system's file picker is used");
    return state == 1;
}

template <class T>
T* FindIn(UI::DialogInstance* dialog, const char* name) {
    return static_cast<T*>(dialog->FindElement(System::String(name)).Get());
}

template <class T>
T* FindChild(UI::Element* parent, const char* name) {
    return static_cast<T*>(parent->FindElement(System::String(name)).Get());
}

// A row's text line: the text element's parameter 0 (a text element shows "@0" through the
// general string table).
void SetText(UI::TextElement* label, const std::string& text) {
    if (label != nullptr) label->SetParameter(0, System::String(text.c_str()));
}

// One row of the grid: the elements the template names, and the thumbnail objects this dialog made for it
// (deleted with the dialog).
struct Row {
    UI::TextElement*  labelName = nullptr;
    UI::TextElement*  labelDate = nullptr;
    UI::TextElement*  labelAuthor = nullptr;
    UI::TextElement*  labelMods = nullptr;
    UI::ImageElement* icon      = nullptr;
    UI::Element*      border    = nullptr;
    UI::ImageElement* corrupt   = nullptr;
    UI::Element*      select    = nullptr;
    Video::ImageBuffer*         image    = nullptr;
    Graphics::MaterialInstance* material = nullptr;
    UI::SpriteSheet*            sheet    = nullptr;
};

// The texture for a thumbnail, through the engine's own Create (what a package load runs). Null when
// the device refuses.
Video::ImageBuffer* MakeImage(const Image& pixels) {
    Video::ImageBuffer* image = new Video::ImageBuffer();   // the class's operator new (MemAlloc)
    if (image == nullptr) return nullptr;
    Video::ImageBuffer::LevelArray levels;
    levels.SetSize(1);
    Collection::Array<uint8_t>& level = levels[0];
    level.SetSize(static_cast<int>(pixels.bgra.size()));
    std::memcpy(level.GetData(), pixels.bgra.data(), pixels.bgra.size());
    if (!image->Create(0, kUsageDefault, kFormatBGRA8, 0u, static_cast<int32_t>(pixels.width),
                       static_cast<int32_t>(pixels.height), kLinear, kClamp, kClamp, kClamp, &levels)) {
        delete image;
        return nullptr;
    }
    return image;
}

class CustomMapDialog final : public UI::DialogInstance {
public:
    CustomMapDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                    Object::InternalPtr<NewGameDialog> parent,
                    std::vector<ListedMap> maps)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0),
          _parent(parent), _maps(std::move(maps)) {}

    ~CustomMapDialog() override {
        for (Row& row : _rows) {
            if (row.material != nullptr) delete row.material;
            if (row.sheet != nullptr) delete row.sheet;
            if (row.image != nullptr) delete row.image;
        }
    }

    void OnOpened() override {
        _buttonLoad   = FindIn<UI::ButtonElement>(this, "buttonLoad");
        _buttonCancel = FindIn<UI::ButtonElement>(this, "buttonCancel");
        _buttonBrowse = FindIn<UI::ButtonElement>(this, "buttonBrowse");
        _table        = FindIn<UI::TableElement>(this, "tableSaveList");
        if (_buttonLoad != nullptr) _buttonLoad->SetElementId(kLoadId);
        if (_buttonBrowse != nullptr) {
            _buttonBrowse->SetElementId(kBrowseId);
            // Browse is the system's file picker: hidden where the platform has none (every platform but
            // Windows so far), so the picker is the maps folder only.
            if (!Platform::CanPickFile()) _buttonBrowse->Hide(true);
        }
        if (_buttonCancel != nullptr) {
            _buttonCancel->SetElementId(kCancelId);
            _buttonCancel->_keyIndex = 8;   // Esc, as the Load screen
        }
        if (_table == nullptr) {
            std::fprintf(stderr, "[CustomMaps] picker: no tableSaveList: empty dialog\n");
            return;
        }
        _selected = -1;
        for (size_t i = 0; i < _maps.size() && _selected < 0; ++i) {
            if (_maps[i].valid) _selected = static_cast<int>(i);
        }
        BuildRows();
        std::fprintf(stderr, "[CustomMaps] picker opened: %zu map(s), %zu row(s) built\n", _maps.size(),
                     _rows.size());
    }

    void OnUpdateLayout() override {
        const int count = static_cast<int>(_maps.size());
        if (_buttonLoad != nullptr) _buttonLoad->Enable(_selected >= 0 && _selected < count &&
                                                        _maps[_selected].valid);
        if (_buttonCancel != nullptr) _buttonCancel->Enable(true);
        if (_buttonBrowse != nullptr) _buttonBrowse->Enable(true);
        for (int i = 0; i < count && i < static_cast<int>(_rows.size()); ++i) {
            // The selected row shows its highlight and hides its plain border; every other row the reverse
            // (the Load screen's rule).
            const uint32_t hidden = static_cast<uint32_t>(UI::kElementFlagHidden);
            const bool notSelected = i != _selected;
            if (_rows[i].select != nullptr) _rows[i].select->SetFlagsOnOff(notSelected ? hidden : 0u,
                                                                           notSelected ? 0u : hidden);
            if (_rows[i].border != nullptr) _rows[i].border->SetFlagsOnOff(notSelected ? 0u : hidden,
                                                                           notSelected ? hidden : 0u);
        }
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId msg, int32_t id, int32_t c,
                        int32_t d,
                        int32_t e, void* userData) override {
        const int count = static_cast<int>(_maps.size());
        if (msg == UI::EventId::LeftPress && id >= kFirstRowId && id < kFirstRowId + count) {
            if (_selected != id - kFirstRowId) _ui->Play(4);
            _selected = id - kFirstRowId;
            return;
        }
        if (msg == UI::EventId::Activated) {
            if (id == kCancelId) {
                Close();
                return;
            }
            if (id == kLoadId) {
                Choose();
                return;
            }
            // a map kept somewhere else: the system's file picker; the picker closes only once a
            // file is chosen
            if (id == kBrowseId) {
                if (BrowseForMap(_parent.Get())) Close();
                return;
            }
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

private:
    void Close() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
    }

    // Load: the dialog closes, then the New Game row reads the file. The path and the dialog are taken first:
    // closing may free `this`.
    void Choose() {
        const int count = static_cast<int>(_maps.size());
        if (_selected < 0 || _selected >= count || !_maps[_selected].valid) return;
        const std::filesystem::path path = _maps[_selected].path;
        NewGameDialog* parent = _parent.Get();
        std::fprintf(stderr, "[CustomMaps] picker: chose \"%s\"\n", path.string().c_str());
        Close();
        MapChosen(parent, path);
    }

    // The size of the Load screen's own thumbnails, so the grid looks the same: the empty-slot sprite's size
    // (0 x 0: the image's own).
    static void ThumbnailSize(uint32_t& width, uint32_t& height) {
        width = height = 0;
        if (g_gameContainer == nullptr) return;
        const int32_t w = g_gameContainer->spriteSaveLoadEmpty.Width();
        const int32_t h = g_gameContainer->spriteSaveLoadEmpty.Height();
        if (w > 0 && h > 0 && w <= 1024 && h <= 1024) {
            width = static_cast<uint32_t>(w);
            height = static_cast<uint32_t>(h);
        }
    }

    // A thumbnail's sprite on the row: the PNG decoded, scaled to the frame, uploaded, and wrapped in the
    // material and sheet the Load screen builds for a save whose thumbnail is raw pixels (saveloaddialog.cpp,
    // PopulateFileList). False leaves the row's empty-slot picture.
    bool SetThumbnail(Row& row, const ListedMap& map) {
        Image decoded;
        if (map.thumbnail.empty() || !DecodePng(map.thumbnail.data(), map.thumbnail.size(),
                                                decoded)) return false;
        uint32_t w = 0;
        uint32_t h = 0;
        ThumbnailSize(w, h);
        const Image fitted = (w != 0 && h != 0) ? FitCover(decoded, w, h) : decoded;
        if (fitted.bgra.empty()) return false;

        ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
        if (factory == nullptr) return false;
        const ::Object::External* materialRes = factory->FindOrLoad(System::Filename(kMaterial));
        if (!::Object::IsDerivedFromNullSafe(materialRes, Graphics::Material::TypeId())) return false;

        row.image = MakeImage(fitted);
        if (row.image == nullptr) return false;
        void* matMem = System::MemoryAllocator::MemAlloc(sizeof(Graphics::MaterialInstance), 8);
        void* sheetMem = System::MemoryAllocator::MemAlloc(sizeof(UI::SpriteSheet), 8);
        if (matMem == nullptr || sheetMem == nullptr) return false;
        row.material = ::new (matMem) Graphics::MaterialInstance();
        Graphics::Material* const material = static_cast<Graphics::Material*>(
            const_cast<::Object::External*>(materialRes));   // CAST-OK: const_api
        row.material->SetMaterial(Object::ExternalPtr<Graphics::Material>(material));
        row.material->AddTexture(System::String("diffuse"),
                                 Object::ExternalPtr<Video::ImageBuffer>(row.image));
        row.material->BuildParameters();

        row.sheet = ::new (sheetMem) UI::SpriteSheet();
        row.sheet->m_materialInstance = row.material;
        row.sheet->m_groupName = System::String("");
        row.sheet->m_textureName = System::String("");
        row.sheet->m_imageWidth = static_cast<int32_t>(fitted.width);
        row.sheet->m_imageHeight = static_cast<int32_t>(fitted.height);
        row.sheet->m_padForFiltering = false;
        row.sheet->AddSpriteInfo(System::String(""), System::String(""), 0, 0,
                                 static_cast<int32_t>(fitted.width),
                                 static_cast<int32_t>(fitted.height));
        row.sheet->SetSpriteInfo(0, static_cast<int32_t>(fitted.width), static_cast<int32_t>(fitted.height),
                                 0.0f, 0.0f, 1.0f, 1.0f);
        UI::Sprite thumb(Object::ExternalPtr<UI::SpriteSheet>(row.sheet), 0);
        row.icon->SetSprite(&thumb);
        return true;
    }

    // The rows of the grid: one per map, and blank dimmed ones up to the Load screen's slot count (at least
    // six, a multiple of three).
    void BuildRows() {
        const int count = static_cast<int>(_maps.size());
        int slots = (count < 6 ? 6 : count) + 2;
        slots = (slots / 3) * 3;
        _table->SetSize(slots / 3 + 1, 3);
        _rows.reserve(static_cast<size_t>(slots));
        for (int i = 0; i < slots; ++i) {
            Object::ExternalPtr<UI::ElementDescription> templateObj = _dialog->GetTemplate(0);
            if (templateObj.Get() == nullptr) {
                std::fprintf(stderr, "[CustomMaps] picker: the dialog has no row template\n");
                break;
            }
            Object::InternalPtr<UI::Element> clone;
            templateObj->CreateInstance(&clone, _dialog);
            UI::Element* cloned = clone.Get();
            if (cloned == nullptr) break;
            Object::InternalPtr<UI::Element> child(cloned);
            _table->SetChild(&child, i);

            Row row;
            row.labelName = FindChild<UI::TextElement>(cloned, "labelSaveName");
            row.labelDate = FindChild<UI::TextElement>(cloned, "labelSaveDate");
            row.labelAuthor = FindChild<UI::TextElement>(cloned, "labelMapAuthor");
            row.labelMods = FindChild<UI::TextElement>(cloned, "labelMapMods");
            row.icon      = FindChild<UI::ImageElement>(cloned, "imageSaveIcon");
            row.border    = FindChild<UI::Element>(cloned, "borderSave");
            row.corrupt   = FindChild<UI::ImageElement>(cloned, "imageCorrupt");
            row.select    = FindChild<UI::Element>(cloned, "selectSave");
            if (row.icon == nullptr || row.corrupt == nullptr || row.border == nullptr ||
                row.select == nullptr) {
                std::fprintf(stderr,
                             "[CustomMaps] picker: row %d lacks an element the Load screen's row has\n", i);
                _rows.push_back(row);
                continue;
            }
            if (i < count) {
                const ListedMap& map = _maps[i];
                row.icon->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);   // hover, press, tooltip
                row.icon->SetElementId(kFirstRowId + i);
                if (map.valid) {
                    row.corrupt->SetFlagsOnOff(UI::kElementFlagHidden, 0);
                    SetThumbnail(row, map);
                    // Four lines: the name; "by Author"; "512 x 512, 11/13/2026" (the date without its time);
                    // the mods it needs.
                    SetText(row.labelName, CutText(map.name, kMaxShownName));
                    SetText(row.labelAuthor, map.author.empty() ? std::string() : "by " + CutText(map.author,
                            kMaxShownName - 3));
                    const std::string date = DateText(map.modified).substr(0, 10);
                    SetText(row.labelDate, SizeText(map.mapSize) + (date.empty() ? "" : ", " + date));
                    SetText(row.labelMods, ModsText(map.mods, kMaxShownMods));
                } else {
                    // the Load screen's corrupt save: dimmed, with its overlay
                    row.icon->SetColor(System::kColorGrey);
                    SetText(row.labelName, CutText(map.name, kMaxShownName));
                    SetText(row.labelDate, "Not a valid map");
                    std::fprintf(stderr, "[CustomMaps] picker: %s: %s\n", map.path.string().c_str(),
                                 map.problem.c_str());
                }
            } else {
                row.corrupt->SetFlagsOnOff(UI::kElementFlagHidden, 0);
                row.select->SetFlagsOnOff(UI::kElementFlagHidden, 0);
                row.border->SetColor(0x80ffffff);
                row.icon->SetColor(0x20ffffff);
            }
            _rows.push_back(row);
        }
    }

    Object::InternalPtr<NewGameDialog> _parent;
    std::vector<ListedMap>             _maps;
    std::vector<Row>                   _rows;
    int                                _selected = -1;
    UI::ButtonElement*                 _buttonLoad = nullptr;
    UI::ButtonElement*                 _buttonCancel = nullptr;
    UI::ButtonElement*                 _buttonBrowse = nullptr;
    UI::TableElement*                  _table = nullptr;
};

}  // namespace

bool OpenMapPicker(NewGameDialog* newGame, std::vector<ListedMap> maps) {
    if (newGame == nullptr || newGame->_ui == nullptr || maps.empty() || !ResourceUsable()) return false;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) return false;
    const ::Object::External* ext = factory->FindOrLoad(System::Filename(kResource));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        std::fprintf(stderr, "[CustomMaps] picker: %s did not load as a UI::Dialog\n", kResource);
        return false;
    }
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(CustomMapDialog), 8);
    if (mem == nullptr) return false;
    CustomMapDialog* dialog = ::new (mem) CustomMapDialog(
        newGame->_ui, static_cast<const UI::Dialog*>(ext), Object::InternalPtr<NewGameDialog>(newGame),
        std::move(maps));
    Object::InternalPtr<UI::DialogInstance> ref(dialog);
    newGame->_ui->AddDialog(&ref);   // dispatches OnOpened
    return true;
}

}  // namespace CustomMaps
}  // namespace Ext
