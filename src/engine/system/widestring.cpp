// ============================================================================
// src/engine/system/widestring.cpp — Wave 9 lane FS: System::WideString
// ============================================================================
//
//   | Export                                          | Retail      | Ord  |
//   |-------------------------------------------------|-------------|------|
//   | ?SetLength@WideString@System@@AEAAXH@Z          | 0x1800ca9b0 | 3902 |
//   | ?Set@WideString@System@@QEAAXPEB_W@Z            | 0x1800caf40 | 3823 |
//   | ?AsUTF8@WideString@System@@QEBA?AVString@2@XZ   | 0x1800cacd0 | 1625 |
//   | ??0WideString@System@@QEAA@AEBVString@1@@Z      | 0x1800cab50 |  513 |
//
// Every one of those four names was read out of Runtime-x64.dll's export
// directory with pefile BEFORE a line of body was written. `SetLength` is
// PRIVATE and therefore AEAA, not QEAA — the QEAA spelling is not an export
// and would resolve NULL forever
// (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md, and the same trap
// Filename::AdjustDelimiters fell into in Wave 2).
//
// THE OBJECT
// ----------
// WideString is System::String with char16_t characters. ONE pointer; the
// allocation header in front of `data` is byte-identical to String's:
//
//     block + 0 : int32 length        (CHARACTER count, not bytes)
//     block + 4 : int32 refcount      (born at 1)
//     block + 8 : char16_t data[length + 1]
//     WideString::data == block + 8
//
// pinned by SetLength (0x1800ca9b0): `MemAlloc(8 + (len+1)*2, 4)`, then
// `data[-8] = len` and `data[-4] = 1`. The RELEASE idiom is the same one
// String.h documents, including the asymmetry (freeing branch leaves
// `data - 8` in the object, sharing branch nulls it) — retail's
// `ADD qword [RCX],-8 ; MOV RCX,[RCX] ; MemFree` versus `MOV qword [RCX],0`.
// B7: this file open-codes it on WideString's own `_data`, as retail's
// ~WideString does (it used to reuse `RtString_ReleaseInline` through a
// `String*` view; see WideReleaseInline below).
//
// THE ALLOCATOR
// -------------
// Retail's SetLength and AsUTF8 do NOT call ?MemAlloc@MemoryAllocator@; they
// inline it — `EnterCriticalSection(_singleton+0x640); Allocate(_singleton,
// n, 4); LeaveCriticalSection`. `RtMemAlloc`/`RtMemFree` (string.cpp, String.h) call
// that sequence's exported wrapper (0x1800c7a50 / 0x1800c7ad0) by name, and string.cpp
// §"The DLL's allocator" establishes the two are interchangeable.
//
// THE ONE PIECE THAT IS *NOT* TRANSCRIBED FROM RETAIL
// ---------------------------------------------------
// The conversions go through two unexported CRT-style dispatchers:
//
//     0x1800c9fc0  decode(encodingId, src, srcEnd, &codepoint, encodingId)
//                      -> bytes consumed; codepoint < 0 means "invalid"
//     0x1800c9b60  encode(codepoint, dst, encodingId)
//                      -> bytes written
//
// `0x1800c9b60` is literally six instructions and a jump table at
// 0x1800c9d40 indexed by the encoding id, so the ids are readable off the
// table: **2 = UTF-8, 4 = UTF-16LE** (AsUTF8 decodes with 4 and encodes with
// 2; the String constructor decodes with 2 and encodes with 4). Ghidra cannot
// recover the table's bodies as functions ("Too many branches"), which is the
// same wall Wave 2 lane A hit on the SAME PAIR when it ported
// String::Serialize — see src/engine/system/string.cpp's Utf16DecodeOne /
// Utf8EncodeOne and its identical disclosure. So the four codec helpers below
// are written FROM THE SPECIFICATION, not lifted from the DLL, and that is
// stated here rather than buried.
//
// What was checked in the disassembly rather than assumed: the UTF-16 encoder
// at table entry 4 (0x1800c9c58) rejects a lone surrogate (`LEA EAX,[R9 -
// 0xD800] ; CMP EAX,0x7FF ; JBE <error>`) and returns 4 for a supplementary
// codepoint after `SUB R9D,0x10000`, i.e. ordinary surrogate-pair encoding.
//
// This matters less than it reads. On this project's only caller chain the
// input is the process's own module path and the strings that come out of it,
// which are ASCII on every machine this has ever run on; every codepoint takes
// the one-byte UTF-8 branch and the two-byte UTF-16 branch.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <new>          // placement new into the retail hidden return slot

#include "system/String.h"
#include "system/string_internal.h"
#include "system/widestring_internal.h"

namespace RtSystem {

namespace {

// ---------------------------------------------------------------------------
// The codec pair, ids 2 (UTF-8) and 4 (UTF-16LE). See the header note.
// ---------------------------------------------------------------------------
// Contract copied from the call sites, not invented: `*cp` is pre-set to -1 by
// the caller and a decoder that cannot make a codepoint leaves it negative and
// still reports a nonzero consumption, so the loops always advance.

int Utf8DecodeOne(const uint8_t* p, const uint8_t* end, int32_t* cp) {
    const uint32_t b0 = p[0];
    if (b0 < 0x80) { *cp = static_cast<int32_t>(b0); return 1; }
    int n;
    uint32_t v;
    if      ((b0 & 0xE0) == 0xC0) { n = 2; v = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { n = 3; v = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { n = 4; v = b0 & 0x07; }
    else                          { *cp = -1; return 1; }   // stray continuation
    if (p + n > end) { *cp = -1; return 1; }
    for (int i = 1; i < n; ++i) {
        const uint32_t b = p[i];
        if ((b & 0xC0) != 0x80) { *cp = -1; return 1; }
        v = (v << 6) | (b & 0x3F);
    }
    *cp = static_cast<int32_t>(v);
    return n;
}

int Utf16DecodeOne(const uint8_t* p, const uint8_t* end, int32_t* cp) {
    if (p + 2 > end) { *cp = -1; return 2; }
    const uint32_t w = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
    if (w >= 0xD800 && w < 0xDC00) {
        if (p + 4 > end) { *cp = -1; return 2; }
        const uint32_t w2 =
            static_cast<uint32_t>(p[2]) | (static_cast<uint32_t>(p[3]) << 8);
        if (w2 < 0xDC00 || w2 >= 0xE000) { *cp = -1; return 2; }
        *cp = static_cast<int32_t>(0x10000u + (((w - 0xD800u) << 10) | (w2 - 0xDC00u)));
        return 4;
    }
    if (w >= 0xDC00 && w < 0xE000) { *cp = -1; return 2; }   // lone low surrogate
    *cp = static_cast<int32_t>(w);
    return 2;
}

int Utf8EncodeOne(int32_t codepoint, uint8_t* dst) {
    const uint32_t c = static_cast<uint32_t>(codepoint);
    if (c < 0x80)    { dst[0] = static_cast<uint8_t>(c); return 1; }
    if (c < 0x800)   { dst[0] = static_cast<uint8_t>(0xC0 | (c >> 6));
                       dst[1] = static_cast<uint8_t>(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) { dst[0] = static_cast<uint8_t>(0xE0 | (c >> 12));
                       dst[1] = static_cast<uint8_t>(0x80 | ((c >> 6) & 0x3F));
                       dst[2] = static_cast<uint8_t>(0x80 | (c & 0x3F));  return 3; }
    dst[0] = static_cast<uint8_t>(0xF0 | (c >> 18));
    dst[1] = static_cast<uint8_t>(0x80 | ((c >> 12) & 0x3F));
    dst[2] = static_cast<uint8_t>(0x80 | ((c >> 6) & 0x3F));
    dst[3] = static_cast<uint8_t>(0x80 | (c & 0x3F));
    return 4;
}

// Table entry 4 at 0x1800c9c58: a lone surrogate is an ERROR (0 written), a
// supplementary codepoint becomes a surrogate pair (4 bytes), everything else
// is one code unit (2 bytes).
int Utf16EncodeOne(int32_t codepoint, uint8_t* dst) {
    const uint32_t c = static_cast<uint32_t>(codepoint);
    if (c - 0xD800u <= 0x7FFu) return 0;             // `CMP EAX,0x7FF ; JBE error`
    if (c < 0x10000u) {
        dst[0] = static_cast<uint8_t>(c & 0xFF);
        dst[1] = static_cast<uint8_t>((c >> 8) & 0xFF);
        return 2;
    }
    const uint32_t v  = c - 0x10000u;
    const uint32_t hi = 0xD800u | (v >> 10);
    const uint32_t lo = 0xDC00u | (v & 0x3FFu);
    dst[0] = static_cast<uint8_t>(hi & 0xFF);
    dst[1] = static_cast<uint8_t>((hi >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>(lo & 0xFF);
    dst[3] = static_cast<uint8_t>((lo >> 8) & 0xFF);
    return 4;
}

// The release idiom, over the SAME header String uses: ~WideString 0x18000a730
// open-codes it, and so does this (B7 D4: with String's release inline, the
// shared out-of-line `RtString_ReleaseInline` call this used through a
// `String*` view was a call retail lacks):
//     MOV RAX,[RCX]; TEST; JZ ret; DEC [RAX-4]; CMP [RAX-4],0; JNZ share
//     ADD qword [RCX],-8; MOV RCX,[RCX]; JMP MemFree 0x1800c7ad0   (free arm)
//     share: MOV qword [RCX],0                                      (null arm)
// The asymmetry is retail's (String.h): the freeing arm leaves `data - 8`
// (four char16_t) in the object, the sharing arm nulls it.
inline void WideReleaseInline(System::WideString* self) {
    char16_t* const d = self->_data;
    if (d == nullptr)
        return;
    int32_t& refs = reinterpret_cast<int32_t*>(d)[-1];  // CAST-OK: string_header
    if (--refs == 0) {
        self->_data = d - 4;                             // ADD qword [RCX],-8
        RtMemFree(self->_data);
    } else {
        self->_data = nullptr;
    }
}

inline int WideLength(const char16_t* d) {
    return d ? reinterpret_cast<const int32_t*>(d)[-2] : 0;  // CAST-OK: string_header
}

int WideStrLen(const char16_t* s) {
    int n = 0;
    while (s[n] != 0) ++n;
    return n;
}

bool SameWide(const char16_t* a, const char16_t* b) {
    if ((a == nullptr) != (b == nullptr)) return false;
    if (a == nullptr) return true;
    if (WideLength(a) != WideLength(b)) return false;
    for (int i = 0; a[i] != 0 || b[i] != 0; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

bool SameText(const char* a, const char* b) {
    if ((a == nullptr) != (b == nullptr)) return false;
    if (a == nullptr) return true;
    return std::strcmp(a, b) == 0;
}

}  // namespace

// ============================================================================
// ?Set@WideString@System@@QEAAXPEB_W@Z                     retail 0x1800caf40
// ============================================================================
// Three things in this body are not obvious and all three are retail's:
//
//  1. `Set(nullptr)` is a RELEASE, not "become empty" — retail tail-jumps
//     straight into MemFree (`JMP 0x1800c7ad0` at 0x1800caf84).
//  2. There is an inlined wcscmp FIRST and an equal string is a NO-OP, so
//     `Set` on an unchanged value does not reallocate and does not disturb the
//     refcount. Anything sharing the buffer keeps sharing it.
//  3. After SetLength, retail RE-WALKS the source for its length instead of
//     reusing the one it just computed, copies `min(len2+1, len+1)` characters
//     and then re-terminates. The two are equal by construction; transcribed
//     as written rather than folded, because folding it would silently change
//     what happens if a caller ever mutated `src` between the two walks.
// (B.4 Z MIRROR2: the `RtSystem_WideString_Set` door is gone with its last
// caller; the body is System::WideString::Set below.)

// ============================================================================
// ?AsUTF8@WideString@System@@QEBA?AVString@2@XZ            retail 0x1800cacd0
// ============================================================================
// Two passes over the UTF-16 buffer: the first only counts the UTF-8 bytes,
// the second writes them. Between them retail allocates `total + 1` RAW bytes
// (NOT a String block) and NUL-terminates at [total]; at the end it hands the
// raw buffer to String::Set, which makes the real String, and frees it.
//
// The three guards inside the write loop are retail's and are kept verbatim:
// `written > total` (the second pass measured more than the first — impossible
// for a stable input, and a silent truncation if it ever happened), a null
// buffer, and a non-positive encode result. The last one is the one that
// matters: a lone surrogate encodes to ZERO bytes, so a malformed UTF-16
// buffer loses that character rather than corrupting the output.
// (B.4 Z MIRROR2: the `RtSystem_WideString_AsUTF8` door is gone with its last
// caller; the body is System::WideString::AsUTF8 below.)

}  // namespace RtSystem

// ============================================================================
// ENDGAME B.3.4b SYSTEM (4a) — the `System::WideString` member BODIES.
// ============================================================================
// Defined at GLOBAL scope with qualified names. Every body below is the one that
// used to sit in the door of the same name above, moved VERBATIM — the only
// edits are `self->data` becoming `_data`, the two internal calls becoming
// member calls, and the anonymous-namespace helpers picking up their `RtSystem::`
// qualification (reachable from here in the same TU, B34_BRIEFS §A.2).
// ============================================================================

void System::WideString::SetLength(int len) {
    RtSystem::WideReleaseInline(this);
    int32_t* block = static_cast<int32_t*>(RtSystem::RtMemAlloc(  // LEFT: an alloc result; the tier-I shape does not match the RtMemAlloc spelling
        static_cast<uint64_t>(8 + (static_cast<int64_t>(len) + 1) * 2),
        RtSystem::kStringAllocAlign));
    _data = reinterpret_cast<char16_t*>(block + 2);  // CAST-OK: string_header
    block[0] = len;
    block[1] = 1;
}

void System::WideString::Set(const char16_t* src) {

    char16_t* const before = _data;

    if (src == nullptr) {
        RtSystem::WideReleaseInline(this);
    } else {
        const char16_t* cur = _data;
        bool unchanged = false;
        if (cur != nullptr) {
            int i = 0;
            if (cur[0] == src[0]) {
                // `TEST CX,CX / CMP word[src+i],0 / INC i / CMP` — stop at
                // either terminator or at the first difference.
                for (;;) {
                    if (cur[i] == 0) break;
                    if (src[i] == 0) break;
                    ++i;
                    if (cur[i] != src[i]) break;
                }
            }
            unchanged = (cur[i] == src[i]);
        }
        if (!unchanged) {
            const int len = RtSystem::WideStrLen(src);
            SetLength(len);
            char16_t* dst = _data;

            const int n1   = len + 1;                 // R14D
            const int len2 = RtSystem::WideStrLen(src) + 1;  // EBX, retail's re-walk
            const int copy = (n1 < len2) ? n1 : len2; // CMOVL — the MINIMUM
            std::memcpy(dst, src, static_cast<size_t>(copy) * 2);
            if (len2 >= n1) dst[n1 - 1] = 0;
        }
    }

}

// ============================================================================
// ?AsUTF8@WideString@System@@QEBA?AVString@2@XZ            retail 0x1800cacd0
// ============================================================================
// Two passes over the UTF-16 buffer: the first only counts the UTF-8 bytes,
// the second writes them. Between them retail allocates `total + 1` RAW bytes
// (NOT a String block) and NUL-terminates at [total]; at the end it hands the
// raw buffer to String::Set, which makes the real String, and frees it.
//
// The three guards inside the write loop are retail's and are kept verbatim:
// `written > total` (the second pass measured more than the first — impossible
// for a stable input, and a silent truncation if it ever happened), a null
// buffer, and a non-positive encode result. The last one is the one that
// matters: a lone surrogate encodes to ZERO bytes, so a malformed UTF-16
// buffer loses that character rather than corrupting the output.
System::String System::WideString::AsUTF8() const {

    const uint8_t* p   = reinterpret_cast<const uint8_t*>(_data);  // CAST-OK: byte_io
    const uint8_t* end = p + static_cast<size_t>(RtSystem::WideLength(_data)) * 2;

    int total = 0;
    for (const uint8_t* q = p; q < end;) {
        int32_t cp = -1;
        q += RtSystem::Utf16DecodeOne(q, end, &cp);
        if (cp >= 0) {
            uint8_t scratch[4];
            total += RtSystem::Utf8EncodeOne(cp, scratch);
        }
    }

    uint8_t* buf = static_cast<uint8_t*>(  // LEFT: an alloc result; the tier-I shape does not match the RtMemAlloc spelling
        RtSystem::RtMemAlloc(static_cast<uint64_t>(total) + 1,
                             RtSystem::kStringAllocAlign));
    buf[total] = 0;

    uint8_t* out    = buf;
    int      written = 0;
    for (const uint8_t* q = p; q < end;) {
        int32_t cp = -1;
        q += RtSystem::Utf16DecodeOne(q, end, &cp);
        if (cp < 0) continue;
        uint8_t scratch[4];
        const int m = RtSystem::Utf8EncodeOne(cp, scratch);
        written += m;
        if (written > total) continue;
        if (buf == nullptr)  continue;
        if (m <= 0)          continue;
        std::memcpy(out, scratch, static_cast<size_t>(m));
        out += m;
    }

    // ENDGAME B.3.1 step 2: retail's `*ret = 0` is the default ctor
    // (0x180006760) and the result is the hidden return slot — raw bytes — so
    // the String was placement-constructed into it before Set (0x1800c95d0)
    // ran. B.3.4b SYSTEM (4a): the hidden slot is the COMPILER's business now,
    // so the member holds an ordinary local and returns it.
    System::String ret;
    ret.Set(reinterpret_cast<const char*>(buf));  // CAST-OK: byte_io
    if (buf != nullptr) RtSystem::RtMemFree(buf);

    return ret;
}

// ============================================================================
// ??0WideString@System@@QEAA@AEBVString@1@@Z               retail 0x1800cab50
// ============================================================================
// The mirror of AsUTF8: two passes over a UTF-8 String producing UTF-16.
//
// The one line worth reading twice is retail's `SAR EAX,1` at 0x1800cabdf: the
// first pass accumulates the encoder's return in BYTES, and the character
// count handed to SetLength is that halved. So a supplementary codepoint
// correctly costs TWO characters, and the terminator is written at
// `data[bytes/2]`.
System::WideString::WideString(const System::String& s) {

    _data = nullptr;

    // ENDGAME B.3.1 step 2's bridge is gone from this body: `s` IS the class
    // now, because the parameter is the class. `GetLength()` (0x1800063c0) is
    // still the null-guarded stored-length read this line used to spell by
    // hand.

    const uint8_t* p   = reinterpret_cast<const uint8_t*>(s._data);  // CAST-OK: byte_io
    const uint8_t* end = p + static_cast<size_t>(s.GetLength());

    int totalBytes = 0;
    for (const uint8_t* q = p; q < end;) {
        int32_t cp = -1;
        q += RtSystem::Utf8DecodeOne(q, end, &cp);
        if (cp >= 0) {
            uint8_t scratch[4];
            totalBytes += RtSystem::Utf16EncodeOne(cp, scratch);
        }
    }

    const int chars = totalBytes >> 1;
    SetLength(chars);

    // Retail RE-READS `src->data` here rather than reusing `p`; kept.
    const uint8_t* r    = reinterpret_cast<const uint8_t*>(s._data);  // CAST-OK: byte_io
    const uint8_t* rend = r + static_cast<size_t>(s.GetLength());
    uint8_t* const base = reinterpret_cast<uint8_t*>(_data);  // CAST-OK: byte_io
    uint8_t*       out  = base;
    int            written = 0;
    for (const uint8_t* q = r; q < rend;) {
        int32_t cp = -1;
        q += RtSystem::Utf8DecodeOne(q, rend, &cp);
        if (cp < 0) continue;
        uint8_t scratch[4];
        const int m = RtSystem::Utf16EncodeOne(cp, scratch);
        written += m;
        if (written > totalBytes) continue;
        if (base == nullptr)      continue;
        if (m <= 0)               continue;
        std::memcpy(out, scratch, static_cast<size_t>(m));
        out += m;
    }
    _data[chars] = 0;

}

// ============================================================================
// The WideString members dx11's device enumeration uses
// ============================================================================
// `src/render/dx11/devicedata.cpp` calls AsUTF8 (above), the `const char16_t*`
// constructor, the destructor and StrLength on the DEVICE-ENUMERATION path —
// DeviceData::EnumerateInterfaces turns each adapter's UTF-16 Description into
// a String — which runs at Video startup on every boot.
//
// All three are small enough to quote in full, and all three were read from
// the DISASSEMBLY: retail dereferences `this` and `src` unguarded in every one
// of them, and so does this port.

// ============================================================================
// ??0WideString@System@@QEAA@PEBD@Z                        retail 0x1800cb110
//                                                   ord 514, Wave 9 lane PLAT
// ============================================================================
// The SAME two-pass UTF-8 -> UTF-16 body as the `const String&` constructor
// above, differing in exactly one thing: the source extent comes from an
// INLINE strlen of a raw `const char*` (0x1800cb132..0x1800cb14a and again at
// 0x1800cb1c3..0x1800cb1d9) instead of from String's stored length.
//
// Retail walks the source twice for its extent — once for the first pass's
// `end` and once again before the second pass. The two walks are transcribed
// separately rather than folded, for the same reason `Set` keeps its re-walk:
// folding them would silently change behaviour if a caller ever mutated `src`
// in between, and this is a transcription.
//
// NOT NULL-TOLERANT. Retail's first act after zeroing `this->data` is
// `CMP byte ptr [RDX],BPL` — an unguarded load of `src[0]`. Kept unguarded.
//
// WHY THIS LANE NEEDED IT: `?PlatformStartup@Application@1@AEAA_NXZ`
// (0x18009fc10) opens with `LEA RDX,[0x180154858] ; CALL 0x1800cb110`, i.e.
// `WideString className("Banished")`, and the resulting buffer is the window
// class name it hands to RegisterClassW and CreateWindowExW. Wave 9 lane FS
// ported four of this class's constructors and left this one, because nothing
// in the tree called it then.
System::WideString::WideString(const char* src) {

    _data = nullptr;

    // 0x1800cb132: the first inline strlen. `R8D` counts, `RSI = RDX + count`.
    int srcLen = 0;
    while (src[srcLen] != 0) ++srcLen;

    const uint8_t* const p   = reinterpret_cast<const uint8_t*>(src);  // CAST-OK: byte_io
    const uint8_t* const end = p + static_cast<size_t>(srcLen);

    int totalBytes = 0;
    for (const uint8_t* q = p; q < end;) {
        int32_t cp = -1;
        q += RtSystem::Utf8DecodeOne(q, end, &cp);
        if (cp >= 0) {
            uint8_t scratch[4];
            totalBytes += RtSystem::Utf16EncodeOne(cp, scratch);
        }
    }

    // 0x1800cb1ab: `SAR EAX,1` — the accumulator is in BYTES, the length handed
    // to SetLength is in CHARACTERS.
    const int chars = totalBytes >> 1;
    SetLength(chars);

    // 0x1800cb1c3: retail's SECOND inline strlen, over the ORIGINAL pointer.
    int srcLen2 = 0;
    while (src[srcLen2] != 0) ++srcLen2;

    const uint8_t* const r    = reinterpret_cast<const uint8_t*>(src);  // CAST-OK: byte_io
    const uint8_t* const rend = r + static_cast<size_t>(srcLen2);
    uint8_t* const base = reinterpret_cast<uint8_t*>(_data);  // CAST-OK: byte_io
    uint8_t*       out  = base;
    int            written = 0;
    for (const uint8_t* q = r; q < rend;) {
        int32_t cp = -1;
        q += RtSystem::Utf8DecodeOne(q, rend, &cp);
        if (cp < 0) continue;
        uint8_t scratch[4];
        const int m = RtSystem::Utf16EncodeOne(cp, scratch);
        written += m;
        if (written > totalBytes) continue;   // 0x1800cb236  CMP R15D,R14D ; JG
        if (base == nullptr)      continue;   // 0x1800cb23b  TEST R12,R12  ; JZ
        if (m <= 0)               continue;   // 0x1800cb243  TEST EAX,EAX  ; JLE
        std::memcpy(out, scratch, static_cast<size_t>(m));
        out += m;
    }
    _data[chars] = 0;                         // 0x1800cb275

}

// ??0WideString@System@@QEAA@PEB_W@Z                       retail 0x18000a6e0
// `this->data = 0; Set(this, src);` and nothing else — eight instructions.
System::WideString::WideString(const char16_t* src) {
    _data = nullptr;
    Set(src);
}

// ??1WideString@System@@QEAA@XZ                            retail 0x18000a730
// Byte-for-byte the String release idiom, asymmetry included: the freeing
// branch leaves `data - 8` in the object (`ADD qword [RCX],-8` then a TAIL JMP
// to MemFree), the sharing branch nulls it.
System::WideString::~WideString() {
    RtSystem::WideReleaseInline(this);
}

// ?StrLength@WideString@System@@SAHPEB_W@Z                 retail 0x1800cab20
// wcslen, and NOT null-tolerant: retail's first instruction after the zeroing
// XOR is `CMP word ptr [RCX],AX`, an unguarded load. Kept unguarded — a null
// here is an upstream fault to find, not one to paper over.
int System::WideString::StrLength(const char16_t* s) {
    int n = 0;
    while (s[n] != 0) ++n;
    return n;
}

namespace RtSystem {

// (B.4 wave X-R2: the CtorFromString / CtorFromChars / Dtor doors had no
// caller left -- every holder is a `System::WideString` local now -- and are
// gone with their declarations.)

}  // namespace RtSystem
