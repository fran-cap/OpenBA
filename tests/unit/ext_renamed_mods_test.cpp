// ============================================================================
// tests/unit/ext_renamed_mods_test.cpp -- ext.renamed_mods's choice of a package's own root list
// ============================================================================
// src/ext/renamed_mods/renamed_mods_math.h without the game. The entry lists are shaped like real
// packages: the root level of EBStatueLiberty.pkm, and of two packages whose file was renamed by a
// player (ChooChoo_NotMineVariant.pkm, RenewableResourcesOld.pkm).
// ============================================================================
#include "unit.h"
#include "ext/renamed_mods/renamed_mods_math.h"

#include <string>
#include <vector>

using namespace Ext::RenamedMods;

namespace {

using Names = std::vector<std::string>;

Names Liberty() {
    return {"EBStatueLiberty.index",
            "EBStatueLiberty_EBStatueLiberty.crs",
            "EBStatueLibertyResources_resource.crs",
            "Template\\EBStatueLiberty_resource.crs",
            "Models\\EBStatueLibertyMesh_resource.crs"};
}

// two root lists, the package's own name in the index
Names Big() {
    return {"Big.index", "BigResources_resource.crs", "BundledResources_resource.crs"};
}

}  // namespace

UNIT_TEST(ext_renamed_mods_a_package_named_after_its_file_needs_nothing) {
    const Choice c = ChooseRoot("EBStatueLiberty", Liberty());
    CHECK(c.kind == Kind::Named);
    CHECK(c.prefix == "EBStatueLiberty");
    CHECK_EQ(c.candidates, 1);
}

UNIT_TEST(ext_renamed_mods_the_case_of_the_file_name_does_not_matter) {
    // the game compares paths without case: MEGAMOD.pkm still finds MegaModResources
    CHECK(ChooseRoot("ebstatueliberty", Liberty()).kind == Kind::Named);
    CHECK(ChooseRoot("EBSTATUELIBERTY", Liberty()).kind == Kind::Named);
}

UNIT_TEST(ext_renamed_mods_a_renamed_file_finds_the_one_list_the_package_has) {
    // ChooChoo_NotMineVariant.pkm holds ChooChooResources_resource.crs: the file name asks for
    // ChooChoo_NotMineVariantResources
    const Names pkg = {"ChooChoo.index",
                       "ChooChoo_ChooChoo.crs",
                       "ChooChooResources_resource.crs",
                       "Template\\ChooChoo_resource.crs"};
    const Choice c = ChooseRoot("ChooChoo_NotMineVariant", pkg);
    CHECK(c.kind == Kind::Found);
    CHECK(c.prefix == "ChooChoo");
    CHECK_EQ(c.candidates, 1);
    // a package whose own name already ends in "Resources": the list is RenewableResourcesResources
    const Names res = {"RenewableResources.index", "RenewableResourcesResources_resource.crs"};
    const Choice r = ChooseRoot("RenewableResourcesOld", res);
    CHECK(r.kind == Kind::Found);
    CHECK(r.prefix == "RenewableResources");
}

UNIT_TEST(ext_renamed_mods_only_root_level_lists_count) {
    // a list inside a folder is some other file of the mod, not the package's root list
    const Names pkg = {"X.index",
                       "XResources_resource.crs",
                       "Template\\YResources_resource.crs",
                       "Dialog/ZResources_resource.crs"};
    const Choice c = ChooseRoot("Renamed", pkg);
    CHECK(c.kind == Kind::Found);
    CHECK(c.prefix == "X");
    CHECK_EQ(c.candidates, 1);
}

UNIT_TEST(ext_renamed_mods_a_package_with_no_root_list_is_left_alone) {
    const Names pkg = {"X.index", "X_X.crs", "Template\\YResources_resource.crs"};
    const Choice c = ChooseRoot("Renamed", pkg);
    CHECK(c.kind == Kind::None);
    CHECK_EQ(c.candidates, 0);
    CHECK(ChooseRoot("Renamed", Names()).kind == Kind::None);
}

UNIT_TEST(ext_renamed_mods_several_root_lists_are_told_apart_by_the_package_name) {
    const Choice c = ChooseRoot("BigRenamed", Big());
    CHECK(c.kind == Kind::Found);
    CHECK(c.prefix == "Big");
    CHECK_EQ(c.candidates, 2);
    // the index names a list the package does not hold, or there is no index, or two: no guess
    const Names otherIndex = {"Other.index", "BigResources_resource.crs", "BundledResources_resource.crs"};
    CHECK(ChooseRoot("BigRenamed", otherIndex).kind == Kind::Ambiguous);
    const Names noIndex = {"BigResources_resource.crs", "BundledResources_resource.crs"};
    CHECK(ChooseRoot("BigRenamed", noIndex).kind == Kind::Ambiguous);
    const Names twoIndex = {"Big.index", "Bundled.index", "BigResources_resource.crs",
                            "BundledResources_resource.crs"};
    CHECK(ChooseRoot("BigRenamed", twoIndex).kind == Kind::Ambiguous);
}

UNIT_TEST(ext_renamed_mods_the_file_name_wins_when_it_is_one_of_several) {
    // two lists, one named after the file: the game finds it already, nothing to change
    CHECK(ChooseRoot("Big", Big()).kind == Kind::Named);
    CHECK(ChooseRoot("bundled", Big()).kind == Kind::Named);
}

UNIT_TEST(ext_renamed_mods_a_bare_suffix_is_not_a_list) {
    // an entry that is only "Resources_resource.crs" has no name in front of it
    CHECK(ChooseRoot("X", {"Resources_resource.crs"}).kind == Kind::None);
    CHECK(ChooseRoot("X", {".index", "Resources_resource.crs"}).kind == Kind::None);
}
