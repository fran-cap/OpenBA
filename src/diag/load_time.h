// ============================================================================
// src/diag/load_time.h -- [LoadTime] stage stamps (NOT RETAIL; block LOADSTAMP)
// ============================================================================
// One stderr line per load-stage boundary, always on (no flag):
//
//     [LoadTime] <stage> t=<ms since process start, 1 decimal> thread=<name>[ <detail>]
//
// <name> is "main", "loader", another registered role (diag_threads.h: persist,
// audio, pathfinder, path, render, cull, parallel, thread) or "tid<N>" for an unregistered
// thread. A stamp marks the START of the step it names; the step's length is the
// next stamp's t minus this one (on the same thread). The origin is captured by this
// file's static initialiser, i.e. before main, within a few ms of process start.
//
// Stamps sit at load boundaries only (boot, new game, load save, loop switches):
// a few dozen lines per load, never a per-frame path. A stamp is a print and
// nothing else: it reads no game state and changes none. fprintf(stderr) survives
// the log-stripped flavours (only `printf` is macro'd away, src/app/log_strip.h).
// The stage list and how to read it: docs/extensions/handins/LOADSTAMP.md.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <typeinfo>

namespace LoadTime {

// ms since process start (the static-init origin).
double NowMs();
// A Platform::PerfCounter value -> ms since process start.
double CounterToMs(int64_t counter);

// One [LoadTime] line. `detail` (optional) is appended after the thread field.
void Stamp(const char* stage, const char* detail = nullptr);
// The same, with a polymorphic object's demangled dynamic type as the detail
// (e.g. which UpdateLoop a loop switch starts).
void StampType(const char* stage, const std::type_info& type);

// FNV-1a 64, chained through `h` (start with kHashSeed). Used by the print-only
// [WorldHash] lines (the loading session's world-identity proof). Bytes are copied out
// with memcpy, so no pointer reinterpretation is needed.
constexpr uint64_t kHashSeed = 0xcbf29ce484222325ull;
inline uint64_t HashBytes(const unsigned char* p, uint64_t n, uint64_t h = kHashSeed) {
    for (uint64_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}
template <typename T>
uint64_t HashArray(const T* data, uint64_t count, uint64_t h = kHashSeed) {
    unsigned char buf[sizeof(T)];
    for (uint64_t i = 0; i < count; ++i) {
        std::memcpy(buf, &data[i], sizeof(T));
        h = HashBytes(buf, sizeof(T), h);
    }
    return h;
}

}  // namespace LoadTime
