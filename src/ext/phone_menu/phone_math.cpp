// ============================================================================
// src/ext/phone_menu/phone_math.cpp -- ext.phone_menu's pure decisions
// ============================================================================
// The rules are in phone_math.h. Standard C++ only.
// ============================================================================

#include "phone_math.h"

namespace Ext {
namespace PhoneMenu {

int32_t SafeMargin(int32_t height) {
    if (height <= 0) return 0;
    return static_cast<int32_t>(static_cast<float>(height) * kSafeMarginFraction + 0.5f);
}

Rect InsetLayout(int32_t width, int32_t height) {
    int32_t m = SafeMargin(height);
    if (2 * m >= width || 2 * m >= height) m = 0;
    return Rect{m, m, width - m, height - m};
}

bool WiderThanBackdrop(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH) {
    if (width <= 0 || height <= 0 || spriteW <= 0 || spriteH <= 0) return false;
    const float screenAspect = static_cast<float>(width) / static_cast<float>(height);
    const float spriteAspect = static_cast<float>(spriteW) / static_cast<float>(spriteH);
    return !(spriteAspect > screenAspect);
}

int32_t BackdropHeight(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH) {
    if (!WiderThanBackdrop(width, height, spriteW, spriteH)) return height;
    const float spriteAspect = static_cast<float>(spriteW) / static_cast<float>(spriteH);
    const int32_t half = static_cast<int32_t>(static_cast<float>(width) / spriteAspect + 0.5f) >> 1;
    return 2 * half;
}

Rect LogoTop(const Rect& retail) {
    if (retail.y1 >= 0) return retail;
    return Rect{retail.x1, 0, retail.x2, retail.y2 - retail.y1};
}

int32_t LogoBottom(int32_t backdropHeight) {
    if (backdropHeight <= 0) return -1;
    return static_cast<int32_t>(static_cast<float>(backdropHeight) * kLogoBottomFraction + 0.5f);
}

int32_t SquareSide(const int32_t* labelWidths, const int32_t* labelHeights, int32_t count) {
    int32_t side = 0;
    for (int32_t i = 0; i < count; ++i) {
        if (labelWidths[i] > side) side = labelWidths[i];
        if (labelHeights[i] > side) side = labelHeights[i];
    }
    return side > 0 ? side + 2 * kSquareInset : 0;
}

Rect RowBand(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop) {
    Rect band = InsetLayout(width, height);
    if (logoBottom > band.y1) band.y1 = logoBottom;
    if (versionTop >= 0 && versionTop - kVersionGap < band.y2) band.y2 = versionTop - kVersionGap;
    if (band.y2 < band.y1) band.y2 = band.y1;
    return band;
}

int32_t BlockHeight(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop) {
    const Rect band = RowBand(width, height, logoBottom, versionTop);
    return band.y2 - band.y1;
}

int32_t RowsWidth(const RowMetrics& m, int32_t perRow, int32_t side) {
    if (perRow <= 0) return m.outerWidth;
    return m.outerWidth + perRow * (side + m.buttonPadWidth) + (perRow - 1) * m.spacing;
}

int32_t RowsHeight(const RowMetrics& m, int32_t rows, int32_t side) {
    if (rows <= 0) return m.outerHeight;
    return m.outerHeight + rows * (side + m.buttonPadHeight) + (rows - 1) * m.spacing;
}

RowPlan PlanRows(int32_t width, int32_t height, const RowMetrics& m, int32_t logoBottom, int32_t versionTop) {
    RowPlan plan;
    if (m.count <= 0 || m.contentSide <= 0) return plan;
    const Rect safe = InsetLayout(width, height);
    const int32_t safeW = safe.x2 - safe.x1;
    const int32_t block = BlockHeight(width, height, logoBottom, versionTop);
    const int32_t cap = static_cast<int32_t>(static_cast<float>(safe.y2 - safe.y1) * kRowSideFraction);
    for (int32_t rows = 1; rows <= 2 && rows <= m.count; ++rows) {
        const int32_t perRow = (m.count + rows - 1) / rows;
        const int32_t across = (safeW - m.outerWidth - (perRow - 1) * m.spacing) / perRow - m.buttonPadWidth;
        const int32_t down = (block - m.outerHeight - (rows - 1) * m.spacing) / rows - m.buttonPadHeight;
        int32_t side = across < down ? across : down;
        if (side > cap) side = cap;
        if (side < m.contentSide) {
            // The cap alone may be under the labels: they still get their side if it fits.
            if (m.contentSide > across || m.contentSide > down) continue;
            side = m.contentSide;
        }
        plan.rows = rows;
        plan.perRow = perRow;
        plan.side = side;
        return plan;
    }
    return plan;
}

Rect MenuBand(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop,
              int32_t columnHeight) {
    const Rect safe = InsetLayout(width, height);
    if (columnHeight <= 0) return safe;
    int32_t bottom = safe.y2;
    if (versionTop > safe.y1 && versionTop - kVersionGap < bottom) bottom = versionTop - kVersionGap;
    int32_t top = safe.y1;
    if (logoBottom > top) top = logoBottom;
    if (columnHeight <= bottom - top) return Rect{safe.x1, top, safe.x2, bottom};
    // Taller than the safe band above the version line but not than the screen above it:
    // DialogInstance::ComputeLayout keeps the column's bottom at `bottom` and lets its top
    // rise into the margin, so the buttons never run under the version line.
    if (columnHeight <= bottom) return Rect{safe.x1, safe.y1, safe.x2, bottom};
    return safe;
}

bool VersionAside(int32_t width, int32_t height, int32_t versionTop, int32_t versionWidth,
                  int32_t columnWidth, int32_t columnHeight) {
    if (versionTop <= 0 || versionWidth <= 0 || columnWidth <= 0 || columnHeight <= 0) return false;
    if (columnHeight <= versionTop - kVersionGap) return false;
    const int32_t besideColumn = (width - columnWidth) / 2;
    return SafeMargin(height) + versionWidth + kVersionGap <= besideColumn;
}

Rect VersionAsideLayout(int32_t width, int32_t height, int32_t versionWidth) {
    const Rect safe = InsetLayout(width, height);
    return Rect{safe.x1, safe.y1, safe.x1 + versionWidth, safe.y2};
}

int32_t VersionTop(int32_t width, int32_t height, int32_t versionHeight) {
    if (versionHeight <= 0) return -1;
    return InsetLayout(width, height).y2 - versionHeight;
}

namespace {

// One axis of SafeArea: {near, far} for an extent with margin `m`.
void SafeAxis(int32_t extent, int32_t m, int32_t upperLeft, int32_t last, int32_t& nearEdge,
              int32_t& farEdge) {
    nearEdge = 0;
    farEdge = extent;
    if (last > extent - 2 * m) return;
    if (upperLeft < m) nearEdge = m - upperLeft;
    farEdge = extent - m;
}

}  // namespace

Rect SafeArea(int32_t width, int32_t height, int32_t upperLeftX, int32_t upperLeftY, int32_t lastWidth,
              int32_t lastHeight) {
    const Rect safe = InsetLayout(width, height);
    const int32_t m = safe.x1;
    if (m == 0) return safe;
    Rect r{};
    SafeAxis(width, m, upperLeftX, lastWidth, r.x1, r.x2);
    SafeAxis(height, m, upperLeftY, lastHeight, r.y1, r.y2);
    return r;
}

Rect SafeBounds(int32_t width, int32_t height, int32_t dialogWidth, int32_t dialogHeight) {
    const Rect safe = InsetLayout(width, height);
    const int32_t m = safe.x1;
    Rect r{0, 0, width, height};
    if (m == 0) return r;
    if (dialogWidth <= width - 2 * m) {
        r.x1 = m;
        r.x2 = width - m;
    }
    if (dialogHeight <= height - 2 * m) {
        r.y1 = m;
        r.y2 = height - m;
    }
    return r;
}

void LaidOutUpperLeft(int32_t alignment, const Rect& root, int32_t leftPad, int32_t topPad, int32_t& upperLeftX,
                      int32_t& upperLeftY) {
    if (root.x2 <= root.x1 && root.y2 <= root.y1) return;
    if (alignment < 0 || alignment > 8) return;
    if (alignment % 3 == 0) upperLeftX = root.x1 - leftPad;
    if (alignment / 3 == 0) upperLeftY = root.y1 - topPad;
}

int32_t IconSide(int32_t side, int32_t labelHeight, int32_t iconHeight) {
    if (side <= 0 || labelHeight <= 0 || iconHeight <= 0) return iconHeight;
    const int32_t room = side - 2 * kSquareInset - (labelHeight - iconHeight);
    int32_t icon = static_cast<int32_t>(static_cast<float>(side) * kIconFraction + 0.5f);
    if (icon > room) icon = room;
    // A whole multiple of the sprite: every sprite pixel covers the same number of layout units,
    // so the scaled-up 16-pixel icons keep even edges instead of uneven blocks.
    icon = icon / iconHeight * iconHeight;
    return icon > iconHeight ? icon : iconHeight;
}

RowMetrics PauseMenuMetrics() {
    RowMetrics m;
    m.count = 10;
    m.contentSide = 68 + 2 * kSquareInset;
    m.outerWidth = 16;
    m.outerHeight = 16;
    m.buttonPadWidth = 4;
    m.buttonPadHeight = 4;
    m.spacing = 2;
    return m;
}

int32_t UiScaleFloor(int32_t backBufferWidth, int32_t backBufferHeight, const RowMetrics& menu,
                     int32_t spriteW, int32_t spriteH, int32_t versionHeight) {
    if (!WiderThanBackdrop(backBufferWidth, backBufferHeight, spriteW, spriteH)) return 0;
    for (int32_t percent = 1; percent <= 100; ++percent) {
        // UserInterface::Update's layout size: (int)(back buffer * _uiScale), _uiScale = percent / 100.
        const float scale = static_cast<float>(percent) / 100.0f;
        const int32_t w = static_cast<int32_t>(static_cast<float>(backBufferWidth) * scale);
        const int32_t h = static_cast<int32_t>(static_cast<float>(backBufferHeight) * scale);
        const int32_t logo = LogoBottom(BackdropHeight(w, h, spriteW, spriteH));
        if (PlanRows(w, h, menu, logo, VersionTop(w, h, versionHeight)).rows > 0) return (percent + 4) / 5 * 5;
    }
    return 100;
}

float SliderMinimum(float platformMinPercent, int32_t floorPercent) {
    if (floorPercent > 0) return static_cast<float>(floorPercent);
    if (platformMinPercent < kUiScaleMinPercent) return platformMinPercent;
    return kUiScaleMinPercent;
}

}  // namespace PhoneMenu
}  // namespace Ext
