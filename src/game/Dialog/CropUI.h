#pragma once
// ============================================================================
// src/game/Dialog/CropUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (B38_SLOTS.md §6): reflected "CropUI", parent "SelectResourceUI",
//   table 0x14015f270, 14 slots, sizeof 0x60. create 0x140020b10 ->
//   ctor 0x1400b2c90: ??0DialogController, stamp 0x14015d330, InternalBasePtr
//   (+0x30, null), stamp 0x14015f270, +0x38..+0x58 = 0.
//   [0] 0x140020b60 scalar deleting dtor
//   [4] 0x1400b2b50  OnCreated (the base's 0x140078be0, then "yield" at +0x38)
//   [7] 0x1400b2a50  OnUpdateLayout (maturity bar, then the base's 0x140078cb0)
//   [8] [9] = parent (SelectResourceUI's 0x140078d30 / 0x140078d80)
//   [11] 0x1400b2920 SetResource  [12] 0x1400b29d0 GetResource
//   [13] 0x1400b29c0 GetCategory -> 0
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Dialog/SelectResourceUI.h"
#include "collection/Collection.h"   // the +0x40 / +0x50 arrays
#include "Object/exe_type.h"

namespace UI { class ProgressElement; }

class CropUI final : public SelectResourceUI {
    DECLARE_EXE_TYPE(CropUI)

public:
    CropUI() = default;                                           // 0x1400b2c90

    // 0x140020b60 minus the free: the body 0x1400b2c10 restamps 0x14015f270,
    // MemFrees +0x50 and zeroes its two qwords, then the same for +0x40, restamps
    // SelectResourceUI's 0x14015d330 and tail-jumps ~DialogController: the two
    // arrays' own dtors in reverse declaration order, then the bases'.
    ~CropUI() override = default;                                 // [0]  0x140020b60
    void  OnCreated() override;                                   // [4]  0x1400b2b50
    void  OnUpdateLayout() override;                              // [7]  0x1400b2a50
    void  SetResource(ComponentDescription* type) override;       // [11] 0x1400b2920
    ComponentDescription** GetResource(ComponentDescription** out) override;  // [12] 0x1400b29d0
    int   GetCategory() override;                                 // [13] 0x1400b29c0  XOR EAX,EAX

    // ---- fields (the Panels.h POD's names) -----------------------------------
    UI::ProgressElement* yield = nullptr;  // +0x38: "yield" ProgressElement (0x1400b2b50)
    // +0x40 / +0x50: two Collection::Array headers. The ctor 0x1400b2c90 zeroes
    // them, the dtor frees each buffer with no element destroy (so the element is
    // trivially destructible), and none of CropUI's bodies (OnCreated 0x1400b2b50,
    // OnUpdateLayout 0x1400b2a50, SetResource 0x1400b2920, GetResource 0x1400b29d0)
    // reads them: the element type is not recoverable, so they are byte arrays.
    Collection::Array<uint8_t> array40;   // +0x40
    Collection::Array<uint8_t> array50;   // +0x50
};

static_assert(sizeof(CropUI) == 0x60, "CropUI is 0x60 (create 0x140020b10 MemAlloc(0x60))");
static_assert(alignof(CropUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(CropUI, yield) == 0x38, "yield @ +0x38 (0x1400b2b50)");
static_assert(offsetof(CropUI, array40) == 0x40, "+0x40");
static_assert(offsetof(CropUI, array50) == 0x50, "+0x50");
