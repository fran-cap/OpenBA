// ============================================================================
// src/ext/create_on_demand/prefetch.cpp -- NOT RETAIL. ext.create_on_demand's prefetches
// ============================================================================
// Class A (docs/extensions/handins/CREATEONDEMAND.md). Installed as Graphics::MeshInstance::
// _extMeshAssigned (Mesh.h has the seam's contract) while the row is on.
//
// A deferred object is made at its first use, on the thread that uses it (the render thread's
// bind): a building never seen before would make its buffers and textures inside the frame that
// first draws it. The world gives notice earlier: a mesh instance gets its mesh (SetMesh) when an
// entity is placed, loaded from a save, or changes its model, well before it is drawn. This queues
// the first-use creates of everything that instance draws with -- every detail level's four
// buffers and image, every material's textures -- for the backend's prefetch workers. Queueing an object that
// has no deferred create (already made, never deferred) is a lookup that finds nothing.
// ============================================================================

#include "ext/create_on_demand/prefetch.h"

#include "create_on_demand/create_on_demand_backend.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Particle.h"
#include "ui/SpriteSheet.h"
#include "video/Buffers.h"
#include "video/ImageBuffer.h"

namespace Ext { namespace CreateOnDemand {

namespace {

void QueueMaterial(const Graphics::MaterialInstance* material, bool keep) {
    if (material == nullptr) return;
    const RtGraphics::MaterialTexture* const textures = material->_textures.GetData();
    for (int32_t t = 0; t < material->_textures.GetSize(); ++t) {
        const Video::ImageBuffer* const image = textures[t].image;
        if (image == nullptr) continue;
        if (keep) {
            CreateOnDemandBackend::Keep(image->_data);
        } else {
            CreateOnDemandBackend::Prefetch(image->_data);
        }
    }
}

}  // namespace

// Every texture of one material. Also Graphics::MeshInstance::_extMaterialAssigned (a skin chosen
// after SetMesh).
void PrefetchMaterial(const Graphics::MaterialInstance* material) {
    QueueMaterial(material, false);
}

// WorldGeometry::_extMaterialTaken: the terrain's ground and water materials, its roads' and decals'
// (a building's footprint), taken while the world is built or loaded or a decal is added (the main
// menu's town drew ~16 of them, ~30 ms, in its first frame). KEPT: the terrain draws them with no
// gather to re-queue one the idle eviction gave back.
void KeepMaterial(const Graphics::MaterialInstance* material) {
    QueueMaterial(material, true);
}

void PrefetchMesh(const Graphics::MeshInstance& instance) {
    const Graphics::GraphicsMesh* const mesh = instance._mesh.Get();
    if (mesh != nullptr) {
        const Graphics::GraphicsMesh::DetailLevel* const levels = mesh->_detailLevels.GetData();
        for (int32_t i = 0; i < mesh->_detailLevels.GetSize(); ++i) {
            const Graphics::GraphicsMesh::DetailLevel& level = levels[i];
            CreateOnDemandBackend::Prefetch(level._vertexBuffer.platformData);
            CreateOnDemandBackend::Prefetch(level._indexBuffer.platformData);
            CreateOnDemandBackend::Prefetch(level._skinVertexBuffer.platformData);
            CreateOnDemandBackend::Prefetch(level._skinIndexBuffer.platformData);
            // The level's own image (its baked occlusion map), which the draw binds to slot 3 apart
            // from the materials (MeshInstance draw, BindMaterialImage).
            const Video::ImageBuffer* const image = level._image.Get();
            if (image != nullptr) CreateOnDemandBackend::Prefetch(image->_data);
        }
    }
    const Object::ExternalPtr<Graphics::MaterialInstance>* const materials = instance._materials.GetData();
    for (int32_t m = 0; m < instance._materials.GetSize(); ++m) {
        PrefetchMaterial(materials[m].Get());
    }
}

// Graphics::MeshInstance::_extLevelGathered: the gather (the view's, a shadow pass's) reaches every
// instance it draws a frame or more before the render thread binds it. An instance whose chosen level's
// vertex buffer and image are made is skipped on those two loads; otherwise its creates are queued: an
// instance first seen without a placement prefetch, or one the idle eviction gave back (a re-make; the
// image is tested too because a shadow pass binds the buffers alone, keeping them while the image
// goes). A use that comes before the worker runs the record itself, as before.
void PrefetchGathered(const Graphics::MeshInstance& instance, int32_t level) {
    const Graphics::GraphicsMesh* const mesh = instance._mesh.Get();
    const Graphics::GraphicsMesh::DetailLevel& drawn = mesh->_detailLevels.GetData()[level];
    // A skinned level keeps its vertices in the skin buffer; its plain one is empty, never made.
    const Video::VertexBuffer& vertices = drawn._vertexBuffer.size != 0 ? drawn._vertexBuffer : drawn._skinVertexBuffer;
    const Video::ImageBuffer* const image = drawn._image.Get();
    if (CreateOnDemandBackend::VertexBufferMade(vertices.platformData) &&
        (image == nullptr || CreateOnDemandBackend::ImageMade(image->_data)))
        return;
    PrefetchMesh(instance);
}

// Graphics::ParticleInstance::_extParticleAssigned: an emitter gets its particle when its entity is
// placed or loaded, well before it first emits (a chimney's smoke). KEPT: particles are drawn with no
// gather to re-queue one the idle eviction gave back (small textures, shared by every emitter).
void PrefetchParticle(const Graphics::Particle& particle) {
    KeepMaterial(particle._material);
}

// UI::SpriteSheet::_extLoaded. A window draws its sprite sheets all in the frame it opens: a
// sub-toolbar of a large mod set shows icons from ~80 sheets at once, ~27 ms of creates made on the
// render thread. Every sheet (the UI's, the fonts', the cursors') is queued as it loads instead, and
// KEPT: an idle eviction would only bring the same frame back the next time the window opens.
void KeepSpriteSheet(const UI::SpriteSheet& sheet) {
    QueueMaterial(sheet.m_materialInstance, true);
}

}}  // namespace Ext::CreateOnDemand
