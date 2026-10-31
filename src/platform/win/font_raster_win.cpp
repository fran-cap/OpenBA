// ============================================================================
// src/platform/win/font_raster_win.cpp -- the GDI glyph rasteriser (KITFREE RSCFONT)
// ============================================================================
// NOT RETAIL. ext/resources/font_raster.h has the contract and the kit's
// rule (read in Ghidra from UI::Font::Compile, Runtime-x64-profile.dll
// 0x180159820; docs/extensions/handins/RSCFONT.md "The kit's rule"). This is
// the Win32 half: the face lookup (installed, then the TTF beside the exe
// loaded privately), then the kit's calls in the kit's order over a DISPLAY
// DC: GetTextMetricsW, per codepoint GetCharABCWidthsW (float fallback) and
// GetGlyphOutlineW(GGO_GRAY8_BITMAP).
// Nothing here touches the engine; the unit tests link it on its own.
// ============================================================================
#include "ext/resources/font_raster.h"
#include "font_raster_freetype.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace Ext {
namespace Resources {

namespace {

std::mutex g_lock;              // GDI objects are per thread; the private-load state is shared
bool g_privateTried = false;    // AddFontResourceEx attempted once per process
bool g_privateLoaded = false;

void ToWide(const char* s, wchar_t* out, int cap) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
    if (n <= 0) out[0] = 0;
}

std::string FromWide(const wchar_t* s) {
    char buf[256];
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof buf, nullptr, nullptr);
    return n > 0 ? std::string(buf) : std::string();
}

// The kit's own CreateFontW call (UI::Font::Compile, Runtime-x64-profile.dll
// 0x180159c0a): height = +px (the CELL height), every other numeric argument 0
// except charset 1 (DEFAULT_CHARSET), out precision 6 (OUT_RASTER_PRECIS) and
// quality 2 (PROOF_QUALITY).
HFONT MakeFont(const wchar_t* face, int pixelHeight) {
    return CreateFontW(pixelHeight, 0, 0, 0, 0 /*FW_DONTCARE*/, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_RASTER_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, 0, face);
}

// Selects `face` at `pixelHeight` into `dc` and verifies the face GDI gave
// back (GDI substitutes silently). Returns the font, or null.
HFONT SelectFace(HDC dc, const wchar_t* face, int pixelHeight, std::string* got) {
    HFONT f = MakeFont(face, pixelHeight);
    if (f == nullptr) return nullptr;
    HGDIOBJ old = SelectObject(dc, f);
    wchar_t name[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, name);
    if (got) *got = FromWide(name);
    if (_wcsicmp(name, face) == 0) return f;
    SelectObject(dc, old);
    DeleteObject(f);
    return nullptr;
}

// `<exe dir>/<face>.ttf`, loaded privately for this process (the release
// package ships it there; a user may also drop it beside the exe).
bool LoadPrivateFace(const wchar_t* face, std::string* why) {
    if (g_privateTried) return g_privateLoaded;
    g_privateTried = true;
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { if (why) *why = "exe path unknown"; return false; }
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash == nullptr) { if (why) *why = "exe path has no directory"; return false; }
    slash[1] = 0;
    wcsncat(path, face, MAX_PATH - wcslen(path) - 1);
    wcsncat(path, L".ttf", MAX_PATH - wcslen(path) - 1);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        if (why) *why = "not installed and no " + FromWide(path);
        return false;
    }
    g_privateLoaded = AddFontResourceExW(path, FR_PRIVATE, nullptr) > 0;
    if (!g_privateLoaded && why) *why = "AddFontResourceEx failed on " + FromWide(path);
    return g_privateLoaded;
}

// The face selected into `dc` at `pixelHeight`: installed, else the private file.
HFONT AcquireFace(HDC dc, const wchar_t* face, int pixelHeight, std::string* why) {
    std::string got;
    if (HFONT f = SelectFace(dc, face, pixelHeight, &got)) return f;
    if (LoadPrivateFace(face, why)) {
        if (HFONT f = SelectFace(dc, face, pixelHeight, &got)) return f;
        if (why) *why = "private " + FromWide(face) + ".ttf loaded but GDI selected '" + got + "'";
        return nullptr;
    }
    if (why && why->empty()) *why = "GDI substituted '" + got + "'";
    return nullptr;
}

}  // namespace

const char* const* DefaultFaces(size_t& count) {
    static const char* const faces[] = {"Segoe UI", "Arial", kGameFace};
    count = sizeof faces / sizeof faces[0];
    return faces;
}

bool FaceAvailable(const char* face, std::string* why) {
    std::lock_guard<std::mutex> lock(g_lock);
    wchar_t wface[LF_FACESIZE];
    ToWide(face, wface, LF_FACESIZE);
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) { if (why) *why = "CreateCompatibleDC failed"; return false; }
    HFONT f = AcquireFace(dc, wface, 24, why);
    if (f != nullptr) {
        SelectObject(dc, GetStockObject(SYSTEM_FONT));
        DeleteObject(f);
    }
    DeleteDC(dc);
    return f != nullptr;
}

bool RasterizeFont(const char* face, int32_t pixelHeight, const int32_t* codepoints,
                   int32_t count, RasterFont& out, std::string* why) {
    out.face.clear();
    out.glyphs.clear();
    out.height = 0;
    out.baseLine = 0;
    if (pixelHeight < 4 || pixelHeight > 256) { if (why) *why = "height out of range"; return false; }
    if (count < 0 || (count > 0 && codepoints == nullptr)) { if (why) *why = "no codepoints"; return false; }

    std::lock_guard<std::mutex> lock(g_lock);
    wchar_t wface[LF_FACESIZE];
    ToWide(face, wface, LF_FACESIZE);
    // A display DC, as the kit's GetDC(<app window>): the ABC widths are the
    // font's realisation on the DC, and only a colour surface realises it
    // antialiased. CreateCompatibleDC(nullptr) holds a 1x1 MONOCHROME bitmap,
    // GDI realises the font bilevel there and GetCharABCWidthsW answers the
    // bilevel box (B one or two pixels narrower than the GRAY8 box on most
    // glyphs) -- the 354 / 5184 odd rows of the first cut (RSCFONT.md).
    HDC dc = GetDC(nullptr);
    if (dc == nullptr) { if (why) *why = "GetDC failed"; return false; }
    const HGDIOBJ oldFont = GetCurrentObject(dc, OBJ_FONT);
    HFONT f = AcquireFace(dc, wface, pixelHeight, why);
    if (f == nullptr) { ReleaseDC(nullptr, dc); return false; }

    TEXTMETRICW tm;
    if (!GetTextMetricsW(dc, &tm)) {
        if (why) *why = "GetTextMetrics failed";
        SelectObject(dc, oldFont); DeleteObject(f); ReleaseDC(nullptr, dc);
        return false;
    }
    out.face     = face;
    out.height   = pixelHeight;          // == tm.tmHeight for a positive lfHeight
    out.baseLine = static_cast<int32_t>(tm.tmDescent);

    const MAT2 identity = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
    std::vector<uint8_t> buf;
    out.glyphs.resize(static_cast<size_t>(count));
    bool ok = true;
    for (int32_t i = 0; i < count && ok; ++i) {
        RasterGlyph& g = out.glyphs[static_cast<size_t>(i)];
        g.codepoint = codepoints[i];
        // The kit's order: the codepoint must lie in [tmFirstChar, tmLastChar]
        // and have ABC widths (integer, else the float ones truncated), or it is
        // left out of the font; we refuse instead (the provider's glyph i must be
        // the base font's glyph i, and FrancophilSans covers all 96).
        const UINT cp = static_cast<UINT>(g.codepoint);
        if (static_cast<int32_t>(cp) < static_cast<int32_t>(tm.tmFirstChar) ||
            static_cast<int32_t>(cp) > static_cast<int32_t>(tm.tmLastChar)) {
            if (why) { char m[96]; std::snprintf(m, sizeof m, "U+%04X outside the face's range", g.codepoint); *why = m; }
            ok = false;
            break;
        }
        int32_t abcA = 0, abcB = 0, abcC = 0;
        ABC abc;
        ABCFLOAT abcf;
        if (GetCharABCWidthsW(dc, cp, cp, &abc)) {
            abcA = abc.abcA; abcB = static_cast<int32_t>(abc.abcB); abcC = abc.abcC;
        } else if (GetCharABCWidthsFloatW(dc, cp, cp, &abcf)) {
            abcA = static_cast<int32_t>(abcf.abcfA);
            abcB = static_cast<int32_t>(static_cast<int64_t>(abcf.abcfB));
            abcC = static_cast<int32_t>(abcf.abcfC);
        } else {
            if (why) { char m[96]; std::snprintf(m, sizeof m, "GetCharABCWidths failed on U+%04X", g.codepoint); *why = m; }
            ok = false;
            break;
        }
        GLYPHMETRICS gm;
        std::memset(&gm, 0, sizeof gm);
        const DWORD bytes = GetGlyphOutlineW(dc, static_cast<UINT>(g.codepoint), GGO_GRAY8_BITMAP,
                                             &gm, 0, nullptr, &identity);
        if (bytes == GDI_ERROR) {
            if (why) { char m[96]; std::snprintf(m, sizeof m, "GetGlyphOutline failed on U+%04X", g.codepoint); *why = m; }
            ok = false;
            break;
        }
        g.width       = static_cast<int32_t>(gm.gmBlackBoxX);
        g.height      = static_cast<int32_t>(gm.gmBlackBoxY);
        g.preAdvance  = abcA;
        g.postKern    = abcC;
        g.postAdvance = abcB;
        g.offsetY     = static_cast<int32_t>(gm.gmptGlyphOrigin.y) - g.height;
        g.alpha.assign(static_cast<size_t>(g.width) * static_cast<size_t>(g.height), 0);
        if (bytes > 0 && g.width > 0 && g.height > 0) {
            // GDI pads each row to a DWORD; levels are 0..64.
            buf.assign(bytes, 0);
            if (GetGlyphOutlineW(dc, static_cast<UINT>(g.codepoint), GGO_GRAY8_BITMAP, &gm, bytes,
                                 buf.data(), &identity) == GDI_ERROR) {
                if (why) *why = "GetGlyphOutline (bitmap) failed";
                ok = false;
                break;
            }
            const size_t stride = (static_cast<size_t>(g.width) + 3u) & ~static_cast<size_t>(3u);
            for (int32_t y = 0; y < g.height; ++y) {
                const uint8_t* row = buf.data() + static_cast<size_t>(y) * stride;
                uint8_t* dst = g.alpha.data() + static_cast<size_t>(y) * static_cast<size_t>(g.width);
                for (int32_t x = 0; x < g.width; ++x) {
                    const unsigned level = row[x] > 64 ? 64u : row[x];
                    // the kit's (int)(level * (1/64.f) * 255.f) << 24 | 0xffffff, which is
                    // level * 4 - 1 for level 1..64 (and 0 for 0)
                    dst[x] = static_cast<uint8_t>(level == 0 ? 0 : level * 4 - 1);
                }
            }
        }
    }
    SelectObject(dc, oldFont);
    DeleteObject(f);
    ReleaseDC(nullptr, dc);
    if (!ok) out.glyphs.clear();
    return ok;
}

// ----------------------------------------------------------------------------
// The TTF file by name, for the FreeType rasteriser (font_raster_freetype.h;
// on Windows only the unit test's GDI comparison uses it): `<exe dir>/<face>.ttf`,
// then the font registry's file for "<face> (TrueType)" -- the per-user key
// holds a full path, the machine key a name under the Windows Fonts directory.
// ----------------------------------------------------------------------------
namespace FreeTypeRaster {

namespace {

bool RegistryFontFile(HKEY root, const wchar_t* value, std::wstring& file) {
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof buf;
    DWORD type = 0;
    const LSTATUS st = RegGetValueW(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                                    value, RRF_RT_REG_SZ, &type, buf, &size);
    if (st != ERROR_SUCCESS || buf[0] == 0) return false;
    file = buf;
    return true;
}

}  // namespace

bool FindFaceFile(const char* face, std::string& path, std::string* why) {
    path.clear();
    if (face == nullptr || *face == 0) { if (why) *why = "no face name"; return false; }
    wchar_t wface[LF_FACESIZE];
    ToWide(face, wface, LF_FACESIZE);
    std::string tried;
    wchar_t exe[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        if (wchar_t* slash = wcsrchr(exe, L'\\')) {
            slash[1] = 0;
            std::wstring p = std::wstring(exe) + wface + L".ttf";
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) { path = FromWide(p.c_str()); return true; }
            tried = FromWide(p.c_str());
        }
    }
    const std::wstring value = std::wstring(wface) + L" (TrueType)";
    std::wstring file;
    if (RegistryFontFile(HKEY_CURRENT_USER, value.c_str(), file) && GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
        path = FromWide(file.c_str());
        return true;
    }
    if (RegistryFontFile(HKEY_LOCAL_MACHINE, value.c_str(), file)) {
        if (file.find(L'\\') == std::wstring::npos) {
            wchar_t windir[MAX_PATH] = {};
            if (GetWindowsDirectoryW(windir, MAX_PATH) > 0) file = std::wstring(windir) + L"\\Fonts\\" + file;
        }
        if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) { path = FromWide(file.c_str()); return true; }
    }
    if (why) *why = "no " + std::string(face) + ".ttf (tried " + tried + ", the font registry)";
    return false;
}

}  // namespace FreeTypeRaster

}  // namespace Resources
}  // namespace Ext
