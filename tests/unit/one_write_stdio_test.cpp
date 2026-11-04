// tests/unit/one_write_stdio_test.cpp -- src/app/one_write_stdio.h (HITCH_STDERR): fprintf /
// vfprintf / printf to the unbuffered stderr are routed through one fwrite and must write the
// same bytes the CRT's own formatter writes, including the long-line fallback and the text-mode
// newline translation. fd 2 is pointed at a temporary file for the duration of the test.
#include "unit.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <type_traits>

#if defined(__MINGW32__)
#include <fcntl.h>
#include <io.h>

namespace {

void ViaVfprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

std::string Expected(const char* fmt, ...) {
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

std::string ReadAll(const char* path) {
    std::string s;
    if (FILE* f = std::fopen(path, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

}  // namespace

UNIT_TEST(one_write_stdio_stderr_bytes_match_the_crt_formatter) {
#ifndef fprintf
    CHECK(!"one_write_stdio.h is not force-included");
    return;
#endif
    const char* path = "unit_one_write_stdio.tmp";
    std::fflush(stderr);
    const int saved = _dup(_fileno(stderr));
    const int fd = _open(path, _O_CREAT | _O_TRUNC | _O_WRONLY | _O_TEXT, 0644);
    CHECK(saved >= 0 && fd >= 0);
    if (saved < 0 || fd < 0) return;
    _dup2(fd, _fileno(stderr));
    _close(fd);

    const std::string longArg(3000, 'x');   // past the 2 KB buffer: the vfprintf fallback
    int r1 = fprintf(stderr, "[T] %d %s %.3f %llx %5.1f%%\n", -42, "abc", 3.14159, 0xdeadbeefULL, 12.25);
    int r2 = std::fprintf(stderr, "[T] a line with no conversions\n");
    ViaVfprintf("[T] vfprintf %u|%-6s|%c\n", 7u, "ab", 'z');
    int r3 = fprintf(stderr, "[T] long %s end\n", longArg.c_str());
    int r4 = fprintf(stderr, "%s", "");                   // empty: zero bytes, returns 0
    std::fflush(stderr);
    _dup2(saved, _fileno(stderr));
    _close(saved);

    std::string want = Expected("[T] %d %s %.3f %llx %5.1f%%\n", -42, "abc", 3.14159, 0xdeadbeefULL, 12.25);
    CHECK_EQ(r1, (int)want.size());
    want += "[T] a line with no conversions\n";
    CHECK_EQ(r2, 31);
    want += Expected("[T] vfprintf %u|%-6s|%c\n", 7u, "ab", 'z');
    const std::string longLine = "[T] long " + longArg + " end\n";
    CHECK_EQ(r3, (int)longLine.size());
    want += longLine;
    CHECK_EQ(r4, 0);
    std::string wantCrlf;                                 // the text-mode fd translates \n
    for (char c : want) { if (c == '\n') wantCrlf += '\r'; wantCrlf += c; }
    const std::string got = ReadAll(path);
    CHECK_EQ(got.size(), wantCrlf.size());
    CHECK(got == wantCrlf);
    std::remove(path);
}

UNIT_TEST(one_write_stdio_other_streams_are_untouched) {
    FILE* f = std::tmpfile();
    CHECK(f != nullptr);
    if (!f) return;
    const int r = fprintf(f, "%d-%s\n", 5, "five");
    CHECK_EQ(r, 7);
    std::rewind(f);
    char buf[32] = {};
    CHECK(std::fgets(buf, sizeof(buf), f) != nullptr);
    CHECK_STREQ(buf, "5-five\n");
    std::fclose(f);
}

// Review F-047: the macros expand to `int32_t(wrapper(...))` rather than adding the wrappers to
// namespace std (undefined behaviour). The three call spellings in the tree must each compile
// and return fprintf's int; a "simplified" macro that drops the int32_t breaks this file.
UNIT_TEST(one_write_stdio_call_spellings_reach_the_wrapper) {
#ifndef fprintf
    CHECK(!"one_write_stdio.h is not force-included");
    return;
#endif
    FILE* f = std::tmpfile();
    CHECK(f != nullptr);
    if (!f) return;
    const auto qualified = std::fprintf(f, "%d", 12);
    const auto global = ::fprintf(f, "%s", "ab");
    const auto plain = fprintf(f, "%c", 'z');
    static_assert(std::is_same<decltype(std::fprintf(f, "")), int>::value, "std::fprintf is int");
    static_assert(std::is_same<decltype(::fprintf(f, "")), int>::value, "::fprintf is int");
    static_assert(std::is_same<decltype(fprintf(f, "")), int>::value, "fprintf is int");
    CHECK_EQ(qualified, 2);
    CHECK_EQ(global, 2);
    CHECK_EQ(plain, 1);
    std::rewind(f);
    char buf[16] = {};
    CHECK(std::fgets(buf, sizeof(buf), f) != nullptr);
    CHECK_STREQ(buf, "12abz");
    std::fclose(f);
}
#endif
