#pragma once
// ============================================================================
// src/game/Dialog/BuildRequirementUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "BuildRequirementUI", parent
//   "UI::DialogController", table 0x14015b1d0, 11 slots, sizeof 0x60.
//   create 0x1400107f0: MemAlloc(0x60, 8), ??0DialogController, stamp, fields.
//   [0] 0x140010860 scalar deleting dtor (~BuildRequirementUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x14005ee80  RET 0
//   [4] 0x14005e7c0  OnCreated
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "collection/Collection.h"   // the +0x40 / +0x50 arrays
#include "Object/exe_type.h"

class ComponentDescription;
namespace UI { class Element; }

class BuildRequirementUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(BuildRequirementUI)

public:
    BuildRequirementUI() = default;                      // 0x1400107f0 (ctor inlined in the factory)

    // 0x140010860 minus the free: MemFree +0x50 and zero it, MemFree +0x40 and zero
    // it, then ~DialogController (and so ~Internal: RemoveObject). The two frees are
    // the typed `counts` / `labels` members' ~Array, in reverse declaration order.
    ~BuildRequirementUI() override = default;                     // [0] 0x140010860
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x14005ee80  RET 0 in retail
    void OnCreated() override;   // [4] 0x14005e7c0
    // The CreateBar setters the tools' UpdateCreateBar (0x1400ffc60) runs (B4 PLAYER;
    // bodies in Player/tool_base_createbar.cpp).
    void SetSizeText(const System::String* text);    // 0x14005e600 "textSize"
    void SetStatusText(const System::String* cost);  // 0x14005e420 "textStatus"
    void SetSizeMultiplier(uint32_t multiplier);     // 0x14005e700 the labelBuild%i quantities
    void SetIconColor(uint32_t color);               // 0x14005e360 "imageIcon"

    // ---- fields (B4 DLG-a) ----------------------------------------------
    // +0x30 the building's ComponentDescription (resolved from the data source)
    ComponentDescription* description = nullptr;
    uint32_t sizeMultiplier = 1;           // +0x38: initialized to 1 by the factory
    uint8_t _pad3C[4] = {};            // +0x3C: gap
    // +0x40 the "labelBuild%i" labels; +0x50 each cost's quantity. Create
    // 0x1400107f0 zeroes the four qwords +0x40..+0x58 (0x14001083b..0x140010847);
    // the dtor 0x140010860 MemFrees +0x50 and zeroes its qwords (0x140010890 ..
    // 0x14001089c), then +0x40 (0x1400108a4 .. 0x1400108ae), then ~DialogController
    // (0x1400108b5): the two members' own dtors, in reverse declaration order.
    Collection::Array<UI::Element*> labels;   // +0x40
    Collection::Array<int32_t>      counts;   // +0x50
};

static_assert(sizeof(BuildRequirementUI) == 0x60, "BuildRequirementUI is 0x60 (create 0x1400107f0 MemAlloc(0x60))");
static_assert(alignof(BuildRequirementUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(BuildRequirementUI, description) == 0x30, "BuildRequirementUI::description @ +0x30");
static_assert(offsetof(BuildRequirementUI, sizeMultiplier) == 0x38, "BuildRequirementUI::sizeMultiplier @ +0x38");
static_assert(offsetof(BuildRequirementUI, labels) == 0x40, "BuildRequirementUI::labels @ +0x40");
static_assert(offsetof(BuildRequirementUI, counts) == 0x50, "BuildRequirementUI::counts @ +0x50");
