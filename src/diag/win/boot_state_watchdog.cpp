// src/diag/win/boot_state_watchdog.cpp
// Machine-readable boot state, so an automated run can tell "blocked" from "slow"
// from "frozen".
//
// WHY THIS EXISTS
// The launcher dialog (Engine::PlatformConfigure -> DialogBoxParamW) is no longer
// patched out; retail shows it whenever the previous run did not finish
// Engine::Startup, which during development means every time we kill the process.
// HarnessGuardUnattendedBoot clears the arming bit for unattended runs, but it is a
// guard, not a guarantee -- it only fires when the run is unattended, it cannot help an
// attended run, and any OTHER modal Win32 dialog (an assert, a driver prompt) blocks
// boot exactly the same way.
//
// When that happens the process is ALIVE and PUMPING MESSAGES: it holds a normal memory
// footprint and the WndMsg trace keeps ticking, while standalone_error.log simply stops
// mid-boot. That is indistinguishable from a slow load or a hang if all you have is a
// log tail, and an agent polling `grep -q HuntScan` will sit there until its timeout
// burns. This session lost a run to exactly that.
//
// AND THE SECOND FAILURE, WHICH IS WHAT THE FRAME HEARTBEAT IS FOR
// A watchdog that only proves ITS OWN THREAD is alive says RUNNING for a process whose
// main thread is wedged -- the common case being teardown after the pause-menu Quit, or
// an in-game modal nobody is there to answer. Every unattended caller then burns its
// whole ten-minute timeout before anyone notices. So the state now carries a HEARTBEAT:
// a counter the main loop bumps once per frame (BootState_MarkFrame, called from
// RtEngine_MainLoop_Run) and the number of seconds since it last moved. A stalled
// counter is what "frozen" means, and it costs ~30s to find instead of ~10min.
//
// WHAT IT DOES
// A watchdog thread polls once a second and writes ONE line to standalone_boot_state.txt
// (in the working directory, next to the logs):
//
//   STATE=<state> pid=<n> uptimeSec=<n> frame=<n> stale=<n> detail=<text>
//
//   BOOTING         started, Engine::Startup has not returned yet
//   RUNNING         Engine::Startup returned; normal operation
//   QUITTING        MainLoop::Run returned; shutdown/teardown is in progress
//   BLOCKED_DIALOG  a visible modal Win32 dialog is open -- boot cannot proceed
//                   without a human click. detail= carries its window title.
//   HUNG            the frame counter has not moved for longer than this phase's
//                   budget. detail= carries the phase it froze in.
//
// The file is rewritten every second, so a stale file from a dead process is detectable
// via its pid (see tools/boot_state.sh, which reports EXITED/STALE for that case).
// The state is also logged: once when it changes, then every 15s while BLOCKED_DIALOG
// persists, so a log tail shows it too rather than going silent.
//
// DETECTION: Win32 dialogs created by DialogBoxParamW have window class "#32770".
// We enumerate top-level windows, keep the visible ones owned by THIS process, and
// report the first "#32770". That covers the launcher and any assert/system dialog
// without hooking or patching anything -- this file only observes.
//
// FALSE POSITIVES, AND THE ONE THAT IS DELIBERATE
//  * BLOCKED_DIALOG IS TESTED FIRST and wins: a Win32 modal is a blocked boot, not a
//    hang, and it has its own verdict and its own instructions.
//  * Loading screens do NOT trip it. The loader loops, the splash and the menu
//    DialogLoopT all tick through MainLoop::Run's frame loop, so the counter advances
//    across a large-map load exactly as it does in game. (Verified on a
//    BANISHED_MAP_SIZE=large autostart run.)
//  * `UI::ShowDialog` pumps its OWN message loop, which never reaches MainLoop::Run and
//    therefore renders no heartbeat. An in-game modal on an UNATTENDED run WILL read as
//    HUNG after kHungRunningSec, AND THAT IS INTENDED: nobody is there to click it, so
//    the run is over either way, and a fast verdict beats a ten-minute timeout.
//    BANISHED_MODAL_ANSWER exists to answer such a dialog when the route needs it, and
//    the hang dump will show ShowDialog's pump on the main thread's stack, which is what
//    tells you which of the two you are looking at. An ATTENDED run never
//    self-terminates, so a human sitting in a dialog is only ever reported, never killed.
//
// WHAT HUNG DOES
//  1. publishes STATE=HUNG and logs one [BootState] HUNG line to stderr;
//  2. writes standalone_hang.log: an all-thread stack dump. Each other thread is
//     suspended, GetThreadContext'd, walked through the exe's ONE stack walker
//     (StandaloneWalkStack in src/diag/win/crash_handler_win.cpp, the VEH crash handler's) and resumed.
//     Thread ids are printed and the main thread is marked;
//  3. on an UNATTENDED run only, TerminateProcess(self, 3).
//
// EXIT CODE 3 IS SHARED WITH THE CRASH HANDLER (crash_handler_win.cpp's CrashHandler ends in
// ExitProcess(3)). The artifact is what discriminates: a hang leaves standalone_hang.log and
// NO standalone_crash.log. (A failed Engine::Startup is neither: since Phase D.4 GAPS, row
// 1273, GameMain shows retail's fatal box and returns -1 = exit 255 after the click.)

#include "boot_state_watchdog.h"

#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "options.h"
#include "platform/common/cpu/topology.h"   // CPULIMIT: the [Cpu] facts on the RUNNING line

namespace BootState {

namespace {

volatile LONG g_startupComplete = 0;
volatile LONG g_quitting        = 0;
volatile LONG g_frames          = 0;
volatile LONG g_stop            = 0;
HANDLE        g_thread          = nullptr;
DWORD         g_startTick       = 0;
DWORD         g_mainThreadId    = 0;

// ---------------------------------------------------------------------------
// HANG THRESHOLDS. tools/boot_state.sh READS THESE TWO LINES out of this file
// and prints them beside its verdict -- exactly as it already derives its
// staleness factor from the Sleep() at the bottom of WatchdogThread, and for
// the same reason: a number restated in the reader goes stale the moment the
// publisher changes it, with nothing to notice. Keep the shape of these two
// declarations (`const unsigned kHung<Phase>Sec = <n>;`) if you edit them.
// ---------------------------------------------------------------------------
const unsigned kHungRunningSec  = 20;   // BOOTING/RUNNING: 20s with no new frame
const unsigned kHungQuittingSec = 45;   // QUITTING: teardown renders no frames at all

struct DialogScan {
    DWORD  pid;
    bool   found;
    wchar_t title[128];
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    auto* scan = reinterpret_cast<DialogScan*>(lp);  // CAST-OK: win32

    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid != scan->pid) return TRUE;

    wchar_t cls[64] = {0};
    if (!GetClassNameW(hwnd, cls, 63)) return TRUE;
    if (wcscmp(cls, L"#32770") != 0) return TRUE;   // not a Win32 dialog

    scan->found = true;
    GetWindowTextW(hwnd, scan->title, 127);
    return FALSE;   // first match is enough
}

// Returns true and fills utf8Title when a modal dialog owned by us is on screen.
bool ModalDialogOpen(char* utf8Title, int cap) {
    DialogScan scan{};
    scan.pid = GetCurrentProcessId();
    EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&scan));  // CAST-OK: win32
    if (!scan.found) return false;

    if (cap > 0) {
        utf8Title[0] = '\0';
        WideCharToMultiByte(CP_UTF8, 0, scan.title, -1, utf8Title, cap, nullptr, nullptr);
        utf8Title[cap - 1] = '\0';
    }
    return true;
}

void WriteStateFile(const char* state, unsigned uptimeSec, unsigned frame,
                    unsigned staleSec, const char* detail) {
    // Rewritten in full every tick; a reader either sees the previous complete line or
    // this one. Kept to a single line so a script can parse it with one grep, and
    // detail= stays LAST because the reader takes everything after it as the value.
    FILE* f = std::fopen("standalone_boot_state.txt", "w");
    if (!f) return;
    std::fprintf(f, "STATE=%s pid=%u uptimeSec=%u frame=%u stale=%u detail=%s\n",
                 state, static_cast<uint32_t>(GetCurrentProcessId()), uptimeSec, frame, staleSec,
                 (detail && detail[0]) ? detail : "-");
    std::fclose(f);
}

// ---------------------------------------------------------------------------
// The hang dump
// ---------------------------------------------------------------------------
// CRT-free output, for the same reason the crash log is: we run this with every
// other thread SUSPENDED, and a suspended thread can be holding the CRT's file
// lock. One handle, WriteFile, FILE_FLAG_WRITE_THROUGH.

HANDLE g_hangLog = INVALID_HANDLE_VALUE;

void HangLogRaw(const char* s) {
    if (g_hangLog == INVALID_HANDLE_VALUE || !s) return;
    DWORD len = 0; while (s[len]) ++len;
    DWORD wrote = 0;
    WriteFile(g_hangLog, s, len, &wrote, nullptr);
}

void HangLogf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = (int)sizeof(buf);
    if (g_hangLog != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(g_hangLog, buf, (DWORD)n, &wrote, nullptr);
    }
}

void HangStackSink(void*, const char* line) { HangLogRaw(line); }

// Suspend every other thread of this process in turn, walk it, resume it. The
// walking thread (this one) is dumped from a captured context without any
// suspend/resume, since it is the one running.
void WriteHangDump(const char* phase, unsigned uptimeSec, unsigned frame,
                   unsigned staleSec, unsigned budgetSec) {
    g_hangLog = CreateFileA("standalone_hang.log", GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (g_hangLog == INVALID_HANDLE_VALUE) return;

    HangLogRaw("=== STANDALONE HANG ===\n");
    HangLogf("Phase:       %s\n", phase);
    HangLogf("Pid:         %u   (main thread id %u)\n",
             static_cast<uint32_t>(GetCurrentProcessId()), static_cast<uint32_t>(g_mainThreadId));
    HangLogf("Uptime:      %us\n", uptimeSec);
    HangLogf("Frame:       %u  (main-loop iterations completed)\n", frame);
    HangLogf("Stale:       %us  (budget for this phase: %us)\n", staleSec, budgetSec);
    HangLogRaw("The frame counter stopped advancing. Every thread's stack follows;\n"
               "the MAIN thread is the one that matters. A UI::ShowDialog pump on it\n"
               "means an unanswered in-game modal, not a deadlock (see\n"
               "src/diag/win/boot_state_watchdog.cpp).\n\n");

    const DWORD selfTid = GetCurrentThreadId();
    const DWORD selfPid = GetCurrentProcessId();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        HangLogRaw("(CreateToolhelp32Snapshot failed -- no thread list)\n");
        CloseHandle(g_hangLog);
        g_hangLog = INVALID_HANDLE_VALUE;
        return;
    }

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.dwSize < FIELD_OFFSET(THREADENTRY32, th32ThreadID) + sizeof(DWORD))
                continue;
            if (te.th32OwnerProcessID != selfPid) continue;

            const bool isMain = (te.th32ThreadID == g_mainThreadId);
            const bool isSelf = (te.th32ThreadID == selfTid);

            HangLogf("\n--- thread %u%s%s ---\n",
                     static_cast<uint32_t>(te.th32ThreadID),
                     isMain ? "  [MAIN THREAD]" : "",
                     isSelf ? "  [watchdog -- this thread]" : "");

            if (isSelf) {
                HangLogRaw("  (the watchdog itself; not interesting)\n");
                continue;
            }

            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                   THREAD_QUERY_INFORMATION,
                                   FALSE, te.th32ThreadID);
            if (!th) {
                HangLogf("  (OpenThread failed, GetLastError=%lu)\n", GetLastError());
                continue;
            }

            if (SuspendThread(th) == (DWORD)-1) {
                HangLogf("  (SuspendThread failed, GetLastError=%lu)\n", GetLastError());
                CloseHandle(th);
                continue;
            }

            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(th, &ctx)) {
                HangLogf("  RIP=%016llX RSP=%016llX RBP=%016llX\n",
                         (unsigned long long)ctx.Rip, (unsigned long long)ctx.Rsp,
                         (unsigned long long)ctx.Rbp);
                StandaloneWalkStack(&ctx, &HangStackSink, nullptr);
            } else {
                HangLogf("  (GetThreadContext failed, GetLastError=%lu)\n", GetLastError());
            }

            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    HangLogRaw("\n=== END HANG ===\n");
    FlushFileBuffers(g_hangLog);
    CloseHandle(g_hangLog);
    g_hangLog = INVALID_HANDLE_VALUE;
}

DWORD WINAPI WatchdogThread(LPVOID) {
    const char* lastState = "";
    unsigned    blockedTicks = 0;
    LONG        lastFrame    = -1;
    DWORD       lastMoveTick = GetTickCount();
    bool        hungReported = false;

    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        const DWORD    now       = GetTickCount();
        const unsigned uptimeSec = (now - g_startTick) / 1000;

        const LONG frame    = InterlockedCompareExchange(&g_frames, 0, 0);
        const bool quitting = InterlockedCompareExchange(&g_quitting, 0, 0) != 0;

        // The heartbeat. QUITTING legitimately renders no frames, so the stale
        // clock is restarted at the transition and the teardown budget is
        // measured from that moment rather than from the last frame.
        if (frame != lastFrame) {
            lastFrame    = frame;
            lastMoveTick = now;
            hungReported = false;
        }
        if (quitting && std::strcmp(lastState, "QUITTING") != 0 &&
            std::strcmp(lastState, "HUNG") != 0) {
            lastMoveTick = now;
            hungReported = false;
        }
        const unsigned staleSec = (now - lastMoveTick) / 1000;

        char title[128] = {0};
        const bool blocked = ModalDialogOpen(title, sizeof(title));

        // A modal Win32 dialog is a BLOCKED boot, not a hang, and it is tested
        // first so it keeps its own verdict and its own instructions.
        const char* phase = quitting
            ? "QUITTING"
            : (InterlockedCompareExchange(&g_startupComplete, 0, 0) ? "RUNNING" : "BOOTING");
        const unsigned budget = quitting ? kHungQuittingSec : kHungRunningSec;
        const bool hung = !blocked && (staleSec >= budget);

        const char* state = blocked ? "BLOCKED_DIALOG" : (hung ? "HUNG" : phase);

        WriteStateFile(state, uptimeSec, (unsigned)frame, staleSec,
                       blocked ? title : (hung ? phase : "-"));

        if (hung && !hungReported) {
            hungReported = true;
            // stderr BEFORE any suspension: the main thread may hold the CRT's
            // file lock, and this must not be the line that deadlocks us.
            std::fprintf(stderr,
                "[BootState] HUNG in %s after %us -- the main loop has not completed a "
                "frame for %us (budget %us, frame=%d). Writing standalone_hang.log "
                "(all-thread stacks).%s\n",
                phase, uptimeSec, staleSec, budget, static_cast<int32_t>(frame),
                Opt().unattendedBoot ? " Unattended run: terminating with exit code 4."
                                     : " Attended run: NOT terminating.");
            std::fflush(stderr);

            WriteHangDump(phase, uptimeSec, (unsigned)frame, staleSec, budget);

            // Self-terminate ONLY when nobody is watching. An attended run may
            // legitimately be sitting in a dialog with a human in front of it.
            if (Opt().unattendedBoot) {
                TerminateProcess(GetCurrentProcess(), 4);
            }
        }

        // Log on transition, then keep reminding while blocked -- a silent log is the
        // very symptom this exists to explain, so it must not go quiet again.
        const bool changed = (std::strcmp(state, lastState) != 0);
        if (blocked) {
            if (changed || (blockedTicks % 15) == 0) {
                std::fprintf(stderr,
                    "[BootState] BLOCKED_DIALOG after %us -- a modal Win32 dialog "
                    "(class #32770, title=\"%s\") is open and boot cannot proceed "
                    "without a click. This is usually the launcher dialog, armed "
                    "because the previous run did not exit cleanly.\n",
                    uptimeSec, title);
                std::fflush(stderr);
            }
            ++blockedTicks;
        } else {
            if (changed && std::strcmp(state, "RUNNING") == 0) {
                // CPULIMIT: the process mask and core counts ride on the RUNNING line, so
                // every run (and every stress A/B leg) proves its sys.cpu_limit.
                char cpu[192];
                Cpu::Describe(Cpu::Get(), cpu, sizeof(cpu));
                std::fprintf(stderr, "[BootState] RUNNING (Engine::Startup returned at %us) cpu %s\n",
                             uptimeSec, cpu);
                std::fflush(stderr);
            }
            if (changed && std::strcmp(state, "QUITTING") == 0) {
                std::fprintf(stderr, "[BootState] QUITTING at %us after %d frames "
                                     "(teardown budget %us)\n",
                             uptimeSec, static_cast<int32_t>(frame), kHungQuittingSec);
                std::fflush(stderr);
            }
            if (changed && blockedTicks) {
                std::fprintf(stderr, "[BootState] dialog cleared after %u s blocked\n",
                             blockedTicks);
                std::fflush(stderr);
            }
            blockedTicks = 0;
        }
        lastState = state;

        Sleep(1000);
    }
    return 0;
}

// The line Stop() writes: EXITED / clean-shutdown unless SetExitState named another
// (boot_state_watchdog.h).
char g_exitState[32]   = "EXITED";
char g_exitDetail[512] = "clean-shutdown";

}  // namespace

void SetExitState(const char* state, const char* detail) {
    std::snprintf(g_exitState, sizeof(g_exitState), "%s", state);
    std::snprintf(g_exitDetail, sizeof(g_exitDetail), "%s", detail);
    // Published at once as well: a box shown before the process exits must not leave a previous
    // run's line (STALE) or none at all behind while it waits.
    WriteStateFile(g_exitState, 0, 0, 0, g_exitDetail);
}

void Start() {
    if (g_thread) return;
    g_startTick    = GetTickCount();
    g_mainThreadId = GetCurrentThreadId();   // Start() is called from WinMain
    // Publish BOOTING immediately: a run that dies before the first tick should still
    // leave a file saying it got this far, rather than none at all.
    WriteStateFile("BOOTING", 0, 0, 0, "-");
    g_thread = CreateThread(nullptr, 0, &WatchdogThread, nullptr, 0, nullptr);
    if (!g_thread) {
        std::fprintf(stderr, "[BootState] WARNING: watchdog thread failed to start "
                             "(GetLastError=%lu) -- boot state will not be published\n",
                     GetLastError());
        std::fflush(stderr);
    }
}

void MarkStartupComplete() {
    InterlockedExchange(&g_startupComplete, 1);
}

void Stop() {
    InterlockedExchange(&g_stop, 1);
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    // Before Start() (a boot stopped by the game-data check) there is no uptime to report.
    const unsigned uptimeSec = g_startTick != 0 ? (GetTickCount() - g_startTick) / 1000 : 0;
    WriteStateFile(g_exitState, uptimeSec, (unsigned)InterlockedCompareExchange(&g_frames, 0, 0), 0,
                   g_exitDetail);
}

}  // namespace BootState

// One interlocked increment per rendered frame. Cheap enough to sit in the main
// loop unconditionally; there is no flag for it (DIRECTION.md: a verified
// feature is unconditional code).
extern "C" void BootState_MarkFrame() {
    InterlockedIncrement(&BootState::g_frames);
}

extern "C" void BootState_MarkQuitting() {
    InterlockedExchange(&BootState::g_quitting, 1);
}
