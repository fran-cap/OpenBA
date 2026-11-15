// ============================================================================
// src/platform/sdl/process_sdl.cpp -- Platform::Process over SDL3
// ============================================================================
// Phase D.4 SDL step W7 (plan §A.6): the OS half of Application::LaunchProcess
// (retail 0x18009f5e0, src/engine/application/application_cursor_launch.cpp).
//
// RETAIL
//   wait:  CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)
//          with si = {cb} only; WaitForSingleObject(pi.hProcess, INFINITE);
//          CloseHandle(pi.hProcess); pi.hThread leaked. True once started.
//   open:  ShellExecuteW(NULL, L"open", target, NULL, NULL, SW_SHOWDEFAULT) > 32.
//
// WHAT SDL DOES DIFFERENTLY, AND WHY IT IS ACCEPTED (LaunchProcess has no caller in the
// tree; it is ported because retail exports it)
//  * RunAndWait: SDL_CreateProcessWithProperties with SDL_PROP_PROCESS_CREATE_CMDLINE_
//    STRING, which SDL hands to CreateProcessW verbatim on Windows (its own parsing of the
//    command line, as retail's NULL application name gave), then SDL_WaitProcess(block)
//    and SDL_DestroyProcess. SDL's CreateProcessW differs from retail's in its arguments:
//    bInheritHandles TRUE with STARTF_USESTDHANDLES (stdin is set to INHERITED here, the
//    nearest to retail's "no std handles given"; stdout / stderr default to inherited),
//    CREATE_UNICODE_ENVIRONMENT with SDL's copy of the environment. SDL owns and closes
//    both handles: retail's hThread leak goes. Elsewhere the command-line property does
//    nothing (SDL's documentation): a POSIX build must split it into
//    SDL_PROP_PROCESS_CREATE_ARGS_POINTER first. Not done here; there is no caller.
//  * Open: SDL_OpenURL(ToOpenUrl(target)) (src/platform/common/open_url.h: an absolute
//    path becomes a file:/// URL). On Windows SDL_OpenURL is ShellExecuteW(NULL, L"open",
//    url, NULL, NULL, SW_SHOWNORMAL) between its own CoInitialize / CoUninitialize
//    (SDL_sysurl.c): SW_SHOWNORMAL where retail passed SW_SHOWDEFAULT (STARTUPINFO's
//    show state is not honoured), and success is SDL's bool where retail tested
//    the result > 32 (SDL tests the same threshold). The main thread is an STA (SDL's video
//    init, src/app/main.cpp), so WIN_CoInitialize's APARTMENTTHREADED request matches it,
//    and SDL balances it with CoUninitialize (SDL_windows.c): read from the source, not run.
// ============================================================================

#include "platform_os.h"
#include "open_url.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>

#include <cstdio>
#include <string>

namespace Platform {
namespace Process {

bool RunAndWait(const char* commandLine) {
    const SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) return false;
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_CMDLINE_STRING, commandLine);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                          SDL_PROCESS_STDIO_INHERITED);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        std::fprintf(stderr, "[SDL] SDL_CreateProcessWithProperties failed: %s\n",
                     SDL_GetError());
        return false;
    }
    int exitCode = 0;
    SDL_WaitProcess(process, true, &exitCode);   // INFINITE; the code is not used (retail)
    SDL_DestroyProcess(process);
    return true;
}

bool Open(const char* target) {
    const std::string url = ToOpenUrl(target);
    if (!SDL_OpenURL(url.c_str())) {
        std::fprintf(stderr, "[SDL] SDL_OpenURL failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

}  // namespace Process
}  // namespace Platform
