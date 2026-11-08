// src/game/Game/serialize_arrays.cpp
// Generic Geometry/Transform/Array serialize helpers used by MenuScene::Slot5.
//
// Decompiled from Application-x64.exe. These helpers are not MenuScene-
// specific — Geometry::Serialize and other scene serializers reuse them.
//
// Helper map (M3.5 / docs/history/notes/M35_recon.md):
//   #1 Geometry init                — exe 0x1400b00e0  (no cascade)
//   #2 Transform::Serialize wrapper — exe 0x1400988c0  (no cascade)
//   #3 Array_Resize stride 0x20     — exe 0x14001baf0  (no cascade)
//   #4 Array_Resize stride 0x08     — exe 0x140001bb0  (no cascade)
//   #5 Array_Resize stride 0x14     — exe 0x14001bf10  (no cascade)
//   #6 Array_Serialize stride 0x20  — exe 0x14001bbf0  (no cascade)
//   #7 Array_Serialize_External GraphicsMesh — exe 0x14001bcc0  (CASCADE)
//   #8 Array_Serialize_External Particle     — exe 0x14001bff0  (CASCADE)
//
// Helpers #7/#8 share a byte-identical body that differs only in the
// BaseTypeInfo* slot it dereferences. We extract that body into a single
// generic implementation parameterized by typeIdPtr.
//
// **param_4 mystery (recon question 2):** the Ghidra decompile of #7 and #8
// shows `undefined8 param_4` threaded into the function but never read past
// `local_res20 = param_4` (which is then overwritten with GetReferenceId's
// return). The caller (MenuScene::Slot5 at 0x1400a3779/0x1400a3928) never
// sets R9 — only RCX/RDX/R8 are loaded before the CALL. **param_4 is
// decompiler noise** (R9 leftover from prior code), so our reimpl signature
// drops it entirely.

#include "Game/WorldGeometry.h"
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "Game/PlayerController.h"
#include "math/Math.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "graphics/GraphicsMesh.h"   // ENDGAME B.3.9 TR: GraphicsMesh::TypeId()
#include "graphics/Particle.h"       // ENDGAME B.3.9 TR: Particle::TypeId()
#include "graphics/Mesh.h"           // B7 LX2: Graphics::EdgeMesh::TypeId()
#include "graphics/Material.h"       // B7 LX2: Graphics::MaterialInstance
#include "collection/Collection.h"   // Collection::Array / ExplicitSpec (Ptr8ZeroArray)
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60 (Entity/EntityList.h)
#include "system/Filename.h"
#include "system/String.h"
#include "Object/factory_registration.h"
#include "collection/Collection.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <new>
#include "Game/exe_bodies.h"
#include "Game/MenuScene.h"
#include "Component/SpawnDescription.h"   // SpawnDescription::EntryArray (0x14000c520's holder)

// ============================================================
// Helper #1 — Geometry init (FUN_1400b00e0)
// ============================================================
//
// Constructs a 0x230-byte Graphics::Geometry-derived container ("TerrainGenerator"
// per the type-name string at exe 0x14015f158). Layout per Ghidra disasm of
// FUN_1400b00e0:
//   +0x000  Graphics::Geometry::Geometry(self)        — base ctor (0xb8 bytes)
//   +0x000  vtable = exe 0x14015f110 (derived 9-slot)
//   +0x0c0..+0x0f8  zeroed (mesh state slots)
//   +0x100  Video::VertexBuffer ctor (~0x20 bytes)
//   +0x120  Video::IndexBuffer  ctor (~0x30 bytes)
//   +0x150,+0x158  zeroed
//   +0x160  Video::VertexBuffer ctor
//   +0x180  Video::IndexBuffer  ctor
//   +0x1a0..+0x228  zeroed
//   +0x030  = 1  (state flag)
//
// ENDGAME B.3.8 block LOOP: the body is WorldGeometry's constructor
// (src/game/Game/WorldGeometry.h). Graphics::Geometry() and the 0x14015f110
// stamp are the compiler's (the old body ran the base ctor door and then
// stamped the hand-built table registered at 0x14015f110); the field stores are
// unchanged. (The Reimpl_Geometry_Init door registered at 0x1400b00e0 went at the
// B.3.8 close with fn_Geometry_Init, its only reader.)
WorldGeometry::WorldGeometry() {
    _gridSize = 0;  _mapSize = 0;       // qword +0xc0
    _chunkSize = 0; _chunkVertsPerSide = 0;       // qword +0xc8
    _heightRowStride = 0;                       // dword +0xd0 (+0xd4 left as found)
    // +0xd8 / +0xe0: two qword stores in retail; three named stores here (H7).
    _heights.data = nullptr; _heights.size = 0; _heights.capacity = 0;
    StillTerrain::Bump();   // [STILLTERRAIN] a new (empty) heightmap
    _dirtyChunks.root = nullptr;        // +0xe8
    _dirtyChunks.count = 0;             // dword +0xf0
    _terrainMaterial = nullptr;         // +0xf8

    Video_VertexBuffer_Ctor(&_vertexBuffer);   // +0x100 (ctor body, B4b-2)
    Video_IndexBuffer_Ctor(&_indexBuffer);     // +0x120

    _waterMaterialA = nullptr;          // +0x150
    _waterMaterialB = nullptr;          // +0x158

    Video_VertexBuffer_Ctor(&_waterVertexBuffer);   // +0x160
    Video_IndexBuffer_Ctor(&_waterIndexBuffer);     // +0x180

    // +0x1a0..+0x218: retail zeroes each {data, size|capacity} with two qword
    // stores (0x1400b018f..0x1400b01f8); three named stores each here (H7).
    // +0x220 / +0x228 (0x1400b01ff / 0x1400b0206) are `_batchCounters`' own ctor.
    _contourVertices.data = nullptr; _contourVertices.size = 0; _contourVertices.capacity = 0;
    _contourIndices.data  = nullptr; _contourIndices.size  = 0; _contourIndices.capacity  = 0;
    _contourRangesA.data  = nullptr; _contourRangesA.size  = 0; _contourRangesA.capacity  = 0;
    _contourRangesB.data  = nullptr; _contourRangesB.size  = 0; _contourRangesB.capacity  = 0;
    _contourOutlines.data = nullptr; _contourOutlines.size = 0; _contourOutlines.capacity = 0;
    // +0x1F0 / +0x200 (0x1400b01d5..0x1400b01ea): `_decals` / `_roadBatches`' own ctors.
    _batchMaterials.data  = nullptr; _batchMaterials.size  = 0; _batchMaterials.capacity  = 0;

    _materialTypeFlags = static_cast<uint32_t>(Graphics::MaterialTypeMask::Opaque);   // +0x30 (Graphics::Geometry's)
}

// ============================================================
// Helper #2 — Transform::Serialize wrapper (FUN_1400988c0)
// ============================================================
//
// Wraps a Stream in a Serializer, then delegates to Math::Transform::Serialize
// + Math::Vector3::Serialize at +0x1c + 3 IO::Serializer::Serialize(float)
// calls at +0x28/+0x2c/+0x30.
//
// ⚠ THIS IS A STREAM CALL, NOT A SETTER. It was a warn-and-return stub for a
// long time, and the "unresolved — Transform fields remain at default values"
// wording made that read as a cosmetic loss (a camera left at the origin). It is
// not: GameUpdateLoop::Serialize calls this for the PlayerController at +0xE0
// immediately BEFORE EntityList::Serialize, so a body that consumes zero bytes
// leaves the whole camera record sitting in the stream, and the entity list then
// reads a camera float as its master count (observed 0x3EB7B5ED = 1051929709,
// which is ~0.358f). Our own saves skipped the same bytes on the SAVE leg, so the
// round trip stayed self-consistent — only the RETAIL-authored packaged tutorial
// save could expose it.
//
// All four callees are Runtime exports, so a null here means engine_funcs never
// resolved — report that loudly rather than silently dropping the read.
void PlayerController::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // ?Serialize@Transform@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x180059b90
    transform.Serialize(ser);                                         // +0x00 Math::Transform
    // ?Serialize@Vector3@Math@@QEAAXAEAVSerializer@IO@@@Z    retail 0x1800150e0
    target.Serialize(ser);                                            // +0x1c Math::Vector3
    ser.Serialize(yaw, nullptr);        // +0x28
    ser.Serialize(pitch, nullptr);      // +0x2c
    ser.Serialize(distance, nullptr);   // +0x30
}

// Generic dynamic-array resize implementation
// ============================================================
//
// Layout of arrayHeader (16 bytes):
//   +0x00: void*  data        — buffer base (or NULL)
//   +0x08: int32  count       — current length (visible to caller)
//   +0x0C: int32  capacity    — allocated slots (>= count)
//
// Algorithm matches FUN_14001baf0 / FUN_140001bb0 / FUN_14001bf10:
//   if (newCount == count) return;
//   if (newCount > capacity) {
//       capacity = newCount;
//       if (newCount == count + 1 && count != 0) capacity = newCount * 3;
//       newBuf = MemAlloc(capacity * stride, alignment);
//       (caller continues below to copy)
//   }
//   for slot in [start..newCount):
//       if (slot < oldCount) memcpy elem from oldBuf
//       else if (newBuf was reallocated) zero-init elem
//   if (oldBuf != newBuf) MemFree(oldBuf)
//
// `start` = oldCount when buf didn't reallocate, else 0.
//
// Note: ALL variants zero every new slot (slot >= oldCount) on BOTH paths,
// matching the original exe resize functions. An earlier version assumed
// "MemAlloc returns zeroed memory" and only zeroed on the realloc path —
// that was WRONG (the engine boundary-tag allocator at Runtime 0x1800c6e60
// returns DIRTY memory), and on an in-place grow it left new slots holding
// stale bytes (a freed-block pointer) → free-list corruption when a later
// serialize treated the stale pointer as live. See the unconditional-zero
// branch below.
//
// ENDGAME B.3.2 step 2: `GenericArrayResize` (the runtime-stride body the
// block above describes) and the two hand-unrolled bodies after it
// (FUN_140001bb0, FUN_14000c520) WERE copies of `rtcoll::SetSizeCore`
// (src/engine/collection/array.cpp). Each retail address is now one
// `Collection::Array<>::SetSize` over its own `ArraySpec` row: the zero-init
// the paragraph above insists on is the row's `init`, and SetSizeCore runs it
// for every slot >= oldCount on both the in-place and the realloc path — the
// same rule. Compared arm for arm before the swap: exact-equal early return,
// `_size` stored before the capacity test, treble-on-single-append-from-
// non-empty, byte-copy the preserved prefix only when the block moved, init
// the new tail, no destroy, free the old block on a realloc (null-tolerant
// 0x1800c7ad0, so the `oldData != nullptr` test is folded in). Two
// deliberate differences: the allocation now goes through the LOCKING static
// `MemAlloc` (0x1800c7a50) instead of the unlocked instance `Allocate` these
// bodies reached through `fn_MemAlloc` (same allocator, same heap); and the
// "fn_MemAlloc unavailable" scaffold guard + printf is gone — fn_MemAlloc is
// resolved before any serializer runs, so that arm was dead. The `!arrayHeader`
// early-out stays on each entry point exactly as before.
namespace {

// Whole-stride byte copy, for the plain-data rows.
template <uint32_t N> void ByteAssign(uint8_t* d, const uint8_t* s) { std::memcpy(d, s, N); }

// ENDGAME B.3.2 closing pass: three of the four rows that used to sit here said
// nothing a C++ type's traits do not say — whole-stride byte copy, whole-stride
// zero tail, no destroy, no deliberately-untouched extent — so each is now the
// trait-derived `Collection::Array<T>` over an element that zeroes itself.
// The old bridge header's zeroing elements were exactly those elements, so
// `SpecFor<T>()` re-derived the identical row; the stride-8 pointer row is now
// named (kSpec_PtrZero). The retail addresses stay here as the provenance.
// `kSpec_SpawnEntry` below does NOT collapse and stays explicit.

// FUN_140001bb0 — stride 8, align 8: raw pointer slots, zeroed on every new
// slot (the heap-corruption note above Reimpl_Array_Resize_Stride08 says why
// that zero is load-bearing): the row is kSpec_PtrZero (stride 8, align 8, qword
// copy, zeroed tail, no destroy), the one the old Ptr8 element's trait row equalled.
using Ptr8ZeroArray = Collection::Array<void*, Collection::DefaultAllocator,   // exe 0x140001bb0
                                        Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;

// FUN_14000c520 — SpawnDescription entry {ExternalPtr @0, float minSpawnAge
// @8, float maxSpawnAge @0xc}, stride 0x10, align 8. Whole-stride copy; the
// tail seed is {0, 0.0f, 10.0f} — see the banner above
// Reimpl_Array_Resize_Stride10_Spawn for why 10.0f is load-bearing.
struct SpawnEntry {
    void*  ref;           // +0x00 ExternalPtr
    float  minSpawnAge;   // +0x08
    float  maxSpawnAge;   // +0x0C
};
static_assert(sizeof(SpawnEntry) == 0x10, "SpawnEntry stride 0x10");
// The new element written into the row's raw storage (the three stores of
// 0x14000c520's unrolled arms).
void InitSpawnEntry(uint8_t* e) {
    static const SpawnEntry kNew = { nullptr, 0.0f, 10.0f };   // maxSpawnAge 0x41200000
    std::memcpy(e, &kNew, sizeof(SpawnEntry));
}
// stays explicit: `init` is a patterned fill (maxSpawnAge = 10.0f at +0x0c), not
// the zero/as-found tail any element type's `T()` would write.

} // namespace

// External linkage: SpawnDescription::EntryArray (Component/SpawnDescription.h)
// names it as its spec policy.
extern const rtcoll::ArraySpec kSpec_SpawnEntry = {      // exe 0x14000c520
    "Array<SpawnEntry>", 0x10, 8,
    &ByteAssign<0x10>, &InitSpawnEntry, nullptr, true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };
static_assert(sizeof(SpawnDescription::Entry) == sizeof(SpawnEntry), "the row's stride is the entry's");

// ============================================================
// Helpers #3-#5 — Array resize variants
// ============================================================
// 0x14001baf0 (stride 0x20): B4 Y GAMEA: its one caller, MenuScene's +0x30
// meshes, is a typed Collection::Array<MenuSceneMesh> whose trait row is this
// one (byte copy, whole-stride zero tail, no destroy); it calls SetSize, and
// Reimpl_Array_Resize_Stride20 is deleted.

// NOTE (heap-corruption fix): the stride-0x08 array holds raw 8-byte POINTERS
// (e.g. the Geometry +0x210 External*/MaterialInstance ref array). The original
// FUN_140001bb0 zeroes EVERY new slot (slot >= oldCount) on BOTH the in-place
// grow and realloc paths — see Ghidra decompile, the `else if (puVar1) *puVar1=0`
// branch runs unconditionally. This is essential because
// System::MemoryAllocator::Allocate (Runtime 0x1800c6e60) returns DIRTY memory
// (boundary-tag allocator, no zero-fill); a non-zeroed new pointer slot would
// retain a STALE pointer from a previously-freed block. A later serialize pass
// (sub_0150 / ArraySerializeExternalGeneric) then treats that stale pointer as a
// live External* and runs External::GetPath / Filename refcount ops on the
// already-freed System::String — incrementing the refcount slot at (strPtr-4),
// which is exactly (freedBlock+4), i.e. the high dword of the allocator's
// free-list link → the deterministic +0x100000000 bit-32 flip → wild allocator
// write → menu heap crash.
//
// (Historical: the shared GenericArrayResize once zeroed new slots only on
// the realloc path, which is why stride-8 got its own body. Both now run
// SetSizeCore, whose `init` arm is unconditional for every slot >= oldCount.)
extern "C" void Reimpl_Array_Resize_Stride08(void* arrayHeader, int newCount) {
    // 0x140001bbf: MOVSXD RBP,dword ptr [RCX + 0x8] is the first read -- no test of
    // RCX; every caller hands the address of a member array (B9 RT).
    // The erased resize core (ruled elem_stride, S4): Geometry+0x210 and friends are still raw headers.
    reinterpret_cast<Ptr8ZeroArray*>(arrayHeader)->SetSize(newCount);   // LEFT: erased core, a FLOOR ruling candidate (B4 Z LAST CODE): eight holders of five element types in two header types reach 0x140001bb0 (Collection::Array<T*>: MenuScene +0x40/+0x60, Tutorial steps, TutorialStep's three ExternalPtr arrays; RawArray: WorldGeometry::_batchMaterials, the +0x1E0 inner Vector2 outlines; AmbientEmitterDescription +0x28 raw triple); Array<T*>'s own SetSize leaves the tail unzeroed (trait row), so each typed wrapper would carry this same header cast  // CAST-OK: elem_stride (S4 ruling)
}

// 0x14001bf10 (stride 0x14, align 4, byte copy, zero tail): B.4 H5-P2: its last
// caller, MenuScene's +0x50 particles, is a typed Collection::Array<MenuSceneParticle>
// whose trait row is this one (stride 0x14, align 4, trivially copyable, no destroy,
// init = value-initialisation, which zero-fills the 0x14 bytes); it calls SetSize, and
// Reimpl_Array_Resize_Stride14 is deleted. (GeometryBatch::vertices, its other
// holder, grows through 0x14001ee40 in retail: Array<GeometryBatchVertex>::SetSize.)

// ============================================================
// Spawn-entry array resize — FUN_14000c520 (stride 0x10)
// ============================================================
// The growable array behind SpawnDescription+0x20: elements are
// { ExternalPtr @+0x00 (8B), float minSpawnAge @+0x08, float maxSpawnAge @+0x0c }.
// It cannot use GenericArrayResize because a NEW element is not zero-filled: the
// original writes ptr=0, min=0.0f, and max = 0x41200000 = **10.0f** (see the
// `*(undefined4*)((longlong)puVar9 + 0xc) = 0x41200000` stores in all four
// unrolled arms and the scalar tail of the decompile). That 10.0f is a real
// default — a spawn entry with no authored max age is eligible up to age 10 —
// so zero-filling instead would give every defaulted entry the window [0,0] and
// silently disable it.
//
// ⚠ This address was implemented in spawndescription.cpp but never registered,
// which is why SpawnDescription::Serialize read the entry count into a LOCAL,
// found `resize` null, and left the description at count=0 with a null array:
// every tree carried an EMPTY spawn table, so no wild food could ever spawn and
// the Gatherer's Hut had nothing it accepts anywhere on the map. Registered in
// address_registry.cpp alongside the other resize variants.
extern "C" void Reimpl_Array_Resize_Stride10_Spawn(SpawnDescription::EntryArray* arrayHeader, int newCount) {
    if (!arrayHeader) return;
    arrayHeader->SetSize(newCount);   // SpawnDescription+0x20, typed (its spec is kSpec_SpawnEntry)
}

// Helper #6 -- 0x14001bbf0 is IO::Serializer::Serialize<MenuSceneMesh> (src/engine/io/Serializer.h)
// over MenuSceneMesh::Serialize (MenuScene.h); MenuScene_Slot5 calls it (B9 RT2).

// (Helpers #7 / #8, the four Array_Serialize_External bodies, are
// Object::SerializeExternalArray<T> at their callers since B8 RT2.)

// ============================================================
// STANDALONE_EXE: address registry hooks
// ============================================================
//
// In standalone we do NOT have the original exe's code mapped. The
// fn_Geometry_Init / fn_Array_Resize_* / etc. function pointers above stay
// null when ResolveAddr can't find them, and the Reimpl_* fallbacks above
// kick in. To wire the registry up, see standalone/address_registry.cpp.
