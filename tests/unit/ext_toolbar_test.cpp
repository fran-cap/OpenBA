// ============================================================================
// tests/unit/ext_toolbar_test.cpp -- the extension window's page list (src/ext/toolbar/toolbar.cpp)
// ============================================================================
// A page is offered only while its extension's row is active for the game, the host adds its button only
// when some page is offered, and the gate's command-line faithful profile never offers it. Each page owns a
// span of element ids and answers only those. The page's controller is UI code (exe only): this file builds
// the Contribution from the same constants (<id>.h) and stubs the three callbacks.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/item_priority/item_priority.h"
#include "ext/family_tree/family_tree.h"
#include "ext/restricted_foods/restricted_foods.h"
#include "ext/toolbar/toolbar.h"
#include "io/settings_file.h"

#include <cstring>
#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

void StubOpen(UI::Element*, int32_t) {}
bool StubChanged(UI::Element*, int32_t) { return false; }
void StubClose() {}

}  // namespace

// The pages the exe defines in src/ext/<id>/toolbar_page.cpp, from the same constants.
const Ext::Toolbar::Contribution& Ext::Toolbar::RestrictedFoodsPage() {
    static const Contribution c = { Ext::RestrictedFoods::kId, Ext::RestrictedFoods::kPageCaption,
                                    Ext::RestrictedFoods::kPageRsc, StubOpen, StubChanged, StubClose };
    return c;
}

const Ext::Toolbar::Contribution& Ext::Toolbar::ItemPriorityPage() {
    static const Contribution c = { Ext::ItemPriority::kId, Ext::ItemPriority::kPageCaption,
                                    Ext::ItemPriority::kPageRsc, StubOpen, StubChanged, StubClose };
    return c;
}

const Ext::Toolbar::Contribution& Ext::Toolbar::TerrainEditPage() {
    static const Contribution c = { "terrain_edit", "TerrainEditTab",
        "Dialog/TerrainEditPage.rsc:pageTerrainEdit",
                                    StubOpen, StubChanged, StubClose };
    return c;
}

const Ext::Toolbar::Contribution& Ext::Toolbar::FamilyTreePage() {
    static const Contribution c = { Ext::FamilyTree::kId, Ext::FamilyTree::kPageCaption,
                                   Ext::FamilyTree::kPageRsc,
                                    StubOpen, StubChanged, StubClose };
    return c;
}

const Ext::Toolbar::Contribution& Ext::Toolbar::TownScreenshotPage() {
    static const Contribution c = { "town_screenshot", "TownScreenshotTab", "Dialog/TownScreenshotPage.rsc:pageTownScreenshot",
                                    StubOpen, StubChanged, StubClose };
    return c;
}

const Ext::Toolbar::Contribution& Ext::Toolbar::ShowGridPage() {
    static const Contribution c = { "show_grid", "ShowGridTab", "Dialog/ShowGridPage.rsc:pageShowGrid",
                                    StubOpen, StubChanged, StubClose };
    return c;
}

UNIT_TEST(ext_toolbar_no_page_while_the_row_is_off) {
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
    const Ext::Toolbar::Contribution* pages[4] = {};
    CHECK_EQ(Ext::Toolbar::ActivePages(pages, 4), 0);
    // no active contributor: no button, the toolbar is retail's
    CHECK(!Ext::Toolbar::Offered());
}

UNIT_TEST(ext_toolbar_a_page_is_offered_while_its_row_is_active) {
    Ext::Install(ResolveText("ext.restricted_foods=1\n"), Ext::Profile::PerEntry, false);
    const Ext::Toolbar::Contribution* pages[4] = {};
    CHECK_EQ(Ext::Toolbar::ActivePages(pages, 4), 1);
    CHECK(pages[0] != nullptr && std::strcmp(pages[0]->id, "restricted_foods") == 0);
    CHECK(Ext::Toolbar::Offered());
    // the count alone, no output buffer
    CHECK_EQ(Ext::Toolbar::ActivePages(nullptr, 0), 1);
    // more active pages than room: the count still says how many there are, the window logs the rest
    const Ext::Toolbar::Contribution* none[1] = {};
    CHECK_EQ(Ext::Toolbar::ActivePages(none, 0), 1);
    CHECK(none[0] == nullptr);
    int n = 0;
    CHECK(Ext::Toolbar::All(&n) != nullptr && n == 6);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_toolbar_the_faithful_gate_never_offers_the_button) {
    Ext::State s = ResolveText("ext.restricted_foods=1\n");
    Ext::Install(s, Ext::Profile::Faithful, true);         // /profile:faithful on the command line
    CHECK(Ext::GateProfileForced());
    CHECK(!Ext::Toolbar::Offered());
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_toolbar_every_page_names_a_registered_extension) {
    int n = 0;
    const Ext::Toolbar::Contribution* const* list = Ext::Toolbar::All(&n);
    CHECK(n > 0);
    for (int i = 0; i < n; ++i) {
        const Ext::Entry* e = Ext::FindEntry(list[i]->id);
        CHECK(e != nullptr);
        if (e != nullptr) {
            // a page belongs to an extension row of any class: the UI surface is not the class axis
            // (docs/extensions/DESIGN.md, surfaces, ruled 2026-10-20); a class A row's page is live
            CHECK(e->tier == Ext::Tier::Extension);
        }
        CHECK(list[i]->caption != nullptr && list[i]->caption[0] != 0);
        CHECK(list[i]->page != nullptr && std::strstr(list[i]->page, ".rsc:") != nullptr);
        CHECK(list[i]->open && list[i]->changed && list[i]->close);
    }
}

UNIT_TEST(ext_toolbar_each_page_owns_its_own_span_of_element_ids) {
    using namespace Ext::Toolbar;
    const int32_t first = IdBaseFor(0);
    const int32_t second = IdBaseFor(1);
    // the window's own title bar ids (0 and 1) are below every page
    CHECK(first > 1);
    CHECK(second == first + kPageIdSpan);
    CHECK(OwnsId(first, first));
    CHECK(OwnsId(first, first + kPageIdSpan - 1));
    CHECK(!OwnsId(first, first + kPageIdSpan));
    CHECK(!OwnsId(first, first - 1));
    // an id inside one page's span is never inside another's
    for (int a = 0; a < kMaxPages; ++a) {
        for (int b = 0; b < kMaxPages; ++b) {
            if (a == b) {
                continue;
            }
            CHECK(!OwnsId(IdBaseFor(a), IdBaseFor(b)));
            CHECK(!OwnsId(IdBaseFor(a), IdBaseFor(b) + kPageIdSpan - 1));
        }
    }
    CHECK(!OwnsId(first, 0));
    CHECK(!OwnsId(first, 1));
}

UNIT_TEST(ext_toolbar_each_active_row_adds_its_page_in_list_order) {
    Ext::Install(ResolveText("ext.restricted_foods=1\next.item_priority=1\n"), Ext::Profile::PerEntry, false);
    const Ext::Toolbar::Contribution* pages[4] = {};
    CHECK_EQ(Ext::Toolbar::ActivePages(pages, 4), 2);
    CHECK(pages[0] != nullptr && std::strcmp(pages[0]->id, "restricted_foods") == 0);
    CHECK(pages[1] != nullptr && std::strcmp(pages[1]->id, "item_priority") == 0);
    Ext::Install(ResolveText("ext.item_priority=1\n"), Ext::Profile::PerEntry, false);
    CHECK_EQ(Ext::Toolbar::ActivePages(pages, 4), 1);       // one active row: one tab
    CHECK(pages[0] != nullptr && std::strcmp(pages[0]->id, "item_priority") == 0);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}
