// ============================================================================
// src/engine/ui/textelement_cachetext.cpp — WAVE 10, lane UIORACLE
//
//   ?SetParameterCount@TextElement@UI@@AEAAXXZ   retail 0x18011e8e0  (ord 3938)
//   ?CacheText@TextElement@UI@@AEAAXXZ           retail 0x18011ec10  (ord 1680)
//
// Both are `AEAA` — PRIVATE, and this DLL exports its private members, so
// "private" was never the reason they were not ported
// (reference_aeaa_is_private_not_unexported.md).
// ============================================================================
//
// WHY THIS FILE EXISTS AND WHAT THE RECORDED BLOCKER TURNED OUT TO BE
// -------------------------------------------------------------------
// The UI census carries `?CacheText@TextElement@UI@@AEAAXXZ` as "not replayable,
// needs the predictive oracle", parked behind the allocator NO-GO. Both halves
// of that were re-checked from scratch rather than inherited
// (reference_holdback_blockers_are_stale_check_them_first.md):
//
//  * NOT REPLAYABLE — STILL TRUE, and it is the SAME defect Wave 9 lane M
//    found on `NumberElement::SetValue`. This body swaps a refcounted
//    System::String at `this+0xF8`: ours releases the old buffer, and a replay
//    that then hands retail a shallow copy hands it a pointer ours has already
//    freed. WAVE9.md finding (1). A prefix replay here is not a weak oracle,
//    it is a use-after-free.
//
//
//  * THE ALLOCATOR "NO-GO" IS NOT LOad-BEARING FOR THIS ROW. This body does not
//    contain an allocator: every allocation and every free it performs is a
//    CALL into a function that is already ported —
//    `String::FormatParamList` (Wave 9 lane B3), `MemoryAllocator::MemFree`
//    (Wave 2), `UI::String::operator System::String` (Wave 7 lane F) and, as of
//    this commit, `Collection::Array<System::String>::SetSize` (0x180029a80).
//    Nothing here decides where memory comes from. **This lane's conclusion
//    therefore does NOT depend on lane ALLOC's re-test of that gate.**
//
//  * ONE DEPENDENCY WAS REAL AND IS CLOSED HERE. `CacheText`'s first act is a
//    call to `SetParameterCount` (0x18011e8e0), whose own callee 0x180029a80
//    was an unported, unexported `Array<System::String>::SetSize`. Wave 9's
//    array_destroy.cpp predicted this exact arrival — "It will get a SECOND
//    caller the day 0x180029a80 … lands" — so 0x180029a80 is ported in
//    src/engine/collection/array.cpp as `kSpec_StringRefZ` and
//    is called through that row, not by a private second body.
//
// LAYOUT (all offsets read from the two disassemblies, not from decompiled C)
// --------------------------------------------------------------------------
//   this+0xC8  Collection::Array<System::String>._data   (the parameters)
//   this+0xD0  ...                       ._size          <- the branch selector
//   this+0xD4  ...                       ._capacity
//   this+0xD8  UI::String (16 bytes) — the raw text reference
//   this+0xF8  char* — the OWNED, refcounted cached display text
//
// THE TWO BRANCHES, AND WHY THEY NEED DIFFERENT ORACLES
// -----------------------------------------------------
// `_size == 0`  -> the cached text becomes the System::String that
//   `UI::String::operator System::String` returns. That operator is
//   `StringTable::GetText(id)` (0x180002550) under an eight-instruction wrapper:
//   it hands back a refcounted COPY OF THE TABLE'S OWN BUFFER, so ours and
//   retail produce the IDENTICAL POINTER. This branch is exactly comparable.
//
// `_size != 0`  -> `String::FormatParamList` builds a FRESH allocation. Ours and
//   retail allocate different blocks, so the pointer at +0xF8 is NOT comparable.
//
// ENDGAME B.3.6 block C3: both bodies are `UI::TextElement` members now. The
// RtSink indirection is gone with the predict mode it served (the sink only
// ever ran live); the one store to `_cachedText` is a plain assignment.
// `UI::String::operator System::String` is a direct call (block D's value
// type), no longer a name lookup.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "element_common.h"   // MemoryAllocator
#include "system/String.h"
#include "ui/TextElement.h"
#include "ui/String.h"
#include "../collection/array_internal.h" // ENDGAME B.3.2 step 2 — kSpec_StringRefZ

namespace {

using Str = System::String;


// ?MemFree@MemoryAllocator@System@@SAXPEAX@Z (0x1800c7ad0) — PORTED, Wave 2.
// Called by name (B3R RT-UI).
inline void MemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// 0x180029a80: `Array<System::String>::SetSize`, `kSpec_StringRefZ` —
// NON-TRIVIAL (the tail is ZEROED, and the destroy releases each String), so
// the call bridges through the hand-derived row.
inline void ArrayStringSetSize(void* arrayHdr, int32_t n) {
    rtcoll::AsArray<&rtcoll::kSpec_StringRefZ>(arrayHdr)->SetSize(n);
}

}  // namespace

namespace UI {

// ============================================================================
// ?SetParameterCount@TextElement@UI@@AEAAXXZ — retail 0x18011e8e0
// ============================================================================
//
// Resize the parameter Array to however many "@n" slots the current format
// string contains. Straight-line; the only subtlety is that retail takes a
// SECOND reference on the same buffer (RBX at 0x18011e91a-0x18011e922) before
// calling FormatParamCount and releases it afterwards, which is the compiler's
// materialisation of passing the String by value. It is reproduced because the
// refcount traffic is observable, not because the count depends on it.
//
// IT IS IDEMPOTENT: a second call computes the same n and 0x180029a80's first
// instruction pair (`CMP EDI,ESI; JZ`) returns immediately.
void TextElement::SetParameterCount() {
    Str tmp = _text;                                          // 0x18011e903 operator System::String

    char* held = nullptr;                                     // RBX
    if (tmp._data) { held = tmp._data; ++RtSystem::StringRefCount(tmp._data); }  // 0x18011e91a

    const int n = System::String::FormatParamCount(held);     // 0x18011e928
    ArrayStringSetSize(&_params, n);                          // 0x18011e936

    if (held) {                                               // 0x18011e93c
        if (--RtSystem::StringRefCount(held) == 0) MemFree(held - 8);
    }
    // 0x18011e962 — `tmp`'s destructor, at the end of the body, is retail's
    // release of the same slot.
}

// ============================================================================
// ?CacheText@TextElement@UI@@AEAAXXZ — retail 0x18011ec10
// ============================================================================
// `_params._size == 0` -> the cached text becomes the System::String that
//   `UI::String::operator System::String` returns (a refcounted COPY of the
//   table's own buffer).
// `_params._size != 0` -> `String::FormatParamList` builds a FRESH allocation.
void TextElement::CacheText() {
    SetParameterCount();                                      // 0x18011ec29

    Str tmp = _text;                                          // 0x18011ec42 / ec9c

    Str result;                                               // [RBP+0x38]
    Str param0;                                               // [RBP+0x40]
    const Str* src = nullptr;                                 // the String whose buffer is adopted

    // The branch selector is the parameter Array's _size, which the call above
    // has just set. Retail reads it at 0x18011ec39 — BEFORE the operator call,
    // which is immaterial (the operator does not touch `this`).
    if (_params._size == 0) {
        src = &tmp;
    } else {
        if (tmp._data) { param0._data = tmp._data; ++RtSystem::StringRefCount(tmp._data); }  // 0x18011ecb3
        // The params pointer is the parameter Array's DATA pointer, not the
        // Array header — Ghidra's decompiled C drops this third argument
        // (reference_ghidra_c_output_vs_disassembly.md).
        result = System::String::FormatParamList(param0, _params._data);
        src = &result;                                        // 0x18011ecd1: R14 = &result
    }

    // `String::operator=` (retail 0x1800c9810), inlined at 0x18011ec55 /
    // 0x18011ecdb: the guard on the buffer, the release that stores `data - 8`
    // back into the member before freeing (`ADD qword ptr [RDI+0xf8],-0x8`) or
    // nulls it, then the adopt and AddRef, from whichever String the branch above
    // chose.
    _cachedText = *src;

    // Retail releases `result` and `param0` here (0x18011ed1d, 0x18011ed46) and
    // `tmp` last (0x18011ed66) — which is exactly reverse declaration order,
    // so the three destructors at the end of this scope are that sequence.
}

}  // namespace UI
