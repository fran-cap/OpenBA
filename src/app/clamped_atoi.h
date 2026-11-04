// src/app/clamped_atoi.h -- atoi / atof / sscanf for user-editable text (settings.cfg,
// BANISHED_* environment, command-line arguments).
//
// Review F-087: atoi's and atof's behaviour is undefined when the value cannot be represented,
// and so is sscanf's %d / %f, and so is a cast of an out-of-range double to float or int. These
// strings are whatever the user typed. The readers below keep atoi's / atof's parse (leading
// white space, an optional sign, the number up to the first other character, 0 when there is
// none) on strtoll / strtod, whose out-of-range results are defined, and clamp. Text in range
// reads exactly as before, so every documented default and range is unchanged.
#ifndef BANISHED_APP_CLAMPED_ATOI_H
#define BANISHED_APP_CLAMPED_ATOI_H

#include <cfloat>
#include <climits>
#include <cstdlib>

// atoi, clamped to the int range. Null reads as empty text (0).
inline int ClampedAtoi(const char* text) {
    if (text == nullptr)
        return 0;
    const long long v = std::strtoll(text, nullptr, 10);
    if (v > INT_MAX)
        return INT_MAX;
    if (v < INT_MIN)
        return INT_MIN;
    return static_cast<int>(v);
}

// atoi, clamped to [lo, hi] (lo <= hi). Garbage reads as 0 and then clamps like any value.
inline int ClampedAtoiRange(const char* text, int lo, int hi) {
    const int v = ClampedAtoi(text);
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

// The harness readers' range (src/diag and the BANISHED_* test fixtures): a frame number, a
// period, a count or a button id. +-1e9 frames is over six months at 60 fps, and a sum of two
// such values (frameNum + delay) cannot overflow an int.
constexpr int kHarnessIntLimit = 1000000000;

inline int HarnessAtoi(const char* text) {
    return ClampedAtoiRange(text, -kHarnessIntLimit, kHarnessIntLimit);
}

// A finite float from a double: NaN reads as 0, anything beyond the float range clamps to
// +-FLT_MAX (infinity included).
inline float ClampedFloat(double v) {
    if (v != v)
        return 0.0f;
    if (v > FLT_MAX)
        return FLT_MAX;
    if (v < -FLT_MAX)
        return -FLT_MAX;
    return static_cast<float>(v);
}

// atof, as a finite float. Null reads as empty text (0).
inline float ClampedAtof(const char* text) {
    if (text == nullptr)
        return 0.0f;
    return ClampedFloat(std::strtod(text, nullptr));
}

// sscanf's %d on a cursor: leading white space, a sign, decimal digits. On a conversion the
// value (clamped to [lo, hi]) is stored, the cursor moves past the number and the result is
// true; otherwise nothing is stored or moved and the result is false.
inline bool ScanClampedInt(const char*& cursor, int* out, int lo = INT_MIN, int hi = INT_MAX) {
    if (cursor == nullptr)
        return false;
    char* end = nullptr;
    const long long v = std::strtoll(cursor, &end, 10);
    if (end == cursor)
        return false;
    if (v < lo)
        *out = lo;
    else if (v > hi)
        *out = hi;
    else
        *out = static_cast<int>(v);
    cursor = end;
    return true;
}

// sscanf's %f on a cursor, as a finite float (ClampedFloat). Same contract as ScanClampedInt.
inline bool ScanClampedFloat(const char*& cursor, float* out) {
    if (cursor == nullptr)
        return false;
    char* end = nullptr;
    const double v = std::strtod(cursor, &end);
    if (end == cursor)
        return false;
    *out = ClampedFloat(v);
    cursor = end;
    return true;
}

// A literal (non-space) character in a sscanf format: it must be the next character.
inline bool ScanLiteral(const char*& cursor, char c) {
    if (cursor == nullptr || *cursor != c)
        return false;
    ++cursor;
    return true;
}

// sscanf(text, "%d,%d,...", ...) for up to `count` integers: the number converted (0 for
// empty or garbage text, where sscanf would give EOF or 0). Entries after the last conversion
// are left as they were, as sscanf leaves them.
inline int ScanClampedInts(const char* text, int* out, int count, int lo = INT_MIN, int hi = INT_MAX) {
    const char* p = text;
    int n = 0;
    while (n < count) {
        if (n > 0 && !ScanLiteral(p, ','))
            break;
        if (!ScanClampedInt(p, &out[n], lo, hi))
            break;
        ++n;
    }
    return n;
}

// sscanf(text, "%f,%f,...", ...) for up to `count` floats, each finite (ClampedFloat). Same
// contract as ScanClampedInts.
inline int ScanClampedFloats(const char* text, float* out, int count) {
    const char* p = text;
    int n = 0;
    while (n < count) {
        if (n > 0 && !ScanLiteral(p, ','))
            break;
        if (!ScanClampedFloat(p, &out[n]))
            break;
        ++n;
    }
    return n;
}

#endif  // BANISHED_APP_CLAMPED_ATOI_H
