// src/diag/showroom_list.h
// The BANISHED_SHOWROOM list file parser (showroom.h). Pure: no engine headers, so the
// unit tests (tests/unit/showroom_list_test.cpp) compile it on every host.
//
// One item per line; '#' starts a comment; a tab ends the line (the ranking TSV's first column
// is the path). The line forms:
//   Template\X.rsc              a building template ('/' works too)
//   tree <species>              one tree of the species (oak, birch, pine, apple, cherry, peach,
//                               pear, plum, chestnut, pecan, walnut; matched case-insensitively)
//   month <m>                   shoot every item at month m too
//   months <m1> <m2> ...        the same for several months
//   forest <species|mixed> <columns> <rows> <spacing>
//                               a grid of fully grown trees, <spacing> tiles apart (1..6), up to
//                               kForestMaxSide trees a side; mixed cycles oak, birch and pine
//   bench <dist> [yaw] [sweep]  no screenshots: pin the camera on the forest's centre at <dist>
//                               (and heading <yaw>, degrees) for the rest of the run, unpaused;
//                               `sweep` moves it as BANISHED_CAMERA_PIN's ,sweep does
// A month is a float: 0 is the start of Early Spring, one month is 300 s of year time, the year
// is 12 months. Months are kept in list order (a repeat is kept too). A list with no month line
// shoots once, at the time the new game starts with. One forest line and one bench line at most
// (a second one is an error and is ignored).
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace ShowroomList {

struct Item {
    bool tree = false;
    std::string path;      // building: "Template\X.rsc"; tree: the species, lower case
    std::string name;      // building: "X"; tree: "tree_<species>" (logs and file names)
};

// The most trees a forest side may hold, and the spacing range in tiles.
constexpr int kForestMaxSide = 200;
constexpr int kForestMinSpacing = 1;
constexpr int kForestMaxSpacing = 6;

struct Forest {
    bool set = false;
    std::string species;   // lower case: "mixed", or one species ("oak")
    int columns = 0;
    int rows = 0;
    int spacing = 0;       // tiles between neighbouring trees
};

struct Bench {
    bool set = false;
    float dist = 0.0f;
    bool hasYaw = false;
    float yaw = 0.0f;      // degrees
    bool sweep = false;
};

struct List {
    std::vector<Item> items;
    std::vector<float> months;
    Forest forest;
    Bench bench;
    std::vector<std::string> errors;   // one per line that could not be read, with its number
};

inline std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// Splits on spaces and tabs.
inline std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        const size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

// A whole float word ("0.3", "6", "-1" is refused: no month is negative).
inline bool ParseMonth(const std::string& word, float* out) {
    if (word.empty()) return false;
    const char* begin = word.c_str();
    char* end = nullptr;
    const float value = std::strtof(begin, &end);
    if (end == begin || *end != '\0') return false;
    if (!(value >= 0.0f) || value > 1000.0f) return false;
    *out = value;
    return true;
}

// A whole integer word in [lo, hi].
inline bool ParseInt(const std::string& word, int lo, int hi, int* out) {
    if (word.empty()) return false;
    const char* begin = word.c_str();
    char* end = nullptr;
    const long value = std::strtol(begin, &end, 10);
    if (end == begin || *end != '\0') return false;
    if (value < lo || value > hi) return false;
    *out = static_cast<int>(value);
    return true;
}

// A whole float word in [lo, hi].
inline bool ParseFloat(const std::string& word, float lo, float hi, float* out) {
    if (word.empty()) return false;
    const char* begin = word.c_str();
    char* end = nullptr;
    const float value = std::strtof(begin, &end);
    if (end == begin || *end != '\0') return false;
    if (!(value >= lo) || !(value <= hi)) return false;
    *out = value;
    return true;
}

// "forest <species|mixed> <columns> <rows> <spacing>".
inline void ParseForest(const std::vector<std::string>& words, const std::string& where, List* list) {
    if (list->forest.set) {
        list->errors.push_back(where + "a second 'forest' line");
        return;
    }
    if (words.size() != 5) {
        list->errors.push_back(where + "'forest' takes <species|mixed> <columns> <rows> <spacing>");
        return;
    }
    Forest forest;
    forest.species = Lower(words[1]);
    if (!ParseInt(words[2], 1, kForestMaxSide, &forest.columns) ||
        !ParseInt(words[3], 1, kForestMaxSide, &forest.rows)) {
        list->errors.push_back(where + "forest columns and rows are 1.." + std::to_string(kForestMaxSide));
        return;
    }
    if (!ParseInt(words[4], kForestMinSpacing, kForestMaxSpacing, &forest.spacing)) {
        list->errors.push_back(where + "forest spacing is " + std::to_string(kForestMinSpacing) + ".." +
                               std::to_string(kForestMaxSpacing) + " tiles");
        return;
    }
    forest.set = true;
    list->forest = forest;
}

// "bench <dist> [yaw] [sweep]".
inline void ParseBench(const std::vector<std::string>& words, const std::string& where, List* list) {
    if (list->bench.set) {
        list->errors.push_back(where + "a second 'bench' line");
        return;
    }
    std::vector<std::string> args(words.begin() + 1, words.end());
    Bench bench;
    if (!args.empty() && Lower(args.back()) == "sweep") {
        bench.sweep = true;
        args.pop_back();
    }
    if (args.empty() || args.size() > 2) {
        list->errors.push_back(where + "'bench' takes <dist> [yaw] [sweep]");
        return;
    }
    if (!ParseFloat(args[0], 1.0f, 1000.0f, &bench.dist)) {
        list->errors.push_back(where + "bad bench distance '" + args[0] + "' (1..1000)");
        return;
    }
    if (args.size() == 2) {
        if (!ParseFloat(args[1], -3600.0f, 3600.0f, &bench.yaw)) {
            list->errors.push_back(where + "bad bench yaw '" + args[1] + "'");
            return;
        }
        bench.hasYaw = true;
    }
    bench.set = true;
    list->bench = bench;
}

// Reads one line (lineNo for the error text) into `list`.
inline void ParseLine(const std::string& raw, int lineNo, List* list) {
    std::string text = raw;
    const size_t hash = text.find('#');
    if (hash != std::string::npos) text.resize(hash);
    const size_t tab = text.find('\t');
    if (tab != std::string::npos) text.resize(tab);
    text = Trim(text);
    if (text.empty()) return;
    const std::vector<std::string> words = Words(text);
    const std::string keyword = Lower(words[0]);
    const std::string where = "line " + std::to_string(lineNo) + ": ";
    if (keyword == "tree") {
        if (words.size() != 2) {
            list->errors.push_back(where + "'tree' takes one species");
            return;
        }
        Item item;
        item.tree = true;
        item.path = Lower(words[1]);
        item.name = "tree_" + item.path;
        list->items.push_back(item);
        return;
    }
    if (keyword == "forest") {
        ParseForest(words, where, list);
        return;
    }
    if (keyword == "bench") {
        ParseBench(words, where, list);
        return;
    }
    if (keyword == "month" || keyword == "months") {
        if (words.size() < 2 || (keyword == "month" && words.size() != 2)) {
            list->errors.push_back(where + "'" + keyword + "' takes " +
                                   (keyword == "month" ? "one month" : "one or more months"));
            return;
        }
        for (size_t i = 1; i < words.size(); ++i) {
            float month = 0.0f;
            if (!ParseMonth(words[i], &month)) {
                list->errors.push_back(where + "bad month '" + words[i] + "'");
                continue;
            }
            list->months.push_back(month);
        }
        return;
    }
    for (char& c : text) {
        if (c == '/') c = '\\';
    }
    Item item;
    item.path = text;
    const size_t slash = text.rfind('\\');
    std::string stem = slash == std::string::npos ? text : text.substr(slash + 1);
    const size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem.resize(dot);
    item.name = stem;
    list->items.push_back(item);
}

inline List Parse(const std::string& contents) {
    List list;
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= contents.size()) {
        size_t end = contents.find('\n', pos);
        if (end == std::string::npos) end = contents.size();
        ++lineNo;
        ParseLine(contents.substr(pos, end - pos), lineNo, &list);
        pos = end + 1;
    }
    return list;
}

}  // namespace ShowroomList
