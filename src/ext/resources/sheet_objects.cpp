// ============================================================================
// src/ext/resources/sheet_objects.cpp -- the exe-side sheet build
// (docs/extensions/KITFREE.md §3, block RSCSPRITE). NOT RETAIL.
// ============================================================================
// Allocates the three objects through their own type factories (the same
// CreateNewObject the loader's factory record calls: ImageBuffer's ctor runs
// PlatformConstruct and takes its device data), resolves the base material
// through the ExternalFactory and creates the GPU texture. Needs the device and
// the factory: it is only reached from a provider, i.e. from a load.
// ============================================================================

#include "ext/resources/sheet_builder.h"

#include <cstdio>
#include <string>
#include <vector>

#include "graphics/Material.h"
#include "object/External.h"
#include "system/Filename.h"
#include "ui/SpriteSheet.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

namespace Ext {
namespace Resources {

namespace {

template <class T>
T* NewObject(const char* what) {
    Object::Base* b = T::CreateNewObject();
    if (b == nullptr) std::fprintf(stderr, "[ExtRes] sheet: %s::CreateNewObject failed\n", what);
    return static_cast<T*>(b);
}

// The textures BuildSheetObjects made, with what made them (RestoreSheetTextures). The spec's two names
// point into the record's own strings.
struct Kept {
    Video::ImageBuffer* texture = nullptr;
    SheetSpec           spec;
    std::string         imageName;
    std::string         materialName;
    SheetLayout         layout;
};
std::vector<Kept> g_kept;

void Keep(Video::ImageBuffer* texture, const SheetSpec& spec, const SheetLayout& layout) {
    Kept k;
    k.texture = texture;
    k.spec = spec;
    k.imageName = spec.imageName != nullptr ? spec.imageName : "";
    k.materialName = spec.materialName != nullptr ? spec.materialName : "";
    k.layout = layout;
    for (Kept& old : g_kept) {
        if (old.texture == texture) {   // an address reused by a later build: the later one is kept
            old = std::move(k);
            return;
        }
    }
    g_kept.push_back(std::move(k));
}

void Forget(const Video::ImageBuffer* texture) {
    for (size_t i = 0; i < g_kept.size(); ++i) {
        if (g_kept[i].texture == texture) {
            g_kept.erase(g_kept.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

}  // namespace

void RestoreSheetTextures() {
    Object::ExternalFactory* const factory = Object::ExternalFactory::_singleton;
    if (factory == nullptr || g_kept.empty()) return;
    int restored = 0;
    int failed = 0;
    for (Video::ImageBuffer* t = factory->FindNextAs<Video::ImageBuffer>(Video::ImageBuffer::TypeId(), nullptr);
         t != nullptr; t = factory->FindNextAs<Video::ImageBuffer>(Video::ImageBuffer::TypeId(), t)) {
        if (t->_width != 0) continue;   // reloaded from a file, or not one of ours
        for (Kept& k : g_kept) {
            if (k.texture != t) continue;
            k.spec.imageName = k.imageName.c_str();
            k.spec.materialName = k.materialName.c_str();
            // The reload left the object as its constructor did, with its path: the fields again, then the
            // texture (Create needs the levels; FillImageBuffer alone makes nothing on the device).
            const System::Filename path = t->_path;
            FillImageBuffer(*t, k.spec, k.layout);
            t->_path = path;
            if (CreateTexture(*t, k.spec, k.layout)) {
                ++restored;
            } else {
                ++failed;
            }
            break;
        }
    }
    std::fprintf(stderr, "[ExtRes] video device restart: %d provided texture(s) made again, %d failed (%d kept)\n",
                 restored, failed, static_cast<int>(g_kept.size()));
}

void DestroySheetObjects(SheetObjects& o) {
    Forget(o.texture);
    // Reverse build order; the virtual destructors and the classes' own
    // operator delete (MemFree) take each one back.
    delete o.sheet;
    delete o.material;
    delete o.texture;
    o = SheetObjects();
}

SheetObjects BuildSheetObjects(const SheetSpec& spec, const SheetBitmap* bitmaps, int count,
                               const char* baseMaterialPath, const char* textureSlot) {
    SheetObjects o;
    Object::ExternalFactory* const factory = Object::ExternalFactory::_singleton;
    if (factory == nullptr || Video::Device::_singleton == nullptr) {
        std::fprintf(stderr, "[ExtRes] sheet: no ExternalFactory / Device yet, cannot build\n");
        return o;
    }

    SheetLayout layout;
    if (!PackSheet(spec, bitmaps, count, layout)) {
        std::fprintf(stderr, "[ExtRes] sheet: %d bitmap(s) do not fit a %d x %d sheet\n",
                     count, spec.sheetWidth, spec.sheetHeight);
        return o;
    }

    Graphics::Material* base =
        factory->FindOrLoadAs<Graphics::Material>(System::Filename(baseMaterialPath));
    if (!Object::IsDerivedFromNullSafe(base, Graphics::Material::TypeId())) {
        std::fprintf(stderr, "[ExtRes] sheet: base material %s did not load (%p)\n",
                     baseMaterialPath, static_cast<const void*>(base));
        return o;
    }

    o.texture  = NewObject<Video::ImageBuffer>("Video::ImageBuffer");
    o.material = NewObject<Graphics::MaterialInstance>("Graphics::MaterialInstance");
    o.sheet    = NewObject<UI::SpriteSheet>("UI::SpriteSheet");
    if (o.texture == nullptr || o.material == nullptr || o.sheet == nullptr) {
        DestroySheetObjects(o);
        return o;
    }

    FillImageBuffer(*o.texture, spec, layout);
    if (!CreateTexture(*o.texture, spec, layout)) {
        std::fprintf(stderr, "[ExtRes] sheet: ImageBuffer::Create(%d x %d, format %d) failed\n",
                     layout.width, layout.height, spec.imageFormat);
        DestroySheetObjects(o);
        return o;
    }
    Keep(o.texture, spec, layout);
    FillMaterialInstance(*o.material, base, textureSlot, o.texture);
    FillSpriteSheet(*o.sheet, spec, bitmaps, count, layout, o.material);
    return o;
}

}  // namespace Resources
}  // namespace Ext
