// ============================================================================
// src/ext/priority_after_load/priority_after_load.h -- fix.priority_after_load: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md section 34. Standard C++ only (tests/unit/ext_priority_after_load_test.cpp).
//
// A job's place in the idle work search is Work::_queueOrder (+0x24), ascending. Increase Priority
// (WorkScheduler::PrioritizeWork 0x1400f9ad0) gives a job the lowest key of the scheduler's queue-order
// tree (+0x10) minus one, or 0 when the tree is empty. The keys are saved with the jobs; the tree is
// not, and WorkScheduler::Serialize (0x1400f9240) rebuilds it on load only for a version-2 save
// (`cmp eax,2` at 0x1400f934f). The game writes version 3, so after any load the tree is empty and a
// new priority starts at 0 again: behind every job prioritized before the save that still waits.
//
// The fix inserts each loaded job under its own saved key, which is what the tree held when the save
// was written. The seam is the load arm of WorkScheduler::Serialize (src/game/Game/goals_serialize.cpp).
// ============================================================================
#pragma once

namespace Ext {
namespace PriorityAfterLoad {

// The save version whose load arm retail rebuilds the tree for (keyed by a running counter, not by the
// jobs' keys: left as retail has it).
constexpr int kRetailRebuildVersion = 2;

// Whether the load of a save of `version` inserts each job under its saved key. Older saves keep
// retail's arm (version 2) or have no saved key to trust (below it).
inline bool RebuildsFromSavedKeys(bool fixOn, int version) {
    return fixOn && version > kRetailRebuildVersion;
}

// The key Increase Priority gives next: one below the lowest key of the tree, 0 for an empty tree
// (retail's rule, 0x1400f9ad0). With the tree rebuilt the lowest key is the lowest saved one.
inline int NextPriorityKey(bool treeEmpty, int lowestKey) {
    return treeEmpty ? 0 : lowestKey - 1;
}

}  // namespace PriorityAfterLoad
}  // namespace Ext
