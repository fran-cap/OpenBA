// ============================================================================
// src/ext/draw_gather/draw_gather.h -- ext.draw_gather (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/DRAWGATHER.md.
//
// The per-frame draw-item gather (Graphics::Scene::AddCulledDrawItems, retail
// 0x1801017a0) walks the scene grid and reads every candidate Geometry's flags,
// detail id and world box by pointer: the main thread's one real cache-miss spot
// (IBS_20261012 §2.3). This extension replaces that body, and Scene::Clean's
// (0x1801023f0), with versions that keep a dense per-node copy of the box
// (gather_side.h) and walk it instead, so a node the frustum rejects is never
// dereferenced; and prefetch ahead in Clean's node loop and in the AddDrawItems
// dispatch loop. Same visible list, same order, so the same draw items.
//
// Selection: the four null-by-default statics on Graphics::Scene (scene_ext.h),
// set by Sync() -- at boot and on every Extensions-dialog toggle -- never in a
// hot loop. Off: they are null, every side array is dropped.
// ============================================================================
#pragma once

#include <cstdint>

#include "ext/draw_gather/gather_side.h"

namespace Ext {
namespace DrawGather {

// Installs or clears the engine seams to match Ext::Ext().draw_gather. Idempotent.
// Off -> on starts from empty side arrays; on -> off drops them.
void Sync();
bool Installed();

// CULLPAR: the helper threads the gather walk runs on now (0 = the sequential walk).
int ParallelHelpers();

// The counters since the last call, zeroed (the stress report's window).
Counters TakeWindow();

}  // namespace DrawGather
}  // namespace Ext
