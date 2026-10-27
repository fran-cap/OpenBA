// ============================================================================
// src/ext/phone_menu/phone_menu.cpp -- ext.phone_menu (class A, live)
// ============================================================================
// The rules are in phone_menu.h; the arithmetic is phone_math.cpp. The one engine seam is
// UI::UserInterface::_extDialogPlace (layout on the main thread, the backdrop on whichever thread
// draws the UI). The layout hook decides from THIS frame's sizes: it sizes the menu's tree (and
// the version line's) itself before retail's ComputeLayout does, so a menu's first drawn frame is
// already its final layout. A dialog that is not a menu is laid out in phone_math.h's SafeArea.
//
// THE BUTTON ROW. On a screen wider than the menus' backdrop a menu's button list (MainMenu.rsc
// `buttonList`; Pause.rsc's root; RibbonElements laid out top-to-bottom) is turned into a
// horizontal row of square buttons (phone_math.h PlanRows): the ribbon's `_vertical` cleared,
// every button's minimum size set to one square side, its label centred with the icon above the
// text. Tried in order: one row, two rows (the first ceil(n / 2) squares, the rest centred under
// them), both again in the small font (Font/FontSmall.rsc), else retail's column. Two rows come
// from padding (ShapeTwoRows: the ribbon sizes the squares as two rows) and a move after
// retail's ComputeLayout (WrapRows, at the seam's kExtPlaceAfterLayout point). The values
// are the menu INSTANCE's (the description is untouched); the hook keeps retail's values and
// puts them back whenever the rows do not apply (the row switched off, a screen they do not
// fit). Main thread only.
// ============================================================================

#include "ext/phone_menu/phone_menu.h"
#include "ext/phone_menu/phone_math.h"
#include "ext/phone_menu/loading_screen.h"   // PlaceLoading (P9)
#include "ext/phone_menu/touch_fit.h"
#include "ext/phone_menu/touch_math.h"
#include "ext/phone_menu/touch_size.h"   // SyncTouchSize (P11)
#include "ext/registry.h"
#include "ext/touch_mode/touch_mode.h"   // ext.touch_mode places its dialogs first (docs/android/TOUCH_MODE.md)

#include <atomic>
#include <cstdio>
#include <cstring>

#include "object/External.h"
#include "system/Filename.h"
#include "ui/DialogInstance.h"
#include "ui/Font.h"
#include "ui/LabelElement.h"
#include "ui/RibbonElement.h"
#include "ui/SpriteSheet.h"
#include "ui/UserInterface.h"
#include "video/Device.h"   // Video::Device::_singleton: the back buffer the UI scale floor is for
#include "platform/common/display_density.h"   // Platform::Display::RenderPercent (P9)
#include "platform/common/ui_render_scale.h"   // ScreenExtent: the floor is in screen units (P9)

namespace Ext {
namespace PhoneMenu {

namespace {

// The descriptions of the dialogs the row moves, once each has been opened: the main menu
// (Dialog/MainMenu.rsc), the pause menu (Dialog/Pause.rsc) and the version line
// (Dialog/BuildId.rsc). Resources live for the run, so a recorded pointer stays valid; written
// on the main thread, read by the draw as well.
constexpr int kMaxDialogs = 3;
std::atomic<const UI::Dialog*> g_menus[kMaxDialogs] = {};
std::atomic<const UI::Dialog*> g_pauses[kMaxDialogs] = {};
std::atomic<const UI::Dialog*> g_buildIds[kMaxDialogs] = {};

// This frame's facts, written by the menu's layout and read by the version line's, which
// UserInterface::Update lays out after it (the version line is the menu's child dialog, added
// after it). Main thread only.
bool g_rowActive = false;
bool g_versionAside = false;
bool g_versionLow = false;    // P9: the backdrop's bottom border is drawn, the version line sits in it
int32_t g_versionWidth = -1;

void Record(std::atomic<const UI::Dialog*>* set, const UI::Dialog* dialog) {
    if (dialog == nullptr) return;
    for (int i = 0; i < kMaxDialogs; ++i) {
        const UI::Dialog* cur = set[i].load(std::memory_order_relaxed);
        if (cur == dialog) return;
        if (cur == nullptr) {
            set[i].store(dialog, std::memory_order_relaxed);
            return;
        }
    }
}

bool In(const std::atomic<const UI::Dialog*>* set, const UI::Dialog* dialog) {
    if (dialog == nullptr) return false;
    for (int i = 0; i < kMaxDialogs; ++i) {
        if (set[i].load(std::memory_order_relaxed) == dialog) return true;
    }
    return false;
}

void Store(const Rect& r, int32_t* rect) {
    rect[0] = r.x1;
    rect[1] = r.y1;
    rect[2] = r.x2;
    rect[3] = r.y2;
}

// ---- the menu instance's retail values and the row ------------------------

constexpr int kMaxButtons = 16;

struct RowState {
    const UI::DialogInstance* dialog = nullptr;   // the menu instance the values below belong to
    UI::RibbonElement* ribbon = nullptr;          // its `buttonList`
    uint8_t vertical = 1;                         // retail's values, as captured
    int32_t ribbonRightPad = 0;
    int32_t count = 0;
    UI::Element* buttons[kMaxButtons] = {};
    UI::LabelElement* labels[kMaxButtons] = {};
    int32_t minWidth[kMaxButtons] = {};
    int32_t minHeight[kMaxButtons] = {};
    int32_t buttonPads[kMaxButtons][4] = {};      // the button's left / top / right / bottom padding
    int32_t alignment[kMaxButtons] = {};          // the label's place in its button
    int32_t inner[kMaxButtons] = {};              // the icon / text pair's alignment (_align)
    uint32_t mode[kMaxButtons] = {};              // the icon's side of the text (_mode)
    UI::Font* font[kMaxButtons] = {};
    int32_t pads[kMaxButtons][4] = {};            // the label's left / top / right / bottom padding
    int32_t iconSize[kMaxButtons][2] = {};        // the label's icon size override (_iconWidth / _iconHeight)
    UI::Font* smallFont = nullptr;                // Font/FontSmall.rsc, when the row needs it
    bool applied = false;
    bool decided = false;                         // the row / column decision for width x height
    int32_t width = 0;
    int32_t height = 0;
    int32_t versionHeight = -1;
    RowPlan plan;                                 // the rows in force (rows 0: the column)
    RowMetrics metrics;                           //   and what they were planned from
    bool small = false;                           //   in the small font
};
RowState g_row;

UI::Element* FindNamed(UI::Element* e, const char* name, int depth) {
    if (e == nullptr || depth > 6) return nullptr;
    if (e->_name != nullptr && std::strcmp(e->_name, name) == 0) return e;
    for (int32_t i = 0; i < e->_children._size; ++i) {
        UI::Element* hit = FindNamed(e->_children._data[i], name, depth + 1);
        if (hit != nullptr) return hit;
    }
    return nullptr;
}

UI::LabelElement* LabelOf(UI::Element* button) {
    for (int32_t i = 0; i < button->_children._size; ++i) {
        UI::Element* child = button->_children._data[i];
        if (::Object::IsDerivedFromNullSafe(child, UI::LabelElement::TypeId()))
            return static_cast<UI::LabelElement*>(child);
    }
    return nullptr;
}

UI::Font* LoadSmallFont() {
    Object::ExternalFactory* const factory = Object::ExternalFactory::_singleton;
    if (factory == nullptr) return nullptr;
    UI::Font* font = factory->FindOrLoadAs<UI::Font>(System::Filename("Font/FontSmall.rsc"));
    return ::Object::IsDerivedFromNullSafe(font, UI::Font::TypeId()) ? font : nullptr;
}

bool Visible(int32_t i) {
    return (g_row.buttons[i]->_flags & UI::kElementFlagHidden) == 0;
}

// The first layout of a menu instance: keep its retail values.
void Capture(const UI::DialogInstance* dialog, UI::Element* root) {
    g_row = RowState();
    g_row.dialog = dialog;
    // The main menu's buttons are its `buttonList`; the pause menu's root is the list itself.
    UI::Element* list = FindNamed(root, "buttonList", 0);
    if (list == nullptr) list = root;
    if (!::Object::IsDerivedFromNullSafe(list, UI::RibbonElement::TypeId())) return;
    UI::RibbonElement* ribbon = static_cast<UI::RibbonElement*>(list);
    for (int32_t i = 0; i < ribbon->_children._size && g_row.count < kMaxButtons; ++i) {
        UI::Element* button = ribbon->_children._data[i];
        if (button == nullptr) continue;
        UI::LabelElement* label = LabelOf(button);
        if (label == nullptr) continue;
        const int32_t n = g_row.count++;
        g_row.buttons[n] = button;
        g_row.labels[n] = label;
        g_row.minWidth[n] = button->_minWidth;
        g_row.minHeight[n] = button->_minHeight;
        g_row.buttonPads[n][0] = button->_leftPad;
        g_row.buttonPads[n][1] = button->_topPad;
        g_row.buttonPads[n][2] = button->_rightPad;
        g_row.buttonPads[n][3] = button->_bottomPad;
        g_row.alignment[n] = label->_alignment;
        g_row.inner[n] = label->_align;
        g_row.mode[n] = label->_mode;
        g_row.font[n] = label->_font;
        g_row.pads[n][0] = label->_leftPad;
        g_row.pads[n][1] = label->_topPad;
        g_row.pads[n][2] = label->_rightPad;
        g_row.pads[n][3] = label->_bottomPad;
        g_row.iconSize[n][0] = label->_iconWidth;
        g_row.iconSize[n][1] = label->_iconHeight;
    }
    g_row.ribbon = ribbon;
    g_row.vertical = ribbon->_vertical;
    g_row.ribbonRightPad = ribbon->_rightPad;
}

void RestoreButtonPads(int32_t i) {
    UI::Element* button = g_row.buttons[i];
    button->_leftPad = g_row.buttonPads[i][0];
    button->_topPad = g_row.buttonPads[i][1];
    button->_rightPad = g_row.buttonPads[i][2];
    button->_bottomPad = g_row.buttonPads[i][3];
}

void Restore() {
    g_row.plan = RowPlan();
    if (!g_row.applied || g_row.ribbon == nullptr) return;
    g_row.ribbon->_vertical = g_row.vertical;
    g_row.ribbon->_rightPad = g_row.ribbonRightPad;
    for (int32_t i = 0; i < g_row.count; ++i) {
        UI::LabelElement* label = g_row.labels[i];
        g_row.buttons[i]->_minWidth = g_row.minWidth[i];
        g_row.buttons[i]->_minHeight = g_row.minHeight[i];
        RestoreButtonPads(i);
        g_row.buttons[i]->SetNeedsSizeCalc();
        label->_alignment = g_row.alignment[i];
        label->_align = g_row.inner[i];
        label->_mode = g_row.mode[i];
        label->_font = g_row.font[i];
        label->_leftPad = g_row.pads[i][0];
        label->_topPad = g_row.pads[i][1];
        label->_rightPad = g_row.pads[i][2];
        label->_bottomPad = g_row.pads[i][3];
        label->_iconWidth = g_row.iconSize[i][0];
        label->_iconHeight = g_row.iconSize[i][1];
        label->SetNeedsSizeCalc();
    }
    g_row.ribbon->SetNeedsSizeCalc();
    g_row.applied = false;
}

// The row's shape: the ribbon across, every label centred in its button with its icon above
// its text (in `font`, or each label's own when null), every button `side` square, the
// buttons' retail padding.
void Shape(UI::Font* font, int32_t side) {
    g_row.ribbon->_vertical = 0;
    g_row.ribbon->_rightPad = g_row.ribbonRightPad;
    for (int32_t i = 0; i < g_row.count; ++i) {
        UI::LabelElement* label = g_row.labels[i];
        g_row.buttons[i]->_minWidth = side;
        g_row.buttons[i]->_minHeight = side;
        RestoreButtonPads(i);
        g_row.buttons[i]->SetNeedsSizeCalc();
        label->_alignment = 4;   // MidCenter: the label in the middle of its square
        label->_align = 4;       //   and the icon / text pair centred inside the label
        label->_mode = 3;        //   with the icon above the text
        label->_font = font != nullptr ? font : g_row.font[i];
        label->_leftPad = 0;     //   with no padding of its own: the square's frame is the margin
        label->_topPad = 0;
        label->_rightPad = 0;
        label->_bottomPad = 0;
        label->_iconWidth = g_row.iconSize[i][0];   //   its icon at the sprite's size until ScaleIcons
        label->_iconHeight = g_row.iconSize[i][1];
        label->SetNeedsSizeCalc();
    }
    g_row.ribbon->SetNeedsSizeCalc();
    g_row.applied = true;
}

// The icons drawn larger once the square's side is known (phone_math.h IconSide): about
// kIconFraction of the side, the text and the frame's inset still inside the square, the
// sprite scaled up (its aspect kept). The square does not grow: the label stays under its side.
void ScaleIcons(int32_t side) {
    for (int32_t i = 0; i < g_row.count; ++i) {
        UI::LabelElement* label = g_row.labels[i];
        if (!Visible(i) || label->_sprite.spriteIndex < 0) continue;
        const int32_t spriteW = label->_sprite.Width();
        const int32_t spriteH = label->_sprite.Height();
        if (spriteW <= 0 || spriteH <= 0) continue;
        label->ComputeSize();
        const int32_t h = IconSide(side, label->_layoutHeight, spriteH);
        int32_t w = spriteW * h / spriteH;
        if (w > side - 2 * kSquareInset) w = side - 2 * kSquareInset;
        label->_iconWidth = w;
        label->_iconHeight = h;
        label->SetNeedsSizeCalc();
    }
}

// Two rows out of the one row Shape made. The ribbon still lays its children out left to right;
// the padding makes it SIZE them as two rows: a first-row square reserves the second row's
// height under it (its bottom padding), and the ribbon's right padding takes the second row's
// width back off its own, so the dialog (and the panel behind the squares) is as wide as the
// first row. WrapRows then puts the squares in place after retail's ComputeLayout.
void ShapeTwoRows(const RowPlan& plan) {
    int32_t seen = 0;
    int32_t secondWidth = 0;
    for (int32_t i = 0; i < g_row.count; ++i) {
        if (!Visible(i)) continue;
        UI::Element* button = g_row.buttons[i];
        const int32_t padW = g_row.buttonPads[i][0] + g_row.buttonPads[i][2];
        const int32_t padH = g_row.buttonPads[i][1] + g_row.buttonPads[i][3];
        if (seen < plan.perRow) {
            button->_bottomPad = g_row.buttonPads[i][3] + plan.side + padH + g_row.ribbon->_spacing;
            button->SetNeedsSizeCalc();
        } else {
            secondWidth += plan.side + padW + g_row.ribbon->_spacing;
        }
        ++seen;
    }
    g_row.ribbon->_rightPad = g_row.ribbonRightPad - secondWidth;
    g_row.ribbon->SetNeedsSizeCalc();
}

// The visible buttons' labels, sized in their row shape: the square side they need.
int32_t ContentSide(int32_t& visible) {
    int32_t widths[kMaxButtons];
    int32_t heights[kMaxButtons];
    visible = 0;
    for (int32_t i = 0; i < g_row.count; ++i) {
        UI::LabelElement* label = g_row.labels[i];
        if (!Visible(i)) continue;
        label->ComputeSize();
        widths[visible] = label->_layoutWidth + label->_leftPad + label->_rightPad;
        heights[visible] = label->_layoutHeight + label->_topPad + label->_bottomPad;
        ++visible;
    }
    return SquareSide(widths, heights, visible);
}

// The padded size the dialog's root will be laid out at (DialogInstance::ComputeLayout's
// arithmetic, sized now).
void PaddedSize(UI::Element* root, int32_t& w, int32_t& h) {
    root->ComputeSize();
    w = root->_layoutWidth + root->_leftPad + root->_rightPad;
    h = root->_layoutHeight + root->_topPad + root->_bottomPad;
}

// The rows in one font: measured as one row at the labels' side, planned (PlanRows), shaped.
// False when no row fits.
bool ShapeRows(UI::Element* root, UI::Font* font, int32_t width, int32_t height, int32_t logoBottom,
               int32_t versionTop) {
    Shape(font, 0);
    RowMetrics m;
    m.contentSide = ContentSide(m.count);
    g_row.metrics = m;
    g_row.small = font != nullptr;
    if (m.count == 0 || m.contentSide <= 0) return false;
    Shape(font, m.contentSide);
    int32_t w = 0;
    int32_t h = 0;
    PaddedSize(root, w, h);
    for (int32_t i = 0; i < g_row.count; ++i) {
        if (!Visible(i)) continue;
        m.buttonPadWidth = g_row.buttonPads[i][0] + g_row.buttonPads[i][2];
        m.buttonPadHeight = g_row.buttonPads[i][1] + g_row.buttonPads[i][3];
        break;
    }
    m.spacing = g_row.ribbon->_spacing;
    m.outerWidth = w - RowsWidth(m, m.count, m.contentSide);
    m.outerHeight = h - RowsHeight(m, 1, m.contentSide);
    g_row.metrics = m;
    const RowPlan plan = PlanRows(width, height, m, logoBottom, versionTop);
    if (plan.rows == 0) return false;
    Shape(font, plan.side);
    if (plan.rows == 2) ShapeTwoRows(plan);
    ScaleIcons(plan.side);
    g_row.plan = plan;
    return true;
}

// The rows: in the labels' own font (one row, else two), else in the small font (one, else
// two); false when none fits (the column).
bool ApplyRows(UI::Element* root, int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop) {
    if (g_row.ribbon == nullptr || g_row.count == 0) return false;
    if (ShapeRows(root, nullptr, width, height, logoBottom, versionTop)) return true;
    if (g_row.smallFont == nullptr) g_row.smallFont = LoadSmallFont();
    if (g_row.smallFont == nullptr) return false;
    return ShapeRows(root, g_row.smallFont, width, height, logoBottom, versionTop);
}

// After retail's ComputeLayout, for two rows: every square put in its row, both rows centred
// across the safe area, the second under the first (UI::Element::AdjustUpperLeft moves a square
// with its frame and label). The ribbon's own rectangle (and its frame, where it has one: the
// pause menu's) is set to the rows', since its laid-out width still counted the second row.
void WrapRows(int32_t width, int32_t height) {
    const RowPlan& plan = g_row.plan;
    if (plan.rows != 2) return;
    const RowMetrics& m = g_row.metrics;
    UI::Element* first = nullptr;
    int32_t visible = 0;
    for (int32_t i = 0; i < g_row.count; ++i) {
        if (!Visible(i)) continue;
        if (first == nullptr) first = g_row.buttons[i];
        ++visible;
    }
    if (first == nullptr || visible <= plan.perRow) return;
    const Rect safe = InsetLayout(width, height);
    const int32_t centre = safe.x1 + (safe.x2 - safe.x1) / 2;
    const int32_t pitch = plan.side + m.buttonPadWidth + m.spacing;
    const int32_t rowWidth[2] = {plan.perRow * pitch - m.spacing, (visible - plan.perRow) * pitch - m.spacing};
    const int32_t top = first->_layoutTop;
    const int32_t padLeft = first->_leftPad;
    int32_t seen = 0;
    for (int32_t i = 0; i < g_row.count; ++i) {
        if (!Visible(i)) continue;
        const int32_t row = seen < plan.perRow ? 0 : 1;
        const int32_t column = row == 0 ? seen : seen - plan.perRow;
        const int32_t x = centre - rowWidth[row] / 2 + column * pitch + padLeft;
        const int32_t y = top + row * (plan.side + m.buttonPadHeight + m.spacing);
        UI::Element* button = g_row.buttons[i];
        button->AdjustUpperLeft(x - button->_layoutLeft, y - button->_layoutTop);
        // The button's frame is laid out over its padded cell (ElementComputeAlignedRect): a
        // first-row cell reaches over the second row. Put it back around the square.
        if (button->_border != nullptr) {
            button->_border->ComputeLayout(button->_layoutLeft - g_row.buttonPads[i][0],
                                           button->_layoutTop - g_row.buttonPads[i][1],
                                           button->_layoutRight + g_row.buttonPads[i][2],
                                           button->_layoutBottom + g_row.buttonPads[i][3]);
        }
        ++seen;
    }
    UI::RibbonElement* ribbon = g_row.ribbon;
    ribbon->_layoutLeft = centre - rowWidth[0] / 2 - padLeft;
    ribbon->_layoutRight = ribbon->_layoutLeft + rowWidth[0];
    if (ribbon->_border != nullptr) {
        ribbon->_border->ComputeLayout(ribbon->_layoutLeft - ribbon->_leftPad, ribbon->_layoutTop - ribbon->_topPad,
                                       ribbon->_layoutRight + g_row.ribbonRightPad,
                                       ribbon->_layoutBottom + ribbon->_bottomPad);
    }
}

// The version line's dialog in the same UserInterface (the menu's child, laid out after it),
// sized now; false when it is not open.
bool VersionSize(const UI::DialogInstance* menu, int32_t& w, int32_t& h) {
    w = -1;
    h = -1;
    UI::UserInterface* ui = menu->_ui;
    if (ui == nullptr) return false;
    for (auto* node = ui->_dialogs._head; node != nullptr; node = node->next) {
        UI::DialogInstance* d = node->element;
        if (d == nullptr || !In(g_buildIds, d->_dialog)) continue;
        UI::Element* root = d->_root.Get();
        if (root == nullptr) return false;
        PaddedSize(root, w, h);
        return true;
    }
    return false;
}

// ---- the hook ---------------------------------------------------------------

// One stderr line each time the menu's layout is decided (a new menu, a new screen size or UI
// scale): rows or column, and the numbers that decided it.
void LogDecision(bool wide, int32_t width, int32_t height) {
    const RowPlan& p = g_row.plan;
    const RowMetrics& m = g_row.metrics;
    if (p.rows > 0) {
        std::fprintf(stderr,
                     "[PhoneMenu] %dx%d: %d row%s of square buttons (%d buttons, %d on the first row, square %d,"
                     " labels need %d%s)\n",
                     width, height, p.rows, p.rows == 1 ? "" : "s", m.count, p.perRow, p.side, m.contentSide,
                     g_row.small ? ", small font" : "");
        return;
    }
    std::fprintf(stderr, "[PhoneMenu] %dx%d: column (wide %d, %d buttons, labels need %d%s)\n", width, height,
                 wide ? 1 : 0, m.count, m.contentSide, g_row.small ? " in the small font" : "");
}

void PlaceMenu(const UI::DialogInstance* dialog, int32_t width, int32_t height, int32_t* rect) {
    // The layout hook runs where retail's ComputeLayout sizes and lays out this same tree; the
    // seam hands the dialog const because it only reads it on every other path.
    UI::Element* const root = const_cast<UI::Element*>(dialog->_root.Get());
    if (root == nullptr) return;
    if (g_row.dialog != dialog) Capture(dialog, root);

    // The backdrop as Draw will place it (this frame, from the dialog's own picture).
    int32_t logoBottom = -1;
    bool wide = false;
    if (dialog->_spriteSheet != nullptr && dialog->_spriteIndex >= 0) {
        const auto& sprite = dialog->_spriteSheet->m_sprites._data[dialog->_spriteIndex];
        wide = WiderThanBackdrop(width, height, sprite.width, sprite.height);
        logoBottom = LogoBottom(BackdropHeight(width, height, sprite.width, sprite.height));
    }
    int32_t versionW = -1;
    int32_t versionH = -1;
    VersionSize(dialog, versionW, versionH);
    g_versionLow = wide;
    const int32_t versionTop = wide ? LowVersionTop(height, versionH) : VersionTop(width, height, versionH);

    // The rows on a screen wider than the backdrop, when they fit; retail's column otherwise. The
    // shape stays on the instance, so it is only decided again when the layout size changes.
    if (!g_row.decided || g_row.width != width || g_row.height != height || g_row.versionHeight != versionH) {
        g_rowActive = wide && ApplyRows(root, width, height, logoBottom, versionTop);
        if (!g_rowActive) Restore();
        g_row.decided = true;
        g_row.width = width;
        g_row.height = height;
        g_row.versionHeight = versionH;
        LogDecision(wide, width, height);
    }

    int32_t columnW = 0;
    int32_t columnH = 0;
    PaddedSize(root, columnW, columnH);
    g_versionWidth = versionW;
    g_versionAside = !g_rowActive && VersionAside(width, height, versionTop, versionW, columnW, columnH);
    if (g_rowActive) {
        Store(RowBand(width, height, logoBottom, versionTop), rect);
        return;
    }
    Store(MenuBand(width, height, logoBottom, g_versionAside ? -1 : versionTop, columnH), rect);
}

void Place(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height,
           int32_t* rect) {
    if (dialog == nullptr) return;
    // The main thread's points (the backdrop ones come from the UI's draw).
    const bool update = point == UI::UserInterface::kExtPlaceLayout ||
                        point == UI::UserInterface::kExtPlaceAfterLayout ||
                        point == UI::UserInterface::kExtPlaceUpperLeft;
    // P11b: a dialog this hook has not seen yet is at its first placement (SetUpperLeft) or its first
    // layout this frame.
    const bool fresh = update && point != UI::UserInterface::kExtPlaceAfterLayout && !TouchKnown(dialog);
    const bool touchOwned = TouchMode::Place(point, dialog, width, height, rect);   // ext.touch_mode first
    if (update) TouchSeen(dialog, touchOwned);   // P11: the touch size walk (touch_size.h)
    if (touchOwned) return;   // placed by ext.touch_mode (TOUCH_MODE.md)
    // P9: scroll bars, close buttons and a dialog larger than the safe area (touch_fit.h).
    TouchFrame(point, dialog);
    // P11b: its touch size and fitted scale before it is first sized, so its first layout is its last
    // (touch_fit.h TouchFirstPlace; docs/android/P11B_DIALOG_SNAP.md).
    if (fresh) TouchFirstPlace(dialog);
    if (!Ext().phone_menu) {
        // Switched off while a menu is open: its retail values back before retail lays it out.
        // (A recorded menu at that address went through MenuOpened, which forgets the old one.)
        const bool menuNow = In(g_menus, dialog->_dialog) || In(g_pauses, dialog->_dialog);
        if (point == UI::UserInterface::kExtPlaceLayout && menuNow && dialog == g_row.dialog) {
            Restore();
            g_row.decided = false;   // decided again when the row is switched back on
        }
        return;
    }
    // P9: the loading screen (loading_screen.cpp): its picture's bottom border on the screen.
    if (PlaceLoading(point, dialog, width, height, rect)) return;
    const bool menu = In(g_menus, dialog->_dialog);
    const bool pause = !menu && In(g_pauses, dialog->_dialog);
    const bool version = !menu && !pause && In(g_buildIds, dialog->_dialog);
    const UI::Element* const root = dialog->_root.Get();

    if (point == UI::UserInterface::kExtPlaceBackdrop) {
        // The menus' backdrop on a wider screen: its top on the screen's top, the logo whole.
        // Retail's fit for every other backdrop.
        if (!menu && !pause) return;
        Store(LogoTop(Rect{rect[0], rect[1], rect[2], rect[3]}), rect);
        return;
    }
    if (point == UI::UserInterface::kExtPlaceBackdropLower) {
        // P9: the menus' backdrop's bottom border, drawn again at the screen's bottom (touch_fit.h).
        if (menu || pause) BackdropLowerDraw(width, height, rect);
        return;
    }
    if (point == UI::UserInterface::kExtPlaceAfterLayout) {
        // A menu in two rows: its second row moved under the first.
        if ((menu || pause) && dialog == g_row.dialog && g_rowActive) WrapRows(width, height);
        return;
    }
    if (point == UI::UserInterface::kExtPlaceUpperLeft) {
        // P11b: SetUpperLeft clamps into the safe area kExtPlaceLayout lays the dialog out in, so the
        // position a game tool gives it after the layout (an entity's panel, every frame) is the one
        // the next layout keeps (phone_math.h SafeBounds). The menus, the version line, a root sized
        // to the screen and a dialog at its own fitted scale keep retail's clamp.
        if (menu || pause || version || root == nullptr || (root->_flags & 0x30) != 0) return;
        if (FitDialogScale(dialog) != 1.0f) return;
        // SetUpperLeft moves the laid-out tree by its delta from _upperLeft: the upper-left the root is
        // actually laid out at first, so the move lands it where the next layout keeps it.
        UI::DialogInstance* placed = const_cast<UI::DialogInstance*>(dialog);
        LaidOutUpperLeft(root->_alignment,
                         Rect{root->_layoutLeft, root->_layoutTop, root->_layoutRight, root->_layoutBottom},
                         root->_leftPad, root->_topPad, placed->_upperLeftX, placed->_upperLeftY);
        int32_t w = 0;
        int32_t h = 0;
        PaddedSize(const_cast<UI::Element*>(root), w, h);
        if (w < root->_minWidth) w = root->_minWidth;
        if (h < root->_minHeight) h = root->_minHeight;
        Store(SafeBounds(width, height, w, h), rect);
        return;
    }
    if (point != UI::UserInterface::kExtPlaceLayout) return;

    if (menu || pause) {
        PlaceMenu(dialog, width, height, rect);
        return;
    }
    if (version) {
        if (g_versionAside) {
            Store(VersionAsideLayout(width, height, g_versionWidth), rect);
            return;
        }
        Store(g_versionLow ? LowVersionLayout(width, height) : InsetLayout(width, height), rect);
        return;
    }
    // Every other dialog: the in-game HUD, its panels, tooltips, drop-downs and the front
    // end's other dialogs (phone_math.h SafeArea). Its backdrop keeps retail's fit.
    if (root == nullptr) return;
    // A root sized to the back buffer (ComputeLayout's 0x10 / 0x20 flags) is meant to cover
    // the screen: retail's rectangle.
    if ((root->_flags & 0x30) != 0) return;
    // P11: a dialog shown at its own scale k (touch_fit.h (c)) keeps its upper-left and size in its
    // own units; the safe area is decided in the layout units the engine then divides by k.
    const float k = FitDialogScale(dialog);
    Store(SafeArea(width, height, ToLayoutUnits(dialog->_upperLeftX, k), ToLayoutUnits(dialog->_upperLeftY, k),
                   ToLayoutUnits(root->_layoutRight - root->_layoutLeft, k),
                   ToLayoutUnits(root->_layoutBottom - root->_layoutTop, k)),
          rect);
}

// UI::UserInterface::_extDialogScale: ext.touch_mode's toolbar scale first, then (c) of touch_fit.h.
float DialogScale(const UI::DialogInstance* dialog) {
    const float toolbar = TouchMode::Scale(dialog);
    if (toolbar != 1.0f) return toolbar;
    return FitDialogScale(dialog);
}

}  // namespace

void Sync() {
    const bool faithful = CurrentProfile() == Profile::Faithful;
    UI::UserInterface::_extDialogPlace = faithful ? nullptr : &Place;
    UI::UserInterface::_extBackdropOver = faithful ? nullptr : &BackdropOver;   // P9 (loading_screen.h)
    UI::UserInterface::_extDialogScale = faithful ? nullptr : &DialogScale;    // P11 + ext.touch_mode
    SyncTouchSize(faithful);   // P11: the slider seam (touch_size.h)
}

void MenuOpened(const UI::Dialog* menu, const UI::Dialog* buildId, bool pause) {
    Record(pause ? g_pauses : g_menus, menu);
    Record(g_buildIds, buildId);
    KeepMenuOverlay(menu);   // P11: the loading screen's torn bottom border (loading_screen.h)
    // A new menu instance: its retail values are captured at its first layout.
    g_row = RowState();
    g_rowActive = false;
    g_versionAside = false;
    g_versionLow = false;
    g_versionWidth = -1;
    std::fprintf(stderr, "[PhoneMenu] %s %p buildId %p recorded (row %s)\n", pause ? "pause menu" : "menu",
                 (const void*)menu, (const void*)buildId, Ext().phone_menu ? "on" : "off");
}

float UiScaleSliderMinimum(float platformMinPercent) {
    if (!Ext().phone_menu) return platformMinPercent;
    return SliderMinimum(platformMinPercent, UiScaleFloorNow());
}

int32_t UiScaleFloorNow() {
    const ::Video::Device* device = ::Video::Device::_singleton;
    if (device == nullptr) return 0;
    // The menus' backdrop is the 16:9 overlay (Dialog/MainMenu.rsc, Dialog/Pause.rsc). P9: the UI
    // scale is in screen units (ui_render_scale.h), so the floor is taken on the screen the back
    // buffer stands for: 35 on the S25 at every render scale.
    const int32_t render = Platform::Display::RenderPercent();
    return UiScaleFloor(Platform::Display::ScreenExtent(device->backBufferWidth, render),
                        Platform::Display::ScreenExtent(device->backBufferHeight, render), PauseMenuMetrics(),
                        16, 9, kVersionHeight);
}

int32_t ClampUiScale(int32_t percent) {
    if (!Ext().phone_menu) return percent;
    const int32_t floor = UiScaleFloorNow();
    if (floor <= 0 || percent >= floor) return percent;
    std::fprintf(stderr, "[PhoneMenu] UI scale %d %% raised to %d %%, the lowest at which the menus fit\n",
                 percent, floor);
    return floor;
}

}  // namespace PhoneMenu
}  // namespace Ext
