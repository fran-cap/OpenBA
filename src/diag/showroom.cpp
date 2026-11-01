// src/diag/showroom.cpp
// BANISHED_SHOWROOM — the building and tree showroom (see showroom.h). Harness only, not retail.
#include "showroom.h"
#include "showroom_list.h"
#include "options.h"
#include "pausemenu_harness.h"   // PauseMenuHarness_QuitGame: the run ends after the last shot
#include "io/FileSystem.h"
#include "system/Filename.h"
#include "system/Value.h"            // System::Time, RtSystem_Time_Ctor
#include "Game/GameUpdateLoop.h"     // m_dayCounter / m_weather: the month a shot is taken at
#include "Game/Calendar.h"
#include "Game/WeatherSimulation.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Frames (GameUpdateLoop::Update calls) the world gets before the first shot: the fade-in
// and any texture created on first bind have landed by then.
constexpr int kSettleFrames = 300;
// Frames between pinning the camera on a building and requesting its shot.
constexpr int kFrameFrames = 60;
// The same after the year time moved to a new month: the season stamp, the weather's season
// step and the lighting fade get more frames to follow.
constexpr int kMonthFrames = 180;
// Year time per month (DebugUI::OnDaySliderChanged: 3600 s a year, 12 months).
constexpr float kSecondsPerMonth = 300.0f;
// Frames to wait for the retail grab to write its .bmp before giving up on a shot.
constexpr int kShotTimeoutFrames = 300;
// Frames between the .bmp appearing and moving it (lets the writer finish).
constexpr int kMoveDelayFrames = 5;
// Two headings per building, degrees, so the walls under the eaves on both sides are seen.
// A tree is shot from the first heading only (a crown looks the same from the far side).
constexpr float kYaws[2] = {45.0f, 225.0f};
constexpr int kViews = 2;
constexpr int kTreeViews = 1;
// Camera distance per unit of the model's largest extent, and its clamp. The pin writes the
// distance directly, so it may go closer than retail's 20-unit zoom floor.
constexpr float kFit = 1.1f;
constexpr float kDistMin = 10.0f;
constexpr float kDistMax = 130.0f;
// The wide shot of the whole tree grid, once per month: heading, and distance per unit of the
// grid's diagonal plus a margin for the crowns, clamped.
constexpr float kWideYaw = 45.0f;
constexpr float kWideFit = 0.9f;
constexpr float kWideMargin = 18.0f;
constexpr float kWideDistMax = 220.0f;
// Bare ground kept around the bounding box of the placed buildings, in tiles.
constexpr int kAreaMargin = 8;
constexpr const char* kOutDir = "showroom";

struct Entry {
    bool tree = false;
    std::string path;   // "Template\X.rsc", or a tree's species
    std::string name;   // "X", or "tree_<species>"
    bool placed = false;
    float worldX = 0.0f;             // footprint centre, the fallback focus
    float worldY = 0.0f;
    float footprint = 0.0f;          // larger footprint side, in tiles
    const float* bounds = nullptr;   // the model's world box: min xyz, max xyz
};

// One framed shot: where the camera looks and from how far.
struct Focus {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float dist = 40.0f;
};

// One screenshot: which entry (kWide = the whole tree grid), from which heading, at which month.
constexpr int kWide = -1;
struct Shot {
    int entry = kWide;
    int view = 0;
    bool hasMonth = false;
    float month = 0.0f;
};

enum class Phase { Settle, Frame, AwaitFile, Done };

struct State {
    bool loaded = false;
    std::vector<Entry> entries;
    std::vector<float> months;   // the list's month lines, in order
    std::vector<Shot> shots;     // built when the settle ends
    int shot = -1;               // index into shots
    bool monthSet = false;       // a month has been written into the calendar
    float lastMonth = 0.0f;      // ...and which
    int frameWait = kFrameFrames;
    Phase phase = Phase::Settle;
    int frame = 0;          // Tick calls so far
    int phaseStart = 0;     // frame the current phase began
    int current = -1;       // entry being framed / shot (kWide: the tree grid)
    float yaw = 0.0f;       // the current shot's heading
    Focus focus;            // the current shot's framing
    int taken = 0;
    int fileSeenAt = -1;
    std::string pendingShot;   // the ScreenshotN.bmp the retail grab is about to write
    bool hasArea = false;      // the bare rectangle, in tiles (inclusive)
    int areaX0 = 0;
    int areaY0 = 0;
    int areaX1 = 0;
    int areaY1 = 0;
};

State& S() {
    static State s;
    return s;
}

void Load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    const char* file = Opt().showroom;
    if (!file || !file[0]) return;
    FILE* f = std::fopen(file, "rb");
    if (!f) {
        std::fprintf(stderr, "[Showroom] cannot open list %s\n", file);
        return;
    }
    std::string contents;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        contents.append(buffer, got);
    }
    std::fclose(f);
    const ShowroomList::List list = ShowroomList::Parse(contents);
    for (const std::string& error : list.errors) {
        std::fprintf(stderr, "[Showroom] list %s %s (ignored)\n", file, error.c_str());
    }
    int trees = 0;
    for (const ShowroomList::Item& item : list.items) {
        Entry e;
        e.tree = item.tree;
        e.path = item.path;
        e.name = item.name;
        if (e.tree) ++trees;
        s.entries.push_back(e);
    }
    s.months = list.months;
    std::fprintf(stderr, "[Showroom] list %s: %zu entries (%d trees), %zu months\n", file, s.entries.size(),
                 trees, s.months.size());
}

// The file the retail grab will write next: <save dir>Screenshots\Screenshot<N>.bmp for
// the first N that does not exist (GameUpdateLoop::TakeScreenshot's own probe).
std::string NextScreenshotPath() {
    const IO::FileSystem* fs = IO::FileSystem::_singleton;
    if (!fs) return std::string();
    System::Filename saveDir(fs->GetSaveDirectory());
    const char* dir = saveDir.AsString()._data;
    if (!dir) return std::string();
    for (int n = 0; n < 100000; ++n) {
        char path[1024];
        std::snprintf(path, sizeof(path), "%sScreenshots\\Screenshot%d.bmp", dir, n);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return path;
    }
    return std::string();
}

// showroom/[m<month>_]NN_<name>_yNNN.bmp, or showroom/[m<month>_]wide_yNNN.bmp.
void MoveShot(const std::string& from) {
    const State& s = S();
    const Shot& shot = s.shots[s.shot];
    std::error_code ec;
    std::filesystem::create_directories(kOutDir, ec);
    char prefix[32] = "";
    if (shot.hasMonth) std::snprintf(prefix, sizeof(prefix), "m%g_", shot.month);
    char to[512];
    const char* name = "wide";
    if (shot.entry == kWide) {
        std::snprintf(to, sizeof(to), "%s\\%swide_y%03d.bmp", kOutDir, prefix, (int)s.yaw);
    } else {
        name = s.entries[shot.entry].name.c_str();
        std::snprintf(to, sizeof(to), "%s\\%s%02d_%s_y%03d.bmp", kOutDir, prefix, shot.entry, name,
                      (int)s.yaw);
    }
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) std::filesystem::remove(from, ec);
    std::fprintf(stderr, "[Showroom] shot %d %s -> %s%s\n", shot.entry, name, to, ec ? " (MOVE FAILED)" : "");
}

// Frame the entry on its model's world box (Scene::Clean has filled it in by the end of the
// settle); a box that is still empty or absurd falls back to the footprint.
Focus FrameFor(const Entry& e) {
    Focus f;
    f.x = e.worldX;
    f.y = e.worldY;
    float size = e.footprint;
    if (e.bounds) {
        const float* b = e.bounds;
        const float dx = b[3] - b[0];
        const float dy = b[4] - b[1];
        const float dz = b[5] - b[2];
        if (dx > 0.0f && dy > 0.0f && dz >= 0.0f && dx < 1000.0f && dy < 1000.0f && dz < 1000.0f) {
            f.x = (b[0] + b[3]) * 0.5f;
            f.y = (b[1] + b[4]) * 0.5f;
            f.z = (b[2] + b[5]) * 0.5f;
            const float across = std::sqrt(dx * dx + dy * dy);
            size = across > dz ? across : dz;
        }
    }
    f.dist = kFit * size;
    if (f.dist < kDistMin) f.dist = kDistMin;
    if (f.dist > kDistMax) f.dist = kDistMax;
    return f;
}

// The whole tree grid: the centre of the placed trees' boxes, far enough back to hold the
// grid's diagonal.
Focus FrameWide() {
    const State& s = S();
    Focus f;
    bool any = false;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float zSum = 0.0f;
    int n = 0;
    for (const Entry& e : s.entries) {
        if (!e.tree || !e.placed) continue;
        const Focus one = FrameFor(e);
        if (!any) {
            any = true;
            x0 = one.x;
            x1 = one.x;
            y0 = one.y;
            y1 = one.y;
        } else {
            x0 = one.x < x0 ? one.x : x0;
            y0 = one.y < y0 ? one.y : y0;
            x1 = one.x > x1 ? one.x : x1;
            y1 = one.y > y1 ? one.y : y1;
        }
        zSum += one.z;
        ++n;
    }
    if (!any) return f;
    f.x = (x0 + x1) * 0.5f;
    f.y = (y0 + y1) * 0.5f;
    f.z = zSum / (float)n;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    f.dist = kWideFit * std::sqrt(dx * dx + dy * dy) + kWideMargin;
    if (f.dist < kDistMin) f.dist = kDistMin;
    if (f.dist > kWideDistMax) f.dist = kWideDistMax;
    return f;
}

// Every shot, in order: per month (or once, when the list has none) each placed entry from
// its headings, then the wide shot of the tree grid when a tree stands.
void BuildShots() {
    State& s = S();
    bool anyTree = false;
    for (const Entry& e : s.entries) {
        if (e.tree && e.placed) anyTree = true;
    }
    const size_t passes = s.months.empty() ? 1 : s.months.size();
    for (size_t m = 0; m < passes; ++m) {
        Shot base;
        base.hasMonth = !s.months.empty();
        base.month = base.hasMonth ? s.months[m] : 0.0f;
        for (int i = 0; i < (int)s.entries.size(); ++i) {
            const Entry& e = s.entries[i];
            if (!e.placed) continue;
            const int views = e.tree ? kTreeViews : kViews;
            for (int v = 0; v < views; ++v) {
                Shot shot = base;
                shot.entry = i;
                shot.view = v;
                s.shots.push_back(shot);
            }
        }
        if (anyTree) {
            Shot wide = base;
            wide.entry = kWide;
            s.shots.push_back(wide);
        }
    }
    std::fprintf(stderr, "[Showroom] %zu shots over %zu month pass(es)\n", s.shots.size(), passes);
}

// Moves the year clock to `month` the way DebugUI::OnDaySliderChanged does: the calendar's
// year time, then one zero-length weather update so the weather takes the new season. While
// paused nothing else follows: snow build-up and fruit ripening are sim state.
void SetMonth(float month) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (!game || !game->m_dayCounter || !game->m_weather) {
        std::fprintf(stderr, "[Showroom] month %g: no calendar / weather, not set\n", month);
        return;
    }
    System::Time time;
    RtSystem_Time_Ctor(&time, month * kSecondsPerMonth);
    game->m_dayCounter->yearTime._ticks = time._ticks;
    System::Time zero;
    RtSystem_Time_Ctor(&zero, 0.0f);
    game->m_weather->Update(&zero);
    std::fprintf(stderr, "[Showroom] month %g (year time %.0f s)\n", month, month * kSecondsPerMonth);
}

void Enter(Phase phase) {
    State& s = S();
    s.phase = phase;
    s.phaseStart = s.frame;
}

void Finish() {
    State& s = S();
    int placed = 0;
    for (const Entry& e : s.entries) {
        if (e.placed) ++placed;
    }
    std::fprintf(stderr, "[Showroom] DONE placed=%d of %zu, shots=%d of %zu\n", placed, s.entries.size(),
                 s.taken, s.shots.size());
    std::fflush(stderr);
    Enter(Phase::Done);
    // The camera is released with the last shot; without this the exe sits on a live world showing
    // whatever the free camera lands on until the script kills it. Quit Game through the pause menu
    // instead, so the settings round-trip and the normal shutdown run.
    PauseMenuHarness_QuitGame();
}

// Frame the next shot (setting its month first when it changes), or finish when none is left.
void NextShot() {
    State& s = S();
    ++s.shot;
    if (s.shot >= (int)s.shots.size()) {
        Finish();
        return;
    }
    const Shot& shot = s.shots[s.shot];
    s.frameWait = kFrameFrames;
    if (shot.hasMonth && (!s.monthSet || shot.month != s.lastMonth)) {
        SetMonth(shot.month);
        s.monthSet = true;
        s.lastMonth = shot.month;
        s.frameWait = kMonthFrames;
    }
    s.current = shot.entry;
    if (shot.entry == kWide) {
        s.yaw = kWideYaw;
        s.focus = FrameWide();
        std::fprintf(stderr, "[Showroom] framing wide yaw=%.0f focus=(%.1f,%.1f,%.1f) dist=%.1f\n", s.yaw,
                     s.focus.x, s.focus.y, s.focus.z, s.focus.dist);
        Enter(Phase::Frame);
        return;
    }
    const Entry& e = s.entries[shot.entry];
    s.yaw = kYaws[shot.view];
    s.focus = FrameFor(e);
    std::fprintf(stderr, "[Showroom] framing %d %s yaw=%.0f focus=(%.1f,%.1f,%.1f) dist=%.1f", shot.entry,
                 e.name.c_str(), s.yaw, s.focus.x, s.focus.y, s.focus.z, s.focus.dist);
    if (e.bounds) {
        const float* b = e.bounds;
        std::fprintf(stderr, " bounds=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)", b[0], b[1], b[2], b[3], b[4], b[5]);
    }
    std::fprintf(stderr, "\n");
    Enter(Phase::Frame);
}

}  // namespace

extern "C" bool Showroom_Enabled() {
    Load();
    return !S().entries.empty();
}

extern "C" int Showroom_Count() {
    Load();
    return (int)S().entries.size();
}

extern "C" const char* Showroom_Path(int index) {
    return S().entries[index].path.c_str();
}

extern "C" const char* Showroom_Name(int index) {
    return S().entries[index].name.c_str();
}

extern "C" bool Showroom_IsTree(int index) {
    return S().entries[index].tree;
}

extern "C" void Showroom_NotePlaced(int index, bool placed, int tileX, int tileY, int w, int h, int mapDim,
                                    const float* worldBounds, const char* why) {
    State& s = S();
    Entry& e = s.entries[index];
    e.placed = placed;
    if (!placed) {
        std::fprintf(stderr, "[Showroom] NOT placed %d %s: %s\n", index, e.path.c_str(), why ? why : "?");
        return;
    }
    // Footprint centre in world units: world = cell - mapDim/2 (skill camera-pin).
    const float half = (float)(mapDim >> 1);
    e.worldX = (float)tileX + (float)w * 0.5f - half;
    e.worldY = (float)tileY + (float)h * 0.5f - half;
    e.footprint = (float)(w > h ? w : h);
    e.bounds = worldBounds;
    const int x0 = tileX - kAreaMargin;
    const int y0 = tileY - kAreaMargin;
    const int x1 = tileX + w - 1 + kAreaMargin;
    const int y1 = tileY + h - 1 + kAreaMargin;
    if (!s.hasArea) {
        s.hasArea = true;
        s.areaX0 = x0;
        s.areaY0 = y0;
        s.areaX1 = x1;
        s.areaY1 = y1;
    } else {
        s.areaX0 = x0 < s.areaX0 ? x0 : s.areaX0;
        s.areaY0 = y0 < s.areaY0 ? y0 : s.areaY0;
        s.areaX1 = x1 > s.areaX1 ? x1 : s.areaX1;
        s.areaY1 = y1 > s.areaY1 ? y1 : s.areaY1;
    }
    std::fprintf(stderr, "[Showroom] placed %d %s tile=(%d,%d) fp=%dx%d world=(%.1f,%.1f)\n", index,
                 e.path.c_str(), tileX, tileY, w, h, e.worldX, e.worldY);
}

extern "C" bool Showroom_InArea(int x, int y) {
    const State& s = S();
    return s.hasArea && x >= s.areaX0 && x <= s.areaX1 && y >= s.areaY0 && y <= s.areaY1;
}

extern "C" bool Showroom_WantsPause() {
    return Showroom_Enabled() && S().phase != Phase::Done;
}

extern "C" bool Showroom_Tick() {
    if (!Showroom_Enabled()) return false;
    State& s = S();
    ++s.frame;
    const int inPhase = s.frame - s.phaseStart;
    switch (s.phase) {
    case Phase::Settle:
        if (inPhase < kSettleFrames) return false;
        std::fprintf(stderr, "[Showroom] bare area tiles (%d,%d)-(%d,%d)\n", s.areaX0, s.areaY0, s.areaX1,
                     s.areaY1);
        BuildShots();
        NextShot();
        return false;
    case Phase::Frame:
        if (inPhase < s.frameWait) return false;
        s.pendingShot = NextScreenshotPath();
        s.fileSeenAt = -1;
        Enter(Phase::AwaitFile);
        return true;
    case Phase::AwaitFile: {
        std::error_code ec;
        if (s.fileSeenAt < 0 && std::filesystem::exists(s.pendingShot, ec)) s.fileSeenAt = s.frame;
        if (s.fileSeenAt >= 0 && s.frame - s.fileSeenAt >= kMoveDelayFrames) {
            MoveShot(s.pendingShot);
            ++s.taken;
        } else if (inPhase < kShotTimeoutFrames) {
            return false;
        } else {
            std::fprintf(stderr, "[Showroom] shot %d: no %s after %d frames\n", s.current, s.pendingShot.c_str(),
                         kShotTimeoutFrames);
        }
        NextShot();
        return false;
    }
    case Phase::Done:
        return false;
    }
    return false;
}

extern "C" bool Showroom_CameraFocus(float* x, float* y, float* z, float* dist, float* yaw) {
    if (!Showroom_Enabled()) return false;
    const State& s = S();
    if (s.shot < 0 || s.phase == Phase::Settle || s.phase == Phase::Done) return false;
    *x = s.focus.x;
    *y = s.focus.y;
    *z = s.focus.z;
    *dist = s.focus.dist;
    *yaw = s.yaw;
    return true;
}
