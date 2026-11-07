// src/game/Dialog/moddetailsdialog.cpp
// Clean reimplementation of ModDetailsDialog methods.
//
// ModDetailsDialog (0xC8 bytes): Shows mod details with action buttons.
// Layout: DialogInstance base (0xB8), +0xB8 = sprite sheet, +0xC0 = mod index
//
// The mod is an RtIO::Mod (src/engine/io/Modification.h): name / author /
// description are System::Strings, file a System::Filename, i18 / i1c the
// version pair, `set` the Filename-keyed resource set, `files` the
// Array<Filename> of the mods this one conflicts with.

#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ButtonElement.h"
#include "ui/DocumentTextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/ModDetailsDialog.h"
#include "Dialog/ModUploadDialog.h"
#include <cstdio>
#include "Dialog/ui_dialogs.h"
#include "ui/DialogInstance.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "io/Modification.h"   // ENDGAME B.3.4 caller batch P
#include <cstdint>
#include <utility>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Configuration.h"   // B3R: System::Configuration::GetVersionString
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// ============================================================
// File-local helper: ModDetails_StringArrayPush (FUN_14006e290)
// ============================================================
// Appends each byte of a System::String to a 1-byte-stride dynamic array.
// Equivalent to: for each char in str, push it as a byte.
//
// Original: FUN_14006e290. The array is Collection::Array<char> (stride 1,
// align 1 — retail's fn_MemAlloc align argument was 1, matching alignof(char)).
static void ModDetails_StringArrayPush_Impl(Collection::Array<char>& arr, System::String&& str) {
    const char* strData = str._data;
    int strLen = str.GetLength();
    for (int i = 0; i < strLen; i++) {
        char ch = strData[i];
        arr.SetSize(arr.GetSize() + 1, ch);
    }
    // `str` is bound by rvalue reference to a caller-owned temporary; the
    // caller's own scope (see call sites) releases it once this returns.
}

// ============================================================
// Helper: find element by name
// ============================================================
static UI::Element* FindElem(ModDetailsDialog* dialog, const char* name) {
    System::String nameStr(name);
    return dialog->FindElement(nameStr).Get();
}

// ============================================================
// Helper: BST leftmost node (in-order start)
// ============================================================
static const RtIO::ModFilenameSet::Node* BSTLeftmost(const RtIO::ModFilenameSet::Node* root) {
    if (!root) return nullptr;
    const RtIO::ModFilenameSet::Node* cur = root;
    while (cur->left)
        cur = cur->left;
    return cur;
}

// ============================================================
// Helper: BST in-order successor
// ============================================================
static const RtIO::ModFilenameSet::Node* BSTNext(const RtIO::ModFilenameSet::Node* node) {
    if (!node) return nullptr;

    // If right child exists, go right then all the way left
    const RtIO::ModFilenameSet::Node* right = node->right;
    if (right) {
        const RtIO::ModFilenameSet::Node* cur = right;
        while (cur->left)
            cur = cur->left;
        return cur;
    }

    // Go up: if we came from a left child, parent is next
    const RtIO::ModFilenameSet::Node* parent = node->parent;
    if (parent) {
        if (parent->left == node)
            return parent;
        // Came from right — walk up until we come from a left child
        const RtIO::ModFilenameSet::Node* cur = node;
        while (parent && parent->right == cur) {
            cur = parent;
            parent = cur->parent;
        }
    }
    return parent;
}

// ============================================================
// Helper: search BST for filename match using Filename::operator<
// ============================================================
static bool BSTContains(const RtIO::ModFilenameSet::Node* root, const RtIO::ModFilenameSet::Node* filename) {
    const System::Filename& fnameRef = filename->element;
    const RtIO::ModFilenameSet::Node* node = root;
    while (node) {
        const System::Filename& nodeRef = node->element;
        if (fnameRef < nodeRef) {
            node = node->left;
        } else if (nodeRef < fnameRef) {
            node = node->right;
        } else {
            return true; // equal (neither < nor >)
        }
    }
    return false;
}

// ============================================================
// OnOpened (0x14006D700): Setup — populates mod details UI
// ============================================================
extern ModDetailsDialog* g_lastModDetails;   // mod_dialog_vtables.cpp (autostart click driver)
void ModDetailsDialog::OnOpened() {
    g_lastModDetails = this;
    printf("[ModDetailsDialog::OnOpened] self=%p modIndex=%d\n", (void*)this, modIndex);

    // --- Find 12 UI elements ---
    UI::Element* buttonOk = FindElem(this, "buttonOk");
    buttonOk->SetElementId(0);
    static_cast<UI::ButtonElement*>(buttonOk)->_keyIndex = 8;

    UI::Element* buttonCreate = FindElem(this, "buttonCreate");
    buttonCreate->SetElementId(1);

    UI::Element* buttonUpdate = FindElem(this, "buttonUpdate");
    buttonUpdate->SetElementId(2);

    UI::ImageElement* imageIcon = static_cast<UI::ImageElement*>(FindElem(this, "imageIcon"));
    UI::TextElement* textName        = static_cast<UI::TextElement*>(FindElem(this, "textName"));
    UI::TextElement* textPath        = static_cast<UI::TextElement*>(FindElem(this, "textPath"));
    UI::TextElement* textAuthor      = static_cast<UI::TextElement*>(FindElem(this, "textAuthor"));
    UI::TextElement* textVersion     = static_cast<UI::TextElement*>(FindElem(this, "textVersion"));
    UI::TextElement* textDescription = static_cast<UI::TextElement*>(FindElem(this, "textDescription"));
    UI::Element* textConflictTitle = FindElem(this, "textConflictTitle");
    UI::DocumentTextElement* textConflictList = static_cast<UI::DocumentTextElement*>(FindElem(this, "textConflictList"));
    UI::DocumentTextElement* textResourceList = static_cast<UI::DocumentTextElement*>(FindElem(this, "textResourceList"));

    // --- Get mod data ---
    IO::Modification* modification = ::IO::Modification::_singleton;
    int modIndex = this->modIndex;
    const RtIO::Mod& mod = modification->GetModification(modIndex);

    // --- Hide create/update buttons ---
    buttonCreate->Hide(true);
    buttonUpdate->Hide(true);

    // --- Set icon sprite from the sprite sheet ---
    // Retail (0x14006dca6) calls dispatch slot 0x140151898 =
    // ??0Sprite@UI@@QEAA@V?$ExternalPtr@VSpriteSheet@UI@@@Object@@H@Z -- the
    // BY-INDEX ctor with index 0, same as ModDialog's row icon. The by-NAME
    // overload that used to be here dereferences its String* (READ of 0).
    UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(spriteSheet), 0);
    imageIcon->SetSprite(&sprite);

    // --- Set text parameters ---
    textName->SetParameter(0, mod.name);

    textAuthor->SetParameter(0, mod.author);

    // Path (the mod's Filename, as a new String)
    {
        System::String pathStr(mod.file._data);
        textPath->SetParameter(0, pathStr);
    }

    // Version: "%i (%s)" with version number and version string
    {
        // ?GetVersionString@Configuration@System@@ is a by-value static; its door
        // placement-constructed the result over this (empty) local.
        System::String versionStr = System::Configuration::GetVersionString((uint32_t)mod.i18, false);
        System::String formatted = System::String::Format("%i (%s)", (int)(uint32_t)mod.i1c,
                         versionStr._data);
        textVersion->SetParameter(0, formatted);
    }

    textDescription->SetParameter(0, mod.description);

    // --- Build conflict list markup ---
    int conflictCount = mod.files._size;
    System::String conflictResult("^jl^f0^c0^lb");
    // The conflicting mods' filenames.
    const System::Filename* conflictArray = mod.files._data;   // Mod +0x70 Array<Filename>

    for (int i = 0; i < conflictCount; i++) {
        // Find(const Filename&) takes the ADDRESS of the array element (retail
        // 0x14006ddee: `LEA RDX,[RAX + RCX*8]` on the array base, not a MOV of
        // the slot's value) — &conflictArray[i], not conflictArray[i].
        int cIdx = modification->Find(conflictArray[i]);
        const RtIO::Mod& cMod = modification->GetModification(cIdx);

        // Color: ^li^c1 if enabled, ^li^c0 if disabled
        const char* prefix = (cMod.enabled != 0) ? "^li^c1" : "^li^c0";

        // Build entry: prefix + name + " ^n[" + path + "]^n"
        const char* modName = cMod.name._data;
        const char* modPath = cMod.file._data;
        System::String entry = System::String::Format("%s%s ^n[%s]^n", prefix, modName, modPath);
        // retail's operator+= returns a copy of the new value that the
        // original discarded (see the Append note); genuinely unused here.
        conflictResult += entry;
    }

    // Append closing tag
    {
        System::String closeTag("^le");
        conflictResult += closeTag;
    }

    if (conflictCount == 0) {
        // Hide conflict title and list when no conflicts
        textConflictTitle->SetFlagsOnOff(UI::kElementFlagHidden, 0);
        textConflictList->SetFlagsOnOff(UI::kElementFlagHidden, 0);
    } else {
        // Set conflict list text (DocumentTextElement)
        textConflictList->SetText(conflictResult);
    }

    // --- Build resource list ---
    Collection::Array<char> resArray;

    // Push header
    {
        System::String hdr("^jl^f0^c0^lb");
        ModDetails_StringArrayPush_Impl(resArray, std::move(hdr));
    }

    // Traverse resource BST in-order
    const RtIO::ModFilenameSet::Node* bstRoot = mod.set._root;
    if (bstRoot) {
        const RtIO::ModFilenameSet::Node* node = BSTLeftmost(bstRoot);
        while (node) {
            // For each resource file, check conflicts with enabled mods
            bool hasConflict = false;
            bool higherPriority = false;

            for (int i = 0; i < conflictCount; i++) {
                // Same address-vs-value fix as above (retail 0x14006dfd7:
                // `LEA RDX,[RAX + RCX*8]`) — &conflictArray[i], not conflictArray[i].
                int cIdx = modification->Find(conflictArray[i]);
                const RtIO::Mod& cMod = modification->GetModification(cIdx);

                if (cMod.enabled != 0) {
                    const RtIO::ModFilenameSet::Node* cBstRoot = cMod.set._root;
                    if (BSTContains(cBstRoot, node)) {
                        hasConflict = true;
                        if (cIdx < modIndex) {
                            higherPriority = true;
                        }
                    }
                    // Restore modification pointer (decompilation shows reload)
                    modification = ::IO::Modification::_singleton;
                }
            }

            // Color prefix
            const char* prefix;
            if (!hasConflict)
                prefix = "^li^c0";
            else if (higherPriority)
                prefix = "^li^c2";
            else
                prefix = "^li^c1";

            // Build entry: prefix + filename + "^n"
            const char* filename = node->element._data;
            System::String entryStr = System::String::Format("%s%s^n", prefix, filename);
            ModDetails_StringArrayPush_Impl(resArray, std::move(entryStr));
            // Note: ModDetails_StringArrayPush_Impl reads entryStr in place; it
            // is released here when this iteration's scope ends.

            node = BSTNext(node);
        }
    }

    // Push footer
    {
        System::String ftr("^le");
        ModDetails_StringArrayPush_Impl(resArray, std::move(ftr));
    }

    // Null-terminate the byte buffer (original code does this inline after last push).
    resArray.SetSize(resArray.GetSize() + 1, '\0');

    // Build String from null-terminated buffer, set on textResourceList
    {
        const char* data = resArray._data;
        System::String resultStr(data ? data : "");
        textResourceList->SetText(resultStr);
    }

    // Free resource array buffer (the ~Array below then frees a null buffer).
    System::MemoryAllocator::MemFree(resArray._data);
    resArray._data = nullptr;
    resArray._size = 0;
    resArray._capacity = 0;
}

// ============================================================
// ModDetailsDialog::OpenUpdateDialog — opens a dialog from .rsc resource
// ============================================================
// Shared by OpenUpdateDialog and DeleteMod (both open "Dialog/ModUpdate.rsc").
// Original: FUN_14006d400 (OpenUpdateDialog) and FUN_14006d550 (DeleteMod).
// Both are structurally identical: load resource, create 0xB8 DialogInstance,
// set vtable to PTR_FUN_14015c6c0 (ModUpdateDialog), add to UI.
void ModDetailsDialog::OpenUpdateDialog(const char* rscPath) {
    // Load dialog resource
    System::Filename filenameBuf(rscPath);
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(filenameBuf);
    // IsDerivedFrom check for Dialog type — the resource IS a Dialog if it loads
    // from a .rsc file, so we pass it directly (matches original behavior where
    // the check always succeeds for valid resources).
    const UI::Dialog* dialog = static_cast<const UI::Dialog*>(ext);

    // Allocate and construct DialogInstance (0xB8 bytes)
    void* newDialog = System::MemoryAllocator::MemAlloc(sizeof(ModUploadDialog), 8);
    ModUploadDialog* dialogInst = nullptr;
    if (newDialog) {
        // ENDGAME B.3.8 DLG: the real class. The old path ran the DialogInstance
        // ctor door and stamped g_ModUpdateDialog_Vtable (0x14015c6c0), which is
        // unregistered, so the stamp was skipped and the object stayed a bare
        // DialogInstance (B38 D9 #15).
        dialogInst = ::new (newDialog) ModUploadDialog(
            _ui, dialog, Object::InternalPtr<UI::DialogInstance>());
    }

    // Add to UI
    Object::InternalPtr<UI::DialogInstance> dialogRef(dialogInst);
    _ui->AddDialog(&dialogRef);
}

// ============================================================
// OnElementEvent (0x14006D6A0): OnCommand — button dispatch.
// ============================================================
void ModDetailsDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 == UI::EventId::Activated)
        printf("[ModDetailsDialog::OnEvent] click id=%d\n", param4);

    if (param3 != UI::EventId::Activated)
        return;

    if (param4 == 0) {
        Object::InternalPtr<UI::DialogInstance> basePtr(this);
        _ui->CloseDialog(&basePtr);
    } else if (param4 == 1) {
        // OpenUpdateDialog — opens ModUpdate.rsc dialog
        OpenUpdateDialog("Dialog/ModUpdate.rsc");
    } else if (param4 == 2) {
        // DeleteMod — also opens ModUpdate.rsc dialog (same behavior as OpenUpdateDialog)
        OpenUpdateDialog("Dialog/ModUpdate.rsc");
    }
}
