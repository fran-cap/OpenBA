// src/game/Dialog/saveloaddialog.cpp
// Clean reimplementation of SaveLoadDialog methods.
//
// SaveLoadDialog (0x160 bytes): Save/load file browser dialog.
// Extends DialogInstance (0xB8 base). Contains UI::Sprite at +0xD0.
//
// Layout:
//   +0xB8  mode (byte): 0 = save mode, nonzero = load mode
//   +0xB9  saveFlag (byte): set to 1 when save completes
//   +0xBA  loadFlag (byte): set to 1 when load completes
//   +0xBB  refreshFlag (byte): set to 1 to trigger file list refresh
//   +0xBC  deleteFlag (byte): set to 1 when delete completes
//   +0xC0  selectedIndex (int32): currently selected save slot (-1 = none)
//   +0xC4  saveCount (int32): number of save files
//   +0xC8  rotation (float): loading spinner angle
//   +0xD0  sprite (16 bytes): UI::Sprite sub-object
//   +0xE0  loaderLoop1 (ptr): used for save/load transition
//   +0xE8  loaderLoop2 (ptr): used for save/load transition
//   +0xF0  mainPage (InternalBasePtr): PageElement for page switching
//   +0xF8  labelLoad (InternalBasePtr)
//   +0x100 labelSave (InternalBasePtr)
//   +0x108 tableSaveList (InternalBasePtr)
//   +0x110 entryArray (ptr): dynamic array of 0x40-byte save entries
//   +0x118 entryCount (int32)
//   +0x120 textLoading (InternalBasePtr)
//   +0x128 textSaving (InternalBasePtr)
//   +0x130 textPleaseWait (InternalBasePtr)
//   +0x138 loadingImage (InternalBasePtr)
//   +0x140 buttonSave (InternalBasePtr)
//   +0x148 buttonLoad (InternalBasePtr)
//   +0x150 buttonCancel (InternalBasePtr)
//   +0x158 buttonDelete (InternalBasePtr)

#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/MessageDialog.h"
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TableElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/PageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ElementDescription.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "save/SaveLoad.h"   // ENDGAME B.3.7 callers (K1)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/SaveLoadDialog.h"
#include "Game/GameUpdateLoop.h"       // ENDGAME B.3.9 EXA: the real loop classes
#include "Game/GameLoadDialogLoopT.h"
#include "Game/GameLoaderLoop.h"
#include "Dialog/ui_dialogs.h"
// ENDGAME B.3.5 batch K1: the runtime type headers come FIRST — they reach
// src/engine/volume/Volume.h, whose global-scope `struct Rectangle` is hidden by
// <wingdi.h>'s `Rectangle()` once <windows.h> has been seen through resolve.h.
#include "video/ImageBuffer.h"
#include "graphics/Material.h"
#include "options.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Dialog/Dialogs.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "io/Modification.h"   // ENDGAME B.3.4 caller batch P
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers: System::Time
#include "Dialog/exe_bodies.h"
#include "Game/exe_constants.h"   // B3R: kRdata_*
#include "system/Configuration.h"   // B3R: System::Configuration::GetVersionString
#include "system/MemoryAllocator.h"   // B3R: the allocator doors


#include "Game/MenuDialogLoopT.h"
#include "ext/registry.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): DoLoad's `ext:` pseudo-mods
#include "ext/load_notice.h"  // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTNOTICE.md): their notices, Dialog/Extensions.rsc:extension*
#include "ext/resources/text_provider.h"  // EXT SEAM (NOT RETAIL, docs/extensions/KITFREE.md): InitText, the notices' provider
#include "ext/quick_screens/quick_screens.h"  // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): ext.quick_screens' minimum operation time
#include <vector>
#include "ui/ButtonElement.h"


// ============================================================
// Helper: find a named element of the dialog as the UI class it is read as
// (OnOpened 0x1400b7d00 stores each FindElement result in place)
// ============================================================
template <class T>
static T* FindDialogElem(SaveLoadDialog* dialog, const char* name) {
    System::String nameStr(name);
    return static_cast<T*>(dialog->FindElement(nameStr).Get());
}

// ============================================================
// Helper: find a named element in an Element tree (a cloned list row)
// ============================================================
template <class T>
static T* FindChildElem(UI::Element* parentElem, const char* name) {
    System::String nameStr(name);
    return static_cast<T*>(parentElem->FindElement(nameStr).Get());
}

// ============================================================
// Save entry structure (0x40 bytes per entry in the file list):
// SaveLoadEntry (Dialog/SaveLoadDialog.h).
// ============================================================

// ============================================================
// PopulateFileList (0x1400b6ce0): Build/refresh the save file list UI
// ============================================================
// Called after the save scan operation completes. Clears the old entry
// array, resizes the table element, then for each save file:
//   1. Clones the dialog template row element
//   2. Finds child elements (name, date, icon, border, etc.)
//   3. If save has a valid thumbnail: sets sprite directly from SaveInfo
//   4. If save has raw image data: creates MaterialInstance + SpriteSheet,
//      wraps the image as a texture, builds a Sprite and assigns it
//   5. Sets save name and date text parameters
//   6. Fills remaining empty rows with dimmed placeholder entries
//
// Original: 0x1400b6ce0 (442 lines in Ghidra). Retail type-checks each row
// child (TextElement / ImageElement / Element) and the material; the tree's
// port does not (LEFT, unchanged).

void SaveLoadDialog::PopulateFileList() {
    // --- Phase 1: Destroy old entries (cleanup MaterialInstance/SpriteSheet objects) ---
    int oldEntryCount = entries._size;

    for (int i = 0; i < oldEntryCount; i++) {
        SaveLoadEntry& oldEntry = entries[i];

        // Destroy MaterialInstance at +0x30 (scalar deleting destructor, vtable[0])
        Graphics::MaterialInstance* matInst = oldEntry.materialInstance;
        if (matInst) {
            delete matInst;   // slot 0, free
        }

        // Destroy SpriteSheet at +0x38 if materialInstance was non-null
        if (oldEntry.materialInstance != nullptr) {
            UI::SpriteSheet* sheet = oldEntry.spriteSheet;
            if (sheet) {
                delete sheet;   // slot 0, free
            }
        }
    }

    // Free the entry array and reset
    System::MemoryAllocator::MemFree(entries._data);
    entries._data = nullptr;
    entries._size = 0;       // count + capacity = 0 (one qword store in retail)
    entries._capacity = 0;

    // --- Phase 2: Calculate table dimensions ---
    int saveCountNow = saveCount;
    int minSlots = (saveCountNow < 6) ? 6 : saveCountNow;
    int totalSlots = minSlots + 2;
    // Round DOWN to a multiple of 3: 0x1400b6d9c `IMUL 0x55555556; SHR 0x1f; ADD` is a
    // signed truncating /3, then `LEA R14D,[RDX+RDX*2]` multiplies back. R14D is kept
    // ([RBP+0x120]) as the filler loop's bound below.
    totalSlots = (totalSlots / 3) * 3;

    // Set table size: (totalSlots/3 + 1) rows, 3 columns
    tableSaveList->SetSize(totalSlots / 3 + 1, 3);

    float uvScale = kRdata_14017214c;   // 0x14017214c = 1.0f (was g_fUVScale; its fallback never ran)
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;

    // --- Phase 3: Populate save file entries ---
    for (int i = 0; i < saveCountNow; i++) {
        const Save::SaveLoad::SaveInfo& saveInfo = saveLoad->GetSaveInfo(i);

        // Clone template element from dialog
        Object::ExternalPtr<UI::ElementDescription> templateObj = _dialog->GetTemplate(0);

        // Clone via vtable[8]: templateObj->CreateInstance(&clone, dialog)
        // (= UI::ElementDescription slot 8 CreateInstance, RETSLOT per B6-2)
        Object::InternalPtr<UI::Element> clone;
        templateObj->CreateInstance(&clone, _dialog);
        UI::Element* clonedElem = clone.Get();

        // Add cloned element to table at index i
        Object::InternalPtr<UI::Element> child(clonedElem);
        tableSaveList->SetChild(&child, i);

        // Initialize entry struct (8 fields, all zero initially)
        SaveLoadEntry entry = {};

        // Find child elements in the cloned row
        entry.labelSaveName = FindChildElem<UI::TextElement>(clonedElem, "labelSaveName");
        entry.labelSaveDate = FindChildElem<UI::TextElement>(clonedElem, "labelSaveDate");
        entry.imageSaveIcon = FindChildElem<UI::ImageElement>(clonedElem, "imageSaveIcon");
        entry.borderSave    = FindChildElem<UI::Element>(clonedElem, "borderSave");
        entry.imageCorrupt  = FindChildElem<UI::ImageElement>(clonedElem, "imageCorrupt");
        entry.selectSave    = FindChildElem<UI::Element>(clonedElem, "selectSave");

        // Set imageSaveIcon flags: disable mouse-over highlight (0x10000)
        entry.imageSaveIcon->SetFlagsOnOff(0x10000, 0);

        // Set imageSaveIcon element ID for selection tracking (ID = i + 4)
        entry.imageSaveIcon->SetElementId(i + 4);

        // Check if SaveInfo has a valid Sprite (thumbnail)
        bool hasSpriteThumb = saveInfo.thumbnail.IsValid();

        if (hasSpriteThumb) {
            // --- Direct sprite from save info ---
            // Copy the 16-byte Sprite from SaveInfo
            UI::Sprite thumb = saveInfo.thumbnail;

            entry.imageSaveIcon->SetSprite(&thumb);

            // Hide imageCorrupt overlay
            entry.imageCorrupt->SetFlagsOnOff(1, 0);
        } else {
            // --- Build sprite from raw image data ---
            // Allocate MaterialInstance (0x60 bytes)
            void* matInst = System::MemoryAllocator::MemAlloc(0x60, 8);
            Graphics::MaterialInstance* matInstFinal = nullptr;
            if (matInst) {
                matInstFinal = ::new (matInst) Graphics::MaterialInstance();   // B.3.9 EXA: the real class (exe table 0x14015c400 is its own)
            }
            entry.materialInstance = matInstFinal;

            // Load UIMaterialNoBlend material
            System::Filename matFilename("Material/UI/UIMaterialNoBlend.rsc");
            ::Object::External* matExternal =
                ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(matFilename);

            const ::Object::External* matResource = nullptr;
            if (matExternal) {
                // IsDerivedFrom check for Material type
                if (::Object::IsDerivedFromNullSafe(matExternal, UI::TextElement::TypeId())) {
                    matResource = matExternal;
                }
            }
            // Note: the IsDerivedFrom check in the original uses the same _typeInfo_exref
            // for all checks, but here we need the Material type. Since the original code
            // works correctly, the ExternalFactory::FindOrLoad result is passed directly.
            // The IsDerivedFrom is checking MaterialExternal type.
            matInstFinal->SetMaterial(
                Object::ExternalPtr<Graphics::Material>(
                    static_cast<Graphics::Material*>(matExternal)));

            // Add texture: "diffuse" -> saveInfo's ImageBuffer at +0x18
            System::String diffuseStr("diffuse");
            matInstFinal->AddTexture(
                diffuseStr,
                Object::ExternalPtr<Video::ImageBuffer>(saveInfo.imageBuffer));

            matInstFinal->BuildParameters();

            // Allocate SpriteSheet (0x58 bytes)
            void* sheet = System::MemoryAllocator::MemAlloc(0x58, 8);
            UI::SpriteSheet* sheetFinal = nullptr;
            if (sheet) {
                sheetFinal = ::new (sheet) UI::SpriteSheet();   // B.3.9 EXA: the real class (exe table 0x14015c3b0 is its own)
            }

            // Set material on spritesheet
            sheetFinal->m_field30 = matInstFinal;

            // Get image dimensions
            Video::ImageBuffer* imageBuffer = saveInfo.imageBuffer;
            int imgHeight = imageBuffer->Height();
            int imgWidth = imageBuffer->Width();

            // Set spritesheet dimensions
            System::String emptyStr1(""), emptyStr2("");
            sheetFinal->m_groupName = emptyStr1;
            sheetFinal->m_textureName = emptyStr2;
            sheetFinal->m_field48 = imgWidth;
            sheetFinal->m_field4C = imgHeight;
            sheetFinal->m_field50 = false;   // the tree's qword zero at +0x50 (the flag and its pad)

            // Add sprite info (full image as single sprite)
            System::String nameStr1(""), nameStr2("");

            imageBuffer = saveInfo.imageBuffer;
            imgHeight = imageBuffer->Height();
            imgWidth = imageBuffer->Width();
            sheetFinal->AddSpriteInfo(nameStr1, nameStr2, 0, 0, imgWidth, imgHeight);

            // Set sprite info with UV coordinates
            imageBuffer = saveInfo.imageBuffer;
            int h = imageBuffer->Height();
            int w = imageBuffer->Width();
            sheetFinal->SetSpriteInfo(0, w, h, 0.0f, 0.0f, uvScale, uvScale);

            // Create Sprite from SpriteSheet
            UI::Sprite thumb(Object::ExternalPtr<UI::SpriteSheet>(sheetFinal), 0);

            entry.imageSaveIcon->SetSprite(&thumb);

            entry.spriteSheet = sheetFinal;

            // Check save validity: SaveInfo::field44 (+0x44, the format version)
            if (saveInfo.field44 == 2) {
                // Valid save: hide imageCorrupt overlay
                entry.imageCorrupt->SetFlagsOnOff(1, 0);
            } else {
                // Invalid/corrupt: dim the icon
                entry.imageSaveIcon->SetColor(0xff808080);
            }
        }

        // --- Set text parameters if save has valid data ---
        if (saveInfo.saveIndex >= 0) {
            if (saveInfo.field44 == 2) {
                // Valid save: show formatted date string
                // ?AsString@DateTime@System@@ returns by value.
                System::String dateStr = saveInfo.sortKey.AsString();
                entry.labelSaveDate->SetParameter(0, dateStr);
            } else {
                // Corrupt save: show "Corrupt" text from the globalNames table (+0xD8)
                System::String corruptKey("Corrupt");
                UI::String uiStr(g_gameContainer->globalNames, corruptKey);
                System::String sysStr(uiStr.operator System::String());
                entry.labelSaveDate->SetParameter(0, sysStr);
            }

            // Set save name: SaveInfo::description (+0x10)
            entry.labelSaveName->SetParameter(0, saveInfo.description);
        }

        // Push entry into the row array at +0x110 (was FUN_1400211e0's own
        // inlined SetSize; the MaterialInstance/SpriteSheet pointers it carries
        // are destroyed separately in Phase 1 above and by ~SaveLoadDialog, not
        // by this growth).
        entries.SetSize(entries.GetSize() + 1, entry);
    }

    // --- Phase 4: Fill remaining empty rows ---
    for (int i = saveCountNow; i < totalSlots; i++) {
        // Clone template element
        Object::ExternalPtr<UI::ElementDescription> templateObj = _dialog->GetTemplate(0);

        Object::InternalPtr<UI::Element> clone;
        templateObj->CreateInstance(&clone, _dialog);
        UI::Element* clonedElem = clone.Get();

        // Add to table
        Object::InternalPtr<UI::Element> child(clonedElem);
        tableSaveList->SetChild(&child, i);

        // Initialize empty entry struct
        SaveLoadEntry entry = {};

        // Find child elements (same lookups)
        entry.labelSaveName = FindChildElem<UI::TextElement>(clonedElem, "labelSaveName");
        entry.labelSaveDate = FindChildElem<UI::TextElement>(clonedElem, "labelSaveDate");
        entry.imageSaveIcon = FindChildElem<UI::ImageElement>(clonedElem, "imageSaveIcon");
        entry.borderSave    = FindChildElem<UI::Element>(clonedElem, "borderSave");
        entry.imageCorrupt  = FindChildElem<UI::ImageElement>(clonedElem, "imageCorrupt");
        entry.selectSave    = FindChildElem<UI::Element>(clonedElem, "selectSave");

        // Hide imageCorrupt and selectSave overlays for empty slots
        entry.imageCorrupt->SetFlagsOnOff(1, 0);
        entry.selectSave->SetFlagsOnOff(1, 0);

        // Dim border (SetColor via vtable[10] = 0x80ffffff)
        {
            entry.borderSave->SetColor(0x80ffffff);
        }

        // Dim icon (SetColor via vtable[10] = 0x20ffffff)
        {
            entry.imageSaveIcon->SetColor(0x20ffffff);
        }
    }
}

// ============================================================
// Startup (vtable +0x20): Build save/load UI, populate file list
// ============================================================
// Original: 0x1400b7d00
//
// Finds 12 named UI elements and stores them in SaveLoadDialog fields.
// Sets button element IDs: buttonSave=1, buttonLoad=2, buttonCancel=0, buttonDelete=3.
// Sets cancel button's shortcut key (ButtonElement::_keyIndex = 8).
// Sets refresh flag (+0xBB = 1) to trigger file list population.
// Copies the container's SaveLoadEmpty sprite (+0xB8) to the SaveLoad singleton.
// Calls SetMinOperationTime(0.75f) and SetOperation(1, mode).

void SaveLoadDialog::OnOpened() {
    // --- Find all named elements ---
    labelLoad      = FindDialogElem<UI::Element>(this, "labelLoad");
    labelSave      = FindDialogElem<UI::Element>(this, "labelSave");
    buttonSave     = FindDialogElem<UI::ButtonElement>(this, "buttonSave");
    buttonLoad     = FindDialogElem<UI::ButtonElement>(this, "buttonLoad");
    buttonCancel   = FindDialogElem<UI::ButtonElement>(this, "buttonCancel");
    buttonDelete   = FindDialogElem<UI::ButtonElement>(this, "buttonDelete");
    mainPage       = FindDialogElem<UI::PageElement>(this, "mainPage");
    textLoading    = FindDialogElem<UI::Element>(this, "textLoading");
    textSaving     = FindDialogElem<UI::Element>(this, "textSaving");
    textPleaseWait = FindDialogElem<UI::Element>(this, "textPleaseWait");
    loadingImage   = FindDialogElem<UI::ImageElement>(this, "loadingImage");
    tableSaveList  = FindDialogElem<UI::TableElement>(this, "tableSaveList");

    // --- Set button element IDs ---
    buttonSave->SetElementId(1);
    buttonLoad->SetElementId(2);
    buttonCancel->SetElementId(0);
    buttonDelete->SetElementId(3);

    // --- Set cancel button's shortcut key ---
    buttonCancel->_keyIndex = 8;

    // --- Set refresh flag ---
    refreshFlag = 1;

    // --- Copy the container's SaveLoadEmpty sprite (+0xB8) to the SaveLoad singleton ---
    UI::Sprite spriteCopy = g_gameContainer->spriteSaveLoadEmpty;
    ::Save::SaveLoad::_singleton->SetSaveSprite(&spriteCopy);

    // --- Set minimum operation time (0.75f seconds) ---
    System::Time timeVal;
    RtSystem_Time_Ctor(&timeVal, 0.75f);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): ext.quick_screens ends the list's
    // floor once the scan is done (SaveLoad::Update runs it only with the
    // persistence worker idle). docs/extensions/handins/SPLASH.md.
    if (Ext::Ext().quick_screens) timeVal._ticks = Ext::QuickScreens::kNoMinOperationTime;
    ::Save::SaveLoad::_singleton->SetMinOperationTime(timeVal);

    // --- Start operation: 1 (list/scan), param = -2 or -3 based on mode ---
    int opParam = -2 - (int)(mode != 0);
    ::Save::SaveLoad::_singleton->SetOperation(static_cast<Save::SaveLoad::Operation>(1), opParam);
}

// ============================================================
// Update (vtable +0x30): Refresh file list, update preview
// ============================================================
// Original: 0x1400b6880
//
// Two main states:
// 1. Operation active (op != 0) OR any flag set: Show loading page (page 0),
//    rotate loading spinner, show/hide text based on operation type.
// 2. Idle (op == 0, no flags): Show main page (page 1), enable/disable buttons,
//    iterate save entries to show/hide selection highlights.

void SaveLoadDialog::OnUpdateLayout() {
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
    if (!saveLoad) return;

    // --- Show/hide load/save labels based on mode ---
    // labelLoad (+0xF8): visible when mode == 0 (save mode shows "load" label? no...)
    // Actually: labelLoad visible when NOT save mode, labelSave visible when save mode
    uint32_t isLoadMode = (uint32_t)(mode == 0);
    uint32_t isSaveMode = (uint32_t)(mode != 0);
    if (labelLoad) labelLoad->SetFlagsOnOff(isLoadMode, 0);
    if (labelSave) labelSave->SetFlagsOnOff(isSaveMode, 0);

    // buttonSave (+0x140): visible in save mode
    if (buttonSave) buttonSave->SetFlagsOnOff(isSaveMode, 0);
    // buttonLoad (+0x148): visible in load mode
    if (buttonLoad) buttonLoad->SetFlagsOnOff(isLoadMode, 0);

    int operation = saveLoad->GetOperation();

    // Check if any operation is in progress or any deferred flag is set
    bool anyFlagsSet = (refreshFlag != 0) ||
                       (saveFlag != 0) ||
                       (loadFlag != 0) ||
                       (deleteFlag != 0);

    if (operation == 0 && !anyFlagsSet) {
        // --- IDLE STATE: show main page, update buttons and entry list ---
        if (mainPage) mainPage->SetActivePage(1);

        // Enable/disable buttonLoad based on selected save validity
        int selectedIdx = selectedIndex;
        int saveCountNow = saveCount;
        bool loadEnabled = false;
        if (saveCountNow >= 1 && selectedIdx >= 0) {
            const Save::SaveLoad::SaveInfo& saveInfo = saveLoad->GetSaveInfo(selectedIdx);
            // SaveInfo::field44 == 2 means valid save
            if (saveInfo.field44 == 2) {
                loadEnabled = true;
            }
        }

        // buttonLoad: SetEnabled via vtable[8]
        if (buttonLoad) {
            buttonLoad->Enable(loadEnabled ? 1 : 0);
        }

        // buttonSave: always enabled
        if (buttonSave) {
            buttonSave->Enable(1);
        }

        // buttonCancel: always enabled
        if (buttonCancel) {
            buttonCancel->Enable(1);
        }

        // Check selected save for delete eligibility
        if (selectedIdx >= 0) {
            saveLoad->GetSaveInfo(selectedIdx);
        }

        // buttonDelete: SetEnabled via vtable[8]
        if (buttonDelete) {
            buttonDelete->Enable((uint64_t)(selectedIdx >= 0));
        }

        // --- Iterate save entries: show/hide selection highlighting ---
        if (saveCountNow < 1) return;

        for (int i = 0; i < saveCountNow; i++) {
            SaveLoadEntry& entry = entries[i];
            bool notSelected = (i != selectedIdx);

            // selectSave — shown when selected, hidden otherwise
            UI::Element* selectElem = entry.selectSave;
            if (selectElem) selectElem->SetFlagsOnOff(notSelected ? 1 : 0, notSelected ? 0 : 1);

            // borderSave — shown when NOT selected, hidden when selected
            UI::Element* borderElem = entry.borderSave;
            if (borderElem) borderElem->SetFlagsOnOff(notSelected ? 0 : 1, notSelected ? 1 : 0);
        }
        return;
    }

    // --- OPERATION IN PROGRESS: show loading page ---
    // Rotate loading spinner image
    if (loadingImage) loadingImage->SetRotation(rotation);

    // Switch to loading page (page 0)
    if (mainPage) mainPage->SetActivePage(0);

    // Disable all four buttons
    if (buttonLoad) {
        buttonLoad->Enable(0);
    }
    if (buttonSave) {
        buttonSave->Enable(0);
    }
    if (buttonCancel) {
        buttonCancel->Enable(0);
    }
    if (buttonDelete) {
        buttonDelete->Enable(0);
    }

    // --- Show/hide status text based on operation type ---
    // Operation types: 1 = listing, 2 = saving, 4 = deleting
    if (operation == 1) {
        // Listing: show textPleaseWait, hide textLoading, hide textSaving
        if (textPleaseWait) textPleaseWait->SetFlagsOnOff(0, 1);
        if (textLoading) textLoading->SetFlagsOnOff(1, 0);
        if (textSaving) textSaving->SetFlagsOnOff(1, 0);
    } else if (operation == 2) {
        // Saving: hide textPleaseWait, show textLoading, hide textSaving
        if (textPleaseWait) textPleaseWait->SetFlagsOnOff(1, 0);
        if (textLoading) textLoading->SetFlagsOnOff(0, 1);
        if (textSaving) textSaving->SetFlagsOnOff(1, 0);
    } else if (operation == 4) {
        // Deleting: hide textPleaseWait, hide textLoading, show textSaving
        if (textPleaseWait) textPleaseWait->SetFlagsOnOff(1, 0);
        if (textLoading) textLoading->SetFlagsOnOff(1, 0);
        if (textSaving) textSaving->SetFlagsOnOff(0, 1);
    }
}

// ============================================================
// Load (vtable +0x38): Process timed operations, trigger state transitions
// ============================================================
// Original: 0x1400b79b0
//
// Timer/state machine called every frame.
//
// Phase 1 — While Save::SaveLoad operation is non-zero (file I/O in progress):
//   Accumulate rotation angle at +0xC8 (180 deg/sec from time param).
//
// Phase 2 — When operation reaches 0 (I/O complete), process one deferred flag:
//
//   deleteFlag (+0xBC): delete completed.
//     Set refreshFlag, clear deleteFlag (one word store in retail), start re-list operation.
//
//   refreshFlag (+0xBB): file list needs refresh.
//     Update saveCount (+0xC4), clamp selectedIndex (+0xC0), populate file list, clear flag.
//
//   saveFlag (+0xB9): save completed.
//     Close this dialog via UI::UserInterface::CloseDialog, clear flag.
//
//   loadFlag (+0xBA): load completed — transition to game.
//     Close this dialog.
//     Allocate GameUpdateLoop (0x160 bytes), construct via GameUpdateLoop ctor.
//     Allocate GameLoadDialogLoopT (0xb60 bytes), construct via DialogLoaderLoopT base ctor,
//       then patch vtable to exe subclass at PTR_FUN_140158800, set extra fields.
//     Allocate outer LoaderLoop (0x48 bytes), construct via LoaderLoop ctor with 0.0s delay,
//       patch vtable to PTR_FUN_1401588d0.
//     Set the outer LoaderLoop as the next loop on the menu loop / the game (+0xE0/+0xE8).
//     Clear loadFlag.

void SaveLoadDialog::OnUpdate(const System::Time& time) {
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;

    // Phase 1: accumulate spinner rotation while I/O is in progress
    int operation = saveLoad->GetOperation();
    if (operation != 0) {
        float elapsed = time.AsSeconds();
        rotation += elapsed * 180.0f;
    }

    // Re-check — may have just completed this frame
    operation = saveLoad->GetOperation();
    if (operation != 0) return;

    // Phase 2: I/O complete, process one deferred flag (priority order matches original)

    // deleteFlag (+0xBC): re-trigger list scan
    if (deleteFlag != 0) {
        refreshFlag = 1;   // retail: one word store at +0xBB (refresh 1, delete 0)
        deleteFlag = 0;
        saveLoad->SetOperation(static_cast<Save::SaveLoad::Operation>(1), -2 - (int)(mode != 0));
        return;
    }

    // refreshFlag (+0xBB): populate file list
    if (refreshFlag != 0) {
        int count = saveLoad->GetSaveCount();
        saveCount = count;
        if (count <= selectedIndex) {
            selectedIndex = count - 1;
        }
        PopulateFileList();
        refreshFlag = 0;
        return;
    }

    // saveFlag (+0xB9): save done, close dialog (no game transition needed)
    if (saveFlag != 0) {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
        saveFlag = 0;
        return;
    }

    // loadFlag (+0xBA): load done — close dialog and transition to game load screen
    if (loadFlag != 0) {
        LoadTime::Stamp("load.chain");   // NOT RETAIL: print-only (the read is done; the loop chain is built)
        // Close the SaveLoad dialog
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);

        // ENDGAME B.3.9 EXA: the three real loop classes, built as retail's
        // 0x1400b79b0 builds them.
        // --- Step 1: the inner GameUpdateLoop (0x160 bytes), ctor 0x140098060 ---
        // Retail: (savedState = GetSaveState(), &InternalBasePtr(0), -1, name,
        // 0, 0, p8 = 1, 0, 0, 0). p8 = 1 writes +0xD0 = 0.
        void* gameLoop = System::MemoryAllocator::MemAlloc(0x160, 8);
        Engine::UpdateLoop* pGameLoop = nullptr;
        if (gameLoop) {
            Save::SavedState* savedState = saveLoad->GetSaveState();
            // Retail (0x1400b7b1b): `MOV [RSP+0xc0],RBX` with RBX = 0 — an EMPTY
            // String (null COW pointer), not String("") (a 1-byte "" buffer).
            const System::String nameStr;
            pGameLoop = ::new (gameLoop) GameUpdateLoop(
                savedState, /*scenarioIbp=*/Object::InternalPtr<Achievement::AchievementInstance>(), /*scenarioIndex=*/-1, nameStr,
                0, 0, /*p8=*/true, nullptr, nullptr, nullptr);
        }

        // --- Step 2: the GameLoadDialogLoopT (0xb60 bytes) — the subclass ctor
        // retail inlines here ---
        void* dlLoop = System::MemoryAllocator::MemAlloc(0xb60, 4);
        Engine::UpdateLoop* pDlLoop = nullptr;
        if (dlLoop) {
            System::Time delayTime{};
            RtSystem_Time_Ctor(&delayTime, 1.0f);
            System::String dialogNameStr;
            GetLoadingSoundString(&dialogNameStr);
            pDlLoop = ::new (dlLoop) GameLoadDialogLoopT(
                dialogNameStr, pGameLoop, delayTime);
        }

        // --- Step 3: the outer GameLoaderLoop (0x48 bytes) ---
        void* outerLoop = System::MemoryAllocator::MemAlloc(0x48, 4);
        GameLoaderLoop* pOuterLoop = nullptr;
        if (outerLoop) {
            System::Time zeroTime{};
            RtSystem_Time_Ctor(&zeroTime, 0.0f);
            pOuterLoop = ::new (outerLoop) GameLoaderLoop(pDlLoop, zeroTime);
        }

        // --- Step 4: Set the outer LoaderLoop as the engine's next loop ---
        if (menuLoop == nullptr) {
            game->SetEngineNextLoop(pOuterLoop);
        } else {
            menuLoop->SetLoaderNextLoop(pOuterLoop);
        }

        loadFlag = 0;
    }
}

// ============================================================
// Unattended-harness escape for the modal dialogs on the save/load path
// ============================================================
// UI::UserInterface::ShowDialog (Runtime 0x18011fd10) is a NESTED MODAL LOOP: it
// re-enters Engine::MainLoop::Frame until the dialog is removed. No autostart driver
// can reach it -- AutoStart::TickMenu runs INSIDE that nested loop with its phase
// already Done -- so an unanswered modal here is an unbounded stall that reads exactly
// like "the menu never advances and no world is created". Same shape and same remedy as
// ModDialog::Apply BANISHED_MODAL_ANSWER (src/game/Dialog/moddialog.cpp).
// 2 is the "confirm" answer every caller on this path tests for.
// True when a harness env var says nobody is at the keyboard. Same list as
// HarnessGuardUnattendedBoot (src/app/main.cpp) -- kept in step deliberately: both
// answer the question "may we take a decision retail would put to the player?".
// The same six drivers src/app/main.cpp's unattended-boot guard keys on;
// options.cpp folds them into one field.
static bool SaveLoad_UnattendedRun() { return Opt().unattendedBoot; }

static int SaveLoad_ModalShowDialog(UI::UserInterface* ui,
                                    Object::InternalPtr<UI::DialogInstance>* dlgPtr,
                                    const char* what) {
    const bool forced = Opt().modalAnswer;
    if (forced || SaveLoad_UnattendedRun()) {
        int answer = forced ? Opt().modalAnswerValue : 2;
        printf("[SaveLoadDialog] unattended run: answering modal \"%s\" with %d "
               "(ShowDialog skipped; override with BANISHED_MODAL_ANSWER)\n",
               what ? what : "?", answer);
        fflush(stdout);
        return answer;
    }
    printf("[SaveLoadDialog] entering MODAL ShowDialog \"%s\" -- re-enters MainLoop::Frame "
           "and blocks until a human clicks; set BANISHED_MODAL_ANSWER=2 to run unattended\n",
           what ? what : "?");
    fflush(stdout);
    int r = ui->ShowDialog(dlgPtr);
    printf("[SaveLoadDialog] modal \"%s\" answered %d\n", what ? what : "?", r);
    fflush(stdout);
    return r;
}

// ============================================================
// Helper: create a confirm-style MessageDialog from a .rsc path string
// ============================================================
// Loads the External resource by filename, verifies it is a Dialog,
// allocates a 0xC0-byte MessageDialog, constructs it and calls UI::ShowDialog.
// Returns the ShowDialog result (2 = confirmed/Yes, other = cancelled).
//
// This pattern appears identically in DoDelete, DoSave, and DoLoad.
static int SaveLoad_ShowConfirmDialog(SaveLoadDialog* self, const char* rscPath) {
    // Load the .rsc External resource
    System::Filename filename(rscPath);
    const ::Object::External* externalRes =
        ::Object::ExternalFactory::_singleton->FindOrLoad(filename);

    // IsDerivedFrom check — ensure it's a Dialog (matches original _typeInfo_exref)
    const UI::Dialog* checkedRes = nullptr;
    if (externalRes) {
        bool isDialog = ::Object::IsDerivedFromNullSafe(externalRes, UI::Dialog::TypeId());
        if (isDialog) checkedRes = static_cast<const UI::Dialog*>(externalRes);
    }

    // Allocate 0xC0 bytes for the MessageDialog
    void* mem = System::MemoryAllocator::MemAlloc(0xC0, 8);
    MessageDialog* dlg = nullptr;
    if (mem) {
        // MessageDialog's real ctor (0x14006d300: DialogInstance(ui, externalRes, null
        // parent, 0, 0, 0), its own table, +0xb8 untouched) - the bytes the tree's
        // DialogInstance ctor door + MessageDialogAddr::Vtable stamp built (B38 B8-5).
        dlg = ::new (mem) MessageDialog(self->_ui, checkedRes,
                                        Object::InternalPtr<UI::DialogInstance>());
    }

    // ShowDialog(ui, InternalPtr<DialogInstance>)
    Object::InternalPtr<UI::DialogInstance> dlgPtr(dlg);
    int result = SaveLoad_ModalShowDialog(self->_ui, &dlgPtr, rscPath);
    dlgPtr.DeletePtr();
    return result;
}

// ============================================================
// DoDelete (0x1400b5e50): Show confirmDelete dialog, then trigger delete operation.
// ============================================================
// Shows "Dialog/SaveLoad.rsc:confirmDelete" dialog.
// If user confirms (result == 2): sets deleteFlag (+0xBC = 1), starts operation 5.
// Original: 0x1400b5e50.

void SaveLoadDialog::DoDelete() {
    int result = SaveLoad_ShowConfirmDialog(this, "Dialog/SaveLoad.rsc:confirmDelete");
    if (result == 2) {
        deleteFlag = 1;
        Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
        saveLoad->SetOperation(static_cast<Save::SaveLoad::Operation>(5), selectedIndex);
    }
}

// ============================================================
// DoSave (0x1400b5fd0): Optional confirm-overwrite, then trigger save operation.
// ============================================================
// Sets min operation time (1.5f seconds).
// Gets save info for selected index: if save slot already has data (saveIndex >= 0),
//   shows "Dialog/SaveLoad.rsc:confirmSave" dialog; if not confirmed, returns.
// Sets saveFlag (+0xB9 = 1), copies the running game's save name
//   (GameUpdateLoop::m_saveName, +0x28), calls SetSaveName, then
//   SetOperation(4, selectedIndex).
// Original: 0x1400b5fd0.

void SaveLoadDialog::DoSave() {
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;

    // Set minimum operation time (1.5f seconds — DAT_140172160)
    System::Time minTime{};
    RtSystem_Time_Ctor(&minTime, 1.5f);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): ext.quick_screens ends the floor once
    // the worker has written the file. docs/extensions/handins/SPLASH.md.
    if (Ext::Ext().quick_screens) minTime._ticks = Ext::QuickScreens::kNoMinOperationTime;
    saveLoad->SetMinOperationTime(minTime);

    // Check if the selected save slot already contains data
    int selectedIdx = selectedIndex;
    const Save::SaveLoad::SaveInfo& saveInfo = saveLoad->GetSaveInfo(selectedIdx);
    int validState = saveInfo.saveIndex;
    if (validState >= 0) {
        // Slot is occupied — ask user to confirm overwrite
        int result = SaveLoad_ShowConfirmDialog(this, "Dialog/SaveLoad.rsc:confirmSave");
        if (result != 2) return;  // user cancelled
    }

    // Mark save pending
    saveFlag = 1;

    // Copy the save name from the running game (+0xE8)
    // FUN_140011f90 inlined: String::String(retval, game->m_saveName)
    System::String saveName(game->m_saveName);
    saveLoad->SetSaveName(saveName, 2);

    // Start save operation (4 = save, with selected slot index)
    saveLoad->SetOperation(static_cast<Save::SaveLoad::Operation>(4), selectedIdx);
}

// ============================================================
// DoLoad (0x1400b61e0): Confirm load, check mod compatibility, then trigger load.
// ============================================================
// Sets min operation time (1.5f seconds).
// If game world is loaded (DAT_1401c3dd8 != 0):
//   Shows "Dialog/SaveLoad.rsc:confirmLoad" dialog; if not confirmed, returns.
// Iterates through all mods in the selected save's mod list:
//   For each mod with a non-zero CheckMod status, shows a mod-warning dialog
//   ("modsOutOfDate", "modsVersion", "modsChanged", or "modsMissing") with
//   mod name/version info set as TextElement parameters.
//   If user declines any warning, returns without loading.
// Sets loadFlag (+0xBA = 1), starts operation 2 (load) with selected index.
// Original: 0x1400b61e0.

void SaveLoadDialog::DoLoad() {
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;

    // Set minimum operation time (1.5f seconds)
    System::Time minTime{};
    RtSystem_Time_Ctor(&minTime, 1.5f);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SPLASH.md): ext.quick_screens ends the floor once
    // the worker has read the file (LOADPLAN ruling 3). docs/extensions/handins/SPLASH.md.
    if (Ext::Ext().quick_screens) minTime._ticks = Ext::QuickScreens::kNoMinOperationTime;
    saveLoad->SetMinOperationTime(minTime);

    // If game world is active, ask user to confirm leaving the current game
    if (GameUpdateLoop::_singleton != nullptr) {
        int result = SaveLoad_ShowConfirmDialog(this, "Dialog/SaveLoad.rsc:confirmLoad");
        if (result != 2) return;
    }

    // Iterate through mods in the selected save and warn about any incompatibilities
    int selectedIdx = selectedIndex;
    const Save::SaveLoad::SaveInfo& saveInfo = saveLoad->GetSaveInfo(selectedIdx);
    int modCount = saveInfo.mods._size;
    printf("[SaveLoadDialog::DoLoad] selectedIdx=%d saveInfo=%p save-mod-count=%d gameWorld=%p\n",
           selectedIdx, &saveInfo, modCount,
           GameUpdateLoop::_singleton);
    fflush(stdout);
    // The list CheckMod matches the save against is IO::Modification::_singleton.
    // Printed alongside so a non-zero CheckMod can be READ rather than guessed at.
    if (::IO::Modification::_singleton) {
        const IO::Modification* modSgl = ::IO::Modification::_singleton;
        int installed = modSgl->GetCount();
        printf("[SaveLoadDialog::DoLoad] installed mod count=%d\n", installed);
        for (int mi = 0; mi < installed; mi++) {
            const RtIO::Mod& m = modSgl->GetModification(mi);
            const char* path = m.file._data;
            printf("[SaveLoadDialog::DoLoad]   installed[%d] path=\"%s\"\n", mi, path ? path : "(null)");
        }
        fflush(stdout);
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SAVETAIL.md §4-5, EXTSAVE.md): the `ext:` pseudo-mods leave the list before CheckMod.
    // the save's `ext:<id>` pseudo-mods (a class B / C extension recorded by
    // SaveMetaData) are split out HERE, before CheckMod: retail's loop below
    // sees the real mods only, in their own order, so a real-mod save is
    // retail's dialog exactly. The pseudo-mods follow, after every real mod (the
    // order the save lists them in), each with a CheckMod-shaped status of our
    // own: 0 when this build has the extension and the player has it requested,
    // else 1 -> our own notice in modsMissing's shape that says EXTENSION and
    // why (block EXTNOTICE, src/ext/load_notice.h: not in this build / saved by
    // a newer version / turned off); Yes loads, No aborts. On Yes the game's
    // B / C set is still the save's: the block-1 manifest locks it
    // (GameUpdateLoop::SaveRestore), or fails the load if it is unknown.
    std::vector<int> modOrder;
    std::vector<bool> modIsExt;
    {
        const Save::SaveLoad::SaveInfo& si = saveLoad->GetSaveInfo(selectedIdx);
        for (int i = 0; i < modCount; i++)
            if (!Ext::IsPseudoModPath(si.mods._data[i].path._data)) {
                modOrder.push_back(i); modIsExt.push_back(false);
            }
        for (int i = 0; i < modCount; i++)
            if (Ext::IsPseudoModPath(si.mods._data[i].path._data)) {
                modOrder.push_back(i); modIsExt.push_back(true);
            }
    }

    for (size_t k = 0; k < modOrder.size(); k++) {
        const int i = modOrder[k];
        int modStatus = 0;
        Ext::PseudoModNotice extNotice = Ext::PseudoModNotice::None;
        if (!modIsExt[k]) {
            modStatus = saveLoad->CheckMod(selectedIdx, i);
        } else {
            const Save::SaveLoad::ModInfo& em = saveLoad->GetSaveInfo(selectedIdx).mods._data[i];
            modStatus = Ext::PseudoModLoadStatus(em.path._data,
                                                 static_cast<int32_t>(em.versionMinor));
            if (modStatus != 0) {
                extNotice = Ext::PseudoModNoticeFor(em.path._data,
                                                    static_cast<int32_t>(em.versionMinor));
                // The two agree by construction (the unit test pins it); should
                // they ever not, the entry still gets a notice.
                if (extNotice == Ext::PseudoModNotice::None)
                    extNotice = Ext::PseudoModNotice::NotInBuild;
            }
            printf("[ExtSave] DoLoad: save pseudo-mod \"%s\" v%u -> %s\n",
                   em.path._data ? em.path._data : "(null)", (unsigned)em.versionMinor,
                   modStatus == 0 ? "present and requested"
                                  : Ext::PseudoModNoticeDialog(extNotice));
        }
        {
            const Save::SaveLoad::SaveInfo& si = saveLoad->GetSaveInfo(selectedIdx);
            const Save::SaveLoad::ModInfo* nameBase = si.mods._data;
            const char* nm = nameBase ? nameBase[i].name._data : nullptr;
            printf("[SaveLoadDialog::DoLoad]   save mod[%d] \"%s\" CheckMod=%d\n",
                   i, nm ? nm : "(null)", modStatus);
            fflush(stdout);
        }
        if (modStatus == 0) continue;  // mod is fine

        // SaveInfo::mods (+0x48) is the save's ModInfo array; each entry is 0x20 bytes.
        // ModInfo::name (+0x00) is the mod's DISPLAY NAME ("Debug Menu") and is what
        // the warning dialog shows. It is NOT what CheckMod compares: CheckMod
        // (retail 0x1800c1090) concatenates FileSystem::GetDataDirectory() with
        // ModInfo::path (+0x08) -- the package path relative to the data
        // directory -- and matches that against IO::Modification's per-entry path
        // String at +0x58. The proof is one instruction, `LEA R8,[RBP+0x8]`
        // immediately before the System::operator+ call, where RBP is
        // modIndex*0x20 + saveInfo->modsData. So a save can name a mod whose
        // display name you recognise and still report missing, because the two
        // strings are different fields. Layout: src/engine/save/SaveLoad.h.
        const Save::SaveLoad::ModInfo& modInfo =
            saveLoad->GetSaveInfo(selectedIdx).mods._data[i];

        // Select the appropriate warning dialog based on mod status
        const char* warnPath = nullptr;
        if      (modStatus == 2) warnPath = "Dialog/SaveLoad.rsc:modsOutOfDate";
        else if (modStatus == 3) warnPath = "Dialog/SaveLoad.rsc:modsVersion";
        else if (modStatus == 4) warnPath = "Dialog/SaveLoad.rsc:modsChanged";
        else                     warnPath = "Dialog/SaveLoad.rsc:modsMissing";
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTNOTICE.md): an `ext:` entry gets our notice instead,
        // the same MessageBox shape with a text that says extension. The notice
        // is built in the exe by the embedded-text provider, which the ext boot
        // registers under every profile but the faithful one -- and a faithful
        // run CAN open a save made with a B / C extension (docs/known_port_issues.md
        // row 1330: the null Dialog crashed MessageDialog's ctor). So the provider
        // is registered here, on demand, before the notice is loaded.
        if (modIsExt[k]) {
            Ext::Resources::InitText();
            warnPath = Ext::PseudoModNoticeDialog(extNotice);
        }

        // Load the warning dialog External resource
        System::Filename warnFilename(warnPath);
        const ::Object::External* warnExt =
            ::Object::ExternalFactory::_singleton->FindOrLoad(warnFilename);

        const UI::Dialog* checkedWarn = nullptr;
        if (warnExt) {
            if (::Object::IsDerivedFromNullSafe(warnExt, UI::Dialog::TypeId())) {
                checkedWarn = static_cast<const UI::Dialog*>(warnExt);
            }
        }
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTNOTICE.md): should our notice still not resolve (the
        // embedded text failed to parse or build; `[ExtRes]` in standalone_error.log
        // says why), the entry gets retail's own modsMissing, which names it by
        // the same three parameters. Our dialog only: retail's path is unchanged.
        if (modIsExt[k] && checkedWarn == nullptr) {
            fprintf(stderr, "[ExtSave] DoLoad: the notice %s did not resolve to a Dialog -- "
                            "showing Dialog/SaveLoad.rsc:modsMissing for \"%s\"\n",
                    warnPath ? warnPath : "(null)",
                    modInfo.path._data ? modInfo.path._data : "(null)");
            fflush(stderr);
            warnPath = "Dialog/SaveLoad.rsc:modsMissing";
            System::Filename missingFilename(warnPath);
            warnExt = ::Object::ExternalFactory::_singleton->FindOrLoad(missingFilename);
            if (::Object::IsDerivedFromNullSafe(warnExt, UI::Dialog::TypeId()))
                checkedWarn = static_cast<const UI::Dialog*>(warnExt);
        }

        // Allocate and construct the warning MessageDialog
        void* warnMem = System::MemoryAllocator::MemAlloc(0xC0, 8);
        MessageDialog* warnDlg = nullptr;
        if (warnMem) {
            // MessageDialog's real ctor (B38 B8-5; see SaveLoad_ShowConfirmDialog).
            warnDlg = ::new (warnMem) MessageDialog(_ui, checkedWarn,
                                                    Object::InternalPtr<UI::DialogInstance>());
        }

        // Find the TextElement inside the warning dialog and set mod info parameters
        Object::InternalPtr<UI::DialogInstance> warnDlgPtr(warnDlg);

        // Find TextElement within dialog (UI::TextElement::TypeId())
        UI::Element* rawElem = warnDlgPtr->FindElementType(UI::TextElement::TypeId()).Get();

        // Check IsDerivedFrom TextElement
        bool isText = ::Object::IsDerivedFromNullSafe(rawElem, UI::TextElement::TypeId());
        UI::TextElement* textElem = isText ? static_cast<UI::TextElement*>(rawElem) : nullptr;

        // SetParameter(0) = mod name, SetParameter(1) = mod path
        textElem->SetParameter(0, modInfo.name);
        textElem->SetParameter(1, modInfo.path);

        // SetParameter(2) = formatted version string "%i (%s)"
        // ModInfo::gameVersion (+0x14), ModInfo::versionMinor (+0x18)
        // Configuration::GetVersionString returns by value.
        const Save::SaveLoad::ModInfo& modEntry =
            saveLoad->GetSaveInfo(selectedIdx).mods._data[i];
        uint32_t isBeta   = modEntry.gameVersion;
        uint32_t modVer   = modEntry.versionMinor;
        System::String verStrBuf = System::Configuration::GetVersionString(isBeta, (bool)(isBeta != 0));
        System::String fmtBuf = System::String::Format("%i (%s)", (unsigned)modVer, verStrBuf._data);
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTNOTICE.md): an extension's @2 is its format version
        // alone; its +0x14 is the pseudo-mod constant, not a game version.
        if (modIsExt[k]) fmtBuf = System::String::Format("%i", (unsigned)modVer);
        textElem->SetParameter(2, fmtBuf);

        // Show the warning dialog and check if user confirmed.
        // Retail (0x1400b61e0) copies the dialog InternalPtr into a scratch slot,
        // hands ShowDialog the COPY (it takes its argument by value and writes
        // through the slot it is given), and releases the ORIGINAL afterwards with
        // InternalBasePtr::DeletePtr. There is no String destructor anywhere on this
        // path.
        //
        // This block used to call `fn_String_Dtor(&showPtr)` on an InternalBasePtr
        // holding `warnDlg`, a MemAlloc(0xC0) user pointer. System::String::~String
        // does `*(int*)(data-4) -= 1`, and for a MemAlloc user pointer `data-4` is the
        // HIGH DWORD OF THE ALLOCATOR BOUNDARY TAG (`data-8`), not a refcount -- so it
        // decremented a live tag, borrowing into the tag's 41-bit size field and
        // setting bits 32..40. That is the "load-path wild write whose fault address is
        // a real pointer with +0x7FC in the high dword" (0x1FF << 32, times 4 = 0x7FC
        // in the high dword of block + size*4). It was never a wild write and never
        // save-specific: it is this line, and it fires on any load whose save names a
        // mod that is not installed.
        Object::InternalPtr<UI::DialogInstance> showPtr = warnDlgPtr;
        int warnResult = SaveLoad_ModalShowDialog(_ui, &showPtr, warnPath);
        warnDlgPtr.DeletePtr();

        if (warnResult != 2) return;  // user declined mod warning — abort load
    }

    // All checks passed — trigger load operation
    LoadTime::Stamp("load.click");   // NOT RETAIL: print-only
    loadFlag = 1;
    saveLoad->SetOperation(static_cast<Save::SaveLoad::Operation>(2), selectedIdx);
}

// ============================================================
// SaveLoadDialog::OnEvent (0x1400b67e0) — Slot8
// ============================================================
// Handles button clicks and list selection changes.
// eventType 2 = list selection change:
//   eventData in (3, saveCount+4) → select entry at (eventData-4)
// eventType 0x12 = button click:
//   eventData 0 = buttonCancel → close dialog
//   eventData 1 = buttonSave → save with optional confirm dialog
//   eventData 2 = buttonLoad → load with optional confirm dialogs
//   eventData 3 = buttonDelete → delete with confirm dialog
//
// Original: 0x1400b67e0.

// SelectEntry (0x1400b6790): set selected index and play UI sound
void SaveLoadDialog::SelectEntry(int index) {
    // if index changed, play sound 4 first
    int oldIndex = selectedIndex;
    if (oldIndex != index) {
        _ui->Play(4);
    }
    selectedIndex = index;
}

void SaveLoadDialog::OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t eventType, int32_t eventData, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (eventType == 2) {
        // List row selection change
        int saveCountNow = saveCount;
        if (eventData > 3 && eventData < saveCountNow + 4) {
            SelectEntry(eventData - 4);
        }
        return;
    }

    if (eventType == 0x12) {
        if (eventData == 0) {
            // buttonCancel: close dialog
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->CloseDialog(&self);
            return;
        }
        if (eventData == 2) {
            DoLoad();
            return;
        }
        if (eventData == 1) {
            DoSave();
            return;
        }
        if (eventData == 3) {
            DoDelete();
            return;
        }
    }
}

// ============================================================
// SaveLoadDialog vtable + ctor (save/load wave 2026-08-27)
// ============================================================
// Retail vtable @ 0x14015f900 (10 slots — verified via read_memory):
//   +0x00 Dtor            0x1400211b0
//   +0x08 TypeName        0x140021150
//   +0x10 TypeId          0x140021140
//   +0x18 Persist         0x14003a948 (runtime thunk)
//   +0x20 OnOpened        0x1400b7d00
//   +0x28 OnClosed        0x14003a94e (runtime thunk)
//   +0x30 OnUpdateLayout  0x1400b6880
//   +0x38 OnUpdate        0x1400b79b0 (the per-frame op tracker + loadFlag game transition)
//   +0x40 OnElementEvent  0x1400b67e0
//   +0x48 OnSerialize     0x14003a954 (runtime thunk)
// The ctor is 0x1400b8270.

#include "Object/factory_registration.h"

// ENDGAME B.3.8 block DLG: SaveLoadDialog is a real class (Dialog/SaveLoadDialog.h);
// the hand-built table is gone. engine_funcs.cpp does not read 0x14015f900 back,
// so its registration went with the table; the ctor door stays.

// The ctor body of exe 0x1400b8270 after the DialogInstance base ctor: mode
// byte at +0xB8, flags/selection/rotation zeroed, Sprite default-ctor at +0xD0,
// menu loop / game at +0xE0/+0xE8, +0xF0..+0x158 zero.
void SaveLoadDialog::InitFields(uint8_t modeArg, MenuDialogLoopT* menuLoopArg,
                                GameUpdateLoop* gameArg) {
    mode = modeArg;
    saveFlag = 0;        // retail: one dword store at +0xB9 (the four flags)
    loadFlag = 0;
    refreshFlag = 0;
    deleteFlag = 0;
    selectedIndex = 0;
    rotation = 0.0f;
    sprite = UI::Sprite();
    menuLoop = menuLoopArg;
    game = gameArg;
    mainPage = nullptr;
    labelLoad = nullptr;
    labelSave = nullptr;
    tableSaveList = nullptr;
    entries._data = nullptr;
    entries._size = 0;
    entries._capacity = 0;
    textLoading = nullptr;
    textSaving = nullptr;
    textPleaseWait = nullptr;
    loadingImage = nullptr;
    buttonSave = nullptr;
    buttonLoad = nullptr;
    buttonCancel = nullptr;
    buttonDelete = nullptr;
}

// ~SaveLoadDialog (0x1400b80f0, called by the scalar deleting dtor
// 0x1400211b0): each row's MaterialInstance is deleted (slot 0, flag 1) and,
// when there was one, its SpriteSheet; then retail MemFrees the row array and
// zeroes +0x110 / +0x118 — `entries`' own dtor, which runs after this body —
// and chains ~DialogInstance.
SaveLoadDialog::~SaveLoadDialog() {
    for (int i = 0; i < entries._size; i++) {
        Graphics::MaterialInstance* matInst = entries[i].materialInstance;
        if (matInst != nullptr) {
            delete matInst;   // slot 0, free
        }
        if (entries[i].materialInstance != nullptr) {
            UI::SpriteSheet* sheet = entries[i].spriteSheet;
            if (sheet != nullptr) {
                delete sheet;   // slot 0, free
            }
        }
    }
}

SaveLoadDialog::SaveLoadDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                               Object::InternalPtr<UI::DialogInstance> parent,
                               uint8_t modeArg, MenuDialogLoopT* menuLoopArg,
                               GameUpdateLoop* gameArg)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0) {
    InitFields(modeArg, menuLoopArg, gameArg);
}

// The reflected create path (create 0x140021160): the tree's factory handed out
// a zero-filled object; this keeps the zero fields over the default base ctor.
SaveLoadDialog::SaveLoadDialog() {
    InitFields(0, nullptr, nullptr);
    saveCount = 0;
}

// create 0x140021160: MemAlloc(0x160, 8), then the ctor.
::Object::Base* SaveLoadDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SaveLoadDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) SaveLoadDialog());
}
// createInPlace 0x1400211a0 (the tree's in-place build is kept, B8-13).
::Object::Base* SaveLoadDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) SaveLoadDialog());
}

// The reflection record stays at the TU position of the UI_DIALOG_STUB row it
// replaces (ui_dialog_stub_factories.cpp, B38 D4).

