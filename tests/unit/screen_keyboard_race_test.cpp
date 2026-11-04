// ============================================================================
// tests/unit/screen_keyboard_race_test.cpp -- the Android screen keyboard's hidden-event watch
// ============================================================================
// src/platform/android/input_android.cpp is compiled into this test with its namespace renamed
// (it would otherwise clash with the host's own input twin) and the few SDL calls it makes
// stubbed: `unit` links no SDL. Runs on every host.
//   1. Control: the keyboard hidden while a box holds it queues the Return that ends the edit.
//   2. Review F-309: the watch read g_textEntry outside g_mutex. When TextEntryChanged(false)'s
//      clear won the lock between that read and the push, a Return stayed queued with no box
//      active, and the next poll pressed it into a game binding.
// ============================================================================
#include "unit.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_properties.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#define Platform ScreenKeyboardUnderTest
#include "../../src/platform/android/input_android.cpp"
#undef Platform

namespace {
int g_fakeWindow = 0;
// The race test's hook in SDL_StopTextInput, which TextEntryChanged(false) calls after it has
// stored false and before it takes g_mutex: the holder lets the mutex go there.
std::atomic<bool> g_hookArmed{false};
std::atomic<bool> g_releaseNow{false};
std::atomic<bool> g_released{false};
}  // namespace

namespace ScreenKeyboardUnderTest {
namespace Window {
namespace Sdl {
SDL_Window* GameWindow() {
    return reinterpret_cast<SDL_Window*>(&g_fakeWindow);
}
}  // namespace Sdl
}  // namespace Window
}  // namespace ScreenKeyboardUnderTest

// SDL stubs: `unit` links no SDL.
extern "C" {
bool SDLCALL SDL_AddEventWatch(SDL_EventFilter, void*) {
    return true;
}
bool SDLCALL SDL_SetHint(const char*, const char*) {
    return true;
}
SDL_PropertiesID SDLCALL SDL_CreateProperties(void) {
    return 1;
}
bool SDLCALL SDL_SetNumberProperty(SDL_PropertiesID, const char*, Sint64) {
    return true;
}
bool SDLCALL SDL_SetBooleanProperty(SDL_PropertiesID, const char*, bool) {
    return true;
}
bool SDLCALL SDL_StartTextInputWithProperties(SDL_Window*, SDL_PropertiesID) {
    return true;
}
void SDLCALL SDL_DestroyProperties(SDL_PropertiesID) {
}
bool SDLCALL SDL_StopTextInput(SDL_Window*) {
    if (g_hookArmed.load()) {
        g_releaseNow.store(true);
        while (!g_released.load()) {
        }
    }
    return true;
}
void SDLCALL SDL_PumpEvents(void) {
}
SDL_Keymod SDLCALL SDL_GetModState(void) {
    return 0;
}
}

namespace {

namespace UT = ScreenKeyboardUnderTest::Input;

void DeliverHidden() {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_SCREEN_KEYBOARD_HIDDEN;
    UT::WatchScreenKeyboard(nullptr, &e);
}

bool PollPressesReturn() {
    uint8_t dik[256];
    std::memset(dik, 0, sizeof(dik));
    UT::AddScreenKeys(dik);
    return dik[UT::kSdlScancodeToDik[SDL_SCANCODE_RETURN]] != 0;
}

void Reset() {
    UT::TextEntryChanged(false);
}

}  // namespace

// The keyboard hidden while the box holds it (the IME's Return or Back) ends the edit.
UNIT_TEST(screen_keyboard_hidden_while_active_queues_return) {
    Reset();
    UT::TextEntryChanged(true);
    DeliverHidden();
    CHECK(PollPressesReturn());
    Reset();
}

// The keyboard hidden after the box let go queues nothing.
UNIT_TEST(screen_keyboard_hidden_after_release_queues_nothing) {
    Reset();
    UT::TextEntryChanged(true);
    UT::TextEntryChanged(false);
    DeliverHidden();
    CHECK(!PollPressesReturn());
    Reset();
}

// The watch's hidden event and TextEntryChanged(false) on two threads: TextEntryChanged stores
// false, then both wait on g_mutex (a holder thread lets it go from inside SDL_StopTextInput).
// Whichever wins, no Return may be left queued once the box has let go.
UNIT_TEST(screen_keyboard_hide_racing_release_leaves_no_return) {
    int stray = 0;
    int rounds = 0;
    for (; rounds < 200; ++rounds) {
        Reset();
        UT::TextEntryChanged(true);
        g_releaseNow.store(false);
        g_released.store(false);
        std::atomic<bool> held{false};
        std::thread holder([&held] {
            UT::g_mutex.lock();
            held.store(true);
            while (!g_releaseNow.load()) {
            }
            UT::g_mutex.unlock();
            g_released.store(true);
        });
        while (!held.load()) {
        }
        std::thread watch(DeliverHidden);   // blocks on g_mutex
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        g_hookArmed.store(true);
        UT::TextEntryChanged(false);        // stores false, then races the watch for g_mutex
        g_hookArmed.store(false);
        holder.join();
        watch.join();
        const bool active = UT::g_textEntry.load();
        const bool queued = PollPressesReturn();
        if (!active && queued) ++stray;
    }
    if (stray != 0) {
        std::printf("  %d of %d rounds left a Return queued with no box active\n", stray, rounds);
    }
    CHECK_EQ(stray, 0);
    Reset();
}
