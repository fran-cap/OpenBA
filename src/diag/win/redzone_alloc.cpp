// src/diag/win/redzone_alloc.cpp — see redzone_alloc.h for design rationale.


#include "redzone_alloc.h"
#include "redzone_canary.h"            // RedzoneCanary::PadBytes / FormatHead
#include "options.h"
#include "thread_stack.h"           // ThreadStack::Capture: the other threads' stacks
#include "system/MemoryAllocator.h"   // -Iruntime/types: the instrument seat
#include "platform_sync.h"           // Platform::MutexLock / MutexUnlock on the allocator lock

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

// The perf build strips every unqualified printf (log_strip.h) — which silenced ALL of
// this file's reports in the 800x10 teardown hunt (empty standalone_debug.log while the
// process died in the allocator). Diagnostics here must survive the strip: route them to
// stderr, which the harness captures per run.
#ifdef printf
#undef printf
#endif
#define printf(...) std::fprintf(stderr, __VA_ARGS__)

#include "alloc_cutover_arm.h"   // RtAllocInstrument_MemAlloc / _MemFree: the shim's real pair

namespace {

// Tail-redzone padding (bytes) added to each allocation. DEFAULT 0 — padding
// every block shifts the whole engine heap layout, which HIDES layout-dependent
// free-list corruption (the OOB write lands on padding instead of a real tag).
// So by default we add NO padding (identical layout to an uninstrumented run →
// the bug still reproduces) and rely on the read-only HeapCheck walker to catch
// the corrupted boundary tag. Set BANISHED_REDZONE_PAD=<bytes> to re-enable the
// tail canary (useful only for contiguous overruns of OUR buffers).
size_t   g_pad      = 0;
bool     g_zeroOnAlloc = false;       // BANISHED_REDZONE_ZERO=1 diagnostic
constexpr uint8_t kCanary   = 0xAB;   // fill byte for the tail redzone
constexpr int    kBtFrames  = 8;      // captured alloc backtrace depth

struct Entry {
    size_t   reqSize;             // bytes the caller actually asked for
    uint32_t seq;                 // monotonic allocation id
    void*    bt[kBtFrames];       // alloc call stack (raw return addresses)
    uint16_t btCount;
};

// The shim's own bookkeeping lives on the CRT heap (std::unordered_map / mutex
// call malloc, NOT the engine allocator), so there is no re-entrancy with the engine
// allocator we are wrapping.
std::unordered_map<void*, Entry>* g_live = nullptr;
std::mutex                         g_mutex;

void* (*g_realAlloc)(uint64_t size, int alignment) = nullptr;
void  (*g_realFree)(void* ptr)                    = nullptr;
bool               g_enabled   = false;
uint32_t           g_seq       = 0;

// Periodic auto-scan: validate every live canary once every g_scanEvery
// alloc/free operations, so an overflow is caught within g_scanEvery ops of the
// bad write (not only at the hand-placed RZ_CHECK checkpoints). 0 = disabled
// (checkpoints only). Set via BANISHED_REDZONE_SCAN=<N>. Heavy (O(live) per
// scan) but fine for diagnosing a one-shot menu-load corruption.
uint32_t g_scanEvery = 0;
uint32_t g_opCounter = 0;

// Watchdog thread: walks the heap in a tight loop CONCURRENTLY with everything
// else, so corruption introduced by Runtime-internal code (between our own
// alloc/free calls) is caught within ~g_watchIntervalMs of the bad write —
// before the fatal Allocate reuses the clobbered free block. Safe because the
// walk takes the allocator's own critical section. Enabled by
// BANISHED_REDZONE_WATCH=<intervalMs> (0 = tight spin with Sleep(0)).
int  g_watchIntervalMs = -1;   // -1 = disabled

// Module base for printing exe-relative backtrace addresses (so they map
// straight onto Ghidra's 0x140000000-based addresses).
uintptr_t g_exeBase = 0;

inline uint8_t* CanaryPtr(void* userPtr, size_t reqSize) {
    return reinterpret_cast<uint8_t*>(userPtr) + reqSize;  // CAST-OK: alloc_header
}

bool CanaryIntact(void* userPtr, size_t reqSize) {
    if (g_pad == 0) return true;  // no canary when padding disabled
    const uint8_t* c = CanaryPtr(userPtr, reqSize);
    for (size_t i = 0; i < g_pad; ++i) {
        if (c[i] != kCanary) return false;
    }
    return true;
}

// Forward decl — comprehensive boundary-tag walker (defined below). Caller
// holds g_mutex.
} // namespace
static int HeapCheckLocked(const char* tag, bool report = true);
namespace {

// Canary reports must go to stderr as well: stdout is fully buffered after the
// freopen to standalone_debug.log and an ExitProcess right after the report was
// observed to lose the whole thing (exit 0x122 with a 90-byte debug log).
void RZBoth(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    printf("%s", buf);
    fputs(buf, stderr);
    fflush(stdout);
    fflush(stderr);
}

void PrintEntry(const char* why, void* userPtr, const Entry& e) {
    // Find how far the corruption extends (first + last bad byte).
    const uint8_t* c = CanaryPtr(userPtr, e.reqSize);
    int firstBad = -1, lastBad = -1;
    for (int i = 0; i < (int)g_pad; ++i) {
        if (c[i] != kCanary) { if (firstBad < 0) firstBad = i; lastBad = i; }
    }
    RZBoth("[REDZONE] %s ptr=%p reqSize=%zu (0x%zx) seq=%u "
           "overflow bytes [%d..%d] of %zu\n",
           why, userPtr, e.reqSize, e.reqSize, e.seq,
           firstBad, lastBad, g_pad);
    // Dump the canary's first bytes (at most 16, never past the pad) — the overflow
    // payload identifies the writer.
    char head[96];
    if (RedzoneCanary::FormatHead(head, sizeof(head), c, g_pad)) RZBoth("%s", head);
    // First user qword = vtable for engine objects; names the corrupted block's type.
    if (!IsBadReadPtr(userPtr, 8))
        RZBoth("[REDZONE]   user+0 qword: 0x%016llx\n",
               (unsigned long long)*reinterpret_cast<uint64_t*>(userPtr));  // CAST-OK: alloc_header
    char bt[512]; int off = 0;
    off += snprintf(bt + off, sizeof(bt) - off, "[REDZONE]   alloc backtrace:");
    for (int i = 0; i < e.btCount && off < (int)sizeof(bt) - 32; ++i) {
        uintptr_t a = reinterpret_cast<uintptr_t>(e.bt[i]);  // CAST-OK: crash_walk
        if (g_exeBase && a >= g_exeBase && a < g_exeBase + 0x600000) {
            // exe-relative → Ghidra address (image base 0x140000000)
            off += snprintf(bt + off, sizeof(bt) - off, " 0x%llx",
                            (unsigned long long)(0x140000000ull + (a - g_exeBase)));
        } else {
            off += snprintf(bt + off, sizeof(bt) - off, " [%p]", e.bt[i]);
        }
    }
    RZBoth("%s\n", bt);
}

// Caller MUST hold g_mutex. Scans every live block's tail canary; logs each
// corrupted one and (unless BANISHED_REDZONE_ABORT=0) ExitProcess()es cleanly
// at the first corruption — before the bad block is ever freed, which is what
// otherwise deadlocks the allocator critical section into a hang.
// Print the CURRENT call stack (the alloc/free that triggered the scan). With
// SCAN=1 the overflow happened between the previous allocator op and this one,
// so this backtrace brackets the writer to a single code window.
void PrintHereBacktrace(const char* what) {
    void* bt[kBtFrames];
    USHORT n = CaptureStackBackTrace(2, kBtFrames, bt, nullptr);
    char buf[512]; int off = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "[REDZONE]   %s backtrace:", what);
    for (USHORT i = 0; i < n && off < (int)sizeof(buf) - 32; ++i) {
        uintptr_t a = reinterpret_cast<uintptr_t>(bt[i]);  // CAST-OK: crash_walk
        if (g_exeBase && a >= g_exeBase && a < g_exeBase + 0x600000) {
            off += snprintf(buf + off, sizeof(buf) - off, " 0x%llx",
                            (unsigned long long)(0x140000000ull + (a - g_exeBase)));
        } else {
            off += snprintf(buf + off, sizeof(buf) - off, " [%p]", bt[i]);
        }
    }
    RZBoth("%s\n", buf);
}

int ValidateAllLocked(const char* tag) {
    int bad = 0;
    for (auto& kv : *g_live) {
        if (!CanaryIntact(kv.first, kv.second.reqSize)) {
            if (bad == 0) {
                RZBoth("[REDZONE] ==== corruption detected at checkpoint '%s' ====\n",
                       tag ? tag : "?");
            }
            PrintEntry("CORRUPT", kv.first, kv.second);
            ++bad;
        }
    }
    if (bad) {
        PrintHereBacktrace("trip-site");
        RZBoth("[REDZONE] checkpoint '%s': %d corrupted block(s), %zu live\n",
               tag ? tag : "?", bad, g_live->size());
        if (Opt().redzoneAbort) {
            RZBoth("[REDZONE] aborting cleanly at first corruption "
                   "(set BANISHED_REDZONE_ABORT=0 to continue)\n");
            ExitProcess(0x122);  // distinctive exit code for "redzone tripped"
        }
    }
    return bad;
}

void* ShimAlloc(uint64_t size, int alignment) {
    void* p = g_realAlloc(size + g_pad, alignment);
    if (!p) return p;  // alloc failed — nothing to stamp
    // Diagnostic (BANISHED_REDZONE_ZERO=1): zero-fill every allocation we hand
    // out. The engine allocator is a boundary-tag free-list over VirtualAlloc
    // and does NOT re-zero reused freed blocks, so a reused block returns with
    // stale bytes. If our reimpl reads an uninitialized slot as a pointer (e.g.
    // a not-yet-deserialized External*/Filename) it then writes through garbage
    // → heap corruption. Zeroing here (PAD=0, so NO layout change) tests that
    // "uninitialized/stale pointer" hypothesis: if the intermittent menu crash
    // vanishes with this on, the bug is a stale-read; if it persists, it's a
    // genuine OOB overrun or a Runtime-side allocation we don't intercept.
    if (g_zeroOnAlloc) memset(p, 0, (size_t)size);
    if (g_pad) memset(CanaryPtr(p, (size_t)size), kCanary, g_pad);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        Entry e{};
        e.reqSize = (size_t)size;
        e.seq     = ++g_seq;
        e.btCount = (uint16_t)CaptureStackBackTrace(1, kBtFrames, e.bt, nullptr);
        (*g_live)[p] = e;
        if (g_scanEvery && (++g_opCounter % g_scanEvery) == 0) {
            ValidateAllLocked("auto-scan(alloc)");
            HeapCheckLocked("auto-scan(alloc)");
        }
    }
    return p;
}

// The pair the allocator's instrument seat (System::g_memoryAllocatorInstrument,
// MemoryAllocator.h) holds in shim mode, so every System::MemoryAllocator::
// MemAlloc / MemFree caller is shimmed, and an allocation and its free always
// go through the same channel. (Before the seat, a block allocated through one
// door and freed through another left a stale g_live entry, and the canary scan
// reported a PHANTOM overflow when the engine reused the memory -- seen twice as
// a "corrupt decal vertex pool" at town load.) `_singleton` is read LIVE
// (retail MemAlloc's own read), inside the real pair RtAllocInstrument_MemAlloc
// / _MemFree.
void ShimFree(void* ptr);
void* ShimAllocStatic(uint64_t size, int alignment) {
    return ShimAlloc(size, alignment);
}
void ShimFreeStatic(void* ptr) {
    ShimFree(ptr);
}

void ShimFree(void* ptr) {
    if (ptr) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_live->find(ptr);
        if (it != g_live->end()) {
            if (!CanaryIntact(ptr, it->second.reqSize)) {
                PrintEntry("CORRUPT-ON-FREE", ptr, it->second);
            }
            g_live->erase(it);
        }
        // The crash always manifests inside the allocator's Free coalescing
        // (it follows a clobbered boundary tag), so frees participate in the
        // same throttled op counter as allocs. Scanning EVERY free was O(live)
        // per free and made loading take minutes once both allocator channels
        // were shimmed (~20k live blocks × constant frees).
        if (g_scanEvery && (++g_opCounter % g_scanEvery) == 0) {
            ValidateAllLocked("auto-scan(pre-free)");
            HeapCheckLocked("auto-scan(pre-free)");
        }
    }
    g_realFree(ptr);
}

} // namespace

static DWORD WINAPI RedzoneWatchdog(LPVOID);
static DWORD WINAPI RedzoneSelfTest(LPVOID);   // negative control — see below
static void   RedzoneStopWatchdog();           // atexit stop — see below
static volatile LONG g_watchStop   = 0;
static HANDLE        g_watchThread = nullptr;
// Completed whole-heap walks. Printed at exit because "0 violations" is worth
// nothing without it: it separates "the heap was clean" from "the walk never
// ran", which are the two readings a silent instrument cannot tell apart.
static volatile LONG g_watchWalks  = 0;
static DWORD  g_mainTid    = 0;      // thread that ran Install (= the sim thread)
static HANDLE g_mainThread = nullptr;

// Start the watchdog (and the optional self-test injector) exactly once, from
// either install path, so the stop handler can never be skipped for one of them.
static void RedzoneStartWatchdog() {
    g_watchThread = CreateThread(nullptr, 0, &RedzoneWatchdog, nullptr, 0, nullptr);
    atexit(&RedzoneStopWatchdog);
    {
        int ms = Opt().redzoneSelfTestMs;
        if (ms > 0) {
            printf("[REDZONE-SELFTEST] armed: will clobber one boundary tag at t=%dms\n", ms);
            fflush(stdout);
            CreateThread(nullptr, 0, &RedzoneSelfTest, (LPVOID)(intptr_t)ms, 0, nullptr);  // CAST-OK: win32
        }
    }
}

void RedzoneAlloc_Install() {
    const char* env = Opt().redzone;
    if (!env || env[0] == '0' || env[0] == 'f' || env[0] == 'F') {
        return;  // disabled — leave the real allocator pointers in place
    }
    // BANISHED_REDZONE=watch — WATCH-ONLY mode: no alloc/free shim at all (the shim's
    // per-op bookkeeping changes timing enough to hide a cross-thread race; the 800x10
    // teardown corruption reproduces plain and vanished under the shim). Only the
    // heap-walking watchdog thread runs, so the corrupt tag is reported within
    // ~BANISHED_REDZONE_WATCH ms of the write together with EVERY thread's stack.
    if (env[0] == 'w' || env[0] == 'W') {
        g_live = new std::unordered_map<void*, Entry>();
        g_exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));  // CAST-OK: crash_walk
        g_mainTid = GetCurrentThreadId();
        g_mainThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                                  FALSE, g_mainTid);
        if (Opt().redzoneWatchMs != -1) g_watchIntervalMs = Opt().redzoneWatchMs;
        if (g_watchIntervalMs < 0) g_watchIntervalMs = 1;
        g_enabled = true;
        printf("[REDZONE] WATCH-ONLY mode: no shim, watchdog=%dms\n", g_watchIntervalMs);
        fflush(stdout);
        RedzoneStartWatchdog();
        return;
    }
    g_live = new std::unordered_map<void*, Entry>();
    g_live->reserve(8192);
    // g_realAlloc / g_realFree are the instrument seat's composed pair,
    // RtAllocInstrument_MemAlloc / _MemFree (lock + raw Allocate / Free +
    // ALLOCSTATS) — NOT System::MemoryAllocator::MemAlloc / MemFree, which
    // consult the instrument seat armed below: ShimFree -> MemFree -> seat ->
    // ShimFree would instrument every call twice. Neither of the two reaches
    // the seat, so a shimmed call is instrumented exactly once.
    g_realAlloc = &RtAllocInstrument_MemAlloc;
    g_realFree  = &RtAllocInstrument_MemFree;
    g_exeBase   = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));  // CAST-OK: crash_walk
    if (Opt().redzoneScan > 0) g_scanEvery = (uint32_t)Opt().redzoneScan;
    g_pad = RedzoneCanary::PadBytes(Opt().redzonePad);  // 8-byte multiple, 0 = off
    if (Opt().redzoneWatchMs != -1) {
        g_watchIntervalMs = Opt().redzoneWatchMs;  // 0 = tight spin
    }
    g_zeroOnAlloc = Opt().redzoneZero;
    // The allocator's instrument seat (B3R W0-A0, STANDALONE DEVIATION in
    // allocator.cpp): every MemoryAllocator::MemAlloc / MemFree caller reaches
    // the shim. Always taken, overriding the ALLOCSTATS pair if that armed
    // first: the shim feeds the stats underneath.
    System::g_memoryAllocatorInstrument.memAlloc = &ShimAllocStatic;
    System::g_memoryAllocatorInstrument.memFree  = &ShimFreeStatic;
    printf("[REDZONE] instrument seat armed: MemoryAllocator::MemAlloc/MemFree -> shim\n");
    g_mainTid = GetCurrentThreadId();
    g_mainThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                              FALSE, g_mainTid);
    g_enabled = true;
    printf("[REDZONE] installed: tailPad=%zu bytes (0=native layout), zeroOnAlloc=%d, "
           "autoScanEvery=%u, watchdog=%dms, real alloc=%p free=%p exeBase=%p\n",
           g_pad, (int)g_zeroOnAlloc, g_scanEvery, g_watchIntervalMs,
           (void*)g_realAlloc, (void*)g_realFree, (void*)g_exeBase);
    fflush(stdout);

    if (g_watchIntervalMs >= 0) {
        RedzoneStartWatchdog();
    }
}

bool RedzoneAlloc_Enabled() { return g_enabled; }

// Forward decl — RZOut is defined in a later anonymous-namespace block (anon
// namespaces merge across the TU, so this refers to the same function).
namespace { void RZOut(const char* fmt, ...); }

int RedzoneAlloc_ValidateAll(const char* tag) {
    if (!g_enabled) return 0;
    std::lock_guard<std::mutex> lk(g_mutex);
    return ValidateAllLocked(tag);
}

// Free-list LINK validation, callable at a code checkpoint (e.g. between
// deserialize steps) to find the exact step that corrupts a freed block's
// forward link. Walks every size bucket's free chain and validates each
// signed-41-bit relative forward link points back into a committed range.
// On the first corrupt link it prints the checkpoint tag + the clobbered
// block body, then aborts (exit 0x124) so the tag names the culprit step.
// Quiescent-allocator assumption: only call from single-threaded reimpl code
// (no concurrent Allocate mutating the free list). Enabled only under
// BANISHED_REDZONE (so production runs are unaffected).
extern "C" int RedzoneAlloc_CheckLinks(const char* tag) {
    // One live read (B3R STORAGE): the Engine dtor nulls _singleton.
    System::MemoryAllocator* alloc = System::MemoryAllocator::_singleton;
    if (!g_enabled || !alloc) return 0;
    if (IsBadReadPtr(alloc, 0x18)) return 0;
    rt::system::MemoryRange* ranges   = alloc->ranges;   // +0x00
    int      rangeCnt = alloc->rangeCount;   // +0x08
    if (!ranges || rangeCnt <= 0 || rangeCnt >= 4096) return 0;

    auto inAnyRange = [&](uint8_t* a) -> bool {
        for (int r = 0; r < rangeCnt; ++r) {
            rt::system::MemoryRange* rg = &ranges[r];
            if (IsBadReadPtr(rg, 0x18)) continue;
            uint8_t* s = rg->base;
            uint8_t* e = rg->end;
            if (s && e && a >= s && a < e) return true;
        }
        return false;
    };

    for (int bucket = 0; bucket < 0xC0; ++bucket) {
        uint8_t* curFT = reinterpret_cast<uint8_t*>(alloc->freeLists[bucket]);  // the walk steps the FreeTag as raw block bytes  // CAST-OK: alloc_header
        uint8_t* prevFT = nullptr;
        int guard = 0;
        while (curFT && ++guard < 2000000) {
            uint8_t* block = curFT - 8;
            if (IsBadReadPtr(curFT, 8) || !inAnyRange(block)) {
                RZOut("[REDZONE-LINK] CORRUPTION first seen at checkpoint '%s' "
                      "(bucket %d): free node @%p (block %p) out of range; "
                      "prev node @%p\n",
                      tag ? tag : "?", bucket, (void*)curFT, (void*)block, (void*)prevFT);
                fflush(stdout);
                ExitProcess(0x124);
            }
            uint64_t link = *reinterpret_cast<uint64_t*>(curFT);  // CAST-OK: alloc_header
            int64_t off = (int64_t)(link << 23) >> 23;
            if (off == 0) break;
            uint8_t* next = curFT + off;
            if (IsBadReadPtr(next, 8) || !inAnyRange(next - 8)) {
                RZOut("[REDZONE-LINK] CORRUPTION first seen at checkpoint '%s' "
                      "(bucket %d): free block @%p forward link qword=%016llx "
                      "off=%lld -> next=%p (out of range)\n",
                      tag ? tag : "?", bucket, (void*)block,
                      (unsigned long long)link, (long long)off, (void*)next);
                fflush(stdout);
                ExitProcess(0x124);
            }
            prevFT = curFT;
            curFT = next;
        }
    }
    return 0;
}

// ============================================================
// Heap-integrity walker over the engine allocator's boundary tags.
// ============================================================
// Catches corruption of ANY block (live OR free, ours OR Runtime's) — unlike
// the tail canaries which only cover our live blocks. Layout (from the Allocate
// decompile at Runtime 0x1800c6e60):
//   MemoryAllocator* this  = System::MemoryAllocator::_singleton
//     +0x00  MemoryRange*  ranges            (array)
//     +0x08  int           rangeCount
//     +0x640 CRITICAL_SECTION                 (alloc/free lock)
//   MemoryRange (stride 0x18):
//     +0x00  uint8_t*      firstBlock
//     +0x08  uint8_t*      end (one past last block)
//     +0x10  uint64_t      committedBytes
//   Block: tag = *(uint64_t*)block;  blockBytes = (tag & 0x1ffffffffff) * 4;
//          next = block + blockBytes;  user = block + 8.
// A block is corrupt if blockBytes < 8 or block+blockBytes overruns the range.
namespace {
// IsBadReadPtr probes with SEH and costs ~1 us, which made the boundary-tag walk
// (one call per block, ~1M blocks on the 800-citizen town) take seconds — far too
// slow for the 1 ms watchdog or a per-step teardown bracket. One VirtualQuery
// answers for a whole committed region, so the readable window is cached per
// thread and only re-queried when a probe leaves it. Same answer, ~100x cheaper.
bool ReadablePtr(const void* p, size_t n) {
    if (!p || n == 0) return false;
    static thread_local const uint8_t* s_lo = nullptr;
    static thread_local const uint8_t* s_hi = nullptr;
    const uint8_t* b = static_cast<const uint8_t*>(p);  // CAST-OK: crash_walk
    if (b >= s_lo && b + n <= s_hi) return true;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) { s_lo = s_hi = nullptr; return false; }
    const DWORD noRead = PAGE_NOACCESS | PAGE_GUARD | PAGE_EXECUTE;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & noRead) != 0 || mbi.Protect == 0) {
        s_lo = s_hi = nullptr;
        return false;
    }
    s_lo = static_cast<const uint8_t*>(mbi.BaseAddress);  // CAST-OK: crash_walk
    s_hi = s_lo + mbi.RegionSize;
    return b >= s_lo && b + n <= s_hi;
}

// Optional secondary output sink for the crash-context heap dump. The crash
// handler sets this to a write-through (FILE_FLAG_WRITE_THROUGH) logger so the
// dump's findings survive even if a re-entrant fault kills the process
// mid-walk. printf-to-stdout (debug.log) is unbuffered but was observed to
// produce NO output in crash context (the dump faults before its first line
// commits), so this sink is the reliable path. RZOut() emits to BOTH.
void (*g_rzSink)(const char*) = nullptr;

void RZOut(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    printf("%s", buf);
    if (g_rzSink) g_rzSink(buf);
}

// Resolve a code/vtable address to "module.dll+0xOFF" for identifying the
// owning class. Engine objects store their vtable pointer at +0x00, so dumping
// the preceding block's first qword and resolving it names the overflower.
void PrintModOff(const char* label, uint64_t addr) {
    uint64_t base = 0;
    char name[MAX_PATH];
    // ThreadStack::ModuleAt: no loader lock (review F-297).
    if (addr && ThreadStack::ModuleAt(addr, &base, name, sizeof(name))) {
        RZOut("[REDZONE-HEAP]   %s = %s+0x%llX\n", label, name, (unsigned long long)(addr - base));
    } else {
        RZOut("[REDZONE-HEAP]   %s = 0x%016llx (not in a module)\n",
               label, (unsigned long long)addr);
    }
}
}

// Core walker. Caller MUST hold g_mutex (it reads g_live). Takes the
// allocator's own critical section internally. When report==false it only
// detects + records the first bad block in g_lastBadBlock (no print, no abort)
// — used by the watchdog to confirm a corruption persists across two walks
// before trusting it (Allocate runs WITHOUT the allocator lock, so a single
// walk can momentarily observe a mid-flight tag = false positive).
static uint8_t* g_lastBadBlock = nullptr;
extern "C" int AllocStats_Lookup(void*, uint64_t*, void**, void**);   // allocator_lock.cpp
static bool   g_reportedOnce = false;
extern "C" void StressHarness_Context(char* buf, int n);

// Print the MAIN thread's stack (from another thread: suspend, read context,
// unwind) so a watchdog detection names what the sim was executing when the
// corrupt tag was first seen. ThreadStack::Capture walks it without taking a lock and
// resumes it before the module lookups and the printf: the printf can wait on a CRT
// lock the suspended thread holds. The module lookups (ThreadStack::ModuleAt) take no
// loader lock, so a thread that holds it for good does not stop the report either.
static void DumpOneThreadStack(HANDLE th, DWORD tid, const char* label) {
    constexpr int kFrames = 40;
    ThreadStack::Frame frames[kFrames];
    CONTEXT ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    const ThreadStack::Result r = ThreadStack::Capture(th, ctx, frames, kFrames);
    if (r.status == ThreadStack::Status::kSuspendFailed) {
        printf("[REDZONE-HEAP] SuspendThread(%s tid=%lu) failed err=%lu\n", label, tid, r.error);
    } else if (r.status == ThreadStack::Status::kContextFailed) {
        printf("[REDZONE-HEAP] GetThreadContext(%s) failed err=%lu\n", label, r.error);
    } else {
        printf("[REDZONE-HEAP] %s thread tid=%lu stack at detection:\n", label, tid);
        for (int frame = 0; frame < r.frames; frame++) {
            const uint64_t rip = frames[frame].rip;
            uint64_t base = 0;
            char name[MAX_PATH];
            // ModuleAt, not GetModuleHandleExA: no loader lock (thread_stack.h, review F-297).
            if (ThreadStack::ModuleAt(rip, &base, name, sizeof(name))) {
                printf("[REDZONE-HEAP]   #%-2d %-18s + 0x%-8llX\n", frame, name,
                       (unsigned long long)(rip - base));
            } else {
                printf("[REDZONE-HEAP]   #%-2d ??? @ %p\n", frame, (void*)(uintptr_t)rip);  // CAST-OK: crash_walk
            }
        }
    }
}

// Every thread of the process except the caller: the corrupter may be the pathfinder
// worker, not the sim thread, so one stack is not enough to name it.
static void DumpMainThreadStack() {
    const DWORD self = GetCurrentThreadId();
    if (g_mainThread && self != g_mainTid) DumpOneThreadStack(g_mainThread, g_mainTid, "MAIN");
    else {
        printf("[REDZONE-HEAP] detected on the main thread itself — caller backtrace:\n");
        void* bt[32];
        USHORT n = CaptureStackBackTrace(0, 32, bt, nullptr);
        for (USHORT i = 0; i < n; ++i) {
            const uint64_t rip = (uintptr_t)bt[i];  // CAST-OK: crash_walk
            uint64_t base = 0;
            char name[MAX_PATH];
            if (ThreadStack::ModuleAt(rip, &base, name, sizeof(name))) {
                printf("[REDZONE-HEAP]   #%-2d %-18s + 0x%llX\n", i, name, (unsigned long long)(rip - base));
            } else {
                printf("[REDZONE-HEAP]   #%-2d ??? @ %p\n", i, bt[i]);
            }
        }
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te; te.dwSize = sizeof(te);
        const DWORD pid = GetCurrentProcessId();
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || te.th32ThreadID == g_mainTid)
                    continue;
                HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                                       FALSE, te.th32ThreadID);
                if (!th) continue;
                DumpOneThreadStack(th, te.th32ThreadID, "OTHER");
                CloseHandle(th);
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
    char sim[256] = {0};
    StressHarness_Context(sim, sizeof(sim));
    printf("[REDZONE-HEAP] sim context: %s\n", sim);
    fflush(stdout);
}

static int HeapCheckLocked(const char* tag, bool report) {
    // One live read (B3R STORAGE): the Engine dtor nulls _singleton.
    System::MemoryAllocator* alloc = System::MemoryAllocator::_singleton;
    if (!g_enabled || !alloc) return 0;
    // BANISHED_REDZONE_ABORT=0: after a report, keep quiet about THAT block (it stays
    // corrupt) but still report a different first-bad block (up to 8 reports).
    static int s_reports = 0;
    static uint8_t* s_lastReported = nullptr;
    if (g_reportedOnce && s_reports >= 8) return 0;

    // Serialize against concurrent alloc/free via the allocator's own lock.
    Platform::LockStorage* cs = &alloc->lock;
    Platform::MutexLock(cs);

    int bad = 0;
    uint8_t* badBlock = nullptr;   // first block whose tag is corrupt
    uint8_t* badPrev  = nullptr;   // physically-preceding block (overflow source)
    uint64_t badTag   = 0;
    size_t   badBytes = 0;
    int      badRange = -1;

    rt::system::MemoryRange* ranges   = alloc->ranges;   // +0x00
    int      rangeCnt = alloc->rangeCount;   // +0x08
    if (ranges && rangeCnt > 0 && rangeCnt < 4096) {
        for (int r = 0; r < rangeCnt && !bad; ++r) {
            rt::system::MemoryRange* rng   = &ranges[r];
            uint8_t* start = rng->base;
            uint8_t* end   = rng->end;
            if (!start || !end || end <= start) continue;

            uint8_t* prev = nullptr;
            uint8_t* p = start;
            while (p < end) {
                if (!ReadablePtr(p, 8)) {
                    badBlock = p; badPrev = prev; badRange = r; ++bad; break;
                }
                uint64_t blkTag = *reinterpret_cast<uint64_t*>(p);  // CAST-OK: alloc_header
                size_t blkBytes = (size_t)(blkTag & 0x1ffffffffffULL) * 4;
                if (blkBytes < 8 || p + blkBytes > end) {
                    badBlock = p; badPrev = prev; badRange = r;
                    badTag = blkTag; badBytes = blkBytes; ++bad; break;
                }
                prev = p;
                p += blkBytes;
            }
        }
    } else {
        printf("[REDZONE-HEAP] suspicious allocator state ranges=%p cnt=%d — skipping\n",
               (void*)ranges, rangeCnt);
    }

    Platform::MutexUnlock(cs);

    g_lastBadBlock = bad ? badBlock : nullptr;
    if (bad && !report) {
        return bad;  // detect-only (watchdog confirmation pass) — no print/abort
    }
    if (bad && g_reportedOnce && badBlock == s_lastReported) return bad;   // already reported

    if (bad) {
        ++s_reports; s_lastReported = badBlock;
        printf("[REDZONE-HEAP] ==== corruption at checkpoint '%s' (seq=%u) ====\n",
               tag ? tag : "?", g_seq);
        printf("[REDZONE-HEAP] range %d: bad block @%p tag=0x%016llx size=%zu (0x%zx)\n",
               badRange, (void*)badBlock, (unsigned long long)badTag, badBytes, badBytes);

        // (1) Physically-preceding block = likely overflow source. Dump its
        // raw header so its vtable pointer (user+0x00) names the overflowing
        // class. Also check our table (stale-flagged) for an alloc backtrace.
        if (badPrev && ReadablePtr(badPrev, 0x20)) {
            void* prevUser = badPrev + 8;
            const uint64_t* q = reinterpret_cast<const uint64_t*>(badPrev);  // CAST-OK: alloc_header
            printf("[REDZONE-HEAP] preceding block @%p raw: "
                   "tag=%016llx user[0]=%016llx user[1]=%016llx user[2]=%016llx\n",
                   (void*)badPrev,
                   (unsigned long long)q[0], (unsigned long long)q[1],
                   (unsigned long long)q[2], (unsigned long long)q[3]);
            // user+0x00 (=q[1]) is the vtable for an engine object — resolve it.
            PrintModOff("preceding user vtable[user+0]", q[1]);
            auto it = g_live->find(prevUser);
            if (it != g_live->end()) {
                size_t recBlock = ((it->second.reqSize + g_pad + 7) & ~size_t(7)) + 8;
                bool reaches = (badPrev + recBlock) >= badBlock;
                printf("[REDZONE-HEAP] preceding block IS OURS (user=%p) "
                       "recBlockBytes~%zu %s:\n", prevUser, recBlock,
                       reaches ? "" : "[STALE? footprint doesn't reach bad block]");
                PrintEntry("PRECEDING-OURS", prevUser, it->second);
            } else {
                printf("[REDZONE-HEAP] preceding block NOT in our table "
                       "(Runtime-allocated, or freed-stale)\n");
            }
        }

        // (1b) ALLOCSTATS owner lookup (BANISHED_ALLOC_STATS=1 records size + alloc
        // caller for every Allocate, including Runtime-internal ones): who owns the
        // preceding block and the bad block?
        {
            uint64_t sz = 0; void* caller = nullptr; void* actx = nullptr;
            if (badPrev && AllocStats_Lookup(badPrev + 8, &sz, &caller, &actx)) {
                printf("[REDZONE-HEAP] preceding block ALLOCSTATS: size=%llu ctx(array header)=%p caller=",
                       (unsigned long long)sz, actx);
                PrintModOff("alloc-caller", (uint64_t)(uintptr_t)caller);  // CAST-OK: crash_walk
                if (actx && ReadablePtr((uint8_t*)actx - 0x40, 0x60)) {  // CAST-OK: crash_walk
                    const uint64_t* q = reinterpret_cast<const uint64_t*>((uint8_t*)actx - 0x40);  // CAST-OK: crash_walk
                    printf("[REDZONE-HEAP]   owner struct around header (header at +0x40):\n");
                    for (int i = 0; i < 12; i += 2)
                        printf("[REDZONE-HEAP]     %p: %016llx %016llx\n", (void*)((uint8_t*)actx - 0x40 + i * 8),  // CAST-OK: crash_walk
                               (unsigned long long)q[i], (unsigned long long)q[i + 1]);
                    PrintModOff("owner candidate vtable (header-0x40)", q[0]);
                }
            }
            if (AllocStats_Lookup(badBlock + 8, &sz, &caller, &actx)) {
                printf("[REDZONE-HEAP] bad block ALLOCSTATS: size=%llu ctx=%p caller=", (unsigned long long)sz, actx);
                PrintModOff("alloc-caller", (uint64_t)(uintptr_t)caller);  // CAST-OK: crash_walk
            }
        }

        // (2) Any live block of OURS that physically CONTAINS the bad tag
        // address — the genuine victim whose tag was clobbered.
        for (auto& kv : *g_live) {
            uint8_t* u = reinterpret_cast<uint8_t*>(kv.first);  // CAST-OK: alloc_header
            uint8_t* blk = u - 8;
            size_t recBlock = ((kv.second.reqSize + g_pad + 7) & ~size_t(7)) + 8;
            if (badBlock >= blk && badBlock < blk + recBlock) {
                printf("[REDZONE-HEAP] bad tag falls INSIDE our block @%p (user=%p):\n",
                       (void*)blk, (void*)u);
                PrintEntry("CONTAINS-BADTAG", (void*)u, kv.second);
            }
        }

        // (3) Most-recent live allocations by seq — temporal suspects (immune
        // to the spatial walk derailing on the first bad tag). The bad write
        // usually happened shortly before detection.
        {
            const int kTop = 8;
            const std::pair<void* const, Entry>* top[kTop] = {nullptr};
            for (auto& kv : *g_live) {
                for (int i = 0; i < kTop; ++i) {
                    if (!top[i] || kv.second.seq > top[i]->second.seq) {
                        for (int j = kTop - 1; j > i; --j) top[j] = top[j-1];
                        top[i] = &kv; break;
                    }
                }
            }
            printf("[REDZONE-HEAP] most-recent %d live allocations (temporal suspects):\n", kTop);
            for (int i = 0; i < kTop && top[i]; ++i) {
                PrintEntry("RECENT", top[i]->first, top[i]->second);
            }
        }

        fflush(stdout);
        DumpMainThreadStack();
        // Dump the 64 bytes around the bad tag: the clobber VALUE identifies the writer
        // (a refcount bump, a state int, a pointer) better than any backtrace of the victim.
        if (ReadablePtr(badBlock - 0x20, 0x60)) {
            const uint64_t* q = reinterpret_cast<const uint64_t*>(badBlock - 0x20);  // CAST-OK: alloc_header
            for (int i = 0; i < 12; i += 2)
                printf("[REDZONE-HEAP]   %p: %016llx %016llx\n", (void*)(badBlock - 0x20 + i * 8),
                       (unsigned long long)q[i], (unsigned long long)q[i + 1]);
        }
        fflush(stdout);
        if (Opt().redzoneAbort) {
            printf("[REDZONE-HEAP] aborting cleanly at heap corruption\n");
            fflush(stdout);
            ExitProcess(0x123);  // distinctive code for "heap walker tripped"
        }
        g_reportedOnce = true;
    }
    return bad;
}

// BANISHED_TEARDOWN_HEAPCHECK=1 (with BANISHED_REDZONE=watch): once GameUpdateLoop_Shutdown
// begins, walk the whole heap on EVERY Allocate/Free (allocator_lock.cpp calls in). The
// 800x10 teardown corruption never shows up during the run (watchdog silent for 9000
// frames) but the process dies in EntityList_Shutdown, so the bad write happens between
// two teardown heap ops — this names the op right after it, with a backtrace.
int g_teardownHeapCheck = 0;
extern "C" void RedzoneAlloc_ArmTeardownCheck() {
    if (!g_enabled || !Opt().teardownHeapCheck) return;
    g_teardownHeapCheck = 1;
    printf("[REDZONE-HEAP] teardown per-op heap check ARMED\n");
    fflush(stdout);
    RedzoneAlloc_HeapCheck("teardown-arm");
}
extern "C" void RedzoneAlloc_TeardownOp(const char* what) {
    if (!g_teardownHeapCheck) return;
    // Per thread: the callers (alloc_cutover_arm.cpp) no longer hold the allocator
    // lock here (review F-287), and the guard is only against the walk re-entering.
    static thread_local int depth = 0;
    if (depth) return;          // the walk itself must not re-enter
    ++depth;
    RedzoneAlloc_HeapCheck(what);
    --depth;
}

int RedzoneAlloc_HeapCheck(const char* tag) {
    if (!g_enabled) return 0;
    std::lock_guard<std::mutex> lk(g_mutex);
    return HeapCheckLocked(tag);
}

// Shim-INDEPENDENT heap dump for the crash handler. Walks the engine
// allocator's boundary tags WITHOUT requiring the redzone shim to be installed
// (so it works on a fully-native run — no layout perturbation, natural crash
// rate). On the SIGSEGV from free-list poisoning, this finds the first corrupt
// block and dumps the physically-PRECEDING block's first qword (its vtable for
// an engine object) resolved to module+offset = the class whose buffer
// overflowed. Does NOT take the allocator critical section (crash context).
// Install a secondary, write-through output sink for the crash-context dump.
// The crash handler points this at its FILE_FLAG_WRITE_THROUGH logger so the
// dump survives even if a re-entrant fault terminates the process mid-walk.
extern "C" void RedzoneAlloc_SetDumpSink(void (*sink)(const char*)) {
    g_rzSink = sink;
}

// Shim-INDEPENDENT, SILENT boundary-tag scan. Same walk as RedzoneAlloc_HeapDump
// but it prints nothing and returns the first corrupt block (null when clean), so
// it can be called at high frequency to BRACKET the moment corruption appears.
//
// Wave 9 lane N, Finding 3: the heap is provably intact at teardown entry and
// corrupt by the fault, so the store happens inside the teardown cascade. Calling
// this after each entity removal narrows "somewhere in teardown" to one removal,
// which is what names the writer — the debug registers cannot help while the
// victim address is unknown until after it has been written.
extern "C" void* RedzoneAlloc_FindCorruptBlockNative() {
    System::MemoryAllocator* alloc = System::MemoryAllocator::_singleton;   // one live read (B3R STORAGE)
    if (!alloc) return nullptr;
    if (IsBadReadPtr(alloc, 0x18)) return nullptr;
    rt::system::MemoryRange* ranges   = alloc->ranges;   // +0x00
    int      rangeCnt = alloc->rangeCount;   // +0x08
    if (!ranges || rangeCnt <= 0 || rangeCnt >= 4096) return nullptr;

    for (int r = 0; r < rangeCnt; ++r) {
        rt::system::MemoryRange* rng = &ranges[r];
        if (IsBadReadPtr(rng, 0x18)) continue;
        uint8_t* start = rng->base;
        uint8_t* end   = rng->end;
        if (!start || !end || end <= start) continue;
        uint8_t* p = start;
        int guard = 0;
        // `IsBadReadPtr` per block made this walk ~2 s on the 800-citizen town,
        // which is too slow to bracket every teardown step. One VirtualQuery
        // covers a whole committed region, so the readable limit is advanced in
        // region-sized jumps instead of probed per block.
        uint8_t* safeEnd = p;
        while (p < end && ++guard < 4000000) {
            if (p + 8 > safeEnd) {
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(p, &mbi, sizeof(mbi))) return p;
                if (mbi.State != MEM_COMMIT ||
                    (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return p;
                safeEnd = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;  // CAST-OK: crash_walk
                if (p + 8 > safeEnd) return p;
            }
            uint64_t blkTag = *reinterpret_cast<uint64_t*>(p);  // CAST-OK: alloc_header
            size_t blkBytes = (size_t)(blkTag & 0x1ffffffffffULL) * 4;
            if (blkBytes < 8 || p + blkBytes > end) return p;
            p += blkBytes;
        }
    }
    return nullptr;
}

extern "C" void RedzoneAlloc_HeapDump(const char* tag) {
    System::MemoryAllocator* alloc = System::MemoryAllocator::_singleton;   // one live read (B3R STORAGE)
    if (!alloc) {
        RZOut("[REDZONE-DUMP] no allocator singleton\n"); fflush(stdout);
        return;
    }
    if (IsBadReadPtr(alloc, 0x18)) { RZOut("[REDZONE-DUMP] singleton unreadable\n"); return; }
    rt::system::MemoryRange* ranges   = alloc->ranges;   // +0x00
    int      rangeCnt = alloc->rangeCount;   // +0x08
    RZOut("[REDZONE-DUMP] '%s': allocator=%p ranges=%p cnt=%d\n",
           tag ? tag : "?", (void*)alloc, (void*)ranges, rangeCnt);
    if (!ranges || rangeCnt <= 0 || rangeCnt >= 4096) { fflush(stdout); return; }

    // Shared: dump a (suspected overflower) block — tag, full payload qwords,
    // user+0 vtable resolution, and our alloc backtrace if the table is tracking.
    auto dumpBlockInfo = [](uint8_t* block) {
        if (!block || IsBadReadPtr(block, 0x10)) return;
        const uint64_t* q = reinterpret_cast<const uint64_t*>(block);  // CAST-OK: alloc_header
        size_t pb = (size_t)(q[0] & 0x1ffffffffffULL) * 4;
        RZOut("[REDZONE-DUMP]   OVERFLOWER block @%p tag=%016llx size=%zu (payload %zu):\n",
               (void*)block, (unsigned long long)q[0], pb, pb >= 8 ? pb - 8 : 0);
        PrintModOff("overflower user+0 (vtable?)", q[1]);
        uint8_t* u = block + 8;
        size_t payQ = pb >= 8 ? (pb - 8) / 8 : 0;
        if (payQ > 32) payQ = 32;
        for (size_t i = 0; i < payQ; ++i) {
            if (IsBadReadPtr(u + i*8, 8)) break;
            uint64_t v = *reinterpret_cast<uint64_t*>(u + i*8);  // CAST-OK: alloc_header
            // Show ASCII for each byte too (catches embedded strings/paths).
            char asc[9]; for (int b = 0; b < 8; ++b) { char c = (char)((v >> (b*8)) & 0xff); asc[b] = (c >= 32 && c < 127) ? c : '.'; } asc[8] = 0;
            RZOut("[REDZONE-DUMP]     user+0x%02zx = %016llx  '%s'\n",
                   i*8, (unsigned long long)v, asc);
        }
        if (g_enabled && g_live) {
            auto it = g_live->find(u);
            if (it != g_live->end()) {
                RZOut("[REDZONE-DUMP]   overflower IS OURS reqSize=%zu seq=%u — alloc backtrace:\n",
                       it->second.reqSize, it->second.seq);
                for (int i = 0; i < it->second.btCount; ++i)
                    PrintModOff("    bt", (uint64_t)it->second.bt[i]);
            } else {
                RZOut("[REDZONE-DUMP]   overflower NOT in our table (Runtime-allocated or stale)\n");
            }
        }
    };

    int found = 0;
    for (int r = 0; r < rangeCnt; ++r) {
        rt::system::MemoryRange* rng   = &ranges[r];
        if (IsBadReadPtr(rng, 0x18)) continue;
        uint8_t* start = rng->base;
        uint8_t* end   = rng->end;
        if (!start || !end || end <= start) continue;

        uint8_t* prev = nullptr;
        uint8_t* p = start;
        int guard = 0;
        while (p < end && ++guard < 4000000) {
            if (IsBadReadPtr(p, 8)) {
                RZOut("[REDZONE-DUMP] range %d: unreadable block @%p prev=%p\n",
                       r, (void*)p, (void*)prev);
                ++found; break;
            }
            uint64_t blkTag = *reinterpret_cast<uint64_t*>(p);  // CAST-OK: alloc_header
            size_t blkBytes = (size_t)(blkTag & 0x1ffffffffffULL) * 4;
            if (blkBytes < 8 || p + blkBytes > end) {
                RZOut("[REDZONE-DUMP] range %d [%p..%p): CORRUPT block @%p "
                       "tag=0x%016llx size=%zu\n",
                       r, (void*)start, (void*)end, (void*)p,
                       (unsigned long long)blkTag, blkBytes);
                if (prev) dumpBlockInfo(prev);
                ++found; break;
            }
            prev = p;
            p += blkBytes;
        }
        if (found) break;
    }
    if (found) { fflush(stdout); return; }

    // ---- Pass 2: free-list LINK validation ----
    // The crash is in Allocate's free-list traversal (GetFreeTagForSize), which
    // follows a per-block forward link stored at block+8 as a signed 41-bit
    // RELATIVE offset: next_FreeTag = (block+8) + signext41(qword & 0x1ffffffffff).
    // Bucket heads live at allocator + 0x28 + bucket*8 for buckets [0, 0xC0).
    // An OOB write into a freed block's body clobbers this link → Allocate
    // follows it to a wild address → crash. Tags stay intact, so pass 1 misses it.
    RZOut("[REDZONE-DUMP] no tag corruption — validating free-list LINKS...\n");
    auto inAnyRange = [&](uint8_t* a) -> bool {
        for (int r = 0; r < rangeCnt; ++r) {
            rt::system::MemoryRange* rg = &ranges[r];
            if (IsBadReadPtr(rg, 0x18)) continue;
            uint8_t* s = rg->base;
            uint8_t* e = rg->end;
            if (s && e && a >= s && a < e) return true;
        }
        return false;
    };
    auto dumpOverflower = [&](uint8_t* badBlock) {
        for (int r = 0; r < rangeCnt; ++r) {
            rt::system::MemoryRange* rg = &ranges[r];
            if (IsBadReadPtr(rg, 0x18)) continue;
            uint8_t* s = rg->base;
            uint8_t* e = rg->end;
            if (!(s && e && badBlock >= s && badBlock < e)) continue;
            uint8_t* pp = s; uint8_t* prev = nullptr; int g2 = 0;
            while (pp < badBlock && ++g2 < 4000000) {
                if (IsBadReadPtr(pp, 8)) { prev = nullptr; break; }
                uint64_t t = *reinterpret_cast<uint64_t*>(pp);  // CAST-OK: alloc_header
                size_t bb = (size_t)(t & 0x1ffffffffffULL) * 4;
                if (bb < 8) { prev = nullptr; break; }
                prev = pp; pp += bb;
            }
            if (pp == badBlock && prev) {
                dumpBlockInfo(prev);
            } else {
                RZOut("[REDZONE-DUMP]   (could not locate preceding block of %p)\n",
                       (void*)badBlock);
            }
            break;
        }
    };

    for (int bucket = 0; bucket < 0xC0 && !found; ++bucket) {
        uint8_t* head = reinterpret_cast<uint8_t*>(alloc->freeLists[bucket]);  // the walk steps the FreeTag as raw block bytes  // CAST-OK: alloc_header
        if (!head) continue;
        uint8_t* curFT = head;  // FreeTag = block+8
        uint8_t* prevFT = nullptr;
        int guard = 0;
        while (curFT && ++guard < 2000000) {
            uint8_t* block = curFT - 8;
            if (IsBadReadPtr(curFT, 8) || !inAnyRange(block)) {
                RZOut("[REDZONE-DUMP] bucket %d: free node @%p (block %p) OUT OF RANGE "
                       "— prior node @%p has the corrupt link\n",
                       bucket, (void*)curFT, (void*)block, (void*)prevFT);
                if (prevFT) {
                    dumpOverflower(prevFT - 8);  // overflower precedes the clobbered block
                } else {
                    // Bucket HEAD itself was clobbered. The corrupt value usually
                    // differs from the real free node only in its HIGH bytes (e.g.
                    // +0x100000000 = a byte at +4 flipped). Reconstruct the in-range
                    // node by grafting each range's high 32 bits onto the corrupt
                    // node's low 32 bits, then dump the LIVE block physically before
                    // it = the overflow source (names the class via its vtable).
                    uint64_t corruptLow = reinterpret_cast<uint64_t>(curFT) & 0xffffffffULL;  // CAST-OK: ptr_int
                    for (int rr = 0; rr < rangeCnt; ++rr) {
                        rt::system::MemoryRange* rg2 = &ranges[rr];
                        if (IsBadReadPtr(rg2, 0x18)) continue;
                        uint8_t* s2 = rg2->base;
                        uint8_t* e2 = rg2->end;
                        if (!s2 || !e2) continue;
                        uint64_t hi = reinterpret_cast<uint64_t>(s2) & ~0xffffffffULL;  // CAST-OK: ptr_int
                        uint8_t* cand = reinterpret_cast<uint8_t*>(hi | corruptLow);  // CAST-OK: ptr_int
                        if (cand >= s2 && cand < e2) {
                            RZOut("[REDZONE-DUMP]   reconstructed in-range node @%p "
                                  "(corrupt low32 grafted onto range hi) — dumping its predecessor:\n",
                                  (void*)cand);
                            dumpOverflower(cand - 8);
                            break;
                        }
                    }
                }
                ++found; break;
            }
            uint64_t link = *reinterpret_cast<uint64_t*>(curFT);  // CAST-OK: alloc_header
            int64_t off = (int64_t)(link << 23) >> 23;  // sign-extend low 41 bits
            if (off == 0) break;                          // end of chain
            uint8_t* next = curFT + off;
            if (IsBadReadPtr(next, 8) || !inAnyRange(next - 8)) {
                RZOut("[REDZONE-DUMP] bucket %d: free block @%p has CORRUPTED forward link "
                       "qword=%016llx off=%lld -> next=%p (out of range)\n",
                       bucket, (void*)block, (unsigned long long)link,
                       (long long)off, (void*)next);
                if (!IsBadReadPtr(block, 0x28)) {
                    const uint64_t* q = reinterpret_cast<const uint64_t*>(block);  // CAST-OK: alloc_header
                    RZOut("[REDZONE-DUMP]   clobbered free block body: "
                           "tag=%016llx +8=%016llx +10=%016llx +18=%016llx +20=%016llx\n",
                           (unsigned long long)q[0], (unsigned long long)q[1],
                           (unsigned long long)q[2], (unsigned long long)q[3],
                           (unsigned long long)q[4]);
                }
                dumpOverflower(block);   // the live block physically before it = overflow source
                ++found; break;
            }
            prevFT = curFT;
            curFT = next;
        }
    }
    if (!found) RZOut("[REDZONE-DUMP] free-list forward links also intact "
                       "(corruption may be in the high-bit prev links or a bucket head)\n");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// NEGATIVE CONTROL — BANISHED_REDZONE_SELFTEST=<ms>
//
// "Zero violations" is only evidence if a violation could have been reported.
// This deliberately clobbers ONE engine boundary tag — the exact qword the
// watchdog's walk reads — <ms> milliseconds into the run, and then does NOT
// check anything itself: the detection has to come from the watchdog thread,
// so a pass proves the thread is alive AND that the walk can express a
// failure. A run that ends without a [REDZONE-HEAP] corruption report after
// this injector announced a successful write is a BLIND instrument.
//
// The injector prints the block address and the tag before/after, so its own
// failure (no ranges, unreadable memory) is distinguishable from the
// watchdog's failure to notice. Without that, "no detection" would have two
// causes and could not discriminate them.
static DWORD WINAPI RedzoneSelfTest(LPVOID param) {
    const int delayMs = (int)(intptr_t)param;  // CAST-OK: win32
    Sleep((DWORD)delayMs);
    System::MemoryAllocator* alloc = System::MemoryAllocator::_singleton;   // one live read (B3R STORAGE)
    if (!alloc) {
        printf("[REDZONE-SELFTEST] NO INJECTION — System::MemoryAllocator::_singleton is null\n");
        fflush(stdout);
        return 0;
    }
    rt::system::MemoryRange* ranges   = alloc->ranges;   // +0x00
    int      rangeCnt = alloc->rangeCount;   // +0x08
    if (!ranges || rangeCnt <= 0 || rangeCnt >= 4096) {
        printf("[REDZONE-SELFTEST] NO INJECTION — ranges=%p cnt=%d\n", (void*)ranges, rangeCnt);
        fflush(stdout);
        return 0;
    }
    Platform::LockStorage* cs = &alloc->lock;
    Platform::MutexLock(cs);
    uint8_t* victim = nullptr;
    uint64_t before = 0;
    {
        rt::system::MemoryRange* rng   = &ranges[0];                       // range 0
        uint8_t* start = rng->base;
        uint8_t* end   = rng->end;
        uint8_t* p     = start;
        // Walk in a little way so the victim is a settled interior block rather
        // than the range header, then zero its size field: blkBytes becomes 0,
        // which is the "< 8" case the walker flags. This is the same shape as
        // the real zeroed-tag corruption the watchdog exists to catch.
        for (int i = 0; p && start && end && p < end && i < 64; ++i) {
            if (!ReadablePtr(p, 8)) break;
            uint64_t t = *reinterpret_cast<uint64_t*>(p);  // CAST-OK: alloc_header
            size_t   b = (size_t)(t & 0x1ffffffffffULL) * 4;
            if (b < 8 || p + b > end) break;
            victim = p; before = t;
            p += b;
        }
        if (victim) *reinterpret_cast<uint64_t*>(victim) = 0;  // CAST-OK: alloc_header
    }
    Platform::MutexUnlock(cs);
    if (victim) {
        printf("[REDZONE-SELFTEST] INJECTED at t=%dms: block @%p tag %016llx -> 0000000000000000 "
               "(walk must now report a violation)\n",
               delayMs, (void*)victim, (unsigned long long)before);
    } else {
        printf("[REDZONE-SELFTEST] NO INJECTION — no walkable block found in range 0\n");
    }
    fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------------------
// Watchdog lifetime.
//
// The watchdog used to be a `for(;;)` with no way to stop it. That is a
// SHUTDOWN RACE, and it is what made `BANISHED_REDZONE=watch` segfault at the
// very end of every stress run (lane REDZONE, 2026-09-08): the sim finishes,
// the atexit handlers print their censuses, and then the CRT tears the process
// down — unloading Runtime-x64.dll and destroying the engine allocator —
// while this thread is still dereferencing the allocator singleton and entering
// a CRITICAL_SECTION inside memory that has just gone away.
//
// The stop is registered with atexit() from Install(). atexit runs LIFO and
// Install() is called very early in WinMain, so this handler runs LAST — after
// every other census. That deliberately keeps the watchdog live through the
// whole world-teardown cascade (which is precisely what the 800x10 teardown
// hunt uses it for) and only silences it once nothing is left to watch.
static void RedzoneStopWatchdog() {
    if (!g_watchThread) return;
    InterlockedExchange(&g_watchStop, 1);
    // Bounded: a hung walk must never wedge process exit. 2s is ~2000 polls at
    // the 1ms default, so a live thread always acknowledges long before this.
    DWORD w = WaitForSingleObject(g_watchThread, 2000);
    printf("[REDZONE-HEAP] watchdog stopped at exit (%s) after %d completed heap walks\n",
           w == WAIT_OBJECT_0 ? "acknowledged" : "TIMED OUT — still walking",
           static_cast<int32_t>(g_watchWalks));
    fflush(stdout);
    CloseHandle(g_watchThread);
    g_watchThread = nullptr;
}

static DWORD WINAPI RedzoneWatchdog(LPVOID) {
    int interval = g_watchIntervalMs > 0 ? g_watchIntervalMs : 2;  // default 2ms
    printf("[REDZONE-HEAP] watchdog thread started (interval=%dms)\n", interval);
    fflush(stdout);
    while (!InterlockedCompareExchange(&g_watchStop, 0, 0)) {
        // Single-pass detect + report + abort. NO confirmation: the corrupted
        // free block is typically REUSED by the very Allocate that then crashes,
        // so a second confirmation pass would no longer see it (it raced away) —
        // confirmation drops real detections right before the crash. The walker
        // only flags egregious corruption (size<8 / overruns the range, e.g. a
        // zeroed tag), which is not a transient mid-Allocate state, so abort-on-
        // first-sight is safe enough.
        RedzoneAlloc_HeapCheck("watchdog");  // aborts on detection
        ++g_watchWalks;
        Sleep((DWORD)interval);
    }
    return 0;
}


// ---------------------------------------------------------------------------
// TEARDOWN_WILD_WRITE.md §3 — name the OWNER of the block that was written past.
//
// Every capture of the teardown wild write has the same geometry: the corrupt tag
// sits immediately after a 24-byte block whose 16-byte payload is small ints, and
// the smashed dword is that block's user+0x14. Four out of four is not a
// coincidence of heap neighbours: the writer owns the PRECEDING block and stores
// past its end. So when the teardown bracket first sees a dirty tag this reports
// (1) the preceding block (tag, payload, allocation caller if ALLOC_STATS is on),
// (2) every live block holding a pointer to that payload — the owner object, whose
//     user+0 vtable resolves to a symbol in OpenBA.exe — found by one walk that
//     scans each block's payload, (3) repairs the tag so teardown can finish and
//     arms a hardware write watchpoint on the smashed dword so a REPEAT write from
//     the same code lands with a full stack in standalone_crash.log.
// One walk, once, only after detection: no cost inside the cascade.
// ---------------------------------------------------------------------------
#include "hw_watch.h"

extern "C" void RedzoneAlloc_PrintBacktrace(const char* tag) {
    void* bt[32];
    USHORT n = CaptureStackBackTrace(1, 32, bt, nullptr);
    RZOut("[WILDWRITE] backtrace (%s):\n", tag ? tag : "");
    for (USHORT i = 0; i < n; ++i) PrintModOff("  bt", (uint64_t)(uintptr_t)bt[i]);  // CAST-OK: crash_walk
    fflush(stderr);
}

extern "C" void RedzoneAlloc_ReportWildWrite(void* badV) {
    uint8_t* bad = static_cast<uint8_t*>(badV);  // CAST-OK: alloc_header
    System::MemoryAllocator* alloc = System::MemoryAllocator::_singleton;   // one live read (B3R STORAGE)
    if (!bad || !alloc) return;
    rt::system::MemoryRange* ranges   = alloc->ranges;   // +0x00
    int      rangeCnt = alloc->rangeCount;   // +0x08
    if (!ranges || rangeCnt <= 0 || rangeCnt >= 4096) return;

    const uint64_t badTag = *reinterpret_cast<uint64_t*>(bad);  // CAST-OK: alloc_header
    // The observed damage never touches the low dword (the size), so the block's
    // true extent is recoverable from it and the walk can step OVER the victim.
    auto blockBytes = [&](uint8_t* p) -> size_t {
        uint64_t t = *reinterpret_cast<uint64_t*>(p);  // CAST-OK: alloc_header
        if (p == bad) t &= 0xffffffffULL;
        return (size_t)(t & 0x1ffffffffffULL) * 4;
    };

    // Pass 1: locate the physically-preceding block.
    uint8_t* prev = nullptr;
    int badRange = -1;
    for (int r = 0; r < rangeCnt && badRange < 0; ++r) {
        rt::system::MemoryRange* rng = &ranges[r];
        uint8_t* start = rng->base;
        uint8_t* end   = rng->end;
        if (!start || !end || end <= start || bad < start || bad >= end) continue;
        badRange = r;
        uint8_t* p = start; int guard = 0;
        while (p < end && ++guard < 4000000) {
            if (!ReadablePtr(p, 8)) break;
            if (p == bad) break;
            size_t nb = blockBytes(p);
            if (nb < 8 || p + nb > end) break;
            prev = p; p += nb;
        }
    }
    RZOut("[WILDWRITE] victim tag @%p = 0x%016llx (size dword intact: %llu bytes); range %d\n",
          (void*)bad, (unsigned long long)badTag,
          (unsigned long long)((badTag & 0xffffffffULL) * 4), badRange);
    if (!prev) { RZOut("[WILDWRITE] no preceding block found\n"); fflush(stderr); return; }

    uint8_t* prevUser = prev + 8;
    size_t   prevBytes = blockBytes(prev);
    size_t   prevPay   = prevBytes >= 8 ? prevBytes - 8 : 0;
    RZOut("[WILDWRITE] preceding block @%p user=%p tag=0x%016llx payload=%zu bytes; "
          "victim dword = user+0x%zx\n",
          (void*)prev, (void*)prevUser, (unsigned long long)*reinterpret_cast<uint64_t*>(prev),  // CAST-OK: alloc_header
          prevPay, prevPay + 4);
    {
        size_t q = prevPay / 8; if (q > 8) q = 8;
        for (size_t i = 0; i < q; ++i) {
            const uint32_t* d = reinterpret_cast<const uint32_t*>(prevUser + i * 8);  // CAST-OK: alloc_header
            RZOut("[WILDWRITE]   user+0x%02zx = %08x %08x\n", i * 8, d[0], d[1]);
        }
        PrintModOff("preceding user+0 as vtable", *reinterpret_cast<uint64_t*>(prevUser));  // CAST-OK: alloc_header
        uint64_t sz = 0; void* caller = nullptr; void* actx = nullptr;
        if (AllocStats_Lookup(prevUser, &sz, &caller, &actx)) {
            RZOut("[WILDWRITE] preceding block ALLOCSTATS size=%llu arrayHeader=%p\n",
                  (unsigned long long)sz, actx);
            PrintModOff("preceding block alloc caller", (uint64_t)(uintptr_t)caller);  // CAST-OK: crash_walk
        }
    }

    // Pass 2: who points at the preceding block's payload? Scan every live block's
    // payload for the user pointer (a DynArray header stores exactly that).
    int hits = 0;
    for (int r = 0; r < rangeCnt; ++r) {
        rt::system::MemoryRange* rng = &ranges[r];
        uint8_t* start = rng->base;
        uint8_t* end   = rng->end;
        if (!start || !end || end <= start) continue;
        uint8_t* p = start; int guard = 0;
        while (p < end && ++guard < 4000000) {
            if (!ReadablePtr(p, 8)) break;
            size_t nb = blockBytes(p);
            if (nb < 8 || p + nb > end) break;
            const uint64_t tag = *reinterpret_cast<uint64_t*>(p);  // CAST-OK: alloc_header
            const bool isFree = (tag & 0x0000200000000000ull) != 0;   // kTagThisFree, bit 45
            if (!isFree && p != prev && nb >= 16 && ReadablePtr(p, nb)) {
                const uint64_t* q = reinterpret_cast<const uint64_t*>(p + 8);  // CAST-OK: alloc_header
                size_t nq = (nb - 8) / 8;
                for (size_t i = 0; i < nq; ++i) {
                    if (q[i] != (uint64_t)(uintptr_t)prevUser) continue;  // CAST-OK: ptr_int
                    ++hits;
                    if (hits <= 12) {
                        RZOut("[WILDWRITE] OWNER candidate #%d: block @%p user=%p size=%zu holds the "
                              "pointer at user+0x%zx; next qwords: %016llx %016llx\n",
                              hits, (void*)p, (void*)(p + 8), nb, i * 8,
                              (unsigned long long)(i + 1 < nq ? q[i + 1] : 0),
                              (unsigned long long)(i + 2 < nq ? q[i + 2] : 0));
                        PrintModOff("  owner user+0 (vtable?)", q[0]);
                        // A DynArray header {data, count, capacity} usually sits INSIDE the
                        // owning object: report any module pointer found before it too.
                        for (size_t back = 1; back <= i && back <= 24; ++back) {
                            HMODULE mod = nullptr;
                            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                   (LPCSTR)(uintptr_t)q[i - back], &mod) && mod) {  // CAST-OK: crash_walk
                                char lbl[96];
                                snprintf(lbl, sizeof lbl, "  module ptr at user+0x%zx", (i - back) * 8);
                                PrintModOff(lbl, q[i - back]);
                            }
                        }
                    }
                }
            }
            p += nb;
        }
    }
    RZOut("[WILDWRITE] %d pointer(s) to the preceding block's payload found in live heap blocks\n", hits);

    // Repair + watch. Clearing the high dword restores the size; the prev-size/prev-free
    // bits it also carried are lost, which only matters if this block is freed while its
    // neighbour is free — acceptable in a run whose purpose is to name the writer.
    *reinterpret_cast<uint64_t*>(bad) = badTag & 0xffffffffULL;  // CAST-OK: alloc_header
    RZOut("[WILDWRITE] tag REPAIRED to 0x%016llx; arming write watchpoint on %p (4 bytes)\n",
          (unsigned long long)*reinterpret_cast<uint64_t*>(bad), (void*)(bad + 4));  // CAST-OK: alloc_header
    fflush(stderr);
    HwWatch_Arm(bad + 4, 4, "teardown wild write victim dword (user+payload+4 of preceding block)");
}
