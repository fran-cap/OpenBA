#pragma once
// ============================================================================
// src/platform/common/platform_pick_file.h -- the system's "open a file" dialog
// ============================================================================
// NOT RETAIL. ext.custom_maps' Load custom map button asks for a .bmap file with it
// (docs/extensions/handins/CUSTOMMAPS.md). Windows: GetOpenFileNameW
// (src/platform/win/pick_file_win.cpp). Every other platform has no such dialog yet:
// CanPickFile says so and PickOpenFile refuses (src/platform/posix/pick_file_posix.cpp).
// ============================================================================

#include <string>

namespace Platform {

// Whether this platform has a file dialog to open.
bool CanPickFile();

// Opens the dialog and waits for the player. `title` is the window title, `filterName` and
// `filterPattern` the one kind of file the dialog lists ("Banished maps (*.bmap)", "*.bmap"),
// besides "All files". `startDir` is the folder it opens in (UTF-8; "" lets the system choose).
// True, with the chosen file's path (UTF-8) in `out`, when the player picked a file; false
// when they cancelled or the platform has no dialog.
bool PickOpenFile(const char* title, const char* filterName, const char* filterPattern,
                  const std::string& startDir, std::string& out);

}  // namespace Platform
