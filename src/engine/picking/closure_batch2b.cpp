// ============================================================================
// src/engine/picking/closure_batch2b.cpp — cutover closure batch 2b
// ============================================================================
//
//   0x18007aef0  Picking::Scene::Scene(float) (unexported, 14 stores + one call)
//
// The shared grid initialiser `??0Scene@Picking@@QEAA@XZ` (0x1801258e0) calls
// this with a cell size of 16.0f; its port (`Scene::Scene()` in
// src/engine/picking/scene.cpp) delegates to it.
//
// This is that body. It writes through `Picking::Scene`'s named fields rather
// than raw offsets, so a layout change breaks the header's static_asserts.
//
// THE BRIEF CALLED THIS "a Frustum/Camera-shaped ctor" AND ASKED FOR A NEW TYPE
// HEADER. It is neither: `src/engine/picking/Scene.h` already pins every one
// of these fields, whole-object, from this very body (its "+0x2c and +0x44 are
// never written by the ctor" note is read off the same fourteen stores). No new
// header is needed and writing one would be a second, competing witness.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "picking/Scene.h"
#include "../collection/array_internal.h"

namespace {

// `DAT_18016169c` — the numerator of the reciprocal. 1.0f, the same constant
// scene.cpp's `kOne` names.
const float kOne = 1.0f;

}  // namespace

// ============================================================================
// Store order, exactly as retail writes it:
//
//   +0x00  _cellSize    = cellSize          (the argument, in XMM1)
//   +0x04  _invCellSize = 1.0f / cellSize   (a real DIVSS, not a folded
//                                            reciprocal — a caller passing 0
//                                            gets +inf here, faithfully)
//   +0x08  _cellHeads   {0, 0, 0}
//   +0x18  _cells       {0, 0, 0}
//   +0x28  _freeCell    = -1
//   +0x2c               NOT WRITTEN
//   +0x30  _links       {0, 0, 0}
//   +0x40  _freeLink    = -1
//   +0x44               NOT WRITTEN
//   +0x48  _entries     {0, 0, 0}
//   +0x58  _freeEntry   = -1
//   +0x5c  _queryStamp  = 0
//   then `_cellHeads` SetSize(10000) (0x18007b960) — which allocates the 10,000-entry bucket
//   table, so this function ALLOCATES and must not be called before the
//   MemoryAllocator is started.
//
// The two unwritten dwords are recorded, not zeroed: a `= {}` would be a
// behavioural difference from retail.
// ============================================================================
// B.4 wave Y: 0x18007aef0 is a CONSTRUCTOR — its EH frame (-2 at
// [RSP+0x20]) covers the four Array members, and its stores are the member
// initialisers in declaration order — so it is `Scene(float cellSize)`, and
// the exported default ctor 0x1801258e0 delegates to it with 16.0f
// (scene.cpp). The fourteen stores are the initialiser list below.
namespace Picking {

Scene::Scene(float cellSize)
    : _cellSize(cellSize), _invCellSize(kOne / cellSize),
      _cellHeads(), _cells(), _freeCell(-1),
      _links(), _freeLink(-1),
      _entries(), _freeEntry(-1),
      _queryStamp(0) {
    // ENDGAME B.3.2 step 2 (callers) — retail 0x18007b960, `Array<int32>::
    // SetSize(10000)` with every new slot filled -1 (both constants folded
    // into the body). NOT trait-equivalent (Collection.h's Array banner names
    // this exact row as one of the twelve a trait cannot express: a fill of
    // -1 is not what `SpecFor<int32_t>()` would leave the new tail holding),
    // so `_cellHeads` is bridged through the hand-derived row rather than
    // trusted to its own (default) spec.
    rtcoll::AsArray<&rtcoll::kSpec_I32Neg1>(&_cellHeads)->SetSize(10000);
}

}  // namespace Picking
