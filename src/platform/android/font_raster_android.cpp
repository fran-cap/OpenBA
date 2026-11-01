// ============================================================================
// src/platform/android/font_raster_android.cpp -- the Android twin of font_raster_posix.cpp
// ============================================================================
// NOT RETAIL. ext/resources/font_raster.h's contract over FreeType
// (src/platform/common/font_raster_freetype.cpp, KITFREE FREETYPE), as on Linux and macOS. It
// replaces src/platform/posix/font_raster_posix.cpp whole on Android (src/platform/android/
// README.md). One body differs, the face's lookup (FindFaceFile):
//   Linux / macOS look for `<face>.ttf` beside the exe, then in the user's and the system's font
//   directories. In an app process the "exe" (/proc/self/exe) is /system/bin/app_process64, there
//   is no per-user font directory, and the system's (/system/fonts) holds no FrancophilSans. The
//   one place is the app's data folder (android_paths.h): the game directory, the folder the
//   release packages put the TTF in beside OpenBA.exe / OpenBA. The apk carries the font as assets
//   (tools/release/fonts) and OpenBAActivity copies each file absent there at start (P9;
//   docs/android/README.md), so the face is in place before this lookup runs.
// Absent: "absent", and ext.sharp_ui keeps its retail(...) fallback, as everywhere.
// ============================================================================
#include "ext/resources/font_raster.h"
#include "font_raster_freetype.h"
#include "android_paths.h"   // Platform::Android::DataDir

#include <sys/stat.h>

#include <mutex>
#include <string>

namespace Ext {
namespace Resources {

namespace {

std::mutex g_lock;
bool g_lookedUp = false;      // the file lookup runs once per process
std::string g_facePath;       // empty when absent
std::string g_faceWhy;

bool IsFile(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
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
    const std::string& data = Platform::Android::DataDir();
    if (data.empty()) {
        if (why) *why = "no " + name + " (the app's data folder is unknown)";
        return false;
    }
    const std::string candidate = data + "/" + name;
    if (IsFile(candidate)) {
        path = candidate;
        return true;
    }
    if (why) *why = "no " + name + " (tried " + candidate + ")";
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
