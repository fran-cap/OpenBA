// ============================================================================
// src/ext/family_tree/tree_window.cpp -- ext.family_tree's tree window
// ============================================================================
// docs/extensions/handins/FAMILYTREE.md section 4a. A utility-style window (Dialog/FamilyTreeWindow.rsc: the
// game's own title bar, not modal, no pause) that draws the picked person's tree (tree_view.cpp). It is
// opened by the Family page's "Show family tree" button and by the citizen panel's Family button, and it
// follows the pick: a click on a box picks that person (the Family page follows), a pick made on the page or
// from the citizen panel redraws the tree here. "Details" shows the Family page of the extension window.
//
// A pick is only written down while a button of the tree is dispatching its event (the cells of that very
// button would be destroyed); OnUpdate redraws, when the selection changed (SelectionStamp), the town grew,
// or a slide is queued. The window sits where the player last left it during the session; it is not part of
// the save.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "input/Input.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include "ext/family_tree/family_tree.h"
#include "ext/family_tree/tree_view.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <cstdint>
#include <cstdio>
#include <new>

namespace Ext {
namespace FamilyTree {

namespace {

constexpr const char* kWindowRsc = "Dialog/FamilyTreeWindow.rsc";

class TreeWindow;
TreeWindow* g_open = nullptr;   // the one open window (null when closed)
int g_x = 60;                   // where it was left (this session)
int g_y = 80;

const UI::Dialog* LoadDialog() {
    if (!::Object::ExternalFactory::_singleton) return nullptr;
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        std::fprintf(stderr, "[FamilyTree] %s did not load as a UI::Dialog (%p)\n", kWindowRsc,
                     static_cast<const void*>(ext));
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

class TreeWindow final : public UI::DialogInstance {
public:
    TreeWindow(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr,
                             0), dialog_(dialog) {}

    ~TreeWindow() override {
        view_.Detach();   // the elements go with the dialog
        if (g_open == this) g_open = nullptr;
    }

    void OnOpened() override {
        g_open = this;
        Toolbar::SetTableText(FindElement(System::String("labelTitle")).Get(), "FamilyTreeWindowTitle");
        if (UI::Element* close = FindElement(System::String("buttonClose")).Get()) close->SetElementId(1);
        if (UI::Element* handle = FindElement(System::String("borderTitle")).Get()) {
            handle->SetElementId(0);
            handle->SetFlagsOnOff(0x10000, 0);
        }
        if (UI::Element* details = FindElement(System::String("buttonFamilyTreeDetails")).Get()) {
            details->SetElementId(kTreeDetailsId);
        }
        if (!view_.Attach(FindElement(System::String("tableFamilyTree")).Get(),
                          FindElement(System::String("buttonFamilyTreeLeft")).Get(),
                          FindElement(System::String("buttonFamilyTreeRight")).Get(), dialog_)) {
            std::fprintf(stderr, "[FamilyTree] tableFamilyTree is not in %s: empty window\n", kWindowRsc);
            return;
        }
        Redraw(true);
    }

    void OnClosed() override {
        view_.Detach();
        _dragging = false;
        if (g_open == this) g_open = nullptr;
        UI::DialogInstance::OnClosed();
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id, int32_t c,
                        int32_t d,
                        int32_t e, void* userData) override {
        // The utility window's behavior, as Toolbar::Window (toolbar_window.cpp): a press on the title bar
        // starts a drag, a release ends it, focus events bring the window to the front, the close button
        // closes it.
        if (msg == 2 && id == 0) {
            _dragging = true;
            _dragOffsetX = c - _upperLeftX;
            _dragOffsetY = d - _upperLeftY;
        } else if (msg == 5) {
            _dragging = false;
        } else if (msg > 7 && msg < 0x0B) {
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->BringDialogToFront(&self);
        } else if (msg == 0x12) {
            if (id == 1) {
                Object::InternalPtr<UI::DialogInstance> self(this);
                _ui->CloseDialog(&self);
                return;
            }
            if (id == kTreeDetailsId) {
                Toolbar::ShowPage(_ui, kId);
            } else {
                uint32_t person = 0;
                if (view_.Click(id, person) == 1 && person != 0) ShowPerson(person);   // redrawn in OnUpdate
            }
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

    void OnUpdate(const System::Time& time) override {
        UI::DialogInstance::OnUpdate(time);
        const uint32_t selected = SelectedPerson();
        if (SelectionStamp() != seenStamp_) {
            Redraw(true);                       // another person was picked: the view moves to them
        } else if (Town().Size() != seenSize_) {
            // a birth or an arrival: the tree is drawn again where the player left it
            Redraw(false);
        } else if (view_.TakeSlide(selected)) {
            // redrawn
        }
        if (!_dragging || Input::Configuration::_singleton == nullptr) return;
        const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
        const float scale = _ui->_uiScale;
        const int keyX = static_cast<int>(keyMap->GetValue(0, 0));
        const int keyY = static_cast<int>(keyMap->GetValue(0, 1));
        int x = static_cast<int>(static_cast<float>(keyX) * scale) - _dragOffsetX;
        int y = static_cast<int>(static_cast<float>(keyY) * scale) - _dragOffsetY;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        SetUpperLeft(x, y, false);
        g_x = x;
        g_y = y;
        if (keyMap->GetValue(0, 5) == 0) _dragging = false;
    }

    // Shows the window in front of the others.
    void ToFront() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->BringDialogToFront(&self);
    }

private:
    void Redraw(bool center) {
        seenStamp_ = SelectionStamp();
        seenSize_ = Town().Size();
        view_.Build(SelectedPerson(), center);
    }

    const UI::Dialog* dialog_ = nullptr;
    TreeView view_;
    uint32_t seenStamp_ = ~0u;
    size_t seenSize_ = ~size_t(0);
    bool _dragging = false;
    int _dragOffsetX = 0;
    int _dragOffsetY = 0;
};

}  // namespace

void OpenTreeWindow(UI::UserInterface* ui) {
    if (ui == nullptr) return;
    if (g_open != nullptr) {
        g_open->ToFront();
        return;
    }
    const UI::Dialog* rsc = LoadDialog();
    if (rsc == nullptr) return;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(TreeWindow), 8);
    if (mem == nullptr) return;
    TreeWindow* dlg = ::new (mem) TreeWindow(ui, rsc);
    dlg->SetUpperLeft(g_x, g_y, false);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    ui->AddDialog(&ref);   // dispatches OnOpened
}

UI::UserInterface* UiOfElement(UI::Element* e) {
    if (e == nullptr) return nullptr;
    UI::Element* root = e;
    for (int i = 0; i < 64 && root->_parent != nullptr; ++i) root = root->_parent;
    return root->_dialogParent != nullptr ? root->_dialogParent->_ui : nullptr;
}

}  // namespace FamilyTree
}  // namespace Ext
