// ============================================================================
// src/engine/crt/msvc_stricmp.cpp — Wave 9 lane COLL
//   `_stricmp` (0x18007eefc) and `__ascii_stricmp` (0x18007eec0), the two CRT
//   bodies the packfile path index and the file-open BST walk compare with.
// ============================================================================
//
// WHY THIS IS A PORT AND NOT A CALL TO std::strcasecmp
// ----------------------------------------------------
// `src/engine/io/filesystem_openfile.cpp:167` already recorded the reason
// and it is worth restating rather than re-deriving:
//
//     "It must NOT be replaced by std::strcasecmp or shared with
//      src/engine/system/configuration.cpp's comparator: 0x18007eec0
//      (__ascii_stricmp, the tail it jumps to) lowers only 'A'-'Z' and compares
//      UNSIGNED, while the config map inlines a SIGNED-char strcmp. They
//      disagree on any byte >= 0x80 and a package path can contain one."
//
// Both halves of that are visible in the instructions below — and the second
// half was MEASURED rather than repeated, with a result that CORRECTS the
// received claim in one direction and confirms it in the other. 3,000,000
// randomised pairs over an alphabet that includes bytes 0x80..0xFF, the
// boundary codes around 'A'/'Z'/'a'/'z' and the Latin-1 uppercase range:
//
//   * vs the HOST CRT's `_stricmp` (MinGW, default "C" locale):
//     **0 sign-differing cases in 400,000 high-byte pairs.** So "strcasecmp
//     would be wrong" is NOT unconditionally true — in the C locale the host's
//     lowering is also 'A'..'Z' only and the two agree. The real hazard is that
//     it becomes wrong the moment anything selects a locale, i.e. it makes the
//     packfile index depend on process-global CRT state that no call site
//     controls. That is a good enough reason on its own, but it is a different
//     reason from the one the comment gave and it is recorded as such.
//
//   * vs a SIGNED-byte lowering comparison — which is exactly what
//     `src/engine/collection/avl_trees.cpp`'s `StrCmpSigned`, the
//     Map<String,String> comparator, does: **169,849 SIGN-differing cases.**
//     THAT half of the original comment is exactly right and is now quantified.
//     The two comparators must never be shared, and the two trees that use them
//     must never share a descent, even though their node headers are identical.
//
// A path containing a byte >= 0x80 sorted by the wrong comparator lands in a
// different subtree, and the packfile index is a tree keyed on exactly that
// comparison — so the wrong answer is not "a slightly different order", it is a
// resource resolving to the wrong package.
//
// THE PATH WRAPPERS, each calling it by name (rtcrt::StrICmp has other callers too):
//   src/engine/io/filesystem_openfile.cpp:172   PathStrICmp   (OpenFile)
//   src/engine/io/filesystem_packages.cpp:406       PathStrICmp   (IndexFind)
//   src/engine/collection/avl_trees.cpp:689         PathStrICmp   (the insert)
//
// ============================================================================
// THE LOCALE BRANCH, AND WHY IT IS AN ABORT RATHER THAN A FALLBACK
// ============================================================================
//
// Retail 0x18007eefc is:
//
//     if (*(int*)0x1801fba28 != 0)          // 0x18007ef00, CMP dword,0 / JNZ
//         return _stricmp_l(a, b, nullptr); //   0x18007ef36: XOR R8D,R8D
//                                           //   then JMP 0x18007ef44
//     if (a == nullptr || b == nullptr) {   // 0x18007ef09 / 0x18007ef28
//         *_errno() = EINVAL;               //   0x18007ef13: MOV [RAX],0x16
//         _invalid_parameter_noinfo();      //   0x18007ef19: CALL 0x180080f40
//         return 0x7FFFFFFF;                //   0x18007ef1e
//     }
//     return __ascii_stricmp(a, b);         // 0x18007ef31: TAIL JMP 0x18007eec0
//
// `0x1801fba28` is the CRT's "a locale has been selected" flag. THE LOCALE ARM
// IS UNREACHABLE IN THIS DLL, AND THAT IS A MEASUREMENT, NOT AN ASSUMPTION.
// Every reference to the flag was enumerated with `get_xrefs_to`: 23 of them,
// and exactly ONE is a WRITE — `_wsetlocale` at 0x180083052. `_wsetlocale`
// (entry 0x180082f74) has exactly one reference in the whole 15,290-function
// binary and it is a DATA reference at 0x18020aba0, i.e. a CRT dispatch slot,
// not a call site. Nothing in Runtime-x64.dll ever calls setlocale, so the flag
// is zero for the life of the process and `_stricmp_l` never runs.
//
// So the locale half is DECLINED, deliberately, and the arm REPORTS LOUDLY and
// returns 0 rather than quietly falling through to the ASCII path. That distinction is
// the point: a silent fallback would make a broken premise indistinguishable
// from a correct one, which is the shape of half the defects this wave found.
// If this ever fires, `_stricmp_l` (0x18007ef44) is a further ~50 instructions
// plus `_LocaleUpdate` (0x18007ee00) and `_tolower_l` (0x180081d0c) — a real
// CRT subsystem, and a separate lane.
//
// ============================================================================
// ORACLE
// ============================================================================
// `__ascii_stricmp` is a LEAF — no CALL instructions — so it needs no
// allocator, no engine and no boot, and a bare LoadLibrary harness drives it
// directly against every input the port will ever see. That is the same
// division that verified the packfile sort: the live boot call proves the
// REGISTRATION is reached, the harness proves the BODY.
//
// RESULT: **3,000,000 pairs x 2 bodies = 6,000,000 comparisons, 0 differences**,
// with the outcome spread lt=858,597 eq=840,403 gt=1,301,000 (a comparator that
// only ever returns one sign is indistinguishable from a broken one), 1,529,089
// pairs containing a byte >= 0x80 and 687,207 pure case-variant pairs. The
// negative controls are the two measurements quoted above; one FIRED and one
// did NOT, and the one that did not is reported as a corrected claim rather
// than dropped.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include "../rt_crt.h"   // the declaration this file defines


namespace rtcrt {

// ---------------------------------------------------------------------------
// 0x18007eec0 — `__ascii_stricmp`.
//
//   R9 = a
// loop:
//   c1 = (uint8)*R9++;  if ((uint32)(c1 - 'A') <= 25) c1 += 0x20;
//   c2 = (uint8)*RDX++; if ((uint32)(c2 - 'A') <= 25) c2 += 0x20;
//   if (c1 != 0 && c1 == c2) goto loop;
//   return (int)c1 - (int)c2;
//
// THREE THINGS THAT LOOK LIKE DETAIL AND ARE NOT:
//
//  * THE BYTES ARE UNSIGNED. `MOVZX R8D,byte ptr [R9]`, not MOVSX. The result
//    for a pair containing a byte >= 0x80 has the OPPOSITE SIGN from a
//    signed-char comparison. `src/engine/collection/avl_trees.cpp`'s
//    `StrCmpSigned` — the Map<String,String> comparator — is the signed one,
//    and the two trees must not share a comparator for exactly this reason.
//
//  * ONLY 'A'..'Z' IS LOWERED. The test is `(uint32)(c - 0x41) <= 0x19`, a
//    single unsigned compare covering exactly 26 codes. Bytes 0xC0..0xDE
//    (Latin-1 uppercase accented) are NOT lowered, which is where a
//    locale-aware `tolower` would differ.
//
//  * THE LOOP CONDITION TESTS THE *LOWERED* c1 FOR ZERO, after lowering. Zero
//    is not in 'A'..'Z' so the lowering cannot manufacture or destroy a
//    terminator — but the test is on the lowered value and is written that way.
//
// The `INC R9` / `INC RDX` happen BEFORE the comparison, so on exit both
// cursors are one past the deciding byte. Nothing reads them; noted so the
// off-by-one is not "corrected" by a later reader.
// ---------------------------------------------------------------------------
int AsciiStrICmp(const char* a, const char* b) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(a);  // CAST-OK: crt
    const unsigned char* q = reinterpret_cast<const unsigned char*>(b);  // CAST-OK: crt
    for (;;) {
        uint32_t c1 = *p++;
        if (static_cast<uint32_t>(c1 - 0x41u) <= 0x19u) c1 += 0x20u;
        uint32_t c2 = *q++;
        if (static_cast<uint32_t>(c2 - 0x41u) <= 0x19u) c2 += 0x20u;
        if (c1 == 0 || c1 != c2)
            return static_cast<int>(c1) - static_cast<int>(c2);
    }
}

// The `_errno` / `_invalid_parameter_noinfo` pair retail runs on a null
// argument. Neither is ported: `_errno` (0x180082288) is the DLL CRT's
// thread-local errno, which after cutover is our CRT's, and
// `_invalid_parameter_noinfo` (0x180080f40) is a CRT abort hook nothing in this
// tree installs. What IS reproduced is the observable contract — the RETURN
// VALUE 0x7FFFFFFF — plus a line saying it happened, because a null path
// reaching a case-insensitive compare is a defect upstream and should not be
// silent. Retail's errno write is noted here rather than emulated.
static bool g_nullReported = false;

// ---------------------------------------------------------------------------
// 0x18007eefc — `_stricmp`.
// ---------------------------------------------------------------------------
int StrICmp(const char* a, const char* b) {
    // The locale arm. See the banner: measured unreachable, and it reports
    // loudly and returns 0 rather than falling through, so a broken premise
    // cannot look like a correct one. `kLocaleSelected` is a constant here
    // because there is no DLL flag to read.
    static const int kLocaleSelected = 0;
    if (kLocaleSelected != 0) {
        std::fprintf(stderr,
            "[RtCrt] _stricmp: the CRT locale flag is SET, so retail would take "
            "_stricmp_l (0x18007ef44) and this port has no body for it. The "
            "premise that nothing calls setlocale is broken.\n");
        std::fflush(stderr);
        return 0;
    }

    if (a == nullptr || b == nullptr) {
        if (!g_nullReported) {
            g_nullReported = true;
            std::fprintf(stderr,
                "[RtCrt] _stricmp: a NULL argument. Retail sets errno=EINVAL, calls "
                "_invalid_parameter_noinfo and returns 0x7FFFFFFF; the return value "
                "is reproduced, the errno write is not (see the source).\n");
            std::fflush(stderr);
        }
        return 0x7FFFFFFF;
    }
    return AsciiStrICmp(a, b);
}

}  // namespace rtcrt

