#pragma once
// ============================================================================
// src/engine/audio/Stream.h — Audio::Stream, 0x88 bytes, a System::Thread
//                                subclass with a 2-slot table
// ============================================================================
//
// ENDGAME B.3 step 7, block B4. Before this header the layout lived as
// file-local `constexpr int kStream*` offsets in three .cpp files
// (audio_stream.cpp, audio_stream_play.cpp,
// audio_stream_run.cpp); they MOVE here under the retail name (B6-8).
//
//   sizeof 0x88   `??0Device@Audio@@QEAA@XZ` (0x1800de3a0) builds the Stream at
//                 Device+0xa00 (`LEA RCX,[RBX+0xa00]; CALL ??0Stream`) and the
//                 next Device member is at +0xa88; `??0Stream` (0x1800a0e30)
//                 stores up to the byte at +0x80 (B37_SLOTS.md section 1a).
//   parent        System::Thread: `??0Stream` CALLs `??0Thread@System@@QEAA@I@Z`
//                 with EDX = 0x10000 (a 64 KB stack) and then stamps
//                 `??_7Stream@Audio@@6B@`. No `_ParentTypeName` (not an
//                 Object::Base class).
//
// `??_7Stream@Audio@@6B@` @ 0x180154950, TWO slots (B37_SLOTS.md section 1):
//   [0] +0x00 0x1800a0f60  ?Run@Stream@Audio@@UEAAXXZ          — the worker body
//   [1] +0x08 0x18000c590  ?OnCompleted@Thread@System@@UEAAXXZ — inherited
// RULE B7-1.4: slot 0 is `Run`; there is NO deleting-dtor slot, and
// `??1Stream@Audio@@QEAA@XZ` (0x1800a0d90) is a NON-virtual `QEAA` export that
// re-stamps the Stream table and then runs the inlined `~Thread`.
//
// REAL CONSTRUCTOR since ENDGAME B.3.9 ENG W2: the one live Stream is built
// inside `Audio::Device` by `Ctor_AudioDevice` (src/engine/engine/
// singleton_ctors.cpp) as `::new (dev + 0xa00) Audio::Stream()`, and torn down
// by `Engine::Shutdown` with `~Stream()`. The `??0` / `??1` doors, the minted
// `??_7Stream@Audio@@6B@` and the `?Run@` slot door are retired. B.4 R-ENG:
// `Audio::Device::_stream` is this class.
//
// THREADS (RULE B7-7): `Run` executes on the AUDIO STREAM WORKER (a
// `_beginthreadex` thread); `Play` / `Stop` / `Lock` / `Unlock` run on the main
// thread. The only synchronisation is the raw CRITICAL_SECTION at +0x50 (the
// ctor's `InitializeCriticalSection` + spin count 5000) — B5-6: opaque storage,
// no `std::mutex`, no <windows.h> here — and the MemoryAllocator lock. No lock
// is added anywhere; the XAudio2 callback thread writes the Device's channel
// flags without one, by retail's design.
//
// PORTABILITY: no <windows.h> (PLAN.md section 4.1 decision 1). The two events
// are `Platform::EventHandle` (platform_sync.h, Phase D.4 A1): on Windows the
// value IS the Win32 event HANDLE, auto-reset, as the ctor always made them.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "../system/Thread.h"
#include "platform_sync.h"               // Platform::LockStorage _lock, Platform::EventHandle
#include "../collection/Collection.h"   // Collection::Array<Block> _buffers



namespace Audio {

// ============================================================================
// Audio::Stream — 0x88, a System::Thread subclass, 2 slots, NO deleting dtor
// ============================================================================
class Stream : public System::Thread {
public:
    // [0] 0x1800a0f60 — the real-time xWMA streaming loop. AUDIO WORKER THREAD.
    void Run() override;
    // [1] 0x18000c590 `System::Thread::OnCompleted` — inherited, not redeclared.

    // ?Play@Stream@Audio@@QEAAXHH@Z  0x1800a0d10 — main thread. Drains the
    // current pass (if any), grows the block table, stamps (channel, byte
    // position) and wakes the worker.
    void Play(int32_t channel, int32_t bytePosition);

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    // 0x18000b650 / 0x18000b660: `ADD RCX,0x50 ; JMP [Enter/LeaveCriticalSection]`.
    // Audio::Device's FreeChannelPlatform takes the same lock by hand
    // (Device+0xa50 == Stream+0x50), as retail's inlined copy does.
    void Lock();
    void Unlock();
    void Stop();                                   // 0x1800a0f40

private:
    void PlatformConstruct();                      // 0x1800a0ed0  (AEAA)
    void PlatformDestruct();                       // 0x1800a0f10  (AEAA)

public:
    // `??0Stream@Audio@@QEAA@XZ` 0x1800a0e30 — the REAL constructor (ENDGAME
    // B.3.9 ENG W2): `System::Thread(0x10000)` in the init list (retail's
    // `CALL ??0Thread` with EDX = 0x10000), the vptr the compiler's, then the
    // array header, the critical section and the two events (audio_stream.cpp).
    // Built in place inside Audio::Device (+0xa00) by Ctor_AudioDevice.
    Stream();
    // `??1Stream@Audio@@QEAA@XZ` 0x1800a0d90 — NON-virtual (ENDGAME B.2) and
    // PUBLIC like retail's QEAA export: Engine::Shutdown calls it directly on
    // Device+0xa00 (it never runs ~Device). The two events, the lock, the
    // block array; the compiler chains ~Thread (retail inlines it).
    ~Stream();

public:
    // One block of the streaming ring: `[0x40 bytes: 16 cumulative packet
    // offsets][0x8b60 bytes: 16 xWMA packets of 0x8b6]` (Run's arithmetic,
    // audio_stream_run.cpp). Grown to FOUR by Play through 0x18000c300.
    struct Block {
        uint32_t cumulativeBytes[16];   // +0x0000  XAUDIO2_BUFFER_WMA table
        uint8_t  packets[0x8b60];       // +0x0040  the audio
    };

    // ---- the 0x58 bytes Stream adds, at retail's offsets (public: Audio::Device
    // reaches them) ----
    // ctor 0x1800a0e67/0x1800a0e6b zero stores; ~Stream 0x1800a0dd4 CALL
    // 0x18000c4a0 (J1: free, zero; Block is trivial) after the body's handles
    // and critical section.
    Collection::Array<Block> _buffers;  // +0x30  (Run reads it)
    Platform::EventHandle _wakeEvent;   // +0x40  auto-reset — Play's "track changed"
    Platform::EventHandle _bufferEvent; // +0x48  auto-reset — PlatformChannel OnBufferEnd
    Platform::LockStorage _lock;        // +0x50  CRITICAL_SECTION (B5-6: opaque, built in place)
    int32_t  _bytePosition;             // +0x78
    int32_t  _channel;                  // +0x7c  the Device channel being streamed
    uint8_t  _stopping;                 // +0x80
    uint8_t  _pad81[7];                 // +0x81
};

static_assert(sizeof(Stream::Block) == 0x8ba0, "the block stride 0x18000c300 allocates");
static_assert(offsetof(Stream::Block, cumulativeBytes) == 0x0, "Block::cumulativeBytes");
static_assert(sizeof(Stream) == 0x88, "Audio::Stream is 0x88 bytes");
static_assert(offsetof(Stream, _buffers) == 0x30, "Stream::_buffers");
static_assert(offsetof(Stream, _bufferEvent) == 0x48, "Stream::_bufferEvent");
static_assert(offsetof(Stream, _bytePosition) == 0x78, "Stream::_bytePosition");
static_assert(offsetof(Stream, _channel) == 0x7C, "Stream::_channel");
static_assert(offsetof(Stream, _pad81) == 0x81, "Stream::_pad81");
static_assert(alignof(Stream) == 8, "Audio::Stream aligns to 8 (the vptr)");
static_assert(std::is_polymorphic<Stream>::value && !std::is_abstract<Stream>::value,
              "Stream overrides Thread's pure slot 0 (Run)");

}  // namespace Audio
