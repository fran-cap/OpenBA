#pragma once
// ============================================================================
// src/ext/resources/sheet_builder.h -- RGBA bitmaps -> a compiled sprite sheet
// (docs/extensions/KITFREE.md §3, block RSCSPRITE)
// ============================================================================
// NOT RETAIL. A generic in-exe twin of the mod kit's SpriteSheet compile: N
// bitmaps are packed into one atlas and become the THREE objects the kit's
// `Tools-x64.exe /build X.rsc` writes for a `SpriteSheet` source --
//
//   UI::SpriteSheet            the sprite table (name, group, x, y, w, h, uv)
//   Graphics::MaterialInstance the UI material by reference + one "diffuse" texture
//   Video::ImageBuffer         the packed atlas, BGRA8 (format 5), one level
//
// -- each registered under its own "File.rsc:name" path by the caller
// (Ext::Resources::SetPath). Nothing here is dialog-specific: block RSCFONT
// hands the same API its rasterised glyphs.
//
// THE KIT'S FORM (read off tools/ext_resources.sh's output for
// Dialog/ExtensionsSprites.rsc, 2026-10-17; docs/extensions/handins/RSCSPRITE.md):
//   * `_padForFiltering = true` puts every bitmap at an odd offset with a 1-texel
//     ring around it that REPLICATES the bitmap's edge (corners included);
//     neighbours are 2 texels apart. The record then carries x = y = 1 and the
//     always-zero `_reserved18` = 1 as well (0 / 0 / 0 without padding).
//   * width / height are the bitmap's own; uv = pixel edge / sheet size.
//   * `group` is the source path (the kit's, with backslashes); `name` the sprite.
//   * the atlas pixels are BGRA8, row-major, top-down; unused texels are 0.
//   * the kit's packing ORDER is not reproduced (it is not derivable from its
//     output); ours is a shelf packer, tallest first, stable in table order.
//     Placement is the one thing the cross-check lets differ (KITFREE.md §5).
//
// Two layers. PackSheet() and the record arithmetic are pure (host unit test,
// tests/unit/ext_sheet_builder_test.cpp); the Fill* functions write engine
// objects the caller constructed; BuildSheetObjects() (sheet_objects.cpp, exe
// only) allocates them through the classes' own factories, resolves the base
// material and creates the GPU texture.
// ============================================================================

#include <cstdint>
#include <vector>

namespace UI       { class SpriteSheet; }
namespace Graphics { class Material; class MaterialInstance; }
namespace Video    { class ImageBuffer; }

namespace Ext {
namespace Resources {

// One source bitmap: straight RGBA8, row-major, top-down, `width * height * 4` bytes.
struct SheetBitmap {
    const char*    name;     // the sprite's name (SpriteInfo::name)
    const char*    group;    // SpriteInfo::group -- the kit writes the source path
    int            width;
    int            height;
    const uint8_t* rgba;
};

// What the .rsc names: the sheet size, the padding rule and the two strings the
// SpriteSheet carries. The ImageBuffer parameters default to the kit's
// `Texture/UITexture.rsc` derivation (2D, usage 2, BGRA8, no mips, clamp, linear).
struct SheetSpec {
    int         sheetWidth      = 256;
    int         sheetHeight     = 256;
    bool        padForFiltering = true;
    const char* materialName    = "";   // SpriteSheet +0x40 (the .rsc's _materialName)
    const char* imageName       = "";   // SpriteSheet +0x38 and ImageBuffer::_sourceName (_imageName)
    int32_t     imageKind       = 2;    // ImageBuffer::_kind   (usage: default)
    int32_t     imageFormat     = 5;    // ImageBuffer::_format (5 = DXGI B8G8R8A8_UNORM)
    uint32_t    imageFlags      = 0;    // ImageBuffer::_flags  (bit 0 clear: one mip level)
    int32_t     imageAddress    = 1;    // ImageBuffer::_addressU/V/W (1 = clamp)
    int32_t     imageFilter     = 1;    // ImageBuffer::_filter (1 = linear)
};

// Where one bitmap's OWN pixels landed (the pad ring sits outside this rectangle).
struct SheetPlacement {
    int x;
    int y;
};

struct SheetLayout {
    int width  = 0;
    int height = 0;
    bool padded = false;
    std::vector<SheetPlacement> places;   // one per bitmap, table order
    std::vector<uint8_t>        bgra;     // width * height * 4, the atlas
};

// Packs the bitmaps into a `spec.sheetWidth x spec.sheetHeight` atlas and
// paints it. Returns false (and leaves `out` empty) when they do not fit; the
// caller picks a larger sheet (RSCFONT's rule) or gives up.
bool PackSheet(const SheetSpec& spec, const SheetBitmap* bitmaps, int count, SheetLayout& out);

// The kit's per-sprite record for bitmap `i` of a packed layout.
struct SheetRecord {
    int32_t x, y;           // SpriteInfo +0x10 / +0x14
    int32_t width, height;  // +0x1c / +0x20
    int32_t reserved;       // +0x18
    float   u0, v0, u1, v1; // +0x24 .. +0x30
};
SheetRecord RecordFor(const SheetLayout& layout, const SheetBitmap& bitmap, int i);

// ---- the engine objects (the caller constructed them; nothing is allocated here)
// The SpriteSheet: both names, the size, the flag, one SpriteInfo per bitmap in
// table order, and the material handle (+0x30).
void FillSpriteSheet(UI::SpriteSheet& sheet, const SheetSpec& spec, const SheetBitmap* bitmaps,
                     int count, const SheetLayout& layout, Graphics::MaterialInstance* material);

// The MaterialInstance: `baseMaterial` by reference, one texture `textureSlot`
// ("diffuse" for the UI material) bound to `texture`, no constants, then
// BuildParameters() as the loader's Serialize does (a null base is left alone).
void FillMaterialInstance(Graphics::MaterialInstance& material, Graphics::Material* baseMaterial,
                          const char* textureSlot, Video::ImageBuffer* texture);

// The ImageBuffer's fields (size, kind, format, flags, addressing, filter, the
// source name) -- NO GPU texture: CreateTexture() does that in the exe.
void FillImageBuffer(Video::ImageBuffer& image, const SheetSpec& spec, const SheetLayout& layout);

// Creates the immutable GPU texture from the atlas through the engine's own
// path (ImageBuffer::Create with one level, exactly what PlatformSerialize does
// on load). Needs the video device; false when Create fails.
bool CreateTexture(Video::ImageBuffer& image, const SheetSpec& spec, const SheetLayout& layout);

// ---- the exe-side one-shot (sheet_objects.cpp) ------------------------------
struct SheetObjects {
    UI::SpriteSheet*            sheet    = nullptr;
    Graphics::MaterialInstance* material = nullptr;
    Video::ImageBuffer*         texture  = nullptr;
};

// Packs, allocates the three objects through their type factories, fills them,
// resolves `baseMaterialPath` through the ExternalFactory ("Material/UI/uiMaterial.rsc"
// for UI sprites) and creates the GPU texture. Paths are NOT set (the provider
// does that with Ext::Resources::SetPath). On failure every object built so far
// is destroyed and all three are null; the reason is logged as `[ExtRes]`.
SheetObjects BuildSheetObjects(const SheetSpec& spec, const SheetBitmap* bitmaps, int count,
                               const char* baseMaterialPath, const char* textureSlot = "diffuse");

// Destroys objects BuildSheetObjects made that were never handed to the factory.
void DestroySheetObjects(SheetObjects& objects);

}  // namespace Resources
}  // namespace Ext
