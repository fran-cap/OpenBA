// ============================================================================
// src/ext/dynamic_professions/professions_window.cpp -- ext.dynamic_professions' window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/DYNAMICPROFESSIONS.md. With the extension on, the toolbar's Professions button
// opens this window instead of retail's ProfessionUI: the same rows (Dialog/Profession.rsc:professionRow:
// the name, the spinner that moves citizens in and out of the profession, "of", the worker slots, the goto
// arrow) but only for the professions the town can use (professions_math.h: the laborer and the builder
// always, then every profession the town has a workplace for or a citizen in).
//
// The seam is one call in PlayerToolManager::ToggleUtilityDialog (toolbardialog.cpp), which the toolbar item,
// the entity panels and the tutorial all go through. The assignment and the goto are retail's
// (ProfessionUI::HandleSpinner / HandleButton, professionui.cpp) written out again over the same
// CitizenBehavior calls: those two are private to the retail controller.
//
// Standing rule of a window like this (the favorites window's): an event is only written down while its
// element is dispatching it; OnUpdate applies it, because the table the element sits in is rebuilt there.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include <cctype>
#include <string>
#include "video/Device.h"
#include "ui/TextElement.h"
#include "ui/LabelElement.h"
#include "ui/NumberEditElement.h"
#include "ui/NumberElement.h"
#include "ui/ContainerElement.h"
#include "ui/ScrollElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "ui/TableElement.h"
#include "input/Input.h"
#include "Game/inputbindings.h"   // GameKey (the KeyMap ids)
#include "ui/String.h"
#include "ui/Sprite.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include "Game/GameUpdateLoop.h"
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "Game/PlayerToolManager.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/UIDescription.h"
#include "Component/WorkComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Dialog/ProfessionUI.h"

#include "ext/dynamic_professions/dynamic_professions.h"
#include "ext/dynamic_professions/professions_math.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <new>
#include <vector>

namespace Ext {
namespace DynamicProfessions {

namespace {

constexpr const char* kWindowRsc = "Dialog/ProfessionsWindow.rsc";
constexpr int32_t kCloseId = 1;
constexpr int32_t kSpinnerId0 = 300;   // profession i's spinner: kSpinnerId0 + i (retail's ids)
constexpr int32_t kGotoId0 = 500;      // and its goto arrow: kGotoId0 + i
constexpr int32_t kOneColumnId = 2;    // the title bar button that makes the list one column
constexpr int32_t kTwoColumnsId = 3;   // ... and the one that makes it two (retail's layout)
constexpr int32_t kGripId = 4;         // the strip under the list: drag it to resize
constexpr int kRowSpacing = 2;         // the table's _spacing (ProfessionsWindow.rsc)
constexpr int kMinHeight = 28;         // the shortest the list may be dragged to (about two rows)
// the scroll box with two columns: two rows (kRowWidth each) and the scroll bar
constexpr int kBoxWidth = 440;
// the room after a row's arrow, so the next column's name is not against it
constexpr int kColumnGap = 8;
// every row is at least this wide (gap included): the laborer's has no "of" or slots, and a narrow first row
// would pull the second column in until a wider one pushed it out
constexpr int kRowWidth = 210;
constexpr int kProbeFrames = 10;       // how often the town is read again


// The shape the window's code relies on (retail's Profession.rsc:professionRow): the name label, the
// spinner with its number edit, "of", the worker slots, the goto arrow. A mod that rebuilds Profession.rsc
// may not keep it, so it is checked on a throwaway row, at every open, before any of its children are
// touched. Not cached: applying a mod change reloads the game's resources in the same run
// (ModDialog::Apply -> CloseAndReload), so an answer from before a reload can be stale either way.

bool RowIsRetails(const UI::ElementDescription* rowDesc, const UI::Dialog* dialog) {
    if (rowDesc == nullptr) return false;
    Object::InternalPtr<UI::Element> cell;
    rowDesc->CreateInstance(&cell, dialog);
    UI::Element* root = cell.Get();
    if (root == nullptr || root->ChildCount() < 5) return false;
    for (int i = 0; i < 5; ++i) {
        if (root->Child(i).Get() == nullptr) return false;
    }
    UI::Element* spinner = root->Child(1).Get();
    return ::Object::IsDerivedFromNullSafe(root->Child(0).Get(), UI::LabelElement::TypeId()) &&
           spinner->ChildCount() >= 1 &&
           ::Object::IsDerivedFromNullSafe(spinner->Child(0).Get(), UI::NumberEditElement::TypeId()) &&
           ::Object::IsDerivedFromNullSafe(root->Child(3).Get(), UI::NumberElement::TypeId());
}

class ProfessionsWindow;
ProfessionsWindow* g_open = nullptr;   // the one open window (null when closed)
int g_x = 80;                          // where it was left (this session)
int g_y = 120;
int g_boxHeight = 300;                 // the list's height: as the player dragged it, or the most it grows to
// the player has set the height (else the list fits its rows, up to g_boxHeight)
bool g_manual = false;
int g_columns = 2;                     // one or two columns: left as the player set it (this session)

const UI::Dialog* LoadDialog() {
    if (!::Object::ExternalFactory::_singleton) return nullptr;
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        std::fprintf(stderr, "[DynamicProfessions] %s did not load as a UI::Dialog (%p)\n", kWindowRsc,
                     static_cast<const void*>(ext));
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

// A scrolled list that got shorter would show nothing under its old scroll position.
void ScrollToTop(UI::Element* table) {
    UI::Element* e = table;
    for (int i = 0; i < 8 && e != nullptr; ++i, e = e->_parent) {
        if (!e->IsA(UI::ContainerElement::TypeId()) || e->_children._size < 2) continue;
        UI::Element* bar = e->_children._data[1];
        if (bar != nullptr && bar->IsA(UI::ScrollElement::TypeId())) {
            static_cast<UI::ScrollElement*>(bar)->_currentValue = 0.0f;
            return;
        }
    }
}

// One row of the table, by the profession's place in the game's list.
struct Row {
    int profession = -1;
    Object::InternalPtr<UI::Element> spinner;   // the NumberEditElement
    Object::InternalPtr<UI::Element> number;    // the worker slots
};

// The town's entities whose workplace staffs `profession` (retail's ProfessionBucket tree), or null.
const ProfessionBucket* BucketOf(const CitizenBehavior* town, const Profession* profession) {
    const ProfessionBucket* node = town->workplacesByProfession._root;
    while (node != nullptr) {
        const Profession* key = node->element.key;
        if (profession < key) {
            node = node->left;
        } else if (profession > key) {
            node = node->right;
        } else {
            return node;
        }
    }
    return nullptr;
}

class ProfessionsWindow final : public UI::DialogInstance {
public:
    ProfessionsWindow(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0),
          dialog_(dialog) {}

    ~ProfessionsWindow() override {
        Release();
        if (g_open == this) g_open = nullptr;
    }

    void OnOpened() override {
        g_open = this;
        held_ = true;
        Toolbar::SetTableText(FindElement(System::String("labelTitle")).Get(), "ProfessionsWindowTitle");
        if (UI::Element* close = FindElement(System::String("buttonClose")).Get()) {
            close->SetElementId(kCloseId);
        }
        if (UI::Element* handle = FindElement(System::String("borderTitle")).Get()) {
            handle->SetElementId(0);
            handle->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);
        }
        if (UI::Element* one = FindElement(System::String("buttonProfessionsOneColumn")).Get()) {
            one->SetElementId(kOneColumnId);
        }
        if (UI::Element* two = FindElement(System::String("buttonProfessionsTwoColumns")).Get()) {
            two->SetElementId(kTwoColumnsId);
        }
        if (UI::Element* grip = FindElement(System::String("borderProfessionsGrip")).Get()) {
            grip->SetElementId(kGripId);
            grip->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);
        }
        box_ = FindElement(System::String("containerProfessionsBox"));
        ShowColumnsButton();
        table_ = FindElement(System::String("tableProfessions"));
        UI::Element* table = table_.Get();
        if (!::Object::IsDerivedFromNullSafe(table, UI::TableElement::TypeId()) || table->ChildCount() < 1) {
            std::fprintf(stderr, "[DynamicProfessions] tableProfessions is not in %s: empty window\n",
                         kWindowRsc);
            table_ = Object::InternalPtr<UI::Element>();
            return;
        }
        // the row's description is the table's own template item (Profession.rsc:professionRow)
        rowDesc_ = table->Child(0).Get()->_description;
        if (!RowIsRetails(rowDesc_, dialog_)) {
            // a mod rebuilt Profession.rsc: Toggle closes this window and retail's opens instead
            declined_ = true;
            return;
        }
        Probe();
        Rebuild(Wanted());
    }

    void OnClosed() override {
        Release();
        _dragging = false;
        if (g_open == this) g_open = nullptr;
        UI::DialogInstance::OnClosed();
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId msg, int32_t id, int32_t c,
                        int32_t d, int32_t e, void* userData) override {
        // A utility window's behavior, as the favorites window's: a press on the title bar starts a drag, a
        // release ends it, focus events bring the window to the front, the close button closes it.
        if (msg == UI::EventId::LeftPress && id == 0) {
            _dragging = true;
            _dragOffsetX = c - _upperLeftX;
            _dragOffsetY = d - _upperLeftY;
        } else if (msg == UI::EventId::LeftRelease) {
            _dragging = false;
            resizing_ = false;
        } else if (msg >= UI::EventId::DialogLeftPress && msg <= UI::EventId::DialogMiddlePress) {
            if (msg == UI::EventId::DialogLeftPress) StartResizeIfOnGrip(c, d);
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->BringDialogToFront(&self);
        } else if (msg == UI::EventId::Activated && id == kCloseId) {
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->CloseDialog(&self);
            return;
        } else if (msg == UI::EventId::Activated && (id == kOneColumnId || id == kTwoColumnsId)) {
            pendingColumns_ = id == kOneColumnId ? 1 : 2;   // applied on the next tick, not inside the event
        } else if (msg == UI::EventId::ValueChanged && id >= kSpinnerId0 && id < kSpinnerId0 + kMaxIds) {
            pendingSpinner_ = id - kSpinnerId0;   // retail's: a spinner changed
        }
        // retail's: a goto arrow (message 0x12 or 0x15)
        if ((msg == UI::EventId::Activated || msg == UI::EventId::ValueChanged)
            && id >= kGotoId0 && id < kGotoId0 + kMaxIds) {
            pendingGoto_ = id - kGotoId0;
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

    void OnUpdate(const System::Time& time) override {
        UI::DialogInstance::OnUpdate(time);
        if (table_.Get() != nullptr) Tick();
        if (resizing_) Resize();
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

    // A drag on the strip under the list: the list's height follows the mouse, a whole number of rows at a
    // time.
    void Resize() {
        if (Input::Configuration::_singleton == nullptr) return;
        const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
        if (keyMap->GetValue(0, ToUnderlying(GameKey::MouseLeft)) == 0) {
            resizing_ = false;
            if (!resizeMoved_) {   // a click on the strip: back to fitting the rows
                g_manual = false;
                g_boxHeight = 300;
                ShowColumnsButton();
                Rebuild(shown_);
            }
            return;
        }
        const float scale = _ui->_uiScale;
        const int keyY = static_cast<int>(keyMap->GetValue(0, ToUnderlying(GameKey::MouseY)));
        const int mouseY = static_cast<int>(static_cast<float>(keyY) * scale);
        int height = resizeStartHeight_ + (mouseY - resizeStartMouseY_);
        const ::Video::Device* device = ::Video::Device::_singleton;
        int most = 4000;
        if (device != nullptr) {
            const float screen = static_cast<float>(device->backBufferHeight) * scale;
            most = static_cast<int>(screen) - _upperLeftY - 90;
        }
        // whole rows only: no half a profession showing (the shortest is one row)
        const int pitch = RowPitch();
        int least = kMinHeight;
        if (pitch > 0) {
            const int rows = std::max(1, (height + kRowSpacing + pitch / 2) / pitch);
            height = rows * pitch - kRowSpacing;
            least = pitch - kRowSpacing;
            while (height > most && rows > 1 && height > least) height -= pitch;
        }
        height = std::max(least, std::min(height, std::max(least, most)));
        if (height == g_boxHeight && g_manual) return;
        if (height == resizeStartHeight_ && !resizeMoved_) return;   // not moved yet
        resizeMoved_ = true;
        g_manual = true;
        g_boxHeight = height;
        ShowColumnsButton();
    }

    // The distance from one row of the table to the next: a row's height and the spacing between rows; 0 if
    // unknown.
    int RowPitch() {
        UI::Element* table = table_.Get();
        if (table == nullptr || table->ChildCount() < 1) return 0;
        const UI::Element* cell = table->Child(0).Get();
        if (cell == nullptr || cell->_layoutHeight <= 0) return 0;
        return cell->_layoutHeight + kRowSpacing;
    }

    // A text element sends no press message of its own, so the window's mouse-down (message 8, with the
    // mouse's place) is tested against the strip's rectangle here. A little slack, as the strip is thin.
    void StartResizeIfOnGrip(int mouseX, int mouseY) {
        UI::Element* grip = FindElement(System::String("borderProfessionsGrip")).Get();
        UI::Element* box = box_.Get();
        if (grip == nullptr || !::Object::IsDerivedFromNullSafe(box, UI::ContainerElement::TypeId())) return;
        constexpr int kSlack = 3;
        if (mouseX < grip->_layoutLeft - kSlack || mouseX > grip->_layoutRight + kSlack ||
            mouseY < grip->_layoutTop - kSlack || mouseY > grip->_layoutBottom + kSlack) {
            return;
        }
        resizing_ = true;
        resizeMoved_ = false;
        resizeStartMouseY_ = mouseY;
        // from the height it has now (the rows' height when it fits them), so it does not jump
        resizeStartHeight_ = std::max(kMinHeight, static_cast<int>(box->_layoutHeight));
        const int pitch = RowPitch();
        if (pitch > 0) {   // as the rows show it, so a first nudge does not snap it a row away
            const int rows = std::max(1, (resizeStartHeight_ + kRowSpacing + pitch / 2) / pitch);
            resizeStartHeight_ = rows * pitch - kRowSpacing;
        }
    }

    void CloseSelf() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
    }

private:
    // retail's id ranges are 200 apart. A profession past the 190th would get a spinner and an arrow that do
    // nothing; retail's list is 20 and MegaMod's about 84.
    static constexpr int kMaxIds = 190;

    void Release() {
        if (held_) {
            held_ = false;
            rows_.clear();
            table_ = Object::InternalPtr<UI::Element>();
            box_ = Object::InternalPtr<UI::Element>();
            rowDesc_ = nullptr;
        }
    }

    // ---- reading the town ----

    // The holders, the workplaces and the worker slots of every profession of the game's list.
    void Probe() {
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        if (game == nullptr || game->m_citizenList == nullptr || game->m_professionList == nullptr) return;
        CitizenBehavior* town = game->m_citizenList;
        ProfessionList* list = game->m_professionList;
        const int n = list->professions.GetSize();
        Profession** profs = list->professions.GetData();
        holders_.assign(static_cast<size_t>(n), 0);
        workplaces_.assign(static_cast<size_t>(n), 0);
        slots_.assign(static_cast<size_t>(n), 0);

        // retail's count: adult citizens who are not at school, by profession
        const int citizens = town->citizens._size;
        Object::InternalPtr<CitizenComponent>* all = town->citizens._data;
        for (int i = 0; i < citizens; ++i) {
            CitizenComponent* citizen = all[i].Get();
            if (citizen == nullptr || citizen->description == nullptr) continue;
            if (citizen->age < citizen->description->adultAge) continue;
            if (citizen->school != nullptr) continue;
            for (int p = 0; p < n; ++p) {
                if (profs[p] == citizen->profession) {
                    ++holders_[static_cast<size_t>(p)];
                    break;
                }
            }
        }

        for (int p = 0; p < n; ++p) {
            const ProfessionBucket* bucket = BucketOf(town, profs[p]);
            if (bucket == nullptr) continue;
            const int entities = bucket->element.entities._size;
            workplaces_[static_cast<size_t>(p)] = entities;
            // retail's "of": the worker slots of the workplaces that are accepting workers
            int total = 0;
            for (int k = 0; k < entities; ++k) {
                ComponentEntity* entity = bucket->element.entities._data[k].Get();
                if (entity == nullptr) continue;
                WorkPlaceComponent* workplace = entity->FindComponent<WorkPlaceComponent>();
                if (workplace == nullptr || workplace->isAccepting == 0) continue;
                ComponentEntity* owner = workplace->_entity.Get();
                WorkComponent* work = owner != nullptr ? owner->FindComponent<WorkComponent>() : nullptr;
                if (work == nullptr) continue;
                if (work->workState == 0) work->workState = work->CalcMaxWorkers();
                total += work->workState;
            }
            slots_[static_cast<size_t>(p)] = total;
        }
        laborer_ = IndexOf(profs, n, list->laborerProfession);
        builder_ = IndexOf(profs, n, list->builderProfession);
        probed_ = true;
        sinceProbe_ = 0;
    }

    static int IndexOf(Profession** profs, int n, const Profession* wanted) {
        for (int i = 0; i < n; ++i) {
            if (profs[i] == wanted) return i;
        }
        return -1;
    }

    std::vector<int> Wanted() {
        std::vector<Usage> probes(holders_.size());
        for (size_t i = 0; i < probes.size(); ++i) {
            probes[i].pin = static_cast<int>(i) == laborer_ ? 0 : (static_cast<int>(i) == builder_ ? 1 : 2);
            probes[i].workplaces = workplaces_[i];
            probes[i].holders = holders_[i];
        }
        return VisibleProfessions(probes, linger_, kProbeFrames);
    }

    // ---- the table ----

    void Rebuild(const std::vector<int>& shown) {
        UI::Element* tableElement = table_.Get();
        if (!probed_ || rowDesc_ == nullptr ||
            !::Object::IsDerivedFromNullSafe(tableElement, UI::TableElement::TypeId())) {
            return;
        }
        UI::TableElement* table = static_cast<UI::TableElement*>(tableElement);
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        ProfessionList* list = game->m_professionList;
        Profession** profs = list->professions.GetData();
        const size_t before = rows_.size();

        std::vector<Object::InternalPtr<UI::Element>> made;
        std::vector<Row> rows;
        for (int index : shown) {
            Object::InternalPtr<UI::Element> cell;
            rowDesc_->CreateInstance(&cell, dialog_);
            UI::Element* root = cell.Get();
            if (root == nullptr || root->ChildCount() < 5) break;
            root->SetPadding(0, kColumnGap, 0, 0);   // left, right, top, bottom
            root->_minWidth = kRowWidth;
            Profession* profession = profs[index];

            // retail's OnOpened (professionui.cpp): the label, its icon and its tip; the spinner and the
            // arrow with their ids; the laborer's row has no "of" and no slots
            UI::Element* label = root->Child(0).Get();
            UI::Element* spinner = root->Child(1).Get();
            UI::Element* of = root->Child(2).Get();
            UI::Element* number = root->Child(3).Get();
            UI::Element* arrow = root->Child(4).Get();
            if (::Object::IsDerivedFromNullSafe(label, UI::LabelElement::TypeId())) {
                UI::LabelElement* text = static_cast<UI::LabelElement*>(label);
                UI::String name(profession->stringTable, profession->stringName);
                text->SetText(&name);
                UI::Sprite sprite(profession->spriteSheet, profession->stringName);
                text->SetSprite(&sprite);
                UI::String tip(profession->stringTable, profession->stringDesc);
                text->SetToolTip(tip);
            }
            const uintptr_t spinnerId = static_cast<uintptr_t>(kSpinnerId0 + index);
            spinner->SetUserData(reinterpret_cast<Object::Base*>(spinnerId));
            spinner->SetElementId(kSpinnerId0 + index);
            UI::Element* edit = spinner->Child(0).Get();
            edit->SetElementId(kSpinnerId0 + index);
            arrow->SetElementId(kGotoId0 + index);
            if (index == laborer_) {
                number->SetFlagsOnOff(UI::kElementFlagHidden, 0);
                of->SetFlagsOnOff(UI::kElementFlagHidden, 0);
            }
            Row row;
            row.profession = index;
            if (::Object::IsDerivedFromNullSafe(edit, UI::NumberEditElement::TypeId())) {
                row.spinner = Object::InternalPtr<UI::Element>(edit);
            }
            if (::Object::IsDerivedFromNullSafe(number, UI::NumberElement::TypeId())) {
                row.number = Object::InternalPtr<UI::Element>(number);
            }
            rows.push_back(row);
            made.push_back(cell);
        }

        const int count = static_cast<int>(made.size());
        const int columns = std::max(1, std::min(g_columns, count));
        const int rowCount = (count + columns - 1) / columns;
        table->SetSize(rowCount, columns);   // clears the old cells
        for (int k = 0; k < count; ++k) {
            Object::InternalPtr<UI::Element> cell = made[static_cast<size_t>(k)];
            table->SetChild(&cell, k);
        }
        rows_ = rows;
        shown_ = shown;
        if (rows_.size() < before) ScrollToTop(table);
        // the window is as large as its rows need: measure it again, from the table up to the dialog's root
        for (UI::Element* e = table; e != nullptr; e = e->_parent) e->SetNeedsSizeCalc();
        PushValues();
    }

    // The spinners and the slots read what the town holds now (retail's OnUpdate): the laborer's spinner is
    // locked to its count, any other can move citizens to and from the laborers.
    void PushValues() {
        const int laborers = laborer_ >= 0 ? holders_[static_cast<size_t>(laborer_)] : 0;
        for (const Row& row : rows_) {
            const size_t p = static_cast<size_t>(row.profession);
            if (p >= holders_.size()) continue;
            UI::Element* edit = row.spinner.Get();
            if (::Object::IsDerivedFromNullSafe(edit, UI::NumberEditElement::TypeId())) {
                UI::NumberEditElement* spinner = static_cast<UI::NumberEditElement*>(edit);
                spinner->SetValue(holders_[p]);
                if (row.profession == laborer_) {
                    spinner->SetMinMax(holders_[p], holders_[p]);
                } else {
                    spinner->SetMinMax(0, SpinnerMax(holders_[p], laborers));
                }
            }
            UI::Element* number = row.number.Get();
            if (::Object::IsDerivedFromNullSafe(number, UI::NumberElement::TypeId())) {
                static_cast<UI::NumberElement*>(number)->SetValue(slots_[p]);
            }
        }
    }

    // The button that is shown is the layout it switches to: with two columns, "one column".
    void ShowColumnsButton() {
        UI::Element* one = FindElement(System::String("buttonProfessionsOneColumn")).Get();
        UI::Element* two = FindElement(System::String("buttonProfessionsTwoColumns")).Get();
        if (one != nullptr) {
            if (g_columns == 2) {
                one->SetFlagsOnOff(0, UI::kElementFlagHidden);
            } else {
                one->SetFlagsOnOff(UI::kElementFlagHidden, 0);
            }
        }
        if (two != nullptr) {
            if (g_columns == 1) {
                two->SetFlagsOnOff(0, UI::kElementFlagHidden);
            } else {
                two->SetFlagsOnOff(UI::kElementFlagHidden, 0);
            }
        }
        // the scroll box is as wide as its columns need
        UI::Element* box = box_.Get();
        if (::Object::IsDerivedFromNullSafe(box, UI::ContainerElement::TypeId())) {
            static_cast<UI::ContainerElement*>(box)->_requestedWidth =
                g_columns == 1 ? kBoxWidth - kRowWidth : kBoxWidth;
            static_cast<UI::ContainerElement*>(box)->_requestedHeight = g_boxHeight;
            // set by hand: not shrunk to the rows
            static_cast<UI::ContainerElement*>(box)->_allowShrinkHeight = g_manual ? 0 : 1;
            for (UI::Element* e = box; e != nullptr; e = e->_parent) e->SetNeedsSizeCalc();
        }
    }

    void Tick() {
        if (pendingColumns_ > 0) {
            g_columns = pendingColumns_;
            pendingColumns_ = 0;
            ShowColumnsButton();
            Rebuild(shown_);
            return;
        }
        if (pendingSpinner_ >= 0) {
            const int index = pendingSpinner_;
            pendingSpinner_ = -1;
            Probe();
            HandleSpinner(index);
            Probe();
            // retail only pushes the values: rebuilding would recreate the row under the cursor
            const std::vector<int> wanted = Wanted();
            if (wanted != shown_) {
                Rebuild(wanted);
            } else {
                PushValues();
            }
            return;
        }
        if (pendingGoto_ >= 0) {
            const int index = pendingGoto_;
            pendingGoto_ = -1;
            HandleGoto(index);
            return;
        }
        if (++sinceProbe_ >= kProbeFrames || !probed_) {
            Probe();
            const std::vector<int> wanted = Wanted();
            if (wanted != shown_) {
                Rebuild(wanted);
            } else {
                PushValues();
            }
        }
    }

    // ---- retail's two actions (professionui.cpp HandleSpinner / HandleButton), over the same calls ----

    void HandleSpinner(int index) {
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        const int n = game->m_professionList->professions.GetSize();
        if (index < 0 || index >= n) return;
        const Row* found = nullptr;
        for (const Row& row : rows_) {
            if (row.profession == index) found = &row;
        }
        if (found == nullptr ||
            !::Object::IsDerivedFromNullSafe(found->spinner.Get(), UI::NumberEditElement::TypeId())) {
            return;
        }
        const int desired = static_cast<UI::NumberEditElement*>(found->spinner.Get())->GetValue();
        const int current = holders_[static_cast<size_t>(index)];
        CitizenBehavior* citizenList = game->m_citizenList;
        Profession* profId = game->m_professionList->professions.GetData()[index];

        for (int toAdd = desired - current; toAdd > 0; --toAdd) {
            // request a worker slot, then a citizen: idle, working, student, any
            ComponentEntity* retBuf[2] = {nullptr, nullptr};
            citizenList->FindAvailableBuilding(retBuf, profId);
            ComponentEntity* slot = retBuf[0];
            ComponentEntity* slotCopy = slot;
            CitizenComponent* findBuf[1] = {nullptr};
            CitizenComponent** result = citizenList->FindWorkerToRelocate(findBuf, &slotCopy, profId);
            CitizenComponent* chosen = *result;
            if (chosen != nullptr) continue;
            if (slot != nullptr) {
                slotCopy = slot;
                CitizenComponent* fb2[1] = {nullptr};
                result = citizenList->FindNearestHousedLaborer(fb2, &slotCopy, profId);
                if (*result != nullptr) continue;
            }
            slotCopy = slot;
            CitizenComponent* fb3[1] = {nullptr};
            result = citizenList->FindLaborerFromFullestHouse(fb3, &slotCopy, profId);
            if (*result != nullptr) continue;
            slotCopy = slot;
            CitizenComponent* anyBuf = nullptr;
            citizenList->FindIdleLaborer(&anyBuf, &slotCopy, profId);
        }
        for (int toRemove = current - desired; toRemove > 0; --toRemove) {
            citizenList->RemoveWorker(profId);
        }
    }

    // Retail's HandleButton (professionui.cpp) cycles through the eligible citizens (adult, not at school),
    // and its scan is bounded by how many of them there are. This one scans every citizen for the next
    // eligible one of the profession, so a town of children and students is not walked twice over; the pick
    // is the same.
    void HandleGoto(int index) {
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        CitizenBehavior* town = game->m_citizenList;
        const int n = game->m_professionList->professions.GetSize();
        if (index < 0 || index >= n) return;
        const Profession* profId = game->m_professionList->professions.GetData()[index];
        const int citizens = town->citizens._size;
        Object::InternalPtr<CitizenComponent>* all = town->citizens._data;
        if (citizens == 0) return;

        // the next adult citizen of this profession after the one the last press went to, wrapping round
        for (int step = 0; step < citizens; ++step) {
            gotoIndex_ = (gotoIndex_ + 1) % citizens;
            CitizenComponent* citizen = all[gotoIndex_].Get();
            if (citizen == nullptr || citizen->profession != profId) continue;
            if (citizen->description == nullptr || citizen->age < citizen->description->adultAge) continue;
            if (citizen->school != nullptr) continue;
            ComponentEntity* entity = citizen->_entity.Get();
            UI::DialogInstance* self = this;
            game->m_toolManager->CenterOnEntity(&self, &entity);
            return;
        }
    }

    const UI::Dialog* dialog_ = nullptr;
    const UI::ElementDescription* rowDesc_ = nullptr;
    bool held_ = false;
    Object::InternalPtr<UI::Element> table_;
    std::vector<Row> rows_;
    std::vector<int> shown_;            // the professions the rows are for, in row order
    std::vector<int> holders_;          // per profession of the game's list
    std::vector<int> workplaces_;
    std::vector<int> slots_;
    std::vector<int> linger_;
    int laborer_ = -1;
    int builder_ = -1;
    bool probed_ = false;
    int sinceProbe_ = 0;
    int pendingSpinner_ = -1;           // a spinner changed: its profession
    bool resizeMoved_ = false;          // the mouse moved since the press (else it was a click)
    bool resizing_ = false;             // the strip is being dragged
    int resizeStartMouseY_ = 0;         // the mouse (in UI units) and the list's height when it was pressed
    int resizeStartHeight_ = 0;
    int pendingColumns_ = 0;            // the columns button was pressed: 1 or 2
    Object::InternalPtr<UI::Element> box_;   // the scroll box
    int pendingGoto_ = -1;              // a goto arrow was pressed: its profession
    int gotoIndex_ = -1;                // the citizen the last goto went to
    bool _dragging = false;
    int _dragOffsetX = 0;
    int _dragOffsetY = 0;

public:
    bool declined_ = false;   // the row is not retail's shape: take nothing over
};

}  // namespace

bool Handles(const UIDescription* ui) {
    if (ui == nullptr || !::Ext::Ext().dynamic_professions) return false;
    for (int i = 0; i < ui->_entryCount; ++i) {
        if (static_cast<uint32_t>(ui->_entries[i].type) != ProfessionUI::TypeId()) continue;
        // only retail's own layout (Dialog/Profession.rsc:content): MegaMod's sized variants
        // (DSProfession1.rsc, ProfessionVN.rsc ...) are chosen on purpose and keep their windows
        const ::Object::External* layout = ui->_entries[i].element;
        if (layout == nullptr) continue;
        const System::Filename path = layout->GetPath();
        std::string name = path._data != nullptr ? std::string(path._data) : std::string();
        for (char& c : name) {
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const size_t slash = name.rfind('/');
        if (slash != std::string::npos) name.erase(0, slash + 1);
        if (name == "profession.rsc:content") return true;
    }
    return false;
}

UI::DialogInstance* Toggle(UI::UserInterface* ui, bool& declined) {
    declined = false;
    if (ui == nullptr) return nullptr;
    if (g_open != nullptr) {
        g_open->CloseSelf();
        return nullptr;
    }
    const UI::Dialog* rsc = LoadDialog();
    if (rsc == nullptr) return nullptr;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(ProfessionsWindow), 8);
    if (mem == nullptr) return nullptr;
    ProfessionsWindow* dlg = ::new (mem) ProfessionsWindow(ui, rsc);
    dlg->SetUpperLeft(g_x, g_y, false);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    ui->AddDialog(&ref);   // dispatches OnOpened
    if (dlg->declined_) {
        dlg->CloseSelf();
        declined = true;
        return nullptr;
    }
    return dlg;
}

}  // namespace DynamicProfessions
}  // namespace Ext
