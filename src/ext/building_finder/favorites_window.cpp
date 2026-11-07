// ============================================================================
// src/ext/building_finder/favorites_window.cpp -- ext.building_finder's favorites window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/BUILDINGFINDER.md. A utility-style window (Dialog/FavoritesWindow.rsc: the game's
// own title bar, not modal, no pause) with the player's favorite buildings as a grid of picture buttons, the
// building's toolbar icon with the toolbar's hover tooltip: a press starts placing the building (what the
// toolbar's button does) and the window stays open. Favorites are added and taken out on the Find Buildings
// page; while there are none, a button here opens it. It is the tree window's way
// (family_tree/tree_window.cpp): it is opened by the page's "Open Favorites Window" button, it is dragged by
// its title bar, and it sits where the player last left it during the session.
//
// A press is only written down while its button is dispatching the event (the cells of that very button would
// be destroyed); OnUpdate applies it and builds the rows again when the favorites changed (FavoritesVersion),
// or when a row's text would read differently (a limit was reached or freed).
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "ui/TableElement.h"
#include "input/Input.h"
#include "input/Device.h"
#include "input/input_key_tables.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include "ui/StringTable.h"
#include "Game/game.h"
#include "Game/inputbindings.h"   // GameKey (the KeyMap ids)
#include "platform_input.h"        // Platform::Input::kVkEscape

#include "ext/building_finder/building_finder.h"
#include "ext/building_finder/finder_core.h"
#include "ext/building_finder/finder_math.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace Ext {
namespace BuildingFinder {

namespace {

constexpr const char* kWindowRsc = "Dialog/FavoritesWindow.rsc";
constexpr int32_t kCloseId = 1;
constexpr int32_t kOpenPageId = 10;             // the button to the Find Buildings page
constexpr int32_t kPickId = 100;                // favorite k's picture button: kPickId + k
constexpr int kMaxRows = 400;
constexpr int kRecheckFrames = 90;              // how often the rows' limit notes are compared
// the note shares its line with the button: a longer one widens the window
constexpr size_t kNoteChars = 26;
constexpr int kConfirmFrames = 300;            // how long "Sure? Clear All" waits for the second press

// One of our table's strings as the text a label takes (the key itself when the table did not load).
System::String OurText(const char* key) {
    const UI::String s = Toolbar::OurString(key);
    if (!s.IsValid()) return System::String(key);
    const System::String text = s;
    return text;
}

class FavoritesWindow;
FavoritesWindow* g_open = nullptr;   // the one open window (null when closed)
int g_x = 80;                        // where it was left (this session)
int g_y = 120;

const UI::Dialog* LoadDialog() {
    if (!::Object::ExternalFactory::_singleton) return nullptr;
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        std::fprintf(stderr, "[BuildingFinder] %s did not load as a UI::Dialog (%p)\n", kWindowRsc,
                     static_cast<const void*>(ext));
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

class FavoritesWindow final : public UI::DialogInstance {
public:
    FavoritesWindow(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr,
                             0), dialog_(dialog) {}

    ~FavoritesWindow() override {
        Release();
        if (g_open == this) g_open = nullptr;
    }

    void OnOpened() override {
        g_open = this;
        AcquireItems();
        held_ = true;
        Toolbar::SetTableText(FindElement(System::String("labelTitle")).Get(), "FavoritesWindowTitle");
        if (UI::Element* close = FindElement(System::String("buttonClose")).Get()) {
            close->SetElementId(kCloseId);
        }
        if (UI::Element* handle = FindElement(System::String("borderTitle")).Get()) {
            handle->SetElementId(0);
            handle->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);
        }
        table_ = FindElement(System::String("tableFavoritesWindow"));
        note_ = FindElement(System::String("textFavoritesWindowNote"));
        openPage_ = FindElement(System::String("buttonFavoritesOpenPage"));
        openText_ = FindElement(System::String("textFavoritesOpenPage"));
        if (UI::Element* open = openPage_.Get()) {
            open->SetElementId(kOpenPageId);
            // the note's line is as tall as the button, so the button's labels never change the window
            int32_t w = 0;
            int32_t h = 0;
            open->GetPaddedSize(w, h);
            if (UI::Element* note = note_.Get()) {
                if (h > note->_minHeight) {
                    note->_minHeight = h;
                    note->SetNeedsSizeCalc();
                }
            }
        }
        if (table_.Get() == nullptr) {
            std::fprintf(stderr, "[BuildingFinder] tableFavoritesWindow is not in %s: empty window\n",
                         kWindowRsc);
            return;
        }
        Rebuild();
    }

    void OnClosed() override {
        Release();
        _dragging = false;
        if (g_open == this) g_open = nullptr;
        UI::DialogInstance::OnClosed();
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId msg, int32_t id, int32_t c,
                        int32_t d,
                        int32_t e, void* userData) override {
        // The utility window's behavior, as TreeWindow (family_tree/tree_window.cpp): a press on the title
        // bar starts a drag, a release ends it, focus events bring the window to the front, the close button
        // closes it.
        if (msg == UI::EventId::LeftPress && id == 0) {
            _dragging = true;
            _dragOffsetX = c - _upperLeftX;
            _dragOffsetY = d - _upperLeftY;
        } else if (msg == UI::EventId::LeftRelease) {
            _dragging = false;
        } else if (msg >= UI::EventId::DialogLeftPress && msg <= UI::EventId::DialogMiddlePress) {
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->BringDialogToFront(&self);
        } else if (msg == UI::EventId::Activated) {
            if (id == kCloseId) {
                Object::InternalPtr<UI::DialogInstance> self(this);
                _ui->CloseDialog(&self);
                return;
            }
            if (id >= kPickId && id < kPickId + kMaxRows) {
                pendingRow_ = id - kPickId;
            } else if (id == kOpenPageId) {
                pendingAction_ = true;
            }
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

    void OnUpdate(const System::Time& time) override {
        UI::DialogInstance::OnUpdate(time);
        if (table_.Get() != nullptr) Tick();
        if (!_dragging || Input::Configuration::_singleton == nullptr) return;
        const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
        const float scale = _ui->_uiScale;
        const int keyX = static_cast<int>(keyMap->GetValue(0, ToUnderlying(GameKey::MouseX)));
        const int keyY = static_cast<int>(keyMap->GetValue(0, ToUnderlying(GameKey::MouseY)));
        int x = static_cast<int>(static_cast<float>(keyX) * scale) - _dragOffsetX;
        int y = static_cast<int>(static_cast<float>(keyY) * scale) - _dragOffsetY;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        SetUpperLeft(x, y, false);
        g_x = x;
        g_y = y;
        if (keyMap->GetValue(0, ToUnderlying(GameKey::MouseLeft)) == 0) _dragging = false;
    }

    // Shows the window in front of the others.
    void ToFront() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->BringDialogToFront(&self);
    }

    void CloseSelf() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
    }

private:
    void Release() {
        if (held_) {
            held_ = false;
            cells_.clear();
            rows_.clear();
            table_ = Object::InternalPtr<UI::Element>();
            note_ = Object::InternalPtr<UI::Element>();
            openPage_ = Object::InternalPtr<UI::Element>();
            openText_ = Object::InternalPtr<UI::Element>();
            ReleaseItems();
        }
    }

    void Say(const std::string& text) {
        if (UI::Element* e = note_.Get()) {
            if (::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
                static_cast<UI::TextElement*>(e)->SetParameter(
                    0, System::String(Clip(text, kNoteChars).c_str()));
            }
        }
    }

    // What the rows read now: the signature the periodic check compares.
    std::string Signature() const {
        std::string s;
        const std::vector<Item>& items = Items();
        for (int i : rows_) s += RowText(items[static_cast<size_t>(i)], false, true) + "\n";
        return s;
    }

    void Rebuild() {
        int missing = 0;
        const size_t before = rows_.size();
        rows_ = FavoriteItems(missing, kMaxRows);
        if (rows_.size() < before) {
            ScrollToTop(table_.Get());   // scrolled past the end of the shorter list: blank
        }
        IconSpec spec;
        spec.dialog = dialog_;
        spec.pickId0 = kPickId;
        cells_ = FillIconTable(table_.Get(), rows_, spec);
        seenVersion_ = FavoritesVersion();
        seenSignature_ = Signature();
        frames_ = 0;
        const bool none = Favorites().empty();
        if (none) {
            Say("No favorites yet.");
        } else if (missing > 0) {
            Say(std::to_string(missing) + " not in these mods.");
        } else {
            Say("Press an icon to place it.");
        }
        confirming_ = false;
        UpdateButton(none);
    }

    // The button on the note's line: "Open Find Buildings" while there are no favorites, "Clear All" once
    // there are (and "Sure? Clear All" after the first press, which the next press confirms).
    void UpdateButton(bool none) {
        const char* label =
            none ? "FavoritesOpenPage" : (confirming_ ? "FavoritesClearSure" : "FavoritesClear");
        const char* tip = none ? "FavoritesOpenPageTip" : "FavoritesClearTip";
        if (UI::Element* t = openText_.Get()) {
            if (::Object::IsDerivedFromNullSafe(t, UI::TextElement::TypeId())) {
                static_cast<UI::TextElement*>(t)->SetParameter(0, OurText(label));
            }
        }
        if (UI::Element* b = openPage_.Get()) b->SetToolTip(Toolbar::OurString(tip));
    }

    void Tick() {
        if (placing_ >= 0) {   // "Placing X." goes when the placing does (placed, right click, Esc)
            const std::vector<Item>& items = Items();
            if (placing_ >= static_cast<int>(items.size()) ||
                !IsPlacing(items[static_cast<size_t>(placing_)])) {
                placing_ = -1;
                Rebuild();   // the note is back to its usual line
                return;
            }
        }
        if (confirming_ && ++confirmFrames_ > kConfirmFrames) {
            Rebuild();   // nobody confirmed: the button and the note are back to their usual
            return;
        }
        if (pendingAction_) {
            pendingAction_ = false;
            if (Favorites().empty()) {
                Toolbar::ShowPage(_ui, kId);   // the extensions window, on the Find Buildings page
            } else if (!confirming_) {
                confirming_ = true;
                confirmFrames_ = 0;
                UpdateButton(false);
                Say("Press again to clear.");
            } else {
                bool saved = false;
                ClearFavoritesAndSave(saved);
                Rebuild();
                if (!saved) Say("Favorites not saved.");
            }
            return;
        }
        if (pendingRow_ >= 0) {
            const int row = pendingRow_;
            pendingRow_ = -1;
            if (row < static_cast<int>(rows_.size())) {
                const Item& it = Items()[static_cast<size_t>(rows_[static_cast<size_t>(row)])];
                // the dim icon and the tooltip have the details: the line beside the button is short
                Say(AtLimit(BuiltOf(it), it.limit) ? std::string("Limit reached.") : StartPlacing(it));
                placing_ = IsPlacing(it) ? rows_[static_cast<size_t>(row)] : -1;
                // The toolbar's buttons are check buttons its radio group keeps to one lit at a time; here
                // there is no group, so the lit one is the one being placed and the others are put out.
                for (size_t k = 0; k < cells_.size(); ++k) {
                    UI::Element* e = cells_[k].pick.Get();
                    if (::Object::IsDerivedFromNullSafe(e, UI::CheckElement::TypeId())) {
                        const bool lit = k == static_cast<size_t>(row) && placing_ >= 0;
                        // EnableCheck is not ported: the byte it would set
                        static_cast<UI::CheckElement*>(e)->_checked = lit ? 1 : 0;
                    }
                }
            }
            return;
        }
        if (FavoritesVersion() != seenVersion_) {
            Rebuild();   // the Buildings page added or removed one
            return;
        }
        if (++frames_ >= kRecheckFrames) {
            frames_ = 0;
            if (Signature() != seenSignature_) Rebuild();   // a limit was reached or freed
        }
    }

    const UI::Dialog* dialog_ = nullptr;
    bool held_ = false;
    Object::InternalPtr<UI::Element> table_;
    Object::InternalPtr<UI::Element> note_;
    std::vector<int> rows_;             // the item at each row
    std::vector<Cell> cells_;
    uint32_t seenVersion_ = 0;
    std::string seenSignature_;
    int frames_ = 0;
    int pendingRow_ = -1;               // an icon was pressed: its position
    bool pendingAction_ = false;        // the button on the note's line was pressed
    bool confirming_ = false;           // "Clear All" was pressed once: the next press clears
    int confirmFrames_ = 0;
    Object::InternalPtr<UI::Element> openPage_;   // that button
    Object::InternalPtr<UI::Element> openText_;   // and its label
    int placing_ = -1;                  // the item whose "Placing ..." the note shows, else -1
    bool _dragging = false;
    int _dragOffsetX = 0;
    int _dragOffsetY = 0;
};

}  // namespace

void OpenFavoritesWindow(UI::UserInterface* ui) {
    if (ui == nullptr) return;
    if (g_open != nullptr) {
        g_open->ToFront();
        return;
    }
    const UI::Dialog* rsc = LoadDialog();
    if (rsc == nullptr) return;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(FavoritesWindow), 8);
    if (mem == nullptr) return;
    FavoritesWindow* dlg = ::new (mem) FavoritesWindow(ui, rsc);
    dlg->SetUpperLeft(g_x, g_y, false);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    ui->AddDialog(&ref);   // dispatches OnOpened
}

// ---- the hotkey ----

// The key that opens or closes the window: H, a letter the game's default bindings leave free (the game's
// bindings are A W S D Q E R T for the camera and tools, C V B X Z for the camera presets, F for the
// structure type, the digits and F1-F9 for the toolbars and tools). A player who moved those keys elsewhere
// may find H taken. The player can pick another key on the Buildings page (the hotkey button): finder_core.h
// Hotkey(), H until then.
bool g_capturing = false;   // the hotkey button was pressed: the next key becomes the hotkey

// The game's keyboard input index of a Win32 virtual key (the table the keyboard poller reads), -1 when none.
int KeyIndex(int virtualKey) {
    for (int i = 0; i < Input::Device::kKeyboardInputCount; ++i) {
        if (Input::KeyTables::kIndexToVirtualKey[i] == virtualKey) return i;
    }
    return -1;
}

std::string g_message;       // what the page says about the last capture; TakeHotkeyMessage hands it over
int g_rejectedIndex = -1;    // a key that was refused: ignored until it is let go

// KeyMap's device number for the keyboard (inputbindings.cpp: 0 pad, 1 keyboard, 2 mouse)
constexpr int kKeyboardDevice = static_cast<int32_t>(Input::Device::InputDevice::Keyboard);

// The name the controls menu shows for a binding ("Rotate Right"), "" when unknown.
std::string BindingName(int inputId) {
    if (g_gameContainer == nullptr) return std::string();
    UI::String name(g_gameContainer->gameKeys, inputId);
    System::String text(name.operator System::String());
    return text._data != nullptr ? std::string(text._data) : std::string();
}

// While capturing: the first allowed key held becomes the hotkey, unless the game binds it already (the
// controls menu's own "already bound" check); Esc gives up. Returns true when it handled a key.
bool CaptureKey(const Input::Device* device, const Input::KeyMap* keyMap) {
    if (g_rejectedIndex >= 0 && device->_keyboard[g_rejectedIndex].value == 0) g_rejectedIndex = -1;
    for (int i = 0; i < Input::Device::kKeyboardInputCount; ++i) {
        if (device->_keyboard[i].value == 0 || i == g_rejectedIndex) continue;
        const int vk = Input::KeyTables::kIndexToVirtualKey[i];
        if (vk == Platform::Input::kVkEscape) {
            g_capturing = false;
            g_message = "Hotkey not changed.";
            return true;
        }
        if (!HotkeyAllowed(vk)) continue;
        const int taken = vk == Hotkey() ? -1 : keyMap->GetIdForDeviceInput(kKeyboardDevice, i);
        if (taken >= 0) {
            g_rejectedIndex = i;
            const std::string by = BindingName(taken);
            g_message = HotkeyName(vk) + " is taken" + (by.empty() ? std::string() : " by " + by) +
                        ". Try another.";
            return true;
        }
        const bool saved = SetHotkey(vk);
        g_capturing = false;
        g_message = "Hotkey is now " + HotkeyName(vk) + (saved ? "." : " (not saved).");
        return true;
    }
    return false;
}

void BeginHotkeyCapture() {
    g_capturing = true;
    g_rejectedIndex = -1;
    g_message.clear();
}

std::string TakeHotkeyMessage() {
    std::string m;
    m.swap(g_message);
    return m;
}

bool HotkeyCapturing() { return g_capturing; }

std::string HotkeyLabel() {
    return g_capturing ? "Press a key..." : "Favorites Window Hotkey: " + HotkeyName(Hotkey());
}

void BeginFrame(UI::UserInterface* ui) {
    static bool wasDown = false;
    const Input::Configuration* cfg = Input::Configuration::_singleton;
    const Input::Device* device = Input::Device::_singleton;
    const int index = KeyIndex(Hotkey());
    if (!::Ext::Ext().building_finder || ui == nullptr || index < 0 || cfg == nullptr ||
        cfg->_keyMap == nullptr || device == nullptr) {
        wasDown = false;
        g_capturing = false;
        return;
    }
    // while a text box has the focus the keyboard bindings are suspended (EditElement's latch): typing an "h"
    // in the search box must not open the window
    const bool typing = cfg->_keyMap->_suspendCount > 0;
    if (g_capturing && !typing) {
        wasDown = true;   // the key that was just chosen is still down: it must not toggle the window
        if (CaptureKey(device, cfg->_keyMap)) return;
    }
    // the pause menu (or any dialog that stops the game) is up: the key does nothing, and one held
    // down through its closing must not toggle the window afterwards
    const bool paused = ui->PausesGame();
    const bool down = !typing && device->_keyboard[index].value != 0;
    if (down && !wasDown && !paused) {
        if (g_open != nullptr) {
            g_open->CloseSelf();
        } else {
            OpenFavoritesWindow(ui);
        }
    }
    wasDown = down;
}

}  // namespace BuildingFinder
}  // namespace Ext
