// ============================================================================
// src/ext/resources/font_provider.cpp -- ext.sharp_ui's font variants, provided (KITFREE RSCFONT)
// ============================================================================
// NOT RETAIL. A kit-compiled Font/Sharp<Kit>_<px>.rsc is four objects: the
// UI::Font (named "resource"), its MaterialInstance ("material"), its
// ImageBuffer ("texture") and the glyph SpriteSheet
// (Build/Sharp<Kit>_<px>Sheet.rsc). This provider builds the same four when
// the factory's disk lookup misses on any of their paths:
//   1. the base font (Font/<Kit>.rsc) is FindOrLoaded, as sharp_ui.cpp does;
//      it must be the game's face (UI::Font::_string20 == FrancophilSans) --
//      a translation mod's font keeps sharp_ui's retail draw;
//   2. the codepoints are the base sheet's sprite names ("c65"), in sprite
//      order, so the variant's glyph i IS the base's glyph i and the base's
//      _charToGlyph table is copied verbatim (sharp_ui maps twins by
//      codepoint through both tables; identical tables cannot miss);
//   3. the glyphs are rasterised (font_raster.h) and cached per (Kit, px):
//      a rebuild after ExternalFactory::Clean is a copy of the cache;
//   4. the sheet, material and texture come from font_sheet.h's builder,
//      which registers them under their paths; the UI::Font is filled field
//      for field as the kit's (font.cpp's Serialize order), given its path,
//      and returned -- the seam registers it.
// Only the heights asked for are built. Every outcome is one [ExtRes] line
// on stderr; a build's cost is timed (first build and cache hits both).
// ============================================================================
#include "ext/resources/font_provider.h"
#include "ext/resources/Resources.h"
#include "ext/resources/font_raster.h"
#include "ext/resources/font_sheet.h"
#include "ext/resources/font_tables.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "graphics/Material.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/String.h"
#include "ui/CharacterSet.h"
#include "ui/Font.h"
#include "ui/SpriteSheet.h"
#include "video/ImageBuffer.h"

namespace Ext {
namespace Resources {

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

struct Cache {
    std::mutex lock;
    std::map<std::pair<int32_t, int32_t>, RasterFont> fonts;   // (kit, px) -> rasterised glyphs
    bool faceChecked = false;
    bool faceOk = false;
};
Cache& G() { static Cache c; return c; }

template <class T>
T* LoadAs(const char* path) {
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr) return nullptr;
    T* p = f->FindOrLoadAs<T>(System::Filename(path));
    return Object::IsDerivedFromNullSafe(p, T::TypeId()) ? p : nullptr;
}

template <class T>
T* FindAs(const char* path) {
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr) return nullptr;
    T* p = f->FindAs<T>(System::Filename(path));
    return Object::IsDerivedFromNullSafe(p, T::TypeId()) ? p : nullptr;
}

bool FaceOk() {
    Cache& c = G();
    if (!c.faceChecked) {
        std::string why;
        c.faceOk = FaceAvailable(kGameFace, &why);
        c.faceChecked = true;
        if (!c.faceOk)
            std::fprintf(stderr, "[ExtRes] font variants off: typeface %s absent (%s); ext.sharp_ui keeps the retail glyphs\n",
                         kGameFace, why.c_str());
    }
    return c.faceOk;
}

// The rasterised glyphs for (kit, px), from the cache or GDI. `cps` is the
// base sheet's codepoint list; null on failure (logged).
const RasterFont* Rasterised(int32_t kit, int32_t px, const std::vector<int32_t>& cps, bool& fromCache) {
    Cache& c = G();
    const auto key = std::make_pair(kit, px);
    auto it = c.fonts.find(key);
    if (it != c.fonts.end()) {
        // A hit only for the same codepoint list (review F-231 / F-235): a mod reload can bring a
        // base font with another glyph set, and BuildVariant indexes glyphs by the base's sprite
        // count.
        bool same = it->second.glyphs.size() == cps.size();
        for (size_t i = 0; same && i < cps.size(); ++i)
            same = it->second.glyphs[i].codepoint == cps[i];
        if (same) {
            fromCache = true;
            return &it->second;
        }
        c.fonts.erase(it);
    }
    fromCache = false;
    RasterFont rf;
    std::string why;
    const Clock::time_point t0 = Clock::now();
    if (!RasterizeFont(kGameFace, px, cps.data(), static_cast<int32_t>(cps.size()), rf, &why)) {
        std::fprintf(stderr, "[ExtRes] font %s at %d px: rasterise failed (%s)\n", kKitFonts[kit].name, px, why.c_str());
        return nullptr;
    }
    std::fprintf(stderr, "[ExtRes] font %s at %d px: %d glyphs rasterised in %.1f ms\n",
                 kKitFonts[kit].name, px, static_cast<int32_t>(rf.glyphs.size()), MsSince(t0));
    return &c.fonts.emplace(key, std::move(rf)).first->second;
}

// Builds the four objects of one variant; returns the one `want` names
// ("resource" / "material" / "texture" / "sheet"), every other one
// registered. Null when anything is missing (logged).
Object::External* BuildVariant(const VariantPath& vp) {
    const Clock::time_point t0 = Clock::now();
    const VariantNames names = MakeVariantNames(vp.kit, vp.px);

    UI::Font* const base = LoadAs<UI::Font>(names.baseFontPath.c_str());
    if (base == nullptr) { std::fprintf(stderr, "[ExtRes] font %s: base %s not loaded\n", names.file.c_str(), names.baseFontPath.c_str()); return nullptr; }
    if (base->_string20._data == nullptr || std::strcmp(base->_string20._data, kGameFace) != 0) {
        std::fprintf(stderr, "[ExtRes] font %s: base face '%s' is not %s, not provided\n", names.file.c_str(),
                     base->_string20._data != nullptr ? base->_string20._data : "", kGameFace);
        return nullptr;
    }
    UI::SpriteSheet* const baseSheet = base->_spriteSheet.Get();
    if (baseSheet == nullptr) { std::fprintf(stderr, "[ExtRes] font %s: base has no sheet\n", names.file.c_str()); return nullptr; }
    const int32_t count = baseSheet->m_sprites._size;
    if (count <= 0 || count != base->_glyphs._size) {
        std::fprintf(stderr, "[ExtRes] font %s: base sheet %d sprites, glyph table %d\n", names.file.c_str(), count, base->_glyphs._size);
        return nullptr;
    }
    std::vector<int32_t> cps(static_cast<size_t>(count));
    std::vector<std::string> spriteNames(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        const char* n = baseSheet->m_sprites._data[i].name._data;
        const int32_t cp = CodepointFromSpriteName(n);
        if (cp < 0) { std::fprintf(stderr, "[ExtRes] font %s: base sprite %d named '%s', not c<codepoint>\n", names.file.c_str(), i, n != nullptr ? n : ""); return nullptr; }
        cps[static_cast<size_t>(i)] = cp;
        spriteNames[static_cast<size_t>(i)] = n;
    }

    bool fromCache = false;
    const RasterFont* const rf = Rasterised(vp.kit, vp.px, cps, fromCache);
    if (rf == nullptr) return nullptr;

    // ---- the sheet, material and texture through the builder ---------------
    std::vector<const uint8_t*> bitmaps(static_cast<size_t>(count));
    std::vector<int32_t> widths(static_cast<size_t>(count)), heights(static_cast<size_t>(count));
    std::vector<const char*> namePtrs(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        const RasterGlyph& g = rf->glyphs[static_cast<size_t>(i)];
        bitmaps[static_cast<size_t>(i)]  = g.alpha.data();
        widths[static_cast<size_t>(i)]   = g.width;
        heights[static_cast<size_t>(i)]  = g.height;
        namePtrs[static_cast<size_t>(i)] = spriteNames[static_cast<size_t>(i)].c_str();
    }
    FontSheetInput in;
    in.count = count;
    in.bitmaps = bitmaps.data();
    in.widths = widths.data();
    in.heights = heights.data();
    in.names = namePtrs.data();
    KitSheetSize(vp.px, in.sheetWidth, in.sheetHeight);
    in.sheetPath = names.sheetPath.c_str();
    in.materialPath = names.materialPath.c_str();
    in.texturePath = names.texturePath.c_str();
    in.baseMaterialPath = names.baseMaterialPath.c_str();
    FontSheetOutput sheet;
    if (!BuildFontSheet(in, sheet) || sheet.sheet == nullptr || sheet.material == nullptr) return nullptr;

    // ---- the UI::Font, field for field as the kit's (font.cpp Serialize order)
    void* mem = Object::detail::InternalMemAlloc(sizeof(UI::Font), 8);
    if (mem == nullptr) return nullptr;
    UI::Font* const font = ::new (mem) UI::Font();
    font->_string20   = System::String(kGameFace);              // the face
    font->_lineHeight = rf->height;                             // _fontHeight
    font->_string80   = System::String(names.sheetName.c_str());    // _sheetName
    font->_field70    = sheet.sheetWidth;                       // _sheetWidth
    font->_field74    = sheet.sheetHeight;                      // _sheetHeight
    font->_string88   = System::String(names.materialName.c_str()); // _materialName
    font->_string78   = System::String(names.imageName.c_str());    // _imageName
    font->_field28    = base->_field28;                         // the CharacterSet, shared
    font->_spriteSheet = sheet.sheet;
    font->_material    = sheet.material;
    font->_charToGlyph.SetSize(base->_charToGlyph._size);
    for (int32_t i = 0; i < base->_charToGlyph._size; ++i)
        font->_charToGlyph._data[i] = base->_charToGlyph._data[i];
    font->_glyphs.SetSize(count);
    for (int32_t i = 0; i < count; ++i) {
        const RasterGlyph& g = rf->glyphs[static_cast<size_t>(i)];
        UI::Font::Kerning& k = font->_glyphs._data[i];
        k._preAdvance = g.preAdvance; k._postKern = g.postKern; k._postAdvance = g.postAdvance; k._offsetY = g.offsetY;
    }
    font->_baseLine = rf->baseLine;
    // The two measured widths (reader version >= 1 stores them; the kit wrote
    // them from the same walk): "..." and " ".
    GlyphRow dot{0, 0, 0, 0}, space{0, 0, 0, 0};
    bool hasDot = false, hasSpace = false;
    for (int32_t i = 0; i < count; ++i) {
        const RasterGlyph& g = rf->glyphs[static_cast<size_t>(i)];
        const GlyphRow row{g.preAdvance, g.postKern, g.postAdvance, g.offsetY};
        if (g.codepoint == '.') { dot = row; hasDot = true; }
        if (g.codepoint == ' ') { space = row; hasSpace = true; }
    }
    const GlyphRow dots[3] = { dot, dot, dot };
    font->_ellipsisWidth = hasDot ? MeasureRun(dots, 3) : 0;
    font->_spaceWidth    = hasSpace ? MeasureRun(&space, 1) : 0;
    SetPath(font, names.fontPath.c_str());

    std::fprintf(stderr, "[ExtRes] font %s provided: %d glyphs, line %d baseline %d, sheet %dx%d, %s, %.1f ms\n",
                 names.file.c_str(), count, font->_lineHeight, font->_baseLine, sheet.sheetWidth, sheet.sheetHeight,
                 fromCache ? "glyphs from cache" : "first build", MsSince(t0));

    if (vp.part == "resource") return font;
    // Another part was asked for first: the font is registered here, the
    // asked part (already registered by the builder) is returned.
    Publish(font);
    if (vp.part == "material") return sheet.material;
    if (vp.part == "texture")  return sheet.texture;
    return sheet.sheet;
}

Object::External* ProvideFont(const char* path, void*) {
    VariantPath vp;
    if (!ParseVariantPath(path, vp)) return nullptr;
    std::lock_guard<std::mutex> lock(G().lock);
    if (!FaceOk()) return nullptr;
    // Idempotent: the part asked for may already be registered (built with
    // another part of the same variant).
    const VariantNames names = MakeVariantNames(vp.kit, vp.px);
    const char* own = vp.part == "resource" ? names.fontPath.c_str()
                    : vp.part == "material" ? names.materialPath.c_str()
                    : vp.part == "texture"  ? names.texturePath.c_str() : names.sheetPath.c_str();
    if (Object::External* existing = FindAs<Object::External>(own)) return existing;
    return BuildVariant(vp);
}

}  // namespace

void InitFonts() {
    if (!Register("Font/SharpFont", &ProvideFont, nullptr) || !Register("Build/SharpFont", &ProvideFont, nullptr))
        std::fprintf(stderr, "[ExtRes] font provider: not registered (null prefix or function)\n");
}

}  // namespace Resources
}  // namespace Ext
