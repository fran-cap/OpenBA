// ============================================================================
// src/engine/save/savedstate_setupload.cpp — Wave 14 lane SETUPLOAD
//
//   ?SetupLoad@SavedState@Save@@QEAA?AUSaveBlock@Persistence@IO@@XZ
//   retail Runtime-x64.dll 0x1800c0c60, ordinal 4031
//
// One of the last seven unported rows in the Save namespace. Its close sibling
// ?GetSaveBlock@SavedState@Save@@QEBA?AUSaveBlock@Persistence@IO@@XZ
// (0x1800c0d50) is READ here as a cross-reference and deliberately NOT ported:
// its eight instructions are byte for byte SetupLoad's tail, which is what
// pins the SaveBlock field order independently of UpdateSave.
//
// ----------------------------------------------------------------------------
// THE ABI, AND A CORRECTION TO THE LANE BRIEF
// ----------------------------------------------------------------------------
// The mangled return type is `?AUSaveBlock@Persistence@IO@@` — a struct BY
// VALUE, 0x10 bytes, so it is memory-returned and there is a hidden return
// slot. The lane brief said the real shape is
// `void f(SaveBlock* retslot, SavedState* self)`, i.e. the slot FIRST. That is
// the rule for a static/free function. For a NON-STATIC MEMBER MSVC keeps
// `this` in RCX and puts the hidden slot in RDX, and the disassembly says so
// with no ambiguity:
//
//   1800c0c7e  MOV R14,RDX          ; R14 = the slot; later written at +0/+8/+0xc
//   1800c0c81  MOV RSI,RCX          ; RSI = this;  read at [RSI] (_id), [RSI+8]
//   1800c0c84  MOV RCX,[RCX+0x8]    ;              (_data), written at [RSI+4]
//
// and the CALLER agrees (UpdateList 0x1800c221f: `ADD RCX,0x28` = the embedded
// SavedState, `LEA RDX,[RBP-0x61]` = the block). `tools/abi_retslot_audit.py`'s
// own docstring states the same rule ("RDX for a member, after `this` in RCX"),
// and `saveload_update.cpp`'s existing `SetupLoadFn` typedef already had it
// right. So: `SaveBlock* SetupLoad(SavedState* self, SaveBlock* ret)`, RAX =
// ret. Getting this backwards would have written 16 bytes through a stale RCX.
//
// ----------------------------------------------------------------------------
// WHAT THE BODY DOES — transcribed instruction for instruction
// ----------------------------------------------------------------------------
//   1. `if (this->_data) MemFree(this->_data);`
//      0x1800c0c84..0x1800c0c8d. The `TEST RCX,RCX / JZ` is REDUNDANT with the
//      callee's own first instruction (0x1800c7ad0 opens with `TEST RCX,RCX`),
//      and it is transcribed anyway because it is what the bytes do.
//      NOTE: _data is NOT nulled here. It is overwritten at step 4.
//
//   2. Look the save's byte count up in Persistence's SLOT TABLE.
//      0x1800c0c92..0x1800c0cd4. Reads the Persistence singleton at
//      *0x1801fc6b8, and:
//        * bails to size 0 when `persistence[0x18] & 1` is set — the same
//          "shutting down / no persistence" bit UpdateList, UpdateSave and
//          IO::Persistence::BeginLoad (0x1800b7992) all test first;
//        * otherwise walks `count = *(int32*)(persistence+0x68)` entries of
//          **12 bytes** starting at `*(void**)(persistence+0x60)`, matching
//          `entry.id == this->_id`, and takes `entry[+8]` as the size.
//        * NOT FOUND (or count <= 0) leaves the size at ZERO. There is no
//          error path and no assert: a SavedState whose id the table does not
//          carry gets a zero-byte buffer.
//      The 12-byte stride is not an inference: `ADD RCX,0xc` walks the keys and
//      `MOV EDX,[R10 + RCX*0x4 + 0x8]` with RCX = 3*index reads 12*index + 8.
//      The middle dword (+0x4) is never read by this function.
//
//      WHO WRITES THAT TABLE IS NOT ESTABLISHED BY THIS LANE, and the obvious
//      candidate is wrong: `IO::Persistence::BeginLoad` (0x1800b7970) calls
//      FUN_1800b7460 with `LEA RCX,[RBX+0x10]`, so the array FUN_1800b7460
//      clears at its own +0x60/+0x68 with an 8-byte stride is Persistence+0x70,
//      a DIFFERENT array. Do not read the two as one.
//
//   3. `this->_size = size;`  (0x1800c0cd4)
//
//   4. `this->_data = MemAlloc(size, 4);`  (0x1800c0cd9..0x1800c0d16)
//      Open-coded, not a call to the export: `EnterCriticalSection(
//      *0x1801fc718 + 0x640); Allocate(*0x1801fc718, size, 4);
//      LeaveCriticalSection(...)`. That triple IS
//      ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z at 0x1800c7a50 — compare
//      0x1800c7a6e..0x1800c7aa3, same four operands in the same order — so the
//      port goes through the one allocator door the rest of the tree uses
//      instead of pinning 0x1800c6e60 and the +0x640 offset a second time.
//      The allocation happens EVEN WHEN size IS ZERO. Faithful; do not "fix".
//
//   5. Fill the return slot: `ret->data = this->_data; ret->size = this->_size;
//      ret->id = this->_id;` (0x1800c0d1a..0x1800c0d26) and return it in RAX.
//      Note retail RE-READS `this->_size` from memory at 0x1800c0d1d rather
//      than reusing EBX, which is why the port reads it back through the sink.
//
// ----------------------------------------------------------------------------
// VERIFICATION — TWO SCREENS, SCORED SEPARATELY
// ----------------------------------------------------------------------------
// The body below was written against the live sink (retired, B.4 R-IO).
// Ghidra lists three callers (UpdateList 0x1800c2227, UpdateLoadFromFile
// 0x1800c25d4, UpdateLoad 0x1800c29a6); our own ported copies of those three
// reach us by name through RT_EXPORT_ONCE.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "saveload_internal.h"
#include "io/Persistence.h"

using RtSave::SavedState;
using RtIo::SaveBlock;
using RtSaveDetail::MemAlloc;
using RtSaveDetail::MemFree;
using RtSaveDetail::PersistenceSingleton;

namespace {

using MemAllocFn = void* (*)(uint64_t, int);
using MemFreeFn  = void  (*)(void*);
// NOT minted here. `RtSaveDetail::SetupLoadFn` (saveload_internal.h) is the one
// definition of this shape, hoisted out of saveload_update.cpp's anonymous
// namespace by this lane precisely so that the port and its three callers
// cannot end up with two spellings of an ABI whose drift mode is a wild write.
using RtSaveDetail::SetupLoadFn;

// The 12-byte row SetupLoad walks at Persistence+0x60, count at +0x68, is
// `RtIo::BlockDesc` (Persistence.h); +0x4 is never read here.

// ---------------------------------------------------------------------------
// Step 2 on its own, because both the body and screen A's driver need it and a
// second transcription is a second thing to drift.
//
// `found` is an OUT parameter and not a sentinel: a row whose size is genuinely
// 0 and a missing row both yield 0, and the driver has to be able to tell them
// apart to choose its absent-id probes.
// ---------------------------------------------------------------------------
uint32_t LookupSlotSize(const ::IO::Persistence* persistence, uint32_t id, bool* found) {
    if (found) *found = false;
    if (persistence == nullptr) return 0;
    if (persistence->_worker.IsWorking()) return 0;                        // 0x1800c0c9b
    const int32_t count = persistence->_worker._blockDescs._size;
    if (count <= 0) return 0;                                    // 0x1800c0ca8
    const RtIo::BlockDesc* table = persistence->_worker._blockDescs._data;
    for (int32_t i = 0; i < count; ++i) {                        // 0x1800c0cb7
        if (table[i].id == id) {
            if (found) *found = true;
            return table[i].size;                                // 0x1800c0ccf
        }
    }
    return 0;                                                    // EDX stays 0
}

}  // namespace

// ============================================================================
// The body (plain stores: the live sink is retired, B.4 R-IO).
// ============================================================================
// ENDGAME B.3.7 B2: the body is the member. `ret` is the member's own return
// value (NRVO), so the five stores into the SaveBlock land where retail's do:
// in the caller's hidden slot, via the door below.
SaveBlock Save::SavedState::SetupLoad() {
    SaveBlock result;

    // ---- 1. free the previous buffer ---------------------------------------
    void* old = _data;
    if (old != nullptr) MemFree(old);

    // ---- 2. the slot-table lookup ------------------------------------------
    const ::IO::Persistence* persistence = PersistenceSingleton();
    const uint32_t size = LookupSlotSize(persistence, _id, nullptr);

    // ---- 3. publish the size (0 when the id is absent) ---------------------
    _size = size;

    // ---- 4. the fresh buffer ------------------------------------------------
    void* buf = MemAlloc(size, 4);
    _data = buf;

    // ---- 5. fill the return slot -------------------------------------------
    // Retail re-reads _size and _id from `this` here rather than reusing the
    // registers it already has.
    result.data = buf;
    result.size = _size;
    result.id   = _id;
    return result;
}

// ?GetSaveBlock@SavedState@Save@@QEBA?AUSaveBlock@Persistence@IO@@XZ
// 0x1800c0d50, read whole: `MOV RAX,[RCX+8] ; MOV [RDX],RAX ;
// MOV EAX,[RCX+4] ; MOV [RDX+8],EAX ; MOV EAX,[RCX] ; MOV [RDX+0xc],EAX ;
// MOV RAX,RDX ; RET`. Defined, not registered (no caller in the tree).
SaveBlock Save::SavedState::GetSaveBlock() const {
    SaveBlock result;
    result.data = _data;
    result.size = _size;
    result.id   = _id;
    return result;
}

// ?Discard@SavedState@Save@@QEAAXXZ 0x1800c0d70, read whole:
// `MOV RCX,[RCX+8] ; TEST ; JZ ; CALL MemFree ; XOR EAX,EAX ;
// MOV [RBX+4],EAX ; MOV [RBX+8],RAX`. The zero stores are unconditional.
// Defined, not registered (no caller in the tree).
void Save::SavedState::Discard() {
    if (_data != nullptr) MemFree(_data);
    _size = 0;
    _data = nullptr;
}
