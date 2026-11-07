#pragma once
// ============================================================================
// src/game/Dialog/OptionsDialog.h — ENDGAME B.3.8 block DLG
//
// The Options dialog (video / audio / input / game settings).
// Original source: C:\Projects\Buildy\code\Game\Dialog/OptionsDialog.h
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "OptionsDialog", parent literal "public UI::DialogInstance"
//   (sic — B38_RULES section 1.3), ti 0x1401c5360
//   create 0x140012df0 (MemAlloc(0x1c0, 8)), createInPlace 0x140012e30
//   ctors 0x140075150 (the door the tree registers), 0x140075280
//   table 0x14015ceb0, 10 slots:
//     [0] 0x140012f30 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x1400742d0 OnOpened
//     [5] 0x140073480 OnClosed `RET 0`      [6] DialogInstance::OnUpdateLayout
//     [7] DialogInstance::OnUpdate          [8] 0x140072f20 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0x1c0.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"

namespace UI {
class CheckElement;
class ComboElement;
class SliderElement;
class TabElement;
}

class OptionsDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(OptionsDialog)

public:
    OptionsDialog();                                                // create 0x140012df0
    OptionsDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                  Object::InternalPtr<UI::DialogInstance> parent);  // 0x140075150

    ~OptionsDialog() override = default;                           // [0] 0x140012f30
    void OnOpened() override;                                      // [4] 0x1400742d0
    void OnClosed() override;                                      // [5] 0x140073480 RET
    // [6] / [7]: DialogInstance's bodies, inherited (B38 D3.1).
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x140072f20

private:
    void ZeroFields();                                             // +0xB8..+0x1B8 = 0

    // Non-virtual members: retail passes the dialog in RCX (B4: the tree's
    // file-local helpers, made members).
    void OnApply();                                                // 0x1400728e0
    void OnOk();                                                   // 0x140072930
    void OnCancel();                                               // 0x140072840
    void ApplyVideoSettings();                                     // 0x140071e10
    void ApplyInputSettings();                                     // 0x140072360
    void ApplyGameSettings();                                      // 0x140072420
    void RestoreVideoSettings();                                   // 0x1400727d0
    void ValidateVideoConfig();                                    // 0x140072eb0
    void PopulateInputBindList();                                  // 0x140073580
    void OnInputBindClick(Object::InternalPtr<UI::Element>* element,
                          int bindIndex, int slot);                // 0x1400729a0
    void PopulateFilterCombo();                                    // 0x140073800
    void PopulateAntialiasCombo();                                 // 0x140073b10
    void PopulateRefreshCombo();                                   // 0x140073d20
    void PopulateResolutionCombo();                                // 0x140073ec0
    void PopulateAdapterCombo();                                   // 0x140074040
    void PopulateVideoComboBoxes();                                // 0x140074140

public:
    // Retail stores each FindElement result (an 8-byte InternalPtr) in place
    // (OnOpened 0x1400742d0); the element classes are the ones the fields are
    // read as (+0xd8 CheckElement::_checked, +0x118 ComboElement::_currentItem,
    // +0x118/+0x11c/+0x128 SliderElement min/max/value, +0xf8 TabElement::_activeTab).
    UI::ComboElement*  comboRenderer;         // +0xB8: "comboRenderer" (ID 4)
    UI::ComboElement*  comboAdapter;          // +0xC0: "comboAdapter" (ID 5)
    UI::ComboElement*  comboResolution;       // +0xC8: "comboResolution" (ID 6)
    UI::ComboElement*  comboRefresh;          // +0xD0: "comboRefresh" (ID 7)
    UI::ComboElement*  comboAntialias;        // +0xD8: "comboAntialias" (ID 8)
    UI::CheckElement*  checkFullscreen;       // +0xE0: "checkFullscreen"
    UI::CheckElement*  checkVSync;            // +0xE8: "checkVSync"
    UI::ComboElement*  comboFilter;           // +0xF0: "comboFilter" (ID 9)
    UI::ComboElement*  comboShadowRes;        // +0xF8: "comboShadowResolution"
    UI::ComboElement*  comboShadowQuality;    // +0x100: "comboShadowQuality"
    UI::ComboElement*  comboReflectionQuality; // +0x108: "comboReflectionQuality"
    UI::Element*       inputList;             // +0x110: "inputList" (key bind rows)
    UI::CheckElement*  checkSoftwareMouse;    // +0x118: "checkSoftwareMouse"
    UI::SliderElement* sliderUI;              // +0x120: "sliderUI" (ID 0)
    UI::SliderElement* sliderMusic;           // +0x128: "sliderMusic" (ID 1)
    UI::SliderElement* sliderAmbient;         // +0x130: "sliderAmbient" (ID 2)
    UI::SliderElement* sliderEffect;          // +0x138: "sliderEffect" (ID 3)
    UI::SliderElement* sliderUIScale;         // +0x140: "sliderUIScale"
    UI::ComboElement*  comboAutosave;         // +0x148: "comboAutosave"
    UI::CheckElement*  checkStatusIcon;       // +0x150: "checkStatusIcon"
    UI::SliderElement* sliderStatusIcon;      // +0x158: "sliderStatusIcon"
    UI::SliderElement* sliderStatusIconScale; // +0x160: "sliderStatusIconScale"
    UI::CheckElement*  checkEdgeScroll;       // +0x168: "checkEdgeScroll"
    UI::CheckElement*  checkShowCelsius;      // +0x170: "checkShowCelsius"
    UI::CheckElement*  checkShowWeather;      // +0x178: "checkShowWeather"
    UI::CheckElement*  checkAutoslow;         // +0x180: "checkAutoslow"
    UI::CheckElement*  checkAutopause;        // +0x188: "checkAutopause"
    // The pending video configuration: Video::Device::ValidateConfiguration's
    // eight int& out-parameters (0x140072eb0); signed (the adapter index is
    // sign-extended and scaled by 0x38).
    int32_t savedRenderer;                    // +0x190: Configuration::interfaceIndex
    int32_t savedAdapter;                     // +0x194: adapterIndex
    int32_t savedWidth;                       // +0x198: width
    int32_t savedHeight;                      // +0x19C: height
    int32_t savedRefreshNum;                  // +0x1A0: refreshNumerator
    int32_t savedRefreshDen;                  // +0x1A4: refreshDenominator
    int32_t savedAntialias;                   // +0x1A8: multisample
    int32_t savedFilter;                      // +0x1AC: textureFilter
    UI::CheckElement*  checkMouseClip;        // +0x1B0: "checkMouseClip"
    UI::TabElement*    tabOptions;            // +0x1B8: "tabOptions"
};

static_assert(offsetof(OptionsDialog, comboRenderer) == 0xB8, "OptionsDialog +0xb8");
static_assert(offsetof(OptionsDialog, comboAdapter) == 0xC0, "OptionsDialog +0xc0");
static_assert(offsetof(OptionsDialog, comboResolution) == 0xC8, "OptionsDialog +0xc8");
static_assert(offsetof(OptionsDialog, comboRefresh) == 0xD0, "OptionsDialog +0xd0");
static_assert(offsetof(OptionsDialog, comboAntialias) == 0xD8, "OptionsDialog +0xd8");
static_assert(offsetof(OptionsDialog, checkFullscreen) == 0xE0, "OptionsDialog +0xe0");
static_assert(offsetof(OptionsDialog, checkVSync) == 0xE8, "OptionsDialog +0xe8");
static_assert(offsetof(OptionsDialog, comboFilter) == 0xF0, "OptionsDialog +0xf0");
static_assert(offsetof(OptionsDialog, comboShadowRes) == 0xF8, "OptionsDialog +0xf8");
static_assert(offsetof(OptionsDialog, comboShadowQuality) == 0x100, "OptionsDialog +0x100");
static_assert(offsetof(OptionsDialog, comboReflectionQuality) == 0x108, "OptionsDialog +0x108");
static_assert(offsetof(OptionsDialog, inputList) == 0x110, "OptionsDialog +0x110");
static_assert(offsetof(OptionsDialog, checkSoftwareMouse) == 0x118, "OptionsDialog +0x118");
static_assert(offsetof(OptionsDialog, sliderUI) == 0x120, "OptionsDialog +0x120");
static_assert(offsetof(OptionsDialog, sliderMusic) == 0x128, "OptionsDialog +0x128");
static_assert(offsetof(OptionsDialog, sliderAmbient) == 0x130, "OptionsDialog +0x130");
static_assert(offsetof(OptionsDialog, sliderEffect) == 0x138, "OptionsDialog +0x138");
static_assert(offsetof(OptionsDialog, sliderUIScale) == 0x140, "OptionsDialog +0x140");
static_assert(offsetof(OptionsDialog, comboAutosave) == 0x148, "OptionsDialog +0x148");
static_assert(offsetof(OptionsDialog, checkStatusIcon) == 0x150, "OptionsDialog +0x150");
static_assert(offsetof(OptionsDialog, sliderStatusIcon) == 0x158, "OptionsDialog +0x158");
static_assert(offsetof(OptionsDialog, sliderStatusIconScale) == 0x160, "OptionsDialog +0x160");
static_assert(offsetof(OptionsDialog, checkEdgeScroll) == 0x168, "OptionsDialog +0x168");
static_assert(offsetof(OptionsDialog, checkShowCelsius) == 0x170, "OptionsDialog +0x170");
static_assert(offsetof(OptionsDialog, checkShowWeather) == 0x178, "OptionsDialog +0x178");
static_assert(offsetof(OptionsDialog, checkAutoslow) == 0x180, "OptionsDialog +0x180");
static_assert(offsetof(OptionsDialog, checkAutopause) == 0x188, "OptionsDialog +0x188");
static_assert(offsetof(OptionsDialog, savedRenderer) == 0x190, "OptionsDialog +0x190");
static_assert(offsetof(OptionsDialog, savedAdapter) == 0x194, "OptionsDialog +0x194");
static_assert(offsetof(OptionsDialog, savedWidth) == 0x198, "OptionsDialog +0x198");
static_assert(offsetof(OptionsDialog, savedHeight) == 0x19C, "OptionsDialog +0x19c");
static_assert(offsetof(OptionsDialog, savedRefreshNum) == 0x1A0, "OptionsDialog +0x1a0");
static_assert(offsetof(OptionsDialog, savedRefreshDen) == 0x1A4, "OptionsDialog +0x1a4");
static_assert(offsetof(OptionsDialog, savedAntialias) == 0x1A8, "OptionsDialog +0x1a8");
static_assert(offsetof(OptionsDialog, savedFilter) == 0x1AC, "OptionsDialog +0x1ac");
static_assert(offsetof(OptionsDialog, checkMouseClip) == 0x1B0, "OptionsDialog +0x1b0");
static_assert(offsetof(OptionsDialog, tabOptions) == 0x1B8, "OptionsDialog +0x1b8");
static_assert(sizeof(OptionsDialog) == 0x1C0, "OptionsDialog is 0x1c0 (create 0x140012df0)");
static_assert(alignof(OptionsDialog) == 8, "OptionsDialog is qword-aligned");
