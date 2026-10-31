// ============================================================================
// src/app/game_data_probe.cpp -- the disk half of the missing-game-data check
// ============================================================================
// NOT RETAIL (issue #72). game_data_check.h has the rationale. The folder is computed as
// IO::FileSystem::SetupDirectories computes the pack directory on a plain boot
// (src/engine/io/filesystem_setupdirs.cpp): GetRootDirectory's executable directory, then
// "WinData". Everything goes through Platform:: (platform_file.h), so each twin answers with
// its own idea of the executable's path: the module on Windows, /proc/self/exe on Linux, the
// Run-folder symlink on macOS, the app's data folder on Android.
// ============================================================================

#include "game_data_check.h"

#include "platform_file.h"

#include <cstdint>
#include <string>
#include <vector>

namespace GameData {

namespace {

using Platform::PathChar;

// UTF-16 -> UTF-8 for the message and the log; a lone surrogate becomes U+FFFD.
std::string ToUtf8(const std::u16string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        uint32_t c = in[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

bool Exists(const std::u16string& path) {
    return Platform::PathAttributes(path.c_str()) != Platform::kInvalidPathAttributes;
}

}  // namespace

ProbeResult Probe(const char* cmdLine) {
    ProbeResult r;

    // GetRootDirectory: the executable's path up to and including its last separator.
    PathChar exe[4096];
    const uint32_t n = Platform::ExecutablePath(exe, sizeof(exe) / sizeof(exe[0]));
    std::u16string root(exe, n < sizeof(exe) / sizeof(exe[0]) ? n : 0);
    const size_t slash = root.find_last_of(u"\\/");
    char16_t sep = u'/';
    if (slash != std::u16string::npos) {
        sep = root[slash];
        root.resize(slash + 1);
    } else {
        root.clear();   // no path at all: the working directory, as a relative "WinData"
    }

    const std::u16string folder = root + u"WinData";
    r.folder = ToUtf8(folder);

    if (CommandLineMovesPackages(cmdLine) || Exists(root + u"cmd.txt")) {
        r.skipped = true;
        return r;
    }

    const uint32_t attrs = Platform::PathAttributes(folder.c_str());
    const bool isFolder =
        attrs != Platform::kInvalidPathAttributes && (attrs & Platform::kFileAttributeDirectory) != 0;

    std::vector<std::string> pkgs;
    if (isFolder) {
        const std::u16string pattern = folder + sep + u"*.pkg";
        Platform::FindData fd;
        Platform::FindHandle h = Platform::FindFirst(pattern.c_str(), &fd);
        if (h != Platform::kInvalidFindHandle) {
            do {
                if ((fd.attributes & Platform::kFileAttributeDirectory) == 0) {
                    pkgs.push_back(ToUtf8(std::u16string(fd.name)));
                }
            } while (Platform::FindNext(h, &fd) != 0);
            Platform::FindClose(h);
        }
    }
    r.verdict = Judge(isFolder, pkgs);
    return r;
}

}  // namespace GameData
