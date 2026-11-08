// ============================================================================
// src/ext/resources/sheet_builder.cpp -- the packer and the three object fills
// (docs/extensions/KITFREE.md §3, block RSCSPRITE). NOT RETAIL.
// ============================================================================
// Host-linkable (tests/unit/ext_sheet_builder_test.cpp): nothing here touches
// the ExternalFactory or the video device. The exe-side allocation and the GPU
// texture live in sheet_objects.cpp.
// ============================================================================

#include "ext/resources/sheet_builder.h"

#include <algorithm>
#include <cstring>

#include "collection/Collection.h"
#include "graphics/Material.h"
#include "system/Filename.h"
#include "system/String.h"
#include "ui/SpriteSheet.h"
#include "video/ImageBuffer.h"

namespace Ext {
namespace Resources {

namespace {

// Paints `bitmap` at (x, y) of the atlas, converting RGBA -> BGRA, and when
// padded the 1-texel ring around it: each edge row / column replicated, the
// corners the corner texels (the kit's form, checked texel by texel against
// its Build/ExtensionsSprites.png).
void Paint(SheetLayout& out, const SheetBitmap& bitmap, int x, int y) {
    const int W = out.width;
    auto put = [&](int px, int py, const uint8_t* rgba) {
        uint8_t* dst = &out.bgra[(static_cast<size_t>(py) * W + px) * 4];
        dst[0] = rgba[2];
        dst[1] = rgba[1];
        dst[2] = rgba[0];
        dst[3] = rgba[3];
    };
    auto src = [&](int sx, int sy) -> const uint8_t* {
        sx = std::max(0, std::min(bitmap.width - 1, sx));
        sy = std::max(0, std::min(bitmap.height - 1, sy));
        return bitmap.rgba + (static_cast<size_t>(sy) * bitmap.width + sx) * 4;
    };
    const int ring = out.padded ? 1 : 0;
    for (int sy = -ring; sy < bitmap.height + ring; ++sy)
        for (int sx = -ring; sx < bitmap.width + ring; ++sx)
            put(x + sx, y + sy, src(sx, sy));
}

}  // namespace

// A shelf packer: rows of decreasing height, left to right, tallest bitmaps
// first (ties keep table order, so the layout is a pure function of the
// table). Every padded bitmap is `w + 2` x `h + 2` with its pixels at (+1, +1);
// the first shelf starts at (0, 0), so the pixels of the first sprite sit at
// (1, 1) exactly as the kit's do.
bool PackSheet(const SheetSpec& spec, const SheetBitmap* bitmaps, int count, SheetLayout& out) {
    out = SheetLayout();
    if (spec.sheetWidth <= 0 || spec.sheetHeight <= 0 || count < 0) return false;
    const int ring = spec.padForFiltering ? 1 : 0;

    std::vector<int> order(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) order[static_cast<size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (bitmaps[a].height != bitmaps[b].height) return bitmaps[a].height > bitmaps[b].height;
        return bitmaps[a].width > bitmaps[b].width;
    });

    std::vector<SheetPlacement> places(static_cast<size_t>(count));
    int shelfX = 0, shelfY = 0, shelfH = 0;
    for (int i : order) {
        const SheetBitmap& b = bitmaps[i];
        if (b.width <= 0 || b.height <= 0 || b.rgba == nullptr) return false;
        const int cw = b.width + 2 * ring, ch = b.height + 2 * ring;
        if (cw > spec.sheetWidth || ch > spec.sheetHeight) return false;
        if (shelfX + cw > spec.sheetWidth) {       // next shelf
            shelfY += shelfH;
            shelfX = 0;
            shelfH = 0;
        }
        if (shelfY + ch > spec.sheetHeight) return false;
        places[static_cast<size_t>(i)] = SheetPlacement{ shelfX + ring, shelfY + ring };
        shelfX += cw;
        shelfH = std::max(shelfH, ch);
    }

    out.width  = spec.sheetWidth;
    out.height = spec.sheetHeight;
    out.padded = spec.padForFiltering;
    out.places = places;
    out.bgra.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    for (int i = 0; i < count; ++i)
        Paint(out, bitmaps[i], places[static_cast<size_t>(i)].x, places[static_cast<size_t>(i)].y);
    return true;
}

SheetRecord RecordFor(const SheetLayout& layout, const SheetBitmap& bitmap, int i) {
    const SheetPlacement p = layout.places[static_cast<size_t>(i)];
    const int32_t pad = layout.padded ? 1 : 0;
    SheetRecord r;
    r.x = pad;                 // the kit's record: the pad offset, not the atlas position
    r.y = pad;
    r.width  = bitmap.width;
    r.height = bitmap.height;
    r.reserved = pad;          // `_reserved18`: 1 in every padded record the kit writes
    const float W = static_cast<float>(layout.width), H = static_cast<float>(layout.height);
    r.u0 = static_cast<float>(p.x) / W;
    r.v0 = static_cast<float>(p.y) / H;
    r.u1 = static_cast<float>(p.x + bitmap.width) / W;
    r.v1 = static_cast<float>(p.y + bitmap.height) / H;
    return r;
}

void FillSpriteSheet(UI::SpriteSheet& sheet, const SheetSpec& spec, const SheetBitmap* bitmaps,
                     int count, const SheetLayout& layout, Graphics::MaterialInstance* material) {
    sheet.m_groupName   = System::String(spec.materialName);   // +0x40, written first
    sheet.m_textureName = System::String(spec.imageName);      // +0x38
    sheet.m_imageWidth = layout.width;
    sheet.m_imageHeight = layout.height;
    sheet.m_padForFiltering = layout.padded;
    sheet.m_materialInstance = material;
    for (int i = 0; i < count; ++i) {
        const SheetRecord r = RecordFor(layout, bitmaps[i], i);
        sheet.AddSpriteInfo(System::String(bitmaps[i].name), System::String(bitmaps[i].group),
                            r.x, r.y, r.width, r.height);
        sheet.SetSpriteInfo(i, r.width, r.height, r.u0, r.v0, r.u1, r.v1);
        // AddSpriteInfo always writes 0 here; the kit's compiled record carries
        // the pad flag (saveload_draw.cpp writes the grown entry the same way).
        sheet.m_sprites._data[i]._reserved18 = r.reserved;
    }
}

void FillMaterialInstance(Graphics::MaterialInstance& material, Graphics::Material* baseMaterial,
                          const char* textureSlot, Video::ImageBuffer* texture) {
    material.SetMaterial(Object::ExternalPtr<Graphics::Material>(baseMaterial));
    material.AddTexture(System::String(textureSlot), Object::ExternalPtr<Video::ImageBuffer>(texture));
    material.BuildParameters();
}

void FillImageBuffer(Video::ImageBuffer& image, const SheetSpec& spec, const SheetLayout& layout) {
    image._width     = layout.width;
    image._height    = layout.height;
    image._dimension = 0;                 // 2D
    image._kind      = spec.imageKind;
    image._format    = spec.imageFormat;
    image._flags     = spec.imageFlags;
    image._addressU  = spec.imageAddress;
    image._addressV  = spec.imageAddress;
    image._addressW  = spec.imageAddress;
    image._filter    = spec.imageFilter;
    // The kit stores _imageName as a path (backslashes): go through Filename.
    image._sourceName = System::Filename(spec.imageName);
}

bool CreateTexture(Video::ImageBuffer& image, const SheetSpec& spec, const SheetLayout& layout) {
    // One level of BGRA8, handed to Create exactly as PlatformSerialize hands
    // the levels it read: Create copies the fields again and makes the
    // immutable texture through the device's ImageBufferData.
    Video::ImageBuffer::LevelArray levels;
    levels.SetSize(1);
    Collection::Array<uint8_t>& level0 = levels[0];
    level0.SetSize(static_cast<int>(layout.bgra.size()));
    std::memcpy(level0.GetData(), layout.bgra.data(), layout.bgra.size());
    return image.Create(0, spec.imageKind, spec.imageFormat, spec.imageFlags,
                        layout.width, layout.height, spec.imageFilter,
                        spec.imageAddress, spec.imageAddress, spec.imageAddress, &levels);
}

}  // namespace Resources
}  // namespace Ext
