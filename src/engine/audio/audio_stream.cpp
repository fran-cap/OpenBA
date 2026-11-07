// ============================================================================
// src/engine/audio/audio_stream.cpp — Wave 24 lane SMALLBODY
//   ??0Stream@Audio@@QEAA@XZ    retail 0x1800a0e30  ord 410  159 bytes  (ctor)
//   ??1Stream@Audio@@QEAA@XZ    retail 0x1800a0d90  ord 688  153 bytes  (dtor)
// ============================================================================
//
// `Audio::Stream` is a `System::Thread` SUBCLASS. These two are the whole of its
// construction and destruction. The run body (`?Run@Stream@Audio@@UEAAXXZ`,
// 0x1800a0f60) used to be "NOT in this lane's scope and stays on the DLL"; it is
// PORTED as of VTABLE BATCH 7b — src/engine/audio/audio_stream_run.cpp,
// which is also what publishes this ctor's +0x48 event into the Device's
// VoiceCallback record.
//
// Its own .cpp, and a `_win` one, because both bodies are almost entirely
// KERNEL32 synchronisation primitives and PLAN.md §4.1 decision 1 keeps
// <windows.h> out of audio.cpp — the same split audio_device.cpp already makes
// for the Device+0xA50 critical section.
//
// ---------------------------------------------------------------------------
// ABSENCE ESTABLISHED FIRST
// ---------------------------------------------------------------------------
// Grepped by ADDRESS and by NAME across runtime/ code/ standalone/ engine/ src/render/dx11/
// (scoped — WAVE24_KICKOFF §1a), before anything was written. Hits were
// CONSUMERS only:
//   ??0Stream@Audio@@QEAA@XZ -> src/engine/engine/singleton_ctors.cpp:742
//   ??1Stream@Audio@@QEAA@XZ -> src/engine/engine/engine_lifecycle.cpp:955
//   0x1800a0e30 / 0x1800a0d90 -> no hits at all.
// A body ported but never REGISTERED is indistinguishable from an absent one to
// the census (reference_half_registered_port_reads_as_absent.md), which is why
// the grep was by address as well as by name.
//
// ---------------------------------------------------------------------------
// THE STATED BLOCKER, RE-CHECKED — IT IS AN *ORACLE* VERDICT, NOT A PORT ONE
// ---------------------------------------------------------------------------
// `src/engine/engine/singleton_ctors.cpp:702-710` (Wave 9 lane C2) holds it
// like this, verbatim:
//
//     "*** ORACLE DEFERRED — SAME BLOCKER AS PERSISTENCE. *** 0x1800a0e30 is
//      `??0Stream@Audio@@QEAA@XZ`, a System::Thread SUBCLASS: it calls the same
//      `??0Thread@System@@QEAA@I@Z` base that `_beginthreadex`s a suspended
//      worker and creates three events, then stamps `??_7Stream@Audio@@6B@`
//      (0x180154950) and initialises a second CRITICAL_SECTION. Running the
//      shipped body twice against scratch buffers would start two threads on
//      fake objects and the handle values would differ by construction."
//
// RE-CHECKED, and the re-check changes what it licenses:
//
//   * The technical content is CORRECT and this lane reproduces it from the
//     disassembly independently — the base ctor really is called with 0x10000,
//     the vptr really is 0x180154950, and there really is a second
//     CRITICAL_SECTION at +0x50.
//   * But it is a verdict about an ORACLE ("running the shipped body twice ...
//     would start two threads"), NOT about portability. It never says the body
//     cannot be written. Two waves of gap tables carried this row as though it
//     did. That is the Wave 23 §6 defect class — a discharge does not
//     propagate — in its other direction: a *narrow* holdback read as a *broad*
//     one.
//   * `??0Thread@System@@QEAA@I@Z` IS ALREADY PORTED and registered
//     (src/platform/win/thread_win.cpp:457), so the base is not a wall
//     either. Re-derived here, not taken from the cross-lane note that also
//     said so.
//
// So: the bodies are ported. The ORACLE holdback SURVIVES re-checking and is
// restated honestly under ORACLE below, rather than quietly dropped.
//
// ---------------------------------------------------------------------------
// ??0Stream@Audio@@QEAA@XZ — the body, from the instruction listing
// ---------------------------------------------------------------------------
//   MOV [RSP+8],RCX / PUSH RDI / SUB RSP,0x30 / MOV [RSP+0x20],-2   unwind
//   MOV RDI,RCX
//   MOV EDX,0x10000 ; CALL 0x1800ca4e0      ??0Thread@System@@QEAA@I@Z, 64 KB
//   LEA RAX,[0x180154950] ; MOV [RDI],RAX   stamp ??_7Stream@Audio@@6B@
//   XOR ESI,ESI
//   MOV [RDI+0x30],RSI ; MOV [RDI+0x38],RSI
//   LEA RCX,[RDI+0x50] ; CALL [0x18014b380]   InitializeCriticalSection
//   MOV EDX,0x1388 ; LEA RCX,[RDI+0x50]
//                    CALL [0x18014b208]       SetCriticalSectionSpinCount 5000
//   MOV [RDI+0x78],RSI
//   MOV BYTE PTR [RDI+0x80],SIL
//   XOR R9D/R8D/EDX/ECX ; CALL [0x18014b360] ; MOV [RDI+0x48],RAX  CreateEventW
//   XOR R9D/R8D/EDX/ECX ; CALL [0x18014b360] ; MOV [RDI+0x40],RAX  CreateEventW
//   MOV RAX,RDI / RET
//
// Every one of those five KERNEL32 slots was named from the DLL's OWN import
// table, not guessed: 0x18014b380 InitializeCriticalSection, 0x18014b208
// SetCriticalSectionSpinCount, 0x18014b360 CreateEventW, 0x18014b368 SetEvent,
// 0x18014b3b8 CloseHandle, 0x18014b3c0 WaitForSingleObject, 0x18014b370
// DeleteCriticalSection.
//
// BOTH events are `CreateEventW(NULL, FALSE, FALSE, NULL)` — all four arguments
// zeroed — i.e. AUTO-RESET and initially NON-SIGNALLED. Note the order: +0x48 is
// created FIRST, +0x40 second. Reproduced in that order because a faithful port
// reproduces side effects, and two successive CreateEventW calls are
// distinguishable in a handle trace.
//
// TWO events, not three. The "creates three events" in the Wave 9 comment
// belongs to the `System::Thread` BASE (hStart/hStop/hFinished,
// src/engine/system/Thread.h), which is a separate object region; the
// sentence is accurate about the base and is not describing this body.
//
// ---------------------------------------------------------------------------
// ??1Stream@Audio@@QEAA@XZ — and the "every element or only one" check
// ---------------------------------------------------------------------------
// The lane brief warns that a prescribed dtor port once swapped a body that
// destroys EVERY element for one that destroys only the first. So this body was
// READ, and every teardown step enumerated from the listing:
//
//   MOV [RCX],&0x180154950                 re-stamp ??_7Stream@Audio@@6B@
//   CloseHandle([RCX+0x48])                the second event
//   CloseHandle([RBX+0x40])                the first event
//   LEA RCX,[RBX+0x50] ; DeleteCriticalSection
//   LEA RCX,[RBX+0x30] ; CALL 0x18000c4a0  release the +0x30 heap buffer
//   MOV [RBX],&0x180155f20                 re-stamp ??_7Thread@System@@6B@  |
//   SetEvent([RBX+0x20])                   hStop                            |
//   WaitForSingleObject([RBX+0x10], -1)    hThread, INFINITE                | the
//   CloseHandle([RBX+0x10])                hThread                          | INLINED
//   CloseHandle([RBX+0x18])                hStart                           | ~Thread
//   CloseHandle([RBX+0x20])                hStop                            |
//   JMP CloseHandle([RBX+0x28])            hFinished  (TAIL call)           |
//
// SIX CloseHandle calls in total: two Stream events and ALL FOUR of the base's
// handles. Nothing is closed twice and nothing is left open — checked against
// `src/engine/system/Thread.h`, which names exactly four handles at
// +0x10/+0x18/+0x20/+0x28.
//
// THE INLINED TAIL IS `??1Thread@System@@QEAA@XZ`, PROVED NOT ASSUMED. The
// shipped `System::Thread` dtor at 0x1800ca590 was disassembled beside this one:
// it is the SAME twelve steps in the SAME order, from the 0x180155f20 stamp to
// the tail CloseHandle of +0x28. That body is already ported and registered
// (thread_win.cpp:458), so this port CALLS it rather than re-inlining a
// thirteenth hand copy — the same reasoning tooltipdialog.cpp gives for sending
// vtable slot 0 to deleting_dtor.cpp instead of copying it.
//
// 0x18000c4a0 is a heap-buffer release: `if (this[0]) { lock;
// Free(*0x1801fc718, this[0]); unlock; } this[0] = 0; this[1] = 0;` — i.e. the
// lock/Free/unlock triple that IS `?MemFree@MemoryAllocator@System@@`, plus a
// two-qword clear. It is reached BY ADDRESS through `ResolveAddr`, which
// consults the port registry first.
//
// *** WAVE 25 LANE INTERNAL42 CORRECTION, left in place rather than deleted so
// the next reader sees which half was wrong. *** This banner used to end: "NOT
// re-implemented here: it is 0x18 bytes of somebody else's object and this lane
// has no witness for what lives at +0x30." The observation was right and the
// inference was wrong, which is the DEPENDENCY-SHAPED blocker class Wave 24
// measured false 5 times in 9. 0x18000c4a0 is byte-for-byte
// `Collection::Array<T>::~Array` — the J1 body already ported seventeen times
// in src/engine/collection/array.cpp — proved by an address-normalised diff
// against the canonical 0x180004890 with the J2 body as a negative control.
// Freeing a buffer does not need a witness for what is in it; that is exactly
// why J1 takes no stride and no parameters at all.
//
// It is now REGISTERED (array.cpp, the eighteenth/nineteenth J1 addresses), so
// the `ResolveAddr` below reaches OUR body. Nothing in this file changed: the
// call site was already a door, and a door is what made the port reachable
// without touching its caller.
//
// ---------------------------------------------------------------------------
// ??_7Stream@Audio@@6B@ (0x180154950) IS NOT MINTED, AND THAT IS DELIBERATE
// ---------------------------------------------------------------------------
// RE-DERIVED HERE, not accepted from a cross-lane note:
//     grep -n '180154950\|Stream@Audio@@6B' standalone/rt_data_mint_tables.inc
//                                           standalone/reg_mint_promoted.cpp
//     -> ZERO hits.
// So at cutover this vptr has no minted storage to point at.
//
// THIS PORT DOES NOT MAKE THAT WORSE — IT IS THE STEP THAT MAKES IT FIXABLE.
// Retail's ctor stamps the vptr with a RIP-relative LEA, and a RIP-relative LEA
// stamp IS NOT A DOOR: we intercept only at doors, so while the DLL owns this
// ctor nothing we do to the mint table can ever change which table gets stamped
// (reference_a_ported_row_can_be_permanently_unpromotable.md). Porting the ctor
// CONVERTS that LEA into a `ResolveAddr(Module::Runtime, 0x180154950)` door,
// which resolves to the minted row the moment one exists and to the DLL's table
// until then.
//
// MINTING IT IS NOT IN THIS LANE'S SCOPE: `standalone/rt_data_mint_tables.inc`
// is GENERATED by tools/rt_mint_gen.py, and regenerating it is a whole-file
// change that would capture six other live Wave 24 lanes mid-wave — the reason
// src/engine/ui/tooltipdialog.cpp gives for leaving its own row to the wave
// owner. Handed forward explicitly instead:
//
//   HANDOVER: mint `??_7Stream@Audio@@6B@` (0x180154950).
//   [SCOPE: CUTOVER-ONLY] — with the DLL present this row is harmless: the door
//   resolves to the DLL's own table, which is what retail stamps anyway.
//   [ADDRESSEE: Wave 24 lane CUTOVER, whose attempt-25 boot faults at this very
//   ctor, or the wave owner at the close regeneration — whichever comes first.]
//
// ---------------------------------------------------------------------------
// ORACLE — WHAT IS AND IS NOT AVAILABLE, STATED PLAINLY
// ---------------------------------------------------------------------------
// PROBE SHAPE (WAVE24_KICKOFF §1c): **shadow/diff site**, and honestly labelled
// as a SELF-CHECK rather than an oracle. The other four shapes were considered
// and each is ruled out by a named fact, not by "no run, therefore STRUCTURAL":
//
//   * INPUT SWEEP — needs a PURE function. Neither body is: one creates two OS
//     event objects and a critical section, the other closes six handles and
//     joins a thread.
//   * SYNTHESISED ORACLE — a fabricated `Audio::Stream` is not a synthetic
//     subject but a FAKE: the base ctor `_beginthreadex`s a real worker, so
//     running either body against scratch memory starts (or joins) a real OS
//     thread. This is the Wave 9 holdback quoted above, re-checked and UPHELD.
//   * WRITE-PATH REPLAY — neither body is a Serialize.
//   * TWO-SIDED A/B — a destructor cannot be replayed: running theirs after ours
//     double-closes four handles, and the handle VALUES differ by construction
//     anyway, so they are not a comparable quantity.
//
// WHAT THE SELF-CHECK DOES ASSERT — the post-conditions the disassembly pins,
// which are invariants a WRONG port breaks even though no second party is
// consulted. It is exactly the class of site
// reference_shadow_marker_is_not_an_oracle.md warns about, and it is labelled
// SELF-CHECK in its own site name so a sweep report cannot count it as an
// oracle.
//
// EFFECTIVENESS COUNTERS: `g_ctorChecks` / `g_dtorChecks` count how many times
// the predicate was actually EVALUATED (not merely how many times the body ran),
// so a "0 divergences" line cannot be confused with a dead screen
// (KICKOFF §3 rule 3).
//
// NEGATIVE CONTROL: `RT_AUDIOSTREAM_CONTROL=1` skips the
// `[+0x38] = 0` store in the ctor. +0x38 IS in the checked set, so the control
// corrupts a subject the predicate actually examines
// (reference_control_must_pick_a_compared_subject.md); and it leaves the two
// handle fields alone, which MUST NOT be compared against any fixed value
// because they legitimately vary per run (the mirror failure,
// reference_a_control_can_corrupt_a_byte_that_should_not_be_compared.md).
//
// ---------------------------------------------------------------------------
// ONE KILL SWITCH: RT_AUDIOSTREAM
// ---------------------------------------------------------------------------
// ============================================================================

// ENDGAME B.3.9 ENG W2: the ctor and dtor are the class's REAL ones (on
// `System::Thread(0x10000)`); `Ctor_AudioDevice` placement-news the one Stream.
// The `kStream*` offsets that lived here are the fields of `Audio::Stream`
// (src/engine/audio/Stream.h, B6-8). The +0x30 `~Array` (0x18000c4a0, J1)
// is `rtcoll::ArrayDtorImpl`, the J1 body.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "audio/Stream.h"
#include "collection/Collection.h"   // rtcoll::ArrayDtorImpl
#include "platform_sync.h"           // Platform::MutexInit / EventCreate / EventClose

// Phase D.4 A1: the two events are Platform::EventHandle (CreateEventW /
// CloseHandle on Windows, src/platform/win/sync_win.cpp), so this file needs
// no OS header and moved from src/platform/win/audio_stream_win.cpp.

namespace {

constexpr uint32_t kStreamStackSize = 0x10000;   // MOV EDX,0x10000 — 64 KB

// ---- attendance ------------------------------------------------------------
uint64_t g_ctorEntries = 0;
uint64_t g_dtorEntries = 0;
uint64_t g_ctorEventFail = 0; // control: CreateEventW refused

struct AudioStreamSummary {
    ~AudioStreamSummary() {
        if (g_ctorEntries == 0 && g_dtorEntries == 0) {
            printf("[RtPort] Audio::Stream ATTENDANCE: neither ctor nor dtor "
                   "was entered in this run.\n");
            fflush(stdout);
            return;
        }
        printf("[RtPort] Audio::Stream ATTENDANCE (entries, not comparisons): "
               "ctor=%llu dtor=%llu eventFail=%llu\n",
               (unsigned long long)g_ctorEntries,
               (unsigned long long)g_dtorEntries,
               (unsigned long long)g_ctorEventFail);
        fflush(stdout);
    }
};
AudioStreamSummary g_summary;

}  // namespace

// ============================================================================
// ??0Stream@Audio@@QEAA@XZ                                 retail 0x1800a0e30
// ============================================================================
// ENDGAME B.3.9 ENG W2: the REAL constructor. `System::Thread(0x10000)` is the
// init list (retail 0x1800a0e55 CALL ??0Thread, EDX = 0x10000); the Stream
// vptr store (0x1800a0e62) is the compiler's. The body is the old door's,
// unchanged. MAIN THREAD (Ctor_AudioDevice), once.
Audio::Stream::Stream() : System::Thread(kStreamStackSize) {
    ++g_ctorEntries;
    // +0x30 `_buffers`: Array<Block>() is retail's 0x1800a0e67 / 0x1800a0e6b
    // zero stores (the member, before this body).
    // InitializeCriticalSection 0x1800a0e73, SetCriticalSectionSpinCount 0x1800a0e82
    Platform::MutexInit(&_lock, Platform::kLockSpinCount);   // MOV EDX,0x1388 — 5000

    _bytePosition = 0; _channel = 0;                     // 0x1800a0e89 (ONE qword store at +0x78)
    _stopping = 0;                                       // 0x1800a0e8d, a BYTE store

    // Both auto-reset, both initially non-signalled. +0x48 FIRST, then +0x40 —
    // the order retail creates them in.
    _bufferEvent = Platform::EventCreate(false, false);   // 0x1800a0ea4  CreateEventW(0, FALSE, FALSE, 0)
    _wakeEvent   = Platform::EventCreate(false, false);   // 0x1800a0eb8

    if (_bufferEvent == nullptr || _wakeEvent == nullptr)
        ++g_ctorEventFail;
}

// ============================================================================
// ??1Stream@Audio@@QEAA@XZ                                 retail 0x1800a0d90
// ============================================================================
// The restamp (0x1800a0dad) is the compiler's; the inlined ~Thread
// (0x1800a0dda..0x1800a0e22: the Thread restamp, SetEvent(hStop), the join and
// the four base CloseHandles) is the compiler's base chain after this body.
Audio::Stream::~Stream() {
    ++g_dtorEntries;
    const Platform::EventHandle hFirst  = _bufferEvent;
    const Platform::EventHandle hSecond = _wakeEvent;

    Platform::EventClose(hFirst);                         // 0x1800a0db4, +0x48  CloseHandle
    Platform::EventClose(hSecond);                        // 0x1800a0dbe, +0x40  CloseHandle
    Platform::MutexDestroy(&_lock);                       // DeleteCriticalSection 0x1800a0dc9
    // 0x1800a0dd4 CALL 0x18000c4a0 — the +0x30 `Array<Block>::~Array`, a J1
    // body (free the buffer, zero the header; no element destroy): the member
    // `_buffers`' destructor, after this body and before ~Thread.
}

// ENDGAME B.3.9 ENG W2: the `??0Stream` / `??1Stream` DOORS and their name +
// address registrations are gone (their two callers construct / destroy the
// class), and so is `RtAudioStreamRegister`.
