// src/diag/redzone_alloc.h
//
// Debug-only heap-overflow detector for the engine's custom block allocator
// (System::MemoryAllocator, Runtime 0x1800c6e60). That allocator uses inline
// boundary-tag metadata (an 8-byte MemoryTag immediately before each user
// pointer, and the next block's tag immediately after the user region), so an
// out-of-bounds WRITE past a buffer corrupts the neighbouring block's tag and
// crashes LATER during free-list coalescing — far from the actual bad write.
//
// This shim sits on the allocator's instrument seat
// (System::g_memoryAllocatorInstrument, consulted at the entry of
// MemoryAllocator::MemAlloc / MemFree, which every caller reaches by name) with
// wrappers that over-allocate by a tail redzone and stamp a canary right after
// the requested size. The bad
// write trips the canary; RedzoneAlloc_ValidateAll(tag), or the BANISHED_REDZONE_SCAN=<N>
// auto-scan every N allocator ops, reports the overflowing buffer by
// size + allocation backtrace BEFORE the displaced crash.
//
// Tail-only: the returned pointer is the allocator's real pointer (no head
// offset), so a block freed by any path (our wrapper OR Runtime-internal Free)
// stays correct. Zero overhead unless BANISHED_REDZONE is set (any value but
// 0/false), so release builds and normal test runs are unaffected.

#pragma once

// Arm the instrument seat with the redzone wrappers (or, for
// BANISHED_REDZONE=watch, start only the heap watchdog) if BANISHED_REDZONE is
// set. Called once from src/app/main.cpp's Init(), before Engine::Startup.
// No-op when the flag is unset.
void RedzoneAlloc_Install();

// True once Install armed the shim or, for BANISHED_REDZONE=watch, the heap watchdog.
bool RedzoneAlloc_Enabled();

// Scan every live (wrapper-allocated) block's tail canary. For each corrupted
// one, log the requested size, allocation sequence number, and captured alloc
// backtrace. `tag` identifies the checkpoint (e.g. "Slot5 step9"). Cheap no-op
// when the shim is disabled. Returns the number of corrupted blocks found.
int RedzoneAlloc_ValidateAll(const char* tag);

// Walk the engine allocator's own boundary-tag chain and validate every block
// (live OR free, ours OR Runtime's) — catches free-list poisoning the per-block
// canaries miss (corruption of a freed block, or a wild-index write that skips
// the canary). When a bad block is found, the physically-preceding block is
// looked up in our live table to surface the overflower's alloc backtrace.
// Aborts cleanly (exit 0x123) on detection unless BANISHED_REDZONE_ABORT=0.
// Cheap no-op when the shim is disabled. Returns corrupted-block count.
int RedzoneAlloc_HeapCheck(const char* tag);

// Shim-INDEPENDENT one-shot heap dump — safe to call from the crash handler on
// a fully-native run (redzone shim NOT required). Walks the allocator's
// boundary tags, finds the first corrupt block, and dumps the physically-
// preceding block's vtable resolved to module+offset (names the overflower).
// Does not abort, does not take the allocator lock.
extern "C" void RedzoneAlloc_HeapDump(const char* tag);

// Shim-INDEPENDENT, SILENT version of the same walk: returns the first corrupt
// block (null when the heap is clean) and prints nothing, so it is cheap enough
// to call after every step of a teardown loop and bracket the moment corruption
// appears. See the comment on the definition (Wave 9 lane N, Finding 3).
extern "C" void* RedzoneAlloc_FindCorruptBlockNative();
// TEARDOWN_WILD_WRITE.md: on the first dirty tag, report the preceding block and its
// owner, repair the tag, arm a write watchpoint on the smashed dword.
extern "C" void RedzoneAlloc_ReportWildWrite(void* badBlock);
// Print the caller's stack as module+offset lines (exe offsets symbolize with nm).
extern "C" void RedzoneAlloc_PrintBacktrace(const char* tag);

// Free-list LINK validation at a code checkpoint. Aborts (exit 0x124) naming
// the tag at the first corrupt forward link. Only active under BANISHED_REDZONE.
extern "C" int RedzoneAlloc_CheckLinks(const char* tag);

// Optional write-through sink so the crash-context dump survives a re-entrant
// fault. The crash handler sets this to its FILE_FLAG_WRITE_THROUGH logger.
extern "C" void RedzoneAlloc_SetDumpSink(void (*sink)(const char*));

