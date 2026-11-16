#pragma once
// src/engine/abi.h -- the data model every pinned layout in the tree assumes, checked once
// (Phase D.4, plans/ABI.md step C.2). Included by global_types.h.
//
// The tree is built by GCC, which lays classes out by the Itanium C++ ABI on every target
// (MinGW-w64 and Linux x86-64 among them). The ~6,900 sizeof / offsetof asserts prove that this
// layout reproduces retail's MSVC offsets; they hold on any target where the facts below hold.
// A pinned layout holds only fixed-width integers, pointers, float / double, bool, enums and
// other pinned classes: never `long` (4 bytes on Win64, 8 on Linux), `wchar_t` (2 / 4), `size_t`,
// a bit-field or a native lock / handle type. `tools/abi_audit.py` enforces that rule.

#include <climits>
#include <cstdint>

static_assert(sizeof(void*) == 8, "the retail layouts are x64: 8-byte pointers");
static_assert(sizeof(int) == 4, "int is 4 bytes");
static_assert(sizeof(short) == 2, "short is 2 bytes");
static_assert(sizeof(long long) == 8, "long long is 8 bytes");
static_assert(sizeof(float) == 4, "float is 4 bytes");
static_assert(sizeof(double) == 8, "double is 8 bytes");
static_assert(sizeof(bool) == 1, "bool is 1 byte");
static_assert(alignof(int64_t) == 8, "int64_t is 8-aligned (retail's 8-byte fields)");
static_assert(alignof(double) == 8, "double is 8-aligned");
static_assert(CHAR_BIT == 8, "8-bit bytes");
static_assert(static_cast<char>(-1) < 0,
              "char is signed, as MSVC's is (build with -fsigned-char on AArch64)");
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "little-endian: the save format and the raw-offset reads assume it");

// RT_ASM_LABEL("_ZTV...") -- bind a declaration to a symbol by its linker name. The literal is
// the C++ (Itanium) name; the target's user-label prefix (empty on x86-64 MinGW and Linux, `_`
// on macOS) is pasted in front of it, so the label names the same symbol on every target.
#define RT_ABI_STR2(x) #x
#define RT_ABI_STR(x) RT_ABI_STR2(x)
#define RT_ASM_LABEL(sym) __asm__(RT_ABI_STR(__USER_LABEL_PREFIX__) sym)
