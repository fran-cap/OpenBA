#pragma once
// ============================================================================
// src/engine/system/String.h — System::String binary layout + allocation
//                                 header (Wave 2, lane A)
// ============================================================================
//
// Every fact below was read out of Runtime-x64.dll in Ghidra (image base
// 0x180000000, decompiled 2026-09-06); the function that pins it is named.
//
// THE OBJECT
// ----------
// System::String is ONE POINTER. `?GetLength@String@System@@QEBAHXZ`
// (0x1800063c0) is the whole class in miniature:
//
//     if (*(int64*)this != 0) return *(int*)(*(int64*)this - 8);
//     return 0;
//
// so the object is `{ char* data; }` and the length lives in front of the
// character data, not in the object. Confirmed independently by the default
// ctor (0x180006760: `*(uint64*)this = 0` and nothing else) and by the copy
// ctor (0x180006cb0), which copies exactly one qword.
//
// THE ALLOCATION HEADER
// ---------------------
// `?SetLength@String@System@@AEAAXH@Z` (0x1800c8220) is the only allocator in
// the class and it pins the whole block:
//
//     block = MemoryAllocator::Allocate(_singleton, (len + 1) + 8, 4);
//     this->data = (int*)block + 2;      // data = block + 8
//     ((int*)block)[0] = len;            // data[-8] = length
//     ((int*)block)[1] = 1;              // data[-4] = refcount, born at 1
//
// So, with `p` = the allocator's user pointer:
//
//     p + 0 : int32 length      (character count, EXCLUDING the NUL)
//     p + 4 : int32 refcount
//     p + 8 : char data[length + 1]
//     String::data == p + 8
//
// This confirms the header comment in standalone/filename_runtime_hook.cpp
// ("refcount at buffer-4, user pointer at buffer-8") and adds the field it did
// not name: the dword at buffer-8 is the LENGTH, which is why a `dec`/`inc` on
// a freed String scribbles on the free-list link's high dword and not its low.
//
// SetLength does NOT write a terminator: every caller writes data[len] itself
// (Set via StrCopy, SubString explicitly, operator+ explicitly).
//
// RETAIL'S RELEASE IDIOM, AND WHY IT MUST BE COPIED EXACTLY
// --------------------------------------------------------
// The release sequence is inlined at ~200 sites in the DLL and is always:
//
//     if (data) {
//         if (--data[-4] == 0) { this->data = data - 8; MemFree(this->data); }
//         else                 { this->data = nullptr; }
//     }
//
// Note the asymmetry, which is NOT a decompiler artifact (see ~String
// 0x180006c80): on the FREEING branch the object is left holding
// `data - 8`, a dangling pointer, not null. On the sharing branch it is
// nulled. Ports must reproduce both, because ~String runs on stack temporaries
// whose bytes are then read by nothing — but Set() and operator= call the same
// sequence and then immediately overwrite `data`, and any port that "tidied"
// the freeing branch to null would still be correct there while diverging in
// the dtor. Reproduce it; do not tidy it.
//
// THE ALLOCATOR IS NOT OURS
// -------------------------
// Strings cross the DLL boundary constantly (the DLL makes them, we destroy
// them, and vice versa), so every allocation and free MUST go through
// Runtime-x64.dll's own System::MemoryAllocator, resolved through the registry (historically the raw door)
// — never ResolveExport, which would hand back a registered port. See
// src/engine/system/string.cpp.
// ============================================================================

#include <cstdint>
#include <cstddef>
#ifndef BANISHED_UNIT_TEST
#include "MemoryAllocator.h"   // B7 D4: the inline release calls MemFree by name
#endif

namespace RtSystem {

// (B.4 acceptance MOD: the `RtSystem::String` POD mirror is gone with its last
// user, `Graphics::MaterialConstant::name`; System::String below is the one
// layout. The block header and its accessors stay: they describe the bytes in
// front of `System::String::_data`.)

// ---- the allocation header in front of `data` ------------------------------
// Laid out so that `reinterpret_cast<StringBlock*>(data - 8)` is exact.
struct StringBlock {
    int32_t length;    // -0x08 from data
    int32_t refCount;  // -0x04 from data
    char    chars[1];  // +0x00 = String::data
};
static_assert(offsetof(StringBlock, chars) == 8, "String chars @ block+8");
static_assert(offsetof(StringBlock, length) == 0x0, "StringBlock::length");
static_assert(offsetof(StringBlock, refCount) == 0x4, "StringBlock::refCount");

// Header accessors, spelled the way the DLL spells them.
inline int32_t& StringLength(char* data)   { return reinterpret_cast<int32_t*>(data)[-2]; }  // CAST-OK: string_header
inline int32_t& StringRefCount(char* data) { return reinterpret_cast<int32_t*>(data)[-1]; }  // CAST-OK: string_header
inline int32_t  StringLength(const char* data) {
    return reinterpret_cast<const int32_t*>(data)[-2];  // CAST-OK: string_header
}
inline int32_t  StringRefCount(const char* data) {
    return reinterpret_cast<const int32_t*>(data)[-1];  // CAST-OK: string_header
}

// The MemAlloc alignment argument SetLength passes (0x1800c8220).
constexpr int kStringAllocAlign = 4;

// The one allocator pair: MemAlloc 0x1800c7a50 / MemFree 0x1800c7ad0. RtMemFree
// is header-inline (B7 D4) because the inline release below calls it and every
// retail open-coded release is ONE direct CALL 0x1800c7ad0: inline, the tree's
// release is one direct call to System::MemoryAllocator::MemFree too, not a
// call to a forwarder. The host unit test links no MemoryAllocator: there both
// stay the malloc / free pair defined in src/engine/system/string.cpp.
void* RtMemAlloc(uint64_t size, int align);
#ifdef BANISHED_UNIT_TEST
void  RtMemFree(void* p);
#else
[[gnu::always_inline]] inline void RtMemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}
#endif

}  // namespace RtSystem

// ============================================================================
// ENDGAME B.3.1 step 1 — the real class, `System::String`
// ============================================================================
//
// The class below is the SAME EIGHT BYTES under retail's own name. The bodies
// are the ones the C functions in src/engine/system/string.cpp already had —
// moved, not rewritten, retail's release asymmetry included.
//
// THE HIDDEN RETURN SLOT IS GONE. It was the DLL boundary's shape; with
// Runtime-x64.dll removed every caller and callee is compiled by the same GCC,
// so the by-memory return is the compiler's business again and the methods
// return `String` by value (B31_RULES.md, "The one fact that changes the rules
// since the port"). The `RtSystem_String_*` free functions keep the old
// (self, ret, ...) signature as one-line shims so that no caller has to move in
// this commit.
// ============================================================================

#include <cstdarg>

namespace IO { class Serializer; }

namespace System {

class String {
public:
    // ---- lifecycle ---------------------------------------------------------
    // B7 D4: the lifecycle, Release, Adopt and the copy operator= are inline and
    // FORCED inline (`gnu::always_inline`): retail open-codes them at every
    // measured caller, and GCC's inliner otherwise keeps an out-of-line copy on
    // some paths (GetNextItem's ^i return measured it), a call retail lacks.
    [[gnu::always_inline]] String();                // ??0String@System@@QEAA@XZ         0x180006760
    String(const char* src);                        // ??0String@System@@QEAA@PEBD@Z     0x18000b630
    [[gnu::always_inline]] String(const String& other); // ??0String@System@@QEAA@AEBV01@@Z  0x180006cb0
    // Not retail's (C++03 had no move). A pure pointer steal that leaves the
    // source null: the refcount is untouched and the moved-from destructor is
    // a no-op, so nothing is observable (B31_RULES class rule 4).
    String(String&& other) noexcept;
    [[gnu::always_inline]] ~String();               // ??1String@System@@QEAA@XZ         0x180006c80

    [[gnu::always_inline]] String& operator=(const String& rhs); // ??4String@System@@QEAAAEAV01@AEBV01@@Z 0x1800c9810
    String& operator=(String&& rhs) noexcept;       // ours, as above

    // ---- content -----------------------------------------------------------
    void Set(const char* src);                      // ?Set@String@System@@QEAAXPEBD@Z   0x1800c95d0
    [[gnu::always_inline]] int GetLength() const;   // ?GetLength@String@System@@QEBAHXZ 0x1800063c0

    String SubString(int start, int end) const;     // ?SubString@..@QEBA?AV12@HH@Z      0x1800c9230
    String InsertAt(int index, const String& s);    // ?InsertAt@..@QEAA?AV12@HAEBV12@@Z 0x1800c8510
    String RemoveAt(int index) const;               // ?RemoveAt@..@QEBA?AV12@H@Z        0x1800c86b0

    // ---- comparison --------------------------------------------------------
    bool operator==(const String& other) const;     // ??8String@System@@QEBA_NAEBV01@@Z 0x1800063d0
    bool operator==(const char* other) const;       // ??8String@System@@QEBA_NPEBD@Z    0x180053c00
    bool operator!=(const String& other) const { return !(*this == other); }
    bool operator!=(const char* other) const   { return !(*this == other); }
    bool operator<(const String& other) const;      // ??MString@System@@QEBA_NAEBV01@@Z 0x180029360

    // ---- append ------------------------------------------------------------
    // Retail's operator+= returns the new value BY VALUE (`?AV01@`), not a
    // reference; every caller normally discards the copy. Kept as retail wrote
    // it -- the copy is a refcount bump the game's watchpoints can see.
    String operator+=(const String& rhs);           // ??YString@System@@QEAA?AV01@AEBV01@@Z 0x1800c8a90
    String operator+=(const char* rhs);             // ??YString@System@@QEAA?AV01@PEBD@Z    0x1800c9120
    String operator+=(char c);                      // ??YString@System@@QEAA?AV01@D@Z       0x1800c89e0

    // ---- statics -----------------------------------------------------------
    // Format is vsnprintf: each argument has its conversion's type. NOT RETAIL (UB, vararg
    // type): the game's `%d` / `%i` callers pass `int`; the port passed retail's zero-extended
    // 64-bit register as a uint64_t, which `%d` does not read. The printed low 32 bits are the same.
    static String Format(const char* fmt, ...);        // ?Format@..@SA?AV12@PEBDZZ         0x1800c9730
    // ?FormatIndexed@..@SA?AV12@PEBDZZ  0x1800c8b40. NOT RETAIL (review F-125, vararg
    // over-read): retail is `(const char* fmt, ...)` and reads as many `const char*` varargs
    // as the highest '@n' in fmt asks for, and fmt is string-table text a mod can supply.
    // A template keeps every call site's spelling and passes the argument count; an '@n'
    // past the arguments given reads an empty String.
    template <typename... Args>
    static String FormatIndexed(const char* fmt, Args... args) {
        const char* const argv[] = {static_cast<const char*>(args)..., nullptr};
        return FormatIndexedList(fmt, argv, static_cast<int>(sizeof...(Args)));
    }
    static int    FormatParamCount(const char* fmt);   // ?FormatParamCount@..@SAHPEBD@Z    0x1800c8d20
    static String FormatParamList(const String& fmt,
                                  const String* params); // ?FormatParamList@..             0x1800c8dc0
    static float  ToFloat(const char* s);              // ?ToFloat@String@System@@SAMPEBD@Z 0x1800c8070
    static int    ToInteger(const char* s);            // ?ToInteger@String@System@@SAHPEBD@Z 0x18003f620
    static int    StrCompare(const char* a, const char* b); // ?StrCompare@..@SAHPEBD0@Z    0x1800c98a0

    // Format's varargs entry point has no va_list twin in retail (there is no
    // such export). This carries the body so that `Format` and the transitional
    // `RtSystem_String_Format` shim are the same code and not two transcriptions.
    static String FormatV(const char* fmt, va_list ap);
    // FormatIndexed's body over the `argc` arguments its caller passed (see above).
    static String FormatIndexedList(const char* fmt, const char* const* argv, int argc);

    // ---- serialization -----------------------------------------------------
    // The u32 written before a serialized string: length + 1 (0 for null), with
    // bit 31 set for a UTF-8 payload (clear = the legacy UTF-16 payload).
    static constexpr uint32_t kSerializedUtf8Flag   = 0x80000000u;   // string.cpp BTS 31
    static constexpr uint32_t kSerializedLengthMask = 0x7FFFFFFFu;   // BTR 31: the length with the terminator
    void Serialize(IO::Serializer& serializer);     // ?Serialize@..@QEAAXAEAVSerializer@IO@@@Z 0x1800c9930

private:
    void SetLength(int len);                        // ?SetLength@..@AEAAXH@Z (PRIVATE)  0x1800c8220
    [[gnu::always_inline]] void Release();          // ?Release@..@AEAAXXZ    (PRIVATE)  0x1800c9870
    // The AddRef half of the copy ctor, inlined ~200 times in the DLL.
    // `_data` must already be null or released.
    [[gnu::always_inline]] void Adopt(char* src);

    friend struct StringPrivateAccess;
    friend class Filename;

public:
    // Retail's one and only field. PUBLIC: ported bodies and the step-1 shims
    // still read and hand the pointer back and forth by name.
    char* _data;
};
static_assert(sizeof(String) == 8, "System::String must be 8 bytes");
static_assert(offsetof(String, _data) == 0x0, "String::_data");

// The three private members, reachable by the step-1 shims (and by Filename's
// bodies, which retail also wrote against String's privates by pointer cast).
// TRANSITIONAL: this struct dies with the shims in step 2.
struct StringPrivateAccess {
    static void SetLength(String& s, int len) { s.SetLength(len); }
    static void Release(String& s)            { s.Release(); }
    static void Adopt(String& s, char* src)   { s.Adopt(src); }
};

// ---- the inline lifecycle (B7 D4) ------------------------------------------
// Retail open-codes these at every caller (the release idiom at ~200 sites, the
// copy ctor's INC [data-4], the default ctor's single zero store), so they are
// header-defined: a typed String local compiles to retail's open-coded shape
// instead of an out-of-line call. The bodies are the ones string.cpp had,
// unchanged: the same reads and stores, MemFree still a call.

// ??0String@System@@QEAA@XZ  retail 0x180006760
inline String::String() : _data(nullptr) {}

// ??0String@System@@QEAA@AEBV01@@Z  retail 0x180006cb0
inline String::String(const String& other) : _data(nullptr) {
    Adopt(other._data);
}

// Not retail's: the pointer steal (B31_RULES class rule 4). No refcount edit,
// and the source's destructor becomes a no-op.
inline String::String(String&& other) noexcept : _data(other._data) {
    other._data = nullptr;
}

// ??1String@System@@QEAA@XZ  retail 0x180006c80
inline String::~String() {
    Release();
}

// ?Release@String@System@@AEAAXXZ  retail 0x1800c9870, and the identical
// sequence inlined in ~String (0x180006c80). The asymmetry is retail's: on the
// freeing branch the object keeps `data - 8`, on the sharing branch it is
// nulled. See the banner above: reproduce it, do not tidy it.
inline void String::Release() {
    char* d = _data;
    if (d == nullptr)
        return;
    if (--RtSystem::StringRefCount(d) == 0) {
        _data = d - 8;
        RtSystem::RtMemFree(_data);
    } else {
        _data = nullptr;
    }
}

// ??4String@System@@QEAAAEAV01@AEBV01@@Z  retail 0x1800c9810, open-coded at
// its callers too (GetNextItem's ^lo restore 1800ce757, BeginLink 1800cdec5,
// GenerateItems 1800cf0a4). Self-assignment and same-buffer assignment are
// short-circuited on the POINTER, before the release; that guard is what makes
// `a = a` safe.
inline String& String::operator=(const String& rhs) {
    if (_data != rhs._data) {
        Release();
        _data = rhs._data;
        if (_data != nullptr)
            ++RtSystem::StringRefCount(_data);
    }
    return *this;
}

// ?GetLength@String@System@@QEBAHXZ  retail 0x1800063c0: `if (data) return
// data[-8]; return 0;`. Header-inline since block SAVELOAD (perf:): the out-of-
// line call was 26% of a save load's entity Serialize, because
// Filename::AdjustDelimiters re-reads the length once or twice PER CHARACTER
// and runs ~20 times per resource reference. The body is unchanged; only the
// call is gone.
inline int String::GetLength() const {
    return _data ? RtSystem::StringLength(_data) : 0;
}

// The AddRef half of the copy ctor (0x180006cb0). `_data` must already have
// been zeroed or released.
inline void String::Adopt(char* src) {
    if (src != nullptr) {
        _data = src;
        ++RtSystem::StringRefCount(src);
    }
}

// The three free operator+ overloads. Namespace scope in retail too (the
// mangling `??HSystem@@..` has no class component).
String operator+(const String& lhs, const String& rhs); // ??HSystem@@YA?AVString@0@AEBV10@0@Z    0x1800c9440
String operator+(const String& lhs, const char* rhs);   // ??HSystem@@YA?AVString@0@AEBV10@PEBD@Z 0x1800c92d0
String operator+(const String& lhs, char rhs);          // ??HSystem@@YA?AVString@0@AEBV10@D@Z    0x1800c88b0

}  // namespace System
