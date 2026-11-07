#pragma once
// ============================================================================
// src/game/Dialog/SaveLoadDialog.h — ENDGAME B.3.8 block DLG
//
// The Save / Load dialog (mode byte at +0xB8: 0 save, 1 load).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "SaveLoadDialog", parent "UI::DialogInstance", ti 0x1401c5cc8
//   create 0x140021160 (MemAlloc(0x160, 8)), createInPlace 0x1400211a0
//   ctors 0x1400b80f0, 0x1400b81c0, 0x1400b8270 (the door the tree registers)
//   table 0x14015f900, 10 slots:
//     [0] 0x1400211b0 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x1400b7d00 OnOpened
//     [5] DialogInstance::OnClosed          [6] 0x1400b6880 OnUpdateLayout
//     [7] 0x1400b79b0 OnUpdate              [8] 0x1400b67e0 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0x160.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "ui/Sprite.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"

class GameUpdateLoop;    // Game/GameUpdateLoop.h
class MenuDialogLoopT;   // Game/MenuDialogLoopT.h
namespace Graphics { class MaterialInstance; }
namespace UI {
class ButtonElement;
class ImageElement;
class PageElement;
class SpriteSheet;
class TableElement;
class TextElement;
}

// One row of the save list (0x40 bytes; PopulateFileList 0x1400b6ce0 builds it
// on the stack in this order and appends it to +0x110; ~SaveLoadDialog
// 0x1400b80f0 deletes +0x30 / +0x38).
struct SaveLoadEntry {
    UI::TextElement*  labelSaveName;             // +0x00
    UI::TextElement*  labelSaveDate;             // +0x08
    UI::ImageElement* imageSaveIcon;             // +0x10
    UI::Element*      borderSave;                // +0x18
    UI::ImageElement* imageCorrupt;              // +0x20
    UI::Element*      selectSave;                // +0x28
    Graphics::MaterialInstance* materialInstance; // +0x30 owned (the thumbnail's)
    UI::SpriteSheet*  spriteSheet;               // +0x38 owned (the thumbnail's)
};
static_assert(sizeof(SaveLoadEntry) == 0x40, "SaveLoadEntry is 0x40 (stride in 0x1400b80f0)");
static_assert(offsetof(SaveLoadEntry, labelSaveName) == 0x0, "SaveLoadEntry::labelSaveName");
static_assert(offsetof(SaveLoadEntry, labelSaveDate) == 0x08, "SaveLoadEntry +0x08");
static_assert(offsetof(SaveLoadEntry, imageSaveIcon) == 0x10, "SaveLoadEntry +0x10");
static_assert(offsetof(SaveLoadEntry, borderSave) == 0x18, "SaveLoadEntry +0x18");
static_assert(offsetof(SaveLoadEntry, imageCorrupt) == 0x20, "SaveLoadEntry +0x20");
static_assert(offsetof(SaveLoadEntry, selectSave) == 0x28, "SaveLoadEntry +0x28");
static_assert(offsetof(SaveLoadEntry, materialInstance) == 0x30, "SaveLoadEntry +0x30");
static_assert(offsetof(SaveLoadEntry, spriteSheet) == 0x38, "SaveLoadEntry +0x38");

class SaveLoadDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(SaveLoadDialog)

public:
    SaveLoadDialog();                                               // create 0x140021160
    SaveLoadDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                   Object::InternalPtr<UI::DialogInstance> parent,
                   uint8_t mode, MenuDialogLoopT* menuLoop,
                   GameUpdateLoop* game);                           // 0x1400b8270

    // 0x1400211b0 minus the free: ~SaveLoadDialog 0x1400b80f0 deletes each row's
    // MaterialInstance / SpriteSheet, then `entries`' own dtor frees the array.
    ~SaveLoadDialog() override;                                    // [0] 0x1400211b0
    void OnOpened() override;                                      // [4] 0x1400b7d00
    // [5] OnClosed: DialogInstance's (SaveSettings), inherited (B38 D3.1).
    void OnUpdateLayout() override;                                // [6] 0x1400b6880
    void OnUpdate(const System::Time& time) override;              // [7] 0x1400b79b0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x1400b67e0

private:
    void InitFields(uint8_t mode, MenuDialogLoopT* menuLoop, GameUpdateLoop* game);
    // Non-virtual members (retail passes the dialog in RCX).
    void PopulateFileList();                                       // 0x1400b6ce0
    void SelectEntry(int index);                                   // 0x1400b6790
    void DoDelete();                                               // 0x1400b5e50
    void DoSave();                                                 // 0x1400b5fd0
    void DoLoad();                                                 // 0x1400b61e0

public:
    uint8_t  mode;             // +0xB8: 0 save, 1 load
    // The four deferred flags are bytes: the ctor zeroes them with one dword
    // store at +0xB9 (0x1400b82c3) and OnUpdate sets refresh / clears delete
    // with one word store at +0xBB; the tree writes the named bytes (H7).
    uint8_t  saveFlag;         // +0xB9: a save was started (close when done)
    uint8_t  loadFlag;         // +0xBA: a load was started (start the game when done)
    uint8_t  refreshFlag;      // +0xBB: re-read the save list
    uint8_t  deleteFlag;       // +0xBC: a delete was started
    uint8_t  _padBD[3];        // +0xBD
    int32_t  selectedIndex;    // +0xC0
    int32_t  saveCount;        // +0xC4: not written by the ctor
    float    rotation;         // +0xC8: spinner angle
    uint8_t  _padCC[4];        // +0xCC
    UI::Sprite sprite;         // +0xD0: Sprite::Sprite() in the ctor (0x1400b82dc)
    MenuDialogLoopT* menuLoop; // +0xE0: the owning front-end menu loop (0 in game)
    GameUpdateLoop*  game;     // +0xE8: the running game (0 at the front end)
    // +0xF0..+0x158: the FindElement results OnOpened (0x1400b7d00) stores.
    UI::PageElement*   mainPage;       // +0xF0
    UI::Element*       labelLoad;      // +0xF8
    UI::Element*       labelSave;      // +0x100
    UI::TableElement*  tableSaveList;  // +0x108
    // +0x110: the save list's rows (~SaveLoadDialog frees them and the array).
    Collection::Array<SaveLoadEntry> entries;   // +0x110 data, +0x118 count, +0x11C capacity
    UI::Element*       textLoading;    // +0x120
    UI::Element*       textSaving;     // +0x128
    UI::Element*       textPleaseWait; // +0x130
    UI::ImageElement*  loadingImage;   // +0x138
    UI::ButtonElement* buttonSave;     // +0x140
    UI::ButtonElement* buttonLoad;     // +0x148
    UI::ButtonElement* buttonCancel;   // +0x150
    UI::ButtonElement* buttonDelete;   // +0x158
};

static_assert(offsetof(SaveLoadDialog, mode) == 0xB8, "SaveLoadDialog +0xb8");
static_assert(offsetof(SaveLoadDialog, saveFlag) == 0xB9, "SaveLoadDialog +0xb9");
static_assert(offsetof(SaveLoadDialog, loadFlag) == 0xBA, "SaveLoadDialog +0xba");
static_assert(offsetof(SaveLoadDialog, refreshFlag) == 0xBB, "SaveLoadDialog +0xbb");
static_assert(offsetof(SaveLoadDialog, deleteFlag) == 0xBC, "SaveLoadDialog +0xbc");
static_assert(offsetof(SaveLoadDialog, selectedIndex) == 0xC0, "SaveLoadDialog +0xc0");
static_assert(offsetof(SaveLoadDialog, saveCount) == 0xC4, "SaveLoadDialog +0xc4");
static_assert(offsetof(SaveLoadDialog, rotation) == 0xC8, "SaveLoadDialog +0xc8");
static_assert(offsetof(SaveLoadDialog, sprite) == 0xD0, "SaveLoadDialog +0xd0");
static_assert(offsetof(SaveLoadDialog, menuLoop) == 0xE0, "SaveLoadDialog +0xe0");
static_assert(offsetof(SaveLoadDialog, game) == 0xE8, "SaveLoadDialog +0xe8");
static_assert(offsetof(SaveLoadDialog, mainPage) == 0xF0, "SaveLoadDialog +0xf0");
static_assert(offsetof(SaveLoadDialog, labelLoad) == 0xF8, "SaveLoadDialog +0xf8");
static_assert(offsetof(SaveLoadDialog, labelSave) == 0x100, "SaveLoadDialog +0x100");
static_assert(offsetof(SaveLoadDialog, tableSaveList) == 0x108, "SaveLoadDialog +0x108");
static_assert(offsetof(SaveLoadDialog, entries) == 0x110, "SaveLoadDialog +0x110");
static_assert(offsetof(SaveLoadDialog, textLoading) == 0x120, "SaveLoadDialog +0x120");
static_assert(offsetof(SaveLoadDialog, textSaving) == 0x128, "SaveLoadDialog +0x128");
static_assert(offsetof(SaveLoadDialog, textPleaseWait) == 0x130, "SaveLoadDialog +0x130");
static_assert(offsetof(SaveLoadDialog, loadingImage) == 0x138, "SaveLoadDialog +0x138");
static_assert(offsetof(SaveLoadDialog, buttonSave) == 0x140, "SaveLoadDialog +0x140");
static_assert(offsetof(SaveLoadDialog, buttonLoad) == 0x148, "SaveLoadDialog +0x148");
static_assert(offsetof(SaveLoadDialog, buttonCancel) == 0x150, "SaveLoadDialog +0x150");
static_assert(offsetof(SaveLoadDialog, buttonDelete) == 0x158, "SaveLoadDialog +0x158");
static_assert(sizeof(SaveLoadDialog) == 0x160, "SaveLoadDialog is 0x160 (create 0x140021160)");
static_assert(offsetof(SaveLoadDialog, _padBD) == 0xBD, "SaveLoadDialog::_padBD");
static_assert(offsetof(SaveLoadDialog, _padCC) == 0xCC, "SaveLoadDialog::_padCC");
static_assert(alignof(SaveLoadDialog) == 8, "SaveLoadDialog is qword-aligned");
