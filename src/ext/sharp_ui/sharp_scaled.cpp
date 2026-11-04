// ============================================================================
// src/ext/sharp_ui/sharp_scaled.cpp -- the magnified dialog sheet's engine half
// (NOT RETAIL; sharp_scaled.h / sharp_sheet.h have the contract)
// ============================================================================
#include "ext/sharp_ui/sharp_scaled.h"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ext/resources/sheet_builder.h"
#include "ext/sharp_ui/sharp_math.h"
#include "ext/sharp_ui/sharp_sheet.h"
#include "ext/sharp_ui/sharp_sprites_data.h"
#include "graphics/Material.h"
#include "system/Filename.h"
#include "system/String.h"
#include "ui/SpriteSheet.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

namespace Ext {
namespace SharpUI {

namespace {

constexpr const char* kDialogSheet = "Dialog/SpriteSheet.rsc";
constexpr int32_t kFormatBgra8 = 5;   // ImageBuffer::_format 5 = DXGI B8G8R8A8_UNORM

// One build: the base texture it was read from and what it made. Kept for the
// process: a texture a frame in flight may still sample is never freed.
struct Built {
    const Video::ImageBuffer*   base     = nullptr;
    int                         factor   = 0;
    Video::ImageBuffer*         texture  = nullptr;
    Graphics::MaterialInstance* material = nullptr;
};
std::vector<Built> g_built;
const Video::ImageBuffer* g_failed = nullptr;   // the base a build failed for (log once)

bool SamePathNoCase(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        const char ca = (*a == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(*a)));
        const char cb = (*b == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(*b)));
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

Video::ImageBuffer* DiffuseOf(const UI::SpriteSheet& sheet) {
    const Graphics::MaterialInstance* const m = sheet.m_field30;
    if (m == nullptr) return nullptr;
    const int32_t i = m->FindTexture(System::String("diffuse"));
    if (i < 0 || i >= m->_textures._size) return nullptr;
    return m->_textures._data[i].image;
}

template <class T>
T* NewObject() {
    return static_cast<T*>(T::CreateNewObject());
}

Built Build(UI::SpriteSheet& sheet, Video::ImageBuffer& base, int factor) {
    Built b;
    const auto t0 = std::chrono::steady_clock::now();
    const int w = base._width;
    const int h = base._height;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    if (!base.CopyFromTexture(pixels.data(), w, h)) {
        std::fprintf(stderr, "[SharpUI] %dx sheet: reading back the %d x %d sheet failed\n", factor, w, h);
        return b;
    }

    const int count = sheet.m_sprites._size;
    std::vector<ScaledSprite> sprites(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const UI::SpriteInfo& s = sheet.m_sprites._data[i];
        // The atlas position is the UVs': a compiled record's x / y are the
        // kit's pad offset (1, 1), not where the sprite sits (sheet_builder.h).
        sprites[static_cast<size_t>(i)].rect = RectFromUvs(s.u0, s.v0, s.u1, s.v1, w, h);
        const ArtSprite* art = factor == SpritesData::kScale
            ? FindArt(SpritesData::kSprites, SpritesData::kSpriteCount, s.name._data, s.width, s.height, factor)
            : nullptr;
        sprites[static_cast<size_t>(i)].art = art != nullptr ? art->rgba : nullptr;
    }

    Resources::SheetLayout layout;
    const int pad = sheet.m_field50 ? 1 : 0;
    const int drawn = BuildScaledSheet(pixels.data(), w, h, factor, pad, sprites.data(), count, layout.bgra);
    layout.width = w * factor;
    layout.height = h * factor;
    layout.padded = pad != 0;

    Resources::SheetSpec spec;
    spec.sheetWidth  = layout.width;
    spec.sheetHeight = layout.height;
    spec.imageName   = "Build/SharpSpriteSheet.png";
    spec.imageAddress = base._addressU;
    spec.imageFilter  = base._filter;

    Graphics::Material* const baseMaterial = sheet.m_field30->_material.Get();
    b.texture  = NewObject<Video::ImageBuffer>();
    b.material = NewObject<Graphics::MaterialInstance>();
    if (b.texture == nullptr || b.material == nullptr || baseMaterial == nullptr) {
        std::fprintf(stderr, "[SharpUI] %dx sheet: could not make its texture / material\n", factor);
        delete b.texture;
        delete b.material;
        return Built();
    }
    Resources::FillImageBuffer(*b.texture, spec, layout);
    if (!Resources::CreateTexture(*b.texture, spec, layout)) {
        std::fprintf(stderr, "[SharpUI] %dx sheet: creating the %d x %d texture failed\n", factor,
                     layout.width, layout.height);
        delete b.texture;
        delete b.material;
        return Built();
    }
    Resources::FillMaterialInstance(*b.material, baseMaterial, "diffuse", b.texture);
    b.base = &base;
    b.factor = factor;
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[SharpUI] %dx sheet built: %d x %d, %d of %d sprites from our art, %.1f ms\n",
                 factor, layout.width, layout.height, drawn, count, ms);
    return b;
}

}  // namespace

bool IsDialogSheet(const char* path) {
    if (path == nullptr) return false;
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s", path);
    char* colon = std::strchr(buf, ':');
    if (colon != nullptr) *colon = '\0';   // "Dialog/SpriteSheet.rsc:resource"
    return SamePathNoCase(buf, kDialogSheet);
}

int PickSheetScale(float pxPerUnitX, float pxPerUnitY) {
    // Magnified at all: the 2x set (the slider's 75-150 % never passes 1.33).
    // A 4x set would be picked above a ratio of 2 here.
    return WantTwin2x(pxPerUnitX, pxPerUnitY) ? SpritesData::kScale : 0;
}

Graphics::MaterialInstance* ScaledSheetMaterial(UI::SpriteSheet* sheet, int factor) {
    if (sheet == nullptr || factor < 2) return nullptr;
    Video::ImageBuffer* const base = DiffuseOf(*sheet);
    if (base == nullptr) return nullptr;
    for (const Built& b : g_built)
        if (b.base == base && b.factor == factor) return b.material;
    if (g_failed == base) return nullptr;
    if (base->_format != kFormatBgra8 || base->_width <= 0 || base->_height <= 0 ||
        Video::Device::_singleton == nullptr) {
        std::fprintf(stderr, "[SharpUI] %dx sheet: the sheet texture is not a plain BGRA8 image "
                     "(format %d, %d x %d); retail's sheet is drawn\n", factor, base->_format,
                     base->_width, base->_height);
        g_failed = base;
        return nullptr;
    }
    const Built b = Build(*sheet, *base, factor);
    if (b.material == nullptr) {
        g_failed = base;
        return nullptr;
    }
    g_built.push_back(b);
    return b.material;
}

}  // namespace SharpUI
}  // namespace Ext
