// ============================================================================
// src/app/game_data_check.cpp -- the pure half of the missing-game-data check
// ============================================================================
// NOT RETAIL (issue #72). game_data_check.h has the rationale. No platform call in this file:
// tests/unit/game_data_check_test.cpp links it on every host.
// ============================================================================

#include "game_data_check.h"

#include <cctype>
#include <cstring>

namespace GameData {

namespace {

bool EqualsNoCase(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char x = static_cast<unsigned char>(a[i]);
        const unsigned char y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

}  // namespace

Verdict Judge(bool winDataIsFolder, const std::vector<std::string>& pkgNames) {
    if (!winDataIsFolder) return Verdict::FolderMissing;
    if (pkgNames.empty()) return Verdict::NoPackages;
    for (const std::string& name : pkgNames) {
        if (EqualsNoCase(name, "data0.pkg")) return Verdict::Present;
    }
    return Verdict::NoBasePackage;
}

const char* VerdictName(Verdict v) {
    switch (v) {
    case Verdict::Present:
        return "present";
    case Verdict::FolderMissing:
        return "folder-missing";
    case Verdict::NoPackages:
        return "no-packages";
    case Verdict::NoBasePackage:
        return "no-data0";
    }
    return "?";
}

bool CommandLineMovesPackages(const char* cmdLine) {
    if (cmdLine == nullptr) return false;
    const char* p = cmdLine;
    while ((p = std::strchr(p, '/')) != nullptr) {
        ++p;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        std::string name;
        while (*p != 0 && *p != ' ' && *p != ':') {
            name += *p;
            ++p;
        }
        if (EqualsNoCase(name, "pathpkg") || EqualsNoCase(name, "bin")) return true;
    }
    return false;
}

// Kept short on purpose: SDL's Android box is a plain text view with no scrolling, and on a
// landscape phone anything past about ten lines pushes the OK button off the screen.
std::string MessageText(const ProbeResult& r, const char* platformHelp) {
    std::string text;
    switch (r.verdict) {
    case Verdict::FolderMissing:
        text = "This folder does not exist:\n";
        break;
    case Verdict::NoPackages:
        text = "No game data (.pkg files) in:\n";
        break;
    case Verdict::NoBasePackage:
        text = "No data0.pkg in:\n";
        break;
    case Verdict::Present:
        break;
    }
    text += r.folder;
    text += "\n\n";
    if (platformHelp != nullptr) text += platformHelp;
    return text;
}

}  // namespace GameData
