// ============================================================================
// tests/unit/loop_abi_test.cpp — ENDGAME B.3.8 block LOOP (B38_RULES.md B8-14)
//
// The ABI claims block LOOP makes for its classes, measured on the SHIPPED
// HEADERS (src/game/Game/<Class>.h):
//
// PART A — LAYOUT. sizeof / alignof against each retail create site's MemAlloc
//   and the member offsets the ported bodies read.
// PART B — SHAPE. Every class is concrete, `final`, single inheritance from its
//   proved Runtime base, polymorphic, a virtual destructor (B3X W1; the
//   PathfinderThread, a System::Thread, has none).
// PART C — SLOT ORDER, read off pointers to VIRTUAL members (Itanium PMF =
//   1 + vtable byte offset, adjustment 0): each override lands on the retail
//   index of B38_SLOTS.md sec. 6 / B37_RULES sec. 3.1, and slot 0 is
//   the destructor (B3X W1: D1 + D0).
//
// NOT PROVEN HERE: the bodies (they stand on the address registry and cannot
// link into a host test; rb_engine_abi_test.cpp proves the loop bases' ctor/dtor
// stamp order through stand-in subclasses).
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include "Game/InnerLoaderLoop.h"
#include "Game/GameLoaderLoop.h"
#include "Game/GameLoadDialogLoopT.h"
#include "Game/MenuDialogLoopT.h"
#include "Game/GameUpdateLoop.h"
#include "Game/ClimateGenerator.h"
#include "Game/Disease.h"
#include "Game/MapGenerator.h"
#include "Game/MenuScene.h"
#include "Game/MusicList.h"
#include "Game/NameDatabase.h"
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "Game/TerrainGenerator.h"
#include "Game/Toolbar.h"
#include "Game/Weather.h"
#include "Game/WorldGeometry.h"
#include "Game/WeatherParticleGeometry.h"
#include "Game/PathfinderThread.h"
#include "Game/Tornado.h"
#include "graphics/Material.h"
#include "ui/SpriteSheet.h"
#include "video/ImageBuffer.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

// ===========================================================================
// PART A — LAYOUT
// ===========================================================================
UNIT_TEST(loop_sizes_match_retail) {
    CHECK_EQ(alignof(InnerLoaderLoop), (size_t)8);
    CHECK_EQ(alignof(GameLoaderLoop),  (size_t)8);
    CHECK_EQ(alignof(GameLoadDialogLoopT), (size_t)8);
    CHECK_EQ(alignof(MenuDialogLoopT), (size_t)8);
    CHECK_EQ(alignof(GameUpdateLoop), (size_t)8);
}

// ===========================================================================
// PART B — SHAPE
// ===========================================================================
UNIT_TEST(loop_classes_shape) {
    CHECK((std::is_base_of<Engine::LoaderLoop, InnerLoaderLoop>::value));
    CHECK((std::is_base_of<Engine::LoaderLoop, GameLoaderLoop>::value));
    CHECK(!std::is_abstract<InnerLoaderLoop>::value);
    CHECK(!std::is_abstract<GameLoaderLoop>::value);
    CHECK(std::is_final<InnerLoaderLoop>::value);
    CHECK(std::is_final<GameLoaderLoop>::value);
    CHECK(std::has_virtual_destructor<InnerLoaderLoop>::value);
    CHECK(std::has_virtual_destructor<GameLoaderLoop>::value);
    CHECK((std::is_base_of<Engine::DialogLoaderLoopT, GameLoadDialogLoopT>::value));
    CHECK(!std::is_abstract<GameLoadDialogLoopT>::value);
    CHECK(std::is_final<GameLoadDialogLoopT>::value);
    CHECK(std::has_virtual_destructor<GameLoadDialogLoopT>::value);
    CHECK((std::is_base_of<Engine::DialogLoopT, MenuDialogLoopT>::value));
    CHECK(!std::is_abstract<MenuDialogLoopT>::value);
    CHECK(std::is_final<MenuDialogLoopT>::value);
    CHECK(std::has_virtual_destructor<MenuDialogLoopT>::value);
    CHECK((std::is_base_of<Engine::UpdateLoop, GameUpdateLoop>::value));
    CHECK(!std::is_abstract<GameUpdateLoop>::value);
    CHECK(std::is_final<GameUpdateLoop>::value);
    CHECK(std::has_virtual_destructor<GameUpdateLoop>::value);
}

// ===========================================================================
// PART C — SLOT ORDER
// ===========================================================================
UNIT_TEST(loop_loaderloop_slots) {
    // 0x140158870: own [0], [6], [10].
    CHECK_DTOR_SLOT(InnerLoaderLoop);
    CHECK_SLOT(InnerLoaderLoop, Load,                6);
    CHECK_SLOT(InnerLoaderLoop, GetResourceFilename, 10);
    // 0x1401588d0: own [0] only.
    CHECK_DTOR_SLOT(GameLoaderLoop);
    // Both tables are 12 slots: the last is AllowsModChanges, UpdateLoop's.
    CHECK_SLOT(Engine::UpdateLoop, AllowsModChanges, 11);
}

UNIT_TEST(loop_splash_slots) {
    // 0x140158800: own [0], [1], [2], [3], [12].
    CHECK_DTOR_SLOT(GameLoadDialogLoopT);
    CHECK_SLOT(GameLoadDialogLoopT, Startup,          1);
    CHECK_SLOT(GameLoadDialogLoopT, Shutdown,         2);
    CHECK_SLOT(GameLoadDialogLoopT, Update,           3);
    CHECK_SLOT(GameLoadDialogLoopT, CreateLoopDialog, 12);
}

UNIT_TEST(loop_menu_slots) {
    // 0x14015ed00: own [0]-[4], [6], [8]-[12]; [5] and [7] are UpdateLoop's.
    CHECK_DTOR_SLOT(MenuDialogLoopT);
    CHECK_SLOT(MenuDialogLoopT, Startup,             1);
    CHECK_SLOT(MenuDialogLoopT, Shutdown,            2);
    CHECK_SLOT(MenuDialogLoopT, Update,              3);
    CHECK_SLOT(MenuDialogLoopT, Draw,                4);
    CHECK_SLOT(MenuDialogLoopT, Load,                6);
    CHECK_SLOT(MenuDialogLoopT, OnVideoError,        8);
    CHECK_SLOT(MenuDialogLoopT, OnVideoRestart,      9);
    CHECK_SLOT(MenuDialogLoopT, GetResourceFilename, 10);
    CHECK_SLOT(MenuDialogLoopT, AllowsModChanges,    11);
    CHECK_SLOT(MenuDialogLoopT, CreateLoopDialog,    12);
}

UNIT_TEST(loop_gameupdateloop_slots) {
    // 0x14015e2f0: own [0]-[10]; [11] AllowsModChanges is UpdateLoop's.
    CHECK_DTOR_SLOT(GameUpdateLoop);
    CHECK_SLOT(GameUpdateLoop, Startup,             1);
    CHECK_SLOT(GameUpdateLoop, Shutdown,            2);
    CHECK_SLOT(GameUpdateLoop, Update,              3);
    CHECK_SLOT(GameUpdateLoop, Draw,                4);
    CHECK_SLOT(GameUpdateLoop, DrawFailed,          5);
    CHECK_SLOT(GameUpdateLoop, Load,                6);
    CHECK_SLOT(GameUpdateLoop, SaveRestore,         7);
    CHECK_SLOT(GameUpdateLoop, OnVideoError,        8);
    CHECK_SLOT(GameUpdateLoop, OnVideoRestart,      9);
    CHECK_SLOT(GameUpdateLoop, GetResourceFilename, 10);
    CHECK_SLOT(GameUpdateLoop, AllowsModChanges,    11);   // inherited
}

// The retslot slot 12: RCX = this, RDX = the caller's InternalPtr slot, RAX =
// the slot back (B6-2). Declared type checked; the body is not linked here.
UNIT_TEST(loop_splash_slot12_is_retslot_first) {
    using Ret = Object::InternalPtr<UI::DialogInstance>;
    CHECK((std::is_same<decltype(&GameLoadDialogLoopT::CreateLoopDialog),
                        Ret* (GameLoadDialogLoopT::*)(Ret*)>::value));
    CHECK((std::is_same<decltype(&MenuDialogLoopT::CreateLoopDialog),
                        Ret* (MenuDialogLoopT::*)(Ret*)>::value));
}

// ===========================================================================
// PART D — THE BUILDERS' CONSTRUCTION (ENDGAME B.3.9 EXA)
// Every loop builder (mainmenudialog, newgamedialog, saveloaddialog, tutorialui,
// tutorial_next_scenario, worldsetup_reload, GameMain) writes
// `::new (MemAlloc(size, align)) X(args)` with exactly the argument shapes
// below — the ctor arguments of retail's 0x140098060 / 0x14009bb00 / the inlined
// splash ctor / ??0LoaderLoop. The expression is checked unevaluated (the ctor
// bodies do not link into a host test); a builder that needed a door or a
// by-address vtable stamp again would have to break one of these.
// ===========================================================================
template <typename T, typename... A>
using PlacementNewResult = decltype(::new (std::declval<void*>()) T(std::declval<A>()...));

template <typename, typename T, typename... A>
struct CanPlacementNew : std::false_type {};
template <typename T, typename... A>
struct CanPlacementNew<std::void_t<PlacementNewResult<T, A...>>, T, A...> : std::true_type {};

UNIT_TEST(loop_builders_construct_in_place) {
    CHECK((CanPlacementNew<void, GameUpdateLoop, Save::SavedState*,
                           const Object::InternalPtr<Achievement::AchievementInstance>&, int32_t,
                           const System::String&, int32_t, int32_t, bool,
                           MapGenerator*, TerrainGenerator*, ClimateGenerator*>::value));
    CHECK((CanPlacementNew<void, MenuDialogLoopT, const System::String&>::value));
    CHECK((CanPlacementNew<void, GameLoadDialogLoopT, const System::String&,
                           Engine::UpdateLoop*, const System::Time&>::value));
    CHECK((CanPlacementNew<void, GameLoaderLoop, Engine::UpdateLoop*,
                           const System::Time&>::value));
    // The builders chain the loops through Engine::UpdateLoop*: single
    // inheritance, so the base pointer IS the object's address (no adjustment).
    alignas(8) unsigned char raw[16] = {};
    auto* g = reinterpret_cast<GameUpdateLoop*>(raw);
    CHECK_EQ(reinterpret_cast<uintptr_t>(static_cast<Engine::UpdateLoop*>(g)),
             reinterpret_cast<uintptr_t>(raw));
    auto* d = reinterpret_cast<GameLoadDialogLoopT*>(raw);
    CHECK_EQ(reinterpret_cast<uintptr_t>(static_cast<Engine::UpdateLoop*>(d)),
             reinterpret_cast<uintptr_t>(raw));
    auto* m = reinterpret_cast<MenuDialogLoopT*>(raw);
    CHECK_EQ(reinterpret_cast<uintptr_t>(static_cast<Engine::UpdateLoop*>(m)),
             reinterpret_cast<uintptr_t>(raw));
}

// The exe's three local table copies (0x14015c400 MaterialInstance, 0x14015c3b0
// SpriteSheet, 0x14015c8c0 ImageBuffer) are the Runtime classes' own tables
// (Ghidra: slot 0 the exe scalar deleting dtor, 1..n thunks onto the class's
// own members), so the thumbnail / minimap builders write `::new (p) X()` at
// retail's MemAlloc size. ENDGAME B.3.9 EXA.
UNIT_TEST(exe_table_copies_are_the_runtime_classes) {
    CHECK((CanPlacementNew<void, Graphics::MaterialInstance>::value));
    CHECK((CanPlacementNew<void, UI::SpriteSheet>::value));
    CHECK((CanPlacementNew<void, Video::ImageBuffer>::value));
    CHECK(!std::is_abstract<Graphics::MaterialInstance>::value);
    CHECK(!std::is_abstract<UI::SpriteSheet>::value);
    CHECK(!std::is_abstract<Video::ImageBuffer>::value);
}

// ===========================================================================
// THE ELEVEN REFLECTED WORLD EXTERNALS (Object::External, 9 retail slots)
// ===========================================================================
template <typename T>
void CheckExternalShape() {
    CHECK((std::is_base_of<Object::External, T>::value));
    CHECK(!std::is_abstract<T>::value);
    CHECK(std::is_final<T>::value);
    CHECK(std::has_virtual_destructor<T>::value);
    CHECK_EQ(alignof(T), (size_t)8);
    CHECK_DTOR_SLOT(T);
    CHECK_SLOT(T, TypeName,       1);
    CHECK_SLOT(T, Type,           2);
    CHECK_SLOT(T, GetFileVersion, 3);
    CHECK_SLOT(T, Compile,        4);
    CHECK_SLOT(T, Serialize,      5);
    CHECK_SLOT(T, Reconstruct,    6);
    CHECK_SLOT(T, Destruct,       7);
    CHECK_SLOT(T, Construct,      8);   // the class's own new virtual
}

UNIT_TEST(loop_externals_shape_and_slots) {
    CheckExternalShape<ClimateGenerator>();
    CheckExternalShape<Disease>();
    CheckExternalShape<MapGenerator>();
    CheckExternalShape<MenuScene>();
    CheckExternalShape<MusicList>();
    CheckExternalShape<NameDatabase>();
    CheckExternalShape<Profession>();
    CheckExternalShape<ProfessionList>();
    CheckExternalShape<TerrainGenerator>();
    CheckExternalShape<Toolbar>();
    CheckExternalShape<Weather>();
}

// ===========================================================================
// THE UNREFLECTED EXE Graphics::Geometry SUBCLASSES (0x14015f110, 0x140160240)
// ===========================================================================
template <typename T>
void CheckGeometryShape() {
    CHECK((std::is_base_of<Graphics::Geometry, T>::value));
    CHECK(!std::is_abstract<T>::value);
    CHECK(std::is_final<T>::value);
    CHECK(std::has_virtual_destructor<T>::value);
    CHECK_EQ(alignof(T), (size_t)8);
    CHECK_DTOR_SLOT(T);
    CHECK_SLOT(T, Update,                 1);
    CHECK_SLOT(T, Draw,                   2);
    CHECK_SLOT(T, AddDrawItems,           3);
    CHECK_SLOT(Graphics::Geometry, Clean, 8);   // the tables stop at 9
}

UNIT_TEST(loop_geometry_subclasses) {
    CheckGeometryShape<WorldGeometry>();
    CheckGeometryShape<WeatherParticleGeometry>();
}

// THE PATHFINDER System::Thread SUBCLASS (0x14015ef18, 2 slots)
UNIT_TEST(loop_pathfinder_thread) {
    CHECK((std::is_base_of<System::Thread, PathfinderThread>::value));
    CHECK(!std::is_abstract<PathfinderThread>::value);
    CHECK(std::is_final<PathfinderThread>::value);
    CHECK(!std::has_virtual_destructor<PathfinderThread>::value);
    CHECK_SLOT(PathfinderThread, Run,         0);
    CHECK_SLOT(PathfinderThread, OnCompleted, 1);   // inherited
}

// TORNADO (reflected Graphics::Particle, 10 slots) AND ITS INSTANCE (10 slots)
UNIT_TEST(loop_tornado_classes) {
    CHECK((std::is_base_of<Graphics::Particle, Tornado>::value));
    CHECK((std::is_base_of<Graphics::ParticleInstance, TornadoInstance>::value));
    CHECK(!std::is_abstract<Tornado>::value);
    CHECK(!std::is_abstract<TornadoInstance>::value);
    CHECK(std::is_final<Tornado>::value);
    CHECK(std::is_final<TornadoInstance>::value);
    CHECK(std::has_virtual_destructor<Tornado>::value);
    CHECK(std::has_virtual_destructor<TornadoInstance>::value);
    CHECK_DTOR_SLOT(Tornado);
    CHECK_SLOT(Tornado, TypeName,       1);
    CHECK_SLOT(Tornado, Type,           2);
    CHECK_SLOT(Tornado, GetFileVersion, 3);
    CHECK_SLOT(Tornado, Compile,        4);
    CHECK_SLOT(Tornado, Serialize,      5);
    CHECK_SLOT(Tornado, Reconstruct,    6);
    CHECK_SLOT(Tornado, Destruct,       7);
    CHECK_SLOT(Tornado, CreateSystem,   8);
    CHECK_SLOT(Tornado, Construct,      9);
    CHECK_DTOR_SLOT(TornadoInstance);
    void (TornadoInstance::*upd)(const System::Time&) = &TornadoInstance::Update;
    CHECK_PMF_SLOT(upd, 1);
    CHECK_SLOT(Graphics::ParticleInstance, SetParticle, 9);
}

}  // namespace
