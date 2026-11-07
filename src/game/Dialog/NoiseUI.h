#pragma once
// ============================================================================
// src/game/Dialog/NoiseUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "NoiseUI", parent
//   "UI::DialogController", table 0x14015ca80, 11 slots, sizeof 0x70.
//   create 0x140012d30: MemAlloc(0x70, 8), ??0DialogController, stamp, fields.
//   [0] 0x140012da0 scalar deleting dtor (~NoiseUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x140071bb0  RET 0
//   [4] 0x1400714c0  OnCreated
//   [9] 0x1400714a0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace Video    { class ImageBuffer; }
namespace Graphics { class MaterialInstance; }
namespace UI       { class EditElement; class SpriteSheet; }

class NoiseUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(NoiseUI)

public:
    NoiseUI() = default;                                 // 0x140012d30 (ctor inlined in the factory)

    // 0x140012da0 minus the free: the body 0x140071bc0 deletes the three owned
    // objects at +0x40, +0x38, +0x30 through their slot 0 (flags 1), then the
    // compiler chains ~DialogController (and so ~Internal: RemoveObject).
    ~NoiseUI() override;                                          // [0] 0x140012da0
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x140071bb0  RET 0 in retail
    void OnCreated() override;   // [4] 0x1400714c0 (noiseui.cpp)
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x1400714a0 (noiseui.cpp)

    // SetupImage 0x140071890: the preview's MaterialInstance and SpriteSheet
    // (UIMaterialNoBlend + `image` as "diffuse"), set on "noiseImage".
    void SetupImage(Video::ImageBuffer* image, UI::SpriteSheet** sheetOut,
                    Graphics::MaterialInstance** materialOut);
    // GenerateNoise 0x1400711d0: the five edits' values drive a 256x256 Perlin image.
    void GenerateNoise();

    // ---- fields ---------------------------------------------------------
    // The factory leaves +0x30 unwritten; the tree zeroes it (kept).
    Video::ImageBuffer*         imageBuffer = nullptr;       // +0x30 the 256x256 preview
    Graphics::MaterialInstance* material = nullptr;          // +0x38
    UI::SpriteSheet*            spriteSheet = nullptr;       // +0x40
    UI::EditElement*            noisePersistance = nullptr;  // +0x48
    UI::EditElement*            noiseOctaves = nullptr;      // +0x50
    UI::EditElement*            noiseScale = nullptr;        // +0x58
    UI::EditElement*            noiseCutOff = nullptr;       // +0x60
    UI::EditElement*            noiseGradient = nullptr;     // +0x68
};

static_assert(offsetof(NoiseUI, imageBuffer) == 0x30, "NoiseUI +0x30");
static_assert(offsetof(NoiseUI, material) == 0x38, "NoiseUI +0x38");
static_assert(offsetof(NoiseUI, spriteSheet) == 0x40, "NoiseUI +0x40");
static_assert(offsetof(NoiseUI, noisePersistance) == 0x48, "NoiseUI +0x48");
static_assert(offsetof(NoiseUI, noiseOctaves) == 0x50, "NoiseUI +0x50");
static_assert(offsetof(NoiseUI, noiseScale) == 0x58, "NoiseUI +0x58");
static_assert(offsetof(NoiseUI, noiseCutOff) == 0x60, "NoiseUI +0x60");
static_assert(offsetof(NoiseUI, noiseGradient) == 0x68, "NoiseUI +0x68");

static_assert(sizeof(NoiseUI) == 0x70, "NoiseUI is 0x70 (create 0x140012d30 MemAlloc(0x70))");
static_assert(alignof(NoiseUI) == 8, "qword-aligned (the vptr)");
