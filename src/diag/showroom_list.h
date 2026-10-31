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
// A month is a float: 0 is the start of Early Spring, one month is 300 s of year time, the year
// is 12 months. Months are kept in list order (a repeat is kept too). A list with no month line
// shoots once, at the time the new game starts with.
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

struct List {
    std::vector<Item> items;
    std::vector<float> months;
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
