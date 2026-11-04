#pragma once
// ============================================================================
// src/engine/system/MemoryAllocator.h — System::MemoryAllocator (Wave 2, lane C)
// ============================================================================
//
// Retail Runtime-x64.dll, image base 0x180000000. Every offset below is
// static_assert'ed at the bottom of this file and every one is pinned by a
// named, decompiled retail function (the function is cited next to the field).
//
// ---------------------------------------------------------------------------
// WHAT IT IS
// ---------------------------------------------------------------------------
// A single-arena BOUNDARY-TAG allocator with 192 segregated free lists.
// It is NOT a bucketed pool allocator: the bins index a first-fit search over
// variable-sized blocks, they do not own fixed-size cells. There is no
// large-allocation path and no fallback to the CRT heap — every allocation of
// every size comes out of the same boundary-tag arena, and when the arena
// cannot satisfy a request the allocator raises a fatal Error box.
//
// Memory comes from VirtualAlloc in two stages: a range is RESERVED whole
// (MEM_RESERVE) and pages are COMMITTED lazily as the high-water mark of used
// bytes advances (CommitToPage) and DECOMMITTED when the tail block is freed
// (ReleaseToPage). So the process's committed footprint tracks the peak of the
// arena, not the reserve.
//
// The allocator object is EMBEDDED AT OFFSET 0 OF Engine::Engine:
// `Engine::Engine::GetMemoryAllocator` (0x18000c8b0) is a bare `return this`.
// `_singleton` (exported data, 0x1801fc718) is set by the ctor and cleared by
// the dtor, so the Engine object's address IS the allocator singleton.
//
// ---------------------------------------------------------------------------
// THE ALLOCATION HEADER — and a correction to the note in
// standalone/filename_runtime_hook.cpp
// ---------------------------------------------------------------------------
// That file's header says "refcount at ptr-4, user pointer at ptr-8". Verified
// against the disassembly: that is TRUE, and it describes System::String, NOT
// this allocator. `System::String::SetLength` (0x1800c8220) is the proof:
//
//     U   = MemoryAllocator::Allocate(_singleton, len + 1 + 8, 4);
//     *(int*)(U + 0) = len;          // String length
//     *(int*)(U + 4) = 1;            // String refcount
//     this->_data    = (char*)U + 8; // the char* a String* points at
//
// So for a String `s`:  s-4 == U+4 == refcount, s-8 == U == the ALLOCATOR
// user pointer — and the allocator's own header sits at U-8 == s-0x10.
//
// The allocator header is 8 bytes and holds NO refcount:
//
//     MemoryTag (8 bytes, immediately below every user pointer)
//     ------------------------------------------------------------------
//     bits  0..40  size of this block in 4-BYTE UNITS, tag included.
//                  Bytes = field * 4. Confirmed by GetAllocationSize
//                  (0x1800c71d0): `return (*(u64*)(p-8) & 0x1ffffffffff) << 2;`
//     bits 41..43  size of the PREVIOUS block in 4-byte units, when that fits
//                  in 3 bits (values 1..7). 0 means "too big for the field":
//                  the previous block's byte size is then stored as a full
//                  qword FOOTER at (tag - 8), i.e. in the last 8 bytes of the
//                  previous block. Meaningful only when bit 44 is set.
//     bit  44      PREV_FREE  (0x0000'1000'0000'0000)
//     bit  45      THIS_FREE  (0x0000'2000'0000'0000)
//     bits 46..63  high 18 bits (bits 23..40) of the free-list PREV delta.
//                  Only meaningful while the block is linked into a free list;
//                  cleared by UnlinkFreeTag and by Allocate.
//
//     FreeTag (the first 8 bytes of the USER AREA of a free block, tag + 8)
//     ------------------------------------------------------------------
//     bits  0..40  free-list NEXT delta, a SIGNED 41-bit byte offset relative
//                  to this FreeTag's own address. 0 == end of list.
//     bits 41..63  low 23 bits of the free-list PREV delta (also a signed
//                  41-bit byte offset relative to this FreeTag).
//
// The free list is therefore DOUBLY linked with 41-bit signed relative
// offsets, and the prev link is SPLIT ACROSS TWO QWORDS: low 23 bits in the
// FreeTag, high 18 bits in the MemoryTag above it. Reassembly (UnlinkFreeTag,
// 0x1800c7410):
//
//     prevDelta = ((int64)tag >> 23) & 0xffffffffff800000   // sign-extended
//               | (freeTag >> 41);
//     nextDelta = (int64)(freeTag << 23) >> 23;             // sign-extended
//
// A block is linked into a free list only when it is at least 0x10 bytes
// (LinkFreeTag's `if (0xf < bytes)`, UnlinkFreeTag(MemoryTag*)'s `if (3 < size)`).
// Free blocks smaller than 16 bytes exist, carry THIS_FREE, and are simply
// unreachable holes until a neighbour coalesces them.
//
// ---------------------------------------------------------------------------
// THE BINS (GetBinForSize 0x1800c78d0 / GetBinForLargeSize 0x1800c7920)
// ---------------------------------------------------------------------------
//     bytes <= 0x200 : bin = (int)bytes >> 2          -> 0 .. 128, exact size
//     bytes >  0x200 : bin = 119 + floor(log2(bytes)) -> 128 .. 191, power-of-2
// The two ranges join continuously at bin 128 (0x200 maps to 128 either way).
// There are 192 bins; only 2..191 are reachable, because the minimum block is
// 16 bytes (bin 4) after Allocate's `if (total < 8) total = 8; total += 8`.
//
// The log2 loop is the classic binary-search bit scan:
//     s=32; n=64; do { if (v>>s) { n-=s; v>>=s; } s>>=1; } while (s);
//     bin = (int)v - n + 0xb6;
// It leaves v == 1 for every input, so it is exactly 119 + floor(log2(bytes));
// it is transcribed literally below rather than replaced by a bit scan, so the
// two builds cannot disagree on a boundary case.
//
// `_minBin` (+0x628) is a search FLOOR, not a hint: GetFreeTagForSize clamps
// its starting bin UP to _minBin. It starts at 0xbf (191, "everything is
// empty") and is lowered by LinkFreeTag; UnlinkFreeTag walks it back up when
// it empties the bin that _minBin names.
//
// ---------------------------------------------------------------------------
// ALLOCATE (0x1800c6e60) — the shape, so the port can be read against it
// ---------------------------------------------------------------------------
//   align8 = (align + 7) & ~7                       // NOTE: 32-bit
//   total  = (size + 7 & ~7) + (int64)(align8 - 8)  // sign-extended
//   if (total < 8) total = 8
//   total += 8                                      // room for the tag
//   tag = GetFreeTagForSize(total) - 8, growing the arena once if needed
//   UnlinkFreeTag(tag + 8, nullptr)
//   range = GetMemoryRange(tag)                     // inlined linear scan
//   pad = alignUp(user, align8) - user              // 32-BIT arithmetic
//   if (pad) split off [tag, tag+pad) as a free block and move tag forward
//   if (blockBytes - total < 0x10) take the whole block
//   else split the tail off as a new free block (and coalesce forward)
//   commit up to the end of the block; _allocatedBytes += blockBytes; ++_allocCount
//   return tag + 8
//
// Two things a reimplementation gets wrong if it "cleans them up":
//   * `pad` is computed in 32-bit registers (`IMUL EAX,EDI` / `SUB EAX,R8D` at
//     0x1800c6f7d) even though the divide is 64-bit. Faithful code keeps the
//     truncation.
//   * `(int64)(align8 - 8)` is SIGN-extended (CDQE at 0x1800c6ea1), so
//     align == 0 subtracts 8 from the request rather than adding 4G.
//
// FREE (0x1800c6c00) coalesces backwards first (via PREV_FREE + the 3-bit
// prevSize or the footer), then forwards, then decommits if the coalesced
// block now reaches the end of its range, then relinks. Note it decrements
// `_allocCount` and `_allocatedBytes` BEFORE coalescing, using the size the
// user block had.
//
// ---------------------------------------------------------------------------
// STATISTICS / DEBUG FIELDS
// ---------------------------------------------------------------------------
//   +0x630 _allocatedBytes  int64, exposed by GetAllocatedSize (0x18007c0b0)
//   +0x638 _allocCount      int, live block count
//   +0x63c _freeTagCount    int, number of linked free blocks
//   0x1801ff300             a FILE-STATIC global (not a member): the largest
//                           `size` argument ever passed to Allocate. Written
//                           unconditionally at the top of Allocate; nothing
//                           reads it in retail. Named g_largestRequestedSize
//                           here; the port maintains it because it is a store
//                           our shadow oracle can observe.
//
// There is no guard byte, no fill pattern and no leak list. Allocate returns
// DIRTY memory (already noted in src/game/Game/serialize_arrays.cpp:354).
//
// ---------------------------------------------------------------------------
// PORTABILITY (PLAN.md §4.1)
// ---------------------------------------------------------------------------
// This header must not include <windows.h>. The allocator genuinely needs
// VirtualAlloc/VirtualFree and a recursive mutex, so those five operations are
// a function-pointer seam (RtSysMemPlatform below) that the host installs. The
// Win32 implementation belongs in `src/engine/system/MemoryAllocator_win.cpp`
// — a file this lane is not allowed to create; its exact contents are given in
// docs/history/runtime_port/WAVE2_ALLOCATOR.md for the lead to land. Until it is
// installed the port refuses to arm and says so, which is why nothing here can
// silently run on a half-built seam.
//
// The CRITICAL_SECTION at +0x640 is declared as an opaque 0x28-byte blob. It
// is 40 bytes on x64 MSVC and the object layout depends on that; the lock/
// unlock seam operates on its address, which is exactly what
// standalone/allocator_lock.cpp already does (`self + 0x640`).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "platform_sync.h"   // Platform::LockStorage: the +0x640 lock bytes

namespace rt {
namespace system {

// ---------------------------------------------------------------------------
// Tag bit field constants
// ---------------------------------------------------------------------------
static const uint64_t kTagSizeMask     = 0x000001ffffffffffull; // bits 0..40, /4
static const uint64_t kTagPrevSizeMask = 0x00000e0000000000ull; // bits 41..43, /4
static const uint64_t kTagPrevFree     = 0x0000100000000000ull; // bit 44
static const uint64_t kTagThisFree     = 0x0000200000000000ull; // bit 45
static const uint64_t kTagPrevHiMask   = 0xffffc00000000000ull; // bits 46..63

// Literals as they appear in the disassembly, kept so the port reads like the
// listing rather than like a cleaned-up rewrite.
static const uint64_t kSizeBytesMask   = 0x000007fffffffffcull; // bits 2..42
static const uint64_t kThisFreeShl2    = 0x0000800000000000ull; // kTagThisFree << 2

// ---------------------------------------------------------------------------
// MemoryTag / FreeTag — the two names the DLL's own mangled signatures use
// (`PEAUMemoryTag@12@`, `PEAUFreeTag@12@`). Both are a single packed qword; the
// bit fields are documented at the top of this file. They are deliberately NOT
// C bitfield structs: the code manipulates them with the same masks and shifts
// the listing does, and a bitfield layout would be a second, unverified model
// of the same bits.
//
//   MemoryTag lives at (userPointer - 8) and heads every block.
//   FreeTag   lives at (tag + 8) — the first qword of a FREE block's user area.
//             GetFreeTagForSize/LinkFreeTag/UnlinkFreeTag pass FreeTag*, i.e.
//             the USER pointer of a free block, not its tag.
// ---------------------------------------------------------------------------
struct MemoryTag { uint64_t bits; };
struct FreeTag   { uint64_t link; };
static_assert(sizeof(MemoryTag) == 8, "MemoryTag is one packed qword");
static_assert(offsetof(MemoryTag, bits) == 0x0, "MemoryTag::bits");
static_assert(sizeof(FreeTag) == 8, "FreeTag is one packed qword");
static_assert(offsetof(FreeTag, link) == 0x0, "FreeTag::link");

// ---------------------------------------------------------------------------
// MemoryRange — one VirtualAlloc reservation. 0x18 bytes.
// Pinned by AddNewMemoryRange (0x1800c7690), CommitToPage (0x1800c7bd0),
// ReleaseToPage (0x1800c7c80), GetMemoryRange (0x1800c71f0).
// ---------------------------------------------------------------------------
struct MemoryRange {
    uint8_t* base;       // +0x00 reservation base; also the first MemoryTag
    uint8_t* end;        // +0x08 base + logical size (NOT base + reserved size)
    uint64_t committed;  // +0x10 bytes committed from base
};
static_assert(sizeof(MemoryRange) == 0x18, "MemoryRange is 0x18 bytes");
static_assert(offsetof(MemoryRange, base) == 0x00, "MemoryRange::base");
static_assert(offsetof(MemoryRange, end) == 0x08, "MemoryRange::end");
static_assert(offsetof(MemoryRange, committed) == 0x10, "MemoryRange::committed");

struct RtSysMemPlatform;

// ---------------------------------------------------------------------------
// EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md): ext.heap_slack.
// Null by default (DESIGN.md §3 hook rule 5). When set, the three places that
// move a range's committed mark (CommitToPage, ReleaseToPage and Allocate's
// inlined CommitToPage) still compute and store retail's mark exactly; only the
// platform call that would follow is handed to the seam, which may keep the
// pages committed and zero them itself when retail would have received them
// fresh. `have` / `need` are byte offsets from `base`, page-aligned, as retail
// computes them. Called with the allocator lock held (the raw members' callers).
// ---------------------------------------------------------------------------
struct MemoryAllocatorCommitSeam {
    // retail: g_plat.commit(base + have, need - have), need > have
    void (*commit)(const RtSysMemPlatform& plat, uint8_t* base, uint64_t have, uint64_t need);
    // retail: g_plat.decommit(base + need, have - need), need < have
    void (*decommit)(const RtSysMemPlatform& plat, uint8_t* base, uint64_t need, uint64_t have);
};

} // namespace system
} // namespace rt

namespace System {

// ---------------------------------------------------------------------------
// MemoryAllocator — 0x670 bytes (operator= at 0x18007c0c0 memcpy's 0x670).
//
// B3R W0-A0: every exported member is declared here with retail's name and
// signature (the mangled name and address beside each). The bodies are in
// src/engine/system/allocator.cpp (the portable half) and allocator_win.cpp
// (the six Platform* members). Retail's access is noted per member ("retail:
// private" = the `AEAA`/`AEBA`/`CA` mangling); the port keeps them public
// because the registration thunks and the shadow oracle in allocator.cpp call
// them from outside the class.
//
// NOT DECLARED, ON PURPOSE: the constructor ??0MemoryAllocator@System@@QEAA@XZ
// (0x1800c79a0) and destructor ??1MemoryAllocator@System@@QEAA@XZ (0x1800c7960).
// `Engine::Engine::_allocator` (Engine.h) and allocator.cpp's shadow replica
// hold this class BY VALUE; a user-declared ctor/dtor would run on the shadow
// at static init / exit (the shadow must never touch `_singleton`) and would
// make every by-value holder non-trivial. Their bodies stay the free functions
// `rtalloc::Construct` / `rtalloc::Destruct` (allocator.cpp), reached through
// the ??0 / ??1 rows in src/engine/engine/singleton_ctors.cpp.
// ---------------------------------------------------------------------------
class MemoryAllocator {
public:
    // ?_singleton@MemoryAllocator@System@@0PEAV12@EA (0x1801fc718). Set by the
    // ctor (0x1800c79a0), cleared by the dtor (0x1800c7960): non-null exactly
    // while the Engine that embeds the allocator at +0 is alive. Defined in
    // src/engine/system/allocator.cpp. PUBLIC: foreign files read it directly
    // (the crash log, the engine_funcs snapshot, RunStartupSelfTests).
    static MemoryAllocator* _singleton;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md): ext.heap_slack's commit /
    // decommit seam, null under the faithful profile. Set only through
    // RtSystemAllocatorSetCommitSeam (below), under the allocator lock. A static: no layout change.
    static const rt::system::MemoryAllocatorCommitSeam* _extCommitSeam;

    // ---- statics ------------------------------------------------------------
    // ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z   0x1800c7a50
    //   Enter(_singleton->lock); Allocate(size, align); Leave. The ONE allocator
    //   API (B3R R9). Consults g_memoryAllocatorInstrument first (below).
    static void* MemAlloc(uint64_t size, int align);
    // ?MemFree@MemoryAllocator@System@@SAXPEAX@Z      0x1800c7ad0
    //   if (p) { Enter; Free(p); Leave; }. Consults g_memoryAllocatorInstrument.
    static void  MemFree(void* p);
    // ?Get@MemoryAllocator@System@@SAPEAV12@XZ        0x1800534e0
    static MemoryAllocator* Get();
    // ?PointerOffset@MemoryAllocator@System@@CA_JPEAX0@Z  0x180034e20 (retail: private)
    static int64_t PointerOffset(void* a, void* b);

    // ---- public members -----------------------------------------------------
    // ?Startup@MemoryAllocator@System@@QEAA_N_K0@Z    0x1800c7620
    bool     Startup(uint64_t minRangeMB, uint64_t maxTotalMB);
    // ?Shutdown@MemoryAllocator@System@@QEAAXXZ       0x1800c75b0
    void     Shutdown();
    // ?Allocate@MemoryAllocator@System@@QEAAPEAX_KH@Z 0x1800c6e60 — RAW: no lock,
    //   exactly as retail. The locked entry point is MemAlloc.
    void*    Allocate(uint64_t size, int align);
    // ?Free@MemoryAllocator@System@@QEAAXPEAX@Z       0x1800c6c00 — RAW: no lock.
    void     Free(void* p);
    // ?GetAllocationSize@MemoryAllocator@System@@QEAA_KPEAX@Z  0x1800c71d0
    uint64_t GetAllocationSize(void* p);
    // ?GetAllocatedSize@MemoryAllocator@System@@QEBA_JXZ       0x18007c0b0
    int64_t  GetAllocatedSize() const;
    // ??4MemoryAllocator@System@@QEAAAEAV01@AEBV01@@Z          0x18007c0c0
    //   retail memcpy's 0x670 bytes: exactly the defaulted (trivial) copy.
    MemoryAllocator& operator=(const MemoryAllocator&) = default;

    // ---- retail: private ----------------------------------------------------
    // ?GetMemoryRange@MemoryAllocator@System@@AEAAPEAUMemoryRange@12@PEAX@Z  0x1800c71f0
    rt::system::MemoryRange* GetMemoryRange(void* p);
    // ?GetFreeTagForSize@MemoryAllocator@System@@AEAAPEAUFreeTag@12@_K@Z    0x1800c7230
    rt::system::FreeTag*     GetFreeTagForSize(uint64_t want);
    // ?LinkFreeTag@MemoryAllocator@System@@AEAAXPEAUMemoryTag@12@@Z         0x1800c7310
    void LinkFreeTag(rt::system::MemoryTag* tag);
    // ?UnlinkFreeTag@MemoryAllocator@System@@AEAAXPEAUFreeTag@12@0@Z        0x1800c7410
    void UnlinkFreeTag(rt::system::FreeTag* node, rt::system::FreeTag* unused);
    // ?UnlinkFreeTag@MemoryAllocator@System@@AEAAXPEAUMemoryTag@12@@Z       0x1800c7580
    void UnlinkFreeTag(rt::system::MemoryTag* tag);
    // ?AddNewMemoryRange@MemoryAllocator@System@@AEAA_N_K@Z                 0x1800c7690
    bool AddNewMemoryRange(uint64_t want);
    // ?ExpandMemoryRangeTable@MemoryAllocator@System@@AEAA_NXZ              0x1800c77e0
    bool ExpandMemoryRangeTable();
    // ?GetBinForSize@MemoryAllocator@System@@AEBAH_K@Z                      0x1800c78d0
    int  GetBinForSize(uint64_t n) const;
    // ?GetBinForLargeSize@MemoryAllocator@System@@AEBAH_K@Z                 0x1800c7920
    int  GetBinForLargeSize(uint64_t n) const;
    // ?CommitToPage@MemoryAllocator@System@@AEAAXPEAUMemoryRange@12@PEAX@Z  0x1800c7bd0
    void CommitToPage(rt::system::MemoryRange* range, void* upTo);
    // ?ReleaseToPage@MemoryAllocator@System@@AEAAXPEAUMemoryRange@12@PEAX@Z 0x1800c7c80
    void ReleaseToPage(rt::system::MemoryRange* range, void* downTo);
    // The six Platform* members — defined in src/platform/win/allocator_win.cpp.
    // ?PlatformConstruct@MemoryAllocator@System@@AEAAXXZ          0x1800c7b30
    void  PlatformConstruct();
    // ?PlatformDestruct@MemoryAllocator@System@@AEAAXXZ           0x1800c7b50
    void  PlatformDestruct();
    // ?PlatformAllocate@MemoryAllocator@System@@AEAAPEAX_KPEAX_N@Z 0x1800c7b60
    void* PlatformAllocate(uint64_t bytes, void* at, bool commitToo);
    // ?PlatformFree@MemoryAllocator@System@@AEAAXPEAX_K@Z         0x1800c7bb0
    void  PlatformFree(void* p, uint64_t bytes);
    // ?PlatformStartup@MemoryAllocator@System@@AEAA_NXZ           0x1800c7c40
    bool  PlatformStartup();
    // ?PlatformShutdown@MemoryAllocator@System@@AEAAXXZ          0x1800c7c70
    void  PlatformShutdown();

    rt::system::MemoryRange* ranges; // +0x000 ctor, GetMemoryRange, Shutdown
    int32_t      rangeCount;     // +0x008 ctor, AddNewMemoryRange
    int32_t      rangeCapacity;  // +0x00c ExpandMemoryRangeTable
    uint64_t     minRangeSize;   // +0x010 Startup arg1 << 20 (megabytes)
    uint64_t     maxTotalSize;   // +0x018 Startup arg2 << 20; 0 == unlimited
    uint64_t     totalReserved;  // +0x020 AddNewMemoryRange accumulates
    rt::system::FreeTag* freeLists[192]; // +0x028 the head FreeTag of each bin (null == empty), 0x600 bytes
    int32_t      minBin;         // +0x628 search floor; ctor sets 0xbf
    int32_t      pad62c;         // +0x62c never written by any retail function
    int64_t      allocatedBytes; // +0x630 GetAllocatedSize
    int32_t      allocCount;     // +0x638 live blocks
    int32_t      freeTagCount;   // +0x63c linked free blocks
    Platform::LockStorage lock;  // +0x640 CRITICAL_SECTION (x64 MSVC: 40 bytes), built in place
    uint64_t     pageSize;       // +0x668 GetSystemInfo().dwPageSize
};

// ---------------------------------------------------------------------------
// THE INSTRUMENT SEAT — STANDALONE DEVIATION, retail has no such branch.
//
// One named hook pair, owned by the allocator's translation unit
// (src/engine/system/allocator.cpp defines it), consulted at the ENTRY of
// MemoryAllocator::MemAlloc and ::MemFree: when a pointer is non-null the call
// is handed to it whole and the retail body does not run. Both are null unless
// a diagnostic instrument armed them at boot from Options:
//   BANISHED_REDZONE (shim mode)  src/diag/win/redzone_alloc.cpp, RedzoneAlloc_Install
//   BANISHED_ALLOC_STATS          src/diag/alloc_cutover_arm.cpp, RtAllocSwapArmCutover
// An armed hook must perform the allocation itself WITHOUT re-entering
// MemAlloc/MemFree (it takes the lock at _singleton+0x640 and calls the raw
// Allocate/Free members), so an allocation is instrumented exactly once.
// No flag of its own (B3R_RULES R9).
// ---------------------------------------------------------------------------
// BANISHED_ASAN_HEAP: 1 in an AddressSanitizer build (CMakeLists.txt BANISHED_SANITIZE=asan|fuzz),
// where allocator.cpp arms the seat from the start with the C heap so ASan sees engine allocations.
#if defined(__SANITIZE_ADDRESS__)
#define BANISHED_ASAN_ON 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BANISHED_ASAN_ON 1
#endif
#endif
#if defined(BANISHED_ASAN_ON) && !defined(_WIN32)
#define BANISHED_ASAN_HEAP 1
#else
#define BANISHED_ASAN_HEAP 0
#endif
struct MemoryAllocatorInstrument {
    void* (*memAlloc)(uint64_t size, int align);
    void  (*memFree)(void* p);
};
extern MemoryAllocatorInstrument g_memoryAllocatorInstrument;

} // namespace System

namespace rtalloc {
// 0x180009c30 (unexported) -- `if (p) { Enter; Free(_singleton, p); Leave; }`
// with a SECOND parameter: the first is IGNORED (retail's callers pass `this`
// in RCX). src/engine/system/allocator.cpp. B3R RT-Core: declared so its
// readers call it by name.
void FreeLockedIgnoringSelf(void* ignoredThis, void* p);
}  // namespace rtalloc

namespace rt {
namespace system {

using MemoryAllocator = ::System::MemoryAllocator;

static_assert(sizeof(MemoryAllocator) == 0x670, "MemoryAllocator is 0x670 bytes");
static_assert(offsetof(MemoryAllocator, pad62c) == 0x62C, "MemoryAllocator::pad62c");
static_assert(offsetof(MemoryAllocator, ranges) == 0x000, "ranges");
static_assert(offsetof(MemoryAllocator, rangeCount) == 0x008, "rangeCount");
static_assert(offsetof(MemoryAllocator, rangeCapacity) == 0x00c, "rangeCapacity");
static_assert(offsetof(MemoryAllocator, minRangeSize) == 0x010, "minRangeSize");
static_assert(offsetof(MemoryAllocator, maxTotalSize) == 0x018, "maxTotalSize");
static_assert(offsetof(MemoryAllocator, totalReserved) == 0x020, "totalReserved");
static_assert(offsetof(MemoryAllocator, freeLists) == 0x028, "freeLists");
static_assert(offsetof(MemoryAllocator, minBin) == 0x628, "minBin");
static_assert(offsetof(MemoryAllocator, allocatedBytes) == 0x630, "allocatedBytes");
static_assert(offsetof(MemoryAllocator, allocCount) == 0x638, "allocCount");
static_assert(offsetof(MemoryAllocator, freeTagCount) == 0x63c, "freeTagCount");
static_assert(offsetof(MemoryAllocator, lock) == 0x640, "lock");
static_assert(offsetof(MemoryAllocator, pageSize) == 0x668, "pageSize");
// The by-value holders (Engine::_allocator, the shadow replica) rely on this:
// no user-declared ctor/dtor, a trivial operator=.
static_assert(std::is_trivially_copyable<MemoryAllocator>::value &&
              std::is_trivially_default_constructible<MemoryAllocator>::value,
              "MemoryAllocator must stay trivial: see the NOT DECLARED note on the class");

// ---------------------------------------------------------------------------
// Platform seam. See the PORTABILITY note above: the host installs this table,
// the port refuses to arm without it. Flag values are named after the Win32
// constants the retail code passes, because that is what the port must
// reproduce, not because the seam has to be Windows.
// ---------------------------------------------------------------------------
struct RtSysMemPlatform {
    // VirtualAlloc(addr, bytes, MEM_RESERVE,             PAGE_READWRITE)
    void*    (*reserve)(void* addr, uint64_t bytes);
    // VirtualAlloc(addr, bytes, MEM_RESERVE|MEM_COMMIT,  PAGE_READWRITE)
    void*    (*reserveCommit)(void* addr, uint64_t bytes);
    // VirtualAlloc(addr, bytes, MEM_COMMIT,              PAGE_READWRITE)
    void*    (*commit)(void* addr, uint64_t bytes);
    // VirtualFree(addr, bytes, MEM_DECOMMIT)
    void     (*decommit)(void* addr, uint64_t bytes);
    // VirtualFree(addr, 0, MEM_RELEASE)
    void     (*release)(void* addr);
    // GetSystemInfo().dwPageSize
    uint64_t (*pageSize)();
    // InitializeCriticalSection + SetCriticalSectionSpinCount(cs, 5000) / Enter /
    // Leave / Delete, over the +0x640 bytes (Platform::Mutex*: no allocation)
    void     (*lockInit)(Platform::LockStorage* cs);
    void     (*lockEnter)(Platform::LockStorage* cs);
    void     (*lockLeave)(Platform::LockStorage* cs);
    void     (*lockDelete)(Platform::LockStorage* cs);
};

} // namespace system
} // namespace rt

// Installed by the host (standalone/) before RtSystemAllocatorArm() runs.
// Declared extern "C" so the _win file can be a plain translation unit.
extern "C" void RtSysMemPlatformInstall(const rt::system::RtSysMemPlatform* p);

// ---- CUTOVER ARMING (Wave 9 lane E4) ---------------------------------------
//
// The host's hook into the allocator's arming (src/diag/alloc_cutover_arm.cpp,
// RtAllocSwapArmCutover): it arms BANISHED_ALLOC_STATS on the instrument seat.
// Returns false to refuse; RtSystemAllocatorArm() then reports REFUSED. (B3R
// W2-RES: it used to hand back the composed (locked) Allocate / Free entry
// points to be registered for ?Allocate@ / ?Free@; with the registry gone that
// hand-back had no reader and went.)
typedef bool (*RtAllocCutoverArmer)();
extern "C" void RtSystemAllocatorSetCutoverArmer(RtAllocCutoverArmer fn);

// src/engine/system/allocator.cpp -- installs the platform seam, calls the
// cutover armer and prints `[RtAlloc] ARMED` / `[RtAlloc] ATTEST`. Called once,
// from RuntimeInit() (src/app/runtime_init.cpp).
void RtSystemAllocatorArm();

// EXT SEAM (NOT RETAIL, docs/extensions/handins/HEAPSLACK.md): installs (or clears, with null)
// MemoryAllocator::_extCommitSeam while holding the live allocator's lock, so no commit or
// decommit is half-way through either policy. src/ext/heap_slack/heap_slack.cpp, once at boot.
void RtSystemAllocatorSetCommitSeam(const rt::system::MemoryAllocatorCommitSeam* seam);

// (The DLL-present detour-installer and lock-retire seams died with the DLL, ENDGAME A.3.)

// src/platform/win/allocator_win.cpp — installs the Win32 seam and holds the
// six exported Platform* bodies. The portable half calls this once, under
// #if defined(_WIN32); a _posix/_sdl twin provides the same symbol.
extern "C" void RtSystemAllocatorInstallPlatform();
