#pragma once
// ============================================================================
// src/game/Dialog/ModDialog.h — ENDGAME B.3.8 block DLG
//
// The local mod list (main menu / pause menu "Mods" button).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "ModDialog", parent "UI::DialogInstance", ti 0x1401c5280
//   create 0x140012900 (MemAlloc(0xd8, 8)), createInPlace 0x140012970
//   ctors 0x140070030, 0x1400700c0
//   dtor ~ModDialog 0x14006ff00 (Ghidra labels it a ctor; per entry: SpriteSheet, MaterialInstance,
//   ImageBuffer deleting dtors; MemFree the entry array; the two IBPs nulled)
//   table 0x14015c870, 10 slots:
//     [0] 0x140012a30 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x14006f150 OnOpened
//     [5] DialogInstance::OnClosed          [6] DialogInstance::OnUpdateLayout
//     [7] DialogInstance::OnUpdate          [8] 0x14006ed40 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0xd8. There is NO secondary vtable: Dialogs.h's old
//   `SecondaryVtable = 0x14015c8c0` is a separately allocated
//   Video::ImageBuffer's table (the per-mod thumbnail), not a second vptr.
//
// B.4 W1 (DLG-b): +0xb8 is retail's `Collection::Array<ModDialogEntry>`. The
// dtor 0x14006ff00 frees and zeroes it by hand (0x14006ff9f..0x14006ffb3), nulls
// the two InternalPtrs, and then runs the member ~Array (a second MemFree of
// the now-null buffer and the same two zero stores, 0x14006ffec..0x140070000).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "collection/Collection.h"
#include "Object/exe_type.h"

class MainMenuDialog;                                   // Dialog/MainMenuDialog.h
namespace Video    { class ImageBuffer; }
namespace Graphics { class MaterialInstance; }
namespace UI       { class SpriteSheet; }

// One row of the mod table (0x58): OnOpened 0x14006f150 builds it and appends
// it with Array<T>::SetSize 0x140012a60; the dtor deletes the three objects.
struct ModDialogEntry {
    Object::InternalPtr<UI::Element> textModName;         // +0x00
    Object::InternalPtr<UI::Element> textModAuthor;       // +0x08
    Object::InternalPtr<UI::Element> textModVersion;      // +0x10
    Object::InternalPtr<UI::Element> textModDescription;  // +0x18
    Object::InternalPtr<UI::Element> imageConflict;       // +0x20
    Object::InternalPtr<UI::Element> checkLoaded;         // +0x28 a CheckElement
    Object::InternalPtr<UI::Element> rowBorder;           // +0x30 the row's GetBorder() (slot 11)
    Object::InternalPtr<UI::Element> imageIcon;           // +0x38 an ImageElement
    Video::ImageBuffer*         imageBuffer;              // +0x40 the 48x48 thumbnail (owned)
    Graphics::MaterialInstance* material;                 // +0x48 owned
    UI::SpriteSheet*            spriteSheet;              // +0x50 owned
};
static_assert(offsetof(ModDialogEntry, textModName) == 0x00, "ModDialogEntry::textModName @ +0x00");
static_assert(offsetof(ModDialogEntry, textModAuthor) == 0x08, "ModDialogEntry::textModAuthor @ +0x08");
static_assert(offsetof(ModDialogEntry, textModVersion) == 0x10, "ModDialogEntry::textModVersion @ +0x10");
static_assert(offsetof(ModDialogEntry, textModDescription) == 0x18, "ModDialogEntry::textModDescription @ +0x18");
static_assert(offsetof(ModDialogEntry, imageConflict) == 0x20, "ModDialogEntry::imageConflict @ +0x20");
static_assert(offsetof(ModDialogEntry, checkLoaded) == 0x28, "ModDialogEntry::checkLoaded @ +0x28");
static_assert(offsetof(ModDialogEntry, rowBorder) == 0x30, "ModDialogEntry::rowBorder @ +0x30");
static_assert(offsetof(ModDialogEntry, imageIcon) == 0x38, "ModDialogEntry::imageIcon @ +0x38");
static_assert(offsetof(ModDialogEntry, imageBuffer) == 0x40, "ModDialogEntry::imageBuffer @ +0x40");
static_assert(offsetof(ModDialogEntry, material) == 0x48, "ModDialogEntry::material @ +0x48");
static_assert(offsetof(ModDialogEntry, spriteSheet) == 0x50, "ModDialogEntry::spriteSheet @ +0x50");
static_assert(sizeof(ModDialogEntry) == 0x58, "ModDialogEntry is 0x58 (the +0xb8 array stride)");

class ModDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(ModDialog)

public:
    // The Mods list's row tints (ARGB, a row border's SetColor; moddialog.cpp).
    static constexpr uint32_t kRowTintIdle     = 0x10ffffffu;   // disabled / idle row (faint white)
    static constexpr uint32_t kRowTintEnabled  = 0x20c0ffc0u;   // enabled row (green)
    static constexpr uint32_t kRowTintConflict = 0x40ff0000u;   // a conflict (red)

    ModDialog();                                                    // create 0x140012900
    ModDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
              Object::InternalPtr<UI::DialogInstance> parent,
              Object::InternalPtr<MainMenuDialog> parentMenu);      // 0x140070030

    // 0x14006ff00
    ~ModDialog() override;                                         // [0] 0x140012a30
    void OnOpened() override;                                      // [4] 0x14006f150
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x14006ed40

    void RefreshDisplay();                                         // 0x14006eec0
    void ToggleMod(int32_t index);                                 // 0x14006e930
    void MoveToTop(int32_t index);                                 // 0x14006e660
    void ShowDetails(int32_t index);                               // 0x14006e490
    void CloseAndReload();                                         // 0x14006e9b0
    void Apply();                                                  // 0x14006ea70

public:
    Collection::Array<ModDialogEntry>   entries;     // +0xB8 one per installed mod
    Object::InternalPtr<UI::Element>    tableMods;   // +0xC8 "tableMods"
    Object::InternalPtr<MainMenuDialog> parentMenu;  // +0xD0 the owning menu (Apply reloads through it)
};

// The tree's "Mods" opener (mod_dialog_vtables.cpp): the shape of retail's
// MainMenuDialog OpenMod 0x1400b2ea0 plus the harness hooks. No retail address.
void ModDialog_OpenFromMainMenu(MainMenuDialog* mainMenu);

static_assert(offsetof(ModDialog, entries) == 0xB8, "ModDialog +0xb8");
static_assert(offsetof(ModDialog, tableMods) == 0xC8, "ModDialog +0xc8");
static_assert(offsetof(ModDialog, parentMenu) == 0xD0, "ModDialog +0xd0");
static_assert(sizeof(ModDialog) == 0xD8, "ModDialog is 0xd8 (create 0x140012900)");
static_assert(alignof(ModDialog) == 8, "ModDialog is qword-aligned");
