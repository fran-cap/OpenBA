// engine/src/ui/achievementdialog.cpp
// Clean reimplementation of AchievementDialog game logic.
//
// AchievementDialog (0xD0 bytes) — achievement progress display.
// Extends DialogInstance (0xB8 base).
// +0x20 = parent element, +0x28 = entity/data source, +0x30 = progress element.
//
// Decompiled from Application-x64.exe.

#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TableElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ProgressElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ButtonElement.h"     // B4 DLG-a: buttonOk's key index
#include "ui/ElementDescription.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // ENDGAME B.3.6 UI callers (K2)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K1)
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/AchievementDialog.h"
#include "Object/factory_registration.h"
#include "system/Value.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Entity/ComponentEntity.h"   // X::TypeId() (ENDGAME B.3.9)
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// ============================================================
// AchievementDialog::OnOpened (vtable +0x20) — Setup/populate achievement table
// ============================================================
// Finds "buttonOk" (ID 100), "tableAchievement", clones template rows
// for each achievement, finds child elements (imageIcon, imageStar,
// textTitle, textDescription), sets sprite/text, pushes 4-ptr entries
// to the entry array at +0xC0. Finally finds "textComplete" and stores at +0xB8.
//
// Original: 0x14005D490.

void AchievementDialog::OnOpened() {
    // Find "buttonOk" element, set ID 100, its shortcut key (+0xC0) = 8
    UI::Element* buttonOk;
    {
        System::String nameStr("buttonOk");
        buttonOk = FindElement(nameStr).Get();
    }
    buttonOk->SetElementId(100);
    // Retail stores the found element unchecked; +0xC0 is the button's key index.
    static_cast<UI::ButtonElement*>(buttonOk)->_keyIndex = 8;

    // Get AchievementManager singleton
    Achievement::AchievementManager* manager = ::Achievement::AchievementManager::_singleton;

    // Find "tableAchievement" element (a TableElement: SetChild below)
    UI::TableElement* tableElem;
    {
        System::String nameStr("tableAchievement");
        tableElem = static_cast<UI::TableElement*>(FindElement(nameStr).Get());
    }

    // Get achievement count
    int achievementCount = manager->GetAchievementCount();

    // The dialog resource, whose template 0 is the row
    const UI::Dialog* dialog = _dialog;

    // For each achievement: clone template, populate elements, push to array
    for (int i = 0; i < achievementCount; i++) {
        // Clone template element from dialog
        const UI::ElementDescription* elemDesc = dialog->GetTemplate(0).Get();

        Object::InternalPtr<UI::Element> newElem;
        if (elemDesc) {
            elemDesc->CreateInstance(&newElem, dialog);
        }
        UI::Element* rowElem = newElem.Get();

        // Set as table child at index i
        tableElem->SetChild(&newElem, i);

        // Find child elements in the cloned row (each stored unchecked)
        UI::ImageElement* imageIcon;
        {
            System::String nameStr("imageIcon");
            imageIcon = static_cast<UI::ImageElement*>(rowElem->FindElement(nameStr).Get());
        }

        UI::Element* imageStar;
        {
            System::String nameStr("imageStar");
            imageStar = rowElem->FindElement(nameStr).Get();
        }

        UI::TextElement* textTitle;
        {
            System::String nameStr("textTitle");
            textTitle = static_cast<UI::TextElement*>(rowElem->FindElement(nameStr).Get());
        }

        UI::TextElement* textDesc;
        {
            System::String nameStr("textDescription");
            textDesc = static_cast<UI::TextElement*>(rowElem->FindElement(nameStr).Get());
        }

        // Get achievement object at index i
        Achievement::Achievement* achievement = manager->GetAchievement(i).Get();

        // Set sprite on imageIcon from achievement sprite sheet + sprite name
        System::String spriteName = achievement->GetSpriteName();
        UI::Sprite sprite(achievement->_spriteSheet, spriteName);
        imageIcon->SetSprite(&sprite);

        // Hide imageStar initially (OnUpdateLayout shows it if awarded)
        imageStar->SetFlagsOnOff(UI::kElementFlagHidden, 0);

        // Set title text
        System::String titleText = achievement->GetTitle();
        UI::String titleStr(achievement->_stringTable, titleText);
        textTitle->SetText(&titleStr);

        // Set description text
        System::String descText = achievement->GetDescription();
        UI::String descStr(achievement->_stringTable, descText);
        textDesc->SetText(&descStr);

        // Push the 4-pointer entry (stride 0x20) to the entry array at +0xC0.
        // (The old hand-rolled copy guarded with `if (newCount != count)` — that is
        // retail's `if (newSize == oldSize) return;` early return, which
        // rtcoll::SetSizeCore already performs, so it is not carried forward.)
        entries.SetSize(entries.GetSize() + 1, Entry{imageIcon, imageStar, textTitle, textDesc});
    }

    // Find "textComplete" element, store at +0xB8
    {
        System::String nameStr("textComplete");
        textComplete = static_cast<UI::TextElement*>(FindElement(nameStr).Get());
    }
}

// ============================================================
// AchievementDialog::OnUpdate (vtable +0x38, exe 0x14005D480): RET 0 (3 bytes)
// ============================================================
// No-op handler; returns 0.
void AchievementDialog::OnUpdate(const System::Time& /*time*/) {
}

// ============================================================
// AchievementDialog::OnUpdateLayout (vtable +0x30) — Update achievement display
// ============================================================
// Iterates the entries at +0xC0 (stride 0x20), count at +0xC8.
// For each: checks IsAchievementAwarded, sets entry colors and star visibility.
// Calculates award percentage, formats as text on +0xB8.
//
// Original: 0x14005D240.

void AchievementDialog::OnUpdateLayout() {
    // Get AchievementManager singleton
    Achievement::AchievementManager* manager = ::Achievement::AchievementManager::_singleton;

    int count = entries._size;
    Entry* entryArray = entries._data;

    int awardedCount = 0;

    for (int i = 0; i < count; i++) {
        bool awarded = manager->IsAchievementAwarded(i);
        if (awarded) awardedCount++;

        uint32_t primaryColor   = awarded ? 0xFFE0E0E0u : 0xFF303030u;
        uint32_t secondaryColor = awarded ? 0xFFC0C0C0u : 0xFFB0B0B0u;

        // The star: show if awarded, hide if not
        entryArray[i].star->SetFlagsOnOff(0, awarded ? 1 : 0);

        // The icon: primary color (Element slot 10)
        entryArray[i].icon->SetColor(primaryColor);

        // The title: secondary color
        entryArray[i].title->SetColor(secondaryColor);

        // The description: secondary color
        entryArray[i].description->SetColor(secondaryColor);
    }

    // Format percentage: (awarded / total) * 100 + 0.5 (rounding)
    float pct = ((float)awardedCount / (float)count) * 100.0f + 0.5f;
    System::String pctStr = System::String::Format("%i", (int)(uint32_t)(int)pct);

    textComplete->SetParameter(0, pctStr);
}

// ============================================================
// AchievementDialog::OnElementEvent (vtable +0x40, exe 0x14005D440)
// ============================================================
// Byte-identical to GoalDialog::Slot8 (0x1400697C0): on a button click (msg 0x12)
// from buttonOk (id 100), wrap self in an InternalBasePtr and ask the UserInterface
// at +0x10 to close this dialog.
void AchievementDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 == UI::EventId::Activated && param4 == 100) {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
    }
}

// (AchievementDialog_Slot14 / _Slot17, two unreferenced second copies of
// AttendanceUI::OnCreated 0x14005dd40 / OnUpdateLayout 0x14005dc50 kept here
// "so the addresses stay searchable", are gone: B4 DLG-a.)

// ============================================================
// Construction, destruction, reflection (ENDGAME B.3.8 block DLG)
// ============================================================

AchievementDialog::AchievementDialog()
    : textComplete(nullptr) {}

// MainMenuDialog's opener inlines this ctor: DialogInstance(ui, dialog,
// nullParent, 0, 0, 0), then the fields zeroed.
AchievementDialog::AchievementDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
             Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      textComplete(nullptr) {}

// Retail's dtor (inside [0] 0x1400103f0): MemFree the entry array at +0xc0 and
// zero its header (+0xc0 data, +0xc8 count/capacity), then ~DialogInstance: the
// `entries` member's own dtor (AchievementDialog.h).
AchievementDialog::~AchievementDialog() {}

// create 0x140010320: MemAlloc(0xd0, 8), then the ctor.
::Object::Base* AchievementDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AchievementDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) AchievementDialog());
}
// createInPlace 0x140010390 (the tree's in-place build is kept, B8-13).
::Object::Base* AchievementDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) AchievementDialog());
}

// The reflection record stays at the TU position of the UI_DIALOG_STUB row it
// replaces (ui_dialog_stub_factories.cpp, B38 D4).
