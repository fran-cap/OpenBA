#pragma once
// ============================================================================
// src/platform/android/phone_seed.h -- the phone defaults' lines an existing settings.cfg lacks
// ============================================================================
// NOT RETAIL. Pure (tests/unit/phone_seed_test.cpp). app_android.cpp seeds a first run's
// settings.cfg with the phone defaults; a file written before a default existed (a P7 phone that
// never saw ext.fps_cap) gets the lines of the defaults whose KEY it does not have, appended. A key
// the file has keeps its value, whatever it is: the player's choice, or the game's own write-back.
// Issue #96: a tablet's seed is the phone's with a few values changed (WithSeedOverrides).
// ============================================================================

#include "io/settings_file.h"   // IO::SettingsFile::Parse: the keys the game reads

#include <cstddef>
#include <string>

namespace Platform {
namespace Android {

// The "key=value\n" lines of `seed` whose key is not a key of `existing` (a seed line's key is its
// text before its '='). In `seed`'s order, each ending in '\n'. The keys of `existing` are the ones
// the settings parser reads (a BOM, blanks around the key, comments, escapes, CRLF): a key spelled
// another way on disk is still the player's, and an appended line would override it, since the
// parser keeps the last value of a duplicate key (review F-315).
inline std::string MissingSeedLines(const std::string& existing, const char* seed) {
    auto keyOf = [](const std::string& line) {
        const size_t eq = line.find('=');
        return eq == std::string::npos ? std::string() : line.substr(0, eq);
    };
    const IO::SettingsFile::Entries entries = IO::SettingsFile::Parse(existing);
    auto hasKey = [&](const std::string& key) {
        for (const IO::SettingsFile::Entry& e : entries) {
            if (e.key == key) return true;
        }
        return false;
    };
    std::string out;
    const std::string all(seed);
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        const std::string line = all.substr(start, end - start);
        const std::string key = keyOf(line);
        if (!key.empty() && !hasKey(key)) {
            out += line;
            out += '\n';
        }
        start = end + 1;
    }
    return out;
}

// `seed` with each line whose key is the key of a line of `overrides` replaced by that line (in
// `seed`'s place), and the `overrides` lines whose key `seed` lacks appended in their order. Each
// line ends in a newline. The tablet seed is the phone seed through this (app_android.cpp).
inline std::string WithSeedOverrides(const char* seed, const char* overrides) {
    auto keyOf = [](const std::string& line) {
        const size_t eq = line.find('=');
        return eq == std::string::npos ? std::string() : line.substr(0, eq);
    };
    const std::string over(overrides);
    auto overrideFor = [&](const std::string& key, std::string* out) {
        size_t start = 0;
        while (start < over.size()) {
            size_t end = over.find('\n', start);
            if (end == std::string::npos) end = over.size();
            const std::string line = over.substr(start, end - start);
            if (!key.empty() && keyOf(line) == key) {
                *out = line;
                return true;
            }
            start = end + 1;
        }
        return false;
    };
    std::string result;
    const std::string all(seed);
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(start, end - start);
        std::string replaced;
        if (overrideFor(keyOf(line), &replaced)) line = replaced;
        if (!line.empty()) {
            result += line;
            result += '\n';
        }
        start = end + 1;
    }
    result += MissingSeedLines(all, overrides);
    return result;
}

}  // namespace Android
}  // namespace Platform
