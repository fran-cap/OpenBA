// src/engine/crt/msvc_libm.cpp -- see msvc_libm.h for why this file exists.
//
// Transcribed from the Runtime-x64.dll disassembly (objdump -d -M intel), NOT
// from Ghidra's decompiled C. Coefficients come from msvc_libm_tables.h, which
// tools/extract_crt_tables.py reads straight out of the DLL's .rdata/.data as
// raw bit patterns.
//
// The retail address of each body is on its comment header. Operation order is
// transcribed instruction for instruction; do not "simplify" an expression --
// floating point is not associative and every reassociation is a last-bit bug.
#include "msvc_libm.h"
#include "rt_libm.h"
#include "msvc_libm_tables.h"

#include <cstdint>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#else
#include <cmath>
#endif

#if defined(__FMA__) || defined(__FMA4__)
#error "msvc_libm.cpp must not be compiled with FMA enabled: a contracted multiply-add rounds once where retail rounds twice."
#endif

// aarch64 contracts a*b+c into fmadd, which rounds once where retail rounds twice. The
// compiler macro __FP_FAST_FMA only says the hardware HAS fma (clang and GCC define it on
// every aarch64 target whatever the contraction mode), so it cannot be tested here.
// CMakeLists.txt passes -ffp-contract=off in the common flags; clang also honours the
// pragma, which keeps this file correct when it is built by hand.
#if defined(__aarch64__) && defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

namespace MsvcLibm {

// ---------------------------------------------------------------------------
// Optional branch-entry counters, compiled out entirely unless
// MSVC_LIBM_COVERAGE is defined (nothing in the tree defines it). They
// let the bit-exactness sweep report an EFFECTIVENESS CONTROL: how many cases
// actually entered each branch. A sweep that never reaches argument reduction
// reports "0 divergences" just as loudly as one that does.
// ---------------------------------------------------------------------------
#ifdef MSVC_LIBM_COVERAGE
Coverage g_cov;
#define COV(field) (++::MsvcLibm::g_cov.field)
#else
#define COV(field) ((void)0)
#endif

namespace {

inline uint32_t U32(float f)    { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline float    F32(uint32_t u) { float f;    std::memcpy(&f, &u, 4); return f; }
inline uint64_t U64(double d)   { uint64_t u; std::memcpy(&u, &d, 8); return u; }
inline double   F64(uint64_t u) { double d;   std::memcpy(&d, &u, 8); return d; }

// Every coefficient is loaded as a 64-bit pattern; keep it that way.
inline double K(uint64_t bits) { return F64(bits); }

// cvttsd2si / cvttpd2dq -- truncate toward zero.
inline int32_t TruncToI32(double d) {
#if defined(__SSE2__)
    return _mm_cvttsd_si32(_mm_set_sd(d));
#else
    const double v = std::trunc(d);
    return v >= -2147483648.0 && v < 2147483648.0 ? static_cast<int32_t>(v) : INT32_MIN;
#endif
}
// cvtpd2dq -- round to nearest, ties to even. powf uses this where sinf/cosf
// use the truncating form; they are not interchangeable.
inline int32_t RoundToI32(double d) {
#if defined(__SSE2__)
    return _mm_cvtsd_si32(_mm_set_sd(d));
#else
    const double v = std::nearbyint(d);
    return v >= -2147483648.0 && v < 2147483648.0 ? static_cast<int32_t>(v) : INT32_MIN;
#endif
}

constexpr uint64_t kSignBit64 = 0x8000000000000000ull;
constexpr uint64_t kAbsMask64 = 0x7fffffffffffffffull;

// ===========================================================================
// __rem_pio2 -- retail 0x18012a1a4
// ===========================================================================
// Payne-Hanek reduction against a 36-bit-limb expansion of 2/pi. Returns the
// reduced value and writes the quadrant. Reached from Sinf/Cosf only when
// |x| >= 500000.0, so for float inputs the limb index stays inside 0..6 (the
// table holds 14 limbs; retail would read past it for a double argument).
double RemPio2(uint64_t xbits, int32_t* quadrant) {
    constexpr uint64_t kLimbMask = 0xfffffffffull;   // 36 bits (retail r14)

    const int32_t e = (int32_t)((xbits >> 52) & 0x7ff) - 0x3ff;
    // Implicit bit plus the top 23 mantissa bits, as a 24-bit integer.
    const uint64_t m =
        ((xbits & 0x000fffffe0000000ull) | 0x0010000000000000ull) >> 29;

    // Retail computes q = e/36 with the 0x38e38e39 reciprocal multiply plus a
    // sign fixup, i.e. truncation toward zero -- which is what C division does.
    const int32_t q   = e / 36;
    const int32_t rem = e - 36 * q;

    // Four limbs of (2/pi * m), most significant first, carries propagated
    // downward. w[] mirrors retail's frame at [rsp+0x10 .. +0x30]: w[0] is the
    // most significant and w[4] is the zero sentinel that stops the scan.
    uint64_t w[6] = {0, 0, 0, 0, 0, 0};
    {
        const uint64_t* L = kTwoOverPiLimbs;
        uint64_t c = L[q + 3] * m;
        uint64_t d = L[q + 2] * m;
        uint64_t a = c;
        c >>= 36;
        d += c;
        const uint64_t w3 = a & kLimbMask;
        a = d;
        d >>= 36;
        const uint64_t t  = L[q + 1] * m + d;
        const uint64_t w2 = a & kLimbMask;
        const uint64_t w1 = t & kLimbMask;
        const uint64_t w0 = (L[q] * m + (t >> 36)) & kLimbMask;
        w[0] = w0; w[1] = w1; w[2] = w2; w[3] = w3;
    }

    COV(remPio2);
    const uint32_t oct     = (uint32_t)(((w[0] << 36) | w[1]) >> (35 - rem)) & 7u;
    const bool     roundUp = (oct & 1u) != 0;

    // `idx` is retail's r9: the index of the last limb folded into `frac`. The
    // limb after it supplies the low bits.
    int32_t  idx = 1;
    uint64_t frac;
    if (roundUp) {
        COV(remPio2RoundUp);
        *quadrant = (((int32_t)oct >> 1) + 1) & 3;
        frac = ((1ull << (36 - rem)) - 1) & ~w[1];
        while (frac < 0x10000ull && idx + 1 < 6) {
            COV(remPio2ExtraLimb);
            const uint64_t hi = frac << 36;
            ++idx;
            frac = (~w[idx] & kLimbMask) | hi;
        }
    } else {
        *quadrant = (int32_t)oct >> 1;
        frac = ((1ull << (36 - rem)) - 1) & w[1];
        while (frac < 0x10000ull && idx + 1 < 6) {
            COV(remPio2ExtraLimb);
            frac <<= 36;
            ++idx;
            frac |= w[idx];
        }
    }
    uint64_t tail = roundUp ? (~w[idx + 1] & kLimbMask) : w[idx + 1];

    // Normalise `frac` so bit 52 is set, counting the shift.
    int32_t lz = 0;
    while (frac < 0x400000000000ull)   { lz += 6; frac <<= 6; }
    while (frac < 0x10000000000000ull) { lz += 1; frac += frac; }

    tail >>= (36 - lz);
    const int32_t  outExp = rem - 36 * idx - lz;
    const uint64_t mant   = (tail | frac) & 0x000fffffffffffffull;
    uint64_t bits = ((uint64_t)(int64_t)(outExp + 0x34 + 0x3ff) << 52) | mant;
    if (roundUp) bits |= kSignBit64;

    return F64(bits) * K(kRemPio2Scale);
}

// The sin/cos NaN/Inf entries (retail 0x18012a5c0 / 0x18012a428) route into a
// shared reporter that returns the indefinite NaN for an infinite argument and
// the quieted argument for a NaN one. Only the returned VALUE is ported; the
// _invalid_parameter / errno reporting beside it changes nothing we read.
inline float TrigNonFinite(float x) {
    const uint32_t b = U32(x);
    if ((b & 0x007fffffu) != 0) { COV(trigNaN); return x + x; }  // NaN -> quieted
    COV(trigInf);
    return F32(0xffc00000u);                          // +-Inf -> indefinite NaN
}

}  // namespace

// ===========================================================================
// sqrtf -- retail 0x180128110
// ===========================================================================
// The whole body is one `sqrtss` plus a domain-error path: correctly rounded by
// the hardware, so this is bit-identical by construction, not by luck.
float Sqrtf(float x) {
    const uint32_t b = U32(x);
    if ((b & 0x7f800000u) == 0x7f800000u) {
        if ((b & 0x007fffffu) != 0) {
            COV(sqrtNaN);
            return x + x;                             // retail: NaN -> quieted NaN
        }
        if ((b & 0x80000000u) != 0) {
            COV(sqrtNegInf);
            return F32(0xffc00000u);                  // retail: -Inf -> domain error
        }
        COV(sqrtPosInf);
        return x;                                     // +Inf
    }
    if ((b & 0x80000000u) != 0 && (b & 0x7fffffffu) != 0) {
        COV(sqrtNegative);
        return F32(0xffc00000u);                      // retail: negative -> domain error
    }
    COV(sqrtHardware);
#if defined(__SSE2__)
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
#else
    return std::sqrt(x);
#endif
}

// ===========================================================================
// sinf -- retail 0x180127d90
// ===========================================================================
float Sinf(float xf) {
    const uint32_t bx = U32(xf);
    if ((bx & 0x7f800000u) == 0x7f800000u) return TrigNonFinite(xf);

    double         x   = (double)xf;
    const uint64_t rdx = U64(x);
    const uint64_t r10 = rdx & kAbsMask64;

    if (r10 <= 0x3fe921fb54442d18ull) {
        // |x| <= pi/4: no reduction, and no quadrant sign to undo.
        double res;
        if (r10 >= 0x3f80000000000000ull) {            // |x| >= 2^-7
            COV(sinSmallPoly);
            const double z  = x * x;
            double       t1 = K(kSinS4) * z;
            const double z2 = z * z;
            double       t2 = K(kSinS2) * z;
            const double xz = x * z;
            t1 = t1 + K(kSinS3);
            t1 = t1 * z2;
            t2 = t2 + K(kSinS1);
            t1 = t1 + t2;
            t1 = t1 * xz;
            res = x + t1;
        } else if (r10 >= 0x3f20000000000000ull) {     // |x| >= 2^-13
            COV(sinCubicTerm);
            double t = x;
            t = t * t;
            t = t * x;
            t = t * K(kSinSixth);
            res = x - t;
        } else {
            COV(sinIdentity);
            res = x;
        }
        return (float)res;
    }

    // |x| > pi/4.
    int32_t neg = 0;
    if (rdx != r10) { COV(sinNegArg); neg = 1; x = 0.0 - x; }

    int32_t n = 0;
    double  y = 0.0;
    double  res = 0.0;
    bool    fullPoly = false;

    if (r10 >= kSinBigThresh) {
        COV(sinPayneHanek);
        y = RemPio2(U64(x), &n);
        fullPoly = true;
    } else {
        COV(sinCodyWaite);
        // Cody-Waite reduction by pi/2.
        double t = x * K(kSinTwoOverPi);
        t = t + K(kSinHalf);
        const int64_t expx = (int64_t)(r10 >> 52);
        n = TruncToI32(t);
        const double fn = (double)n;
        double r = x - K(kSinPio2Hi) * fn;
        double w = K(kSinPio2Mid) * fn;
        y = r - w;
        const int64_t expy = (int64_t)((U64(y) << 1) >> 53);
        if (expx - expy > 15) {
            COV(sinExtendedSplit);
            // More than 15 bits cancelled -- redo with the extended split.
            double r0 = r;
            const double w1 = K(kSinPio2Mid2) * fn;
            double w2 = K(kSinPio2Lo) * fn;
            r  = r - w1;
            r0 = r0 - r;
            r0 = r0 - w1;
            w2 = w2 - r0;
            y  = r - w2;
        }
        if (expy >= 0x3f2) {
            fullPoly = true;
        } else if (expy <= 0x3de) {
            COV(sinReducedTiny);
            // |y| below ~2^-33: sin(y) is y and cos(y) is 1 to this precision.
            res = (n & 1) ? K(kSinOne) : y;
        } else {
            COV(sinReducedSmall);
            const double z = y * y;
            if (n & 1) {
                res = K(kSinOne) - z * K(kSinHalf);
            } else {
                double t2 = K(kSinSixth) * y;
                t2 = t2 * z;
                res = y - t2;
            }
        }
    }

    if (fullPoly) {
        const double z = y * y;
        if ((n & 1) == 0) {
            COV(sinFullSinSeries);
            double       t1 = K(kSinS4) * z;
            const double z2 = z * z;
            double       t2 = K(kSinS2) * z;
            const double yz = y * z;
            t1 = t1 + K(kSinS3);
            t1 = t1 * z2;
            t2 = t2 + K(kSinS1);
            t1 = t1 + t2;
            t1 = t1 * yz;
            res = y + t1;
        } else {
            COV(sinFullCosSeries);
            double       t1 = K(kSinC_m3628800) * z;
            const double z2 = z * z;
            double       t3 = K(kSinC_m720) * z;
            double       t0 = K(kSinC_mHalf) * z;
            t1 = t1 + K(kSinC_40320);
            t1 = t1 * z2;
            t3 = t3 + K(kSinC_24);
            t1 = t1 + t3;
            t1 = t1 * z2;
            t0 = t0 + K(kSinOne);
            res = t0 + t1;
        }
    }

    // Negate unless bit 0 of (n>>1) agrees with the argument's sign flip.
    const int32_t h = n >> 1;
    if ((((h & neg) | (~h & ~neg)) & 1) != 0) return (float)res;
    COV(sinQuadrantNegate);
    return (float)(0.0 - res);
}

// ===========================================================================
// cosf -- retail 0x180126d10
// ===========================================================================
float Cosf(float xf) {
    const uint32_t bx = U32(xf);
    if ((bx & 0x7f800000u) == 0x7f800000u) return TrigNonFinite(xf);

    double         x   = (double)xf;
    const uint64_t rdx = U64(x);
    const uint64_t r10 = rdx & kAbsMask64;

    int32_t n = 0;
    double  y = 0.0;
    double  res = 0.0;
    bool    fullPoly = false;

    if (r10 <= 0x3fe921fb54442d18ull) {
        COV(cosSmall);
        n = 0;
        y = x;
        fullPoly = true;
    } else {
        if (rdx != r10) { COV(cosNegArg); x = 0.0 - x; }   // cos is even
        if (r10 >= kCosBigThresh) {
            COV(cosPayneHanek);
            y = RemPio2(U64(x), &n);
            fullPoly = true;
        } else {
            COV(cosCodyWaite);
            double t = x * K(kCosTwoOverPi);
            t = t + K(kCosHalf);
            const int64_t expx = (int64_t)(r10 >> 52);
            n = TruncToI32(t);
            const double fn = (double)n;
            double r = x - K(kCosPio2Hi) * fn;
            double w = K(kCosPio2Mid) * fn;
            y = r - w;
            const int64_t expy = (int64_t)((U64(y) << 1) >> 53);
            if (expx - expy > 15) {
                COV(cosExtendedSplit);
                double r0 = r;
                const double w1 = K(kCosPio2Mid2) * fn;
                double w2 = K(kCosPio2Lo) * fn;
                r  = r - w1;
                r0 = r0 - r;
                r0 = r0 - w1;
                w2 = w2 - r0;
                y  = r - w2;
            }
            if (expy >= 0x3f2) {
                fullPoly = true;
            } else if (expy <= 0x3de) {
                COV(cosReducedTiny);
                res = (n & 1) ? y : K(kCosOne);
            } else {
                COV(cosReducedSmall);
                const double z = y * y;
                if (n & 1) {
                    double t2 = K(kCosSixth) * y;
                    t2 = t2 * z;
                    res = y - t2;
                } else {
                    res = K(kCosOne) - z * K(kCosHalf);
                }
            }
        }
    }

    if (fullPoly) {
        const double z = y * y;
        if (n & 1) {
            COV(cosFullSinSeries);
            // sin series -- the odd quadrants of cos
            double       t1 = K(kCosC4) * z;
            const double z2 = z * z;
            double       t3 = K(kCosC2) * z;
            const double yz = z * y;
            t1 = t1 + K(kCosC3);
            t1 = t1 * z2;
            t3 = t3 + K(kCosC1);
            t1 = t1 + t3;
            t1 = t1 * yz;
            res = t1 + y;
        } else {
            COV(cosFullCosSeries);
            double       t1 = K(kCosD4) * z;
            const double z2 = z * z;
            double       t3 = K(kCosD2) * z;
            double       t5 = K(kCosHalf) * z;
            t1 = t1 + K(kCosD3);
            t1 = t1 * z2;
            t3 = t3 + K(kCosD1);
            t1 = t1 + t3;
            t1 = t1 * z2;
            t5 = t5 - K(kCosOne);
            res = t1 - t5;
        }
    }

    if (((n + 1) & 2) != 0) { COV(cosQuadrantNegate); res = 0.0 - res; }
    return (float)res;
}

// ===========================================================================
// atan2f -- retail 0x180126680
// ===========================================================================
float Atan2f(float yf, float xf) {
    const double   yd = (double)yf;
    const double   xd = (double)xf;
    const uint64_t by = U64(yd);
    const uint64_t bx = U64(xd);

    const uint64_t signX = bx & kSignBit64;
    const uint64_t signY = by & kSignBit64;
    const uint64_t absX  = bx & kAbsMask64;
    const uint64_t absY  = by & kAbsMask64;
    constexpr uint64_t kInfBits = 0x7ff0000000000000ull;

    const bool    xIsZero = (absX == 0);
    const bool    xIsInf  = (absX == kInfBits);
    const bool    yIsInf  = (absY == kInfBits);
    const int32_t expDiff = (int32_t)((by >> 52) & 0x7ff) - (int32_t)((bx >> 52) & 0x7ff);

    // retail: both NaN branches report a domain error and return the quieted NaN.
    if (absX > kInfBits) { COV(atanNanX); return xf + xf; }
    if (absY > kInfBits) { COV(atanNanY); return yf + yf; }

    if (absY == 0) {
        COV(atanZeroY);
        if (signX == 0) return (float)yd;             // +-0 preserved
        double r = K(kPi);                            // x < 0, y == +-0 -> +-pi
        if (signY) r = F64(U64(r) ^ kSignBit64);
        return (float)r;
    }

    if (xIsZero && signY) {
        COV(atanZeroXNegY);
        return (float)F64(U64(K(kPiOver2)) ^ kSignBit64);
    }

    if (expDiff > 0x1a) {                             // |y/x| enormous -> +-pi/2
        COV(atanRatioHuge);
        double r = K(kPiOver2);
        if (signY) r = F64(U64(r) ^ kSignBit64);
        return (float)r;
    }

    if (expDiff < -13) {
        if (signX == 0) {
            if (expDiff < -150) { COV(atanRatioZero); return signY ? -0.0f : 0.0f; }
            if (expDiff < -126) {
                COV(atanSubnormal);
                // Result lands in the subnormal range: retail scales by 2^100,
                // divides, then re-rounds the quotient by hand.
                double t = F64(0x4630000000000000ull) * yd;
                t = t / xd;
                const uint64_t tb    = U64(t);
                const uint64_t tsign = tb & kSignBit64;
                const uint64_t mag   = tb & kAbsMask64;
                const int32_t  ex    = (int32_t)((mag >> 52) & 0x7ff) - 0x64;
                uint64_t out;
                if (ex > 0) {
                    out = ((uint64_t)(int64_t)ex << 52) | (mag & 0x800fffffffffffffull);
                } else {
                    uint64_t v = (mag & 0x801fffffffffffffull) | 0x0010000000000000ull;
                    const int32_t sh = 1 - ex;
                    if (sh <= 0x36) {
                        v >>= (sh - 1);
                        out = (v >> 1) + (v & 1);
                    } else {
                        out = v;
                    }
                }
                out |= tsign;
                return (float)F64(out);
            }
            COV(atanPlainDivide);
            return (float)(yd / xd);
        }
        if (expDiff < -0x1a) {                        // x < 0 and |y/x| tiny -> +-pi
            COV(atanNearPi);
            double r = K(kPi);
            if (signY) r = F64(U64(r) ^ kSignBit64);
            return (float)r;
        }
    }

    if (yIsInf && xIsInf) {
        COV(atanBothInf);
        double r = signX ? K(k3PiOver4) : K(kPiOver4);
        if (signY) r = F64(U64(r) ^ kSignBit64);
        return (float)r;
    }

    // General case: fold both arguments positive, order them, reduce.
    double a = signY ? F64(by ^ kSignBit64) : yd;
    double b = signX ? F64(bx ^ kSignBit64) : xd;
    const bool swap = (a > b);
    if (swap) { COV(atanSwap); const double t = b; b = a; a = t; }

    double r    = a / b;
    double corr = 0.0;
    bool   haveCorr = true;

    if (r > K(kAtanSixteenth)) {
        COV(atanTablePath);
        // Table path: reduce against atan(k/256) for the nearest k.
        const double c256 = K(kAtan256);
        double scaled = r * c256;
        double num    = a * c256;
        scaled = scaled + K(kAtanHalf);
        const int32_t nIdx = TruncToI32(scaled);
        const double  fn   = (double)(int64_t)(uint32_t)nIdx;
        const double  fnA  = fn * a;
        const double  fnB  = fn * b;
        const double  b256 = b * c256;
        num = num - fnB;
        const double den = fnA + b256;
        const double t   = num / den;
        r    = t + F64(kAtanTable[nIdx - 16]);
        corr = t * t;
        corr = corr * t;
        corr = corr * K(kAtanCubeC);
    } else if (K(kAtanTiny) > r) {
        COV(atanTinyRatio);
        haveCorr = false;                             // atan(r) == r here
    } else {
        COV(atanPolyPath);
        double       p  = K(kAtanP2);
        const double z  = r * r;
        const double q  = z * K(kAtanP1);
        p = p - q;
        double       s  = K(kAtanP3);
        p = p * z;
        const double z3 = z * r;
        s = s - p;
        corr = s * z3;
    }
    if (haveCorr) r = r - corr;

    if (swap)  r = K(kPiOver2) - r;
    if (signX) { COV(atanNegX); r = K(kPi) - r; }
    if (signY) r = F64(U64(r) ^ kSignBit64);
    return (float)r;
}

// ===========================================================================
// powf -- retail 0x180127560
// ===========================================================================
namespace {

// The special-case dispatch reads its constants from the table at 0x1801700a0;
// these names index kPowIntConst so the transcription stays checkable against
// the disassembly's `[rip+...] # 0x1801700xx` operands.
enum PowK : int {
    kExpHugeThresh = 0,   // 0x4f000000
    kExpTinyThresh = 1,   // 0x2e800000
    kSignMaskF     = 2,   // 0x80000000
    kNegInfF       = 3,   // 0xff800000
    kExpMaskF      = 4,   // 0x7f800000
    kPosInfF       = 6,   // 0x7f800000
    kOneF          = 7,   // 0x3f800000
    kZeroF         = 8,   // 0x00000000
    kAbsMaskF      = 9,   // 0x7fffffff
    kMantMaskF     = 10,  // 0x007fffff
    kIndefNaNF     = 11,  // 0xffc00000
    kQuietBitF     = 14,  // 0x00400000
    kMinusOneF     = 15,  // 0xbf800000
    kNegZeroF      = 16,  // 0x80000000
    kMantBits24    = 20,  // 24
    kMantBits23    = 21,  // 23
    kExpBias127    = 22,  // 127
    kLeadBitF      = 24,  // 0x00800000
};
inline uint32_t P(int i) { return kPowIntConst[i]; }

inline bool IsNanBits(uint32_t v) {
    return (v & 0x7f800000u) == 0x7f800000u && (v & 0x007fffffu) != 0;
}

// The log/exp core, entered with |base| and the exponent. `signMask` is the
// 0x80000000 that a negative base with an odd integer exponent contributes; it
// is OR-ed into the float result exactly as retail's `orps xmm0,[rsp+0x70]`.
float PowCore(float absBase, float expo, uint32_t signMask) {
    const double   e  = (double)expo;
    const double   x  = (double)absBase;
    const uint64_t bx = U64(x);

    // psrlq + psubq: exponent - 1023, then cvtdq2pd on the low int32 lane.
    const int32_t ei = (int32_t)(int64_t)((bx >> 52) - kPowExpBias);
    const double  fe = (double)ei;

    const uint64_t idxBits = (bx & kPowIdxMaskA) + ((bx & kPowIdxMaskB) << 1);
    const uint32_t idx     = (uint32_t)(idxBits >> 45);

    const double mant = F64((bx & kPowMantMask) | kPowHalfMask);
    const double node = F64(idxBits | kPowHalfMask);

    double       t  = node - mant;
    t = t * F64(kPowLogTabHi[idx]);
    const double t2 = t;
    double       t3 = t * t2;
    double       c4 = K(kPowLg4) * t2;
    double       c3 = K(kPowLg2) * t2;
    t3 = t3 * t2;
    c4 = c4 + K(kPowLg3);
    c3 = c3 + K(kPowLg1);
    c4 = c4 * t3;
    c3 = c3 * t2;
    c3 = c3 + c4;
    const double eln2 = K(kPowLog2E_hi) * fe;
    double       z    = F64(kPowLogTabLo[idx]) - c3;
    z = z + eln2;
    z = z * e;                                        // ln(base) * exponent

    const double s = K(kPowExpScale) * z;
    if (!(s < K(kPowOvfThresh))) {
        COV(powOverflow);
        return F32(P(kPosInfF) | signMask);           // retail: overflow report
    }
    if (s < K(kPowUnfThresh)) {
        COV(powUnderflow);
        return F32(P(kZeroF) | signMask);             // retail: underflow report
    }

    COV(powCore);
    const int32_t n    = RoundToI32(s);
    const double  fn   = (double)n;
    const double  step = K(kPowExpStep) * fn;
    const int32_t j    = n & 0x1f;
    double        r    = z - step;
    double        r2   = r * r;
    const int32_t mExp = (n - j) >> 5;
    double        p4   = K(kPowE1) * r;
    double        p3   = K(kPowE3) * r;
    r2 = r2 * r;
    p4 = p4 + K(kPowE2);
    p3 = p3 + K(kPowE4);
    p4 = p4 * r2;
    p3 = p3 * r;
    p3 = p3 + p4;
    const double tabv = F64(kPowExp2Tab[j]);
    double       out  = p3 * tabv;
    out = out + tabv;
    out = out * F64((uint64_t)(int64_t)(mExp + 0x3ff) << 52);
    return F32(U32((float)out) | signMask);
}

// Shared tails of the retail dispatch, in the order they are reached.
float PowBaseInfNan(uint32_t bb, uint32_t be, uint32_t signMask);

float PowBaseZero(uint32_t bb, uint32_t be, uint32_t signMask) {
    COV(powBaseZero);
    (void)bb;
    if ((be & P(kExpMaskF)) == P(kExpMaskF)) {
        if (be == P(kNegInfF)) return F32(P(kPosInfF) | signMask);
        if (IsNanBits(be)) return F32(be);
        return F32(0u);                               // exponent is +Inf
    }
    if (be & P(kSignMaskF)) {
        return F32(P(kPosInfF) | signMask);           // retail: divide-by-zero report
    }
    return F32(0u | signMask);
}

float PowBaseMinusOne(uint32_t be, uint32_t signMask) {
    COV(powBaseMinusOne);
    uint32_t out = P(kOneF);
    // retail returns -1 for pow(-1, -Inf); C99 says 1. The DLL is what ships.
    if (be == P(kNegInfF)) out = P(kMinusOneF);
    out |= signMask;
    if (IsNanBits(be)) return F32(be);
    return F32(out);
}

float PowExpHuge(uint32_t bb, uint32_t be, uint32_t signMask) {
    COV(powExpHuge);
    if ((bb & P(kExpMaskF)) == P(kExpMaskF)) return PowBaseInfNan(bb, be, signMask);
    if ((bb & P(kAbsMaskF)) == 0) return PowBaseZero(bb, be, signMask);
    if (bb == P(kMinusOneF)) return PowBaseMinusOne(be, signMask);
    uint32_t out;
    if ((bb & P(kAbsMaskF)) < P(kOneF)) {
        out = (be & P(kSignMaskF)) ? P(kPosInfF) : 0u;   // |base| < 1
    } else {
        out = (be & P(kSignMaskF)) ? 0u : P(kPosInfF);   // |base| > 1
    }
    if (IsNanBits(be)) return F32(be);
    // retail: a finite exponent with |exp| >= 2^32 goes through the range-error
    // reporter, which returns the value computed above unchanged.
    return F32(out);
}

float PowBaseInfNan(uint32_t bb, uint32_t be, uint32_t signMask) {
    COV(powBaseInfNan);
    const uint32_t out = (be & P(kSignMaskF)) ? 0u : P(kPosInfF);
    if ((bb & P(kMantMaskF)) != 0) {                  // base is NaN
        if (IsNanBits(be)) {
            uint32_t v = bb;
            if (v == P(kIndefNaNF)) v = be;
            return F32(v);
        }
        return F32(bb);
    }
    if (IsNanBits(be)) return F32(be);
    return F32(out | signMask);
}

float PowNegBaseNonInt(uint32_t bb, uint32_t be, uint32_t signMask) {
    COV(powNegBaseNonInt);
    if ((bb & P(kExpMaskF)) == P(kExpMaskF)) return PowBaseInfNan(bb, be, signMask);
    if (bb == P(kNegZeroF)) return PowBaseZero(bb, be, signMask);
    return F32(P(kIndefNaNF));                        // retail: domain error
}

}  // namespace

float Powf(float base, float expo) {
    const uint32_t bb = U32(base);
    const uint32_t be = U32(expo);
    uint32_t signMask = P(kZeroF);

    if ((be & P(kAbsMaskF)) == 0) {                   // exponent is +-0
        COV(powExpZero);
        // retail: pow(NaN, 0) is the NaN, NOT 1.0 as C99 requires.
        if (IsNanBits(bb)) return F32(bb);
        return F32(P(kOneF));
    }
    if (be == P(kOneF)) {                             // exponent is 1.0f
        COV(powExpOne);
        return F32(bb);
    }

    if ((bb & P(kSignMaskF)) == P(kSignMaskF)) {      // base is negative
        COV(powNegBase);
        if ((int32_t)(be & P(kExpMaskF)) > (int32_t)P(kExpHugeThresh)) {
            return PowExpHuge(bb, be, signMask);
        }
        const uint32_t absE = be & P(kAbsMaskF);
        const int32_t  ey   = (int32_t)(absE >> P(kMantBits23)) - (int32_t)P(kExpBias127);
        if (ey < 0) return PowNegBaseNonInt(bb, be, signMask);   // |exp| < 1
        if (ey <= (int32_t)P(kMantBits24)) {
            if ((P(kMantMaskF) >> ey) & absE) return PowNegBaseNonInt(bb, be, signMask);
            if ((P(kLeadBitF) >> ey) & absE) {
                COV(powNegBaseOddInt);
                signMask = P(kSignMaskF);                              // odd integer
            } else {
                COV(powNegBaseEvenInt);
            }
        }
        if (bb == P(kNegZeroF)) return PowBaseZero(bb, be, signMask);
        if (bb == P(kMinusOneF)) return PowBaseMinusOne(be, signMask);
        if ((bb & P(kExpMaskF)) == P(kExpMaskF)) return PowBaseInfNan(bb, be, signMask);
        return PowCore(F32(bb & P(kAbsMaskF)), expo, signMask);
    }

    if (bb == P(kOneF)) {                             // base is 1.0f
        COV(powBaseOne);
        return 1.0f;
    }
    if (bb == P(kZeroF)) return PowBaseZero(bb, be, signMask);
    if ((bb & P(kExpMaskF)) == P(kExpMaskF)) return PowBaseInfNan(bb, be, signMask);
    if ((int32_t)(be & P(kExpMaskF)) > (int32_t)P(kExpHugeThresh)) {
        return PowExpHuge(bb, be, signMask);
    }
    if ((int32_t)(be & P(kExpMaskF)) < (int32_t)P(kExpTinyThresh)) {
        COV(powExpTiny);
        return 1.0f + expo;                           // |exp| < 2^-34
    }
    return PowCore(base, expo, signMask);
}

// ===========================================================================
// fmodf -- retail 0x180127010 (Ghidra: Crt_fmodf, was FUN_180127010)
// ===========================================================================
// BATCH 4, 2026-09-12. The sixth member of this family, and the last one the
// Graphics render/cull/sun-camera cluster reaches: Graphics::SunLight::
// GetSunCamera (0x180107010) calls it three times to snap the shadow camera's
// eye onto a shadow-map texel grid, and is its ONLY caller in the DLL.
//
// The body is the classic MSVC shift-and-subtract remainder done in DOUBLE, on
// float arguments widened at entry and narrowed at exit -- so it is NOT
// float-precision arithmetic. When the exponent gap exceeds 24 bits it scales
// the divisor up by 2^(24*n) and walks it back down one 2^-24 step per
// iteration; every intermediate is a double and the truncation is `cvttsd2si`
// (toward zero), not a round.
//
// Same FAITHFULNESS BOUNDARY as the rest of the file: the domain-error arms
// return the value retail returns (the reporter at 0x18012a070 hands its input
// straight back when no user _matherr is installed); the errno /
// _invalid_parameter traffic beside it is not ported. Each omission is marked.
float Fmodf(float xf, float yf) {
    const double   x  = (double)xf;
    const double   y  = (double)yf;
    const uint64_t bx = U64(x);
    const uint64_t by = U64(y);

    double         ax = F64(bx & kAbsMask64);
    double         ay = F64(by & kAbsMask64);
    const uint32_t ex = (uint32_t)((bx >> 52) & 0x7ff);
    const uint32_t ey = (uint32_t)((by >> 52) & 0x7ff);

    if (ex != 0 && ex < 0x7ff && (ey - 1u) < 0x7feu) {
        // Both finite and normal: the only path that computes anything.
        if (ax == ay) return (float)F64(bx & kSignBit64);   // +-0 with x's sign
        if (U64(ax) < U64(ay)) {
            if (xf < 0.0f) ax = F64(U64(ax) ^ kSignBit64);
            return (float)ax;
        }
        int32_t n    = 0;
        double  step = 1.0;                 // retail DAT_1801616d0; dead when n == 0
        if (ey < ex) {
            n    = (int32_t)(ex - ey) / 0x18;
            step = 5.960464477539063e-08;   // 2^-24
            ay   = F64((uint64_t)((int64_t)n * 0x18 + 0x3ff) << 52) * ay;
        }
        for (int32_t i = n; i > 0; --i) {
            const double q = (double)(int32_t)(ax / ay) * ay;
            ay = ay * step;
            ax = ax - q;
        }
        ax = ax - (double)(int32_t)(ax / ay) * ay;
        if (xf < 0.0f) ax = F64(U64(ax) ^ kSignBit64);
        return (float)ax;
    }

    if (ex > 0x7fe) {                                   // x is Inf or NaN
        if ((bx & 0x000fffffffffffffull) != 0)
            return F32(U32(xf) | 0x00400000u);          // retail: quieted x, reports DOMAIN
        return F32(0xffc00000u);                        // +-Inf: reports DOMAIN
    }
    if (ey > 0x7fe) {                                   // y is Inf or NaN
        if ((by & 0x000fffffffffffffull) == 0) return xf;  // y == +-Inf -> x
        return F32(U32(yf) | 0x00400000u);              // retail: quieted y, reports DOMAIN
    }
    if (ex == 0 && ey != 0) return xf;                  // x zero/subnormal, y normal
    return F32(0xffc00000u);                            // y == 0 -> retail reports DOMAIN
}

}  // namespace MsvcLibm

// ===========================================================================
// B3R (registry teardown, W1 RT-Core): the six address rows went. Five had no
// reader by address; 0x180127560 (powf) had one, audio_play_platform.cpp's
// PowF, which now calls RtCrt_Powf by name (src/engine/rt_libm.h). The five
// static one-line wrappers went with their rows.
// ===========================================================================
// B3R: external linkage (src/engine/rt_libm.h) -- audio calls it by name.
extern "C" float RtCrt_Powf(float b, float e) { return MsvcLibm::Powf(b, e); }
