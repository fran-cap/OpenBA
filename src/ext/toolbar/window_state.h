// ============================================================================
// src/ext/toolbar/window_state.h -- the extension window's per-town state and its host record
// ============================================================================
// NOT RETAIL. DESIGN.md "Host records" (ruled 2026-10-21). Retail keeps an open utility window in the save
// (PlayerToolManager's pinned set: the window's template, x, y, an open byte; ToggleUtilityDialog writes
// it, PlayerToolManager::Startup reopens every open one). The extension window cannot be a member of that
// set (its key is a retail template path), so the same record lives in the save tail as the host record
// `toolbar`: whether the window is open, where, and which page is showing (by the page's id, never a tab
// index). Like retail's record it stays when the window closes, so a reopened window comes back where it was.
//
// Plain state only: the save worker encodes it (savetail.h, ENCODE RUNS ON THE SAVE WORKER). The window
// (toolbar_window.cpp) writes it from the UI thread, never during a frozen update.
// Standard C++ (tests/unit/ext_savetail_test.cpp links it).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "ext/savetail.h"

namespace Ext {
namespace Toolbar {

struct WindowState {
    bool        open = false;   // reopened with the town (PlayerToolManager::Startup's EXT SEAM)
    int32_t     x    = 24;      // the upper-left corner, in the UI's scaled pixels, as SetUpperLeft takes it
    int32_t     y    = 120;
    std::string page;           // the showing page's Contribution id; empty = the first page
};

// This town's window state (defaults for a new game).
WindowState& SavedWindow();

// The host record `toolbar` (host_codecs.cpp). Wire format v1 (little-endian): u8 open, i32 x, i32 y,
// u32 length + the page id's bytes (0..kMaxPageIdLength, [a-z0-9_]).
const SaveTail::HostCodec& WindowCodec();
constexpr uint32_t kMaxPageIdLength = 64;

}  // namespace Toolbar
}  // namespace Ext
