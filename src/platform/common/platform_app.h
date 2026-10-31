#pragma once
// ============================================================================
// src/platform/common/platform_app.h -- app lifecycle hooks around SDL_Init / SDL_Quit
// ============================================================================
// NOT RETAIL. The four spots in src/app/main.cpp where a platform may need a step of
// its own in the process lifecycle. Both entry points (WinMain, POSIX main) call them
// at the same places:
//   EnterRunDirectory() -- the first statement, before the log files are opened
//   PreSdlInitHints() -- after the shared SDL hints, immediately before SDL_Init
//   PostSdlInit()     -- after SDL_Init and the drop-event disables, before Init()
//   Shutdown()        -- immediately before every SDL_Quit (the Init-failed path too)
// D4_RULES C1: namespace `Platform`, free functions, NO OS header here. Bodies:
// src/platform/win/app_win.cpp and src/platform/posix/app_posix.cpp (both empty:
// neither platform has a step there today), the Darwin twin and the Android twin
// (src/platform/darwin/README.md, src/platform/android/README.md).
// ============================================================================

namespace Platform {
namespace App {

// The logs, the crash and hang dumps and the boot state are written in the working
// directory. Windows, Linux and macOS run where they were launched (the game directory,
// by the launcher scripts and tools/build.sh); an Android process starts in "/", so the
// Android twin moves to the app's data folder (the game directory there) and starts the
// logcat copy of standalone_error.log.
void EnterRunDirectory();
void PreSdlInitHints();
void PostSdlInit();
void Shutdown();

// The second half of the missing-game-data message (src/app/game_data_check.h, issue #72): where
// this platform's player copies the WinData packages from and how, after the shared text has named
// the folder the game looked in. A static string; the box is SDL's on every platform.
const char* MissingGameDataHelp();

// True once for every save the platform asked for since the last call (main thread, the game's
// update). Android asks at SDL_EVENT_LOW_MEMORY and SDL_EVENT_TERMINATING (PLAN.md §4.6): the
// process may be killed without a quit. Never true on Windows, Linux or macOS.
bool TakeSaveRequest();

}  // namespace App
}  // namespace Platform
