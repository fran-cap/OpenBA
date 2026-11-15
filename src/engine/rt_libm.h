#pragma once
// ============================================================================
// src/engine/rt_libm.h — the transcendental seam for ported Runtime math
// ============================================================================
//
// THE PROBLEM. Runtime-x64.dll's math routines call its own statically-linked
// MSVC CRT. BATCH 4 named all six in retail Ghidra (they were FUN_ until then)
// and each is defined in src/engine/crt/msvc_libm.cpp, so they are now ordinary
// ported functions rather than a private arrangement of this file:
//   0x180128110 Crt_sqrtf   0x180127d90 Crt_sinf    0x180126d10 Crt_cosf
//   0x180126680 Crt_atan2f  0x180127560 Crt_powf    0x180127010 Crt_fmodf
// `Crt_fmodf` has no seam entry below: its one caller (SunLight::GetSunCamera)
// calls MsvcLibm::Fmodf directly.
//
// The five with seam entries below are still routed through it:
// Crt_sqrtf, Crt_sinf, Crt_cosf, Crt_atan2f, Crt_powf (added by Wave 8 lane E for
// Utility::PerlinNoise::Generate — the frequency/amplitude terms are
// pow(2, i) and pow(persistence, i)). Our ports are compiled by MinGW GCC against a
// different libm. Where the two disagree they disagree in the last bit or two,
// which is invisible in gameplay, and a last-bit difference in a routine that feeds
// world generation is not automatically harmless, because generation is
// seed-deterministic and small differences compound.
//
// THE WRONG FIX is to call the DLL's helpers by address and call the port done.
// It makes the oracle green by construction while quietly making the port
// depend on the very DLL we are removing: PLAN.md §4.1 decision 1 is that
// OpenBA.exe must eventually run with Runtime-x64.dll absent, on non-Windows
// systems. A port that cannot survive the DLL's removal is not a port.
//
// SO: the five pointers below are the ported MSVC CRT bodies in
// src/engine/crt/msvc_libm.cpp, the only implementation.
//
// Installation happens once, in RtLibm::Init(), called from RuntimeInit()
// (src/app/runtime_init.cpp) before anything caches a pointer.
// The indirection is a plain function-pointer call with no branch, so the hot
// path costs one indirect call and no per-call env or guard-variable check.
// ============================================================================

// Set from RuntimeInit(). Defaults are installed at static-init time, so a
// caller that runs before RtLibm::Init() already gets the ported bodies rather than null.
namespace RtLibm {

extern float (*Sqrt)(float);
extern float (*Sin)(float);
extern float (*Cos)(float);
extern float (*Atan2)(float y, float x);
extern float (*Pow)(float base, float exp);

// Installs the ported CRT math bodies behind the five pointers above. Safe to
// call more than once.
void Init();

}  // namespace RtLibm

// ==== B3R W0-P0c: Runtime bodies called by name ====
// ENDGAME B.3R (B3R_RULES R5): each body below (retail address in its trailing
// comment) is declared here, copied from its definition (linkage, namespace,
// types), so callers outside its module call it by name.
#include <cstddef>   // size_t (RtCrt_Qsort)

// ---- crt/msvc_qsort.cpp
extern "C" void RtCrt_Qsort_18007f050(void* base, size_t num, size_t width, int (*comp)(const void*, const void*));  // 0x18007f050

// ---- crt/msvc_libm.cpp (B3R RT-Core: external linkage given for the reader)
extern "C" float RtCrt_Powf(float b, float e);  // 0x180127560

#include "rt_crt.h"   // rtcrt::StrICmp (crt/msvc_stricmp.cpp, 0x18007eefc)
// ==== end B3R W0-P0c ====
