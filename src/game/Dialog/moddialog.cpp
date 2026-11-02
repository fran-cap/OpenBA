// src/game/Dialog/moddialog.cpp
// Clean reimplementation of ModDialog vtable methods.
//
// ModDialog (0xD8 bytes), vtable at 0x14015c870.
// Extends DialogInstance (0xB8 base). Fields and the 0x58-byte row: ModDialog.h.
//
// Reimplemented:
//   OnOpened (0x14006F150) — populates mod list table, thumbnail pipeline
//   OnElementEvent (0x14006ED40) — handles all mod list button clicks
//   RefreshDisplay 0x14006eec0, ToggleMod 0x14006e930, MoveToTop 0x14006e660,
//   ShowDetails 0x14006e490, Apply 0x14006ea70 (CloseAndReload 0x14006e9b0 is in
//   mod_dialog_vtables.cpp)
//
// (B.4 acceptance MOD: RtIO::Mod is the real class -- String / Filename / Array
// members -- so its readers below take them by name.)
// SpriteSheet::m_field30 is `uint64_t` (R-UI) and
// MaterialInstance::AddTexture takes the RtSystem::String POD (R-GFX).

// ENDGAME B.3.5 batch K1: the runtime type headers come FIRST — they reach
// src/engine/volume/Volume.h, whose global-scope `struct Rectangle` is hidden by
// <wingdi.h>'s `Rectangle()` once <windows.h> has been seen through resolve.h.
#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "ui/ElementDescription.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "engine/UpdateLoop.h"   // ENDGAME B.3.7 callers (K1)
#include "video/ImageBuffer.h"
#include "video/Device.h"
#include "graphics/Material.h"
#include "Dialog/Dialogs.h"
#include "options.h"
#include "Dialog/ModDialog.h"
#include "Dialog/ModBrowseUI.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Dialog/ModDetailsDialog.h"
#include "Dialog/MessageDialog.h"
#include "Dialog/ui_dialogs.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "io/Modification.h"   // ENDGAME B.3.4 caller batch P
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "engine/MainLoop.h"   // ENDGAME B.3.9 SS2: Engine::MainLoop::_singleton
#include "Dialog/MainMenuDialog.h"     // MainMenuDialog::ReturnToMenu
#include "save/SaveLoad.h"             // B3R: Save::SavedState
#include "system/Configuration.h"      // B3R: System::Configuration::GetVersionString
#include "Game/exe_constants.h"   // B3R: kRdata_*
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "io/Persistence.h"             // fix.mods_persist: Persistence::WriteSettings
#include "ext/registry.h"               // Ext::Fix().mods_persist


// ============================================================
// Helper types
// ============================================================

// (B3R: the two unused raw-vtable-read typedefs fn_ElementVt_Child_t / _Hide_t that
// stood here had no reader and are gone.)

// ENDGAME B.3.5 batch K1: the hand-built `fn_ImageBuffer_Create_t` slot typedef for
// vtable+0x48 went with the slot read below, which is now a virtual call on
// `Video::ImageBuffer`. Keeping the history the typedef carried, because the member's
// signature is what makes it safe: the LAST parameter of
//   ?Create@ImageBuffer@Video@@UEAA_NW4ImageType@12@W4Usage@12@W4Format@12@IHHW4FilterMode@12@
//   W4AddressMode@12@44PEAV?$Array@V?$Array@E@Collection@@@Collection@@@Z
// is a POINTER (`Array<Array<uchar>>* levels`) living in a stack slot, and passing it as
// `int 0` wrote only the low dword — leaving the high dword as whatever was there
// (0x00007ffc... on the first Mods click), which ImageBufferData_Create then
// dereferenced as `levels->count`. The C++ member types it, so that cannot recur.

// (B.4 W1: the row append is Collection::Array<ModDialogEntry>::SetSize(n, fill),
// retail 0x140012a60 — the trivially-copyable row is byte-copied, the old buffer
// freed, capacity trebled on a +1 append, exactly as the transcription here did.)

// ============================================================
// Filename equality check — compares two System::Filename/String objects
// by their underlying string data. Both are 8-byte COW pointers.
// ============================================================
static bool Filename_Equals(const System::Filename& filenameA, const System::Filename& filenameB) {
    const char* a = filenameA._data;
    const char* b = filenameB._data;
    if (a == b) return true;
    if (!a || !b) return false;
    // Compare using C string comparison (null-terminated)
    return strcmp(a, b) == 0;
}

// ============================================================
// ModDialog::RefreshDisplay (0x14006eec0)
//
// Updates the mod list UI display. For each mod entry in the table:
//   1. Sets name, author, description, version text parameters
//   2. Checks for conflicts (same filename dependency in another enabled mod)
//   3. Shows/hides conflict icon
//   4. Copies thumbnail texture from mod data
// ============================================================
void ModDialog::RefreshDisplay() {
    IO::Modification* modification = ::IO::Modification::_singleton;
    if (!modification) return;

    int modCount = modification->GetCount();
    if (modCount <= 0) return;

    for (int i = 0; i < modCount; i++) {
        ModDialogEntry& entry = entries._data[i];

        const RtIO::Mod& mod = modification->GetModification(i);

        // Check for conflicts: another enabled mod depends on a file from this mod
        bool hasConflict = false;
        uint8_t enabled = mod.enabled;

        if (enabled != 0) {
            for (int j = 0; j < modCount; j++) {
                if (j == i) continue;

                const RtIO::Mod& otherMod = modification->GetModification(j);
                uint8_t otherEnabled = otherMod.enabled;
                if (otherEnabled == 0) continue;

                // Check if this mod's filename appears in the other mod's dependency list
                int depCount = mod.files._size;
                if (depCount <= 0) continue;

                const System::Filename* depArray = mod.files._data;   // Mod +0x70 Array<Filename>
                for (int k = 0; k < depCount; k++) {
                    // Compare other mod's filename with this mod's dependency[k]
                    if (Filename_Equals(otherMod.file, depArray[k])) {
                        hasConflict = true;
                        goto conflict_done;
                    }
                }
            }
        }
conflict_done:

        // Set text parameters: name, author, description.
        static_cast<UI::TextElement*>(entry.textModName.Get())->SetParameter(0, mod.name);

        static_cast<UI::TextElement*>(entry.textModAuthor.Get())->SetParameter(0, mod.author);

        static_cast<UI::TextElement*>(entry.textModDescription.Get())->SetParameter(0, mod.description);

        // Format version string: "versionMinor (versionString)"
        {
            uint32_t versionMajor = (uint32_t)mod.i18;
            uint32_t versionMinor = (uint32_t)mod.i1c;

            // GetVersionString returns a String for the version major (as game build version)
            // ?GetVersionString@Configuration@System@@ is a by-value static; its door
            // placement-constructed the result over this (empty) local.
            System::String buildStr = System::Configuration::GetVersionString(versionMajor, false);

            System::String formatted = System::String::Format("%i (%s)", (uint64_t)versionMinor, buildStr._data);

            static_cast<UI::TextElement*>(entry.textModVersion.Get())->SetParameter(0, formatted);
        }

        // Set enabled checkbox state
        static_cast<UI::CheckElement*>(entry.checkLoaded.Get())->_checked = enabled;

        // Colour the row's border (vtable[10] SetColor)
        // Color encodes enabled/conflict state:
        //   0x20c0ffc0 = enabled, no conflict (green)
        //   0x40ff0000 = enabled, has conflict (red)
        //   0x10ffffff = disabled (white/transparent)
        uint32_t rowColor;
        if (enabled) {
            rowColor = hasConflict ? 0x40ff0000 : 0x20c0ffc0;
        } else {
            rowColor = 0x10ffffff;
        }
        entry.rowBorder->SetColor(rowColor);

        // Show/hide conflict icon
        entry.imageConflict->SetFlagsOnOff(
                                  hasConflict ? 0 : 1,    // set bit 0 (hidden) when no conflict
                                  hasConflict ? 1 : 0);   // clear bit 0 (visible) when conflict

        // Copy thumbnail texture (48x48)
        {
            Video::Device* videoDevice = ::Video::Device::_singleton;
            if (videoDevice) {
                videoDevice->Lock();
                // mod.ints: the thumbnail pixels (Array<int32> data)
                entry.imageBuffer->CopyToTexture(mod.ints._data, 0x30, 0x30);
                videoDevice->Unlock();
            }
        }
    }
}

// ============================================================
// ModDialog::OnOpened — Setup (0x14006F150)
// ============================================================
//
// Finds buttonCancel/buttonOk/tableMods, then for each installed mod:
//   1. Clones the dialog template element and adds it to tableMods
//   2. Finds 10 child elements by name, assigns IDs (1000+/2000+/3000+/4000+/5000+i)
//   3. Hides buttonUp+buttonTop for first mod, buttonDown for last
//   4. Builds 48x48 ImageBuffer + MaterialInstance + SpriteSheet thumbnail
//   5. Appends the 0x58-byte row to `entries`
// After loop: calls RefreshDisplay, loads browser controller, hides browse UI.

void ModDialog::OnOpened() {
    printf("[ModDialog::OnOpened] self=%p building mod list\n", (void*)this);

    // --- buttonCancel (id=100, key index 8) ---
    {
        System::String retbuf("buttonCancel");
        Object::InternalPtr<UI::Element> elem = FindElement(retbuf);
        elem->SetElementId(100);
        static_cast<UI::ButtonElement*>(elem.Get())->_keyIndex = 8;
    }

    // --- buttonOk (id=0x65) ---
    {
        System::String retbuf("buttonOk");
        Object::InternalPtr<UI::Element> elem = FindElement(retbuf);
        elem->SetElementId(0x65);
    }

    // --- tableMods ---
    {
        System::String retbuf("tableMods");
        tableMods = FindElement(retbuf);
    }

    IO::Modification* modification = ::IO::Modification::_singleton;
    int modCount = modification->GetCount();
    float uvScale = kRdata_14017214c;   // 0x14017214c = 1.0f (was g_fUVScale; its fallback never ran)

    for (int i = 0; i < modCount; i++) {
        // Clone template element from Dialog, add to tableMods
        const UI::Dialog* dialog = _dialog;
        Object::ExternalPtr<UI::ElementDescription> templateObj = dialog->GetTemplate(0);

        // ElementDescription slot 8 CreateInstance (RETSLOT per B6-2)
        Object::InternalPtr<UI::Element> clonedElem;
        templateObj->CreateInstance(&clonedElem, dialog);

        // Add cloned element to tableMods
        tableMods->AddChild(&clonedElem);

        // Find child elements by name in the cloned row
        ModDialogEntry entry = {};
        Object::InternalPtr<UI::Element> buttonTop;
        Object::InternalPtr<UI::Element> buttonUp;
        Object::InternalPtr<UI::Element> buttonDown;
        Object::InternalPtr<UI::Element> buttonDetails;

        struct {
            const char* name;
            Object::InternalPtr<UI::Element>* slot;
        } findList[] = {
            { "textModName",        &entry.textModName },
            { "textModAuthor",      &entry.textModAuthor },
            { "textModDescription", &entry.textModDescription },
            { "textModVersion",     &entry.textModVersion },
            { "checkLoaded",        &entry.checkLoaded },
            { "imageConflict",      &entry.imageConflict },
            { "imageIcon",          &entry.imageIcon },
            { "buttonTop",          &buttonTop },
            { "buttonUp",           &buttonUp },
            { "buttonDown",         &buttonDown },
            { "buttonDetails",      &buttonDetails },
        };

        for (auto& e : findList) {
            System::String strBuf(e.name);
            *e.slot = clonedElem->FindElement(strBuf);
        }

        // The row's border: slot 11 of every UI::Element table is GetBorder
        // (RETSLOT, B6-2), which takes no index -- the `0` an older call
        // passed was never read.
        {
            Object::InternalPtr<UI::Element> border;
            entry.rowBorder = *clonedElem->GetBorder(&border);
        }

        // Assign element IDs
        entry.checkLoaded->SetElementId(i + 1000);
        buttonTop->SetElementId(i + 4000);
        buttonDown->SetElementId(i + 3000);
        buttonUp->SetElementId(i + 2000);
        buttonDetails->SetElementId(i + 5000);

        // Up+Top for first mod (vtable[8], arg=0 matches original). Slot 8 of
        // every UI::Element table is Enable(bool), not Hide (an older comment's
        // name), so what this does is DISABLE the buttons (B36_RULES 2.2).
        if (i == 0) {
            buttonUp->Enable(0);
            buttonTop->Enable(0);
        }
        // Down for last mod
        if (i == modCount - 1) {
            buttonDown->Enable(0);
        }

        // --- Create 48x48 ImageBuffer ---
        void* imgBufMem = System::MemoryAllocator::MemAlloc(0x68, 8);
        Video::ImageBuffer* imgBuf = nullptr;
        entry.imageBuffer = nullptr;
        if (imgBufMem) {
            // Retail: `CALL [??0ImageBuffer]` then LEA 0x14015c8c0 — the class's
            // own table, i.e. `new Video::ImageBuffer()` compiled in the exe.
            imgBuf = ::new (imgBufMem) Video::ImageBuffer();   // B.3.9 EXA: the real class (exe table 0x14015c8c0 is its own)
            imgBuf->Create(
                0, 2, 5, 0u, 0x30, 0x30, 1, 1, 1, 1, nullptr);
            entry.imageBuffer = imgBuf;
        }

        // --- Create MaterialInstance (0x60 bytes) ---
        void* matInstMem = System::MemoryAllocator::MemAlloc(0x60, 8);
        Graphics::MaterialInstance* matInst = nullptr;
        entry.material = nullptr;
        if (matInstMem) {
            matInst = ::new (matInstMem) Graphics::MaterialInstance();   // B.3.9 EXA: the real class (exe table 0x14015c400 is its own)
            entry.material = matInst;
        }

        // Load "Material/UI/UIMaterial.rsc", IsDerivedFrom Material, SetMaterial
        {
            System::Filename filenameBuf("Material/UI/UIMaterial.rsc");
            ::Object::External* ext =
                ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);
            Graphics::Material* material = nullptr;
            if (ext) {
                bool derived = ::Object::IsDerivedFromNullSafe(ext, Graphics::Material::TypeId());
                if (derived) material = static_cast<Graphics::Material*>(ext);
            }
            matInst->SetMaterial(Object::ExternalPtr<Graphics::Material>(material));
        }

        // AddTexture("diffuse", imageBuffer)
        {
            System::String strBuf("diffuse");
                    matInst->AddTexture(
                strBuf,
                Object::ExternalPtr<Video::ImageBuffer>(imgBuf));
        }

        matInst->BuildParameters();

        // --- Create SpriteSheet (0x58 bytes) ---
        void* sprSheetMem = System::MemoryAllocator::MemAlloc(0x58, 8);
        UI::SpriteSheet* sprSheet = nullptr;
        entry.spriteSheet = nullptr;
        if (sprSheetMem) {
            sprSheet = ::new (sprSheetMem) UI::SpriteSheet();   // B.3.9 EXA: the real class (exe table 0x14015c3b0 is its own)
            entry.spriteSheet = sprSheet;

            // The sheet's material instance (+0x30).
            sprSheet->m_field30 = matInst;

            // operator= empty strings at +0x40 and +0x38 (the two String fields).
            // LEFT: SpriteSheet holds them as char* (H5, R-UI's header).
            System::String emptyStr1, emptyStr2;
            sprSheet->m_groupName = emptyStr1;
            sprSheet->m_textureName = emptyStr2;

            // w/h at +0x48/+0x4c = 48 (0x30)
            sprSheet->m_field48 = 0x30;
            sprSheet->m_field4C = 0x30;
            // Zero +0x50 (one qword store in the transcription: the flag and its pad)
            sprSheet->m_field50 = false;
            for (uint8_t& b : sprSheet->_pad51) b = 0;

            // AddSpriteInfo(sheet, empty, empty, 0, 0, 48, 48)
            System::String emptyA, emptyB;
            sprSheet->AddSpriteInfo(emptyA, emptyB, 0, 0, 0x30, 0x30);

            // SetSpriteInfo(sheet, 0, 48, 48, 0.0, 0.0, uvScale, uvScale)
            sprSheet->SetSpriteInfo(
                0, 0x30, 0x30, 0.0f, 0.0f, uvScale, uvScale);
        }

        // Create Sprite by index and SetSprite on imageIcon
        {
            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(sprSheet), 0);
            static_cast<UI::ImageElement*>(entry.imageIcon.Get())->SetSprite(&sprite);
        }

        // Append the row (0x140012a60)
        entries.SetSize(entries._size + 1, entry);
    }

    // --- Post-loop: refresh display, add browser controller, hide browse UI ---
    RefreshDisplay();

    // Load "Dialog/Mod.rsc:browser", add as controller with type ModBrowseUI::TypeId()
    // (cell 0x1401c5210; the TypeId_BrowserController name lies -- see Table 1/3)
    {
        System::Filename filenameBuf("Dialog/Mod.rsc:browser");
        ::Object::External* ext =
            ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);

        // Pass browser external directly; original has IsDerivedFrom check but the
        // _typeInfo_exref is context-dependent (Ghidra merges refs) and the browser
        // controller IS the right type — passing null would silently break the UI.
        UI::ElementDescription* browserExt = static_cast<UI::ElementDescription*>(ext);

        // AddController(this, typeId, 0, 0, browserExt, "groupBrowse")
        System::String groupStr("groupBrowse");
        uint32_t typeId = ModBrowseUI::TypeId();
        AddController(
            typeId, static_cast<Object::Base*>(nullptr),
            Object::ExternalPtr<UI::DialogControllerConfig>(nullptr),
            Object::ExternalPtr<UI::ElementDescription>(browserExt),
            groupStr);
    }

    // Hide "buttonBrowse"
    {
        System::String strBuf("buttonBrowse");
        FindElement(strBuf)->Hide(true);
    }

    // Hide "groupProgress"
    {
        System::String strBuf("groupProgress");
        FindElement(strBuf)->Hide(true);
    }
}

// ============================================================
// ModDialog::ToggleMod (0x14006e930)
//
// Pushes the row's checkbox state into the mod list, then refreshes.
//
// Retail (0x14006e930) does NOT read the model: it takes row `index` of the
// entry array, CheckPtr's the checkLoaded InternalPtr, and passes THAT
// element's already-toggled bool to Modification::Enable. The previous port
// read Mod::enabled and inverted it, which only agrees while widget and model
// are in lockstep, and it is why the no-mods crash read 0xd0 (GetModification
// on an empty list) instead of faulting on the row itself. No null check: with
// the row present the deref is load-bearing, as in retail; the row's presence is
// the caller's contract (src/diag/autostart.cpp gates its fabricated clicks on
// the row count).
// ============================================================
void ModDialog::ToggleMod(int32_t index) {
    IO::Modification* modification = ::IO::Modification::_singleton;

    const ModDialogEntry& entry = entries._data[index];
    UI::CheckElement* checkElem = static_cast<UI::CheckElement*>(entry.checkLoaded.Get());
    modification->Enable(index, checkElem->_checked != 0);

    RefreshDisplay();
}

// ============================================================
// ModDialog::MoveToTop (0x14006e660)
//
// Moves a mod to position 0 by repeatedly swapping with the entry above.
// Then does a full refresh display.
// ============================================================
void ModDialog::MoveToTop(int32_t index) {
    IO::Modification* modification = ::IO::Modification::_singleton;

    // Swap up to top
    while (index > 0) {
        modification->Swap(index, index - 1);
        index--;
    }

    RefreshDisplay();
}

// ============================================================
// ModDialog::ShowDetails (0x14006e490)
//
// Opens the ModDetailsDialog for a specific mod index.
// Loads "Dialog/ModDetails.rsc", creates a ModDetailsDialog (0xC8 bytes)
// with the row's sprite sheet and the mod index, then adds it to the UI.
// ============================================================
void ModDialog::ShowDetails(int32_t index) {
    // Load dialog resource
    System::Filename filenameBuf("Dialog/ModDetails.rsc");
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(filenameBuf);
    // IsDerivedFrom check — resource is always valid Dialog if load succeeds
    const UI::Dialog* dialog = static_cast<const UI::Dialog*>(ext);

    // Allocate ModDetailsDialog (0xC8 = 200 bytes)
    void* newDialog = System::MemoryAllocator::MemAlloc(0xC8, 8);
    ModDetailsDialog* dialogInst = nullptr;
    if (newDialog) {
        UI::SpriteSheet* spriteHandle = entries._data[index].spriteSheet;

        // ENDGAME B.3.8 DLG: the real class (retail inlines this ctor here):
        // DialogInstance(ui, dialog, nullParent, 0, 0, 0), +0xB8 = sprite
        // handle, +0xC0 = mod index.
        dialogInst = ::new (newDialog) ModDetailsDialog(
            _ui, dialog, Object::InternalPtr<UI::DialogInstance>(), spriteHandle, index);
    }

    // Add to UI
    Object::InternalPtr<UI::DialogInstance> dialogRef(dialogInst);
    _ui->AddDialog(&dialogRef);
}

// ============================================================
// ModDialog::Apply (0x14006ea70)
//
// Applies mod changes: checks if mods have been modified (dirty flag),
// shows appropriate reload dialog (menu vs in-game), saves mod list,
// handles reload/restart sequence, then closes the dialog.
//
// Flow:
//   1. Check the Modification dirty flag
//   2. If dirty:
//      a. If gameWorld == nullptr: menu context -> load "Dialog/Reload.rsc:menu"
//      b. Else: game context -> load "Dialog/Reload.rsc:game"
//      c. Create ReloadDialog (0xC0 bytes, vtable PTR_FUN_14015c480)
//      d. ShowDialog() — blocks until user responds
//      e. If result 1 or 2 (OK/confirm):
//         - SaveList(), CloseAndReload()
//         - Menu: MainMenu_Reload(parentMenu)
//         - Game: create SavedState, call game reload
//      f. If other: RefreshList (cancel)
//   3. Close dialog
// ============================================================
void ModDialog::Apply() {
    IO::Modification* modification = ::IO::Modification::_singleton;

    if (modification->_dirty != 0) {
        GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

        // Load reload dialog resource based on context (menu vs in-game)
        System::Filename filenameBuf(gameWorld == nullptr ? "Dialog/Reload.rsc:menu"
                                                            : "Dialog/Reload.rsc:game");
        const ::Object::External* ext =
            ::Object::ExternalFactory::_singleton->FindOrLoad(filenameBuf);
        const UI::Dialog* dialog = static_cast<const UI::Dialog*>(ext);  // IsDerivedFrom check always passes for valid .rsc

        // Create ReloadDialog (0xC0 bytes)
        void* newDialog = System::MemoryAllocator::MemAlloc(0xC0, 8);
        MessageDialog* dialogInst = nullptr;
        if (newDialog) {
            // ENDGAME B.3.8 DLG: a real MessageDialog (retail stamps its
            // table 0x14015c480 after the DialogInstance ctor).
            dialogInst = ::new (newDialog) MessageDialog(
                _ui, dialog, Object::InternalPtr<UI::DialogInstance>());
        }

        // ShowDialog (modal) — blocks until user responds
        Object::InternalPtr<UI::DialogInstance> dialogRef(dialogInst);
        // BANISHED_MODAL_ANSWER=<n>: unattended harness only. ShowDialog pumps its
        // own message loop until a button is pressed, which no driver can reach, so
        // the answer is injected here instead (1/2 = confirm, anything else = cancel).
        int result;
        if (Opt().modalAnswer) {
            result = Opt().modalAnswerValue;
            printf("[ModDialog::Apply] BANISHED_MODAL_ANSWER=%d (reload dialog skipped)\n", result);
            dialogRef.DeletePtr();
        } else {
            result = _ui->ShowDialog(&dialogRef);
            dialogRef.DeletePtr();
        }
        printf("[ModDialog::Apply] dirty=1 gameWorld=%p reload answer=%d\n", (void*)gameWorld, result);

        if ((unsigned)(result - 1) < 2) {
            // User confirmed: save and reload
            modification->SaveList();
            // FIX mods_persist (docs/retail_bugs.md section 23): retail writes settings.cfg only at a
            // clean shutdown (Persistence::WriteSettings 0x1800b88e0, from Engine::Shutdown), so a
            // crash during or after the reload loses the change. With the row on the list is written
            // now. At the main menu SaveList has just put it in the settings; in a game (reachable only
            // with the row off) it has not, and the write leaves the mod keys as they were.
            if (Ext::Fix().mods_persist) ::IO::Persistence::_singleton->WriteSettings();
            CloseAndReload();   // 0x14006e9b0

            if (gameWorld == nullptr) {
                // Menu context: simple reload through the owning menu
                parentMenu->ReturnToMenu();   // 0x1400b4790
            } else {
                // Game context: allocate SavedState (0x10 bytes), construct it
                void* savedState = System::MemoryAllocator::MemAlloc(0x10, 4);
                Save::SavedState* statePtr = nullptr;
                if (savedState) {
                    statePtr = ::new (savedState) Save::SavedState();   // ??0SavedState 0x1800c0ee0 (its door placement-news the class)
                }

                // The current loop's slot 7 (SaveRestore) prepares the game for
                // reload (dispatch slot 0x140151440 is Engine::MainLoop::_singleton).
                ::Engine::MainLoop* engine = ::Engine::MainLoop::_singleton;
                if (engine) {
                    ::Engine::UpdateLoop* inner = engine->_currentLoop;
                    if (inner) {
                        inner->SaveRestore(true, statePtr);
                    }
                }

                // Build null InternalBasePtr
                Object::InternalPtr<UI::DialogInstance> nullRef2;

                // Trigger game reload with saved state
                parentMenu.Get()->LoadFromSave(statePtr, -1, &nullRef2);   // 0x1400b49f0
            }
        } else {
            // User cancelled: refresh list to undo changes
            modification->RefreshList();
        }
    }

    // Close the mod dialog
    Object::InternalPtr<UI::DialogInstance> closeRef(this);
    _ui->CloseDialog(&closeRef);
}

// ============================================================
// ModDialog::OnElementEvent — OnCommand handler (0x14006ED40)
// ============================================================
//
// Dispatches button clicks by element ID ranges:
//   100         — Cancel: refresh mod list, close dialog
//   0x65 (101)  — OK/Apply: save changes, reload if needed, close
//   1000-1999   — Toggle mod enabled/disabled (index = id - 1000)
//   2000-2999   — Move mod up (swap index, index-1)
//   3000-3999   — Move mod down (swap index, index+1)
//   4000-4999   — Move mod to top of list
//   5000-5999   — Show mod details dialog
//
// Always calls base DialogInstance::OnElementEvent at the end.

void ModDialog::OnElementEvent(Object::InternalPtr<UI::Element>* param2, int32_t msg, int32_t id, int32_t p5, int32_t p6, int32_t p7, void* p8) {
    if (msg == 0x12)
        printf("[ModDialog::OnEvent] click id=%d\n", id);

    if (msg == 0x12) {  // Button click event
        IO::Modification* modification = ::IO::Modification::_singleton;

        if (id == 100) {
            // Cancel — refresh list and close
            modification->RefreshList();

            Object::InternalPtr<UI::DialogInstance> dialogPtr(this);
            _ui->CloseDialog(&dialogPtr);
            goto done;
        }

        if (id == 0x65) {
            // OK/Apply
            Apply();
            goto done;
        }

        if (id >= 1000 && id < 2000) {
            // Toggle mod checkbox
            ToggleMod(id - 1000);
            goto done;
        }

        if (id >= 2000 && id < 3000) {
            // Move up: swap(id-2000, id-2001)
            modification->Swap(id - 2000, id - 2001);
            RefreshDisplay();
            goto done;
        }

        if (id >= 3000 && id < 4000) {
            // Move down: swap(id-3000, id-2999)
            modification->Swap(id - 3000, id - 2999);
            RefreshDisplay();
            goto done;
        }

        if (id >= 4000 && id < 5000) {
            // Move to top
            MoveToTop(id - 4000);
            goto done;
        }

        if (id >= 5000 && id < 6000) {
            // Show details
            ShowDetails(id - 5000);
        }
    }

done:
    // Always call base handler, with a copy of the element pointer.
    Object::InternalPtr<UI::Element> elementPtr = *param2;
    // QUALIFIED (B36_RULES 3.2): an unqualified call would dispatch straight
    // back into this slot body.
    UI::DialogInstance::OnElementEvent(&elementPtr, msg, id, p5, p6, p7, p8);
}
