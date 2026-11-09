// src/game/Dialog/optionsdialog.cpp
// Clean reimplementation of OptionsDialog methods.
//
// OptionsDialog (0x1C0 bytes): Game options panel (audio, video, input, gameplay).
// Layout:
//   +0x00..+0xB7 = DialogInstance base
//   +0xB8..+0x1B8 = UI element pointers and saved settings
//
// Original source: C:\Projects\Buildy\code\Game\Dialog/OptionsDialog.h
//                  Dialog\OptionsDialog.cpp

#include "platform/common/display_density.h"
#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ComboElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TabElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/CheckElement.h"
#include "ui/SliderElement.h"
#include "Dialog/OptionsDialog.h"
#include "Dialog/MessageDialog.h"
#include "Dialog/OptionInputBindDialog.h"
#include "ui/ButtonElement.h"   // X::TypeId() (ENDGAME B.3.9)
#include "ui/Dialog.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Dialog/AutoSaveDialog.h"   // X::TypeId() (ENDGAME B.3.9)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "engine/MainLoop.h"   // ENDGAME B.3.7 callers (K1)
#include "audio/Audio.h"     // ENDGAME B.3.7 callers (K4)
#include "input/Input.h"   // ENDGAME B.3.7 callers (K3)
#include "Dialog/ui_dialogs.h"
#include "input/Device.h"   // ENDGAME B.3.9 SS2: Input::Device::_singleton
// ENDGAME B.3.5 batch K1: video/Device.h FIRST. It pulls no Volume.h itself, but the
// house rule (a runtime type header ahead of anything that reaches <windows.h> through
// resolve.h, whose <wingdi.h> `Rectangle()` hides Volume.h's global `struct Rectangle`)
// is kept here too so a later include change cannot break it silently.
#include "video/Device.h"       // Video::Device, Video::Configuration
#include "options.h"
#include "ui/DialogInstance.h"
#include <cstring>   // strcmp (reached through engine_funcs.h until B3R W2-EZ)
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/Filename.h"
#include "ui/UserInterface.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "io/Persistence.h"   // ENDGAME B.3.4 caller batch P
#include "ext/enhanced_tab/enhanced_tab.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOMENU.md): the Enhanced tab
#include "ext/fullscreen_mode/fullscreen_mode.h"   // EXT SEAM (NOT RETAIL): the Video tab's Off / Exclusive / Borderless
#include "ext/render_backend/render_backend.h"   // EXT SEAM (NOT RETAIL): the Video tab's DirectX 11 / Vulkan
#include "graphics/SunLight.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/FAA-MENU.md): ExtRequestMaxShadowResSync
#include "ext/phone_menu/phone_menu.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): UI scale minimum
#include "ext/render_pipe/render_pipe.h"   // EXT SEAM (NOT RETAIL, review S-028): SettleBeforeStepWrite
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// In-app RenderDoc capture (standalone only) — ApplyVideoSettings arms a delayed
// capture after a video change so we can inspect the post-rebuild render targets.
#include "renderdoc_integration.h"
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "engine/UpdateLoop.h"   // B.3.x W0: UpdateLoop::Update by name
#include "system/Value.h"        // System::Time
#include "Dialog/exe_bodies.h"
#include "Game/exe_bodies.h"      // B3R: GameSettings_Save / _Load
#include "Game/inputbindings.h"   // B3R: Game::RegisterDefaultInputBindings
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// ============================================================
// Element IDs assigned in Startup, dispatched in OnEvent
// ============================================================
enum OptionsElementId {
    // Slider IDs (msg 0x17)
    kSlider_UI          = 0,
    kSlider_Music       = 1,
    kSlider_Ambient     = 2,
    kSlider_Effect      = 3,
    // Combo IDs (msg 0x14)
    kCombo_Renderer     = 4,
    kCombo_Adapter      = 5,
    kCombo_Resolution   = 6,
    kCombo_Refresh      = 7,
    kCombo_Antialias    = 8,
    kCombo_Filter       = 9,
    // Button IDs (msg 0x12)
    kButton_Ok          = 10,
    kButton_Cancel      = 11,
    kButton_Apply       = 12,
    // Input bind buttons start at ID 13 (0x0D)
    kInputBind_Base     = 13,
};

// ============================================================
// Video device data structure layout (from Ghidra analysis)
// ============================================================
// GraphicsInterface (from Video::Device::GetInterface):
//   +0x00: name (System::String, 8 bytes — passed as String* for AddItem)
//   +0x08: adapter array pointer
//   +0x10: adapter count (int)
//
// Adapter (stride 0x38):
//   +0x00: name (System::String, 8 bytes)
//   +0x08: display mode array pointer
//   +0x10: display mode count (int)
//
// DisplayMode (stride 0x18):
//   +0x00: width (uint32_t)
//   +0x04: height (uint32_t)
//   +0x08: refresh rate array pointer
//   +0x10: refresh rate count (int)
//
// RefreshRate (stride 0x08):
//   +0x00: numerator (uint32_t)
//   +0x04: denominator (uint32_t)

// ============================================================
// Layout comments for singletons
// ============================================================
// Audio config singleton (int[4]):
//   +0x00: UI volume, +0x04: Music, +0x08: Ambient, +0x0C: Effect
//
// Video config singleton:
//   +0x04: renderer, +0x08: adapter, +0x0C: width, +0x10: height
//   +0x24: antialias, +0x28: filter
//   +0x2C: shadow res, +0x30: shadow quality, +0x34: reflection quality
//   +0x38: refresh num, +0x3C: refresh den, +0x40: fullscreen, +0x41: vsync
//
// g_pGameSettingsPtr -> int32_t* (pointer at 0x1401c3de0 to actual settings array):
//   [0]=uiScale, [1]=autosave, [2]=statusIconDist, [3]=statusIconEnabled
//   [4]=statusIconScale, [5]=edgeScroll, [6]=autopause, [7]=lastTab
//   Byte: +0x14=edgeScroll, +0x15=showCelsius, +0x16=showWeather, +0x17=autoslow, +0x18=autopause
//
// Video::Device:
//   +0x98: anisotropic filter level count (int)
//   +0xF8: current renderer index (int)

// Helper: find a named element of the dialog as the UI class retail reads it as
// (OnOpened 0x1400742d0 stores each FindElement result in place).
template <class T>
static T* FindElementAs(OptionsDialog* dialog, const char* name) {
    System::String nameStr(name);
    return static_cast<T*>(dialog->FindElement(nameStr).Get());
}

// Helper: set checkbox state (CheckElement::_checked, +0xD8)
static inline void SetCheckState(UI::CheckElement* elem, uint8_t val) {
    if (elem) elem->_checked = val;
}

// ============================================================
// ApplyKeyMapToUI (exe 0x140099260)
// ============================================================
// Ghidra calls it OptionsDialog_helper_9260, but it is not options-specific and it
// is not "cursor speed / zoom" as an older comment here guessed. Retail body, in
// order: copy five UI sound-effect pointers out of the game container into the
// UserInterface's sound table, hand the container's white sprite (+0xA8) to
// UI::UserInterface::SetFullscreenFadeSprite, then write the UI scale ratio
// (settings[0] / 100.0f) to UserInterface::_uiScale — which edittool.cpp:674 reads back.
//
// Retail callers: 0x140098fa0 GameLoadDialogLoopT::Startup (splash_loop_stubs.cpp),
// 0x1400727d0 OptionsDialog::RestoreVideoSettings (below), 0x140097c00
// GameUpdateLoop::Startup and 0x14009b930 MenuDialogLoopT::Startup (the four code
// xrefs). Retail null-checks neither parameter.
// B4: stays free — it reads the GameContainer (game.h, block GAME-a) through its
// first argument; not a member of a class this block owns.
void GameContainer::ApplyKeyMapToUI(UI::UserInterface* userInterface) {
    // 0x140099260 tests neither RCX (the container) nor RDX (the UI) nor the settings global.
    userInterface->_sounds[2] = beginDragEffect.Get();   // +0xA8 BeginDrag
    userInterface->_sounds[3] = endDragEffect.Get();     // +0xB0 EndDrag
    userInterface->_sounds[4] = buttonDownEffect.Get();  // +0xB8 ButtonDown
    userInterface->_sounds[5] = buttonUpEffect.Get();    // +0xC0 ButtonUp
    userInterface->_sounds[6] = alertEffect.Get();       // +0xC8 Alert
    {
        UI::Sprite sprite = spriteWhite;
        userInterface->SetFullscreenFadeSprite(&sprite);
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): ext.phone_menu raises a UI scale
    // below the lowest at which its menus fit (a settings.cfg written before that floor existed).
    g_gameSettings->uiScale = Ext::PhoneMenu::ClampUiScale(g_gameSettings->uiScale);
    // NOT RETAIL (P9, ui_render_scale.h): the setting is in screen units; at render scale 100 (every
    // desktop) this is retail's uiScale / 100.0f ([0x1401c3de0], 0x1400992ba).
    userInterface->_uiScale = Platform::Display::UiScaleRatio(g_gameSettings->uiScale);
}

// Helper: get input key name as UI::String (replaces FUN_140099c20)
// Converts device type (-1=unbound, 0=gamepad, 1=keyboard, 2=mouse) and key index
// to a UI-display string like "Keyboard5" or "Unbound", through the game
// container's keyNames string table (+0xD0).
static UI::String GetInputKeyName(int deviceType, int keyIndex) {
    GameContainer* keyMapContainer = g_gameContainer;
    if (!keyMapContainer) return UI::String();

    System::String tempStr;
    if (deviceType < 0) {
        tempStr = System::String("Unbound");
    } else {
        const char* deviceNames[] = { "Gamepad", "Keyboard", "Mouse" };
        const char* devName = (deviceType >= 0 && deviceType <= 2) ? deviceNames[deviceType] : "Unknown";
        tempStr = System::String::Format("%s%i", devName, keyIndex);
    }

    return UI::String(keyMapContainer->keyNames, tempStr);
}

// Helper: set the key name text on a button element (replaces FUN_140073490)
// Finds "textInputKey" child in the element, formats device+key into display text.
static void SetInputBindButtonText(UI::Element* element, int deviceType, uint32_t keyIndex) {
    // Find "textInputKey" in the element
    System::String nameStr("textInputKey");
    UI::Element* found = element->FindElement(nameStr).Get();

    if (!found) return;

    // Check it's a TextElement
    if (!::Object::IsDerivedFromNullSafe(found, UI::TextElement::TypeId()))
        return;

    // Get the key name as UI::String, then set as text
    UI::String uiStr = GetInputKeyName(deviceType, keyIndex);

    static_cast<UI::TextElement*>(found)->SetText(&uiStr);
}

// Helper: set slider min/max/value (SliderElement _minimum / _maximum / _value)
static inline void SetSliderRange(UI::SliderElement* elem, float minVal, float maxVal) {
    if (!elem) return;
    elem->_minimum = minVal;
    elem->_maximum = maxVal;
}
static inline void SetSliderValue(UI::SliderElement* elem, float val) {
    if (elem) elem->_value = val;
}

// ENDGAME B.3.9: UI::TextElement / UI::StringTable both have a ported TypeId() now
// (class statics, always populated), so the EnsureTextElementTypeId() lazy-resolve
// and LoadStringTable's inline lazy-resolve are dead.

// Helper: load a StringTable resource by path.
static const UI::StringTable* LoadStringTable(const char* path) {
    System::Filename filenameObj(path);   // ctor already normalises delimiters
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(filenameObj);
    if (!ext) return nullptr;

    if (::Object::IsDerivedFromNullSafe(ext, UI::StringTable::TypeId()))
        return static_cast<const UI::StringTable*>(ext);
    return nullptr;
}

// ============================================================
// ValidateVideoConfig (replaces FUN_140072eb0)
// ============================================================
// Calls Video::Device::ValidateConfiguration to fix up any invalid
// combinations of renderer/adapter/resolution/antialias settings.
//
// 8th arg note: the original (verified in Ghidra @ 0x140072eb0) passes the
// same `&savedAdapter` field twice — slots [+0x194] for both the 2nd and 8th
// argument. Not a typo in our reimpl; matches retail.
//
// Defensive guard: ValidateConfiguration internally does
// `*p5 / *p6 == *adapterRefresh / *adapterRefreshDen` to find a refresh-rate
// match by ratio (Runtime @ 0x1800f7370). If `*p6` (savedRefreshDen) is 0 we
// hit `idiv 0` -> SIGFPE. That can only happen if the Configuration singleton
// wasn't populated with refresh defaults before the dialog opened — which in
// turn only happens if g_pVideoConfigSingleton was NULL at Startup time (so
// the saved-* fields stayed zero-initialized). In standalone we now fall back
// to the Runtime DLL's _singleton slot in EngineFuncsInit, but keep this
// guard as belt-and-braces so a future regression here can't crash the game.
void OptionsDialog::ValidateVideoConfig() {
    if (savedRefreshDen == 0) {
        // Defaults from Video::Configuration ctor (Runtime @ 0x1800f6f10): 60/1.
        // Better to pretend "60Hz" than to crash; the next Apply will write
        // ValidateConfiguration's clamped values back to the singleton.
        savedRefreshNum = 60;
        savedRefreshDen = 1;
    }
    // THE DOOR DECIDES THE OVERLOAD. `Video::Device` has TWO
    // `ValidateConfiguration`s at adjacent ordinals; this call is
    // `?ValidateConfiguration@Device@Video@@QEAAXAEAH0000000@Z` (0x1800f7370, the EIGHT
    // `int&` out-parameter one), NOT `…QEAA_NXZ` (0x1800f8830, no arguments). Eight
    // arguments are spelled here so overload resolution cannot pick the sibling.
    ::Video::Device::_singleton->ValidateConfiguration(
        savedRenderer, savedAdapter,
        savedWidth,    savedHeight,
        savedRefreshNum, savedRefreshDen,
        savedAntialias, savedAdapter);
}

// ============================================================
// PopulateFilterCombo (replaces FUN_140073800)
// ============================================================
// Fills texture filter combo: "None", "Bilinear", then "Anisotropic Nx" for each level.
void OptionsDialog::PopulateFilterCombo() {
    Video::Device* vidDev = ::Video::Device::_singleton;
    if (!vidDev) return;

    comboFilter->RemoveAllItems();

    const UI::StringTable* stringTable = LoadStringTable("Dialog/StringTable.rsc:textureFilter");
    if (!stringTable) return;

    // Item 0: "None" (filter -1)
    {
        System::String textStr(stringTable->GetText(0));
        UI::Sprite sprite;
        comboFilter->AddItem(sprite, textStr, (void*)(intptr_t)-1);  // CAST-OK: ptr_int
    }

    // Item 1: "Bilinear" (filter 0)
    {
        System::String textStr(stringTable->GetText(1));
        UI::Sprite sprite;
        comboFilter->AddItem(sprite, textStr, nullptr);
    }

    // Anisotropic filter levels: "Anisotropic 1x", "Anisotropic 2x", etc.
    int anisoLevels = vidDev->maxAnisotropy;
    if (anisoLevels > 0) {
        // First level: "Anisotropic 1x"
        System::String textStr(stringTable->GetText(2));
        System::String fmtStr = System::String::Format("%s 1x", textStr._data);
        UI::Sprite sprite;
        comboFilter->AddItem(sprite, fmtStr, (void*)(intptr_t)1);  // CAST-OK: ptr_int
    }
    // Higher levels: "Anisotropic 2x", "Anisotropic 4x", etc.
    for (int level = 2; level <= anisoLevels; level += 2) {
        System::String textStr(stringTable->GetText(2));
        System::String fmtStr = System::String::Format("%s %ix", textStr._data, level);
        UI::Sprite sprite;
        comboFilter->AddItem(sprite, fmtStr, (void*)(intptr_t)level);  // CAST-OK: ptr_int
    }

    // Set current selection based on savedFilter
    int filterVal = savedFilter;
    int selIdx;
    if (filterVal == -1) selIdx = 0;
    else if (filterVal == 0) selIdx = 1;
    else if (filterVal == 1) selIdx = 2;
    else selIdx = (filterVal >> 1) + 2;

    comboFilter->SetCurrentItem(selIdx);
}

// ============================================================
// PopulateAntialiasCombo (replaces FUN_140073b10)
// ============================================================
// Fills antialias combo: "None", then "Nx multisample" for each supported mode.
void OptionsDialog::PopulateAntialiasCombo() {
    Video::Device* vidDev = ::Video::Device::_singleton;
    if (!vidDev) return;

    comboAntialias->RemoveAllItems();

    const UI::StringTable* stringTable = LoadStringTable("Dialog/StringTable.rsc:multiSample");

    int modeCount = vidDev->GetMultisampleModeCount();

    for (int i = 0; i < modeCount; i++) {
        bool supported = vidDev->IsMultisampleModeSupported(i);
        if (!supported) continue;

        int addedIdx;
        if (i == 0) {
            // "None"
            System::String textStr;
            if (stringTable) ::new (&textStr) System::String(stringTable->GetText(0));
            UI::Sprite sprite;
            addedIdx = comboAntialias->AddItem(sprite, textStr, nullptr);
        } else {
            // "Nx multisample" — e.g. "2x Multisample"
            System::String textStr;
            if (stringTable) ::new (&textStr) System::String(stringTable->GetText(1));
            System::String fmtStr = System::String::Format("%ix %s", i + 1, textStr._data);
            UI::Sprite sprite;
            addedIdx = comboAntialias->AddItem(sprite, fmtStr, (void*)(intptr_t)i);  // CAST-OK: ptr_int
        }

        if (savedAntialias == i) {
            comboAntialias->SetCurrentItem(addedIdx);
        }
    }
}

// ============================================================
// PopulateRefreshCombo (replaces FUN_140073d20)
// ============================================================
// Fills refresh rate combo for the currently selected resolution.
void OptionsDialog::PopulateRefreshCombo() {
    Video::Device* vidDev = ::Video::Device::_singleton;
    if (!vidDev) return;

    const Video::Device::GraphicsInterface& gfxIface = vidDev->GetInterface(savedRenderer);
    const Video::Device::Adapter& adapter = gfxIface.adapterData[savedAdapter];

    // Find display mode matching saved width/height
    int modeCount = adapter.modeCount;
    const Video::Device::DisplayMode* modes = adapter.modeData;
    int modeIdx = -1;
    for (int i = 0; i < modeCount; i++) {
        if (modes[i].width == savedWidth && modes[i].height == savedHeight) {
            modeIdx = i;
            break;
        }
    }
    if (modeIdx < 0) modeIdx = 0;

    const Video::Device::DisplayMode& modeEntry = modes[modeIdx];
    const Video::Device::RefreshRate* refreshArray = modeEntry.refreshData;
    int refreshCount = modeEntry.refreshCount;

    comboRefresh->RemoveAllItems();

    int currentIdx = -1;
    for (int i = 0; i < refreshCount; i++) {
        int num = refreshArray[i].numerator;
        int den = refreshArray[i].denominator;
        int rate = (den != 0) ? (num / den) : 0;

        System::String fmtStr = System::String::Format("%i", rate);
        UI::Sprite sprite;
        comboRefresh->AddItem(sprite, fmtStr, (void*)(intptr_t)i);  // CAST-OK: ptr_int

        if (num == savedRefreshNum && den == savedRefreshDen)
            currentIdx = i;
    }

    comboRefresh->SetCurrentItem(currentIdx);
}

// ============================================================
// PopulateResolutionCombo (replaces FUN_140073ec0)
// ============================================================
// Fills resolution combo with "WxH" entries for the current adapter.
void OptionsDialog::PopulateResolutionCombo() {
    Video::Device* vidDev = ::Video::Device::_singleton;
    if (!vidDev) return;

    const Video::Device::GraphicsInterface& gfxIface = vidDev->GetInterface(savedRenderer);
    const Video::Device::Adapter& adapter = gfxIface.adapterData[savedAdapter];
    int modeCount = adapter.modeCount;
    const Video::Device::DisplayMode* modes = adapter.modeData;

    printf("[OptionsDialog::PopulateResolutionCombo] savedW=%u savedH=%u modeCount=%d "
           "renderer=%u adapter=%u\n",
           savedWidth, savedHeight, modeCount,
           savedRenderer, savedAdapter);

    comboResolution->RemoveAllItems();

    int currentIdx = -1;
    for (int i = 0; i < modeCount; i++) {
        int32_t w = modes[i].width;
        int32_t h = modes[i].height;

        if (i < 5 || (w == savedWidth && h == savedHeight))
            printf("[OptionsDialog::PopRes]   mode[%d] = %ux%u%s\n", i, w, h,
                   (w == savedWidth && h == savedHeight) ? " <-- MATCH" : "");

        System::String fmtStr = System::String::Format("%ix%i", w, h);
        UI::Sprite sprite;
        comboResolution->AddItem(sprite, fmtStr, (void*)(intptr_t)i);  // CAST-OK: ptr_int

        if (w == savedWidth && h == savedHeight)
            currentIdx = i;
    }

    comboResolution->SetCurrentItem(currentIdx);

    PopulateRefreshCombo();
}

// ============================================================
// PopulateAdapterCombo (replaces FUN_140074040)
// ============================================================
// Fills adapter combo for the current renderer, then repopulates dependent combos.
void OptionsDialog::PopulateAdapterCombo() {
    Video::Device* vidDev = ::Video::Device::_singleton;
    if (!vidDev) return;

    comboAdapter->RemoveAllItems();

    const Video::Device::GraphicsInterface& gfxIface = vidDev->GetInterface(savedRenderer);
    int adapterCount = gfxIface.adapterCount;

    for (int i = 0; i < adapterCount; i++) {
        // Adapter name is the System::String at the start of each 0x38-byte entry.
        UI::Sprite sprite;
        void* itemData = (void*)(intptr_t)i;  // CAST-OK: ptr_int
        comboAdapter->AddItem(sprite, gfxIface.adapterData[i].name, itemData);
    }

    comboAdapter->SetCurrentItem(savedAdapter);

    PopulateResolutionCombo();
    PopulateAntialiasCombo();
    PopulateFilterCombo();
}

// ============================================================
// PopulateVideoComboBoxes (replaces FUN_140074140)
// ============================================================
// Fills all video combo boxes: renderer, adapter, resolution, refresh, antialias, filter.
void OptionsDialog::PopulateVideoComboBoxes() {
    Video::Device* vidDev = ::Video::Device::_singleton;
    if (!vidDev) return;

    // --- Renderer combo ---
    comboRenderer->RemoveAllItems();

    int ifaceCount = vidDev->GetInterfaceCount();
    for (int i = 0; i < ifaceCount; i++) {
        // GraphicsInterface starts with a System::String (the interface name).
        const Video::Device::GraphicsInterface& gfxIface = vidDev->GetInterface(i);
        UI::Sprite sprite;
        void* itemData = (void*)(intptr_t)i;  // CAST-OK: ptr_int
        comboRenderer->AddItem(sprite, gfxIface.name, itemData);
    }

    comboRenderer->SetCurrentItem(savedRenderer);

    // --- Adapter combo (also populates resolution/antialias/filter) ---
    PopulateAdapterCombo();
}

// ============================================================
// Startup (0x1400742d0)
// ============================================================
//
// Bisect harness for the standalone-exe Slot4 crash:
// each major init block is wrapped behind a `static bool s_enable*` flag
// (default ON, trivially flippable).
// To localize the per-frame KeyMap::Pressed crash, flip flags to false one
// at a time. See `docs/history/notes/NOTES030126.md` and the project memory for context.
void OptionsDialog::OnOpened() {
    // Bisect knobs — flip these to narrow down the standalone Slot4 crash.
    // Order mirrors the original Startup body.
    static const bool s_enableButtons         = true;
    static const bool s_enableAudioSliders    = true;
    static const bool s_enableVideoCombos     = true;
    static const bool s_enableShadowReflCombos= true;
    static const bool s_enableVidCheckboxes   = true;
    static const bool s_enableInputList       = true;  // re-enabled after KeyMap default bindings populated at boot
    static const bool s_enableAutosaveCombo   = true;
    static const bool s_enableUIScaleSlider   = true;
    static const bool s_enableGameplayChecks  = true;
    static const bool s_enableMouseClipCheck  = true;
    static const bool s_enableTabRestore      = true;

    printf("[OptionsDialog::Startup] this=%p  flags: btn=%d aud=%d vid=%d "
           "sh=%d vchk=%d ilist=%d auto=%d uisc=%d game=%d mclip=%d tab=%d\n",
           this,
           (int)s_enableButtons, (int)s_enableAudioSliders, (int)s_enableVideoCombos,
           (int)s_enableShadowReflCombos, (int)s_enableVidCheckboxes,
           (int)s_enableInputList, (int)s_enableAutosaveCombo,
           (int)s_enableUIScaleSlider, (int)s_enableGameplayChecks,
           (int)s_enableMouseClipCheck, (int)s_enableTabRestore);

    ::Audio::Configuration* audioCfg = ::Audio::Configuration::_singleton;
    ::Video::Configuration* vidCfg = ::Video::Configuration::_singleton;
    ::Input::Configuration* inputCfg = ::Input::Configuration::_singleton;

    // --- Buttons: find, assign IDs, set buttonCancel's shortcut key ---
    if (s_enableButtons) {
        UI::ButtonElement* buttonOk     = FindElementAs<UI::ButtonElement>(this, "buttonOk");
        UI::ButtonElement* buttonCancel = FindElementAs<UI::ButtonElement>(this, "buttonCancel");
        UI::ButtonElement* buttonApply  = FindElementAs<UI::ButtonElement>(this, "buttonApply");
        buttonOk->SetElementId(kButton_Ok);
        buttonCancel->SetElementId(kButton_Cancel);
        buttonApply->SetElementId(kButton_Apply);
        if (buttonCancel) buttonCancel->_keyIndex = 8;
    }

    // --- Audio sliders ---
    if (s_enableAudioSliders) {
        this->sliderUI      = FindElementAs<UI::SliderElement>(this, "sliderUI");
        this->sliderMusic   = FindElementAs<UI::SliderElement>(this, "sliderMusic");
        this->sliderAmbient = FindElementAs<UI::SliderElement>(this, "sliderAmbient");
        this->sliderEffect  = FindElementAs<UI::SliderElement>(this, "sliderEffect");
        this->sliderUI->SetElementId(kSlider_UI);
        this->sliderMusic->SetElementId(kSlider_Music);
        this->sliderAmbient->SetElementId(kSlider_Ambient);
        this->sliderEffect->SetElementId(kSlider_Effect);
        UI::SliderElement* audioSliders[4] = { this->sliderUI, this->sliderMusic,
                                               this->sliderAmbient, this->sliderEffect };
        for (int i = 0; i < 4; i++) {
            SetSliderRange(audioSliders[i], 0.0f, 100.0f);
        }
        if (audioCfg) {
            SetSliderValue(audioSliders[0], (float)audioCfg->uiVolume);
            SetSliderValue(audioSliders[1], (float)audioCfg->musicVolume);
            SetSliderValue(audioSliders[2], (float)audioCfg->ambientVolume);
            SetSliderValue(audioSliders[3], (float)audioCfg->effectVolume);
        }
    }

    // --- Video combos: find + IDs + save config + populate ---
    if (s_enableVideoCombos) {
        this->comboRenderer   = FindElementAs<UI::ComboElement>(this, "comboRenderer");
        this->comboAdapter    = FindElementAs<UI::ComboElement>(this, "comboAdapter");
        this->comboResolution = FindElementAs<UI::ComboElement>(this, "comboResolution");
        this->comboAntialias  = FindElementAs<UI::ComboElement>(this, "comboAntialias");
        this->comboFilter     = FindElementAs<UI::ComboElement>(this, "comboFilter");
        this->comboRefresh    = FindElementAs<UI::ComboElement>(this, "comboRefresh");
        this->comboRenderer->SetElementId(kCombo_Renderer);
        this->comboAdapter->SetElementId(kCombo_Adapter);
        this->comboResolution->SetElementId(kCombo_Resolution);
        this->comboRefresh->SetElementId(kCombo_Refresh);
        this->comboAntialias->SetElementId(kCombo_Antialias);
        this->comboFilter->SetElementId(kCombo_Filter);
        if (vidCfg) {
            this->savedRenderer   = vidCfg->interfaceIndex;
            this->savedAdapter    = vidCfg->adapterIndex;
            this->savedWidth      = vidCfg->width;
            this->savedHeight     = vidCfg->height;
            this->savedRefreshNum = vidCfg->refreshNumerator;
            this->savedRefreshDen = vidCfg->refreshDenominator;
            this->savedAntialias  = vidCfg->multisample;
            this->savedFilter     = vidCfg->textureFilter;
        }
        PopulateVideoComboBoxes();
        // EXT SEAM (NOT RETAIL, src/ext/render_backend/render_backend.h): the dual exe's Renderer combo
        // lists one item per backend in slot 1's place. Retail's items otherwise.
        Ext::RenderBackend::OpenVideoRow(this, this->comboRenderer);
    }

    // --- Shadow/reflection combos ---
    if (s_enableShadowReflCombos) {
        this->comboShadowRes         = FindElementAs<UI::ComboElement>(this, "comboShadowResolution");
        this->comboShadowQuality     = FindElementAs<UI::ComboElement>(this, "comboShadowQuality");
        this->comboReflectionQuality = FindElementAs<UI::ComboElement>(this, "comboReflectionQuality");
        if (vidCfg) {
            this->comboShadowRes->SetCurrentItem(vidCfg->shadowScalar);
            this->comboShadowQuality->SetCurrentItem(vidCfg->shaderDetail);
            this->comboReflectionQuality->SetCurrentItem(vidCfg->reflectionDetail);
        }
    }

    // --- Video / input checkboxes (fullscreen, vsync, software mouse) ---
    if (s_enableVidCheckboxes) {
        this->checkFullscreen = FindElementAs<UI::CheckElement>(this, "checkFullscreen");
        this->checkVSync      = FindElementAs<UI::CheckElement>(this, "checkVSync");
        if (vidCfg) {
            SetCheckState(this->checkFullscreen, vidCfg->fullscreen);
            SetCheckState(this->checkVSync,      vidCfg->vsync);
        }
        this->checkSoftwareMouse = FindElementAs<UI::CheckElement>(this, "checkSoftwareMouse");
        if (inputCfg) SetCheckState(this->checkSoftwareMouse, inputCfg->_mouseDisplay);
    }
    // EXT SEAM (NOT RETAIL, docs/android/P8_INPUT_OPTIONS.md): ext.phone_menu on a fixed surface
    // (Android) hides the Video rows that change nothing there and keeps VSync on; retail elsewhere.
    Ext::PhoneMenu::FitVideoRows(this, this->comboResolution, this->comboRefresh, this->checkFullscreen,
                                 this->checkVSync);
    // EXT SEAM (NOT RETAIL, src/ext/fullscreen_mode/fullscreen_mode.h): ext.fullscreen_mode puts the combo
    // Off / Exclusive / Borderless in the Fullscreen checkbox's cell (the checkbox kept, hidden). Retail
    // elsewhere, and on a renderer without an exclusive mode.
    Ext::FullscreenMode::OpenVideoRow(this, this->checkFullscreen);

    // --- Input bind list (prime suspect for the per-frame KeyMap::Pressed crash) ---
    if (s_enableInputList) {
        this->inputList = FindElementAs<UI::Element>(this, "inputList");
        PopulateInputBindList();
    }

    // --- Autosave combo ---
    if (s_enableAutosaveCombo) {
        this->comboAutosave = FindElementAs<UI::ComboElement>(this, "comboAutosave");
        const UI::StringTable* stringTable = LoadStringTable("Dialog/StringTable.rsc:autoSave");
        if (stringTable && this->comboAutosave) {
            for (int i = 0; i < 9; i++) {
                System::String textStr(stringTable->GetText(i));
                UI::Sprite sprite;
                this->comboAutosave->AddItem(sprite, textStr, (void*)(intptr_t)i);  // CAST-OK: ptr_int
            }
        }
        int autoSaveIdx = (g_gameSettings != nullptr) ? g_gameSettings->autosaveInterval : 0;
        if (autoSaveIdx < 0) autoSaveIdx = 0;
        if (autoSaveIdx > 8) autoSaveIdx = 8;
        if (this->comboAutosave) this->comboAutosave->SetCurrentItem(autoSaveIdx);
    }

    // --- UI scale slider ---
    if (s_enableUIScaleSlider) {
        this->sliderUIScale = FindElementAs<UI::SliderElement>(this, "sliderUIScale");
        float uiScaleMin = 75.0f;
        float uiScaleMax = 150.0f;
        Platform::Display::UiScaleSliderRange(&uiScaleMin, &uiScaleMax);   // unchanged at density 1
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): ext.phone_menu moves the
        // minimum to the lowest scale at which its menus fit on this screen (35 % on the S25), or to
        // 25 % on a screen narrower than the menus' backdrop; retail's 75 with the row off.
        uiScaleMin = Ext::PhoneMenu::UiScaleSliderMinimum(uiScaleMin);
        SetSliderRange(this->sliderUIScale, uiScaleMin, uiScaleMax);
        if (g_gameSettings != nullptr)
            SetSliderValue(this->sliderUIScale, (float)g_gameSettings->uiScale);
    }

    // --- Gameplay settings checkboxes and sliders ---
    if (s_enableGameplayChecks) {
        this->checkStatusIcon       = FindElementAs<UI::CheckElement>(this, "checkStatusIcon");
        this->sliderStatusIcon      = FindElementAs<UI::SliderElement>(this, "sliderStatusIcon");
        this->sliderStatusIconScale = FindElementAs<UI::SliderElement>(this, "sliderStatusIconScale");
        this->checkShowCelsius      = FindElementAs<UI::CheckElement>(this, "checkShowCelsius");
        this->checkEdgeScroll       = FindElementAs<UI::CheckElement>(this, "checkEdgeScroll");
        this->checkShowWeather      = FindElementAs<UI::CheckElement>(this, "checkShowWeather");
        this->checkAutoslow         = FindElementAs<UI::CheckElement>(this, "checkAutoslow");
        this->checkAutopause        = FindElementAs<UI::CheckElement>(this, "checkAutopause");

        if (g_gameSettings != nullptr) {
            GameSettings* gs = g_gameSettings;
            SetCheckState(this->checkAutoslow,   gs->autoSlow);
            SetCheckState(this->checkAutopause,  gs->autoPause);
            SetCheckState(this->checkStatusIcon, gs->statusIconEnable);
            SetCheckState(this->checkEdgeScroll, gs->edgeScroll);
            SetCheckState(this->checkShowCelsius, gs->showCelsius);
            SetCheckState(this->checkShowWeather, gs->showWeather);

            SetSliderRange(this->sliderStatusIcon, 0.0f, 100.0f);
            SetSliderValue(this->sliderStatusIcon, (float)gs->statusIconOpacity);

            SetSliderRange(this->sliderStatusIconScale, 25.0f, 200.0f);
            SetSliderValue(this->sliderStatusIconScale, (float)gs->statusIconScale);
        }
    }

    // --- MouseClip checkbox (depends on inputCfg) ---
    if (s_enableMouseClipCheck) {
        this->checkMouseClip = FindElementAs<UI::CheckElement>(this, "checkMouseClip");
        if (inputCfg) SetCheckState(this->checkMouseClip, inputCfg->_mouseClipping);
    }

    // --- Tab options: restore last active tab ---
    if (s_enableTabRestore) {
        this->tabOptions = FindElementAs<UI::TabElement>(this, "tabOptions");
        if (this->tabOptions) {
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOMENU.md): ext.ambient_occlusion's fifth tab "Enhanced", added
            // BEFORE the restore below so a saved lastOption of 4 lands on it. Offered whenever
            // the build has the extension (not gated on the row: the tab is where the player
            // turns it on); absent under the gate's /profile:faithful (menus stay retail's).
            Ext::EnhancedTab::Open(this, this->tabOptions);
            int lastTab = (g_gameSettings != nullptr) ? g_gameSettings->lastOption : 0;
            // The port's guard, not retail's: 0x1400750db..0x1400750fb passes settings[7]
            // (GameSettings +0x1c) to SetActiveTab unclamped. Bounded by the tab count, so an
            // out-of-range value still opens tab 0 and the added tab (index 4) is kept.
            if (lastTab < 0 || lastTab > this->tabOptions->_tabCount - 1) lastTab = 0;
            this->tabOptions->SetActiveTab(lastTab);
        }
    }
}

// ============================================================
// Shutdown (vtable +0x28, exe 0x140073480): RET 0 (3 bytes)
// ============================================================
// No-op shutdown; nothing to clean up.
void OptionsDialog::OnClosed() {
    Ext::EnhancedTab::Close(this);   // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOMENU.md): forget the Enhanced tab's elements
    Ext::FullscreenMode::CloseVideoRow(this);   // EXT SEAM (NOT RETAIL): forget the Video tab's combo
    Ext::RenderBackend::CloseVideoRow(this);    // EXT SEAM (NOT RETAIL): forget the Renderer combo's backends
}

// ============================================================
// ApplyInputSettings (0x140072360)
// ============================================================
// Reads checkMouseClip and checkSoftwareMouse from UI, writes to
// Input::Configuration singleton, enables/disables mouse clipping,
// enables cursor, and saves input config.
void OptionsDialog::ApplyInputSettings() {
    // No test of either singleton: 0x140072360 stores through Input::Configuration
    // (0x14007237f) and calls EnableMouseClipping / Save on the two singletons as read.
    ::Input::Configuration* inputCfg = ::Input::Configuration::_singleton;

    // Read mouse clip checkbox -> Input::Configuration::_mouseClipping (+0x08)
    inputCfg->_mouseClipping = checkMouseClip->_checked != 0;

    // Read software mouse checkbox -> Input::Configuration::_mouseDisplay (+0x0A)
    inputCfg->_mouseDisplay = checkSoftwareMouse->_checked != 0;

    // Enable cursor in UI
    _ui->EnableCursor(true);

    // Enable/disable mouse clipping based on checkbox
    bool mouseClip = checkMouseClip->_checked;
    ::Input::Device* inputDevice = ::Input::Device::_singleton;
    inputDevice->EnableMouseClipping(static_cast<uint8_t>(mouseClip));

    // Save input configuration (the tail jump at 0x140072413)
    inputCfg->Save();
}

// ============================================================
// ApplyGameSettings (0x140072420)
// ============================================================
// Reads all game settings from UI controls, writes to g_gameSettings,
// updates autosave timers on all open dialogs, persists settings,
// applies key map to UI, and updates UI scale.
void OptionsDialog::ApplyGameSettings() {
    // 0x140072420 reads [0x1401c3de0] unguarded at its first store (row 1300: the
    // port's `if (g_gameSettings == nullptr) return;` stood here until 2026-10-11).
    GameSettings* gs = g_gameSettings;

    // UI scale slider -> uiScale
    gs->uiScale = (int32_t)sliderUIScale->_value;

    // Autosave combo -> autosaveInterval
    gs->autosaveInterval = comboAutosave->_currentItem;

    // Status icon checkbox -> statusIconEnable (the checkbox byte, raw)
    gs->statusIconEnable = checkStatusIcon->_checked;

    // Status icon distance slider -> statusIconOpacity
    gs->statusIconOpacity = (int32_t)sliderStatusIcon->_value;

    // Status icon scale slider -> statusIconScale
    // Original divides by 100.0f then multiplies by 100.0f (cancels out to identity)
    {
        float sliderVal = sliderStatusIconScale->_value;
        gs->statusIconScale = (int32_t)sliderVal;
    }

    // The five flags take the checkbox byte raw (0x14007248f).
    gs->edgeScroll  = checkEdgeScroll->_checked;
    gs->showCelsius = checkShowCelsius->_checked;
    gs->showWeather = checkShowWeather->_checked;
    gs->autoSlow    = checkAutoslow->_checked;
    gs->autoPause   = checkAutopause->_checked;

    // Last active tab -> lastOption
    gs->lastOption = tabOptions->_activeTab;

    // Iterate all open dialogs and update autosave timer on each
    UI::UserInterface* userInterface = _ui;
    // B3R: IsA / IsDerivedFrom / the restart (0x14005e120 AutoSaveDialog_Slot4) are
    // called by name, so the log line's two door-pointer fields (isA / isDerived) are
    // gone and restartFn is the body's address.
    fprintf(stderr, "[AutoSave] Options ApplySettings: newIntervalIdx=%d restartFn=%p tid=%u\n",
            gs->autosaveInterval, (void*)&AutoSaveDialog_Slot4, AutoSaveDialog::TypeId());
    fflush(stderr);
    {
        uint32_t typeId = AutoSaveDialog::TypeId();
        // `_dialogs.GetFirst()` IS retail's GetFirstDialog (0x180031150, whose
        // whole body is `*retval = this->_dialogs._head`); the iterator is the
        // bare node pointer, so `it.node` is what retail hands to CheckPtr.
        for (auto it = userInterface->_dialogs.GetFirst(); it; ++it) {
            UI::DialogInstance* dialog = it.node->element;
            if (dialog->IsA(typeId)) {   // ?IsA@Base@Object@@
                UI::DialogInstance* obj = it.node->element;
                bool isDerived = ::Object::IsDerivedFromNullSafe(obj, typeId);
                UI::DialogInstance* target = isDerived ? obj : nullptr;
                fprintf(stderr, "[AutoSave]   MATCH obj=%p derived=%d -> restart %s\n",
                        static_cast<void*>(obj), (int)isDerived, target ? "YES" : "NO(null target)");
                fflush(stderr);
                if (target) AutoSaveDialog_Slot4(static_cast<AutoSaveDialog*>(target));   // 0x14005e120
            }
        }
    }

    // Persist game settings
    gs->Save();   // 0x140098970

    // 0x140072420 does not CALL 0x140099260 here: it inlines the same body
    // (0x1400724f0..0x140072540: the five UI sound slots +0xA8..+0xC8 from the
    // container, SetFullscreenFadeSprite with the container's White sprite, then
    // _uiScale = uiScale / 100). The call is that inline, spelled once (row 1300).
    g_gameContainer->ApplyKeyMapToUI(userInterface);   // = the inline of 0x140099260's body

    // Set UI scale: uiScale is a percentage, divide by 100.0f to get the ratio
    // (P9: in screen units, Platform::Display::UiScaleRatio; retail's division at render scale 100)
    userInterface->_uiScale = Platform::Display::UiScaleRatio(gs->uiScale);

    // Notify game world of settings change (virtual call at vtable+0x18)
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (gameWorld) {
        // System::Time::Time(0.0f)
        System::Time timeVal{};
        RtSystem_Time_Ctor(&timeVal, 0.0f);
        // UpdateLoop slot 3, dispatched through the loop's own table (the world
        // is a GameUpdateLoop).
        static_cast<Engine::UpdateLoop*>(gameWorld)->Update(timeVal);
    }
}

// ============================================================
// ApplyVideoSettings (0x140071e10)
// ============================================================
// Reads all video combos/checkboxes, compares with current video config,
// applies changes via Video::Device (shutdown/startup or apply changed),
// and saves video config on success. Restores on failure.
void OptionsDialog::ApplyVideoSettings() {
    // Diagnostic trace for the video-apply path (default quiet). The dialog
    // logic itself is verified correct — see notes: ApplyChangedSettings runs
    // and returns success; the standalone gap is the render backend not
    // recreating the M5 scene targets with the new sample count.
    ::Video::Device* vidDev = ::Video::Device::_singleton;
    ::Video::Configuration* vidCfg = ::Video::Configuration::_singleton;
    if (!vidDev || !vidCfg) return;

    // Save current config for potential rollback
    int oldRenderer  = vidDev->activeInterface;  // current renderer
    int oldAdapter   = vidCfg->adapterIndex;
    int oldRefreshN  = vidCfg->refreshNumerator;
    int oldRefreshD  = vidCfg->refreshDenominator;
    int oldWidth     = vidCfg->width;
    int oldHeight    = vidCfg->height;
    int oldAntialias = vidCfg->multisample;
    int oldFilter    = vidCfg->textureFilter;
    int oldShadowRes = vidCfg->shadowScalar;
    int oldShadowQ   = vidCfg->shaderDetail;
    int oldReflQ     = vidCfg->reflectionDetail;
    uint8_t oldFullscreen = vidCfg->fullscreen;
    uint8_t oldVsync      = vidCfg->vsync;

    // Read new values from UI combos (the item data are indices: ptr_int)
    // Renderer
    int rendItem = comboRenderer->_currentItem;
    int newRenderer = (int)(intptr_t)comboRenderer->GetItemData(rendItem);  // CAST-OK: ptr_int

    // Adapter
    int adpItem = comboAdapter->_currentItem;
    int newAdapter = (int)(intptr_t)comboAdapter->GetItemData(adpItem);  // CAST-OK: ptr_int

    // Resolution
    int resItem = comboResolution->_currentItem;
    int resIndex = (int)(intptr_t)comboResolution->GetItemData(resItem);  // CAST-OK: ptr_int

    // Refresh
    int refItem = comboRefresh->_currentItem;

    // Look up actual width/height/refresh from the video interface data
    const Video::Device::GraphicsInterface& gfxIface = vidDev->GetInterface(newRenderer);
    const Video::Device::Adapter& adapter = gfxIface.adapterData[newAdapter];
    const Video::Device::DisplayMode& modeEntry = adapter.modeData[resIndex];
    const Video::Device::RefreshRate& refreshEntry = modeEntry.refreshData[refItem];

    int newRefreshN = refreshEntry.numerator;
    int newRefreshD = refreshEntry.denominator;
    int newWidth    = modeEntry.width;
    int newHeight   = modeEntry.height;

    // Antialias
    int aaItem = comboAntialias->_currentItem;
    int newAntialias = (int)(intptr_t)comboAntialias->GetItemData(aaItem);  // CAST-OK: ptr_int

    // Filter
    int fltItem = comboFilter->_currentItem;
    int newFilter = (int)(intptr_t)comboFilter->GetItemData(fltItem);  // CAST-OK: ptr_int

    // Shadow res, shadow quality, reflection quality
    int newShadowRes = comboShadowRes->_currentItem;
    int newShadowQ = comboShadowQuality->_currentItem;
    int newReflQ = comboReflectionQuality->_currentItem;

    // EXT SEAM (NOT RETAIL, src/ext/fullscreen_mode/fullscreen_mode.h): ext.fullscreen_mode's combo writes
    // its choice into the hidden checkbox read below; Exclusive <-> Borderless keeps VideoFullscreen at 1,
    // so the "nothing changed" test also asks whether that choice changed. False without the row.
    const bool fullscreenModeChanged = Ext::FullscreenMode::ReadVideoRow(this);
    // EXT SEAM (NOT RETAIL, src/ext/render_backend/render_backend.h): the dual exe's Renderer combo names
    // the other backend behind the same interface; a renderer change for the test below. False without
    // the row.
    const bool backendChanged = Ext::RenderBackend::ReadVideoRow(this);

    // Fullscreen / VSync checkboxes
    uint8_t newFullscreen = checkFullscreen->_checked;
    uint8_t newVsync = checkVSync->_checked;

    // Check if anything changed. If nothing changed, return early.
    bool rendererChanged = (oldRenderer != newRenderer) || backendChanged;
    bool adapterChanged = (oldAdapter != newAdapter);
    bool needsFullRestart = rendererChanged || adapterChanged;


    if (!rendererChanged) {
        // Same renderer — check if resolution/antialias/fullscreen match
        if (oldWidth == newWidth && oldHeight == newHeight &&
            oldAntialias == newAntialias && oldFullscreen == newFullscreen &&
            !fullscreenModeChanged) {
            if (!adapterChanged) {
                // Same adapter — check all remaining settings
                if (oldRefreshN == newRefreshN && oldRefreshD == newRefreshD &&
                    oldVsync == newVsync && oldFilter == newFilter &&
                    oldShadowRes == newShadowRes && oldShadowQ == newShadowQ &&
                    oldReflQ == newReflQ) {
                    return;  // Nothing changed
                }
            }
        }
    }

    // EXT SEAM (NOT RETAIL, review S-028): ext.render_pipe -- the video settings and the sun map's
    // descriptors below are read by an emit's pass 2 that P13_OVERLAP runs beside the steps: join it
    // first. A no-op unless that emit is in flight (never under the faithful profile).
    Ext::RenderPipe::SettleBeforeStepWrite();

    // A video setting actually changed → arm a RenderDoc capture ~8 frames out
    // so we capture a steady-state frame AFTER the engine's deferred swapchain/
    // target rebuild (BeginFrame consumes Device+0x9c next frame and skips it).
    // No-op unless launched via RenderDoc's UI. Lets us inspect the scene render
    // target's actual SampleDesc.Count to confirm whether MSAA is bound.
    RenderDocIntegration::RequestCaptureInFrames(8);

    // If renderer or adapter changed, need full shutdown/startup
    if (needsFullRestart) {
        ::Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
        if (mainLoop)
            mainLoop->OnVideoError();
        vidDev->ReleaseDynamic();
        vidDev->Shutdown();
    }

    // EXT SEAM (NOT RETAIL, src/ext/fullscreen_mode/fullscreen_mode.h): ext.fullscreen_borderless follows the
    // combo before the reset below reads it (DX11's swap chain and window size).
    Ext::FullscreenMode::CommitVideoRow(this);
    // EXT SEAM (NOT RETAIL, src/ext/render_backend/render_backend.h): with the device shut down, the
    // picked backend is selected and the adapters listed again before Startup below.
    Ext::RenderBackend::CommitVideoRow(this);

    // Write new settings to video config
    vidCfg->interfaceIndex = newRenderer;
    vidCfg->adapterIndex = newAdapter;
    vidCfg->refreshNumerator = newRefreshN;
    vidCfg->width = newWidth;
    vidCfg->height = newHeight;
    vidCfg->refreshDenominator = newRefreshD;
    // Update windowed size if resolution changed
    if (newWidth != oldWidth || newHeight != oldHeight) {
        vidCfg->windowWidth = newWidth;
        vidCfg->windowHeight = newHeight;
    }
    vidCfg->vsync = newVsync;
    vidCfg->multisample = newAntialias;
    vidCfg->fullscreen = newFullscreen;
    vidCfg->textureFilter = newFilter;
    vidCfg->shadowScalar = newShadowRes;
    vidCfg->shaderDetail = newShadowQ;
    vidCfg->reflectionDetail = newReflQ;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/FAA-MENU.md): ext.max_shadow_res -- the sun map's descriptors follow the NEW shadowScalar here,
    // on the main thread, so the rebuild ApplyChangedSettings requests lands at the right size. Left to the
    // per-frame sync, the render thread raised settingsChanged a second time and deadlocked in the resize.
    ::Graphics::SunLight::ExtRequestMaxShadowResSync();

    bool success;
    if (!needsFullRestart) {
        // Same renderer+adapter: just apply changed settings
        success = vidDev->ApplyChangedSettings();
    } else {
        // Full restart
        success = vidDev->Startup();
        vidDev->RecreateDynamic();
        ::Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
        if (mainLoop)
            mainLoop->OnVideoRestart();
    }


    if (success) {
        // Save the new config
        vidCfg->Save();
        // EXT SEAM (NOT RETAIL, src/ext/render_backend/render_backend.h): a backend change listed the
        // adapters again (the other backend's list), so the combos the dialog keeps open after Apply are
        // filled again from it, as OnOpened fills them. Retail's lists never change while it runs. A start
        // that fell back to the other backend is saved (ext.render_api) and refills them too.
        const bool fellBack = Ext::RenderBackend::SaveFallback();
        if (backendChanged || fellBack) {
            savedRenderer   = vidCfg->interfaceIndex;
            savedAdapter    = vidCfg->adapterIndex;
            savedWidth      = vidCfg->width;
            savedHeight     = vidCfg->height;
            savedRefreshNum = vidCfg->refreshNumerator;
            savedRefreshDen = vidCfg->refreshDenominator;
            savedAntialias  = vidCfg->multisample;
            savedFilter     = vidCfg->textureFilter;
            PopulateVideoComboBoxes();
            Ext::RenderBackend::OpenVideoRow(this, this->comboRenderer);
        }
    } else {
        // Rollback to old config
        vidCfg->adapterIndex = oldAdapter;
        vidCfg->width = oldWidth;
        vidCfg->interfaceIndex = oldRenderer;
        vidCfg->height = oldHeight;
        vidCfg->refreshNumerator = oldRefreshN;
        vidCfg->refreshDenominator = oldRefreshD;
        vidCfg->multisample = oldAntialias;
        vidCfg->fullscreen = oldFullscreen;
        vidCfg->vsync = oldVsync;
        vidCfg->textureFilter = oldFilter;
        vidCfg->shadowScalar = oldShadowRes;
        vidCfg->shaderDetail = oldShadowQ;
        vidCfg->reflectionDetail = oldReflQ;
        Ext::FullscreenMode::RollbackVideoRow(this);   // EXT SEAM (NOT RETAIL): the borderless choice too
        Ext::RenderBackend::RollbackVideoRow(this);    // EXT SEAM (NOT RETAIL): the renderer choice too
    }
}

// ============================================================
// RestoreVideoSettings (0x1400727d0)
// ============================================================
// Restores video/game settings on cancel: re-reads from persistence,
// restores last tab, and re-applies keymap config to UI.
//
// Row 1244: retail tests neither singleton (0x1400727dd-0x140072821). Both
// exist for the whole main loop: Persistence from Engine::Startup, the game
// settings block from GameMain before MainLoop::Run until after it returns.
void OptionsDialog::RestoreVideoSettings() {
    // Re-read all settings from persistence (0x1400727dd-0x1400727e7)
    ::IO::Persistence::_singleton->ReadSettings();

    // Re-read game settings from persistence (0x1400727ed / 0x1400727f4)
    g_gameSettings->Load();   // 0x140098b60

    // Restore last tab (0x140072806-0x140072821; the CheckPtr on +0x1b8 at
    // 0x140072800 is a bare RET)
    g_gameSettings->lastOption = tabOptions->_activeTab;

    // Re-apply keymap config to UI
    g_gameContainer->ApplyKeyMapToUI(_ui);   // 0x140072832: JMP 0x140099260, RCX = [0x1401c3de8] untested
}

// ============================================================
// OnApply (0x1400728e0): Apply all settings and save (keep dialog open)
// ============================================================
// Calls ApplyVideoSettings, ApplyInputSettings, saves audio config,
// applies game settings, and writes persistence settings.

void OptionsDialog::OnApply() {
    ApplyVideoSettings();
    ApplyInputSettings();

    // Save audio configuration
    if (::Audio::Configuration::_singleton)
        ::Audio::Configuration::_singleton->Save();

    ApplyGameSettings();

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOMENU.md): the Enhanced tab's values (Ext::AmbientOcclusion::Set records ext.ao_quality /
    // ext.ao_opacity / ext.ambient_occlusion into the map the WriteSettings below flushes).
    Ext::EnhancedTab::Apply(this);

    // Write all settings to disk
    if (::IO::Persistence::_singleton)
        ::IO::Persistence::_singleton->WriteSettings();
}

// ============================================================
// OnOk (0x140072930): Apply all settings, save, and close dialog
// ============================================================
// Same as OnApply plus closes the dialog window.

void OptionsDialog::OnOk() {
    OnApply();

    // Close dialog
    Object::InternalPtr<UI::DialogInstance> self(this);
    _ui->CloseDialog(&self);
}

// ============================================================
// OnCancel (0x140072840): Revert settings and close dialog
// ============================================================
// Reloads settings from disk, re-registers default input bindings,
// restores video settings, and closes the dialog.

void OptionsDialog::OnCancel() {
    // Reload persistence settings from disk
    if (::IO::Persistence::_singleton)
        ::IO::Persistence::_singleton->ReadSettings();

    // Re-register default input bindings and load saved config
    Game::RegisterDefaultInputBindings();   // 0x1400992f0

    // Reload input configuration
    if (::Input::Configuration::_singleton)
        ::Input::Configuration::_singleton->Load();

    // Restore mouse clipping from Input::Configuration::_mouseClipping (+0x08)
    if (::Input::Device::_singleton &&
        ::Input::Configuration::_singleton) {
        bool clipEnabled = ::Input::Configuration::_singleton->_mouseClipping;
        ::Input::Device::_singleton->EnableMouseClipping(static_cast<uint8_t>(clipEnabled));
    }

    // Reload persistence settings again (for audio config)
    if (::IO::Persistence::_singleton)
        ::IO::Persistence::_singleton->ReadSettings();

    // Reinitialize audio from saved config
    if (::Audio::Configuration::_singleton)
        ::Audio::Configuration::_singleton->Startup();

    // Restore video settings to pre-dialog state
    RestoreVideoSettings();

    // Close dialog
    Object::InternalPtr<UI::DialogInstance> self(this);
    _ui->CloseDialog(&self);
}

// ============================================================
// OnEvent (0x140072f20): Handle options changes
// ============================================================

void OptionsDialog::OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId eventType, int32_t eventData, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {

    if (eventType == UI::EventId::Activated) {
        // --- Button click ---
        if (eventData == kButton_Ok) {
            OnOk();
            return;
        }
        if (eventData == kButton_Cancel) {
            OnCancel();
            return;
        }
        if (eventData == kButton_Apply) {
            OnApply();
            return;
        }
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/COMPRESSAO.md): a button of an Enhanced tab
        // row (its id far above the input binds'); false for every id retail's dialog owns.
        if (Ext::EnhancedTab::OnButton(this, eventData)) return;
        // Input bind button clicks: IDs >= 13
        if (eventData >= kInputBind_Base) {
            // FUN_140010460: IsDerivedFrom(elem, ButtonElement type) — returns elem if valid, else null
            UI::Element* clickedElem = element->Get();
            bool isUIElement = ::Object::IsDerivedFromNullSafe(clickedElem, UI::ButtonElement::TypeId());
            Object::InternalPtr<UI::Element> parentPtr(isUIElement ? clickedElem : nullptr);

            unsigned int offset = (unsigned int)(eventData - kInputBind_Base);
            unsigned int slot = offset & 0x80000001;
            if ((int)slot < 0) {
                slot = ((slot - 1) | 0xFFFFFFFE) + 1;
            }
            int bindIndex = (int)offset / 2;

            OnInputBindClick(&parentPtr, bindIndex, slot);
        }
        return;
    }

    if (eventType == UI::EventId::SelectionChanged) {
        // --- Combo selection change (the item data are indices: ptr_int) ---
        if (eventData == kCombo_Renderer) {
            int curItem = this->comboRenderer->_currentItem;
            void* data = this->comboRenderer->GetItemData(curItem);
            this->savedRenderer = (int32_t)(intptr_t)data;  // CAST-OK: ptr_int
            ValidateVideoConfig();
            PopulateAdapterCombo();
            return;
        }
        if (eventData == kCombo_Adapter) {
            int curItem = this->comboAdapter->_currentItem;
            void* data = this->comboAdapter->GetItemData(curItem);
            this->savedAdapter = (int32_t)(intptr_t)data;  // CAST-OK: ptr_int
            ValidateVideoConfig();
            PopulateAntialiasCombo();
            PopulateFilterCombo();
            PopulateResolutionCombo();
            return;
        }
        if (eventData == kCombo_Resolution) {
            int curItem = this->comboResolution->_currentItem;
            int resIndex = (int)(intptr_t)this->comboResolution->GetItemData(curItem);  // CAST-OK: ptr_int

            int adapterIdx = this->savedAdapter;
            const Video::Device::GraphicsInterface* gfxIface =
                &::Video::Device::_singleton->GetInterface(this->savedRenderer);
            int adapterIdx2 = this->savedAdapter;

            this->savedWidth = gfxIface->adapterData[adapterIdx].modeData[resIndex].width;

            gfxIface = &::Video::Device::_singleton->GetInterface(this->savedRenderer);
            this->savedHeight = gfxIface->adapterData[adapterIdx2].modeData[resIndex].height;

            ValidateVideoConfig();
            PopulateRefreshCombo();
            return;
        }
        if (eventData == kCombo_Refresh) {
            int resItem = this->comboResolution->_currentItem;
            int resIndex = (int)(intptr_t)this->comboResolution->GetItemData(resItem);  // CAST-OK: ptr_int

            int refItem = this->comboRefresh->_currentItem;
            int refIndex = (int)(intptr_t)this->comboRefresh->GetItemData(refItem);  // CAST-OK: ptr_int

            int adapterIdx = this->savedAdapter;
            const Video::Device::GraphicsInterface* gfxIface =
                &::Video::Device::_singleton->GetInterface(this->savedRenderer);
            int adapterIdx2 = this->savedAdapter;

            this->savedRefreshNum =
                gfxIface->adapterData[adapterIdx].modeData[resIndex].refreshData[refIndex].numerator;

            gfxIface = &::Video::Device::_singleton->GetInterface(this->savedRenderer);
            this->savedRefreshDen =
                gfxIface->adapterData[adapterIdx2].modeData[resIndex].refreshData[refIndex].denominator;

            ValidateVideoConfig();
            return;
        }
        if (eventData == kCombo_Antialias) {
            int curItem = this->comboAntialias->_currentItem;
            void* data = this->comboAntialias->GetItemData(curItem);
            this->savedAntialias = (int32_t)(intptr_t)data;  // CAST-OK: ptr_int
            ValidateVideoConfig();
            return;
        }
        if (eventData == kCombo_Filter) {
            int curItem = this->comboFilter->_currentItem;
            void* data = this->comboFilter->GetItemData(curItem);
            this->savedFilter = (int32_t)(intptr_t)data;  // CAST-OK: ptr_int
            ValidateVideoConfig();
            return;
        }
        return;
    }

    if (eventType == UI::EventId::SliderChanged) {
        // --- Slider value change ---
        ::Audio::Configuration* audioCfg = ::Audio::Configuration::_singleton;
        if (!audioCfg) return;

        switch (eventData) {
            case kSlider_UI:      audioCfg->uiVolume      = (int32_t)this->sliderUI->_value; break;
            case kSlider_Music:   audioCfg->musicVolume   = (int32_t)this->sliderMusic->_value; break;
            case kSlider_Ambient: audioCfg->ambientVolume = (int32_t)this->sliderAmbient->_value; break;
            case kSlider_Effect:  audioCfg->effectVolume  = (int32_t)this->sliderEffect->_value; break;
            default: return;
        }
        return;
    }
}

// Helper: the Input::KeyMap the configuration owns (Configuration::_keyMap, +0x00).
static ::Input::KeyMap* GetActualKeyMap() {
    if (::Input::Configuration::_singleton == nullptr) return nullptr;
    return ::Input::Configuration::_singleton->_keyMap;
}

// ============================================================
// PopulateInputBindList (0x140073580): Fills input binding list
// ============================================================
// Iterates children of inputList in groups of 3:
//   child[i*3+0] = row container (contains "textInputName" label)
//   child[i*3+1] = bind button for slot 0
//   child[i*3+2] = bind button for slot 1
// Sets label text from the game container's gameKeys string table, sets button
// key name text, assigns element IDs starting at kInputBind_Base (13).
void OptionsDialog::PopulateInputBindList() {
    GameContainer* keyMapContainer = g_gameContainer;
    if (!keyMapContainer) return;
    ::Input::KeyMap* keyMap = GetActualKeyMap();  // for method calls (GetDeviceForId, etc.)
    if (!keyMap) return;

    int childCount = inputList->ChildCount();

    int numRows = childCount / 3;
    if (numRows <= 0) return;

    int elemId = kInputBind_Base;  // 13
    int inputId = 5;  // KeyMap input IDs start at 5

    for (int row = 0; row < numRows; row++, inputId++) {
        // Get the 3 children for this row
        UI::Element* rowChild = inputList->Child(row * 3).Get();

        // Find "textInputName" in the row container and set label
        {
            System::String nameStr("textInputName");
            UI::Element* textElem = rowChild->FindElement(nameStr).Get();

            if (textElem) {
                if (::Object::IsDerivedFromNullSafe(textElem, UI::TextElement::TypeId())) {
                    // Input name from the game container's gameKeys table (+0xC8)
                    UI::String uiStr(keyMapContainer->gameKeys, inputId);
                    static_cast<UI::TextElement*>(textElem)->SetText(&uiStr);
                }
            }
        }

        // Set bind button texts and element IDs for slots 0 and 1
        for (int slot = 0; slot < 2; slot++) {
            UI::Element* btnChild = inputList->Child(row * 3 + 1 + slot).Get();

            // Get current device and key for this input binding
            int device = keyMap->GetDeviceForId(inputId, slot);
            int key = keyMap->GetInputForId(inputId, slot);

            // Set the button's key name text
            SetInputBindButtonText(btnChild, device, key);

            // Assign element ID
            btnChild->SetElementId(elemId + slot);
        }

        elemId += 2;
    }
}

// ============================================================
// OnInputBindClick (0x1400729a0): Shows rebind dialog
// ============================================================
// When user clicks a key bind button in the options:
// 1. Creates OptionInputBindDialog from "Dialog/Options.rsc:inputBind"
// 2. Sets the input name text as a parameter
// 3. Shows the dialog modally (blocks until user presses a key)
// 4. Reads the captured key from the dialog result
// 5. If result=2 (clear), clears the binding
// 6. If result=1 (bound), checks for conflicts and handles them
// 7. Refreshes the input bind list
void OptionsDialog::OnInputBindClick(Object::InternalPtr<UI::Element>* element, int bindIndex, int slot) {
    GameContainer* keyMapContainer = g_gameContainer;
    if (!keyMapContainer) return;
    ::Input::KeyMap* keyMap = GetActualKeyMap();  // for method calls
    if (!keyMap) return;

    int inputId = bindIndex + 5;  // Convert bind index to KeyMap input ID

    // Load the input bind dialog resource
    System::Filename filenameObj("Dialog/Options.rsc:inputBind");   // ctor already normalises
    const ::Object::External* extResult =
        ::Object::ExternalFactory::_singleton->FindOrLoad(filenameObj);
    // Type check the external resource
    const UI::Dialog* external = nullptr;
    if (extResult) {
        bool isDerived = ::Object::IsDerivedFromNullSafe(extResult, UI::Dialog::TypeId());
        if (isDerived) external = static_cast<const UI::Dialog*>(extResult);
    }

    // Allocate and construct OptionInputBindDialog (0x140071d30)
    void* mem = System::MemoryAllocator::MemAlloc(200, 8);  // 0xC8 = 200 bytes
    OptionInputBindDialog* dialog = nullptr;
    if (mem) {
        dialog = ::new (mem) OptionInputBindDialog(_ui, external,
                                                   Object::InternalPtr<UI::DialogInstance>());
    }

    // Wrap in InternalPtr
    Object::InternalPtr<UI::DialogInstance> dialogPtr(dialog);

    // Set the input name text on the dialog
    // Input name from the game container's gameKeys table (+0xC8)
    UI::String inputNameUI(keyMapContainer->gameKeys, inputId);

    // Convert to System::String
    System::String inputNameStr(inputNameUI.operator System::String());

    // Find TextElement in dialog and set parameter 0
    {
        UI::Element* textElem = dialogPtr->FindElementType(UI::TextElement::TypeId()).Get();
        if (textElem) {
            static_cast<UI::TextElement*>(textElem)->SetParameter(0, inputNameStr);
        }
    }

    // Show dialog modally — get result
    Object::InternalPtr<UI::DialogInstance> showPtr = dialogPtr;
    int result = _ui->ShowDialog(&showPtr);

    // Read captured key data from dialog before deleting
    int capturedDevice = dialog->inputType;
    int capturedKey = dialog->keyIndex;

    // Check if the new binding conflicts with an existing one
    int conflictId = keyMap->GetIdForDeviceInput(capturedDevice, capturedKey);

    // Delete the input bind dialog
    dialogPtr.DeletePtr();

    if (result == 2) {
        // User chose "clear" — unbind this slot
        keyMap->ClearInput(static_cast<int32_t>(inputId), static_cast<int32_t>(slot));
        // Update button text to "Unbound"
        SetInputBindButtonText(element->Get(), -1, 0);
        goto cleanup;
    }

    if (result != 1) goto cleanup;  // User cancelled

    if (conflictId < 0) {
        // No conflict — just set the new binding
        goto set_binding;
    }

    // There IS a conflict — show the "already bound" confirmation dialog
    {
        // Load the inputBound dialog resource
        System::Filename fnameObj2("Dialog/Options.rsc:inputBound");   // ctor already normalises
        const ::Object::External* extResult2 =
            ::Object::ExternalFactory::_singleton->FindOrLoad(fnameObj2);
        const UI::Dialog* external2 = nullptr;
        if (extResult2) {
            if (::Object::IsDerivedFromNullSafe(extResult2, UI::Dialog::TypeId()))
                external2 = static_cast<const UI::Dialog*>(extResult2);
        }

        // Allocate and construct MessageDialog (0xC0 bytes, 0x14006d300)
        void* mem2 = System::MemoryAllocator::MemAlloc(0xC0, 8);
        MessageDialog* dialog2 = nullptr;
        if (mem2) {
            dialog2 = ::new (mem2) MessageDialog(_ui, external2,
                                                 Object::InternalPtr<UI::DialogInstance>());
        }

        Object::InternalPtr<UI::DialogInstance> dialog2Ptr(dialog2);

        // Get the name of the key that was pressed (new binding)
        UI::String newKeyUI = GetInputKeyName(capturedDevice, capturedKey);
        System::String newKeyStr(newKeyUI.operator System::String());

        // Get the name of the existing binding that conflicts
        UI::String conflictNameUI(keyMapContainer->gameKeys, conflictId);
        System::String conflictNameStr(conflictNameUI.operator System::String());

        // Check if the conflicting binding is the same as the current one
        // (in which case no confirmation is needed)
        bool sameBinding = false;
        {
            const char* str1 = inputNameStr._data;
            const char* str2 = conflictNameStr._data;
            if (str1 && str2) {
                sameBinding = (strcmp(str1, str2) == 0);
            } else {
                sameBinding = (str1 == str2);
            }
        }

        int confirmResult = 1;  // default: proceed
        if (!sameBinding) {
            // Set parameters on the confirmation dialog:
            // param 0 = new key name, param 1 = conflicting binding name, param 2 = current input name
            {
                UI::Element* textElem2 = dialog2Ptr->FindElementType(UI::TextElement::TypeId()).Get();
                if (textElem2) {
                    static_cast<UI::TextElement*>(textElem2)->SetParameter(0, newKeyStr);
                }

                UI::Element* textElem3 = dialog2Ptr->FindElementType(UI::TextElement::TypeId()).Get();
                if (textElem3) {
                    static_cast<UI::TextElement*>(textElem3)->SetParameter(1, conflictNameStr);
                }

                UI::Element* textElem4 = dialog2Ptr->FindElementType(UI::TextElement::TypeId()).Get();
                if (textElem4) {
                    static_cast<UI::TextElement*>(textElem4)->SetParameter(2, inputNameStr);
                }
            }

            // Show confirmation dialog
            Object::InternalPtr<UI::DialogInstance> show2Ptr = dialog2Ptr;
            confirmResult = _ui->ShowDialog(&show2Ptr);
            dialog2Ptr.DeletePtr();
        }

        if (confirmResult >= 1 && confirmResult <= 2) {
            // User confirmed (OK or Yes) — clear the old conflicting binding
            keyMap->ClearInput(static_cast<int32_t>(conflictId), static_cast<int32_t>(capturedDevice), static_cast<int32_t>(capturedKey));
            goto set_binding;
        }
        // User cancelled — don't change anything
    }

    // Refresh the input bind list display
    PopulateInputBindList();
    goto cleanup;

set_binding:
    keyMap->SetInput(inputId, slot, capturedDevice, capturedKey);
    ::Input::Device::_singleton->ResetState();

    // Refresh the entire input bind list
    PopulateInputBindList();

cleanup:
    ;
}


// ============================================================
// OptionsDialog standalone scaffolding
// ============================================================
// Vtable layout (exe @ 0x14015ceb0, 10 slots):
//   +0x00 Slot0 Dtor                — sets vtable, ~DialogInstance, conditional free
//   +0x08 Slot1 TypeName            — exe const-load
//   +0x10 Slot2 TypeId              — exe const-load
//   +0x18 Slot3 Persist             — runtime thunk
//   +0x20 Slot4 OnOpened            — above
//   +0x28 Slot5 OnClosed            — above
//   +0x30 Slot6 OnUpdateLayout      — runtime thunk
//   +0x38 Slot7 OnUpdate            — runtime thunk
//   +0x40 Slot8 OnElementEvent      — above
//   +0x48 Slot9 OnSerializeSettings — runtime thunk
//
// 10 slots vs MainMenuDialog's 20: OptionsDialog has only the DialogInstance
// interface (no DialogController), so no second-vtable block.

#include "Object/factory_registration.h"

// ENDGAME B.3.8 block DLG: OptionsDialog is a real class (Dialog/OptionsDialog.h);
// the hand-built table is gone. 0x14015ceb0 has no by-address reader, so its
// registration went with the table.

// Retail 0x140075150: copy parentIBP to a local stack slot, call
// DialogInstance::DialogInstance(self, ui, extRes, &localParent, 0, 0, 0),
// the vtable store, zero +0xB8..+0x1B8.
OptionsDialog::OptionsDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                             Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0) {
    ZeroFields();
}

// The reflected create path (create 0x140012df0): the tree's factory handed out
// a zero-filled object; this keeps the zero fields over the default base ctor.
OptionsDialog::OptionsDialog() { ZeroFields(); }

// +0xB8..+0x1B8 = 0, in address order (the saved-* dwords pairwise, H7).
void OptionsDialog::ZeroFields() {
    comboRenderer = nullptr;
    comboAdapter = nullptr;
    comboResolution = nullptr;
    comboRefresh = nullptr;
    comboAntialias = nullptr;
    checkFullscreen = nullptr;
    checkVSync = nullptr;
    comboFilter = nullptr;
    comboShadowRes = nullptr;
    comboShadowQuality = nullptr;
    comboReflectionQuality = nullptr;
    inputList = nullptr;
    checkSoftwareMouse = nullptr;
    sliderUI = nullptr;
    sliderMusic = nullptr;
    sliderAmbient = nullptr;
    sliderEffect = nullptr;
    sliderUIScale = nullptr;
    comboAutosave = nullptr;
    checkStatusIcon = nullptr;
    sliderStatusIcon = nullptr;
    sliderStatusIconScale = nullptr;
    checkEdgeScroll = nullptr;
    checkShowCelsius = nullptr;
    checkShowWeather = nullptr;
    checkAutoslow = nullptr;
    checkAutopause = nullptr;
    savedRenderer = 0;
    savedAdapter = 0;
    savedWidth = 0;
    savedHeight = 0;
    savedRefreshNum = 0;
    savedRefreshDen = 0;
    savedAntialias = 0;
    savedFilter = 0;
    checkMouseClip = nullptr;
    tabOptions = nullptr;
}

// create 0x140012df0: MemAlloc(0x1c0, 8), then the ctor.
::Object::Base* OptionsDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(OptionsDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) OptionsDialog());
}
// createInPlace 0x140012e30 (the tree's in-place build is kept, B8-13).
::Object::Base* OptionsDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) OptionsDialog());
}


// Parent literal EXACTLY retail's (RegisterType_OptionsDialog 0x14014c440 passes
// "public UI::DialogInstance" — an MSVC-era slip in Shining Rock's own macro use).
// BaseTypeInfo::StripNamespace (src/engine/object/type_reflection.cpp) keeps
// what follows the LAST ':', so both spellings resolve the parent to
// "DialogInstance" (B38_RULES D4, section 6 item 3).
DEFINE_EXE_TYPE(OptionsDialog, "public UI::DialogInstance")
