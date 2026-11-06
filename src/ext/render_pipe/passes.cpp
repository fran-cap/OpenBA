// ============================================================================
// src/ext/render_pipe/passes.cpp -- RENDERPIPE: Renderer::Draw's two loops, split
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/RENDERPIPE0.md section 2).
//
// A COPY of `Graphics::Renderer::Draw`'s two loops (wave27_lane_gfxbig.cpp,
// retail 0x180104bb0), cut where pass 1 ends, the way ext.draw_gather copies the
// cull walk. The faithful `Renderer::Draw` is untouched and still draws every
// unpipelined frame. Every statement below is the faithful body's, in its
// order, with three deliberate differences (THREADRESEARCH section 1.5):
//
//   fix 2  pass 1 latches the MAIN-OWNED frame index it is handed instead of
//          `context._frameIndex` (which, in the pipelined order, has not yet
//          been incremented by the previous frame's emit);
//   fix 6  the SkinMemo scope opens around pass 2 only (it is pass 2's memo);
//   --     pass 1 does not bump rtgfxbig::g_drawGroups / g_drawSorts (the
//          [GFXBIG27] summary counts the faithful Renderer::Draw only).
//
// Pass 1 reads no other Context state than the default render buffer pointer.
// Pass 2 also carries the ext.ambient_occlusion seam (AOCORE): the packet's frame
// record stands in for Graphics::Renderer::_extAo, which only the faithful
// Renderer::Draw reads.
// ============================================================================
#include "ext/render_pipe/render_pipe_internal.h"

#include <cstdint>

#include "graphics/Camera.h"
#include "graphics/Geometry.h"
#include "graphics/Light.h"
#include "graphics/Scene.h"
#include "graphics/pass_split.h"   // issue #253: the stress harness's per-group CPU split
#include "graphics/skin_memo.h"
#include "graphics/SunLight.h"   // ext.staggered_passes: SunLight::ExtShadowHoldEmit
#include "video/Context.h"
#include "video/Device.h"   // Video::RenderBuffer::width / height

namespace Ext {
namespace RenderPipe {

using Graphics::Renderer;
using Graphics::RendererDrawItem;
using Graphics::RendererDrawable;
using Graphics::RendererSceneEntry;

void Pass1(Renderer& r, const Video::RenderBuffer* defaultRenderBuffer, float alpha,
           int32_t frameIndex, const Graphics::Scene& sceneRef, const Graphics::Camera& cameraRef) {
    r.Graphics::Renderer::Reset();

    r._alphaT     = alpha;
    r._frameIndex = frameIndex;   // fix 2 (faithful: context._frameIndex)

    r.Graphics::Renderer::AddScene(&sceneRef, defaultRenderBuffer, cameraRef, r._shaderDetail,
                                   0x40, 0, 0x8007u);

    r._currentSceneGroup = 0;
    if (r._sceneGroups._size > 0) {
        do {
            RendererSceneEntry* const e = r._sceneGroups._data + r._currentSceneGroup;
            const int     splitSlot = PassSplit::P1Slot(e->_shader);   // issue #253 (counters only)
            const int64_t splitT0   = PassSplit::Begin();
            PassSplit::g_p1Group = splitT0 != 0 ? splitSlot : -1;

            r._shaderDetail = e->_shaderDetail;
            r._detailId     = e->_detailId;

            const int32_t nextGroup = r.Graphics::Renderer::BeginDrawGroup(
                e->_renderBuffer, e->_camera, e->_camera, e->_shader, e->_materialTypeFlags);

            e->_drawGroupIndex = r._currentDrawGroup;

            const uint32_t passMask = r._drawGroups._data[r._currentDrawGroup]._materialTypeFlags;

            for (const Collection::LinkedList<Graphics::Geometry*>::Node* node = sceneRef._globals._head;
                 node != nullptr; node = node->next) {
                const Graphics::Geometry* const obj = node->element;
                if ((obj->_materialTypeFlags & passMask) != 0 && obj->_detailId <= r._detailId)
                    obj->AddDrawItems(sceneRef, r);
            }

            if (r._detailId != 0)
                sceneRef.Graphics::Scene::AddCulledDrawItems(r);

            for (int32_t k = 0; k < 3; ++k) {
                Collection::Array<RendererDrawItem>& list =
                    r._drawGroups._data[r._currentDrawGroup]._items[k];
                if (list._size > 0 && list._size > 1) {
                    uint64_t scratch[4];
                    rtgfxbig::RtGraphics_Renderer_DrawItemSort(list._data, list._size, scratch);
                }
            }

            PassSplit::End(splitSlot, splitT0);   // issue #253
            PassSplit::g_p1Group = -1;

            ++r._currentSceneGroup;
            r._currentDrawGroup = nextGroup;
        } while (r._currentSceneGroup < r._sceneGroups._size);
    }

    // CULLPAR (ext.gather_threads): pass 1 is over, as at the same point of the faithful
    // Renderer::Draw; the gather helpers park (graphics/Scene.h).
    if (Graphics::Scene::_extDrawPass1End != nullptr) Graphics::Scene::_extDrawPass1End();
}

void Pass2(Renderer& r, Video::Context& context, const Graphics::ExtAoFrame* ao,
           void (*ground)(Video::Context&, const Graphics::Camera&), bool shadowHeld) {
    const SkinMemo::Scope skinMemoScope;   // fix 6: pass 2's memo, opened around pass 2 only
    // ext.staggered_passes: the packet's "pass 1 held the sun map", for DrawShadowMap on this thread.
    Graphics::SunLight::ExtShadowHoldEmit(shadowHeld);

    for (int64_t si = static_cast<int32_t>(r._sceneGroups._size) - 1; si >= 0; --si) {
        RendererSceneEntry* const e = r._sceneGroups._data + si;

        r._shaderDetail = e->_shaderDetail;
        context._dirty |= 0xc0;
        context._shaderDetail = e->_shaderDetail;

        const int64_t shadowT0 = PassSplit::Begin();   // issue #253 (counters only)
        for (int32_t j = 0; j < e->_drawableIds._size; ++j) {
            const int32_t id = e->_drawableIds._data[j];
            RendererDrawable& d = r._drawables._data[id];
            d._object->DrawShadowMap(context, r, d._value, 1);
        }
        PassSplit::End(PassSplit::kP2Shadow, shadowT0);

        context.SetRenderBuffer(e->_renderBuffer);

        {
            const Video::RenderBuffer* const rb = e->_renderBuffer;
            Video::Viewport vp;
            vp.x = 0;
            vp.y = 0;
            vp.width  = rb->width;
            vp.height = rb->height;
            context.SetViewport(vp);
        }
        {
            const Video::RenderBuffer* const rb = e->_renderBuffer;
            const int32_t w = rb->width, h = rb->height;
            if (!(context._scissor.x == 0 && context._scissor.y == 0 &&
                  context._scissor.width == w && context._scissor.height == h)) {
                context._dirty |= 0x400;
                context._scissor.x = 0;
                context._scissor.y = 0;
                context._scissor.width = w;
                context._scissor.height = h;
            }
        }

        context.Clear(6, 0x1184d6u, 1.0f, 0);

        const int64_t lightT0 = PassSplit::Begin();   // issue #253 (counters only)
        for (int32_t k = 0; k < 3; ++k) {
            for (int32_t j = 0; j < e->_drawableIds._size; ++j) {
                const int32_t id = e->_drawableIds._data[j];
                RendererDrawable& d = r._drawables._data[id];
                d._object->DrawLight(context, r, e->_drawGroupIndex, d._value, k, e->_shader);
            }
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOCORE.md): ext.ambient_occlusion after the opaque light pass; the faithful loop's test
            // (wave27_lane_gfxbig.cpp), with the packet's record instead of the static.
            if (k == 0 && ao != nullptr && e->_shader == 0 &&
                e->_renderBuffer == context._defaultRenderBuffer)
                Graphics::ExtAoAfterOpaque(context, *ao, *e);
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): the ground draw, the faithful loop's
            // test, with the packet's pointer instead of the static.
            if (k == 0 && ground != nullptr && e->_shader == 0 &&
                e->_renderBuffer == context._defaultRenderBuffer)
                ground(context, e->_camera);
        }
        PassSplit::End(PassSplit::P2Slot(e->_shader), lightT0);
    }
    Graphics::SunLight::ExtShadowHoldEmit(false);
}

}  // namespace RenderPipe
}  // namespace Ext
