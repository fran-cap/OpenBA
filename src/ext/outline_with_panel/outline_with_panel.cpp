// ============================================================================
// src/ext/outline_with_panel/outline_with_panel.cpp -- fix.outline_with_panel
// ============================================================================
// The rules are in outline_with_panel.h; the record is hold_set.h.
// ============================================================================

#include "ext/outline_with_panel/outline_with_panel.h"
#include "ext/outline_with_panel/hold_set.h"
#include "ext/registry.h"

#include "graphics/Mesh.h"   // Graphics::EdgeMeshInstance::_extHoldDraw

#include <mutex>

namespace Ext {
namespace OutlineWithPanel {

namespace {

// A frame shows a handful of outlines (the selection, a building's parts); past this the record is
// not trusted and nothing is held.
constexpr int32_t kMaxOutlines = 64;

std::mutex g_lock;
HoldSet<kMaxOutlines> g_set;

}  // namespace

void AfterToolUpdate(bool dialogPending) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_set.BeginFrame(dialogPending && Fix().outline_with_panel);
}

bool HoldDraw(const Graphics::EdgeMeshInstance* outline) {
    std::lock_guard<std::mutex> guard(g_lock);
    return g_set.Gathered(outline);
}

void Sync() {
    Graphics::EdgeMeshInstance::_extHoldDraw = CurrentProfile() == Profile::Faithful ? nullptr : &HoldDraw;
}

}  // namespace OutlineWithPanel
}  // namespace Ext
