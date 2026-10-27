// src/game/Player/tool_base_ray.cpp
// Player tool terrain-raycast / mouse-ray / screen->cell / placement-position chain.
//
// These exe sub-helpers are resolved via ResolveAddr(Module::Exe, 0xADDR) from the
// per-tool Player .cpp files (placementtool.cpp etc.). They resolve to 0 (silently
// skipped) unless registered. This file provides faithful 1:1 reimplementations and a
// single registration point (RegisterPlayerRayBatch in standalone/reg_player_ray.cpp) so
// the shared call sites fire real logic in the standalone exe.
//
// Wave 5 (Player) round 2 — ray/screen-to-cell sub-batch. Each function is a 1:1 port
// of the decompiled exe helper; do not "improve" behavior.

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Camera.h"
#include <new>
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "math/Math.h"
#include "volume/Volume.h"
#include "Game/exe_bodies.h"
#include "Action/exe_bodies.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "input/Input.h"       // ENDGAME B.3.7 callers (K3)
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "Player/exe_bodies.h"
#include "Player/PlacementTool.h"
#include "Player/RotationSetting.h"
#include "Component/CreatePlacedDescription.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "Game/PlayerController.h"
#include "ext/registry.h"                     // Ext::Ext().touch_pointer (GetMouseRay's seam)
#include "ext/touch_pointer/touch_pointer.h"   // EXT SEAM (NOT RETAIL, docs/android/P11B_POINTER.md): ToolCursor

// ============================================================
// Locally-resolved dispatch-slot helpers (Runtime exports behind exe .rdata slots)
// ============================================================
// 1005a0 (GetMouseRay) calls four Math/Volume member functions through exe dispatch
// slots that are NOT registered project-wide. They are pure Runtime exports, so we
// resolve them locally by their mangled names (read from the exe's slot name table)
// and register them as dispatch slots in reg_player_ray.cpp. The bodies here resolve
// them via ResolveExport so they work whether or not the slot is filled.
//
// All four are member functions returning Math::Vector3 by value (12 bytes > 8 -> the
// MS x64 ABI uses a hidden return buffer: RCX=this, RDX=&retbuf, R8/XMM2.. = args).
//   ??0Vector3@Math@@QEAA@AEBVScalar@1@@Z   Vector3::Vector3(const Scalar&)   slot 0x140151320
//   ??DTransform3@Math@@QEBA?AVVector3@1@AEBV21@@Z  Transform3::operator*(Vector3)  slot 0x140152908
//   ?GetScreenDirection@Frustum@Volume@@QEBA?AVVector3@Math@@MM@Z  GetScreenDirection(float,float) slot 0x140152918
//   ?Rotate@Transform3@Math@@QEBA?AVVector3@2@AEBV32@@Z  Transform3::Rotate(Vector3)  slot 0x140151860
// GetScreenDirection is called by name: RtVolume_Frustum_GetScreenDirection
// (src/engine/volume/Volume.h), the retslot passed explicitly as the door did.


// CharacterComponent_impl_f3e0 (0x14009f3e0) = grid->world conversion:
// Reimpl_MapGetTerrainPos (Game/exe_bodies.h), called by name.

// ============================================================
// FUN_1400aa530 — QuarryComponent_impl_a530 (terrain DDA grid ray-march)
// ============================================================
// Marches a ray (origin param_2.xy, direction param_3.xy) across the terrain cell
// grid, testing each visited cell via QuarryComponent_helper_aa20 (a quad-vs-ray
// intersection against the terrain height field). Returns true (in the low byte) on
// the first cell whose helper reports a hit; on a hit, helper_aa20 fills param_5/param_6
// (and the position outputs). Returns false when the march leaves the grid or exceeds
// the max parametric distance (*param_4, normally 400.0).
//
// 1:1 port of the decompiled DDA. The original aliases stack slots through
// (slot - param_2) base-relative arithmetic; this port reasons about the logical
// "lane" (j=0 for X, j=1 for Y) each access targets and uses plain indexed locals.
//
// Stack lanes (RBP-relative in the original, here as named pairs):
//   normDir[2]  = Vector2::Normalize(param_3.xy)   (the ray direction; divisor)
//   center      = (width>>1) as float
//   tNear[2]    = parametric distance to the near grid boundary per axis
//   tFar[2]     = parametric distance to the far grid boundary per axis
//   cell[2]     = current integer cell (col,row), seeded at the origin cell
//   step[2]     = DDA step direction per axis (+1 / -1 / 0)
//   tMax[2]     = parametric distance to the next cell crossing per axis
//   tDelta[2]   = parametric distance between cell crossings per axis
// Caller signature: bool fn(longlong gameWorldMap, float* origin, Vector3* dir,
//                           float* maxDist, float* outA, float* outB)
//   (param_1 = *(g_pGameWorldSingleton+0x110) in the 101270 caller; +0xc0 = grid width.)
uint8_t WorldGeometry::TerrainMarch(float* param_2,
                                                float* param_3, float* param_4,
                                                float* param_5, float* param_6) {
    const float uvScale = kRdata_14017214c;  // DAT_14017214c (=1.0)

    int width = _gridSize;

    // normDir = normalize(param_3.x, param_3.y) — the 2D ray direction (DDA divisor).
    // 🔑 Math::Vector2 is 8 bytes (2 floats); MSVC x64 returns it in RAX, NOT via a
    // hidden return buffer (only structs >8 bytes use one). The dispatch-slot call
    // (retbuf,self) therefore mis-passed the args and normalized (0,0)->(0,0), leaving a
    // ZERO direction — the DDA then can't step (step=0), tests only the seed cell, and
    // never hits the terrain (the "no ghost / CreateBar stuck top-left" root cause).
    // Compute the unit direction inline: behaviour-identical, no ABI dependency.
    float srcDir[2] = { param_3[0], param_3[1] };
    float normDir[2] = { srcDir[0], srcDir[1] };
    {
        float len = std::sqrt(srcDir[0] * srcDir[0] + srcDir[1] * srcDir[1]);
        if (len > 1e-6f) { normDir[0] = srcDir[0] / len; normDir[1] = srcDir[1] / len; }
    }

    float center = static_cast<float>(width >> 1);
    float centerPos[2] = { center, center };      // local_res8[0],[1]
    // local_res8[2],[3] = center ^ 0x80000000 (DAT_1401722c0): the sign flip, -center.
    float centerNeg[2] = { -center, -center };

    // First slab pass: parametric distances to the +/- grid boundaries per axis.
    float tNear[2] = {0.0f, 0.0f};   // local_68[j]
    float tFarA[2] = {0.0f, 0.0f};   // local_res8[4+j]
    for (int j = 0; j < 2; j++) {
        float d = normDir[j];
        float origin = param_2[j];
        if (d > 0.0f) {
            tNear[j] = (centerPos[j] - origin) * (uvScale / d);
            tFarA[j] = (centerNeg[j] - origin) * (uvScale / d);
        } else if (d < 0.0f) {
            tNear[j] = (centerNeg[j] - origin) * (uvScale / d);
            tFarA[j] = (centerPos[j] - origin) * (uvScale / d);
        } else {
            tFarA[j] = 0.0f;
            tNear[j] = 0.0f;
        }
    }

    // Entry/exit slab test: tEnter = max(tFarA[0], tFarA[1]); tExit = min(tNear[0], tNear[1]).
    float tEnter = (tFarA[0] > tFarA[1]) ? tFarA[0] : tFarA[1];
    float tExit  = (tNear[1] > tNear[0]) ? tNear[0] : tNear[1];
    if (tEnter > tExit)
        return 0;

    // Seed the DDA at the origin cell.
    int cell[2];
    cell[0] = static_cast<int>(center + param_2[0]);
    cell[1] = static_cast<int>(center + param_2[1]);

    int   step[2];     // auStack_60[j]
    float tMax[2];     // local_res8[2+j]
    float tDelta[2];   // local_58[2+j]
    for (int j = 0; j < 2; j++) {
        float tFar = tFarA[j];
        float dlt = (tNear[j] - tFar) / static_cast<float>(width);
        tDelta[j] = dlt;
        float dir = normDir[j];
        if (dir > 0.0f) {
            step[j] = 1;
            tMax[j] = static_cast<float>(cell[j] + 1) * dlt + tFar;
        } else if (dir < 0.0f) {
            step[j] = -1;
            tMax[j] = static_cast<float>(width - cell[j]) * dlt + tFar;
        } else {
            // 0x49742400 = large "infinity" sentinel so this axis never crosses.
            tMax[j] = 1000000.0f;   // the bits 0x49742400
            step[j] = 0;
        }
    }

    // Main DDA march.
    for (;;) {
        int axis = (tMax[1] <= tMax[0]) ? 1 : 0;   // SETBE: S[3] <= S[2]
        if (cell[0] >= 0 && cell[0] < width && cell[1] >= 0 && cell[1] < width) {
            // 0x1400aaa20 QuarryComponent_helper_aa20 = Pf_CellQuadRay_aa20 (Action/exe_bodies.h);
            // it returns int, the door read AL as the bool.
            bool hit = static_cast<uint8_t>(CellQuadRay(cell[0], cell[1], param_3, param_2, param_4,
                param_5, param_6, nullptr)) != 0;
            if (hit) return 1;
        }
        // Advance along the chosen axis.
        float oldTMax = tMax[axis];
        cell[axis] += step[axis];
        tMax[axis] = oldTMax + tDelta[axis];
        if (oldTMax >= *param_4) {  // exceeded max parametric distance
            return 0;
        }
    }
}

// ============================================================
// FUN_1401005a0 — GetMouseRay (camera mouse ray: origin + direction)
// ============================================================
// Reads the mouse position from the Input::KeyMap, validates it against the display
// size, then builds the camera ray:
//   origin    (param_2) = cameraTransform * Vector3(0)        (camera world position)
//   direction (param_3) = cameraTransform.Rotate( Frustum.GetScreenDirection(u, v) )
// where u = mouseX / screenWidth, v = mouseY / screenHeight.
// Returns true when the mouse is inside the display, false otherwise.
//   KeyMap   = ***g_pInputConfigSingletonExref (triple-deref)
//   display  = *g_pVideoDeviceSingleton ; +4 = width, +8 = height
//   camera   = *(g_pGameWorldSingleton + 0xe0) ; +0x34 = Graphics::Camera,
//              +0x98..+0xc4 = 48-byte Transform3 (camera world matrix)
// Caller signature: bool fn(void* /*param_1, ignored*/, float* outOrigin, float* outDir)
extern "C" uint8_t PlayerRay_GetMouseRay_1005a0(const void* /*param_1*/, float* outOrigin, float* outDir) {
    if (!Input::Configuration::_singleton) return 0;

    // KeyMap = Input::Configuration::_singleton->_keyMap (retail's triple deref of
    // the import slot 0x140151738: slot -> &_singleton -> _singleton -> _keyMap).
    const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;
    const ::Video::Device* display = ::Video::Device::_singleton;
    PlayerController* controller =
        GameUpdateLoop::_singleton ? GameUpdateLoop::_singleton->m_playerController : nullptr;
    if (!keyMap || !display || !controller) return 0;

    short mouseX = keyMap->GetValue(0, 0);
    short mouseY = keyMap->GetValue(0, 1);
    // EXT SEAM (NOT RETAIL, docs/android/P11B_POINTER.md §5): while a finger presses a dialog, ext.touch_pointer keeps
    // the aiming tool's ray on the aim (the cursor is on the dialog for the click). Row off: retail's cursor.
    if (Ext::Ext().touch_pointer)
        Ext::TouchPointer::ToolCursor(mouseX, mouseY);

    int screenW = display->backBufferWidth;
    int screenH = display->backBufferHeight;

    // Safety: garbage/zero screen dims would let the bounds check pass with junk and then
    // crash in the camera-ray math (writing a by-value result through a bad retbuf). Reject
    // implausible display sizes so sub_da00 returns invalid instead of hard-faulting.
    if (screenW <= 0 || screenH <= 0 || screenW > 16384 || screenH > 16384)
        return 0;

    if (mouseX < 0 || mouseX >= screenW || mouseY < 0 || mouseY >= screenH)
        return 0;

    const Graphics::Camera* camera = &controller->camera;

    // Copy the 48-byte camera Transform3 (controller +0x98 = camera +0x64) onto the stack.
    Math::Transform3 xform = camera->_cameraToWorld;

    // Camera::GetFrustum returns a Frustum (0x64 bytes) by value.
    ::Frustum frustum(camera->GetFrustum());

    // origin = xform * Vector3(0,0,0).  (Vector3(Scalar=0) broadcasts 0 to all lanes.)
    Math::Vector3 zeroVec((Math::Scalar(0.0f)));
    {
        Math::Vector3 r = xform.operator*(zeroVec);
        outOrigin[0] = r.x; outOrigin[1] = r.y; outOrigin[2] = r.z;
    }

    // screenDir = Frustum.GetScreenDirection(mouseX/width, mouseY/height).
    float u = static_cast<float>(mouseX) / static_cast<float>(screenW);
    float v = static_cast<float>(mouseY) / static_cast<float>(screenH);
    Math::Vector3 screenDir;
    {
        Math::Vector3 dirOut;
        const Math::Vector3* r = RtVolume_Frustum_GetScreenDirection(&frustum, &dirOut, u, v);
        screenDir = *r;
    }

    // direction = xform.Rotate(screenDir).
    {
        Math::Vector3 r = xform.Rotate(screenDir);
        outDir[0] = r.x; outDir[1] = r.y; outDir[2] = r.z;
    }
    return 1;
}

// ============================================================
// FUN_140101270 — ScreenToCell (mouse ray -> terrain cell index)
// ============================================================
// Builds the mouse ray (GetMouseRay), marches it across the terrain (aa530), and
// converts the hit world position to a clamped cell index. Returns -1 when the mouse
// is off-screen or the ray hits nothing.
//   maxDist constant = DAT_14017223c = 400.0f.
//   map = GameUpdateLoop::m_game (+0xF8); map->size = width.
//   geometry = GameUpdateLoop::m_worldGeometry (+0x110) (aa530's param_1).
// param_1 is forwarded to GetMouseRay, which never reads it.
extern "C" int PlayerRay_ScreenToCell_101270(const void* param_1) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float dir[3]    = {0.0f, 0.0f, 0.0f};

    uint8_t ok = PlayerRay_GetMouseRay_1005a0(param_1, origin, dir);
    if (ok == 0)
        return -1;

    float maxDist = 400.0f;             // DAT_14017223c = 0x43c80000

    Map* map = GameUpdateLoop::_singleton->m_game;
    WorldGeometry* geometry = GameUpdateLoop::_singleton->m_worldGeometry;

    // local_18[4] in the original — aa530's helper aa20 writes 3 floats (world pos)
    // through this buffer (param_5 -> aa20 param_7), so it must hold at least 3.
    float hitPos[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // local_18 (out)
    float scratch[6] = {0,0,0,0,0,0};             // local_res10 (out, unused by caller)

    uint8_t hit = geometry->TerrainMarch(origin, dir, &maxDist, hitPos, scratch);


    if (hit == 0)
        return -1;

    int width = map->size;
    int col = static_cast<int>(hitPos[0] + static_cast<float>(width >> 1));
    int row = static_cast<int>(hitPos[1] + static_cast<float>(width >> 1));
    if (col < 0) col = 0;
    if (col >= width) col = width - 1;
    if (row < 0) row = 0;
    if (row >= width) row = width - 1;
    return width * row + col;
}

// ============================================================
// FUN_1400fda00 — PlacementTool::GetPlacement (placement footprint position calc)
// ============================================================
// Picks the terrain cell under the mouse (ScreenToCell), centres the building
// footprint (width/height from the tool's description) over it, validates the
// footprint is fully on the map, computes the world centre position (averaging the
// two diagonal corners via 0x14009f3e0 grid->world), and a yaw factor. Returns true
// when the placement is valid.
//   this+0x28 -> CreatePlacedDescription; +0x44 = footprint width, +0x48 = height.
//   this+0x48 -> RotationSetting; rotation 1 or 3 swaps W/H.
//   DAT_140172204 = 90.0f (yaw scalar), DAT_140172114 = 0.5f (cell centre offset).
// A member: every field it reads is the PlacementTool's (B4 PLAYER).
uint8_t PlacementTool::GetPlacement(int* param_2, int* param_3, int* param_4,
                                    int* param_5, Math::Vector3* param_6, float* param_7) {
    const float yawScalar  = 90.0f;  // DAT_140172204 = 0x42b40000
    const float halfCell   = 0.5f;   // DAT_140172114 = 0x3f000000

    uint32_t footW = static_cast<uint32_t>(descComponent->_i44);
    uint32_t footH = static_cast<uint32_t>(descComponent->_i48);

    // Orientation: rotation == 1 or 3 swaps width/height.
    uint32_t dimX = footW, dimY = footH;
    int orient = rotationIBP->rotation;
    if (orient == 1 || orient == 3) {
        dimX = footH;
        dimY = footW;
    }

    Map* grid = GameUpdateLoop::_singleton->m_game;
    int cellIdx = PlayerRay_ScreenToCell_101270(&rotationIBP);   // RCX = &this->+0x48 (never read)
    if (cellIdx < 0)
        return 0;

    int width = grid->size;
    int row = cellIdx / width;
    int col = cellIdx % width;

    // Centre the footprint over (col,row).
    if ((dimX & 1) == 0) {
        *param_2 = (col - (static_cast<int>(dimX) >> 1)) + 1;
        *param_4 = (static_cast<int>(dimX) >> 1) + col;
    } else {
        int h = static_cast<int>(dimX - 1) >> 1;
        *param_2 = col - h;
        *param_4 = h + col;
    }
    int dimYHalf;
    if ((dimY & 1) == 0) {
        dimYHalf = static_cast<int>(dimY) >> 1;
        *param_3 = (row - dimYHalf) + 1;
    } else {
        dimYHalf = static_cast<int>(dimY - 1) >> 1;
        *param_3 = row - dimYHalf;
    }
    *param_5 = dimYHalf + row;

    // Validate the footprint is fully on the map (and start col > 0).
    int startCol = *param_2;
    int w = grid->size;
    uint8_t valid;
    if (startCol < 0 || w <= startCol || *param_3 < 0 || w <= *param_3 ||
        startCol < 1 || w <= *param_4 || *param_5 < 0 || w <= *param_5) {
        valid = 0;
    } else {
        valid = 1;
    }

    // outYaw = orientation * 90.0f.
    *param_7 = static_cast<float>(rotationIBP->rotation) * yawScalar;

    if (valid != 0) {
        // Average the two diagonal corner world positions for the footprint centre.
        int gw = grid->size;
        int idxA = *param_3 * gw + *param_2;
        float centerOff = static_cast<float>(gw >> 1) - halfCell;
        float inGridA[3];
        inGridA[0] = static_cast<float>(idxA % gw) - centerOff;
        inGridA[1] = static_cast<float>(idxA / gw) - centerOff;
        inGridA[2] = 0.0f;
        float worldA[3] = {0.0f, 0.0f, 0.0f};
        grid->GetTerrainPos(worldA, inGridA, '\0', nullptr);

        gw = grid->size;
        int idxB = *param_5 * gw + *param_4;
        centerOff = static_cast<float>(gw >> 1) - halfCell;
        float inGridB[3];
        inGridB[0] = static_cast<float>(idxB % gw) - centerOff;
        inGridB[1] = static_cast<float>(idxB / gw) - centerOff;
        inGridB[2] = 0.0f;
        float worldB[3] = {0.0f, 0.0f, 0.0f};
        grid->GetTerrainPos(worldB, inGridB, '\0', nullptr);

        param_6->x = (worldB[0] + worldA[0]) * halfCell;
        param_6->y = (worldB[1] + worldA[1]) * halfCell;
        param_6->z = 0.0f;
    }
    return valid;
}
