// ============================================================================
// tests/unit/ext_decal_fog_test.cpp -- fix.decal_fog_order (docs/retail_bugs.md section 32)
// ============================================================================
// src/render/common/decal_fog/decal_fog_rewrite.{h,cpp} on synthetic ps_4_0 blobs built here (no
// retail bytes): the shape of a decal program's tail (light, fog lerp, underwater tint, colour output)
// in two register layouts, run through a small float evaluator before and after the rewrite:
//   * the copy computes lerp(lerp(L, tint, u), fog, f) where the original computed
//     lerp(lerp(L, fog, f), tint, u), and equals the original's at u = 0 and at f = 0;
//   * the copy parses, its checksum verifies, it has two instructions more and two temps more;
//   * refusals: the tint already before the fog, a later read of the fogged colour, a lerp that is
//     not the fog, flow control, two tints, a different tint, a vertex program.
// ============================================================================
#include "unit.h"
#include "decal_fog/decal_fog_rewrite.h"
#include "ext/registry.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace Op  = Dxbc::Op;
namespace OT  = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;
using Tokens  = std::vector<uint32_t>;

constexpr uint32_t kXYZW = Enc::Swizzle(0, 1, 2, 3);
constexpr uint32_t kXYZX = Enc::Swizzle(0, 1, 2, 0);
constexpr uint32_t kLoop = 48;
constexpr uint32_t kBreak = 2;
constexpr uint32_t kEndLoop = 22;

uint32_t Comp4(uint32_t sel, uint32_t bits, uint32_t type, uint32_t dims) {
    return 2u | (sel << 2) | (bits << 4) | (type << 12) | (dims << 20);
}
void InputSrc(Tokens& t, uint32_t reg, uint32_t swz) {
    t.push_back(Comp4(1, swz, OT::Input, 1));
    t.push_back(reg);
}
void CbSrc(Tokens& t, uint32_t row, uint32_t swz) {
    t.push_back(Comp4(1, swz, OT::ConstantBuffer, 2));
    t.push_back(0);
    t.push_back(row);
}
void TintImm(Tokens& t, float r, float g, float b) { Enc::Imm4(t, r, g, b, 0.0f); }

struct Ins {
    Tokens& t;
    size_t  at;
    bool    sat;
    explicit Ins(Tokens& tt, bool s = false) : t(tt), at(tt.size()), sat(s) { t.push_back(0); }
    void End(uint32_t op) { t[at] = Enc::Opcode(op, static_cast<uint32_t>(t.size() - at), sat); }
};

struct Shape {
    bool  swizzled  = false;   // f in r0.x and the difference in r0.yzw (C overwrites f)
    bool  tintFirst = false;   // the tint lerp writes a temp, a later mov writes the output
    bool  laterRead = false;   // o0.w reads the fogged colour's x after the tint
    bool  notFog    = false;   // the lerp amount is a plain mov_sat, not min_sat(*, fog.w)
    bool  flow      = false;   // a loop at the start
    bool  twoTints  = false;   // the tint immediate twice
    float tintR     = 0.025f;
    uint32_t type   = 0;       // program type (0 pixel, 1 vertex)
};

// v1.z = the underwater amount, v2.z = the fog amount, v3 x v4 = the lit colour L, cb0[1] = the fog colour.
Tokens Program(const Shape& s) {
    Tokens t;
    {
        Ins i(t);
        t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2));
        t.push_back(0);
        t.push_back(4);
        i.End(Op::DclConstantBuffer);
    }
    {
        Ins i(t);
        t.push_back(3);
        i.End(Op::DclTemps);
    }
    if (s.flow) {
        Ins a(t);
        a.End(kLoop);
        Ins b(t);
        b.End(kBreak);
        Ins c(t);
        c.End(kEndLoop);
    }
    const uint32_t fReg = 0;
    const uint32_t fComp = s.swizzled ? 0 : 3;
    auto fogAmount = [&]() {
        Ins i(t, true);
        Enc::TempDst(t, fReg, 1u << fComp);
        InputSrc(t, 2, Enc::Rep(2));
        if (!s.notFog) {
            CbSrc(t, 1, Enc::Rep(3));
            i.End(Op::Min);
        } else {
            i.End(Op::Mov);
        }
    };
    if (s.swizzled) {
        fogAmount();
        {
            Ins i(t);   // mov r0.yzw, v3.xxyz (X)
            Enc::TempDst(t, 0, 0xE);
            InputSrc(t, 3, Enc::Swizzle(0, 0, 1, 2));
            i.End(Op::Mov);
        }
        {
            Ins i(t);   // mov r1.xyz, v4.xyzx (Y)
            Enc::TempDst(t, 1, 0x7);
            InputSrc(t, 4, kXYZX);
            i.End(Op::Mov);
        }
        {
            Ins i(t);   // mul r2.xyz, r0.yzwy, r1.xyzx (L)
            Enc::TempDst(t, 2, 0x7);
            Enc::TempSrc(t, 0, Enc::Swizzle(1, 2, 3, 1));
            Enc::TempSrc(t, 1, kXYZX);
            i.End(Op::Mul);
        }
        {
            Ins i(t);   // mad r0.yzw, -r0.yyzw, r1.xxyz, cb0[1].xxyz (D = fog - L)
            Enc::TempDst(t, 0, 0xE);
            Enc::TempSrc(t, 0, Enc::Swizzle(1, 1, 2, 3), 1);
            Enc::TempSrc(t, 1, Enc::Swizzle(0, 0, 1, 2));
            CbSrc(t, 1, Enc::Swizzle(0, 0, 1, 2));
            i.End(Op::Mad);
        }
        {
            Ins i(t);   // mad r0.xyz, r0.xxxx, r0.yzwy, r2.xyzx (F)
            Enc::TempDst(t, 0, 0x7);
            Enc::TempSrc(t, 0, Enc::Rep(0));
            Enc::TempSrc(t, 0, Enc::Swizzle(1, 2, 3, 1));
            Enc::TempSrc(t, 2, kXYZX);
            i.End(Op::Mad);
        }
    } else {
        {
            Ins i(t);   // mov r0.xyz, v3.xyzx (X)
            Enc::TempDst(t, 0, 0x7);
            InputSrc(t, 3, kXYZX);
            i.End(Op::Mov);
        }
        {
            Ins i(t);   // mov r1.xyz, v4.xyzx (Y)
            Enc::TempDst(t, 1, 0x7);
            InputSrc(t, 4, kXYZX);
            i.End(Op::Mov);
        }
        {
            Ins i(t);   // mul r2.xyz, r0.xyzx, r1.xyzx (L)
            Enc::TempDst(t, 2, 0x7);
            Enc::TempSrc(t, 0, kXYZX);
            Enc::TempSrc(t, 1, kXYZX);
            i.End(Op::Mul);
        }
        {
            Ins i(t);   // mad r0.xyz, -r0.xyzx, r1.xyzx, cb0[1].xyzx (D)
            Enc::TempDst(t, 0, 0x7);
            Enc::TempSrc(t, 0, kXYZX, 1);
            Enc::TempSrc(t, 1, kXYZX);
            CbSrc(t, 1, kXYZX);
            i.End(Op::Mad);
        }
        fogAmount();
        {
            Ins i(t);   // mad r0.xyz, r0.wwww, r0.xyzx, r2.xyzx (F)
            Enc::TempDst(t, 0, 0x7);
            Enc::TempSrc(t, 0, Enc::Rep(3));
            Enc::TempSrc(t, 0, kXYZX);
            Enc::TempSrc(t, 2, kXYZX);
            i.End(Op::Mad);
        }
    }
    {
        Ins i(t);   // add r1.xyz, -r0.xyzx, l(tint)
        Enc::TempDst(t, 1, 0x7);
        Enc::TempSrc(t, 0, kXYZX, 1);
        TintImm(t, s.tintR, 0.123f, 0.19f);
        i.End(Op::Add);
    }
    {
        Ins i(t, true);   // mov_sat r0.w, v1.z
        Enc::TempDst(t, 0, 0x8);
        InputSrc(t, 1, Enc::Rep(2));
        i.End(Op::Mov);
    }
    {
        Ins i(t);   // mad o0.xyz | r0.xyz, r0.wwww, r1.xyzx, r0.xyzx
        if (s.tintFirst) {
            Enc::TempDst(t, 0, 0x7);
        } else {
            Enc::OutputDst(t, 0, 0x7);
        }
        Enc::TempSrc(t, 0, Enc::Rep(3));
        Enc::TempSrc(t, 1, kXYZX);
        Enc::TempSrc(t, 0, kXYZX);
        i.End(Op::Mad);
    }
    if (s.tintFirst) {
        Ins i(t);   // mov o0.xyz, r0.xyzx
        Enc::OutputDst(t, 0, 0x7);
        Enc::TempSrc(t, 0, kXYZX);
        i.End(Op::Mov);
    }
    if (s.twoTints) {
        Ins i(t);   // add r2.xyz, -r2.xyzx, l(tint)
        Enc::TempDst(t, 2, 0x7);
        Enc::TempSrc(t, 2, kXYZX, 1);
        TintImm(t, s.tintR, 0.123f, 0.19f);
        i.End(Op::Add);
    }
    {
        Ins i(t);   // mov o0.w, l(1) | r0.x
        Enc::OutputDst(t, 0, 0x8);
        if (s.laterRead) {
            Enc::TempSrc(t, 0, Enc::Rep(0));
        } else {
            Enc::ImmScalar(t, 1.0f);
        }
        i.End(Op::Mov);
    }
    {
        Ins i(t);
        i.End(Op::Ret);
    }
    return t;
}

std::vector<uint8_t> Blob(const Shape& s) {
    Dxbc::Program p;
    p.version = (s.type << 16) | 0x40;   // ps_4_0 / vs_4_0
    p.tokens  = Program(s);
    Dxbc::Container c;
    Dxbc::Chunk sh;
    sh.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    sh.data   = Dxbc::EmitProgram(p);
    c.chunks  = {sh};
    return Dxbc::Emit(c);
}

DecalFog::Match Analyse(const std::vector<uint8_t>& b) {
    DecalFog::Match m;
    DecalFog::Analyse(b.data(), b.size(), &m);
    return m;
}

struct Inputs {
    float v[5][4] = {};
    float cb[4][4] = {};
};

float Sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// A small evaluator of the straight-line float subset the shapes use; o0 after the program.
bool Eval(const std::vector<uint8_t>& blob, const Inputs& in, float o0[4]) {
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &c, &err)) return false;
    Dxbc::Program p;
    if (!Dxbc::ParseProgram(*c.Find(Dxbc::FourCC('S', 'H', 'D', 'R')), &p, &err)) return false;
    float r[8][4] = {};
    float o[1][4] = {};
    auto src = [&](const Dxbc::Operand& op, int slot) -> float {
        float v = 0.0f;
        const uint32_t comp = op.Swz(slot);
        if (op.type == OT::Imm32) {
            v = op.ImmF(op.numComp == 1 ? 0 : static_cast<int>(comp));
        } else if (op.type == OT::Temp) {
            v = r[op.index[0]][comp];
        } else if (op.type == OT::Input) {
            v = in.v[op.index[0]][comp];
        } else if (op.type == OT::ConstantBuffer) {
            v = in.cb[op.index[1]][comp];
        }
        if (op.modifier == 1) v = -v;
        return v;
    };
    for (const Dxbc::Instr& x : p.instrs) {
        if (x.isDecl || x.operands.empty()) continue;
        const Dxbc::Operand& d = x.operands[0];
        float res[4] = {};
        for (int k = 0; k < 4; ++k) {
            if (((d.compBits >> k) & 1) == 0) continue;
            float v = 0.0f;
            if (x.opcode == Op::Mov) {
                v = src(x.operands[1], k);
            } else if (x.opcode == Op::Add) {
                v = src(x.operands[1], k) + src(x.operands[2], k);
            } else if (x.opcode == Op::Mul) {
                v = src(x.operands[1], k) * src(x.operands[2], k);
            } else if (x.opcode == Op::Min) {
                v = std::fmin(src(x.operands[1], k), src(x.operands[2], k));
            } else if (x.opcode == Op::Mad) {
                v = src(x.operands[1], k) * src(x.operands[2], k) + src(x.operands[3], k);
            } else {
                return false;
            }
            res[k] = x.Saturate() ? Sat(v) : v;
        }
        float* dst = d.type == OT::Temp ? r[d.index[0]] : o[d.index[0]];
        for (int k = 0; k < 4; ++k) {
            if ((d.compBits >> k) & 1) dst[k] = res[k];
        }
    }
    std::memcpy(o0, o[0], sizeof o[0]);
    return true;
}

float Lerp(float a, float b, float t) { return a + t * (b - a); }

Inputs SampleInputs(float u, float f) {
    Inputs in;
    in.v[1][2] = u;
    in.v[2][2] = f;
    const float x[3] = {0.8f, 0.6f, 0.4f};
    const float y[3] = {0.9f, 0.7f, 0.5f};
    for (int k = 0; k < 3; ++k) {
        in.v[3][k] = x[k];
        in.v[4][k] = y[k];
    }
    in.cb[1][0] = 0.7f;
    in.cb[1][1] = 0.72f;
    in.cb[1][2] = 0.75f;
    in.cb[1][3] = 0.9f;   // the fog cap
    return in;
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

void CheckMeaning(const Shape& s) {
    const std::vector<uint8_t> orig = Blob(s);
    const DecalFog::Match m = Analyse(orig);
    CHECK(m.matched);
    std::vector<uint8_t> xf;
    CHECK(DecalFog::Transform(m, &xf));
    const DecalFog::Match again = Analyse(xf);
    CHECK(again.program);
    CHECK(!again.matched);   // the copy's tint now feeds a temp
    CHECK(again.prog.instrs.size() == m.prog.instrs.size() + DecalFog::kAddedInstrs);
    uint32_t sum[4];
    Dxbc::Checksum(xf.data(), xf.size(), sum);
    Dxbc::Container rc;
    std::string err;
    CHECK(Dxbc::Parse(xf.data(), xf.size(), &rc, &err));
    CHECK(std::memcmp(sum, rc.checksum, 16) == 0);
    CHECK(again.temps == m.temps + 2);

    const float tint[3] = {0.025f, 0.123f, 0.19f};
    const float us[3] = {0.0f, 1.0f, 0.4f};
    const float fs[3] = {0.0f, 0.6f, 1.0f};
    for (float u : us) {
        for (float f : fs) {
            const Inputs in = SampleInputs(u, f);
            float a[4];
            float b[4];
            CHECK(Eval(orig, in, a));
            CHECK(Eval(xf, in, b));
            const float fe = std::fmin(Sat(f), in.cb[1][3]);
            for (int k = 0; k < 3; ++k) {
                const float L = in.v[3][k] * in.v[4][k];
                const float retail = Lerp(Lerp(L, in.cb[1][k], fe), tint[k], u);
                const float fixed = Lerp(Lerp(L, tint[k], u), in.cb[1][k], fe);
                CHECK(Near(a[k], retail));
                CHECK(Near(b[k], fixed));
                if (u == 0.0f || fe == 0.0f) CHECK(Near(a[k], b[k]));
            }
            CHECK(a[3] == b[3]);
        }
    }
}

}  // namespace

UNIT_TEST(ext_decal_fog_rewrite_swaps_the_two_lerps) {
    CheckMeaning(Shape());
}

UNIT_TEST(ext_decal_fog_rewrite_swizzled_layout) {
    Shape s;
    s.swizzled = true;   // the fog amount is overwritten by the fog lerp's own destination
    CheckMeaning(s);
}

UNIT_TEST(ext_decal_fog_rewrite_refusals) {
    {
        Shape s;
        s.tintFirst = true;
        const DecalFog::Match m = Analyse(Blob(s));
        CHECK(!m.matched);
        CHECK(m.tint);
        CHECK(m.tintFirst);
    }
    {
        Shape s;
        s.laterRead = true;
        CHECK(!Analyse(Blob(s)).matched);
    }
    {
        Shape s;
        s.notFog = true;
        CHECK(!Analyse(Blob(s)).matched);
    }
    {
        Shape s;
        s.flow = true;
        const DecalFog::Match m = Analyse(Blob(s));
        CHECK(!m.matched);
        CHECK(m.tint);
    }
    {
        Shape s;
        s.twoTints = true;
        CHECK(!Analyse(Blob(s)).matched);
    }
    {
        Shape s;
        s.tintR = 0.026f;
        const DecalFog::Match m = Analyse(Blob(s));
        CHECK(!m.matched);
        CHECK(!m.tint);
    }
    {
        Shape s;
        s.type = 1;
        const DecalFog::Match m = Analyse(Blob(s));
        CHECK(!m.matched);
        CHECK(!m.pixel);
    }
    {
        const std::vector<uint8_t> junk = {1, 2, 3, 4};
        CHECK(!Analyse(junk).container);
        std::vector<uint8_t> out = {7};
        CHECK(!DecalFog::Transform(Analyse(junk), &out));
        CHECK(out.size() == 1);
    }
}

UNIT_TEST(ext_decal_fog_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("decal_fog_order");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Fix);
    CHECK(e->defaultOn);
    CHECK(Ext::Fixes().decal_fog_order);
    CHECK(Ext::LiveFixes().decal_fog_order.load());
}
