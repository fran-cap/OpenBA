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
//   3. Review F-305: the hide SDL_StopTextInput posts for one box is raised on the UI thread
//      later. A box that took the keyboard in between read it as the player's Return and ended
//      its edit at once. The stubs model SDL's shown flag and the UI thread's posted hide.
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
// SDL's screen_keyboard_shown, and the hide SDL_StopTextInput posted to the UI thread
// (SDL_video.c: Stop posts a hide only while the flag is set; Start shows only while it is clear).
bool g_shown = false;
bool g_hidePosted = false;
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
bool SDLCALL SDL_ScreenKeyboardShown(SDL_Window*) {
    return g_shown;
}
void SDLCALL SDL_DestroyProperties(SDL_PropertiesID) {
}
bool SDLCALL SDL_StopTextInput(SDL_Window*) {
    if (g_shown) g_hidePosted = true;
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

void DeliverShown() {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_SCREEN_KEYBOARD_SHOWN;
    UT::WatchScreenKeyboard(nullptr, &e);
}

// SDL_SendScreenKeyboardShown / Hidden: an event only on a change of the flag.
void UiShows() {
    if (g_shown) return;
    g_shown = true;
    DeliverShown();
}

void UiHides() {
    if (!g_shown) return;
    g_shown = false;
    DeliverHidden();
}

// The UI thread runs the hide a stop posted (SDLActivity's COMMAND_TEXTEDIT_HIDE).
void UiRunsPostedHide() {
    if (!g_hidePosted) return;
    g_hidePosted = false;
    UiHides();
}

void Reset() {
    UT::TextEntryChanged(false);
    g_shown = false;
    g_hidePosted = false;
    UT::g_ownHides = 0;
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

// Box A lets go while the keyboard is up; box B takes it a frame later, before the UI thread has
// run A's posted hide (SDL then skips B's show: its flag is still set). That hidden event is the
// game's own, not the player's Return: B's edit must not end.
UNIT_TEST(screen_keyboard_stale_hide_does_not_end_next_box) {
    Reset();
    UT::TextEntryChanged(true);
    UiShows();
    UT::TextEntryChanged(false);   // A lets go: a hide is posted
    UT::TextEntryChanged(true);    // B takes the keyboard
    UiRunsPostedHide();            // A's hide arrives now
    const bool pressed = PollPressesReturn();
    if (pressed) std::printf("  stale hidden event pressed Return in box B\n");
    CHECK(!pressed);
    // The keyboard shown again for B, the player's Return still ends B's edit.
    UiShows();
    UiHides();
    CHECK(PollPressesReturn());
    Reset();
}

// The player hides the keyboard (Return) before the box lets go: that stop posts no hide, so
// nothing is owed and the next box's Return still ends its edit.
UNIT_TEST(screen_keyboard_player_hide_then_release_owes_nothing) {
    Reset();
    UT::TextEntryChanged(true);
    UiShows();
    UiHides();                     // the player's Return
    UT::TextEntryChanged(false);   // the box lets go: the flag is clear, no hide posted
    UiRunsPostedHide();
    UT::TextEntryChanged(true);
    UiShows();
    UiHides();
    CHECK(PollPressesReturn());
    Reset();
}

// A stop's hide that arrives after the box let go, with no new box, is dropped and owes nothing
// after it: the next box's Return ends its edit.
UNIT_TEST(screen_keyboard_own_hide_after_release_is_consumed) {
    Reset();
    UT::TextEntryChanged(true);
    UiShows();
    UT::TextEntryChanged(false);
    UiRunsPostedHide();
    CHECK(!PollPressesReturn());
    CHECK_EQ(UT::g_ownHides, 0);
    UT::TextEntryChanged(true);
    UiShows();
    UiHides();
    CHECK(PollPressesReturn());
    Reset();
}

// A count no hidden event pays (a keyboard SDL did not hide itself) is cleared by the next
// shown event, so the player's Return is not dropped for good.
UNIT_TEST(screen_keyboard_shown_clears_unpaid_count) {
    Reset();
    UT::TextEntryChanged(true);
    UT::g_ownHides = 1;
    DeliverShown();
    DeliverHidden();
    CHECK(PollPressesReturn());
    Reset();
}
