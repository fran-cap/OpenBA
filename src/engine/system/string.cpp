// ============================================================================
// src/engine/system/string.cpp — System::String   (Wave 2, lane A)
// ============================================================================
//
// Byte-faithful port of the decompilation at the retail address named above
// each function (Runtime-x64.dll, image base 0x180000000, read 2026-09-06).
// src/engine/system/String.h carries the layout and the release idiom;
// read it first.
//
// THE ABI RULE (docs/history/runtime_port/WAVE1.md, "The ABI rule this wave is built
// on"): System::String has a user-declared constructor, so MSVC returns it BY
// MEMORY. The hidden return-slot pointer is an ARGUMENT — the SECOND register
// for a member (RCX=this, RDX=ret, R8=arg1) and the FIRST for a static
// (RCX=ret, RDX=arg1) — and the callee returns it again in RAX. Every port
// below spells the slot out as a parameter and returns it. Writing these as
// C++ methods returning by value is the wild write described in the memory
// note reference_hidden_retslot_wild_write.md.
//
// ALLOCATION GOES THROUGH THE ONE REGISTERED ALLOCATOR. String objects and
// their refcounted buffers are handed around the whole tree, so every buffer
// must come from and go back to the same heap. Both ends resolve
// System::MemoryAllocator's MemAlloc/MemFree through the registry.
// ============================================================================

// ============================================================================
// ENDGAME B.3.1 step 1: the bodies below now live on `System::String`
// (src/engine/system/String.h). They MOVED — they were not rewritten — and
// the hidden return slot went with the DLL boundary that required it. Every
// `RtSystem_String_*` / `RtString_*` free function in this file is now a
// one-line shim over the member on the same eight bytes, so that no caller has
// to move in this commit. THE SHIMS AND THE REGISTRY ROWS DIE IN STEP 2, when
// the last caller of each is converted (B31_RULES.md).
// ============================================================================

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "system/String.h"
#include "io/Serializer.h"   // B3X W0: IO::ReaderWriter, called by name
#include "io/Stream.h"       // IO::Stream::_direction
#include "system/string_internal.h"
#ifndef BANISHED_UNIT_TEST
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name
#endif

namespace RtSystem {

// (B.4 acceptance MOD: the `AsSys(RtSystem::String*)` bridge is gone with the
// POD.)

// ============================================================
// The DLL's allocator
// ============================================================
// ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z  retail 0x1800c7a50
// ?MemFree@MemoryAllocator@System@@SAXPEAX@Z     retail 0x1800c7ad0
// Both are the lock-wrapped statics: MemAlloc = EnterCS(_singleton+0x640);
// Allocate(_singleton, size, align); LeaveCS. String::SetLength inlines that
// same sequence rather than calling MemAlloc, so the two are interchangeable.
//
// NOTE: the ...@SAPEAXII@Z ("unsigned, unsigned") spelling of MemAlloc that
// circulates in this project's notes is NOT an export of Runtime-x64.dll. The
// only two forms are the _KH (size_t, int) and PEAX ones used here.

// Registration is all-or-nothing (RegisterAllocatorExports registers MemAlloc
// and MemFree together), so the two halves can never disagree about whose heap
// they are on.

#ifdef BANISHED_UNIT_TEST
// The host unit test (tests/unit, `make unit`) links this file without the
// registry, so there is no MemoryAllocator to resolve. The class under test
// only needs A heap, and every allocation it makes it also frees, so malloc is
// the whole requirement. B31_RULES rule 6 allows exactly this and asks that it
// be named in the report.
void* RtMemAlloc(uint64_t size, int align) {
    (void)align;
    return std::malloc(static_cast<size_t>(size));
}

void RtMemFree(void* p) { std::free(p); }
#else
// B3R: the two statics by name (they were lazily-filled ResolveExport caches).
void* RtMemAlloc(uint64_t size, int align) {
    return System::MemoryAllocator::MemAlloc(size, align);
}

// (B7 D4: RtMemFree is header-inline in String.h outside the unit test.)
#endif

}  // namespace RtSystem

// ############################################################################
// System::String — the class (ENDGAME B.3.1 step 1)
// ############################################################################
// Every body below is the body the like-named C function in this file had
// before the conversion, with `self->data` spelled `_data` and the hidden
// return slot dropped. The `RtSystem_*` names live on as shims at the end of
// the file.
// ############################################################################
namespace System {

using RtSystem::StringLength;
using RtSystem::StringRefCount;
using RtSystem::RtMemAlloc;
using RtSystem::RtMemFree;
using RtSystem::kStringAllocAlign;

// ============================================================
// The inlined idioms (private)
// ============================================================

// ?SetLength@String@System@@AEAAXH@Z  retail 0x1800c8220
// Release, then allocate (len+1)+8 bytes; length at block+0, refcount 1 at
// block+4, data = block+8. Does NOT write a terminator — every caller does.
void String::SetLength(int len) {
    Release();
    int32_t* block = static_cast<int32_t*>(  // LEFT: an alloc result; the tier-I shape does not match the RtMemAlloc spelling
        RtMemAlloc(static_cast<uint64_t>(static_cast<int64_t>(len) + 1) + 8, kStringAllocAlign));
    _data = reinterpret_cast<char*>(block + 2);  // CAST-OK: string_header
    block[0] = len;
    block[1] = 1;
}

// (B7 D4: Release 0x1800c9870 and Adopt are header-inline in String.h.)

// ?StrCompare@String@System@@SAHPEBD0@Z  retail 0x1800c98a0
// Null-tolerant strcmp: null sorts before everything, two nulls are equal.
// Retail walks to the first differing byte (or to either terminator) and then
// returns the signed difference of the CHARS at that index.
//
// WAVE 22 LANE SYSBODY — the return is SIGN-EXTENDED, and this was latent.
// Retail's last three instructions are
//     1800c98ec  MOVSX EAX,byte ptr [RCX + R10*0x1]
//     1800c98f1  MOVSX ECX,byte ptr [RCX + RDX*0x1]
//     1800c98f5  SUB   EAX,ECX
// i.e. both bytes are widened SIGNED. The body below used to spell that
// `static_cast<int>(a[i])` on a plain `const char*`, which is only the same
// thing while `char` happens to be signed — true for MSVC and for MinGW on
// x86-64 Windows, and therefore invisible to every build this project makes.
// It is written as an explicit `signed char` now so the port states the fact
// rather than inheriting it from the toolchain, and the static_assert below
// records which way the assumption used to point. Any input with a byte >=
// 0x80 at the differing index separates the two spellings; the sweep in
// RtSystem_StrCompareProbe() feeds exactly those (bucket `highBit`).
int String::StrCompare(const char* a, const char* b) {
    if (a == nullptr)
        return -static_cast<int>(b != nullptr);
    if (b == nullptr)
        return 1;
    int i = 0;
    if (a[0] == b[0]) {
        // Note the order: retail ADVANCES first and then tests, so on a
        // mismatch it reports the index of the differing pair, and on equal
        // strings it walks one past the last character and reports the two
        // terminators. Testing before advancing would return the previous
        // index and get the sign right by accident but the magnitude wrong.
        while (a[i] != '\0' && b[i] != '\0') {
            ++i;
            if (a[i] != b[i])
                break;
        }
    }
    // MOVSX, not MOVZX: see the note above.
    return static_cast<int>(static_cast<signed char>(a[i])) -
           static_cast<int>(static_cast<signed char>(b[i]));
}

// ?GetLength@String@System@@QEBAHXZ  retail 0x1800063c0: header-inline in
// String.h since block SAVELOAD (perf:), body unchanged.

// ??HSystem@@YA?AVString@0@AEBV10@0@Z  retail 0x1800c9440
// The free operator+(const String&, const String&). NOT a member: it is a
// namespace-scope function, so the hidden return slot is the FIRST argument
// (RCX=ret, RDX=lhs, R8=rhs) — the static-function form of the ABI rule, not
// the member form. Ghidra shows it with `in_R8` because it cannot type the
// third argument across the hidden slot.
//
// Diffable: both inputs are const, and every branch either allocates a fresh
// buffer or takes a reference on an existing one, so the oracle's copy can be
// released afterwards with no net effect. The adopt branches make theirs
// AddRef an input buffer (+1) and the release afterwards undoes it (-1); the
// count can never reach 0 there, because the input itself still holds a
// reference.
String operator+(const String& lhs, const String& rhs) {
    String ret;
    const int lhsLen = lhs._data ? StringLength(lhs._data) : 0;
    const int rhsLen = rhs._data ? StringLength(rhs._data) : 0;

    // Either side empty (null OR zero stored length): the result ADOPTS the
    // other side's buffer rather than copying it.
    if (lhs._data == nullptr || lhsLen == 0) {
        StringPrivateAccess::Adopt(ret, rhs._data);
        return ret;
    }
    if (rhs._data == nullptr || rhsLen == 0) {
        StringPrivateAccess::Adopt(ret, lhs._data);
        return ret;
    }

    String tmp;
    StringPrivateAccess::SetLength(tmp, lhsLen + rhsLen);
    char* out = tmp._data;
    std::memcpy(out, lhs._data, static_cast<size_t>(lhsLen));
    std::memcpy(out + lhsLen, rhs._data, static_cast<size_t>(rhsLen));
    out[lhsLen + rhsLen] = '\0';

    // Retail's move-out: adopt (refcount 1 -> 2), then release the local
    // (2 -> 1). The pair is not a no-op — it is how the buffer changes owner
    // without a copy — so it is written out rather than folded away.
    if (out != nullptr) {
        ret._data = out;
        ++StringRefCount(out);
        if (--StringRefCount(out) == 0) {
            tmp._data = out - 8;
            RtMemFree(tmp._data);
        }
    }
    // THE ONE THING THE CLASS HAS TO SAY OUT LOUD: after the pair above,
    // `tmp._data` and `ret._data` are the same buffer at refcount 1 — the
    // decrement can never reach 0 here, because the increment just ran. Retail's
    // local was a POD and simply went out of scope; a real destructor would
    // release it and free the buffer `ret` has just taken ownership of. The
    // local is therefore disarmed, which is not a change of behaviour but the
    // C body's unstated "nobody looks at tmp again" made explicit.
    tmp._data = nullptr;
    return ret;
}

// ??HSystem@@YA?AVString@0@AEBV10@PEBD@Z  retail 0x1800c92d0
// Same shape against a C string, and the same static-function ABI: RCX=ret,
// RDX=lhs, R8=rhs. Note the asymmetry with the String overload: when `lhs` is
// empty this one Set()s from `rhs` (a COPY), because a C string has no
// refcounted buffer to adopt.
String operator+(const String& lhs, const char* rhs) {
    String ret;
    const int lhsLen = lhs._data ? StringLength(lhs._data) : 0;

    if (lhs._data == nullptr || lhsLen == 0) {
        ret.Set(rhs);
        return ret;
    }
    if (rhs == nullptr) {
        StringPrivateAccess::Adopt(ret, lhs._data);
        return ret;
    }

    int rhsLen = 0;
    while (rhs[rhsLen] != '\0')
        ++rhsLen;

    String tmp;
    StringPrivateAccess::SetLength(tmp, lhsLen + rhsLen);
    char* out = tmp._data;
    std::memcpy(out, lhs._data, static_cast<size_t>(lhsLen));
    std::memcpy(out + lhsLen, rhs, static_cast<size_t>(rhsLen));
    out[lhsLen + rhsLen] = '\0';

    if (out != nullptr) {
        ret._data = out;
        ++StringRefCount(out);
        if (--StringRefCount(out) == 0) {
            tmp._data = out - 8;
            RtMemFree(tmp._data);
        }
    }
    tmp._data = nullptr;   // see the String overload above
    return ret;
}

// ============================================================
// Constructors / destructor  — NOT DIFFED
// ============================================================
// A ctor has no independent result to compare: running the original a second
// time over the same object would either re-zero it or take a second reference
// on a shared buffer. A dtor is worse — running it twice is a double release
// and, at refcount 1, a double free.

// (B7 D4: String() 0x180006760, String(const String&) 0x180006cb0, the move
// ctor and ~String() 0x180006c80 are header-inline in String.h.)

// ??0String@System@@QEAA@PEBD@Z  retail 0x18000b630
String::String(const char* src) : _data(nullptr) {
    Set(src);
}

// ============================================================
// Assignment / mutation — NOT DIFFED (see the header comment)
// ============================================================

// (B7 D4: operator=(const String&) 0x1800c9810 is header-inline in String.h.)

// Not retail's: the pointer steal. Self-move is short-circuited on the pointer
// exactly as the copy overload above is.
String& String::operator=(String&& rhs) noexcept {
    if (this != &rhs) {
        Release();
        _data = rhs._data;
        rhs._data = nullptr;
    }
    return *this;
}

// ?Set@String@System@@QEAAXPEBD@Z  retail 0x1800c95d0
void String::Set(const char* src) {
    if (src == nullptr) {
        Release();
        return;
    }
    // Setting to a string that compares equal to the current contents is a
    // no-op: retail does not reallocate, and does not un-share either.
    if (_data != nullptr && StrCompare(_data, src) == 0)
        return;

    int len = 0;
    while (src[len] != '\0')
        ++len;
    SetLength(len);
    // Retail recomputes the length a second time here and inlines StrCopy
    // (0x1800c96c0) with capacity len+1; the two lengths are necessarily
    // equal, so StrCopy's truncation arm is unreachable from Set.
    std::memcpy(_data, src, static_cast<size_t>(len) + 1);
}

// ??YString@System@@QEAA?AV01@AEBV01@@Z  retail 0x1800c8a90
// ??Y is operator+= , not operator+ — and the `?AV01@` return type means it
// ALSO returns a copy of the new value by memory. The body proves the name:
// it Set()s `this`.
//
// ABI: RCX=this, RDX=hidden return slot, R8=rhs. Ghidra's decompilation drops
// R8 entirely — the inner concat call is itself a hidden-return-slot call and
// the argument is never re-materialised, exactly the trap WAVE1.md defect 5
// describes. Transcribed from the listing at 0x1800c8ab6..0x1800c8ac4, which
// shows RCX=&tmp, RDX=this and R8 passing straight through untouched.
String String::operator+=(const String& rhs) {
    String tmp = *this + rhs;
    Set(tmp._data);
    StringPrivateAccess::Release(tmp);
    tmp._data = nullptr;   // the explicit release above is retail's; ~tmp must not repeat it

    String ret;
    StringPrivateAccess::Adopt(ret, _data);
    return ret;
}

// ??YString@System@@QEAA?AV01@PEBD@Z  retail 0x1800c9120
String String::operator+=(const char* rhs) {
    String tmp = *this + rhs;
    Set(tmp._data);
    StringPrivateAccess::Release(tmp);
    tmp._data = nullptr;

    String ret;
    StringPrivateAccess::Adopt(ret, _data);
    return ret;
}

// ============================================================
// Comparisons — fully diffable (pure, const, no allocation)
// ============================================================

// ??8String@System@@QEBA_NAEBV01@@Z  retail 0x1800063d0
// Compares the STORED lengths first (data[-8]), not strlen; two strings of
// different stored length are unequal without touching the characters.
bool String::operator==(const String& other) const {
    const char* a = _data;
    const char* b = other._data;
    const int la = a ? StringLength(a) : 0;
    const int lb = b ? StringLength(b) : 0;

    bool ours;
    if (la != lb)
        ours = false;
    else if (a != nullptr && StringLength(a) != 0)
        ours = StrCompare(a, b) == 0;
    else
        ours = true;

    return ours;
}

// ??8String@System@@QEBA_NPEBD@Z  retail 0x180053c00
bool String::operator==(const char* other) const {
    bool ours;
    if (_data == nullptr)
        ours = (other == nullptr);
    else if (other != nullptr)
        ours = StrCompare(_data, other) == 0;
    else
        ours = false;

    return ours;
}

// ??MString@System@@QEBA_NAEBV01@@Z  retail 0x180029360
// Null is LESS than everything, including another null: `x < null` is false
// and `null < null` is false, but `null < x` is true. That asymmetry is the
// ordering every Collection::Set<String> in the DLL is built on.
bool String::operator<(const String& other) const {
    bool ours;
    if (other._data == nullptr)
        ours = false;
    else if (_data == nullptr)
        ours = true;
    else
        ours = StrCompare(_data, other._data) < 0;

    return ours;
}

// ============================================================
// SubString — diffable
// ============================================================

// ?SubString@String@System@@QEBA?AV12@HH@Z  retail 0x1800c9230
// The two ints are START and END (inclusive), not start and count: retail
// computes `count = (end - start) + 1`. Nothing is clamped — an end past the
// terminator reads past it, and an end < start - 1 makes `count` negative and
// the trailing `data[count] = 0` writes BEHIND the buffer. Both are retail's,
// and the inclusive-end convention is load-bearing: Filename::AdjustDelimiters
// and String::FormatParamList both depend on it.
String String::SubString(int start, int end) const {
    const int count = (end - start) + 1;
    String ret;
    StringPrivateAccess::SetLength(ret, count);
    for (int i = 0; i < count; ++i)
        ret._data[i] = _data[start + i];
    ret._data[count] = '\0';
    return ret;
}

// ============================================================
// ToFloat — diffable, pure
// ============================================================

// ?ToFloat@String@System@@SAMPEBD@Z  retail 0x1800c8070
// A hand-rolled strtof. Every float constant below is the .rdata value retail
// loads, named by address; the exponent is applied by repeated multiplication
// in chunks of 1e20 / 1e8 / 10, which is NOT the same rounding as a single
// pow10 table lookup — hence the literal transcription.
float String::ToFloat(const char* s) {
    float ours;
    if (s == nullptr) {
        ours = 0.0f;
    } else {
        while (*s == ' ' || *s == '\t')
            ++s;

        float sign = -1.0f;                       // 0x18016175c
        if (*s == '-') {
            ++s;
        } else {
            sign = 1.0f;                          // 0x18016169c
            if (*s == '+')
                ++s;
        }

        float mant = 0.0f;
        for (char c = *s; c > '/' && c < ':'; c = *s) {
            ++s;
            mant = static_cast<float>(c - '0') + mant * 10.0f;   // 0x1801616f0
        }

        if (*s == '.') {
            ++s;
            float scale = 0.1f;                   // 0x180161684
            for (char c = *s; c > '/' && c < ':'; c = *s) {
                ++s;
                const float term = static_cast<float>(c - '0') * scale;
                scale *= 0.1f;
                mant += term;
            }
        }

        unsigned exp = 0;
        bool negExp = false;
        float escale = 1.0f;

        // ((*s + 0xbb) & 0xdf) == 0 selects exactly 'e' (0x65) and 'E' (0x45).
        if (((static_cast<unsigned>(static_cast<int>(*s)) + 0xbbu) & 0xdfu) == 0) {
            const char* p = s + 1;
            if (s[1] == '-') {
                negExp = true;
                p = s + 2;
            } else if (s[1] == '+') {
                p = s + 2;
            }
            char c = *p;
            if (c > '/') {
                while (c <= '9') {
                    ++p;
                    exp = exp * 10u + static_cast<unsigned>(c - '0');
                    c = *p;
                    if (c <= '/')
                        break;
                }
                bool bigChunks;
                if (exp < 0x80u) {
                    bigChunks = (exp > 0x13u);
                } else {
                    exp = 0x7fu;                  // clamped, not saturated to inf
                    bigChunks = true;
                }
                if (bigChunks) {
                    unsigned k = exp / 0x14u;
                    do {
                        exp -= 0x14u;
                        escale *= 1.0e20f;        // 0x180161748
                        --k;
                    } while (k != 0);
                }
                if (exp > 7u) {
                    unsigned k = exp >> 3;
                    do {
                        exp -= 8u;
                        escale *= 1.0e8f;         // 0x180161740
                        --k;
                    } while (k != 0);
                }
                for (; exp != 0u; --exp)
                    escale *= 10.0f;
            }
        }

        ours = negExp ? (mant / escale) * sign : escale * mant * sign;
    }

    // Bit equality demanded: this is integer-and-multiply arithmetic with no
    // transcendental in it, so a faithful port must round identically.
    return ours;
}

// ============================================================
// Format / FormatIndexed — NOT DIFFED (varargs cannot be forwarded)
// ============================================================
// Both are `...` functions with no va_list twin in the DLL's export table, so
// there is no way to hand the original the same argument list a second time.
// The diff harness has nothing to offer here; faking it with a hand-built
// argument list would test a different call than the one that happened.

// ?Format@String@System@@SA?AV12@PEBDZZ  retail 0x1800c9730
// ABI: a static returning by memory, so RCX = the hidden return slot and RDX =
// the format string; the varargs start at R8, whose home slot is where retail
// points its va_list.
String String::FormatV(const char* fmt, va_list ap) {
    String ret;
    if (fmt == nullptr)
        return ret;

    // Retail's buffer is 0x4000 bytes on the stack, filled with _vsnprintf and
    // then hard-terminated at [0x3fff]. _vsnprintf does not terminate on
    // truncation and C99 vsnprintf does; the explicit terminator makes the two
    // agree on the truncated result too, so this is not a divergence.
    char buf[0x4000];
    std::vsnprintf(buf, 0x4000, fmt, ap);
    buf[0x3fff] = '\0';

    String tmp;
    tmp.Set(buf);

    if (tmp._data != nullptr) {
        ret._data = tmp._data;
        ++StringRefCount(tmp._data);
        if (--StringRefCount(tmp._data) == 0) {
            char* blk = tmp._data - 8;
            tmp._data = blk;
            RtMemFree(blk);
        }
    }
    tmp._data = nullptr;   // see operator+(String, String): retail's local was a POD
    return ret;
}

String String::Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    String ret = FormatV(fmt, ap);
    va_end(ap);
    return ret;
}

// Collection::Array<String>::SetCount, retail FUN_180029a80, reduced to the
// only case FormatIndexed reaches: a freshly zeroed {data,count,cap} array
// grown from empty to n > 0. In that case the general body takes cap(0) < n
// -> cap = n, the "grew by exactly one from non-empty" special case is false,
// a new block is allocated, every slot is zero-initialised, and both the
// destroy-old-range and free-old-block arms are no-ops because the old pointer
// is null. Nothing else in this file needs the general version.
namespace {
struct StringArray {
    String* data;
    int32_t count;
    int32_t capacity;
};

void StringArray_SetCountFromEmpty(StringArray* arr, int n) {
    arr->count = n;
    arr->capacity = n;
    arr->data = static_cast<String*>(RtMemAlloc(static_cast<uint64_t>(n) * 8, 8));  // LEFT: an alloc result; the tier-I shape does not match the RtMemAlloc spelling
    for (int i = 0; i < n; ++i)
        arr->data[i]._data = nullptr;
}

// retail FUN_180011010 — release every String in a range.
void StringArray_DestroyRange(String* p, int n) {
    for (int i = 0; i < n; ++i) {
        char* d = p[i]._data;
        if (d == nullptr)
            continue;
        if (--StringRefCount(d) == 0) {
            p[i]._data = d - 8;
            if (p[i]._data != nullptr)
                RtMemFree(p[i]._data);
        } else {
            p[i]._data = nullptr;
        }
    }
}
}  // namespace

// ?FormatIndexed@String@System@@SA?AV12@PEBDZZ  retail 0x1800c8b40
// "@1".."@9" substitution, not printf: FormatParamCount scans the format for
// the highest @n used, an Array<String> of that many entries is built from the
// C-string varargs, and FormatParamList does the substitution.
//
// FormatParamCount (0x1800c8d20) and FormatParamList (0x1800c8dc0) were left on
// the DLL by Wave 2 as out of lane scope. **Wave 9 lane B3 ported both** (below,
// after this function) — they are two of WAVE9_LANE_V.md's REAL DEPENDENCY rows
// and the two calls here were the raw-door sites that made them so. The
// warning in the paragraph this replaced was accurate and was the reason to be
// careful rather than to stop: FormatParamList IS a hidden-return-slot function
// whose R8 argument Ghidra drops, so it was transcribed from the listing at
// 0x1800c8dc0..0x1800c9017 instruction by instruction and not from decompiled C.
// ============================================================
// Wave 9 lane B3 - the four System::String exports left on the DLL
// ============================================================
// All four are WAVE9_LANE_V.md REAL DEPENDENCY rows. Two are pure and get a
// full differential oracle; Release mutates a refcount and gets a shadow with
// the reason stated; FormatParamList allocates a fresh result from const
// inputs and so replays cleanly, exactly like SubString above.

// ?FormatParamCount@String@System@@SAHPEBD@Z  retail 0x1800c8d20
//
// Returns the HIGHEST parameter index used in `fmt`, where a parameter is the
// two characters `@` followed by an ASCII digit, and `@1` is index 1 -- so the
// answer is `digit - '0'`, computed by retail as `(int)(signed char)d - 0x2f`
// against a `- 0x30` range check. It is a MAXIMUM, not a count of occurrences:
// `"@2 @2"` answers 2 and `"@2"` alone also answers 2, which is why the
// StringArray FormatIndexed sizes from it can legitimately have holes.
//
// Retail's loop bound is the SECOND character's: a format ending in a bare `@`
// is not a parameter, because the `i + 1 >= len` test (0x1800c8d77
// `CMP R9D,R8D / JGE`) skips the case. `fmt == nullptr` returns 0 before the
// strlen.
int String::FormatParamCount(const char* fmt) {
    int result = 0;
    if (fmt != nullptr) {
        int len = 0;
        while (fmt[len] != '\0') ++len;
        for (int i = 0; i < len; ++i) {
            if (fmt[i] != '@' || i + 1 >= len) continue;
            const unsigned char d = static_cast<unsigned char>(fmt[i + 1]);
            if (static_cast<unsigned char>(d - '0') > 9u) continue;
            const int index = static_cast<int>(static_cast<signed char>(d)) - 0x2f;
            if (index > result) result = index;
        }
    }
    return result;
}

// ?FormatParamList@String@System@@SA?AV12@AEBV12@PEBV12@@Z  retail 0x1800c8dc0
//
// ABI: RCX = hidden return slot, RDX = `fmt`, R8 = `params` (an ARRAY of
// String indexed by the digit -- NOT a single String*, despite the `PEBV12@`
// spelling). Ghidra drops R8 entirely; the array indexing is visible only in
// the listing, at 0x1800c8efa `MOV RCX,[RBP+0x58]` (the home slot R8 was
// spilled to at entry) followed by `LEA R8,[RCX + RAX*0x8]`.
//
// The body walks `fmt` and appends alternately: the literal run since the last
// substitution, then `params[digit]`. It appends with `operator+=`
// (0x1800c8a90) and takes literal runs with `SubString` (0x1800c9230), both of
// which this file already ports.
//
// TWO DETAILS THAT ARE NOT SIMPLIFICATIONS:
//  * retail takes a REFCOUNTED COPY of `fmt` at entry (`work`, [RBP+0x50],
//    `INC [RAX-0x4]` at 0x1800c8e0a) and calls SubString on the copy while
//    reading characters out of `fmt` itself. Same buffer, so the characters
//    cannot differ -- but the AddRef and its matching Release at 0x1800c8fe0
//    are real refcount traffic, and a port that skipped them would leave a
//    different refcount behind and the next Release would free at the wrong
//    time.
//  * the literal-run guard is `if (i > 0)` on the ABSOLUTE index, not
//    `i > segStart` (0x1800c8e6d `TEST ESI,ESI / JLE`). For adjacent
//    parameters (`"@1@2"`) that produces `SubString(segStart, i-1)` with
//    `end < start`, i.e. a zero-length append, rather than skipping the append.
//    Transcribed as written.
String String::FormatParamList(const String& fmt, const String* params) {
    String ret;

    String work;
    StringPrivateAccess::Adopt(work, fmt._data);      // refcounted copy of fmt

    const int len = (work._data != nullptr) ? StringLength(work._data) : 0;
    int segStart = 0;                                 // R8D
    int i = 0;                                        // RDI / ESI
    while (i < len) {
        const char* base = fmt._data;
        const bool isParam = base[i] == '@' && (i + 1) < len &&
                             base[i + 1] >= '0' && base[i + 1] <= '9';
        if (!isParam) { ++i; continue; }

        if (i > 0) {
            String lit = work.SubString(segStart, i - 1);
            ret += lit;                               // the discarded copy is the temporary
        }

        const int digit = static_cast<int>(static_cast<signed char>(fmt._data[i + 1])) - '0';
        ret += params[digit];

        i += 2;
        segStart = i;
    }

    if (segStart != i) {                              // trailing literal run
        String lit = work.SubString(segStart, i - 1);
        ret += lit;
    }

    // `work`'s destructor is the release retail wrote out at 0x1800c8fe0.
    return ret;
}

String String::FormatIndexedList(const char* fmt, const char* const* argv, int argc) {
    String ret;
    if (fmt == nullptr)
        return ret;

    const int paramCount = FormatParamCount(fmt);

    if (paramCount == 0) {
        ret.Set(fmt);
        return ret;
    }

    StringArray params{nullptr, 0, 0};
    StringArray_SetCountFromEmpty(&params, paramCount);

    // NOT RETAIL (review F-125, vararg over-read): retail reads `paramCount` varargs
    // whatever its caller passed. Only the `argc` passed are read; the slots past them keep
    // the empty String SetCountFromEmpty put there.
    const int readCount = (argc < paramCount) ? argc : paramCount;
    for (int i = 0; i < readCount; ++i) {
        // Retail walks the register home slots forward from the format
        // string's; every vararg is a `const char*`.
        String tmp;
        tmp.Set(argv[i]);
        params.data[i] = tmp;
    }

    String fmtStr;
    fmtStr.Set(fmt);
    ret = FormatParamList(fmtStr, params.data);

    StringArray_DestroyRange(params.data, params.count);
    if (params.data != nullptr)
        RtMemFree(params.data);
    return ret;
}

// ?Release@String@System@@AEAAXXZ  retail 0x1800c9870
//
// The private release primitive, byte-for-byte the sequence String.h documents
// and RtString_ReleaseInline already implements -- including the asymmetry that
// the freeing branch leaves `data - 8` in the object while the sharing branch
// nulls it. This export is those same eleven instructions; it is registered so
// that the name is not missing at cutover, and so our callers stop reaching the
// DLL for a routine this file has had all along.
//
// NO ORACLE, and the reason is intrinsic rather than a gap in the harness: the
// refcount lives in the one shared block, so running the shipped body after
// ours would decrement a second time and free a live string. There is nothing
// to snapshot and restore -- a second String object referring to the same block
// IS the same counter. The shadow below is our own post-condition (the object
// is either nulled or holds block-8), which is a weaker claim than a diff and
// is labelled as one.
// The body moved to `String::Release()` in the "inlined idioms" section at the
// top of this file — it and RtString_ReleaseInline were always the same eleven
// instructions, and now they are literally the same function.

// ?ToInteger@String@System@@SAHPEBD@Z  retail 0x18003f620
//
// A hand-rolled atoi over `int`: skip ' ' and '\t', one optional '+'/'-', then
// digits accumulated as `v = v*10 + (c - '0')` -- retail spells that
// `EDX = c + 2*(5*EDX - 24)` (0x18003f664..0x18003f670), which is the same
// value.
//
// The digit test is `CMP AL,0x30 / JL` and `CMP AL,0x39 / JG` on a byte loaded
// with MOVZX and compared SIGNED, so any byte >= 0x80 reads as negative and
// ends the number. Reproduced with a signed char rather than an isdigit(), so a
// UTF-8 continuation byte terminates it the same way it does in the DLL.
//
// No overflow handling and no errno: past INT_MAX retail simply wraps, and so
// do we. NOT RETAIL in spelling only (review F-149, signed overflow): the
// accumulator is a uint32_t, because the same wrap on an `int` is undefined in
// C++. Same 32 bits as retail's LEA chain for every input.
int String::ToInteger(const char* s) {
    uint32_t result = 0;
    if (s != nullptr) {
        const char* p = s;
        while (*p == ' ' || *p == '\t') ++p;
        bool negative = false;
        if (*p == '-') { negative = true; ++p; }
        else if (*p == '+') { ++p; }
        while (static_cast<signed char>(*p) >= '0' && static_cast<signed char>(*p) <= '9') {
            result = result * 10u + static_cast<uint32_t>(static_cast<signed char>(*p) - '0');
            ++p;
        }
        if (negative)
            result = 0u - result;
    }
    return static_cast<int>(result);
}

// ============================================================
// Serialize — NOT DIFFED (consumes / produces stream bytes)
// ============================================================
// Running the original as an oracle would read or write the stream a second
// time and desync every record after it — the exact failure mode of the memory
// note reference_slot3_wrapper_must_resolve_first.md, manufactured on purpose.

namespace {
// ---- the legacy UTF-16 branch --------------------------------------------
// Retail decodes with FUN_1800c9fc0(1, p, end, &cp, 1) and re-encodes with
// FUN_1800c9b60(cp, dst, 2). Both are MSVC-CRT jump-table dispatchers indexed
// by an encoding id (1 = UTF-16, 2 = UTF-8) whose tables Ghidra cannot recover
// ("Could not recover jumptable ... Too many branches"), so THIS PAIR IS THE
// ONE PIECE OF THIS LANE THAT IS NOT TRANSCRIBED FROM RETAIL — it is a
// standard UTF-16LE decoder and UTF-8 encoder written from the specification.
//
// The branch is reachable only when READING a string whose serialized length
// word has bit 31 clear. Every write path sets that bit unconditionally
// (`BTS EAX,0x1f` at 0x1800c9959), so it can only fire on data produced by a
// build older than the one that added the flag. It is not expected to run at
// all, and it says so on stderr the first time it does.
// Walks UTF-16LE code units (x64 is little-endian: a unit IS the LE pair);
// returns the units consumed.
int Utf16DecodeOne(const uint16_t* p, const uint16_t* end, int32_t* cp) {
    if (p + 1 > end) {
        *cp = -1;
        return 1;
    }
    const uint16_t w = p[0];
    if (w >= 0xD800 && w < 0xDC00 && p + 2 <= end) {
        const uint16_t w2 = p[1];
        if (w2 >= 0xDC00 && w2 < 0xE000) {
            *cp = 0x10000 + (((w - 0xD800) << 10) | (w2 - 0xDC00));
            return 2;
        }
    }
    *cp = w;
    return 1;
}

int Utf8EncodeOne(int32_t cp, char* dst) {
    const uint32_t c = static_cast<uint32_t>(cp);
    if (c < 0x80) { dst[0] = static_cast<char>(c); return 1; }
    if (c < 0x800) {
        dst[0] = static_cast<char>(0xC0 | (c >> 6));
        dst[1] = static_cast<char>(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        dst[0] = static_cast<char>(0xE0 | (c >> 12));
        dst[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        dst[2] = static_cast<char>(0x80 | (c & 0x3F));
        return 3;
    }
    dst[0] = static_cast<char>(0xF0 | (c >> 18));
    dst[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    dst[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    dst[3] = static_cast<char>(0x80 | (c & 0x3F));
    return 4;
}
}  // namespace

// ?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z  retail 0x1800c9930
void String::Serialize(::IO::Serializer& serializer) {
    ::IO::ReaderWriter* const rw = serializer._readerWriter;   // +0x08

    // Length INCLUDING the terminator, with bit 31 as the "UTF-8 payload"
    // flag. On save this is what goes out; on load the call overwrites it.
    uint32_t n = _data ? static_cast<uint32_t>(StringLength(_data) + 1) : 0u;
    n |= kSerializedUtf8Flag;
    rw->SerializeUInt(n, nullptr);                       // +0x98

    const bool legacyWide = ((n >> 31) & 1u) == 0u;
    n &= kSerializedLengthMask;

    // Stream +0x08: 0 = loading/decode, non-zero = saving/encode.
    const bool loading = (serializer._stream->_direction == 0);

    // NOT RETAIL (hardening, review 2026-11-03 F-079 / F-080): retail sizes the String
    // straight from the 31-bit length word, so a hostile record asks for 2 GB (plain) or
    // 4 GB (legacy UTF-16, n * 2 below) and then reads up to 2^31 failing elements. A
    // length the rest of a binary stream cannot hold is rejected here: the String is
    // left empty and nothing more is read. A well-formed record always fits, so valid
    // saves and packages load byte-identically; a non-binary reading stream (the ext
    // .rsc text reader) is never capped (IO::StringLengthExceedsStream).
    if (loading && n != 0) {
        const uint64_t need = legacyWide ? static_cast<uint64_t>(n) * 2
                                         : static_cast<uint64_t>(n);
        if (::IO::StringLengthExceedsStream(*serializer._stream, need)) {
            Release();
            _data = nullptr;
            return;
        }
    }

    if (loading && n != 0)
        SetLength(static_cast<int>(n) - 1);

    if (legacyWide && loading && n != 0) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr,
                "[RtString] legacy UTF-16 string in stream (length word bit 31 clear); "
                "converting with the spec decoder, not a transcription of the CRT helper\n");
        }

        uint16_t* buf = static_cast<uint16_t*>(RtMemAlloc(static_cast<uint64_t>(n) * 2, 4));
        rw->SerializeUShortArray(static_cast<int>(n), buf, nullptr);   // +0x50

        // Retail stops two bytes short: the trailing UTF-16 NUL is not decoded.
        uint16_t* const end = buf + (static_cast<int>(n) - 1);

        int total = 0;
        for (uint16_t* p = buf; p < end;) {
            int32_t cp = -1;
            p += Utf16DecodeOne(p, end, &cp);
            if (cp >= 0) {
                char scratch[4];
                total += Utf8EncodeOne(cp, scratch);
            }
        }

        SetLength(total);
        _data[total] = '\0';

        char* const base = _data;
        char* out = base;
        int written = 0;
        for (uint16_t* p = buf; p < end;) {
            int32_t cp = -1;
            p += Utf16DecodeOne(p, end, &cp);
            if (cp < 0)
                continue;
            char scratch[4];
            const int m = Utf8EncodeOne(cp, scratch);
            written += m;
            if (written > total || base == nullptr || m <= 0)
                continue;
            std::memcpy(out, scratch, static_cast<size_t>(m));
            out += m;
        }

        RtMemFree(buf);
        return;
    }

    rw->SerializeCharArray(static_cast<int>(n), _data, nullptr);   // +0x18

    // NOT RETAIL (review 2026-11-03 F-023, heap over-read): retail fills all n bytes,
    // _data[n - 1] included, from the stream and never stores the terminator, so a
    // payload that does not end in NUL leaves a String every strlen-walking consumer
    // reads past. A well-formed record already has the NUL there.
    if (loading && n != 0)
        _data[n - 1] = '\0';
}

// ============================================================
// CUTOVER CLOSURE BATCH D2 "helpers-high" (2026-09-12)
// Four more System::String members, all exported and all previously reached
// only by address from src/engine/ui/editelement.cpp.
// ============================================================

// ??HSystem@@YA?AVString@0@AEBV10@D@Z   retail 0x1800c88b0
// The free `operator+(const String&, char)`. Namespace scope, so the hidden
// return slot is the FIRST argument (RCX=ret, RDX=lhs, R8B=c).
//
// The empty-lhs arm is NOT "return a copy of the char": retail builds a
// two-byte buffer ON THE STACK holding {c, 0} (one 16-bit store, `MOV
// [RSP+0x30],AX` with AH already zero) and runs it through String::Set, so the
// result goes through the normal allocate-and-copy path. Transcribed that way
// rather than folded, because Set is where the length header is written.
String operator+(const String& lhs, char c) {
    String ret;
    const int len = lhs.GetLength();
    if (lhs._data == nullptr || len == 0) {
        char buf[2];
        buf[0] = c;
        buf[1] = '\0';
        ret.Set(buf);
        return ret;
    }

    String tmp;
    StringPrivateAccess::SetLength(tmp, len + 1);
    std::memcpy(tmp._data, lhs._data, static_cast<size_t>(len));
    tmp._data[len] = c;
    tmp._data[len + 1] = '\0';

    // Retail's tail, written out rather than expressed as Adopt + Release:
    // it bumps the refcount and then drops it again (the transfer is a copy
    // ctor MSVC inlined into a self-cancelling pair) and, unlike the standard
    // release idiom, it does NOT null `tmp.data` on the surviving branch. The
    // local dies immediately either way; the pair is reproduced because a
    // watchpoint on the refcount word sees it.
    if (tmp._data != nullptr) {
        ret._data = tmp._data;
        int32_t* rc = &RtSystem::StringRefCount(tmp._data);
        ++*rc;
        --*rc;
        if (*rc == 0) RtMemFree(tmp._data - 8);
    }
    tmp._data = nullptr;   // see operator+(String, String): retail's local was a POD
    return ret;
}

// ??YString@System@@QEAA?AV01@D@Z   retail 0x1800c89e0
// MEMBER operator+=(char): RCX=this, RDX=hidden return slot, R8B=c. Same shape
// as the two AppendAssign overloads above -- concat into a temporary, Set
// `this` from it, release the temporary, then return a COPY of the new value.
String String::operator+=(char c) {
    String tmp = *this + c;
    Set(tmp._data);
    StringPrivateAccess::Release(tmp);
    tmp._data = nullptr;

    String ret;
    StringPrivateAccess::Adopt(ret, _data);
    return ret;
}

// ?InsertAt@String@System@@QEAA?AV12@HAEBV12@@Z   retail 0x1800c8510
// RCX=this, RDX=hidden return slot, R8D=index, R9=the String to insert.
//
// Three arms, read from the listing at 0x1800c8547..0x1800c85e9 (Ghidra's
// pseudo-C drops the third argument of every operator+ call here):
//   index == 0    -> ret = s + this            (CALL 0x1800c9440, RDX=R9, R8=RCX)
//   index == len  -> ret = this + s            (RDX=RSI, R8=R9)
//   otherwise     -> tail = this.SubString(index, len - 1)
//                    head = this.SubString(0, index - 1)
//                    tmp  = head + s
//                    ret  = tmp + tail
//                    release tmp, head, tail -- in THAT order
// `len` is re-read from the header on each arm exactly as retail does, and it
// is 0 when `data` is null. Nothing is clamped: SubString's inclusive-end
// convention means an index past the end walks past the terminator, which is
// retail's behaviour and stays.
String String::InsertAt(int index, const String& s) {
    if (index == 0)
        return s + *this;
    if (index == GetLength())
        return *this + s;

    // The three locals are released in retail's order (tmp, head, tail) by
    // being declared in the reverse of it.
    String tail = SubString(index, GetLength() - 1);
    String head = SubString(0, index - 1);
    String tmp  = head + s;
    return tmp + tail;
}

// ?RemoveAt@String@System@@QEBA?AV12@H@Z   retail 0x1800c86b0
// RCX=this, RDX=hidden return slot, R8D=index. Three arms, from the listing at
// 0x1800c86dc..0x1800c8784:
//   index == 0    -> ret = this.SubString(1, len - 1)
//   index == len  -> ret = this.SubString(0, len - 2)   *** note: len, not
//                    len - 1. Removing "at" the length is out of range, and
//                    retail answers it by dropping the LAST character. Kept. ***
//   otherwise     -> head = this.SubString(0, index - 1)
//                    tail = this.SubString(index + 1, len - 1)
//                    ret  = head + tail
//                    release tail, then head
String String::RemoveAt(int index) const {
    if (index == 0)
        return SubString(1, GetLength() - 1);
    if (index == GetLength())
        return SubString(0, GetLength() - 2);

    String head = SubString(0, index - 1);
    String tail = SubString(index + 1, GetLength() - 1);
    return head + tail;
}

}  // namespace System

// ############################################################################
// ENDGAME B.3.1 step 1 — THE SHIMS. THEY DIE IN STEP 2.
// ############################################################################
// Every name below kept its exact pre-conversion signature — including the
// hidden return slot, which the callers still pass — and is now one line over
// the member on the same eight bytes. The `RegisterRuntimeExport` /
// `RegisterRuntimeAddr` rows at the bottom of the file register THESE, so the
// registry seam, the `fn_String_*` pointers in engine/funcs/engine_funcs.h and
// every `RT_EXPORT_ONCE("...String@System@@...")` site keep resolving exactly
// as before.
//
// A caller that reaches a shim hands it RAW BYTES, not a constructed object:
// the `ret` slot is uninitialised String-sized storage. So a shim that
// produces a value CONSTRUCTS INTO the slot with placement new rather than
// assigning to it — an assignment would Release() whatever garbage pointer the
// caller's stack happened to hold.
//
// Step 2 deletes each of these when the last caller of it is converted; the
// grep to run first is in B31_RULES.md ("when a shim ... has zero callers left").
// ############################################################################
namespace RtSystem {

// ---- the primitives declared in src/engine/system/string_internal.h ----------------
// (B.4 acceptance MOD: `RtString_SetLength` and `RtString_GetLength` had no
// caller left and are gone with the POD they took.)
// (RtString_ReleaseInline: deleted at the B7 close -- no caller since cd81636d3, audit F7.)
int RtString_StrCompare(const char* a, const char* b) {
    return System::String::StrCompare(a, b);
}

}  // namespace RtSystem

