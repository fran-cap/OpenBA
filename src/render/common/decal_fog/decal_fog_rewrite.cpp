// ============================================================================
// src/render/common/decal_fog/decal_fog_rewrite.cpp -- decal_fog_rewrite.h
// ============================================================================
// NOT RETAIL (fix.decal_fog_order, docs/retail_bugs.md section 32).
// ============================================================================
#include "decal_fog/decal_fog_rewrite.h"

#include <cstring>

namespace DecalFog {

namespace {

namespace Op  = Dxbc::Op;
namespace OT  = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;

// D3D10_SB_OPCODE_TYPE values this file needs beyond Dxbc::Op: everything that leaves straight-line code.
enum : uint32_t {
    kBreak = 2, kBreakC = 3, kCall = 4, kCallC = 5, kCase = 6, kContinue = 7, kContinueC = 8,
    kDefault = 10, kElse = 18, kEndIf = 21, kEndLoop = 22, kEndSwitch = 23, kIf = 31, kLabel = 44,
    kLoop = 48, kRetC = 63, kSwitch = 76,
};

constexpr uint32_t kXyz  = Dxbc::Mask::XYZ;
constexpr uint32_t kXyzw = Enc::Swizzle(0, 1, 2, 3);

bool IsFlowControl(uint32_t op) {
    switch (op) {
    case kBreak: case kBreakC: case kCall: case kCallC: case kCase: case kContinue: case kContinueC:
    case kDefault: case kElse: case kEndIf: case kEndLoop: case kEndSwitch: case kIf:
    case kLabel: case kLoop: case kRetC: case kSwitch:
        return true;
    default:
        return false;
    }
}

bool Extended(const Dxbc::Instr& in) { return (in.token & Dxbc::kTokenExtended) != 0; }

// A plain four-component temp: no modifier, no relative index.
bool PlainTemp(const Dxbc::Operand& o) {
    return o.type == OT::Temp && o.numComp == 2 && o.indexDim == 1 && !o.relative && o.modifier == 0;
}

// A temp destination writing exactly `mask`.
bool TempDstMask(const Dxbc::Operand& o, uint32_t mask) {
    return PlainTemp(o) && o.selMode == 0 && o.compBits == mask;
}

// A source reading one component of its register in every slot (`r0.xxxx`, `r0.x`): that component, else -1.
int Replicated(const Dxbc::Operand& o) {
    if (o.numComp != 2 || o.selMode == 0) return -1;
    const uint32_t c = o.Swz(0);
    for (int k = 1; k < 4; ++k) {
        if (o.Swz(k) != c) return -1;
    }
    return static_cast<int>(c);
}

// A source whose first three slots read x, y, z of its register (`r0.xyzx`, `r0.xyzw`).
bool ReadsXyz(const Dxbc::Operand& o) {
    return o.numComp == 2 && o.selMode != 0 && o.Swz(0) == 0 && o.Swz(1) == 1 && o.Swz(2) == 2;
}

uint32_t SwizzleOf(const Dxbc::Operand& o) { return Enc::Swizzle(o.Swz(0), o.Swz(1), o.Swz(2), o.Swz(3)); }

// The components of its register a source operand may read (conservative: every selected slot).
uint32_t ReadComponents(const Dxbc::Operand& o) {
    if (o.numComp != 2) return 1;
    if (o.selMode == 0) return o.compBits & Dxbc::Mask::XYZW;
    uint32_t bits = 0;
    for (int k = 0; k < 4; ++k) bits |= 1u << o.Swz(k);
    return bits;
}

// The instruction before `before` that last wrote component `comp` of temp `reg`, or -1.
int LastWriter(const Dxbc::Program& p, int before, uint32_t reg, uint32_t comp) {
    for (int i = before - 1; i >= 0; --i) {
        const Dxbc::Instr& in = p.instrs[static_cast<size_t>(i)];
        if (in.isDecl || in.operands.empty()) continue;
        const Dxbc::Operand& d = in.operands[0];
        if (d.type == OT::Temp && d.indexDim == 1 && d.index[0] == reg && d.selMode == 0 &&
            ((d.compBits >> comp) & 1) != 0)
            return i;
    }
    return -1;
}

// A constant-buffer source reading component `comp` (via the destination slot `slot`): its buffer / row.
bool CbRead(const Dxbc::Operand& o, int slot, uint32_t comp, uint32_t* cb, uint32_t* row) {
    if (o.type != OT::ConstantBuffer || o.indexDim != 2 || o.relative || o.modifier != 0) return false;
    if (o.numComp != 2 || o.selMode == 0 || o.Swz(slot) != comp) return false;
    *cb  = o.index[0];
    *row = o.index[1];
    return true;
}

void Refuse(Match* m, const char* why) {
    m->matched = false;
    m->why     = why;
}

}  // namespace

void Analyse(const uint8_t* blob, size_t size, Match* m) {
    *m = Match();
    std::string err;
    if (!Dxbc::Parse(blob, size, &m->cont, &err)) {
        m->why = "container: " + err;
        return;
    }
    m->container = true;
    const Dxbc::Chunk* shdr = m->cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = m->cont.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) {
        m->why = "no SHDR / SHEX";
        return;
    }
    if (!Dxbc::ParseProgram(*shdr, &m->prog, &err)) {
        m->why = "program: " + err;
        return;
    }
    if (Dxbc::EmitProgram(m->prog) != shdr->data) {
        m->why = "does not round-trip";
        return;
    }
    m->program = true;
    const Dxbc::Program& p = m->prog;
    if (p.ProgramType() != 0 || p.Major() < 4 || p.Major() > 5) {
        m->why = "not ps_4_x / ps_5_x";
        return;
    }
    m->pixel = true;

    // ---- the one underwater tint ----------------------------------------------------------
    int  tints = 0;
    int  e     = -1;
    bool flow  = false;
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.opcode == Op::DclTemps && in.isDecl && in.length == 2) {
            m->tempsIdx = static_cast<int>(i);
            m->temps    = p.tokens[in.offset + 1];
        }
        if (in.isDecl) continue;
        if (IsFlowControl(in.opcode)) flow = true;
        for (const Dxbc::Operand& o : in.operands) {
            if (o.type == OT::Imm32 && o.numComp == 2 && o.imm[0] == kTintBits[0] && o.imm[1] == kTintBits[1] &&
                o.imm[2] == kTintBits[2]) {
                ++tints;
                e = static_cast<int>(i);
            }
        }
    }
    if (tints == 0) {
        m->why = "no underwater tint";
        return;
    }
    m->tint = true;
    if (flow) {
        Refuse(m, "flow control");
        return;
    }
    if (tints != 1) {
        Refuse(m, "the tint immediate appears more than once");
        return;
    }
    if (m->tempsIdx < 0) {
        Refuse(m, "no dcl_temps");
        return;
    }
    const int c = e - 1;
    const int g = e + 1;
    const int h = e + 2;
    if (c < 0 || h >= static_cast<int>(p.instrs.size())) {
        Refuse(m, "the tint is not inside the program");
        return;
    }
    const Dxbc::Instr& C = p.instrs[static_cast<size_t>(c)];
    const Dxbc::Instr& E = p.instrs[static_cast<size_t>(e)];
    const Dxbc::Instr& G = p.instrs[static_cast<size_t>(g)];
    const Dxbc::Instr& H = p.instrs[static_cast<size_t>(h)];
    if (C.isDecl || G.isDecl || H.isDecl || Extended(C) || Extended(E) || Extended(G) || Extended(H)) {
        Refuse(m, "the tint's neighbours are not plain instructions");
        return;
    }

    // ---- H: the tint lerp writes the colour output (else the tint comes before the fog) ---
    if (H.opcode != Op::Mad || H.Saturate() || H.operands.size() != 4) {
        Refuse(m, "the tint does not end in a mad");
        return;
    }
    const Dxbc::Operand& hDst = H.operands[0];
    if (hDst.type != OT::Output) {
        m->tintFirst = hDst.type == OT::Temp;
        Refuse(m, "the tint writes a temp: it comes before the fog");
        return;
    }
    if (hDst.numComp != 2 || hDst.selMode != 0 || hDst.compBits != kXyz || hDst.relative || hDst.indexDim != 1) {
        Refuse(m, "the tint does not write o#.xyz");
        return;
    }

    // ---- E: add U.xyz, -F.xyz, l(tint) -----------------------------------------------------
    if (E.opcode != Op::Add || E.Saturate() || E.operands.size() != 3 || !TempDstMask(E.operands[0], kXyz)) {
        Refuse(m, "the tint is not `add U.xyz, -F, l(tint)`");
        return;
    }
    const Dxbc::Operand& eF = E.operands[1];
    const Dxbc::Operand& eImm = E.operands[2];
    if (eF.type != OT::Temp || eF.relative || eF.modifier != 1 || !ReadsXyz(eF) || eImm.type != OT::Imm32) {
        Refuse(m, "the tint is not `add U.xyz, -F, l(tint)`");
        return;
    }
    const uint32_t uReg = E.operands[0].index[0];

    // ---- C: mad F.xyz, f.cccc, D, L --------------------------------------------------------
    if (C.opcode != Op::Mad || C.Saturate() || C.operands.size() != 4 || !TempDstMask(C.operands[0], kXyz)) {
        Refuse(m, "the instruction before the tint is not the fog lerp's mad");
        return;
    }
    const uint32_t fReg = C.operands[0].index[0];
    const Dxbc::Operand& cf = C.operands[1];
    const Dxbc::Operand& cd = C.operands[2];
    const Dxbc::Operand& cl = C.operands[3];
    const int fComp = Replicated(cf);
    if (!PlainTemp(cf) || fComp < 0 || !PlainTemp(cd) || !PlainTemp(cl) || cd.selMode == 0 || cl.selMode == 0) {
        Refuse(m, "the fog lerp's sources are not plain temps");
        return;
    }
    if (eF.index[0] != fReg) {
        Refuse(m, "the tint does not read the fog lerp");
        return;
    }

    // ---- G: mov_sat u.c, v#.c --------------------------------------------------------------
    if (G.opcode != Op::Mov || !G.Saturate() || G.operands.size() != 2) {
        Refuse(m, "the underwater amount is not a mov_sat");
        return;
    }
    const Dxbc::Operand& gDst = G.operands[0];
    const Dxbc::Operand& gSrc = G.operands[1];
    const uint32_t gMask = gDst.compBits;
    if (!PlainTemp(gDst) || gDst.selMode != 0 || (gMask != 1 && gMask != 2 && gMask != 4 && gMask != 8) ||
        gSrc.type != OT::Input || gSrc.numComp != 2 || gSrc.selMode == 0 || gSrc.relative || gSrc.modifier != 0) {
        Refuse(m, "the underwater amount is not `mov_sat u.c, v#.c`");
        return;
    }
    uint32_t gComp = 0;
    while (((gMask >> gComp) & 1) == 0) ++gComp;
    const uint32_t gReg = gDst.index[0];

    // ---- H's sources: u.cccc, U.xyz, F.xyz --------------------------------------------------
    const Dxbc::Operand& hu = H.operands[1];
    const Dxbc::Operand& hU = H.operands[2];
    const Dxbc::Operand& hF = H.operands[3];
    if (!PlainTemp(hu) || hu.index[0] != gReg || Replicated(hu) != static_cast<int>(gComp) || !PlainTemp(hU) ||
        hU.index[0] != uReg || !ReadsXyz(hU) || !PlainTemp(hF) || hF.index[0] != fReg || !ReadsXyz(hF)) {
        Refuse(m, "the tint lerp's sources are not u, U and F");
        return;
    }

    // ---- C is the fog lerp: f = min_sat(*, fog.w), D = fog.xyz - X Y on the same row -------
    const int fw = LastWriter(p, c, cf.index[0], static_cast<uint32_t>(fComp));
    if (fw < 0) {
        Refuse(m, "the fog amount is never written");
        return;
    }
    const Dxbc::Instr& F = p.instrs[static_cast<size_t>(fw)];
    const uint32_t fDstMask = F.operands.empty() ? 0 : F.operands[0].compBits;
    uint32_t fogCb  = 0;
    uint32_t fogRow = 0;
    bool fogW = false;
    if (F.opcode == Op::Min && F.Saturate() && !Extended(F) && F.operands.size() == 3 &&
        ((fDstMask >> fComp) & 1) != 0) {
        for (size_t k = 1; k <= 2 && !fogW; ++k) fogW = CbRead(F.operands[k], fComp, 3, &fogCb, &fogRow);
    }
    if (!fogW) {
        Refuse(m, "the lerp amount is not `min_sat f, *, cb#[r].w` (not the fog)");
        return;
    }
    int dw = -1;
    for (int k = 0; k < 3; ++k) {
        const int w = LastWriter(p, c, cd.index[0], cd.Swz(k));
        if (w < 0 || (dw >= 0 && w != dw)) {
            Refuse(m, "the fog lerp's difference is not one instruction");
            return;
        }
        dw = w;
    }
    const Dxbc::Instr& Dw = p.instrs[static_cast<size_t>(dw)];
    bool fogColour = Dw.opcode == Op::Mad && !Dw.Saturate() && !Extended(Dw) && Dw.operands.size() == 4 &&
                     Dw.operands[1].modifier == 1;
    for (int k = 0; k < 3 && fogColour; ++k) {
        // The difference's component cd.Swz(k) (written in that slot of the mad's mask) must come from
        // the fog colour's component k.
        const int dc = static_cast<int>(cd.Swz(k));
        uint32_t cb  = 0;
        uint32_t row = 0;
        fogColour = CbRead(Dw.operands[3], dc, static_cast<uint32_t>(k), &cb, &row) && cb == fogCb && row == fogRow;
    }
    if (!fogColour) {
        Refuse(m, "the fog lerp's difference is not `mad D, -X, Y, fog.xyz`");
        return;
    }

    // ---- after H: nothing reads what C, E or G left behind ----------------------------------
    uint32_t left[3][2] = {{fReg, kXyz}, {uReg, kXyz}, {gReg, gMask}};   // reg, components
    for (size_t i = static_cast<size_t>(h) + 1; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.isDecl) continue;
        for (size_t k = 0; k < in.operands.size(); ++k) {
            const Dxbc::Operand& o = in.operands[k];
            if (o.relative) {
                Refuse(m, "a relative index after the tint");
                return;
            }
            if (k == 0 || o.type != OT::Temp || o.indexDim != 1) continue;
            const uint32_t reads = ReadComponents(o);
            for (const auto& l : left) {
                if (o.index[0] == l[0] && (reads & l[1]) != 0) {
                    Refuse(m, "a later instruction reads the fogged colour or the tint");
                    return;
                }
            }
        }
        if (in.operands.empty()) continue;
        const Dxbc::Operand& d = in.operands[0];
        if (d.type != OT::Temp || d.indexDim != 1 || d.selMode != 0) continue;
        for (auto& l : left) {
            if (d.index[0] == l[0]) l[1] &= ~d.compBits;
        }
    }

    m->fogIdx  = c;
    m->matched = true;
}

bool Transform(const Match& m, std::vector<uint8_t>* out) {
    if (!m.matched) return false;
    const Dxbc::Program& p = m.prog;
    const uint32_t N = m.temps;       // ours, never retail's
    const uint32_t M = m.temps + 1;
    const Dxbc::Instr& C = p.instrs[static_cast<size_t>(m.fogIdx)];
    const Dxbc::Instr& E = p.instrs[static_cast<size_t>(m.fogIdx + 1)];
    const Dxbc::Instr& G = p.instrs[static_cast<size_t>(m.fogIdx + 2)];
    const Dxbc::Instr& H = p.instrs[static_cast<size_t>(m.fogIdx + 3)];
    const Dxbc::Operand& f = C.operands[1];
    const Dxbc::Operand& D = C.operands[2];
    const Dxbc::Operand& L = C.operands[3];
    const Dxbc::Operand& u = G.operands[1];
    uint32_t gComp = 0;
    while (((G.operands[0].compBits >> gComp) & 1) == 0) ++gComp;

    std::vector<uint32_t> t;
    t.reserve(p.tokens.size() + 48);
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        const int ii = static_cast<int>(i);
        const uint32_t* src = p.tokens.data() + in.offset;
        if (ii == m.tempsIdx) {
            t.push_back(src[0]);
            t.push_back(m.temps + 2);
            continue;
        }
        if (ii > m.fogIdx && ii <= m.fogIdx + 3) continue;   // E, G, H: emitted with C below
        if (ii != m.fogIdx) {
            t.insert(t.end(), src, src + in.length);
            continue;
        }
        // add rN.xyz, -L, l(tint)
        size_t at = t.size();
        t.push_back(0);
        Enc::TempDst(t, N, kXyz);
        Enc::TempSrc(t, L.index[0], SwizzleOf(L), 1);
        Enc::Copy(t, p, E.operands[2]);
        t[at] = Enc::Opcode(Op::Add, static_cast<uint32_t>(t.size() - at));
        // mov_sat rN.w, v#.c
        at = t.size();
        t.push_back(0);
        Enc::TempDst(t, N, Dxbc::Mask::W);
        Enc::CopyWithSwizzle(t, p, u, Enc::Rep(u.Swz(static_cast<int>(gComp))));
        t[at] = Enc::Opcode(Op::Mov, static_cast<uint32_t>(t.size() - at), true);
        // mad rN.xyz, rN.wwww, rN.xyzw, L          T = lerp(L, tint, u)
        at = t.size();
        t.push_back(0);
        Enc::TempDst(t, N, kXyz);
        Enc::TempSrc(t, N, Enc::Rep(3));
        Enc::TempSrc(t, N, kXyzw);
        Enc::Copy(t, p, L);
        t[at] = Enc::Opcode(Op::Mad, static_cast<uint32_t>(t.size() - at));
        // add rM.xyz, D, L                          fog = L + D
        at = t.size();
        t.push_back(0);
        Enc::TempDst(t, M, kXyz);
        Enc::Copy(t, p, D);
        Enc::Copy(t, p, L);
        t[at] = Enc::Opcode(Op::Add, static_cast<uint32_t>(t.size() - at));
        // add rM.xyz, -rN.xyzw, rM.xyzw             fog - T
        at = t.size();
        t.push_back(0);
        Enc::TempDst(t, M, kXyz);
        Enc::TempSrc(t, N, kXyzw, 1);
        Enc::TempSrc(t, M, kXyzw);
        t[at] = Enc::Opcode(Op::Add, static_cast<uint32_t>(t.size() - at));
        // mad o#.xyz, f.cccc, rM.xyzw, rN.xyzw      lerp(T, fog, f)
        at = t.size();
        t.push_back(0);
        Enc::Copy(t, p, H.operands[0]);
        Enc::Copy(t, p, f);
        Enc::TempSrc(t, M, kXyzw);
        Enc::TempSrc(t, N, kXyzw);
        t[at] = Enc::Opcode(Op::Mad, static_cast<uint32_t>(t.size() - at));
    }

    Dxbc::Program np;
    np.version = p.version;
    np.tokens  = std::move(t);
    Dxbc::Container cont = m.cont;
    Dxbc::Chunk* shdr = cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = cont.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) return false;
    shdr->data = Dxbc::EmitProgram(np);
    std::vector<uint8_t> blob = Dxbc::Emit(cont);

    // Self-check: the copy parses, its checksum verifies, and it has exactly kAddedInstrs instructions more.
    Dxbc::Container rc;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &rc, &err)) return false;
    uint32_t sum[4];
    Dxbc::Checksum(blob.data(), blob.size(), sum);
    if (std::memcmp(sum, rc.checksum, 16) != 0) return false;
    const Dxbc::Chunk* rs = rc.Find(shdr->fourcc);
    Dxbc::Program rp;
    if (rs == nullptr || !Dxbc::ParseProgram(*rs, &rp, &err)) return false;
    if (rp.instrs.size() != p.instrs.size() + kAddedInstrs) return false;
    *out = std::move(blob);
    return true;
}

}  // namespace DecalFog
