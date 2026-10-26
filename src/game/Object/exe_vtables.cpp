// src/game/Object/exe_vtables.cpp -- the compiler-vtable aliases, kept out of LTO.
//
// `Class::Vtable()` answers the address point of the COMPILER's vtable for a final class
// (B38_RULES D5): the walk prefetch tells a CharacterComponent / ModelComponent /
// ParticleComponent by its vptr (entitylist.cpp), ext.walk_batch tells a MoveTo top node
// (walk_batch.cpp), the splash tells "is the next loop a GameUpdateLoop"
// (splash_loop_stubs.cpp), game_main / newgamedialog stamp the loop objects, and
// src/app/main.cpp's factory self-test compares a factory-built StatGoal's vptr with it.
//
// DEFINE_EXE_VTABLE (exe_type.h) reaches the table by declaring an array with the vtable's
// own assembler name (`_ZTV...`): C++ has no way to name a vtable. Under link-time
// optimisation GCC sees that declaration beside the type whose table it is and reports it
// as a one-definition-rule violation (-Wodr, nine of them, 2026-10-26), and an instrumented
// LTO link dies on it ("missing resolution data for _ZTV18CharacterComponent"). So every
// alias lives here, in the one translation unit CMakeLists.txt compiles WITHOUT -flto: the
// linker resolves the name against the table the LTO objects emit, as it always did, and
// the optimiser never sees two owners of one symbol. The engine-side twin for
// Graphics::MeshInstance is src/engine/graphics/mesh_vtable_alias.cpp.
//
// Each class's own .cpp keeps the comment that says who reads its Vtable().

#include "Object/exe_type.h"
#include "Action/MoveTo.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ParticleComponent.h"
#include "Game/GameUpdateLoop.h"
#include "Game/MenuDialogLoopT.h"
#include "Game/GameLoadDialogLoopT.h"
#include "Goal/StatGoal.h"

DEFINE_EXE_VTABLE(MoveTo, "_ZTV6MoveTo")
DEFINE_EXE_VTABLE(CharacterComponent, "_ZTV18CharacterComponent")
DEFINE_EXE_VTABLE(ModelComponent, "_ZTV14ModelComponent")
DEFINE_EXE_VTABLE(ParticleComponent, "_ZTV17ParticleComponent")
DEFINE_EXE_VTABLE(GameUpdateLoop, "_ZTV14GameUpdateLoop")
DEFINE_EXE_VTABLE(MenuDialogLoopT, "_ZTV15MenuDialogLoopT")
DEFINE_EXE_VTABLE(GameLoadDialogLoopT, "_ZTV19GameLoadDialogLoopT")
DEFINE_EXE_VTABLE(StatGoal, "_ZTV8StatGoal")
