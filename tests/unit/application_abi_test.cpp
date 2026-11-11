// ============================================================================
// tests/unit/application_abi_test.cpp — ENDGAME B.3.7 block B1
//   Application::Application as a real, NON-polymorphic class (B7-10).
//
// PART A — size / shape: 0x30 (`MemAlloc(0x30)` in Engine::Startup
//   0x1800dbbb3), no vtable, not default-constructible (the ctor is the door
//   `Ctor_Application`), and `RtApplication::Application` IS the class.
// PART B — Engine::_application at +0x690 (Application's own field offsets
//   are Application.h's static_asserts).
// PART C — declared signatures that carry an ABI claim: IsCursorEnabled
//   returns the RAW byte (uint8_t, the same AL as retail's `_N`, without C++
//   bool normalisation); WindowProc is a STATIC member with the WNDPROC shape
//   spelled portably; RegisterMessageCallback takes the plain callback pointer.
//   The member bodies live in src/engine/application (not linked here), so
//   everything below is layout and declared type, not behaviour.
// ============================================================================
#include "unit.h"

#include "application/Application.h"
#include "engine/Engine.h"

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace {

using App = Application::Application;

// ---------------------------------------------------------------- PART A
UNIT_TEST(application_size_and_shape) {
    // sizeof 0x30 / alignof 8 are Application.h's static_asserts.
    CHECK(!std::is_polymorphic<App>::value);
    CHECK(!std::is_default_constructible<App>::value);   // the ctor is the door
    CHECK((std::is_same<RtApplication::Application, App>::value));
    // Engine's accessor hands back the retail type.
    CHECK((std::is_same<decltype(std::declval<Engine::Engine&>().GetApplication()), App*>::value));
}

// ---------------------------------------------------------------- PART C
UNIT_TEST(application_declared_signatures) {
    CHECK((std::is_same<decltype(&App::IsCursorEnabled), uint8_t (App::*)()>::value));
    CHECK((std::is_same<decltype(&App::WindowProc),
                        int64_t (*)(void*, uint32_t, uint64_t, int64_t)>::value));
    CHECK((std::is_same<decltype(&App::Get), App* (*)()>::value));
    CHECK((std::is_same<App::MessageCallback,
                        int (*)(void*, uint32_t, uint64_t, int64_t)>::value));
    CHECK((std::is_same<decltype(&App::RegisterMessageCallback),
                        void (App::*)(uint32_t, App::MessageCallback)>::value));
    CHECK((std::is_same<decltype(&App::SleepThread), void (App::*)(int32_t)>::value));
    CHECK((std::is_same<decltype(&App::GetInstance), void* (App::*)() const>::value));
}

}  // namespace
