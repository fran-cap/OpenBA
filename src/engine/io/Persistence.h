#pragma once
// ============================================================================
// src/engine/io/Persistence.h — IO::Persistence (Wave 4, lane C)
// ============================================================================
//
// Retail Runtime-x64.dll, image base 0x180000000. Layout is PARTIAL: only the
// two fields this lane's five ported functions actually touch are pinned.
// Everything else in the object is unknown and left as opaque padding — do
// not extend this struct from a decompile without a static_assert, and do not
// allocate a Persistence from this declaration; every instance this lane
// deals with is the live DLL singleton (`?_singleton@Persistence@IO@@0PEAV12@EA`,
// exported data, retail 0x1801fc6b8 — see WAVE4_LANE_C.md §"the singleton").
//
// FIELDS PINNED
// -------------
//   +0x00  root pointer of an in-memory Collection::Set<Pair<String,String>>
//          holding every "section+key" -> "value" entry ever Get/Set this
//          session (loaded by ReadSettings, flushed back by WriteSettings:
//          retail's registry, this exe's settings.cfg, persistence_settings.cpp).
//          Pinned by IO::Persistence::Get/Set (retail 0x1800b81e0, 0x1800b8290,
//          0x1800b80c0, 0x1800b8380), every one of which passes `this` itself
//          — not `this + N` — as the first argument to the shared internal
//          helpers `Map_Find` (retail 0x18002ace0) and `Map_Insert` (retail
//          0x18002a090), both of which read/write `*(void**)self` as the tree
//          root. Map_Find/Map_Insert are NOT owned by this lane (see below).
//   +0x90  System::String _savePath. Pinned by IO::Persistence::GetSavePath
//          (retail 0x180007ad0): `if (*(longlong*)(this+0x90) != 0) { copy
//          the pointer into the hidden return slot; ++refcount; }` — a plain
//          ref-counted read of a String field, nothing else.
//
//   +0x08  int32 element count of that same set. Added 2026-09-08 by Wave 9
//          lane BOOT2, which read `Map_Clear` (retail 0x18000f7d0, an unnamed
//          DLL internal, the first thing `ReadSettings` calls with `this` in
//          RCX). After it has recursively destroyed the tree it writes BOTH
//          roots of the container's state and nothing else:
//              XOR EAX,EAX
//              MOV qword ptr [RBX],RAX        ; +0x00 root  = null
//              MOV dword ptr [RBX+0x8],EAX    ; +0x08 count = 0   <-- a DWORD
//          The 8-vs-4 byte split is the evidence for the width; the empty-tree
//          arm at 0x18000f824 writes the identical pair. Nothing this lane
//          read pins +0x0C.
//
// Offsets 0x0C..0x8F are UNKNOWN. ReadSettings/WriteSettings construct their
// own `Registry` helper object on the STACK (`Registry::Registry(&local, 0)`),
// not through a field of `this`, so whatever else lives in that gap is not
// evidenced by any function this lane read. Held-back functions (see the wave
// doc) may need it; a future lane extending this struct must repin it with
// its own static_assert, not assume this comment's gap is empty.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>   // B.3.4 PACKAGE+FS: the "IO::Persistence has no vptr" pins

#include "../system/String.h"
#include "../system/Filename.h"   // B.3.4: GetSaveFilename returns System::Filename
#include "../system/Value.h"      // System::DateTime (SaveGameInfo::sortKey)

namespace RtIo {

// Defined in full at the bottom of this header; Persistence holds an array of
// them at +0x80 and ?GetSaveInfo@ returns one BY VALUE.
struct SaveGameInfo;

// One row of the block-descriptor array at +0x60 (stride 0xc: `ADD RCX,0xc`
// in ?SetupLoad@SavedState@Save@@ 0x1800c0cb7). The SAVE enqueue (0x1800b75c0)
// writes all three — +0x00 = the block's id, +0x04 = the running byte offset,
// +0x08 = the block's size — and SetupLoad matches +0x00 against
// SavedState::_id and returns +0x08.
struct BlockDesc {
    uint32_t id;       // +0x00
    uint32_t offset;   // +0x04
    uint32_t size;     // +0x08
};
static_assert(offsetof(BlockDesc, offset) == 0x04, "BlockDesc.offset @ 0x04");
static_assert(offsetof(BlockDesc, size)   == 0x08, "BlockDesc.size @ 0x08");
static_assert(sizeof(BlockDesc) == 0xc, "the +0x60 array strides 0xc");
static_assert(offsetof(BlockDesc, id) == 0x0, "BlockDesc::id");

// The `RtIo::Persistence` POD mirror that stood here was deleted by B.4 wave X
// (X7-RT): `IO::Persistence` below holds the worker and the String as typed
// members. Its per-offset evidence, kept (Persistence-relative offsets; the
// worker's own are 0x10 less):
//
//       void*            settingsMapRoot;   // +0x00 — Set<Pair<String,String>> root
//       int32_t          settingsMapCount;  // +0x08 — see below (Wave 9 lane BOOT2)
//       uint8_t          _pad0c[0x04];      // +0x0C — NOT pinned
//
//       // ---- the embedded worker, +0x10 .. +0x7F (Wave 19 lane PERSIST) --------
//       // ??0Persistence@IO@@QEAA@XZ (0x1800b8020) runs
//       // `??0Thread@System@@QEAA@I@Z(this+0x10, 0x10000)` and then stamps a
//       // derived vtable (0x180155860) over `this+0x10`, so IO::Persistence
//       // EMBEDS a Thread-derived save/load worker at +0x10 rather than being
//       // one. Every offset below was read twice, once Persistence-relative in a
//       // public entry point and once worker-relative (rdi = this+0x10) in the
//       // private enqueue helpers, and the two agree:
//       //
//       //   field     Persistence-rel reader        worker-rel reader
//       //   flags     0x1800b7c60 testb $1,0x18     —
//       //   workEvt   0x1800b7911 mov 0x28(rbx)     0x1800b7428 mov 0x18(rdi)
//       //   doneEvt   0x1800b791b mov 0x38(rbx)     0x1800b742c mov 0x28(rdi)
//       //   filename  0x1800b78f7 mov ..,0x40(rbx)  0x1800b73f0 mov ..,0x30(rdi)
//       //   status    0x1800b7903 movl $0,0x48(rbx) 0x1800b7425 mov esi,0x38(rdi)
//       //   command   0x1800b78b7 movl $3,0x4c(rbx) 0x1800b741e movl $2,0x3c(rdi)
//       //
//       // The `flags` bit-0 test is the FIRST instruction of all seven public
//       // entry points this lane ports; retail treats it as "the worker is dead /
//       // persistence unavailable" and refuses the request. It is NOT a
//       // busy flag: BeginGetSaveGameList (0x1800b7b30) tests it too and issues
//       // straight away.
//       void*            workerVtable;      // +0x10 — 0x180155860
//       uint32_t         flags;             // +0x18 — bit 0 == unavailable
//       uint8_t          _pad1c[0x04];      // +0x1C — NOT pinned
//       // +0x20 and +0x30 were `_pad` until the WAVE 19 lane PERSIST post-report
//       // tranche, and what pinned them is the WORKER DESTRUCTOR, 0x1800b7740 —
//       // the one function this lane held ??1Persistence@ back on without ever
//       // reading. Its tail is four CloseHandle calls on consecutive fields, which
//       // is what says all four of these are HANDLEs and not three plus a spare:
//       //     1800b77b6  SetEvent            ( [worker+0x20] )  == this+0x30
//       //     1800b77c3  WaitForSingleObject ( [worker+0x10], INFINITE ) == this+0x20
//       //     1800b77cd  CloseHandle ( [worker+0x10] )          == this+0x20
//       //     1800b77d7  CloseHandle ( [worker+0x18] )          == this+0x28
//       //     1800b77e1  CloseHandle ( [worker+0x20] )          == this+0x30
//       //     1800b77f0  CloseHandle ( [worker+0x28] )  (tail)  == this+0x38
//       // The WaitForSingleObject target is the one that is waited on and closed
//       // FIRST, which is the thread itself; the other three are the events the
//       // issue path already named. Import names read out of the DLL's own import
//       // directory (0x18014b3c0 WaitForSingleObject, 0x18014b3b8 CloseHandle),
//       // not guessed from the call shape.
//       void*            threadHandle;      // +0x20 — HANDLE, WaitForSingleObject(INFINITE)
//       void*            workEvent;         // +0x28 — HANDLE, SetEvent  (wake worker)
//       void*            readyEvent;        // +0x30 — HANDLE, SetEvent  (in the dtor only)
//       void*            doneEvent;         // +0x38 — HANDLE, ResetEvent(before issue)
//       System::String   pendingFilename;   // +0x40 — request filename
//       int32_t          status;            // +0x48 — ?GetStatus@ (0x1800b7c40) reads this
//       int32_t          command;           // +0x4C — 1 load · 2 delete · 3 info · 4 list
//       int32_t          blockCount;        // +0x50 — int arg of BeginGetSaveGameInfo
//
//       // ---- the on-disk header triple, +0x54 .. +0x5F ------------------------
//       // Pinned 2026-09-12 by the SAVE job (FUN_1800b6e00, worker-relative +0x44/
//       // +0x48/+0x4c) and independently corroborated by the GET-SAVE-INFO job
//       // (FUN_1800b65b0, same three worker-relative offsets, reading them BACK):
//       // the SAVE job writes `fileHeader` and `magic` as the file's first 12
//       // bytes (4 + 8, the 8 being magic:crc together) and stores the running
//       // CRC-32 into `crc`; the GET-SAVE-INFO job reads the same 12 bytes off
//       // disk into the SAME three fields and checks them (fileHeader's high
//       // 16 bits against `blockCount` above, `magic` against the sentinel
//       // 0x80808080, `crc` against a freshly recomputed checksum). Whoever
//       // enqueues a SAVE (BeginSaveGame, not yet ported) must pack
//       // `fileHeader = version | (blockDescCount << 16)` and preset
//       // `crc = 0xFFFFFFFF` before signalling the worker — neither job writes
//       // those two on entry, only reads them.
//       uint32_t         fileHeader;        // +0x54 — (version:16 | blockCount:16)
//       uint32_t         magic;             // +0x58 — sentinel 0x80808080
//       uint32_t         crc;               // +0x5C — running/verified CRC-32
//
//       void*            blockDescData;     // +0x60 — Array<{id,?,size}>, stride 0xc
//       int32_t          blockDescCount;    // +0x68 — ?GetDataSize@ (0x1800b7b80) walks these
//       uint8_t          _pad6c[0x04];      // +0x6C — NOT pinned (Array capacity)
//       void*            blockPtrData;      // +0x70 — Array<void*>
//       int32_t          blockPtrCount;     // +0x78 — SetSize'd to blockDescCount at 0x1800b7514
//       uint8_t          _pad7c[0x04];      // +0x7C — NOT pinned (Array capacity)
//
//       // ---- the save list, +0x80 .. +0x8F ------------------------------------
//       // ?GetSaveInfo@ (0x1800b7bd0) is `((SaveGameInfo*)this[0x80])[index]` with
//       // `shl $4` for the stride; ?GetSaveCount@ (0x1800b7c60) returns the dword
//       // at +0x88 and nothing else. Neither bounds-checks.
//       SaveGameInfo*    saves;             // +0x80
//       int32_t          saveCount;         // +0x88
//       uint8_t          _pad8c[0x04];      // +0x8C — NOT pinned (Array capacity)
//
//       System::String   savePath;          // +0x90

// The settings map's node and its {root, count} header are the
// `Collection::Set<Collection::Pair<String,String>>` instantiation itself
// (RtIo::SettingsSet, declared below the Collection.h include): B7 LX (D7a)
// deleted the `SettingsMapNode` / `SettingsMap` views that stood here.

// ============================================================================
// IO::Persistence::SaveBlock — 0x10 bytes. One buffer handed to the save/load
// worker thread. Added by Wave 13, pinned by THREE independent readers rather
// than by one writer:
//
//   * ?UpdateSave@SaveLoad (0x1800c2c60..0x1800c2c88) builds an array of TWO
//     of them, entry 0 from the SavedState embedded in SaveLoad at +0x28 and
//     entry 1 from the heap SavedState at +0x40. The three loads per entry are
//     qword `data`, then dword `size`, then dword `id`, at +0x00/+0x08/+0x0c.
//   * ?UpdateLoadFromFile@SaveLoad (0x1800c25cc) takes ONE straight from
//     ?SetupLoad@SavedState@Save@@, whose mangled return type spells this
//     struct's full name — which is where the name here comes from, not a guess.
//   * The unexported worker-enqueue at 0x1800b7460 walks the array the caller
//     passes with `param_4 += 2` on an 8-byte pointer (stride 0x10) and matches
//     `*(int*)((char*)param_4 + 0xc)` against a per-slot id, which pins BOTH
//     the stride and the fact that +0x0c is the id and not part of the size.
//
// The field ORDER is deliberately data/size/id and not the SavedState order
// (id/size/data): these two structs are NOT the same layout, and UpdateSave's
// six loads per entry are what says so.
struct SaveBlock {
    void*    data;   // +0x00
    uint32_t size;   // +0x08
    uint32_t id;     // +0x0c
};
static_assert(offsetof(SaveBlock, size) == 0x08, "SaveBlock.size @ 0x08");
static_assert(offsetof(SaveBlock, id)   == 0x0c, "SaveBlock.id @ 0x0c");
static_assert(sizeof(SaveBlock) == 0x10, "sizeof(IO::Persistence::SaveBlock) == 0x10");
static_assert(offsetof(SaveBlock, data) == 0x0, "SaveBlock::data");

// ============================================================================
// IO::Persistence::SaveGameInfo — 0x10 bytes, RETURNED BY VALUE.
// ----------------------------------------------------------------------------
// The NAME is not a guess: `?GetSaveInfo@Persistence@IO@@QEBA?AUSaveGameInfo@12@H@Z`
// (retail 0x1800b7bd0, ordinal 2888) spells it in full, and `?A` says the
// return is by value — so the real ABI is
// `GetSaveInfo(this, SaveGameInfo* hiddenSlot, int index)` with the index in
// R8D, which is what 0x1800c1fe3..0x1800c1fed does register for register.
// A typedef that omits that slot is a wild write that still returns the right
// answer (reference_hidden_retslot_wild_write.md).
//
// BOTH FIELDS ARE PINNED BY ONE READER, ?UpdateList@SaveLoad@Save@@ state 1:
//   +0x00  1800c1ff5  MOV RCX,[RAX]    -> copied into SaveInfo+0x30, which the
//                     qsort comparator at 0x1800c3db0 then compares UNSIGNED
//                     and orders descending. Named `sortKey` for what it is
//                     used as; see the SaveInfo banner in ../save/SaveLoad.h.
//   +0x08  1800c202b  MOV RAX,[R15+8]  -> refcount-assigned into SaveInfo+0x38,
//                     the display name. That it is a System::String is settled
//                     by the caller destroying it with the refcount idiom at
//                     0x1800c203b once it has copied it out.
// Nothing reads past +0x10, and the caller's by-value temporary destructor
// touches only +0x08, so 0x10 is the whole struct.
// ============================================================================
// *** +0x00 SETTLED 2026-09-12 (batch 5), FROM ITS PRODUCER. *** The comment
// above named it `sortKey` "for what it is used as", from the CONSUMER side
// only. The producer is the worker's command-4 job (retail 0x1800b6290,
// src/engine/io/persistence_savelist_job.cpp), which fills it with
// `FileSystem::GetFileTimeStamp(<the .sav path>)` — a raw Win32 FILETIME, i.e.
// 100 ns ticks since 1601-01-01 UTC, which is exactly why an UNSIGNED
// DESCENDING sort puts the most recently written save first. The name is kept
// (it is still what the comparator uses it for) and the type is now known.
//
// B.4 R-IO: `name` is the real `System::String`. Every holder of a
// SaveGameInfo is either a C++ object whose destructor IS retail's release
// (the by-value temporaries in UpdateList state 1 and the command-4 job) or an
// element of Persistence's `Array<SaveGameInfo>` at +0x80, whose storage the
// hand-derived kSpec_SaveGameInfo row manages by offset (array.cpp), never
// through this type's destructor.
struct SaveGameInfo {
    System::DateTime sortKey;  // +0x00 — a FILETIME; see the note above
    System::String   name;     // +0x08
};
static_assert(offsetof(SaveGameInfo, name) == 0x08, "SaveGameInfo.name @ 0x08");
static_assert(sizeof(SaveGameInfo) == 0x10, "sizeof(IO::Persistence::SaveGameInfo) == 0x10");
static_assert(offsetof(SaveGameInfo, sortKey) == 0x0, "SaveGameInfo::sortKey");

}  // namespace RtIo

// ============================================================================
// IO::Persistence and the save/load worker it embeds
// ============================================================================
//
// *** NOTE THE POD NAMESPACE IN THIS FILE IS `RtIo`, LOWERCASE o ***, not
// `RtIO` as in `FileSystem.h`.
//
// *** IT HAS NO VTABLE OF ITS OWN. *** There is no `??_7Persistence@IO@@6B@`;
// the only Persistence row of the mint tables is the singleton pointer
// `?_singleton@Persistence@IO@@0PEAV12@EA` (0x1801fc6b8). Every exported
// member is `QEAA` / `QEBA` / `AEBA`. The vptr at +0x10 is the EMBEDDED
// worker's: `??0Persistence@IO@@QEAA@XZ` (0x1800b8020) runs
// `??0Thread@System@@QEAA@I@Z(this+0x10, 0x10000)` and stamps 0x180155860.
//
// B.4 wave X (X7-RT), `behaviour:` -- the three non-trivial members are typed
// together, because retail's constructor and destructor are exactly member
// order (H5):
//
//   ??0Persistence 0x1800b8020
//     1800b8047  MOV qword [RCX],RSI          ; _settingsMap.root  = 0
//     1800b804a  MOV dword [RCX+0x8],ESI      ; _settingsMap.count = 0 (+0x0c untouched)
//     1800b805e  CALL 0x1800ca4e0             ; _worker: ??0Thread(this+0x10, 0x10000)
//     1800b806b  MOV [RBX],0x180155860        ;   the worker's vptr
//     1800b806e..1800b8091                    ;   the worker's member stores
//     1800b8095  MOV qword [RDI+0x90],RSI     ; _savePath = String()
//     1800b809c  MOV [0x1801fc6b8],RDI        ; _singleton = this
//
//   ??1Persistence 0x1800b7fb0
//     1800b7fc6..1800b7ff1  release [RCX+0x90] ; ~_savePath (String's inline release)
//     1800b8000  CALL 0x1800b7740              ; ~_worker
//     1800b8013  JMP  0x18000f7d0              ; ~_settingsMap (Set<Pair<String,String>>::Clear)
//
// Members are constructed in declaration order and destroyed in reverse after
// an empty destructor body: those are the two sequences above.
// ============================================================================

#include "../system/Thread.h"
#include "../collection/Collection.h"   // Collection::Array, Collection::Set / Pair

// The hand-derived Array row for `Array<SaveGameInfo>` (src/engine/collection/
// array.cpp; 0x18002ae20 / 0x18002aae0 / 0x18002ac40 -- NOT trait-expressible,
// array_internal.h). Declared here so the worker's member can name its row.
namespace rtcoll {
extern const ArraySpec kSpec_SaveGameInfo;
}

namespace RtIo {

// ----------------------------------------------------------------------------
// The settings map: `Collection::Set<Collection::Pair<String,String>>` (B7 LX,
// D7a). IO::Persistence holds one at +0x00, System::Configuration at +0x10.
// ----------------------------------------------------------------------------
// The instantiation is the one whose bodies are, read at their addresses:
//   0x18002ace0  Map_Find   == `Find`: the descent on the key's char* with the
//                asymmetric null arms (a null node key descends right, a null
//                search key returns the node) and the inlined SIGNED strcmp;
//                the `Key = char*` parameter is what selects exactly those
//                arms (Collection.h KeyTraits / CompareTraits<AVLCompare,char*>)
//   0x18002a170  the insert core (rtavl::StringInsertCore, avl_trees.cpp,
//                typed on this Set); 0x18002a090 Map_Insert builds the stack
//                Pair and calls it (rtavl::StringMapInsert)
//   0x18000f7d0  `Clear` (== ~Set: ??1Persistence's tail JMP at 0x1800b8013,
//                ~Configuration's CALL at 0x1800c518f); 0x18000f890 is
//                `DestroySubtree`, 0x18000fa90 `~Pair` (value +0x08, then key
//                +0x00 -- reverse declaration order)
//   ctor         two stores, the qword root and the dword count; +0x0c is NOT
//                written (??0Persistence 0x1800b8047 / 0x1800b804a,
//                ??0Configuration 0x1800c51d3 / 0x1800c51d6)
// The node (0x18000f970 allocates 0x30 bytes, align 8; 0x18000fb00 builds
// it) is the template's `Node`: key +0x00, value +0x08, parent +0x10
// (WriteSettings' successor 0x1800b8a9c), left +0x18 / right +0x20 (Map_Find
// 0x18002ad3f / 0x18002ad8d, Map_Clear 0x18000f7f4 / 0x18000f7fd), balance
// +0x28, height +0x2a (set_avl.h's rotations).
using SettingPair  = Collection::Pair<System::String, System::String>;
using SettingsSet  = Collection::Set<SettingPair, Collection::AVLCompare,
                                     Collection::DefaultAllocator, char*>;
using SettingsNode = SettingsSet::Node;
static_assert(sizeof(SettingPair) == 0x10, "the element of a 0x30-byte node");
static_assert(offsetof(SettingPair, key)   == 0x00, "key @ +0x00 (0x18002ace0: MOV R9,[R8])");
static_assert(offsetof(SettingPair, value) == 0x08, "value @ +0x08 (Persistence::Get, 0x1800c533b)");
static_assert(offsetof(SettingsNode, element) == 0x00, "the pair leads the node");
static_assert(offsetof(SettingsNode, parent)  == 0x10, "parent @ +0x10 (0x1800b8a9c)");
static_assert(offsetof(SettingsNode, left)    == 0x18, "left @ +0x18 (0x18000f7f4)");
static_assert(offsetof(SettingsNode, right)   == 0x20, "right @ +0x20 (0x18000f7fd)");
static_assert(offsetof(SettingsNode, balance) == 0x28, "balance @ +0x28");
static_assert(offsetof(SettingsNode, height)  == 0x2a, "height @ +0x2a");
static_assert(sizeof(SettingsNode) == 0x30, "0x18000f970 allocates 0x30");
static_assert(sizeof(SettingsSet) == 0x10, "the {root, count} Set header");
static_assert(offsetof(SettingsSet, _root)  == 0x00, "root @ +0x00");
static_assert(offsetof(SettingsSet, _count) == 0x08, "count @ +0x08 (Map_Clear's dword store)");

}  // namespace RtIo

namespace IO {

class Persistence;

// ----------------------------------------------------------------------------
// IO::PersistenceWorker -- ENDGAME B.3.9 IO (W2)
// ----------------------------------------------------------------------------
// The save/load worker `IO::Persistence` embeds at +0x10: a `System::Thread`
// subclass whose table is the UNEXPORTED 2-slot vtable 0x180155860
// (slot 0 `Run` 0x1800b7260, slot 1 `System::Thread::OnCompleted`
// 0x18000c590). Retail never names the class, so `IO::PersistenceWorker` is
// this project's name for it, and nothing mangles it.
//
// Construction: ??0Persistence (0x1800b8020) inlines this class's ctor --
//     LEA RBX,[RCX+0x10] ; MOV EDX,0x10000 ; CALL 0x1800ca4e0   ; ??0Thread(0x10000)
//     LEA RAX,[0x180155860] ; MOV [RBX],RAX                      ; this class's vptr
//     MOV qword [RBX+0x30],0 ; MOV qword [RBX+0x38],1 ; MOV dword [RBX+0x40],0
//     MOV qword [RBX+0x50..+0x78],0                               ; the three arrays
// -- the base's real ctor, the compiler's vptr store, then the member stores
// below. +0x44..+0x4f (fileHeader / magic / crc) are NOT written.
//
// Destruction: 0x1800b7740 (called directly by ??1Persistence 0x1800b7fb0)
// is `vptr = 0x180155860; ~Array x3; ~String; vptr = 0x180155f20; <four
// CloseHandles>` -- the part after the second stamp is `System::Thread::
// ~Thread` (ReleaseFields) inlined. So the class destructor is the first half
// and the compiler's call into the base destructor is the second.
//
// The worker's Thread fields seen from Persistence: `_runFlags` (+0x18; bit 0
// == "persistence unavailable", the first test of every public entry point),
// `_hThread` (+0x20), `_hStart` (+0x28, the "work" event), `_hStop` (+0x30),
// `_hFinished` (+0x38, the "done" event).
class PersistenceWorker : public System::Thread {
public:
    // The inlined worker half of ??0Persistence 0x1800b8020 (see the banner).
    // The members' own ctors are retail's stores: String() nulls +0x30,
    // Array() zeroes each {data, size, capacity} triple.
    PersistenceWorker()
        : System::Thread(0x10000),
          _pendingFilename(), _status(1), _command(0), _blockCount(0),
          _blockDescs(), _blockPtrs(), _saves() {}

    // slot 0  +0x00  0x1800b7260 -- a five-way jump on `_command`; every arm a
    // tail JMP into a job body. Defined in persistence_worker.cpp.
    void Run() override;
    // slot 1  +0x08  0x18000c590 -- NOT overridden: System::Thread::OnCompleted.

    // ---- the five jobs `Run` tail-jumps into (all unexported) ------------
    void SaveJob();          // command 0  0x1800b6e00  persistence_worker.cpp
    void LoadJob();          // command 1  0x1800b6aa0  closure_batch2b.cpp
    void DeleteJob();        // command 2  0x1800b69c0  closure_batch2b.cpp
    void GetSaveInfoJob();   // command 3  0x1800b65b0  persistence_saveinfo_job.cpp
    void GetSaveListJob();   // command 4  0x1800b6290  persistence_savelist_job.cpp

    // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §27): fix.save_failure_notice, null by default
    // (set by src/ext/registry_boot.cpp to the row's live bool; DESIGN.md hook rule 5). While it
    // reads true, SaveJob writes `<name>.tmp` and replaces the save with it only after every byte
    // is written, so a failed write leaves the previous save as it was. Retail writes in place.
    static const bool* _extFixSaveTemp;

    // B.4 R-IO: the String and the three arrays are the real classes. Their
    // member destructors ARE the four releases 0x1800b7740 makes: members die
    // in reverse declaration order, so _saves (+0x70, 0x18002ac40), _blockPtrs
    // (+0x60, 0x180024aa0), _blockDescs (+0x50, 0x18002a930), then
    // _pendingFilename (+0x30) -- retail's order.
    System::String                     _pendingFilename;  // +0x30  (Persistence +0x40)
    int32_t                            _status;           // +0x38  (+0x48) ?GetStatus@ 0x1800b7c40
    int32_t                            _command;          // +0x3C  (+0x4C) 1 load, 2 delete, 3 info, 4 list
    int32_t                            _blockCount;       // +0x40  (+0x50)
    uint32_t                           _fileHeader;       // +0x44  (+0x54) never written by the ctor
    uint32_t                           _magic;            // +0x48  (+0x58) never written by the ctor
    uint32_t                           _crc;              // +0x4C  (+0x5C) never written by the ctor
    Collection::Array<RtIo::BlockDesc> _blockDescs;       // +0x50  (+0x60)
    Collection::Array<void*>           _blockPtrs;        // +0x60  (+0x70)
    // The save list's element spec is the hand-derived row, named explicitly.
    Collection::Array<RtIo::SaveGameInfo, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpec_SaveGameInfo>>
                                       _saves;            // +0x70  (+0x80)

protected:
    // 0x1800b7740's first half (see the banner): all four releases are the
    // members' destructors, so the body is empty. The compiler then runs
    // ~Thread. Non-virtual: the table has no dtor slot.
    // Out of line in persistence_worker.cpp.
    ~PersistenceWorker();

    friend class Persistence;   // ??1Persistence ends the embedded worker
};

static_assert(sizeof(PersistenceWorker) == 0x80, "the worker spans Persistence +0x10..+0x8F");
static_assert(offsetof(PersistenceWorker, _runFlags)        == 0x08, "Persistence+0x18 flags (0x1800b7c60 testb $1,0x18)");
static_assert(offsetof(PersistenceWorker, _hStart)          == 0x18, "Persistence+0x28 workEvent (0x1800b7911)");
static_assert(offsetof(PersistenceWorker, _hFinished)       == 0x28, "Persistence+0x38 doneEvent (0x1800b791b)");
static_assert(offsetof(PersistenceWorker, _pendingFilename) == 0x30, "worker+0x30");
static_assert(offsetof(PersistenceWorker, _status)          == 0x38, "worker+0x38");
static_assert(offsetof(PersistenceWorker, _command)         == 0x3c, "Run 0x1800b7260: mov 0x3c(%rcx),%edx");
static_assert(offsetof(PersistenceWorker, _blockCount)      == 0x40, "worker+0x40");
static_assert(offsetof(PersistenceWorker, _fileHeader)      == 0x44, "SAVE job worker+0x44");
static_assert(offsetof(PersistenceWorker, _magic)           == 0x48, "SAVE job worker+0x48");
static_assert(offsetof(PersistenceWorker, _crc)             == 0x4c, "SAVE job worker+0x4c");
static_assert(offsetof(PersistenceWorker, _blockDescs)      == 0x50, "worker+0x50");
static_assert(offsetof(PersistenceWorker, _blockPtrs)       == 0x60, "worker+0x60");
static_assert(offsetof(PersistenceWorker, _saves)           == 0x70, "worker+0x70");
static_assert(!std::has_virtual_destructor<PersistenceWorker>::value,
              "B.3.9 D1.3: non-virtual (the 2-slot table has no dtor slot)");

// ----------------------------------------------------------------------------
// IO::Persistence
// ----------------------------------------------------------------------------
// MEMBERS, by the file their body lives in:
//   persistence.cpp              Get(int&), Get(bool&), Set(int), Set(bool),
//                                GetSavePath
//   persistence_settings.cpp     ReadSettings, WriteSettings (settings.cfg,
//                                Phase D.4 SETTINGS E.3; retail: the registry)
//   persistence_saves.cpp        GetSaveCount, GetSaveInfo, GetSaveFilename,
//                                BeginDelete, BeginLoad, ~Persistence
//   persistence_begin_saveinfo.cpp    BeginGetSaveGameInfo
//   persistence_startup.cpp      Startup
//   singleton_ctors.cpp          Persistence()
//
// The size is not independently pinned by an enumeration of the members;
// Engine's startup allocates 0x98 (`MemAlloc(0x98, 4)`, engine_lifecycle.cpp),
// which is what the `sizeof` assert states.
class Persistence {
public:
    // ??0Persistence@IO@@QEAA@XZ                             retail 0x1800b8020
    Persistence();
    // ??1Persistence@IO@@QEAA@XZ                             retail 0x1800b7fb0
    // Non-virtual; the class has no vtable. The body is empty: the three
    // releases are the members' destructors (the banner).
    ~Persistence();
    Persistence(const Persistence&)            = delete;
    Persistence& operator=(const Persistence&) = delete;

    // ?Get@Persistence@IO@@QEBA_NPEBD0AEAH@Z                 retail 0x1800b81e0
    bool Get(const char* section, const char* key, int& out) const;
    // ?Get@Persistence@IO@@QEBA_NPEBD0AEA_N@Z                retail 0x1800b8290
    bool Get(const char* section, const char* key, bool& out) const;
    // ?Set@Persistence@IO@@QEAAXPEBD0H@Z                     retail 0x1800b80c0
    void Set(const char* section, const char* key, int value);
    // ?Set@Persistence@IO@@QEAAXPEBD0_N@Z                    retail 0x1800b8380
    void Set(const char* section, const char* key, bool value);
    // ?GetSavePath@Persistence@IO@@QEBA?AVString@System@@XZ  retail 0x180007ad0
    System::String GetSavePath() const;

    // ?ReadSettings@Persistence@IO@@QEAAXXZ                  retail 0x1800b84b0
    void ReadSettings();
    // ?WriteSettings@Persistence@IO@@QEBAXXZ                 retail 0x1800b88e0
    void WriteSettings() const;

    // ?Startup@Persistence@IO@@QEAA_NXZ                      retail 0x1800b7ee0
    bool Startup();

    // ?GetSaveCount@Persistence@IO@@QEBAHXZ                  retail 0x1800b7c60
    int GetSaveCount() const;
    // ?GetSaveInfo@Persistence@IO@@QEBA?AUSaveGameInfo@12@H@Z
    //                                                        retail 0x1800b7bd0
    // Retail returns `SaveGameInfo` BY VALUE (retslot, non-virtual: the
    // compiler's business). Its `name` is a real System::String, so the
    // by-value return is retail's copy-construct into the hidden slot.
    RtIo::SaveGameInfo GetSaveInfo(int index) const;
    // ?GetSaveFilename@Persistence@IO@@AEBA?AVFilename@System@@AEBVString@2@@Z
    //                                                        retail 0x1800b7c80
    // `AEBA` -- PRIVATE in retail, and exported all the same. Declared public.
    System::Filename GetSaveFilename(const System::String& name) const;
    // ?BeginDelete@Persistence@IO@@QEAA_NAEBVString@System@@@Z
    //                                                        retail 0x1800b7ab0
    bool BeginDelete(const System::String& name);
    // ?BeginLoad@Persistence@IO@@...                         retail 0x1800b7970
    bool BeginLoad(const System::String& name, int blockCount,
                   const RtIo::SaveBlock* blocks);
    // ?BeginGetSaveGameInfo@Persistence@IO@@QEAA_NAEBVString@System@@H@Z
    //                                                        retail 0x1800b7870
    bool BeginGetSaveGameInfo(const System::String& name, int count);

    // ---- the members, at retail's offsets (PUBLIC: the save / input /
    // modification code reads them) ----------------------------------------
    RtIo::SettingsSet      _settingsMap;   // +0x00  Set<Pair<String,String>> {root, count}
    PersistenceWorker      _worker;        // +0x10  the embedded Thread-derived worker
    System::String         _savePath;      // +0x90  GetSavePath 0x180007ad0

    // ?_singleton@Persistence@IO@@0PEAV12@EA (0x1801fc6b8) -- the class static
    // (ENDGAME B.3.9 IO, B39_RULES D7). Defined in persistence.cpp; written by
    // the constructor. PUBLIC while foreign readers still reach it (retail:
    // private `0`).
    static Persistence* _singleton;
};

static_assert(sizeof(Persistence) == 0x98, "Engine startup: MemAlloc(0x98, 4)");
static_assert(offsetof(Persistence, _settingsMap) == 0x00, "Map_Find / Map_Insert take `this`");
static_assert(offsetof(Persistence, _worker)      == 0x10, "??0Persistence: LEA RBX,[RCX+0x10]");
static_assert(offsetof(Persistence, _savePath)    == 0x90, "GetSavePath 0x180007ad0 / ctor 0x1800b8095");
static_assert(!std::is_polymorphic<Persistence>::value,
              "IO::Persistence must never acquire a vtable: retail has none for it, and the "
              "vptr at +0x10 is the EMBEDDED worker's, not this class's");

}  // namespace IO
