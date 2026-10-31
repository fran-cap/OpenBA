#pragma once
// ============================================================================
// src/app/game_data_check.h -- is the retail game data where the loader will look?
// ============================================================================
// NOT RETAIL (issue #72). Retail ships beside its WinData folder and never asks. A standalone
// exe can be unpacked anywhere, and the Android app starts with the empty WinData/ it made
// itself, so with no packages the loader mounts nothing and boot fails with no explanation.
// Both entry points in src/app/main.cpp ask this before Init(): when the data is not there,
// one message box names the folder and says what to copy into it (the per-platform text is
// Platform::App::MissingGameDataHelp, platform_app.h), the boot state reads NO_GAME_DATA and
// the process exits with kExitNoGameData.
//
// The folder is the one IO::FileSystem::SetupDirectories computes on a plain boot:
// <the executable's directory>WinData (src/engine/io/filesystem_setupdirs.cpp). A command
// line or <root>cmd.txt that moves it (/pathpkg, /bin) skips the check: the loader then looks
// elsewhere and is left to answer for itself.
//
// The rule for "present" is the macOS launcher's (docs/macos/README_MAC.md): the folder holds
// data0.pkg, the base package. patch0/patch1 and data1 are mounted when present but are not
// what makes a folder a game folder.
//
// Judge and CommandLineMovesPackages are pure (tests/unit/game_data_check_test.cpp); Probe
// (game_data_probe.cpp) reads the disk through Platform:: (platform_file.h).
// ============================================================================

#include <string>
#include <vector>

namespace GameData {

// The process exit code of a boot stopped here. 1 is Init()'s failure, 3 a crash, 4 a hang,
// 255 retail's fatal Engine::Startup box; 6 is the one boot_state.sh also uses for this state.
constexpr int kExitNoGameData = 6;

enum class Verdict {
    Present,         // WinData holds data0.pkg
    FolderMissing,   // no WinData folder at all
    NoPackages,      // the folder is there, and holds no *.pkg
    NoBasePackage,   // *.pkg files are there, but not data0.pkg
};

// The verdict from what the disk holds: whether <root>WinData is a folder, and the names of the
// regular files in it that match *.pkg (any case, as the loader's FindFiles matches them).
Verdict Judge(bool winDataIsFolder, const std::vector<std::string>& pkgNames);

// The verdict's name in the [BootState] line and boot_state.txt's detail.
const char* VerdictName(Verdict v);

// True when the command line carries a /pathpkg or /bin switch (Configuration::ParseCommand's
// syntax: '/', the name up to a blank or ':'; the name compared without case): either one moves
// the folder the packages are read from.
bool CommandLineMovesPackages(const char* cmdLine);

struct ProbeResult {
    Verdict     verdict = Verdict::Present;
    bool        skipped = false;   // cmd.txt or the command line moves the packages: not checked
    std::string folder;            // UTF-8, the WinData folder looked in
};

// The disk half: the executable's directory, its WinData folder, the *.pkg files in it.
ProbeResult Probe(const char* cmdLine);

// The message box text: what was looked for, where, and the platform's copy instructions.
std::string MessageText(const ProbeResult& r, const char* platformHelp);

}  // namespace GameData
