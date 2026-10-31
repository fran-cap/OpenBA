// ============================================================================
// tests/unit/ext_natural_growth_test.cpp -- ext.better_trees' natural tree growth
// ============================================================================
// src/render/common/better_trees/natural_growth.{h,cpp} (docs/extensions/NATURAL_GROWTH.md) and its
// valued key ext.natural_tree_growth (src/ext/registry.cpp).
//
// NO RETAIL BYTES: every blob here is SYNTHETIC, assembled token by token in the SHAPE NATURAL_GROWTH.md
// §4 documents (the instance translation read cb3[rI + 3], the sway immediate, the move step, the
// season terms userPosition.y / .z, the gold fall-colour mad, the foliage snow interpolant), not any
// shader's content. A small interpreter runs the original and the rewritten programs and checks the
// rewrite's MEANING against the CPU mirror (NgRewrite::TermsOf / SeasonOf).
// ============================================================================
#include "unit.h"

#include "better_trees/dxbc.h"
#include "better_trees/natural_growth.h"
#include "ext/better_trees/leaf_fall.h"
#include "ext/registry.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace Op  = Dxbc::Op;
namespace OT  = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;
using Tokens  = std::vector<uint32_t>;

constexpr uint32_t kXYZW = Enc::Swizzle(0, 1, 2, 3);
constexpr uint32_t kIshl = 41, kDclInputPs = 98, kDclOutput = 101, kLinear = 2u << 11;

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
void Sel(Tokens& t, uint32_t type, uint32_t reg, uint32_t comp, bool neg = false) {
    t.push_back(Comp4(2, comp, type, 1) | (neg ? 0x80000000u : 0u));
    if (neg) t.push_back(1u | (1u << 6));
    t.push_back(reg);
}
void Cb(Tokens& t, uint32_t cb, uint32_t row, uint32_t swz = kXYZW) {
    t.push_back(Comp4(1, swz, OT::ConstantBuffer, 2));
    t.push_back(cb);
    t.push_back(row);
}
void CbSel(Tokens& t, uint32_t cb, uint32_t row, uint32_t comp, bool neg = false) {
    t.push_back(Comp4(2, comp, OT::ConstantBuffer, 2) | (neg ? 0x80000000u : 0u));
    if (neg) t.push_back(1u | (1u << 6));
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
    uint32_t extra = 0;
    explicit Ins(Tokens& tt, uint32_t ex = 0) : t(tt), at(tt.size()), extra(ex) { t.push_back(0); }
    void End(uint32_t op) { t[at] = Enc::Opcode(op, static_cast<uint32_t>(t.size() - at)) | extra; }
};

// A signature chunk: {name, semanticIndex, systemValue, register, mask, rw}.
struct Elem {
    const char* name;
    uint32_t    semIx, sysValue, reg;
    uint8_t     mask, rw;
};
std::vector<uint8_t> Sig(const std::vector<Elem>& es) {
    std::vector<uint8_t> d(8 + 24 * es.size(), 0);
    const uint32_t n = static_cast<uint32_t>(es.size()), eight = 8;
    std::memcpy(d.data(), &n, 4);
    std::memcpy(d.data() + 4, &eight, 4);
    std::vector<std::string> names;
    std::vector<uint32_t> offsets;
    for (const Elem& e : es) {
        uint32_t off = 0;
        for (size_t k = 0; k < names.size(); ++k)
            if (names[k] == e.name) off = offsets[k];
        if (off == 0) {
            off = static_cast<uint32_t>(d.size());
            names.push_back(e.name);
            offsets.push_back(off);
            d.insert(d.end(), e.name, e.name + std::strlen(e.name) + 1);
        }
        offsets.push_back(off);
        names.push_back(e.name);
    }
    for (size_t i = 0; i < es.size(); ++i) {
        uint8_t* p = d.data() + 8 + 24 * i;
        uint32_t off = 0;
        for (size_t k = 0; k < names.size(); ++k)
            if (names[k] == es[i].name) { off = offsets[k]; break; }
        const uint32_t ct = 3;
        std::memcpy(p + 0, &off, 4);
        std::memcpy(p + 4, &es[i].semIx, 4);
        std::memcpy(p + 8, &es[i].sysValue, 4);
        std::memcpy(p + 12, &ct, 4);
        std::memcpy(p + 16, &es[i].reg, 4);
        p[20] = es[i].mask;
        p[21] = es[i].rw;
    }
    return d;
}

std::vector<uint8_t> Blob(const Tokens& t, uint32_t version, const std::vector<uint8_t>& isgn,
                          const std::vector<uint8_t>& osgn) {
    Dxbc::Program p;
    p.version = version;
    p.tokens  = t;
    Dxbc::Container c;
    Dxbc::Chunk a, b, sh;
    a.fourcc = Dxbc::FourCC('I', 'S', 'G', 'N');
    a.data   = isgn;
    b.fourcc = Dxbc::FourCC('O', 'S', 'G', 'N');
    b.data   = osgn;
    sh.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    sh.data   = Dxbc::EmitProgram(p);
    c.chunks = {a, b, sh};
    return Dxbc::Emit(c);
}

// ---- the three vertex shapes -------------------------------------------------------------------
enum class VsShape { Colour, Depth, Foliage, NoSway, Flow, UserElsewhere };

std::vector<uint8_t> VertexBlob(VsShape s) {
    Tokens t;
    { Ins i(t); Cb(t, 0, 12); i.End(Op::DclConstantBuffer); }
    { Ins i(t); t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2)); t.push_back(3); t.push_back(208);
      i.End(Op::DclConstantBuffer); }
    for (uint32_t r = 0; r < 3; ++r) {
        Ins i(t);
        Dst(t, OT::Output, r, s == VsShape::Foliage && r == 2 ? 7 : 0xF);
        i.End(kDclOutput);
    }
    { Ins i(t); t.push_back(4); i.End(Op::DclTemps); }
    // ishl r0.w, v4.x, l(2)                         the instance's first row
    { Ins i(t); Dst(t, OT::Temp, 0, 8); Sel(t, OT::Input, 4, 0); Enc::ImmScalar(t, 2.0f); i.End(kIshl); }
    // mul r1.xyz, cb3[r0.w + 3].xyzx, l(1, 1, 1, 0)  the translation (the tree's origin)
    { Ins i(t); Dst(t, OT::Temp, 1, 7); CbRel(t, 3, 3, 0, 3); Enc::Imm4(t, 1.0f, 1.0f, 1.0f, 0.0f); i.End(Op::Mul); }
    if (s == VsShape::Flow) { Ins i(t); Sel(t, OT::Temp, 0, 0); i.End(31 /* if */); }
    // mul r2.xy, r1.xyxx, l(0.025, 0.025, 0, 0)       the sway
    { Ins i(t); Dst(t, OT::Temp, 2, 3); Swz(t, OT::Temp, 1, Enc::Swizzle(0, 1, 0, 0));
      Enc::Imm4(t, s == VsShape::NoSway ? 0.5f : 0.025f, 0.025f, 0.0f, 0.0f); i.End(Op::Mul); }
    if (s == VsShape::Flow) { Ins i(t); i.End(21 /* endif */); }
    // and r2.z, r2.x, l(1.0)                          the move step
    { Ins i(t); Dst(t, OT::Temp, 2, 4); Sel(t, OT::Temp, 2, 0); Enc::ImmScalar(t, 1.0f); i.End(Op::And); }
    { Ins i(t); Dst(t, OT::Output, 0, 0xF); Swz(t, OT::Temp, 1, Enc::Swizzle(0, 1, 2, 0)); i.End(Op::Mov); }
    { Ins i(t); Dst(t, OT::Output, 1, 3); Swz(t, OT::Temp, 2, kXYZW); i.End(Op::Mov); }
    switch (s) {
    case VsShape::Depth:   // mul o1.zw, r2.zzzz, cb0[9].zzzz  (fadeB * move -> the caster)
        { Ins i(t); Dst(t, OT::Output, 1, 0xC); Swz(t, OT::Temp, 2, Enc::Rep(2)); Cb(t, 0, 9, Enc::Rep(2)); i.End(Op::Mul); }
        break;
    case VsShape::Foliage:   // mov o2.xy, r1.xyxx; mul o2.z, r2.zzzz, cb0[9].xxxx  (the snow)
        { Ins i(t); Dst(t, OT::Output, 2, 3); Swz(t, OT::Temp, 1, kXYZW); i.End(Op::Mov); }
        { Ins i(t); Dst(t, OT::Output, 2, 4); Swz(t, OT::Temp, 2, Enc::Rep(2)); Cb(t, 0, 9, Enc::Rep(0)); i.End(Op::Mul); }
        break;
    case VsShape::UserElsewhere:
        { Ins i(t); Dst(t, OT::Output, 2, 0xF); Cb(t, 0, 9); i.End(Op::Mov); }
        break;
    default:
        { Ins i(t); Dst(t, OT::Output, 2, 0xF); Cb(t, 0, 4); i.End(Op::Mov); }
        break;
    }
    { Ins i(t); i.End(Op::Ret); }
    const std::vector<uint8_t> isgn = Sig({{"TEXCOORD", 0, 0, 0, 0xF, 0x7}, {"SV_InstanceID", 0, 8, 4, 0x1, 0x1}});
    const uint8_t m2 = s == VsShape::Foliage ? 0x7 : 0xF, rw2 = s == VsShape::Foliage ? 0x8 : 0x0;
    const std::vector<uint8_t> osgn = Sig({{"SV_Position", 0, 1, 0, 0xF, 0x0}, {"TEXCOORD", 1, 0, 1, 0xF, 0x0},
                                           {"TEXCOORD", 2, 0, 2, m2, rw2}});
    return Blob(t, 0x10040, isgn, osgn);
}

// ---- the two pixel shapes ----------------------------------------------------------------------
enum class PsShape { Colour, Foliage, NoGold };

void Resource(Tokens& t, uint32_t r) {
    t.push_back(Comp4(1, kXYZW, OT::Resource, 1));
    t.push_back(r);
}
void Sampler(Tokens& t, uint32_t s) {
    t.push_back((OT::Sampler << 12) | (1u << 20));
    t.push_back(s);
}

std::vector<uint8_t> PixelBlob(PsShape s) {
    const bool fol = s == PsShape::Foliage;
    Tokens t;
    { Ins i(t); Cb(t, 0, 5); i.End(Op::DclConstantBuffer); }
    { Ins i(t, kLinear); Dst(t, OT::Input, 1, 0xF); i.End(kDclInputPs); }
    { Ins i(t, kLinear); Dst(t, OT::Input, 2, fol ? 7 : 0xF); i.End(kDclInputPs); }
    { Ins i(t); Dst(t, OT::Output, 0, 0xF); i.End(kDclOutput); }
    { Ins i(t); t.push_back(3); i.End(Op::DclTemps); }
    // sample r0.xyzw, v1.xyzw, t0.xyzw, s0
    { Ins i(t); Dst(t, OT::Temp, 0, 0xF); Swz(t, OT::Input, 1, kXYZW); Resource(t, 0); Sampler(t, 0); i.End(Op::Sample); }
    if (!fol) {   // mad r0.w, -cb0[4].z, v2.w, r0.w   the season leaf loss
        Ins i(t); Dst(t, OT::Temp, 0, 8); CbSel(t, 0, 4, 2, true); Sel(t, OT::Input, 2, 3); Sel(t, OT::Temp, 0, 3);
        i.End(Op::Mad);
    }
    // add r2.x, r0.w, l(-0.5); lt r2.y, r2.x, l(0); discard_nz r2.y   (r2.x keeps the alpha for the checks)
    { Ins i(t); Dst(t, OT::Temp, 2, 1); Sel(t, OT::Temp, 0, 3); Enc::ImmScalar(t, -0.5f); i.End(Op::Add); }
    { Ins i(t); Dst(t, OT::Temp, 2, 2); Sel(t, OT::Temp, 2, 0); Enc::ImmScalar(t, 0.0f); i.End(Op::Lt); }
    { Ins i(t, 1u << 18); Sel(t, OT::Temp, 2, 1); i.End(Op::Discard); }
    if (fol) {
        // sample r1.xyzw, v2.xyzw, t1, s1; mul r1.w, r1.w, v2.z; mad r0.xyz, r1.wwww, r1.xyzx, r0.xyzx
        { Ins i(t); Dst(t, OT::Temp, 1, 0xF); Swz(t, OT::Input, 2, kXYZW); Resource(t, 1); Sampler(t, 1); i.End(Op::Sample); }
        { Ins i(t); Dst(t, OT::Temp, 1, 8); Sel(t, OT::Temp, 1, 3); Sel(t, OT::Input, 2, 2); i.End(Op::Mul); }
        { Ins i(t); Dst(t, OT::Temp, 0, 7); Swz(t, OT::Temp, 1, Enc::Rep(3)); Swz(t, OT::Temp, 1, kXYZW);
          Swz(t, OT::Temp, 0, kXYZW); i.End(Op::Mad); }
    } else {
        // dp3 r0.w, r0.xyzx, r0.xyzx; mad r1.xyz, r0.wwww, l(gold), -r0.xyzx; mul r0.w, v2.w, cb0[4].y;
        // mad r0.xyz, r0.wwww, r1.xyzx, r0.xyzx
        { Ins i(t); Dst(t, OT::Temp, 0, 8); Swz(t, OT::Temp, 0, kXYZW); Swz(t, OT::Temp, 0, kXYZW); i.End(16 /* dp3 */); }
        { Ins i(t); Dst(t, OT::Temp, 1, 7); Swz(t, OT::Temp, 0, Enc::Rep(3));
          if (s == PsShape::NoGold) Enc::Imm4(t, 0.5f, 0.5f, 0.5f, 0.0f);
          else Enc::Imm4(t, NgRewrite::kGold[0], NgRewrite::kGold[1], NgRewrite::kGold[2], 0.0f);
          t.push_back(Comp4(1, kXYZW, OT::Temp, 1) | 0x80000000u); t.push_back(1u | (1u << 6)); t.push_back(0);
          i.End(Op::Mad); }
        { Ins i(t); Dst(t, OT::Temp, 0, 8); Sel(t, OT::Input, 2, 3); CbSel(t, 0, 4, 1); i.End(Op::Mul); }
        { Ins i(t); Dst(t, OT::Temp, 0, 7); Swz(t, OT::Temp, 0, Enc::Rep(3)); Swz(t, OT::Temp, 1, kXYZW);
          Swz(t, OT::Temp, 0, kXYZW); i.End(Op::Mad); }
    }
    { Ins i(t); Dst(t, OT::Output, 0, 7); Swz(t, OT::Temp, 0, kXYZW); i.End(Op::Mov); }
    { Ins i(t); Dst(t, OT::Output, 0, 8); Enc::ImmScalar(t, 0.0f); i.End(Op::Mov); }
    { Ins i(t); i.End(Op::Ret); }
    const std::vector<uint8_t> isgn = Sig({{"SV_Position", 0, 1, 0, 0xF, 0x0}, {"TEXCOORD", 1, 0, 1, 0xF, 0xF},
                                           {"TEXCOORD", 2, 0, 2, uint8_t(fol ? 0x7 : 0xF), uint8_t(fol ? 0x7 : 0xF)}});
    const std::vector<uint8_t> osgn = Sig({{"SV_Target", 0, 0, 0, 0xF, 0x0}});
    return Blob(t, 0x40, isgn, osgn);
}

NgRewrite::Match Analyse(const std::vector<uint8_t>& b) {
    NgRewrite::Match m;
    NgRewrite::Analyse(b.data(), b.size(), &m);
    return m;
}

// ---- a small SM4 interpreter ---------------------------------------------------------------------
struct Vm {
    float r[16][4] = {};
    float v[8][4]  = {};
    float o[8][4]  = {};
    float cb[4][32][4] = {};
};

bool Fetch(const Vm& vm, const Dxbc::Program& p, const Dxbc::Operand& o, float out[4]) {
    float raw[4] = {0, 0, 0, 0};
    switch (o.type) {
    case OT::Temp: std::memcpy(raw, vm.r[o.index[0]], sizeof raw); break;
    case OT::Input: std::memcpy(raw, vm.v[o.index[0]], sizeof raw); break;
    case OT::ConstantBuffer: {
        uint32_t row = o.index[1];
        if (o.relative) {
            const uint32_t* t = p.tokens.data() + o.offset;
            row += static_cast<uint32_t>(vm.r[t[4]][(t[3] >> 4) & 3]);
        }
        std::memcpy(raw, vm.cb[o.index[0]][row], sizeof raw);
        break;
    }
    case OT::Imm32:
        for (int c = 0; c < 4; ++c) std::memcpy(&raw[c], &o.imm[o.numComp == 1 ? 0 : c], 4);
        break;
    case OT::Resource: case OT::Sampler: case 13 /* null */: break;
    default: return false;
    }
    for (int c = 0; c < 4; ++c) out[c] = raw[o.numComp == 1 ? 0 : o.Swz(c)];
    if (o.modifier & 1) for (int c = 0; c < 4; ++c) out[c] = -out[c];
    return true;
}

float Sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

bool Run(Vm& vm, const std::vector<uint8_t>& blob) {
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &c, &err)) return false;
    Dxbc::Program p;
    if (!Dxbc::ParseProgram(*c.Find(Dxbc::FourCC('S', 'H', 'D', 'R')), &p, &err)) return false;
    for (const Dxbc::Instr& in : p.instrs) {
        if (in.isDecl) continue;
        if (in.opcode == Op::Ret) break;
        if (in.opcode == Op::Discard) continue;   // the meaning checks read the alpha before the cut
        float a[4] = {}, b[4] = {}, d[4] = {}, res[4] = {};
        const size_t ns = in.operands.size() - 1;
        if (ns >= 1 && !Fetch(vm, p, in.operands[1], a)) return false;
        if (ns >= 2 && !Fetch(vm, p, in.operands[2], b)) return false;
        if (ns >= 3 && !Fetch(vm, p, in.operands[3], d)) return false;
        for (int k = 0; k < 4; ++k) {
            uint32_t x, y;
            switch (in.opcode) {
            case Op::Mov: res[k] = a[k]; break;
            case Op::Add: res[k] = a[k] + b[k]; break;
            case Op::Mul: res[k] = a[k] * b[k]; break;
            case Op::Mad: res[k] = a[k] * b[k] + d[k]; break;
            case Op::Max: res[k] = a[k] > b[k] ? a[k] : b[k]; break;
            case Op::Dp2: res[k] = a[0] * b[0] + a[1] * b[1]; break;
            case 16 /* dp3 */: res[k] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; break;
            case 26 /* frc */: res[k] = a[k] - std::floor(a[k]); break;
            case 77 /* sincos: dst0 = sin */: res[k] = std::sin(b[k]); break;
            case kIshl: res[k] = a[k] * 4.0f; break;   // the instance id is 0 here: index 0
            case Op::Ge: x = a[k] >= b[k] ? 0xFFFFFFFFu : 0u; std::memcpy(&res[k], &x, 4); break;
            case Op::Lt: x = a[k] < b[k] ? 0xFFFFFFFFu : 0u; std::memcpy(&res[k], &x, 4); break;
            case Op::MovC: std::memcpy(&x, &a[k], 4); res[k] = x != 0 ? b[k] : d[k]; break;
            case Op::And: std::memcpy(&x, &a[k], 4); std::memcpy(&y, &b[k], 4); x &= y; std::memcpy(&res[k], &x, 4); break;
            case Op::Sample: res[k] = a[k]; break;   // the stand-in: a sample yields its coordinates
            default: return false;
            }
            if (in.Saturate()) res[k] = Sat(res[k]);
        }
        const Dxbc::Operand& o = in.operands[0];
        float* dst = o.type == OT::Temp ? vm.r[o.index[0]] : (o.type == OT::Output ? vm.o[o.index[0]] : nullptr);
        if (dst == nullptr) return false;
        for (int k = 0; k < 4; ++k)
            if (o.compBits & (1u << k)) dst[k] = res[k];
    }
    return true;
}

bool Near(float a, float b, float tol = 2e-3f) { return std::fabs(a - b) <= tol; }

// A signature element by register: {mask, rw, semIndex}, or mask 0 when absent.
struct SigOut { uint8_t mask = 0, rw = 0; uint32_t semIx = 0; std::string name; };
SigOut SigAt(const std::vector<uint8_t>& blob, uint32_t fourcc, uint32_t reg) {
    SigOut r;
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &c, &err)) return r;
    const Dxbc::Chunk* ch = c.Find(fourcc);
    if (ch == nullptr) return r;
    uint32_t n;
    std::memcpy(&n, ch->data.data(), 4);
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* e = ch->data.data() + 8 + 24 * i;
        uint32_t er, no;
        std::memcpy(&er, e + 16, 4);
        if (er != reg) continue;
        std::memcpy(&no, e, 4);
        std::memcpy(&r.semIx, e + 4, 4);
        r.mask = e[20];
        r.rw = e[21];
        r.name = reinterpret_cast<const char*>(ch->data.data() + no);  // CAST-OK: byte_io
    }
    return r;
}

void SeedVertex(Vm& vm, float x, float y, float stamp, float fadeA, float fadeB) {
    vm.cb[3][3][0] = x;
    vm.cb[3][3][1] = y;
    vm.cb[3][3][2] = 7.0f;
    vm.cb[0][10][0] = 123.0f;
    vm.cb[0][10][1] = stamp;
    vm.cb[0][9][0] = 0.25f;
    vm.cb[0][9][1] = fadeA;
    vm.cb[0][9][2] = fadeB;
    for (int c = 0; c < 4; ++c) vm.cb[0][4][c] = 0.1f * static_cast<float>(c + 1);
}

}  // namespace

UNIT_TEST(ext_natural_growth_vertex_colour_match_and_meaning) {
    const std::vector<uint8_t> blob = VertexBlob(VsShape::Colour);
    const NgRewrite::Match m = Analyse(blob);
    CHECK(m.kind == NgRewrite::Kind::BillboardColour);
    CHECK_EQ(m.newReg, 3u);
    CHECK_EQ(m.newSemIx, 3u);
    std::vector<uint8_t> xf;
    CHECK(NgRewrite::Transform(m, &xf));
    // the new interpolant: TEXCOORD3 at o3, every component written
    const SigOut e = SigAt(xf, Dxbc::FourCC('O', 'S', 'G', 'N'), 3);
    CHECK(e.mask == 0xF && e.rw == 0x0 && e.semIx == 3 && e.name == "TEXCOORD");
    CHECK(SigAt(xf, Dxbc::FourCC('O', 'S', 'G', 'N'), 0).name == "SV_Position");   // the names moved with the table
    const float trees[3][2] = {{12.5f, 340.25f}, {501.0f, 77.0f}, {-3.0f, 905.5f}};
    const float months[4] = {1.0f, 6.4f, 7.7f, 10.5f};
    for (const auto& tr : trees) {
        for (float mo : months) {
            Vm a, b;
            SeedVertex(a, tr[0], tr[1], NgRewrite::kStampBase + mo, 0.3f, 0.6f);
            b = a;
            CHECK(Run(a, blob));
            CHECK(Run(b, xf));
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c) CHECK(a.o[r][c] == b.o[r][c]);   // retail's outputs untouched
            const NgRewrite::TreeTerms terms = NgRewrite::TermsOf(tr[0], tr[1]);
            float colour, loss;
            NgRewrite::SeasonOf(terms, mo, &colour, &loss);
            CHECK(Near(b.o[3][0], colour));
            CHECK(Near(b.o[3][1], loss));
            CHECK(Near(b.o[3][2], terms.h[1]));
            CHECK(Near(b.o[3][3], terms.tint));
        }
    }
    // no stamp (time.y 0): retail's fadeA / fadeB and gold (hue 0); the tint stays
    Vm n;
    SeedVertex(n, 40.0f, 60.0f, 0.0f, 0.3f, 0.6f);
    CHECK(Run(n, xf));
    CHECK(Near(n.o[3][0], 0.3f) && Near(n.o[3][1], 0.6f) && n.o[3][2] == 0.0f);
    CHECK(Near(n.o[3][3], NgRewrite::TermsOf(40.0f, 60.0f).tint));
}

UNIT_TEST(ext_natural_growth_vertex_depth_and_foliage) {
    {
        const std::vector<uint8_t> blob = VertexBlob(VsShape::Depth);
        const NgRewrite::Match m = Analyse(blob);
        CHECK(m.kind == NgRewrite::Kind::BillboardDepth);
        std::vector<uint8_t> xf;
        CHECK(NgRewrite::Transform(m, &xf));
        CHECK(SigAt(xf, Dxbc::FourCC('O', 'S', 'G', 'N'), 3).mask == 0);   // no signature change
        Vm a;
        SeedVertex(a, 210.0f, 33.0f, NgRewrite::kStampBase + 7.6f, 0.3f, 0.6f);
        a.v[4][0] = 0.0f;
        Vm b = a;
        CHECK(Run(a, blob));
        CHECK(Run(b, xf));
        float colour, loss;
        NgRewrite::SeasonOf(NgRewrite::TermsOf(210.0f, 33.0f), 7.6f, &colour, &loss);
        const float move = a.r[2][2];   // the step (1.0 bits & r2.x)
        CHECK(Near(a.o[1][2], move * 0.6f));   // retail: fadeB * move
        CHECK(Near(b.o[1][2], move * loss));   // the tree's own loss
        CHECK(a.o[1][0] == b.o[1][0] && a.o[1][1] == b.o[1][1]);
    }
    {
        const std::vector<uint8_t> blob = VertexBlob(VsShape::Foliage);
        const NgRewrite::Match m = Analyse(blob);
        CHECK(m.kind == NgRewrite::Kind::Foliage);
        CHECK_EQ(m.snowReg, 2u);
        std::vector<uint8_t> xf;
        CHECK(NgRewrite::Transform(m, &xf));
        const SigOut e = SigAt(xf, Dxbc::FourCC('O', 'S', 'G', 'N'), 2);
        CHECK(e.mask == 0xF && e.rw == 0x0);   // w now written
        Vm a;
        SeedVertex(a, 88.0f, 12.0f, NgRewrite::kStampBase + 3.0f, 0.0f, 0.0f);
        Vm b = a;
        CHECK(Run(a, blob));
        CHECK(Run(b, xf));
        for (int c = 0; c < 3; ++c) CHECK(a.o[2][c] == b.o[2][c]);
        CHECK(Near(b.o[2][3], NgRewrite::TermsOf(88.0f, 12.0f).tint));
    }
}

UNIT_TEST(ext_natural_growth_vertex_refusals) {
    CHECK(Analyse(VertexBlob(VsShape::NoSway)).kind == NgRewrite::Kind::None);
    CHECK(Analyse(VertexBlob(VsShape::Flow)).kind == NgRewrite::Kind::None);
    CHECK(Analyse(VertexBlob(VsShape::UserElsewhere)).kind == NgRewrite::Kind::None);
    std::vector<uint8_t> junk(64, 0x11);
    CHECK(Analyse(junk).kind == NgRewrite::Kind::None);
    std::vector<uint8_t> out;
    CHECK(!NgRewrite::Transform(Analyse(junk), &out));
}

UNIT_TEST(ext_natural_growth_pixel_colour_match_and_meaning) {
    const std::vector<uint8_t> blob = PixelBlob(PsShape::Colour);
    const NgRewrite::Match m = Analyse(blob);
    CHECK(m.kind == NgRewrite::Kind::BillboardColour);
    CHECK_EQ(m.newReg, 3u);
    CHECK_EQ(m.moveReg, 2u);
    std::vector<uint8_t> xf;
    CHECK(NgRewrite::Transform(m, &xf));
    const SigOut e = SigAt(xf, Dxbc::FourCC('I', 'S', 'G', 'N'), 3);
    CHECK(e.mask == 0xF && e.rw == 0xF && e.semIx == 3 && e.name == "TEXCOORD");
    const float leaf[4] = {0.20f, 0.35f, 0.10f, 0.9f};
    // (c, l, hue 0, t 0) = (fadeA, fadeB 0, gold, no tint): retail's colour exactly (no browning at l 0)
    for (float fadeA : {0.0f, 0.4f, 1.0f}) {
        Vm a, b;
        std::memcpy(a.v[1], leaf, sizeof leaf);
        a.v[2][3] = 1.0f;   // move
        a.cb[0][4][1] = fadeA;
        a.cb[0][4][2] = 0.0f;
        b = a;
        b.v[3][0] = fadeA;
        CHECK(Run(a, blob));
        CHECK(Run(b, xf));
        for (int c = 0; c < 3; ++c) CHECK(Near(a.o[0][c], b.o[0][c], 1e-5f));
        CHECK(a.r[2][0] == b.r[2][0]);   // the alpha before the cut
    }
    // the season loss is read from the interpolant, not userPosition.z: alpha - l * move before the cut
    {
        Vm a, b;
        std::memcpy(a.v[1], leaf, sizeof leaf);
        a.v[2][3] = 1.0f;
        a.cb[0][4][2] = 0.35f;
        b = a;
        b.cb[0][4][2] = 0.9f;   // ignored by the copy
        b.v[3][1] = 0.35f;
        CHECK(Run(a, blob));
        CHECK(Run(b, xf));
        CHECK(Near(a.r[2][0], leaf[3] - 0.35f - 0.5f, 1e-6f));
        CHECK(Near(b.r[2][0], a.r[2][0], 1e-6f));
    }
    // full colour at hue 1 lands on the red end; the trunk (move 0) stays retail whatever the terms
    {
        Vm b, t;
        std::memcpy(b.v[1], leaf, sizeof leaf);
        b.v[2][3] = 1.0f;
        b.v[3][0] = 1.0f;
        b.v[3][2] = 1.0f;
        CHECK(Run(b, xf));
        const float d = leaf[0] * leaf[0] + leaf[1] * leaf[1] + leaf[2] * leaf[2];
        for (int c = 0; c < 3; ++c) CHECK(Near(b.o[0][c], d * NgRewrite::kRed[c], 1e-4f));
        std::memcpy(t.v[1], leaf, sizeof leaf);
        t.v[2][3] = 0.0f;
        t.v[3][0] = 1.0f;
        t.v[3][1] = 1.0f;
        t.v[3][3] = 1.0f;
        CHECK(Run(t, xf));
        for (int c = 0; c < 3; ++c) CHECK(Near(t.o[0][c], leaf[c], 1e-6f));
    }
    // the green variation: t = 1 on a leaf (move 1, no autumn) multiplies by 1 + kTint
    {
        Vm b;
        std::memcpy(b.v[1], leaf, sizeof leaf);
        b.v[2][3] = 1.0f;
        b.v[3][3] = 1.0f;
        CHECK(Run(b, xf));
        for (int c = 0; c < 3; ++c) CHECK(Near(b.o[0][c], leaf[c] * (1.0f + NgRewrite::kTint[c]), 1e-5f));
    }
}

UNIT_TEST(ext_natural_growth_pixel_foliage_and_refusals) {
    const std::vector<uint8_t> blob = PixelBlob(PsShape::Foliage);
    const NgRewrite::Match m = Analyse(blob);
    CHECK(m.kind == NgRewrite::Kind::Foliage);
    CHECK_EQ(m.snowReg, 2u);
    std::vector<uint8_t> xf;
    CHECK(NgRewrite::Transform(m, &xf));
    const SigOut e = SigAt(xf, Dxbc::FourCC('I', 'S', 'G', 'N'), 2);
    CHECK(e.mask == 0xF && (e.rw & 0x8) != 0);
    const float needle[4] = {0.10f, 0.30f, 0.12f, 1.0f};
    Vm a, b;
    std::memcpy(a.v[1], needle, sizeof needle);
    a.v[2][0] = 0.0f;   // the snow sample's coordinates (the stand-in returns them): no snow colour
    a.v[2][2] = 0.0f;   // no snow amount
    b = a;
    b.v[2][3] = -1.0f;  // t
    CHECK(Run(a, blob));
    CHECK(Run(b, xf));
    for (int c = 0; c < 3; ++c) CHECK(Near(b.o[0][c], a.o[0][c] * (1.0f - NgRewrite::kPineTint[c]), 1e-5f));
    CHECK(Analyse(PixelBlob(PsShape::NoGold)).kind == NgRewrite::Kind::None);
}

UNIT_TEST(ext_natural_growth_curves) {
    // a spread of trees: summer is in leaf and green, mid winter bare, Mid Fall varied
    int bareWinter = 0, greenSummer = 0, turningFall = 0, colouredFall = 0;
    for (int i = 0; i < 400; ++i) {
        const NgRewrite::TreeTerms t = NgRewrite::TermsOf(17.0f * static_cast<float>(i % 23), 31.0f * static_cast<float>(i / 23));
        CHECK(t.h[0] >= 0.0f && t.h[0] < 1.0f && t.h[1] >= 0.0f && t.h[1] < 1.0f && t.h[2] >= 0.0f && t.h[2] < 1.0f);
        CHECK(t.tint >= -1.0f && t.tint <= 1.0f && t.grove >= -2.0f && t.grove <= 2.0f);
        float c, l;
        NgRewrite::SeasonOf(t, 10.0f, &c, &l);
        if (l == 1.0f) ++bareWinter;
        NgRewrite::SeasonOf(t, 4.0f, &c, &l);
        if (c == 0.0f && l == 0.0f) ++greenSummer;
        NgRewrite::SeasonOf(t, 6.6f, &c, &l);
        if (c > 0.0f && c < 1.0f) ++turningFall;
        if (c == 1.0f) ++colouredFall;
    }
    CHECK_EQ(bareWinter, 400);
    CHECK_EQ(greenSummer, 400);
    CHECK(turningFall > 100);   // staggered: many trees part-way at once
    CHECK(colouredFall > 0 || turningFall == 400);
    // the year's wrap: a bare tree at the end of Late Winter is still bare at the start of Early Spring
    float c0, l0, c1, l1;
    const NgRewrite::TreeTerms t = NgRewrite::TermsOf(100.0f, 200.0f);
    NgRewrite::SeasonOf(t, 11.99f, &c0, &l0);
    NgRewrite::SeasonOf(t, 0.0f, &c1, &l1);
    CHECK(l0 == 1.0f && l1 == 1.0f);
}

UNIT_TEST(ext_natural_growth_registry_key) {
    const Ext::ValueKey* k = Ext::FindValueKey("natural_tree_growth");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK_STREQ(k->owner, "better_trees");
    CHECK_EQ(k->minValue, 0);
    CHECK_EQ(k->maxValue, 1);
    CHECK_EQ(k->defaultValue, 1);
    CHECK_STREQ(Ext::SettingsKey(*k).c_str(), "ext.natural_tree_growth");
    const Ext::State boot;
    CHECK_EQ(boot.val.natural_tree_growth, 1);
}

// ---- the falling leaves' pure part (src/ext/better_trees/leaf_fall.{h,cpp}, NATURAL_GROWTH.md section 7) ----
namespace LF = Ext::BetterTrees::LeafFall;

UNIT_TEST(ext_natural_growth_leaf_atlas) {
    const std::vector<uint8_t> px = LF::PaintAtlas();
    CHECK_EQ(px.size(), static_cast<size_t>(LF::kAtlasSize * LF::kAtlasSize * 4));
    for (int row = 0; row < LF::kColours; ++row) {
        for (int col = 0; col < LF::kShapes; ++col) {
            int opaque = 0;
            int edge = 0;
            for (int y = 0; y < LF::kCell; ++y) {
                for (int x = 0; x < LF::kCell; ++x) {
                    const uint8_t a = px[(static_cast<size_t>(row * LF::kCell + y) * LF::kAtlasSize + col * LF::kCell + x) * 4 + 3];
                    if (a > 128) ++opaque;
                    // the outer two texels stay clear: the 1/255 corner steps and bilinear filtering stay in the cell
                    if ((x < 2 || y < 2 || x >= LF::kCell - 2 || y >= LF::kCell - 2) && a != 0) ++edge;
                }
            }
            CHECK(opaque > LF::kCell * LF::kCell / 8);   // a leaf, not a speck
            CHECK(opaque < LF::kCell * LF::kCell * 3 / 4);   // and not a square
            CHECK_EQ(edge, 0);
        }
    }
}

UNIT_TEST(ext_natural_growth_leaf_timing) {
    // the autumn half of the canopy's own loss (SeasonOf): the same tree is bare in both at the same day
    for (int i = 0; i < 200; ++i) {
        const float x = 13.0f * static_cast<float>(i % 17);
        const float y = 29.0f * static_cast<float>(i / 17);
        float colour, loss;
        for (float m : {6.0f, 7.0f, 7.6f, 8.2f, 9.0f}) {
            NgRewrite::SeasonOf(NgRewrite::TermsOf(x, y), m, &colour, &loss);
            CHECK(Near(LF::AutumnLoss(x, y, m), loss, 1e-6f));
        }
        CHECK_EQ(LF::AutumnLoss(x, y, 3.0f), 0.0f);   // summer: in leaf
        CHECK_EQ(LF::AutumnLoss(x, y, 10.0f), 1.0f);  // winter: bare
        const int c = LF::ColourOf(x, y);
        CHECK(c >= 0 && c <= 2);
    }
    CHECK_EQ(LF::DropRate(0.0f), 0.0f);
    CHECK_EQ(LF::DropRate(1.0f), 0.0f);
    CHECK(LF::DropRate(0.5f) > LF::DropRate(0.1f));
}

UNIT_TEST(ext_natural_growth_leaf_field) {
    // one tree, mid fall at its own timing
    LF::Tree t{50.0f, 80.0f, 2.0f, {48.0f, 78.0f, 2.0f, 52.0f, 82.0f, 10.0f}};
    float m = 6.0f;
    while (m < 9.5f && (LF::AutumnLoss(t.x, t.y, m) < 0.4f)) m += 0.01f;
    CHECK(LF::AutumnLoss(t.x, t.y, m) > 0.0f && LF::AutumnLoss(t.x, t.y, m) < 1.0f);
    const std::vector<LF::Tree> trees{t};
    LF::Field f;
    for (int i = 0; i < 2000; ++i) f.Step(0.05f, m, trees);   // 100 s
    CHECK(!f.Leaves().empty());
    for (const LF::Leaf& l : f.Leaves()) {
        CHECK(l.pos[2] > t.baseZ && l.pos[2] <= t.crown[5]);
        CHECK(l.shape < LF::kShapes && l.colour < LF::kColours);
    }
    // paused: nothing moves, nothing drops
    const std::vector<LF::Leaf> before = f.Leaves();
    f.Step(0.0f, m, trees);
    CHECK_EQ(f.Leaves().size(), before.size());
    CHECK(f.Leaves().empty() || f.Leaves()[0].pos[2] == before[0].pos[2]);
    // summer and winter: none drop, the ones in the air land
    LF::Field s;
    for (int i = 0; i < 400; ++i) s.Step(0.05f, 3.0f, trees);
    CHECK(s.Leaves().empty());
    for (int i = 0; i < 2000; ++i) f.Step(0.05f, 11.0f, trees);
    CHECK(f.Leaves().empty());
}

UNIT_TEST(ext_natural_growth_leaf_quads) {
    LF::Leaf l{};
    l.pos[0] = 1.0f;
    l.pos[1] = 2.0f;
    l.pos[2] = 3.0f;
    l.shape = 2;
    l.colour = 1;
    const std::vector<LF::Leaf> leaves{l};
    const float right[3] = {1.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, 0.0f, 1.0f};
    std::vector<LF::Vertex> v;
    LF::BuildQuads(leaves, right, up, v);
    CHECK_EQ(v.size(), static_cast<size_t>(4));
    float cx = 0.0f, cz = 0.0f;
    for (const LF::Vertex& q : v) {
        cx += q.position[0] / 4.0f;
        cz += q.position[2] / 4.0f;
        CHECK_EQ(q.position[3], 0.0f);
        const uint32_t u = q.corner & 0xFF;
        const uint32_t w = (q.corner >> 8) & 0xFF;
        CHECK(u == 128u || u == 191u);   // shape 2's column
        CHECK(w == 64u || w == 127u);    // colour 1's row
        CHECK_EQ(q.corner >> 16, 0u);
    }
    CHECK(Near(cx, 1.0f, 1e-5f) && Near(cz, 3.0f, 1e-5f));   // centred on the leaf
}
