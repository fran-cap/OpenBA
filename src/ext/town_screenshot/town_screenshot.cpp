// ============================================================================
// src/ext/town_screenshot/town_screenshot.cpp -- ext.town_screenshot (class B, NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TOWNSCREENSHOT.md. The state machine of a screenshot, its camera, the read back of
// each tile, the stitching and the file. The arithmetic is screenshot_math.h (pure, unit tested).
//
// The frames of a screenshot, all through GameUpdateLoop::Draw step 7 (DrawScene): Waiting  Start asked
// ext.render_pipe to go (it draws the frame on a render thread one frame behind, through a path this camera
// does not reach); frames are drawn as usual until it has gone. Tiles    each frame draws the scene through
// the camera of the current tile. The first `kSettle` frames of a tile are only drawn (the sun's shadow map
// and the terrain's detail follow the camera), then the frame is read back and its middle is copied into the
// picture; the camera moves to the next tile. Done     the corners outside the map are made transparent, the
// picture is written, and the pause and the pipelined renderer are put back. The game is paused for the whole
// screenshot, so what moves (citizens, animals) stands still between tiles.
// ============================================================================
#include "ext/town_screenshot/town_screenshot.h"
#include "ext/town_screenshot/screenshot_math.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "Entity/ComponentEntity.h"
#include "Game/GameSettings.h"
#include "Game/GameUpdateLoop.h"
#include "Game/exe_bodies.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "Game/PlayerToolManager.h"
#include "Game/WorldGeometry.h"
#include "engine/MainLoop.h"
#include "ext/eye_camera/eye_camera.h"
#include "ext/registry.h"
#include "ext/render_pipe/render_pipe.h"
#include "graphics/Camera.h"
#include "graphics/FrameCopyImageBuffer.h"
#include "graphics/Scene.h"
#include "graphics/SunLight.h"
#include "io/FileSystem.h"
#include "platform_file.h"
#include "math/Math.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "video/Context.h"
#include "video/Device.h"
#include "volume/Volume.h"

namespace Ext {
namespace TownScreenshot {

namespace {

constexpr int kMargin = 64;                    // pixels cropped from each side of a tile
constexpr int kSettle = 4;                     // frames a tile is drawn before it is read back
constexpr int64_t kMaxPixels = 64000000;       // a picture is held whole in memory (4 bytes a pixel)
constexpr float kTreeAndBuildingHeight = 40.0f;   // what stands above the highest ground, world units
constexpr uint32_t kPauseLockBit = 1u << 10;   // GameUpdateLoop::m_gameFlags: PlayerToolManager::UpdateHotkeys skips its pause key
// The two constant sets the weather's snow cover is pushed through (the global and the pixel constants).
constexpr uint32_t kSnowConstantsDirty = RtVideo::kDirtyGlobalConstants | RtVideo::kDirtyPixelConstants;

enum class Phase { Idle, Waiting, Tiles };

struct Run {
    Phase  phase = Phase::Idle;
    const void* game = nullptr;          // the GameUpdateLoop the screenshot belongs to
    const void* map = nullptr;
    int    screenW = 0;
    int    screenH = 0;
    int    size = 0;                     // the map's cells to a side
    float  zLow = 0.0f;
    float  zHigh = 0.0f;
    float  ppc = 8.0f;
    Basis  basis = {};
    Footprint foot;
    TileGrid grid;
    int    tile = 0;
    int    wait = 0;
    float  distance = 900.0f;
    int    waitFrames = 0;               // frames spent waiting for the pipelined renderer to go
    bool   hadPipe = false;
    bool   wasPaused = false;
    bool   lockedPause = false;          // this run set the pause lock (GameUpdateLoop::m_gameFlags bit 10)
    bool   ignoreWeather = false;        // this screenshot leaves the weather out
    bool   hideSnow = false;             // and the snow on the ground
    std::chrono::steady_clock::time_point started;   // for the timings in the log
    std::vector<uint8_t> rgba;           // the picture, R G B A, top row first
};

Run g_run;
Choices g_choices;
char g_status[160] = "Ready.";
uint32_t g_statusEpoch = 0;   // the game (Ext::LockEpoch) the status line is about
void (*g_statusHook)() = nullptr;

// A refusal ("Wait for the town screenshot to finish.") shown for a few seconds over the progress line, which
// the next tile would overwrite within a frame or two.
char g_note[96] = "";
std::chrono::steady_clock::time_point g_noteUntil;

void SetNote(const char* text) {
    std::snprintf(g_note, sizeof g_note, "%s", text);
    g_noteUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    if (g_statusHook != nullptr) g_statusHook();
}

void SetStatus(const char* text) {
    std::snprintf(g_status, sizeof g_status, "%s", text);
    g_statusEpoch = Ext::LockEpoch();
    if (g_statusHook != nullptr) g_statusHook();
}

void ScreenSize(int& width, int& height) {
    width = 1920;
    height = 1080;
    const ::Video::Device* device = ::Video::Device::_singleton;
    if (device != nullptr && device->backBufferWidth > 0 && device->backBufferHeight > 0) {
        width = device->backBufferWidth;
        height = device->backBufferHeight;
    }
}

// The pipelined renderer on and off. Off: the entry's request is cleared and the frame tail removes it at the
// next frame. On: requested again and installed at once (Sync is what the Extensions window calls after a
// toggle).
void PipeOff() {
    const Ext::Entry* pipe = Ext::FindEntry("render_pipe");
    if (pipe != nullptr) Ext::SetRequested(*pipe, false);
}

void PipeOn() {
    const Ext::Entry* pipe = Ext::FindEntry("render_pipe");
    if (pipe == nullptr) return;
    Ext::SetRequested(*pipe, true);
    Ext::RenderPipe::Sync();
}

// Puts back what Start changed and forgets the run.
void Restore(bool gameStillThere) {
    if (gameStillThere) {
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        if (game != nullptr) {
            if (g_run.lockedPause) game->m_gameFlags &= ~kPauseLockBit;
            if (game->m_toolManager != nullptr) game->m_toolManager->paused = g_run.wasPaused;
        }
    }
    if (g_run.hadPipe) PipeOn();
    g_run = Run();
}

// Another game (or none) is running now: the screenshot's pointers are stale. The pause belonged to the old
// game.
bool Stale() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    return game == nullptr || game != g_run.game || game->m_game != g_run.map;
}

// <saveDir>\Screenshots\TownScreenshot<N>.png, the first N that does not exist; empty when there is no save
// directory.
std::string NextScreenshotPath(const char* stem = "TownScreenshot", const char* extension = "png") {
    const ::IO::FileSystem* fileSys = ::IO::FileSystem::_singleton;
    if (fileSys == nullptr) return std::string();
    System::Filename base = [fileSys, stem]() {
        System::Filename saveDir(fileSys->GetSaveDirectory());
        System::String joined(saveDir.AsString() + "\\Screenshots\\" + stem);
        return System::Filename(joined);
    }();
    System::Filename path;
    int index = 1;
    do {
        System::String suffix(System::String::Format("%i.%s", index, extension));
        System::String full(base.AsString() + suffix);
        path = full;
        ++index;
    } while (fileSys->Exists(path));
    return path._data != nullptr ? std::string(path._data) : std::string();
}

// The corners outside the map are made transparent (and black, so they compress to nothing).
void MaskCorners(Run& r) {
    for (int py = 0; py < r.foot.height; ++py) {
        uint8_t* row = r.rgba.data() + static_cast<size_t>(py) * static_cast<size_t>(r.foot.width) * 4u;
        for (int px = 0; px < r.foot.width; ++px) {
            if (PixelInsideMap(r.basis, r.foot, r.ppc, r.size, r.zLow, r.zHigh, px, py)) continue;
            std::memset(row + static_cast<size_t>(px) * 4u, 0, 4);
        }
    }
}

// A UTF-8 path as the platform's file layer takes it (UTF-16; it reads either separator and converts for the
// OS).
std::u16string WidenPath(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        uint32_t c = b;
        size_t extra = 0;
        if (b >= 0xF0) {
            c = b & 0x07;
            extra = 3;
        } else if (b >= 0xE0) {
            c = b & 0x0F;
            extra = 2;
        } else if (b >= 0xC0) {
            c = b & 0x1F;
            extra = 1;
        }
        for (size_t k = 1; k <= extra && i + k < s.size(); ++k) {
            c = (c << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
        }
        i += extra + 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
    }
    return out;
}

// The bytes into the file at `path`, through a temporary name that is moved into place when it is whole.
bool WriteFileBytes(const std::string& path, const void* data, size_t size) {
    const size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos && !Platform::EnsureDirChain(WidenPath(path.substr(0, slash)).c_str())) {
        return false;
    }
    const std::u16string target = WidenPath(path);
    const std::u16string temporary = target + u".tmp";
    Platform::FileHandle h = Platform::FileCreateWrite(temporary.c_str());
    if (h == Platform::kInvalidFileHandle || h == nullptr) return false;
    uint32_t wrote = 0;
    bool ok = true;
    // a file is written in pieces of at most 256 MB, which a 32-bit count can hold
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t left = size;
    while (ok && left > 0) {
        const uint32_t piece = static_cast<uint32_t>(std::min<size_t>(left, 256u << 20));
        ok = Platform::FileWrite(h, bytes, piece, &wrote) != 0 && wrote == piece;
        bytes += piece;
        left -= piece;
    }
    Platform::FileClose(h);
    ok = ok && Platform::FileReplace(temporary.c_str(), target.c_str()) != 0;
    if (!ok) Platform::FileDelete(temporary.c_str());
    return ok;
}

// The picture as a PNG: encoded in memory by SDL, written through the platform's file layer (the way the
// other extensions write files, so the path and the folder are the OS's own on every platform).
bool WritePng(const std::string& path, std::vector<uint8_t>& rgba, int width, int height) {
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, rgba.data(), width * 4);
    if (surface == nullptr) return false;
    SDL_IOStream* io = SDL_IOFromDynamicMem();
    bool ok = io != nullptr && SDL_SavePNG_IO(surface, io, false);
    const Sint64 size = ok ? SDL_TellIO(io) : 0;
    const void* bytes = nullptr;
    if (ok) {
        bytes = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    }
    ok = ok && bytes != nullptr && size > 0 && WriteFileBytes(path, bytes, static_cast<size_t>(size));
    if (io != nullptr) SDL_CloseIO(io);
    SDL_DestroySurface(surface);
    return ok;
}

// The scene's camera for the current tile: the player's camera moved and given an orthographic frustum.
void BuildTileCamera(const Run& r, Graphics::Camera& camera) {
    int col = r.tile % r.grid.cols;
    int row = r.tile / r.grid.cols;
    float cu = 0.0f;
    float cv = 0.0f;
    TileCentre(r.foot, r.grid, r.ppc, col, row, r.screenW, r.screenH, cu, cv);
    float lx = 0.0f;
    float ly = 0.0f;
    float lz = 0.0f;
    TileLookAt(r.basis, cu, cv, lx, ly, lz);
    const Math::Vector3 look(lx, ly, lz);
    const Math::Vector3 eye(lx - r.distance * r.basis.fx, ly - r.distance * r.basis.fy,
                            lz - r.distance * r.basis.fz);
    camera.SetCameraToWorld(Math::Transform::FromLookAt(eye, look).AsTransform3());
    const float halfW = static_cast<float>(r.screenW) / (2.0f * r.ppc);
    const float halfH = static_cast<float>(r.screenH) / (2.0f * r.ppc);
    // left, right, top, bottom: the order the sun's shadow camera and the projection read them in
    const ::Frustum frustum(true, Math::Scalar(1.0f), Math::Scalar(r.distance * 3.0f), Math::Scalar(-halfW),
                            Math::Scalar(halfW), Math::Scalar(halfH), Math::Scalar(-halfH));
    camera.SetFrustum(frustum);
}

// Reads the frame back and copies the tile's middle into the picture.
void ReadTile(Run& r) {
    ::Video::Device* device = ::Video::Device::_singleton;
    ::Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
    if (device == nullptr || mainLoop == nullptr) return;
    const int width = device->backBufferWidth;
    const int height = device->backBufferHeight;
    Graphics::FrameCopyImageBuffer* copy =
        static_cast<Graphics::FrameCopyImageBuffer*>(System::MemoryAllocator::MemAlloc(0x88, 8));
    ::new (copy) Graphics::FrameCopyImageBuffer();
    copy->Create(0, 4, 5, 0x100u, width, height, 1, 1, 1, 1, nullptr);
    static_cast<const Graphics::FrameCopyImageBuffer*>(copy)->Generate(mainLoop->_context, 1);
    unsigned int* pixels = static_cast<unsigned int*>(
        System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(height) * width) << 2, 4));
    copy->CopyFromTexture(pixels, width, height);
    delete copy;
    const int col = r.tile % r.grid.cols;
    const int row = r.tile / r.grid.cols;
    for (int py = 0; py < r.grid.payloadH; ++py) {
        const int iy = row * r.grid.payloadH + py;
        if (iy >= r.foot.height) break;
        for (int px = 0; px < r.grid.payloadW; ++px) {
            const int ix = col * r.grid.payloadW + px;
            if (ix >= r.foot.width) break;
            const unsigned int word = pixels[static_cast<size_t>(py + r.grid.margin) *
                                                 static_cast<size_t>(width) +
                                             static_cast<size_t>(px + r.grid.margin)];
            uint8_t* out = r.rgba.data() + (static_cast<size_t>(iy) * static_cast<size_t>(r.foot.width) +
                                            static_cast<size_t>(ix)) * 4u;
            out[0] = static_cast<uint8_t>((word >> 16) & 0xff);   // R
            out[1] = static_cast<uint8_t>((word >> 8) & 0xff);    // G
            out[2] = static_cast<uint8_t>(word & 0xff);           // B
            out[3] = 0xff;
        }
    }
    System::MemoryAllocator::MemFree(pixels);
}

// The last tile is in: mask, write, put back.
void Finish() {
    using Clock = std::chrono::steady_clock;
    const auto millis = [](Clock::time_point from, Clock::time_point to) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count());
    };
    const Clock::time_point tilesDone = Clock::now();
    const int width = g_run.foot.width;
    const int height = g_run.foot.height;
    MaskCorners(g_run);
    const Clock::time_point masked = Clock::now();
    const std::string path = NextScreenshotPath();
    char text[160];
    const bool written = !path.empty() && WritePng(path, g_run.rgba, width, height);
    const Clock::time_point encoded = Clock::now();
    const long long megabytes = static_cast<long long>(g_run.rgba.size() >> 20);
    std::fprintf(stderr, "[TownScreenshot] timings: %d tiles %lld ms, corners %lld ms, png %lld ms; "
                 "the picture was "
                 "%lld MB and is freed now\n", g_run.grid.count(), millis(g_run.started, tilesDone),
                 millis(tilesDone, masked), millis(masked, encoded), megabytes);
    if (written) {
        const size_t slash = path.find_last_of("\\/");
        std::snprintf(text, sizeof text, "Saved %s in Screenshots (%d by %d)",
                      slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1, width, height);
        std::fprintf(stderr, "[TownScreenshot] wrote %s (%d x %d, %d tiles)\n", path.c_str(), width, height,
                     g_run.grid.count());
    } else {
        std::snprintf(text, sizeof text, "The picture could not be saved.");
        std::fprintf(stderr, "[TownScreenshot] could not write %s\n", path.c_str());
    }
    Restore(true);
    SetStatus(text);
}

}  // namespace

std::string SaveInScreenshots(const char* stem, const char* extension, const void* data, size_t size) {
    const std::string path = NextScreenshotPath(stem, extension);
    if (path.empty() || !WriteFileBytes(path, data, size)) return std::string();
    return path;
}

std::string SavePngInScreenshots(const char* stem, std::vector<uint8_t>& rgba, int width, int height) {
    if (width <= 0 || height <= 0
        || rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4u) return std::string();
    const std::string path = NextScreenshotPath(stem, "png");
    if (path.empty() || !WritePng(path, rgba, width, height)) return std::string();
    return path;
}

Choices& Choose() {
    return g_choices;
}

const char* Status() {
    if (g_note[0] != '\0') {
        if (std::chrono::steady_clock::now() < g_noteUntil) return g_note;
        g_note[0] = '\0';
    }
    // "Saved TownScreenshot3.png ..." is about the game it was taken in: a new game or a load starts from
    // "Ready."
    if (g_statusEpoch != Ext::LockEpoch()) {
        std::snprintf(g_status, sizeof g_status, "Ready.");
        g_statusEpoch = Ext::LockEpoch();
    }
    return g_status;
}

void SetStatusHook(void (*hook)()) {
    g_statusHook = hook;
}

bool Active() {
    if (g_run.phase == Phase::Idle) return false;
    if (Stale()) {   // the game it belonged to is gone: nothing of it to put back but the renderer
        Restore(false);
        SetStatus("The screenshot was stopped: the game changed.");
        return false;
    }
    return true;
}

bool Start() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (g_run.phase != Phase::Idle) return false;
    if (game == nullptr || game->m_game == nullptr || game->m_worldGeometry == nullptr ||
        game->m_toolManager == nullptr) {
        SetStatus("Start or load a game first.");
        return false;
    }
    if (Ext::Ext().eye_camera && Ext::EyeCamera::Active()) {
        SetStatus("Leave the citizen's view first.");
        return false;
    }
    Run run;
    run.game = game;
    run.map = game->m_game;
    run.size = game->m_game->size;
    ScreenSize(run.screenW, run.screenH);
    // the ground's height range, from the height words (z * 256); trees and buildings stand above it
    const int16_t* heights = game->m_worldGeometry->_heights.data;
    const int stride = game->m_worldGeometry->_fieldD0;
    float zLow = 0.0f;
    float zHigh = 0.0f;
    if (heights != nullptr && stride > 0) {
        int16_t lo = heights[0];
        int16_t hi = heights[0];
        const int64_t n = static_cast<int64_t>(stride) * stride;
        for (int64_t i = 1; i < n; ++i) {
            lo = std::min(lo, heights[i]);
            hi = std::max(hi, heights[i]);
        }
        zLow = static_cast<float>(lo) / 256.0f;
        zHigh = static_cast<float>(hi) / 256.0f + kTreeAndBuildingHeight;
    }
    run.zLow = zLow;
    run.zHigh = zHigh;
    run.basis = MakeBasis(AnglePitch(g_choices.angle), TurnYaw(g_choices.turn));
    run.ppc = ChoosePixelsPerCell(run.basis, run.size, zLow, zHigh, QualityPixelsPerCell(g_choices.quality),
                                  kMaxPixels);
    run.foot = ComputeFootprint(run.basis, run.size, zLow, zHigh, run.ppc);
    run.grid = MakeGrid(run.foot.width, run.foot.height, run.screenW, run.screenH, kMargin);
    run.distance = static_cast<float>(run.size) + 300.0f;
    run.rgba.assign(static_cast<size_t>(run.foot.width) * static_cast<size_t>(run.foot.height) * 4u, 0);
    run.tile = 0;
    run.wait = kSettle;
    run.started = std::chrono::steady_clock::now();
    run.wasPaused = game->m_toolManager->paused;
    run.hadPipe = Ext::Ext().render_pipe;
    // The player's "status icons" and "show weather" options are not touched here: DrawOptions switches them off
    // for the length of one GameUpdateLoop::Draw, so nothing that saves or shows the options sees the change.
    run.ignoreWeather = g_choices.ignoreWeather;
    run.hideSnow = g_choices.hideSnow;
    g_run = std::move(run);
    // The pause is held: the pause key is locked for the run (retail's own lock, m_gameFlags bit 10), unless the
    // game has it locked already.
    g_run.lockedPause = (game->m_gameFlags & kPauseLockBit) == 0;
    game->m_gameFlags |= kPauseLockBit;
    game->m_toolManager->paused = true;
    if (g_run.hadPipe) PipeOff();
    g_run.phase = Phase::Waiting;
    std::fprintf(stderr, "[TownScreenshot] start: map %d, z %.1f..%.1f, %.1f pixels to the cell, %d x %d, "
                 "%d x %d tiles\n", g_run.size, zLow, zHigh, g_run.ppc, g_run.foot.width, g_run.foot.height,
                 g_run.grid.cols, g_run.grid.rows);
    SetStatus("Starting the screenshot...");
    return true;
}

bool TakeViewScreenshot() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_game == nullptr) {
        SetStatus("Start or load a game first.");
        return false;
    }
    if (g_run.phase != Phase::Idle) {
        SetNote("Wait for the town screenshot to finish.");
        return false;
    }
    // The request the game's own screenshot key raises: GameUpdateLoop::Draw takes the screenshot after the
    // scene and before the interface, and writes Screenshots\Screenshot<N>.bmp.
    game->m_quickSavePressed = 1;
    SetStatus("Screenshot requested.");
    return true;
}

void Cancel() {
    if (g_run.phase == Phase::Idle) return;
    const bool there = !Stale();
    Restore(there);
    SetStatus("Screenshot cancelled.");
}

DrawOptions::DrawOptions() {
    if (g_run.phase == Phase::Idle || g_gameSettings == nullptr) return;
    m_held = true;
    m_hadStatusIcons = g_gameSettings->statusIconEnable;
    m_hadShowWeather = g_gameSettings->showWeather;
    g_gameSettings->statusIconEnable = false;                       // the icons over buildings and citizens are left out
    if (g_run.ignoreWeather) g_gameSettings->showWeather = false;   // and the rain and snowfall, when asked
}

DrawOptions::~DrawOptions() {
    if (!m_held || g_gameSettings == nullptr) return;
    g_gameSettings->statusIconEnable = m_hadStatusIcons;
    g_gameSettings->showWeather = m_hadShowWeather;
}

void DrawScene(Graphics::Renderer& renderer, Video::Context& context, float alpha, const Graphics::Scene& scene,
               PlayerController& pc) {
    if (g_run.phase == Phase::Idle) {   // not taking one (Active() said so a moment ago): the game's own frame
        renderer.Draw(context, alpha, scene, pc.camera);
        return;
    }
    int width = 0;
    int height = 0;
    ScreenSize(width, height);
    if (width != g_run.screenW || height != g_run.screenH) {
        Restore(true);
        SetStatus("The screenshot was stopped: the window changed size.");
        renderer.Draw(context, alpha, scene, pc.camera);
        return;
    }
    if (g_run.phase == Phase::Waiting) {
        // The pipelined renderer goes at the next frame tail; until it has, the frame is drawn as usual.
        if (Ext::RenderPipe::Installed()) {
            if (++g_run.waitFrames >= 600) {   // it did not go: the tile camera is not reached on its path
                Restore(true);
                SetStatus("The screenshot was stopped: the pipelined renderer did not stop.");
            }
            renderer.Draw(context, alpha, scene, pc.camera);
            return;
        }
        g_run.phase = Phase::Tiles;
        g_run.wait = kSettle;
    }
    if (g_run.hideSnow) {
        // The snow lying on the ground is a coverage the weather pushes to the terrain's shaders every frame
        // (WeatherSimulation::DrawUpdate, step 5); the screenshot draws the ground bare.
        context._unknown03b8.x = 0.0f;
        context._pixelConstantStaging.pixel[4].x = 0.0f;
        context._dirty |= kSnowConstantsDirty;
    }
    // The pause key is locked for the run; this sets the pause again in case something else cleared it.
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    if (loop != nullptr && loop->m_toolManager != nullptr) loop->m_toolManager->paused = true;
    Graphics::Camera camera(pc.camera);
    BuildTileCamera(g_run, camera);
    Graphics::SunLight::_extShadowFitView = &camera;
    renderer.Draw(context, alpha, scene, camera);
    Graphics::SunLight::_extShadowFitView = nullptr;
    if (g_run.wait > 0) {
        --g_run.wait;
        return;
    }
    ReadTile(g_run);
    ++g_run.tile;
    g_run.wait = kSettle;
    if (g_run.tile >= g_run.grid.count()) {
        Finish();
        return;
    }
    char text[64];
    std::snprintf(text, sizeof text, "Taking the screenshot: %d of %d", g_run.tile + 1, g_run.grid.count());
    SetStatus(text);
}

}  // namespace TownScreenshot
}  // namespace Ext
