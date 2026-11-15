// ============================================================================
// src/engine/system/closure_batch2b.cpp — cutover closure batch 2b
// ============================================================================
//
// The System-namespace half of one batch off docs/history/runtime_port/closure/
// WORKLIST.md (2026-09-12).
//
//   0x1800028a0  ??3MaterialInstance@Graphics@@SAXPEAX@Z   93 B  operator delete
//   0x18001e990  ??3ImageBuffer@Video@@SAXPEAX@Z           93 B  operator delete
//   0x1800251e0  ??3PackageFile@IO@@SAXPEAX@Z              93 B  operator delete
//   0x180061ae0  (unnamed)                                 93 B  operator delete
//   0x180011640  (unnamed)                                 99 B  operator new
//   0x180034d60  (unnamed)                                 99 B  Allocate's block link
//   0x1800063c0  ?GetLength@String@System@@QEBAHXZ         14 B  (body already ported)
//
// ---------------------------------------------------------------------------
// THE FOUR `operator delete` ROWS ARE NOT DELETING DESTRUCTORS
// ---------------------------------------------------------------------------
// The batch brief filed them as deleting-dtor rows, and they are not. A
// deleting destructor is
// `??_E<Class>@@UEAAPEAXI@Z` — two arguments, a vector branch, an array cookie
// eight bytes behind the pointer. These four are `??3<Class>@<Ns>@@SAXPEAX@Z`,
// MSVC's per-class `operator delete`: ONE argument, no dtor, no cookie.
//
// All four were decompiled individually and all four are, instruction for
// instruction, the same body:
//
//     if (p) { EnterCriticalSection(_singleton+0x640);
//              MemoryAllocator::Free(_singleton, p);
//              LeaveCriticalSection(_singleton+0x640); }
//
// which is byte-identical to the EXPORTED ?MemFree@MemoryAllocator@System@@
// (0x1800c7ad0).
//
// The allocator is reached through its two statics, called by name:
// System::MemoryAllocator::MemAlloc / MemFree (retail 0x1800c7a50 / 0x1800c7ad0),
// which keeps <windows.h> out of this file (docs/runtime_port/PLAN.md §4.1).
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "system/string_internal.h"
#include "system/MemoryAllocator.h"

namespace rtclosure2b {

// ---- the allocator boundary (B3R W0-A0: the statics, called directly; they
// were resolved by address, 0x1800c7a50 / 0x1800c7ad0, bound to these two) ---
void* MemAllocAt(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}
void MemFreeAt(void* p) {
    System::MemoryAllocator::MemFree(p);     // 0x1800c7ad0 re-tests null itself
}

}  // namespace rtclosure2b

// ---------------------------------------------------------------------------
// Two of the `operator delete` COMDAT copies. ONE body; an entry point per copy
// so a crash log names the address that was actually reached.
//
// The `if (p)` here is RETAIL'S OWN test, not a port's guard: every one of the
// four opens `TEST RCX,RCX / JZ ret`. MemFree tests again inside; both tests
// are retail's and both are kept.
// ---------------------------------------------------------------------------
extern "C" void RtIo_PackageFile_OperatorDelete(void* p) {
    if (p) rtclosure2b::MemFreeAt(p);
}
// 0x180061ae0 — the same body with no exported name in either build. It sits in
// Graphics' 0x18006xxxx block, next to Material's array helpers, i.e. the
// Material translation unit's own copy.
extern "C" void RtGraphics_Material_OperatorDelete(void* p) {
    if (p) rtclosure2b::MemFreeAt(p);
}

// ---------------------------------------------------------------------------
// 0x180011640 — the matching `operator new` COMDAT copy: `MemAlloc(size, 4)`
// with the Enter/Allocate/Leave triple inlined and NO null test (there is
// nothing to test; the size is the argument). ALIGNMENT IS 4, read from its own
// `MOV R8D,0x4` — not 8, and not inherited from the exported MemAlloc's
// callers. engine_lifecycle.cpp records this address as `MemAlloc(size, 4)` and
// nothing else.
// ---------------------------------------------------------------------------
extern "C" void* RtSystem_OperatorNew_180011640(uint64_t size) {
    return rtclosure2b::MemAllocAt(size, 4);
}

// ---------------------------------------------------------------------------
// 0x180034d60 — `System::MemoryAllocator::Allocate`'s block-link encoder
// (its only caller is 0x1800c6fe5, inside Allocate).
//
// Read from the DISASSEMBLY, not the decompiled C: Ghidra prints the `SAR
// RAX,0x2` as a plain shift and the two 43-bit masks as ordinary constants, and
// this is a bitfield pack where a sign-extending shift and an unsigned one give
// different answers (reference_ghidra_c_output_vs_disassembly.md).
//
//   180034d60  MOV RAX,0x100000000000
//   180034d6a  MOV R9,0x1ffffffffff
//   180034d74  OR  [RCX],RAX              ; set bit 44 in this block's header
//   180034d77  MOV RAX,[RDX]              ; the PREVIOUS block's header
//   180034d7a  MOV R8,[RCX]               ; re-read ours, AFTER the OR
//   180034d7d  AND RAX,R9                 ; prev's low 41 bits = its size
//   180034d80  CMP RAX,0x4
//   180034d87  JA  far                    ; UNSIGNED compare
//   ; near form — the distance fits in three bits at 41..43
//   180034d89  SUB RAX,RDX                ; RAX was RCX: delta = this - prev
//   180034d96  SAR RAX,0x2                ; ARITHMETIC shift
//   180034d9a  SHL RAX,0x29               ; into bits 41..
//   180034d9e  XOR RAX,R8
//   180034da1  AND RAX,0xe0000000000      ; keep only bits 41..43 of the change
//   180034da4  XOR RAX,R8                 ; ... and splice them into the header
//   180034da7  MOV [RCX],RAX
//   ; far form — the distance goes in the qword BEFORE the header
//   180034dab  SUB RAX,RDX
//   180034dae  MOV [RCX-0x8],RAX
//   180034db2  MOV RAX,-0xe0000000001     ; == ~0xe0000000000
//   180034dbf  MOV [RCX],R8 & that mask   ; clear bits 41..43
//
// Nothing in the tree calls it: the ported Allocate in allocator.cpp uses its
// own SetPrevTagInfo (FUN_180034d60).
// ---------------------------------------------------------------------------
extern "C" void RtSystem_MemoryAllocator_LinkPrevBlock(uint64_t* header,
                                                       const uint64_t* prev) {
    *header |= rt::system::kTagPrevFree;
    const uint64_t prevSize = *prev & rt::system::kTagSizeMask;
    const uint64_t hdr = *header;                       // re-read AFTER the OR
    if (prevSize > 4ull) {                            // JA — unsigned
        const int64_t delta = reinterpret_cast<const uint8_t*>(header) -  // CAST-OK: ptr_int
                              reinterpret_cast<const uint8_t*>(prev);  // CAST-OK: ptr_int
        header[-1] = static_cast<uint64_t>(delta);
        *header = hdr & ~rt::system::kTagPrevSizeMask;
        return;
    }
    const int64_t delta = reinterpret_cast<const uint8_t*>(header) -  // CAST-OK: ptr_int
                          reinterpret_cast<const uint8_t*>(prev);  // CAST-OK: ptr_int
    const uint64_t packed = static_cast<uint64_t>(delta >> 2) << rt::system::kTagPrevSizeShift;  // SAR then SHL
    *header = ((packed ^ hdr) & rt::system::kTagPrevSizeMask) ^ hdr;
}
