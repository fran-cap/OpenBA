// ============================================================================
// src/engine/io/text_codec.cpp — BATCH 8b (2026-09-12)
//
//   IO::TextCodec::DetectFormat   retail 0x1800c9d60   371 bytes
//   IO::TextCodec::DecodeOne      retail 0x1800c9fc0   (dispatch + 6 cases,
//                                                       0x1800c9fc0..0x1800ca1b8)
//   IO::TextCodec::EncodeOne      retail 0x1800c9b60   (dispatch + 6 cases,
//                                                       0x1800c9b60..0x1800c9d40)
//   IO::TextCodec::ConvertText    retail 0x1800ca260   446 bytes
//   IO::SerializeStringArray      retail 0x180029bb0   684 bytes
//
// All five UNEXPORTED — measured against the retail export directory with
// pefile, not read off a mangling — so all five are ADDRESS rows.
//
// ---------------------------------------------------------------------------
// THE "SIX-ENTRY JUMP TABLE" IS SIX CASES OF ONE FUNCTION, NOT SIX FUNCTIONS
// ---------------------------------------------------------------------------
// text_file_stream.cpp described 0x1800c9fc0 and 0x1800c9b60 as
// "dispatchers" through tables at 0x1800ca1b8 and 0x1800c9d40, and said porting
// them "would have to reproduce all six entries of both tables", which read like
// twelve unported bodies. Both tables were read out of the image for this file:
//
//   encode 0x1800c9d40 -> c9b7b c9b91 c9baf c9baf c9c58 c9cc2
//   decode 0x1800ca1b8 -> c9fe1 c9ff1 ca00a ca00a ca0e8 ca0e8
//
// Every target is INSIDE the dispatching function's own body — they are MSVC's
// `switch` arms, four distinct ones per table because ids 2/3 share the UTF-8
// arm and 4/5 share the UTF-16 arm. So this is two functions with a switch, not
// twelve doors, and they are written below as switches (PLAN.md §6: port the
// switch as a switch).
//
// THE SIX FORMAT IDS, now readable off the arms rather than guessed:
//   0  single byte, codepoints 0x00..0xFF        (1 byte/char)
//   1  raw 16-bit little-endian, NO surrogates   (2 bytes/char)
//   2  UTF-8                                     (1..4 bytes)
//   3  UTF-8 with a BOM                          (same arm as 2)
//   4  UTF-16 little-endian, WITH surrogates
//   5  UTF-16 big-endian, WITH surrogates
//   6  "detect" — only ConvertText understands it, as "run DetectFormat"
// Ids 2 and 3 share a decode AND an encode arm; 4 and 5 share a decode arm and
// have separate encode arms. That pairing is the table's, not a simplification.
//
// TWO ARGUMENTS CARRY THE FORMAT INTO DecodeOne, and the existing note was
// right to insist on it: the table is indexed by ARG5 (`MOVSXD RAX,[RSP+0x30]`)
// while the UTF-16 arm reads ECX (`MOV R11D,ECX`) to choose endianness. Passing
// one and not the other decodes every encoding as entry 0, or every UTF-16
// stream as big-endian.
//
// ---------------------------------------------------------------------------
// BOUNDS RULES ARE NOT UNIFORM AND ARE TRANSCRIBED AS FOUND
// ---------------------------------------------------------------------------
//   UTF-8    a 2/3/4-byte sequence needs `src + n <  end`  (`JAE` out)
//   UTF-16   a single unit needs        `src + 2 <= end`   (`JA`  out)
//            a surrogate pair needs     `src + 4 <  end`   (`JAE` out)
// The UTF-8 and surrogate-pair rules are one byte stricter than they need to
// be. That is retail's, it is visible in the opcode, and "fixing" it would make
// this port accept a final character retail rejects.
//
// ---------------------------------------------------------------------------
// ONE TABLE READ A FIFTH TIME — the BOM lengths
// ---------------------------------------------------------------------------
// text_file_stream.cpp counted four independent witnesses to
// "3 -> 3 bytes, 4 -> 2, 5 -> 2, else 0". ConvertText's own ladder
// (0x1800ca2a1..0x1800ca2c6) is a fifth, and `DetectFormat`'s literals
// (EF BB BF -> 3, FF FE -> 4, FE FF -> 5) are the source all five agree with.
// `TfsBomSkip` in text_file_stream.cpp already encodes it; that function is
// left alone rather than shared across the TU boundary, for the reason that
// file's own banner gives about per-file doors.
//
// ---------------------------------------------------------------------------
// ORACLE — none. Three of the five are pure functions of their arguments and
// could in principle be swept, but a sweep is an instrument and DIRECTION.md §2
// says not to build more of those; the two that are not pure write through a
// caller-owned buffer with no return value to replay. The verdict is the text
// paths themselves: `TextFileStream::Open`/`Write` and the package-file string
// tables either read back what they wrote or they do not.
// ============================================================================

#include <cstdint>
#include <cstring>

#include "io/Serializer.h"
#include "io/Stream.h"            // IO::Stream::_direction
#include "system/String.h"
#include "system/MemoryAllocator.h"


namespace {

// The allocator boundary. The open-coded
// `EnterCriticalSection(*0x1801fc718 + 0x640) / Allocate / LeaveCriticalSection`
// at 0x180029c8b..0x180029cba IS `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z`
// (0x1800c7a50) -- lane Q proved that composition instruction for instruction.
// B3R RT-IO: called by name, System::MemoryAllocator::MemAlloc / MemFree.

}  // namespace

// ============================================================================
// 0x1800c9b60 — EncodeOne(codepoint /*ECX*/, dst /*RDX*/, format /*R8D*/)
//   -> bytes written, 0 when the codepoint cannot be represented.
// The caller is responsible for `dst` having room for 4 bytes.
// ============================================================================
extern "C" int32_t RtIo_TextCodec_EncodeOne(int32_t cp, void* dstv, int32_t fmt) {
    uint8_t* const d = static_cast<uint8_t*>(dstv);  // CAST-OK: byte_io

    switch (fmt) {
        case 0:                                                    // 0x1800c9b7b
            if (cp >= 0x100) return 0;
            d[0] = static_cast<uint8_t>(cp);
            return 1;

        case 1:                                                    // 0x1800c9b91
            if (cp >= 0x10000) return 0;
            d[0] = static_cast<uint8_t>(cp);
            d[1] = static_cast<uint8_t>(cp >> 8);
            return 2;

        case 2:                                                    // 0x1800c9baf
        case 3:
            if (cp < 0x80) {          // JL back into case 0's store
                d[0] = static_cast<uint8_t>(cp);
                return 1;
            }
            if (cp < 0x800) {
                d[0] = static_cast<uint8_t>(0xC0 | ((cp >> 6) & 0x1F));
                d[1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
                return 2;
            }
            if (cp < 0x10000) {
                d[0] = static_cast<uint8_t>(0xE0 | ((cp >> 12) & 0x0F));
                d[1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
                d[2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
                return 3;
            }
            // 0x200000, not 0x110000: the UTF-8 arm's own ceiling is the
            // 4-byte encoding's, and the 0x10FFFF check lives in DecodeOne.
            if (cp < 0x200000) {
                d[0] = static_cast<uint8_t>(0xF0 | ((cp >> 18) & 0x07));
                d[1] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
                d[2] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
                d[3] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
                return 4;
            }
            return 0;

        case 4: {                                                  // 0x1800c9c58
            // `LEA EAX,[R9-0xD800] / CMP EAX,0x7FF / JBE` — the whole
            // D800..DFFF block, as ONE unsigned range test.
            // NOT RETAIL (review 2026-11-03 F-057, UB): `cp - 0xD800` in int32 overflows
            // for cp < INT_MIN + 0xD800; the subtraction is done unsigned, which is the
            // 32-bit wrap retail's LEA gives, so every cp answers as before.
            if (static_cast<uint32_t>(cp) - 0xD800u <= 0x7FFu) return 0;
            if (cp < 0x10000) {       // JL back into case 1's store
                d[0] = static_cast<uint8_t>(cp);
                d[1] = static_cast<uint8_t>(cp >> 8);
                return 2;
            }
            const int32_t v  = cp - 0x10000;
            const uint16_t hi = static_cast<uint16_t>(((v >> 10) & 0x3FF) | 0xD800);
            const uint16_t lo = static_cast<uint16_t>((v & 0x3FF) | 0xDC00);
            d[0] = static_cast<uint8_t>(hi);
            d[1] = static_cast<uint8_t>(hi >> 8);
            d[2] = static_cast<uint8_t>(lo);
            d[3] = static_cast<uint8_t>(lo >> 8);
            return 4;
        }

        case 5: {                                                  // 0x1800c9cc2
            // NOT RETAIL (F-057): unsigned, as in case 4.
            if (static_cast<uint32_t>(cp) - 0xD800u <= 0x7FFu) return 0;
            if (cp < 0x10000) {
                d[1] = static_cast<uint8_t>(cp);
                d[0] = static_cast<uint8_t>(cp >> 8);
                return 2;
            }
            const int32_t v  = cp - 0x10000;
            const uint16_t hi = static_cast<uint16_t>(((v >> 10) & 0x3FF) | 0xD800);
            const uint16_t lo = static_cast<uint16_t>((v & 0x3FF) | 0xDC00);
            d[1] = static_cast<uint8_t>(hi);
            d[0] = static_cast<uint8_t>(hi >> 8);
            d[3] = static_cast<uint8_t>(lo);
            d[2] = static_cast<uint8_t>(lo >> 8);
            return 4;
        }

        default:
            // Retail INDEXES THE TABLE UNCHECKED and an id outside 0..5 reads
            // past it. Every caller in the DLL screens `fmt <= 5` first
            // (ConvertText at 0x1800ca349, and the two callers below pass
            // constants), so this arm is unreachable through them; it returns
            // the shared `XOR EAX,EAX` at 0x1800c9d3b rather than reproducing
            // an out-of-bounds jump.
            return 0;
    }
}

// ============================================================================
// 0x1800c9fc0 — DecodeOne(formatEcx /*ECX*/, src /*RDX*/, end /*R8*/,
//                         cp /*R9*/, formatArg5 /*[RSP+0x30]*/)
//   -> bytes consumed. `*cp` is set to -1 on any malformed input, and the
//      return is then 1, so a caller can always make progress.
// ============================================================================
extern "C" int32_t RtIo_TextCodec_DecodeOne(int32_t fmtEcx, const void* srcv,
                                            const void* endv, int32_t* cp,
                                            int32_t fmtArg5) {
    const uint8_t* src = static_cast<const uint8_t*>(srcv);  // CAST-OK: byte_io
    const uint8_t* const end = static_cast<const uint8_t*>(endv);  // CAST-OK: byte_io

    switch (fmtArg5) {                 // the TABLE index is arg5
        case 0:                                                    // 0x1800c9fe1
            *cp = src[0];
            return 1;

        case 1:                                                    // 0x1800c9ff1
            // NOT RETAIL (review 2026-11-03 F-034, heap over-read): retail reads both
            // bytes unchecked, so an odd-length source reads one byte past `end`. A
            // lone trailing byte is malformed here, as a short unit is in case 4 / 5.
            if (end - src < 2) goto bad;
            *cp = src[0] | (static_cast<int32_t>(src[1]) << 8);
            return 2;

        case 2:                                                    // 0x1800ca00a
        case 3: {
            const uint8_t c = src[0];
            if (static_cast<int8_t>(c) >= 0) {      // TEST CL,CL / JNS
                *cp = c;
                return 1;
            }
            int32_t n;     // continuation bytes
            int32_t acc;
            if ((c & 0x60) == 0x40) {                              // 110xxxxx
                if (src + 1 >= end) goto bad;
                n = 1;
                acc = (c & 0x1F) << 6;
                src += 1;
            } else if ((c & 0x70) == 0x60) {                       // 1110xxxx
                if (src + 2 >= end) goto bad;
                n = 2;
                acc = (c & 0x0F) << 12;
                src += 1;
            } else if ((c & 0x78) == 0x70) {                       // 11110xxx
                if (src + 3 >= end) goto bad;
                n = 3;
                acc = (c & 0x07) << 18;
                src += 1;
            } else {
                goto bad;
            }
            // 0x1800ca08e — `*cp` is published BEFORE the continuation loop and
            // OR'd into in place, which is why a malformed tail leaves a
            // partially built value behind on the `bad` path's -1 store.
            *cp = acc;
            {
                int32_t k = n - 1;
                int32_t shift = k * 6;           // LEA ECX,[R8+R8*2] / ADD ECX,ECX
                while (k >= 0) {
                    const uint8_t b = *src;
                    if ((b & 0xC0) != 0x80) goto bad;
                    ++src;
                    *cp |= static_cast<int32_t>(b & 0x3F) << shift;
                    shift -= 6;
                    --k;
                }
            }
            if (*cp > 0x10FFFF) goto bad;
            return n + 1;
        }

        case 4:                                                    // 0x1800ca0e8
        case 5: {
            if (end - src < 2) goto bad;          // `JA`, not `JAE`
            // The ENDIANNESS comes from ECX, the table index from arg5.
            const uint16_t w = (fmtEcx == 4)
                ? static_cast<uint16_t>((src[1] << 8) | src[0])
                : static_cast<uint16_t>((src[0] << 8) | src[1]);

            // `MOV EAX,0x2800 / ADD EAX,ECX / CMP AX,0x3FF / JA` — a 16-bit
            // wrap test for "is this a HIGH surrogate (D800..DBFF)".
            if (static_cast<uint16_t>(0x2800 + w) <= 0x3FF) {
                if (src + 4 >= end) goto bad;     // `JAE`, one stricter than 4
                const uint16_t w2 = (fmtEcx == 4)
                    ? static_cast<uint16_t>((src[3] << 8) | src[2])
                    : static_cast<uint16_t>((src[2] << 8) | src[3]);
                // the same test shifted for DC00..DFFF
                if (static_cast<uint16_t>(0x2400 + w2) > 0x3FF) goto bad;
                *cp = static_cast<int32_t>(((w & 0x3FF) << 10) | (w2 & 0x3FF)) + 0x10000;
                return 4;
            }
            // Not a high surrogate. An UNPAIRED LOW surrogate is malformed.
            if (static_cast<uint16_t>(0x2400 + w) <= 0x3FF) goto bad;
            *cp = w;
            return 2;
        }

        default:
            // See EncodeOne's default: retail indexes unchecked and every
            // caller screens first.
            goto bad;
    }

bad:                                                               // 0x1800ca0d7
    *cp = -1;
    return 1;
}

// ============================================================================
// 0x1800c9d60 — DetectFormat(src /*RCX*/, len /*EDX*/) -> format id
//
//   EF BB BF  -> 3      FF FE -> 4      FE FF -> 5
//   otherwise: the whole buffer is decoded as UTF-8; 2 if every codepoint comes
//   back non-negative, 0 on the first malformed one.
//
// The three BOM literals are built on the stack as 16-bit immediates plus a
// trailing byte (`MOV WORD [RBP+0x18],0xBBEF / MOV BYTE [RBP+0x1A],0xBF`), so
// the byte order in the file is EF BB BF / FF FE / FE FF.
// ============================================================================
extern "C" int32_t RtIo_TextCodec_DetectFormat(const uint8_t* src, int32_t len) {

    static const uint8_t kUtf8Bom[3]    = { 0xEF, 0xBB, 0xBF };
    static const uint8_t kUtf16LeBom[2] = { 0xFF, 0xFE };
    static const uint8_t kUtf16BeBom[2] = { 0xFE, 0xFF };

    if (len >= 3 && src[0] == kUtf8Bom[0] && src[1] == kUtf8Bom[1] &&
                    src[2] == kUtf8Bom[2])
        return 3;                                                  // 0x1800c9db9
    if (len >= 2 && src[0] == kUtf16LeBom[0] && src[1] == kUtf16LeBom[1])
        return 4;                                                  // 0x1800c9e09
    if (len >= 2 && src[0] == kUtf16BeBom[0] && src[1] == kUtf16BeBom[1])
        return 5;                                                  // 0x1800c9e58

    // 0x1800c9e68 — validate as UTF-8. `cp` is seeded to -1 before the loop
    // and NOT re-seeded inside it; DecodeOne writes it on every call.
    const uint8_t* p = src;
    const uint8_t* const end = src + len;
    int32_t cp = -1;
    while (p < end) {
        const int32_t used = RtIo_TextCodec_DecodeOne(2, p, end, &cp, 2);
        if (cp < 0) return 0;                                      // 0x1800c9ecb
        p += used;
    }
    return 2;                                                      // 0x1800c9eac
}

// ============================================================================
// 0x1800ca260 — ConvertText, nine arguments.
//
//   int32_t ConvertText(int32_t srcFmt, uint8_t forceDetect, const void* src,
//                       int32_t srcLen, int32_t dstFmt, int32_t scratch,
//                       uint8_t appendTerminator, void* dst, int32_t dstCap)
//
// Returns the number of bytes the conversion PRODUCES, whether or not they fit
// — so the documented two-pass use is: call once with `dst == nullptr` to size,
// then again with a buffer.
//
// ARG6 (`scratch`) IS NEVER READ BY THE CALLER AND NEVER READ AS AN ARGUMENT.
// The callee uses its home slot (`[RSP+0x88]`) as the decoded-codepoint
// variable, writing -1 into it before every DecodeOne. text_file_stream.cpp
// already worked this out and it is confirmed here: `MOV DWORD [RSP+0x88],-1`
// at 0x1800ca300 is the first instruction of the loop. The parameter stays in
// the signature because removing it would shift args 7, 8 and 9.
// ============================================================================
extern "C" int32_t RtIo_TextCodec_ConvertText(int32_t srcFmt, uint8_t forceDetect,
                                              const uint8_t* src, int32_t srcLen,
                                              int32_t dstFmt, int32_t /*scratch*/,
                                              uint8_t appendTerminator,
                                              uint8_t* const dst, int32_t dstCap) {
    const uint8_t* const srcEnd = src + srcLen;

    // NOT RETAIL (review 2026-11-03 F-058, UB): retail counts in EBX and wraps once the
    // output passes 2 GB (a source above about 1.4 GB); int32 overflow is UB here. The
    // count is kept in 64 bits and the return saturates at INT32_MAX. Below 2 GB every
    // comparison and the return are retail's.
    int64_t produced = 0;                                   // EBX
    int32_t fmt = srcFmt;                                   // EBP

    // 0x1800ca289 — detect when asked to, OR when the id is the magic 6.
    if (forceDetect != 0 || srcFmt == 6) {
        fmt = RtIo_TextCodec_DetectFormat(src, srcLen);
        int32_t skip;                                       // 0x1800ca2a1 ladder
        switch (fmt) {
            case 3:  skip = 3; break;
            case 4:  skip = 2; break;
            case 5:  skip = 2; break;
            default: skip = 0; break;      // includes 0 and 2
        }
        src += skip;
    }

    uint8_t* out = dst;                                     // R13, the write cursor
    uint8_t buf[8];                                         // [RSP+0x68]
    int32_t termLen = 1;                                    // R14D, seeded to 1

    while (src < srcEnd) {                                  // 0x1800ca2eb / ca300
        int32_t cp = -1;                                    // [RSP+0x88]
        int32_t used;
        if (static_cast<uint32_t>(fmt) <= 5) {
            used = RtIo_TextCodec_DecodeOne(fmt, src, srcEnd, &cp, fmt);
        } else {
            cp = -1;                                        // OR ECX,-1
            used = termLen;                                 // MOV EAX,R14D  (== 1)
        }
        src += used;

        if (cp < 0) continue;                               // JS to the loop tail

        int32_t n;
        if (static_cast<uint32_t>(dstFmt) <= 5)
            n = RtIo_TextCodec_EncodeOne(cp, buf, dstFmt);
        else
            n = 0;

        produced += n;
        // NOTE: overflow does NOT end the loop, it only skips the copy — which
        // is what lets a `dst == nullptr` sizing call still return the full
        // length. Both tests jump to the same loop tail.
        if (produced > dstCap) continue;
        if (dst == nullptr) continue;
        if (n > 0) {
            std::memcpy(out, buf, static_cast<size_t>(n));   // 0x18008a290
            out += n;
        }
    }

    // 0x1800ca39d — the optional terminator, sized by the DESTINATION format
    // through a second six-entry table at 0x1800ca420: ids 0/2/3 -> one zero
    // byte, ids 1/4/5 -> one zero word.
    if (appendTerminator != 0) {
        if (static_cast<uint32_t>(dstFmt) <= 5) {
            switch (dstFmt) {
                case 0: case 2: case 3:
                    buf[0] = 0;
                    termLen = 1;           // R14D is still 1 from the seed
                    break;
                default:                   // 1, 4, 5
                    buf[0] = 0; buf[1] = 0;
                    termLen = 2;
                    break;
            }
        } else {
            termLen = 0;
        }
        produced += termLen;
        // `LEA EDX,[R14+RBX] / CMP EDX,EDI / JG` — the check adds termLen a
        // SECOND time, after it has already been folded into `produced`.
        // Transcribed, not corrected.
        if (termLen + produced <= dstCap && dst != nullptr && termLen > 0)
            std::memcpy(out, buf, static_cast<size_t>(termLen));
    }

    if (produced > INT32_MAX)                               // NOT RETAIL (F-058)
        return INT32_MAX;
    return static_cast<int32_t>(produced);
}

// ============================================================================
// 0x180029bb0 — serialize `count` System::String values at `data`.
//
//   void SerializeStringArray(IO::Serializer* ser /*RCX*/, int32_t count /*EDX*/,
//                             System::String* data /*R8*/, void* /*R9, spilled
//                             into the home slot and never read*/)
//
// `count <= 0` returns before the prologue.
//
// ---------------------------------------------------------------------------
// THE TOP BIT OF THE LENGTH IS A FORMAT FLAG, and it is the whole reason this
// body needs the codecs at all.
// ---------------------------------------------------------------------------
// Per element retail emits `(len + 1) | 0x80000000` as a kScalarUInt, where
// `len` is `String`'s stored length at `data - 8`. On the WRITE side the bit is
// always set. On the READ side the value comes back from the stream, and:
//
//   * bit 31 SET   -> the payload is a plain byte array; read it with
//                     kArrayChar straight into the String (the PLAIN path).
//   * bit 31 CLEAR -> the payload is an array of UTF-16 code units written by
//                     an OLDER format; read it with kArrayUShort into a
//                     temporary and TRANSCODE it to UTF-8 in place.
//
// So a save written by this build is never transcoded, and a save written
// before the flag existed still loads. `BL` is computed as `!(value >> 31)`
// (`SHR/NOT/AND 1`) and the bit is then cleared before anything else uses the
// length.
//
// THE TRANSCODE IS TWO PASSES over the same temporary, and both are needed:
// the first measures the UTF-8 length so `String::SetLength` can size the
// buffer once, the second writes the bytes. The first pass hands EncodeOne the
// CODEPOINT SLOT ITSELF as its destination (`LEA RDX,[RSP+0xB8]` at
// 0x180029d42) — harmless, because only the return value is used, and
// transcribed rather than tidied.
//
// The decode format is 1 (raw 16-bit little-endian, no surrogates), not 4:
// `MOV ECX,1 / MOV DWORD [RSP+0x20],1`. The encode format is 2 (UTF-8).
//
// THE TEMPORARY IS ALLOCATED LOCKED AND FREED UNLOCKED — `MemAlloc` under the
// allocator's critical section at 0x180029c90, `0x1800c7ad0` bare at
// 0x180029e14. That asymmetry is retail's.
// ============================================================================
extern "C" void RtIo_SerializeStringArray(::IO::Serializer* ser, int32_t count,
                                          ::System::String* data, void* /*unused*/) {
    if (count <= 0) return;                                        // 0x180029bb0

    ::System::String* slot = data;                                 // R12

    for (int32_t i = 0; i < count; ++i, ++slot) {                  // 0x180029bf0
        // `len + 1`, or 0 for a null String. The length lives at `str - 8`
        // (the refcount is at `str - 4`).
        uint32_t tagged = 0;
        if (slot->_data != nullptr)
            tagged = static_cast<uint32_t>(
                *reinterpret_cast<const int32_t*>(slot->_data - 8) + 1);  // CAST-OK: string_header
        tagged |= 0x80000000u;                                     // BTS EAX,31

        ser->_readerWriter->SerializeUInt(tagged, nullptr);    // +0x98

        const bool wasWide = (tagged >> 31) == 0;                  // SHR/NOT/AND 1
        uint32_t len = tagged & 0x7FFFFFFFu;                       // BTR EDX,31

        const bool reading = (ser->_stream->_direction == rt::io::kDirectionReading);

        // NOT RETAIL (hardening, review 2026-11-03 F-079): retail sizes the String and
        // the UTF-16 temporary straight from the length word (2 GB / 4 GB from a 5-byte
        // record). A length the rest of a binary stream cannot hold leaves this String
        // empty and reads nothing for it; a well-formed table always fits, and a
        // non-binary reading stream is never capped (IO::StringLengthExceedsStream).
        if (reading && len != 0) {
            const uint64_t need = wasWide ? static_cast<uint64_t>(len) * 2
                                          : static_cast<uint64_t>(len);
            if (::IO::StringLengthExceedsStream(*ser->_stream, need)) {
                ::System::StringPrivateAccess::Release(*slot);
                slot->_data = nullptr;
                continue;
            }
        }

        if (reading && len != 0)
            ::System::StringPrivateAccess::SetLength(*slot,
                                                     static_cast<int>(len) - 1);   // 0x1800c8220

        if (!wasWide || !reading || len == 0) {
            // ---- PLAIN: the payload is bytes --------------------- 0x180029e23
            ser->_readerWriter->SerializeCharArray(
                static_cast<int32_t>(len), slot->_data, nullptr);  // +0x18
            // NOT RETAIL (review 2026-11-03 F-023, heap over-read): the last of the
            // len bytes is the terminator only when the writer put a NUL there; retail
            // never stores one. A well-formed record already has it.
            if (reading && len != 0)
                slot->_data[len - 1] = 0;
            continue;
        }

        // ---- WIDE: read UTF-16 units into a temporary and transcode --------
        const uint64_t bytes = static_cast<uint64_t>(len) * 2;     // ADD RBX,RBX
        uint8_t* const tmp = static_cast<uint8_t*>(::System::MemoryAllocator::MemAlloc(bytes, 4));

        ser->_readerWriter->SerializeUShortArray(
            static_cast<int32_t>(len),
            reinterpret_cast<uint16_t*>(tmp), nullptr);            // +0x50  // CAST-OK: byte_io

        // `len*2 - 2`: the trailing NUL code unit is not transcoded.
        // NOT RETAIL (review 2026-11-03 F-059 / F-060, UB): retail doubles len in a
        // 32-bit register, which overflows int32 for len >= 0x40000000; the offset is
        // computed in 64 bits, the same value for every len below that.
        const uint8_t* const wideEnd =
            tmp + (static_cast<int64_t>(len) * 2 - 2);

        // PASS 1 — measure.                                        0x180029d00
        int32_t utf8Len = 0;
        for (const uint8_t* p = tmp; p < wideEnd; ) {
            int32_t cp = -1;
            p += RtIo_TextCodec_DecodeOne(1, p, wideEnd, &cp, 1);
            if (cp >= 0)
                utf8Len += RtIo_TextCodec_EncodeOne(cp, &cp, 2);    // dst == &cp
        }

        ::System::StringPrivateAccess::SetLength(*slot, utf8Len);  // 0x180029d5b
        slot->_data[utf8Len] = 0;                                  // 0x180029d67

        // PASS 2 — write.                                          0x180029d94
        char* const base = slot->_data;
        char* out = base;
        int32_t written = 0;
        uint8_t enc[8];                                            // [RSP+0x30]
        for (const uint8_t* p = tmp; p < wideEnd; ) {
            int32_t cp = -1;
            p += RtIo_TextCodec_DecodeOne(1, p, wideEnd, &cp, 1);
            if (cp < 0) continue;
            const int32_t n = RtIo_TextCodec_EncodeOne(cp, enc, 2);
            written += n;
            if (written > utf8Len) continue;
            if (base == nullptr) continue;
            if (n > 0) {
                std::memcpy(out, enc, static_cast<size_t>(n));     // 0x18008a290
                out += n;
            }
        }

        ::System::MemoryAllocator::MemFree(tmp);                   // 0x180029e14
    }
}
