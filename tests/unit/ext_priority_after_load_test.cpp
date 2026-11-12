// ============================================================================
// tests/unit/ext_priority_after_load_test.cpp -- fix.priority_after_load's rule
// ============================================================================
// src/ext/priority_after_load/priority_after_load.h without the world: which loads rebuild the
// queue-order tree from the saved keys, and what Increase Priority then hands out. The numbers are a
// player's save (docs/retail_bugs.md section 34): keys -2146 .. 5982, three of them negative.
// ============================================================================
#include "unit.h"

#include "ext/priority_after_load/priority_after_load.h"

using namespace Ext::PriorityAfterLoad;

UNIT_TEST(ext_priority_after_load_rebuilds_only_the_written_version_with_the_row_on) {
    CHECK(RebuildsFromSavedKeys(true, 3));
    CHECK(!RebuildsFromSavedKeys(false, 3));   // the row off: retail's empty tree
    CHECK(!RebuildsFromSavedKeys(true, 2));    // retail's own arm rebuilds a version-2 save
    CHECK(!RebuildsFromSavedKeys(true, 1));
    CHECK(!RebuildsFromSavedKeys(true, 0));
}

UNIT_TEST(ext_priority_after_load_next_key_is_below_the_saved_priorities) {
    // retail after a load: the tree is empty, the next priority is 0, behind the saved -2146
    CHECK_EQ(NextPriorityKey(true, 0), 0);
    CHECK(NextPriorityKey(true, 0) > -2146);
    // with the tree rebuilt its lowest key is the lowest saved one: the next priority is ahead of it
    CHECK_EQ(NextPriorityKey(false, -2146), -2147);
    CHECK(NextPriorityKey(false, -2146) < -2146);
}
