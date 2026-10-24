#pragma once
// ============================================================================
// src/game/Dialog/ExtensionsDialog.h -- the Extensions dialog (NOT RETAIL)
// ============================================================================
// docs/extensions/DESIGN.md §3 "Menu". Lists every fix and extension of the
// registry (src/ext/registry.h) with its kind and class letter, its lock
// state ("live" / "locked for this game"), a toggle and its description, in
// two tabs: Optional (the entries with defaultOn false, where the player adds
// extras; the tab shown first) and Default (defaultOn true: every fix and the
// default-on extensions, where one is switched off). Presentation only: the
// toggles, the keys and the lock are the same for both halves.
//
// THE RESOURCES ARE OURS, COMPILED. The shipped loader reads COMPILED
// resources only (Object::External::Load, external_load.cpp: a
// CompressedFileStream, magic 0x5412a91f, the Binary encoding; the Text
// encoding's reader/writer were stripped from the DLL). The sources are
// src/ext/ui/Dialog/: Extensions.rsc (the dialog, our own markup deriving
// its list from the game's Dialog/Mod.rsc elements by reference; its row
// template `entryRow`, its tables `tableOptional` / `tableDefault`; the menu button
// `buttonExtensions`, MainMenu.rsc:buttonMod with our label, icon and
// tooltip), ExtensionsStringTable.rsc, ExtensionsSprites.rsc (the button icon,
// the per-tier row glyphs). tools/ext_resources.sh compiles them with the mod
// kit's Tools-x64.exe; `tools/build.sh deploy` copies the .crs files loose
// into the game's WinData/Dialog/ (docs/extensions/handins/EXTRSC.md). A row's
// name and description are the registry's texts through the general table's
// "Parameter" (@0); its kind, lock state and glyph come from our table/sheet.
//
// Shape: ModDialog's (ModDialog.h) -- OnOpened builds a row per entry,
// OnElementEvent dispatches Cancel 100 / OK 0x65 / row toggles 1000+i, and
// OK in a running game with a B / C change shows Dialog/Reload.rsc:game, the
// notice the Mods dialog shows, and reloads the game the way ModDialog's
// CloseAndReload does. Not a reflected type (no factory registration): it is
// only ever built by ExtensionsDialog_OpenFromMainMenu.
// ============================================================================

#include <cstdint>
#include "ui/DialogInstance.h"
#include "object/Internal.h"

class MainMenuDialog;                                   // Dialog/MainMenuDialog.h

// The menu button's element id (MainMenuDialog's ids stop at 12, Tutorial).
constexpr int32_t kExtensionsMenuButtonId = 13;

class ExtensionsDialog final : public UI::DialogInstance {
public:
    ExtensionsDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                     Object::InternalPtr<MainMenuDialog> parentMenu);
    ~ExtensionsDialog() override;

    void OnOpened() override;
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        int32_t a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;

private:
    struct Row {
        int32_t entry;                               // registry index
        bool    pending;                             // the checkbox, not yet applied
        Object::InternalPtr<UI::Element> check;      // CheckElement
        Object::InternalPtr<UI::Element> state;      // textState: the lock state
        Object::InternalPtr<UI::Element> border;     // the row's border (colour)
    };
    static constexpr int kMaxRows = 64;

    void RefreshRow(int index);
    void Apply();
    void ReloadGame();
    void Close();

    Row     _rows[kMaxRows];
    int32_t _rowCount = 0;
    Object::InternalPtr<UI::Element>  _tableOptional; // "tableOptional": the rows with defaultOn false
    Object::InternalPtr<UI::Element>  _tableDefault;  // "tableDefault": the rows with defaultOn true
    Object::InternalPtr<MainMenuDialog> _parentMenu; // the menu that opened it (the reload goes through it)
};

// The "Extensions" button's click (MainMenuDialog::OnElementEvent, id 13).
void ExtensionsDialog_OpenFromMainMenu(MainMenuDialog* menu);

// Called from the seam in MainMenuDialog::OnOpened: instantiate
// Dialog/Extensions.rsc:buttonExtensions beside buttonMod as the Extensions
// button (id 13). Does nothing when the gate forced the faithful profile on
// the command line (Ext::GateProfileForced), when the menu has no buttonMod,
// when the compiled resource is absent, or when any step fails (each logged
// as `[ExtMenu]`).
void ExtensionsDialog_AttachMenuButton(MainMenuDialog* menu);
