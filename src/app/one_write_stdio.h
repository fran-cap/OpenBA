// src/app/one_write_stdio.h -- one write per diagnostic line on the unbuffered stdout / stderr.
//
// Force-included into every translation unit of OpenBA and unit (CMakeLists.txt,
// BANISHED_ONE_WRITE_STDIO), the same mechanism as log_strip.h. Vendored third-party sources
// (ASSAO, Dear ImGui) are compiled with BANISHED_THIRD_PARTY_SOURCE and see nothing of it.
// Hand-in: docs/extensions/handins/HITCH_STDERR.md.
//
// Why. main.cpp opens standalone_debug.log / standalone_error.log with
// setvbuf(..., _IONBF, 0) so every line survives an abnormal exit. MinGW's printf family
// (__mingw_vfprintf -> __pformat_putc -> fputc) writes an UNBUFFERED stream one character at
// a time: one WriteFile per character, ~150-250 us for a 90-character line. That holds for
// fprintf, vfprintf and printf alike, even for a format with no conversions (MinGW's fprintf
// is an inline overload, so GCC never turns it into fputs). fputs and fwrite stage the whole
// call through a temporary buffer: one WriteFile, ~4 us.
//
// What. fprintf / vfprintf to stdout or stderr format into a stack buffer with vsnprintf
// (the same __mingw_pformat engine, so the same bytes) and hand the line to the stream in one
// fwrite. Every other FILE* goes to the real vfprintf, untouched. A line that does not fit the
// buffer falls back to the real vfprintf (no heap: these run in crash and hang paths). The
// stream stays unbuffered: a line is written before the call returns, as before. printf is
// routed the same way where it is not stripped (debug / og; log_strip.h removes it in perf
// and release).
//
// How. Function-like macros, so `std::fprintf(`, `::fprintf(` and `fprintf(` all reach the
// wrapper (each expands to `int32_t(wrapper(...))`, below), while non-call spellings -- `&fprintf`,
// `__attribute__((format(printf, 1, 2)))` (imgui.h, text_reader.cpp) -- are left alone.
//
// MinGW only: glibc already stages an unbuffered stream's printf through one buffer.
#ifndef BANISHED_ONE_WRITE_STDIO_H
#define BANISHED_ONE_WRITE_STDIO_H

#if defined(__MINGW32__) && !defined(BANISHED_THIRD_PARTY_SOURCE)
// The real declarations first, so any later <cstdio> / <stdio.h> is a guarded no-op and
// never sees the macros below.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <stdint.h>
#include <stdio.h>

__attribute__((format(gnu_printf, 2, 0)))
inline int BanishedOneWriteVfprintf(FILE* f, const char* fmt, va_list ap) {
    if (f != stderr && f != stdout) return vfprintf(f, fmt, ap);   // (the real one: no macro yet)
    char buf[2048];
    va_list probe;
    va_copy(probe, ap);
    const int n = vsnprintf(buf, sizeof(buf), fmt, probe);
    va_end(probe);
    if (n < 0 || n >= (int)sizeof(buf)) return vfprintf(f, fmt, ap);
    return fwrite(buf, 1, (size_t)n, f) == (size_t)n ? n : -1;
}

__attribute__((format(gnu_printf, 2, 3)))
inline int BanishedOneWriteFprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int n = BanishedOneWriteVfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

__attribute__((format(gnu_printf, 1, 2)))
inline int BanishedOneWritePrintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int n = BanishedOneWriteVfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

// NOT `namespace std { using ::BanishedOneWriteFprintf; ... }` (review F-047): adding a
// declaration to namespace std is undefined behaviour ([namespace.std]). Each macro instead
// expands to a functional cast to int32_t, a name that std, the global namespace and plain
// lookup all have (<cstdint> / <stdint.h>, included above), so `std::fprintf(`, `::fprintf(`
// and `fprintf(` stay valid: `std::int32_t(BanishedOneWriteFprintf(...))`. MinGW's int32_t is
// int, so the result type is fprintf's own.
static_assert(sizeof(int32_t) == sizeof(int) && static_cast<int32_t>(-1) < 0, "int32_t is int");

#define fprintf(...)  int32_t(BanishedOneWriteFprintf(__VA_ARGS__))
#define vfprintf(...) int32_t(BanishedOneWriteVfprintf(__VA_ARGS__))
#ifndef BANISHED_STRIP_LOGS
#define printf(...)   int32_t(BanishedOneWritePrintf(__VA_ARGS__))
#endif

#endif  // __MINGW32__ && !BANISHED_THIRD_PARTY_SOURCE
#endif  // BANISHED_ONE_WRITE_STDIO_H
