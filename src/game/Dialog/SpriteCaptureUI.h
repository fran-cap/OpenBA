#pragma once
// ============================================================================
// src/game/Dialog/SpriteCaptureUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "SpriteCaptureUI", parent
//   "UI::DialogController", table 0x14015d4f0, 11 slots, sizeof 0x70.
//   create 0x140013d10: MemAlloc(0x70, 8), ??0DialogController, stamp, fields.
//   [0] 0x140013fc0 scalar deleting dtor (~SpriteCaptureUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x140079850  RET 0
//   [4] 0x140079640  OnCreated
//   [8] 0x1400793a0  OnUpdate
//   [9] 0x140079620  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace UI { class EditElement; class ButtonElement; }
namespace Object { class External; }
class CaptureBoxElement;

class SpriteCaptureUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(SpriteCaptureUI)

public:
    SpriteCaptureUI() = default;                         // 0x140013d10 (ctor inlined in the factory)

    // 0x140013fc0 minus the free. The compiler chains ~DialogController (and so
    // ~Internal: RemoveObject), which the tree's hand-built slot 0 skipped.
    ~SpriteCaptureUI() override = default;                        // [0] 0x140013fc0
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x140079850  RET 0 in retail
    void OnCreated() override;   // [4] 0x140079640 (spritecaptureui.cpp)
    void OnUpdate(const System::Time& time) override;   // [8] 0x1400793a0 (spritecaptureui.cpp)
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x140079620 (spritecaptureui.cpp)
    void StartCapture();                                  // 0x1400792b0 (the button's arm of [9])

    // ---- fields (OnCreated 0x140079640 finds and initialises them) -------
    UI::EditElement*   editCapture = nullptr;   // +0x30 "editCapture" (IsDerivedFrom EditElement)
    UI::ButtonElement* buttonCapture = nullptr; // +0x38 "buttonCapture" (IsDerivedFrom ButtonElement), id 1000
    CaptureBoxElement* captureBox = nullptr;    // +0x40 "captureBox" (IsDerivedFrom CaptureBoxElement)
    int32_t curX = 0;             // +0x48: box corner X (the press position)
    int32_t curY = 0;             // +0x4C: box corner Y
    int32_t anchorX = 0;          // +0x50: opposite corner X (init 100; follows the mouse)
    int32_t anchorY = 0;          // +0x54: opposite corner Y (init 100)
    uint8_t dragging = 0;         // +0x58: 1 = left mouse button held, dragging
    uint8_t _pad59[7] = {};        // +0x59: pad to +0x60
    Object::External* pendingExternal = nullptr; // +0x60: the External OnUpdate (0x1400793a0) saves
    int32_t saveFrameCount = 0;   // +0x68: incremented each frame; triggers save at ==1
    uint8_t _pad6C[4] = {};        // +0x6C: pad to 0x70
};

static_assert(sizeof(SpriteCaptureUI) == 0x70, "SpriteCaptureUI is 0x70 (create 0x140013d10 MemAlloc(0x70))");
static_assert(alignof(SpriteCaptureUI) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(SpriteCaptureUI, editCapture) == 0x30, "editCapture @ 0x30");
static_assert(offsetof(SpriteCaptureUI, buttonCapture) == 0x38, "buttonCapture @ 0x38");
static_assert(offsetof(SpriteCaptureUI, captureBox) == 0x40, "captureBox @ 0x40");
static_assert(offsetof(SpriteCaptureUI, curX) == 0x48, "curX @ 0x48");
static_assert(offsetof(SpriteCaptureUI, curY) == 0x4C, "curY @ 0x4C");
static_assert(offsetof(SpriteCaptureUI, anchorY) == 0x54, "anchorY @ 0x54");
static_assert(offsetof(SpriteCaptureUI, anchorX) == 0x50, "anchorX @ 0x50");
static_assert(offsetof(SpriteCaptureUI, dragging) == 0x58, "dragging @ 0x58");
static_assert(offsetof(SpriteCaptureUI, pendingExternal) == 0x60, "pendingExternal @ 0x60");
static_assert(offsetof(SpriteCaptureUI, saveFrameCount) == 0x68, "saveFrameCount @ 0x68");
