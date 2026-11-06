// src/game/Dialog/mapui.cpp
// Clean reimplementation of MapUI methods.
//
// MapUI (0x50 bytes): Minimap display panel.
// Extends DialogController (0x30 base).
//
// Layout: Dialog/MapUI.h (the map and overlay images' materials and sheets).

#include "Dialog/ui_panels.h"
// ENDGAME B.3.5 batch K1: the runtime type headers come FIRST — they reach
// src/engine/volume/Volume.h, whose global-scope `struct Rectangle` is hidden by
// <wingdi.h>'s `Rectangle()` once <windows.h> has been seen through resolve.h.
#include "video/ImageBuffer.h"
#include "graphics/Material.h"
#include "options.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerController.h"
#include "Game/Map.h"
#include "Game/Minimap.h"   // GameUpdateLoop::m_imageCapture
#include "Object/factory_registration.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ImageElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Component/exe_bodies.h"   // B3R: FrustumOutline_ProjectCorners
#include "Game/exe_constants.h"   // B3R: kRdata_*


// ============================================================
// MapUI::SetupImage (0x14006cb00): Create MaterialInstance + SpriteSheet
// from an ImageBuffer, find the named image element, set sprite.
// ============================================================
// Allocates MaterialInstance (0x60) and SpriteSheet (0x58),
// loads material ("UIMaterialNoBlend.rsc" or "UIMaterial.rsc"),
// adds the ImageBuffer as "diffuse" texture, builds parameters,
// configures sprite info, finds named element, sets sprite.
// If useBlend is set, also sets flags and element ID 1000.
void MapUI::SetupImage(Video::ImageBuffer* imageBuffer, UI::SpriteSheet** spriteSheetOut,
                       Graphics::MaterialInstance** materialOut, char useBlend,
                       const char* elementName) {
    bool trc = Opt().mapTrace != nullptr;
    #define MAPTRC(msg) do { if (trc) { printf("[SetupImg] " msg " (elem=%s)\n", elementName); fflush(stdout);} } while(0)
    MAPTRC("enter");

    // Defensive (standalone bring-up): the minimap ImageBuffer must exist — the
    // very first thing below is fn_ImageBuffer_Width(imageBuffer). If the GPU
    // buffer was never created (Minimap_CreateAndUpload not yet run) skip rather
    // than fault. Also require the allocator (B3R: the fn_MemAlloc readiness test was
    // always true and is gone; the g_pMemAllocSingleton snapshot is read live).
    if (!imageBuffer || !System::MemoryAllocator::_singleton) {
        if (trc) printf("[SetupImg] GUARD-RETURN elem=%s imgBuf=%p MemAlloc=%p Alloc*=%p\n",
               elementName, imageBuffer, (void*)&System::MemoryAllocator::MemAlloc,
               System::MemoryAllocator::_singleton);
        *materialOut = nullptr;
        *spriteSheetOut = nullptr;
        return;
    }

    // --- Allocate and construct MaterialInstance (0x60 bytes) ---
    void* matInst = System::MemoryAllocator::MemAlloc(0x60, 8);
    Graphics::MaterialInstance* materialPtr = nullptr;
    if (matInst) {
        materialPtr = ::new (matInst) Graphics::MaterialInstance();   // B.3.9 EXA: the real class (exe table 0x14015c400 is its own)
    }
    *materialOut = materialPtr;

    // --- Load material resource ---
    System::Filename filenameObj(useBlend == '\0' ? "Material/UI/UIMaterialNoBlend.rsc"
                                                   : "Material/UI/UIMaterial.rsc");

    MAPTRC("material alloc+ctor done");
    ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameObj);
    Graphics::Material* material = nullptr;
    if (ext) {
        if (::Object::IsDerivedFromNullSafe(ext, Graphics::Material::TypeId())) {
            material = static_cast<Graphics::Material*>(ext);
        }
    }
    (*materialOut)->SetMaterial(Object::ExternalPtr<Graphics::Material>(material));

    // --- Add ImageBuffer as "diffuse" texture ---
    System::String diffuseStr("diffuse");
    MAPTRC("SetMaterial done, add diffuse texture");
    (*materialOut)->AddTexture(
        diffuseStr,
        Object::ExternalPtr<Video::ImageBuffer>(imageBuffer));

    MAPTRC("BuildParameters");
    (*materialOut)->BuildParameters();
    MAPTRC("BuildParameters done, alloc spritesheet");

    // --- Allocate and construct SpriteSheet (0x58 bytes) ---
    void* sheet = System::MemoryAllocator::MemAlloc(0x58, 8);
    UI::SpriteSheet* spriteSheet = nullptr;
    if (sheet) {
        spriteSheet = ::new (sheet) UI::SpriteSheet();   // B.3.9 EXA: the real class (exe table 0x14015c3b0 is its own)
    }
    *spriteSheetOut = spriteSheet;

    // The sheet's material instance (+0x30).
    spriteSheet->m_field30 = *materialOut;

    // Get image dimensions
    int imgW = imageBuffer->Width();
    int imgH = imageBuffer->Height();

    // Set sprite sheet name/group strings (empty) and dimensions
    UI::SpriteSheet* ss = *spriteSheetOut;
    System::String emptyStr1, emptyStr2;
    // LEFT: SpriteSheet holds them as char* (H5, R-UI's header).
    ss->m_groupName = emptyStr1;
    ss->m_textureName = emptyStr2;
    ss->m_field48 = imgW;
    ss->m_field4C = imgH;
    ss->m_field50 = false;

    // Add sprite info: empty name, empty group, full image rect
    imgW = imageBuffer->Width();
    imgH = imageBuffer->Height();
    System::String nameEmpty, groupEmpty;
    MAPTRC("AddSpriteInfo");
    (*spriteSheetOut)->AddSpriteInfo(nameEmpty, groupEmpty, 0, 0, imgW, imgH);
    MAPTRC("AddSpriteInfo done");

    // Set sprite info with UV scale
    imgW = imageBuffer->Width();
    imgH = imageBuffer->Height();
    float uvScale = kRdata_14017214c;   // 0x14017214c = 1.0f (was g_fUVScale; its fallback never ran)
    (*spriteSheetOut)->SetSpriteInfo(0, imgW, imgH, 0.0f, 0.0f, uvScale, uvScale);

    // --- Find named image element and set sprite ---
    System::String elemNameStr(elementName);
    UI::Element* rawElem = _container->FindElement(elemNameStr).Get();

    // Cast to ImageElement via IsDerivedFrom
    UI::ImageElement* imageElem = nullptr;
    if (rawElem && ::Object::IsDerivedFromNullSafe(rawElem, UI::ImageElement::TypeId())) {
        imageElem = static_cast<UI::ImageElement*>(rawElem);
    }
    if (Opt().mapTrace)
        printf("[SetupImage] elem '%s': raw=%p imageElem=%p mat=%p sheet=%p\n",
               elementName, rawElem, imageElem, *materialOut, *spriteSheetOut);

    // Create Sprite from SpriteSheet (index 0) and set on element
    UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(*spriteSheetOut), 0);
    imageElem->SetSprite(&sprite);

    // If blend mode, set flags and element ID
    if (useBlend != '\0') {
        imageElem->SetFlagsOnOff(0x10000, 0);
        imageElem->SetElementId(1000);
    }
}

// ============================================================
// HandleClick (0x14006c850): Move camera to map click position
// ============================================================
// Translates minimap click coordinates into camera movement.
// Gets current camera frustum, projects corners to ground plane,
// and computes new camera position based on click offset.
//
// Original: FUN_14006c850 (RCX is the clicked element, unused)
//
// NOTE: This function relies heavily on camera/frustum internals
// (Graphics::Camera::GetFrustum, Volume::Frustum::GetCorners,
// Math::Transform3::Rotate, Math::Vector3 operations) which are
// complex to reimplement. The core logic is:
//   1. Validate click is within bounds
//   2. Get camera frustum corners, project far corners to ground
//   3. Average the projected positions (camera center)
//   4. Compute new position offset: (click - center) * worldScale
//   5. Write to camera target position
//
// For now, we implement the final coordinate mapping directly,
// which is the essential game logic. The frustum projection
// (FUN_14006c5b0) is called as a resolved function pointer since
// it depends on deep math/camera internals.

// FUN_14006c5b0 is FrustumOutline_ProjectCorners, called by name (B3R).

static void MapUI_HandleClick(Object::InternalPtr<UI::Element>* /*element*/, int clickX, int clickY,
                              int mapWidth, int mapHeight) {
    if (clickX < 0 || clickX >= mapWidth || clickY < 0 || clickY >= mapHeight)
        return;


    // FUN_14006c5b0 fills an array of 4 projected ground-plane corner positions
    // (Vector3, 12 bytes each) plus 4 frustum corners = 8 Vector3s total
    float fullBuf[8 * 3];
    for (int i = 0; i < 8 * 3; i++) fullBuf[i] = 0.0f;

    // Call the ground projection function — fills corner positions from camera frustum
    FrustumOutline_ProjectCorners(fullBuf);   // 0x14006c5b0

    // The original also averages the 4 projected corners (0.25 * Σcorners) but
    // DISCARDS the result — dead code confirmed in the 0x14006c850 disassembly —
    // so the camera move depends only on the click position and map grid size.

    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;

    // The camera controller
    PlayerController* camCtrl = gameWorld->m_playerController;

    // The map's edge in cells.
    // (GameUpdateLoop::m_game is the Map.)
    int mapGridSize = gameWorld->m_game->size;

    // Compute world-space scale factor: mapGridSize / mapWidth
    float worldScale = (float)mapGridSize / (float)mapWidth;

    // Set the camera focus (the same field WASD panning drives).
    // Maps click position: X = (clickX - halfWidth) * scale, Y = (halfHeight - clickY) * scale
    camCtrl->target.x = (float)(clickX - (mapWidth >> 1)) * worldScale;
    camCtrl->target.y = (float)((mapHeight >> 1) - clickY) * worldScale;
    camCtrl->target.z = 0.0f;
}

// ---- MapUI: PROMOTED to the real reimpl (map-render bring-up) ----------------
// The minimap dialog's controller. Slot4 (+0x20, SetupImage) builds a
// MaterialInstance + SpriteSheet from the minimap subsystem's terrain/overlay
// ImageBuffers (gameWorld+0x148 +0x08/+0x10) and binds them to the "imageMap"/
// "imageOverlay" elements. Slot9 (+0x48) handles minimap clicks → move camera.
// The generic stub left both no-ops so the map dialog opened blank. The GPU
// buffers are created by Minimap_CreateAndUpload (src/game/Game/minimap_upload.cpp);
// SetupImage is standalone-guarded (skips if the buffer is null / vtables absent).

// ============================================================
// Slot4 (vtable +0x20): Setup map and overlay images
// ============================================================
// Original: 0x14006CA80
//
// The image capture holds two ImageBuffers: terrain map and overlay.
// For each, SetupImage creates MaterialInstance + SpriteSheet,
// finds the image element, and sets the sprite.

// [4] 0x14006ca80
void MapUI::OnCreated() {
    static bool s_logged = false;
    if (!s_logged && Opt().mapTrace) {
        s_logged = true;
        GameUpdateLoop* gw = GameUpdateLoop::_singleton;
        // GameUpdateLoop +0x148 is the Minimap.
        [[maybe_unused]] Minimap* mapObj =
            gw ? gw->m_imageCapture : nullptr;
        printf("[MapUI::Slot4] dispatched self=%p mat_tid=%u img_tid=%u "
               "terrBuf=%p ovrBuf=%p\n", this,
               Graphics::Material::TypeId(), UI::ImageElement::TypeId(),
               mapObj ? mapObj->terrainImage : nullptr,
               mapObj ? mapObj->overlayImage : nullptr);
        fflush(stdout);
    }
    // The image capture (GameUpdateLoop +0x148) holds the terrain map (+0x08) and
    // overlay (+0x10) ImageBuffers.
    // (GameUpdateLoop::m_imageCapture is the Minimap.)
    Minimap* imageCapture =
        GameUpdateLoop::_singleton->m_imageCapture;

    // Setup terrain map image (no blend, element "imageMap")
    SetupImage(imageCapture->terrainImage, &mapSpriteSheet, &mapMaterial,
               '\0', "imageMap");

    // Setup overlay image (with blend, element "imageOverlay")
    SetupImage(imageCapture->overlayImage, &overlaySpriteSheet, &overlayMaterial,
               '\x01', "imageOverlay");
}

// ============================================================
// Slot9 (vtable +0x48): Handle map click — move camera
// ============================================================
// Original: 0x14006C9E0
//
// When msg==2 (mouse click) and id==1000 (overlay element),
// reads the element's position/size from the clicked element,
// calculates relative click position, and moves the camera.

// [9] 0x14006c9e0
void MapUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id,
                           int32_t clickX, int32_t clickY, int32_t /*e*/, void* /*userData*/) {
    if (msg != 2 || id != 1000) return;

    // The clicked element's layout position and size
    UI::Element* elem = element->Get();
    int elemX = elem->_layoutLeft;
    int elemY = elem->_layoutTop;
    int elemW = elem->_layoutWidth;
    int elemH = elem->_layoutHeight;

    // Translate to map-relative coordinates and move camera
    MapUI_HandleClick(element, clickX - elemX, clickY - elemY, elemW, elemH);
}

// [0] 0x14006ce90 (the body of the scalar-deleting dtor 0x1400124d0). Retail stamps
// 0x14015c350, then deletes +0x38 (0x14006ceb0), +0x30 (0x14006cec3), +0x48
// (0x14006ced6) and +0x40 (0x14006cee9), each `TEST RCX,RCX; JZ` then `CALL [vptr]`
// with EDX = 1: C++ `delete` through the virtual dtor, in that order (each sheet
// before the material it draws with), then tail-jumps to ~DialogController
// ([0x1401515c8]). NoiseUI's dtor spells its three the same way.
MapUI::~MapUI() {
    delete mapSpriteSheet;       // +0x38
    delete mapMaterial;          // +0x30
    delete overlaySpriteSheet;   // +0x48
    delete overlayMaterial;      // +0x40
}
