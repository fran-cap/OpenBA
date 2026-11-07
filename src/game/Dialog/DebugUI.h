#pragma once
// ============================================================================
// src/game/Dialog/DebugUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "DebugUI", parent
//   "DialogController", table 0x14015b870, 11 slots, sizeof 0x130.
//   create 0x140010ec0: MemAlloc(0x130, 8), ??0DialogController, stamp, fields.
//   [0] 0x140010f10 scalar deleting dtor (~DebugUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [4] 0x1400619e0  OnCreated
//   [6] 0x1400645f0  RET 0
//   [7] 0x140061280  OnUpdateLayout
//   [8] 0x1400610c0  OnUpdate
//   [9] 0x140060bc0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI {
class ButtonElement;
class CheckElement;
class ComboElement;
class ImageElement;
class NumberElement;
class SliderElement;
}

class DebugUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(DebugUI)

public:
    DebugUI() = default;                                 // 0x140010ec0 (ctor inlined in the factory)

    // 0x140010f10 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~DebugUI() override = default;                                // [0] 0x140010f10
    void OnCreated() override;   // [4] 0x1400619e0
    void OnClosed() override {}   // [6] 0x1400645f0  RET 0 in retail
    void OnUpdateLayout() override;   // [7] 0x140061280
    void OnUpdate(const System::Time& time) override;   // [8] 0x1400610c0
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x140060bc0

private:
    // ---- non-virtual members (B4 DLG-a; each reads DebugUI fields through RCX;
    // no retail symbol, named from what they do) ----
    void SetDayLighting(int month);                  // 0x140061780 month keyframe -> sliders
    void WriteLightingColors(int month);             // 0x140061530 sliders -> month keyframe
    void OnDaySliderChanged();                       // 0x1400607e0 element 0x154
    void OnDrawComboChanged();                       // 0x14005fc60 element 0x166 (drawCombo +0x38)
    void OnWeatherComboChanged();                    // 0x1400608a0 element 0x163 (comboWeather +0x108)
    void TriggerDisasterOnBuilding();                // 0x14005ff30 button 327 (diseaseCombo +0x30)
    // 0x14005fcc0: RCX (this) is not read; RDX is the InternalPtr<CheckElement>
    // the caller copied (by address), R8D the flag.
    void ToggleDebugFlag(Object::InternalPtr<UI::Element>* check, uint32_t flag);

public:
    // ---- fields (Ghidra's element-name strings; B4 H4: typed with the class
    // each element is read as) --------
    UI::ComboElement*  diseaseCombo = nullptr;        // +0x30: "diseaseCombo" ComboElement
    UI::ComboElement*  drawCombo = nullptr;           // +0x38: "drawCombo" ComboElement
    UI::NumberElement* numberDrawCalls = nullptr;     // +0x40: "numberDrawCalls" NumberElement
    UI::NumberElement* numberTriangles = nullptr;     // +0x48: "numberTriangles" NumberElement
    UI::NumberElement* numberVertices = nullptr;      // +0x50: "numberVertices" NumberElement
    UI::NumberElement* numberBatches = nullptr;       // +0x58: "numberBatches" NumberElement
    UI::NumberElement* numberFPS = nullptr;           // +0x60: "numberFPS" NumberElement (smoothed FPS, int)
    UI::NumberElement* numberFrameMS = nullptr;       // +0x68: "numberFrameMS" NumberElement (smoothed frame ms, float)
    UI::NumberElement* numberTotalMS = nullptr;       // +0x70: "numberTotalMS" NumberElement (smoothed total ms, float)
    UI::SliderElement* sliderDay = nullptr;           // +0x78: "sliderDay" SliderElement (day of year)
    UI::SliderElement* valueSunR = nullptr;           // +0x80: "valueSunR" SliderElement
    UI::SliderElement* valueSunG = nullptr;           // +0x88: "valueSunG" SliderElement
    UI::SliderElement* valueSunB = nullptr;           // +0x90: "valueSunB" SliderElement
    UI::SliderElement* valueAmbR = nullptr;           // +0x98: "valueAmbR" SliderElement
    UI::SliderElement* valueAmbG = nullptr;           // +0xA0: "valueAmbG" SliderElement
    UI::SliderElement* valueAmbB = nullptr;           // +0xA8: "valueAmbB" SliderElement
    UI::SliderElement* valueFogR = nullptr;           // +0xB0: "valueFogR" SliderElement
    UI::SliderElement* valueFogG = nullptr;           // +0xB8: "valueFogG" SliderElement
    UI::SliderElement* valueFogB = nullptr;           // +0xC0: "valueFogB" SliderElement
    UI::SliderElement* valueFogN = nullptr;           // +0xC8: "valueFogN" SliderElement (fog near)
    UI::SliderElement* valueFogF = nullptr;           // +0xD0: "valueFogF" SliderElement (fog far)
    UI::SliderElement* valueFogM = nullptr;           // +0xD8: "valueFogM" SliderElement (fog max density)
    UI::SliderElement* valueSunH = nullptr;           // +0xE0: "valueSunH" SliderElement (sun heading)
    UI::SliderElement* valueSunP = nullptr;           // +0xE8: "valueSunP" SliderElement (sun pitch)
    UI::ImageElement*  imageColorSun = nullptr;       // +0xF0: "imageColorSun" ImageElement
    UI::ImageElement*  imageColorAmb = nullptr;       // +0xF8: "imageColorAmb" ImageElement
    UI::ImageElement*  imageColorFog = nullptr;       // +0x100: "imageColorFog" ImageElement
    UI::ComboElement*  comboWeather = nullptr;        // +0x108: "comboWeather" ComboElement
    UI::ButtonElement* buttonSave = nullptr;          // +0x110: "buttonSave" ButtonElement
    UI::ButtonElement* buttonMenuScene = nullptr;     // +0x118: "buttonMenuScene" ButtonElement
    float smoothedFPS = 0;         // +0x120: EMA FPS accumulator
    float smoothedFrameMS = 0;     // +0x124: EMA frame time accumulator
    float smoothedTotalMS = 0;     // +0x128: EMA total time accumulator
    uint8_t _pad12C[4] = {};        // +0x12C: gap to reach 0x130
};

static_assert(sizeof(DebugUI) == 0x130, "DebugUI is 0x130 (create 0x140010ec0 MemAlloc(0x130))");
static_assert(alignof(DebugUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(DebugUI, diseaseCombo) == 0x30, "diseaseCombo @ 0x30");
static_assert(offsetof(DebugUI, numberDrawCalls) == 0x40, "numberDrawCalls @ 0x40");
static_assert(offsetof(DebugUI, numberFPS) == 0x60, "numberFPS @ 0x60");
static_assert(offsetof(DebugUI, sliderDay) == 0x78, "sliderDay @ 0x78");
static_assert(offsetof(DebugUI, valueSunH) == 0xE0, "valueSunH @ 0xE0");
static_assert(offsetof(DebugUI, imageColorSun) == 0xF0, "imageColorSun @ 0xF0");
static_assert(offsetof(DebugUI, comboWeather) == 0x108, "comboWeather @ 0x108");
static_assert(offsetof(DebugUI, smoothedFPS) == 0x120, "smoothedFPS @ 0x120");
static_assert(offsetof(DebugUI, drawCombo) == 0x38, "drawCombo @ 0x38");
static_assert(offsetof(DebugUI, numberTriangles) == 0x48, "numberTriangles @ 0x48");
static_assert(offsetof(DebugUI, numberVertices) == 0x50, "numberVertices @ 0x50");
static_assert(offsetof(DebugUI, numberBatches) == 0x58, "numberBatches @ 0x58");
static_assert(offsetof(DebugUI, numberFrameMS) == 0x68, "numberFrameMS @ 0x68");
static_assert(offsetof(DebugUI, numberTotalMS) == 0x70, "numberTotalMS @ 0x70");
static_assert(offsetof(DebugUI, valueSunR) == 0x80, "valueSunR @ 0x80");
static_assert(offsetof(DebugUI, valueSunG) == 0x88, "valueSunG @ 0x88");
static_assert(offsetof(DebugUI, valueSunB) == 0x90, "valueSunB @ 0x90");
static_assert(offsetof(DebugUI, valueAmbR) == 0x98, "valueAmbR @ 0x98");
static_assert(offsetof(DebugUI, valueAmbG) == 0xA0, "valueAmbG @ 0xA0");
static_assert(offsetof(DebugUI, valueAmbB) == 0xA8, "valueAmbB @ 0xA8");
static_assert(offsetof(DebugUI, valueFogR) == 0xB0, "valueFogR @ 0xB0");
static_assert(offsetof(DebugUI, valueFogG) == 0xB8, "valueFogG @ 0xB8");
static_assert(offsetof(DebugUI, valueFogB) == 0xC0, "valueFogB @ 0xC0");
static_assert(offsetof(DebugUI, valueFogN) == 0xC8, "valueFogN @ 0xC8");
static_assert(offsetof(DebugUI, valueFogF) == 0xD0, "valueFogF @ 0xD0");
static_assert(offsetof(DebugUI, valueFogM) == 0xD8, "valueFogM @ 0xD8");
static_assert(offsetof(DebugUI, valueSunP) == 0xE8, "valueSunP @ 0xE8");
static_assert(offsetof(DebugUI, imageColorAmb) == 0xF8, "imageColorAmb @ 0xF8");
static_assert(offsetof(DebugUI, imageColorFog) == 0x100, "imageColorFog @ 0x100");
static_assert(offsetof(DebugUI, buttonSave) == 0x110, "buttonSave @ 0x110");
static_assert(offsetof(DebugUI, buttonMenuScene) == 0x118, "buttonMenuScene @ 0x118");
static_assert(offsetof(DebugUI, smoothedFrameMS) == 0x124, "smoothedFrameMS @ 0x124");
static_assert(offsetof(DebugUI, smoothedTotalMS) == 0x128, "smoothedTotalMS @ 0x128");
static_assert(offsetof(DebugUI, _pad12C) == 0x12C, "_pad12C @ 0x12C");
