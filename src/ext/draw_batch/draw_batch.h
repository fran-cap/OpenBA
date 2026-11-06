// ============================================================================
// src/ext/draw_batch/draw_batch.h -- ext.draw_batch (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/DRAWBATCH.md.
//
// Graphics::Renderer::DrawItemList (retail 0x180105240, batchc_gfx_leaves.cpp) issues one
// GPU draw per draw item, except the instancing batch. The terrain's decals and road meshes
// (WorldGeometry::Draw modes 0x2000 / 0x4000) and the smoke emitters (ParticleInstance::Draw)
// are hundreds of tiny dynamic draws per frame whose only per-item state is the vertex data.
// This extension replaces DrawItemList with a copy of retail's loop that merges a run of
// consecutive such items into one dynamic draw; every other item is drawn by the copy exactly
// as retail draws it. Same items, same order, same GPU input bytes (batch_fill.h).
//
// Selection: one null-by-default static, Graphics::Renderer::_extDrawItemList (Camera.h),
// set by Sync() at boot and on every Extensions-dialog toggle, never in a hot loop.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace DrawBatch {

// Installs or clears the engine seam to match Ext::Ext().draw_batch. Idempotent.
void Sync();
bool Installed();

// Issue #253 (stress harness only): the copy's instancing batches by shaderIndex (0 main,
// 1 shadow, 2 reflection, 3 any other), tallied in locals and folded once per DrawItemList
// call while counting is on (the harness turns it on at BANISHED_STRESS_COMPPROF >= 2).
// Merged decal / road / smoke runs are not counted here.
constexpr int kBatchShaderSlots = 4;
constexpr int kBatchSizeSlots   = 4;   // batches of 1, 2-8, 9-32, 33+ items
constexpr int kBatchBreakSlots  = 7;   // list end, cap, instance, _arg5, _userData, _arg10, wide seam
// Issue #253: the instancing draws the same lists would take at caps of 52, 104, 208 and 988 instances
// (wide batches, batch_fill.h ContinuesPastCut), whatever the backend's cap.
constexpr int kWideSimSlots     = 4;
struct BatchCounters {
    uint64_t lists     = 0;   // DrawItemList calls with items
    uint64_t singles   = 0;   // drawn items with _arg8 <= 1 (one draw each)
    uint64_t batches   = 0;   // instancing batches (_arg8 > 1)
    uint64_t instances = 0;   // items those batches drew
    uint64_t sizes[kBatchSizeSlots]   = {};
    uint64_t breaks[kBatchBreakSlots] = {};
    uint64_t wideSim[kWideSimSlots]   = {};
};
void SetCounting(bool on);
// Copies the window into out[kBatchShaderSlots] and zeroes it.
void TakeWindow(BatchCounters* out);

}  // namespace DrawBatch
}  // namespace Ext
