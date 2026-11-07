// ============================================================================
// src/engine/io/package_file_read.cpp — Wave 9 lane PKG3
// ?ReadPackfile@PackageFile@IO@@QEAA_NAEBVFilename@System@@@Z  0x1800b46b0
// ============================================================================
//
// THE LAST BODY BETWEEN LoadPackages AND A MOUNTED PACKAGE. Lane PKG2 ported
// every callee across five rungs and then spent four commits decoding this
// function without writing it (WAVE9_LANE_PKG2.md §67-§90). This file is the
// transcription, plus the two targeted reads §87 said had to happen first.
//
// 1,864 instructions in the shipped body; ~660 of distinct logic once the 28
// inlined copies of `?Read@CompressedStream@IO@@` collapse back to calls. That
// substitution is not a paraphrase and it is proved twice — §68 (the inlined
// loop IS rung 2's body, and the copies discard the return value exactly as
// ignoring the `bool` does) and §75 (retail itself left TWO of the sites as
// real `CALL 0x1800aa780`, to the same function).
//
// ONE DIRECT OS CALL: `SetFilePointer` at 0x1800b47e3, through Runtime's IAT
// slot 0x18014b0f0, and only on the loose-file branch. It is
// Platform::FileSeek32 since Phase D.4 THREADS E.5, which moved this file out of
// src/platform/win (it was `package_file_read_win.cpp`).
//
//
// ---------------------------------------------------------------------------
// §91. THE TWO READS §87 REFUSED TO GUESS — BOTH ANSWERED, AND ONE OF THEM
//      WAS ABOUT TO BE GUESSED THE WRONG WAY ROUND BY FIELD NAME
// ---------------------------------------------------------------------------
// §87 stopped on the packfile-entry record's field assignment because "which
// goes to +0x08 and which to +0x0c is a coin flip on a field pair whose swap
// produces a packfile index that is wrong in a way no green run detects".
// The four instructions are at 0x1800b5fa4, and the frame identity that reads
// them is `RBP = RSP + 0x100` (from `LEA RBP,[RSP-0x1860]` at 0x1800b46c0
// followed by `SUB RSP,0x1960`; every other slot in this file is resolved by
// that same equation and each one cross-checks):
//
//   1800b5fa4  MOV EAX,dword ptr [RBP + -0x10]   ; the FIRST of the two reads
//   1800b5fa7  MOV dword ptr [RBP + -0x18],EAX   ; -> temp + 0x08  == offset
//   1800b5faa  MOV EAX,dword ptr [RSP + 0x3c]    ; the SECOND
//   1800b5fae  MOV dword ptr [RBP + -0x14],EAX   ; -> temp + 0x0c  == size
//
// The temp is at `[RBP-0x20]`, so `-0x18` is `+0x08` and `-0x14` is `+0x0c`.
// Against `PackageFileEntry` in src/engine/io/FileSystem.h (+0x08 offset,
// +0x0C size, both pinned by the accessors' one-instruction bodies) that makes
// the ORDER ON DISK **offset first, size second**.
//
// And the second read, at 0x1800b5fbe:
//
//   1800b5fb1  MOV EDX,dword ptr [R13 + 0x8]     ; entries._size
//   1800b5fb5  INC EDX                           ; n = _size + 1
//   1800b5fb7  LEA R8,[RBP + -0x20]              ; fill = &temp
//   1800b5fbb  MOV RCX,R13
//   1800b5fbe  CALL 0x1800298f0                  ; Array<Entry>::SetSize(a,n,fill)
//
// `_size + 1`, as §87 predicted — and R13 is `this + 0xb0` (set at 0x1800b5b86),
// which independently re-confirms the `entries` / `entryCount` pair at +0xB0 /
// +0xB8 that FileSystem.h carries.
//
// ---------------------------------------------------------------------------
// §92. FOUR CORRECTIONS TO THE RECORDED DECODE. Three change what gets written.
// ---------------------------------------------------------------------------
//
// (a) **The +0xAC read is ONE BYTE, not four.** §75's table lists eight fields
//     as "4 each" and +0xac is in that list. At 0x1800b4b4b the count register
//     is `MOV ESI,0x1`, not `MOV ESI,0x4`; every other field in the run is 4.
//     +0xAC is `overridable`, a `uint8_t` in FileSystem.h — so retail reads a
//     one-byte bool, and a 4-byte read there would have consumed three bytes of
//     the next field (`crc`, +0xA8) and desynced everything after it. This is
//     the transcription's single most consequential correction.
//
// (b) **The uncompressed ELSE arm writes FOUR MORE FIELDS than §86 recorded.**
//     §86 has it as `+0x78 = 1; +0x7c = 1; SetSize(+0x80,1); [0] = 0xff000000`.
//     The full arm at 0x1800b5b25 is:
//         +0x68 = 0x010007ff   (dword)      <- MISSING from §86
//         +0x70 = 0x2710       (QWORD: priority 10000, platform 0)  <- MISSING
//         +0xa8 = 0            (crc)                                <- MISSING
//         +0xac = 1            (overridable)                        <- MISSING
//         +0x78 = 1;  +0x7c = 1;  Array<int32>::SetSize(+0x80, 1);
//         ((int32*)this->[0x80])[0] = 0xff000000
//     Three of the four are re-stating what the constructor already wrote, so
//     omitting them would have been invisible — except +0x68, which the
//     constructor leaves at 0 and which `?GetVersion@` returns.
//
// (c) **PATH A and PATH B do NOT merge at the same address.** §80 says groups
//     1-3 `JMP 0x1800b5b6c`; they do. Groups 4-6 jump to **0x1800b5b73**, one
//     instruction later — skipping `MOV R15D,[RBP+0x18b0]`. That is not
//     cosmetic: on PATH A the format version has to be RELOADED from the stack
//     before the entry loop reads it, because PATH A's decode loop clobbers
//     R15; on PATH B it is still live. Merging both arms at the same point in a
//     C++ transcription is harmless (the value is the same either way), and the
//     asymmetry is recorded here rather than silently smoothed over.
//
// (d) **`[RBP+0x18a0]` is `this`'s incoming home slot**, not a scratch length
//     slot as §80's table implies. `MOV [RSP+0x8],RCX` at 0x1800b46b0 stores
//     `this` there, and PATH A groups 1 and 2 RELOAD `this` from it after their
//     decode loops (0x1800b4fba, 0x1800b522a) because the loop uses R13 as a
//     constant register. Group 3 then reuses the slot as its String temp
//     (0x1800b54c2) — which is why group 3 is last and why nothing reloads
//     `this` after it. In C++ `this` is a parameter and none of this survives;
//     it is written down because reading §80's table as "these are two length
//     temps" is how a transcription ends up spilling a live pointer over.
//
// ---------------------------------------------------------------------------
// §93. THE TWO STACK BUFFERS OVERLAP, AND THE 0x800 CAP IS THE OVERLAP POINT
// ---------------------------------------------------------------------------
// The decoded-output buffer is `[RBP+0x60]` and the raw-read buffer is
// `[RBP+0x860]`. **0x860 - 0x60 = 0x800**, and 0x800 is exactly the cap the
// re-encode loop enforces (`CMP ESI,0x800 ; JG`). They are one region: the
// output buffer runs from the frame slot up to where the input buffer begins,
// and the cap is what stops the decoder from writing over its own input. So
// this file models them as ONE array with `buf860 = buf60 + 0x800`, which is
// what retail has, rather than as two independent arrays — which would silently
// change what happens on any packfile that hits the cap.
//
// Neither read is bounds-checked against the frame in retail (`Read(len, buf860)`
// with `len` straight off disk). NOT RETAIL since review 2026-11-03: a length
// that does not fit its buffer is a stack overflow (UB), and the package is
// rejected instead (see "the hardening" below). Every valid package fits.
//
// ---------------------------------------------------------------------------
// THE HARDENING (review 2026-11-03; NOT RETAIL, no fix.* row)
// ---------------------------------------------------------------------------
// Retail trusts every length in the index. These checks reject what a valid
// package never contains, at the parse, and change nothing a valid package reads:
//  * a negative blob length, or a blob the file does not hold (F-033);
//  * a raw string or name longer than the frame less its terminator, a UTF-16
//    one longer than buf860 less the frame's last byte, kept zero as the
//    terminator a full decode does not write (F-019, F-020, F-021);
//  * an icon or preview larger than the bytes the index has left (F-055,
//    F-056, F-076);
//  * a file count the index cannot hold at 12 bytes an entry (F-077).
// Two broken fields shipped mods carry are NOT rejected: a negative preview
// count reads as no preview (retail's reading), and a negative icon width or
// height loads the mod with a 48x48 "?" icon (SetPlaceholderIcon).
// A rejected package returns false with its file closed and no entry kept: the
// state of a package whose file never opened, which both callers handle.
// Three UB fixes keep retail's reads otherwise: the frame starts zeroed (F-054),
// a raw read is terminated (F-018), and the UTF-16 decode stops at the end of
// buf860 instead of walking off the frame (F-017).
//
// ---------------------------------------------------------------------------
// §94. THE CODEC PAIR IS TRANSCRIBED FROM THE JUMP-TABLE BODIES
// ---------------------------------------------------------------------------
// The two dispatchers are `0x1800c9fc0` (decode) and `0x1800c9b60` (encode),
// both a `JMP` through a table indexed by an encoding id. Ghidra cannot make
// the table bodies into functions, so src/engine/system/string.cpp and
// widestring.cpp both wrote their pair FROM SPEC and said so. This file does
// not have to: the two entries ReadPackfile actually uses were read out of
// .rdata and disassembled by hand, and they are nine and about forty bytes.
//
//   decode table 0x1800ca1b8, entry **1** -> 0x1800c9ff1, NINE instructions:
//       0fb602 0fb64a01 c1e108 0bc8 b802000000 418909 4883c408 c3
//     = `*cp = p[0] | (p[1] << 8); return 2;`
//     UTF-16LE, TWO BYTES PER CODEPOINT, **`end` is accepted and never read**,
//     no surrogate handling, no validation. This is §76's over-read at its
//     source: the caller reads `len` bytes and then walks `2 * len`.
//
//     NOTE the id: ReadPackfile passes **1**, not the 4 that widestring.cpp's
//     `Utf16DecodeOne` reproduces. Table entry 4 (0x1800ca0e8) is a different,
//     surrogate-aware body. Reusing widestring's helper here because "it is the
//     UTF-16 decoder" would have been the fourth by-resemblance defect this
//     lane has caught (cf. §51's alignment, §74's AdjustDelimiters).
//
//   encode table 0x1800c9d40, entry **2** -> 0x1800c9baf: plain UTF-8, the
//     four-way `< 0x80 / < 0x800 / < 0x10000 / < 0x200000` ladder, returning
//     1/2/3/4. Since the decoder above can only ever produce 0..0xFFFF, the
//     4-byte and error arms are UNREACHABLE from this caller; they are written
//     anyway (the ladder is the body) and the fact is recorded rather than the
//     arms omitted.
//
// ---------------------------------------------------------------------------
// §95. THE ORACLE, AND THE ONE EXCLUSION §76 REQUIRES
// ---------------------------------------------------------------------------
// `ReadPackfile` is not pure, but it is REPLAYABLE: everything it produces
// lands in `*this`, and `this` can be a second, freshly-constructed scratch
// object. So the oracle runs our body on the real object, then constructs a
// scratch `PackageFile` with our own (byte-proven, §5) constructor and runs the
// SHIPPED body on it with the same filename, then compares field by field.
//
// EXCLUSIONS, each with the instruction that justifies it:
//
//  * `_fileInfo.winHandle` (+0xD8) — two independent `CreateFileW` calls return
//    two different HANDLEs. Compared as "both valid or both -1", not by value.
//  * `_fileInfo._unk04` (+0xC4) — `?OpenFile@` never writes it
//    (filesystem_openfile.cpp §4); comparing memory neither side wrote is
//    reference_oracle_must_not_compare_unwritten_bytes.md.
//  * `_fileInfo._name0/_name1` — different String objects; compared by TEXT.
//  * **AND THE ONE THAT MATTERS: a string whose real data does not terminate
//    inside the bytes actually read.** §76: retail decodes `2 * len` bytes out
//    of a buffer it filled with `len`, so the tail comes from whatever was on
//    the stack. Ours and theirs decode from DIFFERENT stack frames with
//    DIFFERENT stale contents, and the resulting `strlen` — and therefore the
//    whole String, not just its tail — is legitimately allowed to differ.
//
//    This file does NOT hand-wave that as "compare up to the terminator". It
//    DECIDES it, per string, from data both sides read identically: a string is
//    DETERMINISTIC iff a terminator appears within the region actually read
//    (a zero 16-bit unit fully inside `[0, len)` on the UTF-16 arm, a zero byte
//    inside `[0, len)` on the raw arm). Deterministic strings are compared
//    strictly. Non-deterministic ones are counted and reported by name in the
//    census line, and excluded from the verdict. A run in which that count is
//    zero has NO exclusion in force at all, and says so.
//
// ---------------------------------------------------------------------------
// §96. COVERAGE — READ THE CENSUS LINE, NOT THE `divergences=0`
// ---------------------------------------------------------------------------
// Two conditionals gate large amounts of code and a corpus of shipped `.pkg`
// files probably cannot cover both sides of either (§72, §81):
//
//   * the header's SIGN BIT (`compressed`) — gates the whole metadata block;
//   * the FORMAT VERSION (`> 2` vs `<= 2`) — selects between two entirely
//     separate string implementations, TWICE (metadata and per-entry names),
//     and in the entry loop the arms are in the OPPOSITE order.
//
// So this file counts, INSIDE ITS OWN BODY, how many times each arm was taken,
// and prints them. A branch with `0` is UNTESTED and the report must say so; a
// green run over one arm is not coverage of the other. Counting branches taken
// inside the function rather than proxies observed outside it is the correction
// §65 earned twice.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "platform_file.h"    // Platform::FileSeek32
#include "io/FileSystem.h"
#include "io/PackageFile.h"   // B.3.4 PACKAGE+FS: the real class
#include "io/Stream.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/string_internal.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"
#include "system/Configuration.h"   // System::kGameVersion (the uncompressed default)
#include "system/Value.h"           // System::kColorWhite (the placeholder icon ink)

// ---- the ported callees, called DIRECTLY --------------------------------
// Every one of these is this lane's own port from an earlier rung. They are
// called by name, not through a resolver door: a door here would only be a way
// to reach the shipped body by accident
// (reference_bypassed_port_needs_a_source_checker.md).
namespace RtIO {
}  // namespace RtIO


namespace RtIO {



namespace {

// ---- the frame's two overlapping stack buffers, §93 ------------------------
// `[RBP+0x60] .. [RBP+0x18a0)` is 0x1840 bytes; the raw-read buffer starts
// 0x800 into it. Sized and positioned exactly as retail's frame.
constexpr size_t kFrameBufBytes = 0x1840;
constexpr size_t kOutCap        = 0x800;    // = 0x860 - 0x60
// NOT RETAIL (review 2026-11-03): the longest length each buffer takes. Both keep
// the frame's last byte for a terminator. The raw arm writes it; the UTF-16 arm
// leaves it zero, because a read that fills buf860 to the frame's end and a
// decode that fills buf60 (no terminator then) would be strlen'd past the frame.
constexpr int32_t kRawMaxLen    = static_cast<int32_t>(kFrameBufBytes) - 1;            // 0x183f
constexpr int64_t kBuf860Bytes  = static_cast<int64_t>(kFrameBufBytes - kOutCap);      // 0x1040
constexpr int32_t kUtf16MaxLen  = static_cast<int32_t>(kBuf860Bytes) - 1;              // 0x103f

// `?_singleton@FileSystem@IO@@0PEAV12@EA`  (0x1801fc6a8) — the global retail
// loads into RSI at 0x1800b46e8 and RELOADS at 0x1800b478b.

// B3R RT-IO: the per-file MemAlloc / MemFree (0x1800c7a50 / 0x1800c7ad0) and
// Array SetSize / SetSizeFill (0x180029510, 0x180029630, 0x1800298f0) helpers
// that stood here are direct calls: System::MemoryAllocator::MemAlloc /
// MemFree and the rtcoll bodies by name.


// ---- §94: the two jump-table bodies ---------------------------------------

// decode table 0x1800ca1b8 entry 1 -> 0x1800c9ff1. Nine instructions, and
// `end` (R8) is accepted and NEVER READ. That is the over-read, at its source.
inline int32_t Decode1(const uint8_t* p, const uint8_t* /*end, ignored*/,
                       int32_t* cp) {
    const uint32_t lo = p[0];                            // 1800c9ff1 MOVZX EAX,[RDX]
    const uint32_t hi = p[1];                            // 1800c9ff4 MOVZX ECX,[RDX+1]
    *cp = static_cast<int32_t>((hi << 8) | lo);          // 1800c9ff8 SHL/OR/MOV [R9]
    return 2;                                            // 1800c9ffd MOV EAX,2
}

// encode table 0x1800c9d40 entry 2 -> 0x1800c9baf. The comparisons are SIGNED
// (`JL` / `JGE`), which is why a negative codepoint would take the one-byte
// arm — the caller never lets one through (`TEST ECX,ECX ; JS`).
inline int32_t Encode2(int32_t cp, uint8_t* dst) {
    if (cp < 0x80) {                                     // 1800c9baf CMP 0x80 / JL
        dst[0] = static_cast<uint8_t>(cp);               // 1800c9b88
        return 1;
    }
    if (cp < 0x800) {                                    // 1800c9bb8 CMP 0x800 / JGE
        dst[0] = static_cast<uint8_t>(((cp >> 6) & 0x1F) | 0xC0);   // 1800c9bc1..cf
        dst[1] = static_cast<uint8_t>((cp & 0x3F) | 0x80);          // 1800c9bd3
        return 2;
    }
    if (cp < 0x10000) {                                  // 1800c9be1 CMP 0x10000 / JGE
        dst[0] = static_cast<uint8_t>(((cp >> 12) & 0x0F) | 0xE0);  // 1800c9bea..f4
        dst[1] = static_cast<uint8_t>(((cp >> 6)  & 0x3F) | 0x80);  // 1800c9bf6..c08
        dst[2] = static_cast<uint8_t>((cp & 0x3F) | 0x80);          // 1800c9c0b
        return 3;
    }
    // UNREACHABLE FROM THIS CALLER: Decode1 tops out at 0xFFFF. Written because
    // the ladder is the body, and flagged rather than omitted.
    if (cp < 0x200000) {                                 // 1800c9c14 CMP 0x200000
        dst[0] = static_cast<uint8_t>(((cp >> 18) & 0x07) | 0xF0);
        dst[1] = static_cast<uint8_t>(((cp >> 12) & 0x3F) | 0x80);
        dst[2] = static_cast<uint8_t>(((cp >> 6)  & 0x3F) | 0x80);
        dst[3] = static_cast<uint8_t>((cp & 0x3F) | 0x80);
        return 4;
    }
    return 0;                                            // the 0x1800c9d3b error arm
}

// ---- the String tail every one of the six metadata groups and the entry
//      loop share (0x1800b4fce..0x1800b509d and its five clones) -------------
//
// THREE THINGS A TIDIED-UP VERSION GETS WRONG, all reproduced:
//  1. `strlen(buf)` is computed TWICE, into two different registers, with an
//     allocation in between. They cannot differ here, but the second one is
//     what the copy length is built from and collapsing them is a change to
//     what the code does.
//  2. `CMP ESI,EDI ; CMOVL EAX,ESI` on `EAX = EDI` is `min`, not `max` — the
//     copy length is `min(len + 1, strlen + 1)`.
//  3. The trailing `if (n2 > cap) data[cap-1] = 0` is retail's own truncation
//     guard, and it indexes `cap - 1`, not `n2 - 1`.
// B.4 Z LAST IO: `tmp` is the caller's String local; its null store
// (1800b4fce) is that local's default ctor, run at its declaration.
void BuildStringFrom(const uint8_t* buf, System::String& tmp) {
    int32_t n1 = 0;                                        // 1800b4fd3..4fe9
    while (buf[n1] != 0) ++n1;
    System::StringPrivateAccess::SetLength(tmp, n1);       // 1800b4ff2  0x1800c8220

    const int32_t cap = n1 + 1;                            // 1800b4ff7  LEA ESI,[RBX+1]
    int32_t n2 = 0;                                        // 1800b4ffa..5019
    while (buf[n2] != 0) ++n2;
    ++n2;                                                  // 1800b501b  INC EDI

    const int32_t copyLen = (cap < n2) ? cap : n2;         // 1800b501d..5021  MIN
    std::memcpy(tmp._data, buf, static_cast<size_t>(copyLen));   // 1800b5033
    if (n2 > cap)                                          // 1800b5038  CMP EDI,ESI / JLE
        tmp._data[cap - 1] = 0;                            // 1800b503f
}

// (B.4 Z LAST IO: `AssignStringField` is gone. Its six metadata uses were
// `String::operator=` (0x1800b5044 and clones) and are spelled as that; its
// entry-loop use was the inlined Filename copy ctor of the entry's name.)

// ---- §95's determinism test ------------------------------------------------
// "Did a terminator appear inside the bytes actually read?" — decided from data
// both sides read identically, so the answer itself cannot diverge.
bool Utf16TerminatorInRead(const uint8_t* buf, int32_t len) {
    for (int32_t i = 0; i + 1 < len; i += 2)
        if (buf[i] == 0 && buf[i + 1] == 0) return true;
    return false;
}
bool RawTerminatorInRead(const uint8_t* buf, int32_t len) {
    for (int32_t i = 0; i < len; ++i)
        if (buf[i] == 0) return true;
    return false;
}

// ---- PATH A's string group (0x1800b4e1f, twice more at 5309/. and 530d) ----
// Read a length, read that many BYTES, then decode `2 * len` of them as
// UTF-16LE and re-encode to UTF-8 into the overlapping output buffer.
// Returns whether the result is deterministic (§95).
// `cs->IO::CompressedStream::Read` is retail's direct CALL 0x1800aa780 (a
// qualified, non-virtual call, as the door that stood here was).
// `malformed` is set, and nothing read, for a length buf860 cannot hold.
bool ReadStringUtf16(IO::CompressedStream* cs, uint8_t* buf60, uint8_t* buf860,
                     System::String& tmp, bool& malformed) {
    int32_t len = 0;                                       // 1800b4e22
    cs->IO::CompressedStream::Read(4, &len);               // 1800b4e2e group
    // NOT RETAIL (review 2026-11-03, F-019): retail reads `len` bytes into buf860,
    // which has 0x1040 before the frame ends; a longer length overflows the stack
    // (UB), and 0x1040 itself leaves no terminator for strlen inside the frame
    // (an out-of-frame read, UB). Rejected at the parse; retail has no check.
    if (len > kUtf16MaxLen) {
        malformed = true;
        return false;
    }
    if (len > 0)                                           // 1800b4eba  TEST/JLE
        cs->IO::CompressedStream::Read(len, buf860);       // 1800b4eb3 group

    const bool deterministic = (len <= 0) || Utf16TerminatorInRead(buf860, len);

    // `LEA EAX,[R15+R15*1] ; CDQE` — TWICE what was read. §76.
    // NOT RETAIL (review 2026-11-03, F-017): retail's 2 * len walks past the frame
    // for len > 0x820 (an out-of-bounds stack read, UB). The walk stops at buf860's
    // end; below that it is retail's, stale bytes and all.
    int64_t walk = 2 * static_cast<int64_t>(len);
    if (walk > kBuf860Bytes)
        walk = kBuf860Bytes;
    const uint8_t* src       = buf860;                     // 1800b4f2a  RDI
    const uint8_t* const end = buf860 + walk;              // 1800b4f23/f33
    uint8_t*       dst       = buf60;                      // 1800b4f3d  R14
    int32_t        total     = 0;                          // 1800b4f27  ESI

    while (src < end) {                                    // 1800b4f48/fb5  unsigned CMP
        int32_t cp = -1;                                   // 1800b4f53
        src += Decode1(src, end, &cp);                     // 1800b4f6e / f73
        if (cp >= 0) {                                     // 1800b4f7d  TEST/JS
            uint8_t scratch[4];
            const int32_t m = Encode2(cp, scratch);        // 1800b4f8c
            total += m;                                    // 1800b4f91
            // The cap SKIPS THE COPY and keeps looping — it does not break.
            if (total <= 0x800 && m > 0) {                 // 1800b4f93/9e
                std::memcpy(dst, scratch, static_cast<size_t>(m));   // 1800b4fad
                dst += m;                                  // 1800b4fb2
            }
        }
    }
    if (total + 2 <= 0x800)                                // 1800b4fc1/c4  LEA/CMP/JG
        *dst = 0;                                          // 1800b4fcb

    BuildStringFrom(buf60, tmp);
    return deterministic;
}

// ---- PATH B's string group (0x1800b559f and its two clones) ----------------
// Read a length, read that many bytes STRAIGHT into the output buffer. No
// transcode, and — note — no terminator is written either.
// `malformed` is set, and nothing read, for a length the frame cannot hold.
bool ReadStringRaw(IO::CompressedStream* cs, uint8_t* buf60, System::String& tmp,
                   bool& malformed) {
    int32_t len = 0;                                       // 1800b55a2
    cs->IO::CompressedStream::Read(4, &len);               // 1800b55ae group
    // NOT RETAIL (review 2026-11-03, F-020): retail reads `len` bytes into the
    // 0x1840-byte frame; a longer length overflows the stack (UB). Rejected at the
    // parse (one byte is kept for the terminator below); retail has no check.
    if (len > kRawMaxLen) {
        malformed = true;
        return false;
    }
    if (len > 0)                                           // 1800b5633  TEST/JLE
        cs->IO::CompressedStream::Read(len, buf60);        // 1800b562f group

    const bool deterministic = (len > 0) && RawTerminatorInRead(buf60, len);
    // NOT RETAIL (review 2026-11-03, F-018): retail writes no terminator, so a
    // string with no NUL in its bytes is strlen'd into stale stack and past the
    // frame (UB). A valid string carries its NUL, so this byte changes nothing.
    if (len > 0)
        buf60[len] = 0;
    BuildStringFrom(buf60, tmp);
    return deterministic;
}

// NOT RETAIL (review 2026-11-03): the decompressed bytes the index has left after
// the read position: the block directory's length (CompressedStream::GetSize's
// directory term, every block 32 KB but the last) less `_position`. Valid
// packages never ask for more than this.
int64_t IndexBytesLeft(const IO::CompressedStream* cs) {
    if (cs->_headerSize <= 0)
        return 0;
    const int64_t total = (static_cast<int64_t>(cs->_headerSize) << 15) - RtIo::kCompressedStreamWindowSize +
                          cs->_headerData[cs->_headerSize - 1].uncompressedSize;
    return total - cs->_position;
}

// NOT RETAIL (review 2026-11-03): the icon of a package whose icon size is
// broken: 48x48 (the Mods page's thumbnail size), a white "?" on grey. Both
// colours read the same in any channel order.
constexpr int32_t kPlaceholderIconSize = 48;
constexpr int32_t kPlaceholderScale    = 3;
constexpr uint32_t kPlaceholderInk     = System::kColorWhite;
constexpr uint32_t kPlaceholderPaper   = 0xff404040u;
constexpr const char* kPlaceholderGlyph[] = {
    "..####..",
    ".##..##.",
    "##....##",
    "......##",
    ".....##.",
    "....##..",
    "...##...",
    "...##...",
    "...##...",
    "........",
    "...##...",
    "...##...",
};
constexpr int32_t kPlaceholderGlyphRows = 12;
constexpr int32_t kPlaceholderGlyphCols = 8;

void SetPlaceholderIcon(IO::PackageFile& pkg) {
    pkg._iconWidth = kPlaceholderIconSize;
    pkg._iconHeight = kPlaceholderIconSize;
    rtcoll::RtColl_ArraySetSize_180029510(&pkg._iconData, kPlaceholderIconSize * kPlaceholderIconSize);
    const int32_t left = (kPlaceholderIconSize - kPlaceholderGlyphCols * kPlaceholderScale) / 2;
    const int32_t top = (kPlaceholderIconSize - kPlaceholderGlyphRows * kPlaceholderScale) / 2;
    for (int32_t y = 0; y < kPlaceholderIconSize; ++y) {
        for (int32_t x = 0; x < kPlaceholderIconSize; ++x) {
            const int32_t gx = (x - left) / kPlaceholderScale;
            const int32_t gy = (y - top) / kPlaceholderScale;
            const bool inGlyph = x >= left && y >= top && gx < kPlaceholderGlyphCols && gy < kPlaceholderGlyphRows;
            const bool ink = inGlyph && kPlaceholderGlyph[gy][gx] == '#';
            pkg._iconData._data[y * kPlaceholderIconSize + x] = ink ? kPlaceholderInk : kPlaceholderPaper;
        }
    }
}


}  // namespace

// ============================================================================
// ?ReadPackfile@PackageFile@IO@@QEAA_NAEBVFilename@System@@@Z  retail 0x1800b46b0
// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS commit 3: the body is now
// `IO::PackageFile::ReadPackfile(const System::Filename&)`, at the bottom of
// this file. Non-virtual (`QEAA`).
//
// THIS IS ON THE PATH B34_BRIEFS section D BINDS. The move changed nothing but
// field spellings: no lock, retry, loop bound, null check or reorder was added,
// and nothing was removed.

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::PackageFile::ReadPackfile`'s member body
// ============================================================================
namespace IO {

bool PackageFile::ReadPackfile(const System::Filename& name) {
    // A member of `IO::X` must be DEFINED in a namespace ENCLOSING the class
    // (B34_BRIEFS section A.2), so this body cannot live inside `namespace
    // RtIO` where its helpers, typedefs and using-declarations do. ONE
    // function-scope using-directive brings them in unchanged, which is what
    // keeps this a MOVE and not a rewrite: BYTE-FAITHFULNESS ON THIS PATH IS
    // BINDING (B34_BRIEFS section D -- no added lock, retry, loop bound, null
    // check or reorder, and no silent removal either), so every line below is
    // the line that was in the door, except the field reads that became
    // members and the parameter that became a reference.
    using namespace RtIO;

    // ---- the re-entrancy flag (0x1800b46e8..0x1800b4795) -------------------
    // `fs->_platformExistsGate` (+0x19) is Exists()'s second gate. ReadPackfile
    // forces it to 1 across the OPEN so the file is found on disk rather than
    // through the (still empty) packfile index, then restores the SAVED byte.
    // Retail RE-READS the singleton before restoring; reproduced.
    IO::FileSystem* fs = IO::FileSystem::_singleton;           // 1800b46e8
    const uint8_t savedGate = fs->_platformExistsGate;         // 1800b46ef
    fs->_platformExistsGate = 1;                               // 1800b46f9

    FileInfo* const fi =
        &_file   /* +0xC0, the embedded FileInfo */;

    // OpenFile into the hidden return slot `[RBP+0x10]` (1800b4707),
    // FileInfo::operator= (1800b471a), then ~FileInfo on the temporary: +0x38
    // (_name1) FIRST (1800b472e..4757), then +0x30 (_name0) (1800b4762..478b).
    *fi = fs->OpenFile(name);

    fs = IO::FileSystem::_singleton;                           // 1800b478b
    fs->_platformExistsGate = savedGate;                       // 1800b4792

    if (fi->handle == RtIO::FileInfo::kHandleNone) {           // 1800b4795  CMP [RDI],-1
        return false;                                          // 1800b479a  XOR AL,AL
    }

    // ---- the header word, its sign bit, and the seek (§69) -----------------
    int32_t hdr = 0;
    fs->ReadFile(*fi, 4, &hdr);                                // 1800b47b1
    const int32_t compressed = (hdr & static_cast<int32_t>(IO::PackageFile::kHeaderCompressedBit)) ? 1 : 0;
                                                               // 1800b47b9  BT/CMOVC
    hdr &= static_cast<int32_t>(IO::PackageFile::kHeaderOffsetMask);   // 1800b47ca  BTR
    fi->cursor = hdr;                                          // 1800b47d1  the seek
    // Only when the handle is -1 or -2, i.e. a LOOSE file, is there an OS
    // handle to mirror it into. `CMP dword [RDI],-3 ; JBE` is UNSIGNED.
    if (!RtIO::FileInfo::IsPackedHandle(fi->handle)) {         // 1800b47d4
        Platform::FileSeek32(fi->winHandle, hdr);              // 1800b47e3 SetFilePointer
    }

    // ---- the compressed blob -----------------------------------------------
    int32_t blobLen = 0;
    fs->ReadFile(*fi, 4, &blobLen);                            // 1800b47f9
    // NOT RETAIL (review 2026-11-03, F-033): retail sign-extends a negative length
    // into a 16 EB MemAlloc. Rejected at the parse, as is a blob the file does not
    // hold (retail then parses an uninitialised heap block).
    if (blobLen < 0) {
        fs->CloseFile(*fi);
        return false;
    }
    void* const blob = ::System::MemoryAllocator::MemAlloc(
        static_cast<uint64_t>(static_cast<int64_t>(blobLen)), 4);   // 1800b47fe MOVSXD
    if (!fs->ReadFile(*fi, blobLen, blob)) {                   // 1800b4820
        ::System::MemoryAllocator::MemFree(blob);
        fs->CloseFile(*fi);
        return false;
    }

    // ---- ONE CompressedMemoryStream, built in halves (§48) -----------------
    // Not two objects: the CompressedStream base ctor, then the class's own
    // vtable stamped over it, then the EMBEDDED MemoryStream's ctor at +0x68,
    // then four raw field stores, then Open with the embedded stream as source.
    // ENDGAME B.3.9 IO (W2) / B.4 R-IO: the REAL class, a frame local -- base
    // ctor, vptr, member ctor, the same three steps (1800b482a..4840). Its
    // destructor at the function's end is retail's ~CompressedMemoryStream at
    // 1800b604f, after the blob's MemFree.
    IO::CompressedMemoryStream cms;
    cms._memoryStream._data      = blob;                       // 1800b4846
    cms._memoryStream._size      = static_cast<uint32_t>(blobLen);  // 1800b484d
    cms._memoryStream._cursor    = 0;                          // 1800b4850
    cms._memoryStream._direction = 0;                          // 1800b4854
    cms.IO::CompressedStream::Open(cms._memoryStream, 1);      // 1800b4864

    IO::CompressedStream* const cs = &cms;

    // The frame's one buffer region, §93.
    // NOT RETAIL (review 2026-11-03, F-054): retail's frame is never initialised,
    // and a length <= 0 or a short read leaves bytes that are then strlen'd and
    // decoded (an indeterminate read). Zeroed here; a written byte reads the same.
    uint8_t  frameBuf[kFrameBufBytes] = {};
    uint8_t* const buf60  = frameBuf;                          // [RBP+0x60]
    uint8_t* const buf860 = frameBuf + kOutCap;                // [RBP+0x860]

    // NOT RETAIL (review 2026-11-03): set by the hardening; the package is then
    // rejected at the teardown below.
    bool malformed = false;

    int32_t fileCount = 0;
    cs->IO::CompressedStream::Read(4, &fileCount);             // 1800b4869 group

    int32_t formatVersion = 0;                                 // 1800b48e9

    if (compressed >= 1) {                                     // 1800b48f0  CMP/JL

        cs->IO::CompressedStream::Read(4, &formatVersion);     // 1800b48fa
        cs->IO::CompressedStream::Read(4, &_version);          // 1800b4979  +0x68
        cs->IO::CompressedStream::Read(4, &_userVersion);      // 1800b49eb  +0x6c
        cs->IO::CompressedStream::Read(4, &_priority);         // 1800b4a5d  +0x70
        cs->IO::CompressedStream::Read(4, &_platform);         // 1800b4ad9  +0x74
        // ONE BYTE — §92(a). `MOV ESI,0x1` at 0x1800b4b4b.
        cs->IO::CompressedStream::Read(1, &_overridable);      // 1800b4b4b  +0xac
        cs->IO::CompressedStream::Read(4, &_crc);              // 1800b4bc9  +0xa8
        cs->IO::CompressedStream::Read(4, &_iconWidth);        // 1800b4c49  +0x78
        cs->IO::CompressedStream::Read(4, &_iconHeight);       // 1800b4cc0  +0x7c

        // The icon pixel array. Retail sizes it from height*width and then
        // reads `array._size * 4` bytes — the array's OWN size, not the product
        // it just passed. Identical unless SetSize clamped; transcribed as
        // spelled.
        // NOT RETAIL (review 2026-11-03, F-055 / F-056 / F-076): retail multiplies
        // the two int32s (it overflows: UB) and sizes the array from the file with
        // no cap. More pixels than the index has bytes left is rejected at the
        // parse; a valid icon is inside the index. The product is taken in int64,
        // and a negative one sizes the array 0, as SetSize clamps it (F-012).
        const int64_t iconPixels = static_cast<int64_t>(_iconHeight) * _iconWidth;
        if (iconPixels > INT32_MAX / 4 || iconPixels > IndexBytesLeft(cs) / 4) {
            malformed = true;
            goto teardown;
        }
        // Array<uint32>::SetSize, handed the {data, size, capacity} header.
        rtcoll::RtColl_ArraySetSize_180029510(&_iconData,
                     iconPixels < 0 ? 0 : static_cast<int32_t>(iconPixels));   // 1800b4d27/d37
        cs->IO::CompressedStream::Read(
            _iconData._size * 4, _iconData._data);             // 1800b4d56  REAL CALL
        // NOT RETAIL (review 2026-11-03): a negative width or height is a broken
        // icon, not a broken package. The read above keeps retail's stream position
        // and the mod loads with the "?" icon (ModDialog::Update copies 48x48 pixels
        // whatever the size says).
        if (_iconWidth < 0 || _iconHeight < 0)
            SetPlaceholderIcon(*this);

        if (formatVersion >= 2) {                              // 1800b4d62  CMP 2 / JL
            int32_t* const n90 =
                &_previewByteCount;   /* +0x90 -- see PackageFile.h */
            cs->IO::CompressedStream::Read(4, n90);            // 1800b4d6c  +0x90
            // NOT RETAIL (review 2026-11-03, F-076): retail sizes the preview from
            // the file with no cap. More bytes than the index has left is rejected
            // at the parse. A NEGATIVE count is not: shipped mods carry one (North7.pkm
            // stores 0xE1E1E1E1, its packer's uninitialised fill), and retail reads
            // them as packages with no preview (SetSize clamps it to 0 here, F-012;
            // a Read of a negative count reads nothing). Rejecting it hid the mod.
            if (*n90 > IndexBytesLeft(cs)) {
                malformed = true;
                goto teardown;
            }
            rtcoll::RtColl_ArraySetSize_180029630(&_previewData, *n90);   // 1800b4dea/df1
            // NOTE the bound: retail reads `*n90` bytes, not the ARRAY's size.
            cs->IO::CompressedStream::Read(*n90, _previewData._data);  // 1800b4e09
        }

        // Each group builds a String (BuildStringFrom's null store is the
        // local's ctor), assigns it (String::operator=) and releases it: one
        // block-scoped local per group.
        if (formatVersion <= 2) {
            // ---- PATH A (§81): UTF-16 metadata, with the §76 over-read -----
            {
                System::String tmp;
                ReadStringUtf16(cs, buf60, buf860, tmp, malformed);   // 1800b4e1f  group 1
                _name = tmp;                               // 1800b5044  +0x40
            }                                              // ~tmp 1800b507b
            if (malformed)                                 // NOT RETAIL (review 2026-11-03)
                goto teardown;
            {
                System::String tmp;
                ReadStringUtf16(cs, buf60, buf860, tmp, malformed);   // 1800b530d  group 2
                _author = tmp;                             // 1800b52b4  +0x48
            }
            if (malformed)                                 // NOT RETAIL (review 2026-11-03)
                goto teardown;
            {
                System::String tmp;
                ReadStringUtf16(cs, buf60, buf860, tmp, malformed);   // group 3
                _description = tmp;                        // 1800b5536  +0x50
            }
            if (malformed)                                 // NOT RETAIL (review 2026-11-03)
                goto teardown;
            // 1800b558e / 559a  JMP 0x1800b5b6c  (reloads formatVersion; §92(c))
        } else {
            // ---- PATH B (§81): the bytes are already UTF-8 -----------------
            {
                System::String tmp;
                ReadStringRaw(cs, buf60, tmp, malformed);  // 1800b559f  group 4
                _name = tmp;                               // 1800b5726  +0x40
            }
            if (malformed)                                 // NOT RETAIL (review 2026-11-03)
                goto teardown;
            {
                System::String tmp;
                ReadStringRaw(cs, buf60, tmp, malformed);  // 1800b5783  group 5
                _author = tmp;                             // 1800b58f6  +0x48
            }
            if (malformed)                                 // NOT RETAIL (review 2026-11-03)
                goto teardown;
            {
                System::String tmp;
                ReadStringRaw(cs, buf60, tmp, malformed);  // 1800b5953  group 6
                _description = tmp;                        // 1800b5ac6  +0x50
            }
            if (malformed)                                 // NOT RETAIL (review 2026-11-03)
                goto teardown;
            // 1800b5b1a / 5b23  JMP 0x1800b5b73
        }
    } else {
        // ---- the uncompressed default, 0x1800b5b25. §92(b): SEVEN stores. --
        _version = System::kGameVersion;                  // 1800b5b25  dword
        // `MOV qword ptr [R13+0x70],0x2710` — one store, two fields.
        _priority = kDefaultPriority;                     // 1800b5b2d
        _platform = 0;
        _crc  = 0;                                 // 1800b5b35
        _overridable = true;                              // 1800b5b3c
        _iconWidth  = 1;                                  // 1800b5b49
        _iconHeight = 1;                                  // 1800b5b4d
        rtcoll::RtColl_ArraySetSize_180029510(&_iconData, 1);  // 1800b5b5a
        _iconData._data[0] = kPlaceholderIconPixel;                 // 1800b5b66
        // falls into 0x1800b5b6c, which reloads formatVersion — still 0 here.
    }

    // ---- the entry directory (0x1800b5b73) ---------------------------------
    if (fileCount > 0) {                                       // 1800b5b73  TEST/JLE

        // NOT RETAIL (review 2026-11-03, F-077): retail loops `fileCount` times
        // whatever the index holds (2^31 iterations, one entry each, past the end
        // of the stream). Every entry takes at least 12 bytes (name length,
        // offset, size); a count the index cannot hold is rejected at the parse.
        if (static_cast<int64_t>(fileCount) * 12 > IndexBytesLeft(cs)) {
            malformed = true;
            goto teardown;
        }

        for (int32_t i = fileCount; i != 0; --i) {             // 1800b5b99 / 601a

            int32_t nameLen = 0;
            cs->IO::CompressedStream::Read(4, &nameLen);       // 1800b5ba0 group

            // THE SAME SELECTOR AS THE METADATA BLOCK, WITH THE ARMS IN THE
            // OPPOSITE ORDER — raw first at 0x1800b5c1f, jumping over the
            // UTF-16 arm at 0x1800b5c9d. §86.
            if (formatVersion > 2) {                           // 1800b5c19  CMP 2 / JLE
                // NOT RETAIL (review 2026-11-03, F-021): as ReadStringRaw, a name
                // longer than the frame overflows the stack (UB); rejected.
                if (nameLen > kRawMaxLen) {
                    malformed = true;
                    goto teardown;
                }
                if (nameLen > 0)                               // 1800b5c29
                    cs->IO::CompressedStream::Read(nameLen, buf60);  // 1800b5c25
                // 1800b5c98  JMP 0x1800b5de3 — NO terminator is written here.
                // NOT RETAIL (review 2026-11-03, F-018): one is, as in ReadStringRaw.
                if (nameLen > 0)
                    buf60[nameLen] = 0;
            } else {
                // NOT RETAIL (review 2026-11-03, F-019 / F-021): as ReadStringUtf16,
                // a name longer than buf860 overflows the stack, and one that fills
                // it leaves no terminator inside the frame (UB); rejected.
                if (nameLen > kUtf16MaxLen) {
                    malformed = true;
                    goto teardown;
                }
                if (nameLen > 0)                               // 1800b5cae
                    cs->IO::CompressedStream::Read(nameLen, buf860); // 1800b5ca7
                // NOT RETAIL (review 2026-11-03, F-017): the 2 * nameLen walk stops
                // at buf860's end, as in ReadStringUtf16.
                int64_t walk = 2 * static_cast<int64_t>(nameLen);
                if (walk > kBuf860Bytes)
                    walk = kBuf860Bytes;
                const uint8_t* src       = buf860;             // 1800b5d22
                const uint8_t* const end = buf860 + walk;      // 1800b5d1c/d32
                uint8_t*       dst       = buf60;              // 1800b5d35
                int32_t        total     = 0;                  // 1800b5d20
                while (src < end) {                            // 1800b5d40/db8
                    int32_t cp = -1;                           // 1800b5d50
                    src += Decode1(src, end, &cp);             // 1800b5d6f/74
                    if (cp >= 0) {                             // 1800b5d80
                        uint8_t scratch[4];
                        const int32_t m = Encode2(cp, scratch);// 1800b5d8f
                        total += m;                            // 1800b5d94
                        if (total <= 0x800 && m > 0) {         // 1800b5d96/da1
                            std::memcpy(dst, scratch, static_cast<size_t>(m));  // 1800b5db0
                            dst += m;                          // 1800b5db5
                        }
                    }
                }
                // 1800b5dc5 `MOV byte ptr [RSP+0x38],0` zeroes the encode
                // scratch on the way out. A dead store on a slot nothing reads
                // again; recorded, not reproduced, because our scratch is a
                // block-scoped local that does not exist here.
                if (total + 2 <= 0x800)                        // 1800b5dca/d4
                    *dst = 0;                                  // 1800b5ddb
            }

            int32_t entryOffset = 0, entrySize = 0;
            cs->IO::CompressedStream::Read(4, &entryOffset);   // 1800b5de3  -> +0x08
            cs->IO::CompressedStream::Read(4, &entrySize);     // 1800b5e59  -> +0x0c

            // Set-then-AdjustDelimiters on a null String: the Filename local.
            System::Filename tmp;
            BuildStringFrom(buf60, tmp);                       // 1800b5ec6..5f3d
            System::FilenamePrivateAccess::AdjustDelimiters(tmp);   // 1800b5f42

            // The entry's name is the Filename COPY CTOR from tmp, inlined
            // (1800b5f4a null store, 1800b5f4e..5f8e the guarded adopt, then
            // AdjustDelimiters TWICE, 0x1800b5f95 / 0x1800b5f9e — the copy
            // ctor's own two calls, 0x1800067d0); then the two dwords.
            PackageFileEntry entry = {
                tmp,
                static_cast<uint32_t>(entryOffset),            // 1800b5fa4  §91
                static_cast<uint32_t>(entrySize) };            // 1800b5faa  §91

            rtcoll::RtColl_ArraySetSizeFill_1800298f0(&_entries,  // 1800b5b86
                             _entries._size + 1, &entry);      // 1800b5fb1..fbe

            // ~entry (its name, 1800b5fc4..5fe9), then ~tmp (1800b5fed..6015):
            // reverse declaration order, at the end of the iteration.

        }
    }

    // ---- teardown (0x1800b6023) --------------------------------------------
teardown:
    // §70 proves this Commit is unreachable on the read path: `?Open@` sets
    // direction = 0 and only direction == 1 gets here. The GUARD is
    // transcribed and the arm is COUNTED, so "provably dead" is measured on
    // every run rather than asserted once.
    if (cs->_headerSize > 0 && cs->_direction == 1) {          // 1800b6023/29
        cs->Commit();                                          // 1800b6035
    }
    cms._memoryStream.Close();                                 // 1800b603a  slot 7, the member's vtable
    ::System::MemoryAllocator::MemFree(blob);                  // 1800b6045
    // ~cms at the return: 1800b604f  ~CompressedMemoryStream

    // NOT RETAIL (review 2026-11-03): a package the hardening rejected keeps no
    // entry and has its file closed, the state of a package whose file never
    // opened: ModificationList skips it (no CloseFile on that path) and
    // LoadPackages indexes nothing from it.
    if (malformed) {
        _entries.Clear();
        fs->CloseFile(*fi);
        return false;
    }
    const bool ours = true;                                    // 1800b6059  MOV AL,1

    return ours;
}

}  // namespace IO
