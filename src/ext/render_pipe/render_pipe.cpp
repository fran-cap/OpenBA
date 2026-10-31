// ============================================================================
// src/ext/render_pipe/render_pipe.cpp -- ext.render_pipe: the split frame, its
// packet emitted on a render thread (RENDERPIPE-1) or, on fewer than three
// physical cores, on the main thread in the pipelined order (RENDERPIPE-0)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RENDERPIPE0.md, RENDERPIPE1.md;
// THREADRESEARCH.md sections 1.4, 1.5 and 5.1.
//
// THE SEAM. Engine::MainLoop::_extFrame (MainLoop.h), null by default. While
// the extension is installed, MainLoop::Frame calls `Hook`:
//   kExtBeforeSteps  before the first fixed step of a Frame that steps: THE
//                    FENCE -- the pending packet is joined (sequential: emitted;
//                    threaded: the render thread waited idle), so the sim never
//                    runs with render work in flight;
//   kExtFrameTail    in place of the tail's `_time` store and `Draw(alpha)`.
//
// THE TAIL (pipe_driver.h's Driver over pipe_state.h's PipeState): either the
// faithful tail (the two statements, copied below) after joining anything
// pending, or the pipelined frame:
//   1. the main half of frame N+1 into the free Renderer slot (MainHalf: a copy
//      of GameUpdateLoop::Draw steps 1-6 and Renderer::Draw's pass 1, the
//      context writes recorded into the packet as values) -- on the render
//      thread's machine, CONCURRENT with the emit of frame N;
//   2. the join of frame N: the render thread waited idle (THE HAND-OFF), or,
//      sequential, N emitted here (EngineOps through packet.h's Emit:
//      MainLoop::Frame's `_time` store, MainLoop::Draw's BeginCommands /
//      ClearResources / viewport, the recorded context writes, pass 2, the
//      post-scene draws, the post-draw callbacks, EndCommands, the frame
//      index, Finish);
//   3. N+1 held -- threaded: published to the render thread.
//
// THE RENDER THREAD (RENDERPIPE-1): one persistent System::Thread at NORMAL
// priority, created at install when Cpu::RenderThread(physical cores), parked
// on handoff.h's condition variable between packets, stopped and joined at
// uninstall. The device lock moves with the emit: BeginCommands takes it and
// FinishPresent drops it, both on the render thread; the main half takes none.
// A failed Present is only REPORTED by the render thread; the recovery
// (finish_split.h) runs on the main thread at the next join.
//
// THE RELEASE (RENDERPIPE_FENCE.md): after the post-draw callbacks the emit
// releases the fence (EngineOps::Released -> Handoff::Release), so the steps
// overlap EndCommands, the frame index and Present. The command stream is the
// same: only the main thread's wait moves.
//
// THE OVERLAP (docs/android/P13_OVERLAP.md): with the render thread up and the
// fence wait worth it (overlap_governor.h), the main half ends by pointing the
// packet's items at frozen copies of their geometry (snapshot.h); such a packet
// is not waited for at THE FENCE: its pass 2 runs beside the next Frame's steps,
// and its Gate (EngineOps::Gate, between pass 2 and the post-scene draws, the
// device lock dropped while it waits) holds the UI, tool and save-load draws
// until the main thread reaches the Frame tail, which opens it and waits for the
// release there (the fence, moved after the steps).
// Before MainLoop::Update ends the loop (kExtBeforeTransition) the thread is joined.
//
// THE SIX FIXES (THREADRESEARCH section 1.5):
//   1 two Renderer instances, ping-pong: slot 0 = GameUpdateLoop::m_renderer,
//     slot 1 = g_second (allocated at install, freed at uninstall);
//   2 pass 1 latches the index the context will hold at this packet's pass 2
//     (pipe_driver.h: the pending packet's + 1, or the context's when nothing
//     is pending -- the context is never read while the render thread runs);
//   3 the context writes made before pass 2 are packet values replayed by the
//     emit in retail's order;
//   4 SunLight::Set (Weather::DrawUpdate's sun half) and
//   5 PlayerController::UpdateCamera run only when nothing is pending (the
//     render thread idle and joined); otherwise skipped -- the RenderPipeIdem
//     shadow proves the skipped call would have written the same bytes (the
//     camera half runs after the join, render thread idle, on the real camera
//     with its bytes restored);
//   6 the SkinMemo scope around pass 2 only (passes.cpp).
// ============================================================================
#include "ext/render_pipe/render_pipe.h"
#include "ext/better_trees/better_trees.h"   // NATURAL_GROWTH.md: SeasonStamp
#include "Game/Calendar.h"                     // NATURAL_GROWTH.md: GameUpdateLoop::m_dayCounter's year time
#include "ext/render_pipe/render_pipe_internal.h"
#include "ext/render_pipe/packet.h"
#include "ext/render_pipe/pipe_state.h"
#include "ext/render_pipe/pipe_driver.h"
#include "ext/render_pipe/handoff.h"
#include "ext/render_pipe/finish_split.h"
#include "ext/render_pipe/snapshot.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/MainLoop.h"
#include "engine/UpdateLoop.h"
#include "graphics/Camera.h"
#include "graphics/Mesh.h"        // MeshInstance::_extInstancedState (P13_OVERLAP)
#include "graphics/SunLight.h"
#include "math/Math.h"
#include "object/External.h"
#include "system/Thread.h"
#include "system/Timer.h"
#include "system/Value.h"
#include "video/CompanionData.h"   // Video::ContextData: BeginCommands / EndCommands / Finish
#include "video/Context.h"
#include "video/Device.h"
#include "ui/UserInterface.h"
#include "save/SaveLoad.h"
#include "Game/GameUpdateLoop.h"
#include "frame_limiter.h"     // src/app: FrameLimiter::TickInGame (the in-game frame cap), LastWaitUs
#include "platform_perf_hint.h"   // Platform::PerfHint: the render thread's performance hint session (Android P12)
#include "Game/Minimap.h"
#include "Game/PlayerController.h"
#include "Entity/ComponentEntity.h"   // PlayerToolManager.h instantiates InternalPtr<ComponentEntity>::Get
#include "Game/PlayerToolManager.h"
#include "Game/WeatherSimulation.h"
#include "Game/exe_constants.h"   // kRdata_*
#include "Component/EnvironmentDescription.h"
#include "Player/Tool.h"
#include "ext/registry.h"
#include "platform/common/cpu/roster.h"      // Cpu::RenderThread (block CPULIMIT)
#include "platform/common/cpu/topology.h"    // Cpu::Get().physical
#include "ext/eye_camera/eye_camera.h"
#include "ext/touch_pointer/touch_pointer.h"   // ToolAimHeld: the tool flag (P11b)
#include "ext/sharp_ui/sharp_ui.h"
#include "ext/ambient_occlusion/ambient_occlusion.h"   // AOCORE: the AO arm, recorded into the packet
#include "options.h"
#include "renderdoc_integration.h"
#include "scene_watch.h"
#include "stress_harness.h"   // g_verifyOn / StressHarness_Verify, the pipe draw brackets

namespace Ext {
namespace RenderPipe {
namespace {

// Video::Context's dirty bit beside `_time` (mainloop_frame.cpp kContextDirtyBit).
constexpr uint32_t kContextDirtyFrameTime = 0x800;
// Weather::DrawUpdate's dirty bits for the four terrain floats (weather_sun.cpp).
constexpr uint32_t kContextDirtyWeather = 0x10800;

Graphics::Renderer* g_second    = nullptr;  // fix 1: slot 1
bool                g_installed = false;

Graphics::Renderer* SlotRenderer(GameUpdateLoop& g, int32_t slot) {
    return slot == 0 ? g.m_renderer : g_second;
}

inline uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// weather_sun.cpp's UnpackColor (anonymous there), copied: one packed colour to
// {r, g, b, alpha} with each channel / 255.
inline float BitsF(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
inline Math::Vector4 UnpackColor(uint32_t c, float alpha) {
    const float k255 = BitsF(kRdata_14017222c);
    return Math::Vector4(static_cast<float>((c >> 16) & 0xFF) / k255,
                         static_cast<float>((c >> 8) & 0xFF) / k255,
                         static_cast<float>(c & 0xFF) / k255,
                         alpha);
}

// The BANISHED_FORCE_SNOW diagnostic, parsed once (GameUpdateLoop::Draw keeps
// its own copy of the same two statics).
bool ForceSnow(float* value) {
    static int   s_forceSnow = -1;
    static float s_snowVal   = 0.0f;
    if (s_forceSnow < 0) {
        const char* e = Opt().forceSnow;
        s_forceSnow = (e && e[0]) ? 1 : 0;
        if (s_forceSnow) s_snowVal = static_cast<float>(std::atof(e));
    }
    *value = s_snowVal;
    return s_forceSnow != 0;
}

// ===========================================================================
// The faithful tail: MainLoop::Frame's two statements, as the seam replaced them.
// ===========================================================================
void FaithfulTail(::Engine::MainLoop& loop, float alpha, float time) {
    Video::Context* context = loop._context;
    context->_time.x = time;
    context->_time.y = 0.0f;
    context->_time.z = 0.0f;
    context->_time.w = 0.0f;
    context->_dirty |= kContextDirtyFrameTime;
    loop.Draw(alpha);
}

// ===========================================================================
// Fixes 4 and 5: the RenderPipeIdem shadow on a frame that skips the writes.
// ===========================================================================

// UpdateCamera writes the camera's frustum (+0x00..+0x60, SetFrustum's 0x60 + 1
// bytes) and its projection (+0x94..+0xd3). Run the real call, compare, put
// the bytes back: a COMPPROF=4 run draws exactly what a COMPPROF=0 run draws.
// RENDERPIPE-1: called by the Driver AFTER the join (Host::AfterJoin), so the
// render thread -- whose tools read the live camera -- is idle while the real
// camera's bytes are written and restored. Nothing between the main half and
// the join writes UpdateCamera's inputs (the camera transform and the back
// buffer are written in steps and at resize frames, neither of which pipelines).
void IdemCamera(PlayerController& pc) {
    Graphics::Camera& cam = pc.camera;
    Graphics::Camera saved(cam);   // retail's partial copy ctor (Camera.h) ...
    saved = cam;                   // ... then the trivial assignment copies every byte
    pc.UpdateCamera(kRdata_1401721dc, kRdata_140172174, kRdata_140172240);
    bool same = cam._frustum.isOrthographic == saved._frustum.isOrthographic;
    for (int i = 0; i < 6 && same; ++i) {
        const Math::Vector4& a = cam._frustum.planes[i];
        const Math::Vector4& b = saved._frustum.planes[i];
        same = Bits(a.x) == Bits(b.x) && Bits(a.y) == Bits(b.y) &&
               Bits(a.z) == Bits(b.z) && Bits(a.w) == Bits(b.w);
    }
    for (int i = 0; i < 16 && same; ++i) same = Bits(cam._projection.m[i]) == Bits(saved._projection.m[i]);
    cam = saved;
    StressHarness_Verify("RenderPipeIdem", same, 2);   // detail 2 = the camera
}

// ===========================================================================
// The main half of frame N+1: GameUpdateLoop::Draw steps 1-6 and pass 1, with
// steps 2 / 5's context writes and steps 8-12's inputs recorded into `p`.
// A COPY of gameupdateloop.cpp's Draw (the faithful body is untouched).
// ===========================================================================
void MainHalf(::Engine::MainLoop& loop, GameUpdateLoop& g, Graphics::Renderer& r, float alpha,
              bool renderIdle, bool verify, Packet& p) {
    FrameLimiter::TickInGame();   // the in-game frame cap, as GameUpdateLoop::Draw's (src/app/frame_limiter.h)
    StressHarness_DrawBegin();   // [STRESS] render-frame period + the Draw bracket

    // Step 1: pause -> alpha 1.0 (no interpolation).
    const bool paused = g.m_userInterface->PausesGame();
    const bool entityUIPaused = g.m_toolManager->paused;
    if (paused || entityUIPaused) alpha = kRdata_14017214c;
    p.alpha = alpha;

    // Step 2: SetTime's game time. The emit adds the frame time (it reads
    // MainLoop::_frameUpdateTime, which the previous emit wrote, as retail's
    // SetTime reads the previous Draw's).
    p.gameTimeTicks = g.m_gameTime;
    p.seasonStamp = 0.0f;   // NATURAL_GROWTH.md: ext.better_trees' day of the year (Draw step 2's seam)
    if (Ext::BetterTrees::NaturalGrowthOn() && g.m_dayCounter != nullptr)
        p.seasonStamp = Ext::BetterTrees::SeasonStamp(g.m_dayCounter->yearTime.AsSeconds());

    // Step 3 (PaintTerrain) never reaches here: PlanTail runs that frame unpipelined.

    // Step 4: the camera. Fix 5: only while nothing is pending (the render
    // thread joined). A skipping frame's RenderPipeIdem camera check runs after
    // the join (Host::AfterJoin -> IdemCamera).
    if (renderIdle) {
        g.m_playerController->UpdateCamera(kRdata_1401721dc, kRdata_140172174, kRdata_140172240);
    }

    // Step 5: Weather::DrawUpdate (weather_sun.cpp), split. The context half
    // (the four floats, the fog) is recorded (fix 3); the sun half runs only
    // while nothing is held (fix 4).
    {
        WeatherSimulation* const w = g.m_weather;
        EnvironmentDescription::WeatherEntry setting;
        w->BlendLighting(&setting);
        p.weather[0] = w->field_0x1C.x;
        p.weather[1] = w->field_0x1C.y;
        p.weather[2] = w->field_0x1C.z;
        p.weather[3] = w->field_0x1C.w;
        p.fogNear = setting.fogNear;
        p.fogFar  = setting.fogFar;
        const Math::Vector4 fog = UnpackColor(setting.fogColor, setting.fogMax);
        p.fogColor[0] = fog.x; p.fogColor[1] = fog.y; p.fogColor[2] = fog.z; p.fogColor[3] = fog.w;

        const Math::Vector4 ambient = UnpackColor(setting.ambientColor, 1.0f);
        const Math::Vector4 light   = UnpackColor(setting.lightColor, 1.0f);
        const Math::Scalar heading(setting.sunHeading);
        const Math::Scalar pitch(-setting.sunPitch);
        const Math::Vector3 dir = Math::Vector3::FromYawPitch(heading, pitch);
        Graphics::SunLight* const sun = GameUpdateLoop::_singleton->m_sunLight;
        if (renderIdle) {
            sun->Set(dir, light, ambient);
        } else if (verify) {
            const bool same =
                Bits(sun->_direction.x) == Bits(dir.x) && Bits(sun->_direction.y) == Bits(dir.y) &&
                Bits(sun->_direction.z) == Bits(dir.z) &&
                Bits(sun->_sun.x) == Bits(light.x) && Bits(sun->_sun.y) == Bits(light.y) &&
                Bits(sun->_sun.z) == Bits(light.z) && Bits(sun->_sun.w) == Bits(light.w) &&
                Bits(sun->_ambient.x) == Bits(ambient.x) && Bits(sun->_ambient.y) == Bits(ambient.y) &&
                Bits(sun->_ambient.z) == Bits(ambient.z) && Bits(sun->_ambient.w) == Bits(ambient.w);
            StressHarness_Verify("RenderPipeIdem", same, 1);   // detail 1 = the sun
        }
        p.forceSnow = ForceSnow(&p.snow);
    }

    // Step 6: the shader detail, on the instance about to run pass 1 (fix 1).
    {
        uint32_t shadowMode = 0;
        if (::Video::Configuration::_singleton)
            shadowMode = ::Video::Configuration::_singleton->shaderDetail;   // +0x30
        r._shaderDetail = static_cast<int32_t>(shadowMode);
        p.shaderDetail  = static_cast<int32_t>(shadowMode);
    }
    SceneWatch_LodCensus(g.m_graphicsScene, &g.m_playerController->camera, 0);   // [diag]

    // Step 7, pass 1 (passes.cpp), with the main-owned frame index (fix 2).
    Pass1(r, loop._context->_defaultRenderBuffer, alpha, p.frameIndex, *g.m_graphicsScene,
          g.m_playerController->camera);
    SceneWatch_DrawItemCensus(&r);   // [diag]

    // Step 8: SaveLoad::Draw's receiver and material.
    p.saveLoad   = g.m_saveLoad;
    p.uiMaterial = g.m_uiMaterial;
    // Step 10: the UI camera matrices, as values.
    {
        const Graphics::Camera* camera = &g.m_playerController->camera;
        const Math::Transform3 transform3 = camera->_cameraToWorld;
        const Math::Matrix viewMatrix = transform3.AsMatrix();
        const Math::Matrix projMatrix = camera->_projection;
        for (int i = 0; i < 16; ++i) {
            p.view[i] = viewMatrix.m[i];
            p.proj[i] = projMatrix.m[i];
        }
    }
    // Step 11: the tool and its flag.
    {
        bool hasInput         = (g.m_userInterface->_mouseCaptured != 0);
        const bool pauseCheck = g.m_userInterface->PausesGame();
        p.tool     = g.m_toolManager->currentTool.Get();
        // ext.touch_pointer (P11B_POINTER.md §5): GameUpdateLoop::Draw's same seam, a pressed dialog keeps the ghost.
        if (hasInput && Ext::Ext().touch_pointer && Ext::TouchPointer::ToolAimHeld(p.tool))
            hasInput = false;
        p.toolFlag = (hasInput || pauseCheck) ? 1 : 0;
    }
    // Step 12: which UI draw.
    p.sharpUi = Ext::Ext().sharp_ui;
    // Step 7's ext.ambient_occlusion arm (GameUpdateLoop::Draw), as a value for pass 2.
    p.aoOn = Ext::AmbientOcclusion::Enabled() && Ext::AmbientOcclusion::BuildFrame(*g.m_playerController, p.ao);

    StressHarness_PipeMainEnd();   // [STRESS] draw main ms
}

// ===========================================================================
// The emit: packet.h's Emit over the engine.
// ===========================================================================
struct EngineOps {
    ::Engine::MainLoop& loop;
    Video::Context*     ctx;
    Handoff*            handoff;           // non-null: the render thread (Finish split, the fence released)
    int64_t             frameStartTicks;   // MainLoop::_frameStartTicks for FrameUpdateTime
    bool                check  = false;    // COMPPROF=4: hash the frame at the release
    bool                failed = false;
    int32_t             hr     = 0;        // threaded: FinishPresent's result (0 = presented)
    uint64_t            post   = 0;        // the RenderPipe shadow's hash at the release

    void FrameTailTime(const Packet& p) {
        ctx->_time.x = p.frameTime;
        ctx->_time.y = 0.0f;
        ctx->_time.z = 0.0f;
        ctx->_time.w = 0.0f;
        ctx->_dirty |= kContextDirtyFrameTime;
    }
    bool BeginCommands() {
        const bool begun = ctx->_contextData->BeginCommands();
        if (handoff != nullptr) handoff->NoteBegun();   // the governor: its waits before here are the GPU's / display's
        return begun;
    }
    void BeginFailed() {
        failed = true;
        loop._currentLoop->DrawFailed();
        Video::Context* c = loop._context;
        loop._contextDirty = 0;
        c->Reset();
    }
    void ClearResourcesIfDirty() {
        if (loop._contextDirty) {
            Video::Context* c = loop._context;
            loop._contextDirty = 0;
            c->ClearResources();
        }
    }
    void ViewportReconcile() {
        Video::Context* c = loop._context;
        const Video::RenderBuffer* rb = c->_defaultRenderBuffer;
        const int32_t w = rb->width;
        const int32_t h = rb->height;
        if (c->_viewport.x != 0 || c->_viewport.y != 0 ||
            c->_viewport.width != w || c->_viewport.height != h) {
            c->_dirty |= 0x200u;
            c->_viewport.x = 0;
            c->_viewport.y = 0;
            c->_viewport.width  = w;
            c->_viewport.height = h;
        }
    }
    void PacketBegin() { StressHarness_PipePacketBegin(); }
    void RenderDocFrame() { RenderDocIntegration::OnFrame(); }
    void SetTime(const Packet& p) {
        ::Engine::MainLoop* engine = ::Engine::MainLoop::_singleton;
        System::Time gameTime;        gameTime._ticks = p.gameTimeTicks;
        System::Time engineFrameTime; engineFrameTime._ticks = engine->_frameUpdateTime;
        const float gameSeconds  = gameTime.AsSeconds();
        const float frameSeconds = engineFrameTime.AsSeconds();
        ctx->SetTime(gameSeconds + frameSeconds * p.alpha);
        if (p.seasonStamp > 0.0f) ctx->_time.y = p.seasonStamp;   // NATURAL_GROWTH.md (GameUpdateLoop::Draw step 2)
    }
    void WeatherContext(const Packet& p) {
        ctx->_unknown03b8.x = p.weather[0];
        ctx->_unknown03b8.y = p.weather[1];
        ctx->_unknown03b8.z = p.weather[2];
        ctx->_unknown03b8.w = p.weather[3];
        ctx->_pixelConstantStaging.pixel[4].x = p.weather[0];
        ctx->_pixelConstantStaging.pixel[4].y = p.weather[1];
        ctx->_pixelConstantStaging.pixel[4].z = p.weather[2];
        ctx->_pixelConstantStaging.pixel[4].w = p.weather[3];
        ctx->_dirty |= kContextDirtyWeather;
        ctx->SetFog(p.fogNear, p.fogFar,
                    Math::Vector4(p.fogColor[0], p.fogColor[1], p.fogColor[2], p.fogColor[3]));
    }
    void ForceSnow(const Packet& p) {
        if (p.forceSnow) {
            ctx->_unknown03b8.x = p.snow;
            ctx->_pixelConstantStaging.pixel[4].x = p.snow;
        }
    }
    void Pass2(const Packet& p) {
        if (g_verifyOn) {
            // RenderPipeFrame: what pass 2 sees equals the sequential order's.
            const Graphics::Renderer& r = *p.renderer;
            const bool same = r._frameIndex == ctx->_frameIndex && r._frameIndex == p.modelIndex &&
                              Bits(r._alphaT) == Bits(p.alpha) &&
                              r._sceneGroups._size > 0 &&
                              r._sceneGroups._data[0]._shaderDetail == p.shaderDetail;
            StressHarness_Verify("RenderPipeFrame", same,
                                 (static_cast<long long>(r._frameIndex) << 32) |
                                     static_cast<long long>(static_cast<uint32_t>(ctx->_frameIndex)));
        }
        Ext::RenderPipe::Pass2(*p.renderer, *ctx, p.aoOn ? &p.ao : nullptr);
    }
    // P13_OVERLAP: when the next Frame's steps claimed the gate, wait until they are over, the
    // device lock dropped meanwhile (a step that uploads takes it: P13_OVERLAP 2.5).
    void Gate(const Packet& p) {
        if (handoff == nullptr || !p.overlap || handoff->PassGate()) return;
        Video::Device* const device = ctx->_device;
        device->Unlock();
        handoff->WaitGate();
        device->Lock();
    }
    void SaveLoadDraw(const Packet& p) {
        if (p.saveLoad != nullptr)
            p.saveLoad->Draw(*ctx, Object::ExternalPtr<Graphics::Material>(p.uiMaterial));
    }
    void SetCamera(const Packet& p) {
        Math::Matrix viewMatrix;
        Math::Matrix projMatrix;
        for (int i = 0; i < 16; ++i) {
            viewMatrix.m[i] = p.view[i];
            projMatrix.m[i] = p.proj[i];
        }
        ctx->SetCamera(viewMatrix, projMatrix);
    }
    void ToolDraw(const Packet& p) {
        if (p.tool != nullptr) p.tool->Draw(ctx, p.toolFlag);
    }
    void UIDraw(const Packet& p) {
        if (p.sharpUi)
            Ext::SharpUI::Draw(*p.game->m_userInterface, *ctx);
        else
            p.game->m_userInterface->Draw(*ctx);
    }
    void PacketEnd() { StressHarness_PipePacketEnd(); }
    void PostDraw() {
        for (int32_t i = 0; i < loop._postDraw._size; ++i) {
            ::Engine::MainLoop::PostDrawCallback e = loop._postDraw._data[i];
            e._func(*loop._context, e._context);
        }
    }
    // RENDERPIPE_FENCE: the last read of the packet's Renderer and sun (the
    // RenderPipe shadow's post hash), then the fence lets the steps run. Nothing
    // below reads sim, scene, UI or Renderer state.
    void Released(const Packet& p) {
        if (check) post = HashFrame(*p.renderer, p.game->m_sunLight);
        if (handoff != nullptr) handoff->Release();
    }
    void EndCommands() { loop._context->_contextData->EndCommands(); }
    void FrameUpdateTime() {
        // Retail: Now - MainLoop::_frameStartTicks. The start is handed in by
        // Host::Emit: the main thread rewrites the live field at every Frame
        // start, which the render thread must not read.
        if (::Engine::MainLoop* gm = ::Engine::MainLoop::_singleton)
            gm->_frameUpdateTime = System::Timer::CurrentTimeTicks() - frameStartTicks;
    }
    void IncFrameIndex() { ++ctx->_frameIndex; }
    void Finish() {
        if (handoff != nullptr)
            hr = FinishPresent(*ctx);          // Present + Unlock; a loss is only reported
        else
            ctx->_contextData->Finish();       // the faithful slot: Present + the inline recovery
    }
};

// ===========================================================================
// The Driver's Host (pipe_driver.h) over the engine.
// ===========================================================================
// P13_OVERLAP: the snapshot's share of the [Stress::pipe] window (main thread).
struct SnapshotCounters {
    uint64_t built   = 0;
    uint64_t refused = 0;
    uint64_t us      = 0;
    uint64_t bytes   = 0;
};
SnapshotCounters g_snapshot;
Handoff*         g_handoff = nullptr;   // non-null: the render thread is up

// The first refusals, by reason, once each (a type name or a rule).
void NoteRefusal(const char* why) {
    static const char* s_said[8] = {};
    for (const char*& said : s_said) {
        if (said == why) return;
        if (said == nullptr) {
            said = why;
            std::fprintf(stderr, "[RenderPipe] snapshot refused (%s): the packet keeps the fence\n",
                         why != nullptr ? why : "?");
            std::fflush(stderr);
            return;
        }
    }
}

struct EngineHost {
    // Written only while nothing is in flight (the first Hook call after install).
    ::Engine::MainLoop* loop = nullptr;
    // The main-thread side of one tail (never read by the render thread).
    GameUpdateLoop* game   = nullptr;
    float           alpha  = 0.0f;
    float           time   = 0.0f;
    bool            verify = false;

    int32_t ContextFrameIndex() { return loop->_context->_frameIndex; }

    void MainHalf(Packet& next, const TailPlan& plan) {
        GameUpdateLoop& g = *game;
        Graphics::Renderer* const r = SlotRenderer(g, next.slot);
        next.game            = &g;
        next.renderer        = r;
        next.frameTime       = time;
        next.frameStartTicks = loop->_frameStartTicks;   // this Frame's start (threaded FrameUpdateTime)
        Ext::RenderPipe::MainHalf(*loop, g, *r, alpha, plan.renderIdle, verify, next);
        next.idleUs = FrameLimiter::LastWaitUs();   // the frame cap's sleep in it (the governor's idle)
        if (verify) {
            next.hash   = HashFrame(*r, g.m_sunLight);
            next.hashed = true;
        }
        if (g_handoff != nullptr && next.snapshot) Freeze(next, *r, g);   // the governor asked (pipe_driver.h)
    }

    // P13_OVERLAP: the packet's items at frozen copies (snapshot.h), when every one is covered.
    void Freeze(Packet& next, Graphics::Renderer& r, GameUpdateLoop& g) {
        const uint64_t live = verify ? HashFrame(r, g.m_sunLight, false) : 0;
        const int64_t t0 = NowUs();
        const SnapshotResult s = BuildSnapshot(next.slot, r, Ext::Ext().terrain_edit);
        const int64_t us = NowUs() - t0;
        g_snapshot.us += static_cast<uint64_t>(us);
        if (!s.ok) {
            ++g_snapshot.refused;
            NoteRefusal(s.refused);
            return;
        }
        ++g_snapshot.built;
        g_snapshot.bytes += s.bytes;
        next.overlap = true;
        next.copyUs  = us;
        if (verify) {
            // RenderPipeCopy: the copies hold what the live objects held; the shadow's later
            // hashes (the emit's pre / post) are of the copies.
            const uint64_t copied = HashFrame(r, g.m_sunLight, false);
            StressHarness_Verify("RenderPipeCopy", copied == live, static_cast<long long>(s.geometries));
            next.hash = HashFrame(r, g.m_sunLight);
        }
    }

    // The EMITTING thread: the render thread (handoff non-null) or the main thread.
    EmitResult Emit(const Packet& p, Handoff* handoff) {
        const bool threaded = handoff != nullptr;
        EmitResult res;
        Video::Context* const ctx = loop->_context;   // only the main thread's recovery replaces it, at a join
        const bool check = g_verifyOn != 0 && p.hashed;
        const uint64_t pre = check ? HashFrame(*p.renderer, p.game->m_sunLight) : 0;

        // Sequential: RENDERPIPE-0's FrameUpdateTime, the live start of the Frame
        // whose tail emits (no other thread writes it).
        const int64_t start = threaded ? p.frameStartTicks : loop->_frameStartTicks;

        StressHarness_PipeEmitBegin();
        EngineOps ops{ *loop, ctx, handoff, start };
        ops.check = check;
        // The render thread's frame work for its performance hint session (a no-op off Android).
        if (threaded) Platform::PerfHint::WorkBegin(Platform::PerfHint::Role::Render);
        Ext::RenderPipe::Emit(ops, p);
        if (threaded) Platform::PerfHint::WorkEnd(Platform::PerfHint::Role::Render);
        StressHarness_PipeEmitEnd();

        res.failed = ops.failed;
        if (threaded) {
            res.lost    = !ops.failed && ops.hr != 0;
            res.recover = res.lost;
            res.hr      = ops.hr;
        } else {
            res.lost = loop->_context != ctx;   // the faithful Finish recovered inline
        }
        if (check) {
            const bool kept = !res.failed && !res.lost;
            const uint64_t post = kept ? ops.post : p.hash;   // hashed at the release (EngineOps::Released)
            const bool same = pre == p.hash && post == p.hash;
            StressHarness_Verify("RenderPipe", same, (pre != p.hash ? 1 : 0) | (post != p.hash ? 2 : 0));
        }
        return res;
    }

    // Main thread, render thread idle: Finish's recovery half, where retail runs it.
    void Recover(const EmitResult& r) {
        Video::Device& device = *loop->_context->_device;
        std::fprintf(stderr, "[RenderPipe] device lost in the render thread's Present (hr=0x%08x): "
                             "recovery on the main thread, the gathered packet discarded\n",
                     static_cast<unsigned>(r.hr));
        std::fflush(stderr);
        FinishRecover(device, r.hr);
    }

    void AfterJoin(const Packet& next) {
        if (verify) IdemCamera(*next.game->m_playerController);
    }

    // P13_OVERLAP: the pose seam answers for frozen copies only while a packet that has them
    // is in flight; null otherwise, so the sim's pose lookups run retail's code alone. Written
    // here with the render thread idle; the publish orders it before the emit's reads.
    void BeforePublish(const Packet& next) {
        Graphics::MeshInstance::_extInstancedState = next.overlap ? &FrozenInstancedState : nullptr;
    }

    void OverlapSwitch(const OverlapGovernor& gov) {
        std::fprintf(stderr, "[RenderPipe] overlap %s: removable fence wait %lld us a frame, idle %lld us "
                             "(%lld without the copies), copy %lld us\n",
                     gov.On() ? "on" : "off", static_cast<long long>(gov.RemovableUs()),
                     static_cast<long long>(gov.IdleUs()), static_cast<long long>(gov.IdleWithout()),
                     static_cast<long long>(gov.CopyUs()));
        std::fflush(stderr);
    }

    void FaithfulTail() { Ext::RenderPipe::FaithfulTail(*loop, alpha, time); }
};

EngineHost          g_host;
Driver<EngineHost>  g_driver;

// The render thread: System::Thread's Start / park protocol (as path_workers'
// PoolThread); Run is the Driver's worker loop, which parks on the Handoff
// between packets and returns at Stop. The base destructor then signals its
// stop event and joins. NORMAL priority (System::Thread's ctor sets it).
class RenderThread final : public System::Thread {
public:
    // The emit runs UI / tool / save-load draws that ran on the main thread's
    // stack; give it a main-thread-sized stack, not the pool's 64 KB.
    RenderThread() : System::Thread(0x100000) {}
    ~RenderThread() = default;
    // P12: the thread's performance hint session lives as long as the thread (platform_perf_hint.h).
    void Run() override {
        Platform::PerfHint::AttachThread(Platform::PerfHint::Role::Render);
        g_driver.WorkerLoop(g_host);
        Platform::PerfHint::DetachThread(Platform::PerfHint::Role::Render);
    }
};
RenderThread* g_thread = nullptr;

TailInputs Inputs(::Engine::MainLoop& loop, GameUpdateLoop* g) {
    TailInputs in;
    in.want     = Ext::Ext().render_pipe;
    in.gameLoop = g != nullptr && loop._currentLoop == g && g->m_game != nullptr;
    in.quitting = loop._quit != 0;
    in.settingsChanged = loop._context->_device->settingsChanged != 0;
    if (in.gameLoop) {
        in.paintTerrain = g->m_imageCapture->terrainImage == nullptr;
        in.screenshot   = g->m_quickSavePressed != 0 || g->m_quickLoadPressed != 0;
    }
    // Ext::EyeCamera::Active() reads the game loop's tool manager and UI: only inside the built
    // game (2026-10-14: called from the menu loop at boot, it crashed every
    // eye_camera + render_pipe run; the menu loop is never pipelined anyway).
    in.eyeCamera = in.gameLoop && Ext::Ext().eye_camera && Ext::EyeCamera::Active();
    const char* cap = Opt().captureIngame;   // BANISHED_CAPTURE_INGAME counts sequential frames
    in.diagnostic = cap != nullptr && cap[0] != '\0';
    return in;
}

// The render thread is idle and joined (every caller drained first).
void StopThread() {
    if (g_thread == nullptr) return;
    g_handoff->Stop();
    delete g_thread;            // the base destructor joins it
    g_thread = nullptr;
    g_driver.Attach(nullptr);
    delete g_handoff;
    g_handoff = nullptr;
    std::fprintf(stderr, "[RenderPipe] render thread joined\n");
    std::fflush(stderr);
}

void Uninstall() {
    ::Engine::MainLoop::_extFrame = nullptr;
    StopThread();
    Graphics::MeshInstance::_extInstancedState = nullptr;
    FreeSnapshots();
    delete g_second;
    g_second    = nullptr;
    g_installed = false;
    g_driver.Reset();
}

// The one function behind MainLoop::_extFrame.
void Hook(::Engine::MainLoop& loop, int32_t point, float alpha, float time) {
    if (g_host.loop != &loop) g_host.loop = &loop;   // once (nothing is in flight before the first tail)
    if (point == ::Engine::MainLoop::kExtBeforeSteps) {
        g_driver.BeforeSteps(g_host);   // THE FENCE
        return;
    }
    if (point == ::Engine::MainLoop::kExtBeforeTransition) {
        g_driver.BeforeTransition(g_host);   // P13_OVERLAP: nothing in flight while the loop ends
        return;
    }
    GameUpdateLoop* const g = GameUpdateLoop::_singleton;
    g_host.game   = g;
    g_host.alpha  = alpha;
    g_host.time   = time;
    g_host.verify = g_verifyOn != 0;
    if (g_driver.Tail(g_host, Inputs(loop, g))) Uninstall();
}

}  // namespace

void Sync() {
    if (Ext::Ext().render_pipe && !g_installed) {
        if (g_second == nullptr) g_second = new Graphics::Renderer();
        g_driver.Reset();
        const int physical = Cpu::Get().physical;
        if (Cpu::RenderThread(physical)) {
            g_handoff = new Handoff();
            g_driver.Attach(g_handoff);
            g_thread = new RenderThread();
            g_thread->Start();
            std::fprintf(stderr, "[RenderPipe] render thread up (%d physical cores)\n", physical);
            std::fflush(stderr);
        } else {
            g_driver.Attach(nullptr);
            static bool s_said = false;
            if (!s_said) {
                s_said = true;
                std::fprintf(stderr, "[RenderPipe] %d physical cores: sequential split\n", physical);
                std::fflush(stderr);
            }
        }
        g_installed = true;
        ::Engine::MainLoop::_extFrame = &Hook;
    }
    // Off: Hook drains, stops the render thread and uninstalls at the next Frame
    // tail (PlanTail: want = false).
}

bool Installed() { return g_installed; }
bool Threaded() { return g_handoff != nullptr; }

Counters TakeWindow() {
    Counters c = g_driver.TakeWindow();
    c.snapshots  = g_snapshot.built;
    c.refused    = g_snapshot.refused;
    c.snapshotUs = g_snapshot.us;
    c.snapshotKB = g_snapshot.bytes / 1024;
    g_snapshot = SnapshotCounters{};
    return c;
}

}  // namespace RenderPipe
}  // namespace Ext
