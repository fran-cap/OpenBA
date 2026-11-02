// ============================================================================
// src/ext/save_failure_notice/save_failure_notice_rule.h -- fix.save_failure_notice: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §29. Standard C++ only, so
// tests/unit/ext_save_failure_notice_test.cpp links it without the world.
// ============================================================================
#pragma once

namespace Ext {
namespace SaveFailureNotice {

// Whether this save result opens the notice. The first failure after a success (or after boot)
// does; further failures in a row do not, so a failing autosave does not stack a box every few
// minutes. A success re-arms it. `shownSinceSuccess` is the state, updated here.
inline bool ShouldNotify(bool saved, bool& shownSinceSuccess) {
    if (saved) {
        shownSinceSuccess = false;
        return false;
    }
    if (shownSinceSuccess) return false;
    shownSinceSuccess = true;
    return true;
}

}  // namespace SaveFailureNotice
}  // namespace Ext
