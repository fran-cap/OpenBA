// ============================================================================
// src/ext/custom_maps/map_list.cpp -- ext.custom_maps: the maps in a folder (map_list.h)
// ============================================================================
#include "ext/custom_maps/map_list.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <system_error>
#include <utility>

namespace Ext {
namespace CustomMaps {

namespace {

bool HasBmapExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".bmap";
}

// The file's last write as seconds since 1970 (file_time_type's epoch is not the system
// clock's before C++20).
int64_t ModifiedSeconds(const std::filesystem::path& path) {
    std::error_code ec;
    const auto fileTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    const auto sys = std::chrono::system_clock::now() +
                     (fileTime - std::filesystem::file_time_type::clock::now());
    const auto since = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch());
    return static_cast<int64_t>(since.count());
}

}  // namespace

bool ReadMapFile(const std::filesystem::path& path, Map& out, std::string& why) {
    // The size is tested before the file is read: the list calls this for every .bmap of the folder at each
    // press of the button, and a large file must not be read only to be refused.
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        why = "cannot open the file";
        return false;
    }
    if (size > kMaxMapFile) {
        why = "the file is too large";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        why = "cannot open the file";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) {
        why = "cannot read the file";
        return false;
    }
    const Status status = Parse(bytes.data(), bytes.size(), out);
    if (status != Status::Ok) {
        why = std::string("not a valid map file: ") + StatusText(status);
        return false;
    }
    const std::vector<Issue> issues = Validate(out);
    for (const Issue& issue : issues) {
        if (issue.error) {
            why = issue.text;
            return false;
        }
    }
    return true;
}

void SortNewestFirst(std::vector<ListedMap>& maps) {
    std::stable_sort(maps.begin(), maps.end(), [](const ListedMap& a, const ListedMap& b) {
        if (a.modified != b.modified) return a.modified > b.modified;
        if (a.name != b.name) return a.name < b.name;
        return a.path < b.path;
    });
}

std::vector<ListedMap> ScanMaps(const std::filesystem::path& dir) {
    std::vector<ListedMap> maps;
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return maps;
    std::filesystem::directory_iterator it(dir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc || !HasBmapExtension(it->path())) continue;
        ListedMap row;
        row.path = it->path();
        row.modified = ModifiedSeconds(row.path);
        row.name = row.path.stem().string();
        Map map;
        row.valid = ReadMapFile(row.path, map, row.problem);
        if (row.valid) {
            if (!map.name.empty()) row.name = map.name;
            row.mapSize = map.mapSize;
            row.author = map.author;
            for (const ModRef& mod : map.mods) row.mods.push_back(mod.name);
            row.thumbnail = std::move(map.thumbnail);
        }
        maps.push_back(std::move(row));
    }
    SortNewestFirst(maps);
    if (maps.size() > kMaxListed) maps.resize(kMaxListed);
    return maps;
}

std::string SizeText(uint32_t mapSize) {
    return std::to_string(mapSize) + " x " + std::to_string(mapSize);
}

std::string CutText(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t cut = maxBytes;
    // not inside a multi-byte character
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xc0u) == 0x80u) --cut;
    return text.substr(0, cut) + "...";
}

std::string ModsText(const std::vector<std::string>& mods, size_t maxBytes) {
    if (mods.empty()) return "No mods listed";
    const std::string prefix = "Needs: ";
    std::string line = prefix;
    size_t shown = 0;
    for (const std::string& name : mods) {
        const std::string next = line + (shown > 0 ? ", " : "") + name;
        // Leave room for the "+N" that tells how many names did not fit.
        const bool last = shown + 1 == mods.size();
        if (shown > 0 && next.size() + (last ? 0 : 3) > maxBytes) break;
        line = next;
        ++shown;
    }
    if (shown < mods.size()) line += " +" + std::to_string(mods.size() - shown);
    return shown == 1 && line.size() > maxBytes ? CutText(line, maxBytes) : line;
}

std::string DateText(int64_t seconds) {
    if (seconds <= 0) return std::string();
    const std::time_t t = static_cast<std::time_t>(seconds);
    // std::localtime shares one buffer; the picker is built and read on the UI thread only.
    const std::tm* local = std::localtime(&t);
    if (local == nullptr) {
        return std::string();
    }
    char text[40];
    if (std::strftime(text, sizeof(text), "%m/%d/%Y %I:%M %p", local) == 0) {
        return std::string();
    }
    return text;
}

}  // namespace CustomMaps
}  // namespace Ext
