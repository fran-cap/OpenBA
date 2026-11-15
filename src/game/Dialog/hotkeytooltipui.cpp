// src/game/Dialog/hotkeytooltipui.cpp
// Clean reimplementation of HotKeyToolTipUI::Serialize (Slot4, vtable +0x20).
//
// HotKeyToolTipUI (0x30 bytes) is the per-element DialogController attached to
// toolbar (and other) buttons that displays a button's keyboard shortcut. Each
// time the dialog refreshes (Slot4 / "Serialize") it:
//   1. reads the controlled button's shortcut id, _keyIndex (set by
//      ToolbarDialog::ToolbarDialog when it auto-assigns Toolbar0..8 / Tool0..9 ids),
//   2. resolves that id to a (device, keyCode) pair via Input::KeyMap,
//   3. formats "<deviceName><keyCode>" (e.g. "Keyboard1") and looks it up in the
//      keyNames StringTable (Game container +0xD0) for a localized display
//      string,
//   4. writes it into the "textToolTip" (param 1) and "textHotKey" (param 0)
//      TextElements of its dialog.
//
// In standalone the generic UI_DIALOG_STUB left Slot4 a no-op, so the buttons'
// hotkey text kept the .rsc placeholder "[]" (the reported empty-brackets bug).
// Promoting Slot4 to this reimpl fills it. This is the same pattern as the
// BuildRequirementUI "????????" fix (src/game/Dialog/buildrequirementui.cpp).
//
// Decompiled from Application-x64.exe:
//   HotKeyToolTipUI_Serialize @0x14006a980 (vtable 0x14015bfc0, slot 4 / +0x20)
//   key-name builder FUN_140099c20 (format "%s%i" -> keyNames lookup)
// The (device,key) -> UI::String step mirrors the proven GetInputKeyName helper
// already used by optionsdialog.cpp in standalone.

#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Dialog/HotKeyToolTipUI.h"
#include "Object/factory_registration.h"
#include "system/String.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include "object/External.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/ButtonElement.h"
#include "input/Input.h"  // ENDGAME B.3.7 callers (K3)

namespace Game {


// Controller layout (shared DialogController base, see buildrequirementui.cpp):
//   _dialog     = the dialog instance
//   _dataSource = the controlled element (the button)

// Live Input::KeyMap (maps shortcut id -> device/key): the Configuration's
// first field, allocated by Input::Configuration::Startup.
static const Input::KeyMap* GetActualKeyMap() {
    if (Input::Configuration::_singleton == nullptr) return nullptr;
    return Input::Configuration::_singleton->_keyMap;
}

// Port of FUN_140099c20: build a UI::String for the (device,key) pair by
// formatting "<deviceName><keyIndex>" and resolving it against the keyNames
// StringTable. retval must be 16 bytes (UI::String). device < 0 => "Unbound".
//
// Shared with the two Tutorial call sites (tutorialimagehotkey.cpp and
// tutorialimageinputkey.cpp). Retail's param_1 is the KeyMap container whose
// +0xD0 is the keyNames StringTable; both tutorial callers pass
// *g_pKeyMapSingleton, which is exactly the container this function looks up, so
// the substitution is exact.
void Hotkey_BuildKeyName(UI::String* retval, int device, int keyIndex) {
    GameContainer* container = g_gameContainer;
    if (!container) return;

    System::String tempStr;
    if (device < 0) {
        tempStr.Set("Unbound");
    } else {
        static const char* kDeviceNames[] = { "Gamepad", "Keyboard", "Mouse" };
        const char* devName = (device <= 2) ? kDeviceNames[device] : "Unknown";
        tempStr = System::String::Format("%s%i", devName, keyIndex);
    }

    ::new (retval) UI::String(container->keyNames, tempStr);   // (retbuf, table, name)
}

// Find the named TextElement in the controller's dialog, resolve the bound key
// into a localized System::String, and set it as the element's parameter.
static void FillHotKeyText(HotKeyToolTipUI* self, const char* elemName, int paramIndex,
                           const Input::KeyMap* keyMap, int shortcutId) {
    System::String nameStr(elemName);
    UI::TextElement* textElem =
        static_cast<UI::TextElement*>(self->_dialog->FindElement(nameStr).Get());
    if (!textElem) return;

    int input  = keyMap->GetInputForId(shortcutId, 0);
    int device = keyMap->GetDeviceForId(shortcutId, 0);

    UI::String uiStr;
    Hotkey_BuildKeyName(&uiStr, device, input);

    // UI::String -> System::String (member, hidden retval). TextElement params
    // take a System::String; the converted string owns a ref released by its dtor.
    System::String sysStr(uiStr.operator System::String());

    textElem->SetParameter(paramIndex, sysStr);
}

}  // namespace Game

// ---- HotKeyToolTipUI -----------------------------------------------------------
// This controller is bound to the "textHotKey"/"textToolTip" TextElements of
// every toolbar button. Its Slot4 (OnCreated, +0x20) reads the button's
// auto-assigned shortcut id (ButtonElement::_keyIndex), resolves it through
// Input::KeyMap + the keyNames StringTable, and fills the hotkey text over the
// .rsc placeholder "[]" (empty brackets). Construction is the factory's alloc +
// ctor (ui_dialog_stub_factories.cpp); the engine fills the base fields
// (dialog +0x18, datasource +0x28) afterward.
// ENDGAME B.3.9: the type id the body checks the controlled element against
// (exref 0x140151660) is UI::Element's own _typeInfo; UI::Element has a ported
// TypeId().

// HotKeyToolTipUI::Serialize — Slot4 (+0x20). self = the controller.
// [4] 0x14006a980.
void HotKeyToolTipUI::OnCreated() {
    // Resolve the controlled element (the button) and read its shortcut id.
    Object::Base* obj = _dataSource;
    if (!(obj && ::Object::IsDerivedFromNullSafe(obj, UI::Element::TypeId()))) return;
    // The IsDerivedFrom check is UI::Element's; the shortcut id is the button's.
    UI::ButtonElement* button = static_cast<UI::ButtonElement*>(obj);

    int shortcutId = button->_keyIndex;
    if (shortcutId < 0) return;  // no shortcut assigned -> leave default

    const Input::KeyMap* keyMap = Game::GetActualKeyMap();
    if (!keyMap) return;


    Game::FillHotKeyText(this, "textToolTip", 1, keyMap, shortcutId);
    Game::FillHotKeyText(this, "textHotKey",  0, keyMap, shortcutId);
}
