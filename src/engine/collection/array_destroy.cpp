// ============================================================================
// src/engine/collection/array_destroy.cpp — Wave 9 lane D2
//
// Collection::Array<T>::~Array for NON-trivially-destructible T (lane D's J2
// and J2b), the per-element destroy helpers those bodies call, and
// Collection::Array<T*>::operator= (lane D's J4, the 0x180003970 half).
// ============================================================================
//
// WHAT THIS RETIRES
//
// Lane Q retired 27 addresses for two bodies by proving the 34-strong
// `Collection::Array<T>` cluster was three functions written many times. The
// same proof runs one level deeper here, and comes out better than lane D
// priced it.
//
// Lane D's ranked list says J2 costs "+6 new lane-D rows", because each of its
// six `~Array` bodies opens with a CALL to an unexported per-element destroy
// helper, and J2b needs a seventh. Lane Q then created five more rows for the
// same reason (three of them destroy helpers). Following every one of those to
// its own depth-2 callees gives ELEVEN destroy-helper addresses. Hashing the
// MNEMONIC SEQUENCE of all eleven (byte hashes find nothing — only the
// RIP-relative displacements differ) says they are **THREE BODIES**, and ten of
// the eleven are two of them:
//
//   BODY A  "free the inner Array<T> at element+off, clear its 16-byte header"
//           0x18001ce90   offs {0x100}                stride 0x110
//           0x1800419b0   offs {0x008}                stride 0x018
//           0x180052110   offs {0x030}                stride 0x040
//           0x180074100   offs {0x020}                stride 0x048
//           0x18005bce0   offs {0x038,0x028,0x008}    stride 0x048
//           (and, as the eh_vector form, J2b's three inner Arrays — see below)
//
//   BODY B  "release the System::String* at element+off"
//           0x180011010   offs {0x000}                stride 0x008
//           0x180005930   offs {0x000}                stride 0x010
//           0x180014a40   offs {0x000}                stride 0x048
//           0x180034440   offs {0x010}                stride 0x018
//           0x1800059e0   offs {0x008,0x000}          stride 0x038
//
// The eleventh, 0x18005c550, is a RECURSIVE destroy that calls one of each body
// before freeing; it is written out separately below.
//
// So J2 does not cost six new rows and lane Q's five were three too many. The
// helpers ARE the parameters, and the parameter is a (stride, offset-list)
// pair, exactly as `SetSize`'s parameter turned out to be (stride, align,
// element-assign).
//
// Three of those eleven — 0x180052110, 0x180005930, 0x1800059e0 — are already
// rows in wave9_internals.csv because lane Q reached them by address. They are
// retired here rather than deepened.
//
// CORRECTION TO LANE Q'S ROW TABLE. Lane Q filed 0x180052110 as "destroy-range
// for Audio's 0x40-byte EffectSlot — PORT WITH THE AUDIO HOLDBACKS". It is
// nothing of the sort: it is BODY A with offs {0x30}, stride 0x40, it touches
// no Audio state, calls nothing but the allocator, and is not gated on lane A.
// Its sibling 0x180051650 (the EffectSlot copy-assign) genuinely IS Audio —
// it calls 0x180050d30 — and stays where lane Q put it.
//
// ---------------------------------------------------------------------------
// THE OUTER BODY (J2), read at 0x180010ab0, identical at the other five
// ---------------------------------------------------------------------------
//     MOV RDI,RCX
//     MOV R8D,[RCX+8]        ; count = this->_size
//     MOV RDX,[RCX]          ; base  = this->_data
//     CALL <helper>          ; RCX still = this, and every helper IGNORES it
//     ... then, instruction for instruction, the J1 body lane Q already wrote:
//     MOV RSI,[RDI] / TEST / Enter / Free(_singleton, p) / Leave
//     XOR EAX,EAX / MOV [RDI],RAX / MOV [RDI+8],RAX      ; _size AND _capacity
//
// So J2 == helper + J1. `ArrayDtorImpl` is lane Q's J1 body, reused unchanged;
// there is no second copy of it in this file.
//
// Every helper's first parameter is `ignoredThis`: each one spills RCX to the
// shadow space at entry and never reads it again (verified in all thirteen
// disassemblies).
// It is kept in the signature because retail passes it, so the entry points
// keep retail's arity, not a tidied one.
//
// ---------------------------------------------------------------------------
// J2b (0x18001c8c0) IS BODY A, VIA A CRT TRAMPOLINE
// ---------------------------------------------------------------------------
// Lane D filed 0x18001c8c0 separately because it destroys its elements through
// MSVC's `eh_vector_destructor_iterator` (0x18007ec98) instead of a bespoke
// loop, and priced it as "J1 + CRT iterator + a per-element dtor". Following it
// through says otherwise:
//
//     LEA RCX,[RBX+0x1c0]    ; first inner object
//     LEA R9,[0x18001b960]   ; the per-element dtor
//     MOV EDX,0x10           ; sizeof = 0x10
//     LEA R8D,[RDX-0xd]      ; count  = 3
//     CALL 0x18007ec98
//     ADD RBX,0x1f0          ; element stride
//
//   * 0x18001b960 is NOT A FUNCTION in Ghidra's Runtime program — it is five
//     bytes, `E9 EB 0C 00 00`, an incremental-link thunk to **0x18001c650**.
//   * 0x18001c650, read out of raw memory rather than the function list, is the
//     J1 BODY AGAIN — a twelfth copy of `Array<T>::~Array` with a trivially
//     destructible T, byte-for-byte the shape lane Q proved eleven times.
//   * 0x18007ec98 is 32 instructions and its whole content is
//     `p = base + count*size; while (--count >= 0) { p -= size; dtor(p); }` —
//     i.e. **destroy in REVERSE order**.
//
// Put together: 0x18001c8c0 destroys, per element, three inner `Array<T>` at
// +0x1c0 / +0x1d0 / +0x1e0 in DESCENDING order, stride 0x1f0. That is BODY A
// with `offs = {0x1e0, 0x1d0, 0x1c0}`. No CRT dependency survives, and the
// per-element dtor is not a new row.
//
// (Order is preserved because it reaches the allocator, not because it is
// observable in the object: the three slots do not alias, so a comparator on
// the element buffer cannot see the order. Said plainly here so nobody reads
// a matching buffer as proof that the order is right.)
//
// ---------------------------------------------------------------------------
// BODY B's ASYMMETRY — the one thing in this file a tidy-up would break
// ---------------------------------------------------------------------------
// Read at 0x180034440, confirmed at all five:
//
//     RAX = *(char**)(e+off)
//     if (RAX) {
//         DEC dword [RAX-4]                 ; System::String's refcount
//         CMP dword [RAX-4],0
//         JNZ  ->  MOV [e+off], 0           ; STILL SHARED: slot becomes NULL
//         ADD qword [e+off], -8             ; LAST REF: slot becomes str-8 ...
//         MOV RBP,[e+off]
//         JZ  -> skip                       ; ... flags from the ADD: str==8
//         Enter; Free(_singleton, RBP); Leave
//     }                                     ; and the slot KEEPS str-8
//
// The two arms leave the slot holding DIFFERENT THINGS — NULL when the string
// survives, `str-8` (a dangling, just-freed pointer) when it does not. Retail
// never clears it on the freeing path. That is not a bug we may fix: the object
// is being destroyed and the caller's next act is to free the whole buffer, and
// a helpful `= nullptr` would hide a use-after-free that today is loud.
//
// It is also the ONLY part of either body a comparator can see: getting the
// asymmetry backwards changes the buffer.
//
// The `JZ` after `ADD [e+off],-8` tests the RESULT OF THE ADD, not the refcount
// — the free is skipped when `str == 8`. Unreachable in practice; reproduced
// because "unreachable" is a claim about callers, not about this function.
// `rtcoll::MemFree` (0x1800c7ad0) re-tests null itself, so handing it the
// possibly-null pointer is retail's shape and not a lost guard.
//
// ---------------------------------------------------------------------------
// 0x180011010 IS BODY B, addressed differently
// ---------------------------------------------------------------------------
// Its mnemonic hash differs from the other four (3000b22572 vs 154da96279)
// because MSVC indexed it — `MOV RAX,[R14+RDI*8]` with an INC/CMP counter —
// instead of bumping a pointer by the stride. Every store, every branch and
// every immediate is otherwise the same, and offs{0} / stride 8 makes the two
// addressings compute identical addresses. Filed as Body B, and the difference
// noted here so the next hash sweep does not re-open it.
//
// ---------------------------------------------------------------------------
// THE ALLOCATOR, AND WHAT THIS FILE DOES NOT TAKE
// ---------------------------------------------------------------------------
// Every free here goes through `rtcoll::MemFree` — outside the unit test it is
// header-inline in Collection.h and calls System::MemoryAllocator::MemFree by
// name (retail 0x1800c7ad0), which IS `if (p) { Enter; Free(_singleton, p);
// Leave; }`, instruction for instruction the tail of each of these loops.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "collection/Collection.h"
#include "array_internal.h"
#include "array_destroy_internal.h"
#include "graphics/Animation.h"   // B3R: RtGraphics_Animation_Dtor (0x1800fb290)
// B.4 (R-CORE): the element types the written-out bodies below read by name.
#include "system/String.h"              // RtSystem::StringRefCount
#include "ui/SpriteSheet.h"             // UI::SpriteInfo
#include "ui/LayoutDescription.h"       // UI::LayoutDescription::Entry
#include "graphics/AnimationManager.h"  // AnimationManager::Instance
#include "picking/PickingMesh.h"        // Picking::PickingMesh::SubMesh
#include "video/ShaderProgram.h"        // Video::VertexProgram::Stream

namespace rtcoll {

// `DestroySpec` and the two body signatures are in array_destroy_internal.h,
// shared with the other files that include it, so they reach THESE bodies and
// not a copy of them.

// ---- one String slot, by name --------------------------------------------
// The adopt half of retail's inlined System::String copy (slot zeroed FIRST)
// and Body B's release of ONE slot (the freeing arm leaves `str - 8`, the
// sharing arm null). The written-out element bodies below use these on their
// named String fields; Body B itself still walks its offset table.
static inline void AdoptStringSlot(char*& slot, char* src) {
    slot = nullptr;
    if (src) {
        slot = src;
        ++RtSystem::StringRefCount(src);
    }
}
static inline void ReleaseStringSlot(char*& slot) {
    char* str = slot;
    if (!str) return;
    int32_t& rc = RtSystem::StringRefCount(str);
    --rc;                                         // DEC dword [RAX-4]
    if (rc != 0) {                                // CMP / JNZ — still shared
        slot = nullptr;
        return;
    }
    char* blk = str - 8;                          // ADD qword [slot],-8
    slot = blk;                                   // ... and the slot KEEPS it
    if (blk != nullptr) MemFree(blk);             // JZ on the ADD's result
}

// ---- BODY A ---------------------------------------------------------------
// Free the inner Array<T>'s buffer and clear its 16-byte header, per element.
//
// The header clear is `[e+off] = 0` and `[e+off+8] = 0` — one qword each, the
// second covering _size AND _capacity together, exactly as J1 does. It happens
// UNCONDITIONALLY, including when the buffer was already null.
void FreeInnerArraysCore(uint8_t* base, int count, const DestroySpec* s) {
    if (count <= 0) return;                       // TEST R8D,R8D / JLE
    const int64_t n = static_cast<int64_t>(count);  // MOVSXD
    for (int64_t i = 0; i < n; ++i) {
        uint8_t* e = base + i * s->stride;
        for (int k = 0; k < s->nOffs; ++k) {
            uint8_t* slot = e + s->offs[k];
            void* p = *reinterpret_cast<void**>(slot);  // CAST-OK: elem_stride
            if (p) MemFree(p);                    // Enter; Free(_singleton,p); Leave
            *reinterpret_cast<uint64_t*>(slot + 0) = 0;  // CAST-OK: elem_stride
            *reinterpret_cast<uint64_t*>(slot + 8) = 0;  // CAST-OK: elem_stride
        }
    }
}

// ---- BODY B ---------------------------------------------------------------
// Release one System::String reference per (element, offset). See the banner
// for why the two arms leave the slot holding different things.
void ReleaseStringsCore(uint8_t* base, int count, const DestroySpec* s) {
    if (count <= 0) return;
    const int64_t n = static_cast<int64_t>(count);
    for (int64_t i = 0; i < n; ++i) {
        uint8_t* e = base + i * s->stride;
        for (int k = 0; k < s->nOffs; ++k) {
            char** slot = reinterpret_cast<char**>(e + s->offs[k]);  // CAST-OK: elem_stride
            char* str = *slot;
            if (!str) continue;
            int32_t& rc = RtSystem::StringRefCount(str);
            --rc;                                 // DEC dword [RAX-4]
            if (rc != 0) {                        // CMP / JNZ — still shared
                *slot = nullptr;
                continue;
            }
            char* blk = str - 8;                  // ADD qword [slot],-8
            *slot = blk;                          // ... and the slot KEEPS it
            if (blk != nullptr) MemFree(blk);     // JZ on the ADD's result
        }
    }
}

// ============================================================================
// The specs, one row per helper address.
//
// Every row was read out of that address's own disassembly: `stride` is the
// `ADD RBX,imm` (or the `*8` scale for 0x180011010) at the bottom of the loop,
// and each offset is the `LEA RBX,[RDX+imm]` seed plus the loop's displacement.
// ============================================================================

// ---- BODY A rows ----
constexpr DestroySpec kD_InnerArr_110 = { "inner Array @+0x100, stride 0x110", 0x110, 1, { 0x100 } };
constexpr DestroySpec kD_InnerArr_18  = { "inner Array @+0x008, stride 0x018", 0x018, 1, { 0x008 } };
constexpr DestroySpec kD_InnerArr_40  = { "inner Array @+0x030, stride 0x040", 0x040, 1, { 0x030 } };
constexpr DestroySpec kD_InnerArr_48  = { "inner Array @+0x020, stride 0x048", 0x048, 1, { 0x020 } };
constexpr DestroySpec kD_InnerArr3_48 = { "3 inner Arrays, stride 0x048",      0x048, 3, { 0x038, 0x028, 0x008 } };
// J2b's element: three inner Arrays destroyed in DESCENDING order by
// eh_vector_destructor_iterator. Not a registered helper of its own — it has no
// address, it is the loop 0x18001c8c0 inlines.
constexpr DestroySpec kD_InnerArr3_1f0 = { "3 inner Arrays (eh_vector, reverse), stride 0x1f0",
                                       0x1f0, 3, { 0x1e0, 0x1d0, 0x1c0 } };

// ---- BODY B rows ----
constexpr DestroySpec kD_Str_08 = { "String @+0x000, stride 0x008", 0x008, 1, { 0x000 } };
constexpr DestroySpec kD_Str_10 = { "String @+0x000, stride 0x010", 0x010, 1, { 0x000 } };
constexpr DestroySpec kD_Str_48 = { "String @+0x000, stride 0x048", 0x048, 1, { 0x000 } };
constexpr DestroySpec kD_Str_18 = { "String @+0x010, stride 0x018", 0x018, 1, { 0x010 } };
constexpr DestroySpec kD_Str_20 = { "String @+0x000, stride 0x020", 0x020, 1, { 0x000 } };  // VertexLayout::Element (2026-09-12)
constexpr DestroySpec kD_Str2_38 = { "2 Strings @+0x008,+0x000, stride 0x038", 0x038, 2, { 0x008, 0x000 } };
// VTABLE BATCH 2 (2026-09-12) — UI::LayoutDescription's entry, stride 0x38
// with THREE refcounted Strings. NOT kD_Str2_38 above, which shares the stride
// and has two Strings at DIFFERENT offsets. The three offsets and their ORDER
// are read from the per-element destructor 0x180044050's own body, which
// releases +0x18 first, then +0x10, then +0x08 — descending, like every other
// multi-String row here.
constexpr DestroySpec kD_Str3_38at08 = { "3 Strings @+0x018,+0x010,+0x008, stride 0x038",
                                         0x038, 3, { 0x018, 0x010, 0x008 } };
// WAVE 27 LANE FRONTIER - UI::StringTable's TextEntry {name, text}, the element
// of the array at StringTable+0x20. Read out of 0x180005570's own body:
//   0x1800055a7  MOV RAX,[RBX+0x8]   the FIRST slot released is +0x08
//   0x180005600  MOV RAX,[RBX]       the SECOND is +0x00
//   0x180005655  ADD RBX,0x10        stride
// so the order is DESCENDING, like kD_Str2_38, and the assert below enforces it.
constexpr DestroySpec kD_Str2_10 = { "2 Strings @+0x008,+0x000, stride 0x010", 0x010, 2, { 0x008, 0x000 } };

// ---- Cutover closure batch 2 (2026-09-12): Graphics::Material's parameter
// array and the shader-program compiled-blob array. Both read out of their own
// callee bodies (0x180005880 / 0x18004ff80), not inherited from a sibling row
// -- see the notes above kD_Str_18 for why an offset must never be assumed
// from a stride match alone.
//   0x180005880 (Collection_Array_MaterialParam_DestroyStrings): `*param_2`
//       (offset +0x00) is the released String*, `param_2 += 3` -> stride 0x18.
//       NOT kD_Str_18, which is String at +0x10.
constexpr DestroySpec kD_Str_18at00 = { "String @+0x000, stride 0x018", 0x018, 1, { 0x000 } };
//   0x18004ff80 (ShaderProgram::_byteCode inner destroy) was ported the same
//       day by the ShaderProgram batch as kD_InnerArr_10 below; one row only.

// ---- WAVE 25 LANE INTERNAL42: two more Body-B rows -------------------------
// Both read out of their own helper's disassembly. The helpers walk a POINTER
// rather than indexing (`LEA RBX,[RDX+off]` ... `ADD RBX,stride`), which is the
// same body as 0x180011010's indexed form with a non-zero offset; the loop
// shape differs, the rule does not.
//   0x18002afc0  LEA RBX,[RDX+0x08] ... ADD RBX,0x10   -> stride 0x10, off 0x08
//   0x180039640  LEA RBX,[RDX+0x08] ... ADD RBX,0x10   -> the SAME row
//   0x180039580  LEA RBX,[RDX+0x18] ... ADD RBX,0x20   -> stride 0x20, off 0x18
// kD_Str_10 is stride 0x10 at offset 0x00 and is NOT these: the suffix in these
// names is the STRIDE, so a row at the same stride and a different offset needs
// its own name or the next reader reuses the wrong one.
constexpr DestroySpec kD_Str_10at08 = { "String @+0x008, stride 0x010", 0x010, 1, { 0x008 } };
constexpr DestroySpec kD_Str_20at18 = { "String @+0x018, stride 0x020", 0x020, 1, { 0x018 } };

// ---- Cutover closure batch 2b (2026-09-12): two more rows ------------------
// Both read out of their own helper's disassembly, not inherited from a stride
// match (see the kD_Str_18 note above for why that distinction has bitten).
//   0x180005680 (Collection_Array_Achievement_DestroyInnerArrays): TWO inner
//       `Collection::Array<T>` per element. The walk seeds `RBX = base+0x10`
//       and advances by four qwords, so stride 0x20; it frees +0x10 FIRST and
//       then +0x00 (`puVar2[-2]`), and clears both 16-byte headers. Descending
//       offsets, like every multi-offset row in this file.
constexpr DestroySpec kD_InnerArr2_20 = { "2 inner Arrays @+0x010,+0x000, stride 0x020",
                                          0x020, 2, { 0x010, 0x000 } };
//   0x1800341e0 (Save_MetaDataEntry_DestroyStrings): TWO System::String per
//       element at +0x08 then +0x00, stride 0x20 (`param_2 += 4` qwords). The
//       element is Save's {String*, String*, int, int, int} — array.cpp's
//       kSpec_MetaEntry20.
constexpr DestroySpec kD_Str2_20 = { "2 Strings @+0x008,+0x000, stride 0x020",
                                     0x020, 2, { 0x008, 0x000 } };

// UI leftovers batch (docs/history/runtime_port/closure/WORKLIST.md) — 0x18003f070,
// the shrink/realloc destroy for `UI::SpriteCapture::_entries`
// (Array<CaptureEntry>, src/engine/ui/SpriteCapture.h). One String release
// at +0x08 (the `name` slot), stride 0x28 — read out of the loop's own
// `plVar3 = plVar3 + 5` (5 qwords = 0x28) with the release at `plVar3[0]`
// where `plVar3` starts at `param_2 + 8`.
constexpr DestroySpec kD_Str_28at08 = { "String @+0x008, stride 0x028", 0x028, 1, { 0x008 } };

// ---- 2026-09-12: the Video::ShaderProgram byte-code array ------------------
// A Body-A row at stride 0x10 — the first one this file has had. The element is
// a whole Collection::Array<uint8> (data +0x00, size +0x08, capacity +0x0c), so
// the inner header IS the element and the offset is 0. Read out of 0x18004ff80:
//     18004ffb2  MOV RSI,[RBX]         the buffer at +0x00
//     18004fff0  MOV [RBX],R14(=0)     both header qwords zeroed after the free
//     18004fff3  MOV [RBX+0x8],R14
//     18004fff7  ADD RBX,0x10          stride
// NOT kD_Str_10, which has the same two numbers and the OPPOSITE rule: that row
// is Body B (release a refcounted String) and this one is Body A (free a buffer
// outright). The stride/offset pair does not identify the body — the CALL at
// the top of the ~Array that reaches it does.
constexpr DestroySpec kD_InnerArr_10 = { "inner Array @+0x000, stride 0x010", 0x010, 1, { 0x000 } };

// ---------------------------------------------------------------------------
// Compile-time checks on the transcription.
//
// This project static_asserts every struct offset; the equivalent here is that
// every (offset, stride) pair a slot is read out of has to FIT. Body A writes
// sixteen bytes at each offset and Body B writes eight, so an offset that
// overruns its own stride is a wild write into the NEXT element -- the failure
// family of ~Internal's swap-remove writing through a stale handle-table entry
// and of a String destructor on a non-String corrupting the allocator's boundary
// tag, and neither had a proximate symptom.
//
// These caught nothing when they were written. That is the point: they are free
// and they run on every build.
// ---------------------------------------------------------------------------
#define RTCOLL_FITS(SPEC, N, W)                                               \
    static_assert((SPEC).nOffs == (N), #SPEC " nOffs");                       \
    static_assert((SPEC).offs[0] + (W) <= (SPEC).stride, #SPEC " offs[0]")

RTCOLL_FITS(kD_InnerArr_110, 1, 16u);
RTCOLL_FITS(kD_InnerArr_18,  1, 16u);
RTCOLL_FITS(kD_InnerArr_40,  1, 16u);
RTCOLL_FITS(kD_InnerArr_48,  1, 16u);
RTCOLL_FITS(kD_InnerArr_10,  1, 16u);   // 2026-09-12, ShaderProgram::_byteCode
RTCOLL_FITS(kD_InnerArr3_48, 3, 16u);
static_assert(kD_InnerArr3_48.offs[1] + 16u <= kD_InnerArr3_48.stride, "offs[1]");
static_assert(kD_InnerArr3_48.offs[2] + 16u <= kD_InnerArr3_48.stride, "offs[2]");
RTCOLL_FITS(kD_InnerArr3_1f0, 3, 16u);
static_assert(kD_InnerArr3_1f0.offs[1] + 16u <= kD_InnerArr3_1f0.stride, "offs[1]");
static_assert(kD_InnerArr3_1f0.offs[2] + 16u <= kD_InnerArr3_1f0.stride, "offs[2]");

RTCOLL_FITS(kD_Str_08, 1, 8u);
RTCOLL_FITS(kD_Str_10, 1, 8u);
RTCOLL_FITS(kD_Str_48, 1, 8u);
RTCOLL_FITS(kD_Str_18, 1, 8u);
RTCOLL_FITS(kD_Str2_38, 2, 8u);
RTCOLL_FITS(kD_Str3_38at08, 3, 8u);
static_assert(kD_Str3_38at08.offs[1] + 8u <= kD_Str3_38at08.stride, "offs[1]");
static_assert(kD_Str3_38at08.offs[2] + 8u <= kD_Str3_38at08.stride, "offs[2]");
RTCOLL_FITS(kD_Str2_10, 2, 8u);
static_assert(kD_Str2_38.offs[1] + 8u <= kD_Str2_38.stride, "offs[1]");
RTCOLL_FITS(kD_Str_28at08, 1, 8u);

// Four DESCENDING rows, asserted as descending. "Sorted them while tidying
// up" is a silent change to the order the ALLOCATOR sees, and no comparator on
// the element buffer can see it (the slots do not alias) -- so it has to be
// checked here or not at all.
static_assert(kD_InnerArr3_1f0.offs[0] > kD_InnerArr3_1f0.offs[1] &&
              kD_InnerArr3_1f0.offs[1] > kD_InnerArr3_1f0.offs[2],
              "eh_vector_destructor_iterator destroys in REVERSE order");
static_assert(kD_InnerArr3_48.offs[0] > kD_InnerArr3_48.offs[1] &&
              kD_InnerArr3_48.offs[1] > kD_InnerArr3_48.offs[2],
              "0x18005bce0 walks 0x38, 0x28, 0x08 in that order");
static_assert(kD_Str2_10.offs[0] > kD_Str2_10.offs[1],
              "0x180005570 releases +0x08 BEFORE +0x00 (0x1800055a7 then 0x180005600)");
static_assert(kD_Str2_38.offs[0] > kD_Str2_38.offs[1],
              "0x1800059e0 releases +0x08 BEFORE +0x00");

#undef RTCOLL_FITS

// Counting wrappers. These count the non-null slots in a pass BEFORE the body
// runs rather than re-walking the buffer afterwards, because after Body A the
// slots are zeroed and after Body B they are the answer — a pass afterwards could
// not tell what the body did.
static int FreeInnerArraysCounted(uint8_t* base, int count, const DestroySpec* s) {
    int freed = 0;
    if (count > 0) {
        const int64_t n = static_cast<int64_t>(count);
        for (int64_t i = 0; i < n; ++i)
            for (int k = 0; k < s->nOffs; ++k)
                if (*reinterpret_cast<void**>(base + i * s->stride + s->offs[k])) ++freed;  // CAST-OK: elem_stride
    }
    FreeInnerArraysCore(base, count, s);
    return freed;
}

static int ReleaseStringsCounted(uint8_t* base, int count, const DestroySpec* s) {
    int nonNull = 0;
    if (count > 0) {
        const int64_t n = static_cast<int64_t>(count);
        for (int64_t i = 0; i < n; ++i)
            for (int k = 0; k < s->nOffs; ++k)
                if (*reinterpret_cast<void**>(base + i * s->stride + s->offs[k])) ++nonNull;  // CAST-OK: elem_stride
    }
    ReleaseStringsCore(base, count, s);
    return nonNull;
}

}  // namespace rtcoll

// ============================================================================
// The entry points
// ============================================================================
namespace rtcoll {

// ---- the thirteen helper addresses ----------------------------------------
// Signature is retail's: (ignoredThis, base, count). `ignoredThis` is spilled
// to the shadow space and never read by any of them; it stays in the signature
// so the entry points keep retail's arity, not a cleaned-up one.

#define A_ENTRY(ADDR, SPEC)                                                   \
    void RtColl_DestroyInner_##ADDR(void* ignoredThis, void* base, int count) { \
        (void)ignoredThis;                                                    \
        const int freed = FreeInnerArraysCounted(                             \
            static_cast<uint8_t*>(base), count, &SPEC);  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased element walk steps the caller's element storage (void* base) as bytes */ \
    }

// NOT EVERY HELPER ADDRESS GETS AN A_ENTRY / B_ENTRY POINT HERE.
//
// The ones without one, with the spec that already covers each, so a new entry
// point is one A_ENTRY/B_ENTRY line away and no re-derivation:
//     0x18001ce90 kD_InnerArr_110   0x180074100 kD_InnerArr_48
//     0x18005bce0 kD_InnerArr3_48   0x180014a40 kD_Str_48
//     0x180034440 kD_Str_18         0x18005c550 DestroyNestedPair
//
// *** WAVE 9 LANE UIW, 2026-09-08 — TWO OF THE EIGHT NOW HAVE A CALLER, and
// the note above says exactly what to do about it: "Register it the day a
// caller appears." Taken up literally, one line each, no re-derivation. ***
//
//   0x1800419b0 (kD_InnerArr_18) — the shrink/realloc destroy arm of
//       `Array<GraphSeries>::SetSize` (0x1800417d0), ported this commit in
//       array.cpp as `kSpec_GraphSeries` to close
//       `?AddGraph@GraphElement@UI@@`.
//   0x180011010 (kD_Str_08) — the same arm of `Array<System::String>::SetSize`
//       (0x180034860), ported this commit as `kSpec_StringRef` to close
//       `?AddItem@ComboElement@UI@@`. It will get a SECOND caller the day
//       0x180029a80 (the two-argument sibling, `TextElement::
//       SetParameterCount`'s) lands.
//
// This is the good outcome of a mistake worth recording. Lane UIW's first draft
// wrote its OWN body for both of these and registered 0x180010ab0 as well —
// which this file already registers as a J2 entry — producing a shadowed
// address and two live copies of one function. It surfaced only because a diff
// run printed a site name (`…~Array(+elem) @0x180010ab0`) that lane had not
// written. **array.cpp and array_destroy.cpp are two files and one namespace;
// grep the address across both before porting a Collection helper.**
A_ENTRY(180052110, kD_InnerArr_40)    // Audio::EffectSlot array (NOT Audio-gated — see banner)
A_ENTRY(1800419b0, kD_InnerArr_18)    // UI: GraphElement::_graphs, the nested point Array
// Video::ShaderProgram::_byteCode. Its caller is array.cpp's kSpec_InnerArrayU8
// (0x180053f00's SetSize), on both its destroy arms.
A_ENTRY(18004ff80, kD_InnerArr_10)    // Video: ShaderProgram::_byteCode inner Array<uint8>
// Cutover closure batch 2b: 0x180005680, Achievement's two-inner-Array element
// destroy, has no entry point of its own: 0x180004b70's J2_ENTRY below runs its
// row, kD_InnerArr2_20.

#undef A_ENTRY

#define B_ENTRY(ADDR, SPEC)                                                   \
    void RtColl_DestroyStrings_##ADDR(void* ignoredThis, void* base, int count) { \
        (void)ignoredThis;                                                    \
        const int n = ReleaseStringsCounted(                                  \
            static_cast<uint8_t*>(base), count, &SPEC);  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased element walk steps the caller's element storage (void* base) as bytes */ \
    }

B_ENTRY(180005930, kD_Str_10)         // {String*, ExternalPtr} — lane Q's kSpec_StringExt
B_ENTRY(1800059e0, kD_Str2_38)        // SpriteSheet's 0x38 entry — lane Q's kSpec_Sprite38
B_ENTRY(180011010, kD_Str_08)         // UI: ComboElement::_strings — lane UIW's kSpec_StringRef
B_ENTRY(180026b50, kD_Str_10)         // IO::PackageFile::Entry — lane Q's kSpec_PkgEntry
B_ENTRY(18004fe20, kD_Str_20)         // Video::VertexLayout::Element — array.cpp kSpec_VertexElement (2026-09-12)
// WAVE 26 LANE INT33. Its caller is array.cpp's DestroyExtStringAt08, the
// destroy of kSpec_ExtStr10at08 (0x180075d20's SetSize), which calls it by name
// as retail's 0x180075d20 reaches it by rel32.
// Offset re-read here rather than inherited: `LEA RBX,[RDX+0x8]` at
// 0x18003966c, `ADD RBX,0x10` -- String at +0x08, stride 0x10 == kD_Str_10at08.
B_ENTRY(180039640, kD_Str_10at08)     // UI: DialogNamedTemplate {ptr, String*}
// ...and the sixth, on the same terms. `LEA RBX,[RDX+0x18]` at 0x1800395ac and
// `ADD RBX,0x20` at 0x18003961e -- String at +0x18, stride 0x20 ==
// kD_Str_20at18. Its caller is array.cpp's kSpec_CfgEntry20 (0x180075b20).
// NOTE these two lines are the pair Wave 25 nearly got wrong: 0x180039640 and
// 0x180039580 are called from bodies that share an opcode hash, and the hash
// erases the CALL operand that tells them apart. Each offset above is read from
// the HELPER's own disassembly, not from its caller's hash.
B_ENTRY(180039580, kD_Str_20at18)     // UI: DialogControllerConfigEntry

// ---- WAVE 27 LANE FRONTIER: the SIXTH Body-B address, 0x180006ac0 ----------
//
// `Object::ExternalArrayElement`'s range destroy -- the element-destroy arm of
// `Collection::Array<ExternalArrayElement>::SetSize` (0x18002c6a0), which is
// the wall cutover attempt 29 died on (EXEC-of-0 at ArraySetSize+0x4d).
//
// *** IT IS BODY B AND THAT IS MEASURED, NOT RESEMBLED. *** 0x180006ac0 was
// disassembled in full and set beside 0x180005930, the row directly above.
// They are identical instruction for instruction and in every resolved
// absolute operand (0x1801fc718, 0x18014b390, 0x18014b388, 0x1800c6c00) --
// the two differences are EXACTLY the pair array.cpp's banner already
// tabulates between 0x180026b50 and 0x180005930 and that no caller can see:
//     0x180006af2  NOP dword ptr [RAX + RAX*0x1]   alignment padding
//     0x180006b05  MOV qword ptr [RSP+0x50],RBX    a DUPLICATED store into
//                                                  this body's own SEH scope
//                                                  slot (0x180006b00 makes it
//                                                  first, with the same value)
// The spec is read out of THIS body: `ADD RBX,0x10` at 0x180006b5f is the
// stride, and the slot is `MOV RAX,qword ptr [RBX]` at 0x180006b0a -- offset
// 0x00. That is kD_Str_10, no new row. Third instance of one body; the first
// two are the two lines above.
B_ENTRY(180006ac0, kD_Str_10)         // Object: ExternalArrayElement {Filename, 2x int32}
// UI leftovers batch (docs/history/runtime_port/closure/WORKLIST.md) — the SHRINK/
// REALLOC destroy behind `UI::SpriteCapture::_entries`'s two SetSize
// instantiations (array.cpp's kSpec_CaptureEntry / kSpec_CaptureEntryZ,
// 0x18004bde0 / 0x18004ba90). One String release at +0x08, stride 0x28 — see
// kD_Str_28at08 above for the read.
B_ENTRY(18003f070, kD_Str_28at08)     // UI: SpriteCapture::_entries (CaptureEntry, name @+0x08)

// ---- WAVE 27 LANE FRONTIER: 0x180005570, the FIRST TWO-SLOT Body-B at 0x10 --
//
// `UI::StringTable`'s TextEntry range destroy. Reached from TWO places, and
// both are ported in this same commit:
//   * 0x180004a90, `~Array<TextEntry>` (the J2 entry below), which is what
//     `??1StringTable@UI@@` calls on `this+0x20`;
//   * 0x180078c90, `Array<TextEntry>::SetSize`, on BOTH of its destroy arms -
//     array.cpp's kSpec_StringPair10.
//
// It is Body B with a TWO-OFFSET spec, which no previous Body-B row at stride
// 0x10 has been: kD_Str_10 (0x180005930 / 0x180026b50 / 0x180006ac0) releases
// ONE slot at +0x00 and this one releases TWO. Sharing kD_Str_10 by stride
// would have left every TextEntry's `text` String leaked, silently, with a
// clean build - the fifth time this family has offered a wrong answer by
// resemblance. The spec is read out of THIS body (see kD_Str2_10 above).
B_ENTRY(180005570, kD_Str2_10)        // UI: StringTable::TextEntry {name, text}
// 2026-09-12 — Video::ShaderProgram::Parameter {String name, int dest, bool}.
// Its caller is array.cpp's kSpec_ShaderParam (0x180055db0's SetSize). The
// row is kD_Str_10 — stride 0x10, one String at +0x00
// — read out of 0x18004fed0's own `MOV RAX,[RBX]` / `ADD RBX,0x10`, not
// inherited from the three other addresses that share that spec.
B_ENTRY(18004fed0, kD_Str_10)         // Video: ShaderProgram::Parameter

// ---------------------------------------------------------------------------
// 0x180026b50 — WAVE 9 LANE DESTROY, and why it shares kD_Str_10 WITHOUT this
// being the fifth near-miss-by-resemblance this family has produced
// ---------------------------------------------------------------------------
// This address is the CUTOVER WALL of attempt 17: `ReadPackfile` -> the
// `Array<IO::PackageFile::Entry>` SetSize at 0x1800298f0 -> `SetSizeCore` ->
// `DestroyPkgEntryRange`, which forwarded here BY ADDRESS. Unexported and
// unregistered, so with the DLL absent the call executed address 0.
//
// array.cpp's row for kSpec_PkgEntry said this was "a different function from
// DestroyStringExt's 0x180005930", and that sentence was doing two jobs at
// once. It is true of the ADDRESS and it is false of the BODY, and only the
// first was ever checked. Both listings, read out of Runtime-x64.dll for this
// commit rather than inferred from each other:
//
//   0x180026b50                              0x180005930
//   TEST R8D,R8D / JLE ret                   TEST R8D,R8D / JLE ret
//   MOV [RSP+8],RCX      ; spilled, never    MOV [RSP+8],RCX      ; ... read
//   PUSH RSI/RDI/R14 ; SUB RSP,0x30          PUSH RSI/RDI/R14 ; SUB RSP,0x30
//   MOV [RSP+0x20],-2   ; SEH state          MOV [RSP+0x20],-2
//   MOV RBX,RDX ; MOVSXD RSI,R8D             MOV RBX,RDX ; MOVSXD RSI,R8D
//   XOR R14D,R14D                            XOR R14D,R14D
//   NOP dword [RAX+RAX]  ; 14-byte align     (no padding)
//   MOV [RSP+0x50],RBX   ; *** TWICE ***     MOV [RSP+0x50],RBX   ; once
//   MOV [RSP+0x50],RBX
//   MOV RAX,[RBX] / TEST / JZ next           MOV RAX,[RBX] / TEST / JZ next
//   DEC [RAX-4] / CMP [RAX-4],0 / JNZ        (identical)
//   ADD qword [RBX],-8 / MOV RBP,[RBX] / JZ  (identical)
//   Enter / Free(_singleton,RBP) / Leave     (identical)
//   MOV [RBX],R14        ; the shared arm    (identical)
//   ADD RBX,0x10 / DEC RSI / JNZ loop        ADD RBX,0x10 / DEC RSI / JNZ loop
//
// The ONLY two differences are an alignment `NOP` and a DUPLICATED store of RBX
// into the SEH scope slot at [RSP+0x50] — MSVC emitting the frame-variable
// update once per inlined scope. Neither is observable: [RSP+0x50] is this
// function's own frame, written and never read. Every store to the SUBJECT,
// every branch, every immediate, the stride (`ADD RBX,0x10`) and the single
// offset (bare `[RBX]`, i.e. +0x00) agree.
//
// So this IS Body B with kD_Str_10, and the reason that is allowed to be said
// here is that the sentence is a conclusion from the listing above and not from
// the fact that the two are alike. The four near-misses this family has already
// produced (an alignment immediate, a missing SHL, a TRAILING CALL to
// Filename::AdjustDelimiters, and a 0x32/0x33 that meant 51 vs 52) all hid in
// exactly the columns tabulated above, which is why they are tabulated rather
// than summarised.
//
// The trailing-call trap in particular does NOT recur here: this listing's only
// CALLs are the allocator's Enter / Free / Leave, the same three 0x180005930
// makes, and there is nothing after the loop but the epilogue.
//
// NOT a new spec row, and that is the load-bearing negative: `kD_Str_10` is
// (stride 0x10, offs {0x00}) and this body computes those two numbers itself.
// The ELEMENT is IO::PackageFile::Entry — System::Filename at +0x00, int32
// offset at +0x08, int32 size at +0x0c — so the two dwords in the tail are
// exactly the eight bytes Body B does not touch.

// Cutover closure batch 2: Graphics::Material's parameter array (String* name
// at +0x00, stride 0x18, ~MaterialInstance's call). Its caller is array.cpp's
// kSpec_MaterialParam; 0x180004c80's J2_ENTRY below runs the same row.
B_ENTRY(180005880, kD_Str_18at00)      // Graphics: Material parameter array
// ---- BATCH 5 (2026-09-12): the two helpers the IO save-list / mod-reload ----
//      arrays destroy through. Both are element-destroy helpers for SetSize
//      instantiations ported in array.cpp in the same commit
//      (kSpec_SaveGameInfo @0x18002ae20 / 0x18002aae0, and
//       kSpec_ModExternal18 @0x180028cd0), so each gets the caller this file's
//      banner says to wait for.
//
// THE SPEC IS READ OUT OF EACH HELPER'S OWN DISASSEMBLY, never from its
// caller's element type and never from a hash — the Wave 25 lesson three
// blocks up (0x18002ac40 / 0x180039360 / 0x180039280 share one opcode hash and
// take two different specs, because the hash erases the one CALL operand that
// decides the answer):
//
//   0x18002afc0   LEA RBX,[RDX+0x8]  @0x18002afec   -> String at +0x08
//                 ADD RBX,0x10       @0x18002b05e   -> stride 0x10
//                 == kD_Str_10at08, the row 0x180039640 already uses. NOT
//                 kD_Str_10 (offset 0x00) and NOT kD_Str2_10 (two slots).
//   0x180029190   LEA RBX,[RDX+0x10] @0x1800291bc   -> String at +0x10
//                 ADD RBX,0x18       @0x180029233   -> stride 0x18
//                 == kD_Str_18, the row 0x180034440 already uses.
//
// 0x18002afc0 is also the helper `J2_ENTRY(18002ac40, …)` below names as the
// one that entry point CALLs (a rel32 inside retail's body). This B_ENTRY is
// what array.cpp's kSpec_SaveGameInfo destroy (DestroySaveGameInfoRange) calls.
B_ENTRY(18002afc0, kD_Str_10at08)     // IO: Persistence::SaveGameInfo {DateTime, String*@+8}
B_ENTRY(180029190, kD_Str_18)         // IO: SetupFileSystem's {typeId, External*, Filename@+0x10}

// Cutover closure batch 2b: Save's metadata entry {String*, String*, 3 ints}.
// Its caller is array.cpp's kSpec_MetaEntry20 (0x180034010 and 0x180033080);
// this file's 0x180033f30 operator= runs the same row through ArrayClearMetaEntry20.
B_ENTRY(1800341e0, kD_Str2_20)        // Save: Persistence metadata entry

#undef B_ENTRY

// ---- J2: the six ~Array bodies with an element-destroy prologue ------------
//
// `ArrayDtorImpl` is lane Q's J1 body, called rather than restated: J2 IS
// helper-then-J1, and a second copy of J1 here would be a place for the two to
// drift apart.
//
// The helper's body is run directly with the same DestroySpec its A_ENTRY /
// B_ENTRY line uses: these entry points know statically which helper retail
// calls.

#define J2_ENTRY(ADDR, CORE, SPEC)                                            \
    void* RtColl_ArrayDtorElem_##ADDR(void* self) {                           \
        ArrayHdr* a = static_cast<ArrayHdr*>(self);  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its receiver (void* self, a caller's Array<T>) as the ArrayHdr core; M2 bars a self operand */ \
        /* retail: R8D=[RCX+8], RDX=[RCX], RCX unchanged, then CALL helper */  \
        const int freed = CORE(a->_data, a->_size, &SPEC);                    \
        void* r = ArrayDtorImpl(a);                                           \
        return r;                                                             \
    }

// The helper each entry point calls is the one its own disassembly CALLs, and
// the pairing is not guessable from the address: 0x180033880 sits between two
// Body-A neighbours and calls a Body-B helper. Each line names the address it
// was read from.
J2_ENTRY(180010ab0, ReleaseStringsCounted,  kD_Str_08)       // CALL 0x180011010  Body B
J2_ENTRY(180033880, ReleaseStringsCounted,  kD_Str_18)       // CALL 0x180034440  Body B
J2_ENTRY(1800416d0, FreeInnerArraysCounted, kD_InnerArr_18)  // CALL 0x1800419b0  Body A
J2_ENTRY(18004fa30, ReleaseStringsCounted,  kD_Str_20)       // CALL 0x18004fe20  Body B  ~VertexLayout (2026-09-12)

// ---- VTABLE BATCH 2 (2026-09-12): UI::LayoutDescription's entry array -------
//
// This instantiation has NO range helper in retail. Its per-element destructor
// 0x180044050 takes ONE element, and both callers (the ~Array at 0x180043a00
// and the shrink arm of SetSize at 0x180043d80) run their own `for` loop around
// it. So the entry point below is the SINGLE-element form, which is why it does
// not use A_ENTRY/B_ENTRY: those are (this, base, count) because retail's
// helpers are.
void RtColl_DestroyElem_180044050(uint8_t* elem) {
    const int n = ReleaseStringsCounted(elem, 1, &kD_Str3_38at08);
}
J2_ENTRY(180043a00, ReleaseStringsCounted,  kD_Str3_38at08)  // UI: ~LayoutDescription's entry array

// ---- WAVE 25 LANE INTERNAL42: three more J2 addresses ----------------------
//
// *** THE TRAP THESE THREE CARRY, AND IT NEARLY COST TWO WRONG SPECS. ***
// Wave 23 established by address-normalised opcode hash that
//     0x18002ac40 == 0x180039280 == 0x180039360
// are ONE body, 127 bytes, hash 089ee8. That is true, and it does NOT mean they
// take the same DestroySpec. An address-normalised hash erases the rel32 field
// of the `CALL helper` at the top — which is the ONLY thing that distinguishes
// a J2 entry from its siblings. Read by filtering each body's own CALLs:
//     0x18002ac40 -> CALL 0x18002afc0   stride 0x10, String at +0x08
//     0x180039360 -> CALL 0x180039640   stride 0x10, String at +0x08  (same row)
//     0x180039280 -> CALL 0x180039580   stride 0x20, String at +0x18  (DIFFERENT)
// So "byte-identical" here means "identical except for the one operand that
// decides the answer". Taking the hash as authority would have run a
// stride-0x20 array at stride 0x10 and walked off the end of the buffer.
J2_ENTRY(18002ac40, ReleaseStringsCounted,  kD_Str_10at08)   // CALL 0x18002afc0  Body B
J2_ENTRY(180039360, ReleaseStringsCounted,  kD_Str_10at08)   // CALL 0x180039640  Body B
J2_ENTRY(180039280, ReleaseStringsCounted,  kD_Str_20at18)   // CALL 0x180039580  Body B

// ---- WAVE 26 LANE INT33: the tenth J2 address -----------------------------
//
// 0x1800269a0 — `~Array<IO::PackageFile::Entry>`, `~PackageFile`'s member at
// +0xB0 (src/engine/io/package_file_dtor.cpp:31 named it and recorded it as
// "NOT PORTED <- the only one" of that dtor's seven callees).
//
// MEASURED, not inherited (9/9 controls):
//
//   0x1800269a0 vs 0x18002ac40   extent 127/127   raw byte diff 13
//     the ONLY normalised difference is the one operand that decides the
//     answer:  +0x23 CALL 0x180026b50   vs   +0x23 CALL 0x18002afc0
//   0x1800269a0 --calls
//     +0x23 CALL 0x180026b50     the element-destroy helper
//     +0x46 CALL 0x18014b390  }
//     +0x57 CALL 0x1800c6c00  }  the allocator Enter / Free / Leave triple,
//     +0x60 CALL 0x18014b388  }  byte-for-byte the triple 0x180010ab0 makes
//
// The helper is ported in this file as `RtColl_DestroyStrings_180026b50` under
// `kD_Str_10`, and the B_ENTRY line for it (above) names its element as
// `IO::PackageFile::Entry`, so 0x1800269a0 needs NO new DestroySpec row — which
// is asserted from 0x1800269a0's own CALL operand and not from a hash, because a
// hash erases exactly that operand (see the three Wave 25 rows above, where two
// siblings sharing hash 089ee8 take two specs). This file has no entry point for
// 0x1800269a0.

// ---- WAVE 27 LANE FRONTIER: the eleventh J2 address ------------------------
//
// 0x180006b80 -- `~Array<Object::ExternalArrayElement>`. Read out of its own
// disassembly, not inferred from the neighbour:
//     180006b9c  MOV R8D,dword ptr [RCX+0x8]     count  = _size
//     180006ba0  MOV RDX,qword ptr [RCX]         base   = _data
//     180006ba3  CALL 0x180006ac0                <-- the helper, by rel32
//   then the allocator Enter / Free / Leave triple through 0x1801fc718+0x640,
//   0x18014b390, 0x1800c6c00, 0x18014b388, and finally `XOR EAX,EAX` with the
//   two stores at [RDI] and [RDI+0x8] -- which is ArrayDtorImpl exactly.
// So: no new spec. This file has no entry point for 0x180006b80; the helper
// 0x180006ac0's B_ENTRY above is what array.cpp's kSpec_ExtArrayElem destroys
// through.

// ---- WAVE 27 LANE FRONTIER: the twelfth J2 address -------------------------
//
// 0x180004a90 - `~Array<UI::StringTable::TextEntry>`, the member destroy in
// `??1StringTable@UI@@` (0x18011d914 `ADD RCX,0x20 ; CALL 0x180004a90`). Read
// out of its own disassembly, and its CALL operand is the field that decides
// the spec (the Wave 25 lesson three lines of this file already carry):
//     180004aac  MOV R8D,[RCX+0x8]      count = _size
//     180004ab0  MOV RDX,[RCX]          base  = _data
//     180004ab3  CALL 0x180005570       <-- kD_Str2_10, NOT kD_Str_10
//   then the allocator Enter / Free / Leave triple and the two zero stores
//   that are ArrayDtorImpl.
J2_ENTRY(180004a90, ReleaseStringsCounted,  kD_Str2_10)      // CALL 0x180005570  Body B

// UI leftovers batch (docs/history/runtime_port/closure/WORKLIST.md) — 0x180004f50,
// `~Array<UI::SpriteSheet::SpriteInfo>`, the member destroy `~SpriteSheet`
// (src/engine/ui/spritesheet_new.cpp) calls on `this+0x20`. Read out of its
// own disassembly:
//     FUN_1800059e0(this, *(void**)this, *(int32*)(this+8));  // Body B
//     free *(void**)this; zero the header                     // ArrayDtorImpl
// 0x1800059e0 is ALREADY PORTED AND REGISTERED (B_ENTRY(1800059e0, kD_Str2_38)
// above), so this is an entry point for existing machinery, no new spec.
J2_ENTRY(180004f50, ReleaseStringsCounted,  kD_Str2_38)      // CALL 0x1800059e0  Body B

// ---- Cutover closure batch 2 (2026-09-12): the thirteenth and fourteenth --
//
// 0x180004c80 -- `~Array<{String*,MaterialParam}>`, ~MaterialInstance's
// parameter array. Read out of its own body: `MOV R8D,[RCX+8]` / `MOV
// RDX,[RCX]` / `CALL 0x180005880` then the allocator triple and the two zero
// stores -- the same J2 shape, a new (String, stride 0x18, offset 0x00) row.
J2_ENTRY(180004c80, ReleaseStringsCounted,  kD_Str_18at00)   // CALL 0x180005880  Body B
// 0x180051fe0 -- another `~Array` calling 0x180052110 (ported above as
// A_ENTRY's Audio::EffectSlot row, kD_InnerArr_40). Read
// from its own body: `CALL 0x180052110` then the same allocator triple. This
// file has no entry point for 0x180051fe0.
// ---- 2026-09-12: the two Video::ShaderProgram ~Array bodies ----------------
//
// ~ShaderProgram (0x1800f50b0) destroys three member arrays, through TWO
// distinct entry points, and the pairing is exactly the trap the Wave 25 rows
// above record — an address-normalised hash calls 0x18004fb00 and 0x18004fbd0
// the same 127-byte body, and the one operand it erases is the CALL that picks
// the spec AND the body:
//     0x18004fb00  CALL 0x18004fed0   Body B, kD_Str_10      (_constants, _textures)
//     0x18004fbd0  CALL 0x18004ff80   Body A, kD_InnerArr_10 (_byteCode)
// Same stride, same offset, OPPOSITE rule: one releases a refcounted String,
// the other frees an inner Array's buffer outright. Read from each body's own
// rel32, both re-read for this commit:
//     18004fb1c  MOV R8D,[RCX+0x8] / MOV RDX,[RCX] / CALL 0x18004fed0
//     18004fbec  MOV R8D,[RCX+0x8] / MOV RDX,[RCX] / CALL 0x18004ff80
// then in both cases the allocator Enter / Free / Leave triple and the two zero
// stores that are ArrayDtorImpl.
J2_ENTRY(18004fb00, ReleaseStringsCounted,  kD_Str_10)       // CALL 0x18004fed0  Body B
J2_ENTRY(18004fbd0, FreeInnerArraysCounted, kD_InnerArr_10)  // CALL 0x18004ff80  Body A

// ---- Cutover closure batch 2b (2026-09-12) --------------------------------
// 0x180004b70 -- `~Array<{Array<T>,Array<T>}>`. Read from its own body:
// `CALL 0x180005680` with R8D=[RCX+8], RDX=[RCX], then the allocator triple and
// the two zero stores. Body A, the new two-offset kD_InnerArr2_20 row.
J2_ENTRY(180004b70, FreeInnerArraysCounted, kD_InnerArr2_20) // CALL 0x180005680  Body A
// 0x180004d60 -- `CALL 0x180005930`, the ALREADY-PORTED Body B helper
// (kD_Str_10, the {String*, ExternalPtr} element). A second caller for a row
// this file has carried since Wave 9; no new spec.
J2_ENTRY(180004d60, ReleaseStringsCounted,  kD_Str_10)       // CALL 0x180005930  Body B

#undef J2_ENTRY

// ---- 0x18005bf60 / 0x18005c550: the one that is NEITHER body ---------------
//
// Lane D filed 0x18005bf60 as an ordinary J2 with "one more helper row". Its
// helper 0x18005c550 is not Body A and not Body B: each of ITS elements holds
// two nested Arrays whose OWN elements need destroying first, so it is a
// RECURSIVE destroy that calls one of each body before freeing.
//
// Read at 0x18005c550 (RBX = base + 0x28, stride 0x30, so the displacements
// below are RBX-relative in the listing and element-relative here):
//
//   for each element e:
//       0x18005bce0(ignored, *(void**)(e+0x20), *(int32*)(e+0x28));  // Body A x3
//       free *(void**)(e+0x20);  e+0x20 = 0;  e+0x28 = 0;
//       0x180014a40(ignored, *(void**)(e+0x10), *(int32*)(e+0x18));  // Body B
//       free *(void**)(e+0x10);  e+0x10 = 0;  e+0x18 = 0;
//
// Element layout, therefore: {…, Array<String-bearing> at +0x10,
// Array<Array-bearing> at +0x20}, stride 0x30. Both sub-helpers are rows of this
// file (kD_InnerArr3_48, kD_Str_48), so this creates no new row either — but it is a THIRD
// body and had to be written out rather than parameterized, because the inner
// element count is read from the nested header and not passed in.
//
// ORDER IS LOAD-BEARING HERE in a way it is not elsewhere in this file: the
// +0x20 array is destroyed and freed BEFORE the +0x10 one is even read, and the
// two frees reach the allocator in that order.
using AnimInstance = Graphics::AnimationManager::Instance;
static void DestroyNestedPair(AnimInstance* base, int count) {
    if (count <= 0) return;
    const int64_t n = static_cast<int64_t>(count);
    for (int64_t i = 0; i < n; ++i) {
        AnimInstance* e = base + i;

        auto& states = e->_states;   // +0x20
        FreeInnerArraysCore(reinterpret_cast<uint8_t*>(states._data), states._size, &kD_InnerArr3_48);  // CAST-OK: elem_stride
        if (states._data) MemFree(states._data);
        states._data = nullptr;
        states._size = 0;                         // one qword (H7)
        states._capacity = 0;

        Graphics::GraphicsMesh::NodeArray& nodes = e->_nodes; // +0x10
        ReleaseStringsCore(reinterpret_cast<uint8_t*>(nodes._data), nodes._size, &kD_Str_48);  // CAST-OK: elem_stride
        if (nodes._data) MemFree(nodes._data);
        nodes._data = nullptr;
        nodes._size = 0;                          // one qword (H7)
        nodes._capacity = 0;
    }
}

void* RtColl_ArrayDtorElem_18005bf60(void* self) {
    ArrayHdr* a = static_cast<ArrayHdr*>(self);   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
    DestroyAs<AnimInstance, &DestroyNestedPair>(nullptr, a->_data, a->_size);   // CALL 0x18005c550
    void* r = ArrayDtorImpl(a);
    return r;
}

// ---- J2b: 0x18001c8c0, the eh_vector form ---------------------------------
// Body A with offs {0x1e0,0x1d0,0x1c0}, stride 0x1f0 — see the banner for the
// two hops (0x18001b960 is a 5-byte JMP thunk to a twelfth copy of J1) that
// make it Body A and not a CRT dependency.

// ============================================================================
// 0x180004220 — the SpriteSheet element's copy-assign
// ============================================================================
//
// Lane Q's fifth new row, and the one of the five that is a self-contained leaf:
// 37 instructions, ZERO callees, no allocation. Retired here because it costs
// nothing and it CORROBORATES the destroy side — it assigns exactly the two
// System::String slots at +0x00 and +0x08 that 0x1800059e0 releases, in the
// same two places. Two independently-read bodies agreeing on a layout is worth
// more than either reading alone.
//
//     [dst+0x00] = 0
//     s = [src+0x00];  if (s) { [dst+0x00] = s; ++*(int32*)(s-4); }   // refcount
//     [dst+0x08] = 0
//     s = [src+0x08];  if (s) { [dst+0x08] = s; ++*(int32*)(s-4); }   // refcount
//     nine DWORD copies: +0x10 +0x14 +0x18 +0x1c +0x20 +0x24 +0x28 +0x2c +0x30
//     return dst                                        ; MOV RAX,RCX
//
// The pre-zero before each conditional store is retail's, not tidiness: a
// smart-pointer `operator=` inlined, and it is the visible difference between
// "assigned null" and "left whatever was there". Reproduced because
// BANISHED_REDZONE=watch and a hardware write watchpoint can tell them apart.
//
// *** THE ELEMENT IS 0x38 BYTES AND THIS WRITES 0x00 THROUGH 0x33. BYTES
// +0x34 .. +0x37 ARE NEVER WRITTEN BY THE ASSIGN. *** array.cpp's kSpec_Sprite38
// carries that window as `assignUnwritten = [0x34, 0x38)`. Any oracle that
// memcmps a freshly assigned SpriteSheet element without it will fail on four
// bytes of allocator fill and read like a catastrophic port defect — which is
// exactly how an oracle that compares bytes retail never stores fails. The finding is
// filed in docs/history/runtime_port/WAVE9_LANE_D2.md.
static void SpriteInfoCopy(UI::SpriteInfo* d, const UI::SpriteInfo* s) {
    AdoptStringSlot(d->name._data,  s->name._data);   // +0x00
    AdoptStringSlot(d->group._data, s->group._data);  // +0x08
    d->x           = s->x;                        // nine dwords, +0x10 .. +0x30
    d->y           = s->y;
    d->_reserved18 = s->_reserved18;
    d->width       = s->width;
    d->height      = s->height;
    d->u0          = s->u0;
    d->v0          = s->v0;
    d->u1          = s->u1;
    d->v1          = s->v1;
    /* +0x34 .. +0x37 deliberately untouched — retail does not write them */
}
void* RtColl_SpriteEntryAssign_180004220(uint8_t* dst, const uint8_t* src) {
    AssignAs<UI::SpriteInfo, &SpriteInfoCopy>(dst, src);
    return dst;
}

// ---- VTABLE BATCH 2 (2026-09-12): the SECOND element copy-assign -----------
//
// 0x180043980, UI::LayoutDescription's entry (stride 0x38, the same stride as
// the SpriteSheet element above and NOT the same element). Transcribed from its
// own body:
//     +0x00  plain 8-byte copy (the ExternalPtr; NO refcount touch)
//     +0x08, +0x10, +0x18  THREE refcounted Strings: dst is NULLED first, then
//                          set and incremented only when src is non-null
//     +0x20, +0x24, +0x28, +0x2c  four dwords
//     +0x30  one byte
// *** +0x31 .. +0x37 ARE NEVER WRITTEN. *** Same trap as the row above, one
// byte later: an oracle that memcmps 0x38 bytes after this runs compares seven
// bytes of allocator fill. array.cpp's kSpec_LayoutEntry38 carries that exact
// window as `assignUnwritten = [0x31, 0x38)`.
using LayoutEntry = UI::LayoutDescription::Entry;
static void LayoutEntryCopy(LayoutEntry* d, const LayoutEntry* s) {
    d->desc = s->desc;                            // +0x00, no refcount touch
    AdoptStringSlot(d->name._data,        s->name._data);         // +0x08
    AdoptStringSlot(d->relativeToX._data, s->relativeToX._data);  // +0x10
    AdoptStringSlot(d->relativeToY._data, s->relativeToY._data);  // +0x18
    d->horizontal = s->horizontal;                // four dwords
    d->vertical   = s->vertical;
    d->offsetX    = s->offsetX;
    d->offsetY    = s->offsetY;
    d->force      = s->force;                     // one byte
    /* +0x31 .. +0x37 deliberately untouched -- retail does not write them */
}
void* RtColl_LayoutEntryAssign_180043980(uint8_t* dst, const uint8_t* src) {
    AssignAs<LayoutEntry, &LayoutEntryCopy>(dst, src);
    return dst;
}

// ============================================================================
// J4 (the 0x180003970 half) — Collection::Array<T*>::operator=
// ============================================================================
//
//   Array<T*>& operator=(this, const Array<T*>& src)   stride 8, align 8
//
//     if (this == &src) return this;          ; CMP RCX,RDX / JZ
//     ~Array(this);                           ; CALL 0x180004890 — a J1 body
//     ECX = src._size;  EDX = this->_size;    ; *** read AFTER the dtor ***
//     this->_size = ECX;
//     if (ECX > this->_capacity) {
//         this->_capacity = ECX;
//         if (ECX == EDX+1 && EDX != 0) this->_capacity = ECX*3;
//         this->_data = MemAlloc((int64)(int32)_capacity * 8, 8);   ; MOVSXD, SHL 3
//     }
//     for (i=0, off=0; i < this->_size; ++i, off += 8) {
//         p = this->_data + off;
//         if (p) *(void**)p = *(void**)(src._data + off);   ; tests the COMPUTED addr
//     }
//     return this;                            ; MOV RAX,RBX
//
// TWO THINGS THAT LOOK LIKE TRANSCRIPTION ERRORS AND ARE NOT.
//
// 1. `oldSize` is read at 0x18000398d — AFTER the destructor at 0x180003985 has
//    zeroed `_size`. So `oldSize` is ALWAYS 0, which makes `oldSize != 0` always
//    false and the `* 3` treble DEAD CODE in this instantiation. It is written
//    out anyway, in retail's order, because the port's job is the shipped
//    behaviour and not the behaviour MSVC meant; folding it away would silently
//    diverge if the read order were ever misread.
//
// 2. `_capacity` is likewise 0 after the dtor, so `newSize > _capacity` is taken
//    for every non-empty source and this always reallocates. That is why there
//    is no "copy from `old`" limb here as there is in SetSize: `old` is gone.
//
// The element copy is a plain 8-byte move with NO refcount and NO pre-zero —
// this instantiation's T is a raw pointer. Lane Q's `kSpec_StringExt` shows what
// the refcounting variant looks like; do not merge the two.
//
// The other J4 address, 0x180010110 (`ModArrayCopy`), is NOT ported here: it
// calls 0x1800101e0 (Mod's copy ctor) and is Array<Mod>::operator= over
// kSpec_Mod in io/modification.cpp.
void* RtColl_ArrayAssign_180003970(void* self, const void* src) {
    ArrayHdr* a = static_cast<ArrayHdr*>(self);   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
    const ArrayHdr* s = static_cast<const ArrayHdr*>(src);  // CAST-OK: elem_stride
    if (a == s) return a;

    ArrayDtorImpl(a);                      // CALL 0x180004890 — lane Q's J1 body

    const int32_t newSize = s->_size;
    const int32_t oldSize = a->_size;      // always 0; see note 1 above
    a->_size = newSize;

    bool realloced = false;
    if (newSize > a->_capacity) {          // signed 32-bit compare
        a->_capacity = newSize;
        if (newSize == static_cast<int32_t>(oldSize + 1) && oldSize != 0)
            a->_capacity = static_cast<int32_t>(newSize * 3);   // dead; kept faithful
        a->_data = static_cast<uint8_t*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity)) * 8, 8));
        realloced = true;
    }

    int copied = 0;
    for (int32_t i = 0; i < a->_size; ++i) {
        uint8_t* p = a->_data + static_cast<int64_t>(i) * 8;
        if (p == nullptr) continue;        // retail tests the COMPUTED address
        AssignAs<void*, &CopyWords<void*, 1>>(p, s->_data + static_cast<int64_t>(i) * 8);
        ++copied;
    }

    return a;
}

// ============================================================================
// CUTOVER CLOSURE BATCH 2b (2026-09-12)
//   * one more J2 whose helper is NEITHER Body A nor Body B
//   * the generic J4 (`Array<T>::operator=`) and its three new instantiations
// ============================================================================

// ---- 0x1800109d0: a J2 with a COMPOUND element helper ----------------------
//
// Shape is J2 exactly — `R8D=[RCX+8]; RDX=[RCX]; CALL <helper>` then
// ArrayDtorImpl — but the helper, 0x180010f00, is a THIRD body and not a member
// of the A/B pair this file is built around. Per element (stride 0x18) it:
//     CALL 0x180010da0(this, e[1], (int)e[2])   a sub-destroy of its own
//     free(e[1]); e[1] = 0; e[2] = 0            Body-A-shaped, on a non-Array
//                                               {ptr,count} pair
//     release the System::String at e[0]        Body-B-shaped
// Expressing that as a DestroySpec would need a THIRD core. This file has no
// entry point for 0x1800109d0; the helper 0x180010f00 is
// RtVideo_ArrayInterfaceElemDestroy in video/device_enum_types.cpp.

// ---- The generic J4 --------------------------------------------------------
//
// `RtColl_ArrayAssign_180003970` above and `..._18001b1a0` below each write this body
// out by hand because each was landed alone. Three arrive at once here, one of
// them with a non-trivial element, so the body is parameterised by the same
// `ArraySpec` the SetSize family uses plus a CLEAR callback — which is the one
// thing that genuinely varies between instantiations (a plain `~Array` for the
// trivial elements, a destroy-then-free for the refcounted one).
//
// Read out of 0x180014c70 and confirmed instruction-shape-identical at
// 0x180005770 and 0x180033f30:
//     if (this == &src) return this;          CMP RCX,RDX / JZ
//     <clear>(this);                          CALL — leaves _size = 0
//     _size = src._size;
//     if (_size > _capacity) { grow; MemAlloc(cap * stride, align); }
//     for (i = 0; i < _size; ++i) assign(data + i*stride, src.data + i*stride);
//     return this;
// The `_capacity = n*3` branch is DEAD here (it needs oldSize != 0 and the
// clear has just zeroed it) and is reproduced anyway, because "dead" is a claim
// about the clear, not about this function.
//
// NO OLD-BUFFER FREE AFTER THE COPY: the clear already freed it. A tidied
// version that mirrored SetSizeCore's tail would double-free.
//
// The body, rtcoll::ArrayAssignCore, is header-inline in
// src/engine/collection/Collection.h (B9 RT2, the B7 D4 shape): retail
// open-codes the assign at its callers, and an out-of-line core made `&src`
// escape at Audio::Scene::Update (row 880).

// The trivial clear: retail's J1 body, which is what 0x180014d00, 0x180005800
// and every other `~Array` in the trivial family is.
void ArrayClearTrivial(ArrayHdr* a) { ArrayDtorImpl(a); }

// 0x180033f30's clear is 0x1800331f0 — the J2 shape: release the two Strings in
// every element (Body B, kD_Str2_20) and THEN the J1 body. Written out rather
// than resolved by address so the element spec and the clear cannot drift apart.
void ArrayClearMetaEntry20(ArrayHdr* a) {
    ReleaseStringsCounted(a->_data, a->_size, &kD_Str2_20);
    ArrayDtorImpl(a);
}

// The element operations live in array.cpp (one namespace, two files — see the
// A_ENTRY banner). Declared rather than duplicated: a second copy of
// AssignMetaEntry20 is exactly how this family produces divergences.
void Assign8(uint8_t* d, const uint8_t* s);
void AssignMetaEntry20(uint8_t* d, const uint8_t* s);

// Element specs for the three J4 addresses. Only `stride`, `align` and `assign`
// are consulted by ArrayAssignCore; the rest is filled so the row still reads
// as an ArraySpec.
//   0x180014c70  Graphics: Array<T*>, `MemAlloc(cap << 3, 8)`
//   0x180005770  UI:       Array<T*>, `MemAlloc(cap << 3, 4)`  <-- ALIGN 4, and
//                the ONLY difference from the row above. Read from its own
//                `MOV EDX,0x4`; assuming 8 from the sibling would hand the
//                allocator a wrong alignment on every UI list copy.
//   0x180033f30  Save:     Array<Save::MetaDataEntry>, stride 0x20, align 8
const ArraySpec kAsgn_Ptr8A8 = {
    "Array<T*>", 8, 8, &Assign8, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgn_Ptr8A4 = {
    "Array<T*>(align 4)", 8, 4, &Assign8, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgn_MetaEntry20 = {
    "Array<Save::MetaDataEntry>", 0x20, 8, &AssignMetaEntry20, nullptr, nullptr,
    false, 0x1c, 0x20, 0, 0, 0x00, 0x10 };

#define J4_ENTRY(ADDR, SPEC, CLEAR)                                           \
    void* RtColl_ArrayAssign_##ADDR(void* self, const void* src) {            \
        return ArrayAssignCore(static_cast<ArrayHdr*>(self),  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its receiver (void* self, a caller's Array<T>) as the ArrayHdr core; M2 bars a self operand */ \
                               static_cast<const ArrayHdr*>(src),  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its caller's Array<T> (void*) as the ArrayHdr core */ \
                               &SPEC, &CLEAR);                  \
    }

J4_ENTRY(180014c70, kAsgn_Ptr8A8,      ArrayClearTrivial)       // clear 0x180014d00
J4_ENTRY(180033f30, kAsgn_MetaEntry20, ArrayClearMetaEntry20)   // clear 0x1800331f0

#undef J4_ENTRY

// ---- Cutover closure close-out (2026-09-12): the stride-0x38 Array family ---
//
// Three bodies, one element type, read out of their own disassembly:
//   0x18001cd30  Array<blob0x38>::operator=   J4: clear (0x18001c650), then
//                `_size = src._size`; grow if `_capacity < size` with the
//                usual `(size == old+1 && old != 0) ? size*3 : size` rule,
//                `MemAlloc(cap * 0x38, 8)`; then a per-element copy of SEVEN
//                qwords (`MOV RAX,[RDX+0x00..0x30]` / `MOV [RCX+..],RAX`).
//                No element ctor, no String adopt: a POD element.
//   0x18001c620  Array<blob0x38>::Array(const Array&)   `_data = 0; _size =
//                _capacity = 0;` then a CALL to 0x18001cd30 and `MOV RAX,RCX`.
//   0x18001c650  Array<blob0x38>::~Array   the thirteenth copy of J1
//                (allocator lock, Free, zero the three fields) — the same body
//                array.cpp's 0x1800638e0 row already named as the one ArrayDtorImpl IS.
// The one caller of the copy ctor is the eh_vector copy iterator at
// 0x18001bece (Graphics::DrawGroup's inner arrays; graphics/closure_batch2b.cpp
// calls it by name through InnerArrayCopyCtor), which is how the closure
// found all three.
void Assign56(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 7>>(d, s); }
const ArraySpec kAsgn_Blob38 = {
    "Array<blob0x38>", 0x38, 8, &Assign56, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

void* RtColl_ArrayAssign_18001cd30(void* self, const void* src) {
    return ArrayAssignCore(static_cast<ArrayHdr*>(self),   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
                           static_cast<const ArrayHdr*>(src),  // CAST-OK: elem_stride
                           &kAsgn_Blob38, &ArrayClearTrivial);
}
void* RtColl_ArrayCopyCtor_18001c620(void* self, const void* src) {
    ArrayHdr* h = static_cast<ArrayHdr*>(self);   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
    h->_data = nullptr;                                       // 18001c624
    h->_size = 0;                                             // 18001c62b (qword:
    h->_capacity = 0;                                         //   size + capacity)
    RtColl_ArrayAssign_18001cd30(self, src);                  // 18001c635
    return self;                                              // MOV RAX,RCX
}

// Cutover closure batch 2 (2026-09-12): 0x18001b1a0, `Array<Graphics::
// Geometry*>::operator=`. Read out of its own body and IDENTICAL in shape to
// 0x180003970 above -- dtor first (CALL 0x18001bf50, a J1 body, called here as
// ArrayDtorImpl: a parameterless body so which address it
// is does not matter), then the same realloc-and-copy loop, T a raw 8-byte
// pointer with no refcount and no pre-zero. A second occurrence of one J4
// body, not a new one, with its own entry point for its own retail address.
void* RtColl_ArrayAssign_18001b1a0(void* self, const void* src) {
    ArrayHdr* a = static_cast<ArrayHdr*>(self);   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
    const ArrayHdr* s = static_cast<const ArrayHdr*>(src);  // CAST-OK: elem_stride
    if (a == s) return a;

    ArrayDtorImpl(a);                      // CALL 0x18001bf50 — a J1 body

    const int32_t newSize = s->_size;
    const int32_t oldSize = a->_size;      // always 0; dtor already zeroed it
    a->_size = newSize;

    bool realloced = false;
    if (newSize > a->_capacity) {
        a->_capacity = newSize;
        if (newSize == static_cast<int32_t>(oldSize + 1) && oldSize != 0)
            a->_capacity = static_cast<int32_t>(newSize * 3);   // dead; kept faithful
        a->_data = static_cast<uint8_t*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity)) * 8, 8));
        realloced = true;
    }

    int copied = 0;
    for (int32_t i = 0; i < a->_size; ++i) {
        uint8_t* p = a->_data + static_cast<int64_t>(i) * 8;
        if (p == nullptr) continue;
        AssignAs<void*, &CopyWords<void*, 1>>(p, s->_data + static_cast<int64_t>(i) * 8);
        ++copied;
    }

    return a;
}


// ============================================================================
// CUTOVER CLOSURE BATCH D1 (2026-09-12) — the destroy / J2 / J4 half
// ============================================================================
//
// The SetSize and J1 half is in array.cpp. Every row below was read out of its
// own disassembly in retail Runtime-x64.dll; the two composite bodies at the
// end are written out longhand because neither is Body A nor Body B.

// ---- ONE NEW BODY-B ROW ----------------------------------------------------
// TWO Strings at +0x08 then +0x00, stride 0x18 — read at 0x18001cbf0 and
// confirmed instruction-for-instruction at 0x180056960. DESCENDING, like
// kD_Str2_38 and kD_Str2_10; the assert below enforces it.
//
// NOT kD_Str_18 (one String at +0x10, same stride) and NOT kD_Str_18at00 (one
// String at +0x00, same stride). Three rows now share the stride 0x18 and
// disagree about every offset, which is exactly the trap the kD_Str_18 note
// above was written for.
constexpr DestroySpec kD_Str2_18 = { "2 Strings @+0x008,+0x000, stride 0x018",
                                     0x018, 2, { 0x008, 0x000 } };
static_assert(kD_Str2_18.offs[0] + 8u <= kD_Str2_18.stride, "offs[0]");
static_assert(kD_Str2_18.offs[1] + 8u <= kD_Str2_18.stride, "offs[1]");
static_assert(kD_Str2_18.offs[0] > kD_Str2_18.offs[1],
              "0x18001cbf0 releases +0x08 BEFORE +0x00");

#define D1_A_ENTRY(ADDR, SPEC)                                                \
    void RtColl_DestroyInner_##ADDR(void* ignoredThis, void* base, int count) { \
        (void)ignoredThis;                                                    \
        const int freed = FreeInnerArraysCounted(                             \
            static_cast<uint8_t*>(base), count, &SPEC);  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased element walk steps the caller's element storage (void* base) as bytes */ \
    }

// 0x180052220: `LEA RBX,[RDX+0x8]` at 0x18005224c, the two zero stores at
// 0x1800522a7/0x1800522aa and `ADD RBX,0x18` at 0x1800522ae. Body A with
// offs {0x08} and stride 0x18 == kD_InnerArr_18, the row 0x1800419b0 uses.
// Its caller is array.cpp's kSpec_SfxBuffer18 (0x180053190).
D1_A_ENTRY(180052220, kD_InnerArr_18)  // Audio: SoundEffect::Buffer's nested Array<Sample>

#undef D1_A_ENTRY

#define D1_B_ENTRY(ADDR, SPEC)                                                \
    void RtColl_DestroyStrings_##ADDR(void* ignoredThis, void* base, int count) { \
        (void)ignoredThis;                                                    \
        const int n = ReleaseStringsCounted(                                  \
            static_cast<uint8_t*>(base), count, &SPEC);  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased element walk steps the caller's element storage (void* base) as bytes */ \
    }

// 0x18003e3e0: `MOV RBX,RDX` (no +off seed) and `ADD RBX,0x18` at 0x18003e46c
// — String at +0x00, stride 0x18 == kD_Str_18at00, the row 0x180005880 uses.
// Caller: array.cpp's kSpec_Link18 (0x18003e1a0 and 0x18003d430).
D1_B_ENTRY(18003e3e0, kD_Str_18at00)  // UI: DocumentText::Link {String*, 2x int32, bool}
// 0x180054920: `MOV RBX,RDX`, `ADD RBX,0x8` — kD_Str_08.
D1_B_ENTRY(180054920, kD_Str_08)      // Video: PixelProgram's string table
// 0x180054ba0: `MOV RBX,RDX`, `ADD RBX,0x10` — kD_Str_10.
D1_B_ENTRY(180054ba0, kD_Str_10)      // Video: PixelProgram::Sampler
// The two copies of the new two-String row.
D1_B_ENTRY(18001cbf0, kD_Str2_18)     // Graphics: {String*, String*} element
D1_B_ENTRY(180056960, kD_Str2_18)     // Video: VertexProgram::Element

#undef D1_B_ENTRY

#define D1_J2_ENTRY(ADDR, CORE, SPEC)                                         \
    void* RtColl_ArrayDtorElem_##ADDR(void* self) {                           \
        ArrayHdr* a = static_cast<ArrayHdr*>(self);  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its receiver (void* self, a caller's Array<T>) as the ArrayHdr core; M2 bars a self operand */ \
        const int freed = CORE(a->_data, a->_size, &SPEC);                    \
        void* r = ArrayDtorImpl(a);                                           \
        return r;                                                             \
    }

// Five more J2 bodies. Each line names the helper its own disassembly CALLs.
D1_J2_ENTRY(18001c2d0, ReleaseStringsCounted,  kD_Str2_18)     // CALL 0x18001cbf0  Body B
D1_J2_ENTRY(18003e310, ReleaseStringsCounted,  kD_Str_18at00)  // CALL 0x18003e3e0  Body B
D1_J2_ENTRY(18003e4a0, ReleaseStringsCounted,  kD_Str_28at08)  // CALL 0x18003f070  Body B
D1_J2_ENTRY(1800516c0, FreeInnerArraysCounted, kD_InnerArr_18) // CALL 0x180052220  Body A
D1_J2_ENTRY(180056610, ReleaseStringsCounted,  kD_Str2_18)     // CALL 0x180056960  Body B

#undef D1_J2_ENTRY

// ---- 0x180056cb0: a per-element destroy that is NEITHER Body A NOR Body B --
//
// Video::VertexProgram::Stream, stride 0x18: {Collection::Array<Element>
// @+0x00, System::String* @+0x10}. Per element, in THIS order (0x180056ce1):
//     1. release the String at +0x10          (Body B's arm, inlined)
//     2. run 0x180056960 over the nested array's elements (kD_Str2_18) with
//        base = [e+0x00] and count = [e+0x08]
//     3. free [e+0x00] and zero BOTH [e+0x00] and [e+0x08]
// Step 2 is what makes it composite: Body A frees the inner buffer but never
// destroys the inner ELEMENTS, and every inner element here holds two Strings.
// Expressing it as a DestroySpec would need a third core, so it is written out.
void RtColl_DestroyVtxStream_180056cb0(void* ignoredThis, void* basev, int count) {
    (void)ignoredThis;
    if (count <= 0) return;                       // TEST R8D,R8D / JLE
    // B.4 (X-RL): the element is `Video::VertexProgram::Stream` (ShaderProgram.h).
    Video::VertexProgram::Stream* base = static_cast<Video::VertexProgram::Stream*>(basev);  // CAST-OK: elem_stride
    const int64_t n = static_cast<int64_t>(count);   // MOVSXD
    int freed = 0;
    for (int64_t i = 0; i < n; ++i) {
        Video::VertexProgram::Stream* e = base + i;  // stride 0x18
        ReleaseStringsCore(reinterpret_cast<uint8_t*>(e), 1, &kD_Str_18);      // the String at +0x10 (kD_Str_18)  // CAST-OK: elem_stride
        Video::VertexProgram::Element* inner = e->elements.data;
        const int32_t innerN = e->elements.size;
        freed += ReleaseStringsCounted(reinterpret_cast<uint8_t*>(inner), innerN, &kD_Str2_18);  // CAST-OK: elem_stride
        if (inner) MemFree(inner);                 // 0x180056d53..0x180056d83
        e->elements.data     = nullptr;            // 0x180056d89
        e->elements.size     = 0;                  // 0x180056d8c, one qword in retail (H7)
        e->elements.capacity = 0;
    }
}

// ---- 0x18001c1e0: ~Array<Graphics::Animation> ------------------------------
//
// J2's shape with an INLINED element loop instead of a call to a shared helper:
//     EDI = _size; RBX = _data;
//     while (EDI--) { 0x1800fb290(RBX); RBX += 0x70; }     // forward order
// then, instruction for instruction, the J1 body. 0x1800fb290 is
// `Graphics::~Animation`, called by name as RtGraphics_Animation_Dtor
// (graphics/Animation.h, defined in graphics/batchc_gfx_leaves.cpp).
void* RtColl_ArrayDtorAnim_18001c1e0(void* self) {
    ArrayHdr* a = static_cast<ArrayHdr*>(self);   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
    const int32_t n = a->_size;
    uint8_t* p = a->_data;
    if (n > 0) {                                   // TEST EAX,EAX / JLE
        for (int32_t i = 0; i < n; ++i, p += 0x70) RtGraphics_Animation_Dtor(p);
    }
    void* r = ArrayDtorImpl(a);
    return r;
}

// ---- Five more J4 (`Array<T>::operator=`) instantiations -------------------
//
// All five are ArrayAssignCore's shape exactly — `if (this == &src) return`,
// the clear, `_size = src._size`, the same grow rule and MemAlloc, then a
// per-element copy, and NO free after the copy. What varies is stride, align,
// the element assign and which J1 body the clear is, and every one of those was
// read from the body rather than from a neighbour:
//
//   0x180013640  clear 0x180013e70, `LEA RCX,[RAX+RAX*2]; SHL RCX,0x3`
//                (cap * 0x18), `MOV EDX,0x4` — ALIGN 4 — assign SIX dwords
//   0x18001b270  clear 0x18001c020, `SHL RCX,0x4`, `MOV EDX,0x8`,
//                assign TWO qwords
//   0x180052700  clear 0x1800527a0, cap * 0x18, `MOV EDX,0x8` — ALIGN 8, and
//                that is the ONLY difference from 0x180013640 — assign THREE
//                qwords
//   0x18003ce30  clear 0x18003d8c0, `SHL RCX,0x3`, `MOV EDX,0x8`, one qword
//   0x18003cf20  clear 0x18003da00, byte-identical to the row above
//
// The element operations are declared, not duplicated: array.cpp owns them and
// a second copy is how this family produces divergences.
void Assign18d(uint8_t* d, const uint8_t* s);
void Assign16(uint8_t* d, const uint8_t* s);
void Assign18(uint8_t* d, const uint8_t* s);

const ArraySpec kAsgn_Tri18A4 = {
    "Array<EdgeMesh::Triangle>", 0x18, 4, &Assign18d, nullptr, nullptr, true,
    0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgn_Blk16A8 = {
    "Array<blk16>", 0x10, 8, &Assign16, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgn_Blk18A8 = {
    "Array<Audio::SoundEffect::Sample>", 0x18, 8, &Assign18, nullptr, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

#define D1_J4_ENTRY(ADDR, SPEC, CLEAR)                                        \
    void* RtColl_ArrayAssign_##ADDR(void* self, const void* src) {            \
        return ArrayAssignCore(static_cast<ArrayHdr*>(self),  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its receiver (void* self, a caller's Array<T>) as the ArrayHdr core; M2 bars a self operand */ \
                               static_cast<const ArrayHdr*>(src),  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its caller's Array<T> (void*) as the ArrayHdr core */ \
                               &SPEC, &CLEAR);                  \
    }

D1_J4_ENTRY(180013640, kAsgn_Tri18A4, ArrayClearTrivial)  // clear 0x180013e70
D1_J4_ENTRY(18001b270, kAsgn_Blk16A8, ArrayClearTrivial)  // clear 0x18001c020
D1_J4_ENTRY(180052700, kAsgn_Blk18A8, ArrayClearTrivial)  // clear 0x1800527a0
D1_J4_ENTRY(18003ce30, kAsgn_Ptr8A8,  ArrayClearTrivial)  // clear 0x18003d8c0
D1_J4_ENTRY(18003cf20, kAsgn_Ptr8A8,  ArrayClearTrivial)  // clear 0x18003da00

#undef D1_J4_ENTRY


// ============================================================================
// CUTOVER CLOSURE BATCH D2 "helpers-high" (2026-09-12)
// ============================================================================
//
// The J2 / J4 half of the unnamed helper bodies at or above 0x1800571b0 (the
// SetSize and ~Array halves are array.cpp's block of the same name). Owners,
// from `get_function_callers` in retail Ghidra:
//   0x1800571b0            Video::VertexProgram::Serialize's parameter array
//   0x1800664c0/0x180065d40  Collision2D::CollisionMesh::_faces
//   0x180066b30/0x180066de0  Collision2D::Face's two inner arrays
//   0x180073ee0/0x180073720  Picking::PickingMesh::_nodes
//   0x1800741b0/0x180073dc0/0x1800742d0  Picking::SubMesh's four inner arrays

// ---- two more DestroySpec rows --------------------------------------------
//
// 0x1800664c0 (Collision2D::Face's range destroy). Read out of its own body:
// the walk seeds `RBX = base + 0x24` and advances by 0x28, freeing
// `[RBX-0x0c]` (= +0x18, Face::quads) FIRST and `[RBX-0x1c]` (= +0x08,
// Face::triples) second, clearing both 16-byte headers. DESCENDING offsets,
// like every multi-offset row in this file, and asserted as such below.
// It is NOT kD_InnerArr2_20 (stride 0x20, offsets {0x10, 0x00}).
constexpr DestroySpec kD_InnerArr2_28 = { "2 inner Arrays @+0x018,+0x008, stride 0x028",
                                          0x028, 2, { 0x018, 0x008 } };
static_assert(kD_InnerArr2_28.nOffs == 2, "kD_InnerArr2_28 nOffs");
static_assert(kD_InnerArr2_28.offs[0] + 16u <= kD_InnerArr2_28.stride, "offs[0]");
static_assert(kD_InnerArr2_28.offs[1] + 16u <= kD_InnerArr2_28.stride, "offs[1]");
static_assert(kD_InnerArr2_28.offs[0] > kD_InnerArr2_28.offs[1], "descending");

// The Video::VertexProgram parameter element's row, `kD_Str2_18`, is NOT
// redefined here: it is defined above, with the entry points for 0x180056960
// (D1_B_ENTRY) and its ~Array wrapper 0x180056610 (D1_J2_ENTRY). One row, one owner.

// ---- 0x1800664c0: a Body-A range destroy ----------------------------------
#define A_ENTRY(ADDR, SPEC)                                                   \
    void RtColl_DestroyInner_##ADDR(void* ignoredThis, void* base, int count) { \
        (void)ignoredThis;                                                    \
        const int freed = FreeInnerArraysCounted(                             \
            static_cast<uint8_t*>(base), count, &SPEC);  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased element walk steps the caller's element storage (void* base) as bytes */ \
    }

A_ENTRY(1800664c0, kD_InnerArr2_28)   // Collision2D: ~Face's two inner arrays

#undef A_ENTRY

// ---- 0x180073ee0: a THIRD kind of element helper --------------------------
//
// Picking::SubMesh's range destroy is neither Body A nor Body B: per element
// (stride 0x80) it frees FOUR inner `Collection::Array<T>` and THEN releases
// one System::String, and `DestroySpec` carries at most three offsets. Rather
// than widen the shared struct for one caller, the loop is written out here in
// retail's own order, read from the body's seed `RBX = base + 0x7c` and its
// four `[RBX-0x0c] [RBX-0x1c] [RBX-0x2c] [RBX-0x3c]` slots:
//
//     +0x70  _uvs        free buffer, zero both header qwords
//     +0x60  _triangles  same
//     +0x50  _normals    same
//     +0x40  _vertices   same
//     +0x00  _name       System::String release
//
// DESCENDING through the arrays and the String LAST -- the same shape as
// 0x1800109d0's compound helper, and the reason that one is written out too.
using SubMesh = Picking::PickingMesh::SubMesh;
template <class T>
static inline void FreeArrayField(Collection::Array<T>& a) {
    if (a._data) MemFree(a._data);
    a._data     = nullptr;
    a._size     = 0;                              // one qword (H7)
    a._capacity = 0;
}
static void DestroySubMesh80(SubMesh* base, int count) {
    if (count <= 0) return;                       // TEST R8D,R8D / JLE
    const int64_t n = static_cast<int64_t>(count);  // MOVSXD
    for (int64_t i = 0; i < n; ++i) {
        SubMesh* e = base + i;
        FreeArrayField(e->_uvs);                  // +0x70
        FreeArrayField(e->_triangles);            // +0x60
        FreeArrayField(e->_normals);              // +0x50
        FreeArrayField(e->_vertices);             // +0x40
        ReleaseStringSlot(e->_name._data);        // +0x00, LAST
    }
}
void RtColl_DestroySubMesh_180073ee0(void* ignoredThis, void* base, int count) {
    (void)ignoredThis;
    DestroyAs<SubMesh, &DestroySubMesh80>(ignoredThis, static_cast<uint8_t*>(base), count);  // CAST-OK: elem_stride
}

// ---- the two J2 entry points ----------------------------------------------
// Shape is J2 exactly (`R8D=[RCX+8]; RDX=[RCX]; CALL helper`, then the J1
// body); only the helper differs.
void* RtColl_ArrayDtorElem_180065d40(void* self) {          // ~Array<Face>
    ArrayHdr* a = static_cast<ArrayHdr*>(self);   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
    const int freed = FreeInnerArraysCounted(a->_data, a->_size, &kD_InnerArr2_28);
    void* r = ArrayDtorImpl(a);
    return r;
}

void* RtColl_ArrayDtorElem_180073720(void* self) {          // ~Array<SubMesh>
    ArrayHdr* a = static_cast<ArrayHdr*>(self);   // the erased core views its receiver (void* self) as the header  // CAST-OK: elem_stride
    DestroyAs<SubMesh, &DestroySubMesh80>(nullptr, a->_data, a->_size);
    void* r = ArrayDtorImpl(a);
    return r;
}

// ---- six more J4 (`Array<T>::operator=`) ----------------------------------
//
// All six are ArrayAssignCore exactly -- `if (this == &src) return; clear(this);
// _size = src._size; grow; copy loop; return this` -- and five of the six clear
// through a J1 body, so `ArrayClearTrivial`. What varies is the element spec,
// and the four stride-collisions in this group are the reason each row was read
// out of its OWN body's `MOV EDX,<align>` and its own copy loop:
//
//   0x180066b30  stride 8    align 2   one qword move        clear 0x180066cd0
//   0x180066de0  stride 6    align 2   dword + word          clear 0x180066fa0
//   0x1800742d0  stride 4    align 2   one dword move        clear 0x180074360
//   0x180073dc0  stride 0xc  align 2   qword + dword         clear 0x180073e60
//   0x1800741b0  stride 0xc  align 4   THREE dword moves     clear 0x180074250
//   0x1800571b0  stride 0x18 align 8   two Strings + 2 ints  clear 0x180056610
//
// Note 0x180073dc0 and 0x1800741b0: same stride, different align AND different
// store widths. Sharing one row between them would hand the allocator the wrong
// alignment on every Picking triangle-array copy.

// The element operations live in array.cpp (one namespace, two files -- see the
// A_ENTRY banner). Declared rather than duplicated.
void AssignD2_6(uint8_t* d, const uint8_t* s);
void AssignD2_12q(uint8_t* d, const uint8_t* s);
void Assign12(uint8_t* d, const uint8_t* s);
void Assign4(uint8_t* d, const uint8_t* s);

// 0x1800571b0's element is the SAME `Video::VertexProgram::Element` the
// sibling batch read out of 0x1800567d0's SetSize -- {String* @+0x00,
// String* @+0x08, uint32 @+0x10, int32 @+0x14}, stride 0x18. Its copy loop at
// 0x180057220.. is that same two-adopts-then-two-dwords sequence, so
// `AssignVtxElement18` is REUSED rather than written a second time (declared,
// not duplicated: it lives in array.cpp with the other element ops).
void AssignVtxElement18(uint8_t* d, const uint8_t* s);

// 0x1800571b0's clear is 0x180056610, which is ported as a J2 (D1_J2_ENTRY)
// in this same file. Calling its entry point IS retail's
// `CALL 0x180056610`, and it keeps one body for that address.
void* RtColl_ArrayDtorElem_180056610(void* self);
void ArrayClearVPParam18(ArrayHdr* a) { RtColl_ArrayDtorElem_180056610(a); }

const ArraySpec kAsgnD2_Blk8A2 = {
    "Array<Quad4>(align 2)", 8, 2, &Assign8, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgnD2_Edge6A2 = {
    "Array<Triple3>", 6, 2, &AssignD2_6, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgnD2_U32A2 = {
    "Array<Picking::UV16>", 4, 2, &Assign4, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgnD2_Tri12A2 = {
    "Array<Picking::Triangle>", 0xc, 2, &AssignD2_12q, nullptr, nullptr, true,
    0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgnD2_Vec12A4 = {
    "Array<Vector3>", 0xc, 4, &Assign12, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };
const ArraySpec kAsgnD2_VPParam18 = {
    "Array<Video::VertexProgram::Element>", 0x18, 8, &AssignVtxElement18, nullptr,
    nullptr, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x10 };   // both String slots are dereferenced

#define J4_ENTRY(ADDR, SPEC, CLEAR)                                           \
    void* RtColl_ArrayAssign_##ADDR(void* self, const void* src) {            \
        return ArrayAssignCore(static_cast<ArrayHdr*>(self),  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its receiver (void* self, a caller's Array<T>) as the ArrayHdr core; M2 bars a self operand */ \
                               static_cast<const ArrayHdr*>(src),  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its caller's Array<T> (void*) as the ArrayHdr core */ \
                               &SPEC, &CLEAR);                  \
    }

J4_ENTRY(180066b30, kAsgnD2_Blk8A2,    ArrayClearTrivial)     // clear 0x180066cd0
J4_ENTRY(180066de0, kAsgnD2_Edge6A2,   ArrayClearTrivial)     // clear 0x180066fa0
J4_ENTRY(1800742d0, kAsgnD2_U32A2,     ArrayClearTrivial)     // clear 0x180074360
J4_ENTRY(180073dc0, kAsgnD2_Tri12A2,   ArrayClearTrivial)     // clear 0x180073e60
J4_ENTRY(1800741b0, kAsgnD2_Vec12A4,   ArrayClearTrivial)     // clear 0x180074250
J4_ENTRY(1800571b0, kAsgnD2_VPParam18, ArrayClearVPParam18)   // clear 0x180056610

#undef J4_ENTRY


}  // namespace rtcoll

// ENDGAME B.3.2 closing pass (2026-09-18): ten entry points and their rows were
// removed at zero callers — 18001c7e0, 1800269a0, 180073c90, 180006b80,
// 180051fe0, 18001c8c0, 180005680, 1800109d0, 180005770, 18001c650. The test is
// the one array.cpp's banner spells out (no caller by name, no reference to the
// address in code in ANY form, absent from both .inc files). Their provenance
// comments are left standing above; a banner naming an address with no entry is
// one of these.
