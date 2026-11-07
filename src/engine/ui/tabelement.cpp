// ============================================================================
// src/engine/ui/tabelement.cpp — UI::TabElement, the whole class
// ============================================================================
//
// `??_7TabElement@UI@@6B@` (retail 0x180157790):
//
//   0  vector deleting dtor  0x18004ca90  -> deleting_dtor.cpp row (this batch)
//   1  TypeName              0x18004c700  here
//   2  Type                  0x18004c6e0  here
//   4  ComputeSize           0x1800d9ea0  here
//   5  ComputeLayout         0x1800d9b20  here
//   6  Draw                  0x1800d98b0  here
//   7  Update                0x1800d9810  here
//
// plus ??0TabElement (0x1800da390), ??1TabElement (0x1800da360) and the
// private `?UpdateContainers@TabElement@UI@@AEAAXXZ` (0x1800d9710), which IS
// this file's: it is not on the UI non-slot callee list and
// src/engine/ui/TabElement.h's note that it is "NOT reimplemented here"
// was true of Wave 7 lane E and is not any more.
//
// NOT HERE: `?AddTab@TabElement@UI@@` (0x1800da130) — already ported, by
// src/engine/ui/tabelement_addtab.cpp. `?SetActiveTab@` (0x18004c890) and
// `?GetActiveTab@` (0x18004c880) are lane E's, in widgets_layout.cpp.
//
// LAYOUT and the paired child list: src/engine/ui/TabElement.h (sizeof
// 0x108; RULE B6-8 moved this file's offsets there under the retail name).
//
// ENDGAME B.3.6 block C5: the six slot bodies and UpdateContainers MOVED into
// the members of the real `UI::TabElement`; each registered door is a
// one-line QUALIFIED delegation. The ctor / dtor stay doors (B4b-2).
// ============================================================================

#include <new>

#include "widget_common.h"
#include "ui/TabElement.h"
#include "ui/SpriteContext.h"

using namespace rtui;

namespace {

constexpr int kTabCount     = 0xC0;
constexpr int kGap          = 0xC4;
constexpr int kEdgeLeft     = 0xC8;
constexpr int kEdgeMiddle   = 0xD8;
constexpr int kEdgeRight    = 0xE8;
constexpr int kActiveTab    = 0xF8;
constexpr int kTabBarHeight = 0xFC;
constexpr int kTabBarWidth  = 0x100;

static_assert(kActiveTab == UI::TabElementOffsets::kActiveTab, "");
static_assert(kTabCount     == offsetof(UI::TabElement, _tabCount), "");
static_assert(kGap          == offsetof(UI::TabElement, _gap), "");
static_assert(kEdgeLeft     == offsetof(UI::TabElement, _edgeLeft), "");
static_assert(kEdgeMiddle   == offsetof(UI::TabElement, _edgeMiddle), "");
static_assert(kEdgeRight    == offsetof(UI::TabElement, _edgeRight), "");
static_assert(kTabBarHeight == offsetof(UI::TabElement, _tabBarHeight), "");
static_assert(kTabBarWidth  == offsetof(UI::TabElement, _tabBarWidth), "");


}  // namespace

namespace UI {

// ---------------------------------------------------------------------------
// ?UpdateContainers@TabElement@UI@@AEAAXXZ   0x1800d9710
//
// Hide every container but the active one, and lay the active one out inside
// the page frame. The "dirty walk if bit 0 flipped" guard is Element's.
// ---------------------------------------------------------------------------
void TabElement::UpdateContainers() {
    const int32_t n = _tabCount;
    for (int32_t i = 0; i < n; ++i) {
        Element* c = _children.GetData()[GetContainerIndex(i)];
        const uint32_t old = c->_flags;
        if (i == _activeTab) {
            c->_flags = old & ~UI::kElementFlagHidden;
            if ((old & UI::kElementFlagHidden) != 0) DirtyWalk(c);
            const int32_t gap = _gap;
            Element* cc = _children.GetData()[GetContainerIndex(i)];
            cc->ComputeLayout(gap + _layoutLeft,
                              _tabBarHeight + gap * 2 + _layoutTop,
                              _layoutRight  - gap,
                              _layoutBottom - gap);                      // slot 5
        } else {
            c->_flags = old | UI::kElementFlagHidden;
            if ((((old | UI::kElementFlagHidden) ^ old) & UI::kElementFlagHidden) != 0) DirtyWalk(c);
        }
    }
}

// ---------------------------------------------------------------------------
// [4] ?ComputeSize@TabElement@UI@@UEAAXXZ   0x1800d9ea0
// ---------------------------------------------------------------------------
void TabElement::ComputeSize() {
    if ((_flags & UI::kElementFlagNeedsSizeCalc) == 0) return;

    _tabBarWidth  = _gap;
    _tabBarHeight = 0;

    int32_t maxW = 0, maxH = 0;

    const int32_t n = _tabCount;
    for (int32_t i = 0; i < n; ++i) {
        Element* button = _children.GetData()[GetButtonIndex(i)];
        if ((button->_flags & UI::kElementFlagHidden) != 0) continue;

        int32_t bw = 0, bh = 0;
        InlineGetPaddedSize(button, &bw, &bh);
        if (bh < _tabBarHeight) bh = _tabBarHeight;
        _tabBarHeight = bh;
        _tabBarWidth = _tabBarWidth + _gap + bw;

        int32_t cw = 0, ch = 0;
        InlineGetPaddedSize(_children.GetData()[GetContainerIndex(i)], &cw, &ch);
        if (maxW <= cw) maxW = cw;
        if (maxH <= ch) maxH = ch;
    }

    _flags &= ~UI::kElementFlagNeedsSizeCalc;
    _layoutHeight = _gap * 3 + _tabBarHeight + maxH;
    _layoutWidth  = maxW + _gap * 2;
}

// ---------------------------------------------------------------------------
// [5] ?ComputeLayout@TabElement@UI@@UEAAXHHHH@Z   0x1800d9b20
// ---------------------------------------------------------------------------
void TabElement::ComputeLayout(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (_border != nullptr)
        _border->ComputeLayout(x, y, right, bottom);

    ComputeSize();   // slot 4, VIRTUAL — `CALL [RAX+0x20]` in retail

    int32_t w = _layoutWidth;
    int32_t h = _layoutHeight;
    const int32_t align = _alignment;

    if (right  < w + x) x = right  - w;
    if (bottom < h + y) y = bottom - h;

    int32_t px = x + _leftPad;
    int32_t py = y + _topPad;
    const int32_t aw = (right  - _rightPad)  - px;
    const int32_t ah = (bottom - _bottomPad) - py;

    if ((_flags & UI::kElementFlagAlignArg1) != 0) w = aw;
    if ((_flags & UI::kElementFlagAlignArg2) != 0) h = ah;

    switch (align) {
        case ElementDescription::TopCenter:
        case ElementDescription::MidCenter:
        case ElementDescription::BottomCenter:
            px = ((aw >> 1) - (w >> 1)) + px;
            break;
        case ElementDescription::TopRight:
        case ElementDescription::MidRight:
        case ElementDescription::BottomRight:
            px = (right - _rightPad) - w;
            break;
        default: break;
    }
    if (align > ElementDescription::TopRight) {
        if (align < ElementDescription::BottomLeft)
            py = ((ah >> 1) - (h >> 1)) + py;
        else if (align <= ElementDescription::BottomRight)
            py = (bottom - _bottomPad) - h;
    }

    const int32_t rx = px + w;
    const int32_t by = py + h;
    _layoutLeft   = px;
    _layoutRight  = rx;
    _layoutTop    = py;
    _layoutBottom = by;

    const int32_t barBottom = _tabBarHeight + _gap + py;

    // The array is RE-READ before each call (slot 5, virtual).
    _children.GetData()[0]->ComputeLayout(px, py, _tabBarWidth + px, barBottom);
    _children.GetData()[1]->ComputeLayout(px, barBottom, rx, by);
    {
        const int32_t gap = _gap;
        _children.GetData()[2]->ComputeLayout(gap + px, gap + barBottom, rx - gap, by - gap);
    }

    int32_t cursor = _gap + px;
    const int32_t n = _tabCount;
    for (int32_t i = 0; i < n; ++i) {
        Element* button = _children.GetData()[GetButtonIndex(i)];
        if ((button->_flags & UI::kElementFlagHidden) != 0) continue;

        int32_t bw = 0, bh = 0;
        InlineGetPaddedSize(button, &bw, &bh);

        button = _children.GetData()[GetButtonIndex(i)];
        button->ComputeLayout(cursor, _gap + py, bw + cursor, _gap + bh + py);
        cursor = cursor + _gap + bw;
    }

    {
        Element* active = _children.GetData()[GetContainerIndex(_activeTab)];
        const int32_t gap = _gap;
        active->ComputeLayout(gap + px, barBottom + gap, rx - gap, by - gap);
    }
}

// ---------------------------------------------------------------------------
// [6] ?Draw@TabElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x1800d98b0
//
// The three edge sprites are drawn with the element's RAW colour — no
// ComputeColor, no dim test. Retail passes `[RDI+0x38]` straight through.
// ---------------------------------------------------------------------------
void TabElement::Draw(SpriteContext& context, uint8_t alpha) const {
    if (_border != nullptr)
        _border->Draw(context, alpha);

    _children.GetData()[0]->Draw(context, alpha);   // slot 6
    _children.GetData()[1]->Draw(context, alpha);
    _children.GetData()[2]->Draw(context, alpha);

    const int32_t n = _tabCount;
    for (int32_t i = 0; i < n; ++i) {
        const Element* button = _children.GetData()[GetButtonIndex(i)];
        if ((button->_flags & UI::kElementFlagHidden) == 0)
            button->Draw(context, alpha);
    }

    const Element* activeBtn = _children.GetData()[_activeTab * 2 + 3];
    const int32_t bl = activeBtn->_layoutLeft;
    const int32_t br = activeBtn->_layoutRight;
    const int32_t y0 = activeBtn->_layoutBottom - 4;
    const int32_t y1 = _children.GetData()[2]->_layoutTop + 4;
    const uint32_t c = _color;

    // UI::SpriteContext::AddSprite (block D's class) — by address, doors stay.
    context.AddSprite(_edgeLeft, bl, y0, _edgeLeft.Width() + bl, y1, c);

    const int32_t rw = _edgeRight.Width();
    const int32_t lw = _edgeLeft.Width();
    context.AddSprite(_edgeMiddle, bl + lw, y0, br - rw, y1, c);

    context.AddSprite(_edgeRight, br - _edgeRight.Width(), y0, br, y1, c);

    _children.GetData()[GetContainerIndex(_activeTab)]->Draw(context, alpha);
}

// ---------------------------------------------------------------------------
// [7] ?Update@TabElement@UI@@ ...   0x1800d9810
// ---------------------------------------------------------------------------
void TabElement::Update(const System::Time& time, UserInterface* ui,
                        Object::InternalPtr<DialogInstance>* dialog,
                        const UpdateState& state) {
    // The base Update, QUALIFIED (this object's slot 7 is this very body), with
    // the handle copied into a temporary whose ADDRESS is passed — retail's shape.
    Object::InternalPtr<DialogInstance> slot = *dialog;
    Element::Update(time, ui, &slot, state);

    const int32_t before = _activeTab;
    const int32_t n = _tabCount;
    for (int32_t i = 0; i < n; ++i) {
        const Element* button = _children.GetData()[GetButtonIndex(i)];
        if ((button->_flags & UI::kElementFlagHidden) == 0 &&
            (button->_flags & UI::kElementFlagWasActivated) != 0) {
            _activeTab = i;
            break;
        }
    }
    if (before == _activeTab) return;
    UpdateContainers();
}

// ??1TabElement@UI@@UEAA@XZ   0x1800da360 — retail's body is the restamp and
// the tail into ~Element: the compiler's chain is all of it.
TabElement::~TabElement() {}

// ?CreateNewObject@TabElement@UI@@SAPEAVBase@Object@@XZ 0x18004c7f0 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x108, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* TabElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TabElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) TabElement();
}
// ?CreateInPlaceObject@TabElement@UI@@SAPEAVBase@Object@@PEAX@Z 0x18004c870
Object::Base* TabElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) TabElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::TabElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@TabElement@UI@@ (0x1801fa468): name "TabElement", parent
// "Element" (retail ?_ParentTypeName@TabElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::TabElement, "Element", nullptr)
