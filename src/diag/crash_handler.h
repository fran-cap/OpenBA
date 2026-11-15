// src/diag/crash_handler.h — the exe's crash handler (Windows: src/diag/win/crash_handler_win.cpp).
//
// THE TRIAGE CONTRACT (tools/boot_state.sh, the triage-run skill): a second-chance fault
// writes standalone_crash.log (write-through) and exits with code 3; a pure-virtual call
// exits 4, std::terminate exits 5; a first-chance fail-fast is logged and passed on. The
// POSIX twin (sigaction + backtrace) is src/diag/posix/crash_handler_posix.cpp (D4_RULES C10).
#pragma once

// SetUnhandledExceptionFilter(CrashHandler) and AddVectoredExceptionHandler(1,
// FirstChanceFailFast) -- the first VEH also routes hw_watch's single steps.
void CrashHandler_Install();
// Open standalone_crash.log (CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH; a pid-suffixed name
// when a zombie holds it). Idempotent.
void CrashHandler_OpenLog();
// std::set_terminate: an uncaught C++ exception or a terminate() logs a stack and exits 5.
void CrashHandler_InstallTerminate();
