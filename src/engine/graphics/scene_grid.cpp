// ============================================================================
// src/engine/graphics/scene_grid.cpp — WAVE 9 LANE GFX3
// Graphics::Scene's quadtree grid: the five by-address cutover doors and their
// transitive closure.
// ============================================================================
//
// WHAT THIS FILE CLOSES
// ---------------------
// docs/history/runtime_port/WAVE9_LANE_GFX2.md §7 listed seven unregistered
// `ResolveAddr(Module::Runtime, …)` doors in src/engine/graphics/scene.cpp and
// established the verdict this lane builds on rather than re-derives:
//
//   * NONE of them is exported, so no name-based instrument — the census, the
//     residue list, unsatisfied_export_scan — can see them. The call site never
//     mentions a name at all.
//   * EVERY one has a live, non-diff-guarded caller in a PORTED body in that
//     file. With the DLL absent each is a call through null on the first scene
//     built or the first frame drawn.
//
// Two of the seven were already closed by other lanes: `0x18001f470` is lane
// PICK's `BoxTransform` and `0x180009c30` is lane COLL's
// `RtColl_FreeLocked_180009c30` (src/engine/collection/linkedlist.cpp). That
// second one is the grep-before-you-write win of this lane: it was on my list
// as "trivially the MemFree static", and a second registration of it would have
// been last-binding-wins and silently made one of the two bodies dead code.
//
// The remaining FIVE are this file:
//
//   0x18005e4a0  GridInit      Scene::Scene         (scene.cpp:340)
//   0x18005e5e0  GridAdd       Scene::AddObject     (scene.cpp:352)
//   0x18005f090  GridUnthread  Scene::RemoveObject  (scene.cpp:362)
//   0x18005ee30  GridReindex   Scene::Clean         (scene.cpp:373)  PER FRAME
//   0x18005f1f0  FreeGlobals   Scene::~Scene        (scene.cpp:382)
//
// THE CLOSURE, COUNTED BEFORE ANY CODE WAS WRITTEN
// ------------------------------------------------
// Lane PICK's five census rows called fourteen more unexported internals, so
// porting only the visible rows would have traded five doors for fourteen. The
// call graph (docs/history/runtime_port/callgraph/retail_edges.csv) gives this file's
// closure exactly, and it was checked against each disassembly:
//
//   GridInit      -> 0x18002fea0                          (Array<int32>::SetSize)
//   GridAdd       -> 0x18002fea0, 0x18005fb00, 0x18005ee30
//   GridReindex   -> 0x18005f090, 0x18005f4b0
//   GridUnthread  -> (leaf)
//   GridThread    -> (leaf)
//   FreeGlobals   -> Enter/Free/Leave, i.e. the allocator
//
// Three names outside the five, and their disposition:
//
//   * 0x18002fea0 — ALREADY PORTED AND REGISTERED, lane Q, as
//     `RtColl_ArraySetSize_18002fea0` with `kSpec_I32`. Nothing to do.
//   * 0x18005fb00 — the Array<Scene::Node> three-argument SetSize. NOT ported.
//     It is a member of lane Q's `SetSizeCore` family and is added THERE, as
//     `J3_ENTRY3(18005fb00, kSpec_Node18)`, so there is ONE body for the family
//     rather than a private second copy that can drift. Its spec was read out
//     of its own disassembly, not inferred from the element type — see the
//     comment on kSpec_Node18 in array.cpp.
//   * 0x18005f4b0 — the thread-in half of the re-index. NOT ported, not on any
//     census list because nothing outside the DLL calls it. Ported here.
//
// The allocator entries the two SetSize bodies and FreeGlobals reach
// (0x1800c7a50 / 0x1800c7ad0) are already armed by
// `RegisterAllocatorAddresses()`. So: **5 doors in, 7 bodies written, 0 new
// doors out.**
//
// THE FACT THAT DECIDES EVERY ONE OF THESE BODIES
// -----------------------------------------------
// *** A CELL'S CHAIN IS CIRCULAR, NOT NULL-TERMINATED. *** Read out of
// 0x18005f4b0's empty-cell arm at 0x18005f51c: when a node is the FIRST in its
// cell, retail writes `_next = id` and `_prev = id` — the node points at
// ITSELF. It does not write -1.
//
// This is not a detail; it is the difference between a correct port and a wild
// write, and the first draft of GridUnthread here got it wrong. GridUnthread's
// two splice stores are UNCONDITIONAL:
//
//     nodes[nodes[id]._prev]._next = nodes[id]._next;      // 18005f0cf..0dd
//     nodes[nodes[id]._next]._prev = nodes[id]._prev;      // 18005f0e5..0f3
//
// with no `if (prev >= 0)` anywhere. Read against a NULL-TERMINATED list that
// says retail writes to `nodes[-1]`, i.e. sixteen bytes in FRONT of the array
// buffer, which is the allocator's boundary tag — this project has already paid
// for one wild write of exactly that shape
// (reference_load_wild_write_high_dword_7fc.md), so "retail has a wild write
// here" was a plausible reading. It is wrong. With a circular chain a lone node
// has `prev == next == id` and both stores land on the node itself, which is
// also why GridUnthread needs the SECOND head fix at 0x18005f11e: the first one
// sets `head = _next`, and for a lone node `_next` IS `id`, so the head still
// names the node being removed and only then is it set to -1.
//
// The `-1` that DOES exist is written by GridUnthread at 0x18005f0fb/0x18005f108
// AFTER the splice, into the node being removed. It marks the node unthreaded;
// it is never a chain terminator.
//
// ORACLE
// ------
// A lockstep synthetic-subject probe, `scene_grid_probe.cpp`. Not a live diff:
// every body here mutates the one shared grid, so "run ours, run theirs,
// compare" re-threads a live scene twice and the second run starts from the
// state the first left — a different experiment whose first product is a
// corrupted allocator. And not an end-state comparison either
// (lane PICK: "a defect that cancels out — push a link on the free list in the
// wrong order, pop it back in the wrong order — compares equal at the end while
// every query in between saw a different grid"): the probe applies each of its
// indexed steps to BOTH subjects and compares the WHOLE scene after EVERY step.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstring>

#include "scene_grid.h"

namespace rtgfxgrid {

namespace {

// ---------------------------------------------------------------------------
// ENDGAME B.3.2 step 2 (callers) — the two `ArraySetSizeI32`/`ArraySetSizeNode`
// ResolveAddr forwarders that used to sit here are GONE. `Scene::_levelOffsets`,
// `_cellHeads`, `_cellCounts`, `_freeIds` (all `Collection::Array<int32_t>` /
// `<uint32_t>`) and `_nodes` (`Collection::Array<SceneNode>`) are already real
// class members (src/engine/graphics/Scene.h), and both rows are
// TRAIT-EQUIVALENT once the element type is the real one:
//   0x18002fea0  kSpec_I32     — int32_t is trivially constructible/destructible,
//                                so SpecFor<int32_t>() leaves the new tail
//                                UNINITIALISED exactly like this row.
//   0x18005fb00  kSpec_Node18  — SceneNode (Scene.h) is a plain POD, so
//                                SpecFor<SceneNode>() is the same {3-qword
//                                assign, no init, no destroy} shape.
// GridInit and GridAdd below now call `.SetSize(...)` on the members directly.
// Both rows stay registered in array.cpp: `code/` still reaches them by address.
// ---------------------------------------------------------------------------

// ENDGAME B.3.2 step 2 (callers) — the `MemFreeLocked` forwarder that used to
// sit here (0x1800c7ad0, ?MemFree@MemoryAllocator@System@@SAXPEAX@Z) is GONE:
// its only caller, `FreeGlobals`, now bridges onto `Collection::LinkedList<T>::
// Clear()` below, which reaches the identical door through
// `Collection::detail::ListNodeFree` (linkedlist.cpp).

// ---------------------------------------------------------------------------
// The float constants, LIFTED AS RAW BITS and not retyped from a decimal
// (reference_crt_coefficients_must_be_lifted_not_retyped.md). Each was read
// with `read_memory` out of the shipped DLL at the address the instruction
// names, not from Ghidra's rendering of it.
//
//   0x180161694 = 0x3f000000 = 0.5f      the half-cell / half-extent scale
//   0x18016166c = 0x3a83126f = 0.001f    the epsilon added to the half-extent
//   0x18016169c = 0x3f800000 = 1.0f      the reciprocal numerator
// ---------------------------------------------------------------------------
inline float FromBits(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
inline uint32_t ToBits(float f)   { uint32_t b; std::memcpy(&b, &f, 4); return b; }

const float kHalf    = FromBits(0x3f000000u);   // DAT_180161694
const float kEpsilon = FromBits(0x3a83126fu);   // DAT_18016166c
const float kOneF    = FromBits(0x3f800000u);   // DAT_18016169c

}  // namespace

}  // namespace rtgfxgrid

// ============================================================================
// 0x18005e4a0 — the grid's constructor, Graphics::SceneGrid::SceneGrid(float)
//
// Transcribed store for store. Retail's order is: zero the two half-cell floats
// as one qword, zero the ten qwords spanning +0x10..+0x5f (the five Arrays'
// default constructors, run before the body), THEN write _cellSize, THEN
// _levelCount, then the half-cell pair. The
// zero-then-write order matters only if something could observe the object in
// between, which nothing can — it is kept because it is what the body does and
// because the +0x60 global list is CONSPICUOUSLY NOT ZEROED here (Scene::Scene
// does that itself, three stores of its own).
//
// The eight level offsets are the 4^n prefix sums {0,1,5,0x15,…,0x1555} and are
// stored one at a time through a RE-READ of `_levelOffsets._data` — eight
// separate `MOV RAX,[RBP+0x40]` loads. Reproduced as such: a compiler that
// hoisted the load would be equivalent here, but the point of a transcription
// is not to be equivalent.
//
// The fill loop is bounded by a BYTE offset (`CMP RCX,0x15554`), which is
// 0x5555 int32s, and it reloads BOTH array pointers on every iteration.
// ORACLE: the probe's step 0 — both subjects are constructed, ours by this body
// and theirs by the shipped one, and the whole 0x78-byte object plus all three
// array CONTENTS are compared.
// ============================================================================
Graphics::SceneGrid::SceneGrid(float cellSize) {
    // 18005e4c9..4f1: the ten qwords are the five Arrays' constructors above.
    _halfCellX = 0.0f;                             // 18005e4c5, one qword
    _halfCellY = 0.0f;

    _cellSize   = cellSize;                        // 18005e4f1
    _levelCount = 8;                               // 18005e4f5

    const float half = cellSize * rtgfxgrid::kHalf;   // 18005e4fc  MULSS
    _halfCellX = half;                             // 18005e512, one qword
    _halfCellY = half;

    _levelOffsets.SetSize(8);                      // 18005e51e  retail 0x18002fea0, kSpec_I32 (trait-equivalent for int32_t)

    _levelOffsets._data[0] = 0x0000;               // 18005e523
    _levelOffsets._data[1] = 0x0001;               // 18005e52a
    _levelOffsets._data[2] = 0x0005;               // 18005e535
    _levelOffsets._data[3] = 0x0015;               // 18005e540
    _levelOffsets._data[4] = 0x0055;               // 18005e54b
    _levelOffsets._data[5] = 0x0155;               // 18005e556
    _levelOffsets._data[6] = 0x0555;               // 18005e561
    _levelOffsets._data[7] = 0x1555;               // 18005e56c

    _cellHeads.SetSize(RtGraphics::kSceneCellCount);   // 18005e580  retail 0x18002fea0
    _cellCounts.SetSize(RtGraphics::kSceneCellCount);  // 18005e58e  retail 0x18002fea0

    for (int32_t i = 0; i < RtGraphics::kSceneCellCount; ++i) {   // 18005e5a0..5be, byte offset < 0x15554
        _cellHeads._data[i]  = -1;                 // both array pointers re-read
        _cellCounts._data[i] = 0;                  // every iteration
    }
}                                                  // 18005e5c0  MOV RAX,RBP

namespace rtgfxgrid {

// ============================================================================
// 0x18005e5e0 — the grid insert
//
// Two arms, and BOTH must be exercised by any run that claims to verify this:
//   * free list non-empty  — pop the LAST id off `_freeIds`, shrink the array
//     by one, and overwrite that node in place;
//   * free list empty      — append one node built on the stack.
//
// `geo` is a POINTER TO a Geometry*: retail's `MOV RAX,[RSI]` at 18005e611 and
// 18005e64b dereferences RDX once. Scene::AddObject spills its own argument to
// a local for exactly this reason, and scene.cpp already does so.
//
// +0x14 (`SceneNode::_pad14`) IS NEVER WRITTEN WITH A DEFINED VALUE ON EITHER
// ARM, and that is retail, not an omission here. Both arms build the
// `{_cell = -1, ?}` pair as ONE qword out of a stack slot whose high dword is
// an uninitialised local (`MOV dword [RSP+0x30],0xffffffff` then
// `MOV RAX,qword [RSP+0x30]`, with nothing writing [RSP+0x34]) — the same shape
// Wave 5 found in the shipped Picking::Scene::RemoveMesh. So:
//   * the append arm below uses a genuinely partially-initialised local, which
//     reproduces the shape exactly: an indeterminate byte range is copied into
//     the array;
//   * the reuse arm writes only `_cell` and leaves +0x14 holding whatever the
//     slot held before, which is equally indeterminate;
//   * NO ORACLE MAY COMPARE +0x14. The probe excludes it, and the exclusion is
//     derived from these two instructions rather than discovered from a failure
//     (reference_oracle_must_not_compare_unwritten_bytes.md).
//
// ORACLE: the probe. `_freeIds` is deliberately non-empty before four of its
// adds so the reuse arm runs; the counters say how many times each arm ran and
// a zero is reported as a GAP.
// ============================================================================
uint32_t GridAdd(Scene* self, Geometry* const* geo) {

    const int32_t freeCount = self->_grid._freeIds.GetSize();  // 18005e5f2  MOVSXD
    int32_t id;

    if (freeCount > 0) {
        // ---- reuse arm ----------------------------------------------- 5fd
        id = static_cast<int32_t>(self->_grid._freeIds[freeCount - 1]);        // 605
        self->_grid._freeIds.SetSize(freeCount - 1);                           // 60c  retail 0x18002fea0

        SceneNode* const nodes = self->_grid._nodes.GetData();                 // 627
        nodes[id]._object = *geo;                                        // 62b
        nodes[id]._next   = 0;                                           // 634, one
        nodes[id]._prev   = 0;                                           //      qword
        nodes[id]._cell   = -1;                                          // 63e, low
        /* nodes[id]._pad14 deliberately untouched — see the banner */   //      dword
    } else {
        // ---- append arm ---------------------------------------------- 645
        id = self->_grid._nodes.GetSize();                                     // 645
        SceneNode tmp;                                                   // [RSP+0x20]
        tmp._object = *geo;                                              // 660
        tmp._next   = 0;                                                 // 657, one
        tmp._prev   = 0;                                                 //      qword
        tmp._cell   = -1;                                                // 665
        /* tmp._pad14 left UNINITIALISED on purpose: retail's [RSP+0x34]  */
        /* is never written and is copied into the array as-is.           */
        // retail 0x18005fb00, kSpec_Node18 — trait-equivalent: SceneNode is a
        // plain POD (Scene.h), so SpecFor<SceneNode>() has the same {assign =
        // 3-qword copy, init = null (no else arm), destroy = null} shape.
        self->_grid._nodes.SetSize(self->_grid._nodes.GetSize() + 1, tmp);           // 66d
    }

    GridReindex(self, id);                                               // 677
    return static_cast<uint32_t>(id);                                    // 686
}

// ============================================================================
// 0x18005f090 — the grid unthread
//
// Read the banner's circular-chain paragraph before touching this. In order:
//
//   1. head = cellHeads[nodes[id]._cell];  if (head == id) *headSlot = nodes[head]._next;
//   2. nodes[nodes[id]._prev]._next = nodes[id]._next;     UNCONDITIONAL
//   3. nodes[nodes[id]._next]._prev = nodes[id]._prev;     UNCONDITIONAL
//   4. nodes[id]._prev = -1;  nodes[id]._next = -1;        (in that order)
//   5. if (cellHeads[cell] == id) cellHeads[cell] = -1;    the LONE-NODE case
//   6. cellCounts[cell]--;  then walk up: cell = (cell-1)>>2 until cell == 0,
//      decrementing each ancestor.
//
// Step 5 exists BECAUSE of step 1 and the circular chain: for a lone node
// `_next == id`, so step 1 sets the head to the node it is removing and only
// step 5 empties the cell. A port that treated the chain as null-terminated
// would make step 5 dead and leave a dangling head — and this project has
// already paid for a missing erase of precisely this kind, a cancel path that
// never removed its primary node, with a teardown 100%-CPU hang and an `IsA`
// execute crash at the far end of it (reference_workarea_bst_bad_entry.md).
// `CheckPtr` is a bare RET in every shipped build, so nothing would have caught
// it downstream.
//
// Retail RE-READS `_nodes._data` before each of steps 2, 3, 4a, 4b and 5. That
// is transcribed rather than hoisted: none of these can realloc, so a hoist
// would be equivalent, but the point of a transcription is not to be
// equivalent.
//
// ORACLE: the probe, whose remove steps deliberately unthread a HEAD of a
// multi-node chain, a MIDDLE node, and a LONE node — the three arms.
// ============================================================================
void GridUnthread(Scene* self, int32_t id) {

    SceneNode* nodes    = self->_grid._nodes._data;                    // 18005f099
    int32_t*   heads    = self->_grid._cellHeads._data;                // 18005f095
    int32_t*   headSlot = &heads[nodes[id]._cell];               // 18005f0af..b4

    const int32_t head = *headSlot;                              // 18005f0b8
    const bool wasHead = (head == id);
    if (wasHead)                                                 // 18005f0bd
        *headSlot = nodes[head]._next;                           // 18005f0c3..c8

    nodes = self->_grid._nodes._data;                                  // 18005f0cb
    nodes[nodes[id]._prev]._next = nodes[id]._next;              // 18005f0cf..dd
    nodes = self->_grid._nodes._data;                                  // 18005f0e1
    nodes[nodes[id]._next]._prev = nodes[id]._prev;              // 18005f0e5..f3

    nodes = self->_grid._nodes._data;                                  // 18005f0f7
    nodes[id]._prev = -1;                                        // 18005f0fb
    nodes = self->_grid._nodes._data;                                  // 18005f104
    nodes[id]._next = -1;                                        // 18005f108

    nodes = self->_grid._nodes._data;                                  // 18005f111
    int32_t cell = nodes[id]._cell;                              // 18005f115
    heads = self->_grid._cellHeads._data;                              // 18005f11a
    const bool emptied = (heads[cell] == id);
    if (emptied)                                                 // 18005f11e
        heads[cell] = -1;                                        // 18005f123

    nodes = self->_grid._nodes._data;                                  // 18005f12a
    cell  = nodes[id]._cell;                                     // 18005f132
    --self->_grid._cellCounts._data[cell];                             // 18005f137
    while (cell != 0) {                                          // 18005f13a
        cell = (cell - 1) >> 2;                                  // 18005f144..46
        --self->_grid._cellCounts._data[cell];                         // 18005f14c
    }

    // Branch coverage, INSIDE the function rather than a proxy outside it
    // (WAVE9.md, "the effectiveness control was wrong twice, and both times it
    // said PASS"). `wasHead` and `emptied` are the two arms a single-node-per-
    // cell population never separates.
}

// ============================================================================
// 0x18005f4b0 — the grid thread-in
//
//   head = cellHeads[cell]
//   if (head >= 0) {                                   // 18005f4dc  TEST/JS
//       nodes[id]._prev = nodes[head]._prev;           // 4e4..e8   insert BEFORE
//       nodes[id]._next = head;                        // 4f1        the head, i.e.
//       nodes[nodes[id]._prev]._next = id;             // 4fa..503   at the TAIL of
//       nodes[nodes[id]._next]._prev = id;             // 50c..515   a circular ring
//   } else {
//       cellHeads[cell] = id;                          // 51c
//       nodes[id]._next = id;                          // 523   *** SELF-LINK ***
//       nodes[id]._prev = id;                          // 52c
//   }
//   cellCounts[cell]++; then every ancestor, cell = (cell-1)>>2 until 0.
//
// The head is NOT updated on the non-empty arm, so an insert always lands at
// the ring's tail and the cell's head is whichever node arrived first and has
// not yet been removed.
//
// This body is a leaf, it is reached only from GridReindex, and nothing outside
// the DLL calls it — which is why no census row names it. Porting GridReindex
// without it would have swapped one unregistered door for another.
// ============================================================================
void GridThread(Scene* self, int32_t id) {

    SceneNode* nodes = self->_grid._nodes._data;                       // 18005f4b5
    int32_t*   heads = self->_grid._cellHeads._data;                   // 18005f4b9
    const int32_t cell = nodes[id]._cell;                        // 18005f4cf
    const int32_t head = heads[cell];                            // 18005f4d4

    const bool empty = (head < 0);
    if (!empty) {                                                // 18005f4de
        nodes[id]._prev = nodes[head]._prev;                     // 18005f4e4..e8
        nodes = self->_grid._nodes._data;                              // 18005f4ed
        nodes[id]._next = head;                                  // 18005f4f1
        nodes = self->_grid._nodes._data;                              // 18005f4f6
        nodes[nodes[id]._prev]._next = id;                       // 18005f4fa..503
        nodes = self->_grid._nodes._data;                              // 18005f508
        nodes[nodes[id]._next]._prev = id;                       // 18005f50c..515
    } else {
        heads[cell] = id;                                        // 18005f51c
        nodes = self->_grid._nodes._data;                              // 18005f51f
        nodes[id]._next = id;                                    // 18005f523
        nodes = self->_grid._nodes._data;                              // 18005f528
        nodes[id]._prev = id;                                    // 18005f52c
    }

    nodes = self->_grid._nodes._data;                                  // 18005f531
    int32_t c = nodes[id]._cell;                                 // 18005f539
    ++self->_grid._cellCounts._data[c];                                // 18005f53e
    while (c != 0) {                                             // 18005f541
        c = (c - 1) >> 2;                                        // 18005f554..56
        ++self->_grid._cellCounts._data[c];                            // 18005f55c
    }

}

// ============================================================================
// 0x18005ee30 — the grid re-index
//
// Reads the geometry's CURRENT world AABB, derives a quadtree level from the
// larger of its two half-extents, derives a Morton cell index within that
// level, and rethreads the node if the cell changed.
//
// ONLY X AND Y ARE USED. Retail loads all six floats of `_worldBounds` but the
// z pair is spilled and then overwritten before any read: `[RBP-0x18]` receives
// max.z at 18005ee71 and min.z at 18005ee85 and is then clobbered by the
// `{max.x, max.y}` qword at 18005eeb4. That is a dead load in the shipped
// build, not a term this port dropped, and it is why the level depends on a
// 2-D half-extent.
//
// THE LEVEL IS AN EXPONENT FIELD, NOT A LOGARITHM CALL:
//     ratio = _cellSize / (halfExtent + 0.001f)
//     level = (int)((bits(ratio) >> 23)) - 127          // 18005ef43..4b
// `SHR` is LOGICAL, so a negative ratio would read its sign bit as part of the
// exponent. That is retail's arithmetic and it is reproduced as an unsigned
// shift over the raw bits; it is reachable only for an inverted box, which
// nothing in the engine produces.
//
// The Morton interleave at 18005efca..18005f043 is transcribed operation for
// operation rather than replaced by a canonical `part1by1`. The two are the
// same function on the inputs that reach it, but the compiler's form shares
// sub-expressions in a way that is easy to "simplify" into something that
// differs on some input, and this project has four near-misses in one container
// family that all passed green runs — two differing only in an alignment
// immediate, one in a shift, one in a trailing call.
//
// ORACLE: the probe. Its re-index steps deliberately include a box that does
// NOT move (the early-out arm at 18005f04f, where `calls` is not coverage) and
// boxes that move both within a level and across levels.
// ============================================================================
void GridReindex(Scene* self, int32_t id) {

    SceneNode* const nodes = self->_grid._nodes._data;                 // 18005ee4b  R15
    const Geometry* const geo = nodes[id]._object;               // 18005ee5e

    // The geometry's world box (+0x4c): min.x/min.y and max.x/max.y.
    const float minX = geo->_worldBounds.min.x;                  // 18005ee8f
    const float minY = geo->_worldBounds.min.y;                  // 18005ee80
    const float maxX = geo->_worldBounds.max.x;                  // 18005ee62
    const float maxY = geo->_worldBounds.max.y;                  // 18005ee67

    float cx = (minX + maxX) * kHalf;                            // 18005eec8/eed4
    float cy = (minY + maxY) * kHalf;                            // 18005eecc/eed8
    float hx = (maxX - minX) * kHalf;                            // 18005eed0/eedc
    float hy = (maxY - minY) * kHalf;                            // 18005eeea/eef2

    // COMISS XMM5,XMM6 / JA keep / MOVAPS XMM5,XMM6 — i.e. keep hx only when
    // hx > hy. An unordered compare (a NaN extent) falls through to hy, which
    // is what `JA` does and what this spelling does.
    float half = (hx > hy) ? hx : hy;                            // 18005eefa..eff

    const float x = cx + self->_grid._halfCellX;                       // 18005ef10
    const float y = cy + self->_grid._halfCellY;                       // 18005ef1d
    half += kEpsilon;                                            // 18005ef15
    const float ratio = self->_grid._cellSize / half;                  // 18005ef3a  DIVSS

    int32_t level = static_cast<int32_t>(ToBits(ratio) >> 23) - 0x7f;  // ef43..4b
    if (level >= self->_grid._levelCount) level = self->_grid._levelCount - 1;     // ef4f..54
    if (level < 0) level = 0;                                          // ef63..6a

    const int32_t dim = static_cast<int32_t>(1u << level);       // 18005ef72
    const float levelCell = self->_grid._cellSize / static_cast<float>(dim);  // ef78..7b
    const float inv = kOneF / levelCell;                         // 18005ef7f
    const float fx = inv * x;                                    // 18005ef8b
    const float fy = inv * y;                                    // 18005ef86

    int32_t gx = static_cast<int32_t>(fx);                       // 18005efa2  CVTTSS2SI
    int32_t gy = static_cast<int32_t>(fy);                       // 18005efa7
    if (gx < 0) gx = 0;                                          // 18005efad
    if (gy < 0) gy = 0;                                          // 18005efb3
    if (gx >= dim) gx = dim - 1;                                 // 18005efba
    if (gy >= dim) gy = dim - 1;                                 // 18005efc1

    // The interleave, 18005efca..18005f043, in the compiler's own order.
    const uint32_t ux = static_cast<uint32_t>(gx);
    const uint32_t uy = static_cast<uint32_t>(gy);
    const uint32_t x1 = ((ux << 8) | ux) & 0x00ff00ffu;          // efcc..d1
    const uint32_t x2 = ((x1 << 4) | x1) & 0x0f0f0f0fu;          // efd9..e1
    const uint32_t x3 = ((x2 << 2) | x2) & 0x33333333u;          // efe9..ffd
    const uint32_t y1 = ((uy << 8) | uy) & 0x00ff00ffu;          // efe6..ff5
    const uint32_t y2 = ((y1 << 4) | y1) & 0x0f0f0f0fu;          // f003..008
    const uint32_t y3 = ((y2 << 2) | y2) & 0x33333333u;          // f00d..016
    const uint32_t a  = ((y3 << 1) | y3) << 1;                   // f01c..02b
    const uint32_t cell =
        ((((a ^ x3) & 0x55555555u) ^ a) | ((x3 & 0x2aaaaaaau) << 1))  // f02d..043
        + static_cast<uint32_t>(self->_grid._levelOffsets._data[level]);    // f03f/f045

    const int32_t was = nodes[id]._cell;                         // 18005f048
    const bool moved  = (was != static_cast<int32_t>(cell));
    if (moved) {                                                 // 18005f04f
        if (was >= 0)                                            // 18005f051
            GridUnthread(self, id);                              // 18005f05a
        nodes[id]._cell = static_cast<int32_t>(cell);            // 18005f064
        GridThread(self, id);                                    // 18005f069
    }

    // `calls` on this site counts ENTRIES, and Scene::Clean enters it for every
    // object every frame. `worked` is the number that actually rethreaded.
}

}  // namespace rtgfxgrid
