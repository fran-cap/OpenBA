// ============================================================================
// src/ext/staggered_passes/staggered_passes.cpp -- NOT RETAIL. ext.staggered_passes
// (staggered_passes.h has the contract)
// ============================================================================

#include "ext/staggered_passes/staggered_passes.h"

#include "ext/registry.h"

#include "graphics/GeneratedImageBuffer.h"   // Graphics::ReflectionImageBuffer::_extStaggerEvery
#include "graphics/SunLight.h"               // Graphics::SunLight::_extStaggerEvery
#include "graphics/stagger_hold.h"           // Graphics::StaggerHold::Invalidate

namespace Ext {
namespace StaggeredPasses {

void Sync() {
    const bool on = Ext().staggered_passes;
    ::Graphics::SunLight::_extStaggerEvery = on ? &Val().staggered_passes_shadow : nullptr;
    ::Graphics::ReflectionImageBuffer::_extStaggerEvery = on ? &Val().staggered_passes_reflection : nullptr;
    ::Graphics::StaggerHold::Invalidate();
}

}  // namespace StaggeredPasses
}  // namespace Ext
