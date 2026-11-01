// ============================================================================
// src/ext/toolbar/toolbar.h -- the in-game extension window: one toolbar button, a page per extension
// ============================================================================
// NOT RETAIL. docs/extensions/DESIGN.md "Surfaces" (ruled 2026-10-20),
// docs/extensions/handins/RESTRICTEDFOODS.md. An extension with an in-game control does not get a button of
// its own: the main toolbar gets ONE extension button, in its Reports group (Status Bar, Limits, Town Hall,
// Help), and it opens one window with one tab per extension that contributes a page. With no active
// contributor the button is not added and the toolbar is retail's.
//
// A contribution keeps its own element pointers (found by name in its page); this host holds only the list.
// The page's controls are live: `changed` writes the extension's state at once (no Apply). The lock (class B
// / C) is on the extension's ACTIVATION, so a page is offered only while the row is active for this game;
// what the page edits may change whenever the player likes (DESIGN.md "What the lock covers").
//
// ELEMENT IDS. The window's own title bar uses ids 0 and 1. Each open page owns the span [idBase, idBase +
// kPageIdSpan) and the host hands it that `idBase` in `open`, so two pages can never answer each other's
// clicks: a control gets `idBase + local` as its element id, and `changed` is called only for ids in the
// page's own span, with the LOCAL id (0 .. kPageIdSpan - 1).
//
// The host's arms (the one EXT SEAM line per site):
//   src/game/Dialog/toolbardialog.cpp   InsertAt / Style while the Reports group is built, Toggle on the
//                                       button's click
//   src/game/Dialog/entityui.cpp        RestoreWindow in PlayerToolManager::Startup (the window reopens
//                                       with the save, window_state.h)
//
// Standard C++ for the list and Offered (tests/unit/ext_toolbar_test.cpp links toolbar.cpp alone); the
// window, the button and the helpers every page shares are toolbar_window.cpp (exe only).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace UI {
class Element;
class String;
class UserInterface;
}
class Toolbar;   // Game/Toolbar.h: the retail toolbar item (global)

namespace Ext {
namespace Toolbar {

constexpr int32_t kPageIdSpan = 10000;  // the element ids one page owns
constexpr int kMaxPages = 8;            // pages the window shows; more are logged and left out

// The first element id of the page shown at `pageIndex` (0-based); the window's own ids sit below it.
constexpr int32_t IdBaseFor(int pageIndex) {
    return kPageIdSpan * (pageIndex + 1);
}
// Whether element id `id` belongs to the page whose span starts at `idBase`.
constexpr bool OwnsId(int32_t idBase, int32_t id) {
    return id >= idBase && id < idBase + kPageIdSpan;
}

struct Contribution {
    const char* id;        // the Entry id; the page is offered only while that row is active for the game
    const char* caption;   // the tab's text: a key in Dialog/ExtensionsStringTable.rsc
    // "Dialog/RestrictedFoodsPage.rsc:pageRestrictedFoods": our text .rsc, the page's root object
    const char* page;
    // find the elements by name, give the controls ids from `idBase`, fill them from the state
    void (*open)(UI::Element* page, int32_t idBase);
    // a control of this page (local id `id`) was used: write the state now; false = not mine
    bool (*changed)(UI::Element* page, int32_t id);
    void (*close)();       // forget the element pointers
    // optional: called every frame while the page is the one showing, so it can keep what it displays up to
    // date when the player is busy with another tool
    void (*tick)() = nullptr;
};

// The contributions (defined beside their extensions, src/ext/<id>/toolbar_page.cpp).
const Contribution& RestrictedFoodsPage();
const Contribution& ItemPriorityPage();
const Contribution& TerrainEditPage();
const Contribution& TownScreenshotPage();
const Contribution& FamilyTreePage();
const Contribution& ShowGridPage();

// The list, and the ones whose row is active for this game (into `out`; returns how many there are,
// which may exceed `capacity`).
const Contribution* const* All(int* n);
int ActivePages(const Contribution** out, int capacity);

// Whether the host adds its button: at least one active contributor, and not under the
// gate's command-line faithful profile.
bool Offered();

// ---- helpers every page shares (toolbar_window.cpp; exe only) ----
// A string of Dialog/ExtensionsStringTable.rsc; invalid when the table did not load.
UI::String OurString(const char* key);
// Sets a text element to one of our table's strings. Null-safe: anything but a text element is left alone.
void SetTableText(UI::Element* e, const char* key);
// ASCII lower case of a C string (null = empty).
std::string Lower(const char* s);

// Opens the window on the page whose Contribution::id is `pageId` (brings it to the front when it is open
// already). For a control elsewhere in the game that leads to a page (the citizen panel's Family button).
void ShowPage(UI::UserInterface* ui, const char* pageId);

// ---- the button (toolbar_window.cpp; EXT SEAMS in ToolbarDialog) ----
// Where in the sorted `items` (count `n`) of the Reports group the button goes, or -1 for none: not the
// Reports group, no running game, not Offered(), or the resources do not load. The toolbar keeps a null
// entry in its item array for it, so each retail reader of that array skips null.
int InsertAt(const ::Toolbar* group, const ::Toolbar* const* items, int n);
// Name, id, tooltip and icon on the freshly cloned toolbar button `button` (a template-3 clone) at `index`.
void Style(UI::Element* button, int32_t index);
// The button's click: opens the window, or closes it when it is open.
void Toggle(UI::UserInterface* ui);
// PlayerToolManager::Startup's EXT SEAM, after retail reopened its pinned utility windows: reopens the
// window when the town's save had it open (window_state.h) and some page is offered.
void RestoreWindow(UI::UserInterface* ui);

}  // namespace Toolbar
}  // namespace Ext
