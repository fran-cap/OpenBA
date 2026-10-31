// ============================================================================
// src/render/common/better_trees/natural_growth.cpp -- natural_growth.h
// ============================================================================
// NOT RETAIL (ext.better_trees). Standard C++ only: the unit tests link it.
// ============================================================================
#include "better_trees/natural_growth.h"

#include "better_trees/blossom.h"   // the spring blossom's constants (BLOSSOM.md)

#include <cmath>
#include <cstring>

namespace NgRewrite {
namespace {

namespace Op = Dxbc::Op;
namespace OT = Dxbc::OperandType;
namespace E  = Dxbc::Enc;

// D3D10_SB_OPCODE_TYPE values beyond Dxbc::Op.
enum : uint32_t {
    kBreak = 2, kBreakC = 3, kCall = 4, kCallC = 5, kCase = 6, kContinue = 7, kContinueC = 8, kCut = 9,
    kDefault = 10, kDp3 = 16, kElse = 18, kEmit = 19, kEmitThenCut = 20, kEndIf = 21, kEndLoop = 22,
    kEndSwitch = 23, kFrc = 26, kIf = 31, kLabel = 44, kLoop = 48, kRetC = 63, kRoundNi = 65, kSwitch = 76,
    kSinCos = 77,
    kDclInputPs = 98, kDclOutput = 101, kDclOutputSgv = 102, kDclOutputSiv = 103, kDclIndexableTemp = 105,
};
constexpr uint32_t kOperandNull = 13;   // D3D10_SB_OPERAND_TYPE_NULL
constexpr uint32_t kGlobalCb = 0, kUserPositionRow = 9, kTimeRow = 10;   // gc.userPosition, gc.time
constexpr uint32_t kPixelCb = 0, kPixelUserRow = 4;                       // pc.userPosition
constexpr uint32_t kTransformCb = 3, kTranslationRow = 3;                 // tc.transforms[i] row 3
constexpr float    kSway = 0.025f;                                        // the billboard / foliage sway

const uint32_t XYZW = E::Swizzle(0, 1, 2, 3);
const uint32_t XYZX = E::Swizzle(0, 1, 2, 0);
const uint32_t XYXX = E::Swizzle(0, 1, 0, 0);

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

void Refuse(Match* m, const std::string& why) {
    m->kind = Kind::None;
    m->why  = why;
}

bool IsCb(const Dxbc::Operand& o, uint32_t slot, uint32_t row) {
    return o.type == OT::ConstantBuffer && o.indexDim == 2 && !o.relative && o.index[0] == slot && o.index[1] == row;
}

// Every component the operand reads is `c` (a select, or a swizzle replicating it).
bool ReadsOnly(const Dxbc::Operand& o, uint32_t c) {
    if (o.numComp != 2) return false;
    if (o.selMode == 2) return (o.compBits & 3) == c;
    if (o.selMode == 1) return o.Swz(0) == c && o.Swz(1) == c && o.Swz(2) == c && o.Swz(3) == c;
    return false;
}

// The single component a scalar read takes (a select, or swizzle .x of a replicated one).
bool ScalarComp(const Dxbc::Operand& o, uint32_t* c) {
    if (o.numComp != 2) return false;
    if (o.selMode == 2) {
        *c = o.compBits & 3;
        return true;
    }
    if (o.selMode == 1) {
        *c = o.Swz(0);
        return true;
    }
    return false;
}

bool ImmIs(const Dxbc::Operand& o, int comp, float v) {
    return o.type == OT::Imm32 && std::fabs(o.ImmF(comp) - v) < 1e-4f;
}

bool IsTempScalarDst(const Dxbc::Operand& o, uint32_t reg, uint32_t mask) {
    return o.type == OT::Temp && !o.relative && o.index[0] == reg && o.selMode == 0 && o.Mask() == mask;
}

// ---- the signature chunks (ISGN / OSGN): u32 count, u32 8, 24-byte elements {nameOffset, semanticIndex,
// systemValue, componentType, register, u8 mask, u8 rw, u16 pad}, then the names. `rw` is the read mask
// in an ISGN, the never-written mask in an OSGN.
struct SigElem {
    uint32_t nameOffset, semIndex, sysValue, compType, reg;
    uint8_t  mask, rw;
    std::string name;
};

bool ReadSig(const Dxbc::Container& c, uint32_t fourcc, std::vector<SigElem>* out) {
    const Dxbc::Chunk* ch = c.Find(fourcc);
    if (ch == nullptr || ch->data.size() < 8) return false;
    uint32_t count;
    std::memcpy(&count, ch->data.data(), 4);
    if (ch->data.size() < 8 + 24ull * count) return false;
    out->clear();
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = ch->data.data() + 8 + 24 * i;
        SigElem s;
        std::memcpy(&s.nameOffset, e + 0, 4);
        std::memcpy(&s.semIndex, e + 4, 4);
        std::memcpy(&s.sysValue, e + 8, 4);
        std::memcpy(&s.compType, e + 12, 4);
        std::memcpy(&s.reg, e + 16, 4);
        s.mask = e[20];
        s.rw   = e[21];
        if (s.nameOffset >= ch->data.size()) return false;
        const char* nm = reinterpret_cast<const char*>(ch->data.data() + s.nameOffset);  // CAST-OK: byte_io
        s.name.assign(nm, strnlen(nm, ch->data.size() - s.nameOffset));
        out->push_back(s);
    }
    return true;
}

// The next free register and TEXCOORD index after a signature's elements.
bool NextSlot(const std::vector<SigElem>& sig, uint32_t* reg, uint32_t* semIx) {
    uint32_t r = 0, s = 0;
    bool any = false;
    for (const SigElem& e : sig) {
        if (e.reg != 0xFFFFFFFFu && e.reg + 1 > r) r = e.reg + 1;
        if (e.name == "TEXCOORD") {
            if (e.semIndex + 1 > s) s = e.semIndex + 1;
            any = true;
        }
    }
    *reg = r;
    *semIx = s;
    return any;
}

// Appends a float4 TEXCOORD element at (reg, semIx), every component provided (and, in an ISGN, read).
bool AppendElement(Dxbc::Container& c, uint32_t fourcc, uint32_t reg, uint32_t semIx) {
    Dxbc::Chunk* ch = c.Find(fourcc);
    if (ch == nullptr || ch->data.size() < 8) return false;
    std::vector<uint8_t>& d = ch->data;
    uint32_t count;
    std::memcpy(&count, d.data(), 4);
    const size_t table = 8 + 24ull * count;
    if (d.size() < table) return false;
    int tex = -1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t no;
        std::memcpy(&no, d.data() + 8 + 24 * i, 4);
        if (no < table || no >= d.size()) return false;   // names live after the table
        if (std::strncmp(reinterpret_cast<const char*>(d.data() + no), "TEXCOORD", 9) == 0) tex = static_cast<int>(i);  // CAST-OK: byte_io
    }
    if (tex < 0) return false;
    uint8_t e[24];
    std::memcpy(e, d.data() + 8 + 24 * tex, 24);
    std::memcpy(e + 4, &semIx, 4);
    std::memcpy(e + 16, &reg, 4);
    e[20] = 0xF;
    e[21] = fourcc == Dxbc::FourCC('I', 'S', 'G', 'N') ? 0xF : 0x0;
    d.insert(d.begin() + static_cast<std::ptrdiff_t>(table), e, e + 24);
    ++count;
    std::memcpy(d.data(), &count, 4);
    for (uint32_t i = 0; i < count; ++i) {   // every name moved 24 bytes down
        uint32_t no;
        std::memcpy(&no, d.data() + 8 + 24 * i, 4);
        no += 24;
        std::memcpy(d.data() + 8 + 24 * i, &no, 4);
    }
    return true;
}

// Widens the element at `reg` to xyzw (an OSGN: provided, all written; an ISGN: provided, all read).
bool WidenElement(Dxbc::Container& c, uint32_t fourcc, uint32_t reg) {
    Dxbc::Chunk* ch = c.Find(fourcc);
    if (ch == nullptr || ch->data.size() < 8) return false;
    uint32_t count;
    std::memcpy(&count, ch->data.data(), 4);
    if (ch->data.size() < 8 + 24ull * count) return false;
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* e = ch->data.data() + 8 + 24 * i;
        uint32_t r;
        std::memcpy(&r, e + 16, 4);
        if (r != reg) continue;
        e[20] = 0xF;
        e[21] = fourcc == Dxbc::FourCC('I', 'S', 'G', 'N') ? static_cast<uint8_t>(e[21] | 0x8)
                                                           : static_cast<uint8_t>(e[21] & ~0x8);
        found = true;
    }
    return found;
}

// ---- token emitters -------------------------------------------------------------------------------
void InputSrc(std::vector<uint32_t>& t, uint32_t reg, uint32_t swz, uint32_t modifier = 0) {
    uint32_t tok = 2u | (1u << 2) | ((swz & 0xFF) << 4) | (OT::Input << 12) | (1u << 20);
    if (modifier) tok |= 0x80000000u;
    t.push_back(tok);
    if (modifier) t.push_back(1u | (modifier << 6));
    t.push_back(reg);
}

void CbSrc(std::vector<uint32_t>& t, uint32_t slot, uint32_t row, uint32_t swz) {
    t.push_back(2u | (1u << 2) | ((swz & 0xFF) << 4) | (OT::ConstantBuffer << 12) | (2u << 20));
    t.push_back(slot);
    t.push_back(row);
}

struct Writer {
    std::vector<uint32_t> t;
    int added = 0;
    template <class F> void Ins(uint32_t opc, bool sat, F body) {
        const size_t at = t.size();
        t.push_back(0);
        body();
        t[at] = E::Opcode(opc, static_cast<uint32_t>(t.size() - at), sat);
        ++added;
    }
    template <class F> void Ins(uint32_t opc, F body) { Ins(opc, false, body); }
    // `in` again with operand `op` replaced by `repl`'s tokens (a re-encode: the length is recomputed).
    void ReEmit(const Dxbc::Program& p, const Dxbc::Instr& in, int op, const std::vector<uint32_t>& repl) {
        const size_t at = t.size();
        const uint32_t head = in.operands.empty() ? in.length : in.operands[0].offset - in.offset;
        t.insert(t.end(), p.tokens.begin() + in.offset, p.tokens.begin() + in.offset + head);
        for (size_t k = 0; k < in.operands.size(); ++k) {
            if (static_cast<int>(k) == op) t.insert(t.end(), repl.begin(), repl.end());
            else E::Copy(t, p, in.operands[k]);
        }
        const uint32_t len = static_cast<uint32_t>(t.size() - at);
        t[at] = (t[at] & ~(0x7Fu << 24)) | ((len & 0x7F) << 24);
    }
};

// Re-parses `blob`, verifies its checksum and its instruction count.
bool SelfCheck(const std::vector<uint8_t>& blob, uint32_t shdrCc, size_t instrs) {
    Dxbc::Container rc;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &rc, &err)) return false;
    uint32_t sum[4];
    Dxbc::Checksum(blob.data(), blob.size(), sum);
    if (std::memcmp(sum, rc.checksum, 16) != 0) return false;
    const Dxbc::Chunk* rs = rc.Find(shdrCc);
    Dxbc::Program rp;
    if (rs == nullptr || !Dxbc::ParseProgram(*rs, &rp, &err)) return false;
    return rp.instrs.size() == instrs;
}

// The common head of Analyse: container, program, the generic rules. False = refused.
bool Head(const uint8_t* blob, size_t size, Match* m) {
    std::string err;
    if (!Dxbc::Parse(blob, size, &m->cont, &err)) {
        Refuse(m, "container: " + err);
        return false;
    }
    m->container = true;
    const Dxbc::Chunk* shdr = m->cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = m->cont.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) {
        m->container = false;
        Refuse(m, "no SHDR / SHEX chunk");
        return false;
    }
    const std::vector<uint8_t> again = Dxbc::Emit(m->cont);
    if (again.size() != size || std::memcmp(again.data(), blob, size) != 0) {
        Refuse(m, "container does not round-trip");
        return false;
    }
    if (!Dxbc::ParseProgram(*shdr, &m->prog, &err)) {
        Refuse(m, "program: " + err);
        return false;
    }
    m->program = true;
    m->vertex = m->prog.ProgramType() == 1;
    if (m->prog.ProgramType() > 1) {
        Refuse(m, "neither a vertex nor a pixel shader");
        return false;
    }
    if (m->prog.Major() != 4) {
        Refuse(m, "not shader model 4");
        return false;
    }
    int lastReal = -1, rets = 0;
    for (size_t i = 0; i < m->prog.instrs.size(); ++i) {
        const Dxbc::Instr& in = m->prog.instrs[i];
        if (in.isDecl) {
            if (in.opcode == Op::DclTemps && in.length >= 2) m->temps = m->prog.tokens[in.offset + 1];
            if (in.opcode == kDclIndexableTemp) {
                Refuse(m, "indexable temps");
                return false;
            }
            continue;
        }
        if (IsFlowControl(in.opcode)) {
            Refuse(m, "flow control");
            return false;
        }
        if (in.opcode == Op::Ret) ++rets;
        lastReal = static_cast<int>(i);
    }
    if (lastReal < 0 || m->prog.instrs[static_cast<size_t>(lastReal)].opcode != Op::Ret || rets != 1) {
        Refuse(m, "ret is not the one last instruction");
        return false;
    }
    if (m->temps > 2048) {
        Refuse(m, "dcl_temps too large");
        return false;
    }
    return true;
}

// ---- vertex ---------------------------------------------------------------------------------------
void AnalyseVertex(Match* m) {
    const Dxbc::Program& p = m->prog;
    bool sway = false, step = false, readsUser = false;
    int foliageIdx = -1;
    uint32_t foliageReg = 0;
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.isDecl) {
            if (in.opcode == kDclOutput || in.opcode == kDclOutputSgv || in.opcode == kDclOutputSiv)
                m->lastDclOut = static_cast<int>(i);
            continue;
        }
        for (size_t k = 0; k < in.operands.size(); ++k) {
            const Dxbc::Operand& o = in.operands[k];
            if (ImmIs(o, 0, kSway)) sway = true;
            if (o.type == OT::Imm32 && o.imm[0] == 0x3f800000u && in.opcode == Op::And) step = true;
            if (m->xlateIdx < 0 && o.type == OT::ConstantBuffer && o.relative && o.indexDim == 2 &&
                o.index[0] == kTransformCb && o.index[1] == kTranslationRow) {
                m->xlateIdx = static_cast<int>(i);
                m->xlateOp  = static_cast<int>(k);
            }
            if (k == 0 || !IsCb(o, kGlobalCb, kUserPositionRow)) continue;
            readsUser = true;
            const Dxbc::Operand& dst = in.operands[0];
            if (dst.type != OT::Output || dst.relative) continue;
            if (in.opcode == Op::Mul && ReadsOnly(o, 2) && m->depthMulIdx < 0) {   // fadeB * move -> the caster
                m->depthMulIdx = static_cast<int>(i);
                m->depthMulOp  = static_cast<int>(k);
            }
            if (in.opcode == Op::Mul && ReadsOnly(o, 0) && dst.selMode == 0 && dst.Mask() == 0x4) {   // n.z * snow
                foliageIdx = static_cast<int>(i);
                foliageReg = dst.index[0];
            }
        }
    }
    m->candidate = sway && m->xlateIdx >= 0;
    if (!sway) return Refuse(m, "no sway immediate (not a tree program)");
    if (m->xlateIdx < 0) return Refuse(m, "no read of the instance translation cb3[rI + 3]");
    if (m->lastDclOut < 0) return Refuse(m, "no dcl_output");
    std::vector<SigElem> osgn;
    if (!ReadSig(m->cont, Dxbc::FourCC('O', 'S', 'G', 'N'), &osgn)) return Refuse(m, "no readable OSGN");
    if (m->depthMulIdx >= 0) {
        if (m->depthMulIdx < m->xlateIdx) return Refuse(m, "depth: the caster's fadeB is read before the translation");
        m->kind = Kind::BillboardDepth;
        return;
    }
    if (foliageIdx >= 0) {
        for (const SigElem& e : osgn) {
            if (e.reg != foliageReg) continue;
            if (e.mask != 0x7) return Refuse(m, "foliage: the snow interpolant is not xyz");
            m->snowReg = foliageReg;
        }
        if (m->snowReg != foliageReg) return Refuse(m, "foliage: no OSGN element for the snow interpolant");
        for (size_t i = 0; i < p.instrs.size(); ++i) {
            const Dxbc::Instr& in = p.instrs[i];
            if (in.isDecl && in.opcode == kDclOutput && in.length == 3 && p.tokens[in.offset + 2] == foliageReg)
                m->dclIdx = static_cast<int>(i);
        }
        if (m->dclIdx < 0) return Refuse(m, "foliage: no plain dcl_output for the snow interpolant");
        m->kind = Kind::Foliage;
        return;
    }
    if (readsUser) return Refuse(m, "reads userPosition in a shape that is not ours");
    if (!step) return Refuse(m, "no move step (and l(1.0)): not a billboard colour program");
    if (!NextSlot(osgn, &m->newReg, &m->newSemIx)) return Refuse(m, "no TEXCOORD output");
    if (m->newReg > 14) return Refuse(m, "no two free output registers");   // the terms + the blossom
    m->kind = Kind::BillboardColour;
}

// The per-tree block, before m.xlateIdx. Temps A..D = n..n+3. Results: A.y c, A.z l, B.y hue, D.w t.
void EmitTerms(Writer& w, const Match& m, const Curves& cv) {
    const Dxbc::Program& p = m.prog;
    const Dxbc::Operand& T = p.instrs[static_cast<size_t>(m.xlateIdx)].operands[static_cast<size_t>(m.xlateOp)];
    const uint32_t A = m.temps, B = m.temps + 1, C = m.temps + 2, D = m.temps + 3;
    auto& t = w.t;
    // hash33(T.xy)
    w.Ins(Op::Mul, [&] { E::TempDst(t, A, 7); E::CopyWithSwizzle(t, p, T, XYXX); E::Imm4(t, kHashK[0], kHashK[1], kHashK[2], 0.0f); });
    w.Ins(kFrc, [&] { E::TempDst(t, A, 7); E::TempSrc(t, A, XYZX); });
    w.Ins(Op::Add, [&] { E::TempDst(t, B, 7); E::TempSrc(t, A, E::Swizzle(1, 0, 2, 1)); E::Imm4(t, kHashAdd, kHashAdd, kHashAdd, 0.0f); });
    w.Ins(kDp3, [&] { E::TempDst(t, B, 1); E::TempSrc(t, A, XYZX); E::TempSrc(t, B, XYZX); });
    w.Ins(Op::Add, [&] { E::TempDst(t, A, 7); E::TempSrc(t, A, XYZX); E::TempSrc(t, B, E::Rep(0)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, B, 7); E::TempSrc(t, A, E::Swizzle(0, 0, 1, 0)); E::TempSrc(t, A, E::Swizzle(1, 2, 2, 1)); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, B, 7); E::TempSrc(t, B, XYZX); E::TempSrc(t, A, E::Swizzle(2, 1, 0, 2)); });
    w.Ins(kFrc, [&] { E::TempDst(t, B, 7); E::TempSrc(t, B, XYZX); });
    // the grove term
    w.Ins(Op::Dp2, [&] { E::TempDst(t, C, 1); E::CopyWithSwizzle(t, p, T, XYXX); E::Imm4(t, kGroveA[0], kGroveA[1], 0.0f, 0.0f); });
    w.Ins(Op::Dp2, [&] { E::TempDst(t, C, 2); E::CopyWithSwizzle(t, p, T, XYXX); E::Imm4(t, kGroveB[0], kGroveB[1], 0.0f, 0.0f); });
    w.Ins(Op::Add, [&] { E::TempDst(t, C, 2); E::TempSrc(t, C, E::Rep(1)); E::ImmScalar(t, kGroveBPhase); });
    w.Ins(kSinCos, [&] { E::TempDst(t, C, 3); t.push_back(kOperandNull << 12); E::TempSrc(t, C, XYXX); });
    w.Ins(Op::Add, [&] { E::TempDst(t, C, 1); E::TempSrc(t, C, E::Rep(0)); E::TempSrc(t, C, E::Rep(1)); });
    // the green variation
    w.Ins(Op::Mad, [&] { E::TempDst(t, D, 8); E::TempSrc(t, B, E::Rep(2)); E::ImmScalar(t, 2.0f); E::ImmScalar(t, -1.0f); });
    if (m.kind == Kind::Foliage) return;
    // the tree's autumn offset o (D.x) and regrowth start s (D.y)
    const float a = cv.autumnSpread, s = cv.springSpread;
    w.Ins(Op::Mad, [&] { E::TempDst(t, D, 1); E::TempSrc(t, B, E::Rep(0)); E::ImmScalar(t, 1.2f * a); E::ImmScalar(t, -0.6f * a); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, D, 1); E::TempSrc(t, C, E::Rep(0)); E::ImmScalar(t, 0.2f * a); E::TempSrc(t, D, E::Rep(0)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, D, 2); E::TempSrc(t, B, E::Rep(0)); E::TempSrc(t, B, E::Rep(2)); });
    w.Ins(kFrc, [&] { E::TempDst(t, D, 2); E::TempSrc(t, D, E::Rep(1)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, D, 2); E::TempSrc(t, D, E::Rep(1)); E::ImmScalar(t, 1.2f * s); E::ImmScalar(t, cv.springStart - 0.6f * s); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, D, 2); E::TempSrc(t, C, E::Rep(0)); E::ImmScalar(t, 0.2f * s); E::TempSrc(t, D, E::Rep(1)); });
    // d = time.y - 1 (D.z); c (A.y), the autumn loss (A.z), the spring loss (A.w), l = max (A.z)
    w.Ins(Op::Add, [&] { E::TempDst(t, D, 4); CbSrc(t, kGlobalCb, kTimeRow, E::Rep(1)); E::ImmScalar(t, -kStampBase); });
    w.Ins(Op::Add, [&] { E::TempDst(t, A, 1); E::TempSrc(t, D, E::Rep(2)); E::TempSrc(t, D, E::Rep(0), 1); });
    w.Ins(Op::Mad, true, [&] { E::TempDst(t, A, 2); E::TempSrc(t, A, E::Rep(0)); E::ImmScalar(t, 1.0f / cv.colourLength);
                               E::ImmScalar(t, -cv.colourStart / cv.colourLength); });
    w.Ins(Op::Mad, true, [&] { E::TempDst(t, A, 4); E::TempSrc(t, A, E::Rep(0)); E::ImmScalar(t, 1.0f / cv.fallLength);
                               E::ImmScalar(t, -cv.fallStart / cv.fallLength); });
    w.Ins(Op::Add, [&] { E::TempDst(t, A, 8); E::TempSrc(t, D, E::Rep(2)); E::TempSrc(t, D, E::Rep(1), 1); });
    w.Ins(Op::Mul, true, [&] { E::TempDst(t, A, 8); E::TempSrc(t, A, E::Rep(3)); E::ImmScalar(t, 1.0f / cv.growLength); });
    w.Ins(Op::Add, [&] { E::TempDst(t, A, 8); E::TempSrc(t, A, E::Rep(3), 1); E::ImmScalar(t, 1.0f); });
    w.Ins(Op::Max, [&] { E::TempDst(t, A, 4); E::TempSrc(t, A, E::Rep(2)); E::TempSrc(t, A, E::Rep(3)); });
    // no stamp (time.y < 0.5): retail's fadeA / fadeB and gold
    w.Ins(Op::Ge, [&] { E::TempDst(t, A, 1); CbSrc(t, kGlobalCb, kTimeRow, E::Rep(1)); E::ImmScalar(t, 0.5f); });
    w.Ins(Op::MovC, [&] { E::TempDst(t, A, 2); E::TempSrc(t, A, E::Rep(0)); E::TempSrc(t, A, E::Rep(1)); CbSrc(t, kGlobalCb, kUserPositionRow, E::Rep(1)); });
    w.Ins(Op::MovC, [&] { E::TempDst(t, A, 4); E::TempSrc(t, A, E::Rep(0)); E::TempSrc(t, A, E::Rep(2)); CbSrc(t, kGlobalCb, kUserPositionRow, E::Rep(2)); });
    w.Ins(Op::MovC, [&] { E::TempDst(t, B, 2); E::TempSrc(t, A, E::Rep(0)); E::TempSrc(t, B, E::Rep(1)); E::ImmScalar(t, 0.0f); });
}

// The blossom (blossom.h "THE VERTEX TERMS"), BillboardColour only, after EmitTerms: reads A.x (the stamp
// test), B.xy (h), C.x (g), D.z (d) and gc.time.zw; temps E, F = n+4, n+5; writes o(N2) = (r, g, b, cov).
// Blossom::VertexOut is the same arithmetic in the same order.
static_assert(Blossom::kBudTint[0] == 0.0f, "EmitBloomVertex leaves R untinted");
void EmitBloomVertex(Writer& w, const Match& m, uint32_t n2) {
    namespace Bl = Blossom;
    const uint32_t A = m.temps, B = m.temps + 1, C = m.temps + 2, D = m.temps + 3, Ev = m.temps + 4, F = m.temps + 5;
    auto& t = w.t;
    // decode z: q (E.x), rem (E.y), s (E.z), peak + 1 (E.w)
    w.Ins(Op::Mul, [&] { E::TempDst(t, Ev, 1); CbSrc(t, kGlobalCb, kTimeRow, E::Rep(2)); E::ImmScalar(t, 1.0f / 64.0f); });
    w.Ins(kRoundNi, [&] { E::TempDst(t, Ev, 1); E::TempSrc(t, Ev, E::Rep(0)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, Ev, 2); E::TempSrc(t, Ev, E::Rep(0)); E::ImmScalar(t, -64.0f); CbSrc(t, kGlobalCb, kTimeRow, E::Rep(2)); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, Ev, 4); E::TempSrc(t, Ev, E::Rep(1)); E::ImmScalar(t, 1.0f / 16.0f); });
    w.Ins(kRoundNi, [&] { E::TempDst(t, Ev, 4); E::TempSrc(t, Ev, E::Rep(2)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, Ev, 8); E::TempSrc(t, Ev, E::Rep(2)); E::ImmScalar(t, -16.0f); E::TempSrc(t, Ev, E::Rep(1)); });
    // valid (F.x) = a code and a stamp
    w.Ins(Op::Ge, [&] { E::TempDst(t, F, 1); E::TempSrc(t, Ev, E::Rep(3)); E::ImmScalar(t, 0.5f); });
    w.Ins(Op::And, [&] { E::TempDst(t, F, 1); E::TempSrc(t, F, E::Rep(0)); E::TempSrc(t, A, E::Rep(0)); });
    // the tree's offset j (F.y), the wrapped distance from its peak x (F.z)
    w.Ins(Op::Add, [&] { E::TempDst(t, F, 2); E::TempSrc(t, B, E::Rep(0)); E::TempSrc(t, B, E::Rep(1)); });
    w.Ins(kFrc, [&] { E::TempDst(t, F, 2); E::TempSrc(t, F, E::Rep(1)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, F, 2); E::TempSrc(t, F, E::Rep(1)); E::ImmScalar(t, 2.0f * Bl::kJitter); E::ImmScalar(t, -Bl::kJitter); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, F, 2); E::TempSrc(t, C, E::Rep(0)); E::ImmScalar(t, Bl::kGroveShift); E::TempSrc(t, F, E::Rep(1)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, F, 4); E::TempSrc(t, D, E::Rep(2)); E::TempSrc(t, Ev, E::Rep(3), 1); });
    w.Ins(Op::Add, [&] { E::TempDst(t, F, 4); E::TempSrc(t, F, E::Rep(2)); E::TempSrc(t, F, E::Rep(1), 1); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, F, 4); E::TempSrc(t, F, E::Rep(2)); E::ImmScalar(t, 1.0f / 12.0f); E::ImmScalar(t, 7.0f / 12.0f); });
    w.Ins(kFrc, [&] { E::TempDst(t, F, 4); E::TempSrc(t, F, E::Rep(2)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, F, 4); E::TempSrc(t, F, E::Rep(2)); E::ImmScalar(t, 12.0f); E::ImmScalar(t, -6.0f); });
    // the bloom b (F.w), zero without a valid code; open (F.y)
    w.Ins(Op::Mad, true, [&] { E::TempDst(t, F, 8); E::TempSrc(t, F, E::Rep(2), 2); E::ImmScalar(t, -Bl::kPlateau / Bl::kHalfWidth);
                               E::ImmScalar(t, Bl::kPlateau); });
    w.Ins(Op::MovC, [&] { E::TempDst(t, F, 8); E::TempSrc(t, F, E::Rep(0)); E::TempSrc(t, F, E::Rep(3)); E::ImmScalar(t, 0.0f); });
    w.Ins(Op::Mad, true, [&] { E::TempDst(t, F, 2); E::TempSrc(t, F, E::Rep(2)); E::ImmScalar(t, 1.0f / Bl::kHalfWidth); E::ImmScalar(t, 1.0f); });
    // cov = b * density, negative for catkins; the pink-bud amount (F.y) = pink * (1 - open)
    w.Ins(Op::Mad, [&] { E::TempDst(t, Ev, 1); E::TempSrc(t, Ev, E::Rep(0)); E::ImmScalar(t, 1.0f / 16.0f); E::ImmScalar(t, 1.0f / 16.0f); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, F, 8); E::TempSrc(t, F, E::Rep(3)); E::TempSrc(t, Ev, E::Rep(0)); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, Ev, 4); E::TempSrc(t, Ev, E::Rep(2)); E::ImmScalar(t, 0.5f); });
    w.Ins(kFrc, [&] { E::TempDst(t, Ev, 2); E::TempSrc(t, Ev, E::Rep(2)); });
    w.Ins(kRoundNi, [&] { E::TempDst(t, Ev, 4); E::TempSrc(t, Ev, E::Rep(2)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, Ev, 2); E::TempSrc(t, Ev, E::Rep(1)); E::ImmScalar(t, -4.0f); E::ImmScalar(t, 1.0f); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, F, 8); E::TempSrc(t, F, E::Rep(3)); E::TempSrc(t, Ev, E::Rep(1)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, F, 2); E::TempSrc(t, F, E::Rep(1), 1); E::ImmScalar(t, 1.0f); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, F, 2); E::TempSrc(t, F, E::Rep(1)); E::TempSrc(t, Ev, E::Rep(2)); });
    // decode w: R (F.x), the rest (F.z), G (E.x), B (E.y)
    w.Ins(Op::Mul, [&] { E::TempDst(t, F, 1); CbSrc(t, kGlobalCb, kTimeRow, E::Rep(3)); E::ImmScalar(t, 1.0f / 65536.0f); });
    w.Ins(kRoundNi, [&] { E::TempDst(t, F, 1); E::TempSrc(t, F, E::Rep(0)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, F, 4); E::TempSrc(t, F, E::Rep(0)); E::ImmScalar(t, -65536.0f); CbSrc(t, kGlobalCb, kTimeRow, E::Rep(3)); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, Ev, 1); E::TempSrc(t, F, E::Rep(2)); E::ImmScalar(t, 1.0f / 256.0f); });
    w.Ins(kRoundNi, [&] { E::TempDst(t, Ev, 1); E::TempSrc(t, Ev, E::Rep(0)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, Ev, 2); E::TempSrc(t, Ev, E::Rep(0)); E::ImmScalar(t, -256.0f); E::TempSrc(t, F, E::Rep(2)); });
    // the pink buds' G and B (kBudTint.x is 0: R is left alone), then the output
    w.Ins(Op::Mad, [&] { E::TempDst(t, Ev, 0xC); E::TempSrc(t, F, E::Rep(1)); E::Imm4(t, 0.0f, 0.0f, Bl::kBudTint[1], Bl::kBudTint[2]);
                         E::Imm4(t, 0.0f, 0.0f, 1.0f, 1.0f); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, Ev, 3); E::TempSrc(t, Ev, E::Swizzle(0, 1, 0, 0)); E::TempSrc(t, Ev, E::Swizzle(2, 3, 2, 2)); });
    w.Ins(Op::Mul, [&] { E::OutputDst(t, n2, 1); E::TempSrc(t, F, E::Rep(0)); E::ImmScalar(t, 1.0f / 255.0f); });
    w.Ins(Op::Mul, [&] { E::OutputDst(t, n2, 6); E::TempSrc(t, Ev, E::Swizzle(0, 0, 1, 0)); E::ImmScalar(t, 1.0f / 255.0f); });
    w.Ins(Op::Mov, [&] { E::OutputDst(t, n2, 8); E::TempSrc(t, F, E::Rep(3)); });
}

bool TransformVertex(const Match& m, const Curves& cv, std::vector<uint8_t>* out) {
    const Dxbc::Program& p = m.prog;
    const uint32_t A = m.temps, B = m.temps + 1, D = m.temps + 3;
    Writer w;
    w.t.reserve(p.tokens.size() + 256);
    auto& t = w.t;
    int dclAdded = 0;
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        const uint32_t* src = p.tokens.data() + in.offset;
        const int ii = static_cast<int>(i);
        if (in.isDecl) {
            const size_t at = t.size();
            t.insert(t.end(), src, src + in.length);
            if (in.opcode == Op::DclTemps) t[at + 1] = m.temps + (m.kind == Kind::BillboardColour ? 6 : 4);
            if (m.kind == Kind::Foliage && ii == m.dclIdx) t[at + 1] |= 0xFu << 4;   // dcl_output oS.xyzw
            if (m.kind == Kind::BillboardColour && ii == m.lastDclOut) {
                for (uint32_t r = m.newReg; r <= m.newReg + 1; ++r) {   // the terms, the blossom
                    t.push_back(E::Opcode(kDclOutput, 3));
                    t.push_back(2u | (0xFu << 4) | (OT::Output << 12) | (1u << 20));
                    t.push_back(r);
                    ++dclAdded;
                }
            }
            continue;
        }
        if (ii == m.xlateIdx) {
            EmitTerms(w, m, cv);
            if (m.kind == Kind::Foliage) {
                w.Ins(Op::Mov, [&] { E::OutputDst(t, m.snowReg, 8); E::TempSrc(t, D, E::Rep(3)); });
            } else if (m.kind == Kind::BillboardColour) {
                w.Ins(Op::Mov, [&] { E::OutputDst(t, m.newReg, 3); E::TempSrc(t, A, E::Swizzle(1, 2, 1, 1)); });
                w.Ins(Op::Mov, [&] { E::OutputDst(t, m.newReg, 4); E::TempSrc(t, B, E::Rep(1)); });
                w.Ins(Op::Mov, [&] { E::OutputDst(t, m.newReg, 8); E::TempSrc(t, D, E::Rep(3)); });
                EmitBloomVertex(w, m, m.newReg + 1);
            }
        }
        if (m.kind == Kind::BillboardDepth && ii == m.depthMulIdx) {
            std::vector<uint32_t> repl;
            E::TempSrc(repl, A, E::Rep(2));   // l instead of userPosition.z
            w.ReEmit(p, in, m.depthMulOp, repl);
            continue;
        }
        t.insert(t.end(), src, src + in.length);
    }
    Dxbc::Program np;
    np.version = p.version;
    np.tokens  = std::move(t);
    Dxbc::Container cont = m.cont;
    if (m.kind == Kind::BillboardColour) {
        if (!AppendElement(cont, Dxbc::FourCC('O', 'S', 'G', 'N'), m.newReg, m.newSemIx)) return false;
        if (!AppendElement(cont, Dxbc::FourCC('O', 'S', 'G', 'N'), m.newReg + 1, m.newSemIx + 1)) return false;
    }
    if (m.kind == Kind::Foliage && !WidenElement(cont, Dxbc::FourCC('O', 'S', 'G', 'N'), m.snowReg)) return false;
    Dxbc::Chunk* shdr = cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = cont.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) return false;
    shdr->data = Dxbc::EmitProgram(np);
    std::vector<uint8_t> blob = Dxbc::Emit(cont);
    if (!SelfCheck(blob, shdr->fourcc, p.instrs.size() + static_cast<size_t>(w.added + dclAdded))) return false;
    *out = std::move(blob);
    return true;
}

// ---- pixel ----------------------------------------------------------------------------------------
void AnalysePixel(Match* m) {
    const Dxbc::Program& p = m->prog;
    const size_t n = p.instrs.size();
    uint32_t rA = 0;
    bool readsPixelUser = false;
    for (size_t i = 0; i < n; ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.isDecl) {
            if (in.opcode == kDclInputPs) m->lastDclIn = static_cast<int>(i);
            continue;
        }
        for (const Dxbc::Operand& o : in.operands)
            if (IsCb(o, kPixelCb, kPixelUserRow)) readsPixelUser = true;
        if (m->sampleIdx < 0 && in.opcode == Op::Sample && in.operands.size() == 4 &&
            in.operands[1].type == OT::Input && in.operands[2].type == OT::Resource && in.operands[2].index[0] == 0 &&
            in.operands[0].type == OT::Temp && in.operands[0].Mask() == 0xF) {
            m->sampleIdx = static_cast<int>(i);
            rA = in.operands[0].index[0];
        }
        if (m->sampleIdx >= 0 && m->discardIdx < 0 && in.opcode == Op::Discard) m->discardIdx = static_cast<int>(i);
    }
    if (m->sampleIdx < 0) return Refuse(m, "no diffuse sample into a temp");
    if (m->discardIdx < 0 || m->discardIdx > m->sampleIdx + 4) return Refuse(m, "no cutout discard after the sample");
    // a refusal is worth a log line only for a program with a season term or a snow sample (the other
    // alpha-tested materials are not trees)
    m->candidate = readsPixelUser;
    std::vector<SigElem> isgn;
    if (!ReadSig(m->cont, Dxbc::FourCC('I', 'S', 'G', 'N'), &isgn)) return Refuse(m, "no readable ISGN");
    if (m->lastDclIn < 0) return Refuse(m, "no dcl_input_ps");

    if (readsPixelUser) {
        // BillboardColour: the season mad straight after the sample, the gold mad, the fall mul.
        const Dxbc::Instr& sm = p.instrs[static_cast<size_t>(m->sampleIdx + 1)];
        if (sm.opcode != Op::Mad || sm.operands.size() != 4) return Refuse(m, "no season mad after the sample");
        const Dxbc::Operand& f = sm.operands[1];
        const Dxbc::Operand& mv = sm.operands[2];
        uint32_t c = 0;
        if (!IsTempScalarDst(sm.operands[0], rA, 8) || !IsCb(f, kPixelCb, kPixelUserRow) || !ReadsOnly(f, 2) ||
            f.modifier != 1 || mv.type != OT::Input || mv.relative || !ScalarComp(mv, &c))
            return Refuse(m, "the mad after the sample is not -userPosition.z * move + a");
        m->seasonMadIdx = m->sampleIdx + 1;
        m->moveReg = mv.index[0];
        m->moveComp = c;
        for (size_t i = static_cast<size_t>(m->discardIdx) + 1; i < n; ++i) {
            const Dxbc::Instr& in = p.instrs[i];
            if (in.isDecl) continue;
            if (m->goldIdx < 0 && in.opcode == Op::Mad && in.operands.size() == 4 && ImmIs(in.operands[2], 0, kGold[0]) &&
                ImmIs(in.operands[2], 1, kGold[1]) && ImmIs(in.operands[2], 2, kGold[2]))
                m->goldIdx = static_cast<int>(i);
            if (m->fallMulIdx < 0 && in.opcode == Op::Mul && in.operands.size() == 3) {
                for (int k = 1; k <= 2; ++k) {
                    const Dxbc::Operand& o = in.operands[static_cast<size_t>(k)];
                    const Dxbc::Operand& other = in.operands[static_cast<size_t>(3 - k)];
                    uint32_t oc = 0;
                    if (IsCb(o, kPixelCb, kPixelUserRow) && ReadsOnly(o, 1) && o.modifier == 0 && other.type == OT::Input &&
                        other.index[0] == m->moveReg && ScalarComp(other, &oc) && oc == m->moveComp) {
                        m->fallMulIdx = static_cast<int>(i);
                        m->fallMulOp  = k;
                    }
                }
            }
        }
        if (m->goldIdx < 0) return Refuse(m, "no gold fall-colour mad");
        if (m->fallMulIdx < 0) return Refuse(m, "no move * userPosition.y");
        int uses = 0;
        for (const Dxbc::Instr& in : p.instrs)
            for (const Dxbc::Operand& o : in.operands)
                if (IsCb(o, kPixelCb, kPixelUserRow)) ++uses;
        if (uses != 2) return Refuse(m, "userPosition is read outside the two season terms");
        if (!NextSlot(isgn, &m->newReg, &m->newSemIx)) return Refuse(m, "no TEXCOORD input");
        if (m->newReg > 14) return Refuse(m, "no two free input registers");   // the terms + the blossom
        m->kind = Kind::BillboardColour;
        return;
    }

    // Foliage: the snow sample from input D (t1) and `mul x.w, x.w, vD.z`, D an xyz element.
    int snow = -1;
    for (size_t i = static_cast<size_t>(m->discardIdx) + 1; i < n; ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.opcode == Op::Sample && in.operands.size() == 4 && in.operands[1].type == OT::Input &&
            in.operands[2].type == OT::Resource && in.operands[2].index[0] == 1) {
            snow = static_cast<int>(in.operands[1].index[0]);
            break;
        }
    }
    if (snow < 0) return Refuse(m, "no snow sample from an input (not a foliage program)");
    m->candidate = true;
    bool amount = false;
    for (const Dxbc::Instr& in : p.instrs) {
        if (in.isDecl || in.opcode != Op::Mul || in.operands.size() != 3) continue;
        for (int k = 1; k <= 2; ++k) {
            const Dxbc::Operand& o = in.operands[static_cast<size_t>(k)];
            uint32_t c = 0;
            if (o.type == OT::Input && o.index[0] == static_cast<uint32_t>(snow) && ScalarComp(o, &c) && c == 2) amount = true;
        }
    }
    if (!amount) return Refuse(m, "foliage: no snow amount read from the interpolant's z");
    for (const SigElem& e : isgn) {
        if (e.reg != static_cast<uint32_t>(snow)) continue;
        if (e.mask != 0x7) return Refuse(m, "foliage: the snow interpolant is not xyz");
        m->snowReg = e.reg;
    }
    if (m->snowReg != static_cast<uint32_t>(snow)) return Refuse(m, "foliage: no ISGN element for the snow input");
    for (size_t i = 0; i < n; ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        if (in.isDecl && in.opcode == kDclInputPs && in.length == 3 && p.tokens[in.offset + 2] == m->snowReg)
            m->dclIdx = static_cast<int>(i);
    }
    if (m->dclIdx < 0) return Refuse(m, "foliage: no plain dcl_input_ps for the snow input");
    m->kind = Kind::Foliage;
}

// The blossom mask (blossom.h "THE PIXEL TERMS"), BillboardColour only, before the season mad. Reads the
// diffuse sample's UV, vN.y (l), vN2 (r, g, b, cov) and the move input; temps P, Q, S, R = n+3..n+6.
// Leaves R.w = l' (the season mad's loss), R.x = k (the colour amount, move included), R.y = the cluster's
// brightness. Blossom::Pixel is the same arithmetic in the same order.
void EmitBloomMask(Writer& w, const Match& m) {
    namespace Bl = Blossom;
    const Dxbc::Program& p = m.prog;
    const uint32_t P = m.temps + 3, Q = m.temps + 4, S = m.temps + 5, R = m.temps + 6;
    const uint32_t N = m.newReg, N2 = m.newReg + 1;
    const Dxbc::Operand& uv = p.instrs[static_cast<size_t>(m.sampleIdx)].operands[1];
    const uint32_t uvSwz = E::Swizzle(uv.Swz(0), uv.Swz(1), uv.Swz(0), uv.Swz(0));
    auto& t = w.t;
    // the cell coordinates (Q.xy), the cells per pixel -> the far-field blend (P.x), the cell (P.zw)
    w.Ins(Op::Mul, [&] { E::TempDst(t, Q, 3); E::CopyWithSwizzle(t, p, uv, uvSwz); E::Imm4(t, Bl::kCells, Bl::kCells, 0.0f, 0.0f); });
    w.Ins(Op::DerivRtx, [&] { E::TempDst(t, P, 3); E::TempSrc(t, Q, XYXX); });
    w.Ins(Op::DerivRty, [&] { E::TempDst(t, P, 0xC); E::TempSrc(t, Q, E::Swizzle(0, 0, 0, 1)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, P, 3); E::TempSrc(t, P, XYXX, 2); E::TempSrc(t, P, E::Swizzle(2, 3, 2, 2), 2); });
    w.Ins(Op::Max, [&] { E::TempDst(t, P, 1); E::TempSrc(t, P, E::Rep(0)); E::TempSrc(t, P, E::Rep(1)); });
    w.Ins(Op::Mad, true, [&] { E::TempDst(t, P, 1); E::TempSrc(t, P, E::Rep(0)); E::ImmScalar(t, Bl::kAaSlope);
                               E::ImmScalar(t, -Bl::kAaStart * Bl::kAaSlope); });
    w.Ins(kRoundNi, [&] { E::TempDst(t, P, 0xC); E::TempSrc(t, Q, E::Swizzle(0, 0, 0, 1)); });
    // hash33(cell) -> R.xyz (EmitTerms' sequence, S the scratch)
    w.Ins(Op::Mul, [&] { E::TempDst(t, S, 7); E::TempSrc(t, P, E::Swizzle(2, 3, 2, 2)); E::Imm4(t, kHashK[0], kHashK[1], kHashK[2], 0.0f); });
    w.Ins(kFrc, [&] { E::TempDst(t, S, 7); E::TempSrc(t, S, XYZX); });
    w.Ins(Op::Add, [&] { E::TempDst(t, R, 7); E::TempSrc(t, S, E::Swizzle(1, 0, 2, 1)); E::Imm4(t, kHashAdd, kHashAdd, kHashAdd, 0.0f); });
    w.Ins(kDp3, [&] { E::TempDst(t, R, 1); E::TempSrc(t, S, XYZX); E::TempSrc(t, R, XYZX); });
    w.Ins(Op::Add, [&] { E::TempDst(t, S, 7); E::TempSrc(t, S, XYZX); E::TempSrc(t, R, E::Rep(0)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, R, 7); E::TempSrc(t, S, E::Swizzle(0, 0, 1, 0)); E::TempSrc(t, S, E::Swizzle(1, 2, 2, 1)); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, R, 7); E::TempSrc(t, R, XYZX); E::TempSrc(t, S, E::Swizzle(2, 1, 0, 2)); });
    w.Ins(kFrc, [&] { E::TempDst(t, R, 7); E::TempSrc(t, R, XYZX); });
    // f = frc(Q) - the cluster centre, squashed for catkins; d^2 (Q.x)
    w.Ins(kFrc, [&] { E::TempDst(t, Q, 3); E::TempSrc(t, Q, XYXX); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, S, 3); E::TempSrc(t, R, E::Swizzle(1, 2, 1, 1)); E::Imm4(t, -0.5f, -0.5f, 0.0f, 0.0f);
                         E::Imm4(t, -0.25f, -0.25f, 0.0f, 0.0f); });
    w.Ins(Op::Add, [&] { E::TempDst(t, Q, 3); E::TempSrc(t, Q, XYXX); E::TempSrc(t, S, XYXX); });
    w.Ins(Op::Lt, [&] { E::TempDst(t, S, 1); InputSrc(t, N2, E::Rep(3)); E::ImmScalar(t, 0.0f); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, S, 8); E::TempSrc(t, R, E::Rep(1)); E::ImmScalar(t, Bl::kCatkinYVar); E::ImmScalar(t, Bl::kCatkinY); });
    w.Ins(Op::MovC, [&] { E::TempDst(t, S, 2); E::TempSrc(t, S, E::Rep(0)); E::ImmScalar(t, Bl::kCatkinX); E::ImmScalar(t, 1.0f); });
    w.Ins(Op::MovC, [&] { E::TempDst(t, S, 4); E::TempSrc(t, S, E::Rep(0)); E::TempSrc(t, S, E::Rep(3)); E::ImmScalar(t, 1.0f); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, Q, 3); E::TempSrc(t, Q, XYXX); E::TempSrc(t, S, E::Swizzle(1, 2, 1, 1)); });
    w.Ins(Op::Dp2, [&] { E::TempDst(t, Q, 1); E::TempSrc(t, Q, XYXX); E::TempSrc(t, Q, XYXX); });
    // the soft cluster (S.y), present when h.x < |cov|; the far-field blend toward |cov| * kArea
    w.Ins(Op::Mad, [&] { E::TempDst(t, S, 2); E::TempSrc(t, R, E::Rep(2)); E::ImmScalar(t, Bl::kRadius1); E::ImmScalar(t, Bl::kRadius0); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, S, 2); E::TempSrc(t, S, E::Rep(1)); E::TempSrc(t, S, E::Rep(1)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, S, 2); E::TempSrc(t, S, E::Rep(1)); E::TempSrc(t, Q, E::Rep(0), 1); });
    w.Ins(Op::Mul, true, [&] { E::TempDst(t, S, 2); E::TempSrc(t, S, E::Rep(1)); E::ImmScalar(t, Bl::kEdge); });
    w.Ins(Op::Lt, [&] { E::TempDst(t, S, 4); E::TempSrc(t, R, E::Rep(0)); InputSrc(t, N2, E::Rep(3), 2); });
    w.Ins(Op::And, [&] { E::TempDst(t, S, 2); E::TempSrc(t, S, E::Rep(1)); E::TempSrc(t, S, E::Rep(2)); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, S, 4); InputSrc(t, N2, E::Rep(3), 2); E::ImmScalar(t, Bl::kArea); });
    w.Ins(Op::Add, [&] { E::TempDst(t, S, 4); E::TempSrc(t, S, E::Rep(2)); E::TempSrc(t, S, E::Rep(1), 1); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, S, 2); E::TempSrc(t, P, E::Rep(0)); E::TempSrc(t, S, E::Rep(2)); E::TempSrc(t, S, E::Rep(1)); });
    // kept (S.z); l' (R.w); k (R.x); the brightness (R.y)
    w.Ins(Op::Mul, true, [&] { E::TempDst(t, S, 4); E::TempSrc(t, S, E::Rep(1)); E::ImmScalar(t, Bl::kKeep); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, R, 8); E::TempSrc(t, S, E::Rep(2), 1); InputSrc(t, N, E::Rep(1)); InputSrc(t, N, E::Rep(1)); });
    w.Ins(Op::Add, [&] { E::TempDst(t, S, 8); E::TempSrc(t, S, E::Rep(2)); E::TempSrc(t, S, E::Rep(1), 1); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, S, 8); InputSrc(t, N, E::Rep(1)); E::TempSrc(t, S, E::Rep(3)); E::TempSrc(t, S, E::Rep(1)); });
    w.Ins(Op::Mul, [&] { E::TempDst(t, R, 1); E::TempSrc(t, S, E::Rep(3)); InputSrc(t, m.moveReg, E::Rep(m.moveComp)); });
    w.Ins(Op::Mad, [&] { E::TempDst(t, R, 2); E::TempSrc(t, R, E::Rep(1)); E::ImmScalar(t, Bl::kBright1); E::ImmScalar(t, Bl::kBright0); });
}

bool TransformPixel(const Match& m, std::vector<uint8_t>* out) {
    const Dxbc::Program& p = m.prog;
    const uint32_t X = m.temps, Y = m.temps + 1, Z = m.temps + 2;
    const uint32_t rA = p.instrs[static_cast<size_t>(m.sampleIdx)].operands[0].index[0];
    const bool bb = m.kind == Kind::BillboardColour;
    const uint32_t N = m.newReg, M = m.moveReg;
    Writer w;
    w.t.reserve(p.tokens.size() + 192);
    auto& t = w.t;
    int dclAdded = 0;
    for (size_t i = 0; i < p.instrs.size(); ++i) {
        const Dxbc::Instr& in = p.instrs[i];
        const uint32_t* src = p.tokens.data() + in.offset;
        const int ii = static_cast<int>(i);
        if (in.isDecl) {
            const size_t at = t.size();
            t.insert(t.end(), src, src + in.length);
            if (in.opcode == Op::DclTemps) t[at + 1] = m.temps + (bb ? 7 : 1);
            if (!bb && ii == m.dclIdx) t[at + 1] |= 0xFu << 4;   // dcl_input_ps vD.xyzw
            if (bb && ii == m.lastDclIn) {
                for (uint32_t r = N; r <= N + 1; ++r) {   // the terms, the blossom
                    t.push_back(src[0]);   // dcl_input_ps with the same (linear) interpolation
                    t.push_back(2u | (0xFu << 4) | (OT::Input << 12) | (1u << 20));
                    t.push_back(r);
                    t[t.size() - 3] = (src[0] & ~(0x7Fu << 24)) | (3u << 24);
                    ++dclAdded;
                }
            }
            continue;
        }
        if (bb && ii == m.seasonMadIdx) {
            // the blossom mask, then the season mad reads -l' (R.w): -l where nothing flowers
            EmitBloomMask(w, m);
            std::vector<uint32_t> repl;
            E::TempSrc(repl, m.temps + 6, E::Rep(3), 1);   // -l' instead of -userPosition.z
            w.ReEmit(p, in, 1, repl);
            continue;
        }
        if (bb && ii == m.goldIdx) {
            // the palette colour of hue (vN.z) into X, browned by l (vN.y)
            w.Ins(Op::Mul, true, [&] { E::TempDst(t, Y, 1); InputSrc(t, N, E::Rep(2)); E::ImmScalar(t, kOrangeAt); });
            w.Ins(Op::Mad, true, [&] { E::TempDst(t, Y, 2); InputSrc(t, N, E::Rep(2)); E::ImmScalar(t, kRedFrom + 1.0f);
                                       E::ImmScalar(t, -kRedFrom); });
            w.Ins(Op::Mad, [&] { E::TempDst(t, X, 7); E::TempSrc(t, Y, E::Rep(0));
                                 E::Imm4(t, kOrange[0] - kGold[0], kOrange[1] - kGold[1], kOrange[2] - kGold[2], 0.0f);
                                 E::Imm4(t, kGold[0], kGold[1], kGold[2], 0.0f); });
            w.Ins(Op::Mad, [&] { E::TempDst(t, X, 7); E::TempSrc(t, Y, E::Rep(1));
                                 E::Imm4(t, kRed[0] - kOrange[0], kRed[1] - kOrange[1], kRed[2] - kOrange[2], 0.0f);
                                 E::TempSrc(t, X, XYZX); });
            w.Ins(Op::Add, [&] { E::TempDst(t, Z, 7); E::TempSrc(t, X, XYZX, 1); E::Imm4(t, kBrown[0], kBrown[1], kBrown[2], 0.0f); });
            w.Ins(Op::Mul, [&] { E::TempDst(t, Y, 1); InputSrc(t, N, E::Rep(1)); E::ImmScalar(t, kBrowning); });
            w.Ins(Op::Mad, [&] { E::TempDst(t, X, 7); E::TempSrc(t, Y, E::Rep(0)); E::TempSrc(t, Z, XYZX); E::TempSrc(t, X, XYZX); });
            std::vector<uint32_t> repl;
            E::TempSrc(repl, X, XYZX);
            w.ReEmit(p, in, 2, repl);
            continue;
        }
        if (bb && ii == m.fallMulIdx) {
            std::vector<uint32_t> repl;
            InputSrc(repl, N, E::Rep(0));   // c instead of userPosition.y
            w.ReEmit(p, in, m.fallMulOp, repl);
            continue;
        }
        t.insert(t.end(), src, src + in.length);
        if (ii == m.discardIdx) {
            // the green variation: rA.xyz *= 1 + t (* move) * kTint (kPineTint on the pines)
            const float* k = bb ? kTint : kPineTint;
            if (bb) {
                w.Ins(Op::Mul, [&] { E::TempDst(t, X, 1); InputSrc(t, N, E::Rep(3)); InputSrc(t, M, E::Rep(m.moveComp)); });
            } else {
                w.Ins(Op::Mov, [&] { E::TempDst(t, X, 1); InputSrc(t, m.snowReg, E::Rep(3)); });
            }
            w.Ins(Op::Mad, [&] { E::TempDst(t, X, 7); E::TempSrc(t, X, E::Rep(0)); E::Imm4(t, k[0], k[1], k[2], 0.0f);
                                 E::Imm4(t, 1.0f, 1.0f, 1.0f, 0.0f); });
            w.Ins(Op::Mul, [&] { E::TempDst(t, rA, 7); E::TempSrc(t, rA, XYZX); E::TempSrc(t, X, XYZX); });
            if (bb) {
                // the blossom: rA.rgb = lerp(rA.rgb, vN2.rgb * bright (R.y), k (R.x))
                const uint32_t R = m.temps + 6;
                w.Ins(Op::Mul, [&] { E::TempDst(t, X, 7); InputSrc(t, N + 1, XYZX); E::TempSrc(t, R, E::Rep(1)); });
                w.Ins(Op::Add, [&] { E::TempDst(t, X, 7); E::TempSrc(t, X, XYZX); E::TempSrc(t, rA, XYZX, 1); });
                w.Ins(Op::Mad, [&] { E::TempDst(t, rA, 7); E::TempSrc(t, R, E::Rep(0)); E::TempSrc(t, X, XYZX); E::TempSrc(t, rA, XYZX); });
            }
        }
    }
    Dxbc::Program np;
    np.version = p.version;
    np.tokens  = std::move(t);
    Dxbc::Container cont = m.cont;
    if (bb && !AppendElement(cont, Dxbc::FourCC('I', 'S', 'G', 'N'), N, m.newSemIx)) return false;
    if (bb && !AppendElement(cont, Dxbc::FourCC('I', 'S', 'G', 'N'), N + 1, m.newSemIx + 1)) return false;
    if (!bb && !WidenElement(cont, Dxbc::FourCC('I', 'S', 'G', 'N'), m.snowReg)) return false;
    Dxbc::Chunk* shdr = cont.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    if (shdr == nullptr) shdr = cont.Find(Dxbc::FourCC('S', 'H', 'E', 'X'));
    if (shdr == nullptr) return false;
    shdr->data = Dxbc::EmitProgram(np);
    std::vector<uint8_t> blob = Dxbc::Emit(cont);
    if (!SelfCheck(blob, shdr->fourcc, p.instrs.size() + static_cast<size_t>(w.added + dclAdded))) return false;
    *out = std::move(blob);
    return true;
}

float Frc(float v) { return v - std::floor(v); }
float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

const char* KindName(Kind k) {
    switch (k) {
    case Kind::BillboardColour: return "billboard colour";
    case Kind::BillboardDepth: return "billboard depth";
    case Kind::Foliage: return "foliage";
    default: return "none";
    }
}

void Analyse(const uint8_t* blob, size_t size, Match* m) {
    *m = Match();
    if (!Head(blob, size, m)) return;
    if (m->vertex) AnalyseVertex(m);
    else AnalysePixel(m);
    if (m->kind != Kind::None) m->why.clear();
}

bool Transform(const Match& m, std::vector<uint8_t>* out, const Curves& c) {
    if (m.kind == Kind::None) return false;
    if (m.vertex) return TransformVertex(m, c, out);
    if (m.kind == Kind::BillboardDepth) return false;   // a vertex-only kind
    return TransformPixel(m, out);
}

TreeTerms TermsOf(float x, float y) {
    TreeTerms r;
    float a[3] = {Frc(x * kHashK[0]), Frc(y * kHashK[1]), Frc(x * kHashK[2])};
    const float d = a[0] * (a[1] + kHashAdd) + a[1] * (a[0] + kHashAdd) + a[2] * (a[2] + kHashAdd);
    for (float& v : a) v += d;
    r.h[0] = Frc((a[0] + a[1]) * a[2]);
    r.h[1] = Frc((a[0] + a[2]) * a[1]);
    r.h[2] = Frc((a[1] + a[2]) * a[0]);
    r.grove = std::sin(x * kGroveA[0] + y * kGroveA[1]) + std::sin(x * kGroveB[0] + y * kGroveB[1] + kGroveBPhase);
    r.tint = 2.0f * r.h[2] - 1.0f;
    return r;
}

void SeasonOf(const TreeTerms& t, float months, float* colour, float* loss, const Curves& c) {
    const float o = c.autumnSpread * (1.2f * t.h[0] - 0.6f + 0.2f * t.grove);
    const float s = c.springStart + c.springSpread * (1.2f * Frc(t.h[0] + t.h[2]) - 0.6f + 0.2f * t.grove);
    const float d = months - o;
    *colour = Sat((d - c.colourStart) / c.colourLength);
    const float autumn = Sat((d - c.fallStart) / c.fallLength);
    const float spring = 1.0f - Sat((months - s) / c.growLength);
    *loss = autumn > spring ? autumn : spring;
}

}  // namespace NgRewrite
