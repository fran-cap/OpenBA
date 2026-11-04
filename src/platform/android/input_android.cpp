// ============================================================================
// src/platform/android/input_android.cpp -- the Android twin of src/platform/posix/input_posix.cpp
// ============================================================================
// It replaces input_posix.cpp whole on Android (src/platform/android/README.md). ShiftHeld and
// the identity hooks are posix's (that file's banner has ShiftHeld's story). One hook differs:
// RotateDragReturnsCursor is true. The camera-rotate drag's relative mode is pointer capture on
// Android (SDL's SDLGenericMotionListener_API26), and Android ends a capture with the pointer
// where the capture began; SDL's own position has moved with the drag's relative motion, so the
// first motion after the release snapped the cursor back across the screen. Measured on the S25
// with a uinput mouse: drag began at x 802, SDL at 1002 at the release, the next motion at 833
// (docs/android/P8_INPUT_OPTIONS.md, round 2; docs/reference/android_mouse.md).
//
// THE SCREEN KEYBOARD (issue #100). Retail typed from the polled key state only
// (UI::EditElement::Update, SelectCharacter's US table), and a phone has no keys to poll.
//  * Showing it. TextEntryChanged(true), when a text box takes the keyboard, starts SDL's
//    text input on the game window, which shows the keyboard (SDL_HINT_ENABLE_SCREEN_KEYBOARD
//    "auto": Android registers no keyboard with SDL, so it always shows). Plain text, no
//    autocorrection, Return hides it (SDL_HINT_RETURN_KEY_HIDES_IME). Losing the box stops it.
//  * Its keys. SDL's input connection turns each committed character into a press and a
//    release of the US-layout scan code that types it, Left Shift around it for a capital
//    (SDL_SendKeyboardUnicodeKey; a deletion is a Backspace), all sent together from the UI
//    thread with `which` 0, SDL's virtual keyboard. The poller would never see one down.
//    An event watch (it runs as each event is sent, so the pumps' draining loses none)
//    queues every virtual press with the Shift state around it, and AddScreenKeys hands the
//    poller one per poll, with a released poll after each so two equal letters are two
//    presses, Shift included. The edit box then types it through SelectCharacter as it
//    types a key of a real keyboard: what retail's US table has is what can be typed (SDL
//    gives the rest scan codes past the DIK table, and they are dropped). The virtual keys
//    are also cleared from what the state showed, in case a poll ran between a press and its
//    release, so none is typed twice. Backspace is not a virtual key: the keyboard sends it as
//    a key event of its own (`which` 1), its press and release again together. So while a box
//    has the keyboard, a real key pressed and released between two polls is queued the same
//    way (with SDL's Shift state at its release); a key a poll saw down is the poller's, as
//    before. On the emulator's keyboard (Gboard) letters came as virtual keys and Backspace as
//    a real one.
//  * Ending it. The keyboard hidden while the box still holds the keyboard (Return, or the
//    system's Back) queues a Return after the pending keys: the box ends its edit as Enter
//    ends it on a desktop. Losing the box clears the queue, so no key typed into the box
//    reaches a game binding.
//  * The game's own hide (review F-305). Losing the box stops text input, and SDL hides a
//    shown keyboard by posting to the UI thread, whose handler raises the hidden event later.
//    A box that takes the keyboard before that handler has run must not read that event as
//    the player's Return, or its edit ends as it begins. TextEntryChanged(false) counts a stop
//    made while SDL holds the keyboard shown, and the watch drops one hidden event per count.
// ============================================================================

#include "platform_input.h"
#include "platform_input_hooks.h"
#include "sdl_dik_tables.h"
#include "platform/sdl/window_sdl_internal.h"   // Sdl::GameWindow

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_properties.h>

#include <atomic>
#include <mutex>

namespace Platform {
namespace Input {

namespace {

// One queued screen-keyboard press: the scan code and whether Shift was down around it.
struct ScreenKey {
    uint16_t scancode;
    bool     shift;
};

constexpr int kScreenKeyCapacity = 256;   // a burst past it (a pasted page) is dropped

// SDL's virtual keyboard id (SDL_GLOBAL_KEYBOARD_ID, src/events/SDL_keyboard_c.h: not in the
// public headers): the `which` of the keys SDL makes up from text. Android's real keys are 1.
constexpr SDL_KeyboardID kVirtualKeyboard = 0;

// The watch runs on Android's UI thread, the poller on the game thread: g_mutex guards the
// queue, both key-state arrays, g_virtualShift and g_gap.
std::mutex g_mutex;
ScreenKey  g_queue[kScreenKeyCapacity];
int        g_head  = 0;
int        g_count = 0;
bool       g_virtualDown[kSdlScancodeCount] = {};
bool       g_downSincePoll[kSdlScancodeCount] = {};   // a real key pressed since the last poll
bool       g_virtualShift = false;
bool       g_gap = false;                 // the last poll pressed a key: this one releases it
bool       g_watching = false;            // game thread only
std::atomic<bool> g_textEntry{false};

// NOT RETAIL (issue #100 glue, review F-305): hidden events still owed to the game's own stops.
// g_hideMutex guards it. It is a lock of its own, never held with g_mutex, so the count and the
// watch's drop are ordered against each other without waiting on the key queue.
std::mutex g_hideMutex;
int        g_ownHides = 0;

// Under g_mutex.
void PushScreenKey(uint16_t scancode, bool shift) {
    if (g_count == kScreenKeyCapacity) return;
    g_queue[(g_head + g_count) % kScreenKeyCapacity] = ScreenKey{scancode, shift};
    ++g_count;
}

bool SDLCALL WatchScreenKeyboard(void* /*userdata*/, SDL_Event* e) {
    if (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) {
        const bool down = e->type == SDL_EVENT_KEY_DOWN;
        const int sc = static_cast<int>(e->key.scancode);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (e->key.which != kVirtualKeyboard) {
            // A real key event: the IME's own (Backspace) or a keyboard's. Queued only when
            // its press and release both fall between two polls, which the state never shows.
            if (sc < 0 || sc >= kSdlScancodeCount || !g_textEntry.load()) return true;
            if (down) {
                g_downSincePoll[sc] = true;
            } else if (g_downSincePoll[sc]) {
                g_downSincePoll[sc] = false;
                PushScreenKey(static_cast<uint16_t>(sc), (e->key.mod & SDL_KMOD_SHIFT) != 0);
            }
            return true;
        }
        if (sc >= 0 && sc < kSdlScancodeCount) g_virtualDown[sc] = down;
        if (sc == SDL_SCANCODE_LSHIFT) {
            g_virtualShift = down;
        } else if (down && g_textEntry.load()) {
            PushScreenKey(static_cast<uint16_t>(sc), g_virtualShift);
        }
    } else if (e->type == SDL_EVENT_SCREEN_KEYBOARD_SHOWN) {
        // SDL raises shown only after a hidden event (its shown flag goes false, then true), and
        // every counted stop's hidden event comes before it: a count left here is owed to no
        // event (a keyboard SDL did not hide itself), so the player's next Return is not dropped.
        std::lock_guard<std::mutex> lock(g_hideMutex);
        g_ownHides = 0;
    } else if (e->type == SDL_EVENT_SCREEN_KEYBOARD_HIDDEN) {
        {
            // The hide a stop of the game posted: not the player's Return (review F-305).
            std::lock_guard<std::mutex> lock(g_hideMutex);
            if (g_ownHides > 0) {
                --g_ownHides;
                return true;
            }
        }
        // The test of g_textEntry is under g_mutex, as the key arm's is: TextEntryChanged(false)
        // stores false before it takes g_mutex to clear the queue, so a Return pushed here
        // either precedes that clear or sees false. Tested outside the lock, the push could
        // follow the clear and reach a game binding with no box active (review F-309).
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_textEntry.load()) return true;   // hidden because the box let go
        PushScreenKey(SDL_SCANCODE_RETURN, false);
    }
    return true;
}

}  // namespace

void TextEntryChanged(bool active) {
    SDL_Window* w = Window::Sdl::GameWindow();
    if (active) {
        if (!g_watching) g_watching = SDL_AddEventWatch(WatchScreenKeyboard, nullptr);
        g_textEntry.store(true);
        SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME, "1");
        if (w == nullptr) return;
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        SDL_StartTextInputWithProperties(w, props);
        SDL_DestroyProperties(props);
        return;
    }
    if (w != nullptr) {
        // SDL_StopTextInput posts a hide when SDL holds the keyboard shown (the banner's "auto":
        // no keyboard registered), and the UI thread clears that flag before it raises the
        // hidden event, which waits on g_hideMutex. So a flag seen true here under the lock
        // means exactly one hidden event still to come, whichever path raises it.
        std::lock_guard<std::mutex> lock(g_hideMutex);
        if (SDL_ScreenKeyboardShown(w)) ++g_ownHides;
    }
    g_textEntry.store(false);
    if (w != nullptr) SDL_StopTextInput(w);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_head  = 0;
    g_count = 0;
    g_gap   = false;
}

void AddScreenKeys(uint8_t dik[256]) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int s = 0; s < kSdlScancodeCount; ++s) {
        if (g_virtualDown[s]) dik[kSdlScancodeToDik[s]] = 0;
        g_downSincePoll[s] = false;   // this poll read the state: a key still down is seen here
    }
    if (g_gap) {
        g_gap = false;
        return;
    }
    if (g_count == 0) return;
    const ScreenKey k = g_queue[g_head];
    g_head = (g_head + 1) % kScreenKeyCapacity;
    --g_count;
    g_gap = true;
    if (k.scancode >= kSdlScancodeCount) return;
    const uint8_t code = kSdlScancodeToDik[k.scancode];
    if (code == 0) return;
    dik[code] = 0x80;
    if (k.shift) dik[kSdlScancodeToDik[SDL_SCANCODE_LSHIFT]] = 0x80;
}

bool ShiftHeld() {
    SDL_PumpEvents();
    return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
}

// The other hooks of platform_input_hooks.h: the identity, but RotateDragReturnsCursor (banner).
const bool* ApplyKeyLayer(const bool* raw, int* /*count*/, bool* /*scratch*/) {
    return raw;
}

uint32_t PolledMouseButtons(uint32_t physical) {
    return physical;
}

void PreprocessPumpEvent(SDL_Event* /*event*/) {
}

bool ConsumePumpEvent(const SDL_Event& /*event*/) {
    return false;
}

bool RotateDragReturnsCursor() {
    return true;
}

}  // namespace Input
}  // namespace Platform
