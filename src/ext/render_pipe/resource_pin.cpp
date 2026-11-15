// ============================================================================
// src/ext/render_pipe/resource_pin.cpp -- RENDERPIPE: the resource pin's engine glue
// (resource_pin.h)
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/android/P13_OVERLAP.md section 2.8).
//
// Graphics::EdgeMesh::_extBeforeDestroy is set while the extension runs its render
// thread (render_pipe.cpp Sync / Uninstall), null otherwise: under the faithful
// profile, with the row off and in the sequential split every ~EdgeMesh runs retail's
// body alone. Installed, a destroy of a mesh the packet in flight does not pin costs
// one locked test.
// ============================================================================
#include "ext/render_pipe/resource_pin.h"

#include <cstdio>

#include "graphics/Mesh.h"   // Graphics::EdgeMesh::_extBeforeDestroy

namespace Ext {
namespace RenderPipe {
namespace {

// How long a free waits for the reads at most (us): pass 2 of one packet is a few ms; a
// second means something the review missed, and the free then runs as before the pin.
constexpr int64_t kPinBoundUs = 1000000;

PinGate g_pinGate;
PinSet  g_slotPins[2];

void BeforeEdgeMeshDestroy(const Graphics::EdgeMesh* mesh) {
    g_pinGate.BeforeFree(mesh, kPinBoundUs);
}

}  // namespace

void InstallPin() {
    Graphics::EdgeMesh::_extBeforeDestroy = &BeforeEdgeMeshDestroy;
}

void RemovePin() {
    Graphics::EdgeMesh::_extBeforeDestroy = nullptr;
    g_pinGate.Publish(nullptr);
    for (PinSet& s : g_slotPins) s.Clear();
}

void PublishPins(bool overlap, int32_t slot) {
    g_pinGate.Publish(overlap ? &g_slotPins[slot & 1] : nullptr);
}

void PinReadsOver() {
    g_pinGate.ReadsOver();
}

PinSet& SlotPins(int32_t slot) {
    return g_slotPins[slot & 1];
}

PinStats TakePinStats() {
    const PinStats s = g_pinGate.TakeStats();
    // The bound was reached: the free ran beside the reads. Said once per window it happens in,
    // at most eight times a session (no hot-path output: a timeout is a second-long event).
    static int s_said = 0;
    if (s.timeouts != 0 && s_said < 8) {
        ++s_said;
        std::fprintf(stderr, "[RenderPipe] resource pin: %llu free(s) gave up waiting for the emit's pass 2 "
                             "after %lld us (max wait %llu us)\n",
                     static_cast<unsigned long long>(s.timeouts), static_cast<long long>(kPinBoundUs),
                     static_cast<unsigned long long>(s.maxUs));
        std::fflush(stderr);
    }
    return s;
}

}  // namespace RenderPipe
}  // namespace Ext
