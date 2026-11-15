// src/diag/stall_debug.h
// Lightweight timing instrumentation for hunting splash-screen flashes.
//
// Purpose: detect main-thread stalls (gaps between consecutive Update/Draw
// calls) and time discrete phases inside Startup/Load slots. The goal is to
// identify any synchronous operation on the main thread that takes longer
// than ~16ms (one frame at 60fps) and would cause a visible flash because
// the swap chain shows the last presented backbuffer for that gap.
//
// Usage:
//   StallDebug::Initialize();           // call once at boot
//   StallDebug::Tick("MenuDLT::Update"); // call per-frame; logs if gap > threshold
//   {
//     StallDebug::Phase phase("LoadScene_PhaseA");  // RAII: logs duration on dtor
//     ...
//   }
//
// Output goes to stdout (standalone_debug.log). Each line is prefixed with
// `[Stall]` (Mark's with `[BootTime]`) so it can be greppped from the log.

#pragma once


#include <cstdio>

namespace StallDebug {

// Call once at boot (e.g. from WinMain before MainLoop::Run).
void Initialize();

// Returns milliseconds since Initialize().
double NowMs();

// Per-frame heartbeat. Logs a [Stall] line when the gap from the last Tick()
// call exceeds the threshold (default 30ms). Pass a string identifying the
// caller (e.g. "MenuDLT::Update", "MenuDLT::Draw").
void Tick(const char* tag);

// Override the stall threshold. Default 30ms (≈2 frames at 60fps).
void SetStallThresholdMs(double thresholdMs);

// Boot-sequence stopwatch. One [BootTime] line per call, giving both the
// delta since the previous Mark and the absolute time since Initialize().
// Cheaper to sprinkle than a Phase scope when what you want is a profile of a
// long straight-line sequence (Init / RunGameMain) rather than one nested span.
void Mark(const char* what);

// Discrete phase timing: BEGIN / END lines with the time; the Phase dtor also prints the duration.
void BeginPhase(const char* name);
void EndPhase(const char* name);

// RAII wrapper.
struct Phase {
    const char* name;
    double startMs;
    explicit Phase(const char* n);
    ~Phase();
    Phase(const Phase&) = delete;
    Phase& operator=(const Phase&) = delete;
};

} // namespace StallDebug

