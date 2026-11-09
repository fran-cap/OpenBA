// ============================================================================
// tests/unit/ext_rsc_text_test.cpp -- our .rsc text dialect (KITFREE, RSCTEXT)
// ============================================================================
// src/ext/resources/text_parser.{h,cpp} over the three embedded files
// (gen/ext_rsc_text.h, the exact text the exe carries) and ResolveReference.
// The object construction needs the live factory and the game's packages, so
// it is checked in the exe (src/diag/rsc_text_check.cpp + tools/ext_rsc_check.py).
// ============================================================================
#include "unit.h"
#include "ext/load_notice.h"
#include "ext/registry.h"   // issue #96: the key combos have one item per choice
#include "ext/resources/text_parser.h"
#include "ext/resources/text_enums.h"
#include "ext_rsc_text.h"

#include <cstring>
#include <initializer_list>
#include <string>

using namespace Ext::RscText;

namespace {

const EmbeddedFile* Embedded(const char* path) {
    for (int i = 0; i < kEmbeddedCount; ++i)
        if (std::strcmp(kEmbedded[i].path, path) == 0) return &kEmbedded[i];
    return nullptr;
}

bool ParseEmbedded(const char* path, File& f) {
    const EmbeddedFile* e = Embedded(path);
    if (e == nullptr) return false;
    std::string err;
    const bool ok = Parse(e->text, e->size, f, err);
    if (!ok) std::printf("  %s: %s\n", path, err.c_str());
    return ok;
}

}  // namespace

UNIT_TEST(ext_rsc_text_embedded_files) {
    CHECK(kEmbeddedCount == 22);
    CHECK(Embedded("Dialog/Extensions.rsc") != nullptr);
    CHECK(Embedded("Dialog/EnhancedOptions.rsc") != nullptr);
    CHECK(Embedded("Dialog/ExtensionsStringTable.rsc") != nullptr);
    CHECK(Embedded("Dialog/ExtensionsWindow.rsc") != nullptr);
    CHECK(Embedded("Dialog/RestrictedFoodsPage.rsc") != nullptr);
    CHECK(Embedded("Dialog/ItemPriorityPage.rsc") != nullptr);
    CHECK(Embedded("Dialog/ItemLimitsPanel.rsc") != nullptr);
    CHECK(Embedded("Dialog/FamilyTreePage.rsc") != nullptr);
    CHECK(Embedded("Dialog/FamilyTreeWindow.rsc") != nullptr);
    CHECK(Embedded("Dialog/FamilyTreeSummary.rsc") != nullptr);
    CHECK(Embedded("Dialog/TerrainEditPage.rsc") != nullptr);
    CHECK(Embedded("Dialog/TouchControls.rsc") != nullptr);
    CHECK(Embedded("Dialog/TouchPointer.rsc") != nullptr);
    CHECK(Embedded("Dialog/PhoneScroll.rsc") != nullptr);
    CHECK(Embedded("Dialog/TownScreenshotPage.rsc") != nullptr);
    CHECK(Embedded("Dialog/ShowGridPage.rsc") != nullptr);
    CHECK(Embedded("Dialog/FpsCounterPage.rsc") != nullptr);
    CHECK(Embedded("Dialog/BuildingFinderPage.rsc") != nullptr);
    CHECK(Embedded("Dialog/FavoritesWindow.rsc") != nullptr);
    CHECK(Embedded("Dialog/FullscreenMode.rsc") != nullptr);
    CHECK(Embedded("Dialog/MapOverlays.rsc") != nullptr);
    CHECK(Embedded("Dialog/ProfessionsWindow.rsc") != nullptr);
}

UNIT_TEST(ext_rsc_text_parse_extensions) {
    File f;
    CHECK(ParseEmbedded("Dialog/Extensions.rsc", f));
    const Object* dlg = f.Find("resource");
    CHECK(dlg != nullptr && dlg->type == "Dialog" && dlg->base.empty());
    if (dlg) {
        const Field* flags = dlg->body.Find("_flags");
        CHECK(flags && !flags->isArray && flags->tokens.size() == 3 && flags->tokens[1].text == "PauseGame");
        const Field* refs = dlg->body.Find("_references");
        CHECK(refs && refs->isArray && refs->tokens.size() == 5 && refs->tokens[0].quoted);
        const Field* tpl = dlg->body.Find("_templates");
        CHECK(tpl && tpl->isArray && tpl->tokens.size() == 1 && tpl->tokens[0].text == "entryRow");
    }
    const Object* row = f.Find("entryRow");
    CHECK(row && row->type == "LayoutDescription");
    if (row) {
        const Field* items = row->body.Find("_elements");
        CHECK(items && items->isArray && items->blocks.size() == 4 && items->tokens.empty());
        if (items && items->blocks.size() == 4) {
            const Field* off = items->blocks[1].Find("_xOffset");
            CHECK(off && off->tokens.size() == 1 && off->tokens[0].text == "4" && !off->tokens[0].quoted);
            const Field* anchor = items->blocks[3].Find("_xAnchorTo");
            CHECK(anchor && anchor->tokens[0].quoted && anchor->tokens[0].text == "blurb");
        }
    }
    // The description wraps at a width of our own (not the game's, which a mod can widen over the toggle).
    const Object* blurb = f.Find("textModDescription");
    CHECK(blurb && blurb->base == "Mod.rsc:textModDescription");
    if (blurb) {
        const Field* size = blurb->body.Find("_wordWrapSize");
        CHECK(size && size->tokens.size() == 1 && size->tokens[0].text == "192");
        CHECK(blurb->body.Find("_wordWrap") == nullptr);   // the base's, unchanged
    }
    const Object* list = f.Find("tableList");
    CHECK(list && list->type == "ContainerDescription");
    if (list) {
        const Field* w = list->body.Find("_width");
        CHECK(w && w->tokens.size() == 1 && w->tokens[0].text == "596");
        const Field* h = list->body.Find("_height");
        CHECK(h && h->tokens.size() == 1 && h->tokens[0].text == "350");
        const Field* gap = list->body.Find("_scrollSpacing");
        CHECK(gap && gap->tokens.size() == 1 && gap->tokens[0].text == "4");
    }
    const Object* title = f.Find("labelTitle");
    CHECK(title && title->base == "Mod.rsc:labelTitle" && title->type == "LabelDescription");
    const Object* notice = f.Find("extensionNotInBuild");
    CHECK(notice && notice->base == "SaveLoad.rsc:modsMissing");
    if (notice) {
        const Field* ov = notice->body.Find("_override");
        CHECK(ov && ov->blocks.size() == 3 && ov->blocks[1].fields.size() == 1);
    }
    // Three tabs, Optional first, then Default and Experimental (ruled 2026-11-08): each its own button and list,
    // down to its own table (the controller finds tableOptional / tableDefault / tableExperimental by name), all
    // deriving from the bases the in-game pages share.
    const Object* tabs = f.Find("tabExtensions");
    CHECK(tabs && tabs->type == "TabDescription" && tabs->base == "Mod.rsc:tabMod");
    if (tabs) {
        const Field* list = tabs->body.Find("_tabs");
        CHECK(list && list->isArray && list->blocks.size() == 3);
        if (list && list->blocks.size() == 3) {
            const Field* b0 = list->blocks[0].Find("_button");
            const Field* c0 = list->blocks[0].Find("_content");
            const Field* b1 = list->blocks[1].Find("_button");
            const Field* c1 = list->blocks[1].Find("_content");
            const Field* b2 = list->blocks[2].Find("_button");
            const Field* c2 = list->blocks[2].Find("_content");
            CHECK(b0 && b0->tokens[0].text == "tabButtonOptional" && c0 && c0->tokens[0].text == "entryListOptional");
            CHECK(b1 && b1->tokens[0].text == "tabButtonDefault" && c1 && c1->tokens[0].text == "entryListDefault");
            CHECK(b2 && b2->tokens[0].text == "tabButtonExperimental" && c2 && c2->tokens[0].text == "entryListExperimental");
        }
    }
    for (const char* half : { "Optional", "Default", "Experimental" }) {
        const std::string h(half);
        const Object* caption = f.Find(("tabCaption" + h).c_str());
        CHECK(caption && caption->base == "tabCaption");
        if (caption) {
            const Field* text = caption->body.Find("_text");
            CHECK(text && text->tokens[0].text == "ExtensionsTab" + h);
        }
        const Object* button = f.Find(("tabButton" + h).c_str());
        CHECK(button && button->type == "ButtonDescription" && button->base == "tabButton");
        const Object* entries = f.Find(("entryList" + h).c_str());
        CHECK(entries && entries->base == "entryList");
        if (entries) {
            const Field* els = entries->body.Find("_elements");
            CHECK(els && els->tokens.size() == 2 && els->tokens[1].text == "tableList" + h);
        }
        const Object* box = f.Find(("tableList" + h).c_str());
        CHECK(box && box->type == "ContainerDescription" && box->base == "tableList");
        if (box) {
            const Field* content = box->body.Find("_content");
            CHECK(content && content->tokens[0].text == "table" + h);
        }
        const Object* table = f.Find(("table" + h).c_str());
        CHECK(table && table->type == "RibbonDescription" && table->base == "tableMods");
    }
    // The bases the in-game pages derive from (RestrictedFoodsPage.rsc, ItemPriorityPage.rsc) stay.
    CHECK(f.Find("entryList") && f.Find("tableList") && f.Find("tableMods"));
    // 46 + fix.save_failure_notice's box (saveFailed, noticeSaveFailed) + 2: buttonGameFiles and labelGameFiles
    // (issue #96) + 2: fix.mods_persist's in-game warning (modsInGame, noticeModsInGame) + 5: the Experimental
    // tab (button, caption, list, table list, table)
    CHECK(f.objects.size() == 57);
}

UNIT_TEST(ext_rsc_text_parse_enhanced_and_strings) {
    File e;
    CHECK(ParseEmbedded("Dialog/EnhancedOptions.rsc", e));
    const Object* label = e.Find("labelEnhanced");
    CHECK(label && label->type == "LabelDescription");
    if (label) {
        const Field* font = label->body.Find("_font");
        CHECK(font && font->tokens[0].text == "Font\\FontSmall.rsc");   // a backslash is not an escape
        const Field* pos = label->body.Find("_labelPosition");
        CHECK(pos && pos->tokens[0].text == "TextBottom");
    }
    // The page is the Game tab's container (it takes the retail tabs' width) around the rows' table.
    const Object* page = e.Find("enhancedOptions");
    CHECK(page && page->type == "ContainerDescription" && page->base == "Options.rsc:gameOptions");
    CHECK(page && page->body.Find("_content") &&
          page->body.Find("_content")->tokens[0].text == "enhancedOptionsTable");
    const Object* table = e.Find("enhancedOptionsTable");
    CHECK(table && table->body.Find("_items") && table->body.Find("_items")->blocks.size() == 55);   // twenty-eight labels, twenty-seven controls (DOF.md: depth blur; FANCYTERRAIN: fancy terrain; BETTERCAM: wider zoom; CAMERAANGLE: camera angle; NOFOG: fog distance; SHADOWZOOM: adaptive shadow map; BETTERWATER: better water; WINTERICE: better ice; BETTERCLOUDS: better clouds; SHARP_SPRITES: Sharp UI; NATURAL_GROWTH.md: natural tree growth; COMPRESSAO: the cache size and, under it, its button; TEXQUALITY: texture quality; P8: render scale, frame rate cap; TOUCHMODE: touch controls; P11b: touch pointer; issue #96: the "Touch screen" and "General" headers, camera pad, placement bar, screen edge margin; the reporter's follow-up: corner camera buttons)
    // Issue #96: one combo item per choice of its value key (registry.cpp), in the key's order.
    const char* const keyCombos[][2] = {{"touchPadItems", "touch_mode_pad_scale"},
                                        {"touchCameraButtonItems", "touch_mode_camera_buttons"},
                                        {"touchActionBarItems", "touch_mode_action_bar_scale"},
                                        {"phoneSafeMarginItems", "phone_menu_safe_margin"}};
    for (const auto& kc : keyCombos) {
        const Object* items = e.Find(kc[0]);
        const Ext::ValueKey* key = Ext::FindValueKey(kc[1]);
        CHECK(items && items->body.Find("_strings") && key &&
              static_cast<int32_t>(items->body.Find("_strings")->blocks.size()) == key->choiceCount);
    }
    const Object* tpItems = e.Find("touchPointerItems");   // P11b: Off, four presets, Custom
    CHECK(tpItems && tpItems->body.Find("_strings") && tpItems->body.Find("_strings")->blocks.size() == 6);
    // TEXQUALITY: the Texture quality combo offers Full / Half only (Quarter removed by the user's ruling).
    const Object* tqItems = e.Find("textureQualityItems");
    CHECK(tqItems && tqItems->body.Find("_strings") && tqItems->body.Find("_strings")->blocks.size() == 2);
    const Object* opt = e.Find("textAoOption");
    CHECK(opt && opt->body.Find("_color") && opt->body.Find("_color")->tokens[0].text == "0xFFC0C0C0");

    File s;
    CHECK(ParseEmbedded("Dialog/ExtensionsStringTable.rsc", s));
    CHECK(s.objects.size() == 1 && s.objects[0].type == "StringTable" && s.objects[0].name == "resource");
    const Field* strings = s.objects[0].body.Find("_strings");
    // 70: terrain_edit Redo (NextStartSuffix removed, P9), + 17 for the Town Screenshot page, + 2 for the
    // Never button and its tip, + 3 for the tree window (Show family tree, Details, the title), + 3 for the
    // picture (button, check box, its tip), + 5 for the Family page, + 3 for its drawn tree (the slide
    // buttons and the note), + 1 for the citizen panel's Family button and its tip, + 2 for the SVG button
    // and its tip, + 2 for the search box (its label and tip), + 3 for the town summary (button, title,
    // note), + 4 for the picture's outline check box and the Show in town button (each with its tip), + 2 for
    // the picture's main-job check box and its tip, + 10 for the Grid page, + 1 for
    // fix.save_failure_notice's message, + 2 for the Game files menu button and its tip (issue #96),
    // + 1 for the touch pointer bar's Shift tip, + 1 for fix.mods_persist's in-game warning, + 9 for the FPS
    // page, + 18 for the Find Buildings page and its favorites window, + 52 for the map window's overlay
    // views, + 4 for the dynamic Professions window (its title, its two column-button tips and its resize
    // strip), + 1 for the Experimental tab's caption, + 3 for ext.item_limits' panel row (label, two tips)
    CHECK(strings && strings->blocks.size() == 142 + 18 + 52 + 4 + 1 + 3);
    if (strings) {
        // The Extensions dialog's three tab captions.
        int tabKeys = 0;
        for (const auto& b : strings->blocks) {
            const Field* n = b.Find("_name");
            if (n && !n->tokens.empty() &&
                (n->tokens[0].text == "ExtensionsTabOptional" || n->tokens[0].text == "ExtensionsTabDefault" ||
                 n->tokens[0].text == "ExtensionsTabExperimental"))
                ++tabKeys;
        }
        CHECK(tabKeys == 3);
    }
    if (strings) {
        // The load notice with two parameters (found by its text: the window and page keys sit before the
        // notices, so an index into the list moves whenever one is added).
        bool found = false;
        for (const auto& b : strings->blocks) {
            const Field* t = b.Find("_text");
            if (t && !t->tokens.empty() && t->tokens[0].text.find("~n~n@0 [@1]") != std::string::npos)
                found = true;
        }
        CHECK(found);
    }
}

namespace {
// Every object is of a type Extensions.rsc already uses (so the exe's text reader has a schema for it; the
// reader itself needs the live engine and is checked in the exe, not here).
void CheckKnownTypes(const File& f, const char* file) {
    CHECK(!f.objects.empty());
    for (const Object& o : f.objects) {
        const bool known = o.type == "Dialog" || o.type == "GroupDescription" ||
                           o.type == "RibbonDescription" || o.type == "LabelDescription" ||
                           o.type == "TextDescription" || o.type == "TabDescription" ||
                           o.type == "ButtonDescription" || o.type == "TableDescription" ||
                           o.type == "ContainerDescription" || o.type == "CheckDescription" ||
                           o.type == "ImageDescription" || o.type == "SliderDescription" ||
                           o.type == "ComboDescription" || o.type == "EditDescription";
        CHECK(known);
        if (!known) std::printf("  %s:%s has type %s\n", file, o.name.c_str(), o.type.c_str());
    }
}
}  // namespace

UNIT_TEST(ext_rsc_text_parse_extensions_window) {
    File f;
    CHECK(ParseEmbedded("Dialog/ExtensionsWindow.rsc", f));
    const Object* dlg = f.Find("resource");
    CHECK(dlg != nullptr && dlg->type == "Dialog" && dlg->base.empty());
    // no flags: a utility-style window (FirstDraw would hide the toolbar)
    if (dlg) CHECK(dlg->body.Find("_flags") == nullptr);
    // One tab control with no tabs of its own (the controller adds one per active page), and
    // the shared tab button.
    const Object* tabs = f.Find("tabPages");
    CHECK(tabs && tabs->base == "Mod.rsc:tabMod" && tabs->type == "TabDescription");
    if (tabs) {
        const Field* list = tabs->body.Find("_tabs");
        CHECK(list && list->isArray && list->blocks.empty());
    }
    CHECK(f.Find("tabButtonPage") && f.Find("dialogRoot") && f.Find("dialogBody"));
    CheckKnownTypes(f, "ExtensionsWindow.rsc");
}

UNIT_TEST(ext_rsc_text_parse_restricted_foods_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/RestrictedFoodsPage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageRestrictedFoods
    const Object* page = f.Find("pageRestrictedFoods");
    CHECK(page && page->type == "RibbonDescription");
    CHECK(f.Find("tableRestrictedFoods") && f.Find("ribbonRestrictedFoodsCell") &&
          f.Find("labelRestrictedFoodsRow") && f.Find("checkRestrictedFoodsRow") &&
          f.Find("checkRestrictedFoodsShowAll") && f.Find("textRestrictedFoodsHint"));
    CheckKnownTypes(f, "RestrictedFoodsPage.rsc");
}

UNIT_TEST(ext_rsc_text_parse_item_limits_panel) {
    File f;
    CHECK(ParseEmbedded("Dialog/ItemLimitsPanel.rsc", f));
    // The row panel_row.cpp clones onto a workshop panel: a check box and the text, by the names it looks up.
    const Object* row = f.Find("ribbonItemLimitsRow");
    CHECK(row && row->type == "RibbonDescription");
    if (row) {
        const Field* els = row->body.Find("_elements");
        CHECK(els && els->tokens.size() == 2 && els->tokens[0].text == "checkItemLimitsRow" &&
              els->tokens[1].text == "textItemLimitsRow");
    }
    const Object* check = f.Find("checkItemLimitsRow");
    CHECK(check && check->type == "CheckDescription" && check->base == "Extensions.rsc:checkLoaded");
    const Object* text = f.Find("textItemLimitsRow");
    CHECK(text && text->type == "TextDescription");
    // The column that takes the category row's slot: vertical, so the row gets its own line under it.
    const Object* column = f.Find("ribbonItemLimitsColumn");
    CHECK(column && column->type == "RibbonDescription");
    if (column) {
        const Field* vertical = column->body.Find("_vertical");
        CHECK(vertical && !vertical->tokens.empty() && vertical->tokens[0].text == "true");
    }
    CheckKnownTypes(f, "ItemLimitsPanel.rsc");
}

UNIT_TEST(ext_rsc_text_parse_item_priority_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/ItemPriorityPage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageItemPriority
    const Object* page = f.Find("pageItemPriority");
    CHECK(page && page->type == "RibbonDescription");
    CHECK(f.Find("tableItemPriorityTool") && f.Find("tableItemPriorityFuel") &&
          f.Find("tableItemPriorityClothing") && f.Find("tableItemPriorityHealth") &&
          f.Find("ribbonItemPriorityCell") && f.Find("labelItemPriorityRow") &&
          f.Find("checkItemPriorityRow") && f.Find("textItemPriorityRank") &&
          f.Find("ribbonItemPriorityMoves") && f.Find("checkItemPriorityShowAll"));
    // The category strip: one tab per category, each a button and its own list.
    const Object* strip = f.Find("tabItemPriorityCategories");
    CHECK(strip && strip->type == "TabDescription" && strip->base == "Mod.rsc:tabMod");
    if (strip) {
        const Field* tabs = strip->body.Find("_tabs");
        CHECK(tabs && tabs->isArray && tabs->blocks.size() == 4);
        if (tabs && tabs->blocks.size() == 4) {
            CHECK(tabs->blocks[0].Find("_button") && tabs->blocks[0].Find("_content"));
        }
    }
    const Object* up = f.Find("buttonItemPriorityUp");
    CHECK(up && up->base == "Mod.rsc:buttonMod");
    const Object* rank = f.Find("textItemPriorityRank");
    CHECK(rank && rank->body.Find("_text"));
    CheckKnownTypes(f, "ItemPriorityPage.rsc");
}

UNIT_TEST(ext_rsc_text_parse_family_tree_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/FamilyTreePage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageFamilyTree
    const Object* page = f.Find("pageFamilyTree");
    CHECK(page && page->type == "RibbonDescription");
    CHECK(f.Find("checkFamilyShowDead") && f.Find("tableFamilyPeople") && f.Find("tableFamilyDetail") &&
          f.Find("textFamilyDetailTitle") && f.Find("ribbonFamilyPersonCell") && f.Find("buttonFamilyPick") &&
          f.Find("textFamilyPickName") && f.Find("ribbonFamilyRelationCell") &&
          f.Find("textFamilyRelation") &&
          f.Find("buttonFamilyRelated") && f.Find("textFamilyRelatedName") && f.Find("textFamilyInfo"));
    const Object* related = f.Find("buttonFamilyRelated");
    CHECK(related && related->base == "Mod.rsc:buttonMod");
    // the drawn tree's cell templates: the box, the spaces, and the 15 line tiles plus the two gap tiles
    CHECK(f.Find("ribbonFamilyBox") && f.Find("buttonFamilyBox") && f.Find("textFamilyBoxName") &&
          f.Find("ribbonFamilySpaceBox") && f.Find("ribbonFamilySpaceLink") && f.Find("imageFamilyGap") &&
          f.Find("imageFamilyGapStem") && f.Find("buttonFamilyShowTree") && f.Find("buttonFamilyImage") &&
          f.Find("checkFamilyImageLiving") && f.Find("textFamilyImageStatus"));
    for (int m = 1; m <= 15; ++m) CHECK(f.Find(("imageFamilyLink" + std::to_string(m)).c_str()) != nullptr);
    CHECK(f.Find("tableFamilyTree") == nullptr);   // the tree itself is the window's
    CHECK(f.Find("buttonFamilyCitizen") && f.Find("textFamilyCitizen"));   // the citizen panel's button
    CheckKnownTypes(f, "FamilyTreePage.rsc");
}

UNIT_TEST(ext_rsc_text_parse_family_tree_window) {
    File w;
    CHECK(ParseEmbedded("Dialog/FamilyTreeWindow.rsc", w));
    CHECK(w.Find("resource") != nullptr);   // a dialog: the tree window
    CHECK(w.Find("dialogRoot") && w.Find("dialogBody") && w.Find("ribbonFamilyTreeBar")
          && w.Find("ribbonFamilyTree"));
    CHECK(w.Find("buttonFamilyTreeLeft") && w.Find("buttonFamilyTreeRight")
          && w.Find("buttonFamilyTreeDetails") &&
          w.Find("tableFamilyTree") && w.Find("ribbonFamilyTreeRow"));
    CheckKnownTypes(w, "FamilyTreeWindow.rsc");
}

UNIT_TEST(ext_rsc_text_parse_family_tree_summary) {
    File w;
    CHECK(ParseEmbedded("Dialog/FamilyTreeSummary.rsc", w));
    CHECK(w.Find("resource") != nullptr);   // a dialog: the town summary window
    CHECK(w.Find("dialogRoot") && w.Find("dialogBody") && w.Find("textFamilySummaryNote")
          && w.Find("ribbonFamilySummary") &&
          w.Find("containerFamilySummaryBox") && w.Find("tableFamilySummary"));
    CheckKnownTypes(w, "FamilyTreeSummary.rsc");
}

// The element flags our pages may set (`Flags _flags = ExpandWidth;`): the kit's names with the bits the engine
// reads (Element.h: the stretch bits 0x4 / 0x8 the layouts test, the screen-size bits 0x10 / 0x20 GetPaddedSize
// tests), and nothing the table does not know.
UNIT_TEST(ext_rsc_text_element_flags) {
    uint32_t v = 0;
    CHECK(LookupEnum(kElementFlags, "Hidden", v) && v == 0x1);
    CHECK(LookupEnum(kElementFlags, "NoMouseCapture", v) && v == 0x2);
    CHECK(LookupEnum(kElementFlags, "ExpandWidth", v) && v == 0x4);
    CHECK(LookupEnum(kElementFlags, "ExpandHeight", v) && v == 0x8);
    CHECK(LookupEnum(kElementFlags, "FullHeight", v) && v == 0x10);
    CHECK(LookupEnum(kElementFlags, "FullWidth", v) && v == 0x20);
    CHECK(!LookupEnum(kElementFlags, "ExtendWidth", v));   // Options.rsc's spelling: not a name the kit knows
    CHECK(!LookupEnum(kElementFlags, "PauseGame", v));     // a dialog flag, not an element's
    CHECK(LookupEnum(kDialogFlags, "FirstDraw", v) && v == 0x10);

    // The Foods cell lays itself across the column the controller gives it.
    File f;
    CHECK(ParseEmbedded("Dialog/RestrictedFoodsPage.rsc", f));
    const Object* cell = f.Find("ribbonRestrictedFoodsCell");
    CHECK(cell != nullptr);
    if (cell) {
        const Field* flags = cell->body.Find("_flags");
        CHECK(flags && !flags->isArray && flags->tokens.size() == 1 && flags->tokens[0].text == "ExpandWidth");
        CHECK(cell->body.Find("_minWidth") == nullptr);   // the width is the list's, not a constant
    }
    File p;
    CHECK(ParseEmbedded("Dialog/ItemPriorityPage.rsc", p));
    const Object* row = p.Find("ribbonItemPriorityCell");
    CHECK(row && row->body.Find("_minWidth") == nullptr);
    const Object* label = p.Find("labelItemPriorityRow");
    CHECK(label && label->body.Find("_minWidth") == nullptr);
}

UNIT_TEST(ext_rsc_text_parse_fps_counter_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/FpsCounterPage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageFpsCounter
    const Object* page = f.Find("pageFpsCounter");
    CHECK(page && page->type == "RibbonDescription");
    const char* const values[] = { "textFpsCounterCurrentValue", "textFpsCounterAverageValue",
                                   "textFpsCounterMinimumValue", "textFpsCounterMaximumValue",
                                   "textFpsCounterMeasuredValue" };
    for (const char* name : values) {
        const Object* v = f.Find(name);
        CHECK(v && v->type == "TextDescription");
    }
    const Object* reset = f.Find("buttonFpsCounterReset");
    CHECK(reset && reset->type == "ButtonDescription");
    CheckKnownTypes(f, "FpsCounterPage.rsc");
}

UNIT_TEST(ext_rsc_text_parse_building_finder_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/BuildingFinderPage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageBuildingFinder
    const Object* page = f.Find("pageBuildingFinder");
    CHECK(page && page->type == "RibbonDescription");
    const Object* search = f.Find("editFinderSearch");
    CHECK(search && search->type == "EditDescription");
    CHECK(f.Find("tableFinderAll") && f.Find("ribbonFinderCell") && f.Find("buttonFinderOpenFav"));
    CHECK(f.Find("buttonFinderKey") && f.Find("textFinderKey"));
    CHECK(f.Find("buttonFinderPick") && f.Find("buttonFinderStar") && f.Find("buttonFinderClear"));
    CheckKnownTypes(f, "BuildingFinderPage.rsc");
}

UNIT_TEST(ext_rsc_text_parse_favorites_window) {
    File w;
    CHECK(ParseEmbedded("Dialog/FavoritesWindow.rsc", w));
    CHECK(w.Find("dialogRoot") && w.Find("tableFavoritesWindow") && w.Find("textFavoritesWindowNote"));
    CHECK(w.Find("buttonFavoriteIcon") && w.Find("imageFavoriteIcon") && w.Find("buttonFavoritesOpenPage"));
    CheckKnownTypes(w, "FavoritesWindow.rsc");
}

// ext.fullscreen_mode's Video tab cell: the group fullscreen_mode.cpp TakeCell expects, combo first, the
// checkbox's slot second.
UNIT_TEST(ext_rsc_text_parse_fullscreen_mode) {
    File f;
    CHECK(ParseEmbedded("Dialog/FullscreenMode.rsc", f));
    const Object* cell = f.Find("fullscreenModeCell");
    CHECK(cell && cell->type == "GroupDescription");
    const Object* combo = f.Find("comboFullscreenMode");
    CHECK(combo && combo->type == "ComboDescription");
    const Object* slot = f.Find("fullscreenModeSlot");
    CHECK(slot && slot->type == "GroupDescription");
    // The combo's items: a StringTable, as EnhancedOptions.rsc's combos (CheckKnownTypes lists the
    // element descriptions only, so it is not run on this file).
    const Object* items = f.Find("fullscreenModeItems");
    CHECK(items && items->type == "StringTable");
}

// ext.map_overlays's views in the map window: the row map_window.cpp injects at userGroup0, the map's cell, the
// column of view buttons, the pages group, the four page kinds and the names the controller looks up in them.
UNIT_TEST(ext_rsc_text_parse_map_overlays) {
    File f;
    CHECK(ParseEmbedded("Dialog/MapOverlays.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // not a dialog: retail's map window hosts it
    const Object* column = f.Find("ribbonMapOverlays");
    CHECK(column && column->type == "RibbonDescription");
    CHECK(f.Find("groupMapOverlaysMapSlot") && f.Find("columnMapOverlaysViews"));
    const Object* button = f.Find("buttonMapOverlaysTab");
    CHECK(button && button->type == "ButtonDescription");
    const Object* pages = f.Find("groupMapOverlaysPages");
    CHECK(pages && pages->type == "GroupDescription");
    CHECK(f.Find("pageMapOverlaysMap") && f.Find("pageMapOverlaysResource") && f.Find("pageMapOverlaysKind") &&
          f.Find("pageMapOverlaysTraffic"));
    CHECK(f.Find("comboMapOverlaysCategory") && f.Find("comboMapOverlaysResource") &&
          f.Find("comboMapOverlaysKind") && f.Find("rowMapOverlaysMonths") && f.Find("sliderMapOverlaysMonths") &&
          f.Find("sliderMapOverlaysScale") && f.Find("ribbonMapOverlaysRamp") &&
          f.Find("textMapOverlaysLegendMax") && f.Find("checkMapOverlaysTerrain"));
    CheckKnownTypes(f, "MapOverlays.rsc");
}

UNIT_TEST(ext_rsc_text_parse_professions_window) {
    File w;
    CHECK(ParseEmbedded("Dialog/ProfessionsWindow.rsc", w));
    CHECK(w.Find("dialogRoot") && w.Find("tableProfessions") && w.Find("containerProfessionsBox"));
    CheckKnownTypes(w, "ProfessionsWindow.rsc");
}

UNIT_TEST(ext_rsc_text_parse_show_grid_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/ShowGridPage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageShowGrid
    const Object* page = f.Find("pageShowGrid");
    CHECK(page && page->type == "RibbonDescription");
    const Object* shown = f.Find("checkShowGridShown");
    CHECK(shown && shown->type == "CheckDescription");
    CHECK(f.Find("sliderShowGridOpacity") && f.Find("sliderShowGridShade") && f.Find("sliderShowGridWidth"));
    CheckKnownTypes(f, "ShowGridPage.rsc");
}

UNIT_TEST(ext_rsc_text_parse_town_screenshot_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/TownScreenshotPage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageTownScreenshot
    const Object* page = f.Find("pageTownScreenshot");
    CHECK(page && page->type == "RibbonDescription");
    CHECK(f.Find("comboTownScreenshotQuality") && f.Find("comboTownScreenshotAngle") && f.Find("comboTownScreenshotTurn"));
    CHECK(f.Find("buttonTownScreenshotTake") && f.Find("buttonTownScreenshotCancel") && f.Find("textTownScreenshotStatus"));
    const Object* weather = f.Find("checkTownScreenshotWeather");
    CHECK(weather && weather->type == "CheckDescription");
    const Object* snow = f.Find("checkTownScreenshotSnow");
    CHECK(snow && snow->type == "CheckDescription");
    const Object* take = f.Find("buttonTownScreenshotTake");
    CHECK(take && take->type == "ButtonDescription");
    const Object* view = f.Find("buttonTownScreenshotView");
    CHECK(view && view->type == "ButtonDescription" && f.Find("imageTownScreenshotView"));
    CheckKnownTypes(f, "TownScreenshotPage.rsc");
}

UNIT_TEST(ext_rsc_text_parse_terrain_edit_page) {
    File f;
    CHECK(ParseEmbedded("Dialog/TerrainEditPage.rsc", f));
    CHECK(f.Find("resource") == nullptr);   // a page, not a dialog: the root object is pageTerrainEdit
    const Object* page = f.Find("pageTerrainEdit");
    CHECK(page && page->type == "RibbonDescription");
    CHECK(f.Find("textTerrainEditHint") && f.Find("textTerrainEditHint2")
        && f.Find("textTerrainEditHint3") && f.Find("textTerrainEditHint4") &&
          f.Find("textTerrainEditStatus"));
    CHECK(f.Find("rowTerrainEditModes") && f.Find("rowTerrainEditSize") && f.Find("rowTerrainEditStrength"));
    CHECK(f.Find("buttonTerrainEditRaise") && f.Find("buttonTerrainEditLower")
        && f.Find("buttonTerrainEditFlatten") && f.Find("buttonTerrainEditMountain")
        && f.Find("buttonTerrainEditSmooth") && f.Find("buttonTerrainEditUndo"));
    CHECK(f.Find("imageTerrainEditRaise") && f.Find("imageTerrainEditUndo"));
    CHECK(f.Find("sliderTerrainEditSize") && f.Find("sliderTerrainEditStrength"));
    // The six tools are check boxes (the one in use stays lit, with its own lit glyph as the check mark);
    // Undo is a plain button.
    const Object* raise = f.Find("buttonTerrainEditRaise");
    CHECK(raise && raise->type == "CheckDescription" && raise->body.Find("_check") != nullptr);
    const Object* plantButton = f.Find("buttonTerrainEditPlant");
    CHECK(plantButton && plantButton->type == "CheckDescription");
    CHECK(plantButton->body.Find("_check") != nullptr);
    CHECK(f.Find("imageTerrainEditLitRaise") && f.Find("imageTerrainEditLitPlant"));
    const Object* undoButton = f.Find("buttonTerrainEditUndo");
    CHECK(undoButton && undoButton->type == "ButtonDescription");
    const Object* redoButton = f.Find("buttonTerrainEditRedo");
    CHECK(redoButton && redoButton->type == "ButtonDescription" && f.Find("imageTerrainEditRedo"));
    CheckKnownTypes(f, "TerrainEditPage.rsc");
}

// TOUCHMODE (docs/android/TOUCH_MODE.md): the cluster dialog. The controller finds the five holders and buttons
// by name; every look is the game's own (the toolbar's raised button, SpriteSheet.rsc's glyphs).
UNIT_TEST(ext_rsc_text_parse_touch_controls) {
    File f;
    CHECK(ParseEmbedded("Dialog/TouchControls.rsc", f));
    const Object* dialog = f.Find("resource");
    CHECK(dialog && dialog->type == "Dialog");
    const char* names[] = {"Pad", "RotateLeft", "RotateRight", "ZoomIn", "ZoomOut"};
    for (const char* n : names) {
        const Object* holder = f.Find((std::string("holderTouch") + n).c_str());
        const Object* button = f.Find((std::string("buttonTouch") + n).c_str());
        CHECK(holder && holder->type == "GroupDescription");
        CHECK(button && button->type == "ButtonDescription");
    }
    const Object* content = f.Find("groupTouchContent");
    CHECK(content && content->type == "GroupDescription");
    const Object* pad = f.Find("buttonTouchPad");
    CHECK(pad && pad->base == "Toolbar.rsc:buttonToolbar");
    const Object* arrow = f.Find("imageTouchArrow");
    CHECK(arrow && arrow->body.Find("_spriteSheet") &&
          arrow->body.Find("_spriteSheet")->tokens[0].text == "SpriteSheet.rsc");
    // Issue #96 (the reporter's follow-up): the corner camera buttons, a second dialog of the file around the same
    // four rotate / zoom holders (the controller finds them by the cluster's names), no pad.
    const Object* corner = f.Find("cameraButtons");
    CHECK(corner && corner->type == "Dialog" && corner->body.Find("_rootElement") &&
          corner->body.Find("_rootElement")->tokens[0].text == "cameraButtonsRoot");
    const Object* cornerContent = f.Find("groupCameraButtonsContent");
    CHECK(cornerContent && cornerContent->type == "GroupDescription");
    if (cornerContent && cornerContent->body.Find("_elements")) {
        const Field* elements = cornerContent->body.Find("_elements");
        CHECK(elements->tokens.size() == 4);
        for (const auto& t : elements->tokens) CHECK(t.text != "holderTouchPad" && f.Find(t.text.c_str()) != nullptr);
    }
    CheckKnownTypes(f, "TouchControls.rsc");
}

// P11 (docs/android/P11_TOUCH_POINTER.md): the action bar and the aim marker. The controller finds the five holders
// and buttons by name; every look is the game's own (the toolbar's button, SpriteSheet.rsc's glyphs).
UNIT_TEST(ext_rsc_text_parse_touch_pointer) {
    File f;
    CHECK(ParseEmbedded("Dialog/TouchPointer.rsc", f));
    const Object* dialog = f.Find("resource");
    CHECK(dialog && dialog->type == "Dialog");
    const Object* marker = f.Find("marker");
    CHECK(marker && marker->type == "Dialog");
    const char* names[] = {"Place", "RotateLeft", "RotateRight", "Flip", "Shift", "Cancel"};
    for (const char* n : names) {
        const Object* holder = f.Find((std::string("holderPointer") + n).c_str());
        const Object* button = f.Find((std::string("buttonPointer") + n).c_str());
        CHECK(holder && holder->type == "GroupDescription");
        CHECK(button && button->type == "ButtonDescription");
    }
    const Object* place = f.Find("buttonPointerPlace");
    CHECK(place && place->base == "Toolbar.rsc:buttonToolbar");
    CHECK(f.Find("imagePointerMarker") != nullptr);
    CheckKnownTypes(f, "TouchPointer.rsc");
}

// P11b (docs/android/P11B_SCROLL_SIZE.md): the scrolling frame ext.phone_menu moves a page into.
UNIT_TEST(ext_rsc_text_parse_phone_scroll) {
    File f;
    CHECK(ParseEmbedded("Dialog/PhoneScroll.rsc", f));
    const Object* frame = f.Find("phoneScroll");
    CHECK(frame && frame->type == "ContainerDescription" && frame->base == "Options.rsc:gameOptions");
    CHECK(frame && frame->body.Find("_height") && frame->body.Find("_height")->tokens[0].text == "0");
    CHECK(f.Find("phoneScrollContent") != nullptr);
    CheckKnownTypes(f, "PhoneScroll.rsc");
}

UNIT_TEST(ext_rsc_text_syntax_errors) {
    File f;
    std::string err;
    CHECK(!Parse("Dialog x { int _a = 1 }", 22, f, err) && err.find("line 1") == 0);   // no ';'
    CHECK(!Parse("Dialog x : base { }", 19, f, err));                                  // base not quoted
    CHECK(!Parse("Dialog x { int _a [ 1 { } ] }", 29, f, err));                        // mixed array
    CHECK(!Parse("Dialog x { String _s = \"open; }", 31, f, err));                     // unterminated
    const char* ok = "// c\nT n : \"B.rsc\" { bool _b = true; Flags _f = A | B; X _x [ ]\n}";
    CHECK(Parse(ok, std::strlen(ok), f, err) && f.objects.size() == 1 && f.objects[0].body.fields.size() == 3);
}

UNIT_TEST(ext_rsc_text_resolve_reference) {
    CHECK(ResolveReference("SharedElements.rsc:dialogBorder", "Dialog/", "Extensions.rsc") == "Dialog/SharedElements.rsc:dialogBorder");
    CHECK(ResolveReference("dialogRoot", "Dialog/", "Extensions.rsc") == "Dialog/Extensions.rsc:dialogRoot");
    CHECK(ResolveReference("Font/FontSmall.rsc", "Dialog/", "Extensions.rsc") == "Font/FontSmall.rsc:resource");
    CHECK(ResolveReference("Font\\FontMedium.rsc", "Dialog/", "EnhancedOptions.rsc") == "Font/FontMedium.rsc:resource");
    CHECK(ResolveReference("MessageBox.rsc", "Dialog/", "Extensions.rsc") == "Dialog/MessageBox.rsc:resource");
    CHECK(ResolveReference("EnhancedOptions.rsc:aoStrings", "Dialog/", "EnhancedOptions.rsc") == "Dialog/EnhancedOptions.rsc:aoStrings");
    CHECK(ResolveReference("ExtensionsStringTable.rsc", "Dialog/", "Extensions.rsc") == "Dialog/ExtensionsStringTable.rsc:resource");
}

// Every `ext:` load notice DoLoad can pick (src/ext/load_notice.h) names a Dialog
// object of the embedded Dialog/Extensions.rsc, under a prefix InitText registers
// ("Dialog/Extensions"). DoLoad registers that provider on demand before the notice
// is loaded, under any profile (known_port_issues row 1330: under /profile:faithful
// the provider was never registered and the null Dialog crashed MessageDialog).
UNIT_TEST(ext_rsc_text_load_notices_are_embedded) {
    using N = Ext::PseudoModNotice;
    File f;
    CHECK(ParseEmbedded("Dialog/Extensions.rsc", f));
    for (N n : {N::NotInBuild, N::NewerVersion, N::TurnedOff}) {
        const char* path = Ext::PseudoModNoticeDialog(n);
        CHECK(path != nullptr);
        if (path == nullptr) continue;
        const std::string p(path);
        const size_t colon = p.find(':');
        CHECK(colon != std::string::npos);
        if (colon == std::string::npos) continue;
        CHECK(p.substr(0, colon) == "Dialog/Extensions.rsc");
        CHECK(p.compare(0, std::strlen("Dialog/Extensions"), "Dialog/Extensions") == 0);
        const Object* o = f.Find(p.substr(colon + 1).c_str());
        CHECK(o != nullptr && o->type == "Dialog" && o->base == "SaveLoad.rsc:modsMissing");
    }
}

// F-038 (review 2026-11-03): ParseBlock -> ParseField -> ParseBlock recursed once per
// `T f[{` level with no cap, so 5 bytes of hostile text per level overflowed the stack.
// The embedded files nest blocks at most 2 deep; the parser refuses past 64.
UNIT_TEST(ext_rsc_text_nesting_depth_limit) {
    auto nested = [](int levels) {
        std::string s = "A b {";
        for (int i = 0; i < levels; ++i)
            s += "T f[{";
        for (int i = 0; i < levels; ++i)
            s += "}]";
        s += "}";
        return s;
    };
    std::string err;
    File f;
    const std::string ok = nested(63);   // 64 blocks: the object body plus 63 array elements
    CHECK(Parse(ok.data(), ok.size(), f, err));
    File g;
    const std::string deep = nested(64);   // 65 blocks
    CHECK(!Parse(deep.data(), deep.size(), g, err) && err.find("nested too deep") != std::string::npos);
    // The fuzz input (scratchpad/crashes/rsc_text/deep_nesting_50000): "A b {" + "T f[{" x 50000.
    std::string hostile = "A b {";
    for (int i = 0; i < 50000; ++i)
        hostile += "T f[{";
    File h;
    CHECK(!Parse(hostile.data(), hostile.size(), h, err) && err.find("nested too deep") != std::string::npos);
}
