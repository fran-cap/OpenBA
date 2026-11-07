// ============================================================================
// src/render/common/draw_batch/smoke_vs_rewrite.cpp -- smoke_vs_rewrite.h
// ============================================================================
// NOT RETAIL (ext.draw_batch). docs/extensions/handins/DRAWBATCH.md.
// ============================================================================
#include "draw_batch/smoke_vs_rewrite.h"

#include <cstring>

namespace SmokeVs {

namespace {

namespace Op = Dxbc::Op;
namespace OT = Dxbc::OperandType;

constexpr uint32_t kOpFtou         = 28;   // D3D10_SB_OPCODE_FTOU
constexpr uint32_t kOpIadd         = 30;   // D3D10_SB_OPCODE_IADD
constexpr uint32_t kOpIshl         = 41;   // D3D10_SB_OPCODE_ISHL
constexpr uint32_t kOpDclInput     = 95;   // D3D10_SB_OPCODE_DCL_INPUT
constexpr uint32_t kOpDclInputSgv  = 96;   // D3D10_SB_OPCODE_DCL_INPUT_SGV
constexpr uint32_t kNameInstanceId = 8;    // D3D_NAME_INSTANCE_ID
constexpr uint32_t kTypeUint       = 1;    // D3D_REGISTER_COMPONENT_UINT32
constexpr uint32_t kTypeFloat      = 3;    // D3D_REGISTER_COMPONENT_FLOAT32
constexpr int      kAddedInstrs    = 2;

uint32_t Rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

struct SigElement {
    uint32_t sysValue  = 0;
    uint32_t compType  = 0;
    uint32_t reg       = 0;
    uint32_t mask      = 0;
    uint32_t rwMask    = 0;
    size_t   offset    = 0;   // in the chunk data
};

bool ReadSignature(const Dxbc::Chunk& c, std::vector<SigElement>* out) {
    if (c.data.size() < 8) return false;
    const uint32_t count = Rd32(c.data.data());
    const uint32_t first = Rd32(c.data.data() + 4);
    if (first + 24ull * count > c.data.size()) return false;
    out->clear();
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = first + 24ull * i;
        const uint8_t* e = c.data.data() + at;
        SigElement s;
        s.sysValue = Rd32(e + 8);
        s.compType = Rd32(e + 12);
        s.reg      = Rd32(e + 16);
        s.mask     = e[20];
        s.rwMask   = e[21];
        s.offset   = at;
        out->push_back(s);
    }
    return true;
}

// True when the source operand reads component `comp` (3 = w) of input register `reg`.
bool ReadsInputComponent(const Dxbc::Operand& o, uint32_t reg, uint32_t comp) {
    if (o.type != OT::Input || o.indexDim < 1 || o.index[0] != reg) return false;
    if (o.numComp != 2) return comp == 0;
    if (o.selMode == 0) return (o.compBits & (1u << comp)) != 0;
    for (int c = 0; c < 4; ++c) {
        if (o.Swz(c) == comp) return true;
    }
    return false;
}

void InputSrc(std::vector<uint32_t>& t, uint32_t reg, uint32_t swizzle) {
    t.push_back(2u | (1u << 2) | ((swizzle & 0xFF) << 4) | (OT::Input << 12) | (1u << 20));
    t.push_back(reg);
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
    m->program = true;
    if (m->prog.ProgramType() != 1) {
        m->why = "not a vertex program";
        return;
    }

    // ---- the input signature: the particle vertex + the instance id -------------------------
    const Dxbc::Chunk* isgn = m->cont.Find(Dxbc::FourCC('I', 'S', 'G', 'N'));
    std::vector<SigElement> sig;
    if (isgn == nullptr || !ReadSignature(*isgn, &sig)) {
        m->why = "no readable ISGN";
        return;
    }
    if (sig.size() != 5) {
        m->why = "input signature is not 5 elements";
        return;
    }
    static const uint32_t kMasks[4] = {Dxbc::Mask::XYZW, Dxbc::Mask::XYZW, Dxbc::Mask::XYZW, Dxbc::Mask::X};
    for (uint32_t r = 0; r < 4; ++r) {
        const SigElement& e = sig[r];
        if (e.reg != r || e.sysValue != 0 || e.compType != kTypeFloat || e.mask != kMasks[r]) {
            m->why = "input signature is not the particle vertex";
            return;
        }
    }
    if (sig[4].sysValue != kNameInstanceId || sig[4].compType != kTypeUint) {
        m->why = "no SV_InstanceID as the fifth input";
        return;
    }
    if ((sig[0].rwMask & Dxbc::Mask::W) != 0) {
        m->why = "v0.w is read";
        return;
    }
    m->iidReg = sig[4].reg;
    m->isgnV0 = sig[0].offset;

    // ---- the declarations and the one instance-id read ---------------------------------------
    bool cbOk = false;
    bool sgvOk = false;
    int  iidReads = 0;
    for (size_t i = 0; i < m->prog.instrs.size(); ++i) {
        const Dxbc::Instr& in = m->prog.instrs[i];
        const uint32_t* t = m->prog.tokens.data() + in.offset;
        if (in.isDecl) {
            if (in.opcode == kOpDclInput && in.length == 3) {
                const uint32_t tok = t[1];
                const bool input = ((tok >> 12) & 0xFF) == OT::Input && (tok & 3) == 2 && ((tok >> 2) & 3) == 0;
                if (input && t[2] == 0) {
                    if ((tok >> 4) & Dxbc::Mask::W) {
                        m->why = "dcl_input v0 declares w";
                        return;
                    }
                    m->dclInputIdx = static_cast<int>(i);
                }
            } else if (in.opcode == kOpDclInputSgv && in.length == 4 && t[2] == m->iidReg && t[3] == kNameInstanceId) {
                sgvOk = true;
            } else if (in.opcode == Op::DclConstantBuffer && in.length == 4) {
                const bool dynamic = ((in.token >> 11) & 1) != 0;
                if (dynamic && t[3] == kTransformCbFloat4) cbOk = true;
            }
            continue;
        }
        for (size_t k = 0; k < in.operands.size(); ++k) {
            const Dxbc::Operand& o = in.operands[k];
            if (k > 0 && ReadsInputComponent(o, 0, 3)) {
                m->why = "an instruction reads v0.w";
                return;
            }
            if (o.type == OT::Input && o.indexDim >= 1 && o.index[0] == m->iidReg) {
                ++iidReads;
                m->shiftIdx = static_cast<int>(i);
            }
        }
    }
    if (m->dclInputIdx < 0) {
        m->why = "no dcl_input v0";
        return;
    }
    if (!sgvOk) {
        m->why = "no dcl_input_sgv instanceid";
        return;
    }
    if (!cbOk) {
        m->why = "no dynamically indexed cb of 208 float4";
        return;
    }
    if (iidReads != 1) {
        m->why = "the instance id is not read exactly once";
        return;
    }
    const Dxbc::Instr& sh = m->prog.instrs[static_cast<size_t>(m->shiftIdx)];
    if (sh.opcode != kOpIshl || sh.Saturate() || (sh.token & Dxbc::kTokenExtended) != 0 || sh.operands.size() != 3) {
        m->why = "the instance-id read is not a plain ishl";
        return;
    }
    const Dxbc::Operand& dst = sh.operands[0];
    const Dxbc::Operand& src = sh.operands[1];
    const Dxbc::Operand& amt = sh.operands[2];
    const uint32_t mask = dst.Mask();
    const bool oneBit = mask == 1 || mask == 2 || mask == 4 || mask == 8;
    if (dst.type != OT::Temp || dst.relative || dst.numComp != 2 || dst.selMode != 0 || !oneBit ||
        src.type != OT::Input || src.index[0] != m->iidReg || src.modifier != 0 || src.relative ||
        amt.type != OT::Imm32 || amt.imm[0] != 2) {
        m->why = "the ishl is not `ishl rT.c, vI, l(2)`";
        return;
    }
    m->matched = true;
}

bool Transform(const Match& m, std::vector<uint8_t>* out) {
    if (!m.matched) return false;
    const Dxbc::Program& p = m.prog;
    std::vector<uint32_t> t;
    t.reserve(p.tokens.size() + 16);
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        const uint32_t* src = p.tokens.data() + in.offset;
        if (static_cast<int>(i) == m.dclInputIdx) {
            t.insert(t.end(), src, src + in.length);
            t[t.size() - 2] |= (Dxbc::Mask::XYZW << 4);   // dcl_input v0.xyzw
            continue;
        }
        if (static_cast<int>(i) != m.shiftIdx) {
            t.insert(t.end(), src, src + in.length);
            continue;
        }
        const Dxbc::Operand& dst = in.operands[0];
        const Dxbc::Operand& iid = in.operands[1];
        const Dxbc::Operand& amt = in.operands[2];
        const uint32_t mask = dst.Mask();
        uint32_t comp = 0;
        while (((mask >> comp) & 1) == 0) ++comp;
        const uint32_t reg = dst.index[0];

        // ftou rT.c, v0.wwww
        size_t at = t.size();
        t.push_back(0);
        Dxbc::Enc::Copy(t, p, dst);
        InputSrc(t, 0, Dxbc::Enc::Rep(3));
        t[at] = Dxbc::Enc::Opcode(kOpFtou, static_cast<uint32_t>(t.size() - at));
        // iadd rT.c, rT.cccc, vI (retail's operand)
        at = t.size();
        t.push_back(0);
        Dxbc::Enc::Copy(t, p, dst);
        Dxbc::Enc::TempSrc(t, reg, Dxbc::Enc::Rep(comp));
        Dxbc::Enc::Copy(t, p, iid);
        t[at] = Dxbc::Enc::Opcode(kOpIadd, static_cast<uint32_t>(t.size() - at));
        // ishl rT.c, rT.cccc, l(2) (retail's immediate)
        at = t.size();
        t.push_back(0);
        Dxbc::Enc::Copy(t, p, dst);
        Dxbc::Enc::TempSrc(t, reg, Dxbc::Enc::Rep(comp));
        Dxbc::Enc::Copy(t, p, amt);
        t[at] = Dxbc::Enc::Opcode(kOpIshl, static_cast<uint32_t>(t.size() - at));
    }

    Dxbc::Program np;
    np.version = p.version;
    np.tokens  = std::move(t);
    Dxbc::Container c = m.cont;
    Dxbc::Chunk* shdr = c.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = c.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    Dxbc::Chunk* isgn = c.Find(Dxbc::FourCC('I', 'S', 'G', 'N'));
    if (shdr == nullptr || isgn == nullptr || m.isgnV0 + 22 > isgn->data.size()) return false;
    shdr->data = Dxbc::EmitProgram(np);
    isgn->data[m.isgnV0 + 21] |= Dxbc::Mask::W;   // v0's read mask gains w
    std::vector<uint8_t> blob = Dxbc::Emit(c);

    // Self-check: the copy parses, its checksum verifies, and it has exactly two instructions more.
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

}  // namespace SmokeVs
