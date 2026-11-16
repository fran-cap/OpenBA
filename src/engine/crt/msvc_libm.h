#pragma once
// ============================================================================
// src/engine/crt/msvc_libm.h -- Runtime-x64.dll's own math routines, ported
// ============================================================================
//
// WHY THIS FILE EXISTS. Runtime-x64.dll statically links the MSVC CRT, so every
// float the shipped engine computes from sqrtf/sinf/cosf/atan2f/powf comes out
// of five specific bodies at five fixed addresses. Our ports were calling
// MinGW/UCRT's libm instead, which disagrees with those bodies in the last bit
// on roughly 0.1-0.2% of ordinary arguments. That is invisible on screen and
// very visible in the Wave 0 diff oracle:
//
//     Math::Vector3::FromYawPitch                 calls=3838   divergences=16
//     Math::Transform::FromYawPitchOffset(...)    calls=45473  divergences=88
//
// Both went silent when routed to the DLL's own CRT, which proved the divergence was
// libm rounding and nothing else -- but that escape hatch calls into the DLL by
// address, so it dies at cutover and takes the fix with it. Porting the five
// bodies fixes the divergence AND removes five ResolveRuntimeRaw dependencies.
//
// These are ordinary functions at known addresses and they are ported the same
// way as anything else in this tree: from the DISASSEMBLY, not from Ghidra's C,
// with the coefficient tables
// lifted as raw bit patterns by tools/extract_crt_tables.py rather than retyped
// as decimals. The cos series coefficients are NOT the sin ones -- e.g. cos uses
// 0x3f81111111110bb3 where sin uses 0x3f81111111111111 -- which is exactly the
// difference a retyped "1.0/120.0" would erase.
//
//   Retail address   This file
//   0x180128110      Sqrtf   (hardware sqrtss + the CRT's domain-error path)
//   0x180127d90      Sinf
//   0x180126d10      Cosf
//   0x180126680      Atan2f
//   0x180127560      Powf
//   0x180127010      Fmodf   (BATCH 4; only DLL caller is SunLight::GetSunCamera)
//   0x18012a1a4      RemPio2 (Payne-Hanek helper shared by Sinf/Cosf)
//
// FAITHFULNESS BOUNDARY. The retail bodies also call the CRT's error reporting
// (_invalid_parameter / _matherr / errno / feraiseexcept) on domain and range
// errors. Those calls do not change the returned value, and nothing in the game
// reads errno or the FP exception flags, so they are NOT ported; every RETURN
// VALUE is. Each omission is marked with a `// retail: reports <what>` comment
// at the site.
//
// ORDER OF OPERATIONS IS THE POINT. Floating point is not associative, so these
// bodies transcribe the retail instruction sequence, including temporaries that
// look redundant. Do not "simplify" an expression here; it will change results.
// The build must not contract a multiply-add into an FMA either -- msvc_libm.cpp
// has a static_assert-equivalent #error for an FMA-enabled x86 build, and
// CMakeLists.txt passes -ffp-contract=off (aarch64 has FMA instructions).
// ============================================================================

namespace MsvcLibm {

float Sqrtf(float x);
float Sinf(float x);
float Cosf(float x);
float Atan2f(float y, float x);
float Powf(float base, float exp);
float Fmodf(float x, float y);            // retail 0x180127010 (BATCH 4)

// ---------------------------------------------------------------------------
// Branch-entry counters for the bit-exactness sweep's EFFECTIVENESS CONTROL.
// Nothing in the game build sees any of this: the counters are only compiled
// when MSVC_LIBM_COVERAGE is defined, which nothing in the tree defines. A sweep
// that never enters argument reduction reports "0 divergences"
// exactly as loudly as one that does, so the counts are part of the evidence.
// ---------------------------------------------------------------------------
#ifdef MSVC_LIBM_COVERAGE
struct Coverage {
    unsigned long long
        remPio2 = 0, remPio2RoundUp = 0, remPio2ExtraLimb = 0,
        trigNaN = 0, trigInf = 0,
        sqrtHardware = 0, sqrtNegative = 0, sqrtNaN = 0,
        sqrtPosInf = 0, sqrtNegInf = 0,
        sinSmallPoly = 0, sinCubicTerm = 0, sinIdentity = 0, sinNegArg = 0,
        sinCodyWaite = 0, sinExtendedSplit = 0, sinPayneHanek = 0,
        sinReducedTiny = 0, sinReducedSmall = 0,
        sinFullSinSeries = 0, sinFullCosSeries = 0, sinQuadrantNegate = 0,
        cosSmall = 0, cosNegArg = 0, cosCodyWaite = 0, cosExtendedSplit = 0,
        cosPayneHanek = 0, cosReducedTiny = 0, cosReducedSmall = 0,
        cosFullSinSeries = 0, cosFullCosSeries = 0, cosQuadrantNegate = 0,
        atanNanX = 0, atanNanY = 0, atanZeroY = 0, atanZeroXNegY = 0,
        atanRatioHuge = 0, atanRatioZero = 0, atanSubnormal = 0,
        atanPlainDivide = 0, atanNearPi = 0, atanBothInf = 0, atanSwap = 0,
        atanTablePath = 0, atanPolyPath = 0, atanTinyRatio = 0, atanNegX = 0,
        powCore = 0, powOverflow = 0, powUnderflow = 0, powExpZero = 0,
        powExpOne = 0, powNegBase = 0, powNegBaseOddInt = 0,
        powNegBaseEvenInt = 0, powNegBaseNonInt = 0, powBaseOne = 0,
        powBaseZero = 0, powBaseMinusOne = 0, powBaseInfNan = 0,
        powExpHuge = 0, powExpTiny = 0;
};
extern Coverage g_cov;
#endif

}  // namespace MsvcLibm
