// ============================================================================
// src/render/common/better_trees/edge_dither.cpp -- edge_dither.h
// ============================================================================
// NOT RETAIL (ext.better_trees). Standard C++ only: the unit tests link it.
// ============================================================================
#include "better_trees/edge_dither.h"

#include <cstring>

namespace BbsDither {
namespace {

namespace Op = Dxbc::Op;
namespace OT = Dxbc::OperandType;

// D3D10_SB_OPCODE_TYPE values beyond Dxbc::Op.
enum : uint32_t {
    kBreak = 2, kBreakC = 3, kCall = 4, kCallC = 5, kCase = 6, kContinue = 7, kContinueC = 8, kCut = 9,
    kDefault = 10, kElse = 18, kEmit = 19, kEmitThenCut = 20, kEndIf = 21, kEndLoop = 22, kEndSwitch = 23,
    kFrc = 26, kIf = 31, kLabel = 44, kLoop = 48, kRetC = 63, kRoundNi = 65, kSwitch = 76, kSinCos = 77,
    kDclInputPs = 98, kDclIndexableTemp = 105,
};
constexpr uint32_t kOperandNull = 13;   // D3D10_SB_OPERAND_TYPE_NULL
constexpr int kAddedInstrs = 13;

bool IsFlowControl(uint32_t op) {
    switch (op) {
    case kBreak: case kBreakC: case kCall: case kCallC: case kCase: case kContinue: case kContinueC:
    case kCut: case kDefault: case kElse: case kEmit: case kEmitThenCut: case kEndIf: case kEndLoop:
    case kEndSwitch: case kIf: case kLabel: case kLoop: case kRetC: case kSwitch:
        return true;   // discard is not flow control here: the cutout programs discard
    default:
        return false;
    }
}

bool Refuse(Match* m, const std::string& why) {
    m->matched = false;
    m->why = why;
    return false;
}

// The register index dword of an operand with an immediate first index (after any extended tokens).
uint32_t IndexSlot(const uint32_t* t, uint32_t avail) {
    uint32_t n = 1;
    if (t[0] & Dxbc::kTokenExtended) {
        while (n < avail && (t[n] & Dxbc::kTokenExtended)) ++n;
        ++n;
    }
    return n < avail ? n : 0;
}

// ISGN: u32 count, u32 8, then 24-byte elements {nameOffset, semanticIndex, systemValue, componentType,
// register, u8 mask, u8 readWriteMask, u16 pad}. The `mask` byte (20) is what the previous stage writes,
// `readWriteMask` (21) what this program reads. -1 = no chunk / malformed / no element for `reg`.
int IsgnMask(const Dxbc::Container& c, uint32_t reg) {
    const Dxbc::Chunk* isgn = c.Find(Dxbc::FourCC('I', 'S', 'G', 'N'));
    if (isgn == nullptr || isgn->data.size() < 8) return -1;
    uint32_t count;
    std::memcpy(&count, isgn->data.data(), 4);
    if (isgn->data.size() < 8 + 24ull * count) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = isgn->data.data() + 8 + 24 * i;
        uint32_t r;
        std::memcpy(&r, e + 16, 4);
        if (r == reg) return e[20];
    }
    return -1;
}

void InputSrc(std::vector<uint32_t>& t, uint32_t reg, uint32_t swz, uint32_t modifier = 0) {
    uint32_t tok = 2u | (1u << 2) | ((swz & 0xFF) << 4) | (OT::Input << 12) | (1u << 20);
    if (modifier) tok |= Dxbc::kTokenExtended;
    t.push_back(tok);
    if (modifier) t.push_back(1u | (modifier << 6));
    t.push_back(reg);
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
    {
        const std::vector<uint8_t> again = Dxbc::Emit(m->cont);
        if (again.size() != size || std::memcmp(again.data(), blob, size) != 0) {
            Refuse(m, "container does not round-trip");
            return;
        }
    }
    if (!Dxbc::ParseProgram(*shdr, &m->prog, &err)) { Refuse(m, "program: " + err); return; }
    m->program = true;
    if (m->prog.ProgramType() != 0) { Refuse(m, "not a pixel shader"); return; }
    m->pixel = true;
    if (m->prog.Major() != 4) { Refuse(m, "not shader model 4"); return; }

    const Dxbc::Program& p = m->prog;
    const size_t n = p.instrs.size();
    int  lastReal = -1, rets = 0, lastDiscard = -1;
    int  shadowIn = -1, shadowComp = -1, uvIn = -1;
    uint32_t uvSwz = 0;
    for (size_t i = 0; i < n; ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.isDecl) {
            if (in.opcode == Op::DclTemps && in.length >= 2) m->temps = p.tokens[in.offset + 1];
            if (in.opcode == kDclIndexableTemp) { Refuse(m, "indexable temps"); return; }
            continue;
        }
        if (m->firstInstr < 0) m->firstInstr = static_cast<int>(i);
        if (IsFlowControl(in.opcode)) { Refuse(m, "flow control"); return; }
        // (extended opcode tokens -- the sample instructions' resource dim / return type -- are copied as they are)
        if (in.opcode == Op::Ret) ++rets;
        if (in.opcode == Op::Discard) lastDiscard = static_cast<int>(i);
        lastReal = static_cast<int>(i);
        if (in.opcode == Op::SampleC || in.opcode == Op::SampleCLz) {
            m->candidate = true;
            if (in.operands.size() != 5) { Refuse(m, "sample_c: operand count"); return; }
            const Dxbc::Operand& ref = in.operands[4];
            if (ref.type == OT::Input) {
                if (ref.relative || ref.modifier != 0 || ref.numComp != 2 || ref.selMode != 2) {
                    Refuse(m, "sample_c: the compare value is not a plain input select");
                    return;
                }
                const int reg = static_cast<int>(ref.index[0]), comp = static_cast<int>(ref.compBits & 3);
                if (shadowIn >= 0 && (reg != shadowIn || comp != shadowComp)) {
                    Refuse(m, "sample_c: two different compare inputs");
                    return;
                }
                shadowIn = reg;
                shadowComp = comp;
            }
        }
        if (uvIn < 0 && in.opcode == Op::Sample && in.operands.size() == 4) {
            const Dxbc::Operand& uv = in.operands[1];
            if (uv.type == OT::Input && !uv.relative && uv.modifier == 0 && uv.numComp == 2 && uv.selMode == 1) {
                uvIn  = static_cast<int>(uv.index[0]);
                uvSwz = uv.compBits & 0xFF;
            }
        }
    }
    if (!m->candidate) { Refuse(m, "no sample_c"); return; }
    if (shadowIn < 0) { Refuse(m, "no sample_c compares an input directly"); return; }
    if (uvIn < 0) { Refuse(m, "no sample reads its coordinates from an input"); return; }
    if (uvIn == shadowIn) { Refuse(m, "the texcoord and the shadow input are one register"); return; }
    if (lastReal < 0 || p.instrs[static_cast<size_t>(lastReal)].opcode != Op::Ret || rets != 1) {
        Refuse(m, "ret is not the one last instruction");
        return;
    }
    if (m->temps == 0 || m->temps > 2048) { Refuse(m, "dcl_temps missing or too large"); return; }
    for (size_t i = 0; i < n; ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (!in.isDecl || in.opcode != kDclInputPs || in.length < 3) continue;
        const uint32_t tok = p.tokens[in.offset + 1];
        if (((tok >> 12) & 0xFF) != OT::Input || (tok & 3) != 2 || ((tok >> 2) & 3) != 0) continue;
        if (((tok >> 20) & 3) != 1 || ((tok >> 22) & 7) != 0) continue;   // one immediate index
        if (tok & Dxbc::kTokenExtended) continue;
        if (p.tokens[in.offset + 2] == static_cast<uint32_t>(shadowIn)) m->dclIdx = static_cast<int>(i);
    }
    if (m->dclIdx < 0) { Refuse(m, "the shadow input has no plain dcl_input_ps"); return; }
    {
        const int provided = IsgnMask(m->cont, static_cast<uint32_t>(shadowIn));
        if (provided < 0) { Refuse(m, "no ISGN element for the shadow input"); return; }
        if ((provided & static_cast<int>(Dxbc::Mask::W)) == 0) {
            Refuse(m, "the vertex stage does not write the shadow input's w (ISGN)");
            return;
        }
    }
    // THE PLACEMENT (edge_dither.h): after the last discard when every read of S follows it, else right
    // before the first read of S; a program without a discard keeps the block before its first instruction.
    for (size_t i = 0; i < n && m->firstShadowRead < 0; ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.isDecl) continue;
        for (const Dxbc::Operand& o : in.operands) {
            if (o.type == OT::Input && o.index[0] == static_cast<uint32_t>(shadowIn)) {
                m->firstShadowRead = static_cast<int>(i);
                break;
            }
        }
    }
    m->insertAt = m->firstInstr;
    if (lastDiscard >= 0) {
        m->insertAt = lastDiscard + 1;
        if (m->firstShadowRead >= 0 && m->firstShadowRead < m->insertAt) m->insertAt = m->firstShadowRead;
    }
    m->lastDiscard = lastDiscard;
    m->shadowIn   = static_cast<uint32_t>(shadowIn);
    m->shadowComp = static_cast<uint32_t>(shadowComp);
    m->uvIn       = static_cast<uint32_t>(uvIn);
    m->uvSwizzle  = uvSwz;
    m->matched    = true;
    m->why.clear();
}

namespace {

// Widens register S's read mask to xyzw (IsgnMask has the layout). False when the chunk is absent,
// malformed, lists no element for S, or S's w is not provided by the previous stage.
bool WidenInputSignature(Dxbc::Container& c, uint32_t reg) {
    Dxbc::Chunk* isgn = c.Find(Dxbc::FourCC('I', 'S', 'G', 'N'));
    if (isgn == nullptr || isgn->data.size() < 8) return false;
    uint32_t count;
    std::memcpy(&count, isgn->data.data(), 4);
    if (isgn->data.size() < 8 + 24ull * count) return false;
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* e = isgn->data.data() + 8 + 24 * i;
        uint32_t r;
        std::memcpy(&r, e + 16, 4);
        if (r != reg) continue;
        // w not written by the vertex stage: never declare a read of it
        if ((e[20] & Dxbc::Mask::W) == 0) return false;
        e[21] |= Dxbc::Mask::XYZW;   // readWriteMask
        found = true;
    }
    return found;
}

}  // namespace

const char* PlacementName(const Match& m) {
    if (m.lastDiscard < 0) return "first instruction";
    if (m.insertAt == m.lastDiscard + 1) return "after the discard";
    return "before the first read of S";
}

bool Transform(const Match& m, std::vector<uint8_t>* out, const BbsRewrite::Params& k) {
    if (!m.matched) return false;
    namespace E = Dxbc::Enc;
    const Dxbc::Program& p = m.prog;
    const uint32_t T = m.temps, N = m.temps + 1;
    const uint32_t S = m.shadowIn, U = m.uvIn, c = m.shadowComp;
    const uint32_t XY   = E::Swizzle(0, 1, 0, 0);
    const uint32_t XYZW = E::Swizzle(0, 1, 2, 3);
    std::vector<uint32_t> t;
    t.reserve(p.tokens.size() + 64);
    auto ins = [&](uint32_t opc, auto body) {
        const size_t at = t.size();
        t.push_back(0);
        body();
        t[at] = E::Opcode(opc, static_cast<uint32_t>(t.size() - at));
    };
    int retargeted = 0;
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        const uint32_t* src = p.tokens.data() + in.offset;
        if (in.isDecl) {
            const size_t at = t.size();
            t.insert(t.end(), src, src + in.length);
            if (in.opcode == Op::DclTemps) t[at + 1] = T + 2;
            if (static_cast<int>(i) == m.dclIdx) t[at + 1] |= Dxbc::Mask::XYZW << 4;   // dcl_input_ps vS.xyzw
            continue;
        }
        if (static_cast<int>(i) == m.insertAt) {
            ins(Op::Mov, [&] { E::TempDst(t, T, Dxbc::Mask::XYZW); InputSrc(t, S, XYZW); });
            ins(Op::Mul, [&] { E::TempDst(t, N, 3); InputSrc(t, U, m.uvSwizzle); E::Imm4(t, k.noiseScale, k.noiseScale, 0.0f, 0.0f); });
            ins(kRoundNi, [&] { E::TempDst(t, N, 3); E::TempSrc(t, N, XY); });
            ins(Op::Dp2, [&] { E::TempDst(t, N, 1); E::TempSrc(t, N, XY); E::Imm4(t, kHashU, kHashV, 0.0f, 0.0f); });
            ins(kSinCos, [&] { E::TempDst(t, N, 1); t.push_back(kOperandNull << 12); E::TempSrc(t, N, E::Rep(0)); });
            ins(Op::Mul, [&] { E::TempDst(t, N, 1); E::TempSrc(t, N, E::Rep(0)); E::ImmScalar(t, kHashMul); });
            ins(kFrc, [&] { E::TempDst(t, N, 1); E::TempSrc(t, N, E::Rep(0)); });
            ins(Op::Add, [&] { E::TempDst(t, N, 1); E::TempSrc(t, N, E::Rep(0)); E::ImmScalar(t, -0.5f); });
            ins(Op::Max, [&] { E::TempDst(t, N, 2); InputSrc(t, S, E::Rep(3), 1); E::ImmScalar(t, 0.0f); });
            // the extent is tiny (r x the depth per world unit, under 0.1): anything else in w is not ours
            ins(Op::Lt, [&] { E::TempDst(t, N, 4); E::TempSrc(t, N, E::Rep(1)); E::ImmScalar(t, kExtentLimit); });
            ins(Op::And, [&] { E::TempDst(t, N, 2); E::TempSrc(t, N, E::Rep(1)); E::TempSrc(t, N, E::Rep(2)); });
            ins(Op::Mul, [&] { E::TempDst(t, N, 1); E::TempSrc(t, N, E::Rep(0)); E::TempSrc(t, N, E::Rep(1)); });
            ins(Op::Mad, [&] { E::TempDst(t, T, 1u << c); E::TempSrc(t, N, E::Rep(0)); E::ImmScalar(t, k.noise);
                               E::TempSrc(t, T, E::Rep(c)); });
        }
        const size_t at = t.size();
        t.insert(t.end(), src, src + in.length);
        for (const Dxbc::Operand& o : in.operands) {
            if (o.type != OT::Input || o.relative || o.index[0] != S) continue;
            uint32_t* ot = t.data() + at + (o.offset - in.offset);
            const uint32_t slot = IndexSlot(ot, o.length);
            if (slot == 0) return false;
            ot[0] = (ot[0] & ~(0xFFu << 12)) | (OT::Temp << 12);
            ot[slot] = T;
            ++retargeted;
        }
    }
    if (retargeted == 0) return false;

    Dxbc::Program np;
    np.version = p.version;
    np.tokens  = std::move(t);
    Dxbc::Container cont = m.cont;
    if (!WidenInputSignature(cont, S)) return false;
    Dxbc::Chunk* shdr = cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = cont.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) return false;
    shdr->data = Dxbc::EmitProgram(np);
    std::vector<uint8_t> blob = Dxbc::Emit(cont);

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

}  // namespace BbsDither
