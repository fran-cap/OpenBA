#pragma once
// ============================================================================
// src/game/Dialog/MapUI.h — ENDGAME B.3.8 block PNL (B38_RULES D1-D5)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6): reflected "MapUI", parent
//   "UI::DialogController", table 0x14015c350, 11 slots, sizeof 0x50.
//   create 0x140012240: MemAlloc(0x50, 8), ??0DialogController, stamp, fields.
//   [0] 0x1400124d0 scalar deleting dtor (~MapUI + MemFree on &1)
//   [1] TypeName  [2] Type  (DECLARE_EXE_TYPE)
//   [3] 0x14006ce80  RET 0
//   [4] 0x14006ca80  OnCreated
//   [9] 0x14006c9e0  OnElementEvent
//   every other slot is UI::DialogController's (inherited, not declared).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

namespace Video    { class ImageBuffer; }
namespace Graphics { class MaterialInstance; }
namespace UI       { class SpriteSheet; }

class MapUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(MapUI)

public:
    MapUI() = default;                                   // 0x140012240 (ctor inlined in the factory)

    // 0x1400124d0 minus the free: the body 0x14006ce90 deletes the four owned
    // objects at +0x38, +0x30, +0x48, +0x40 through their slot 0 (flags 1), then the
    // compiler chains ~DialogController (and so ~Internal: RemoveObject).
    ~MapUI() override;                                            // [0] 0x1400124d0
    void Persist(IO::Stream&/*stream*/) override {}   // [3] 0x14006ce80  RET 0 in retail
    void OnCreated() override;   // [4] 0x14006ca80
    void OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId a, int32_t b,
                        int32_t c, int32_t d, int32_t e, void* userData) override;   // [9] 0x14006c9e0

    // SetupImage 0x14006cb00: a MaterialInstance and SpriteSheet over `image`
    // (UIMaterial when useBlend, else UIMaterialNoBlend), set on `elementName`.
    void SetupImage(Video::ImageBuffer* image, UI::SpriteSheet** sheetOut,
                    Graphics::MaterialInstance** materialOut, char useBlend,
                    const char* elementName);

    // ---- fields (OnCreated 0x14006ca80 fills them) ----------------------
    Graphics::MaterialInstance* mapMaterial = nullptr;         // +0x30 "imageMap"
    UI::SpriteSheet*            mapSpriteSheet = nullptr;      // +0x38
    Graphics::MaterialInstance* overlayMaterial = nullptr;     // +0x40 "imageOverlay"
    UI::SpriteSheet*            overlaySpriteSheet = nullptr;  // +0x48
};

static_assert(offsetof(MapUI, mapMaterial) == 0x30, "MapUI +0x30");
static_assert(offsetof(MapUI, mapSpriteSheet) == 0x38, "MapUI +0x38");
static_assert(offsetof(MapUI, overlayMaterial) == 0x40, "MapUI +0x40");
static_assert(offsetof(MapUI, overlaySpriteSheet) == 0x48, "MapUI +0x48");

static_assert(sizeof(MapUI) == 0x50, "MapUI is 0x50 (create 0x140012240 MemAlloc(0x50))");
static_assert(alignof(MapUI) == 8, "qword-aligned (the vptr)");
