// ============================================================================
// src/game/Dialog/extensionsdialog.cpp -- the Extensions dialog (NOT RETAIL)
// ============================================================================
// ExtensionsDialog.h has the design. Everything here is standalone code, not a
// retail body: guards are ours to place (CLAUDE.md's no-null-guard rule is
// about faithful bodies), and each failed step is logged as `[ExtDialog]` /
// `[ExtMenu]` to standalone_debug.log and leaves the menu as retail's.
//
// The resources are OURS (src/ext/ui/Dialog/, compiled by
// tools/ext_resources.sh with the mod kit's Tools-x64.exe and deployed loose
// into the game's WinData/Dialog/): Dialog/Extensions.rsc (the dialog, its
// row template, the menu button), Dialog/ExtensionsStringTable.rsc,
// Dialog/ExtensionsSprites.rsc. When they are absent (not compiled / not
// deployed) the button is not added and the dialog does not open; both say
// so in the log.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "engine/UpdateLoop.h"
#include "engine/MainLoop.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "save/SaveLoad.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "Game/GameUpdateLoop.h"
#include "Dialog/ExtensionsDialog.h"
#include "Dialog/MainMenuDialog.h"
#include "Dialog/MessageDialog.h"
#include "ext/registry.h"
#include "options.h"
#include "rsc_text_check.h"   // src/diag: the KITFREE cross-check at menu time (NOT RETAIL)

#include <cstdint>
#include <cstdio>
#include <new>
#include <string>

namespace {

// Our compiled resources (src/ext/ui/Dialog/*.rsc).
constexpr const char* kDialogRsc      = "Dialog/Extensions.rsc";
constexpr const char* kMenuButtonRsc  = "Dialog/Extensions.rsc:buttonExtensions";
constexpr const char* kStringTableRsc = "Dialog/ExtensionsStringTable.rsc";
constexpr const char* kSpritesRsc     = "Dialog/ExtensionsSprites.rsc";

// ---------------------------------------------------------------------------
// Resource helpers
// ---------------------------------------------------------------------------
const UI::Dialog* LoadDialog(const char* path) {
    if (!::Object::ExternalFactory::_singleton) return nullptr;
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        printf("[ExtDialog] %s did not load as a UI::Dialog (%p)\n", path, (const void*)ext);
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

// Our string table (the factory owns it; ExternalPtr is a bare pointer).
UI::StringTable* ExtStringTable() {
    if (!::Object::ExternalFactory::_singleton) return nullptr;
    UI::StringTable* table = ::Object::ExternalFactory::_singleton->FindOrLoadAs<UI::StringTable>(
        System::Filename(kStringTableRsc));
    if (!::Object::IsDerivedFromNullSafe(table, UI::StringTable::TypeId())) {
        printf("[ExtDialog] %s did not load (%p)\n", kStringTableRsc, (const void*)table);
        return nullptr;
    }
    return table;
}

UI::String ExtString(const char* key) {
    UI::StringTable* table = ExtStringTable();
    if (table == nullptr) return UI::String();
    return UI::String(Object::ExternalPtr<UI::StringTable>(table), System::String(key));
}

UI::SpriteSheet* ExtSprites() {
    if (!::Object::ExternalFactory::_singleton) return nullptr;
    UI::SpriteSheet* sheet = ::Object::ExternalFactory::_singleton->FindOrLoadAs<UI::SpriteSheet>(
        System::Filename(kSpritesRsc));
    if (!::Object::IsDerivedFromNullSafe(sheet, UI::SpriteSheet::TypeId())) {
        printf("[ExtDialog] %s did not load (%p)\n", kSpritesRsc, (const void*)sheet);
        return nullptr;
    }
    return sheet;
}

UI::TextElement* AsText(UI::Element* e) {
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) return nullptr;
    return static_cast<UI::TextElement*>(e);
}

// A text element whose string is a parameter ("Parameter" = "@0", or our
// "(profile: @0)"): fill parameter 0.
void SetParameterText(UI::Element* e, const char* text) {
    if (UI::TextElement* t = AsText(e)) t->SetParameter(0, System::String(text));
}

// A text element shows one of our table's strings.
void SetTableText(UI::Element* e, const char* key) {
    UI::TextElement* t = AsText(e);
    if (t == nullptr) return;
    const UI::String s = ExtString(key);
    if (s.IsValid()) t->SetText(&s);
}

UI::Element* Find(UI::DialogInstance* d, const char* name) {
    return d->FindElement(System::String(name)).Get();
}

// The row's kind (ExtensionsStringTable.rsc).
const char* KindKey(const Ext::Entry& e) {
    if (e.tier == Ext::Tier::Fix) return "ExtensionsKindFix";
    switch (e.cls) {
    case Ext::Class::A: return "ExtensionsKindA";
    case Ext::Class::B: return "ExtensionsKindB";
    case Ext::Class::C: return "ExtensionsKindC";
    case Ext::Class::None: break;
    }
    return "ExtensionsKindOther";
}

// The row icon: one sprite per registry entry, ExtensionsEntry_<id> (tools/ext_icons/).
std::string EntrySprite(const Ext::Entry& e) { return std::string("ExtensionsEntry_") + e.id; }

}  // namespace

// ===========================================================================
// ExtensionsDialog
// ===========================================================================
ExtensionsDialog::ExtensionsDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                                   Object::InternalPtr<MainMenuDialog> parentMenu)
    : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0),
      _parentMenu(parentMenu) {
    for (Row& r : _rows) { r.entry = -1; r.pending = false; }
}

ExtensionsDialog::~ExtensionsDialog() {}

void ExtensionsDialog::OnOpened() {
    if (UI::Element* cancel = Find(this, "buttonCancel")) {
        cancel->SetElementId(100);
        static_cast<UI::ButtonElement*>(cancel)->_keyIndex = 8;   // Esc, as ModDialog
    }
    if (UI::Element* ok = Find(this, "buttonOk")) ok->SetElementId(0x65);

    // A profile from settings.cfg overrides the toggles at the next boot: say
    // so beside the title ("(profile: faithful)"); hidden otherwise.
    if (UI::Element* profile = Find(this, "textProfile")) {
        if (Ext::CurrentProfile() != Ext::Profile::PerEntry)
            SetParameterText(profile, Ext::ProfileName(Ext::CurrentProfile()));
        else
            profile->Hide(true);
    }

    // Two tabs, one table each: Optional (defaultOn false) and Default (defaultOn true), each in
    // registry order. The rows keep one id space (1000 + row) whichever table holds them.
    _tableOptional = FindElement(System::String("tableOptional"));
    _tableDefault = FindElement(System::String("tableDefault"));
    const UI::Dialog* dialog = _dialog;
    if (_tableOptional.Get() == nullptr || _tableDefault.Get() == nullptr || dialog == nullptr) {
        printf("[ExtDialog] no tableOptional / tableDefault / dialog resource: empty dialog\n");
        return;
    }
    UI::SpriteSheet* sprites = ExtSprites();

    // The registry indices in row order: the Optional half first, then the Default half.
    int order[kMaxRows];
    int orderCount = 0;
    for (int half = 0; half < 2; ++half) {
        for (int i = 0; i < Ext::EntryCount() && orderCount < kMaxRows; ++i) {
            if (Ext::EntryAt(i).defaultOn == (half == 1)) order[orderCount++] = i;
        }
    }

    _rowCount = 0;
    int optionalRows = 0;
    for (int k = 0; k < orderCount; ++k) {
        const int i = order[k];
        const Ext::Entry& entry = Ext::EntryAt(i);
        UI::Element* table = entry.defaultOn ? _tableDefault.Get() : _tableOptional.Get();

        Object::ExternalPtr<UI::ElementDescription> rowTemplate = dialog->GetTemplate(0);   // entryRow
        if (rowTemplate.Get() == nullptr) break;
        Object::InternalPtr<UI::Element> row;
        rowTemplate->CreateInstance(&row, dialog);
        if (row.Get() == nullptr) break;
        table->AddChild(&row);
        if (!entry.defaultOn) ++optionalRows;

        auto child = [&row](const char* name) {
            return row->FindElement(System::String(name)).Get();
        };
        // Name and description: the registry's texts through "Parameter" (@0).
        SetParameterText(child("textModName"), entry.displayName);
        SetParameterText(child("textModDescription"), entry.description);
        SetTableText(child("textKind"), KindKey(entry));
        UI::Element* glyph = child("imageTier");
        if (sprites != nullptr && ::Object::IsDerivedFromNullSafe(glyph, UI::ImageElement::TypeId())) {
            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(sprites),
                              System::String(EntrySprite(entry).c_str()));
            static_cast<UI::ImageElement*>(glyph)->SetSprite(&sprite);
        }

        Row& r = _rows[_rowCount];
        r.entry = i;
        r.pending = Ext::Requested(entry);
        r.check = Object::InternalPtr<UI::Element>(child("checkLoaded"));
        r.state = Object::InternalPtr<UI::Element>(child("textState"));
        // ModDialog's row border: slot 11, GetBorder.
        Object::InternalPtr<UI::Element> border;
        r.border = *row->GetBorder(&border);
        if (r.check.Get() != nullptr) r.check->SetElementId(1000 + _rowCount);

        RefreshRow(_rowCount);
        ++_rowCount;
    }
    printf("[ExtDialog] opened: %d row(s) (optional %d, default %d), game=%p locked=%d\n", _rowCount,
           optionalRows, _rowCount - optionalRows, (void*)GameUpdateLoop::_singleton, Ext::Locked() ? 1 : 0);
}

void ExtensionsDialog::RefreshRow(int index) {
    Row& r = _rows[index];
    const Ext::Entry& e = Ext::EntryAt(r.entry);

    if (::Object::IsDerivedFromNullSafe(r.check.Get(), UI::CheckElement::TypeId()))
        static_cast<UI::CheckElement*>(r.check.Get())->_checked = r.pending ? 1 : 0;

    // Enabled / disabled as the checkbox stands; a B / C row locked by the
    // running game says so, and says when a changed request takes effect.
    const char* state = r.pending ? "ExtensionsStateEnabled" : "ExtensionsStateDisabled";
    if (Ext::EntryLocked(e)) {
        if (r.pending != Ext::Active(e))
            state = r.pending ? "ExtensionsStateLockedOn" : "ExtensionsStateLockedOff";
        else
            state = r.pending ? "ExtensionsStateLockedEnabled" : "ExtensionsStateLockedDisabled";
    }
    SetTableText(r.state.Get(), state);

    // ModDialog's colours: green enabled, white disabled.
    if (r.border.Get() != nullptr) r.border->SetColor(r.pending ? 0x20c0ffc0u : 0x10ffffffu);
}

void ExtensionsDialog::Close() {
    Object::InternalPtr<UI::DialogInstance> self(this);
    _ui->CloseDialog(&self);
}

// The game reload ModDialog::CloseAndReload (0x14006e9b0) runs for a mods
// change: save-restore the running game and load it again through the menu.
// The new GameUpdateLoop takes the class lock with the new request
// (Ext::LockForGame, the seam in GameUpdateLoop::Load), and keeps it although
// the save-restore below writes the running game's set into the manifest
// (Ext::KeepRequestOnNextLock, read by the load arm ExtSave_ReadTail).
void ExtensionsDialog::ReloadGame() {
    if (_parentMenu.Get() == nullptr) return;
    Ext::KeepRequestOnNextLock();
    Save::SavedState* state = nullptr;
    if (void* mem = System::MemoryAllocator::MemAlloc(0x10, 4))
        state = ::new (mem) Save::SavedState();
    if (::Engine::MainLoop* loop = ::Engine::MainLoop::_singleton)
        if (::Engine::UpdateLoop* inner = loop->_currentLoop)
            inner->SaveRestore(true, state);
    Object::InternalPtr<UI::DialogInstance> none;
    _parentMenu.Get()->LoadFromSave(state, -1, &none);
}

// OK. Fixes and class A apply at once. A B / C change while a game holds the
// lock shows Dialog/Reload.rsc:game (the Mods dialog's notice): confirmed, the
// request is recorded and the game reloads under it; cancelled, the B / C
// changes are dropped (the live ones are still applied).
void ExtensionsDialog::Apply() {
    bool lockedChange = false;
    for (int i = 0; i < _rowCount; ++i) {
        const Ext::Entry& e = Ext::EntryAt(_rows[i].entry);
        if (_rows[i].pending != Ext::Requested(e) && Ext::EntryLocked(e)) lockedChange = true;
    }

    bool reload = false;
    if (lockedChange && GameUpdateLoop::_singleton != nullptr) {
        int result = 0;
        if (const UI::Dialog* rsc = LoadDialog("Dialog/Reload.rsc:game")) {
            MessageDialog* notice = nullptr;
            if (void* mem = System::MemoryAllocator::MemAlloc(sizeof(MessageDialog), 8))
                notice = ::new (mem) MessageDialog(_ui, rsc, Object::InternalPtr<UI::DialogInstance>());
            Object::InternalPtr<UI::DialogInstance> ref(notice);
            // BANISHED_MODAL_ANSWER: the harness answer, as ModDialog::Apply.
            if (Opt().modalAnswer) {
                result = Opt().modalAnswerValue;
                ref.DeletePtr();
            } else {
                result = _ui->ShowDialog(&ref);
                ref.DeletePtr();
            }
        }
        reload = static_cast<unsigned>(result - 1) < 2u;
        printf("[ExtDialog] B/C change in game: reload answer=%d -> %s\n", result,
               reload ? "reload" : "dropped");
    }

    bool any = false;
    for (int i = 0; i < _rowCount; ++i) {
        const Ext::Entry& e = Ext::EntryAt(_rows[i].entry);
        if (_rows[i].pending == Ext::Requested(e)) continue;
        if (Ext::EntryLocked(e) && !reload && GameUpdateLoop::_singleton != nullptr) continue;
        Ext::SetAndRecord(e, _rows[i].pending);
        any = true;
    }
    if (any) Ext::PersistSettings();

    if (reload) ReloadGame();
    Close();
}

void ExtensionsDialog::OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg,
                                      int32_t id, int32_t c, int32_t d, int32_t e, void* userData) {
    if (msg == 0x12) {
        printf("[ExtDialog] click id=%d\n", id);
        if (id == 100) {
            Close();                                  // nothing applied
        } else if (id == 0x65) {
            Apply();
        } else if (id >= 1000 && id < 1000 + _rowCount) {
            Row& r = _rows[id - 1000];
            if (::Object::IsDerivedFromNullSafe(r.check.Get(), UI::CheckElement::TypeId()))
                r.pending = static_cast<UI::CheckElement*>(r.check.Get())->_checked != 0;
            else
                r.pending = !r.pending;
            RefreshRow(id - 1000);
        }
    }
    Object::InternalPtr<UI::Element> copy = *element;
    UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
}


// ===========================================================================
// Opener (MainMenuDialog::OnElementEvent, id 13)
// ===========================================================================
void ExtensionsDialog_OpenFromMainMenu(MainMenuDialog* menu) {
    if (menu == nullptr || menu->_ui == nullptr) return;
    const UI::Dialog* rsc = LoadDialog(kDialogRsc);
    if (rsc == nullptr) return;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(ExtensionsDialog), 8);
    if (mem == nullptr) return;
    ExtensionsDialog* dlg = ::new (mem) ExtensionsDialog(menu->_ui, rsc,
                                                         Object::InternalPtr<MainMenuDialog>(menu));
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    menu->_ui->AddDialog(&ref);   // dispatches OnOpened
}

// ===========================================================================
// The menu button (the seam in MainMenuDialog::OnOpened)
// ===========================================================================
void ExtensionsDialog_AttachMenuButton(MainMenuDialog* menu) {
    if (menu == nullptr || menu->_ui == nullptr || menu->_dialog == nullptr) return;
    if (Ext::GateProfileForced()) {
        printf("[ExtMenu] faithful profile forced on the command line: no Extensions button\n");
        return;
    }
    UI::Element* mod = Find(menu, "buttonMod");
    if (mod == nullptr || mod->_parent == nullptr) {
        printf("[ExtMenu] no buttonMod / parent: no Extensions button\n");
        return;
    }
    // The KITFREE cross-check at menu time (/rsccheck:1; src/diag/rsc_text_check.cpp).
    Diag::RscTextCheckIfArmed("standalone_rsccheck_menu");
    // Our button: Dialog/Extensions.rsc:buttonExtensions, the menu's button
    // shape (MainMenu.rsc:buttonMod) with our label, icon and tooltip.
    const UI::ElementDescription* desc = nullptr;
    if (::Object::ExternalFactory::_singleton) {
        const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(
            System::Filename(kMenuButtonRsc));
        if (::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId()))
            desc = static_cast<const UI::ElementDescription*>(ext);
    }
    if (desc == nullptr) {
        printf("[ExtMenu] %s did not load (tools/ext_resources.sh not deployed?): no Extensions button\n",
               kMenuButtonRsc);
        return;
    }

    Object::InternalPtr<UI::Element> button;
    desc->CreateInstance(&button, menu->_dialog);
    if (button.Get() == nullptr) {
        printf("[ExtMenu] CreateInstance failed\n");
        return;
    }
    UI::Element* list = mod->_parent;
    button->SetName(System::String("buttonExtensions"));
    button->SetElementId(kExtensionsMenuButtonId);
    list->AddChild(&button);

    // Beside Mods: move the appended child from the end to just after buttonMod.
    UI::Element** kids = list->_children.GetData();
    const int n = list->_children.GetSize();
    int modIndex = -1;
    for (int i = 0; i < n; ++i) if (kids[i] == mod) { modIndex = i; break; }
    if (modIndex >= 0 && n >= 2 && kids[n - 1] == button.Get()) {
        UI::Element* added = kids[n - 1];
        for (int i = n - 1; i > modIndex + 1; --i) kids[i] = kids[i - 1];
        kids[modIndex + 1] = added;
    }
    // In-game the Mods button is disabled for some games (game flag bit 10);
    // follow it.
    if (!mod->IsEnabled()) button->Enable(false);

    printf("[ExtMenu] Extensions button added (id %d, list %p, after index %d of %d)\n",
           kExtensionsMenuButtonId, (void*)list, modIndex, n);
}
