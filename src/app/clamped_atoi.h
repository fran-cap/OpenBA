// src/app/clamped_atoi.h -- atoi for user-editable text (settings.cfg, BANISHED_* environment).
//
// Review F-087: atoi's behaviour is undefined when the value cannot be represented in an int,
// and these strings are whatever the user typed. ClampedAtoi parses as atoi does (leading
// white space, an optional sign, decimal digits up to the first other character, 0 when there
// are none) with strtoll, whose out-of-range result is defined (LLONG_MAX / LLONG_MIN), then
// clamps to the int range. Text in range reads exactly as before.
#ifndef BANISHED_APP_CLAMPED_ATOI_H
#define BANISHED_APP_CLAMPED_ATOI_H

#include <climits>
#include <cstdlib>

inline int ClampedAtoi(const char* text) {
    const long long v = std::strtoll(text, nullptr, 10);
    if (v > INT_MAX)
        return INT_MAX;
    if (v < INT_MIN)
        return INT_MIN;
    return static_cast<int>(v);
}

#endif  // BANISHED_APP_CLAMPED_ATOI_H
