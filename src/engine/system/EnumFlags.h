#pragma once
// ============================================================================
// src/engine/system/EnumFlags.h — bitwise operators for a scoped flag enum
// ============================================================================
//
// A flag set is declared as a scoped enum with a fixed unsigned underlying
// type, then opted in with OPENBA_ENUM_FLAGS in the same namespace:
//
//     enum class PathBits : uint32_t { Faster = 0x001, Fast = 0x002, ... };
//     OPENBA_ENUM_FLAGS(PathBits)
//
// The macro defines, for that one enum:
//   a | b, a & b, a ^ b, ~a, a |= b, a &= b, a ^= b   (results stay the enum)
//   Any(a)    true when any bit of a is set          (the old `(x & m) != 0`)
//   Bits(a)   the underlying integer                 (a uint32_t field, a
//             retail-typed argument, a serializer door)
//
// Every operator is a constexpr inline function over the underlying integer.
// On CONSTANT operands (`PathBits::Walk | PathBits::Water`, `Bits(Flag::X)`)
// the front end evaluates it, and the result is the same constant a literal
// gives. On a VARIABLE operand it is an inline call: the value is the same,
// but GCC folds the expression later than it folds the plain integer form and
// can pick different instructions (a shift-test instead of a bit-test, an
// inverted cmov). Where code must stay byte-identical to the integer original,
// compare the object (a one-expression test may need a helper that holds the
// whole expression, like PathType.h's HasType, or a static_cast). A
// class-nested enum is opted in from the enclosing namespace with its
// qualified name (ADL finds the operators there).
//
// ToUnderlying(e) (C++23's std::to_underlying) is the same cast for any scoped
// enum, flag set or not, e.g. to index an array by an id enum.
//
// The macro is the one preprocessor definition allowed for named constants:
// the operators have to be stamped out per enum.
// ============================================================================

#include <type_traits>

template <typename E>
constexpr std::underlying_type_t<E> ToUnderlying(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}

#define OPENBA_ENUM_FLAGS(Name)                                                       \
    constexpr Name operator|(Name a, Name b) noexcept {                               \
        return static_cast<Name>(::ToUnderlying(a) | ::ToUnderlying(b));              \
    }                                                                                 \
    constexpr Name operator&(Name a, Name b) noexcept {                               \
        return static_cast<Name>(::ToUnderlying(a) & ::ToUnderlying(b));              \
    }                                                                                 \
    constexpr Name operator^(Name a, Name b) noexcept {                               \
        return static_cast<Name>(::ToUnderlying(a) ^ ::ToUnderlying(b));              \
    }                                                                                 \
    constexpr Name operator~(Name a) noexcept {                                       \
        return static_cast<Name>(~::ToUnderlying(a));                                 \
    }                                                                                 \
    constexpr Name& operator|=(Name& a, Name b) noexcept {                            \
        a = a | b;                                                                    \
        return a;                                                                     \
    }                                                                                 \
    constexpr Name& operator&=(Name& a, Name b) noexcept {                            \
        a = a & b;                                                                    \
        return a;                                                                     \
    }                                                                                 \
    constexpr Name& operator^=(Name& a, Name b) noexcept {                            \
        a = a ^ b;                                                                    \
        return a;                                                                     \
    }                                                                                 \
    constexpr bool Any(Name a) noexcept {                                             \
        return ::ToUnderlying(a) != 0;                                                \
    }                                                                                 \
    constexpr std::underlying_type_t<Name> Bits(Name a) noexcept {                    \
        return ::ToUnderlying(a);                                                     \
    }
