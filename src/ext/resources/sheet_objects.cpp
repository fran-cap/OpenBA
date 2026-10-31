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

}  // namespace

void DestroySheetObjects(SheetObjects& o) {
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
    FillMaterialInstance(*o.material, base, textureSlot, o.texture);
    FillSpriteSheet(*o.sheet, spec, bitmaps, count, layout, o.material);
    return o;
}

}  // namespace Resources
}  // namespace Ext
