#pragma once
// ============================================================================
// src/game/Dialog/resourcelimitui_index.h -- which ledger entry a limit row's config names
// ============================================================================
// ResourceLimitUI's Slot4 / Slot7 / Slot9 (retail 0x140077970 / 0x1400778d0 / 0x1400777e0) index the
// town's resource ledger (CitizenBehavior::professionResources, 0x20 bytes an entry) with the bound
// config's resource index and nothing else: no upper bound. The vanilla Town Hall shows 11 rows, entries
// 0..10. Retail's ledger has ten more, the custom resource limits Custom0..Custom9 (entries 11..20) that
// the modkit documents for modders; a mod that adds rows for them (the modkit's own limitresource example
// does) names those entries.
//
// Standalone differs from retail in one way: a row's config may not be bound, and then the caller falls
// back to the dispatch order. So the index is checked against the ledger's size (retail's is always 21)
// and -1 means "this config names no entry".
// ============================================================================

namespace resourcelimitui_sa {

inline int ConfigLedgerIndex(int configIndex, int ledgerSize)
{
    if (configIndex >= 0 && configIndex < ledgerSize)
        return configIndex;
    return -1;
}

}  // namespace resourcelimitui_sa
