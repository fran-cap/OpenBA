// ============================================================================
// tests/unit/link_stubs.cpp — the symbols the classes under test reference but do
// not own (ENDGAME B.3.1 step 1)
// ============================================================================
// `System::Filename::GetPathExtension` forwards to the C body in
// src/engine/io/filename_extension.cpp (linked since 2026-10-17; it used to sit in
// external_path.cpp, which would drag in ExternalFactory, the package system and
// Win32, so a null stub stood in here and filename_test.cpp could not cover the
// member). The body counts into that lane's `RtObject::g_pc`, whose definition is
// external_path.cpp's; the unit's copy is below.
// ============================================================================
#include "system/Filename.h"
#include "io/Modification.h"
#include "save/SaveLoad.h"
#include "graphics/Scene.h"
#include "graphics/Animation.h"
#include "graphics/GraphicsMesh.h"   // B.4 Y RT: kSpec_MeshNode
#include "graphics/AnimationState.h" // B.4 Z: kSpec_AnimLayer
#include "object/external_path.h"    // RtObject::PathCounters

#include <cstdio>
#include <cstdlib>

namespace RtObject {
PathCounters g_pc = {};   // external_path.cpp's lane counters (that file is not linked)
}  // namespace RtObject

// ============================================================================
// B3R RT-Core: the element-op bodies src/engine/collection/array.cpp and
// array_destroy.cpp now call BY NAME, whose translation units (io/, save/,
// graphics/) a host test does not link. Nothing in the unit build drives the six
// Array specs that reach them (Array<Array<uint8>>, Array<Save::MetaData>,
// Array<Graphics::DrawGroup>, Array<Graphics::Animation>); before B3R the same
// calls went through the stub ResolveAddr, which answered null. Each stub
// aborts loudly rather than faking a body, so a future test that does drive
// one of those specs has to link the real translation unit.
// ============================================================================
namespace {
[[noreturn]] void UnlinkedBody(const char* what) {
    std::fprintf(stderr, "[unit] %s is not linked into the unit build; see link_stubs.cpp.\n", what);
    std::abort();
}
}  // namespace

namespace RtIO {
void* RtColl_ArrayU8Assign_180011220(Collection::Array<uint8_t>*, const Collection::Array<uint8_t>*) { UnlinkedBody("RtColl_ArrayU8Assign_180011220"); }
}  // namespace RtIO
extern "C" void* RtGraphics_DrawGroup_CopyCtor(void*, const void*) { UnlinkedBody("RtGraphics_DrawGroup_CopyCtor"); }
extern "C" void* RtGraphics_Animation_CopyCtor(void*, const void*) { UnlinkedBody("RtGraphics_Animation_CopyCtor"); }
extern "C" void  RtGraphics_Animation_Dtor(void*) { UnlinkedBody("RtGraphics_Animation_Dtor"); }
Save::SaveLoad::SaveInfo::~SaveInfo() { UnlinkedBody("Save::SaveLoad::SaveInfo::~SaveInfo"); }
// B.4 S4 GFX: kSpec_MetaData58's element op names the copy ctor directly now
// (the RtSave_MetaData_CopyCtor door is gone); the ctor's member set names
// ModInfo's copy ctor and dtor in turn.
Save::SaveLoad::SaveInfo::SaveInfo(const SaveInfo&) { UnlinkedBody("Save::SaveLoad::SaveInfo::SaveInfo(const SaveInfo&)"); }
Save::SaveLoad::ModInfo::ModInfo(const ModInfo&) { UnlinkedBody("Save::SaveLoad::ModInfo::ModInfo(const ModInfo&)"); }
Save::SaveLoad::ModInfo::~ModInfo() { UnlinkedBody("Save::SaveLoad::ModInfo::~ModInfo"); }

// B.4 Y RT: `Graphics::GraphicsMesh::_nodes` is `NodeArray`, whose ~Array names
// the hand-derived row `rtcoll::gfxanim::kSpec_MeshNode`
// (src/engine/collection/array_graphics_anim.cpp, not linked into the unit build:
// it pulls in the graphics element copy ctors). The GraphicsMesh probe's inline
// ctor references it from its cleanup path only; no test sizes or destroys a
// node array, so the row's operations abort if one ever does.
namespace {
void UnlinkedNodeAssign(uint8_t*, const uint8_t*) { UnlinkedBody("kSpec_MeshNode assign"); }
void UnlinkedNodeInit(uint8_t*) { UnlinkedBody("kSpec_MeshNode init"); }
void UnlinkedNodeDestroy(void*, uint8_t*, int) { UnlinkedBody("kSpec_MeshNode destroy"); }
}  // namespace
namespace rtcoll { namespace gfxanim {
const ArraySpec kSpec_MeshNode = {
    "Array<GraphicsMesh::Node> (unit stub)", 0x48, 8,
    &UnlinkedNodeAssign, &UnlinkedNodeInit, &UnlinkedNodeDestroy, false,
    0, 0, 0, 0, 0, 0 };
} }  // namespace rtcoll::gfxanim

// B.4 Z MIRROR2 GFX: `Graphics::AnimationState::_layers` (MeshInstance::_animation)
// is on the hand-derived row `kSpec_AnimLayer`, same file, same reason. Its
// destroy step is null in the real row too (J1 only), so a MeshInstance probe's
// ~Array frees exactly as the real build does; assign / init abort.
namespace {
void UnlinkedLayerAssign(uint8_t*, const uint8_t*) { UnlinkedBody("kSpec_AnimLayer assign"); }
void UnlinkedLayerInit(uint8_t*) { UnlinkedBody("kSpec_AnimLayer init"); }
}  // namespace
namespace rtcoll { namespace gfxanim {
const ArraySpec kSpec_AnimLayer = {
    "Array<Graphics::AnimationState::Layer> (unit stub)", 0x38, 8,
    &UnlinkedLayerAssign, &UnlinkedLayerInit, nullptr, true,
    0, 0, 0, 0, 0, 0 };
// `MeshInstance::_nodes` / `InstancedAnimationState::_nodes`: kSpec_InterpTransform,
// likewise (real row: stride 0x38, alignment 4, no destroy step).
const ArraySpec kSpec_InterpTransform = {
    "Array<Graphics::InterpolatedState<Math::Transform>> (unit stub)", 0x38, 4,
    &UnlinkedLayerAssign, &UnlinkedLayerInit, nullptr, true,
    0, 0, 0, 0, 0, 0 };
} }  // namespace rtcoll::gfxanim

// ============================================================================
// System::Random is the REAL class now (2026-10-17): src/engine/system/value_types.cpp
// and timer.cpp are linked (sources.cmake), so the time-seeded ctor every by-value
// Object::InternalFactory runs, the seeded ctor and every Rand overload are retail's
// MT19937 (tests/unit/random_test.cpp pins the known answers). Until then this file
// carried an empty Random() and a xorshift Random(seed) / Rand(bound) for the erosion
// tests, which compare two bodies under one generator and pass under either.
//
// `TerrainHeightfield::paths`' row kSpec_WgPathVec (worldgen_f390_coastline.cpp, not
// linked) is referenced by the record's Array member; the erosion tests never size it.
// ============================================================================
#include "system/Value.h"
#include "Game/TerrainHeightfield.h"
namespace {
void UnlinkedWgPathAssign(uint8_t*, const uint8_t*) { UnlinkedBody("kSpec_WgPathVec assign"); }
void UnlinkedWgPathInit(uint8_t*) { UnlinkedBody("kSpec_WgPathVec init"); }
void UnlinkedWgPathDestroy(void*, uint8_t*, int) { UnlinkedBody("kSpec_WgPathVec destroy"); }
}  // namespace
const rtcoll::ArraySpec kSpec_WgPathVec = {
    "Array<Array<PathPoint>> (unit stub)", 0x10, 8,
    &UnlinkedWgPathAssign, &UnlinkedWgPathInit, &UnlinkedWgPathDestroy, false,
    0, 0, 0, 0, 0, 0 };

// ============================================================================
// Block STILLTERRAIN (tests/unit/still_terrain_test.cpp): src/game/Game/mapquery.cpp is
// linked so the test drives the real Map::GetTerrainPos over a WorldGeometry record it
// builds. The body reads the world through `GameUpdateLoop::_singleton` (the class
// static, defined in gameupdateloop.cpp, which the unit does not link: the test points
// it at a zeroed 0x160-byte record and clears it after) and ray-casts the map's
// Picking::Scene when a cell carries a floor (scene.cpp, not linked: the test keeps
// `m_game` null, so the floor branch is never reached; the stub aborts if it is).
// ============================================================================
#include "Game/GameUpdateLoop.h"
#include "picking/Scene.h"
GameUpdateLoop* GameUpdateLoop::_singleton = nullptr;
int32_t Picking::Scene::Intersect(const Math::Vector3&, const Math::Vector3&, const Math::Scalar&,
                                  Math::Vector3&, Math::Vector3&) const {
    UnlinkedBody("Picking::Scene::Intersect");
}

// ============================================================================
// package_file_read_test.cpp: src/engine/io/package_file_read.cpp is linked so the
// test drives the real ReadPackfile. Its teardown names CompressedStream::Commit
// (compressed_stream_write.cpp, not linked) behind `_direction == 1`, which a read
// never sets (CompressedStream::Open stores 0); the stub aborts if it is reached.
// ============================================================================
#include "io/Stream.h"
void IO::CompressedStream::Commit() {
    UnlinkedBody("IO::CompressedStream::Commit");
}
