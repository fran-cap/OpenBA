// ============================================================================
// src/engine/math/wave9_lane_gfxr.cpp — Wave 9 lane GFXR, Math namespace
// ============================================================================
//
//   ?Inverse@Matrix@Math@@QEBA?AV12@XZ      retail 0x1800bb310, ordinal 3082
//
// Called by `Video::Context::SetCamera` (src/engine/video/context.cpp), which
// calls it by name: `_inverseView = view.Inverse()`.
//
// 334 instructions, all scalar SSE, no calls. A cofactor/adjugate 4x4 inverse:
// six 2x2 minors from the BOTTOM two rows, four cofactors of the top row, one
// determinant, then `1.0f / det` ONCE and sixteen multiplies by that
// reciprocal. `det == 0.0f` exactly (and ORDERED — a NaN determinant does NOT
// take this branch) returns the IDENTITY, not garbage and not a zero matrix.
//
// ---------------------------------------------------------------------------
// WHY THIS FILE IS A TRANSLITERATION AND MUST STAY ONE
// ---------------------------------------------------------------------------
// Wave 9 lane N spent a session on `Picking::Scene::Intersect` because a
// translation term had been folded into a float sum FIRST instead of added
// LAST. That is algebraically identical and bit-different only where the terms
// nearly cancel — it presented at ONE CALL IN 23,654, so a clean re-run could
// never have exonerated it.
//
// A determinant IS a near-cancelling sum, and this body has sixteen more of
// them in the adjugate. So every sum below is parenthesised to match the
// ADDSS/SUBSS order in the retail schedule, each cell cites the address of the
// instruction that fixes its grouping, and the reciprocal is computed once and
// multiplied in exactly where retail multiplies it.
//
//   *** DO NOT SIMPLIFY THE ARITHMETIC IN THIS FILE. ***
//   Float addition is not associative. `(x + y) + z` and `x + (y + z)` are
//   different functions, and this is the shape where the difference shows.
//
// Multiplication and addition are both COMMUTATIVE in IEEE-754, so `a*b` vs
// `b*a` and `x+y` vs `y+x` are free; only the GROUPING is load-bearing. The
// operand order below still mirrors the disassembly, to keep the cross-check
// against the listing mechanical.
//
// Negation is `XORPS` against the 16-byte -0.0f mask at 0x1801617c0, i.e. a
// SIGN-BIT FLIP, applied exactly where retail applies it: to the finished sum
// for the cofactors C1 / C3, and after the multiply by the reciprocal for the
// other negated cells. `NegBits` below does the bit flip rather than
// spelling `-x`, so the transcription is checkable against the bytes without
// an argument about what the compiler does to unary minus on a NaN.
//
// Constants LIFTED AS RAW BITS, never retyped as decimals:
//   0x18016169c = 0x3f800000  (1.0f, the reciprocal's numerator AND the
//                              identity-matrix diagonal)
//   0x1801617c0 = 80000000 80000000 80000000 80000000  (the -0.0f sign mask)
//
// ============================================================================

#include "math/Math.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

inline float BitsToFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
inline uint32_t FloatToBits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

// 0x18016169c. Used both as the reciprocal numerator (DIVSS at 0x1800bb5b3)
// and as the identity diagonal on the det==0 path (0x1800bb545).
const float kOneF = BitsToFloat(0x3f800000u);

// XORPS against 0x1801617c0 — a sign-bit flip, not a subtraction from zero.
inline float NegBits(float f) { return BitsToFloat(FloatToBits(f) ^ 0x80000000u); }

}  // namespace

// ============================================================================
// Math::Matrix Math::Matrix::Inverse() const
// ?Inverse@Matrix@Math@@QEBA?AV12@XZ      retail 0x1800bb310
//
// Member, struct return: RCX = this, RDX = hidden return slot. Returns RDX in
// RAX (0x1800bb8e4), which is what the ABI requires.
//
// Cell naming: `aRC` is m[R*4 + C], matching the offsets the disassembly loads
//   a00 +0x00  a01 +0x04  a02 +0x08  a03 +0x0c
//   a10 +0x10  a11 +0x14  a12 +0x18  a13 +0x1c
//   a20 +0x20  a21 +0x24  a22 +0x28  a23 +0x2c
//   a30 +0x30  a31 +0x34  a32 +0x38  a33 +0x3c
// ============================================================================
//
// ENDGAME B.3.1 step 1: MOVED onto Math::Matrix::Inverse unchanged, returning
// by value. The retired diff oracle's input snapshot (which existed only
// because the hidden return slot could alias `self`) is gone with the slot.
Math::Matrix Math::Matrix::Inverse() const {
    Matrix out;
    Matrix* const ret = &out;
    const Matrix* const self = this;

    const float a00 = self->m[0],  a01 = self->m[1],  a02 = self->m[2],  a03 = self->m[3];
    const float a10 = self->m[4],  a11 = self->m[5],  a12 = self->m[6],  a13 = self->m[7];
    const float a20 = self->m[8],  a21 = self->m[9],  a22 = self->m[10], a23 = self->m[11];
    const float a30 = self->m[12], a31 = self->m[13], a32 = self->m[14], a33 = self->m[15];

    // ---- Six 2x2 minors of the BOTTOM two rows (0x1800bb3e2 .. 0x1800bb48f)
    const float A = a20 * a31 - a21 * a30;   // XMM9   0x1800bb3e7 / 0x1800bb427
    const float B = a32 * a20 - a22 * a30;   // XMM10  0x1800bb3e2 / 0x1800bb435
    const float C = a32 * a21 - a22 * a31;   // XMM11  0x1800bb401 / 0x1800bb448
    const float D = a33 * a21 - a23 * a31;   // XMM12  0x1800bb3ec / 0x1800bb459
    const float E = a33 * a22 - a23 * a32;   // XMM13  0x1800bb406 / 0x1800bb46a
    const float F = a23 * a30 - a33 * a20;   // XMM14  0x1800bb422 / 0x1800bb48a

    // ---- The four top-row cofactors (0x1800bb47d .. 0x1800bb519).
    // Grouping fixed by the ADDSS/SUBSS order; the sign flips at 0x1800bb4d1
    // and 0x1800bb519 are XORPS, applied to the finished sum.
    const float C0 = (a11 * E - a12 * D) + a13 * C;              // XMM6
    const float C1 = NegBits((a10 * E + a12 * F) + a13 * B);     // XMM3
    const float C2 = (a10 * D + a11 * F) + a13 * A;              // XMM8
    const float C3 = NegBits((a10 * C - a11 * B) + a12 * A);     // XMM4

    // ---- Determinant (0x1800bb515 .. 0x1800bb53a), left-associated.
    const float det = ((a00 * C0 + a01 * C1) + a02 * C2) + a03 * C3;   // XMM5

    // 0x1800bb53e  UCOMISS XMM5,XMM2(=0) ; JP ; JNZ
    // The JP sends an UNORDERED compare (det is NaN) to the DIVIDE path, so
    // only an ordered, exactly-zero determinant returns the identity. Written
    // as `det == 0.0f` because that is false for NaN, which is the same branch
    // retail takes. It is true for -0.0f in both implementations.
    if (det == 0.0f) {
        // 0x1800bb545 .. 0x1800bb59c — written DIRECTLY to the return slot,
        // bypassing the zeroed local the divide path uses.
        for (int idx = 0; idx < 16; ++idx) ret->m[idx] = 0.0f;
        ret->m[0] = kOneF;
        ret->m[5] = kOneF;
        ret->m[10] = kOneF;
        ret->m[15] = kOneF;
        return out;
    }

    // 0x1800bb5b3  DIVSS XMM7,XMM5 — ONE reciprocal, then sixteen MULSS. Not
    // sixteen divisions: `x * (1/det)` and `x / det` differ in the last bit.
    const float inv = kOneF / det;   // XMM7

    // ---- Six 2x2 minors of the TOP two rows (0x1800bb6d1 .. 0x1800bb757)
    const float G = a00 * a11 - a01 * a10;   // XMM6
    const float H = a00 * a12 - a02 * a10;   // XMM5
    const float I = a01 * a12 - a02 * a11;   // XMM4
    const float J = a01 * a13 - a03 * a11;   // XMM13
    const float K = a02 * a13 - a03 * a12;   // XMM11
    const float L = a03 * a10 - a00 * a13;   // XMM14

    // ---- The sixteen result cells, into a LOCAL first.
    // Retail builds the whole matrix in a stack local (RSP+0x10 .. RSP+0x4c)
    // and copies it out as four 16-byte MOVUPS at the very end (0x1800bb88f,
    // 0x1800bb8b2, 0x1800bb8bb, 0x1800bb8cc). That is what makes `ret`
    // aliasing `self` safe, so the port does the same rather than writing
    // through `ret` as it goes.
    float r[16];

    // Row 0. r[0] is the cofactor already computed for the determinant.
    r[0]  = C0 * inv;                                                     // 0x1800bb5c3
    r[1]  = NegBits(((a01 * E - a02 * D) + a03 * C) * inv);               // 0x1800bb627
    r[2]  = ((a31 * K - a32 * J) + a33 * I) * inv;                        // 0x1800bb783
    r[3]  = NegBits(((a21 * K - a22 * J) + a23 * I) * inv);               // 0x1800bb845

    // Row 1.
    r[4]  = C1 * inv;                                                     // 0x1800bb5bb
    r[5]  = ((a02 * F + a00 * E) + a03 * B) * inv;                        // 0x1800bb623
    r[6]  = NegBits(((a32 * L + a30 * K) + a33 * H) * inv);               // 0x1800bb7d7
    r[7]  = ((a22 * L + a20 * K) + a23 * H) * inv;                        // 0x1800bb870

    // Row 2.
    r[8]  = C2 * inv;                                                     // 0x1800bb5ec
    r[9]  = NegBits(((a00 * D + a01 * F) + a03 * A) * inv);               // 0x1800bb6b1
    r[10] = ((a30 * J + a31 * L) + a33 * G) * inv;                        // 0x1800bb7eb
    r[11] = NegBits(((a21 * L + a20 * J) + a23 * G) * inv);               // 0x1800bb892

    // Row 3.
    r[12] = C3 * inv;                                                     // 0x1800bb5bf
    r[13] = ((a00 * C - a01 * B) + a02 * A) * inv;                        // 0x1800bb6cd
    r[14] = NegBits(((a30 * I - a31 * H) + a32 * G) * inv);               // 0x1800bb804
    r[15] = ((a20 * I - a21 * H) + a22 * G) * inv;                        // 0x1800bb8bf

    std::memcpy(ret->m, r, sizeof(r));

    return out;
}
