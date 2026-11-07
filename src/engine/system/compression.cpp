// ============================================================================
// src/engine/system/compression.cpp — Wave 9 lane PKG2, iteration 2
// System::Compression::Decompress — the bottom of the ReadPackfile stack
// ============================================================================
//
//   | Export                                            | Retail      | Ord  | Instrs |
//   |---------------------------------------------------|-------------|------|--------|
//   | ?Decompress@Compression@System@@SAHPEBXIPEAXI@Z   | 0x1800c4660 | 2150 |   72   |
//
// Name and ordinal printed out of Runtime-x64.dll's own export directory with
// `pefile`, not transcribed. `tools/export_name_audit.py` re-checks it.
//
// ---------------------------------------------------------------------------
// 1. WHY THIS FUNCTION, AND WHY FIRST
// ---------------------------------------------------------------------------
// `?ReadPackfile@PackageFile@IO@@` (0x1800b46b0) is **1,864 instructions**, the
// largest single body of the wave. Reading its callee set explains the size and
// dictates the order — see docs/history/runtime_port/WAVE9_READPACKFILE_PLAN.md. The
// short version: ReadPackfile is not 1,864 instructions of novel logic, it is a
// header/directory parser in which MSVC **inlined `CompressedStream::Read`
// twenty-five times**, and the whole of it stands on an `IO::CompressedStream`
// read path that nothing in this tree has ported.
//
// `Compression::Decompress` is the bottom of that stack. It is 72 instructions,
// it is PURE, it calls nothing, and it is the function whose correctness
// decides **every byte of every package the game loads**. A silent defect here
// is not a crash; it is subtly wrong resource data.
//
// It is also the only member of the stack with a *perfect* oracle available
// today — see §3.
//
// ---------------------------------------------------------------------------
// 2. THE CODEC, INSTRUCTION BY INSTRUCTION
// ---------------------------------------------------------------------------
// A bespoke LZ77/LZSS byte codec. **No zlib, no LZ4, no external library** —
// which is worth stating because "the package format is compressed" invites the
// assumption that a library is involved and that assumption would have sent
// this lane looking for one.
//
//   static int Decompress(const void* src, unsigned srcLen,
//                         void* dst, unsigned dstCap)
//
// Signature read off the mangled name AND off the registers, which agree:
// `SAH` = static, returns int; `PEBX I PEAX I` = (const void*, unsigned, void*,
// unsigned) = RCX, EDX, R8, R9D.
//
//   1800c466a  MOV EDI,EDX / ADD RDI,RCX      srcEnd = src + srcLen
//   1800c466c  MOV R11D,R9D / ADD R11,R8      dstEnd = dst + dstCap
//   1800c4675  R10 = sp (read cursor), RAX = op (write cursor), RBX = dst
//
// Both length arguments are moved through a 32-bit register first (`MOV EDI,EDX`,
// `MOV R11D,R9D`), i.e. **zero-extended**, then added to a 64-bit pointer. They
// are unsigned and cannot go negative; transcribed as uint32 -> uint64 for that
// reason and not as a sign-extended int.
//
//   loop:
//     c = *sp++
//     if (c < 0x20) {                                     // LITERAL RUN
//         n = c + 1;                                      // 1..32
//         if (op + n > dstEnd) return 0;                  // 1800c4696
//         copy n bytes sp -> op
//     } else {                                            // MATCH
//         len = c >> 5;                                   // 1..7
//         mp  = op - ((c & 0x1f) << 8) - 1;
//         if (len == 7) len = 7 + *sp++;                  // 7..262
//         mp -= *sp++;                                    // the low offset byte
//         if (op + 2 + len > dstEnd) return 0;            // 1800c46f3
//         if (mp < dst)            return 0;              // 1800c46f8, UNSIGNED (JC)
//         copy 2 + len bytes mp -> op, ONE BYTE AT A TIME
//     }
//   while (sp < srcEnd);                                  // 1800c4727, UNSIGNED (JC)
//   return (int)(op - dst);
//
// FOUR THINGS THAT ARE EASY TO GET WRONG AND ARE LOAD-BEARING:
//
//  1. **The match copy MUST be byte-at-a-time.** `mp` and `op` overlap whenever
//     the distance is less than the length, and retail's loop reads each byte
//     *after* the previous one was written — that is the RLE idiom every LZ77
//     depends on. A `memcpy` or `memmove` here compiles, runs, and produces
//     different bytes for exactly the inputs that compress best. The two
//     unrolled bytes at 0x1800c46fa..0x1800c470e are part of the same chain.
//     (Issue #13: the body below copies the chain in pieces no longer than the
//     distance, each a non-overlapping memcpy -- the same bytes; see its
//     banner. A plain memcpy/memmove of the whole match is still wrong.)
//
//  2. **Total copied is `2 + len`, and the loop runs `len` times.** Two bytes
//     are copied unrolled before the `do { } while (--len)`, which is why the
//     bounds check is `op + 2 + len` and why the minimum match is 3 bytes.
//
//  3. **The loop is a do-while: `srcLen == 0` still reads one control byte.**
//     There is no test before the first iteration (0x1800c4678 falls straight
//     into 0x1800c4680); the only test is at the bottom. That is a one-byte read
//     past the end of a zero-length buffer, and it is retail's behaviour, not a
//     transcription slip. It is NOT guarded here — the project rule is that a
//     faithful unguarded read stays faithful, and a guard would change what the
//     function returns rather than protect anything (the caller always passes a
//     non-empty window). The probe in §3 never passes srcLen == 0.
//
//  4. **`mp < dst` is an UNSIGNED compare (`JC`, not `JL`).** With `mp`
//     computed by subtraction it can wrap below the buffer; the unsigned form is
//     what makes that wrap a rejection rather than a wild read.
//
// Distance is `((c & 0x1f) << 8) + lo + 1`, so 1..8192 — an 8 KB window, which
// is consistent with CompressedStream's 32 KB (0x8000) page size.
//
// ---------------------------------------------------------------------------
// 3. THE ORACLE — THREE INDEPENDENT CHECKS, AND VALID INPUTS BY CONSTRUCTION
// ---------------------------------------------------------------------------
// Nothing in our tree calls Decompress today: its only caller is the DLL's own
// `CompressedStream::DecompressFromSource`, and a `RegisterRuntimeExport` does
// not redirect the DLL's internal direct CALLs. So registering it alone would
// report `calls=0` — the failure this project keeps paying for
// (reference_uncalled_port_is_not_verified.md). It needs a driver, and it gets
// a good one.
//
// `?Compress@Compression@System@@QEAAHPEBXIPEAXI@Z` (0x1800c4230, ord 1749) is
// exported. So the probe **compresses a real buffer with the DLL's own
// compressor and decompresses it with both implementations**, which gives:
//
//   A. ours == theirs, on the returned length AND on every byte written;
//   B. ours round-trips: the decompressed bytes equal the ORIGINAL input.
//      This check does not involve the DLL's Decompress at all, so a shared
//      misreading of the format cannot pass it;
//   C. the three failure exits return 0 on both sides.
//
// Check B is the one that matters most. A and B together mean a defect would
// have to be present in our port, absent from the DLL's, and still reproduce
// the original bytes — which is to say there is no such defect.
//
// EFFECTIVENESS CONTROL (reference_probe_needs_an_effectiveness_control.md).
// A codec probe is exactly where a uniform population hides: feed it garbage and
// almost every case returns 0 from the same bounds check, and 5,000 agreeing
// zeroes read as a thorough test. So the port counts which branch each case
// actually took — literal run, short match, extended-length match, and each of
// the three failure exits — and the probe prints all six. **A bucket that reads
// 0 is reported as uncovered rather than averaged into a pass**, and the
// corpus is built to fill all six on purpose (§4).
//
// ---------------------------------------------------------------------------
// 4. THE CORPUS
// ---------------------------------------------------------------------------
// Compressible-real, incompressible-real, and pathological, because the three
// exercise different branches:
//
//   * **Runtime-x64.dll's own loaded image**, in sliding windows. Real, highly
//     non-uniform data — x86 code, ASCII string tables, relocation tables, long
//     zero runs — and it is already mapped, so reading it needs no file I/O, no
//     new Win32 API and no new import (this file adds neither). The zero runs
//     drive long overlapping matches; the code bytes drive literal runs.
//   * **Synthetic RLE**: a single repeated byte, which forces the
//     extended-length branch (`len == 7`, read another byte) and maximal
//     overlap, the case a memcpy-based port gets wrong.
//   * **Synthetic incompressible**: a counter-derived pattern with no repeats,
//     which forces literal runs almost exclusively.
//   * **Malformed and tight-buffer cases**: RAW (uncompressed) bytes fed in as
//     `src` to make the decoder walk nonsense, and valid streams given a `dstCap`
//     one byte short of what they need — that is what fills the failure buckets
//     deliberately rather than by luck.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <new>        // placement new: the ??0 door builds into the caller's 8 bytes

#include "system/Compression.h"   // ENDGAME B.3.4b SYSTEM (4b)
#include "system/string_internal.h"   // RtMemAlloc -- the lock-wrapped MemoryAllocator door

// ============================================================================
// ENDGAME B.3.4b SYSTEM (4b) — THE CLASS IS REAL; THE DOORS DELEGATE.
// ============================================================================
// `System::Compression` is declared in `src/engine/system/Compression.h`,
// which carries the "is it a class?" evidence (a `QEAA` ctor, a `QEAA` member
// and an `SA` static, all class-scoped manglings) and the layout pins. The two
// codec bodies and the constructor moved onto members VERBATIM; the three
// registered doors keep their names, their retail addresses and their
// registration rows and each delegates in one line.
//
// The member bodies are defined at GLOBAL scope with qualified names
// (`int System::Compression::Compress(...)`) rather than inside a
// `namespace System { ... }` block — rule B4b-5. There is no POD of this name,
// but the branch counters, `CompressHash` and the two counter structs are
// `RtSystem`'s and are reached from the bodies as `RtSystem::X`, which is the
// arrangement B34_BRIEFS §A.2 established (anonymous-namespace members ARE
// reachable as `RtSystem::X` from elsewhere in the same translation unit).
//
// THE DECODER'S BRANCH COUNTERS ARE GONE (issue #13). `g_branch` was a
// file-static pointer that only the Wave-9 oracle ever pointed at storage, and
// that oracle died with the cutover: nothing in the tree set it, nothing read
// it, so every shipped call paid a null test per op for a counter that was
// always null. Removing it changes nothing observable. The ENCODER's
// `g_cbranch` is the same kind of dead control and is left as it is (the
// encoder runs only on a save).
// ============================================================================

namespace RtSystem {
namespace {

// The decoder's copies, as fixed-width loads and stores (issue #13). Each writes
// EXACTLY [d, d + n): an overlapping tail store rewrites bytes it already wrote,
// with the same values, and never touches a byte past the run.
inline void Copy8(uint8_t* d, const uint8_t* s) {
    uint64_t v;
    std::memcpy(&v, s, 8);
    std::memcpy(d, &v, 8);
}
inline void Copy4(uint8_t* d, const uint8_t* s) {
    uint32_t v;
    std::memcpy(&v, s, 4);
    std::memcpy(d, &v, 4);
}

// n bytes, s and d in different buffers (a literal run, 1..32).
inline void CopyDisjoint(uint8_t* d, const uint8_t* s, uint32_t n) {
    if (n >= 8u) {
        uint32_t k = 0;
        for (; k + 8u <= n; k += 8u)
            Copy8(d + k, s + k);
        if (k < n)
            Copy8(d + n - 8u, s + n - 8u);
    } else if (n >= 4u) {
        Copy4(d, s);
        Copy4(d + n - 4u, s + n - 4u);
    } else {
        d[0] = s[0];
        if (n > 1u) {
            d[1] = s[1];
            if (n > 2u)
                d[2] = s[2];
        }
    }
}

// A match at distance >= 8: op[i] = op[i - dist] for i in [0, total). The
// 8-byte step at k reads [k - dist, k - dist + 8), which ends at or before k,
// so every byte it reads is already final; the tail step at total - 8 reads
// below total - 8, which is already written. 3..7-byte matches read only below
// op. Byte for byte retail's chain.
inline void CopyMatchFar(uint8_t* op, size_t dist, uint32_t total) {
    const uint8_t* const mp = op - dist;
    if (total >= 8u) {
        uint32_t k = 0;
        for (; k + 8u <= total; k += 8u)
            Copy8(op + k, mp + k);
        if (k < total)
            Copy8(op + total - 8u, mp + total - 8u);
    } else if (total >= 4u) {
        Copy4(op, mp);
        Copy4(op + total - 4u, mp + total - 4u);
    } else {
        op[0] = mp[0];
        op[1] = mp[1];
        op[2] = mp[2];
    }
}

}  // namespace
}  // namespace RtSystem

// ============================================================================
// ?Decompress@Compression@System@@SAHPEBXIPEAXI@Z            retail 0x1800c4660
// ============================================================================
// The bespoke LZ77/LZSS byte codec §2 of this file's banner derives instruction
// by instruction. `SA`, so no `this` and no instance state is touched — which
// is exactly why `src/engine/io/compressed_stream_read.cpp` can reach it from
// two `RT_EXPORT_ONCE` sites with no receiver at all.
//
// AN EXACT PERF BODY, NOT RETAIL'S SHAPE (issue #13). Retail copies every byte
// of a literal run and of a match one at a time (§2 note 1). This body makes
// the same decisions in the same order -- the same control-byte walk, the same
// two bounds checks with the same `return 0`, the same do-while with its
// unsigned `sp < srcEnd` exit, the same return value -- and only the COPIES
// are wider:
//   * A literal run copies sp[0..n) to op[0..n) in 8- / 4-byte steps
//     (CopyDisjoint). Retail's loop reads and writes them in order; the two
//     ranges are in different buffers, so the order cannot be observed, and no
//     byte outside either range is read or written. Retail reads up to 31
//     bytes past srcEnd when a run straddles the end (the overshoot exit);
//     this body returns 0 there instead (NOT RETAIL, review F-001, below).
//   * A match of `total = len + 2` bytes at distance `dist` is op[i] =
//     op[i - dist] for i = 0..total in order, so each byte may be one the same
//     match just wrote. dist >= 8 copies 8 bytes a step (CopyMatchFar: each
//     step reads only bytes already final); dist == 1 is a run of one byte,
//     memset; dist 2..7 keeps retail's byte loop. Byte for byte the same
//     result as retail's chain, and nothing is written past op + total.
//   * `mp < dst` (retail's unsigned JC) is tested as `dist > op - dst`. With
//     op inside the destination buffer and dist <= 8192, op - dist cannot wrap
//     the address space, so the two tests agree; this form never forms the
//     out-of-buffer pointer.
// PRECONDITION, as for retail's loop: src and dst do not overlap. Both callers
// (CompressedStream::DecompressFromSource, from a stack scratch block, and
// SetWindow's in-memory arm, from a block buffer) decode into a window buffer.
// Pure and reentrant: no state outside the arguments, safe from any thread.
int System::Compression::Decompress(const void* src, uint32_t srcLen,
                                    void* dst, uint32_t dstCap) {
    const uint8_t* sp           = static_cast<const uint8_t*>(src);  // CAST-OK: byte_io
    const uint8_t* const srcEnd = sp + srcLen;          // 1800c4672, zero-extended
    uint8_t* const dstBase      = static_cast<uint8_t*>(dst);  // CAST-OK: byte_io
    uint8_t* const dstEnd       = dstBase + dstCap;     // 1800c467b, zero-extended
    uint8_t* op                 = dstBase;

    // NOT RETAIL (review F-001, plain hardening): retail never tests the
    // source end inside the loop, so an empty source, a literal run or a
    // match's length / offset byte past srcLen reads beyond the source buffer
    // (and a corrupt block's run copies those bytes into the window). Such a
    // stream is rejected with the decoder's own failure value, 0. A valid
    // stream never reads past its end, so it decodes exactly as before.
    if (srcLen == 0)
        return 0;

    // do-while: retail has NO test before the first iteration (§2 note 3).
    do {
        const uint32_t c = *sp++;                       // 1800c4680
        if (c < kMaxLiteralRun) {                       // 1800c468a  JNC -> match
            // ---- literal run: 1..32 bytes ------------------------------
            const uint32_t n = c + 1u;                  // 1800c468c
            if (op + n > dstEnd)                        // 1800c4696  JA
                return 0;
            if (n > static_cast<size_t>(srcEnd - sp))   // NOT RETAIL (F-001): run past the source end
                return 0;
            RtSystem::CopyDisjoint(op, sp, n);          // 1800c46a0 loop
            op += n;
            sp += n;
        } else {
            // ---- match ----------------------------------------------------
            uint32_t len = c >> 5;                      // 1800c46bf   1..7
            // NOT RETAIL (F-001): the length byte (len 7) and the offset byte
            // must lie inside the source.
            if (static_cast<size_t>(srcEnd - sp) < (len == 7u ? 2u : 1u))
                return 0;
            // dist = ((c & 0x1f) << 8) + 1 + lo: retail's mp = op - dist, built
            // in the same two steps (1800c46b6/bc/c5/c8, then the low byte).
            size_t dist = (static_cast<size_t>(c & 0x1fu) << 8) + 1u;
            if (len == 7u)                              // 1800c46cf
                len = 7u + *sp++;                       // 1800c46d1..d9
            dist += *sp++;                              // 1800c46dc/e7, the low offset byte
            const uint32_t total = len + 2u;            // 2 unrolled + len looped
            if (op + total > dstEnd)                    // 1800c46f3  JA
                return 0;
            if (dist > static_cast<size_t>(op - dstBase))   // 1800c46f8  JC: mp < dst
                return 0;
            if (dist >= 8u) {
                RtSystem::CopyMatchFar(op, dist, total);
            } else if (dist == 1u) {
                std::memset(op, op[-1], total);
            } else {
                // NOT RETAIL (F-002, UB): `op[k - dist]` wrapped the unsigned
                // index (k < dist) and so formed a pointer 2^64 - n bytes past
                // op; the signed index names the same byte without the overflow.
                const ptrdiff_t back = static_cast<ptrdiff_t>(dist);
                for (uint32_t k = 0; k < total; ++k)    // 1800c46fa..1800c4711
                    op[k] = op[static_cast<ptrdiff_t>(k) - back];
            }
            op += total;
        }
    } while (sp < srcEnd);                              // 1800c4727  JC (unsigned)

    // The read cursor `sp` is a local: the returned length and the destination
    // bytes are this function's only outputs.
    return static_cast<int>(op - dstBase);              // 1800c472d
}

// The encoder's own file-local state reopens the two namespaces: `g_cbranch`
// and `CompressHash` belong to `RtSystem`'s anonymous namespace exactly as they
// always did, and `System::Compression::Compress` below reaches them as
// `RtSystem::X` (B34_BRIEFS §A.2).
namespace RtSystem {
namespace {

// ============================================================================
// WAVE 22 LANE SYSBODY — the ENCODER: Compression::Compress and its ctor
// ============================================================================
//   ??0Compression@System@@QEAA@XZ                    0x1800c41a0  ord  109
//   ?Compress@Compression@System@@QEAAHPEBXIPEAXI@Z   0x1800c4230  ord 1749
//
// WHAT THE ENCODER IS. LZ77 with a 16-bit hash chain of ONE slot per bucket
// (a "hash head" table, no chain walking, no lazy matching) producing exactly
// the stream the Decompress above consumes. 0x40000 bytes = 0x10000 uint32
// entries, each a byte OFFSET from `src`, which is why the table is useless
// across calls and why the ctor zeroes it.
//
//   op starts at dst+1 (0x1800c429b: LEA R10,[R9+1]). The first byte is
//   RESERVED for the literal-run control that has not been emitted yet. Every
//   emit path writes that control BACKWARDS, at op-ii-1, and every match path
//   reserves a fresh one by advancing op TWO past the low offset byte.
//
// THE SIX THINGS THAT ARE EASY TO GET WRONG HERE, all verified against the
// disassembly rather than the decompiler (which renders this body as a
// 60-line ternary nest):
//
//  1. **`dv` IS A 32-BIT ROLLING REGISTER AND IS NEVER MASKED TO 24 BITS —
//     BUT THE UPPER BYTE IS PROVABLY UNOBSERVABLE, AND THE FIRST DRAFT OF
//     THIS NOTE CLAIMED THE OPPOSITE.** EDI starts as `(src[0]<<8)|src[1]`
//     and every loop top does `SHL EDI,8 / OR EDI,ip[2]`. On the LITERAL path
//     it is NOT reset, so after k consecutive literals the register holds k+2
//     bytes. This note used to say that those upper bytes therefore "CHANGE
//     THE BUCKET" and that masking to 24 bits would silently produce a
//     different stream. **That is false, and a negative control found it**:
//     masking dv to 24 bits was injected on purpose, and the sweep reported
//     ZERO divergences over 40 cases. The sweep was right and the note was
//     wrong. Write dv = M + (U << 24), M = dv & 0xFFFFFF:
//
//         hash = (uint16)((dv >> 8) - 5*dv)
//         (dv >> 8)  low 16 bits = dv bits 8..23  = M >> 8      (U is at 16+)
//         (5 * dv)   low 16 bits = 5*M low 16     (5*(U<<24) is all bits 24+)
//         subtraction borrows propagate UPWARD only
//         => hash = (uint16)((M >> 8) - 5*M), independent of U.
//
//     And dv feeds nothing but the hash. So the upper byte cannot reach any
//     output, and masking is a NO-OP rather than a silent defect. It is still
//     transcribed unmasked, because the register is what retail has and a
//     mask would be an unforced claim about what is safe -- but the reason is
//     now "no reason to deviate", not "load-bearing".
//     The lesson generalises and is why this note survives its own
//     retraction: a control that corrupts a value NOTHING OBSERVES reports
//     NOT CAUGHT and reads as an indictment of a sound instrument
//     (reference_control_must_pick_a_compared_subject.md). Note 5 is the
//     round-trip-invisible hazard on this body that IS real; that is the one
//     the effectiveness control uses.
//
//  2. **`dv` IS CLOBBERED BY THE MATCH PATH.** EDI is reused as `maxLen`
//     (0x1800c4332). Retail rebuilds it afterwards from ip-1 (0x1800c4566),
//     24-bit-clean, which is why the rolling value restarts clean after every
//     match and only accumulates across literal runs. Reproduced exactly.
//
//  3. **THE HASH IS `(uint16)((dv>>8) - 5*dv)`**, all in 32-bit arithmetic
//     (`SHR EDX,8`, `LEA ECX,[RDI+RDI*4]`, `SUB EDX,ECX`, `MOVZX EDX,DX`).
//     The subtraction wraps; the truncation to 16 bits is what bounds it to
//     the table. Written with explicit uint32_t so the wrap is defined.
//
//  4. **THE MATCH GUARD IS `m_pos > src`, STRICTLY** (`CMP R11,R14 / JBE`),
//     not `>=`. Offset 0 in the table means "nothing stored", and a stored
//     offset of 0 at position 0 is indistinguishable from it — so retail
//     simply never matches against the very first byte. A `>=` here matches
//     one byte earlier than retail and diverges on inputs that begin with a
//     repeat.
//
//  5. **THE TWO HASH-TABLE BACKFILLS AFTER A MATCH ARE NOT OPTIONAL.**
//     0x1800c4522..0x1800c4585 stores the buckets for ip-2 and ip-1 (the two
//     positions the match skipped over). Dropping them still round-trips and
//     still compresses; it just finds different matches later. Byte-visible,
//     round-trip-invisible.
//
//  6. **THE TAIL'S 3-BYTE RESERVATION CAN BE OVERRUN BY ONE**, and that is
//     retail's. `CMP RCX(op+3),R12 / JBE` reserves three bytes, then the tail
//     literal loop appends up to two bytes with NO further bounds check; if
//     `ii` reaches 0x20 inside that loop it writes a control byte and advances
//     again, so worst case is 2 data + 1 flush control + 1 final control = 4.
//     Not guarded — the project rule is that a faithful unguarded write stays
//     faithful, and a guard would change the RETURN VALUE rather than protect
//     anything. Callers reach this only through CompressedStream, which sizes
//     its output window at 0x8000 for a 0x8000 input.
//
// ALSO FAITHFUL AND ALSO UNGUARDED: the two loads at 0x1800c425e/0x1800c4272
// read src[0] AND src[1] before anything has established srcLen >= 2, so
// Compress(src, 1, ...) reads one byte past the input. Retail's. The sweep
// below passes srcLen == 1 on purpose so the behaviour is exercised and
// compared rather than merely asserted.

// BRANCH COVERAGE FOR THE ENCODER. Same reason the decoder above counts its
// six branches: a codec sweep is exactly where a uniform population hides. A
// corpus of incompressible bytes drives ONLY the literal path and the two
// 0x20-flushes, agrees perfectly with retail on every case, and says nothing
// at all about the match encoder -- which is two thirds of the body. Six
// counters, printed, with a verdict line that FAILS when any of them is zero.
struct CompressBranchCounts {
    long long literal      = 0;   // a byte emitted as a literal
    long long literalFlush = 0;   // a run hit 0x20 and flushed its control
    long long matchShort   = 0;   // code < 7, one control byte
    long long matchLong    = 0;   // code >= 7, control + extra length byte
    long long matchUnroll  = 0;   // the maxLen > 0x10 unrolled comparison ran
    long long failDst      = 0;   // an output-bounds rejection (either site)
};
CompressBranchCounts* g_cbranch = nullptr;

// The hash retail computes, in the width retail computes it in.
static inline uint32_t CompressHash(uint32_t dv) {
    return static_cast<uint16_t>((dv >> 8) - dv * 5u);   // 1800c42bc..1800c42c9
}

}  // namespace
}  // namespace RtSystem

// ============================================================================
// ??0Compression@System@@QEAA@XZ                             retail 0x1800c41a0
// ============================================================================
// EnterCS(_singleton+0x640); Allocate(_singleton, 0x40000, 4); LeaveCS;
// *this = p; memset(p, 0, 0x40000).
//
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) IS that exact
// lock/Allocate/unlock sequence — verified by decompiling it, not assumed — so
// `RtMemAlloc(0x40000, 4)` from string.cpp is the same three instructions plus
// a call, and it keeps standalone/allocator_lock.cpp's detour on Allocate in
// the path. Open-coding EnterCriticalSection on `_singleton + 0x640` here
// would be a second, private opinion about where that lock lives.
//
// NOT NULL-CHECKED: retail stores the result and memsets it unconditionally.
// An allocation failure here is an upstream fault to find.
System::Compression::Compression() {
    uint32_t* const p = static_cast<uint32_t*>(RtSystem::RtMemAlloc(kHashTableBytes, 4));
    _table = p;
    std::memset(p, 0, kHashTableBytes);
}

// ============================================================================
// ?Compress@Compression@System@@QEAAHPEBXIPEAXI@Z             retail 0x1800c4230
// ============================================================================
int System::Compression::Compress(const void* src, uint32_t srcLen,
                                  void* dst, uint32_t dstCap) {
    // 1800c4247 / 1800c4255 — both length tests, before anything else.
    if (srcLen == 0 || dstCap == 0)
        return 0;                                        // 1800c4645

    uint32_t* const table = _table;                           // 1800c42c6: MOV RCX,[R11]
    const uint8_t* const in     = static_cast<const uint8_t*>(src);  // CAST-OK: byte_io
    const uint8_t* const inEnd  = in + srcLen;                // 1800c4289
    const uint8_t* const inMax  = inEnd - 2;                  // 1800c4292: R15
    uint8_t* const outBase      = static_cast<uint8_t*>(dst);  // CAST-OK: byte_io
    uint8_t* const outEnd       = outBase + dstCap;           // 1800c428e: R12

    const uint8_t* ip = in;                                   // 1800c4296: RAX
    uint8_t* op = outBase + 1;                                // 1800c429b: the RESERVED control byte
    int32_t  ii = 0;                                          // 1800c4299: EBX, the pending literal count
    uint32_t dv = (static_cast<uint32_t>(in[0]) << 8) | in[1];   // 1800c427d..1800c428c

    bool fail = false;

    if (ip < inMax) {                                         // 1800c429f: CMP RDX,R15 / JNC
        for (;;) {
            dv = (dv << 8) | ip[2];                           // 1800c42b0..1800c42ba
            uint32_t* const slot = &table[RtSystem::CompressHash(dv)];
            const uint8_t* const mpos = in + *slot;           // 1800c42d2: zero-extended uint32
            *slot = static_cast<uint32_t>(ip - in);           // 1800c42db
            const uint64_t off = static_cast<uint64_t>(ip - mpos) - 1u;   // 1800c42de..42e1

            const bool isMatch =
                off < kMaxOffset &&                           // 1800c42e4  JNC (unsigned)
                mpos > in &&                                  // 1800c42f1  JBE (STRICT, see note 4)
                mpos[2] == ip[2] &&                           // 1800c42fa
                mpos[0] == ip[0] && mpos[1] == ip[1];         // 1800c4308..4327 (as one 16-bit cmp)

            if (!isMatch) {
                // ---- LITERAL ---- 1800c458f
                if (op >= outEnd) { fail = true; if (RtSystem::g_cbranch) ++RtSystem::g_cbranch->failDst; break; }  // 1800c458f  JNC
                *op++ = *ip++;
                ++ii;
                if (RtSystem::g_cbranch) ++RtSystem::g_cbranch->literal;
                if (ii == static_cast<int32_t>(kMaxLiteralRun)) {   // 1800c45a7
                    if (RtSystem::g_cbranch) ++RtSystem::g_cbranch->literalFlush;
                    // 1800c45ac: the reserved slot, 32 literals back (op[-0x21] = 0x1f)
                    op[-static_cast<int32_t>(kMaxLiteralRun + 1)] = kMaxLiteralRun - 1;
                    ++op;                                     // and reserve the next one
                    ii = 0;
                }
                if (!(ip < inMax)) break;                     // 1800c45b6  JC
                continue;
            }

            // ---- MATCH ---- how long?  1800c432d..1800c4342
            uint32_t maxLen = static_cast<uint32_t>(inEnd - ip) - 2u;
            if (maxLen > kMaxMatchLength) maxLen = kMaxMatchLength;   // CMOVA

            // The output reservation, BEFORE the literal header is flushed.
            // 1800c4345..1800c4363. Note the second test backs `op` up by one
            // when there is no pending literal run, because that reserved byte
            // is about to be reclaimed.
            if (!(op + 4 < outEnd)) {
                if (op - (ii == 0 ? 1 : 0) + 4 >= outEnd) {
                    fail = true; if (RtSystem::g_cbranch) ++RtSystem::g_cbranch->failDst; break;
                }
            }

            // Flush the pending literal run header at op-ii-1. 1800c4369..4382.
            // When ii == 0 this writes 0xff into the reserved byte and then
            // reclaims it, so the match control overwrites it. Faithful, and
            // the write is retail's, not a transcription slip.
            op[-ii - 1] = static_cast<uint8_t>(ii - 1);
            if (ii == 0) --op;

            uint32_t len;                                      // R9D
            if (maxLen <= 0x10u) {                             // 1800c4385  JBE
                len = 2;
                goto generic;                                  // 1800c44b2
            }
            // The unrolled 3..0x12 comparison. Transcribed as a loop: every
            // step is `if (mpos[k] != ip[k]) { len = k; goto emit; }` with the
            // SAME k in the same order, and the fallthrough sets len = 0x12
            // and enters the generic loop (1800c44a2..44b0).
            len = 0x12;
            if (RtSystem::g_cbranch) ++RtSystem::g_cbranch->matchUnroll;
            for (uint32_t k = 3; k <= 0x12u; ++k) {
                if (mpos[k] != ip[k]) { len = k; goto emit; }
            }
        generic:
            // 1800c44b2..44c3
            for (;;) {
                ++len;
                if (len >= maxLen) break;                      // JNC (unsigned)
                if (ip[len] != mpos[len]) break;
            }
        emit:
            {
                const uint32_t mlen = len;                     // total bytes matched
                const uint32_t code = mlen - 2;                // 1800c44c5: ADD R9D,-2
                ++ip;                                          // 1800c44c9: INC RAX
                if (code < 7u) {                               // 1800c44cc  JNC
                    // 1800c44d2..44e5
                    if (RtSystem::g_cbranch) ++RtSystem::g_cbranch->matchShort;
                    *op++ = static_cast<uint8_t>((off >> 8) + (code << 5));
                } else {
                    // 1800c44eb..4501
                    if (RtSystem::g_cbranch) ++RtSystem::g_cbranch->matchLong;
                    *op++ = static_cast<uint8_t>((off >> 8) - 0x20u);
                    *op++ = static_cast<uint8_t>(code - 7u);
                }
                *op = static_cast<uint8_t>(off);               // 1800c450d: the low offset byte
                op += 2;                                       // ...and RESERVE the next control
                ip += mlen - 1;                                // 1800c4510 (ip was already +1)
                ii = 0;                                        // 1800c4517
            }
            if (!(ip < inMax)) break;                          // 1800c4519  JNC -> 45bf

            // The two skipped positions get their buckets filled.
            // 1800c4522..1800c4585.  q = ip-2, then ip-1.
            {
                const uint8_t* const q = ip - 2;
                uint32_t v = (static_cast<uint32_t>(q[0]) << 16) |
                             (static_cast<uint32_t>(q[1]) << 8)  |
                              static_cast<uint32_t>(q[2]);
                table[RtSystem::CompressHash(v)] = static_cast<uint32_t>(q - in);
                v = (v << 8) | q[3];                           // 1800c4557/4566: 3 bytes at ip-1
                table[RtSystem::CompressHash(v)] = static_cast<uint32_t>((q + 1) - in);
                dv = v;                                        // EDI, 24-bit-clean again (note 2)
            }
        }
    }

    if (fail)
        return 0;                                              // 1800c45cd

    // ---- THE TAIL ---- 1800c45c4
    if (op + 3 > outEnd)
        return 0;
    while (ip < inEnd) {                                       // 1800c4600..4621
        *op++ = *ip++;
        ++ii;
        if (ii == static_cast<int32_t>(kMaxLiteralRun)) {
            op[-static_cast<int32_t>(kMaxLiteralRun + 1)] = kMaxLiteralRun - 1;   // op[-0x21] = 0x1f
            ++op;
            ii = 0;
        }
    }
    // 1800c4623..4640
    op[-ii - 1] = static_cast<uint8_t>(ii - 1);
    if (ii == 0) --op;
    return static_cast<int>(op - outBase);
}

// ============================================================================
// The three registered doors — names, signatures and registration rows all
// unchanged. Each delegates in one line.
// ============================================================================
// `System::Compression` has NO vtable, so none of these needs the qualified
// non-virtual call B34_BRIEFS §A.1 forces on a polymorphic receiver: a
// non-virtual member call cannot dispatch back into its own door.
namespace RtSystem {

}  // namespace RtSystem
