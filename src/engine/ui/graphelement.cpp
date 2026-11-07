// ============================================================================
// src/engine/ui/graphelement.cpp — UI::GraphElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7GraphElement@UI@@6B@` (retail 0x1801569d0).
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x1800408b0  deleting_dtor.cpp row
//    1    TypeName              0x180040460  here
//    2    Type                  0x180040440  here
//    4    ComputeSize           0x1800d1810  here
//    5    ComputeLayout         0x1800d16b0  here
//    6    Draw                  0x1800d1360  here
//
// plus ??0GraphElement@UI@@QEAA@XZ         0x1800d1880
//      ??0GraphElement@UI@@QEAA@AEBV01@@Z  0x1800406d0
//      ??1GraphElement@UI@@UEAA@XZ         0x1800d1840
//
// `?AddGraph@`, `?AddGraphPoint@` and `?RemoveGraphs@` are ALREADY PORTED in
// src/engine/ui/widgets_layout.cpp (Waves 7/9); the series record layout is in
// src/engine/ui/GraphElement.h. This file adds only the vtable rows.
//
// ENDGAME B.3.6 block C5: the five slot bodies MOVED into the members of the
// real `UI::GraphElement` (src/engine/ui/GraphElement.h, which now names the
// fields this banner used to list as offsets); each registered door is a
// one-line QUALIFIED delegation. The ctor / copy-ctor / dtor stay doors (B4b-2).
//
// `??0GraphElement@UI@@QEAA@XZ` (0x1800d1880) is NOT here: it is already
// ported and registered, table-driven, by src/engine/ui/factory_ui1.cpp
// (the factory roster). One address, one owner.
// ============================================================================
#include <new>

#include "element_common.h"
#include "ui/GraphElement.h"
#include "ui/SpriteContext.h"

using namespace rtuielem;

namespace {

constexpr int kSeries    = UI::GraphElementOffsets::kGraphsData;    // 0xf0
constexpr int kSeriesCnt = UI::GraphElementOffsets::kGraphsCount;   // 0xf8

static_assert(kSeries == 0xf0 && kSeriesCnt == 0xf8,
              "GraphElement.h must still pin the series Array at +0xf0");

// The two exported DATA slots, in namespace-scope atomics (RULE B6-5: the
// members below may run on the loader thread).

// UI::SpriteContext::AddLine(ctx, const Sprite&, x0, y0, x1, y1, color) — block
// D's class, reached by address (its door stays a door).


// 0x180161694 and 0x18016169c, read out of the DLL's float pool for this batch.
constexpr float kHalf = 0.5f;
constexpr float kOne  = 1.0f;

}  // namespace

namespace UI {

// ============================================================================
// [4] ?ComputeSize@GraphElement@UI@@UEAAXXZ   0x1800d1810
// ============================================================================
// A graph is exactly the size it was told to be. Note the store ORDER: the flag
// clear sits BETWEEN the two size stores (0x1800d1826), not after them.
void GraphElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;
    _layoutWidth = _width;
    _flags &= ~UI::kElementFlagNeedsSizeCalc;
    _layoutHeight = _height;
}

// ============================================================================
// [5] ?ComputeLayout@GraphElement@UI@@UEAAXHHHH@Z   0x1800d16b0
// ============================================================================
void GraphElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ElementComputeAlignedRect(this, x, y, w, h);
}

// ============================================================================
// [6] ?Draw@GraphElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1800d1360
// ============================================================================
// Frame, grid, series — all of it `SpriteContext::AddLine` (0x18011cbe0) with
// the one line sprite at +0xd0.
//
// FOUR THINGS THE BYTES SAY THAT A REWRITE WOULD GET WRONG:
//
//  1. The grid loops run `divisions + 1` times (`while (i <= _divs)`), so a
//     graph with 4 horizontal divisions draws 5 lines — both edges included.
//     They are guarded by `if (_divs >= 0)`, so a NEGATIVE division count draws
//     nothing while ZERO still draws one line at the top edge.
//  2. The grid step is recomputed by ADDING `(far - near) / divs` to a running
//     float, not by multiplying the index. Float error accumulates, and it
//     accumulates the same way here.
//  3. Each grid coordinate is `(int)(running + 0.5f)` — a truncation of the
//     biased value, not a round-half-even.
//  4. The X grid lines are drawn BELOW the frame (from `bottom` to
//     `bottom + tick`) and the Y grid lines extend LEFT of it (from
//     `left - tick`), i.e. the ticks hang outside the box on two sides only.
//
// The series scale `(1.0f / _yRange) * _layoutHeight` is computed ONCE,
// before the loop, and each point is `bottom - (int)(value * scale)`.
void GraphElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (_border != nullptr)
        _border->Draw(context, alpha);

    const uint32_t base = _color;
    alpha = DimAlpha(this, alpha);
    const uint32_t c = rtuielem::ComputeColor(base, alpha);   // NOT the (unported) member

    const UI::Sprite& sprite = _lineSprite;   // ?AddLine@SpriteContext@UI@@ 0x18011cbe0

    const int L = _layoutLeft;
    const int T = _layoutTop;
    const int R = _layoutRight;
    const int B = _layoutBottom;

    context.AddLine(sprite, L, T, L, B, c);
    context.AddLine(sprite, L, T, R, T, c);
    context.AddLine(sprite, L, B, R, B, c);
    context.AddLine(sprite, R, T, R, B, c);

    // ---- horizontal grid, `_yDivisions + 1` lines, ticks to the LEFT -------
    {
        const int y0 = T;
        const int divs = _yDivisions;
        const int y1 = B;
        float run = static_cast<float>(y0);
        if (divs >= 0) {
            for (int i = 0; i <= _yDivisions; ++i) {
                const int yy = static_cast<int>(run + kHalf);
                context.AddLine(sprite, L - _tickSize, yy, R, yy, c);
                run += static_cast<float>(y1 - y0) / static_cast<float>(divs);
            }
        }
    }

    // ---- vertical grid, `_xDivisions + 1` lines, ticks BELOW ---------------
    {
        const int x0 = L;
        const int divs = _xDivisions;
        const int x1 = R;
        float run = static_cast<float>(x0);
        if (divs >= 0) {
            for (int i = 0; i <= _xDivisions; ++i) {
                const int xx = static_cast<int>(run + kHalf);
                context.AddLine(sprite, xx, B, xx, _tickSize + B, c);
                run += static_cast<float>(x1 - x0) / static_cast<float>(divs);
            }
        }
    }

    // ---- the series --------------------------------------------------------
    const float scale = (kOne / _yRange) * static_cast<float>(_layoutHeight);

    const int32_t seriesCount = _seriesCount;
    for (int32_t s = 0; s < seriesCount; ++s) {
        // LEFT: _seriesData stays void* (widgets_layout.cpp, R-UI-b, views it as bytes).
        const GraphSeries* rec = static_cast<const GraphSeries*>(_seriesData) + s;   // stride 0x18
        const int width = _layoutWidth;
        const int segments = rec->points._size - 1;                  // rec+0x10
        float xf = static_cast<float>(L);
        for (int64_t p = 0; p < segments; ++p) {
            const int32_t* pts = rec->points._data;                  // rec+0x08, re-read
            const float xn = xf + static_cast<float>(width) / static_cast<float>(segments);
            context.AddLine(sprite,
                 static_cast<int>(xf),
                 B - static_cast<int>(static_cast<float>(pts[p]) * scale),
                 static_cast<int>(xn),
                 B - static_cast<int>(static_cast<float>(pts[p + 1]) * scale),
                 rec->colour);                                        // rec+0x00
            xf = xn;
        }
    }
}

// ??1GraphElement@UI@@UEAA@XZ  0x1800d1840 — retail's body, moved from the
// retired door (ENDGAME B.3.9 UI-B): destroy the series array (0x1800416d0,
// the same helper `?RemoveGraphs@` tail-calls). The entry restamp and the
// tail into ~Element are the compiler's.
GraphElement::~GraphElement() {
    rtcoll::RtColl_ArrayDtorElem_1800416d0(&_seriesData);           // +0xf0
}

// ?CreateNewObject@GraphElement@UI@@SAPEAVBase@Object@@XZ 0x180040550 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x100, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* GraphElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GraphElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) GraphElement();
}
// ?CreateInPlaceObject@GraphElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x1800405d0
Object::Base* GraphElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) GraphElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::GraphElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@GraphElement@UI@@ (0x1801f9fd0): name "GraphElement", parent
// "Element" (retail ?_ParentTypeName@GraphElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::GraphElement, "Element", nullptr)
