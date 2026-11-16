// src/game/Player/tool_base_screenrect.cpp
// Player registration wave, round 2d — batch "screenrect" (edittool.cpp deep tier).
//
// Lands 0x140100380 (Entity_impl_0380) — computes an entity's screen-space bounding
// rectangle by transforming its world bounding-box corners and projecting them through
// the camera. Called by name from src/game/Player/edittool.cpp (EditTool::Update
// positions the tool dialog near the selected entity's on-screen rectangle) and from
// src/game/Tutorial/tutorialhighlightdialog.cpp.
//
// This file provides faithful 1:1 reimplementations of the four-function closure:
//   0x1400fec70 CharacterComponent_impl_ec70  -> CharacterComponent_impl_f3e0 (0x14009f3e0)
//   0x140098210 cameraWorldToScreen           -> Math members
//   0x1400fe960 boundsAccumulate              -> 0x140098210 + Vector2 Min/Max/*=
//   0x140100380 Entity_impl_0380              -> 0x1400fec70, 0x1400fe960,
//                                                Entity_ClearPathQueue (0x140003f60),
//                                                FUN_140004680 (0x140004680),
//                                                + Volume/Vector3/Transform/Box calls
//
// Retail invokes the Math operations through exe IAT dispatch slots (0x14015xxxx). Each
// was decoded from the exe's name table (slot RVA + 2-byte hint -> mangled string) and is
// a pure Runtime export, called here by name.
//
// All value-returning Math members use the MS x64 member struct-return ABI:
//   RCX=this, RDX=&retbuf, R8/R9.. = args. The retbuf pointer is returned in RAX.
// Verified instruction-by-instruction from disassemble_function (this path is DORMANT on
// the autostart A/B test, so it is ported from disasm, not the muddled decompile).

#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "math/Math.h"
#include "volume/Volume.h"
#include "Game/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Component/exe_bodies.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "Player/exe_bodies.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "Game/GameSettings.h"
#include "Component/ModelComponent.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "graphics/Camera.h"

// ============================================================
// Math/Volume dispatch-slot exports (value-return, member ABI).
// ============================================================
// Each is a Runtime export decoded from the exe dispatch-slot name table:
//   0x140151f00 ??0Vector4@Math@@QEAA@AEBVVector3@1@AEBVScalar@1@@Z   Vector4(Vector3&,Scalar&)
//   0x140151868 ??0Transform3@Math@@QEAA@AEBV01@@Z                    Transform3(const Transform3&)
//   0x1401522d0 ?Inverse@Transform3@Math@@QEBA?AV12@XZ                Transform3::Inverse()
//   0x1401520a0 ?AsMatrix@Transform3@Math@@QEBA?AVMatrix@2@XZ         Transform3::AsMatrix()
//   0x1401522c8 ??DMatrix@Math@@QEBA?AV01@AEBV01@@Z                   Matrix::operator*(Matrix)
//   0x1401522c0 ??DMatrix@Math@@QEBA?AVVector4@1@AEBV21@@Z            Matrix::operator*(Vector4)
//   0x1401528a8 ??0Rectangle@Volume@@QEAA@XZ                          Rectangle::Rectangle()
//   0x140152a78 ??0Vector3@Math@@QEAA@XZ                              Vector3::Vector3()
//   0x140152950 ?GetCorners@Box@Volume@@QEBAXPEAVVector3@Math@@@Z     Box::GetCorners(Vector3*)
//   0x140152900 ?Min@Vector3@Math@@QEBA?AV12@AEBV12@@Z                Vector3::Min(Vector3)
//   0x140151470 ??DTransform@Math@@QEBA?AVVector3@1@AEBV21@@Z         Transform::operator*(Vector3)
//   0x140152780 ?Max@Vector2@Math@@QEBA?AV12@AEBV12@@Z                Vector2::Max(Vector2)
//   0x140152788 ?Min@Vector2@Math@@QEBA?AV12@AEBV12@@Z                Vector2::Min(Vector2)
//   0x140151e70 ??XVector2@Math@@QEAAXAEBV01@@Z                       Vector2::operator*=(Vector2)

// Rectangle::Rectangle() and Box::GetCorners are called by name:
// RtVolume_Rectangle_Ctor / RtVolume_Box_GetCorners (src/engine/volume/Volume.h).

// CharacterComponent_impl_f3e0 (0x14009f3e0) = grid->world conversion:
// Map::GetTerrainPos (Game/Map.h), called by name.

// ============================================================
// FUN_1400fec70 — build 4 footprint corners
// ============================================================
// Given a tile rectangle (startCol=p2, startRow=p3, endCol=p4, endRow=p5) writes 4
// world-space corner Vector3s into param_6[0..3]. Each corner is the grid->world
// position of a (col,row) cell: the input grid coord's z is 0, but the f3e0
// grid->world conversion fills a real z (terrain height) which is kept verbatim.
//   map = GameUpdateLoop::m_game (+0xF8); map->size = width.
//   DAT_140172114 = 0.5f (cell-centre offset).
// param_1 is never read (the 0380 caller leaves the end row in RCX).
extern "C" uint32_t PlayerScreenRect_BuildCorners_ec70(uint64_t /*param_1*/, int param_2,
                                                       int param_3, int param_4,
                                                       int param_5, Math::Vector3* param_6) {
    const float halfCell = 0.5f;  // DAT_140172114
    Map* grid = GameUpdateLoop::_singleton->m_game;

    auto cornerToWorld = [&](int col_for_idx, int row_for_idx, Math::Vector3* out) {
        int w = grid->size;
        int idx = w * row_for_idx + col_for_idx;
        float centerOff = static_cast<float>(w >> 1) - halfCell;
        float in[3];
        in[0] = static_cast<float>(idx % w) - centerOff;
        in[1] = static_cast<float>(idx / w) - centerOff;
        in[2] = 0.0f;
        float world[3] = {0.0f, 0.0f, 0.0f};
        grid->GetTerrainPos(world, in, '\0', nullptr);
        out->x = world[0];
        out->y = world[1];
        out->z = world[2];   // f3e0 fills a real z (terrain height); kept verbatim
    };

    // Corner 0: (col=p2, row=p3)
    cornerToWorld(param_2, param_3, &param_6[0]);
    // Corner 1: (col=p2, row=p5)
    cornerToWorld(param_2, param_5, &param_6[1]);
    // Corner 2: (col=p4, row=p3)
    cornerToWorld(param_4, param_3, &param_6[2]);
    // Corner 3: (col=p4, row=p5)
    cornerToWorld(param_4, param_5, &param_6[3]);
    return 4;
}

// ============================================================
// FUN_140098210 — camera world->screen projection
// ============================================================
// Projects a single world-space point (param_2) through the camera (param_1 = the
// PlayerController, GameUpdateLoop +0xE0) into screen-space pixel coordinates,
// written to param_3[0]=x, param_3[1]=y. Returns true when the point is in front of
// the camera (w > 0).
//   v4      = Vector4(param_2, g_fUVScale)                 (homogeneous, w = 1.0)
//   projMat = the camera's projection Matrix (controller +0xc8 = camera +0x94)
//   xform   = the camera Transform3 (controller +0x98 = camera +0x64)
//   mat     = xform.Inverse().AsMatrix() * projMat         (view-inverse * projection)
//   clip    = mat * v4                                     (Vector4)
//   if clip.w > 0:
//       ndc = (clip.x / w, clip.y / w)
//       screen.x = ndc.x * (W*0.5) + (W*0.5)
//       screen.y = H - (ndc.y * (H*0.5) + (H*0.5))
//   display = Video::Device::_singleton ; +4=W, +8=H ; DAT_140172114 = 0.5f.
uint8_t PlayerController::WorldToScreen(const Math::Vector3* param_2,
                                                         float* param_3) {
    const float halfScale = 0.5f;  // DAT_140172114

    float uvScale = kRdata_14017214c;  // DAT_14017214c = 1.0f (the Scalar)

    // local_f8 = Vector4(param_2, uvScale)  ([RSP+0x70] in the original)
    Math::Vector4 v4(*param_2, uvScale);

    const Graphics::Camera* camera = &this->camera;

    // The 64-byte projection Matrix ([RSP+0x20] in the original).
    Math::Matrix projMat = camera->_projection;

    // local_e8 = Transform3(camera transform)  (copy ctor)
    Math::Transform3 xform = camera->_cameraToWorld;

    // inv = xform.Inverse() ; asMat = inv.AsMatrix() ; mat = asMat * projMat
    Math::Transform3 inv    = xform.Inverse();
    Math::Matrix     asMat  = inv.AsMatrix();
    Math::Matrix     mat    = asMat * projMat;

    // clip = mat * v4
    Math::Vector4 clipVec = mat * v4;

    float w = clipVec.w;   // local_fc
    if (w <= 0.0f)
        return 0;

    const ::Video::Device* disp = ::Video::Device::_singleton;
    if (!disp) return 0;
    int screenW = disp->backBufferWidth;
    int screenH = disp->backBufferHeight;

    float ndcX = clipVec.x / w;
    float ndcY = clipVec.y / w;
    float halfW = static_cast<float>(screenW) * halfScale;
    float halfH = static_cast<float>(screenH) * halfScale;
    param_3[0] = ndcX * halfW + halfW;
    param_3[1] = static_cast<float>(screenH) - (ndcY * halfH + halfH);
    return 1;
}

// ============================================================
// FUN_1400fe960 — accumulate projected corners into a screen rectangle
// ============================================================
// Projects each of param_2 (count) world corners through PlayerController::WorldToScreen, tracks
// the per-axis min/max in screen space (seeded with +/-1e6), clamps the result to the
// display [0..W]x[0..H], scales by (GameSettings::uiScale / 100.0), and writes the
// resulting rectangle (min.xy, max.xy) to param_4. Returns true when at least one
// corner projected in front of the camera and the box overlaps the display.
//   running min seed = ( 1000000.0,  1000000.0)   (0x49742400)
//   running max seed = (-1000000.0, -1000000.0)   (0xc9742400)
//   DAT_140172208 = 100.0f. param_1 is never read.
extern "C" uint8_t PlayerScreenRect_AccumulateRect_fe960(uint64_t /*param_1*/, uint32_t param_2,
                                                        const Math::Vector3* param_3,
                                                        struct Rectangle* param_4) {
    const float bigPos =  1000000.0f;  // 0x49742400
    const float bigNeg = -1000000.0f;  // 0xc9742400
    const float scaleDiv = 100.0f;     // DAT_140172208

    const ::Video::Device* disp = ::Video::Device::_singleton;
    if (!disp) { return 0; }
    int W = disp->backBufferWidth;   // R14
    int H = disp->backBufferHeight;  // R15

    Math::Vector2 runMin(bigPos, bigPos);   // [RBP+0x30]
    Math::Vector2 runMax(bigNeg, bigNeg);   // [RBP-0x50]

    if (static_cast<int>(param_2) <= 0)
        { return 0; }

    PlayerController* camHolder = GameUpdateLoop::_singleton->m_playerController;

    bool any = false;
    const Math::Vector3* corner = param_3;
    for (uint32_t i = 0; i < param_2; i++) {
        float scr[2] = {0.0f, 0.0f};   // [RBP-0x48]
        uint8_t ok = camHolder->WorldToScreen(corner, scr);
        if (ok != 0) {
            Math::Vector2 s(scr[0], scr[1]);
            runMin = runMin.Min(s);
            runMax = runMax.Max(s);
            any = true;
        }
        corner += 1;  // stride 0xc bytes
    }

    if (!any)
        { return 0; }

    // Reject if the box is entirely off the display.
    float fW = static_cast<float>(W);
    float fH = static_cast<float>(H);
    if (runMin.x > fW) { return 0; }
    if (0.0f > runMax.x) { return 0; }
    if (runMin.y > fH) { return 0; }
    if (0.0f > runMax.y) { return 0; }

    // Clamp runMin to [0, (W,H)] : runMin = Min( Max(runMin, 0), (W,H) ).
    {
        Math::Vector2 zero(0.0f, 0.0f);
        Math::Vector2 wh(fW, fH);
        runMin = runMin.Max(zero);
        runMin = runMin.Min(wh);
    }
    // Clamp runMax similarly.
    {
        Math::Vector2 zero(0.0f, 0.0f);
        Math::Vector2 wh(fW, fH);
        runMax = runMax.Max(zero);
        runMax = runMax.Min(wh);
    }

    // Scale by (gameSettings UI value / 100.0) on both axes (in place operator*=).
    float uiScale = 1.0f;
    if (g_gameSettings != nullptr)
        uiScale = static_cast<float>(g_gameSettings->uiScale) / scaleDiv;
    {
        Math::Vector2 sv(uiScale, uiScale);
        runMin.operator*=(sv);
    }
    {
        Math::Vector2 sv(uiScale, uiScale);
        runMax.operator*=(sv);
    }

    // Write rect: min.xy, max.xy (two 8-byte stores).
    param_4->min = runMin;
    param_4->max = runMax;
    return 1;
}

// ============================================================
// FUN_140100380 — an entity's screen bounding-rectangle
// ============================================================
// Writes an entity's screen-space bounding rectangle (Volume::Rectangle, 16 bytes) to
// param_3. If the entity's ModelComponent has a mesh (+0x166 >= 0) it uses the
// component's Transform (+0x148) and its MeshInstance's local Box (+0x28 + 0x34):
// transforms the box's 8 corners and projects them. Otherwise it falls back to the 4
// footprint corners built from the MapComponent's tile rect (FUN_1400fec70).
//
//   param_2 = InternalPtr<ComponentEntity>* (the entity)
//   param_3 = out Rectangle (16 bytes)
//   const clamp vec (R8 in the Min loop) = (1e6, 1e6, 2.5)  (0x49742400,0x49742400,0x40200000)
//
// Return value: the original falls straight into the fe960 tail call and returns
// whatever fe960 left in AL (its "rect is valid" bool) — EditTool reads it as
// `char cVar5` and deselects when 0.
extern "C" uint8_t PlayerScreenRect_EntityRect_0380(const void* /*param_1*/,
                                                    ComponentEntity* const* param_2,
                                                    struct Rectangle* param_3) {

    // local_a8 = Rectangle()  (default ctor); copy its 16 bytes to param_3 as the seed.
    struct Rectangle rect;
    struct Rectangle* rectPtr = RtVolume_Rectangle_Ctor(&rect);
    *param_3 = rectPtr ? *rectPtr : rect;

    // local_88 = Vector3[8] (96 bytes), each default-constructed.
    Math::Vector3 corners[8];

    // CheckPtr(param_2) ; FindComponent<ModelComponent> (0x140003f60)
    ModelComponent* model = (*param_2)->FindComponent<ModelComponent>();

    uint32_t cornerCount = 0;  // R15D — count fed to fe960

    bool useBox;
    if (model == nullptr) {
        useBox = false;
    } else {
        // meshIndex (+0x166) < 0  -> no mesh -> fallback
        useBox = (model->meshIndex >= 0);
    }

    if (useBox) {
        // CheckPtr ; copy the Transform (+0x148).
        Math::Transform transform = model->transform;

        // CheckPtr ; copy the MeshInstance's local Box (+0x5c..+0x70).
        ::Box box = model->meshInstance._localBounds;

        // box.GetCorners(corners)  (8 corners)
        RtVolume_Box_GetCorners(&box, corners);

        // For each of 8 corners: corner = transform * corner.Min(clampVec).
        const Math::Vector3 clampVec(1000000.0f, 1000000.0f, 2.5f);   // 0x49742400, 0x49742400, 0x40200000
        for (int i = 0; i < 8; i++) {
            corners[i] = corners[i].Min(clampVec);
            corners[i] = transform.operator*(corners[i]);
        }
        cornerCount = 8;
    } else {
        // Fallback: CheckPtr(param_2) ; FindComponent<MapComponent> (0x140004680)
        MapComponent* obj = (*param_2)->FindComponent<MapComponent>();
        if (obj) {
            int16_t f28 = obj->tileX0;  // startCol
            int16_t f2a = obj->tileY0;  // startRow
            int16_t f2c = obj->tileX1;  // width
            int16_t f2e = obj->tileY1;  // height
            // p4 = (f2c-1) + f28 ; p5 = (f2e-1) + f2a  (end col/row)
            int endCol = (static_cast<int>(f2c) - 1) + static_cast<int>(f28);
            int endRow = (static_cast<int>(f2e) - 1) + static_cast<int>(f2a);
            // ec70(endRow_combined, startCol=f28, startRow=f2a, endCol, endRow, corners)
            cornerCount = PlayerScreenRect_BuildCorners_ec70(
                static_cast<uint64_t>(static_cast<uint32_t>(endRow)),
                static_cast<int>(f28), static_cast<int>(f2a),
                endCol, endRow, corners);
        }
    }

    // FUN_1400fe960(?, cornerCount, corners, param_3)  -> fills the screen rectangle.
    return PlayerScreenRect_AccumulateRect_fe960(0, cornerCount, corners, param_3);
}
