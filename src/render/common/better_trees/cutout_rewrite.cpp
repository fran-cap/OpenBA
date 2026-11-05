// ============================================================================
// src/render/common/better_trees/cutout_rewrite.cpp -- see cutout_rewrite.h
// ============================================================================
// NOT RETAIL (ext.foliage_aa). Pure: no D3D, no retail bytes.
// ============================================================================
#include "better_trees/cutout_rewrite.h"

#include <cstring>

namespace FaaRewrite {

using Dxbc::Instr;
using Dxbc::Operand;
using Dxbc::Program;
namespace Op = Dxbc::Op;
namespace OT = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;

namespace {

// ---- the pattern -------------------------------------------------------------

bool IsFlowControl(uint32_t op) {
    // break breakc call callc case continue continuec default else endif endloop endswitch if
    // loop retc switch
    return op == 2 || op == 3 || op == 4 || op == 5 || op == 6 || op == 7 || op == 8 || op == 10 ||
           op == 18 || op == 21 || op == 22 || op == 23 || op == 31 || op == 48 || op == 63 || op == 76;
}
bool HasExtendedOpcode(const Instr& in) { return (in.token & 0x80000000u) != 0; }
uint32_t ReadComp(const Operand& o) {   // the one component a scalar source reads
    if (o.selMode == 2) return o.compBits & 3;
    return o.Swz(0);
}
bool IsScalarImm(const Operand& o, float v) {
    return o.type == OT::Imm32 && o.numComp == 1 && o.ImmF(0) == v;
}
bool IsScalarTempDst(const Operand& o, uint32_t reg, uint32_t comp) {
    return o.type == OT::Temp && o.indexDim == 1 && !o.relative && o.index[0] == reg && o.selMode == 0 &&
           o.compBits == (1u << comp) && o.modifier == 0;
}
// For a scalar-destination instruction (mask one bit d), the component `src` reads for it.
uint32_t SrcCompFor(const Operand& dst, const Operand& src) {
    uint32_t d = 0;
    while (d < 4 && !(dst.compBits & (1u << d))) ++d;
    if (src.selMode == 2) return src.compBits & 3;
    return src.Swz(static_cast<int>(d));
}
bool IsPlainTempSrc(const Operand& o) {
    return o.type == OT::Temp && o.indexDim == 1 && !o.relative && o.modifier == 0 && o.numComp == 2;
}

// ---- the emit ----------------------------------------------------------------
// One instruction at a time into `t`: Begin pushes a placeholder opcode token, End patches it with
// the opcode and the instruction's length.
struct Emitter {
    std::vector<uint32_t>& t;
    const Program&         p;
    size_t                 at = 0;

    void Begin() { at = t.size(); t.push_back(0); }
    void End(uint32_t op, bool sat = false) {
        t[at] = Enc::Opcode(op, static_cast<uint32_t>(t.size() - at), sat);
    }
    // dst.mask = op(src a, src b): temps with swizzles
    void Op3(uint32_t op, uint32_t d, uint32_t mask, uint32_t a, uint32_t sa, uint32_t b, uint32_t sb,
             uint32_t modA = 0, uint32_t modB = 0, bool sat = false) {
        Begin();
        Enc::TempDst(t, d, mask);
        Enc::TempSrc(t, a, sa, modA);
        Enc::TempSrc(t, b, sb, modB);
        End(op, sat);
    }
    void OpImm(uint32_t op, uint32_t d, uint32_t mask, uint32_t a, uint32_t sa, float v, bool sat = false) {
        Begin();
        Enc::TempDst(t, d, mask);
        Enc::TempSrc(t, a, sa);
        Enc::ImmScalar(t, v);
        End(op, sat);
    }
    void Op2(uint32_t op, uint32_t d, uint32_t mask, uint32_t a, uint32_t sa) {
        Begin();
        Enc::TempDst(t, d, mask);
        Enc::TempSrc(t, a, sa);
        End(op);
    }
};

constexpr uint32_t kX = 1, kY = 2, kZ = 4, kW = 8;

// P.x = the filled alpha: rA.c * (1 + max(0, lod) * k), lod the anisotropic estimate (§6.5);
// with a floor, rA.c itself where rA.c < floor. Uses N and M as scratch.
void EmitFill(Emitter& e, const Match& m, uint32_t P, uint32_t N, uint32_t M, float k, float floor) {
    const Program& p   = e.p;
    const Instr&   smp = p.instrs[static_cast<size_t>(m.iSample)];
    const Operand& uv  = smp.operands[1];
    const Operand& res = smp.operands[2];
    const uint32_t sx = uv.Swz(0), sy = uv.Swz(1);
    // M.zw = the texture's mip-0 size (resinfo, float return)
    e.Begin();
    Enc::TempDst(e.t, M, kZ | kW);
    Enc::ImmScalar(e.t, 0.0f);
    Enc::CopyWithSwizzle(e.t, p, res, Enc::Swizzle(0, 1, 0, 1));
    e.End(Op::ResInfo);
    // N.xy = ddx(uv), N.zw = ddy(uv)
    e.Begin();
    Enc::TempDst(e.t, N, kX | kY);
    Enc::CopyWithSwizzle(e.t, p, uv, Enc::Swizzle(sx, sy, sx, sy));
    e.End(Op::DerivRtx);
    e.Begin();
    Enc::TempDst(e.t, N, kZ | kW);
    Enc::CopyWithSwizzle(e.t, p, uv, Enc::Swizzle(sx, sy, sx, sy));
    e.End(Op::DerivRty);
    // in texels
    e.Op3(Op::Mul, N, 0xF, N, Enc::Swizzle(0, 1, 2, 3), M, Enc::Swizzle(2, 3, 2, 3));
    e.Op3(Op::Dp2, M, kX, N, Enc::Swizzle(0, 1, 0, 0), N, Enc::Swizzle(0, 1, 0, 0));   // |ddx|^2
    e.Op3(Op::Dp2, M, kY, N, Enc::Swizzle(2, 3, 2, 2), N, Enc::Swizzle(2, 3, 2, 2));   // |ddy|^2
    e.Op3(Op::Max, M, kZ, M, Enc::Rep(0), M, Enc::Rep(1));                               // Pmax^2
    e.Op3(Op::Min, M, kW, M, Enc::Rep(0), M, Enc::Rep(1));                               // Pmin^2
    e.OpImm(Op::Mul, M, kZ, M, Enc::Rep(2), 1.0f / 256.0f);                              // (Pmax / 16)^2: max aniso 16
    e.Op3(Op::Max, M, kZ, M, Enc::Rep(2), M, Enc::Rep(3));
    e.Op2(Op::Log, M, kZ, M, Enc::Rep(2));
    e.OpImm(Op::Mul, P, kX, M, Enc::Rep(2), 0.5f);                                      // P.x = lod
    e.OpImm(Op::Max, P, kX, P, Enc::Rep(0), 0.0f);
    // P.x = lod * k + 1
    e.Begin();
    Enc::TempDst(e.t, P, kX);
    Enc::TempSrc(e.t, P, Enc::Rep(0));
    Enc::ImmScalar(e.t, k);
    Enc::ImmScalar(e.t, 1.0f);
    e.End(Op::Mad);
    if (floor > 0.0f) {
        // the fill floor (FAA-TUNE): no fill where rA.c < floor -- M.x = rA.c >= floor, P.x = M.x ? P.x : 1
        e.Begin();
        Enc::TempDst(e.t, M, kX);
        Enc::CopyWithSwizzle(e.t, p, m.alpha, Enc::Rep(m.alphaComp));
        Enc::ImmScalar(e.t, floor);
        e.End(Op::Ge);
        e.Begin();
        Enc::TempDst(e.t, P, kX);
        Enc::TempSrc(e.t, M, Enc::Rep(0));
        Enc::TempSrc(e.t, P, Enc::Rep(0));
        Enc::ImmScalar(e.t, 1.0f);
        e.End(Op::MovC);
    }
    // P.x = rA.c * P.x
    e.Begin();
    Enc::TempDst(e.t, P, kX);
    Enc::CopyWithSwizzle(e.t, p, m.alpha, Enc::Rep(m.alphaComp));
    Enc::TempSrc(e.t, P, Enc::Rep(0));
    e.End(Op::Mul);
}

// P.x = saturate((P.x - cut) / max(fwidth(P.x), 1e-4) + 0.5)
void EmitSharpen(Emitter& e, uint32_t P, float cut) {
    e.Op2(Op::DerivRtx, P, kY, P, Enc::Rep(0));
    e.Op2(Op::DerivRty, P, kZ, P, Enc::Rep(0));
    e.Op3(Op::Add, P, kY, P, Enc::Rep(1), P, Enc::Rep(2), 2, 2);   // |ddx| + |ddy|
    e.OpImm(Op::Max, P, kY, P, Enc::Rep(1), 0.0001f);
    e.OpImm(Op::Add, P, kX, P, Enc::Rep(0), -cut);
    e.Op3(Op::Div, P, kX, P, Enc::Rep(0), P, Enc::Rep(1));
    e.OpImm(Op::Add, P, kX, P, Enc::Rep(0), 0.5f, true);
}

}  // namespace

void Analyse(const uint8_t* blob, size_t size, Match* m) {
    if (!Dxbc::Parse(blob, size, &m->cont, &m->err)) { m->why = "not a DXBC container"; return; }
    m->container = true;
    uint32_t sum[4];
    Dxbc::Checksum(blob, size, sum);
    m->checksum = std::memcmp(sum, m->cont.checksum, 16) == 0;
    Dxbc::Chunk* shdr = m->cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (!shdr) { m->why = "no SHDR chunk"; return; }
    if (!Dxbc::ParseProgram(*shdr, &m->prog, &m->err)) { m->why = "SHDR does not parse"; return; }
    m->program = true;
    // identity: SHDR re-emitted from the parsed tokens, the container re-emitted (fresh offsets,
    // sizes and checksum) -- must equal the input byte for byte, or we do not touch the blob
    {
        Dxbc::Container c = m->cont;
        c.Find(shdr->fourcc)->data = Dxbc::EmitProgram(m->prog);
        const std::vector<uint8_t> out = Dxbc::Emit(c);
        m->identity = out.size() == size && std::memcmp(out.data(), blob, size) == 0;
    }
    if (!m->checksum) { m->why = "checksum mismatch"; return; }
    if (!m->identity) { m->why = "does not round-trip"; return; }
    if (m->prog.ProgramType() != 0 || m->prog.Major() != 4) { m->why = "not ps_4_x"; return; }

    const std::vector<Instr>& in = m->prog.instrs;
    for (size_t i = 0; i < in.size(); ++i) {
        const Instr& x = in[i];
        if (x.opcode == Op::Discard) { ++m->discards; m->iDiscard = static_cast<int>(i); }
        if (IsFlowControl(x.opcode)) ++m->flowControl;
        if (x.opcode == Op::Ret) { ++m->rets; m->iRet = static_cast<int>(i); }
        if (x.opcode == Op::DclTemps && x.length == 2) {
            m->iTemps = static_cast<int>(i);
            m->temps  = m->prog.tokens[x.offset + 1];
        }
        if (x.opcode == Op::Mov && !HasExtendedOpcode(x) && !x.Saturate() && x.operands.size() == 2 &&
            x.operands[0].type == OT::Output && x.operands[0].index[0] == 0 && x.operands[0].selMode == 0 &&
            x.operands[0].compBits == kW && IsScalarImm(x.operands[1], 0.0f)) {
            ++m->o0wCount;
            m->iO0w = static_cast<int>(i);
        }
    }
    if (m->discards == 0) { m->why = "no discard"; return; }
    if (m->discards > 1) { m->why = "more than one discard"; return; }
    if (m->flowControl) { m->why = "flow control"; return; }
    if (m->rets != 1 || m->iRet != static_cast<int>(in.size()) - 1) { m->why = "ret not single / last"; return; }
    if (m->iTemps < 0) { m->why = "no dcl_temps"; return; }
    if (m->o0wCount != 1) {
        m->why = m->o0wCount == 0 ? "no `mov o0.w, l(0)` (depth / other)" : "`mov o0.w, l(0)` more than once";
        return;
    }
    const int d = m->iDiscard;
    const Instr& disc = in[static_cast<size_t>(d)];
    if (!disc.TestNonZero() || HasExtendedOpcode(disc)) { m->why = "not discard_nz"; return; }
    if (disc.operands.size() != 1 || !IsPlainTempSrc(disc.operands[0]) || disc.operands[0].selMode == 0) {
        m->why = "discard operand";
        return;
    }
    const uint32_t rT = disc.operands[0].index[0];
    const uint32_t cT = ReadComp(disc.operands[0]);
    if (d < 3) { m->why = "discard too early"; return; }
    const Instr& lt = in[static_cast<size_t>(d - 1)];
    if (lt.opcode != Op::Lt || HasExtendedOpcode(lt) || lt.Saturate() || lt.operands.size() != 3 ||
        !IsScalarTempDst(lt.operands[0], rT, cT) || !IsPlainTempSrc(lt.operands[1]) ||
        lt.operands[1].index[0] != rT || SrcCompFor(lt.operands[0], lt.operands[1]) != cT ||
        !IsScalarImm(lt.operands[2], 0.0f)) {
        m->why = "no `lt rT, rT, l(0)` before the discard: " + Dxbc::Describe(m->prog, lt);
        return;
    }
    const Instr& add = in[static_cast<size_t>(d - 2)];
    if (add.opcode != Op::Add || HasExtendedOpcode(add) || add.Saturate() || add.operands.size() != 3 ||
        !IsScalarTempDst(add.operands[0], rT, cT)) {
        m->why = "no `add rT, a, l(-cut)` before the lt: " + Dxbc::Describe(m->prog, add);
        return;
    }
    int immAt = -1;
    for (int k = 1; k <= 2; ++k)
        if (add.operands[static_cast<size_t>(k)].type == OT::Imm32 && add.operands[static_cast<size_t>(k)].numComp == 1)
            immAt = k;
    const int srcAt = immAt == 1 ? 2 : 1;
    if (immAt < 0 || !IsPlainTempSrc(add.operands[static_cast<size_t>(srcAt)])) {
        m->why = "add operands: " + Dxbc::Describe(m->prog, add);
        return;
    }
    m->cut = -add.operands[static_cast<size_t>(immAt)].ImmF(0);
    if (!(m->cut > 0.0f && m->cut < 1.0f)) { m->why = "cutoff outside (0, 1)"; return; }
    m->alpha     = add.operands[static_cast<size_t>(srcAt)];
    m->alphaComp = SrcCompFor(add.operands[0], m->alpha);
    m->iLt  = d - 1;
    m->iAdd = d - 2;
    // the writer chain of rA.c, walking back from the add: an optional season `mad`, then the sample
    const uint32_t rA = m->alpha.index[0];
    for (int i = d - 3; i >= 0; --i) {
        const Instr& w = in[static_cast<size_t>(i)];
        if (w.isDecl || w.operands.empty()) continue;
        const Operand& dst = w.operands[0];
        if (dst.type != OT::Temp || dst.index[0] != rA || dst.selMode != 0 || !(dst.compBits & (1u << m->alphaComp)))
            continue;
        if (w.opcode == Op::Mad && m->iMad < 0) { m->iMad = i; continue; }
        if (w.opcode == Op::Sample && w.operands.size() == 4 && !HasExtendedOpcode(w) &&
            w.operands[2].type == OT::Resource && w.operands[3].type == OT::Sampler) {
            m->iSample = i;
            break;
        }
        m->why = "alpha written by: " + Dxbc::Describe(m->prog, w);
        return;
    }
    if (m->iSample < 0) { m->why = "no sample writes the alpha"; return; }
    m->colour = true;
    m->why    = "colour";
}

bool Transform(const Match& m, const Options& o, std::vector<uint8_t>* out) {
    if (!m.colour) return false;
    const bool fill = o.fillK > 0.0f;
    if (!o.coverage && !fill) return false;   // nothing to change: the retail blob is the answer
    const Program& p = m.prog;
    const std::vector<Instr>& in = p.instrs;
    const uint32_t P = m.temps, N = m.temps + 1, M = m.temps + 2;   // ours, never retail's

    std::vector<uint32_t> t;
    t.reserve(p.tokens.size() + 160);
    Emitter e{t, p};
    for (size_t i = 0; i < in.size(); ++i) {
        const Instr& x = in[i];
        const int ii = static_cast<int>(i);
        const auto copy = [&]() {
            t.insert(t.end(), p.tokens.begin() + x.offset, p.tokens.begin() + x.offset + x.length);
        };
        if (ii == m.iTemps) {
            t.push_back(p.tokens[x.offset]);
            t.push_back(m.temps + (fill ? 3u : 1u));
            continue;
        }
        if (ii == m.iAdd) {
            if (fill) {
                // P.x = the filled alpha; the retail add reads it instead of rA.c
                EmitFill(e, m, P, N, M, o.fillK, o.fillFloor);
                e.Begin();
                Enc::Copy(t, p, x.operands[0]);
                for (size_t k = 1; k <= 2; ++k) {
                    if (x.operands[k].type == OT::Imm32) Enc::Copy(t, p, x.operands[k]);
                    else Enc::TempSrc(t, P, Enc::Rep(0));
                }
                e.End(Op::Add);
            } else {
                // save the alpha: in four programs the retail add writes its result over it
                e.Begin();
                Enc::TempDst(t, P, kX);
                Enc::CopyWithSwizzle(t, p, m.alpha, Enc::Rep(m.alphaComp));
                e.End(Op::Mov);
                copy();
            }
            continue;
        }
        if (ii == m.iDiscard) {
            if (o.coverage) {
                EmitSharpen(e, P, m.cut);   // retail's discard is dropped
                // the floor also gates the coverage: under alpha to coverage the ramp still covers
                // an unfilled texel under the floor where fwidth is wide (the distance) -- M.x is the
                // fill's `rA.c >= floor` mask (all ones / zero), untouched by retail's code after it
                if (fill && o.fillFloor > 0.0f) e.Op3(Op::And, P, kX, P, Enc::Rep(0), M, Enc::Rep(0));
                // issue #253: `discard_z P.x` -- a fragment whose coverage is exactly 0 covers no sample
                // (alpha to coverage of 0 is an empty mask: no colour, depth or stencil write), so it
                // leaves the same image discarded, and the GPU may drop it before the rest of the program
                // (the shadow taps, the lighting) as under retail's discard (dxbc-spirv: a demote, so the
                // derivatives later in the program stay defined). Only the texels with coverage run on.
                e.Begin();
                Enc::TempSrc(t, P, Enc::Rep(0));
                e.End(Op::Discard);
            } else {
                copy();
                if (fill && o.fillFloor > 0.0f) {   // MSAA off: discard_z M.x (under the floor)
                    e.Begin();
                    Enc::TempSrc(t, M, Enc::Rep(0));
                    e.End(Op::Discard);
                }
            }
            continue;
        }
        if (ii == m.iO0w && o.coverage) {
            e.Begin();
            Enc::OutputDst(t, 0, kW);
            Enc::TempSrc(t, P, Enc::Rep(0));
            e.End(Op::Mov);
            continue;
        }
        copy();
    }
    Program q = p;
    q.tokens = std::move(t);
    Dxbc::Container c = m.cont;
    c.Find(Dxbc::FourCC('S', 'H', 'D', 'R'))->data = Dxbc::EmitProgram(q);
    *out = Dxbc::Emit(c);
    // round-trip the result through our own reader: a malformed emit fails here, not in the driver
    Match chk;
    Analyse(out->data(), out->size(), &chk);
    return chk.program && chk.identity && chk.checksum;
}

}  // namespace FaaRewrite
