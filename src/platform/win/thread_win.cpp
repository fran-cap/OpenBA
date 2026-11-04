// ============================================================================
// src/platform/win/thread_win.cpp -- Platform:: threads over Win32
// ============================================================================
// The Windows half of src/platform/common/platform_thread.h, one call each.
// (Until Phase D.4 this filename held System::Thread itself; that class is portable
// engine code now, src/engine/system/thread.cpp.)
// ============================================================================

#include "platform_thread.h"

#include <windows.h>
#include <process.h>   // _beginthreadex: retail's own CRT copy is 0x18007fbfc

static_assert(sizeof(HANDLE) == sizeof(Platform::ThreadHandle), "a thread handle is a HANDLE");

namespace Platform {

ThreadHandle ThreadCreateSuspended(ThreadEntry entry, void* arg, uint32_t stackSize) {
    unsigned int threadId = 0;
    const uintptr_t h = _beginthreadex(nullptr, stackSize, entry, arg, CREATE_SUSPENDED, &threadId);
    return reinterpret_cast<ThreadHandle>(h);  // CAST-OK: win32
}
bool ThreadSetNormalPriority(ThreadHandle t) {
    return SetThreadPriority(t, THREAD_PRIORITY_NORMAL) != FALSE;
}
bool ThreadSetBelowNormalPriority(ThreadHandle t) {   // NOT RETAIL: ext.path_workers (CPULIMIT)
    return SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL) != FALSE;
}
bool ThreadBackgroundMode(bool on) {   // NOT RETAIL: the derived cache's rewrite
    return SetThreadPriority(GetCurrentThread(), on ? THREAD_MODE_BACKGROUND_BEGIN : THREAD_MODE_BACKGROUND_END) !=
           FALSE;
}
uint32_t ThreadResume(ThreadHandle t) { return ResumeThread(t); }
uint32_t ThreadJoin(ThreadHandle t)   { return WaitForSingleObject(t, INFINITE); }
bool ThreadClose(ThreadHandle t)      { return CloseHandle(t) != FALSE; }

bool YieldThread()          { return SwitchToThread() != FALSE; }
void SleepMs(uint32_t ms)   { Sleep(ms); }
uint32_t CurrentThreadId()  { return GetCurrentThreadId(); }

bool DispatchSentMessages() {   // NOT RETAIL: platform_thread.h (review F-224)
    MSG m;
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    return true;
}

}  // namespace Platform
