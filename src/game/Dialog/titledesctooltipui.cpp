// src/game/Dialog/titledesctooltipui.cpp
// Clean reimplementation of TitleDescToolTipUI game logic.
//
// TitleDescToolTipUI (0x30 bytes) — tooltip that shows a title and description.
// Extends DialogController. No subclass fields.
//
// Decompiled from Application-x64.exe @ 0x14007b730 (OnCreated / vtable +0x20).

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/Internal.h"
#include "system/String.h"
#include <cstdint>
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/DialogInstance.h"

// ============================================================
// TitleDescToolTipUI::OnCreated (vtable +0x20)
// ============================================================
// Finds "textTitle" and "textDesc" in the owning dialog, then reads the data
// source's user data (two consecutive Strings, the title and the description)
// and sets them as each text's parameter 0.
//
// Original: 0x14007b730, 50 instructions.

// [4] 0x14007b730 — the class's only real slot (every other entry of its 11-slot
// table is a shared micro-return).
void TitleDescToolTipUI::OnCreated() {
    // --- "textTitle" / "textDesc": DialogInstance::FindElement, each narrowed with
    // IsDerivedFrom through the import slot 0x140151508 (TextElement) and CMOVNZ
    // (0x14007b785..0x14007b79d, 0x14007b7f8..0x14007b80e) ---
    UI::Element* foundTitle = _dialog->FindElement(System::String("textTitle")).Get();
    // B7 LX row 993: the member, unguarded (MOV RCX,RBX / CALL [0x140152a98] with no
    // null test at 0x14007b791, 0x14007b804, 0x14007b839).
    UI::TextElement* titleElement = foundTitle->IsDerivedFrom(UI::TextElement::TypeId())
        ? static_cast<UI::TextElement*>(foundTitle) : nullptr;
    UI::Element* foundDesc = _dialog->FindElement(System::String("textDesc")).Get();
    UI::TextElement* descElement = foundDesc->IsDerivedFrom(UI::TextElement::TypeId())
        ? static_cast<UI::TextElement*>(foundDesc) : nullptr;

    // --- The data source, narrowed to UI::Element, and its user data (read
    // unguarded, as retail's GetUserData call) ---
    Object::Base* dataSource = _dataSource;
    UI::Element* sourceElement = dataSource->IsDerivedFrom(UI::Element::TypeId())
        ? static_cast<UI::Element*>(dataSource) : nullptr;
    Object::Base* userData = sourceElement->GetUserData();

    if (userData) {
        // The user data is a TitleDescToolTipData (TitleDescToolTipUI.h) that the
        // tooltip's owner hands over through the Object::Base* slot; retail reads
        // it the same way, and sets both parameters with no test of either text.
        const TitleDescToolTipData* strings = reinterpret_cast<const TitleDescToolTipData*>(userData);   // LEFT: UI::Element's user data is Object::Base* (R-UI); the producer is not ported  // CAST-OK: user_data (S4 ruling)
        titleElement->SetParameter(0, strings->title);
        descElement->SetParameter(0, strings->desc);
    }
}
