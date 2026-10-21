// ============================================================================
// tests/unit/resourcelimitui_index_test.cpp -- which ledger entry a limit row's config names
// ============================================================================
// src/game/Dialog/resourcelimitui_index.h on its own. The vanilla Town Hall's 11 rows name entries 0..10 of
// retail's 21-entry ledger; the custom limits Custom0..Custom9, which the modkit documents and mods can add
// rows for, are entries 11..20. Retail indexes the ledger with that value directly.
// ============================================================================
#include "unit.h"
#include "Dialog/resourcelimitui_index.h"

using resourcelimitui_sa::ConfigLedgerIndex;

namespace {
constexpr int kRetailLedger = 21;   // CitizenBehavior::professionResources, retail's entry count
}  // namespace

UNIT_TEST(resourcelimitui_index_vanilla_rows_keep_their_entry) {
    for (int i = 0; i <= 10; ++i)
        CHECK_EQ(ConfigLedgerIndex(i, kRetailLedger), i);
}

UNIT_TEST(resourcelimitui_index_custom_rows_name_entries_11_to_20) {
    for (int i = 11; i <= 20; ++i)
        CHECK_EQ(ConfigLedgerIndex(i, kRetailLedger), i);   // these used to fall to the dispatch order
}

UNIT_TEST(resourcelimitui_index_out_of_range_names_no_entry) {
    CHECK_EQ(ConfigLedgerIndex(-1, kRetailLedger), -1);
    CHECK_EQ(ConfigLedgerIndex(kRetailLedger, kRetailLedger), -1);   // one past the last entry
    CHECK_EQ(ConfigLedgerIndex(1000, kRetailLedger), -1);
}

UNIT_TEST(resourcelimitui_index_the_bound_is_the_ledgers_size) {
    CHECK_EQ(ConfigLedgerIndex(30, 42), 30);   // a longer ledger accepts the entries it has
    CHECK_EQ(ConfigLedgerIndex(41, 42), 41);
    CHECK_EQ(ConfigLedgerIndex(42, 42), -1);
    CHECK_EQ(ConfigLedgerIndex(0, 0), -1);     // an empty ledger names nothing
}
