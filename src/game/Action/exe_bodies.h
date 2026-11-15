// src/game/Action/exe_bodies.h
//
// Declarations of the C bodies in src/game/Action/ that were reached through the address
// registry (RegisterExeAddr rows, the readers' ResolveAddr(Module::Exe, ...) / helper /
// X_ResolveFuncs / engine_funcs doors), or through a local extern prototype in the caller.
// B3R_RULES R5: every DIRECT body is declared in exactly one header, written from its
// DEFINITION (linkage included), and each defining .cpp includes this header so the
// compiler checks the pair. Readers call these by name through this header (R2).
//
// Grouped by defining file; the retail address is the trailing comment on each line.
// `uint` in a definition (a file-local typedef there) is spelled `unsigned int` here.
// The address -> name table is docs/history/endgame_b/b3r_session/data/P0a_declared.tsv.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "collection/Collection.h"   // Collection::Array
#include "math/Math.h"               // Math::Vector3 (Vector3Slot's member)

// ---- action_base_cnr.cpp
class Map;                    // Game/Map.h
class GrowthComponent;        // Component/GrowthComponent.h
class ModelComponent;         // Component/ModelComponent.h
class NaturalResourceComponent;   // Component/NaturalResourceComponent.h
class ComponentEntity;        // Entity/ComponentEntity.h

// ---- action_base_complete.cpp
class CitizenComponent;       // Component/CitizenComponent.h
class WorkScheduler;          // Game/WorkScheduler.h

// ---- action_base_hunt.cpp
class HuntComponent;          // Component/HuntComponent.h
class ComponentEntity;        // Entity/ComponentEntity.h
template <class T> struct RawArray;   // Action/PathSearch.h
// 0x140003110 (QwordArraySetSize until B9 RT) is Collection::Array<InternalPtr<T>>::SetSize
// over kSpec_PtrZero: callers call the member.

// ---- action_base_move.cpp
class CharacterComponent;     // Component/CharacterComponent.h
class CitizenComponent;       // Component/CitizenComponent.h
class ComponentEntity;        // Entity/ComponentEntity.h
namespace Math { class Vector3; }
class CharacterDescription;   // Component/CharacterDescription.h
class MoveTo;                 // Action/MoveTo.h
struct ActionStack;           // Action/Action.h
namespace IO { class Serializer; }

// ---- action_base_movedeps.cpp
class ModelComponent;         // Component/ModelComponent.h
namespace System { class Time; class Random; }
bool BreakTimeCheck(System::Time* time, const float* pair, System::Random* rng);  // 0x1400a9290  // B8: free in retail -- RCX is the System::Time it tests (a Runtime class; 0x140049021's caller accumulates it), an exe-side helper

// ---- action_base_rawmat.cpp
class AutoPickupComponent;    // Component/AutoPickupComponent.h
class CitizenBehavior;        // Entity/CitizenBehavior.h
class Component;              // Component/Component.h
class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h
class StorageComponent;       // Component/StorageComponent.h
class TrackerComponent;       // Component/TrackerComponent.h
int ResourceCategory(const ComponentDescription* material);  // 0x140088f90  // B8: stays free -- free in retail: 0x140088f90 reads the material from RDX (0x140088faa MOV RBX,RDX) and never reads RCX, which its callers leave unset (0x14010806c: the storage item array)
// 0x14000da70 Array<T>::SetSize / 0x14000d9d0 Array<T>::operator= over a 16-byte
// {material, count, pad} element: ONE retail body each serving four element types
// (TrackerComponent::Entry, ConsumeRawMaterial::Entry, PickupStoredRawMaterial::Entry,
// MarketWork::Carry). The core works on its own element view; callers use the typed
// wrappers MaterialArraySetSize / MaterialArrayAssign over their {data, count, capacity}
// holder.
template <class T> struct RawArray;   // Action/PathSearch.h
struct MaterialCount16 {               // the core's own 16-byte element view
    ComponentDescription* material;    // +0x00 ExternalPtr<ComponentDescription>
    int32_t               count;       // +0x08
    int32_t               pad0C;       // +0x0C (the SetSize tail leaves it unwritten)
};
static_assert(sizeof(MaterialCount16) == 0x10, "16-byte stride (0x14000da70)");
static_assert(offsetof(MaterialCount16, material) == 0x0, "MaterialCount16::material");
static_assert(offsetof(MaterialCount16, count) == 0x8, "MaterialCount16::count");
static_assert(offsetof(MaterialCount16, pad0C) == 0xC, "MaterialCount16::pad0C");
void ActRm_ArrayResize16_da70(RawArray<MaterialCount16>* arr, int newCount);  // 0x14000da70  // B9: stays free -- erased core over holders that are NOT one Collection::Array: ConsumeRawMaterial::EntryList / PickupStoredRawMaterial::EntryList / MarketWork::CarryList are POD {data, count, capacity} triples (H5), TrackerComponent::EntryList is Collection::Array<Entry, kSpec_Fill16>; and no row exists for 0x14000da70: stride 0x10, MemAlloc(cap<<4, 8), two-qword copy, init `MOV [R8],R11 / MOV dword [R8+8],R11D` (bytes +0x0c..+0x0f unwritten) -- kSpec_Fill16 has no init arm, so TrackerComponent's own SetSize would leave the tail unwritten; moving needs that row and the three triples typed (B9 RT)
void ActRm_VectorClone_d9d0(RawArray<MaterialCount16>* dst, const RawArray<MaterialCount16>* src);  // 0x14000d9d0  // B9: stays free -- Array<T>::operator= (0x14000d9d0: CMP RCX,RDX; CALL [MemFree] on the old data; one qword zero of size+capacity; MemAlloc(n<<4, 8); two-qword copy) over the same holders as 0x14000da70: three POD triples and one Collection::Array<Entry, kSpec_Fill16> (whose Assign over ArrayAssignCore IS this body); moves with the triples typed (B9 RT)
template <class Holder>
inline void MaterialArraySetSize(Holder* arr, int newCount) {
    static_assert(sizeof(Holder) == 0x10, "an Array<16-byte element> {data, count, capacity}");
    ActRm_ArrayResize16_da70(reinterpret_cast<RawArray<MaterialCount16>*>(arr), newCount);  // CAST-OK: elem_stride
}
template <class Holder>
inline void MaterialArrayAssign(Holder* dst, const Holder* src) {
    static_assert(sizeof(Holder) == 0x10, "an Array<16-byte element> {data, count, capacity}");
    ActRm_VectorClone_d9d0(reinterpret_cast<RawArray<MaterialCount16>*>(dst),  // CAST-OK: elem_stride
                           reinterpret_cast<const RawArray<MaterialCount16>*>(src));  // CAST-OK: elem_stride
}

// ---- action_child_ctors.cpp
class ComponentEntity;        // Entity/ComponentEntity.h
float HeadingToward(const float* worldPos, ComponentEntity* const* entityIBP);  // 0x1400c29a0  // B8: free in retail -- RCX is the world-position pointer (0x1400baaf9 MOV RCX,[RBP+0x77]), a float vector

// ---- action_child_ctors2.cpp
class ConsumeProduceComponent;   // Component/ConsumeProduceComponent.h

// ---- action_pf_astar.cpp

// ---- action_pf_avl.cpp
struct PathGraph;
extern "C" void PathPairs_Clear_25b30(PathGraph* pg);  // 0x140025b30  // B9: stays free -- the holder is not a Collection::Array: retail 0x140025b30 is Array<PathCellPair>::SetSize(0) constant-folded, ONE argument (0x140025b3a MOVSXD RBP,[RCX+8] / TEST EBP,EBP / JZ; MOV [RCX+8],EDI with EDI 0; the dead grow arm MemAlloc(0, 4) and the -1 fill), over PathGraph's raw {m_pairs, m_pairCount, m_pairCapacity} triple (Entity/CitizenBehavior.h, not RT's file); its whole effect is `size = 0`; it is that Array's Clear() once the triple is typed Collection::Array<PathCellPair> (B9 RT)

// ---- action_pf_ctors.cpp
struct ActionStack;
class Component;              // Component/Component.h
class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h
class InteractComponent;      // Component/InteractComponent.h
class StorageComponent;       // Component/StorageComponent.h
template <class T> struct RawArray;   // Action/PathSearch.h

// ---- action_pf_geom.cpp (Pf_RayTriIntersect_aa810: Game/worldgeometry_ray.cpp)
int Pf_RayTriIntersect_aa810(void*, const float* P, const float* D, const float* maxT, const float* V0, const float* V1, const float* V2, float* outT, float* outNormal);  // 0x1400aa810  // B8: free in retail -- RCX dead: its four calls (0x1400aac26..0x1400aacd6) never load it
int Pf_TileShapeLut_ae180(void*, char p2, char p3, char p4, char p5, char p6, char p7, char p8, char p9);  // 0x1400ae180  // B8: free in retail -- RCX dead: at 0x1400ad355 the caller leaves a byte in CL (its stack argument 5)
class PopulationComponent;    // Component/PopulationComponent.h

// ---- action_pf_pools.cpp
// (0x14001ee40 / 0x14001f8d0 / 0x14001fa80, the GeometryBatch arrays' SetSize forms,
// are Collection::Array<T>::SetSize on the typed members: Game/WorldGeometryElements.h.)
struct GeometryBatchVertex;   // Game/WorldGeometryElements.h

// ---- action_pf_region.cpp

// ---- action_pf_road.cpp
class WorldGeometry;
// The caller's slot for a by-value Math::Vector3 result that retail leaves UNCONSTRUCTED.
// 0x140122e20 returns a Vector3 through the hidden return slot (RDX) and writes x, y, z on
// every path, so its retail callers hand it bare stack (e.g. 0x14010d9ab, 0x1400e52d3). A
// Math::Vector3 local would run the zeroing default ctor (0x180015070: three stores);
// this holds the Vector3 without constructing it (the anonymous union only suppresses
// the member's ctor; nothing is read as another type). `&slot.v` is the Math::Vector3*.
// The ctor is always_inline so -O0 emits no call for it.
struct Vector3Slot {
    union { Math::Vector3 v; };
    [[gnu::always_inline]] Vector3Slot() {}
};
static_assert(sizeof(Vector3Slot) == sizeof(Math::Vector3), "Vector3Slot is exactly a Vector3");
int EntityAnchorCell(ComponentEntity* const* entityIBP, float* outPos);  // 0x140087fb0  // B8: free in retail -- RCX is the address of an InternalPtr slot (0x140111988 LEA RCX,[R11+0x10]), not an object

// ---- action_pf_roots_drop.cpp
class AnimalDescription;      // Component/AnimalDescription.h
class CitizenComponent;       // Component/CitizenComponent.h
class DropOffRawMaterial;     // Action/DropOffRawMaterial.h
struct ActionStack;           // Action/Action.h
void RemoveAnimalActions(CitizenComponent* const* citizenIBP, uint32_t task, float* destination, uint32_t createFlag, Component* const* animal, const AnimalDescription* description, uint32_t gate);  // 0x1400bbea0  // B8: free in retail -- action factory: RCX is a stack InternalPtr slot (0x1400c816b LEA RCX,[RBP-0x31]), not an object

// ---- action_pf_roots_misc.cpp
class CharacterComponent;     // Component/CharacterComponent.h
class CharacterGrid;          // Component/CharacterGrid.h
class HerdComponent;          // Component/HerdComponent.h
namespace Math { class Scalar; }

// ---- action_pf_roots_road.cpp

// ---- action_pf_roots_route.cpp
namespace Object { template <class T> class InternalPtr; }

// ---- keystone_a_zone.cpp
class PathfinderThread;       // Game/PathfinderThread.h
namespace Object { template <class T> class InternalPtr; }
namespace System { class String; }

// ---- region_graph_build.cpp

// ---- region_paircache.cpp
struct RegionPairCache;   // Action/PathSearch.h
extern "C" RegionPairCache* Pf_RegionPairCache_Build();  // no retail address (the tail of 0x14009e8c0; caller src/game/Game/region_seed.cpp)  // B9: harness / port-only -- read: no retail symbol; 0x14009e8c0 builds the 0x38-byte cache in place (MemAlloc(0x38, 4), the field init, the 0x20000-entry ring resize 0x14001cca0), region_paircache.cpp's port of that snippet; region_seed.cpp (the standalone's region-map seeding) is its one caller
