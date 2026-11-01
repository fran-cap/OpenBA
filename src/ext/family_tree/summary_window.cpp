// ============================================================================
// src/ext/family_tree/summary_window.cpp -- ext.family_tree's town summary window
// ============================================================================
// docs/extensions/handins/FAMILYTREE.md section 4d. A utility-style window (Dialog/FamilyTreeSummary.rsc: the
// game's own title bar, not modal, no pause) with the town's numbers from the family record
// (family_summary.h): people, where they came from, deaths by cause, generations, and each year. It is opened
// by the Family page's "Town summary" button and is rebuilt when the town grows, someone dies, or a year
// passes. The window sits where the player last left it during the session; it is not part of the save.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/TableElement.h"
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
#include "Game/EventLog.h"
#include "Game/GameUpdateLoop.h"

#include "ext/family_tree/family_summary.h"
#include "ext/family_tree/family_tree.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"
#include "ext/ui/list_width.h"

#include <cstdint>
#include <cstdio>
#include <new>
#include <string>

namespace Ext {
namespace FamilyTree {

namespace {

constexpr const char* kWindowRsc = "Dialog/FamilyTreeSummary.rsc";
constexpr const char* kRowRsc = "Dialog/FamilyTreePage.rsc:ribbonFamilyRelationCell";
constexpr int kRecheckFrames = 120;      // how often the open window looks at the town

class SummaryWindow;
SummaryWindow* g_open = nullptr;
int g_x = 90;                            // where it was left (this session)
int g_y = 110;

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

// The town's name as the status bar shows it (the save name, GameUpdateLoop::m_saveName).
std::string TownName() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_saveName._data == nullptr) return std::string();
    return std::string(g->m_saveName._data);
}

// "Year 12, Early Spring": the year and the season word the Event Log prints (EventLog::SeasonString) for
// now.
std::string DateText() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    const int32_t now = NowStamp();
    if (g == nullptr || now < 0) return std::string();
    std::string text = "Year " + std::to_string(now / 12);
    if (g->m_eventNotifier != nullptr) {
        const System::String season = g->m_eventNotifier->SeasonString();
        if (season._data != nullptr && season._data[0] != '\0') text += std::string(", ") + season._data;
    }
    return text;
}

void SetText(UI::Element* e, const std::string& text) {
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) return;
    static_cast<UI::TextElement*>(e)->SetParameter(0, System::String(text.c_str()));
}

class SummaryWindow final : public UI::DialogInstance {
public:
    SummaryWindow(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr,
                             0), dialog_(dialog) {}

    ~SummaryWindow() override {
        table_ = Object::InternalPtr<UI::Element>();
        if (g_open == this) g_open = nullptr;
    }

    void OnOpened() override {
        g_open = this;
        Toolbar::SetTableText(FindElement(System::String("labelTitle")).Get(), "FamilySummaryWindowTitle");
        if (UI::Element* close = FindElement(System::String("buttonClose")).Get()) close->SetElementId(1);
        if (UI::Element* handle = FindElement(System::String("borderTitle")).Get()) {
            handle->SetElementId(0);
            handle->SetFlagsOnOff(0x10000, 0);
        }
        table_ = Object::InternalPtr<UI::Element>(FindElement(System::String("tableFamilySummary")).Get());
        if (!::Object::IsDerivedFromNullSafe(table_.Get(), UI::TableElement::TypeId())) {
            std::fprintf(stderr, "[FamilyTree] tableFamilySummary is not in %s: empty window\n", kWindowRsc);
            table_ = Object::InternalPtr<UI::Element>();
            return;
        }
        Rebuild();
    }

    void OnClosed() override {
        table_ = Object::InternalPtr<UI::Element>();
        _dragging = false;
        if (g_open == this) g_open = nullptr;
        UI::DialogInstance::OnClosed();
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id, int32_t c,
                        int32_t d,
                        int32_t e, void* userData) override {
        // The utility window's behavior, as TreeWindow (tree_window.cpp): a press on the title bar starts a
        // drag, a release ends it, focus events bring the window to the front, the close button closes it.
        if (msg == 2 && id == 0) {
            _dragging = true;
            _dragOffsetX = c - _upperLeftX;
            _dragOffsetY = d - _upperLeftY;
        } else if (msg == 5) {
            _dragging = false;
        } else if (msg > 7 && msg < 0x0B) {
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->BringDialogToFront(&self);
        } else if (msg == 0x12 && id == 1) {
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->CloseDialog(&self);
            return;
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

    void OnUpdate(const System::Time& time) override {
        UI::DialogInstance::OnUpdate(time);
        if (--countdown_ <= 0) {
            countdown_ = kRecheckFrames;
            if (Signature() != seen_) Rebuild();
            else RefreshDate();
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

    void ToFront() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->BringDialogToFront(&self);
    }

private:
    // What the rows depend on: how many people, how many are alive, and the year.
    std::string Signature() const {
        const Tree& tree = Town();
        size_t living = 0;
        for (const Person& p : tree.People())
            if (p.Alive()) ++living;
        return std::to_string(tree.Size()) + "/" + std::to_string(living) + "/" +
               std::to_string(NowStamp() / 12);
    }

    void Rebuild() {
        seen_ = Signature();
        countdown_ = kRecheckFrames;
        UI::Element* t = table_.Get();
        if (!::Object::IsDerivedFromNullSafe(t, UI::TableElement::TypeId())) return;
        UI::TableElement* table = static_cast<UI::TableElement*>(t);
        const int32_t now = NowStamp();
        const std::vector<SummaryRow> rows = SummaryRows(Summarize(Town(), now), now, TownName(), DateText());
        dateRow_ = -1;
        for (size_t k = 0; k < rows.size() && k < 3; ++k)
            if (rows[k].label == "Date") dateRow_ = static_cast<int32_t>(k);
        // The same rows with other numbers (a birth, a death): only the texts change, so the list keeps its
        // scroll.
        if (!shown_.empty() && rows.size() == shown_.size()) {
            bool sameRows = true;
            for (size_t k = 0; k < rows.size() && sameRows; ++k) sameRows = rows[k].label == shown_[k].label;
            if (sameRows) {
                for (size_t k = 0; k < rows.size(); ++k) {
                    if (rows[k].info == shown_[k].info) continue;
                    Object::InternalPtr<UI::Element> cell = table->Child(static_cast<int32_t>(k));
                    if (cell.Get() == nullptr) continue;
                    SetText(Ext::FindElement<UI::TextElement>(cell.Get(), "textFamilyInfo"), rows[k].info);
                }
                shown_ = rows;
                return;
            }
        }
        shown_.clear();
        const ::Object::External* ext =
            ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kRowRsc));
        if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
            std::fprintf(stderr, "[FamilyTree] %s did not load: no summary rows\n", kRowRsc);
            table->SetSize(0, 1);
            return;
        }
        UI::ElementDescription* tmpl =
            const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
        const int32_t width = Ext::ListWidth(table);
        table->SetSize(static_cast<int>(rows.size()), 1);   // clears the old cells
        for (size_t k = 0; k < rows.size(); ++k) {
            Object::InternalPtr<UI::Element> cell;
            tmpl->CreateInstance(&cell, dialog_);
            if (cell.Get() == nullptr) break;
            table->SetChild(&cell, static_cast<int32_t>(k));
            SetText(Ext::FindElement<UI::TextElement>(cell.Get(), "textFamilyRelation"), rows[k].label);
            SetText(Ext::FindElement<UI::TextElement>(cell.Get(), "textFamilyInfo"), rows[k].info);
            if (UI::Element* button = Ext::FindElement<UI::ButtonElement>(cell.Get(),
                    "buttonFamilyRelated")) button->Hide(true);
            if (width > 0) cell->_minWidth = width;
        }
        shown_ = rows;
    }

    // The month moves on every few minutes: the Date row's text is set in place, the list (and its scroll
    // position) kept.
    void RefreshDate() {
        UI::Element* t = table_.Get();
        if (dateRow_ < 0 || !::Object::IsDerivedFromNullSafe(t, UI::TableElement::TypeId())) return;
        Object::InternalPtr<UI::Element> cell = static_cast<UI::TableElement*>(t)->Child(dateRow_);
        if (cell.Get() == nullptr) return;
        SetText(Ext::FindElement<UI::TextElement>(cell.Get(), "textFamilyInfo"), DateText());
    }

    const UI::Dialog* dialog_ = nullptr;
    int32_t dateRow_ = -1;
    std::vector<SummaryRow> shown_;   // the rows the table holds now
    Object::InternalPtr<UI::Element> table_;
    std::string seen_;
    int countdown_ = kRecheckFrames;
    bool _dragging = false;
    int _dragOffsetX = 0;
    int _dragOffsetY = 0;
};

}  // namespace

void OpenSummaryWindow(UI::UserInterface* ui) {
    if (ui == nullptr) return;
    if (g_open != nullptr) {
        g_open->ToFront();
        return;
    }
    const UI::Dialog* rsc = LoadDialog();
    if (rsc == nullptr) return;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(SummaryWindow), 8);
    if (mem == nullptr) return;
    SummaryWindow* dlg = ::new (mem) SummaryWindow(ui, rsc);
    dlg->SetUpperLeft(g_x, g_y, false);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    ui->AddDialog(&ref);   // dispatches OnOpened
}

}  // namespace FamilyTree
}  // namespace Ext
