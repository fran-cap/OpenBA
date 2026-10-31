// src/ext/family_tree/family_raster.cpp -- see family_raster.h
#include "ext/family_tree/family_raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>

namespace Ext {
namespace FamilyTree {

namespace {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

Rgb Hex(const char* s) {   // "#rrggbb"
    const unsigned long long v = std::strtoull(s + 1, nullptr, 16);
    Rgb c;
    c.r = static_cast<uint8_t>((v >> 16) & 0xFF);
    c.g = static_cast<uint8_t>((v >> 8) & 0xFF);
    c.b = static_cast<uint8_t>(v & 0xFF);
    return c;
}

// ---- text -------------------------------------------------------------------------------------------------

// UTF-8 to code points; a bad byte is a '?'.
std::vector<int32_t> Decode(const std::string& s) {
    std::vector<int32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        int32_t c = b;
        size_t extra = 0;
        if (b >= 0xF0) { c = b & 0x07; extra = 3; }
        else if (b >= 0xE0) { c = b & 0x0F; extra = 2; }
        else if (b >= 0xC0) { c = b & 0x1F; extra = 1; }
        else if (b >= 0x80) { c = '?'; }
        if (extra > 0 && i + extra >= s.size()) { out.push_back('?'); break; }   // cut short
        for (size_t k = 1; k <= extra; ++k) c = (c << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
        i += extra + 1;
        out.push_back(c);
    }
    return out;
}

class SystemGlyphSource final : public GlyphSource {
public:
    bool Glyphs(int cellPx, const std::vector<int32_t>& cps, Resources::RasterFont& out,
                std::string* why) override {
        size_t n = 0;
        const char* const* faces = Resources::DefaultFaces(n);
        std::string last;
        for (size_t i = 0; i < n; ++i) {
            std::string w;
            if (Resources::RasterizeFont(faces[i], cellPx, cps.data(), static_cast<int32_t>(cps.size()), out,
                                         &w)) return true;
            last = w;
        }
        if (why) *why = last.empty() ? std::string("no font") : last;
        return false;
    }
};

// A size of the font: the glyphs by code point.
struct SizedFont {
    int cell = 0;
    Resources::RasterFont font;
    std::unordered_map<int32_t, size_t> at;
    bool ok = false;
};

// ---- drawing ----------------------------------------------------------------------------------------------

class Painter {
public:
    explicit Painter(PictureRaster& r) : r_(r) {}

    void Blend(int x, int y, Rgb c, float a) {
        if (x < 0 || y < 0 || x >= r_.width || y >= r_.height || a <= 0.0f) return;
        if (a > 1.0f) a = 1.0f;
        const size_t pixel = static_cast<size_t>(y) * static_cast<size_t>(r_.width) + static_cast<size_t>(x);
        uint8_t* p = &r_.rgba[pixel * 4u];
        p[0] = static_cast<uint8_t>(std::lround(p[0] * (1.0f - a) + c.r * a));
        p[1] = static_cast<uint8_t>(std::lround(p[1] * (1.0f - a) + c.g * a));
        p[2] = static_cast<uint8_t>(std::lround(p[2] * (1.0f - a) + c.b * a));
    }

    // An axis-aligned rectangle, its edges covering a pixel as far as they reach into it.
    void Rect(float x0, float y0, float x1, float y1, Rgb c) {
        const int ix0 = std::max(0, static_cast<int>(std::floor(x0))), ix1 = std::min(r_.width,
                static_cast<int>(std::ceil(x1)));
        const int iy0 = std::max(0, static_cast<int>(std::floor(y0))), iy1 = std::min(r_.height,
                static_cast<int>(std::ceil(y1)));
        for (int y = iy0; y < iy1; ++y) {
            const float cy = std::min(static_cast<float>(y + 1), y1) - std::max(static_cast<float>(y), y0);
            for (int x = ix0; x < ix1; ++x) {
                const float cx = std::min(static_cast<float>(x + 1), x1) - std::max(static_cast<float>(x),
                        x0);
                Blend(x, y, c, cx * cy);
            }
        }
    }

    void Dot(float cx, float cy, float radius, Rgb c) {
        for (int y = static_cast<int>(std::floor(cy - radius - 1));
             y <= static_cast<int>(std::ceil(cy + radius + 1)); ++y) {
            for (int x = static_cast<int>(std::floor(cx - radius - 1));
                 x <= static_cast<int>(std::ceil(cx + radius + 1)); ++x) {
                const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                Blend(x, y, c, std::clamp(radius + 0.5f - d, 0.0f, 1.0f));
            }
        }
    }

    // A rounded box with a fill and a border centered on its edge; `dash` > 0 breaks the straight parts of
    // the border into dashes `dash` long with gaps of 3/5 of that (the corners stay solid).
    void Box(float x, float y, float w, float h, float radius, Rgb fill, Rgb border, float borderW,
             float dash) {
        const float hx = w / 2, hy = h / 2, cx = x + hx, cy = y + hy;
        const float pad = borderW + 1.0f;
        for (int py = std::max(0, static_cast<int>(std::floor(y - pad))); py < std::min(r_.height,
                static_cast<int>(std::ceil(y + h + pad))); ++py) {
            for (int px = std::max(0, static_cast<int>(std::floor(x - pad))); px < std::min(r_.width,
                    static_cast<int>(std::ceil(x + w + pad))); ++px) {
                const float qx = std::fabs(px + 0.5f - cx) - (hx - radius);
                const float qy = std::fabs(py + 0.5f - cy) - (hy - radius);
                const float d = std::hypot(std::max(qx, 0.0f),
                                           std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
                Blend(px, py, fill, std::clamp(0.5f - d, 0.0f, 1.0f));
                float a = std::clamp(borderW / 2 + 0.5f - std::fabs(d), 0.0f, 1.0f);
                if (a > 0.0f && dash > 0.0f) {
                    // the straight stretches of the four edges carry the dashes
                    float along = -1.0f;
                    if (qx <= 0.0f && qy > 0.0f) along = px + 0.5f - x;           // the top and bottom edges
                    else if (qy <= 0.0f && qx > 0.0f) along = py + 0.5f - y;      // the left and right edges
                    if (along >= 0.0f && std::fmod(along, dash * 1.6f) > dash) a = 0.0f;
                }
                Blend(px, py, border, a);
            }
        }
    }

private:
    PictureRaster& r_;
};

}  // namespace

GlyphSource& SystemGlyphs() {
    static SystemGlyphSource s;
    return s;
}

bool RenderPictureRaster(const Tree& tree, const PictureLayout& lay, const PictureOptions& opt,
                         GlyphSource& glyphs,
                         PictureRaster& out) {
    out = PictureRaster();
    if (lay.boxes.empty()) {
        out.why = "nobody to draw";
        return false;
    }
    const PictureCanvas canvas = CanvasFor(lay);

    // the scale: whole pixels per unit, or less when the picture would be too big
    double scale = 1.0;
    scale = std::min(scale, static_cast<double>(kRasterMaxSide) / std::max(canvas.width, canvas.height));
    scale = std::min(scale, std::sqrt(static_cast<double>(kRasterMaxPixels) /
                                  (static_cast<double>(canvas.width) *
                                   static_cast<double>(canvas.height))));
    const float S = static_cast<float>(scale);
    out.scale = scale;
    out.width = std::max(1, static_cast<int>(std::ceil(canvas.width * scale)));
    out.height = std::max(1, static_cast<int>(std::ceil(canvas.height * scale)));
    out.rgba.assign(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4u, 255);

    Painter paint(out);
    const Rgb ink = Hex("#262626"), deadInk = Hex("#6a6a6a"), grey = Hex("#555555"),
              lightGrey = Hex("#666666");

    // ---- the words: collect them, and get the glyphs of each size they are drawn at ----
    struct Word {
        std::string text;
        float x, baseline;   // in units; x is the left edge, or the center when `center`
        float em;
        bool bold, center;
        Rgb color;
    };
    std::vector<Word> words;
    const bool named = !opt.village.empty();
    words.push_back({PictureTitle(opt), static_cast<float>(kPictureMargin), 44.0f, 28.0f, true, false,
                     Hex("#2b2b2b")});
    if (named) words.push_back({PictureKind(opt), static_cast<float>(kPictureMargin), 68.0f, 16.0f, true,
                                false, grey});
    words.push_back({PictureSubtitle(lay, opt), static_cast<float>(kPictureMargin), named ? 90.0f : 72.0f,
                     14.0f, false, false,
                     lightGrey});
    for (const PictureBox& b : lay.boxes) {
        const Person* p = tree.Find(b.id);
        if (p == nullptr) continue;
        const float y = static_cast<float>(canvas.RowY(b.gen)), cx = b.x + b.w / 2.0f;
        const Rgb c = p->Alive() ? ink : deadInk;
        words.push_back({PictureName(*p), cx, y + 20, 13.0f, true, true, c});
        words.push_back({PictureArrival(*p), cx, y + 35, 10.5f, false, true, c});
        const std::string death = PictureDeath(*p);
        if (!death.empty()) words.push_back({death, cx, y + 48, 10.5f, false, true, c});
        if (!b.job.empty()) words.push_back({b.job, cx, y + (death.empty() ? 48.0f : 62.0f), 10.5f, false,
                                             true, c});
    }
    const float ly = static_cast<float>(canvas.height - kPictureLegend + 26);
    const float m = static_cast<float>(kPictureMargin);
    words.push_back({"woman", m + 34, ly, 12.0f, false, false, grey});
    words.push_back({"man", m + 144, ly, 12.0f, false, false, grey});
    words.push_back({"has died", m + 234, ly, 12.0f, false, false, grey});
    words.push_back({"marriage", m + 364, ly, 12.0f, false, false, grey});
    words.push_back({"Each family's line, from its parents down to its children, has one color.", m + 450, ly,
                     12.0f, false,
                     false, grey});

    std::set<int32_t> wanted;
    for (const Word& w : words)
        for (const int32_t c : Decode(w.text)) wanted.insert(c);
    std::vector<int32_t> cps(wanted.begin(), wanted.end());
    const auto cellFor = [&](float em) { return static_cast<int>(std::lround(em * S * 1.33f)); };
    constexpr int kMinCell = 5;

    std::unordered_map<int, SizedFont> fonts;
    bool asciiOnly = false;   // a face that does not hold every character gets '?' for the rest
    std::string why;
    const auto fontAt = [&](int cell) -> SizedFont* {
        if (cell < kMinCell || cell > 256) return nullptr;
        SizedFont& f = fonts[cell];
        if (f.cell != 0) return f.ok ? &f : nullptr;
        f.cell = cell;
        std::vector<int32_t> ask = cps;
        if (asciiOnly) {
            for (int32_t& c : ask) if (c < 32 || c > 126) c = '?';
            std::sort(ask.begin(), ask.end());
            ask.erase(std::unique(ask.begin(), ask.end()), ask.end());
        }
        f.ok = glyphs.Glyphs(cell, ask, f.font, &why);
        if (!f.ok && !asciiOnly) {
            asciiOnly = true;
            std::vector<int32_t> plain;
            for (const int32_t c : cps) plain.push_back(c < 32 || c > 126 ? '?' : c);
            std::sort(plain.begin(), plain.end());
            plain.erase(std::unique(plain.begin(), plain.end()), plain.end());
            f.ok = glyphs.Glyphs(cell, plain, f.font, &why);
        }
        if (f.ok) {
            for (size_t i = 0; i < f.font.glyphs.size(); ++i) f.at[f.font.glyphs[i].codepoint] = i;
        }
        return f.ok ? &f : nullptr;
    };

    // the names are the text that matters: if their size is a real size and no face gives it, there is no PNG
    const int nameCell = cellFor(13.0f);
    if (nameCell >= kMinCell && nameCell <= 256 && fontAt(nameCell) == nullptr) {
        out.why = why.empty() ? std::string("no font") : why;
        out.rgba.clear();
        return false;
    }

    const auto glyphFor = [&](SizedFont& f, int32_t c) -> const Resources::RasterGlyph* {
        auto it = f.at.find(c);
        if (it == f.at.end() && (c < 32 || c > 126)) it = f.at.find('?');
        if (it == f.at.end() && asciiOnly) it = f.at.find('?');
        return it == f.at.end() ? nullptr : &f.font.glyphs[it->second];
    };

    // ---- the lines, under the boxes ----
    const std::vector<PictureStroke> strokes = BuildPictureStrokes(lay);
    const auto line = [&](const PictureSegment& g, float widthUnits, Rgb c) {
        const float hw = std::max(widthUnits * S, 1.0f) / 2;
        const float x0 = std::min(g.x0, g.x1) * S, x1 = std::max(g.x0, g.x1) * S;
        const float y0 = std::min(g.y0, g.y1) * S, y1 = std::max(g.y0, g.y1) * S;
        paint.Rect(x0 - hw, y0 - hw, x1 + hw, y1 + hw, c);
    };
    for (const PictureStroke& s : strokes)
        if (s.color < 0)
            for (const PictureSegment& g : s.segments) line(g, 2.5f, Hex("#4a4a4a"));
    for (const PictureStroke& s : strokes) {
        if (s.color < 0) continue;
        const Rgb c = Hex(PictureColor(s.color));
        for (const PictureSegment& g : s.segments) line(g, 2.2f, c);
        for (const auto& d : s.dots) paint.Dot(d.first * S, d.second * S, std::max(3.2f * S, 1.0f), c);
    }

    // ---- the people ----
    const std::vector<uint32_t> outlined = PictureHighlighted(tree, opt);
    for (const PictureBox& b : lay.boxes) {
        const Person* p = tree.Find(b.id);
        if (p == nullptr) continue;
        const bool dead = !p->Alive();
        const bool mark = std::find(outlined.begin(), outlined.end(), b.id) != outlined.end();
        const Rgb fill = Hex(dead ? "#efefef" : (p->gender == 0 ? "#fde8ef" : "#e4eefb"));
        const Rgb edge = Hex(mark ? PictureHighlightColor()
                             : (dead ? "#9b9b9b" : (p->gender == 0 ? "#c4718b" : "#6d95c8")));
        const float edgeW = mark ? static_cast<float>(kPictureHighlightWidth) : 1.6f;
        paint.Box(b.x * S, canvas.RowY(b.gen) * S, b.w * S, lay.boxHeight * S, 6.0f * S, fill, edge,
                  std::max(edgeW * S, 1.0f),
                  dead ? 5.0f * S : 0.0f);
    }
    // the key's samples
    {
        const float y = (ly - 11) * S;
        paint.Box(m * S, y, 26 * S, 14 * S, 3 * S, Hex("#fde8ef"), Hex("#c4718b"), std::max(S, 1.0f), 0);
        paint.Box((m + 110) * S, y, 26 * S, 14 * S, 3 * S, Hex("#e4eefb"), Hex("#6d95c8"), std::max(S, 1.0f),
                  0);
        paint.Box((m + 200) * S, y, 26 * S, 14 * S, 3 * S, Hex("#efefef"), Hex("#9b9b9b"), std::max(S, 1.0f),
                  5.0f * S);
        line({static_cast<int>(m) + 330, static_cast<int>(ly) - 4, static_cast<int>(m) + 356,
              static_cast<int>(ly) - 4}, 2.5f,
             Hex("#4a4a4a"));
    }

    // ---- the words, over everything ----
    for (const Word& w : words) {
        SizedFont* f = fontAt(cellFor(w.em));
        if (f == nullptr) continue;
        const std::vector<int32_t> text = Decode(w.text);
        int total = 0;
        for (const int32_t c : text) {
            const Resources::RasterGlyph* g = glyphFor(*f, c);
            if (g != nullptr) total += g->preAdvance + g->postAdvance + g->postKern;
        }
        int pen = static_cast<int>(std::lround(w.x * S)) - (w.center ? total / 2 : 0);
        const int base = static_cast<int>(std::lround(w.baseline * S));
        const bool bold = w.bold && f->cell >= 12;
        for (const int32_t c : text) {
            const Resources::RasterGlyph* g = glyphFor(*f, c);
            if (g == nullptr) continue;
            const int gx = pen + g->preAdvance, gy = base - g->offsetY - g->height;
            for (int pass = 0; pass < (bold ? 2 : 1); ++pass) {
                for (int yy = 0; yy < g->height; ++yy) {
                    for (int xx = 0; xx < g->width; ++xx) {
                        const size_t glyphRow = static_cast<size_t>(yy) * static_cast<size_t>(g->width);
                        const uint8_t a = g->alpha[glyphRow + static_cast<size_t>(xx)];
                        if (a != 0) paint.Blend(gx + xx + pass, gy + yy, w.color, a / 255.0f);
                    }
                }
            }
            pen += g->preAdvance + g->postAdvance + g->postKern;
            out.textDrawn = out.textDrawn || g->width > 0;
        }
    }
    return true;
}

}  // namespace FamilyTree
}  // namespace Ext
