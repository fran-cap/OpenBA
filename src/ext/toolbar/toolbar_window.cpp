// ============================================================================
// src/ext/toolbar/toolbar_window.cpp -- the extension window and its toolbar button (toolbar.h)
// ============================================================================
// NOT RETAIL. Standalone code, not a retail body: guards are ours to place, and each failed step is logged as
// `[ExtToolbar]` and leaves the toolbar as it was.
//
// The window is a utility-style window like Limits and Status Bar: not modal, it does not pause the game, it
// has the game's own title bar (StandardDialog.rsc:titleBar, with its close button and drag handle), it
// remembers its place while the game runs, and the toolbar's extension button opens and closes it. Its body
// is ONE tab control (Dialog/ExtensionsWindow.rsc); every active extension's page is added to it at run time
// by UI::TabElement::AddTab, the way the Options dialog's Enhanced tab is (enhanced_tab.cpp). Not a reflected
// type: only ever built by Toggle.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "ui/ComboElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "ui/TabElement.h"
#include "input/Input.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Toolbar.h"

#include "ext/toolbar/toolbar.h"
#include "ext/registry.h"
#include "ext/ui/find_element.h"
#include "ext/toolbar/window_state.h"   // SavedWindow(): open / where / which page, the host record

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace Ext {
namespace Toolbar {

namespace {

constexpr const char* kWindowRsc      = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kTabButtonRsc   = "Dialog/ExtensionsWindow.rsc:tabButtonPage";
constexpr const char* kStringTableRsc = "Dialog/ExtensionsStringTable.rsc";
constexpr const char* kSpritesRsc     = "Dialog/ExtensionsSprites.rsc";
constexpr int32_t kToolbarSortPriority = 950;   // between Town Hall (900) and Help (1000)

class Window;
Window* g_open = nullptr;   // the one open window (null when closed); where it sits is SavedWindow()

const UI::Dialog* LoadDialog(const char* path) {
    if (!::Object::ExternalFactory::_singleton) {
        return nullptr;
    }
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        std::fprintf(stderr, "[ExtToolbar] %s did not load as a UI::Dialog (%p)\n",
                     path, static_cast<const void*>(ext));
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

UI::ElementDescription* LoadDescription(const char* path) {
    if (!::Object::ExternalFactory::_singleton) {
        return nullptr;
    }
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[ExtToolbar] %s did not load as an ElementDescription (%p)\n",
                     path, static_cast<const void*>(ext));
        return nullptr;
    }
    // AddTab takes the description as an ExternalPtr (non-const); the factory hands out const objects.
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

UI::StringTable* OurStringTable() {
    if (!::Object::ExternalFactory::_singleton) {
        return nullptr;
    }
    UI::StringTable* table =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<UI::StringTable>(
            System::Filename(kStringTableRsc));
    if (!::Object::IsDerivedFromNullSafe(table, UI::StringTable::TypeId())) {
        return nullptr;
    }
    return table;
}

UI::SpriteSheet* OurSpriteSheet() {
    if (!::Object::ExternalFactory::_singleton) {
        return nullptr;
    }
    UI::SpriteSheet* sheet =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<UI::SpriteSheet>(System::Filename(kSpritesRsc));
    if (!::Object::IsDerivedFromNullSafe(sheet, UI::SpriteSheet::TypeId())) {
        return nullptr;
    }
    return sheet;
}

// The toolbar group's own name ("reports" for the Utility group), lower case.
std::string GroupName(const ::Toolbar* group) {
    if (group == nullptr) {
        return std::string();
    }
    System::Filename path = group->GetPath();
    System::Filename id = path.GetPathIdentifer();
    return Lower(id.AsString()._data);
}

// One page the window shows: its contribution and the page's root element.
struct OpenPage {
    const Contribution* contribution = nullptr;
    Object::InternalPtr<UI::Element> root;
    int32_t idBase = 0;   // the page's element ids are [idBase, idBase + kPageIdSpan)
    int32_t tab = 0;      // its index in the TabElement
};

class Window final : public UI::DialogInstance {
public:
    Window(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0) {}

    ~Window() override {
        // A window deleted without OnClosed (the game quits, or a load destroys the UI) must still let every
        // page forget the elements this window is about to free.
        ClosePages();
        if (g_open == this) {
            g_open = nullptr;
        }
    }

    void OnOpened() override {
        g_open = this;
        SavedWindow().open = true;

        // The title bar is the game's own (StandardDialog.rsc:titleBar), wired the way ToolUI::OnCreated
        // wires a utility window's: the title text, the close button (id 1) and the drag handle (borderTitle:
        // id 0, flag 0x10000).
        SetTableText(FindElement(System::String("labelTitle")).Get(), "ExtensionsWindowTitle");
        if (UI::Element* close = FindElement(System::String("buttonClose")).Get()) {
            close->SetElementId(1);
        }
        if (UI::Element* handle = FindElement(System::String("borderTitle")).Get()) {
            handle->SetElementId(0);
            handle->SetFlagsOnOff(0x10000, 0);
        }

        // One tab per active extension page.
        UI::Element* tabsElement = FindElement(System::String("tabPages")).Get();
        if (!::Object::IsDerivedFromNullSafe(tabsElement, UI::TabElement::TypeId())) {
            std::fprintf(stderr, "[ExtToolbar] tabPages is not a TabElement (%p): empty window\n",
                         static_cast<void*>(tabsElement));
            return;
        }
        UI::TabElement* tabs = static_cast<UI::TabElement*>(tabsElement);
        _tabs = tabs;
        UI::ElementDescription* buttonDesc = LoadDescription(kTabButtonRsc);
        if (buttonDesc == nullptr) {
            return;
        }

        const Contribution* active[kMaxPages] = {};
        const int count = ActivePages(active, kMaxPages);
        if (count > kMaxPages) {
            std::fprintf(stderr, "[ExtToolbar] %d pages are active, the window shows %d: the rest are left "
                         "out\n", count, kMaxPages);
        }
        for (int i = 0; i < count && i < kMaxPages; ++i) {
            const Contribution* c = active[i];
            UI::ElementDescription* pageDesc = LoadDescription(c->page);
            if (pageDesc == nullptr) {
                continue;
            }
            Object::InternalPtr<UI::Element> root =
                tabs->AddTab(Object::ExternalPtr<UI::ElementDescription>(buttonDesc),
                             Object::ExternalPtr<UI::ElementDescription>(pageDesc));
            if (root.Get() == nullptr) {
                std::fprintf(stderr, "[ExtToolbar] AddTab failed for %s\n", c->id);
                continue;
            }
            // The tab's button is the paired child just before its container (TabElement.h: button
            // at 3 + 2i).
            const int tabIndex = tabs->_tabCount - 1;
            UI::Element** kids = tabs->_children.GetData();
            const int nKids = tabs->_children.GetSize();
            if (3 + 2 * tabIndex < nKids && kids[3 + 2 * tabIndex] != nullptr) {
                UI::Element* button = kids[3 + 2 * tabIndex];
                const int nb = button->ChildCount();
                for (int k = 0; k < nb; ++k) {
                    SetTableText(button->Child(k).Get(), c->caption);
                }
            }
            OpenPage p;
            p.contribution = c;
            p.root = root;
            p.idBase = IdBaseFor(static_cast<int>(_pages.size()));
            p.tab = tabIndex;
            _pages.push_back(p);
            c->open(root.Get(), p.idBase);
            std::fprintf(stderr, "[ExtToolbar] page %s opened (tab %d)\n", c->id, tabIndex);
        }
        if (_pages.empty()) {
            std::fprintf(stderr, "[ExtToolbar] no page opened: empty window\n");
            return;
        }
        // The page that was showing (by its id; the first page when it is gone or none was saved).
        for (const OpenPage& p : _pages) {
            if (SavedWindow().page == p.contribution->id) {
                tabs->SetActiveTab(p.tab);
                break;
            }
        }
        _shownTab = -1;   // OnUpdate records the showing page
    }

    void OnClosed() override {
        // The player closed it. A quit or a load deletes the window without OnClosed: the save already
        // holds `open`, and the next town starts from the reset.
        SavedWindow().open = false;
        ClosePages();
        if (g_open == this) {
            g_open = nullptr;
        }
        _dragging = false;
        UI::DialogInstance::OnClosed();
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id, int32_t c,
                        int32_t d, int32_t e, void* userData) override {
        // The utility window's behaviour, as ToolUI::OnElementEvent (toolui.cpp): a press on the
        // title bar starts a drag, a release ends it, focus events bring the window to the front, the
        // close button closes it.
        if (msg == 2 && id == 0) {
            _dragging = true;
            _dragOffsetX = c - _upperLeftX;
            _dragOffsetY = d - _upperLeftY;
        } else if (msg == 5) {
            _dragging = false;
        } else if (msg > 7 && msg < 0x0B) {
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->BringDialogToFront(&self);
        } else if (msg == 0x12 || msg == 0x14) {
            // A combo box is a button: its click (0x12) only opens the list, and the pick comes from the
            // list as 0x14 (ComboDialog::OnElementEvent). A page hears a combo when something is picked,
            // not when the list is opened or closed again; every other control, as before, on its click.
            const bool isCombo = ::Object::IsDerivedFromNullSafe(element->Get(), UI::ComboElement::TypeId());
            if (isCombo != (msg == 0x14)) {
                Object::InternalPtr<UI::Element> copy = *element;
                UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
                return;
            }
            if (id == 1) {
                Object::InternalPtr<UI::DialogInstance> self(this);
                _ui->CloseDialog(&self);
                return;
            }
            for (OpenPage& p : _pages) {
                if (!OwnsId(p.idBase, id)) {
                    continue;
                }
                if (p.contribution->changed(p.root.Get(), id - p.idBase)) {
                    break;
                }
            }
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

    // The drag, as ToolUI::OnUpdate: the window follows the mouse (never above or left of the screen)
    // until it is released.
    void OnUpdate(const System::Time& time) override {
        UI::DialogInstance::OnUpdate(time);
        RecordShownPage();
        if (_tabs != nullptr) {
            const int32_t showing = _tabs->GetActiveTab();
            for (const OpenPage& p : _pages) {
                if (p.tab == showing && p.contribution->tick != nullptr) p.contribution->tick();
            }
        }
        if (!_dragging || Input::Configuration::_singleton == nullptr) {
            return;
        }
        const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
        const float scale = _ui->_uiScale;
        const int keyX = static_cast<int>(keyMap->GetValue(0, 0));
        const int keyY = static_cast<int>(keyMap->GetValue(0, 1));
        int x = static_cast<int>(static_cast<float>(keyX) * scale) - _dragOffsetX;
        int y = static_cast<int>(static_cast<float>(keyY) * scale) - _dragOffsetY;
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }
        SetUpperLeft(x, y, false);
        SavedWindow().x = x;
        SavedWindow().y = y;
        if (keyMap->GetValue(0, 5) == 0) {
            _dragging = false;
        }
    }

    // Shows the page with this id and puts the window in front.
    void ActivatePage(const char* pageId) {
        if (_tabs == nullptr) return;
        for (const OpenPage& p : _pages) {
            if (std::string(p.contribution->id) == pageId) {
                _tabs->SetActiveTab(p.tab);
                break;
            }
        }
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->BringDialogToFront(&self);
    }

private:
    // The showing page into SavedWindow().page, when the player switched tabs.
    void RecordShownPage() {
        if (_tabs == nullptr) {
            return;
        }
        const int32_t active = _tabs->GetActiveTab();
        if (active == _shownTab) {
            return;
        }
        _shownTab = active;
        for (const OpenPage& p : _pages) {
            if (p.tab == active) {
                SavedWindow().page = p.contribution->id;
                return;
            }
        }
    }

    void ClosePages() {
        for (OpenPage& p : _pages) {
            p.contribution->close();
        }
        _pages.clear();
    }

    std::vector<OpenPage> _pages;
    UI::TabElement* _tabs = nullptr;   // owned by the dialog's element tree
    int32_t _shownTab = -1;
    bool _dragging = false;
    int  _dragOffsetX = 0;
    int  _dragOffsetY = 0;
};

void OpenWindow(UI::UserInterface* ui) {
    if (ui == nullptr || g_open != nullptr) {  // one window at a time
        return;
    }
    const UI::Dialog* rsc = LoadDialog(kWindowRsc);
    if (rsc == nullptr) {
        return;
    }
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(Window), 8);
    if (mem == nullptr) {
        return;
    }
    Window* dlg = ::new (mem) Window(ui, rsc);
    // Where it was last, as ToggleUtilityDialog places a pinned utility window (no clamp, as retail's).
    dlg->SetUpperLeft(SavedWindow().x, SavedWindow().y, false);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    ui->AddDialog(&ref);   // dispatches OnOpened
}

}  // namespace

void ShowPage(UI::UserInterface* ui, const char* pageId) {
    if (ui == nullptr || pageId == nullptr) return;
    SavedWindow().page = pageId;   // a window that opens now starts on it
    if (g_open != nullptr) {
        g_open->ActivatePage(pageId);
        return;
    }
    OpenWindow(ui);
}

UI::String OurString(const char* key) {
    UI::StringTable* table = OurStringTable();
    if (table == nullptr) {
        return UI::String();
    }
    return UI::String(Object::ExternalPtr<UI::StringTable>(table), System::String(key));
}

void SetTableText(UI::Element* e, const char* key) {
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        return;
    }
    const UI::String text = OurString(key);
    if (text.IsValid()) {
        static_cast<UI::TextElement*>(e)->SetText(&text);
    }
}

std::string Lower(const char* s) {
    std::string out(s != nullptr ? s : "");
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

void RestoreWindow(UI::UserInterface* ui) {
    // A save made with the window open reopens it, as PlayerToolManager::Startup reopens retail's pinned
    // utility windows; only while some page is offered (a reload that turned the last contributor off
    // opens nothing).
    if (ui == nullptr || !SavedWindow().open || !Offered()) {
        return;
    }
    std::fprintf(stderr, "[ExtToolbar] reopening the extension window at %d,%d (page '%s')\n",
                 SavedWindow().x, SavedWindow().y, SavedWindow().page.c_str());
    OpenWindow(ui);
}

void Toggle(UI::UserInterface* ui) {
    if (ui == nullptr) {
        return;
    }
    if (g_open != nullptr) {
        Object::InternalPtr<UI::DialogInstance> ref(g_open);
        ui->CloseDialog(&ref);   // OnClosed clears g_open
        return;
    }
    OpenWindow(ui);
}

// ===========================================================================
// The toolbar button (EXT SEAMS in ToolbarDialog)
// ===========================================================================
int InsertAt(const ::Toolbar* group, const ::Toolbar* const* items, int n) {
    if (GroupName(group) != "reports") {
        return -1;
    }
    if (GameUpdateLoop::_singleton == nullptr) {
        return -1;
    }
    if (!Offered()) {
        return -1;
    }
    if (OurStringTable() == nullptr || OurSpriteSheet() == nullptr) {
        std::fprintf(stderr, "[ExtToolbar] ExtensionsStringTable / ExtensionsSprites did not load: "
                     "no extension button\n");
        return -1;
    }
    int at = n;
    for (int i = 0; i < n; ++i) {
        if (items[i] != nullptr && items[i]->_field50 > kToolbarSortPriority) {
            at = i;
            break;
        }
    }
    std::fprintf(stderr, "[ExtToolbar] the extension button goes at slot %d of %d in the Reports group\n",
                 at, n);
    return at;
}

void Style(UI::Element* button, int32_t index) {
    if (!::Object::IsDerivedFromNullSafe(button, UI::ButtonElement::TypeId())) {
        std::fprintf(stderr, "[ExtToolbar] template 3 clone is not a ButtonElement\n");
        return;
    }
    UI::ButtonElement* b = static_cast<UI::ButtonElement*>(button);
    button->SetName(System::String("extensions"));
    b->SetElementId(index);
    b->SetUserData(nullptr);
    b->SetToolTip(OurString("ExtensionsWindowTip"));
    b->SetFlagsOnOff(0x10000, 0);
    UI::SpriteSheet* sheet = OurSpriteSheet();
    if (sheet == nullptr) {
        return;
    }
    const int count = b->ChildCount();
    for (int i = 0; i < count; ++i) {
        UI::Element* child = b->Child(i).Get();
        if (!::Object::IsDerivedFromNullSafe(child, UI::ImageElement::TypeId())) {
            continue;
        }
        UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(sheet), System::String("ToolbarExtensions"));
        if (sprite.IsValid()) {
            static_cast<UI::ImageElement*>(child)->SetSprite(&sprite);
        }
    }
}

}  // namespace Toolbar
}  // namespace Ext
