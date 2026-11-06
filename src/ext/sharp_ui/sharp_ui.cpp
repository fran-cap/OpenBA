// ============================================================================
// src/ext/sharp_ui/sharp_ui.cpp -- ext.sharp_ui's engine glue
// ============================================================================
// NOT RETAIL. sharp_ui.h has the contract and the seams; sharp_math.h the rules;
// docs/extensions/handins/SHARPUI.md the why.
// ============================================================================
#include "ext/sharp_ui/sharp_ui.h"
#include "ext/sharp_ui/sharp_math.h"
#include "ext/sharp_ui/sharp_scaled.h"
#include "platform/common/platform_window_hooks.h"   // GamePixelDensity
#include "platform/common/display_density.h"         // UiScaleRatio: the boot prewarm's UI scale
#include "Game/GameSettings.h"                       // g_gameSettings->uiScale (the boot prewarm)
#include "Game/exe_bodies.h"                         // g_gameSettings
#include "ext/registry.h"                            // Val().sharp_ui_icons
#include "ext/resources/Resources.h"   // HasProvider: the in-exe font variants (KITFREE)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "collection/Collection.h"
#include "io/FileSystem.h"
#include "object/External.h"
#include "system/Filename.h"
#include "ui/Font.h"
#include "ui/SharpDraw.h"
#include "ui/SpriteContext.h"
#include "ui/SpriteSheet.h"
#include "ui/UserInterface.h"
#include "video/CompanionData.h"
#include "video/Context.h"
#include "video/Device.h"

namespace Ext {
namespace SharpUI {

namespace {

// The game's four fonts (the kit's resource/Font/*.rsc) and the face they are
// compiled from. A font whose face is not this one (a translation mod's) keeps
// the retail draw: our variants would be a different typeface.
constexpr const char* kFontNames[] = { "Font", "FontSmall", "FontMedium", "FontLarge" };
constexpr int kFontCount = 4;
constexpr const char* kFace = "FrancophilSans";

constexpr int kMaxSwaps = 64;

// One installed twin and the tables its SharpSheetSwap points into.
struct Twin {
    std::vector<int32_t>             twinSprite;
    std::vector<UI::SharpGlyphPlace> place;
};

struct State {
    // The key the selection was made for.
    bool     valid = false;
    float    scale = 0.0f;
    int32_t  bbW = 0, bbH = 0;
    int32_t  icons = -1;   // ext.sharp_ui_icons the selection was made with
    int32_t  generation = 0;
    // The selection.
    UI::SharpSheetSwap swaps[kMaxSwaps];
    Twin               twins[kMaxSwaps];
    int32_t            swapCount = 0;
    char               fontText[kFontCount][32];   // the trace's "Font->32" parts
    int32_t            twin2x = 0;
    int32_t            scaled = 0;   // the dialog sheet's factor (sharp_scaled.h), 0 = retail's
    // Engage detection: the context frame of the last wrapped draw.
    bool     drewBefore = false;
    int32_t  lastFrame = 0;
};
State g;

void BackBuffer(int32_t& w, int32_t& h) {
    const Video::Device* const dev = Video::Device::_singleton;
    w = dev != nullptr ? dev->backBufferWidth : 0;
    h = dev != nullptr ? dev->backBufferHeight : 0;
}

int32_t FactoryGeneration() {
    const Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    return f != nullptr ? f->_generation : 0;
}

// A compiled resource that is present LOOSE in the data directory (where
// tools/ext_resources.sh deploys ours). Checked before FindOrLoad: a missing
// resource is a loader warning (and, in the kit's tools, a modal assert), and
// "not deployed" is an expected state, not an error.
bool LooseCompiledExists(const char* path) {
    IO::FileSystem* const fs = IO::FileSystem::_singleton;
    if (fs == nullptr) return false;
    // GetCompiledPath wants the "File.rsc:name" form and returns the FULL
    // path under the data directory; a top-level resource is named "resource"
    // (Font/SharpFont_32.rsc -> <data>/Font/SharpFont_32_resource.crs, the
    // file tools/ext_resources.sh deploys). The first deployed run looked for
    // "<data>/<data>/Font/SharpFont_32_.crs" and every variant fell back.
    std::string named = path;
    if (named.find(':') == std::string::npos) named += ":resource";
    const System::Filename compiled =
        Object::ExternalFactory::GetCompiledPath(System::Filename(named.c_str()));
    // PlatformExists, not Exists: Exists gates the platform stat behind
    // _platformExistsGate, which is off during play (filesystem_startup.cpp sets
    // only the packfile gate), so a loose file always read as absent there.
    return compiled._data != nullptr && fs->PlatformExists(compiled);
}

template <class T>
T* LoadAs(const char* path) {
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr) return nullptr;
    T* p = f->FindOrLoadAs<T>(System::Filename(path));
    return Object::IsDerivedFromNullSafe(p, T::TypeId()) ? p : nullptr;
}

bool SameFace(const UI::Font* font) {
    return font->_string20._data != nullptr && std::strcmp(font->_string20._data, kFace) == 0;
}

UI::SharpSheetSwap* NewSwap() {
    if (g.swapCount >= kMaxSwaps) return nullptr;
    Twin& t = g.twins[g.swapCount];
    t.twinSprite.clear();
    t.place.clear();
    g.swaps[g.swapCount] = UI::SharpSheetSwap{};
    return &g.swaps[g.swapCount];
}

// One of the four fonts: pick the variant for `pxPerUnit`, load it, and build
// its glyph table keyed by the BASE glyph (= sprite) index through the
// codepoint tables of both fonts. Writes the trace text either way.
void SelectFont(int k, float pxPerUnit) {
    char* const text = g.fontText[k];
    char path[96];
    std::snprintf(path, sizeof path, "Font/%s.rsc", kFontNames[k]);
    UI::Font* const base = LoadAs<UI::Font>(path);
    if (base == nullptr) { std::snprintf(text, 32, "%s->retail(no font)", kFontNames[k]); return; }
    if (!SameFace(base)) { std::snprintf(text, 32, "%s->retail(face)", kFontNames[k]); return; }

    const int height = base->_lineHeight;
    bool keepRetail = false;
    const int pick = PickFontHeight(height, pxPerUnit, Platform::Window::GamePixelDensity(), &keepRetail);

    // The native pick (the wanted height within half a pixel of the font's
    // own) is the font swapped for ITSELF: its glyphs still go one texel per
    // device pixel through the word run, instead of retail's quad with its
    // corners snapped, which at 99 % stretched a 13-texel glyph into 13 or 14
    // pixels (SHARP-SPACING.md).
    UI::Font* variant = base;
    if (keepRetail) {
        // Ratio beyond even the Retina range: swapping the font for itself
        // would draw base-size glyphs one texel per device pixel, too small and
        // letter-spaced. Keep retail's scaled quads. (A Retina 2x frame picks a
        // provider-built variant above; with no typeface that load fails and
        // lands in "retail(bad)" -- the same soft scaled quads.)
        std::snprintf(text, 32, "%s->retail(range)", kFontNames[k]);
        return;
    }
    if (pick != height) {
        // A variant exists when it is deployed loose OR a resource provider
        // builds it in the exe (KITFREE: Ext::Resources::InitFonts).
        if (!VariantFontPath(kFontNames[k], pick, path, sizeof path) ||
            (!LooseCompiledExists(path) && !Ext::Resources::HasProvider(path))) {
            std::snprintf(text, 32, "%s->retail(no %d)", kFontNames[k], pick);
            return;
        }
        variant = LoadAs<UI::Font>(path);
    }
    UI::SpriteSheet* const baseSheet = base->_spriteSheet.Get();
    UI::SpriteSheet* const varSheet = variant != nullptr ? variant->_spriteSheet.Get() : nullptr;
    if (variant == nullptr || varSheet == nullptr || baseSheet == nullptr ||
        !SameFace(variant) || variant->_lineHeight != pick) {
        std::snprintf(text, 32, "%s->retail(bad %d)", kFontNames[k], pick);
        return;
    }
    UI::SharpSheetSwap* const swap = NewSwap();
    if (swap == nullptr) { std::snprintf(text, 32, "%s->retail(full)", kFontNames[k]); return; }
    Twin& t = g.twins[g.swapCount];

    const int32_t count = baseSheet->m_sprites._size;
    t.twinSprite.assign(static_cast<size_t>(count), -1);
    t.place.assign(static_cast<size_t>(count), UI::SharpGlyphPlace{});
    for (int32_t cp = 0; cp < base->_charToGlyph._size; ++cp) {
        const int32_t gb = base->GetSpriteIndex(cp);
        const int32_t gv = variant->GetSpriteIndex(cp);
        if (gb < 0 || gb >= count || gb >= base->_glyphs._size) continue;
        if (gv < 0 || gv >= varSheet->m_sprites._size || gv >= variant->_glyphs._size) continue;
        const UI::Font::Kerning& eb = base->_glyphs._data[gb];
        const UI::Font::Kerning& ev = variant->_glyphs._data[gv];
        const UI::SpriteInfo& sv = varSheet->m_sprites._data[gv];
        t.twinSprite[static_cast<size_t>(gb)] = gv;
        t.place[static_cast<size_t>(gb)] = MakeGlyphPlace(
            GlyphMetrics{eb._preAdvance, eb._postKern, eb._postAdvance, eb._offsetY},
            GlyphMetrics{ev._preAdvance, ev._postKern, ev._postAdvance, ev._offsetY},
            sv.width, sv.height, cp == ' ');
    }
    int32_t unmapped = 0;
    for (int32_t v : t.twinSprite) if (v < 0) ++unmapped;

    swap->base       = baseSheet;
    swap->twin       = varSheet;
    swap->twinSprite = t.twinSprite.data();
    swap->place      = t.place.data();
    swap->count      = count;
    ++g.swapCount;
    if (pick == height)     std::snprintf(text, 32, "%s->native", kFontNames[k]);
    else if (unmapped == 0) std::snprintf(text, 32, "%s->%d", kFontNames[k], pick);
    else                    std::snprintf(text, 32, "%s->%d(-%d)", kFontNames[k], pick, unmapped);
}

bool AlreadySwapped(const UI::SpriteSheet* sheet) {
    for (int32_t i = 0; i < g.swapCount; ++i)
        if (g.swaps[i].base == sheet) return true;
    return false;
}

// Every loaded sprite sheet with a deployed `<name>2x.rsc` twin, its sprites
// matched by name. Candidates are collected before anything loads: a load
// changes the factory's tree, which FindNext walks.
void SelectTwins2x() {
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr) return;
    struct Candidate { UI::SpriteSheet* sheet; std::string twinPath; };
    std::vector<Candidate> found;
    for (UI::SpriteSheet* s = f->FindNextAs<UI::SpriteSheet>(UI::SpriteSheet::TypeId(), nullptr);
         s != nullptr; s = f->FindNextAs<UI::SpriteSheet>(UI::SpriteSheet::TypeId(), s)) {
        if (AlreadySwapped(s)) continue;
        const System::Filename path = s->GetPath();
        char twin[256];
        if (!Twin2xPath(path._data, twin, sizeof twin)) continue;
        if (!LooseCompiledExists(twin)) continue;
        found.push_back(Candidate{s, twin});
    }
    for (const Candidate& c : found) {
        UI::SpriteSheet* const twin = LoadAs<UI::SpriteSheet>(c.twinPath.c_str());
        if (twin == nullptr) continue;
        UI::SharpSheetSwap* const swap = NewSwap();
        if (swap == nullptr) return;
        Twin& t = g.twins[g.swapCount];
        std::unordered_map<std::string, int32_t> byName;
        for (int32_t i = 0; i < twin->m_sprites._size; ++i) {
            const char* n = twin->m_sprites._data[i].name._data;
            if (n != nullptr) byName.emplace(n, i);
        }
        const int32_t count = c.sheet->m_sprites._size;
        t.twinSprite.assign(static_cast<size_t>(count), -1);
        int32_t matched = 0;
        for (int32_t i = 0; i < count; ++i) {
            const char* n = c.sheet->m_sprites._data[i].name._data;
            if (n == nullptr) continue;
            const auto it = byName.find(n);
            if (it == byName.end()) continue;
            t.twinSprite[static_cast<size_t>(i)] = it->second;
            ++matched;
        }
        if (matched != count) {
            // An incomplete twin would blank the sprites it lacks: not installed.
            std::fprintf(stderr, "[SharpUI] 2x twin %s: %d of %d sprites, not used\n",
                         c.twinPath.c_str(), matched, count);
            continue;
        }
        swap->base       = c.sheet;
        swap->twin       = twin;
        swap->twinSprite = t.twinSprite.data();
        swap->place      = nullptr;
        swap->count      = count;
        ++g.swapCount;
        ++g.twin2x;
    }
}

// The sheets we have art for (sharp_scaled.h: the game's dialog sheet, our
// Extensions sheet) drawn from their copy at `factor`: our art over the sheet
// upscaled, the base UVs. A deployed twin wins.
void SelectScaledSheets(int factor) {
    Object::ExternalFactory* const f = Object::ExternalFactory::_singleton;
    if (f == nullptr || factor < 2) return;
    for (UI::SpriteSheet* s = f->FindNextAs<UI::SpriteSheet>(UI::SpriteSheet::TypeId(), nullptr);
         s != nullptr; s = f->FindNextAs<UI::SpriteSheet>(UI::SpriteSheet::TypeId(), s)) {
        if (AlreadySwapped(s)) continue;
        const System::Filename path = s->GetPath();
        const SheetKind kind = KindOfSheet(path._data);
        if (kind == SheetKind::None) continue;
        Graphics::MaterialInstance* const material = ScaledSheetMaterial(s, kind, factor);
        if (material == nullptr) continue;
        UI::SharpSheetSwap* const swap = NewSwap();
        if (swap == nullptr) return;
        swap->base     = s;
        swap->count    = s->m_sprites._size;
        swap->material = material;
        ++g.swapCount;
        g.scaled = factor;
    }
}

void Select(const DrawChoice& c) {
    g.swapCount = 0;
    g.twin2x = 0;
    g.scaled = 0;
    for (int k = 0; k < kFontCount; ++k) {
        if (c.snap) SelectFont(k, c.pxPerUnitY);
        else std::snprintf(g.fontText[k], 32, "%s->retail(frame)", kFontNames[k]);
    }
    // The sprite sheets only at "Text + icons" (ext.sharp_ui_icons, the Enhanced tab's Sharp UI).
    const bool icons = Val().sharp_ui_icons != 0;
    if (icons && c.snap && WantTwin2x(c.pxPerUnitX, c.pxPerUnitY)) SelectTwins2x();
    if (icons && c.snap) SelectScaledSheets(PickSheetScale(c.pxPerUnitX, c.pxPerUnitY));
    DropSeenJobs();
}

}  // namespace

void Draw(UI::UserInterface& ui, Video::Context& context) {
    int32_t bbW, bbH;
    BackBuffer(bbW, bbH);
    const DrawChoice c = ChooseDraw(ui._uiScale, bbW, bbH);
    const int32_t generation = FactoryGeneration();

    // Engaged = the first wrapped frame after a frame this did not wrap (the
    // toggle came on, or the loop changed).
    const int32_t frame = context._frameIndex;
    const bool engaged = !g.drewBefore || (frame != g.lastFrame && frame != g.lastFrame + 1);
    g.drewBefore = true;
    g.lastFrame = frame;

    const bool scaleChanged = !g.valid || ui._uiScale != g.scale || bbW != g.bbW || bbH != g.bbH ||
                              Val().sharp_ui_icons != g.icons;
    // A sheet copy finished on its worker (sharp_scaled.h): select again to install it.
    const bool built = ScaledSheetJobFinished();
    if (scaleChanged || generation != g.generation || built) {
        // The factory's Clean (a loop change) frees what it evicts and bumps
        // the generation: every pointer in the selection is re-resolved.
        Select(c);
        g.valid = true;
        g.scale = ui._uiScale;
        g.bbW = bbW;
        g.bbH = bbH;
        g.icons = Val().sharp_ui_icons;
        g.generation = FactoryGeneration();   // after our own loads
    }

    // TRACE FIRST (EYECAM §11): which path ran, once per engage / scale change.
    if (engaged || scaleChanged) {
        std::fprintf(stderr, "[SharpUI] on scale=%.4f px/unit=%.4f fonts: %s %s %s %s 2x=%d sheet=%d\n",
                     static_cast<double>(ui._uiScale), static_cast<double>(c.pxPerUnitY),
                     g.fontText[0], g.fontText[1], g.fontText[2], g.fontText[3], g.twin2x, g.scaled);
    }

    UI::SharpDraw sharp;
    sharp.pxPerUnitX  = c.pxPerUnitX;
    sharp.pxPerUnitY  = c.pxPerUnitY;
    sharp.unitsPerPxX = c.pxPerUnitX > 0.0f ? 1.0f / c.pxPerUnitX : 0.0f;
    sharp.unitsPerPxY = c.pxPerUnitY > 0.0f ? 1.0f / c.pxPerUnitY : 0.0f;
    sharp.point       = c.point;
    sharp.swaps       = g.swaps;
    sharp.swapCount   = g.swapCount;

    // Each Flush picks its batch's sheet / sampler (SpriteContext::Flush,
    // ui/SharpDraw.h). The point flag is false on entry; on exit it is put back
    // to false and the slots are marked for rebind, because
    // Context::BindTextures binds only dirty slots and the next pass would
    // otherwise keep a point sampler.
    if (c.snap) UI::SpriteContext::_extSharp = &sharp;

    ui.Draw(context);   // retail's UserInterface::Draw (0x180121890), unchanged

    UI::SpriteContext::_extSharp = nullptr;
    if (Video::ImageBufferData::_extPointSampling) {
        constexpr uint32_t kAllImageSlots = (1u << RtVideo::kImageBufferSlots) - 1u;
        Video::ImageBufferData::_extPointSampling = false;
        context._imageBufferDirty |= kAllImageSlots;
        context._dirty |= RtVideo::kDirtyImageBuffers;
    }
}

void OnVideoLost() {
    DropScaledSheets();
    g.valid = false;   // the swaps named the dropped materials: select again before the next draw
}

void Prewarm() {
    if (Val().sharp_ui_icons == 0 || g_gameSettings == nullptr) return;
    int32_t bbW, bbH;
    BackBuffer(bbW, bbH);
    if (bbW <= 0 || bbH <= 0) return;
    // The scale the menu will draw at: GameContainer::ApplyKeyMapToUI's (MenuDLT::Startup).
    const float uiScale = Platform::Display::UiScaleRatio(g_gameSettings->uiScale);
    const DrawChoice c = ChooseDraw(uiScale, bbW, bbH);
    if (!c.snap) return;
    PrewarmScaledSheets(PickSheetScale(c.pxPerUnitX, c.pxPerUnitY));
}

void PrewarmAndWait() {
    Prewarm();
    WaitForScaledSheets(2000);
}

}  // namespace SharpUI
}  // namespace Ext
