// src/game/Component/statusicon_geometry.cpp
// Phase B — standalone billboard Geometry draw path for StatusIconComponent.
//
// The StatusIconComponent embeds a Graphics::Geometry sub-object at component+0x28
// whose vtable in the original exe is the billboard subclass PTR_FUN_14015f0c0 (12
// slots). The two draw slots live in the (unmapped-in-standalone) exe:
//   slot 2 (+0x10) Draw(geometry, Context*, camera, ?, lerpT)   = 0x1400a9c10
//   slot 3 (+0x18) AddDrawItems(geometry, scene, renderer)      = 0x1400a9b90
// This file provides faithful C++ ports of both, and — since ENDGAME B.3.8 VIS —
// the billboard as a REAL `Graphics::Geometry` subclass (StatusIconGeometry,
// StatusIconComponent.h): the compiler's 9-slot table inherits Geometry's slots
// 4-8 and overrides 0-3. Before, this file COPIED the minted
// `??_7Geometry@Graphics@@` table into a 16-entry array and patched slots 2/3
// (one of the six B38 D5 slot-copy sites).
//
// Ported faithfully from Application-x64.exe (Ghidra), verified against the disassembly.

// video/Context.h FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once
// <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
// NOTE: the Geometry SUBCLASS in this file is an EXE-PHASE class, a real
// subclass of the Runtime's Graphics::Geometry since ENDGAME B.3.8 VIS.
#include "video/Context.h"
#include "graphics/Geometry.h"   // ENDGAME B.3.5 batch K1 — same include-order reason
#include "graphics/Material.h"
#include "graphics/Camera.h"     // Graphics::Renderer
#include "graphics/InterpolatedState.h"   // ENDGAME B.3.5 closing pass --
                                          // Graphics::InterpolatedState<Transform>
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/External.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "math/Math.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K4)
#include "Component/StatusIconComponent.h"
#include "object/Internal.h"   // Object::detail::InternalMemFree

// ---- .rdata float constants (NULL via ResolveAddr in standalone → use literals) ----
//   g_fUVScale       0x14017214c = 1.0f
//   _DAT_1401720c4   0x1401720c4 = 0.01f
//   DAT_140172114    0x140172114 = 0.5f
static const float kUVScale   = 1.0f;
static const float kScale01   = 0.01f;
static const float kHalf      = 0.5f;

// ---- Resolved Runtime engine functions ----
// cc!=0 camera-constant-screen-size projection uses the Math:: classes directly
// (Transform3::Inverse / AsMatrix, Matrix::operator*(Matrix|Vector4)).



// 32-byte billboard vertex: pos(12) + color(4) + uv(8) + pad(8). Layout matches the
// original's stack-built vertex (BeginIndexedVertices stride = 0x20).
struct SIVertex {
    float    x, y, z;
    uint32_t color;
    float    u, v;
    float    pad0, pad1;
};
static_assert(sizeof(SIVertex) == 32, "SIVertex must be 32 bytes");



// ============================================================
// StatusIconGeometry — the billboard as a real Graphics::Geometry subclass
// ============================================================

// Inlined in StatusIconComponent's ctor 0x14012c0c0: `??0Geometry@Graphics@@`
// (Geometry() — the vptr stores are the compiler's), then +0xc0 (qword) =
// +0xc8 = +0xcc (byte) = +0xd0 = 0.
StatusIconGeometry::StatusIconGeometry()
    : iconSize(0.0f), spriteCount(0), activeMask(0), constantSize(0), _padCD{},
      spriteSheet(nullptr) {}

// [1] 0x1400aa430 — `RET`.
void StatusIconGeometry::Update(const System::Time&) {}

// ============================================================
// slot 2 — Draw  (port of FUN_1400a9c10)
// ============================================================
// Emits a camera-facing billboard quad per active sprite, stacked vertically in the
// geometry's local frame. param order matches the original vtable Draw:
//   (geometry, Context*, camera, p4, lerpT)
void StatusIconGeometry::Draw(Video::Context& context, const Graphics::Camera& cameraRef,
                              Graphics::ExternalPtrHandle /*material*/, float t,
                              int, int, int) const {
    // 1) Model transform = Lerp(state, t) -> AsMatrix -> Context::SetTransform(0, M).
    //    Keep the Lerp'd Transform around: its position (+0x10) is the billboard origin
    //    used by the cc!=0 camera-constant-screen-size projection below.
    // ?Lerp@?$InterpolatedState@VTransform@Math@@@Graphics@@QEBA?AVTransform@Math@@
    // AEBVScalar@4@@Z (0x18001aff0) on Geometry's +0x84 InterpolatedState<Transform>.
    // A hidden-retslot member returning by value in C++, so `xf` is constructed ONCE,
    // directly from the prvalue, in the same storage the retbuf occupied.
    const Math::Transform xf =        // .translation is the billboard origin
        _transform.Lerp(Math::Scalar(t));
    {
        Math::Matrix mat = xf.AsMatrix();
        context.SetTransform(0, mat);
    }

    // 2) Color = (1,1,1, lerp(geom+0x80, geom+0x7c, t)). geom+0x7c/+0x80 hold the
    //    settings-derived scale written by OnRemove (used here as the alpha).
    float alpha = (kUVScale - t) * _opacityPrev
                + t           * _opacity;
    const Math::Vector4 color4(1.0f, 1.0f, 1.0f, alpha);
    context.SetColor(color4);

    // 3) Count active sprites; begin the indexed-vertex batch (n quads).
    int n = 0;
    for (int i = 0; i < spriteCount && i < 32; ++i)
        if (activeMask & (1u << i)) ++n;

    if (!context.BeginIndexedVertices(n * 4, 0x20, n * 6)) {
        return;
    }

    // 4) Camera basis (the camera's Transform3 at +0x64): axisX = right, axisZ = up.
    const Math::Transform3& cam = cameraRef._cameraToWorld;
    float rightU[3] = { cam.axisX.x, cam.axisX.y, cam.axisX.z };   // row0
    float upU[3]    = { cam.axisZ.x, cam.axisZ.y, cam.axisZ.z };   // row2

    // 5) Scale. fVar9 = iconSize. When cc!=0 the icon keeps a CONSTANT SCREEN SIZE:
    //    fVar9 /= |ndcB.x - ndcA.x|, where ndcA/ndcB are the NDC-x of the icon origin P
    //    and P+cameraRight, projected through (inverse-view * projection). Faithful port
    //    of the FUN_1400a9c10 cc!=0 block (AsVector3/Vector ctors inlined).
    float    fVar9 = iconSize;
    uint8_t  cc    = constantSize;
    if (cc != 0) {
        const Math::Matrix& proj = cameraRef._projection;   // 4x4 projection
        Math::Transform3 invView = cameraRef._cameraToWorld.Inverse();
        Math::Matrix invMat = invView.AsMatrix();                        // invView as Matrix
        Math::Matrix vpMat  = invMat * proj;                             // vp = invView * proj

        float P[3] = { xf.translation.x, xf.translation.y, xf.translation.z };   // billboard origin (Transform pos)
        Math::Vector4 v4a(P[0],             P[1],             P[2],             1.0f);
        Math::Vector4 v4b(P[0] + rightU[0], P[1] + rightU[1], P[2] + rightU[2], 1.0f);
        Math::Vector4 clipB = vpMat * v4b;
        Math::Vector4 clipA = vpMat * v4a;
        float wa = clipA.w, wb = clipB.w;
        if (wa != 0.0f && wb != 0.0f) {
            float d = (clipB.x / wb) - (clipA.x / wa);
            float ad = d < 0.0f ? -d : d;
            if (ad > 1e-9f) fVar9 = fVar9 / ad;
        }
    }

    GameSettings* settings = g_gameSettings;
    int32_t scaleSet  = settings ? settings->statusIconScale : 0; // settings+0x10
    fVar9 = fVar9 * static_cast<float>(scaleSet) * kScale01;
    float halfsize = fVar9 * kHalf;

    float right[3]   = { rightU[0] * halfsize, rightU[1] * halfsize, rightU[2] * halfsize };
    float up[3]      = { upU[0]    * halfsize, upU[1]    * halfsize, upU[2]    * halfsize };
    float advance[3] = { upU[0]    * fVar9,    upU[1]    * fVar9,    upU[2]    * fVar9 };
    float center[3]  = { 0.0f, 0.0f, halfsize };

    const UI::SpriteSheet* sheet = spriteSheet;

    // index pointer lives at ctx+0x20f0 (a short* that advances as we emit indices).
    int   emitted = 0;
    short base    = 0;
    for (int i = 0; i < spriteCount && i < 32; ++i) {
        if (!((activeMask >> i) & 1u)) continue;

        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (sheet) sheet->GetUVs(i, u0, v0, u1, v1);

        SIVertex vtx[4];
        std::memset(vtx, 0, sizeof(vtx));
        // v0 = center - right + up   uv(u0,v0)
        vtx[0].x = center[0] - right[0] + up[0];
        vtx[0].y = center[1] - right[1] + up[1];
        vtx[0].z = center[2] - right[2] + up[2];
        vtx[0].color = System::kColorWhite; vtx[0].u = u0; vtx[0].v = v0;
        // v1 = center + right + up   uv(u1,v0)
        vtx[1].x = center[0] + right[0] + up[0];
        vtx[1].y = center[1] + right[1] + up[1];
        vtx[1].z = center[2] + right[2] + up[2];
        vtx[1].color = System::kColorWhite; vtx[1].u = u1; vtx[1].v = v0;
        // v2 = center + right - up   uv(u1,v1)
        vtx[2].x = center[0] + right[0] - up[0];
        vtx[2].y = center[1] + right[1] - up[1];
        vtx[2].z = center[2] + right[2] - up[2];
        vtx[2].color = System::kColorWhite; vtx[2].u = u1; vtx[2].v = v1;
        // v3 = center - right - up   uv(u0,v1)
        vtx[3].x = center[0] - right[0] - up[0];
        vtx[3].y = center[1] - right[1] - up[1];
        vtx[3].z = center[2] - right[2] - up[2];
        vtx[3].color = System::kColorWhite; vtx[3].u = u0; vtx[3].v = v1;

        context.AddVertices(4, vtx);

        // 6 indices: base, base+1, base+2, base, base+2, base+3.
        // Video::Context +0x20f0, the dynamic index cursor (16-bit indices).
        uint16_t* ip = context._dynamicIndexCursor;
        ip[0] = base;
        ip[1] = static_cast<short>(base + 1);
        ip[2] = static_cast<short>(base + 2);
        ip[3] = base;
        ip[4] = static_cast<short>(base + 2);
        ip[5] = static_cast<short>(base + 3);
        context._dynamicIndexCursor = ip + 6;

        base = static_cast<short>(base + 4);
        center[0] += advance[0];
        center[1] += advance[1];
        center[2] += advance[2];
        ++emitted;
    }

    context.EndIndexedVertices(0);


}

// ============================================================
// slot 3 — AddDrawItems  (port of FUN_1400a9b90)
// ============================================================
// Registers the billboard with the renderer if it has any active sprite bits AND
// the game-settings icon-visibility byte (settings+0xc) is set.
void StatusIconGeometry::AddDrawItems(const Graphics::Scene& /*scene*/,
                                      Graphics::Renderer& rendererRef) const {
    if (static_cast<int32_t>(activeMask) == 0) return;

    GameSettings* settings = g_gameSettings;
    if (!settings || settings->statusIconEnable == 0) return;

    const UI::SpriteSheet* sheet = spriteSheet;
    // UI::SpriteSheet +0x30 is its ExternalPtr<MaterialInstance>.
    const Graphics::MaterialInstance* material =
        sheet ? sheet->m_field30 : nullptr;
    if (material) {
        // AddDrawItem(geom, material, 0x8000, 0, 0.0, uvScale, 1, null, 0)
        rendererRef.AddDrawItem(this, material, 0x8000, 0, 0.0f, kUVScale, 1, nullptr, 0);
    }
}
