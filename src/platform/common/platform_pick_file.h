#pragma once
// ============================================================================
// src/platform/common/platform_pick_file.h -- the system's "open a file" dialog
// ============================================================================
// NOT RETAIL. ext.custom_maps' Browse button asks for a .bmap file with it, and so does the Load
// custom map button when the maps folder is empty (docs/extensions/handins/CUSTOMMAPS.md); the
// in-game Custom map picker needs no such dialog. One body on every platform,
// src/platform/sdl/pick_file_sdl.cpp, over SDL's file dialog: GetOpenFileNameW on Windows, the
// system document picker on Android, the desktop portal or zenity on Linux, NSOpenPanel on macOS.
//
// The dialog does not block the game: BeginPickOpenFile opens it and returns, the player's answer
// arrives on another thread (Windows, Android) or during a later frame's event pump (Linux, macOS),
// and the game reads it with PollPickedFile from its own frame. One dialog at a time.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace Platform {

// Whether this build has a file dialog to open. A true answer can still fail at run time (a Linux
// desktop with neither the file chooser portal nor zenity): PollPickedFile then says Failed.
bool CanPickFile();

// Opens the dialog. `title` is the window title, `filterName` and `filterExtension` the one kind of
// file it lists ("Banished maps (*.bmap)", "bmap": an extension without "*."), besides "All files".
// `startDir` is the folder it opens in (UTF-8; "" lets the system choose; Android ignores it).
// False when a dialog is already open or none could be opened: nothing will arrive then.
bool BeginPickOpenFile(const char* title, const char* filterName, const char* filterExtension,
                       const std::string& startDir);

enum class PickState {
    None,        // no dialog was opened, or its answer was already read
    Open,        // the dialog is up: no answer yet
    Picked,      // the player chose a file: `out` holds it (a path, or an Android content:// URI)
    Cancelled,   // the player closed the dialog without a choice
    Failed,      // the dialog could not be shown; `out` holds the system's reason
};

// The answer of the dialog BeginPickOpenFile opened. Picked, Cancelled and Failed are returned once;
// the next call says None. Main thread.
PickState PollPickedFile(std::string& out);

// The bytes of a file PollPickedFile handed back: a path, or (Android) a content:// URI, which only
// the system can open. False, with `why`, when it cannot be read.
bool ReadPickedFile(const std::string& picked, std::vector<uint8_t>& out, std::string& why);

}  // namespace Platform
