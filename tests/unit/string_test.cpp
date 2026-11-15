// ============================================================================
// tests/unit/string_test.cpp — System::String standing on its own (ENDGAME B.3.1)
// ============================================================================
// The cases are the ones the C bodies' comments document, in the order
// B31_RULES rule 6 lists them: the empty string is null, the refcount is born
// at 1, SubString's inclusive-end bounds, the Format forms retail supports, and
// ToFloat / ToInteger's edge cases.
//
// The allocator here is malloc: src/engine/system/string.cpp swaps
// System::MemoryAllocator for it under -DBANISHED_UNIT_TEST, because the real
// one locks and allocates on the MemoryAllocator singleton, which is exactly
// what a host test must not need.
// ============================================================================
#include "unit.h"
#include "system/String.h"

using System::String;

// The header the class allocates in front of the characters (String.h).
static int RefCount(const String& s) {
    return s._data ? reinterpret_cast<const int32_t*>(s._data)[-1] : 0;
}
static int StoredLength(const String& s) {
    return s._data ? reinterpret_cast<const int32_t*>(s._data)[-2] : 0;
}

UNIT_TEST(string_default_is_null) {
    String s;
    CHECK(s._data == nullptr);
    CHECK_EQ(s.GetLength(), 0);
    // A default String and a String set from nullptr are the same value.
    String t;
    t.Set(nullptr);
    CHECK(t._data == nullptr);
    CHECK(s == t);
}

UNIT_TEST(string_from_cstr_and_header) {
    String s("hello");
    CHECK_STREQ(s._data, "hello");
    CHECK_EQ(s.GetLength(), 5);
    CHECK_EQ(StoredLength(s), 5);      // the length EXCLUDES the terminator
    CHECK_EQ(RefCount(s), 1);          // born at 1
    CHECK_EQ(s._data[5], '\0');        // Set writes the terminator
}

UNIT_TEST(string_empty_cstr_still_allocates) {
    // Set("") is not Set(nullptr): retail allocates a one-byte buffer.
    String s("");
    CHECK(s._data != nullptr);
    CHECK_EQ(s.GetLength(), 0);
    CHECK_EQ(RefCount(s), 1);
}

UNIT_TEST(string_copy_shares_the_buffer) {
    String a("shared");
    String b(a);
    CHECK(a._data == b._data);         // no copy: the buffer is refcounted
    CHECK_EQ(RefCount(a), 2);
    {
        String c;
        c = a;
        CHECK_EQ(RefCount(a), 3);
    }
    CHECK_EQ(RefCount(a), 2);          // c's destructor gave the reference back
}

UNIT_TEST(string_move_steals_the_pointer) {
    String a("moved");
    char* raw = a._data;
    String b(static_cast<String&&>(a));
    CHECK(b._data == raw);
    CHECK(a._data == nullptr);
    CHECK_EQ(RefCount(b), 1);          // a steal is not an AddRef
}

UNIT_TEST(string_self_assign_is_safe) {
    String a("keep me");
    const String& alias = a;
    a = alias;                         // short-circuited on the POINTER
    CHECK_STREQ(a._data, "keep me");
    CHECK_EQ(RefCount(a), 1);
}

UNIT_TEST(string_set_to_equal_text_is_a_noop) {
    String a("same");
    char* before = a._data;
    a.Set("same");                     // compares equal -> no reallocation
    CHECK(a._data == before);
}

UNIT_TEST(string_equality_uses_stored_length_first) {
    String a("abc");
    String b("abc");
    String c("abd");
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a == "abc");
    CHECK(a != "ab");
    String nul;
    CHECK(nul == static_cast<const char*>(nullptr));
    CHECK(nul != "abc");
    CHECK(a != nul);
}

UNIT_TEST(string_less_puts_null_first_but_not_before_null) {
    String nul;
    String a("a");
    CHECK(nul < a);                    // null < x
    CHECK(!(a < nul));                 // x < null is false
    CHECK(!(nul < nul));               // null < null is false -- the ordering
                                       // every Collection::Set<String> is built on
    CHECK(String("a") < String("b"));
    CHECK(!(String("b") < String("a")));
}

UNIT_TEST(string_strcompare_is_null_tolerant_and_sign_extended) {
    CHECK_EQ(String::StrCompare(nullptr, nullptr), 0);
    CHECK_EQ(String::StrCompare(nullptr, "a"), -1);
    CHECK_EQ(String::StrCompare("a", nullptr), 1);
    CHECK_EQ(String::StrCompare("abc", "abc"), 0);
    CHECK(String::StrCompare("abc", "abd") < 0);
    // MOVSX, not MOVZX: a byte >= 0x80 compares as NEGATIVE, so "\x80" sorts
    // BEFORE "a". This is the WAVE 22 lane SYSBODY fact.
    const char high[] = {static_cast<char>(0x80), '\0'};
    CHECK(String::StrCompare(high, "a") < 0);
}

UNIT_TEST(string_concat) {
    CHECK(String("ab") + String("cd") == "abcd");
    CHECK(String("ab") + "cd" == "abcd");
    CHECK(String("ab") + 'c' == "abc");

    // An empty side ADOPTS the other side's buffer rather than copying it.
    String rhs("only");
    String sum = String() + rhs;
    CHECK(sum._data == rhs._data);
    CHECK_EQ(RefCount(rhs), 2);

    // The C-string overload cannot adopt (no refcounted buffer), so it copies.
    String cs = String() + "only";
    CHECK(cs == "only");
    CHECK_EQ(RefCount(cs), 1);

    // operator+(String, char) on an empty lhs goes through Set, so the header
    // is written the normal way.
    String ch = String() + 'x';
    CHECK(ch == "x");
    CHECK_EQ(StoredLength(ch), 1);

    // Concatenating two non-empty sides allocates a fresh buffer at refcount 1.
    String fresh = String("ab") + String("cd");
    CHECK_EQ(RefCount(fresh), 1);
    CHECK_EQ(StoredLength(fresh), 4);
}

UNIT_TEST(string_append_assign) {
    String s("ab");
    s += String("cd");
    CHECK(s == "abcd");
    s += "ef";
    CHECK(s == "abcdef");
    s += 'g';
    CHECK(s == "abcdefg");
    // The returned copy is a reference on the same buffer; discarding it must
    // not free anything.
    CHECK_EQ(RefCount(s), 1);
}

UNIT_TEST(string_substring_end_is_inclusive) {
    String s("abcdef");
    CHECK(s.SubString(0, 0) == "a");         // count = (end - start) + 1
    CHECK(s.SubString(1, 3) == "bcd");
    CHECK(s.SubString(0, 5) == "abcdef");
    // end == start - 1 is the zero-length case FormatParamList relies on.
    String empty = s.SubString(2, 1);
    CHECK(empty._data != nullptr);
    CHECK_EQ(empty.GetLength(), 0);
}

UNIT_TEST(string_insert_at) {
    String s("acd");
    CHECK(s.InsertAt(1, String("b")) == "abcd");
    CHECK(s.InsertAt(0, String("X")) == "Xacd");            // index 0 -> s + this
    CHECK(s.InsertAt(s.GetLength(), String("X")) == "acdX"); // index len -> this + s
}

UNIT_TEST(string_remove_at) {
    String s("abcd");
    CHECK(s.RemoveAt(0) == "bcd");
    CHECK(s.RemoveAt(1) == "acd");
    // index == len is out of range and retail answers it by dropping the LAST
    // character, not by returning the whole string. Kept.
    CHECK(s.RemoveAt(s.GetLength()) == "abc");
}

UNIT_TEST(string_format) {
    CHECK(String::Format("%d-%s", 42, "x") == "42-x");
    CHECK(String::Format("%5.2f", 1.5) == " 1.50");
    CHECK(String::Format("%%") == "%");
    CHECK(String::Format("plain") == "plain");
    String nul = String::Format(nullptr);
    CHECK(nul._data == nullptr);
}

// RETAIL'S INDEXING IS ZERO-BASED AND FormatParamCount IS A SIZE, NOT A MAX.
// The C body's own comment says "`@1` is index 1", and the code it describes
// says otherwise: `index = (int)(signed char)d - 0x2f`, i.e. `digit - '0' + 1`.
// FormatParamList then substitutes `params[digit - '0']`. So `@0` is the FIRST
// vararg, `@1` the second, and FormatParamCount answers the ARRAY SIZE the
// highest digit needs. The two fit together exactly; the prose did not.
UNIT_TEST(string_format_param_count) {
    CHECK_EQ(String::FormatParamCount(nullptr), 0);
    CHECK_EQ(String::FormatParamCount("no params"), 0);
    CHECK_EQ(String::FormatParamCount("@0"), 1);
    CHECK_EQ(String::FormatParamCount("@1"), 2);
    CHECK_EQ(String::FormatParamCount("@2 @2"), 3);   // a MAXIMUM, not an occurrence count
    CHECK_EQ(String::FormatParamCount("@2"), 3);      // ... so holes are legitimate
    CHECK_EQ(String::FormatParamCount("@9@1"), 10);
    CHECK_EQ(String::FormatParamCount("trailing @"), 0);  // a bare '@' is not a parameter
}

UNIT_TEST(string_format_indexed) {
    CHECK(String::FormatIndexed("no params") == "no params");
    CHECK(String::FormatIndexed("a@0b", "X") == "aXb");
    CHECK(String::FormatIndexed("@0-@1", "A", "B") == "A-B");
    // `@1` with two varargs is the SECOND one; see the note above.
    CHECK(String::FormatIndexed("a@1b", "X", "Y") == "aYb");
    // Adjacent parameters: the literal-run guard is `i > 0` on the ABSOLUTE
    // index, so "@0@1" appends a zero-length run between them rather than
    // skipping the append.
    CHECK(String::FormatIndexed("@0@1", "A", "B") == "AB");
    String nul = String::FormatIndexed(nullptr);
    CHECK(nul._data == nullptr);
}

UNIT_TEST(string_format_param_list) {
    String params[3];
    params[1].Set("one");
    params[2].Set("two");
    CHECK(String::FormatParamList(String("<@1|@2>"), params) == "<one|two>");
    CHECK(String::FormatParamList(String("literal"), params) == "literal");
}

UNIT_TEST(string_to_integer) {
    CHECK_EQ(String::ToInteger(nullptr), 0);
    CHECK_EQ(String::ToInteger(""), 0);
    CHECK_EQ(String::ToInteger("42"), 42);
    CHECK_EQ(String::ToInteger("  \t-7"), -7);
    CHECK_EQ(String::ToInteger("+7"), 7);
    CHECK_EQ(String::ToInteger("12abc"), 12);
    CHECK_EQ(String::ToInteger("abc"), 0);
    // A byte >= 0x80 reads as negative through the signed compare and ends the
    // number, exactly as a UTF-8 continuation byte does in the DLL.
    const char cont[] = {'1', static_cast<char>(0xC3), '2', '\0'};
    CHECK_EQ(String::ToInteger(cont), 1);
}

UNIT_TEST(string_to_float) {
    CHECK_EQ(String::ToFloat(nullptr), 0.0f);
    CHECK_EQ(String::ToFloat(""), 0.0f);
    CHECK_EQ(String::ToFloat("1"), 1.0f);
    CHECK_EQ(String::ToFloat(" \t-2.5"), -2.5f);
    CHECK_EQ(String::ToFloat("+0.5"), 0.5f);
    CHECK_EQ(String::ToFloat("1e2"), 100.0f);
    CHECK_EQ(String::ToFloat("1E2"), 100.0f);
    CHECK_EQ(String::ToFloat("1e-2"), 0.01f);
    CHECK_EQ(String::ToFloat("abc"), 0.0f);
    // The exponent is CLAMPED at 0x7f rather than saturating to infinity, and
    // it is applied in chunks of 1e20 / 1e8 / 10.
    const float clamped = String::ToFloat("1e999");
    CHECK(clamped > 0.0f);
}

// ---- 2026-10-17: the exponent clamp's actual values, Format's buffer edge, and operator<
//      on equal non-null strings -----------------------------------------------------------
#include <cmath>
#include <string>

UNIT_TEST(string_to_float_exponent_clamp_values) {
    // The exponent clamps at 0x7f (127), not at the float range: the scale is then
    // 1e20 x6 (the 20-chunks) x 10 x7, and 1e20f * 1e20f already overflows a float, so
    // "1e999" is +inf, "-1e999" -inf, and "1e-999" divides by that inf: +0 (and "-1e-999"
    // -0, the sign multiplied in last).
    const float big = String::ToFloat("1e999");
    CHECK(std::isinf(big) && big > 0.0f);
    const float neg = String::ToFloat("-1e999");
    CHECK(std::isinf(neg) && neg < 0.0f);
    const float tiny = String::ToFloat("1e-999");
    CHECK_EQ(tiny, 0.0f);
    CHECK(!std::signbit(tiny));
    const float negTiny = String::ToFloat("-1e-999");
    CHECK_EQ(negTiny, 0.0f);
    CHECK(std::signbit(negTiny));
    // Everything at or past the clamp is the same value: 127 is the clamp, 128 is the
    // first clamped exponent, 999 is clamped too.
    uint32_t b127, b128, b999;
    const float f127 = String::ToFloat("1e127"), f128 = String::ToFloat("1e128");
    std::memcpy(&b127, &f127, 4); std::memcpy(&b128, &f128, 4); std::memcpy(&b999, &big, 4);
    CHECK_EQ(b127, b999);
    CHECK_EQ(b128, b999);
    // Just below the float range the chunked multiplication stays finite: "1e38" is
    // 1e20 * 1e8 * 1e8 * 10 * 10 (about 1e38), "1e39" is one more 10 and overflows.
    const float f38 = String::ToFloat("1e38");
    CHECK(std::isfinite(f38));
    CHECK(std::fabs(f38 / 1e38f - 1.0f) < 1e-5f);
    CHECK(std::isinf(String::ToFloat("1e39")));
    // A negative exponent past the range divides by inf: "1e-39" is 0 here, where strtof
    // would give a denormal. "1e-38" is finite (1 / 1e38).
    CHECK_EQ(String::ToFloat("1e-39"), 0.0f);
    const float m38 = String::ToFloat("1e-38");
    CHECK(m38 > 0.0f && std::fabs(m38 / 1e-38f - 1.0f) < 1e-5f);
    // The mantissa is scaled after the exponent: "0e999" is 0 * inf = NaN (retail's
    // arithmetic, kept), while "0e38" is 0.
    CHECK(std::isnan(String::ToFloat("0e999")));
    CHECK_EQ(String::ToFloat("0e38"), 0.0f);
}

UNIT_TEST(string_format_has_one_buffer_and_truncates_at_0x3fff) {
    // Retail formats into a 0x4000-byte stack buffer and forces the terminator at
    // [0x3fff]: there is no second, larger attempt. An output longer than 0x3fff
    // characters is cut there; one of exactly 0x3fff fits whole.
    const std::string fits(0x3fff, 'y');
    String f = String::Format("%s", fits.c_str());
    CHECK_EQ(f.GetLength(), 0x3fff);
    CHECK(f._data[0x3ffe] == 'y');

    const std::string over(0x5000, 'x');
    String o = String::Format("%s", over.c_str());
    CHECK_EQ(o.GetLength(), 0x3fff);
    CHECK(o._data[0x3ffe] == 'x' && o._data[0x3fff] == '\0');

    // The cut is by output position, not by argument: a format whose tail is a number
    // loses the number's last digits.
    const std::string lead(0x3ffb, 'z');
    String t = String::Format("%s-%d", lead.c_str(), 123456);
    CHECK_EQ(t.GetLength(), 0x3fff);
    CHECK(std::strcmp(t._data + 0x3ffb, "-123") == 0);
    CHECK_EQ(RefCount(t), 1);
}

UNIT_TEST(string_less_on_equal_non_null_strings_is_false_both_ways) {
    String a("same"), b("same");
    CHECK(a == b);
    CHECK(!(a < b));
    CHECK(!(b < a));
    String c = a;                      // the same buffer
    CHECK(!(a < c));
    CHECK(!(c < a));
    String e1(""), e2("");             // empty but NOT null: allocated, equal
    CHECK(e1._data != nullptr && e2._data != nullptr);
    CHECK(!(e1 < e2));
    CHECK(!(e2 < e1));
    CHECK(e1 < String("a"));
    CHECK(!(String("a") < e1));
    // A high byte sorts by SIGNED char (StrCompare), both ways consistent.
    const char hi[] = {static_cast<char>(0xe9), '\0'};
    String h(hi);
    CHECK(h < String("a"));
    CHECK(!(String("a") < h));
    CHECK(!(h < String(hi)));
    CHECK(!(String(hi) < h));
}
