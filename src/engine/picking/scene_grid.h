#pragma once
// ============================================================================
// src/engine/picking/scene_grid.h — Wave 9 lane PICK
// ============================================================================
//
// Declares the nineteen bodies ported in scene_grid.cpp for the files that
// call them by name (picking/scene.cpp and several files in src/engine/graphics/).
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>

#include "picking/Scene.h"

namespace rtpickgrid {

// ENDGAME B.3.7 block B6: the Wave 5 POD rows are the real class's nested
// types now (src/engine/picking/Scene.h); B.4 (R-CORE): the nineteen
// bodies read them by name.
using Scene   = Picking::Scene;
using Entry   = Picking::Scene::Entry;
using Cell    = Picking::Scene::Cell;
using Link    = Picking::Scene::Link;
using AddDesc = Picking::Scene::AddDesc;
using PickingMesh    = Picking::PickingMesh;
using TransformArray = Collection::Array<Math::Transform>;

// ---------------------------------------------------------------------------
// The cell-rectangle iterator `Picking::Scene::Intersect` drives.
//
// Layout pinned by Intersect's own frame at [RSP+0x30] and re-confirmed by
// every displacement in 0x18007b6d0's disassembly. ONE definition (B7-6):
// scene.cpp's Intersect drives this very struct (it used to declare a copy).
// ---------------------------------------------------------------------------
struct CellIter {
    int32_t minX, minY;   // +0x00 +0x04
    int32_t maxX, maxY;   // +0x08 +0x0c
    int32_t curX, curY;   // +0x10 +0x14
    int32_t stamp;        // +0x18
    int32_t link;         // +0x1c
    const Scene* scene;   // +0x20  read only by CellIterAdvance
};
static_assert(sizeof(CellIter) == 0x28, "Picking cell iterator must be 0x28 bytes");
static_assert(offsetof(CellIter, scene) == 0x20, "CellIter::scene @ +0x20");

// ---------------------------------------------------------------------------
// The nineteen bodies, in dependency order. The five the address-door census
// named are marked (*); the other fourteen are their transitive closure and
// were NOT reachable from our tree before this lane — porting the five without
// them would have swapped five unregistered address doors for fourteen new
// ones, which is not progress.
//
// ---------------------------------------------------------------------------

// 0x18001c100  Array<T>::~Array for trivially-destructible T (the J1 body).
void  ArrayDtorTransform(TransformArray* a);
// 0x18001b360  Array<Math::Transform>::operator=
TransformArray* TransformArrayAssign(TransformArray* dst, const TransformArray* src);
// 0x180074100  destroy `count` Entries' inner Array<Transform>, in place
void  DestroyEntryTransforms(void* ignored, Entry* base, int count);
// 0x1800735c0  Entry copy-CONSTRUCT (used by the Array<Entry> grower)
Entry* EntryCopyConstruct(Entry* dst, const Entry* src);
// 0x18007b590  Entry::operator=
Entry* EntryAssign(Entry* dst, const Entry* src);
// 0x18001f470  Volume::Box transformed by a Math::Transform -> AABB; the out
// box is the six floats {min.xyz, max.xyz}, the in box a ::Box (B.4 S4 GFX2)
float* BoxTransform(float* outBox, const Math::Transform* transform, const ::Box* inBox);
// 0x180125bc0  project a mesh's node AABBs through their transforms, XY only:
// a file-local template over the two {_mesh, _transforms} holders (AddDesc and
// Entry), scene_grid.cpp
// 0x1800738c0  Array<Cell>::SetSize(n, fill)
void  CellArraySetSize(Collection::Array<Cell>* a, int newSize, const Cell* fill);
// 0x18007bc30  Array<Entry>::SetSize(n, fill)
void  EntryArraySetSize(Collection::Array<Entry>* a, int newSize, const Entry* fill);
// 0x180072e70  unlink a now-empty Cell from its bucket, push on the free list
void  FreeEmptyCell(Scene* scene, Cell* cell);
// 0x180072d10  unlink one entry's Link from one Cell's list
void  UnlinkEntryFromCell(Scene* scene, Cell* cell, int32_t entryId);
// 0x180072b30  find the Cell for (x,y), creating it if absent
Cell* FindOrCreateCell(Scene* scene, const int32_t* xy);
// 0x180072730  thread an entry into every cell of a rectangle
void  AddToCells(Scene* scene, const int32_t* xy, uint16_t w, uint16_t h,
                 int32_t entryId);
// 0x18007b210  allocate an Entry (free list or grow) and fill it in
int32_t EntryAlloc(Scene* scene, const AddDesc* desc, const int32_t* xy,
                   uint16_t w, uint16_t h);

// (*) The five census rows.
uint32_t GridAdd(Scene* scene, const AddDesc* desc);                  // 0x18007af70
void     GridReindex(Scene* scene, uint32_t id);                      // 0x1800724d0
void     GridUnthread(Scene* scene, int32_t* cellXY, uint16_t w,
                      uint16_t h, int32_t id);                        // 0x180072950
void     GridFreeEntry(Scene* scene, uint32_t id);                    // 0x18007b390
void     CellIterAdvance(CellIter* it);                               // 0x18007b6d0

// ---------------------------------------------------------------------------
}  // namespace rtpickgrid
