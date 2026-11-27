// ============================================================================
// src/platform/sdl/pick_file_sdl.cpp -- Platform::BeginPickOpenFile over SDL's file dialog
// ============================================================================
// NOT RETAIL. The rules are in platform_pick_file.h. SDL_ShowFileDialogWithProperties picks the
// system's dialog per platform (the vendored SDL is built with SDL_DIALOG on: CMakeLists.txt) and
// calls back once, from whichever thread its backend answers on: the dialog's own thread on
// Windows, the Java UI thread on Android, the event pump (portal) or a waiting thread (zenity) on
// Linux, the main thread on macOS. The answer is left in a mutex-guarded mailbox the game's frame
// reads with PollPickedFile.
// ============================================================================

#include "platform_pick_file.h"
#include "window_sdl_internal.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

#include <mutex>
#include <string>
#include <vector>

namespace Platform {

namespace {

std::mutex g_pickMutex;
PickState g_pickState = PickState::None;
std::string g_pickAnswer;

// The filter lives as long as the dialog may read it: SDL keeps the pointers until it calls back.
std::string g_filterName;
std::string g_filterPattern;
SDL_DialogFileFilter g_filters[2];

void SDLCALL OnDialogAnswer(void* /*userdata*/, const char* const* filelist, int /*filter*/) {
    std::lock_guard<std::mutex> lock(g_pickMutex);
    if (filelist == nullptr) {
        // An error: SDL_GetError is this thread's, so it is read here.
        const char* why = SDL_GetError();
        g_pickAnswer = (why != nullptr && why[0] != 0) ? why : "the file dialog could not open";
        g_pickState = PickState::Failed;
    } else if (filelist[0] == nullptr) {
        g_pickAnswer.clear();
        g_pickState = PickState::Cancelled;
    } else {
        g_pickAnswer = filelist[0];
        g_pickState = PickState::Picked;
    }
}

}  // namespace

bool CanPickFile() {
    return true;
}

bool BeginPickOpenFile(const char* title, const char* filterName, const char* filterExtension,
                       const std::string& startDir) {
    {
        std::lock_guard<std::mutex> lock(g_pickMutex);
        if (g_pickState == PickState::Open) {
            return false;
        }
        g_pickState = PickState::Open;
        g_pickAnswer.clear();
    }
    g_filterName = filterName != nullptr ? filterName : "";
    g_filterPattern = filterExtension != nullptr ? filterExtension : "*";
    g_filters[0].name = g_filterName.c_str();
    g_filters[0].pattern = g_filterPattern.c_str();
    g_filters[1].name = "All files";
    g_filters[1].pattern = "*";

    const SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) {
        std::lock_guard<std::mutex> lock(g_pickMutex);
        g_pickState = PickState::None;
        return false;
    }
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, g_filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 2);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, Window::Sdl::GameWindow());
    if (!startDir.empty()) {
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, startDir.c_str());
    }
    if (title != nullptr) {
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    }
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    // An error SDL finds before the dialog opens is also delivered through the callback (filelist
    // null), so the mailbox always gets one answer.
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, OnDialogAnswer, nullptr, props);
    SDL_DestroyProperties(props);
    return true;
}

PickState PollPickedFile(std::string& out) {
    std::lock_guard<std::mutex> lock(g_pickMutex);
    const PickState state = g_pickState;
    if (state == PickState::Picked || state == PickState::Cancelled || state == PickState::Failed) {
        out = g_pickAnswer;
        g_pickState = PickState::None;
        g_pickAnswer.clear();
    }
    return state;
}

bool ReadPickedFile(const std::string& picked, std::vector<uint8_t>& out, std::string& why) {
    // SDL_LoadFile opens a content:// URI through Android's ContentResolver, and a path elsewhere.
    size_t size = 0;
    void* data = SDL_LoadFile(picked.c_str(), &size);
    if (data == nullptr) {
        const char* err = SDL_GetError();
        why = (err != nullptr && err[0] != 0) ? err : "the file could not be read";
        return false;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.assign(bytes, bytes + size);
    SDL_free(data);
    return true;
}

}  // namespace Platform
