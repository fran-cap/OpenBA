// ============================================================================
// tests/unit/ext_better_trees_test.cpp -- ext.better_trees' pure parts
// ============================================================================
// Two halves, each in its own namespace so the helpers (Comp4, Dst, Shape, Program, Blob, Analyse,
// ...) keep their names: `bbs` = the billboard shadow side (receiver_rewrite, edge_dither),
// `faa` = the foliage cutout side (dxbc, cutout_rewrite); the two files merged 2026-10-20.
//
// bbs:
// src/render/common/better_trees/receiver_rewrite.{h,cpp} (the margin lookup of handins/TS-CORE.md
// "TS-FIX2") and the registry row (src/ext/registry.cpp).
//
// NO RETAIL BYTES: every blob here is SYNTHETIC, assembled token by token in the SHAPE TS-CORE
// documented (a world position built from an instance, a camera-facing offset from cb0[4..6], the
// clip position from cb0[0..3], the shadow output from cb2[10..13]), not any shader's content. A tiny
// interpreter for the instructions used here runs the original and the rewritten program and checks
// the rewrite's MEANING: the clip output is unchanged, and the shadow position is the drawn position
// moved along the light direction to the card centre's depth minus k |C - P| (TS-FIX2).
//
// faa:
// src/render/common/better_trees/dxbc.{h,cpp} (the DXBC container, its checksum, the
// SM4 token walk) and cutout_rewrite.{h,cpp} (the cutout grammar of FOLIAGE_AA.md
// §6.1 and the emit), plus the registry row / key (src/ext/registry.cpp).
//
// NO RETAIL BYTES: every blob here is SYNTHETIC, assembled token by token below
// in the shape FAA-SPIKE S2 documented (the instruction forms, not any shader's
// content): a ps_4_0 program with the cutout, the two rT variants (the alpha
// register itself, or a separate temp), the optional season `mad`, the four
// DETAIL variants' temp counts, and the near misses the matcher must refuse.
// ============================================================================
#include "unit.h"

#include "better_trees/cutout_rewrite.h"
#include "better_trees/dxbc.h"
#include "better_trees/edge_dither.h"   // BETTER-TREES: the pixel side
#include "better_trees/receiver_rewrite.h"
#include "ext/registry.h"

#include <cmath>
#include <cstring>
#include <vector>

// ============================================================================
// bbs: the billboard shadow side (receiver_rewrite, edge_dither)
// ============================================================================
namespace bbs {

namespace {

namespace Op  = Dxbc::Op;
namespace OT  = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;
using Tokens  = std::vector<uint32_t>;

constexpr uint32_t kXYZW = Enc::Swizzle(0, 1, 2, 3);

uint32_t Comp4(uint32_t sel, uint32_t bits, uint32_t type, uint32_t dims) {
    return 2u | (sel << 2) | (bits << 4) | (type << 12) | (dims << 20);
}
void Dst(Tokens& t, uint32_t type, uint32_t reg, uint32_t mask) {
    t.push_back(Comp4(0, mask, type, 1));
    t.push_back(reg);
}
void Swz(Tokens& t, uint32_t type, uint32_t reg, uint32_t swz) {
    t.push_back(Comp4(1, swz, type, 1));
    t.push_back(reg);
}
void Cb(Tokens& t, uint32_t cb, uint32_t row, uint32_t swz = kXYZW) {
    t.push_back(Comp4(1, swz, OT::ConstantBuffer, 2));
    t.push_back(cb);
    t.push_back(row);
}
// cb<cb>[r<reg>.<comp> + row]: dim 1 is "imm32 + relative" (representation 3)
void CbRel(Tokens& t, uint32_t cb, uint32_t row, uint32_t reg, uint32_t comp, uint32_t swz = kXYZW) {
    t.push_back(Comp4(1, swz, OT::ConstantBuffer, 2) | (3u << 25));
    t.push_back(cb);
    t.push_back(row);
    t.push_back(Comp4(2, comp, OT::Temp, 1));
    t.push_back(reg);
}

struct Ins {
    Tokens& t;
    size_t  at;
    explicit Ins(Tokens& tt) : t(tt), at(tt.size()) { t.push_back(0); }
    void End(uint32_t op) { t[at] = Enc::Opcode(op, static_cast<uint32_t>(t.size() - at)); }
};

struct Shape {
    uint32_t cb2Rows   = 14;
    bool     shadowFromCamera = true;    // the shadow output reads the camera-facing position
    bool     noShadow  = false;          // no cb2[10..13] output at all
    bool     flow      = false;          // an if / endif
    bool     relCb0    = false;          // the offset reads cb0[r0.w + 4] (relative)
    bool     relCb3    = false;          // an instance read cb3[r3.w + 0] after the first taint (renamed sub-operand)
    int      filler    = 0;              // extra unrelated instructions (lighting-like)
    bool     grow      = false;          // C = v0 * cb3[r5.w + 0].w + cb3[r5.w + 3] (retail: scaled by the growth, translated)
    bool     noInput   = false;          // C is loaded from cb0[8], not from a vertex input
};

// v0 = position, cb0 rows 0..3 worldToProjection, 4..7 worldToCamera, 8 cameraPosition;
// cb2 row 3 nearPlane (the light direction), 10..13 shadowProjection[0]. o0 clip, o1 a texcoord, o3 shadow.
Tokens Program(const Shape& s) {
    Tokens t;
    { Ins i(t); t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2)); t.push_back(0); t.push_back(12);
      i.End(Op::DclConstantBuffer); }
    { Ins i(t); t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2)); t.push_back(2); t.push_back(s.cb2Rows);
      i.End(Op::DclConstantBuffer); }
    { Ins i(t); t.push_back(6); i.End(Op::DclTemps); }
    // 0: mov r0.xyz, v0.xyz                    the card centre (shared)
    if (s.noInput) { Ins i(t); Dst(t, OT::Temp, 0, 7); Cb(t, 0, 8); i.End(Op::Mov); }
    else { Ins i(t); Dst(t, OT::Temp, 0, 7); Swz(t, OT::Input, 0, kXYZW); i.End(Op::Mov); }
    // 1: mov r3.xyz, v1.xyz                    the world normal N
    { Ins i(t); Dst(t, OT::Temp, 3, 7); Swz(t, OT::Input, 1, kXYZW); i.End(Op::Mov); }
    // 2: mov r5.w, l(0)                        an index register (for the relative variants)
    { Ins i(t); Dst(t, OT::Temp, 5, 8); Enc::ImmScalar(t, 0.0f); i.End(Op::Mov); }
    if (s.grow) {
        // 2a: mul r0.xyz, r0.xyzx, cb3[r5.w + 0].wwww   the instance scale (the growth)
        { Ins i(t); Dst(t, OT::Temp, 0, 7); Swz(t, OT::Temp, 0, kXYZW); CbRel(t, 3, 0, 5, 3, Enc::Rep(3)); i.End(Op::Mul); }
        // 2b: add r0.xyz, r0.xyzx, cb3[r5.w + 3].xyzx   the instance translation T
        { Ins i(t); Dst(t, OT::Temp, 0, 7); Swz(t, OT::Temp, 0, kXYZW); CbRel(t, 3, 3, 5, 3); i.End(Op::Add); }
    }
    // 3: mul r1.xyz, r3.yyyy, cb0[5].xyzx      camera-space normal (tainted from here)
    { Ins i(t); Dst(t, OT::Temp, 1, 7); Swz(t, OT::Temp, 3, Enc::Rep(1));
      if (s.relCb0) CbRel(t, 0, 5, 5, 3); else Cb(t, 0, 5); i.End(Op::Mul); }
    // 4: mad r1.xyz, cb0[4].xyzx, r3.xxxx, r1.xyzx
    { Ins i(t); Dst(t, OT::Temp, 1, 7); Cb(t, 0, 4); Swz(t, OT::Temp, 3, Enc::Rep(0)); Swz(t, OT::Temp, 1, kXYZW);
      i.End(Op::Mad); }
    if (s.flow) { Ins i(t); t.push_back(Comp4(2, 0, OT::Temp, 1)); t.push_back(0); i.End(31 /* if */); }
    // 5: mad r1.xyz, r1.xyzx, l(0.5, 0.5, 0.5, 0), cb0[6].xyzx   the card code's 0.5 tilt shape
    { Ins i(t); Dst(t, OT::Temp, 1, 7); Swz(t, OT::Temp, 1, kXYZW); Enc::Imm4(t, 0.5f, 0.5f, 0.5f, 0.0f); Cb(t, 0, 6);
      i.End(Op::Mad); }
    // 6: mul r2.xyz, r1.xyzx, cb0[6].wwww      a scale, so the camera row's w takes part
    { Ins i(t); Dst(t, OT::Temp, 2, 7); Swz(t, OT::Temp, 1, kXYZW); Cb(t, 0, 6, Enc::Rep(3)); i.End(Op::Mul); }
    if (s.flow) { Ins i(t); i.End(21 /* endif */); }
    if (s.relCb3) {
        // 6b: add r2.xyz, r2.xyzx, cb3[r5.w + 0].xyzx  (an untainted relative instance read inside the slice)
        Ins i(t); Dst(t, OT::Temp, 2, 7); Swz(t, OT::Temp, 2, kXYZW); CbRel(t, 3, 0, 5, 3); i.End(Op::Add);
    }
    // 7: add r4.xyz, r0.xyzx, r2.xyzx          the billboarded position P
    { Ins i(t); Dst(t, OT::Temp, 4, 7); Swz(t, OT::Temp, 0, kXYZW); Swz(t, OT::Temp, 2, kXYZW); i.End(Op::Add); }
    // clip: o0 = worldToProjection * (r4, 1)
    { Ins i(t); Dst(t, OT::Temp, 2, 0xF); Swz(t, OT::Temp, 4, Enc::Rep(1)); Cb(t, 0, 1); i.End(Op::Mul); }
    { Ins i(t); Dst(t, OT::Temp, 2, 0xF); Cb(t, 0, 0); Swz(t, OT::Temp, 4, Enc::Rep(0)); Swz(t, OT::Temp, 2, kXYZW);
      i.End(Op::Mad); }
    { Ins i(t); Dst(t, OT::Temp, 2, 0xF); Cb(t, 0, 2); Swz(t, OT::Temp, 4, Enc::Rep(2)); Swz(t, OT::Temp, 2, kXYZW);
      i.End(Op::Mad); }
    { Ins i(t); Dst(t, OT::Output, 0, 0xF); Swz(t, OT::Temp, 2, kXYZW); Cb(t, 0, 3); i.End(Op::Add); }
    // filler: o1 = r4 * 0.5 (+ more), reading the billboarded position
    for (int k = 0; k <= s.filler; ++k) {
        Ins i(t); Dst(t, OT::Temp, 5, 7); Swz(t, OT::Temp, 4, kXYZW); Enc::ImmScalar(t, 0.5f + 0.25f * k); i.End(Op::Mul);
    }
    { Ins i(t); Dst(t, OT::Output, 1, 0xF); Swz(t, OT::Temp, 5, Enc::Swizzle(0, 1, 2, 2)); i.End(Op::Mov); }
    if (!s.noShadow) {
        const uint32_t src = s.shadowFromCamera ? 4 : 1;
        if (!s.shadowFromCamera) {
            // the shadow reads the UNbillboarded position: r1 = v0 (no camera dependence)
            Ins i(t); Dst(t, OT::Temp, 1, 7); Swz(t, OT::Input, 0, kXYZW); i.End(Op::Mov);
        }
        { Ins i(t); Dst(t, OT::Temp, 2, 0xF); Swz(t, OT::Temp, src, Enc::Rep(1)); Cb(t, 2, 11); i.End(Op::Mul); }
        { Ins i(t); Dst(t, OT::Temp, 2, 0xF); Cb(t, 2, 10); Swz(t, OT::Temp, src, Enc::Rep(0)); Swz(t, OT::Temp, 2, kXYZW);
          i.End(Op::Mad); }
        { Ins i(t); Dst(t, OT::Temp, 2, 0xF); Cb(t, 2, 12); Swz(t, OT::Temp, src, Enc::Rep(2)); Swz(t, OT::Temp, 2, kXYZW);
          i.End(Op::Mad); }
        { Ins i(t); Dst(t, OT::Output, 3, 0xF); Swz(t, OT::Temp, 2, kXYZW); Cb(t, 2, 13); i.End(Op::Add); }
    }
    { Ins i(t); i.End(Op::Ret); }
    return t;
}

std::vector<uint8_t> Blob(const Shape& s, uint32_t version = 0x10040) {   // vs_4_0
    Dxbc::Program p;
    p.version = version;
    p.tokens  = Program(s);
    Dxbc::Container c;
    Dxbc::Chunk a;
    a.fourcc = Dxbc::FourCC('I', 'S', 'G', 'N');
    a.data.assign(24, 0x33);
    Dxbc::Chunk sh;
    sh.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    sh.data   = Dxbc::EmitProgram(p);
    c.chunks = {a, sh};
    return Dxbc::Emit(c);
}

BbsRewrite::Match Analyse(const std::vector<uint8_t>& b) {
    BbsRewrite::Match m;
    BbsRewrite::Analyse(b.data(), b.size(), &m);
    return m;
}

// ---- a tiny SM4 interpreter: mov / add / mul / mad / div / dp3 over temps, inputs, cbuffers, immediates ----
struct Vm {
    float r[16][4] = {};
    float v[4][4]  = {};
    float o[8][4]  = {};
    float cb[4][16][4] = {};
};

bool Fetch(const Vm& vm, const Dxbc::Program& p, const Dxbc::Operand& o, float out[4]) {
    float raw[4];
    switch (o.type) {
    case OT::Temp: std::memcpy(raw, vm.r[o.index[0]], sizeof raw); break;
    case OT::Input: std::memcpy(raw, vm.v[o.index[0]], sizeof raw); break;
    case OT::ConstantBuffer: {
        uint32_t row = o.index[1];
        if (o.relative) {
            // the sub-operand follows the two index dwords: token, reg (select-1)
            const uint32_t* t = p.tokens.data() + o.offset;
            const uint32_t sub = t[3], reg = t[4];
            row += static_cast<uint32_t>(vm.r[reg][(sub >> 4) & 3]);
        }
        std::memcpy(raw, vm.cb[o.index[0]][row], sizeof raw);
        break;
    }
    case OT::Imm32:
        for (int c = 0; c < 4; ++c) std::memcpy(&raw[c], &o.imm[o.numComp == 1 ? 0 : c], 4);
        break;
    case OT::Resource: case OT::Sampler: case 13 /* null */:
        for (int c = 0; c < 4; ++c) raw[c] = 0.0f;   // the pixel stand-ins never read these
        break;
    default: return false;
    }
    for (int c = 0; c < 4; ++c) out[c] = raw[o.numComp == 1 ? 0 : o.Swz(c)];
    if (o.modifier & 1) for (int c = 0; c < 4; ++c) out[c] = -out[c];
    return true;
}

bool Run(Vm& vm, const std::vector<uint8_t>& blob) {
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &c, &err)) return false;
    Dxbc::Program p;
    if (!Dxbc::ParseProgram(*c.Find(Dxbc::FourCC('S', 'H', 'D', 'R')), &p, &err)) return false;
    for (const Dxbc::Instr& in : p.instrs) {
        if (in.isDecl) continue;
        if (in.opcode == Op::Ret) break;
        float a[4], b[4], d[4], res[4];
        const size_t ns = in.operands.size() - 1;
        if (ns >= 1 && !Fetch(vm, p, in.operands[1], a)) return false;
        if (ns >= 2 && !Fetch(vm, p, in.operands[2], b)) return false;
        if (ns >= 3 && !Fetch(vm, p, in.operands[3], d)) return false;
        if (in.opcode == Op::SampleCLz && ns == 4 && !Fetch(vm, p, in.operands[4], d)) return false;   // the compare value
        for (int k = 0; k < 4; ++k) {
            switch (in.opcode) {
            case Op::Mov: res[k] = a[k]; break;
            case Op::Add: res[k] = a[k] + b[k]; break;
            case Op::Mul: res[k] = a[k] * b[k]; break;
            case Op::Mad: res[k] = a[k] * b[k] + d[k]; break;
            case Op::Div: res[k] = a[k] / b[k]; break;
            case Op::Max: res[k] = a[k] > b[k] ? a[k] : b[k]; break;
            case Op::Dp2: res[k] = a[0] * b[0] + a[1] * b[1]; break;
            case 16 /* dp3 */: res[k] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; break;
            case 75 /* sqrt */: res[k] = std::sqrt(a[k]); break;
            case 26 /* frc */: res[k] = a[k] - std::floor(a[k]); break;
            case 65 /* round_ni */: res[k] = std::floor(a[k]); break;
            case 77 /* sincos: dst0 = sin(src), the second (null) destination unwritten */: res[k] = std::sin(b[k]); break;
            case Op::Lt: { const uint32_t m = a[k] < b[k] ? 0xFFFFFFFFu : 0u; std::memcpy(&res[k], &m, 4); break; }
            case Op::And: { uint32_t x, y; std::memcpy(&x, &a[k], 4); std::memcpy(&y, &b[k], 4); x &= y; std::memcpy(&res[k], &x, 4); break; }
            // the pixel stand-ins (ext_better_trees_dither_*): `sample` yields its coordinates, `sample_c_lz` its
            // compare value, so the outputs expose what the dither fed them
            case Op::Sample: res[k] = a[k]; break;
            case Op::SampleCLz: res[k] = d[k]; break;
            default: return false;
            }
        }
        const Dxbc::Operand& o = in.operands[0];
        float* dst = o.type == OT::Temp ? vm.r[o.index[0]] : (o.type == OT::Output ? vm.o[o.index[0]] : nullptr);
        if (dst == nullptr) return false;
        for (int k = 0; k < 4; ++k)
            if (o.compBits & (1u << k)) dst[k] = res[k];
    }
    return true;
}

void Seed(Vm& vm, uint32_t salt) {
    uint32_t x = 0x9e3779b9u ^ salt;
    auto next = [&]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return static_cast<float>(x % 2000) / 1000.0f - 1.0f; };
    for (auto& row : vm.cb) for (auto& reg : row) for (float& f : reg) f = next();
    for (float& f : vm.v[0]) f = next();
    for (float& f : vm.v[1]) f = next();
    // cb2[3].xyz: the light direction, unit length (SunLight::DrawLight hands Context::SetLight a
    // normalised direction).
    float* L = vm.cb[2][3];
    const float len = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    if (len < 0.1f) { L[0] = 0.0f; L[1] = -1.0f; L[2] = 0.0f; }
    else for (int k = 0; k < 3; ++k) L[k] /= len;
    for (int r = 0; r < 4; ++r) vm.cb[3][r][0] = vm.cb[3][r][0];   // cb3: instance rows (any)
}

bool Near(const float a[4], const float b[4], float tol = 1e-4f) {
    for (int c = 0; c < 4; ++c)
        if (std::fabs(a[c] - b[c]) > tol) return false;
    return true;
}

void IdentityShadow(Vm& vm) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) vm.cb[2][10 + r][c] = r == c ? 1.0f : 0.0f;
}

// The rewrite means: o0 / o1 as the original, and with an identity shadow matrix the shadow output is
//   P' = P + (basis dot(C - P, L) - (margin + jitter (2 frc(C.K) - 1)) |C - P|) L
// where P is the original's shadow position, C the card centre (v0, or v0 * s + T with `grow`) and
// L = cb2[3].xyz; the hash reads v0 and T only (THE HASH: never the scale s).
void CheckMeaning(const std::vector<uint8_t>& orig, const std::vector<uint8_t>& xf, bool grow = false) {
    for (uint32_t salt = 1; salt <= 6; ++salt) {
        Vm a, b;
        Seed(a, salt);
        b = a;
        CHECK(Run(a, orig));
        CHECK(Run(b, xf));
        CHECK(Near(b.o[0], a.o[0]));
        CHECK(Near(b.o[1], a.o[1]));

        Vm pv, xv;
        Seed(pv, salt);
        IdentityShadow(pv);
        xv = pv;
        CHECK(Run(pv, orig));
        CHECK(Run(xv, xf));
        float C[3], P[3], L[3], d[3], T[3];
        const float sc = grow ? pv.cb[3][0][3] : 1.0f;
        for (int k = 0; k < 3; ++k) {
            T[k] = grow ? pv.cb[3][3][k] : 0.0f;
            C[k] = pv.v[0][k] * sc + T[k]; P[k] = pv.o[3][k]; L[k] = pv.cb[2][3][k]; d[k] = C[k] - P[k];
        }
        auto dot = [](const float* u, const float* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
        // the hash, in the interpreter's own float order (dp3 = x*x + y*y + z*z, add, then frc)
        const float hv = pv.v[0][0] * BbsRewrite::kHashX + pv.v[0][1] * BbsRewrite::kHashY + pv.v[0][2] * BbsRewrite::kHashZ;
        const float ht = T[0] * BbsRewrite::kTreeX + T[1] * BbsRewrite::kTreeY + T[2] * BbsRewrite::kTreeZ;
        const float hd = hv + ht;
        const float h = hd - std::floor(hd);
        const BbsRewrite::Params k = BbsRewrite::kDefaultParams;
        const float e = h * (2.0f * k.jitter) + (k.margin - k.jitter);
        const float r = std::sqrt(dot(d, d));
        const float s = dot(d, L) * k.basis - e * r;
        // BETTER-TREES: o3.w = -(r * |cb2[12].xyz|) = -r under the identity shadow matrix (the extent)
        float want[4] = {P[0] + s * L[0], P[1] + s * L[1], P[2] + s * L[2], -r};
        CHECK(Near(xv.o[3], want, 2e-3f));
        if (std::fabs(s) > 1e-3f) CHECK(!Near(xv.o[3], pv.o[3]));   // the lookup really moved (in depth)
        // its depth is P's moved by s along L ...
        float e3[3] = {xv.o[3][0] - P[0], xv.o[3][1] - P[1], xv.o[3][2] - P[2]};
        CHECK(std::fabs(dot(e3, L) - s) < 2e-3f);
        // ... and across L it is P's (the same texel)
        float dx[3] = {xv.o[3][0] - P[0], xv.o[3][1] - P[1], xv.o[3][2] - P[2]};
        float across[3];
        const float along = dot(dx, L);
        for (int k = 0; k < 3; ++k) across[k] = dx[k] - along * L[k];
        CHECK(std::sqrt(dot(across, across)) < 1e-4f);
    }
}

}  // namespace

UNIT_TEST(ext_better_trees_receiver_match_and_meaning) {
    for (int filler = 0; filler < 3; ++filler) {
        for (int rel = 0; rel < 4; ++rel) {
            Shape s;
            s.filler = filler;
            s.relCb3 = (rel & 1) != 0;
            s.grow   = (rel & 2) != 0;
            const std::vector<uint8_t> b = Blob(s);
            const BbsRewrite::Match m = Analyse(b);
            CHECK(m.container && m.program && m.vertex && m.candidate);
            CHECK(m.matched);
            CHECK_STREQ(m.why.c_str(), "");
            CHECK_EQ(m.temps, 6u);
            CHECK_EQ(m.shadowOut, 3u);
            CHECK_EQ(m.posReg, 4u);
            CHECK_EQ(m.centerOperand, 1);   // add r4, r0 (the centre), r2 (the camera-facing offset)
            std::vector<uint8_t> out;
            CHECK(BbsRewrite::Transform(m, &out));
            // the copy is a valid container and is not rewritten twice
            const BbsRewrite::Match m2 = Analyse(out);
            CHECK(m2.container && m2.program);
            CHECK(!m2.matched);
            uint32_t temps2 = 0;
            for (const Dxbc::Instr& in : m2.prog.instrs)
                if (in.opcode == Op::DclTemps) temps2 = m2.prog.tokens[in.offset + 1];
            CHECK_EQ(temps2, 9u);   // T + 3
            CheckMeaning(b, out, s.grow);
        }
    }
}

// THE HASH (TS-GROW): the lookup of a growing tree does not re-roll. Two runs that differ only in the
// instance scale (cb3[0].w) move each card's cut by its scaled extent, never by a new hash: with
// jitter alone (basis 0, margin 0) e / r is the same in both.
UNIT_TEST(ext_better_trees_receiver_hash_ignores_growth) {
    Shape s;
    s.grow = true;
    const std::vector<uint8_t> b = Blob(s);
    const BbsRewrite::Match m = Analyse(b);
    CHECK(m.matched);
    CHECK(m.inputIdx >= 0);
    CHECK(m.xlateIdx >= 0);
    std::vector<uint8_t> out;
    BbsRewrite::Params k = BbsRewrite::kDefaultParams;
    k.basis = 0.0f;
    k.margin = 0.0f;
    CHECK(BbsRewrite::TransformWithParams(m, &out, k));
    for (uint32_t salt = 1; salt <= 6; ++salt) {
        float ratio[2] = {};
        for (int run = 0; run < 2; ++run) {
            Vm vm;
            Seed(vm, salt);
            IdentityShadow(vm);
            vm.cb[3][0][3] = run == 0 ? 0.9f : 0.9009f;   // a 0.1% growth step
            Vm pv = vm;
            CHECK(Run(pv, b));
            CHECK(Run(vm, out));
            const float* L = vm.cb[2][3];
            float along = 0.0f;
            for (int c = 0; c < 3; ++c) along += (vm.o[3][c] - pv.o[3][c]) * L[c];   // = -e r
            const float r = -vm.o[3][3];
            if (r > 1e-3f) ratio[run] = -along / r;   // = e, from the hash alone
        }
        CHECK(std::fabs(ratio[0] - ratio[1]) < 1e-3f);
    }
}

// ---- the pixel side: edge_dither (BETTER-TREES) ------------------------------------------------

namespace {

constexpr uint32_t kDclInputPs = 98, kSampleCLz = 71;

// A synthetic ps_4_0 in the SHAPE of a billboard colour program: a diffuse sample from v1.xy, a
// shadow compare from v3.z (two taps, one through a temp offset), the compare value written out.
// `shape` 0 = that; 1 = no sample_c (a depth variant); 2 = the compare value comes through a temp
// only; 3 = the texcoord sample reads a temp (no input uv); 4 = flow control.
Tokens DitherProgram(int shape) {
    Tokens t;
    auto dclInput = [&](uint32_t reg, uint32_t mask) {
        Ins i(t);
        t.push_back(Comp4(0, mask, OT::Input, 1));
        t.push_back(reg);
        t[i.at] = Enc::Opcode(kDclInputPs, static_cast<uint32_t>(t.size() - i.at)) | (2u << 11);   // linear
    };
    dclInput(1, 0xF);
    dclInput(3, 0x7);
    { Ins i(t); t.push_back(3); i.End(Op::DclTemps); }
    if (shape == 4) { Ins i(t); t.push_back(Comp4(2, 0, OT::Temp, 1)); t.push_back(0); i.End(31 /* if */); }
    // 0: sample r0.xyzw, v1.xyxx, t0, s0      (3: from r2 instead of v1)
    { Ins i(t); Dst(t, OT::Temp, 0, 0xF);
      if (shape == 3) Swz(t, OT::Temp, 2, Enc::Swizzle(0, 1, 0, 0)); else Swz(t, OT::Input, 1, Enc::Swizzle(0, 1, 0, 0));
      Swz(t, OT::Resource, 0, kXYZW); t.push_back(Comp4(0, 0, OT::Sampler, 1) & ~3u); t.push_back(0); i.End(Op::Sample); }
    if (shape == 4) { Ins i(t); i.End(21 /* endif */); }
    if (shape != 1) {
        // 1: mov r1.xyz, v3.xyzx             2: sample_c_lz r1.w, r1.xyxx, t5, s5, r1.z   (a tap through a temp)
        { Ins i(t); Dst(t, OT::Temp, 1, 7); Swz(t, OT::Input, 3, Enc::Swizzle(0, 1, 2, 0)); i.End(Op::Mov); }
        { Ins i(t); Dst(t, OT::Temp, 1, 8); Swz(t, OT::Temp, 1, Enc::Swizzle(0, 1, 0, 0)); Swz(t, OT::Resource, 5, Enc::Rep(0));
          t.push_back(Comp4(0, 0, OT::Sampler, 1) & ~3u); t.push_back(5);
          t.push_back(Comp4(2, 2, OT::Temp, 1)); t.push_back(1); i.End(kSampleCLz); }
        if (shape != 2) {
            // 3: sample_c_lz r0.w, v3.xyxx, t5, s5, v3.z   (the direct tap)
            Ins i(t); Dst(t, OT::Temp, 0, 8); Swz(t, OT::Input, 3, Enc::Swizzle(0, 1, 0, 0)); Swz(t, OT::Resource, 5, Enc::Rep(0));
            t.push_back(Comp4(0, 0, OT::Sampler, 1) & ~3u); t.push_back(5);
            t.push_back(Comp4(2, 2, OT::Input, 1)); t.push_back(3); i.End(kSampleCLz);
        } else {
            Ins i(t); Dst(t, OT::Temp, 0, 8); Swz(t, OT::Temp, 1, Enc::Rep(3)); i.End(Op::Mov);
        }
        // 4: mov o0.xyzw, r0.wwww   (the direct tap's compare value)   5: mov o1.xyzw, r1.wwww (the temp tap's)
        { Ins i(t); Dst(t, OT::Output, 0, 0xF); Swz(t, OT::Temp, 0, Enc::Rep(3)); i.End(Op::Mov); }
        { Ins i(t); Dst(t, OT::Output, 1, 0xF); Swz(t, OT::Temp, 1, Enc::Rep(3)); i.End(Op::Mov); }
    } else {
        Ins i(t); Dst(t, OT::Output, 0, 0xF); Swz(t, OT::Temp, 0, kXYZW); i.End(Op::Mov);
    }
    { Ins i(t); i.End(Op::Ret); }
    return t;
}

// ISGN: two TEXCOORD elements (v1 xyzw read xyzw, v3 xyzw read xyz), the real layout.
std::vector<uint8_t> Isgn(uint8_t v3Provided = 0xF) {
    std::vector<uint8_t> d;
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    w32(2); w32(8);
    const uint32_t name = 8 + 2 * 24;
    w32(name); w32(1); w32(0); w32(3); w32(1); d.push_back(0xF); d.push_back(0xF); d.push_back(0); d.push_back(0);
    w32(name); w32(3); w32(0); w32(3); w32(3); d.push_back(v3Provided); d.push_back(0x7); d.push_back(0); d.push_back(0);
    for (char c : std::string("TEXCOORD")) d.push_back(static_cast<uint8_t>(c));
    d.push_back(0);
    while (d.size() % 4) d.push_back(0);
    return d;
}

std::vector<uint8_t> DitherBlob(int shape, bool realIsgn = true, uint8_t v3Provided = 0xF) {
    Dxbc::Program p;
    p.version = 0x40;   // ps_4_0
    p.tokens  = DitherProgram(shape);
    Dxbc::Container c;
    Dxbc::Chunk a;
    a.fourcc = Dxbc::FourCC('I', 'S', 'G', 'N');
    if (realIsgn) a.data = Isgn(v3Provided); else a.data.assign(24, 0x33);
    Dxbc::Chunk sh;
    sh.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    sh.data   = Dxbc::EmitProgram(p);
    c.chunks = {a, sh};
    return Dxbc::Emit(c);
}

uint8_t IsgnReadMask(const std::vector<uint8_t>& blob, uint32_t reg) {
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &c, &err)) return 0xEE;
    const Dxbc::Chunk* isgn = c.Find(Dxbc::FourCC('I', 'S', 'G', 'N'));
    if (isgn == nullptr) return 0xEE;
    uint32_t count;
    std::memcpy(&count, isgn->data.data(), 4);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = isgn->data.data() + 8 + 24 * i;
        uint32_t r;
        std::memcpy(&r, e + 16, 4);
        if (r == reg) return e[21];
    }
    return 0xEE;
}

}  // namespace

UNIT_TEST(ext_better_trees_dither_match_and_meaning) {
    const std::vector<uint8_t> b = DitherBlob(0);
    BbsDither::Match m;
    BbsDither::Analyse(b.data(), b.size(), &m);
    CHECK(m.container && m.program && m.pixel && m.candidate);
    CHECK(m.matched);
    CHECK_STREQ(m.why.c_str(), "");
    CHECK_EQ(m.temps, 3u);
    CHECK_EQ(m.shadowIn, 3u);
    CHECK_EQ(m.shadowComp, 2u);
    CHECK_EQ(m.uvIn, 1u);
    BbsRewrite::Params k = BbsRewrite::kDefaultParams;
    k.noise = 0.6f;
    k.noiseScale = 32.0f;
    std::vector<uint8_t> out;
    CHECK(BbsDither::Transform(m, &out, k));
    // the copy: a valid container, v3 declared and signed xyzw, temps + 2, not rewritten twice
    BbsDither::Match m2;
    BbsDither::Analyse(out.data(), out.size(), &m2);
    CHECK(m2.container && m2.program && m2.candidate);
    CHECK(!m2.matched);
    CHECK_EQ(IsgnReadMask(out, 3), 0xF);
    CHECK_EQ(IsgnReadMask(out, 1), 0xF);
    uint32_t temps2 = 0, v3mask = 0;
    for (const Dxbc::Instr& in : m2.prog.instrs) {
        if (in.opcode == Op::DclTemps) temps2 = m2.prog.tokens[in.offset + 1];
        if (in.opcode == kDclInputPs && m2.prog.tokens[in.offset + 2] == 3) v3mask = (m2.prog.tokens[in.offset + 1] >> 4) & 0xF;
    }
    CHECK_EQ(temps2, 5u);
    CHECK_EQ(v3mask, 0xFu);
    // meaning: both taps see z + (h - 0.5) * noise * max(-w, 0); w = +1 (a retail VS) leaves z alone
    for (uint32_t salt = 1; salt <= 6; ++salt) {
        Vm a, c;
        Seed(a, salt);
        a.v[3][0] = 0.25f * salt; a.v[3][1] = 0.5f - 0.1f * salt; a.v[3][2] = 0.3f + 0.1f * salt;   // the shadow interpolant
        // w: the extent (negative, tiny), retail's +1, or a negative value past kExtentLimit (not ours)
        a.v[3][3] = salt % 3 == 0 ? -0.037f : (salt % 3 == 1 ? 1.0f : -0.37f);
        c = a;
        CHECK(Run(a, b));
        CHECK(Run(c, out));
        const float u = std::floor(a.v[1][0] * k.noiseScale), v = std::floor(a.v[1][1] * k.noiseScale);
        const float hd = std::sin(u * BbsDither::kHashU + v * BbsDither::kHashV) * BbsDither::kHashMul;
        const float h = hd - std::floor(hd);
        const float amp = a.v[3][3] < 0.0f && -a.v[3][3] < BbsDither::kExtentLimit ? -a.v[3][3] : 0.0f;
        const float want = a.v[3][2] + (h - 0.5f) * amp * k.noise;
        CHECK(std::fabs(c.o[0][0] - want) < 2e-3f);
        CHECK(std::fabs(c.o[1][0] - want) < 2e-3f);   // the tap through the temp copy sees the same z'
        CHECK(std::fabs(a.o[0][0] - a.v[3][2]) < 1e-6f);
        if (amp == 0.0f) CHECK(std::fabs(c.o[0][0] - a.v[3][2]) < 1e-6f);
        else CHECK(std::fabs(c.o[0][0] - a.v[3][2]) > 1e-4f || std::fabs(h - 0.5f) < 1e-3f);
    }
}

UNIT_TEST(ext_better_trees_dither_refusals) {
    { BbsDither::Match m; const std::vector<uint8_t> b = DitherBlob(1); BbsDither::Analyse(b.data(), b.size(), &m);
      CHECK(!m.matched && !m.candidate); }                      // no sample_c at all (a depth variant)
    { BbsDither::Match m; const std::vector<uint8_t> b = DitherBlob(2); BbsDither::Analyse(b.data(), b.size(), &m);
      CHECK(m.candidate && !m.matched); }                       // no direct input compare
    { BbsDither::Match m; const std::vector<uint8_t> b = DitherBlob(3); BbsDither::Analyse(b.data(), b.size(), &m);
      CHECK(m.candidate && !m.matched); }                       // no input texcoord
    { BbsDither::Match m; const std::vector<uint8_t> b = DitherBlob(4); BbsDither::Analyse(b.data(), b.size(), &m);
      CHECK(!m.matched); }                                      // flow control
    { BbsDither::Match m; const std::vector<uint8_t> b = DitherBlob(0, false); BbsDither::Analyse(b.data(), b.size(), &m);
      CHECK(m.candidate && !m.matched); }                       // a malformed ISGN
    { BbsDither::Match m; const std::vector<uint8_t> b = DitherBlob(0, true, 0x7); BbsDither::Analyse(b.data(), b.size(), &m);
      CHECK(m.candidate && !m.matched); }                       // the vertex stage does not write v3.w
    { Shape s; const std::vector<uint8_t> b = Blob(s);          // the vertex program is not a pixel shader
      BbsDither::Match m; BbsDither::Analyse(b.data(), b.size(), &m);
      CHECK(!m.matched && !m.pixel); }
}

UNIT_TEST(ext_better_trees_receiver_refusals) {
    {
        Shape s; s.noShadow = true;
        const BbsRewrite::Match m = Analyse(Blob(s));
        CHECK(!m.matched && !m.candidate);
    }
    {
        Shape s; s.shadowFromCamera = false;   // the shadow does not depend on the camera rows
        const BbsRewrite::Match m = Analyse(Blob(s));
        CHECK(m.candidate && !m.matched);
    }
    {
        Shape s; s.cb2Rows = 8;   // shadowProjection[0] not declared
        const BbsRewrite::Match m = Analyse(Blob(s));
        CHECK(!m.matched);
    }
    {
        Shape s; s.flow = true;
        const BbsRewrite::Match m = Analyse(Blob(s));
        CHECK(!m.matched);
    }
    {
        Shape s; s.noInput = true;   // the centre is not a vertex input: no growth-stable hash, kept retail
        const BbsRewrite::Match m = Analyse(Blob(s));
        CHECK(m.candidate && !m.matched);
    }
    {
        Shape s; s.relCb0 = true;   // a relative gc read: which rows it reads is unknown
        const BbsRewrite::Match m = Analyse(Blob(s));
        CHECK(!m.matched);
    }
    {
        Shape s;
        const BbsRewrite::Match m = Analyse(Blob(s, 0x40));   // a pixel shader with the same body
        CHECK(!m.matched && !m.vertex);
    }
    {
        Shape s;
        std::vector<uint8_t> b = Blob(s);
        b[b.size() - 9] ^= 0x01;   // checksum no longer valid: the round trip refuses it
        const BbsRewrite::Match m = Analyse(b);
        CHECK(!m.matched);
    }
}

UNIT_TEST(ext_better_trees_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("better_trees");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2, ruled 2026-10-18)
    CHECK_STREQ(Ext::SettingsKey(*e).c_str(), "ext.better_trees");
    CHECK_STREQ(e->displayName, "Better trees");
}

}  // namespace bbs

// ============================================================================
// faa: the foliage cutout side (dxbc, cutout_rewrite)
// ============================================================================
namespace faa {

namespace {

namespace Op  = Dxbc::Op;
namespace OT  = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;
using Tokens  = std::vector<uint32_t>;

// ---- operand helpers the encoder does not have (inputs, resources, selects) ----
uint32_t Comp4(uint32_t sel, uint32_t bits, uint32_t type, uint32_t dims) {
    return 2u | (sel << 2) | (bits << 4) | (type << 12) | (dims << 20);
}
void Sel1(Tokens& t, uint32_t type, uint32_t reg, uint32_t comp, uint32_t neg = 0) {
    t.push_back(Comp4(2, comp, type, 1) | (neg ? 0x80000000u : 0u));
    if (neg) t.push_back(1u | (1u << 6));
    t.push_back(reg);
}
void Swz(Tokens& t, uint32_t type, uint32_t reg, uint32_t swz) {
    t.push_back(Comp4(1, swz, type, 1));
    t.push_back(reg);
}
void Dst(Tokens& t, uint32_t type, uint32_t reg, uint32_t mask) {
    t.push_back(Comp4(0, mask, type, 1));
    t.push_back(reg);
}
void SamplerOp(Tokens& t, uint32_t reg) {
    t.push_back(0u | (OT::Sampler << 12) | (1u << 20));
    t.push_back(reg);
}
// -cb0[row].comp
void NegCb(Tokens& t, uint32_t row, uint32_t comp) {
    t.push_back(Comp4(2, comp, OT::ConstantBuffer, 2) | 0x80000000u);
    t.push_back(1u | (1u << 6));
    t.push_back(0);
    t.push_back(row);
}

struct Ins {
    Tokens& t;
    size_t  at;
    Ins(Tokens& tt) : t(tt), at(tt.size()) { t.push_back(0); }
    void End(uint32_t op, uint32_t extra = 0) { t[at] = Enc::Opcode(op, static_cast<uint32_t>(t.size() - at), false, extra); }
};
constexpr uint32_t kTestNonZero = 1u << 18;

struct Shape {
    bool     seasonMad  = false;   // G1: mad r0.w, -cb0[4].z, v2.w, r0.w
    bool     separateRt = false;   // G5 / G6: the test in r1.x instead of over r0.w
    uint32_t temps      = 3;       // 3 / 3 / 4 / 4 over DETAIL0..3
    int      filler     = 0;       // DETAIL variants differ in the lighting length
    // near misses
    bool     noO0w      = false;   // the depth programs: no `mov o0.w, l(0)`
    bool     discardZ   = false;
    bool     flow       = false;   // an if / endif around the lighting
    bool     waterClip  = false;   // lt on an interpolant, not on the add's result
    bool     movBetween = false;   // depth shape: a mov between the add and the lt
    float    cut        = 0.5f;
};

Tokens Program(const Shape& s) {
    Tokens t;
    { Ins i(t); i.End(Op::DclGlobalFlags, 1u << 11); }   // refactoringAllowed
    { Ins i(t); t.push_back(s.temps); i.End(Op::DclTemps); }
    // sample r0.xyzw, v1.xyxx, t0.xyzw, s0
    { Ins i(t); Dst(t, OT::Temp, 0, 0xF); Swz(t, OT::Input, 1, Enc::Swizzle(0, 1, 0, 0));
      Swz(t, OT::Resource, 0, Enc::Swizzle(0, 1, 2, 3)); SamplerOp(t, 0); i.End(Op::Sample); }
    if (s.seasonMad) {
        Ins i(t); Dst(t, OT::Temp, 0, 8); NegCb(t, 4, 2); Sel1(t, OT::Input, 2, 3); Sel1(t, OT::Temp, 0, 3);
        i.End(Op::Mad);
    }
    const uint32_t rT = s.separateRt ? 1 : 0, cT = s.separateRt ? 0 : 3;
    { Ins i(t); Dst(t, OT::Temp, rT, 1u << cT); Sel1(t, OT::Temp, 0, 3); Enc::ImmScalar(t, -s.cut); i.End(Op::Add); }
    if (s.movBetween) { Ins i(t); Dst(t, OT::Output, 0, 0xF); Swz(t, OT::Temp, 0, Enc::Swizzle(0, 1, 2, 3)); i.End(Op::Mov); }
    if (s.waterClip) {
        Ins i(t); Dst(t, OT::Temp, rT, 1u << cT); Sel1(t, OT::Input, 4, 1, 1); Enc::ImmScalar(t, 0.0f); i.End(Op::Lt);
    } else {
        Ins i(t); Dst(t, OT::Temp, rT, 1u << cT); Sel1(t, OT::Temp, rT, cT); Enc::ImmScalar(t, 0.0f); i.End(Op::Lt);
    }
    { Ins i(t); Sel1(t, OT::Temp, rT, cT); i.End(Op::Discard, s.discardZ ? 0u : kTestNonZero); }
    if (s.flow) { Ins i(t); Sel1(t, OT::Temp, 0, 0); i.End(31 /* if */, kTestNonZero); }
    // "lighting": a few movs / muls over r1 / r2 (the DETAIL variants differ in length)
    { Ins i(t); Dst(t, OT::Temp, 1, 7); Swz(t, OT::Temp, 0, Enc::Swizzle(0, 1, 2, 0)); i.End(Op::Mov); }
    for (int k = 0; k < s.filler; ++k) {
        Ins i(t); Dst(t, OT::Temp, 2, 7); Swz(t, OT::Temp, 1, Enc::Swizzle(0, 1, 2, 0));
        Enc::ImmScalar(t, 0.5f + 0.125f * static_cast<float>(k)); i.End(Op::Mul);
    }
    if (s.flow) { Ins i(t); i.End(21 /* endif */); }
    { Ins i(t); Dst(t, OT::Output, 0, 7); Swz(t, OT::Temp, 1, Enc::Swizzle(0, 1, 2, 0)); i.End(Op::Mov); }
    if (!s.noO0w) { Ins i(t); Dst(t, OT::Output, 0, 8); Enc::ImmScalar(t, 0.0f); i.End(Op::Mov); }
    { Ins i(t); i.End(Op::Ret); }
    return t;
}

// A container: a dummy RDEF-like chunk (our own bytes), the SHDR, a dummy OSGN-like chunk.
std::vector<uint8_t> Blob(const Shape& s, uint32_t version = 0x40) {
    Dxbc::Program p;
    p.version = version;
    p.tokens  = Program(s);
    Dxbc::Container c;
    Dxbc::Chunk a;
    a.fourcc = Dxbc::FourCC('R', 'D', 'E', 'F');
    a.data.assign(28, 0x11);
    Dxbc::Chunk sh;
    sh.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    sh.data   = Dxbc::EmitProgram(p);
    Dxbc::Chunk o;
    o.fourcc = Dxbc::FourCC('O', 'S', 'G', 'N');
    o.data.assign(32, 0x22);
    c.chunks = {a, sh, o};
    return Dxbc::Emit(c);
}

FaaRewrite::Match Analyse(const std::vector<uint8_t>& b) {
    FaaRewrite::Match m;
    FaaRewrite::Analyse(b.data(), b.size(), &m);
    return m;
}

int CountOp(const FaaRewrite::Match& m, uint32_t op) {
    int n = 0;
    for (const Dxbc::Instr& in : m.prog.instrs) n += in.opcode == op;
    return n;
}

// The one `mov o0.w, <src>` of a program, or null.
const Dxbc::Instr* MovO0w(const FaaRewrite::Match& m) {
    for (const Dxbc::Instr& in : m.prog.instrs)
        if (in.opcode == Op::Mov && in.operands.size() == 2 && in.operands[0].type == OT::Output &&
            in.operands[0].compBits == 8)
            return &in;
    return nullptr;
}

uint32_t TempsOf(const FaaRewrite::Match& m) {
    for (const Dxbc::Instr& in : m.prog.instrs)
        if (in.opcode == Op::DclTemps) return m.prog.tokens[in.offset + 1];
    return 0;
}

bool ChunkEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t fourcc) {
    Dxbc::Container ca, cb;
    std::string err;
    if (!Dxbc::Parse(a.data(), a.size(), &ca, &err) || !Dxbc::Parse(b.data(), b.size(), &cb, &err)) return false;
    const Dxbc::Chunk* x = ca.Find(fourcc);
    const Dxbc::Chunk* y = cb.Find(fourcc);
    return x && y && x->data == y->data;
}

}  // namespace

// ---- the container + checksum ------------------------------------------------

UNIT_TEST(ext_better_trees_dxbc_container_roundtrip_and_checksum) {
    // Both of the checksum's final-block layouts: (size - 20) % 64 below and at / above 56.
    int below = 0, above = 0;
    for (int filler = 0; filler < 24; ++filler) {
        Shape s;
        s.filler = filler;
        const std::vector<uint8_t> b = Blob(s);
        ((b.size() - 20) % 64 >= 56 ? above : below)++;
        const FaaRewrite::Match m = Analyse(b);
        CHECK(m.container && m.program);
        CHECK(m.identity);   // parse -> re-emit is byte-identical
        CHECK(m.checksum);   // our checksum verifies our own emit
        uint32_t sum[4];
        Dxbc::Checksum(b.data(), b.size(), sum);
        CHECK(std::memcmp(sum, b.data() + 4, 16) == 0);
        // a flipped bit anywhere past the checksum is caught
        std::vector<uint8_t> bad = b;
        bad[bad.size() - 5] ^= 0x10;
        Dxbc::Checksum(bad.data(), bad.size(), sum);
        CHECK(std::memcmp(sum, bad.data() + 4, 16) != 0);
        const FaaRewrite::Match mb = Analyse(bad);
        CHECK(!mb.checksum && !mb.colour);
    }
    CHECK(below > 0 && above > 0);
    // a frozen value: the checksum of one synthetic container (regression guard for the MD5 variant)
    {
        Shape s;
        const std::vector<uint8_t> b = Blob(s);
        uint32_t sum[4];
        Dxbc::Checksum(b.data(), b.size(), sum);
        // frozen 2026-10-16 (FAA-CORE); the algorithm itself was proven on 148/148 installed blobs (FAA-SPIKE S3)
        CHECK_EQ(b.size(), static_cast<size_t>(316));
        CHECK_EQ(sum[0], 0x1f38e537u);
        CHECK_EQ(sum[1], 0x048539b6u);
        CHECK_EQ(sum[2], 0x60a69380u);
        CHECK_EQ(sum[3], 0xb00741a1u);
    }
    // not a container
    std::vector<uint8_t> junk(64, 0x5a);
    const FaaRewrite::Match mj = Analyse(junk);
    CHECK(!mj.container && !mj.colour);
}

UNIT_TEST(ext_better_trees_dxbc_checksum_covers_bytes_20_on) {
    // DXBC's checksum covers bytes [20, size): the magic and the checksum itself are outside it.
    // An 84-byte input = exactly one full 64-byte block, then the length block.
    std::vector<uint8_t> z(84, 0);
    uint32_t a[4], b[4];
    Dxbc::Checksum(z.data(), z.size(), a);
    z[20] = 1;
    Dxbc::Checksum(z.data(), z.size(), b);
    CHECK(std::memcmp(a, b, 16) != 0);
    z[20] = 0;
    z[0]  = 0xff;   // bytes 0..19 are not covered
    Dxbc::Checksum(z.data(), z.size(), b);
    CHECK(std::memcmp(a, b, 16) == 0);
}

// ---- the grammar ---------------------------------------------------------------

UNIT_TEST(ext_better_trees_cutout_grammar_matches_both_rt_variants_and_the_season_mad) {
    for (int g = 0; g < 4; ++g) {
        Shape s;
        s.seasonMad  = g == 0 || g == 2;
        s.separateRt = g >= 2;
        const FaaRewrite::Match m = Analyse(Blob(s));
        CHECK(m.colour);
        CHECK_STREQ(m.why.c_str(), "colour");
        CHECK_EQ(m.iMad >= 0, s.seasonMad);
        CHECK_EQ(m.cut, 0.5f);
        CHECK_EQ(m.alpha.index[0], 0u);
        CHECK_EQ(m.alphaComp, 3u);   // r0.w
        CHECK_EQ(m.temps, 3u);
        CHECK_EQ(m.iLt, m.iDiscard - 1);
        CHECK_EQ(m.iAdd, m.iDiscard - 2);
        CHECK(m.iSample >= 0 && m.iSample < m.iAdd);
    }
}

UNIT_TEST(ext_better_trees_cutout_grammar_refuses_the_near_misses) {
    struct Case { const char* name; Shape s; uint32_t version; };
    std::vector<Case> cases;
    { Shape s; s.noO0w = true;      cases.push_back({"depth: no mov o0.w", s, 0x40}); }
    { Shape s; s.discardZ = true;   cases.push_back({"discard_z", s, 0x40}); }
    { Shape s; s.flow = true;       cases.push_back({"flow control", s, 0x40}); }
    { Shape s; s.waterClip = true;  cases.push_back({"water clip", s, 0x40}); }
    { Shape s; s.movBetween = true; cases.push_back({"mov between add and lt", s, 0x40}); }
    { Shape s; s.cut = 1.5f;        cases.push_back({"cut outside (0,1)", s, 0x40}); }
    { Shape s;                      cases.push_back({"vertex program", s, 0x10040u}); }
    { Shape s;                      cases.push_back({"ps_5_0", s, 0x50}); }
    for (const Case& c : cases) {
        const std::vector<uint8_t> b = Blob(c.s, c.version);
        const std::vector<uint8_t> keep = b;
        const FaaRewrite::Match m = Analyse(b);
        if (m.colour) std::printf("  [faa] near miss '%s' matched\n", c.name);
        CHECK(!m.colour);
        CHECK(m.identity);   // readable, only not ours
        std::vector<uint8_t> out;
        for (int mode = 1; mode <= 2; ++mode)
            for (int msaa = 0; msaa <= 1; ++msaa)
                CHECK(!FaaRewrite::Transform(m, FaaRewrite::OptionsFor(mode, 0.25f, msaa != 0), &out));
        CHECK(b == keep);   // the blob handed to CreatePixelShader stays byte-identical
    }
}

// ---- the emit --------------------------------------------------------------------

UNIT_TEST(ext_better_trees_cutout_emit_coverage_all_detail_variants) {
    const uint32_t temps[4] = {3, 3, 4, 4};
    for (int g = 0; g < 2; ++g) {
        for (int v = 0; v < 4; ++v) {
            Shape s;
            s.separateRt = g == 1;
            s.seasonMad  = g == 0;
            s.temps      = temps[v];
            s.filler     = v * 3;
            const std::vector<uint8_t> in = Blob(s);
            const FaaRewrite::Match m = Analyse(in);
            CHECK(m.colour);
            std::vector<uint8_t> out;
            CHECK(FaaRewrite::Transform(m, FaaRewrite::OptionsFor(1, 0.25f, true), &out));
            const FaaRewrite::Match t = Analyse(out);
            CHECK(t.program && t.identity && t.checksum);
            CHECK_EQ(t.discards, 0);                   // dropped
            CHECK_EQ(TempsOf(t), s.temps + 1);         // P only
            CHECK_EQ(static_cast<int>(t.prog.instrs.size()),
                     static_cast<int>(m.prog.instrs.size()) - 1 + 1 + 7);   // - discard + save + sharpen
            const Dxbc::Instr* o = MovO0w(t);
            CHECK(o != nullptr && o->operands[1].type == OT::Temp && o->operands[1].index[0] == s.temps);
            // the alpha is saved to P.x right before the retail add, from rA.c (r0.w)
            const Dxbc::Instr& save = t.prog.instrs[static_cast<size_t>(m.iAdd)];
            CHECK_EQ(save.opcode, static_cast<uint32_t>(Op::Mov));
            CHECK(save.operands[0].index[0] == s.temps && save.operands[0].compBits == 1);
            CHECK(save.operands[1].index[0] == 0 && save.operands[1].Swz(0) == 3);
            // the sharpen ends with add_sat P.x, P.x, l(0.5)
            const Dxbc::Instr& last = t.prog.instrs[static_cast<size_t>(m.iDiscard) + 1 + 6];
            CHECK(last.opcode == Op::Add && last.Saturate() && last.operands[2].ImmF(0) == 0.5f);
            CHECK_EQ(CountOp(t, Op::DerivRtx), 1);
            CHECK_EQ(CountOp(t, Op::DerivRty), 1);
            CHECK_EQ(CountOp(t, Op::ResInfo), 0);      // no fill in mode 1
            // the other chunks are untouched
            CHECK(ChunkEqual(in, out, Dxbc::FourCC('R', 'D', 'E', 'F')));
            CHECK(ChunkEqual(in, out, Dxbc::FourCC('O', 'S', 'G', 'N')));
            // everything before the add is retail's, token for token (the season mad included)
            const uint32_t addAt = m.prog.instrs[static_cast<size_t>(m.iAdd)].offset;
            for (uint32_t k = 0; k < addAt; ++k)
                if (k != m.prog.instrs[static_cast<size_t>(m.iTemps)].offset + 1)   // dcl_temps' count
                    CHECK_EQ(t.prog.tokens[k], m.prog.tokens[k]);
        }
    }
}

UNIT_TEST(ext_better_trees_cutout_emit_fill_with_and_without_coverage) {
    Shape s;
    s.seasonMad = true;
    const FaaRewrite::Match m = Analyse(Blob(s));
    CHECK(m.colour);
    // mode 2, MSAA on: fill + coverage
    {
        std::vector<uint8_t> out;
        CHECK(FaaRewrite::Transform(m, FaaRewrite::OptionsFor(2, 0.25f, true), &out));
        const FaaRewrite::Match t = Analyse(out);
        CHECK(t.program && t.identity && t.checksum);
        CHECK_EQ(t.discards, 0);
        CHECK_EQ(TempsOf(t), s.temps + 3);
        CHECK_EQ(CountOp(t, Op::ResInfo), 1);
        CHECK_EQ(CountOp(t, Op::Log), 1);
        CHECK_EQ(CountOp(t, Op::DerivRtx), 2);     // the uv derivatives + fwidth
        // k is an immediate: the mad P.x, P.x, l(k), l(1)
        bool kFound = false;
        for (const Dxbc::Instr& in : t.prog.instrs)
            if (in.opcode == Op::Mad && in.operands.size() == 4 && in.operands[2].type == OT::Imm32 &&
                in.operands[2].ImmF(0) == 0.25f && in.operands[3].ImmF(0) == 1.0f)
                kFound = true;
        CHECK(kFound);
        // the season mad survives, before the fill reads the alpha
        CHECK_EQ(CountOp(t, Op::Mad), 2);
        const Dxbc::Instr* o = MovO0w(t);
        CHECK(o != nullptr && o->operands[1].type == OT::Temp);
        // a different k re-emits a different blob (the shaders are re-created on a k change)
        std::vector<uint8_t> out2;
        CHECK(FaaRewrite::Transform(m, FaaRewrite::OptionsFor(2, 0.5f, true), &out2));
        CHECK(out2.size() == out.size() && out2 != out);
    }
    // mode 2, MSAA off: the discard stays, o0.w stays 0, only the fill
    {
        std::vector<uint8_t> out;
        CHECK(FaaRewrite::Transform(m, FaaRewrite::OptionsFor(2, 0.25f, false), &out));
        const FaaRewrite::Match t = Analyse(out);
        CHECK(t.program && t.identity && t.checksum);
        CHECK_EQ(t.discards, 1);
        CHECK_EQ(t.o0wCount, 1);                   // still `mov o0.w, l(0)`
        CHECK_EQ(CountOp(t, Op::ResInfo), 1);
        CHECK_EQ(CountOp(t, Op::DerivRtx), 1);     // no fwidth
        // the retail add now reads the filled alpha (P.x), not r0.w
        const Dxbc::Instr& add = t.prog.instrs[static_cast<size_t>(t.iDiscard) - 2];
        CHECK_EQ(add.opcode, static_cast<uint32_t>(Op::Add));
        CHECK(add.operands[1].type == OT::Temp && add.operands[1].index[0] == s.temps);
    }
    // mode 1, MSAA off: nothing to emit (retail)
    {
        std::vector<uint8_t> out;
        CHECK(!FaaRewrite::Transform(m, FaaRewrite::OptionsFor(1, 0.25f, false), &out));
    }
    // OptionsFor
    CHECK(FaaRewrite::OptionsFor(2, 0.25f, true).fillFloor == 0.0f);
    CHECK(FaaRewrite::OptionsFor(2, 0.25f, true, 0.1f).fillFloor == 0.1f);
    CHECK(FaaRewrite::OptionsFor(1, 0.25f, true, 0.1f).fillFloor == 0.0f);   // no fill, no floor
    CHECK(FaaRewrite::OptionsFor(1, 0.25f, true).coverage && FaaRewrite::OptionsFor(1, 0.25f, true).fillK == 0.0f);
    CHECK(FaaRewrite::OptionsFor(2, 0.25f, true).fillK == 0.25f);
    CHECK(!FaaRewrite::OptionsFor(2, 0.25f, false).coverage);
    CHECK(!FaaRewrite::OptionsFor(0, 0.25f, true).coverage);
}

// The fill floor (FAA-TUNE): after the k mad, `ge M.x, rA.c, l(floor)` + `movc P.x, M.x, P.x, l(1)`
// (the factor is 1 where the sampled alpha is under the floor); then the floor gates the result:
// MSAA on `and P.x, P.x, M.x` right after the sharpen's add_sat, MSAA off `discard_z M.x` right
// after the retail discard_nz.
UNIT_TEST(ext_better_trees_cutout_emit_fill_floor) {
    for (int rt = 0; rt < 2; ++rt) {
        for (int msaa = 0; msaa < 2; ++msaa) {
            Shape s;
            s.seasonMad = rt == 0;
            s.separateRt = rt == 1;
            const FaaRewrite::Match m = Analyse(Blob(s));
            CHECK(m.colour);
            std::vector<uint8_t> plain, floored;
            CHECK(FaaRewrite::Transform(m, FaaRewrite::OptionsFor(2, 0.25f, msaa != 0), &plain));
            CHECK(FaaRewrite::Transform(m, FaaRewrite::OptionsFor(2, 0.25f, msaa != 0, 0.1f), &floored));
            const FaaRewrite::Match p = Analyse(plain), t = Analyse(floored);
            CHECK(t.program && t.identity && t.checksum);
            CHECK_EQ(CountOp(p, Op::Ge), 0);
            CHECK_EQ(CountOp(p, Op::MovC), 0);
            CHECK_EQ(CountOp(t, Op::Ge), 1);
            CHECK_EQ(CountOp(t, Op::MovC), 1);
            CHECK_EQ(TempsOf(t), TempsOf(p));              // no new temp: M is the lod scratch
            CHECK_EQ(t.prog.instrs.size(), p.prog.instrs.size() + 3);
            CHECK_EQ(t.discards, msaa ? 0 : 2);
            CHECK_EQ(CountOp(t, Op::And), msaa ? 1 : 0);
            // the two follow the k mad, in order, and read the alpha the retail add read (rA.c)
            int iMadK = -1;
            for (size_t i = 0; i < t.prog.instrs.size(); ++i) {
                const Dxbc::Instr& in = t.prog.instrs[i];
                if (in.opcode == Op::Mad && in.operands.size() == 4 && in.operands[2].type == OT::Imm32 &&
                    in.operands[2].ImmF(0) == 0.25f)
                    iMadK = static_cast<int>(i);
            }
            CHECK(iMadK >= 0);
            const Dxbc::Instr& ge = t.prog.instrs[static_cast<size_t>(iMadK) + 1];
            const Dxbc::Instr& mc = t.prog.instrs[static_cast<size_t>(iMadK) + 2];
            CHECK_EQ(ge.opcode, static_cast<uint32_t>(Op::Ge));
            CHECK(ge.operands[1].type == OT::Temp && ge.operands[1].index[0] == m.alpha.index[0] &&
                  ge.operands[1].Swz(0) == m.alphaComp);
            CHECK(ge.operands[2].type == OT::Imm32 && ge.operands[2].ImmF(0) == 0.1f);
            CHECK_EQ(mc.opcode, static_cast<uint32_t>(Op::MovC));
            CHECK(mc.operands[0].index[0] == s.temps && mc.operands[0].compBits == 1);
            CHECK(mc.operands[1].index[0] == ge.operands[0].index[0]);
            CHECK(mc.operands[3].type == OT::Imm32 && mc.operands[3].ImmF(0) == 1.0f);
            // the next instruction is the fill's final mul (rA.c * factor)
            CHECK_EQ(t.prog.instrs[static_cast<size_t>(iMadK) + 3].opcode, static_cast<uint32_t>(Op::Mul));
            const uint32_t mReg = ge.operands[0].index[0];
            CHECK(mReg != s.temps && mReg >= s.temps);   // ours, not P, not retail's
            for (size_t i = 0; i < t.prog.instrs.size(); ++i) {
                const Dxbc::Instr& in = t.prog.instrs[i];
                if (msaa && in.opcode == Op::And) {
                    // and P.x, P.x, M.x right after the sharpen's add_sat
                    const Dxbc::Instr& before = t.prog.instrs[i - 1];
                    CHECK(before.opcode == Op::Add && before.Saturate());
                    CHECK(in.operands[0].index[0] == s.temps && in.operands[1].index[0] == s.temps &&
                          in.operands[2].index[0] == mReg);
                }
                if (!msaa && in.opcode == Op::Discard && !in.TestNonZero()) {
                    // discard_z M.x right after the retail discard_nz
                    CHECK(in.operands[0].index[0] == mReg);
                    const Dxbc::Instr& before = t.prog.instrs[i - 1];
                    CHECK(before.opcode == Op::Discard && before.TestNonZero());
                }
            }
        }
    }
}

// ---- the registry row and key -----------------------------------------------------

UNIT_TEST(ext_better_trees_registry_row_and_mode_key) {
    const Ext::Entry* e = Ext::FindEntry("better_trees");
    CHECK(e != nullptr);
    CHECK(e->tier == Ext::Tier::Extension && e->cls == Ext::Class::A && e->defaultOn && e->formatVersion == 0);
    CHECK_STREQ(Ext::SettingsKey(*e).c_str(), "ext.better_trees");
    const Ext::ValueKey* k = Ext::FindValueKey("foliage_aa_mode");
    CHECK(k != nullptr);
    CHECK_STREQ(k->owner, "better_trees");
    CHECK_EQ(k->minValue, 1);
    CHECK_EQ(k->maxValue, 2);
    CHECK_EQ(k->defaultValue, 2);
    CHECK_STREQ(Ext::SettingsKey(*k).c_str(), "ext.foliage_aa_mode");
    bool ok = false;
    CHECK_EQ(Ext::ParseValue(*k, "1", ok), 1); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(*k, "2", ok), 2); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(*k, "0", ok), 1); CHECK(ok);    // clamped up
    CHECK_EQ(Ext::ParseValue(*k, "9", ok), 2); CHECK(ok);    // clamped down
    CHECK_EQ(Ext::ParseValue(*k, "smooth", ok), 2); CHECK(!ok);   // malformed -> the default
    const Ext::State boot;
    CHECK_EQ(boot.val.foliage_aa_mode, 2);
    CHECK(!boot.ext.better_trees);
}

}  // namespace faa
