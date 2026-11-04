// ============================================================================
// tests/unit/ext_font_provider_cache_test.cpp -- ext.sharp_ui's glyph cache (KITFREE RSCFONT)
// ============================================================================
// font_provider.cpp caches the rasterised glyphs per (kit, px). BuildVariant
// then reads rf->glyphs[i] for i < the CURRENT base sheet's sprite count.
// A rebuild over a base font with more sprites must not get a cached
// RasterFont sized by the old base. The provider's TU is compiled here with
// the rasteriser (GDI / FreeType) replaced by a stub that needs no face.
// ============================================================================
#include "unit.h"

#define RasterizeFont StubRasterizeFont
#include "../../src/ext/resources/font_provider.cpp"
#undef RasterizeFont

// The factory and the Resources seam are not in the host build; BuildVariant /
// ProvideFont are compiled but not called here.
Object::ExternalFactory* Object::ExternalFactory::_singleton = nullptr;
const Object::External* Object::ExternalFactory::Find(const System::Filename&) const { return nullptr; }
const Object::External* Object::ExternalFactory::FindOrLoad(const System::Filename&) { return nullptr; }

namespace Ext {
namespace Resources {
bool Register(const char*, ProviderFn, void*) { return false; }
bool BuildFontSheet(const FontSheetInput&, FontSheetOutput&) { return false; }
void SetPath(Object::External*, const char*) {}
Object::External* Publish(Object::External* obj) { return obj; }
static int g_stubCalls = 0;
bool StubRasterizeFont(const char* face, int32_t pixelHeight, const int32_t* codepoints, int32_t count,
                       RasterFont& out, std::string*) {
    ++g_stubCalls;
    out = RasterFont();
    out.face = face;
    out.height = pixelHeight;
    out.baseLine = 1;
    for (int32_t i = 0; i < count; ++i) {
        RasterGlyph g;
        g.codepoint = codepoints[i];
        g.width = 1;
        g.height = 1;
        g.alpha.assign(1, 0);
        out.glyphs.push_back(g);
    }
    return true;
}
}  // namespace Resources
}  // namespace Ext

UNIT_TEST(ext_font_provider_cache_matches_the_base_codepoints) {
    using namespace Ext::Resources;
    // First build: a base sheet of 3 sprites (c65 c66 c67).
    const std::vector<int32_t> first = { 65, 66, 67 };
    bool fromCache = false;
    const RasterFont* a = Rasterised(0, 977, first, fromCache);
    CHECK(a != nullptr);
    CHECK(!fromCache);
    CHECK_EQ(a->glyphs.size(), first.size());

    // Rebuild (after ExternalFactory::Clean / a mod reload) over a base with 5 sprites:
    // BuildVariant loops i < 5 over rf->glyphs.
    const std::vector<int32_t> second = { 65, 66, 67, 0xE9, 0xE8 };
    const RasterFont* b = Rasterised(0, 977, second, fromCache);
    CHECK(b != nullptr);
    CHECK_EQ(b->glyphs.size(), second.size());   // origin/main: 3 cached glyphs for 5 codepoints
    if (b != nullptr && b->glyphs.size() == second.size()) {
        for (size_t i = 0; i < second.size(); ++i)
            CHECK_EQ(b->glyphs[i].codepoint, second[i]);
    }

    // Same count, different codepoints: the glyphs must be the new ones.
    const std::vector<int32_t> third = { 65, 66, 67, 0xC0, 0xC9 };
    const RasterFont* c = Rasterised(0, 977, third, fromCache);
    CHECK(c != nullptr);
    if (c != nullptr) {
        CHECK_EQ(c->glyphs.size(), third.size());
        for (size_t i = 0; i < third.size() && i < c->glyphs.size(); ++i)
            CHECK_EQ(c->glyphs[i].codepoint, third[i]);
    }
}
