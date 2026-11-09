// ============================================================================
// src/ext/scene_draw.cpp -- the extensions' draws over the finished scene (see scene_draw.h)
// ============================================================================
// NOT RETAIL. docs/extensions/DOF.md.
// ============================================================================

#include "ext/scene_draw.h"

#include "ext/depth_of_field/depth_of_field.h"

namespace Ext {

SceneDrawFn SceneDraw() {
    return DepthOfField::SceneDraw();
}

}  // namespace Ext
