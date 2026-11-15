// ============================================================================
// src/engine/object/external_path_stack.cpp — Wave 11 lane OBJEXT
//   `Object::ExternalFactory::PushPath` / `PopPath`, the two WRITERS of the
//   path stack that Wave 9 lane RRES ported the READERS of.
// ============================================================================
//
//   ?PushPath@ExternalFactory@Object@@QEAAXAEBVFilename@System@@@Z  0x1800bd0e0
//   ?PopPath@ExternalFactory@Object@@QEAAXXZ                        0x1800bd040
//
// plus the four `Collection::Array<PathStackEntry>` bodies MSVC emitted for
// this one instantiation, which are what lane RRES named as its blocker:
//
//   0x18002e220  Array<PathStackEntry>::SetSize(n, const T& fill)
//   0x18002e8d0  elem-destroy(range)   — two Filenames per element
//   0x18002ccb0  RemoveLast(&out)      — copy the tail out, then RemoveAt
//   0x18002de30  RemoveAt(index)       — release, shift down, --size
//
// WHY THIS FILE EXISTS
// --------------------
// `src/engine/object/external_path.h` states the deferral verbatim:
//
//     `PushPath` (0x1800bd0e0) and `PopPath` (0x1800bd040) are the WRITERS of
//     the path stack this file reads, and they are NOT ported. They need
//     `0x18002e220` — `Collection::Array<PathStackEntry>::Resize(n, const T&)`,
//     the 0x10-element instantiation, which exists nowhere in the tree — plus
//     its element copy-ctor and destroy-range.
//
// That blocker is discharged here, and NOT by writing a fourth copy of
// `SetSize`: Wave 9 lane Q already ported that body once, parameterised by
// `rtcoll::ArraySpec` (src/engine/collection/array_internal.h), and
// `src/engine/engine/mainloop_video.cpp` already sets the precedent of
// defining a spec OUTSIDE array.cpp for an instantiation that belongs to
// another lane's subject (`kSpec_VideoResource`). `kSpec_PathStack` below is
// that same move. `SetSizeCore` was read against 0x18002e220's disassembly
// arm by arm before it was reused — the `cap = n*3` growth rule when
// `n == old+1 && old != 0`, the `MemAlloc(cap*stride, align)` with align 8,
// the realloc arm's `destroy(old, oldSize)` before `MemFree`, and the
// non-realloc shrink arm's `destroy(old + newSize*stride, oldSize - newSize)`
// — all four agree. The two `Remove*` bodies are NOT SetSize and are written
// out here in full.
//
// A separate translation unit from `external_path.cpp`. Nothing here
// needs <windows.h>, so this is the portable half and carries no `_win`.
//
// EVIDENCE
// --------
// Every body was transcribed from `disassemble_function(address=)` on
// Runtime-x64.dll (image base 0x180000000, read 2026-09-09), not from the
// decompiled C. Ghidra's C for `PushPath` is misleading in the usual way
// (reference_ghidra_c_output_vs_disassembly.md): it DROPS the right-hand
// operand of both `System::operator+` calls, so the two string literals the
// entry filename is built from — "." at 0x180155814 — are invisible in it, and
// it reports four "unreachable" blocks that are the ordinary release idiom.
//
// The two literals are read out of the image:
//   0x180155810 = "_"   0x180155814 = "."
// PushPath uses only the second.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <new>          // placement new into the retail raw element slots

#include "external_path.h"
#include "external_path_stack.h"

#include "system/String.h"
#include "system/string_internal.h"
#include "../system/setupdir_helpers.h"
#include "../collection/array_internal.h"

namespace RtObject {


namespace {

// 0x180155814.
const char* const kDot = ".";

}  // namespace


PathStackCounters g_psc = {};


// ============================================================================
// ?PushPath@ExternalFactory@Object@@QEAAXAEBVFilename@System@@@Z   0x1800bd0e0
// ============================================================================
// RCX = this, RDX = path. Retail, in listing order:
//
//   0x1800bd108  ext   = path.GetPathExtension()                 [RBP-0x30]
//   0x1800bd11a  fname = path.GetPathFilename(stripExtension=1)  [RBP-0x38]
//   0x1800bd12e  t1    = fname + "."                             [RBP-0x40]
//   0x1800bd13e  t2    = t1 + ext                                [RBP+0x38]
//   0x1800bd146  file  = adopt(t2); AdjustDelimiters(file)       [RBP+0x20]
//   0x1800bd16a  dir   = path.GetPathDirectory()                 [RBP+0x30]
//   0x1800bd177  entry.directory = Filename(dir)                 [RBP-0x18]
//   0x1800bd1bf  entry.filename  = release-then-adopt(file)      [RBP-0x10]
//   0x1800bd1cf  AdjustDelimiters(entry.filename)   TWICE
//   0x1800bd1de  SetSize(&this->_pathStack, size + 1, &entry)
//
// THE DOUBLE AdjustDelimiters IS RETAIL'S, NOT A TRANSCRIPTION SLIP. Two
// separate `LEA RCX,[RBP-0x10] ; CALL 0x1800c6970` at 0x1800bd1cb and
// 0x1800bd1d4. It is idempotent, so it has no observable effect — but it is a
// second pass over the buffer and it is what the bytes say, so it is kept.
// (`ExpandRelativePath` carries the same doubled call, which is corroboration
// that this is a source-level idiom of retail's and not a one-off.)
//
// `ext` is used, unlike in GetCompiledPath where the same call's result is
// computed and DROPPED — see external_compiled.cpp.
void RtObject_ExternalFactory_PushPath(ExternalFactory* self, const System::Filename* path) {
    ++g_psc.pushCalls;

    const System::Filename& in = *path;

    System::Filename ext   = in.GetPathExtension();              // [RBP-0x30]
    System::Filename fname = in.GetPathFilename(true);           // [RBP-0x38]

    System::String t1 = fname.AsString() + kDot;                 // [RBP-0x40]
    System::String t2 = t1 + ext.AsString();                     // [RBP+0x38]

    // 0x1800bd14a-0x1800bd15d: adopt t2's buffer, then normalise ONCE. That is
    // `Filename::Filename(const String&)`, 0x18000c780.
    System::Filename file(t2);                                   // [RBP+0x20]

    System::Filename dir = in.GetPathDirectory();                // [RBP+0x30]

    // The 0x10-byte local at [RBP-0x18]: two ADJACENT stack slots, which is
    // the WRITE-side pin for the PathStackEntry field order that
    // src/engine/object/External.h records. `PathStackEntry` is a
    // not-yet-converted struct, so its two members keep the POD type
    // (B31_RULES) and both halves are constructed and destroyed BY HAND, in
    // retail's own order — that is what retail's own code does here too.
    // 0x1800bd177 — the copy ctor into the directory slot; 0x1800bd185-
    // 0x1800bd1d4 — null the filename slot, release-then-adopt, then
    // AdjustDelimiters TWICE (see the banner: it really is twice). Null-init
    // plus that sequence IS the copy ctor 0x1800067d0, whose ported body
    // normalises twice for exactly this reason. Both are PathStackEntry's
    // constructor (B.4 Z MIRROR: the halves are the real Filename).
    PathStackEntry entry(dir, file);

    RtObject_PathStackSetSize_18002e220(&self->_pathStack,
                                        self->_pathStack._size + 1, &entry);
    g_psc.depthAfterPush = self->_pathStack._size;

    // Retail's release order is reversed construction order: ~PathStackEntry
    // (filename, then directory) first, then the six locals before it — dir,
    // file, t2, t1, fname, ext — in reverse declaration order.

}

// ============================================================================
// ?PopPath@ExternalFactory@Object@@QEAAXXZ                        0x1800bd040
// ============================================================================
// RCX = this. Retail:
//
//   0x1800bd04d  RemoveLast(&this->_pathStack, &out)   out at [RSP+0x28]
//   0x1800bd066  release(out.filename)                      [RSP+0x30]
//   0x1800bd0??  release(out.directory)                     [RSP+0x28]
//
// The entry is copied OUT and then immediately thrown away, which looks
// pointless and is not: `RemoveAt` releases the array's own reference, so
// without the copy the last element's two buffers would be freed while
// `RemoveLast`'s caller still held nothing at all. It is retail's `T tmp =
// arr.RemoveLast();` with an unused `tmp`, and the refcount traffic is
// observable.
//
// NOTE THE EMPTY-STACK BEHAVIOUR IS FAITHFULLY UNGUARDED. On a zero-deep stack
// `idx` is -1 and retail reads `_data[-1]`. We do not "fix" that
// (CLAUDE.md, "Don't 'fix' a faithful crash with a null check"): every caller
// in the tree pops only what it pushed, and a guard here would hide the
// upstream imbalance rather than the crash.
void RtObject_ExternalFactory_PopPath(ExternalFactory* self) {
    ++g_psc.popCalls;

    // `out` is the raw 0x10-byte slot RemoveLast copy-CONSTRUCTS into (retail's
    // by-value return), and the pair of releases is ~PathStackEntry
    // (~Filename 0x180006980 on +0x08, then +0x00).
    alignas(PathStackEntry) unsigned char slot[sizeof(PathStackEntry)];   // [RSP+0x28]
    PathStackEntry* out = RtObject_PathStackRemoveLast_18002ccb0(&self->_pathStack, slot);
    out->~PathStackEntry();                                      // 0x1800bd066
    g_psc.depthAfterPop = self->_pathStack._size;

}

}  // namespace RtObject

// ============================================================================
// THE FOUR CONTAINER BODIES — GLOBAL SCOPE, ON PURPOSE
// ============================================================================
// They are `extern "C"` and external_path_stack.h declares them at global
// scope. Defining them inside `namespace RtObject` as well would give one
// C-linkage entity two declarations in two scopes; GCC then rejects the
// `&RtObject_PathStackDestroyRange_18002e8d0` in `kSpec_PathStack` as "address of overloaded
// function with no contextual type information". And `rtcoll` must be the REAL
// `rtcoll` — nested inside `RtObject` it is a different namespace with no
// `ArraySpec` and no `SetSizeCore` in it.
//
// Everything they touch is therefore spelled out. No `using` at global scope:
// src/engine/common.h forward-declares a GLOBAL struct called `Filename`,
// so an unqualified use here is AMBIGUOUS rather than wrong
// (src/engine/engine/mainloop_video.cpp:255 records the same trap).
// ============================================================================

// ============================================================================
// 0x18002e8d0 — elem-destroy(range) for PathStackEntry
// ============================================================================
// Signature read off the listing, and it is the SAME not-the-obvious-one shape
// `rtcoll::ElemDestroy` already describes: RCX is stored to its home slot and
// never read, so the function takes (ignored, T* base, int32 count). Retail
// releases the SECOND Filename (+0x08, the filename) BEFORE the first (+0x00,
// the directory); the order is observable whenever the two share a buffer.
//
// It IS `kSpec_PathStack`'s destroy arm. factory_dtors.cpp's `PathStackDestroyRange`,
// which `PathStackArrayDtor` runs, is a second body for the same address.
extern "C" void RtObject_PathStackDestroyRange_18002e8d0(void* ignoredThis,
                                                          uint8_t* base, int count) {
    (void)ignoredThis;
    // The erased core's element bytes, handed to the element type.
    RtObject::PathStackEntry* const elems =
        reinterpret_cast<RtObject::PathStackEntry*>(base);  // CAST-OK: elem_stride
    for (int i = 0; i < count; ++i) {
        RtObject::PathStackEntry& e = elems[i];
        ++RtObject::g_psc.elemsDestroyed;
        // ENDGAME B.3.1 step 2: retail's inlined release IS ~Filename
        // (0x180006980), +0x08 before +0x00.
        e.filename.~Filename();
        e.directory.~Filename();
    }
}

namespace rtcoll {

// The element assignment retail's SetSize uses is a placement copy-CONSTRUCT of
// both halves (`Filename::Filename(dst, src)` twice, 0x1800067d0), not a
// release-then-adopt: the destination is raw MemAlloc memory on the realloc arm
// and freshly-vacated-by-destroy memory nowhere. It must NOT release `dst`.
static void AssignPathStackEntry(uint8_t* dst, const uint8_t* src) {
    RtObject::PathStackEntry* const d =
        reinterpret_cast<RtObject::PathStackEntry*>(dst);         // CAST-OK: elem_stride
    const RtObject::PathStackEntry* const s =
        reinterpret_cast<const RtObject::PathStackEntry*>(src);   // CAST-OK: elem_stride
    new (&d->directory) System::Filename(s->directory);
    new (&d->filename)  System::Filename(s->filename);
}

// stride 0x10, align 8 — the `SHL RDX,0x4` and the `MOV EDX,0x8` at
// 0x18002e220's MemAlloc call site.
//
// byteCopy=false: the assignment bumps two refcounts, so SetSizeCore's
// "copied prefix equals the old buffer" check would be comparing pointers it
// has legitimately re-owned. assign covers the whole stride (both halves are
// written), so there is no unwritten window on either arm; `init` is null
// because retail has no fill-less arm here — PushPath always passes an entry.
// seedZero is the WHOLE element: every byte of it is a `char*` that the
// element ops dereference at `p[-1]` to bump a refcount, so the probe must
// never seed garbage into either half.
const ArraySpec kSpec_PathStack = {                // 0x18002e220
    "Array<Object::PathStackEntry:0x10>", 0x10, 8,
    &AssignPathStackEntry, nullptr, &RtObject_PathStackDestroyRange_18002e8d0,
    /*byteCopy        */ false,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0x00, 0x10 };

}  // namespace rtcoll

// ============================================================================
// 0x18002e220 — Array<PathStackEntry>::SetSize(n, const T& fill)
// ============================================================================
extern "C" void RtObject_PathStackSetSize_18002e220(void* a, int n, const void* fill) {
    ++RtObject::g_psc.setSizeCalls;
    // NO ctl==4 BRANCH HERE. There used to be one, passing
    // `nullptr` as the fill, and retiring it is what this
    // line records. Control 4's meaning was changed to
    // "grow by two" in the probe and in two comment blocks
    // and NOT here, so the control had two live
    // implementations and ran both: it grew by two AND left
    // the tail uninitialised, which is the very corruption
    // the rewrite was for. It took the run down in
    // ucrtbase reading a garbage `char*`.
    // See WAVE11_LANE_OBJEXT.md §10.
    // ENDGAME B.3.2 step 2 — the raw `rtcoll::SetSizeCore` call is now the
    // `Collection::Array<uint8_t, …, ExplicitSpec<&kSpec_PathStack>>` member
    // it is byte-identical to; `a` is a `void*` handed in by every caller
    // (there is no typed `PathStackEntry` array member to bridge from instead).
    rtcoll::AsArray<&rtcoll::kSpec_PathStack>(a)->SetSizeFill(n, fill);
}

// ============================================================================
// 0x18002de30 — Array<PathStackEntry>::RemoveAt(index)
// ============================================================================
// NOT a SetSize. Retail:
//
//   if (index < size) {
//       i = index;
//       do {
//           release(e[i].filename); release(e[i].directory);
//           next = i + 1;
//           if (next < size) { copy-construct e[i] from e[next]; }
//           i = next;
//       } while (i < size);
//   }
//   --size;                      // BOTH arms, hence unconditional here
//
// Two details that a paraphrase loses. (1) The `--size` sits in both the taken
// and the not-taken arm of the outer `if`, so a `RemoveAt` past the end still
// decrements — faithful, and load-bearing for `RemoveLast` on an empty stack.
// (2) The shift is a copy-CONSTRUCT into memory the two releases immediately
// above just vacated, not an assignment; using `ReleaseThenAdopt` here would
// double-release.
extern "C" void RtObject_PathStackRemoveAt_18002de30(
        RtObject::PathStackArray* a, int index) {
    ++RtObject::g_psc.removeAtCalls;

    if (index < a->_size) {
        int32_t i = index;
        do {
            // `_data` is re-read at each of the three uses, as retail and the
            // tree before B.4 did (the destructors in between are calls).
            RtObject::PathStackEntry* e = a->_data + i;
            e->filename.~Filename();
            e->directory.~Filename();

            const int32_t next = i + 1;
            if (next < a->_size) {
                RtObject::PathStackEntry* dst = a->_data + i;
                // Retail tests the COMPUTED address, which can only be null
                // when _data is null and i == 0.
                if (dst != nullptr) {
                    const RtObject::PathStackEntry* src = a->_data + next;
                    ++RtObject::g_psc.elemsShifted;
                    new (&dst->directory) System::Filename(src->directory);
                    new (&dst->filename)  System::Filename(src->filename);
                }
            }
            i = next;
        } while (i < a->_size);
        ++RtObject::g_psc.removeAtInRange;
    } else {
        ++RtObject::g_psc.removeAtPastEnd;
    }
    a->_size = a->_size - 1;
}

// ============================================================================
// 0x18002ccb0 — Array<PathStackEntry>::RemoveLast(&out)
// ============================================================================
// Copy-constructs the tail element into the caller's raw 0x10-byte slot, then
// RemoveAt(size - 1). Returns the slot in RAX, as every by-value-out helper in
// this DLL does.
extern "C" RtObject::PathStackEntry* RtObject_PathStackRemoveLast_18002ccb0(
        RtObject::PathStackArray* a, void* out) {
    ++RtObject::g_psc.removeLastCalls;

    const int32_t idx = a->_size - 1;
    const RtObject::PathStackEntry* e = a->_data + idx;
    // The two copy ctors (0x1800067d0), directory then filename, into the
    // caller's raw slot: PathStackEntry's constructor.
    RtObject::PathStackEntry* const o = new (out) RtObject::PathStackEntry(e->directory, e->filename);
    RtObject_PathStackRemoveAt_18002de30(a, idx);
    return o;
}

