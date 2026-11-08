// src/game/Dialog/noiseui.cpp
// Clean reimplementation of NoiseUI panel methods.
//
// NoiseUI (0x70 bytes): Noise/terrain visualization debug panel.
// Extends DialogController (0x30 base).
//
// Layout: Dialog/NoiseUI.h (the preview image, its material and sheet, the
// five noise edits).
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
// ENDGAME B.3.5 batch K1: the runtime type headers come FIRST — they reach
// src/engine/volume/Volume.h, whose global-scope `struct Rectangle` is hidden by
// <wingdi.h>'s `Rectangle()` once <windows.h> has been seen through resolve.h.
#include "video/ImageBuffer.h"
#include "graphics/Material.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers: System::Random
#include "ui/EditElement.h"
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3); ~NoiseUI deletes by name
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ImageElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "utility/PerlinNoise.h"   // B3R: RtUtility_PerlinNoise_Ctor / _Generate
#include "Game/exe_constants.h"   // B3R: kRdata_*
#include "system/MemoryAllocator.h"   // B3R: the allocator doors


// Helper: the named element under the panel's root, if it is an EditElement.
// B6 row 9: retail (0x1400714c0, five times) CheckPtrs the FindElement result,
// calls IsDerivedFrom(EditElement's type id) on it with no null test, and
// stores the element when it answers true, else null (the CMOV over 0).
static UI::EditElement* NoiseUI_FindEdit(UI::Element* root, const char* name) {
    System::String nameStr(name);
    UI::Element* elem = root->FindElement(nameStr).Get();
    return elem->IsDerivedFrom(UI::EditElement::TypeId())
               ? static_cast<UI::EditElement*>(elem)
               : nullptr;
}

// ============================================================
// NoiseUI::SetupImage (0x140071890): Create MaterialInstance + SpriteSheet
// for the noise preview, hardcoded to "UIMaterialNoBlend.rsc"
// and "noiseImage" element name.
// ============================================================
// Nearly identical to MapUI's SetupImage but without blend option.
// Allocates MaterialInstance (0x60) and SpriteSheet (0x58),
// loads UIMaterialNoBlend material, adds ImageBuffer as "diffuse",
// builds parameters, configures sprite info, finds "noiseImage"
// element and sets the sprite.
void NoiseUI::SetupImage(Video::ImageBuffer* image, UI::SpriteSheet** sheetOut,
                         Graphics::MaterialInstance** materialOut) {
    // --- Allocate and construct MaterialInstance (0x60 bytes) ---
    void* matInst = System::MemoryAllocator::MemAlloc(0x60, 8);
    Graphics::MaterialInstance* materialPtr = nullptr;
    if (matInst) {
        materialPtr = ::new (matInst) Graphics::MaterialInstance();   // B.3.9 EXA: the real class (exe table 0x14015c400 is its own)
    }
    *materialOut = materialPtr;

    // --- Load UIMaterialNoBlend resource ---
    // Filename's ctor already normalises delimiters, so the separate
    // AdjustDelimiters call retail made on the raw buffer is redundant here.
    System::Filename filename("Material/UI/UIMaterialNoBlend.rsc");

    ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filename);
    Graphics::Material* material = nullptr;
    if (ext) {
        if (::Object::IsDerivedFromNullSafe(ext, Graphics::Material::TypeId())) {
            material = static_cast<Graphics::Material*>(ext);
        }
    }
    (*materialOut)->SetMaterial(Object::ExternalPtr<Graphics::Material>(material));

    // --- Add ImageBuffer as "diffuse" texture ---
    System::String diffuseStr("diffuse");
    (*materialOut)->AddTexture(
        diffuseStr,
        Object::ExternalPtr<Video::ImageBuffer>(image));

    (*materialOut)->BuildParameters();

    // --- Allocate and construct SpriteSheet (0x58 bytes) ---
    void* sheet = System::MemoryAllocator::MemAlloc(0x58, 8);
    UI::SpriteSheet* newSheet = nullptr;
    if (sheet) {
        newSheet = ::new (sheet) UI::SpriteSheet();   // B.3.9 EXA: the real class (exe table 0x14015c3b0 is its own)
    }
    *sheetOut = newSheet;

    // The sheet's material instance (+0x30).
    (*sheetOut)->m_materialInstance = *materialOut;

    // Get image dimensions
    int imgW = image->Width();
    int imgH = image->Height();

    // Set sprite sheet name/group strings (empty) and dimensions
    UI::SpriteSheet* ss = *sheetOut;
    System::String emptyStr1;
    System::String emptyStr2;
    // LEFT: SpriteSheet holds them as char* (H5, R-UI's header).
    ss->m_groupName = emptyStr1;
    ss->m_textureName = emptyStr2;
    ss->m_imageWidth = imgW;
    ss->m_imageHeight = imgH;
    ss->m_padForFiltering = false;

    // Add sprite info: empty name, empty group, full image rect
    imgW = image->Width();
    imgH = image->Height();
    System::String nameEmpty;
    System::String groupEmpty;
    (*sheetOut)->AddSpriteInfo(nameEmpty, groupEmpty, 0, 0, imgW, imgH);

    // Set sprite info with UV scale
    imgW = image->Width();
    imgH = image->Height();
    float uvScale = kRdata_14017214c;   // 0x14017214c = 1.0f (was g_fUVScale; its fallback never ran)
    (*sheetOut)->SetSpriteInfo(0, imgW, imgH, 0.0f, 0.0f, uvScale, uvScale);

    // --- Find "noiseImage" element and set sprite ---
    System::String elemNameStr("noiseImage");
    UI::Element* rawElem = _container->FindElement(elemNameStr).Get();

    UI::ImageElement* imageElem = nullptr;
    if (rawElem && ::Object::IsDerivedFromNullSafe(rawElem, UI::ImageElement::TypeId())) {
        imageElem = static_cast<UI::ImageElement*>(rawElem);
    }

    // Create Sprite from SpriteSheet (index 0) and set on element
    UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(*sheetOut), 0);
    imageElem->SetSprite(&sprite);
}

// ============================================================
// NoiseUI::GenerateNoise (0x1400711D0): Generate Perlin noise texture
// ============================================================
// Reads 5 edit values (persistance, octaves, scale, cutOff, gradient),
// creates a PerlinNoise generator with random seeds, renders a 256x256
// grayscale texture, and copies it to the ImageBuffer.

// Helper: an edit element's text as a float
static float NoiseUI_ReadEditFloat(const UI::EditElement* edit) {
    return System::String::ToFloat(edit->_text._data);
}

void NoiseUI::GenerateNoise() {
    // Read edit values
    float persistance = NoiseUI_ReadEditFloat(noisePersistance);
    float octaves     = NoiseUI_ReadEditFloat(noiseOctaves);
    float scale       = NoiseUI_ReadEditFloat(noiseScale);
    float cutOff      = NoiseUI_ReadEditFloat(noiseCutOff);
    float gradient    = NoiseUI_ReadEditFloat(noiseGradient);

    // Compute inverse scale (uvScale / scale)
    float uvScale = kRdata_14017214c;   // 0x14017214c = 1.0f (was g_fUVScale; its fallback never ran)
    float invScale = uvScale / scale;

    // Create Random with time-based seed, get two random seeds.
    System::Random random;   // ??0Random@System@@QEAA@XZ 0x1800c7f20 (no dtor)
    int seed1 = random.RandTol(static_cast<int32_t>(0x7ffffff));
    int seed2 = random.RandTol(static_cast<int32_t>(0x7ffffff));

    // Create PerlinNoise (0x10 bytes)
    RtUtility::PerlinNoise noise;
    RtUtility_PerlinNoise_Ctor(&noise, persistance, octaves, seed2, seed1);   // ??0PerlinNoise@Utility@@QEAA@MMHH@Z

    // Allocate 256*256 pixel buffer
    uint32_t* pixels = (uint32_t*)System::MemoryAllocator::MemAlloc(0x40000, 4);

    // Generate noise texture
    // Constants from exe .rdata:
    //   0.5f  (DAT_140172114) — noise bias (center around 0)
    //   255.0f (DAT_14017222c) — gradient divisor
    float halfVal = 0.5f;
    float gradientScale = 255.0f / gradient;

    uint32_t* pix = pixels;
    for (int y = 0; y < 0x100; y++) {
        for (int x = 0; x < 0x100; x++) {
            // Generate noise value at scaled position
            float noiseVal = RtUtility_PerlinNoise_Generate(
                &noise, (float)x * invScale, (float)y * invScale);   // ?Generate@PerlinNoise@Utility@@

            // Map from [-1,1] to [0,1], subtract cutoff, clamp
            float val = (noiseVal * halfVal + halfVal) - cutOff;
            if (val < 0.0f) val = 0.0f;

            // Normalize to [0,1] range based on (1 - cutOff)
            val = val / (uvScale - cutOff);

            // Clamp to [0, 1]
            if (val > uvScale) val = uvScale;
            else if (val < 0.0f) val = 0.0f;

            // Quantize to gradient steps and back
            uint32_t gray = (uint32_t)(int64_t)((float)(int)(val * gradient) * gradientScale);
            if (gray > 0xFF) gray = 0xFF;

            // Pack as ARGB (opaque gray)
            *pix = 0xFF000000 | (gray << 16) | (gray << 8) | gray;
            pix++;
        }
    }

    // Copy to ImageBuffer texture
    imageBuffer->CopyToTexture(pixels, 0x100, 0x100);
    System::MemoryAllocator::MemFree(pixels);
}

// ============================================================
// NoiseUI::OnCreated (slot 4, vtable +0x20) — Setup noise visualization
// ============================================================
// Original: 0x1400714C0
//
// Allocates a 256x256 ImageBuffer, fills with black, creates
// MaterialInstance + SpriteSheet via SetupImage, finds 5 edit
// elements and the "buttonGenerate" button (ID 1000).

void NoiseUI::OnCreated() {
    // Allocate ImageBuffer (0x68 bytes)
    void* imgBuf = System::MemoryAllocator::MemAlloc(0x68, 8);
    Video::ImageBuffer* image = nullptr;
    if (imgBuf != nullptr) {
        image = ::new (imgBuf) Video::ImageBuffer();   // B.3.9 EXA: the real class (exe table 0x14015c8c0 is its own)
    }
    imageBuffer = image;

    // ImageBuffer::Create (slot 9, vtable +0x48):
    //   Create(ImageType=0, Usage=2, Format=5, mip=0, width=256,
    //          height=256, FilterMode=1, AddressMode=1,1,1,
    //          Array<Array<uchar>>* levels=null)
    // The last parameter is a POINTER in a stack slot -- passing it as an int
    // leaves the high dword uninitialised (that exact bug crashed ModDialog).
    image->Create(
        /*type     */ 0,
        /*usage    */ 2,
        /*format   */ 5,
        /*flags    */ 0u,
        /*width    */ 0x100,
        /*height   */ 0x100,
        /*filter   */ 1,
        /*addressU */ 1,
        /*addressV */ 1,
        /*addressW */ 1,
        /*levels   */ nullptr);

    // Allocate 256*256 ARGB pixel buffer, fill with black (0xFF000000)
    uint32_t* pixels = (uint32_t*)System::MemoryAllocator::MemAlloc(0x40000, 4);
    uint32_t* p = pixels;
    for (int i = 0; i < 0x10000; i++) {
        *p = 0xFF000000;
        p++;
    }

    // Copy pixel data to ImageBuffer texture
    imageBuffer->CopyToTexture(pixels, 0x100, 0x100);
    System::MemoryAllocator::MemFree(pixels);

    // Setup MaterialInstance + SpriteSheet, find "noiseImage" element
    SetupImage(imageBuffer, &spriteSheet, &material);

    // The five edits
    UI::Element* root = _container;
    noisePersistance = NoiseUI_FindEdit(root, "noisePersistance");
    noiseOctaves     = NoiseUI_FindEdit(root, "noiseOctaves");
    noiseScale       = NoiseUI_FindEdit(root, "noiseScale");
    noiseCutOff      = NoiseUI_FindEdit(root, "noiseCutOff");
    noiseGradient    = NoiseUI_FindEdit(root, "noiseGradient");

    // Find "buttonGenerate" button, set element ID to 1000
    System::String nameStr("buttonGenerate");
    UI::Element* buttonElem = root->FindElement(nameStr).Get();
    buttonElem->SetElementId(1000);
}

// ============================================================
// NoiseUI::OnElementEvent (slot 9, vtable +0x48) — Handle button click
// ============================================================
// Original: 0x1400714A0
//
// When msg==0x12 (button click) and id==1000 (buttonGenerate),
// regenerates the Perlin noise texture using current edit values.

void NoiseUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId msg,
                             int32_t id, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/,
                             void* /*userData*/) {
    if (msg != UI::EventId::Activated || id != 1000) return;

    // Regenerate noise texture from edit values
    GenerateNoise();
}

// [0] 0x140071bc0 (the body of the scalar-deleting dtor 0x140012da0). Retail
// stamps 0x14015ca80, then deletes +0x40 (0x140071be0..0x140071bf1), +0x38
// (0x140071bf3..0x140071c04) and +0x30 (0x140071c06..0x140071c17), each
// `TEST RCX,RCX; JZ` then `CALL [vptr]` with EDX = 1: C++ `delete` through the
// virtual dtor, in that order (ModDialog's dtor spells its three the same way),
// then tail-jumps to ~DialogController ([0x1401515c8]).
NoiseUI::~NoiseUI() {
    delete spriteSheet;   // +0x40
    delete material;      // +0x38
    delete imageBuffer;   // +0x30
}
