// src/game/Game/minimap_paint.cpp
// GUI Keystone F — minimap TERRAIN PAINTER (render-to-texture).
//
// Port of exe FUN_1400a5140 (Ghidra label ModDialog_impl_5140), the minimap's
// "ImageCapture::DrawCheck". The retail game calls it lazily from GameWorld::Draw
// (FUN_140096010) the first frame the minimap's terrain GPU buffer (minimap+0x08)
// is still null:
//
//     if (*(minimap+0x08) == 0) {
//         Scene::RemoveGlobalObject(mainScene, sunLight);
//         Scene::RemoveGlobalObject(mainScene, worldGeom);
//         ImageCapture::DrawCheck(minimap, ctx, worldGeom, sunLight);   // <-- this
//         Scene::AddGlobalObject(mainScene, sunLight);
//         Scene::AddGlobalObject(mainScene, worldGeom);
//     }
//
// It renders the terrain geometry with a top-down orthographic camera into an
// off-screen RenderBuffer, reads the rendered color back into the minimap's CPU
// terrain pixel array (minimap+0x18) via ImageBuffer::CopyFromTexture, then
// creates the display GPU buffers (minimap+0x08/+0x10) and uploads both pixel
// arrays — the CreateAndUpload tail is shared with src/game/Game/minimap_upload.cpp.
//
// In standalone the exe function is unavailable (ResolveAddr(Exe,0x1400a5140)==NULL),
// so GameUpdateLoop::Draw's paint block was skipped and the minimap stayed blank.
// This clean port lets it run: gameupdateloop.cpp wires fn_ImageCapture_DrawCheck to
// Minimap_PaintTerrain when the exe address doesn't resolve (standalone).
//
// Minimap struct (== GameWorld+0x148):
//   +0x00 byte  dirty flag
//   +0x04 int   width (grid edge, e.g. 256)
//   +0x08 ImageBuffer* terrain GPU texture   (display; built by the tail)
//   +0x10 ImageBuffer* overlay GPU texture   (display; built by the tail)
//   +0x18 uint32*      terrain raw pixel array (width*width ARGB)  <-- painted here
//   +0x28 uint32*      overlay raw pixel array (width*width ARGB)  (stays 0 here)
//
// Ortho camera (byte-faithful to FUN_1400a5140):
//   half = width*0.5;  ortho frustum near=0 far=512 box[-half..half]x[half..-half]
//   yaw=0 pitch=-90deg (straight down), world position (0,0,100).
//   fog start=1000 end=1001 color(1,1,1,1); clear color 0xff000000 then 0xffff00ff.

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::{Camera,Scene},
// Video::{Context,RenderBuffer,ImageBuffer} are real classes. These headers go
// AHEAD of "resolve.h" — resolve.h pulls <windows.h>, whose <wingdi.h> declares
// a global `Rectangle` FUNCTION that hides `Volume::Rectangle`'s class name
// inside Volume.h's own static_asserts (Camera.h and Geometry.h include Volume.h).
#include "graphics/Camera.h"
#include "graphics/Scene.h"
#include "video/Context.h"       // also brings Video::{Device,RenderBuffer,Viewport}
#include "video/ImageBuffer.h"
#include <new>
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "options.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Configuration::_singleton
#include "Game/exe_bodies.h"
#include "Game/Minimap.h"
#include "Game/WorldGeometry.h"
#include "graphics/SunLight.h"
#include "graphics/stagger_hold.h"   // ext.staggered_passes: the capture invalidates held targets
#include "Game/exe_constants.h"   // kRdata_14017214c

// B.4 GAME-b: the painter is Minimap::PaintTerrain and reads the minimap, the
// context, the camera and the video configuration by name. Its doors (ENDGAME
// B.3.5 / B3R): Video_RenderBuffer_Ctor / _Dtor; the Camera is constructed
// directly (X5-GAME); the Frustum, Renderer and Scene are typed locals built by
// their real ctors (ENDGAME B7 CTOR: their doors are deleted).

namespace {

// Allocate + construct a Video::ImageBuffer (retail: the imported ctor + the
// exe's own copy of the class's table), then Create it as a render-target
// (color) / depth surface. Returns the buffer or null.
Video::ImageBuffer* MakeRenderTargetBuffer(int width, int usage, int format) {
    void* mem = System::MemoryAllocator::MemAlloc(0x68, 8);
    if (!mem) return nullptr;
    Video::ImageBuffer* img = ::new (mem) Video::ImageBuffer();   // B.3.9 EXA: the real class (exe table 0x14015c8c0 is its own)
    img->Create(
        /*type*/0, usage, format, /*flags*/0u, width, width,
        /*filter*/1, /*addressU*/1, /*addressV*/1, /*addressW*/1,
        /*levels*/nullptr);
    return img;
}

// Free an ImageBuffer via its scalar-deleting destructor (vtable[0](self, 1)).
void FreeImageBuffer(Video::ImageBuffer* img) {
    if (!img) return;
    delete img;   // slot 0, free
}

}  // namespace

// ============================================================
// Minimap::PaintTerrain — paint the minimap terrain (port of FUN_1400a5140).
// ============================================================
void Minimap::PaintTerrain(Video::Context& context, Graphics::Geometry* worldGeom,
                           Graphics::Geometry* sunLight) {
    if (!System::MemoryAllocator::_singleton) return;

    static bool s_trace = Opt().mapTrace != nullptr;

    int width = size;
    if (width <= 0) {
        if (s_trace) printf("[MinimapPaint] width<=0 (%d) — skip\n", width);
        return;
    }

    // --- Off-screen color + depth surfaces (temporary render target) ---
    Video::ImageBuffer* colorBuf = MakeRenderTargetBuffer(width, /*usage*/0, /*format*/5); // ARGB render target
    Video::ImageBuffer* depthBuf = MakeRenderTargetBuffer(width, /*usage*/1, /*format*/7); // depth

    Video::RenderBuffer* renderBuffer =
        static_cast<Video::RenderBuffer*>(System::MemoryAllocator::MemAlloc(0x30, 8));
    if (renderBuffer) Video_RenderBuffer_Ctor(renderBuffer);
    bool rbOk = renderBuffer->Create(colorBuf, depthBuf);
    (void)rbOk;

    // --- Orthographic top-down camera ---
    const float half = (float)width * 0.5f;
    const Math::Scalar halfS(half), negHalfS(-half);
    const Math::Scalar fNear(0.0f);
    const Math::Scalar fFar(512.0f);    // DAT_140172240

    // Transform3::Identity is a static that returns a Transform3 BY VALUE (48 bytes).
    // Retail calls it first (0x1400a5302), then the general Frustum ctor
    // ??0Frustum@Volume@@QEAA@_NAEBVScalar@Math@@11111@Z (0x1800fa3d0, CALL at
    // 0x1400a5347) on the untouched stack block [RBP+0x1c0]: no zeroing first.
    Math::Transform3 identity = Math::Transform3::Identity();
    const ::Frustum frustum(/*ortho*/true, fNear, fFar, negHalfS, halfS, halfS, negHalfS);

    // Camera::Camera(frustum, transform, zUp) 0x1800fe960 on a stack local
    // (trivially destructible: nothing runs at scope exit).
    Graphics::Camera cameraLocal(frustum, identity, /*zUp*/true);
    Graphics::Camera* camera = &cameraLocal;

    // Look straight down (yaw 0, pitch -90deg), positioned at world (0,0,100).
    const float yaw   = 0.0f;
    const float pitch = -90.0f;      // DAT_14017229c
    const float camZ  = 100.0f;      // DAT_140172208

    // Math::Transform = quaternion (xyzw) + position (xyz).
    Math::Transform transform;
    transform.rotation = Math::Quaternion::FromYawPitch(yaw, pitch);
    transform.translation = Math::Vector3(0.0f, 0.0f, camZ);

    Math::Transform3 transform3 = transform.AsTransform3();
    camera->SnapCameraToWorld(transform3);

    // View matrix from the camera's cameraToWorld transform; the projection — the
    // two reads FUN_1400a5140 inlines.
    Math::Matrix viewMatrix = camera->_cameraToWorld.AsMatrix();
    context.SetCamera(viewMatrix, camera->_projection);

    // Viewport {x=0, y=0, w=width, h=width}.
    Video::Viewport viewport;
    viewport.x = 0; viewport.y = 0; viewport.width = width; viewport.height = width;
    context.SetViewport(viewport);

    const float clearDepth = kRdata_14017214c;   // DAT_14017214c == 1.0
    context.Clear(1, 0xff000000, clearDepth, 0);

    // Redirect rendering to the off-screen buffer (saving the current + default).
    const Video::RenderBuffer* savedRenderBuffer = context._renderBuffer;
    Video::RenderBuffer* savedDefaultBuffer = context._defaultRenderBuffer;

    // The Renderer ([RBP+0x170], Renderer() 0x180105180 at 0x1400a554b) and the
    // Scene ([RBP+0xf0], Scene() 0x180102730 at 0x1400a5559) are stack locals of
    // this block. It ends right after context.Reset() (0x1400a57a2), where retail
    // runs ~Scene 0x1801026c0 (0x1400a57b0) then ~Renderer 0x1800180a0
    // (0x1400a57be): reverse declaration order.
    uint32_t* pixels = nullptr;   // read inside the block, traced after it
    {
    Graphics::Renderer renderer;
    Graphics::Scene scene;

    context.SetDefaultRenderBuffer(renderBuffer);
    context.SetRenderBuffer(renderBuffer);
    context.Clear(7, 0xffff00ff, clearDepth, 0);

    // Fog + context flags matching the retail minimap render setup.
    const Math::Vector4 fogColor(1.0f, 1.0f, 1.0f, 1.0f);
    const float fogStart = 1000.0f;  // DAT_140172248
    const float fogEnd   = 1001.0f;  // DAT_14017224c
    context.SetFog(fogStart, fogEnd, fogColor);

    context._unknown03b8 = Math::Vector4(0.0f, 0.0f, clearDepth, 0.0f);   // +0x3b8..+0x3c4
    // Context +0x288: Context::PixelConstantStaging::pixel[4] (Video/Context.h).
    Math::Vector4& row = context._pixelConstantStaging.pixel[4];
    row.x = 0.0f;         // +0x288
    row.y = 0.0f;         // +0x28c
    row.z = clearDepth;   // +0x290
    row.w = 0.0f;         // +0x294
    context._dirty |= 0x10800;

    renderer._shaderDetail = 0;   // +0x3c (SetShaderDetail's field)

    // Temporarily clear VideoConfig's reflection detail (+0x34) during the capture.
    ::Video::Configuration* videoConfig = ::Video::Configuration::_singleton;
    int32_t savedCfg = 0;
    if (videoConfig) {
        savedCfg = videoConfig->reflectionDetail;
        videoConfig->reflectionDetail = 0;
    }

    // Render the terrain (worldGeom) + sun/lighting (sunLight) top-down.
    scene.AddGlobalObject(worldGeom);
    scene.AddGlobalObject(sunLight);
    scene.Clean();
    // NOT RETAIL (ext.staggered_passes, graphics/stagger_hold.h): this draw renders the water's
    // reflection image with the top-down camera and no reflected scene, so no target the game
    // frame holds may be reused across it.
    Graphics::StaggerHold::Invalidate();
    renderer.Draw(context, 0.0f, scene, *camera);
    Graphics::StaggerHold::Invalidate();
    scene.RemoveGlobalObject(sunLight);
    scene.RemoveGlobalObject(worldGeom);

    if (videoConfig)
        videoConfig->reflectionDetail = savedCfg;

    // Restore the previous render buffers.
    context.SetRenderBuffer(savedRenderBuffer);
    context.SetDefaultRenderBuffer(savedDefaultBuffer);

    // Read the rendered color back into the terrain pixel array.
    pixels = terrainPixels._data;
    if (colorBuf && pixels)
        // ?CopyFromTexture@ImageBuffer@Video@@QEAA_NPEAXHH@Z.
        colorBuf->CopyFromTexture(pixels, width, width);

    // Free the temporary render surfaces.
    if (renderBuffer) {
        Video_RenderBuffer_Dtor(renderBuffer);
        System::MemoryAllocator::MemFree(renderBuffer);
    }
    FreeImageBuffer(colorBuf);
    FreeImageBuffer(depthBuf);

    // Create the display GPU buffers (terrainImage / overlayImage) and upload both
    // pixel arrays — the shared tail in minimap_upload.cpp (FUN_1400a4e80 + uploads).
    CreateAndUpload();

    context.Reset();
    }   // ~Scene 0x1801026c0, then ~Renderer 0x1800180a0

    if (s_trace)
        printf("[MinimapPaint] painted terrain: width=%d colorBuf=%p terrainPixels._data=%p "
               "display(+0x08)=%p\n", width, (void*)colorBuf, (void*)pixels,
               (void*)terrainImage);
}

