// ============================================================================
// src/engine/io/settings_file.cpp — the settings.cfg codec (Phase D.4 SETTINGS E.1)
// ============================================================================
// NOT RETAIL: the format is ours (settings_file.h has the rules). No engine
// type, no OS call: the unit tests link this file alone.
// ============================================================================

#include "io/settings_file.h"

#include <algorithm>
#include <unordered_map>

namespace IO {
namespace SettingsFile {

namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

// [begin, end) of `line` with unescaped blanks trimmed off both ends. A blank
// preceded by an odd run of backslashes is escaped content and stays.
void TrimRaw(const std::string& line, size_t& begin, size_t& end) {
    while (begin < end && IsBlank(line[begin]))
        ++begin;
    while (end > begin && IsBlank(line[end - 1])) {
        size_t slashes = 0;
        for (size_t i = end - 1; i > begin && line[i - 1] == '\\'; --i)
            ++slashes;
        if (slashes % 2 == 1)
            break;              // `\ ` : escaped, keep it
        --end;
    }
}

std::string Unescape(const std::string& line, size_t begin, size_t end) {
    std::string out;
    out.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) {
        const char c = line[i];
        if (c != '\\' || i + 1 == end) {
            out += c;           // a lone trailing backslash reads as itself
            continue;
        }
        const char e = line[++i];
        switch (e) {
            case '\\': out += '\\'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 's':  out += ' ';  break;
            case '=':  out += '=';  break;
            case '#':  out += '#';  break;
            default:   out += '\\'; out += e; break;   // unknown: as itself
        }
    }
    return out;
}

bool KeyLess(const Entry& a, const Entry& b) { return a.key < b.key; }

}  // namespace

std::string Escape(const std::string& field, bool isKey) {
    std::string out;
    out.reserve(field.size() + 4);
    const size_t n = field.size();
    for (size_t i = 0; i < n; ++i) {
        const char c = field[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case ' ':
                // Interior spaces are written raw; only a leading or trailing
                // one would be eaten by the reader's trim.
                if (i == 0 || i + 1 == n) out += "\\s";
                else                      out += ' ';
                break;
            case '=':
                if (isKey) out += "\\=";
                else       out += '=';      // the split is the FIRST `=`
                break;
            case '#':
                if (isKey && i == 0) out += "\\#";
                else                 out += '#';
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

Entries Parse(const char* text, size_t length) {
    Entries entries;
    std::unordered_map<std::string, size_t> index;   // key -> position in entries

    size_t pos = 0;
    if (length >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        pos = 3;

    while (pos < length) {
        size_t eol = pos;
        while (eol < length && text[eol] != '\n')
            ++eol;
        std::string line(text + pos, eol - pos);
        pos = eol + 1;

        // CRLF: a raw CR is never content (the writer escapes it), so one at
        // the end of a line is the line end.
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        size_t begin = 0;
        while (begin < line.size() && IsBlank(line[begin]))
            ++begin;
        if (begin == line.size() || line[begin] == '#')
            continue;                               // blank or comment

        size_t eq = std::string::npos;
        for (size_t i = begin; i < line.size(); ++i) {
            if (line[i] == '\\') { ++i; continue; }
            if (line[i] == '=') { eq = i; break; }
        }
        if (eq == std::string::npos)
            continue;                               // not an entry

        size_t kb = begin, ke = eq;
        TrimRaw(line, kb, ke);
        size_t vb = eq + 1, ve = line.size();
        TrimRaw(line, vb, ve);

        std::string key = Unescape(line, kb, ke);
        std::string value = Unescape(line, vb, ve);

        auto it = index.find(key);
        if (it != index.end()) {
            entries[it->second].value = std::move(value);   // last wins
        } else {
            index.emplace(key, entries.size());
            entries.push_back(Entry{std::move(key), std::move(value)});
        }
    }
    return entries;
}

std::string Serialize(const Entries& entries) {
    Entries sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(), KeyLess);

    std::string out;
    // A key that begins with the UTF-8 BOM bytes would land at offset 0 and be stripped
    // by Parse as a BOM, renaming the key on the next read. Such a file gets a real BOM
    // in front, which Parse skips, so the key comes back whole (review F-085).
    if (!sorted.empty() && sorted[0].key.compare(0, 3, "\xEF\xBB\xBF") == 0)
        out += "\xEF\xBB\xBF";
    for (const Entry& e : sorted) {
        out += Escape(e.key, true);
        out += '=';
        out += Escape(e.value, false);
        out += '\n';
    }
    return out;
}

Entries Merge(const Entries& disk, const Entries& map) {
    Entries merged;
    merged.reserve(disk.size() + map.size());
    std::unordered_map<std::string, size_t> index;

    for (const Entry& e : disk) {
        auto it = index.find(e.key);
        if (it != index.end()) {
            merged[it->second].value = e.value;
        } else {
            index.emplace(e.key, merged.size());
            merged.push_back(e);
        }
    }
    for (const Entry& e : map) {
        auto it = index.find(e.key);
        if (it != index.end()) {
            merged[it->second].value = e.value;     // the map wins
        } else {
            index.emplace(e.key, merged.size());
            merged.push_back(e);
        }
    }
    std::stable_sort(merged.begin(), merged.end(), KeyLess);
    return merged;
}

}  // namespace SettingsFile
}  // namespace IO
