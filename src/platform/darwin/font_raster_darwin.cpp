// ============================================================================
// src/platform/darwin/font_raster_darwin.cpp -- the Darwin twin of font_raster_posix.cpp
// ============================================================================
// NOT RETAIL. ext/resources/font_raster.h's contract over FreeType
// (src/platform/common/font_raster_freetype.cpp, KITFREE FREETYPE). The face
// is a file found by NAME -- `<face>.ttf` beside the exe, then the user's and
// the system's font directories -- never through fontconfig: the release
// package ships the TTF beside the exe and that is the one place the game
// needs. Absent: "absent", and ext.sharp_ui keeps its retail(...) fallback.
// No getenv: the home directory comes from the password database.
// ============================================================================
#include "ext/resources/font_raster.h"
#include "font_raster_freetype.h"

#include <dirent.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <mutex>
#include <string>

namespace Ext {
namespace Resources {

namespace {

std::mutex g_lock;
bool g_lookedUp = false;      // the file lookup runs once per process
std::string g_facePath;       // empty when absent
std::string g_faceWhy;

std::string ExeDir() {
    char buf[4096];
    uint32_t capacity = sizeof(buf);
    const ssize_t n = _NSGetExecutablePath(buf, &capacity) == 0 ? std::strlen(buf) : -1;
    if (n <= 0) return std::string();
    buf[n] = 0;
    const char* slash = std::strrchr(buf, '/');
    if (slash == nullptr) return std::string();
    return std::string(buf, static_cast<size_t>(slash - buf + 1));
}

std::string HomeDir() {
    // getpwuid_r, never $HOME (the tree's "never getenv" rule).
    struct passwd pw;
    struct passwd* result = nullptr;
    char buf[4096];
    if (::getpwuid_r(::getuid(), &pw, buf, sizeof buf, &result) != 0 || result == nullptr || pw.pw_dir == nullptr)
        return std::string();
    return std::string(pw.pw_dir) + "/";
}

bool IsFile(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// `<dir>/**/<name>` to `depth` levels (the system font tree is one or two deep).
bool FindUnder(const std::string& dir, const char* name, int depth, std::string& found) {
    if (IsFile(dir + name)) {
        found = dir + name;
        return true;
    }
    if (depth <= 0) return false;
    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return false;
    bool hit = false;
    while (!hit) {
        const struct dirent* ent = ::readdir(d);
        if (ent == nullptr) break;
        if (ent->d_name[0] == '.') continue;
        const std::string sub = dir + ent->d_name + "/";
        struct stat st;
        if (::stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        hit = FindUnder(sub, name, depth - 1, found);
    }
    ::closedir(d);
    return hit;
}

}  // namespace

namespace FreeTypeRaster {

bool FindFaceFile(const char* face, std::string& path, std::string* why) {
    path.clear();
    if (face == nullptr || *face == 0) {
        if (why) *why = "no face name";
        return false;
    }
    const std::string name = std::string(face) + ".ttf";
    std::string tried;
    const std::string exeDir = ExeDir();
    if (!exeDir.empty()) {
        if (IsFile(exeDir + name)) {
            path = exeDir + name;
            return true;
        }
        tried += exeDir + name;
    }
    const std::string home = HomeDir();
    const std::string roots[] = {
        home.empty() ? std::string() : home + ".fonts/",
        home.empty() ? std::string() : home + ".local/share/fonts/",
        "/usr/local/share/fonts/",
        "/usr/share/fonts/",
    };
    for (const std::string& root : roots) {
        if (root.empty()) continue;
        if (FindUnder(root, name.c_str(), 3, path)) return true;
        if (!tried.empty()) tried += ", ";
        tried += root + "**/" + name;
    }
    if (why) *why = "no " + name + " (tried " + tried + ")";
    return false;
}

}  // namespace FreeTypeRaster

namespace {

// The face's file, looked up once; `why` when absent.
bool FacePath(const char* face, std::string& path, std::string* why) {
    if (!g_lookedUp) {
        g_lookedUp = true;
        if (FreeTypeRaster::FindFaceFile(face, g_facePath, &g_faceWhy)) {
            std::string family;
            if (!FreeTypeRaster::FamilyName(g_facePath.c_str(), family, &g_faceWhy)) {
                g_facePath.clear();
            } else if (family != face) {
                g_faceWhy = g_facePath + " is '" + family + "', not " + face;
                g_facePath.clear();
            }
        }
    }
    path = g_facePath;
    if (path.empty() && why) *why = g_faceWhy;
    return !path.empty();
}

}  // namespace

const char* const* DefaultFaces(size_t& count) {
    static const char* const faces[] = {kGameFace, "Arial", "Helvetica"};
    count = sizeof faces / sizeof faces[0];
    return faces;
}

bool FaceAvailable(const char* face, std::string* why) {
    std::lock_guard<std::mutex> lock(g_lock);
    std::string path;
    return FacePath(face, path, why);
}

bool RasterizeFont(const char* face, int32_t pixelHeight, const int32_t* codepoints,
                   int32_t count, RasterFont& out, std::string* why) {
    out.glyphs.clear();
    out.height = 0;
    out.baseLine = 0;
    std::lock_guard<std::mutex> lock(g_lock);
    std::string path;
    if (!FacePath(face, path, why)) return false;
    return FreeTypeRaster::RasterizeFile(path.c_str(), face, pixelHeight, codepoints, count,
                                         FreeTypeRaster::DefaultOptions(), out, why);
}

}  // namespace Resources
}  // namespace Ext
