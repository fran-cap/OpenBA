#pragma once
// ============================================================================
// src/platform/posix/posix_path.h -- the PathChar <-> syscall spelling (Phase D.4 POSIX)
// ============================================================================
// Only the posix, darwin and android platform files (and tests/unit/platform_mac_data_test.cpp)
// include this. The engine hands the file primitives
// `Platform::PathChar` strings (UTF-16, char16_t: System::WideString's unit) spelled
// the engine's way: System::Filename::AdjustDelimiters (0x1800c6970) has turned every
// '/' into '\'. A POSIX syscall wants UTF-8 with '/'. These helpers are the one place
// that conversion happens (plans/THREADS.md F.5):
//
//   * '\' -> '/'. A POSIX absolute path therefore reaches the engine as "\home\...":
//     GetRootDirectory feeds /proc/self/exe through Filename, and the leading '\'
//     comes back here as the leading '/'. No ':' is involved (the drive / package
//     marker the Windows spelling carries at index 1 is simply absent).
//   * UTF-16 -> UTF-8 by WideString::AsUTF8's rule (0x1800cacd0): a surrogate pair is
//     one 4-byte sequence, a lone surrogate encodes to zero bytes.
//   * UTF-8 -> UTF-16 for what the OS hands back (directory entries, the exe path):
//     a malformed byte becomes U+FFFD.
//
// std::string only, never System::String: the settings file layer calls these
// before the engine allocator exists (platform_settings.h, ALLOCATOR-FREE).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform_file.h"

namespace Platform {
namespace Posix {

// Append the UTF-8 form of one code point.
inline void AppendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

// `len` UTF-16 units -> UTF-8, AsUTF8's rule. `slashes`: '\' becomes '/'.
inline std::string Utf16ToUtf8(const PathChar* w, size_t len, bool slashes) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        uint32_t c = static_cast<uint16_t>(w[i]);
        if (c >= 0xD800 && c < 0xDC00) {
            if (i + 1 >= len) continue;                        // lone high
            const uint32_t c2 = static_cast<uint16_t>(w[i + 1]);
            if (c2 < 0xDC00 || c2 >= 0xE000) continue;         // lone high
            c = 0x10000u + (((c - 0xD800u) << 10) | (c2 - 0xDC00u));
            ++i;
        } else if (c >= 0xDC00 && c < 0xE000) {
            continue;                                          // lone low
        }
        if (slashes && c == '\\') c = '/';
        AppendUtf8(out, c);
    }
    return out;
}

inline size_t PathLength(const PathChar* w) {
    size_t n = 0;
    while (w[n] != 0) ++n;
    return n;
}

// The syscall spelling of an engine path: UTF-8, '\' -> '/'.
inline std::string ToNative(const PathChar* path) {
    return Utf16ToUtf8(path, PathLength(path), true);
}

// UTF-8 -> UTF-16. A malformed or truncated sequence becomes U+FFFD.
inline std::u16string Utf8ToUtf16(const char* s, size_t len) {
    std::u16string out;
    out.reserve(len);
    size_t i = 0;
    while (i < len) {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        uint32_t c;
        size_t extra;
        if (b < 0x80)                { c = b;        extra = 0; }
        else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; extra = 1; }
        else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; extra = 2; }
        else if ((b & 0xF8) == 0xF0) { c = b & 0x07; extra = 3; }
        else                         { out.push_back(u'\xFFFD'); ++i; continue; }
        bool ok = true;   // a truncated sequence fails here (i + k reaches len)
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= len) { ok = false; break; }
            const uint8_t t = static_cast<uint8_t>(s[i + k]);
            if ((t & 0xC0) != 0x80) { ok = false; break; }
            c = (c << 6) | (t & 0x3F);
        }
        if (!ok) { out.push_back(u'\xFFFD'); ++i; continue; }
        i += extra + 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
    }
    return out;
}

// Copy a UTF-16 string into a fixed PathChar buffer of `cap` units, NUL-terminated,
// truncating. The units written, without the NUL.
inline uint32_t CopyOut(const std::u16string& s, PathChar* buf, uint32_t cap) {
    if (cap == 0) return 0;
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    for (size_t i = 0; i < n; ++i) buf[i] = s[i];
    buf[n] = 0;
    return static_cast<uint32_t>(n);
}

}  // namespace Posix
}  // namespace Platform
