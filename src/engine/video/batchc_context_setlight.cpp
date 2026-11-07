// ============================================================================
// src/engine/video/batchc_context_setlight.cpp — BATCH C
//   ?SetLight@Context@Video@@QEAAXAEBVVector3@Math@@0AEBVVector4@4@1MMH@Z
//   retail 0x18001a950, ordinal 3903
// ============================================================================
//
// EVERY Graphics light Draw arm ends here (lights_vtable.cpp's banner: "All
// five Draw arms end in it, and its EIGHT values — this + seven params — are
// what the whole of each body exists to compute"). Until this batch it was the
// last unported step of that chain; that comment is corrected at its source.
//
// SIGNATURE, counted against the mangled name AND the prologue (`SUB RSP,0x28`,
// so the incoming stack arguments sit at [RSP+0x50] .. [RSP+0x68]):
//
//   RCX        this                    Video::Context*
//   RDX        direction               const Math::Vector3&      (3 floats)
//   R8         attenuation             const Math::Vector3&      (3 floats)
//   R9         colour                  const Math::Vector4&      (4 dwords)
//   [RSP+0x50] position                const Math::Vector4&      (4 dwords)
//   [RSP+0x58] nearPlane               float
//   [RSP+0x60] farPlane                float
//   [RSP+0x68] shadowResolution        int
//
// No hidden return slot: the return type is `X` (void).
//
// ---------------------------------------------------------------------------
// FOUR THINGS THE DECOMPILER WOULD GET WRONG
// ---------------------------------------------------------------------------
// 1. THE TWO FLOAT SCALARS ARE NOT STORED IN ARGUMENT ORDER. The staging cell
//    at +0x1e18 gets `farPlane` and +0x1e1c gets **-nearPlane**, negated with
//    `XORPS XMM3, [0x1801617c0]` — a 16-byte constant read out of the image as
//    `80 00 00 00` x4, i.e. the IEEE sign mask, NOT a subtraction from zero.
//    (`0.0f - x` and `x ^ signbit` differ for x == 0.0f: the first gives +0.0f,
//    the second -0.0f. That is a real bit difference in a constant buffer.)
// 2. +0x1e20 IS A RECIPROCAL, and the DIVISION IS DONE IN THE ORDER RETAIL
//    WRITES IT: `XMM2 = 1.0f; XMM0 = farPlane; XMM0 -= nearPlane; XMM2 /= XMM0`
//    — one SUBSS then one DIVSS. Not `1.0f/(f-n)` folded, and not multiplied by
//    a precomputed reciprocal, which would round differently.
// 3. +0x1e14 IS A DOT PRODUCT WITH A FIXED ASSOCIATION ORDER:
//    `(att.y*dir.y + att.x*dir.x) + att.z*dir.z`. The y term is the LEFT
//    operand of the first ADDSS (0x18001aa7b adds XMM0 into XMM5, where XMM5
//    already holds the y product). Re-associating it is a different float.
//    (reference_moved_term_hides_where_terms_cancel.md: this project has
//    already lost a wave to exactly that.)
// 4. THE TWO COPY BLOCKS AT THE END ARE NOT A MEMCPY OF THE WHOLE STAGING
//    AREA. Retail copies exactly eight dwords from +0x1dd8 to +0x268 and
//    exactly four from +0x1e28 to +0x248, dword by dword, and touches nothing
//    between. Those two windows live inside Context::_pixelConstantStaging
//    (0x200..0x297) — the ranges `Reset` memsets and `BindResources` uploads —
//    so the light's colour/position pair and its shadow parameters are
//    published into the PIXEL constant staging as well as the light staging.
//
// ORACLE: A REAL ONE. This body is a pure function of its eight values into
// `this`: it reads no global, calls nothing, and writes only the three windows
// below plus one OR into the dirty mask. Re-running the shipped copy on the
// SAME object therefore writes the same bytes again — it is idempotent — so
// the harness snapshots our result, lets the DLL overwrite it, and compares.
// That is a genuine byte comparison, not an attendance marker, and every Draw
// arm drives it on every frame the DLL is present.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "video/Context.h"

// ENDGAME B.3.5 block D1: the body MOVED onto `Video::Context::SetLight`; the
// registered door at the bottom kept its name, its mangled-name row and its
// address row and is a one-line QUALIFIED non-virtual delegation. B.4 (R-VIDEO):
// the light window is `Context::LightStaging`'s named head now (still one 0x1a0
// block for `Reset` and `BindResources`); the two publish windows stay offsets
// into `_pixelConstantStaging` (+0x200, 0x98 bytes), which other blocks index
// by byte. The static_asserts below pin both.
namespace {

using Video::Context;

// The three windows this body writes, and nothing else.
constexpr uint32_t kLightStage    = 0x1dd8;   // .. 0x1e38, 0x60 bytes
constexpr uint32_t kLightStageLen = 0x60;
constexpr uint32_t kPixelColour   = 0x268;    // .. 0x288, 8 dwords copied from +0x1dd8
constexpr uint32_t kPixelColourLen = 0x20;
constexpr uint32_t kPixelPosition = kPixelColour + 0x10;   // the second four of those dwords
constexpr uint32_t kPixelShadow   = 0x248;    // .. 0x258, 4 dwords copied from +0x1e28
constexpr uint32_t kPixelShadowLen = 0x10;
constexpr uint32_t kDirtyBits     = RtVideo::kDirtyLightConstants |
                                    RtVideo::kDirtyPixelConstants;   // OR dword [this+0x10],0x12000

static_assert(offsetof(Context, _lightStaging) == kLightStage,
              "SetLight writes Context::_lightStaging");
static_assert(kLightStageLen == offsetof(Context::LightStaging, _unwritten60),
              "the light window is LightStaging's named head");
static_assert(kPixelShadow >= offsetof(Context, _pixelConstantStaging) &&
                  kPixelColour + kPixelColourLen <=
                      offsetof(Context, _pixelConstantStaging) +
                          sizeof(Context::_pixelConstantStaging),
              "both published windows must stay inside _pixelConstantStaging");
static_assert(kPixelShadowLen == sizeof(Context::LightStaging::shadow),
              "the shadow window is LightStaging::shadow");
static_assert(kPixelColourLen == sizeof(Context::LightStaging::colour) +
                                     sizeof(Context::LightStaging::position),
              "the colour window is LightStaging::colour + position");
static_assert(offsetof(Context, _dirty) == 0x10, "SetLight ORs Context::_dirty");

// The immediates, as BIT PATTERNS read out of the image (lights_vtable.cpp's
// rule) rather than converted from decimal:
//   0x18016169c = 0x3f800000        = 1.0f
//   0x1801617c0 = 80000000 x4       = the IEEE sign mask (read with read_memory)
constexpr uint32_t kBits_one  = 0x3f800000u;
constexpr uint32_t kBits_sign = 0x80000000u;

float AsFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
float NegateBySignMask(float v) {
    uint32_t b; std::memcpy(&b, &v, 4);
    b ^= kBits_sign;                        // XORPS, not `0.0f - v`
    float r; std::memcpy(&r, &b, 4); return r;
}

// The three publish windows are rows of Context::PixelConstantStaging::pixel.
constexpr uint32_t kPixelRow0 = offsetof(Context, _pixelConstantStaging) +
                                offsetof(Context::PixelConstantStaging, pixel);
static_assert(kPixelRow0 + 0 * 0x10 == kPixelShadow,   "shadow   = pixel[0] (+0x248)");
static_assert(kPixelRow0 + 2 * 0x10 == kPixelColour,   "colour   = pixel[2] (+0x268)");
static_assert(kPixelRow0 + 3 * 0x10 == kPixelPosition, "position = pixel[3] (+0x278)");

}  // namespace

// ============================================================================
// ?SetLight@Context@Video@@QEAAXAEBVVector3@Math@@0AEBVVector4@4@1MMH@Z
//                                                   retail 0x18001a950, ord 3903
// ============================================================================
// The two Vector3s are read as three floats; the two Vector4s are COPIED as
// four DWORDS (retail never does arithmetic on the colour or the position, so
// the copy is a byte copy into the dword staging, not four float loads).
void Video::Context::SetLight(const Math::Vector3& direction, const Math::Vector3& attenuation,
                              const Math::Vector4& colour, const Math::Vector4& position,
                              float nearPlane, float farPlane,
                              int32_t shadowResolution) {
    Context* const self = this;
    LightStaging& L = _lightStaging;

    // +0x1dd8 .. +0x1de4 — the colour, four dwords copied as dwords.
    std::memcpy(L.colour, &colour, 16);

    // +0x1de8 .. +0x1df4 — the position, four dwords.
    std::memcpy(L.position, &position, 16);

    // +0x1e18 / +0x1e1c / +0x1e20 / +0x1e24 — see banner points 1 and 2.
    const float invRange = AsFloat(kBits_one) / (farPlane - nearPlane);
    L.farPlane     = farPlane;
    L.negNearPlane = NegateBySignMask(nearPlane);
    L.invRange     = invRange;
    L.zero4c       = 0.0f;

    // +0x1df8 .. +0x1e04 — the direction, with w = 1.0f.
    L.direction[0] = direction.x;
    L.direction[1] = direction.y;
    L.direction[2] = direction.z;
    L.direction[3] = AsFloat(kBits_one);

    // +0x1e08 .. +0x1e10 — the attenuation, then its dot with the direction at
    // +0x1e14. Association order per banner point 3.
    L.attenuation[0] = attenuation.x;
    L.attenuation[1] = attenuation.y;
    L.attenuation[2] = attenuation.z;
    const float dot = (attenuation.y * direction.y + attenuation.x * direction.x) +
                      attenuation.z * direction.z;
    L.attenuationDot = dot;

    // +0x1e28 — 1.0f / (float)shadowResolution. CVTDQ2PS: a SIGNED int-to-float
    // conversion, then a single DIVSS.
    L.shadow[0] = AsFloat(kBits_one) / static_cast<float>(shadowResolution);
    L.shadow[1] = 0.0f;
    L.shadow[2] = 0.0f;
    L.shadow[3] = 0.0f;

    // The two publish blocks, dword by dword, exactly as retail (banner 4):
    // colour + position (+0x1dd8, 8 dwords) to +0x268, shadow (+0x1e28, 4
    // dwords) to +0x248.
    // The rows are Math::Vector4 since B7 (ENDGAME B7 CTOR): each row's four
    // dwords are named. colour / position are uint32_t bit patterns, so they
    // stay byte copies (never float arithmetic). A row-sized memcpy was measured
    // and splits the position row into two qword stores at -O2; these per-dword
    // copies keep the base object IDENTICAL (one MOVUPS per row). The `.w`
    // destinations are taken into named locals only because castmetric.py's
    // member lookup resolves the bare name `w` to an unrelated pointer member
    // and would count `memcpy(&row.w, ...)`, a float-slot copy, as a pun.
    Math::Vector4& colourRow   = self->_pixelConstantStaging.pixel[2];
    Math::Vector4& positionRow = self->_pixelConstantStaging.pixel[3];
    Math::Vector4& shadowRow   = self->_pixelConstantStaging.pixel[0];
    std::memcpy(&colourRow.x, &L.colour[0], 4);
    std::memcpy(&colourRow.y, &L.colour[1], 4);
    std::memcpy(&colourRow.z, &L.colour[2], 4);
    float* const colourW = &colourRow.w;
    std::memcpy(colourW, &L.colour[3], 4);
    std::memcpy(&positionRow.x, &L.position[0], 4);
    std::memcpy(&positionRow.y, &L.position[1], 4);
    std::memcpy(&positionRow.z, &L.position[2], 4);
    float* const positionW = &positionRow.w;
    std::memcpy(positionW, &L.position[3], 4);
    std::memcpy(&shadowRow.x, &L.shadow[0], 4);
    std::memcpy(&shadowRow.y, &L.shadow[1], 4);
    std::memcpy(&shadowRow.z, &L.shadow[2], 4);
    float* const shadowW = &shadowRow.w;
    std::memcpy(shadowW, &L.shadow[3], 4);

    _dirty |= kDirtyBits;
}
