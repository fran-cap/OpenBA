// src/render/common/better_trees/dxbc.cpp -- see dxbc.h.
#include "better_trees/dxbc.h"

#include <cstdio>
#include <cstring>

namespace Dxbc {
namespace {

uint32_t Rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void Wr32(std::vector<uint8_t>& v, uint32_t x) {
    const size_t o = v.size();
    v.resize(o + 4);
    std::memcpy(v.data() + o, &x, 4);
}

// ---- MD5 block transform (RFC 1321) ---------------------------------------
inline uint32_t Rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

void Md5Block(uint32_t st[4], const uint8_t block[64]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) m[i] = Rd32(block + 4 * i);
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) & 15; }
        else             { f = c ^ (b | ~d);       g = (7 * i) & 15; }
        const uint32_t t = d;
        d = c;
        c = b;
        b = b + Rotl(a + f + K[i] + m[g], R[i]);
        a = t;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

// ---- operand decode --------------------------------------------------------
// Returns the operand's length in dwords, 0 on a malformed / truncated operand.
uint32_t DecodeOperand(const uint32_t* t, uint32_t avail, uint32_t at, Operand* o) {
    if (avail == 0) return 0;
    uint32_t n = 0;
    const uint32_t tok = t[n++];
    o->offset   = at;
    o->token    = tok;
    o->numComp  = tok & 3;
    o->selMode  = (tok >> 2) & 3;
    o->type     = (tok >> 12) & 0xFF;
    o->indexDim = (tok >> 20) & 3;
    if (o->numComp == 2) {
        if (o->selMode == 0)      o->compBits = (tok >> 4) & 0xF;
        else if (o->selMode == 1) o->compBits = (tok >> 4) & 0xFF;
        else                      o->compBits = (tok >> 4) & 3;
    }
    if (tok & kTokenExtended) {   // extended operand token(s)
        uint32_t ext;
        do {
            if (n >= avail) return 0;
            ext = t[n++];
            if ((ext & 0x3F) == 1) o->modifier = (ext >> 6) & 0xFF;
        } while (ext & kTokenExtended);
    }
    if (o->type == OperandType::Imm32 || o->type == OperandType::Imm64) {
        const uint32_t comps = (o->numComp == 1) ? 1 : (o->numComp == 2 ? 4 : 0);
        const uint32_t per   = (o->type == OperandType::Imm64) ? 2 : 1;
        if (n + comps * per > avail) return 0;
        for (uint32_t c = 0; c < comps * per; ++c)
            if (c < 4) o->imm[c] = t[n + c];
        n += comps * per;
        return n;
    }
    for (uint32_t d = 0; d < o->indexDim; ++d) {
        const uint32_t rep = (tok >> (22 + 3 * d)) & 7;
        // 0 imm32, 1 imm64, 2 relative, 3 imm32 + relative, 4 imm64 + relative
        if (rep == 0 || rep == 3) {
            if (n >= avail) return 0;
            o->index[d] = t[n++];
        } else if (rep == 1 || rep == 4) {
            if (n + 2 > avail) return 0;
            o->index[d] = t[n];
            n += 2;
        }
        if (rep >= 2) {
            o->relative = true;
            Operand sub;
            const uint32_t len = DecodeOperand(t + n, avail - n, at + n, &sub);
            if (len == 0) return 0;
            n += len;
        }
    }
    return n;
}

bool IsDeclaration(uint32_t opcode) {
    return (opcode >= Op::DclResource && opcode <= Op::DclGlobalFlags) || opcode == Op::CustomData ||
           opcode >= 0xC8;   // SM5 declarations (not used by ps_4_0)
}

}  // namespace

float Operand::ImmF(int c) const {
    float f;
    std::memcpy(&f, &imm[c], 4);
    return f;
}

Chunk* Container::Find(uint32_t fourcc) {
    for (Chunk& c : chunks)
        if (c.fourcc == fourcc) return &c;
    return nullptr;
}
const Chunk* Container::Find(uint32_t fourcc) const {
    for (const Chunk& c : chunks)
        if (c.fourcc == fourcc) return &c;
    return nullptr;
}

void Checksum(const uint8_t* blob, size_t size, uint32_t out[4]) {
    uint32_t st[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const uint8_t* data = blob + 20;
    const size_t   len  = size - 20;
    const size_t   full = len / 64;
    for (size_t i = 0; i < full; ++i) Md5Block(st, data + 64 * i);
    const size_t   rem  = len % 64;
    const uint8_t* tail = data + 64 * full;
    const uint32_t bits = static_cast<uint32_t>(len * 8);
    const uint32_t last = (bits >> 2) | 1u;
    uint8_t block[64];
    if (rem >= 56) {
        std::memset(block, 0, 64);
        std::memcpy(block, tail, rem);
        block[rem] = 0x80;
        Md5Block(st, block);
        std::memset(block, 0, 64);
        std::memcpy(block, &bits, 4);
        std::memcpy(block + 60, &last, 4);  // CAST-OK: byte_io
        Md5Block(st, block);
    } else {
        std::memset(block, 0, 64);
        std::memcpy(block, &bits, 4);
        std::memcpy(block + 4, tail, rem);  // CAST-OK: byte_io
        block[4 + rem] = 0x80;
        std::memcpy(block + 60, &last, 4);  // CAST-OK: byte_io
        Md5Block(st, block);
    }
    for (int i = 0; i < 4; ++i) out[i] = st[i];
}

bool Parse(const uint8_t* blob, size_t size, Container* out, std::string* err) {
    auto fail = [&](const char* why) { if (err) *err = why; return false; };
    if (size < 32) return fail("too small");
    if (Rd32(blob) != FourCC('D', 'X', 'B', 'C')) return fail("no DXBC magic");
    for (int i = 0; i < 4; ++i) out->checksum[i] = Rd32(blob + 4 + 4 * i);
    out->version = Rd32(blob + 20);
    if (Rd32(blob + 24) != size) return fail("total size != blob size");
    const uint32_t count = Rd32(blob + 28);
    if (32 + 4ull * count > size) return fail("chunk table past the end");
    out->chunks.clear();
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t off = Rd32(blob + 32 + 4 * i);
        if (off + 8ull > size) return fail("chunk header past the end");
        Chunk c;
        c.fourcc = Rd32(blob + off);
        const uint32_t len = Rd32(blob + off + 4);
        if (off + 8ull + len > size) return fail("chunk data past the end");
        c.data.assign(blob + off + 8, blob + off + 8 + len);
        out->chunks.push_back(std::move(c));
    }
    return true;
}

std::vector<uint8_t> Emit(const Container& c) {
    std::vector<uint8_t> v;
    Wr32(v, FourCC('D', 'X', 'B', 'C'));
    for (int i = 0; i < 4; ++i) Wr32(v, 0);   // checksum, filled last
    Wr32(v, c.version);
    Wr32(v, 0);                               // total size, filled below
    Wr32(v, static_cast<uint32_t>(c.chunks.size()));
    const size_t table = v.size();
    for (size_t i = 0; i < c.chunks.size(); ++i) Wr32(v, 0);
    for (size_t i = 0; i < c.chunks.size(); ++i) {
        const uint32_t off = static_cast<uint32_t>(v.size());
        std::memcpy(v.data() + table + 4 * i, &off, 4);  // CAST-OK: byte_io
        Wr32(v, c.chunks[i].fourcc);
        Wr32(v, static_cast<uint32_t>(c.chunks[i].data.size()));
        v.insert(v.end(), c.chunks[i].data.begin(), c.chunks[i].data.end());
    }
    const uint32_t total = static_cast<uint32_t>(v.size());
    std::memcpy(v.data() + 24, &total, 4);  // CAST-OK: byte_io
    uint32_t sum[4];
    Checksum(v.data(), v.size(), sum);
    std::memcpy(v.data() + 4, sum, 16);  // CAST-OK: byte_io
    return v;
}

bool ParseProgram(const Chunk& shdr, Program* out, std::string* err) {
    auto fail = [&](const std::string& why) { if (err) *err = why; return false; };
    if (shdr.data.size() < 8 || (shdr.data.size() & 3)) return fail("SHDR size");
    const uint8_t* d = shdr.data.data();
    out->version = Rd32(d);
    const uint32_t lenDw = Rd32(d + 4);
    if (lenDw * 4ull != shdr.data.size()) return fail("SHDR length token != chunk size");
    out->tokens.resize(lenDw - 2);
    if (lenDw > 2) std::memcpy(out->tokens.data(), d + 8, (lenDw - 2) * 4ull);  // CAST-OK: byte_io
    out->instrs.clear();
    const uint32_t* t = out->tokens.data();
    const uint32_t  n = static_cast<uint32_t>(out->tokens.size());
    uint32_t at = 0;
    while (at < n) {
        Instr in;
        in.offset = at;
        in.token  = t[at];
        in.opcode = in.token & 0x7FF;
        if (in.opcode == Op::CustomData) {
            if (at + 1 >= n) return fail("customdata truncated");
            in.length = t[at + 1];
        } else {
            in.length = (in.token >> 24) & 0x7F;
        }
        // Review F-211: `in.length > n - at`, not `at + in.length > n`: a customdata length near
        // 2^32 wrapped the uint32 sum, passed, and moved `at` backwards (an endless loop).
        if (in.length == 0 || in.length > n - at) {
            char b[96];
            std::snprintf(b, sizeof b, "bad instruction length %u at dword %u (opcode %u)", in.length, at,
                          in.opcode);
            return fail(b);
        }
        in.isDecl = IsDeclaration(in.opcode);
        if (!in.isDecl) {
            uint32_t k = 1;
            if (in.token & kTokenExtended) {   // extended opcode token(s)
                uint32_t ext;
                do {
                    if (k >= in.length) return fail("extended opcode truncated");
                    ext = t[at + k++];
                } while (ext & kTokenExtended);
            }
            while (k < in.length) {
                Operand o;
                const uint32_t len = DecodeOperand(t + at + k, in.length - k, at + k, &o);
                if (len == 0) return fail("bad operand");
                o.length = len;
                in.operands.push_back(o);
                k += len;
            }
        }
        out->instrs.push_back(std::move(in));
        at += out->instrs.back().length;
    }
    return true;
}

std::vector<uint8_t> EmitProgram(const Program& p) {
    std::vector<uint8_t> v;
    Wr32(v, p.version);
    Wr32(v, static_cast<uint32_t>(p.tokens.size() + 2));
    const size_t o = v.size();
    v.resize(o + p.tokens.size() * 4);
    if (!p.tokens.empty()) std::memcpy(v.data() + o, p.tokens.data(), p.tokens.size() * 4);
    return v;
}

std::string Describe(const Program& p, const Instr& in) {
    (void)p;
    static const char* const kNames[] = {
        "add", "and", "break", "breakc", "call", "callc", "case", "continue", "continuec", "cut",
        "default", "deriv_rtx", "deriv_rty", "discard", "div", "dp2", "dp3", "dp4", "else", "emit",
        "emitthencut", "endif", "endloop", "endswitch", "eq", "exp", "frc", "ftoi", "ftou", "ge",
        "iadd", "if", "ieq", "ige", "ilt", "imad", "imax", "imin", "imul", "ine", "ineg", "ishl",
        "ishr", "itof", "label", "ld", "ld_ms", "log", "loop", "lt", "mad", "min", "max",
        "customdata", "mov", "movc", "mul", "ne", "nop", "not", "or", "resinfo", "ret", "retc",
        "round_ne", "round_ni", "round_pi", "round_z", "rsq", "sample", "sample_c", "sample_c_lz",
        "sample_l", "sample_d", "sample_b", "sqrt", "switch", "sincos", "udiv", "ult", "uge", "umul",
        "umad", "umax", "umin", "ushr", "utof", "xor"};
    std::string s;
    if (in.opcode < sizeof(kNames) / sizeof(kNames[0])) s = kNames[in.opcode];
    else { char b[16]; std::snprintf(b, sizeof b, "op%u", in.opcode); s = b; }
    if (in.opcode == Op::Discard) s += in.TestNonZero() ? "_nz" : "_z";
    if (in.Saturate()) s += "_sat";
    static const char kC[] = "xyzw";
    for (size_t i = 0; i < in.operands.size(); ++i) {
        const Operand& o = in.operands[i];
        s += i == 0 ? " " : ", ";
        if (o.modifier & 1) s += "-";
        if (o.modifier & 2) s += "|";
        char b[64];
        switch (o.type) {
        case OperandType::Temp:           std::snprintf(b, sizeof b, "r%u", o.index[0]); break;
        case OperandType::Input:          std::snprintf(b, sizeof b, "v%u", o.index[0]); break;
        case OperandType::Output:         std::snprintf(b, sizeof b, "o%u", o.index[0]); break;
        case OperandType::Sampler:        std::snprintf(b, sizeof b, "s%u", o.index[0]); break;
        case OperandType::Resource:       std::snprintf(b, sizeof b, "t%u", o.index[0]); break;
        case OperandType::ConstantBuffer: std::snprintf(b, sizeof b, "cb%u[%u]", o.index[0], o.index[1]); break;
        case OperandType::Imm32:
            if (o.numComp == 1) std::snprintf(b, sizeof b, "l(%g)", o.ImmF(0));
            else std::snprintf(b, sizeof b, "l(%g,%g,%g,%g)", o.ImmF(0), o.ImmF(1), o.ImmF(2), o.ImmF(3));
            break;
        default: std::snprintf(b, sizeof b, "?%u", o.type); break;
        }
        s += b;
        if (o.relative) s += "[rel]";
        if (o.type != OperandType::Imm32 && o.numComp == 2) {
            s += ".";
            if (o.selMode == 0) {
                for (int c = 0; c < 4; ++c)
                    if (o.compBits & (1u << c)) s += kC[c];
            } else if (o.selMode == 1) {
                for (int c = 0; c < 4; ++c) s += kC[o.Swz(c)];
            } else {
                s += kC[o.compBits & 3];
            }
        }
        if (o.modifier & 2) s += "|";
    }
    return s;
}

namespace Enc {
uint32_t Opcode(uint32_t opcode, uint32_t lengthDwords, bool saturate, uint32_t extra) {
    return (opcode & 0x7FF) | (saturate ? (1u << 13) : 0u) | ((lengthDwords & 0x7F) << 24) | extra;
}
namespace {
void Dst(std::vector<uint32_t>& t, uint32_t type, uint32_t reg, uint32_t mask) {
    t.push_back(2u | (0u << 2) | ((mask & 0xF) << 4) | (type << 12) | (1u << 20));
    t.push_back(reg);
}
}  // namespace
void TempDst(std::vector<uint32_t>& t, uint32_t reg, uint32_t mask) { Dst(t, OperandType::Temp, reg, mask); }
void OutputDst(std::vector<uint32_t>& t, uint32_t reg, uint32_t mask) { Dst(t, OperandType::Output, reg, mask); }
void TempSrc(std::vector<uint32_t>& t, uint32_t reg, uint32_t swizzle, uint32_t modifier) {
    uint32_t tok = 2u | (1u << 2) | ((swizzle & 0xFF) << 4) | (OperandType::Temp << 12) | (1u << 20);
    if (modifier) tok |= kTokenExtended;
    t.push_back(tok);
    if (modifier) t.push_back(1u | (modifier << 6));
    t.push_back(reg);
}
void ImmScalar(std::vector<uint32_t>& t, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    t.push_back(1u | (OperandType::Imm32 << 12));
    t.push_back(bits);
}
void Imm4(std::vector<uint32_t>& t, float x, float y, float z, float w) {
    t.push_back(2u | (OperandType::Imm32 << 12));
    const float v[4] = {x, y, z, w};
    for (float f : v) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        t.push_back(bits);
    }
}
void Copy(std::vector<uint32_t>& t, const Program& p, const Operand& o) {
    t.insert(t.end(), p.tokens.begin() + o.offset, p.tokens.begin() + o.offset + o.length);
}
void CopyWithSwizzle(std::vector<uint32_t>& t, const Program& p, const Operand& o, uint32_t swizzle) {
    const size_t at = t.size();
    Copy(t, p, o);
    uint32_t tok = t[at];
    tok &= ~((3u << 2) | (0xFFu << 4));
    tok |= (1u << 2) | ((swizzle & 0xFF) << 4);
    t[at] = tok;
}
}  // namespace Enc

}  // namespace Dxbc
