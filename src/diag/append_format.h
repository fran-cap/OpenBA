#pragma once
// src/diag/append_format.h -- a bounded "append one formatted field to a line" for the
// diagnostics' fixed stack buffers (scene_watch's LodCensus lines).
//
// snprintf returns the length the field WOULD have had, not what it wrote. A loop that does
// `off += snprintf(buf + off, sizeof(buf) - off, ...)` steps past the end once a field is cut
// off, and the next `sizeof(buf) - off` wraps to a huge size_t: that call writes past the buffer.
// AppendFormat clamps the offset instead (review 2026-11-03, F-011).

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace Diag {

// Appends at buf + off, never writing past buf[cap - 1]; returns the new offset, at most cap - 1.
// A full buffer, a negative offset or an encoding error leaves the buffer as it is.
__attribute__((format(printf, 4, 5)))
inline int AppendFormat(char* buf, size_t cap, int off, const char* fmt, ...) {
    if (cap == 0) return off;
    const int last = static_cast<int>(cap - 1);   // the terminator's slot
    if (off < 0 || off >= last) return off < 0 ? off : last;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf + off, cap - static_cast<size_t>(off), fmt, ap);
    va_end(ap);
    if (n < 0) return off;
    return n < last - off ? off + n : last;
}

}  // namespace Diag
