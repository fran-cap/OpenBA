// ============================================================================
// src/engine/audio/audio_cvtt.h — the x86-64 truncating float -> integer
// conversions, with their out-of-range result defined
// ============================================================================
// Retail converts data-derived seconds to ticks (and a byte position to an int)
// with CVTTSD2SI / CVTTSS2SI. Those instructions return the "integer
// indefinite" value (INT64_MIN / INT32_MIN) for NaN and for anything outside the
// destination's range. The C++ `static_cast` is the same instruction for every
// in-range value, but undefined behaviour out of range (review F-118, F-136:
// a serialized _maxLength, startSeconds or _durationSeconds that is huge, inf or
// NaN). These helpers are the cast for in-range values and the instruction's
// result otherwise, so valid data converts bit-identically and the UB is gone.
// NOT RETAIL as C++: retail has no check, the instruction defines the result.
// ============================================================================
#pragma once

#include <cstdint>

namespace Audio {

// CVTTSD2SI r64, xmm (double -> int64, truncate toward zero).
inline int64_t CvttSd2Si64(double v) {
    // [-2^63, 2^63) is the representable range; NaN fails both compares.
    if (v >= -9223372036854775808.0 && v < 9223372036854775808.0)
        return static_cast<int64_t>(v);
    return INT64_MIN;
}

// CVTTSS2SI r32, xmm (float -> int32, truncate toward zero).
inline int32_t CvttSs2Si32(float v) {
    // [-2^31, 2^31) is the representable range; NaN fails both compares.
    if (v >= -2147483648.0f && v < 2147483648.0f)
        return static_cast<int32_t>(v);
    return INT32_MIN;
}

// `nowTicks % loopTicks` as Device::PlayPlatform forms it (retail 0x1800de84c
// CQO ; 0x1800de871 IDIV R8). NOT RETAIL (review F-118, undefined behaviour):
// retail has no zero test and a zero-length loop (a SoundBuffer whose
// _durationSeconds is 0 or below one tick) raises #DE; the remainder is 0 then,
// the position of a loop that has no length. INT64_MIN % -1 (the other IDIV
// fault) is 0 as well.
inline int64_t LoopRemainderTicks(int64_t nowTicks, int64_t loopTicks) {
    if (loopTicks == 0 || loopTicks == -1)
        return 0;
    return nowTicks % loopTicks;
}

}  // namespace Audio
