// ============================================================================
// src/engine/utility/perlin.cpp — Wave 8 lane E — Utility::PerlinNoise
// ============================================================================
//   ??0PerlinNoise@Utility@@QEAA@MMHH@Z    retail 0x180108c70   ord 305
//   ?Generate@PerlinNoise@Utility@@QEBAMMM@Z  retail 0x1801089b0   ord 2396
//
// See src/engine/utility/PerlinNoise.h for the layout and the vtable
// verdict (not polymorphic, proven from the ctor's first store being a float,
// never a `LEA` of a `??_7…6B@` symbol).
//
// THE ONE GENUINELY PURE FUNCTION IN WAVE 8. Generate reads only `*this` and
// its two float arguments, calls two private const helpers on the same
// object (SmoothNoise) and two transcendentals (pow, cos), and allocates and
// mutates nothing. It is used by map generation (this project's own callers
// are terrain-shape code), so a wrong bit here changes a seeded world — which
// is exactly the case WAVE8.md §5 singles out for "a full replay oracle to
// the bit."
//
// TRANSCENDENTALS GO THROUGH src/engine/rt_libm.h, NOT A DIRECT CALL TO
// THE DLL. Disassembling 0x1801089b0 shows two CALLs to un-exported internal
// helpers, 0x180127560 and 0x180126d10:
//   * 0x180126d10 is Crt_cosf and 0x180127560 is Crt_powf in rt_libm.h's list
//     of the ported CRT math bodies. 0x180127560's body is the classic
//     fdlibm-shaped ieee754_pow, confirmed by decompiling it and reading the
//     exponent/mantissa branches.
//
// RtLibm::Cos and RtLibm::Pow are MsvcLibm::Cosf and MsvcLibm::Powf, the
// ported MSVC CRT bodies in src/engine/crt/msvc_libm.cpp (installed by
// src/engine/rt_libm.cpp), so Generate does not depend on Runtime-x64.dll
// (PLAN.md §4.1 decision 1). The "1-ULP residual" a different libm leaves
// against MSVC's CRT is in docs/history/runtime_port/WAVE8.md, "Pre-wave control".
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include "rt_libm.h"
#include "utility/PerlinNoise.h"

using RtUtility::PerlinNoise;

namespace {

inline float BitsToFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }

// Read directly out of the shipped DLL's .rdata (image base 0x180000000),
// 2026-09-07, rather than assumed from the decompile's DAT_ placeholders:
constexpr uint32_t kOneBits   = 0x3f800000u;  // DAT_18016169c = 1.0f
constexpr uint32_t kHalfBits  = 0x3f000000u;  // DAT_180161694 = 0.5f
constexpr uint32_t kTwoBits   = 0x40000000u;  // DAT_1801616d8 = 2.0f  (per-octave frequency base)
constexpr uint32_t kPiBits    = 0x40490fdbu;  // DAT_1801616e0 = pi
constexpr uint32_t kNormBits  = 0x30800000u;  // DAT_180161660 = 2^-24 (hash-to-float normaliser)
constexpr uint32_t kW16Bits   = 0x3d800000u;  // DAT_180161680 = 1/16 (corner weight)
constexpr uint32_t kW8Bits    = 0x3e000000u;  // DAT_180161688 = 1/8  (edge weight)
constexpr uint32_t kW4Bits    = 0x3e800000u;  // DAT_18016168c = 1/4  (center weight)

// Retail's lattice hash: `n ^ (n * 0x2000)`, a uint32 multiply-XOR (equal to
// `n ^ (n << 13)` for the low bits that survive the later `& 0x7fffffff`, but
// transcribed as the multiply retail actually performs, not the shift a
// well-known tutorial algorithm would use — 0x3d73 and 0xc0ae5 are the textbook
// 15731/789221, and 0xd208dd0d is the textbook 1376312589 (0x5208dd0d) with
// bit 31 set, which the `& 0x7fffffffu` discards).
inline uint32_t LatticeHash(uint32_t n) { return n ^ (n * 0x2000u); }

// `1.0f - (float)(((n*n*15731u + 789221u... )` — the actual retail constants:
inline float CornerValue(uint32_t n) {
    const uint32_t h = LatticeHash(n);
    const uint32_t v = (h * h * 0x3d73u + 0xc0ae5u) * h + 0xd208dd0du;
    return BitsToFloat(kOneBits) - static_cast<float>(v & 0x7fffffffu) * BitsToFloat(kNormBits);
}

// ============================================================================
// Utility::PerlinNoise::SmoothNoise(int, int) const   retail 0x1801085c0   ord 4070
// ?SmoothNoise@PerlinNoise@Utility@@AEBAMHH@Z
// ----------------------------------------------------------------------------
// NOT in this lane's function list (not exported to our tree at all — it is
// `private`/AEBA and this project's census never lists it), so it is not
// registered on its own. Reproduced here only because Generate needs it, the
// same way Wave 5's CheckMod needed IO::Modification's layout without owning
// that class.
//
// base = (_offsetY + y) * 57 + _offsetX + x
// corners (weight 1/16): base-0x38, base-0x3a, base+0x38, base+0x3a
// edges   (weight 1/8):  base+1, base-1, base-0x39, base+0x39
// center  (weight 1/4):  base
// Every offset and the summation ORDER is transcribed exactly as retail sums
// them (float addition is not associative, so the order is load-bearing for
// a bit-exact port).
// ============================================================================
float SmoothNoise(const PerlinNoise* self, int32_t x, int32_t y) {
    // Unsigned arithmetic throughout, matching the raw 32-bit machine ops
    // retail performs (hardware wraps; signed overflow here would be UB in
    // C++ where retail's disassembly has none).
    const uint32_t base = (static_cast<uint32_t>(self->_offsetY) + static_cast<uint32_t>(y)) * 57u +
                           static_cast<uint32_t>(self->_offsetX) + static_cast<uint32_t>(x);

    const uint32_t nCornerA = base - 0x38u;  // row-1, col+1
    const uint32_t nCornerB = base - 0x3au;  // row-1, col-1
    const uint32_t nCornerC = base + 0x38u;  // row+1, col-1
    const uint32_t nCornerD = base + 0x3au;  // row+1, col+1
    const uint32_t nEdgeA   = base + 1u;     // row+0, col+1
    const uint32_t nEdgeB   = base - 1u;     // row+0, col-1
    const uint32_t nEdgeC   = base - 0x39u;  // row-1, col+0
    const uint32_t nEdgeD   = base + 0x39u;  // row+1, col+0

    const float corners = (CornerValue(nCornerA) + CornerValue(nCornerB) +
                            CornerValue(nCornerC) + CornerValue(nCornerD)) * BitsToFloat(kW16Bits);
    const float edges   = (CornerValue(nEdgeA) + CornerValue(nEdgeB) +
                            CornerValue(nEdgeC) + CornerValue(nEdgeD)) * BitsToFloat(kW8Bits);
    const float center  = CornerValue(base) * BitsToFloat(kW4Bits);

    return corners + edges + center;
}

}  // namespace

// ============================================================================
// Utility::PerlinNoise::PerlinNoise(float persistence, float octaves,
//                                   int offsetX, int offsetY)   retail 0x180108c70
// ??0PerlinNoise@Utility@@QEAA@MMHH@Z
// ----------------------------------------------------------------------------
// The residue census in docs/history/runtime_port/wave9_residue_tutorial3.txt
// lists this ctor's mangled name.
//
// The body is verified against the retail bytes at
// 0x180108c70 — seven instructions, all four stores and no others:
//
//     MOV   EAX,[RSP+0x28]        ; 4th argument, from its home slot
//     MOVSS [RCX],XMM1            ; +0x00 = 1st (float)
//     MOVSS [RCX+0x4],XMM2        ; +0x04 = 2nd (float)
//     MOV   [RCX+0xc],EAX         ; +0x0c = 4th (int)
//     MOV   RAX,RCX               ; return `this`
//     MOV   [RCX+0x8],R9D         ; +0x08 = 3rd (int)
//     RET
//
// NO ORACLE, AND NONE IS NEEDED OR POSSIBLE: there is no state to compare
// against — a scratch replay would compare four stores to four stores of the
// same four argument registers, which is true by construction and would prove
// nothing (reference_recheck_must_match_its_own_proof.md). The verification
// here is the instruction listing above, which is total: every store the
// shipped body makes is in the list, so a field this port missed would be a
// field that is not in the disassembly.
//
// The one thing this does NOT verify is the FIELD NAMES; those are still the
// usage inference PerlinNoise.h documents. A wrong name would not change a
// single emitted byte.
// ============================================================================
PerlinNoise* RtUtility_PerlinNoise_Ctor(PerlinNoise* self, float persistence, float octaves,
                                        int32_t offsetX, int32_t offsetY) {
    self->_persistence = persistence;
    self->_octaves     = octaves;
    self->_offsetX     = offsetX;
    self->_offsetY     = offsetY;
    return self;
}

// ============================================================================
// Utility::PerlinNoise::Generate(float x, float y) const   retail 0x1801089b0   ord 2396
// ?Generate@PerlinNoise@Utility@@QEBAMMM@Z
// ----------------------------------------------------------------------------
// Classic multi-octave value noise: for octave i in [0, _octaves), frequency
// = pow(2, i), amplitude = pow(_persistence, i); sample SmoothNoise at the
// four lattice points around (x*freq, y*freq) and bilinearly interpolate
// with cosine-smoothed weights, accumulating amplitude * interpolated value.
//
// TRANSCRIBED INSTRUCTION-FOR-INSTRUCTION, not "cleaned up": retail computes
// the Y-axis frequency term (`fVar6`) from the SAME `fVar8` (2^i) that was
// just used for X, reusing the register rather than recomputing pow; this
// port mirrors that by computing `freq` once. The loop's float index
// (`fVar12`) is reused mid-body to briefly HOLD the amplitude before being
// reset to `(float)(i+1)` for the next pow call — reproduced with two
// separate local variables here for clarity, which changes no value, only
// which C++ name owns which SSA slot.
//
// ABI: `this` is a const PerlinNoise*, the two float args arrive in XMM1/XMM2
// per the mangled `QEBAMMM` (member, const, returns M=float, args M,M) — no
// hidden return slot, a float fits in XMM0.
// ============================================================================
float RtUtility_PerlinNoise_Generate(const PerlinNoise* self, float x, float y) {

    float total = 0.0f;
    const float octaveBound = self->_octaves;
    if (octaveBound > 0.0f) {
        int32_t i = 0;
        float loopIndex = 0.0f;
        do {
            const float freq = RtLibm::Pow(BitsToFloat(kTwoBits), loopIndex);

            const float fx = freq * x;
            const float fy = freq * y;
            const int32_t ix = static_cast<int32_t>(fx);   // truncating cast, matches CVTTSS2SI
            const int32_t iy = static_cast<int32_t>(fy);

            const float cosX = RtLibm::Cos((fx - static_cast<float>(ix)) * BitsToFloat(kPiBits));
            const float weightX = (BitsToFloat(kOneBits) - cosX) * BitsToFloat(kHalfBits);
            const float cosY = RtLibm::Cos((fy - static_cast<float>(iy)) * BitsToFloat(kPiBits));
            const float weightY = (BitsToFloat(kOneBits) - cosY) * BitsToFloat(kHalfBits);

            const float s00 = SmoothNoise(self, ix,     iy);
            const float s10 = SmoothNoise(self, ix + 1, iy);
            const float s01 = SmoothNoise(self, ix,     iy + 1);
            const float s11 = SmoothNoise(self, ix + 1, iy + 1);

            const float amplitude = RtLibm::Pow(self->_persistence, loopIndex);
            ++i;

            const float interpTop    = s00 * (BitsToFloat(kOneBits) - weightX) + s10 * weightX;
            const float interpBottom = s01 * (BitsToFloat(kOneBits) - weightX) + s11 * weightX;
            total += (interpTop * (BitsToFloat(kOneBits) - weightY) + interpBottom * weightY) * amplitude;

            loopIndex = static_cast<float>(i);
        } while (loopIndex < octaveBound);
    }

    return total;
}
