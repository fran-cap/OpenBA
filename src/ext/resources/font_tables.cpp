// ============================================================================
// src/ext/resources/font_tables.cpp -- the pure part of the font provider (KITFREE RSCFONT)
// ============================================================================
// NOT RETAIL. font_tables.h has the contract.
// ============================================================================
#include "ext/resources/font_tables.h"

#include <cstdlib>
#include <cstring>

namespace Ext {
namespace Resources {

namespace {

// Case-sensitive prefix test.
bool StartsWith(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

}  // namespace

bool ParseVariantPath(const char* path, VariantPath& out) {
    out = VariantPath();
    if (path == nullptr) return false;
    std::string s(path);
    for (char& c : s) if (c == '\\') c = '/';
    std::string ident = "resource";
    const size_t colon = s.find(':');
    if (colon != std::string::npos) { ident = s.substr(colon + 1); s.resize(colon); }

    bool isSheet = false;
    const char* rest = nullptr;
    if (StartsWith(s.c_str(), "Font/Sharp")) rest = s.c_str() + 10;
    else if (StartsWith(s.c_str(), "Build/Sharp")) { rest = s.c_str() + 11; isSheet = true; }
    else return false;

    // "<Kit>_<px>.rsc" or "<Kit>_<px>Sheet.rsc": the longest kit name wins
    // ("FontSmall" before "Font").
    int32_t kit = -1; size_t kitLen = 0;
    for (int32_t k = 0; k < kKitFontCount; ++k) {
        const size_t n = std::strlen(kKitFonts[k].name);
        if (StartsWith(rest, kKitFonts[k].name) && rest[n] == '_' && n > kitLen) { kit = k; kitLen = n; }
    }
    if (kit < 0) return false;
    // MakeVariantNames' own spelling only (review F-239): 1-3 digits, no sign, blank or leading
    // zero. strtol took "_032" / "_+32" (and, with a 64-bit long, wrapped "_4294967328") to 32,
    // so an alias resolved to the canonical object and Provide re-pathed and re-added it.
    const char* num = rest + kitLen + 1;
    const char* end = num;
    int32_t px = 0;
    while (*end >= '0' && *end <= '9' && end - num < 4) {
        px = px * 10 + (*end - '0');
        ++end;
    }
    if (end == num || num[0] == '0' || end - num > 3) return false;
    const char* tail = isSheet ? "Sheet.rsc" : ".rsc";
    if (std::strcmp(end, tail) != 0) return false;
    if (isSheet && ident != "resource") return false;
    if (!isSheet && ident != "resource" && ident != "material" && ident != "texture") return false;

    out.kit = kit;
    out.px = static_cast<int32_t>(px);
    out.part = isSheet ? "sheet" : ident;
    return true;
}

VariantNames MakeVariantNames(int32_t kit, int32_t px) {
    VariantNames v;
    if (kit < 0 || kit >= kKitFontCount) return v;
    const std::string name = kKitFonts[kit].name;
    v.file             = "Sharp" + name + "_" + std::to_string(px);
    v.fontPath         = "Font/" + v.file + ".rsc:resource";
    v.materialPath     = "Font/" + v.file + ".rsc:material";
    v.texturePath      = "Font/" + v.file + ".rsc:texture";
    v.sheetPath        = "Build/" + v.file + "Sheet.rsc:resource";
    v.sheetName        = "Build/" + v.file + "Sheet.rsc";
    v.materialName     = "Font/" + v.file + ".rsc:material";
    v.imageName        = "Build/ui" + v.file + "Image.png";
    v.baseFontPath     = "Font/" + name + ".rsc";
    v.baseMaterialPath = "Font/" + name + "Material.rsc";
    return v;
}

int32_t CodepointFromSpriteName(const char* name) {
    if (name == nullptr || name[0] != 'c' || name[1] == 0) return -1;
    char* end = nullptr;
    const int64_t cp = std::strtoll(name + 1, &end, 10);
    if (*end != 0 || cp < 0 || cp > 0x10ffff) return -1;
    return static_cast<int32_t>(cp);
}

int32_t MeasureRun(const GlyphRow* rows, int32_t count) {
    int32_t total = 0, kern = 0;
    for (int32_t i = 0; i < count; ++i) {
        total += rows[i].postAdvance + rows[i].preAdvance + kern;
        kern = rows[i].postKern;
    }
    return total + kern;
}

bool KitSheetSize(int32_t px, int32_t& width, int32_t& height) {
    static const int32_t kSheets[][2] = { {128, 128}, {256, 128}, {256, 256}, {512, 256}, {512, 512},
                                         {1024, 512}, {1024, 1024} };   // the last two: Retina variants (px > 64)
    const int64_t need = 64LL * px * px;
    for (const auto& s : kSheets) {
        if (static_cast<int64_t>(s[0]) * s[1] >= need) { width = s[0]; height = s[1]; return true; }
    }
    width = height = 0;
    return false;
}

}  // namespace Resources
}  // namespace Ext
