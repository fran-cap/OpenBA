// ============================================================================
// tests/unit/platform_thread_null_test.cpp -- review F-132: the thread calls on a
// null handle
// ============================================================================
// System::Thread (src/engine/system/thread.cpp) keeps retail's unchecked sequence
// after _beginthreadex (retail 0x1800ca4e0): ThreadSetNormalPriority, ThreadResume,
// and in the destructor ThreadJoin and ThreadClose, all on the handle it got back.
// When thread creation fails that handle is null. Win32 fails each call on a null
// HANDLE and returns its failure value; the POSIX twin dereferenced it. This test
// pins the Win32 answers on every platform's twin (Windows: thread_win.cpp; Linux,
// macOS, Android: thread_posix.cpp).
// ============================================================================

#include "unit.h"

#include "platform_thread.h"

#include <cstdint>

UNIT_TEST(platform_thread_null_handle_fails_like_win32) {
    const Platform::ThreadHandle none = nullptr;
    CHECK_EQ(Platform::ThreadResume(none), 0xFFFFFFFFu);   // ResumeThread(NULL) == (DWORD)-1
    CHECK_EQ(Platform::ThreadJoin(none), 0xFFFFFFFFu);     // WaitForSingleObject(NULL) == WAIT_FAILED
    CHECK(!Platform::ThreadClose(none));                   // CloseHandle(NULL) == FALSE
}
