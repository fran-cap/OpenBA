#pragma once
// ============================================================================
// src/ext/map_overlays/overlay_render.h -- ext.map_overlays: the renderers' device seams
// ============================================================================
// NOT RETAIL. The run-time video object of overlay_render.cpp (the map window's ImageBuffer) across a video
// device restart; called from src/ext/registry_boot.cpp's MainLoop::_extVideoLost / _extVideoRestored
// hooks. Both do nothing while the image was never made (the row off, the faithful profile). Main thread.
// ============================================================================

namespace Ext {
namespace MapOverlays {

void OnVideoLost();       // the image's device companion released (Minimap: GameUpdateLoop::OnVideoError)
void OnVideoRestored();   // made again and refilled from the kept pixels (Minimap::OnVideoRestart)

}  // namespace MapOverlays
}  // namespace Ext
