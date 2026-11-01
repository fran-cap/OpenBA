// ============================================================================
// src/render/common/better_trees/receiver_rewrite.cpp -- receiver_rewrite.h
// ============================================================================
// NOT RETAIL (ext.billboard_shadow). Standard C++ only: the unit tests link it.
// ============================================================================
#include "better_trees/receiver_rewrite.h"

#include <cstdio>
#include <cstring>

namespace BbsRewrite {
namespace {

namespace Op = Dxbc::Op;
constexpr uint32_t kFrc = 26;   // D3D10_SB_OPCODE_FRC
namespace OT = Dxbc::OperandType;

constexpr uint32_t kOperandNull = 13;   // D3D10_SB_OPERAND_TYPE_NULL
constexpr uint32_t kInstanceCb = 3, kTranslationRow = 3;   // tc.transforms[i] row 3 (THE HASH's T)

// D3D10_SB_OPCODE_TYPE values this file needs beyond Dxbc::Op.
enum : uint32_t {
    kBreak = 2, kBreakC = 3, kCall = 4, kCallC = 5, kCase = 6, kContinue = 7, kContinueC = 8, kCut = 9,
    kDefault = 10, kDp3 = 16, kDp4 = 17, kElse = 18, kEmit = 19, kEmitThenCut = 20, kEndIf = 21,
    kEndLoop = 22, kEndSwitch = 23, kIf = 31, kIMul = 38, kItof = 43, kLabel = 44, kLd = 45, kLoop = 48,
    kRetC = 63, kSqrt = 75, kSwitch = 76, kSinCos = 77, kUDiv = 78, kUMul = 81, kDclIndexableTemp = 105,
};

bool IsFlowControl(uint32_t op) {
    switch (op) {
    case kBreak: case kBreakC: case kCall: case kCallC: case kCase: case kContinue: case kContinueC:
    case kCut: case kDefault: case Op::Discard: case kElse: case kEmit: case kEmitThenCut: case kEndIf:
    case kEndLoop: case kEndSwitch: case kIf: case kLabel: case kLoop: case kRetC: case kSwitch:
        return true;
    default:
        return false;
    }
}

int DstCount(const Dxbc::Instr& in) {
    if (in.operands.empty()) return 0;
    switch (in.opcode) {
    case kIMul: case kSinCos: case kUDiv: case kUMul: return 2;
    default: return 1;
    }
}

// How many source components a source operand contributes, by opcode: -1 = per destination
// component (the default), N = the first N swizzle slots (dot products), 4 = all four (fetches).
int SourceSpan(uint32_t op) {
    switch (op) {
    case Op::Dp2: return 2;
    case kDp3: return 3;
    case kDp4: return 4;
    case kLd: case Op::ResInfo: case Op::Sample: case Op::SampleC: case Op::SampleCLz: case Op::SampleL:
    case Op::SampleD: case Op::SampleB:
        return 4;
    default:
        return -1;
    }
}

// One temp read: register and component bits.
struct Read {
    uint32_t reg;
    uint32_t mask;
};

// The temps an operand's RELATIVE indices read (cb3[r0.w + 1] reads r0.w), recursively.
// Returns the operand's length in dwords (0 = malformed).
uint32_t RelativeReads(const uint32_t* t, uint32_t avail, std::vector<Read>* reads, bool* cb0Relative) {
    if (avail == 0) return 0;
    uint32_t n = 0;
    const uint32_t tok = t[n++];
    const uint32_t type = (tok >> 12) & 0xFF;
    const uint32_t dims = (tok >> 20) & 3;
    const uint32_t numComp = tok & 3;
    if (tok & 0x80000000u) {
        uint32_t ext;
        do {
            if (n >= avail) return 0;
            ext = t[n++];
        } while (ext & 0x80000000u);
    }
    if (type == OT::Imm32 || type == OT::Imm64) {
        const uint32_t comps = numComp == 1 ? 1 : (numComp == 2 ? 4 : 0);
        const uint32_t len = comps * (type == OT::Imm64 ? 2 : 1);
        return n + len <= avail ? n + len : 0;
    }
    uint32_t idx0 = 0;
    for (uint32_t d = 0; d < dims; ++d) {
        const uint32_t rep = (tok >> (22 + 3 * d)) & 7;
        if (rep == 0 || rep == 3) {
            if (n >= avail) return 0;
            if (d == 0) idx0 = t[n];
            ++n;
        } else if (rep == 1 || rep == 4) {
            n += 2;
            if (n > avail) return 0;
        }
        if (rep >= 2) {
            if (type == OT::ConstantBuffer && d == 1 && idx0 == kGlobalCb && cb0Relative) *cb0Relative = true;
            // The index register: a temp (or input) with one selected component.
            const uint32_t sub = t[n];
            const uint32_t subType = (sub >> 12) & 0xFF;
            const uint32_t subSel  = (sub >> 2) & 3;
            const uint32_t subBits = (sub >> 4) & 0xFF;
            const uint32_t len = RelativeReads(t + n, avail - n, reads, cb0Relative);
            if (len == 0) return 0;
            if (subType == OT::Temp && reads) {
                uint32_t mask = 0;
                if (subSel == 2) mask = 1u << (subBits & 3);
                else if (subSel == 1) for (int c = 0; c < 4; ++c) mask |= 1u << ((subBits >> (2 * c)) & 3);
                else mask = subBits & 0xF;
                // the sub-operand's register index is its first index dword
                uint32_t k = 1;
                if (sub & 0x80000000u) {
                    while (t[n + k] & 0x80000000u) ++k;
                    ++k;
                }
                reads->push_back({t[n + k], mask});
            }
            n += len;
        }
    }
    return n;
}

// Per instruction: what it writes and reads, in temps and the two windows.
struct Info {
    std::vector<Read> dstTemps;
    uint32_t          outReg  = ~0u;   // an output destination (o#), if any
    uint32_t          outMask = 0;
    std::vector<Read> reads;
    bool              readsW2C   = false;   // cb0[4..7]
    bool              readsShadow = false;  // cb2[10..13]
    bool              cb0Relative = false;
};

bool Describe(const Dxbc::Program& p, const Dxbc::Instr& in, Info* info, std::string* why) {
    const int nd = DstCount(in);
    uint32_t dstUnion = 0;
    for (int i = 0; i < nd && i < static_cast<int>(in.operands.size()); ++i) {
        const Dxbc::Operand& o = in.operands[static_cast<size_t>(i)];
        if (o.type == kOperandNull) continue;
        const uint32_t mask = o.numComp == 2 && o.selMode == 0 ? (o.compBits & 0xF) : 0xF;
        dstUnion |= mask;
        if (o.type == OT::Temp) {
            if (o.relative) { *why = "relative temp destination"; return false; }
            info->dstTemps.push_back({o.index[0], mask});
        } else if (o.type == OT::Output) {
            if (o.relative) { *why = "relative output destination"; return false; }
            info->outReg  = o.index[0];
            info->outMask = mask;
        } else {
            *why = "unexpected destination type";
            return false;
        }
    }
    if (dstUnion == 0) dstUnion = 0xF;
    const int span = SourceSpan(in.opcode);
    for (size_t i = static_cast<size_t>(nd); i < in.operands.size(); ++i) {
        const Dxbc::Operand& o = in.operands[i];
        // relative sub-operands first (their temps are reads too)
        const uint32_t len = RelativeReads(p.tokens.data() + o.offset, o.length, &info->reads, &info->cb0Relative);
        if (len != o.length) { *why = "operand re-walk mismatch"; return false; }
        if (o.type == OT::ConstantBuffer && o.indexDim == 2) {
            if (o.index[0] == kGlobalCb && o.index[1] >= kWorldToCamera && o.index[1] < kWorldToCamera + 4)
                info->readsW2C = true;
            if (o.index[0] == kLightCb && o.index[1] >= kShadowProj0 && o.index[1] < kShadowProj0 + 4)
                info->readsShadow = true;
        }
        if (o.type != OT::Temp) continue;
        uint32_t mask = 0;
        if (o.numComp != 2) {
            mask = 0xF;
        } else if (o.selMode == 2) {
            mask = 1u << (o.compBits & 3);
        } else if (o.selMode == 0) {
            mask = o.compBits & 0xF;
        } else if (span < 0) {
            for (int c = 0; c < 4; ++c)
                if (dstUnion & (1u << c)) mask |= 1u << o.Swz(c);
        } else {
            for (int c = 0; c < span; ++c) mask |= 1u << o.Swz(c);
        }
        info->reads.push_back({o.index[0], mask});
    }
    return true;
}

bool Refuse(Match* m, const std::string& why) {
    m->matched = false;
    m->why = why;
    return false;
}

}  // namespace

void Analyse(const uint8_t* blob, size_t size, Match* m) {
    *m = Match();
    std::string err;
    if (!Dxbc::Parse(blob, size, &m->cont, &err)) { Refuse(m, "container: " + err); return; }
    m->container = true;
    const Dxbc::Chunk* shdr = m->cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = m->cont.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) { Refuse(m, "no SHDR / SHEX chunk"); m->container = false; return; }
    // The container must round-trip byte-identical (same chunk order, same checksum) before it is touched.
    {
        const std::vector<uint8_t> again = Dxbc::Emit(m->cont);
        if (again.size() != size || std::memcmp(again.data(), blob, size) != 0) {
            Refuse(m, "container does not round-trip");
            return;
        }
    }
    if (!Dxbc::ParseProgram(*shdr, &m->prog, &err)) { Refuse(m, "program: " + err); return; }
    m->program = true;
    if (m->prog.ProgramType() != 1) { Refuse(m, "not a vertex shader"); return; }
    m->vertex = true;
    if (m->prog.Major() != 4) { Refuse(m, "not shader model 4"); return; }

    const Dxbc::Program& p = m->prog;
    const size_t n = p.instrs.size();
    uint32_t cb0Rows = 0;
    std::vector<Info> info(n);
    bool anyW2C = false, anyShadow = false;
    int  lastReal = -1;
    for (size_t i = 0; i < n; ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.isDecl) {
            if (in.opcode == Op::DclTemps && in.length >= 2) m->temps = p.tokens[in.offset + 1];
            if (in.opcode == kDclIndexableTemp) { Refuse(m, "indexable temps"); return; }
            if (in.opcode == Op::DclConstantBuffer && in.length >= 4) {
                const uint32_t idx0 = p.tokens[in.offset + 2], idx1 = p.tokens[in.offset + 3];
                if (idx0 == kLightCb) m->cb2Rows = idx1;
                if (idx0 == kGlobalCb) cb0Rows = idx1;
            }
            continue;
        }
        if (IsFlowControl(in.opcode)) { Refuse(m, "flow control"); return; }
        if (in.token & 0x80000000u) { Refuse(m, "extended opcode token"); return; }
        std::string why;
        if (!Describe(p, in, &info[i], &why)) { Refuse(m, why); return; }
        if (info[i].cb0Relative) { Refuse(m, "a relative gc read (its rows cannot be told apart)"); return; }
        anyW2C    = anyW2C || info[i].readsW2C;
        anyShadow = anyShadow || info[i].readsShadow;
        lastReal  = static_cast<int>(i);
    }
    m->candidate = anyW2C && anyShadow;
    if (!m->candidate) { Refuse(m, "does not read both gc.worldToCamera and lc.shadowProjection[0]"); return; }
    if (lastReal < 0 || p.instrs[static_cast<size_t>(lastReal)].opcode != Op::Ret) {
        Refuse(m, "ret is not the last instruction");
        return;
    }
    for (int i = 0; i < lastReal; ++i)
        if (!p.instrs[static_cast<size_t>(i)].isDecl && p.instrs[static_cast<size_t>(i)].opcode == Op::Ret) {
            Refuse(m, "more than one ret");
            return;
        }
    if (m->temps == 0 || m->temps > 2048) { Refuse(m, "dcl_temps missing or too large"); return; }
    for (size_t i = 0; i < n; ++i) {
        for (const Read& r : info[i].reads)
            if (r.reg >= m->temps) { Refuse(m, "temp index past dcl_temps"); return; }
        for (const Read& d : info[i].dstTemps)
            if (d.reg >= m->temps) { Refuse(m, "temp index past dcl_temps"); return; }
    }
    if (m->cb2Rows < kShadowProj0 + 4) { Refuse(m, "cb2 declared too small"); return; }
    if (cb0Rows < kWorldToCamera + 4) { Refuse(m, "cb0 declared too small"); return; }

    // The shadow output: the one output written by instructions that read cb2[10..13].
    bool haveOut = false;
    for (size_t i = 0; i < n; ++i) {
        if (!info[i].readsShadow || info[i].outReg == ~0u) continue;
        if (haveOut && info[i].outReg != m->shadowOut) { Refuse(m, "two shadow outputs"); return; }
        m->shadowOut = info[i].outReg;
        haveOut = true;
    }
    if (!haveOut) { Refuse(m, "no output written from lc.shadowProjection[0]"); return; }
    // The extent (o<shadow>.w) goes after the output's last write, whatever that writes.
    for (size_t i = 0; i < n; ++i)
        if (info[i].outReg == m->shadowOut) m->extentIdx = static_cast<int>(i);

    // Backward slice of the shadow output, component by component (straight-line code: one reaching
    // definition per component).
    const uint32_t T = m->temps;
    std::vector<uint32_t> needed(T, 0);
    uint32_t neededOut = 0xF;
    std::vector<uint8_t> inSlice(n, 0);
    for (size_t k = n; k-- > 0;) {
        const Dxbc::Instr& in = p.instrs[k];
        if (in.isDecl) continue;
        const Info& f = info[k];
        bool mark = false;
        bool writesOut = false;
        if (f.outReg == m->shadowOut && (f.outMask & neededOut)) {
            mark = true;
            writesOut = true;
        }
        for (const Read& d : f.dstTemps)
            if (needed[d.reg] & d.mask) mark = true;
        if (!mark) continue;
        inSlice[k] = 1;
        if (f.outReg != ~0u && f.outReg != m->shadowOut) { Refuse(m, "a slice instruction writes another output"); return; }
        if (writesOut) neededOut &= ~f.outMask;
        for (const Read& d : f.dstTemps) needed[d.reg] &= ~d.mask;
        for (const Read& r : f.reads) needed[r.reg] |= r.mask;
    }

    // P and the insertion point: the slice instructions that read cb2[10..12] all read one position
    // temp with a replicated swizzle; the first cb2[10..13] reader is where the margin block goes.
    {
        int posTemp = -1;
        for (size_t i = 0; i < n; ++i) {
            if (!inSlice[i] || !info[i].readsShadow) continue;
            if (m->insertAt < 0) m->insertAt = static_cast<int>(i);
            const Dxbc::Instr& in = p.instrs[i];
            int row = -1;
            for (const Dxbc::Operand& o : in.operands)
                if (o.type == OT::ConstantBuffer && o.index[0] == kLightCb) row = static_cast<int>(o.index[1]);
            if (row < static_cast<int>(kShadowProj0) || row > static_cast<int>(kShadowProj0) + 2) continue;
            int found = -1;
            for (size_t k = 1; k < in.operands.size(); ++k) {
                const Dxbc::Operand& o = in.operands[k];
                if (o.type == OT::Temp && !o.relative && o.selMode == 1 && o.Swz(0) == o.Swz(1) &&
                    o.Swz(1) == o.Swz(2) && o.Swz(2) == o.Swz(3) &&
                    o.Swz(0) == static_cast<uint32_t>(row - static_cast<int>(kShadowProj0)))
                    found = static_cast<int>(o.index[0]);
            }
            if (found < 0 || (posTemp >= 0 && found != posTemp)) { Refuse(m, "shadow-matrix instructions: no single position temp"); return; }
            posTemp = found;
        }
        if (posTemp < 0 || m->insertAt < 0) { Refuse(m, "no position temp feeding lc.shadowProjection[0]"); return; }
        m->posReg = static_cast<uint32_t>(posTemp);
    }

    // Taint forward (instruction granularity): what depends on cb0[4..7]. The CENTRE is the
    // untainted temp source of the first camera-dependent `add` that writes the position temp's xyz
    // (retail: `add r0.xyz, r0.xyzx, r2.xywx`, the sway / corner offset added to the card centre).
    std::vector<uint32_t> taint(T, 0);
    bool sliceReadsW2C = false;
    for (size_t i = 0; i < n; ++i) {
        if (p.instrs[i].isDecl) continue;
        const Info& f = info[i];
        bool t = f.readsW2C;
        for (const Read& r : f.reads)
            if (taint[r.reg] & r.mask) t = true;
        if (inSlice[i] && f.readsW2C) sliceReadsW2C = true;
        if (m->centerIdx < 0 && inSlice[i] && t && static_cast<int>(i) < m->insertAt) {
            bool writesPos = false;
            for (const Read& d : f.dstTemps)
                if (d.reg == m->posReg && (d.mask & 7) == 7) writesPos = true;
            if (writesPos) {
                const Dxbc::Instr& in = p.instrs[i];
                if (in.opcode != Op::Add || in.operands.size() != 3) { Refuse(m, "the position's first offset is not an add"); return; }
                int clean = -1, dirty = 0;
                for (int k = 1; k <= 2; ++k) {
                    const Dxbc::Operand& o = in.operands[static_cast<size_t>(k)];
                    if (o.type != OT::Temp || o.relative || o.modifier != 0) { Refuse(m, "the position's first offset: not two temps"); return; }
                    uint32_t mask = 0;
                    for (int c = 0; c < 3; ++c) mask |= 1u << o.Swz(c);
                    if (taint[o.index[0]] & mask) ++dirty; else clean = k;
                }
                if (clean < 0 || dirty != 1) { Refuse(m, "the position's first offset: no single camera-independent centre"); return; }
                m->centerIdx = static_cast<int>(i);
                m->centerOperand = clean;
            }
        }
        for (const Read& d : f.dstTemps) taint[d.reg] = t ? (taint[d.reg] | d.mask) : (taint[d.reg] & ~d.mask);
    }
    if (!sliceReadsW2C) { Refuse(m, "the shadow output does not depend on gc.worldToCamera"); return; }
    if (m->centerIdx < 0) { Refuse(m, "no card centre found"); return; }

    // THE HASH (receiver_rewrite.h): V, the vertex input the centre's register is loaded from (the last
    // whole-xyz load of it from an input before the centre add), and T, the first read of the instance
    // translation cb3[rI + 3] before it.
    {
        const Dxbc::Operand& c = p.instrs[static_cast<size_t>(m->centerIdx)].operands[static_cast<size_t>(m->centerOperand)];
        const uint32_t centreReg = c.index[0];
        for (int i = 0; i < m->centerIdx; ++i) {
            const Dxbc::Instr& in = p.instrs[static_cast<size_t>(i)];
            if (in.isDecl) continue;
            if (in.opcode == Op::Mov || in.opcode == kItof) {
                if (in.operands.size() == 2) {
                    const Dxbc::Operand& d = in.operands[0];
                    const Dxbc::Operand& s = in.operands[1];
                    if (d.type == OT::Temp && !d.relative && d.index[0] == centreReg && d.selMode == 0 &&
                        (d.Mask() & 7) == 7 && s.type == OT::Input && !s.relative && s.modifier == 0)
                        m->inputIdx = i;
                }
            }
            if (m->xlateIdx < 0) {
                for (size_t k = 1; k < in.operands.size(); ++k) {
                    const Dxbc::Operand& o = in.operands[k];
                    if (o.type == OT::ConstantBuffer && o.relative && o.indexDim == 2 &&
                        o.index[0] == kInstanceCb && o.index[1] == kTranslationRow) {
                        m->xlateIdx = i;
                        m->xlateOperand = static_cast<int>(k);
                        break;
                    }
                }
            }
        }
        if (m->inputIdx < 0) { Refuse(m, "the card centre is not loaded from a vertex input (no growth-stable hash)"); return; }
        if (m->xlateIdx >= 0 && m->xlateIdx == m->inputIdx) m->xlateIdx = -1;   // a translation read cannot also be the load
    }
    m->matched = true;
    m->why.clear();
}

namespace {

void CbSrc(std::vector<uint32_t>& t, uint32_t cb, uint32_t row, uint32_t swz) {
    t.push_back(2u | (1u << 2) | ((swz & 0xFF) << 4) | (OT::ConstantBuffer << 12) | (2u << 20));
    t.push_back(cb);
    t.push_back(row);
}

// Point a temp operand (immediate index) at another register.
bool RetargetTemp(uint32_t* t, uint32_t avail, uint32_t reg) {
    if (avail < 2) return false;
    uint32_t n = 1;
    if (t[0] & 0x80000000u) {
        while (n < avail && (t[n] & 0x80000000u)) ++n;
        ++n;
    }
    if (n >= avail) return false;
    t[n] = reg;
    return true;
}

// The lookup block (receiver_rewrite.h "THE LOOKUP"): rW.xyz = P + (basis (C - P).L - e |C - P|) L with
// e = margin + jitter (2 frc(V.K + T.Kt) - 1), from rP (P), rS.xyz (C, saved earlier), rV (rV.x = V.K and
// rV.w = T.Kt, written by EmitHashInput / EmitHashTree) and L = lc.nearPlane.xyz (cb2[3]). 10 instructions.
void EmitLookup(std::vector<uint32_t>& t, uint32_t P, uint32_t S, uint32_t W, uint32_t V, const Params& k) {
    namespace E = Dxbc::Enc;
    const uint32_t XYZ = E::Swizzle(0, 1, 2, 0);
    auto ins = [&](uint32_t opc, auto body) {
        const size_t at = t.size();
        t.push_back(0);
        body();
        t[at] = E::Opcode(opc, static_cast<uint32_t>(t.size() - at));
    };
    ins(Op::Add, [&] { E::TempDst(t, W, 7); E::TempSrc(t, S, XYZ); E::TempSrc(t, P, XYZ, 1); });      // d = C - P
    ins(kDp3, [&] { E::TempDst(t, W, 8); E::TempSrc(t, W, XYZ); E::TempSrc(t, W, XYZ); });          // |d|^2
    ins(kSqrt, [&] { E::TempDst(t, W, 8); E::TempSrc(t, W, E::Rep(3)); });                          // r = |d|
    ins(kDp3, [&] { E::TempDst(t, S, 8); E::TempSrc(t, W, XYZ); CbSrc(t, kLightCb, kNearPlane, XYZ); });  // d . L
    ins(Op::Add, [&] { E::TempDst(t, V, 1); E::TempSrc(t, V, E::Rep(0)); E::TempSrc(t, V, E::Rep(3)); });  // V.K + T.Kt
    ins(kFrc, [&] { E::TempDst(t, V, 1); E::TempSrc(t, V, E::Rep(0)); });                            // h = frc
    ins(Op::Mad, [&] { E::TempDst(t, V, 1); E::TempSrc(t, V, E::Rep(0)); E::ImmScalar(t, 2.0f * k.jitter);
                       E::ImmScalar(t, k.margin - k.jitter); });                                    // e = 2j h + (m - j)
    ins(Op::Mul, [&] { E::TempDst(t, V, 1); E::TempSrc(t, V, E::Rep(0)); E::TempSrc(t, W, E::Rep(3)); }); // e r
    ins(Op::Mad, [&] { E::TempDst(t, S, 8); E::TempSrc(t, S, E::Rep(3)); E::ImmScalar(t, k.basis);
                       E::TempSrc(t, V, E::Rep(0), 1); });                                          // s = basis d.L - e r
    ins(Op::Mad, [&] { E::TempDst(t, W, 7); CbSrc(t, kLightCb, kNearPlane, XYZ); E::TempSrc(t, S, E::Rep(3));
                       E::TempSrc(t, P, XYZ); });                                                   // P + s L
}

// The extent (receiver_rewrite.h "THE EXTENT"): o<out>.w = -(rW.w * |cb2[12].xyz|), rW.w = r from the
// lookup block (never written again: the retail tail writes only its own temps). 4 instructions.
void EmitExtent(std::vector<uint32_t>& t, uint32_t out, uint32_t S, uint32_t W) {
    namespace E = Dxbc::Enc;
    const uint32_t XYZ = E::Swizzle(0, 1, 2, 0);
    auto ins = [&](uint32_t opc, auto body) {
        const size_t at = t.size();
        t.push_back(0);
        body();
        t[at] = E::Opcode(opc, static_cast<uint32_t>(t.size() - at));
    };
    ins(kDp3, [&] { E::TempDst(t, S, 8); CbSrc(t, kLightCb, kShadowProj0 + 2, XYZ); CbSrc(t, kLightCb, kShadowProj0 + 2, XYZ); });
    ins(kSqrt, [&] { E::TempDst(t, S, 8); E::TempSrc(t, S, E::Rep(3)); });                           // depth per world unit
    ins(Op::Mul, [&] { E::TempDst(t, S, 8); E::TempSrc(t, S, E::Rep(3)); E::TempSrc(t, W, E::Rep(3)); }); // x r
    ins(Op::Mov, [&] { E::OutputDst(t, out, 8); E::TempSrc(t, S, E::Rep(3), 1); });                   // o.w = -(...)
}

// THE HASH's V (receiver_rewrite.h): after C's input load `in`, the same load into rV.xyz and
// rV.x = rV.xyz . K (K scaled by kInputUnit after an itof). With no translation read, rV.w = 0 too.
// 2 or 3 instructions.
int EmitHashInput(std::vector<uint32_t>& t, const Dxbc::Program& p, const Dxbc::Instr& in, uint32_t V, bool noTree) {
    namespace E = Dxbc::Enc;
    const uint32_t XYZ = E::Swizzle(0, 1, 2, 0);
    const float unit = in.opcode == kItof ? kInputUnit : 1.0f;
    auto ins = [&](uint32_t opc, auto body) {
        const size_t at = t.size();
        t.push_back(0);
        body();
        t[at] = E::Opcode(opc, static_cast<uint32_t>(t.size() - at));
    };
    ins(in.opcode, [&] { E::TempDst(t, V, 7); E::Copy(t, p, in.operands[1]); });                      // V
    ins(kDp3, [&] { E::TempDst(t, V, 1); E::TempSrc(t, V, XYZ);
                    E::Imm4(t, kHashX * unit, kHashY * unit, kHashZ * unit, 0.0f); });                  // V . K
    if (!noTree) return 2;
    ins(Op::Mov, [&] { E::TempDst(t, V, 8); E::ImmScalar(t, 0.0f); });                                // T.Kt = 0
    return 3;
}

// THE HASH's T: before retail's first read of cb3[rI + 3] (its index register is live there),
// rV.w = cb3[rI + 3].xyz . Kt. 1 instruction.
void EmitHashTree(std::vector<uint32_t>& t, const Dxbc::Program& p, const Dxbc::Operand& xlate, uint32_t V) {
    namespace E = Dxbc::Enc;
    const size_t at = t.size();
    t.push_back(0);
    E::TempDst(t, V, 8);
    E::CopyWithSwizzle(t, p, xlate, E::Swizzle(0, 1, 2, 0));
    E::Imm4(t, kTreeX, kTreeY, kTreeZ, 0.0f);
    t[at] = E::Opcode(kDp3, static_cast<uint32_t>(t.size() - at));
}

constexpr int kAddedInstrs = 1 + 10 + 4;   // the centre save + the lookup block + the extent (+ THE HASH's, counted per program)

}  // namespace

bool Transform(const Match& m, std::vector<uint8_t>* out) { return TransformWithParams(m, out, kDefaultParams); }

bool TransformWithParams(const Match& m, std::vector<uint8_t>* out, const Params& k) {
    if (!m.matched) return false;
    const Dxbc::Program& p = m.prog;
    const uint32_t T = m.temps, S = T, W = T + 1, V = T + 2;
    std::vector<uint32_t> t;
    t.reserve(p.tokens.size() + 64);
    int retargeted = 0;
    int hashInstrs = 0;
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        const uint32_t* src = p.tokens.data() + in.offset;
        if (in.isDecl) {
            if (in.opcode == Op::DclTemps) {
                t.push_back(src[0]);
                t.push_back(T + 3);   // S (the centre, then the s scalar), W (d, r, the lookup point), V (the hash)
            } else {
                t.insert(t.end(), src, src + in.length);
            }
            continue;
        }
        if (static_cast<int>(i) == m.centerIdx) {
            // mov rS.xyz, <the centre operand, as the add reads it>
            const Dxbc::Operand& c = in.operands[static_cast<size_t>(m.centerOperand)];
            const size_t at = t.size();
            t.push_back(0);
            Dxbc::Enc::TempDst(t, S, 7);
            Dxbc::Enc::Copy(t, p, c);
            t[at] = Dxbc::Enc::Opcode(Op::Mov, static_cast<uint32_t>(t.size() - at));
        }
        if (static_cast<int>(i) == m.insertAt) EmitLookup(t, m.posReg, S, W, V, k);
        if (static_cast<int>(i) == m.xlateIdx) {
            EmitHashTree(t, p, in.operands[static_cast<size_t>(m.xlateOperand)], V);
            ++hashInstrs;
        }
        const size_t at = t.size();
        t.insert(t.end(), src, src + in.length);
        if (static_cast<int>(i) == m.inputIdx) hashInstrs += EmitHashInput(t, p, in, V, m.xlateIdx < 0);
        if (static_cast<int>(i) == m.extentIdx) EmitExtent(t, m.shadowOut, S, W);
        // The shadow-matrix instructions read the lookup point instead of P.
        if (static_cast<int>(i) >= m.insertAt) {
            bool shadow = false;
            for (const Dxbc::Operand& o : in.operands)
                if (o.type == OT::ConstantBuffer && o.index[0] == kLightCb && o.index[1] >= kShadowProj0 &&
                    o.index[1] < kShadowProj0 + 3)
                    shadow = true;
            if (shadow)
                for (size_t k = 1; k < in.operands.size(); ++k) {
                    const Dxbc::Operand& o = in.operands[k];
                    if (o.type == OT::Temp && !o.relative && o.index[0] == m.posReg && o.selMode == 1 &&
                        o.Swz(0) == o.Swz(1) && o.Swz(1) == o.Swz(2) && o.Swz(2) == o.Swz(3)) {
                        if (!RetargetTemp(t.data() + at + (o.offset - in.offset), o.length, W)) return false;
                        ++retargeted;
                    }
                }
        }
    }
    if (retargeted != 3) return false;   // x, y, z of the one shadow matrix

    Dxbc::Program np;
    np.version = p.version;
    np.tokens  = std::move(t);
    Dxbc::Container c = m.cont;
    Dxbc::Chunk* shdr = c.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = c.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) return false;
    shdr->data = Dxbc::EmitProgram(np);
    std::vector<uint8_t> blob = Dxbc::Emit(c);

    // Self-check: the copy parses, its checksum verifies, and it has exactly the expected shape.
    Dxbc::Container rc;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &rc, &err)) return false;
    uint32_t sum[4];
    Dxbc::Checksum(blob.data(), blob.size(), sum);
    if (std::memcmp(sum, rc.checksum, 16) != 0) return false;
    const Dxbc::Chunk* rs = rc.Find(shdr->fourcc);
    Dxbc::Program rp;
    if (rs == nullptr || !Dxbc::ParseProgram(*rs, &rp, &err)) return false;
    if (rp.instrs.size() != p.instrs.size() + kAddedInstrs + static_cast<size_t>(hashInstrs)) return false;
    *out = std::move(blob);
    return true;
}

}  // namespace BbsRewrite
