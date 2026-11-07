#pragma once
// ============================================================================
// src/render/common/better_trees/dxbc.h -- a minimal DXBC container + SM4 token
// reader / writer (ext.foliage_aa; FAA-SPIKE wrote it, FAA-CORE ships it).
//
// NOT RETAIL. No D3D dependency and no retail bytes: it parses whatever blob it
// is handed (the user's installed shaders, at run time) and re-emits it. The
// unit tests (tests/unit/ext_better_trees_test.cpp) build their own SM4 blobs.
//
// Container (little-endian):
//   +0  'DXBC'   +4  16-byte checksum (DXBC's MD5 variant over bytes 20..end)
//   +20 version (1)   +24 total size   +28 chunk count   +32 chunk offsets[]
//   each chunk: fourcc, byte size, data.
// SHDR / SHEX chunk data: version token (bits 0-3 minor, 4-7 major, 16-31
// program type: 0 = pixel), length in dwords (incl. both header dwords), then
// the instruction stream.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Dxbc {

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

struct Chunk {
    uint32_t             fourcc = 0;
    std::vector<uint8_t> data;
};

struct Container {
    uint32_t           checksum[4] = {0, 0, 0, 0};   // as read (Parse) -- Emit recomputes
    uint32_t           version     = 1;
    std::vector<Chunk> chunks;

    Chunk*       Find(uint32_t fourcc);
    const Chunk* Find(uint32_t fourcc) const;
};

// DXBC's checksum: MD5 over bytes [20, size) with the container's own final-block
// layout (the bit count first in the last block, `(bits >> 2) | 1` in its last dword).
void Checksum(const uint8_t* blob, size_t size, uint32_t out[4]);

bool Parse(const uint8_t* blob, size_t size, Container* out, std::string* err);
std::vector<uint8_t> Emit(const Container& c);   // fresh offsets, sizes and checksum

// ---------------------------------------------------------------------------
// SM4 program: the SHDR chunk's tokens.
// ---------------------------------------------------------------------------
namespace Op {   // D3D10_SB_OPCODE_TYPE (d3d10tokenizedprogramformat.hpp)
enum : uint32_t {
    Add = 0, And = 1, DerivRtx = 11, DerivRty = 12, Discard = 13, Div = 14, Dp2 = 15,
    Ge = 29, Log = 47, Lt = 49, Mad = 50, Min = 51, Max = 52, CustomData = 53, Mov = 54, MovC = 55,
    Mul = 56,
    ResInfo = 61, Ret = 62, Sample = 69, SampleC = 70, SampleCLz = 71, SampleL = 72,
    SampleD = 73, SampleB = 74,
    DclResource = 88, DclConstantBuffer = 89, DclSampler = 90, DclTemps = 104,
    DclGlobalFlags = 106, Lod = 108,   // Lod: SM4.1 (107 is reserved)
};
}
namespace OperandType {   // D3D10_SB_OPERAND_TYPE
enum : uint32_t { Temp = 0, Input = 1, Output = 2, IndexableTemp = 3, Imm32 = 4, Imm64 = 5,
                  Sampler = 6, Resource = 7, ConstantBuffer = 8 };
}
// A component mask: an operand's write / read mask (bits 4-7 of a mask-mode
// operand token, so `Mask::XYZW << 4`) and a signature element's mask /
// read-write-mask bytes.
namespace Mask {
enum : uint32_t {
    X    = 0x1,
    Y    = 0x2,
    Z    = 0x4,
    W    = 0x8,
    XYZ  = 0x7,
    ZW   = 0xC,
    XYZW = 0xF,
};
}
// Bit 31 of an opcode or operand token: an extended token follows
// (D3D10_SB_OPCODE_EXTENDED / D3D10_SB_OPERAND_EXTENDED).
constexpr uint32_t kTokenExtended = 0x80000000u;

struct Operand {
    uint32_t offset   = 0;   // dword index of the operand token in Program::tokens
    uint32_t length   = 0;   // dwords, including extended token, indices and immediates
    uint32_t token    = 0;
    uint32_t type     = 0;   // OperandType
    uint32_t numComp  = 0;   // token bits 0-1: 0 = none, 1 = one, 2 = four
    uint32_t selMode  = 0;   // 0 mask, 1 swizzle, 2 select-1 (four-component operands)
    uint32_t compBits = 0;   // the mask (4 bits) / swizzle (8 bits) / select (2 bits)
    uint32_t modifier = 0;   // 0 none, 1 neg, 2 abs, 3 -abs
    uint32_t indexDim = 0;
    uint32_t index[3] = {0, 0, 0};
    bool     relative = false;   // any index uses a relative representation
    uint32_t imm[4]   = {0, 0, 0, 0};   // Imm32 values (raw bits)

    // The component the operand reads first / writes (for scalar patterns).
    uint32_t Mask() const { return selMode == 0 ? (compBits & Dxbc::Mask::XYZW) : 0; }
    // Component c's source selector for a swizzled / selected source.
    uint32_t Swz(int c) const {
        if (selMode == 1) return (compBits >> (2 * c)) & 3;
        if (selMode == 2) return compBits & 3;
        return static_cast<uint32_t>(c);
    }
    float ImmF(int c) const;
};

struct Instr {
    uint32_t             offset  = 0;   // dword index in Program::tokens
    uint32_t             length  = 0;   // dwords
    uint32_t             opcode  = 0;
    uint32_t             token   = 0;
    bool                 isDecl  = false;
    std::vector<Operand> operands;      // instructions only (not declarations)

    bool Saturate() const { return ((token >> 13) & 1) != 0; }
    bool TestNonZero() const { return ((token >> 18) & 1) != 0; }   // discard / if / breakc
};

struct Program {
    uint32_t              version = 0;   // the version token
    std::vector<uint32_t> tokens;        // the stream AFTER the two header dwords
    std::vector<Instr>    instrs;

    uint32_t ProgramType() const { return version >> 16; }   // 0 = pixel
    uint32_t Major() const { return (version >> 4) & 0xF; }
    uint32_t Minor() const { return version & 0xF; }
};

bool ParseProgram(const Chunk& shdr, Program* out, std::string* err);
std::vector<uint8_t> EmitProgram(const Program& p);   // version, length, tokens

// One line of a short human-readable form of an instruction ("add r0.w, r0.w, l(-0.5)").
std::string Describe(const Program& p, const Instr& in);

// ---------------------------------------------------------------------------
// Token encoders (what the rewriter emits)
// ---------------------------------------------------------------------------
namespace Enc {
uint32_t Opcode(uint32_t opcode, uint32_t lengthDwords, bool saturate = false, uint32_t extra = 0);
// Four-component temp destination with a write mask (bits x=1 y=2 z=4 w=8).
void TempDst(std::vector<uint32_t>& t, uint32_t reg, uint32_t mask);
void OutputDst(std::vector<uint32_t>& t, uint32_t reg, uint32_t mask);
// Swizzled temp source, swizzle = x | y<<2 | z<<4 | w<<6, modifier 0/1/2/3.
void TempSrc(std::vector<uint32_t>& t, uint32_t reg, uint32_t swizzle, uint32_t modifier = 0);
void ImmScalar(std::vector<uint32_t>& t, float v);
void Imm4(std::vector<uint32_t>& t, float x, float y, float z, float w);
constexpr uint32_t Swizzle(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    return x | (y << 2) | (z << 4) | (w << 6);
}
constexpr uint32_t Rep(uint32_t c) { return Swizzle(c, c, c, c); }
// A copy of an existing operand's tokens with its swizzle replaced (selMode -> 1).
void CopyWithSwizzle(std::vector<uint32_t>& t, const Program& p, const Operand& o, uint32_t swizzle);
void Copy(std::vector<uint32_t>& t, const Program& p, const Operand& o);
}  // namespace Enc

}  // namespace Dxbc
