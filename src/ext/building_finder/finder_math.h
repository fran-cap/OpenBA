// ============================================================================
// src/ext/building_finder/finder_math.h -- ext.building_finder's decisions, standard C++ only
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BUILDINGFINDER.md. The search (every word typed must be in the
// building's name or in the name of the toolbar group it sits in), the favorites list (a list of template
// paths, one per line in a text file: the player's own, not the game's) and the sentence that says a
// building's limit is reached. Nothing here touches the game; tests/unit/ext_building_finder_test.cpp links
// this header alone.
// ============================================================================
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace Ext {
namespace BuildingFinder {

constexpr size_t kMaxFavorites = 400;     // a longer file is cut; nobody stars more than this
constexpr size_t kMaxQueryWords = 8;

inline std::string LowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// The words of a query, lower case; a space, a tab or a comma ends a word.
inline std::vector<std::string> QueryWords(const std::string& query) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : query + " ") {
        if (c == ' ' || c == '\t' || c == ',') {
            if (!cur.empty() && words.size() < kMaxQueryWords) {
                words.push_back(LowerAscii(cur));
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    return words;
}

// Whether `text` has every word of `query` in it (any case, anywhere). An empty query matches everything.
inline bool Matches(const std::string& text, const std::string& query) {
    const std::string hay = LowerAscii(text);
    for (const std::string& w : QueryWords(query)) {
        if (hay.find(w) == std::string::npos) {
            return false;
        }
    }
    return true;
}

// The favorites file: one key per line. Blank lines and '#' lines are skipped, spaces around a key are
// trimmed, a key that repeats is kept once (its first place), and the list is cut at kMaxFavorites.
inline std::vector<std::string> ParseFavorites(const std::string& text) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size() && out.size() < kMaxFavorites) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t first = 0;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) {
            ++first;
        }
        line = line.substr(first);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (std::find(out.begin(), out.end(), line) == out.end()) {
            out.push_back(line);
        }
    }
    return out;
}

inline std::string FormatFavorites(const std::vector<std::string>& favorites) {
    std::string out = "# Building finder favorites: one building template path per line.\n";
    for (const std::string& key : favorites) {
        out += key;
        out += '\n';
    }
    return out;
}

inline bool IsFavorite(const std::vector<std::string>& favorites, const std::string& key) {
    return std::find(favorites.begin(), favorites.end(), key) != favorites.end();
}

// Adds the key when it is not a favorite and removes it when it is; false if the list is full and it was not
// added. Returns the state after: true = now a favorite.
inline bool ToggleFavorite(std::vector<std::string>& favorites, const std::string& key) {
    auto it = std::find(favorites.begin(), favorites.end(), key);
    if (it != favorites.end()) {
        favorites.erase(it);
        return false;
    }
    if (favorites.size() >= kMaxFavorites) {
        return false;
    }
    favorites.push_back(key);
    return true;
}

// The note after a building's name when the town has as many of it as the toolbar allows ("" when it can
// still be built; `limit` < 0 = no limit, as the toolbar's own test).
inline std::string LimitNote(int built, int limit) {
    if (limit < 0 || built < limit) {
        return std::string();
    }
    return "  (limit reached: " + std::to_string(built) + " of " + std::to_string(limit) + ")";
}

inline bool AtLimit(int built, int limit) {
    return limit >= 0 && built >= limit;
}

// `s` cut to at most `max` characters, with "..." in place of the cut part. A text element sizes the window
// to its text, so a line that can be long (a building's name) is cut where the window must not grow.
inline std::string Clip(const std::string& s, size_t max) {
    if (s.size() <= max) {
        return s;
    }
    if (max <= 3) {
        return s.substr(0, max);
    }
    return s.substr(0, max - 3) + "...";
}

// ---- the hotkey (a Win32 virtual key code) ----

// A key the hotkey may be: a letter, a digit, F1-F12. (Esc cancels; the rest are the game's own.)
inline bool HotkeyAllowed(int vk) {
    return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || (vk >= 0x70 && vk <= 0x7b);
}

// "H", "7", "F5".
inline std::string HotkeyName(int vk) {
    if (vk >= 0x70 && vk <= 0x7b) {
        return "F" + std::to_string(vk - 0x70 + 1);
    }
    if (HotkeyAllowed(vk)) {
        return std::string(1, static_cast<char>(vk));
    }
    return "?";
}

// The text after the last '/' or '\\' (a template path's file name), without its extension.
inline std::string LeafName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = leaf.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        leaf.resize(dot);
    }
    return leaf;
}

}  // namespace BuildingFinder
}  // namespace Ext
