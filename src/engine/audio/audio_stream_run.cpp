// ============================================================================
// src/engine/audio/audio_stream_run.cpp — VTABLE BATCH 7b
// (was src/platform/win/vtbatch7b_stream_run_win.cpp until Phase D.4 SDL-4 step 1:
// its last Win32 call, SetFilePointer, is Platform::FileSeek32 now)
//   ?Run@Stream@Audio@@UEAAXXZ            retail 0x1800a0f60  ord 3602
// ============================================================================
//
// The real-time xWMA streaming decode loop: the body of the worker thread
// `Audio::Stream`'s base `System::Thread` starts. `Audio::Stream::Play`
// (audio_stream_play.cpp, already ours) is what wakes it; the ctor/dtor
// (audio_stream.cpp) are what create and join it.
//
// EXPORTED, checked against the retail export directory with pefile by RVA
// before anything was written (not taken from a worklist):
//     0x1800a0f60  ord 3602  ?Run@Stream@Audio@@UEAAXXZ
//
// HOW IT IS DISPATCHED. `System::Thread`'s vtable slot for `Run` is reached
// from the thread trampoline inside `??0Thread@System@@QEAA@I@Z`
// (src/platform/win/thread_win.cpp), so registering BOTH doors matters: the
// EXPORT door serves a `ResolveExport` caller and the ADDRESS door serves the
// vtable slot, which is a raw address until the mint owns
// `??_7Stream@Audio@@6B@` (0x180154950 — audio_stream.cpp's standing
// HANDOVER, unchanged by this file).
//
// It was its own `_win.cpp` for the reason both sibling files gave (PLAN.md
// §4.1 decision 1): it called SetFilePointer, WaitForMultipleObjects and the
// two critical-section imports directly. Phase D.4 moved each behind a
// primitive (THREADS: MutexLock / EventWaitAny; AUDIO-A: the voice calls;
// SDL-4 step 1: SetFilePointer -> Platform::FileSeek32), so it lives in
// src/engine/audio with no OS header.
//
// ---------------------------------------------------------------------------
// WHAT THE BODY ACTUALLY IS — and every offset in it has a named writer
// ---------------------------------------------------------------------------
// Read from Ghidra's `disassemble_function` over 0x1800a0f60..0x1800a1667
// (Ghidra's C output drops arguments on indirect calls —
// reference_ghidra_c_output_vs_disassembly.md — and this body is almost
// entirely indirect calls).
//
//   dev       = *(Device**)0x1801fe760            ?_singleton@Device@Audio@@
//   slot      = self->_channel        (+0x7c, stamped by Stream::Play)
//   voiceRec  = dev + 0xa98 + slot*0x20           the embedded
//               IXAudio2VoiceCallback record (audio_voice_callback.cpp §2)
//   chan      = dev + slot*0x28                   Device::AudioChannel[slot]
//               (audio_device.cpp: 64 of them at Device+0x00, stride 0x28)
//   buf       = chan->source          (+0x08)     Audio::SoundBuffer*
//
// and from `buf`, the three fields this body reads are the three
// vtbatch7_soundbuffer_soundeffect.cpp already pins from that class's OWN ctor
// and Serialize:
//   +0x20 loopForever      +0x30 the asset path String     +0x48/+0x50 the
//   xWMA seek table and its BYTE length (hence `>> 2` for its entry count).
//
// THE STREAM BUFFER TABLE. `Stream::Play` grows `self+0x30` to FOUR elements of
// 0x8ba0 bytes (0x18000c300, audio_stream_play.cpp's `GrowStreamBuffers`).
// This body is what gives those numbers meaning, and the arithmetic closes
// exactly:
//     16 packets * 0x8b6 bytes  = 0x8b60   the audio window
//     16 packets *    4 bytes   =   0x40   the per-packet cumulative table
//                                 ------
//                                 0x8ba0   = the element stride
// so each slot is `[0x40 bytes of XAUDIO2_BUFFER_WMA seek table][0x8b60 bytes
// of xWMA packets]`, the audio starting at +0x40. 0x8ba0/4 == 0x22e8, which is
// the constant in retail's index arithmetic at 0x1800a1212.
//
// THE COM CALLS. Two, both on the `IXAudio2SourceVoice` at voiceRec+0x08:
//   +0xc8  GetState(&XAUDIO2_VOICE_STATE)
//   +0xa8  SubmitSourceBuffer(&XAUDIO2_BUFFER, &XAUDIO2_BUFFER_WMA)
// The indices are NOT guessed from a header this project does not carry: they
// are forced by the two already-measured anchors in this tree —
// audio_voice_callback.cpp reads `*0x90` on a voice as DestroyVoice
// (IXAudio2Voice index 18) and audio_scene_update.cpp reads `*0x60` as
// SetVolume (index 12). With IXAudio2Voice ending at +0x90, IXAudio2SourceVoice
// continues Start(+0x98) Stop(+0xa0) SubmitSourceBuffer(+0xa8)
// FlushSourceBuffers(+0xb0) Discontinuity(+0xb8) ExitLoop(+0xc0) GetState(+0xc8),
// and the ARGUMENT SHAPES agree: +0xa8 takes two pointers, +0xc8 takes one
// out-pointer whose +0x08 dword this body compares against 3 and 0 — i.e.
// XAUDIO2_VOICE_STATE::BuffersQueued.
//
// THE TWO EVENTS IT WAITS ON, in retail's order:
//   handles[0] = self->+0x48   signalled by the VoiceCallback's OnBufferEnd
//                              (audio_voice_callback.cpp slot 4) — "a slot
//                              freed up". This body is also the WRITER that
//                              publishes the handle into voiceRec+0x18
//                              (0x1800a114d), which is the fact that file's §2
//                              cites for that field.
//   handles[1] = self->+0x40   Stream::Play's "drop what you are doing" wake.
// `WaitForMultipleObjects(2, handles, FALSE, INFINITE)` returning NON-ZERO
// therefore means "the track changed", and every such return abandons the pass.
//
// ---------------------------------------------------------------------------
// FOUR THINGS THAT LOOK WRONG IN THE TRANSCRIPTION AND ARE NOT
// ---------------------------------------------------------------------------
//  1. `curSeg` (the starting packet) is the index of the FIRST cumulative entry
//     STRICTLY GREATER than `self->_bytePos` (+0x78), and it stays 0 when no
//     entry exceeds it — because retail only stores the counter on the `JA`
//     exit (0x1800a1009), never on loop fall-through.
//  2. `file.cursor = curSeg * 0x8b6` is written BEFORE the seek and on the path
//     that does NOT seek. That is deliberate, for exactly the reason
//     closure_batch2b.cpp states about the same pair: a PACKED file has no
//     Win32 handle, and `FileSystem::ReadFile` reads from `cursor`, so the
//     store IS the seek. The unsigned `handle > 0xfffffffd` test selects
//     handle == -1 or -2 (FileSystem.h's encoding note) — "not packed".
//  3. The buffer-slot index is kept TWICE, as an int at +0x68 and an int64 at
//     +0x78, and both are reset to 0 together when the int reaches 4. They are
//     always equal; retail keeps two because one feeds a 32-bit IMUL and the
//     other a 64-bit one. Written as two variables here so a reader comparing
//     against the listing finds both.
//  4. The END_OF_STREAM flag is set only when the LAST chunk of a NON-LOOPING
//     buffer is submitted, and the test is inverted in the listing
//     (`CMP byte[buf+0x20],0 ; MOV EDX,0x40 ; CMOVZ EAX,EDX`) — loopForever ==
//     0 selects 0x40.
//
// ---------------------------------------------------------------------------
// EVERY CALLEE, AND ITS DOOR. All six are ALREADY OURS; this port opens no new
// address door at all.
// ---------------------------------------------------------------------------
//   0x1800aaa10  ??0FileStream@IO@@QEAA@XZ              file_stream.cpp
//   0x1800aac70  ?Open@FileStream@IO@@                  file_stream.cpp
//   0x180007bd0  ?GetDataDirectory@FileSystem@IO@@      filesystem.cpp
//   0x1800c9440  ??HSystem@@ (String operator+)         string.cpp
//   0x1800c6970  ?AdjustDelimiters@Filename@System@@    (registered)
//   0x1800ace00  ?ReadFile@FileSystem@IO@@              filesystem_openfile.cpp
//   0x1800accc0  ?CloseFile@FileSystem@IO@@             filesystem_createfile.cpp
//   0x1800aaad0  ??1FileStream@IO@@UEAA@XZ              file_stream.cpp
// Retail INLINES the FileStream ctor and dtor here (0x1800a100c..0x1800a106a
// and 0x1800a14b3..0x1800a1654); this port calls the exported bodies instead,
// the same deliberate departure closure_batch2b.cpp states for the same two
// functions — what is computed is identical, and a second hand copy of
// ~FileStream's String/FileInfo teardown in this tree is exactly what that
// choice avoids. Their inlined store lists were compared field for field first
// (FileStream.h's ctor note: +0x18 q, +0x20 d, +0x28 q, +0x30 d, +0x38 q(-1),
// +0x40 q, +0x48 q, and +0x24 left alone) and agree store for store.
//
// The two String temporaries around `GetDataDirectory() + buf->path` ARE
// written out, because retail's refcount traffic there is observable: the
// operator+ result is copy-constructed into a SECOND Filename with an
// `INC [rax-4]` (0x1800a10a6) and BOTH are released.
//
// ---------------------------------------------------------------------------
// ORACLE — NONE, and for a stronger reason than its two siblings
// ---------------------------------------------------------------------------
// This is a thread body that BLOCKS on real OS events and drives a live
// XAudio2 voice. All five probe shapes are ruled out by a fact, not by
// convenience: it is not pure (input sweep), a fabricated Stream would need a
// real voice and a real Device singleton (synthesised oracle), it is not a
// Serialize (write-path replay), and a two-sided A/B would submit every packet
// to the voice twice and consume the file handle twice. Attendance only —
// counters below, and no RT_DIFF site that could only ever print
// `divergences=0` (reference_shadow_marker_is_not_an_oracle.md).
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "system/Filename.h"
#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "audio/Device.h"
#include "audio/SoundBuffer.h"
#include "audio/Stream.h"
#include "platform_sync.h"   // Platform::MutexLock / MutexUnlock on the Stream lock, EventWaitAny
#include "platform_audio.h"  // Platform::AudioVoiceBuffersQueued / AudioVoiceSubmit (Phase D.4 A1)
#include "platform_file.h"   // Platform::FileSeek32 (SetFilePointer, FILE_BEGIN; Phase D.4 SDL-4)
#include "audio/xwma_decode.h"   // Audio::XwmaDecoder (Phase D.4 SDL-4 A5)

namespace {

// ENDGAME B.3.7 block B4: the `kStream*`, `kChannel*`, `kVoiceCallback*` and
// `kSb*` byte offsets that lived here MOVED into src/engine/audio/Stream.h,
// Device.h and SoundBuffer.h (B6-8). `Run` is `Audio::Stream::Run`, the slot-0
// override; the registered door delegates with a QUALIFIED call (a live Stream
// wears the minted table, whose slot 0 resolves back to that door).

// ---- the streaming geometry, all from this body ---------------------------
constexpr int32_t kPacketBytes   = 0x8b6;   // 0x1800a1172 IMUL EDX,EDX,0x8b6
constexpr int32_t kPacketsPerSlot = 16;     // 0x1800a1178 MOV R15D,0x10
static_assert(sizeof(Audio::Stream::Block) == 0x8ba0, "0x1800a11c0 IMUL RCX,RCX,0x8ba0");
constexpr int     kSlotCount     = 4;       // 0x1800a1380 CMP ECX,0x4
static_assert(offsetof(Audio::Stream::Block, packets) == 0x40, "0x1800a11d0 ADD RCX,0x40");
constexpr uint32_t kQueueHighWater = 3;     // 0x1800a1275 CMP …,0x3

// ---- Phase D.4 SDL-4 (A5, D4_RULES §3 Q5): the music is decoded HERE, on the
// Stream worker, chunk-ahead -- never on the audio callback. Each 16-packet Block
// the worker reads is decoded (XwmaDecoder::DecodeChunk) into the PCM slot of the
// same ring index, and the PCM is what is submitted. The backend references a
// submitted buffer until it finishes, exactly as XAudio2 referenced Block::packets,
// so the PCM ring has retail's Block ring's shape and lifetime: four slots, the
// high-water mark 3 keeps the slot being written out of the queue, and the ring
// is never freed or moved while a voice may still hold a buffer of it (a track
// change returns from Run with buffers still queued on the old voice, which is
// stopped and destroyed by the main thread later). The Stream's layout is
// retail's (0x88 bytes, Device+0xa00, no free pointer slot), and there is one
// Stream (the Device's), so the ring is this file's: allocated once, at the size
// any WMA v2 format the decoder accepts can need (mono / stereo, a frame of at
// most 2048 samples at <= 48 kHz: XwmaDecoder::MaxSamplesFor(16)). 7.9 MB.
constexpr size_t kMaxChannels    = 2;
constexpr size_t kMaxFrameLength = 2048;
constexpr size_t kPcmSlotSamples = (static_cast<size_t>(kPacketsPerSlot) * 15u + 2u) * kMaxFrameLength;

int16_t* PcmSlot(int slot) {
    static std::vector<int16_t> ring(kSlotCount * kPcmSlotSamples * kMaxChannels);
    return &ring[static_cast<size_t>(slot) * kPcmSlotSamples * kMaxChannels];
}

// ---- XAudio2 (2.7): GetState (+0xc8) and SubmitSourceBuffer (+0xa8) on the
// source voice. Phase D.4 A1: `Platform::AudioVoiceBuffersQueued` (GetState's
// XAUDIO2_VOICE_STATE local and its BuffersQueued read moved into it) and
// `Platform::AudioVoiceSubmit` (the XAUDIO2_BUFFER / XAUDIO2_BUFFER_WMA locals
// moved into it); since SDL-4 the backend is src/platform/sdl/audio_sdl.cpp.

uint64_t g_calls         = 0;
uint64_t g_opened        = 0;
uint64_t g_chunks        = 0;
uint64_t g_abandoned     = 0;   // the track changed mid-pass
uint64_t g_undecodable   = 0;   // the decoder refused the track's format (no shipped track)

struct RunSummary {
    ~RunSummary() {
        if (!g_calls) return;
        printf("[RtPort] Audio::Stream::Run ATTENDANCE: calls=%llu opened=%llu "
               "chunksSubmitted=%llu abandoned=%llu undecodable=%llu (NO ORACLE: a "
               "blocking thread body driving a live voice; see the banner)\n",
               (unsigned long long)g_calls, (unsigned long long)g_opened,
               (unsigned long long)g_chunks, (unsigned long long)g_abandoned,
               (unsigned long long)g_undecodable);
        fflush(stdout);
    }
};
RunSummary g_summary;

::IO::FileSystem* FileSystemSingleton() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now -- same one deref.
    return ::IO::FileSystem::_singleton;
}

// The inlined System::String refcount idioms. Written out rather than routed
// through ~String because the ASYMMETRY is observable: the freeing branch
// leaves the slot holding `str-8` and the surviving branch nulls it — retail's
// 0x1800a10cf..0x1800a10ef, and the same shape collection/array_destroy.cpp's
// Body B documents.
// ENDGAME B.3.1 step 2: the adopt at 0x1800a1096..0x1800a10a6 and the three
// releases at 0x1800a10c6 / 0x1800a10ef / 0x1800a1118 are System::String's own
// AddRef and destructor; the hand-written copies that lived here are gone.

}  // namespace

// ============================================================================
// Audio::Stream::Run() — retail 0x1800a0f60, ord 3602
// ============================================================================
namespace Audio {

// Not analysed (clang -Wthread-safety): retail's bail exits leave the Stream lock held and release
// it after the loop on `how`, which the analysis cannot follow.
void Stream::Run() NO_THREAD_SAFETY_ANALYSIS {
    ++g_calls;

    // `?_singleton@Device@Audio@@` (0x1801fe760), one load.
    Device* const dev = Device::_singleton;

    // Retail computes both record addresses from the singleton with no null
    // test of the singleton itself, then tests the SUMS (1800a0fa0 /
    // 1800a0fb1); the tests stay.
    const int64_t slot = _channel;                                      // MOVSXD
    Device::PlatformChannel* const voiceRec = &dev->_platformChannels[slot]; // 1800a0f9d
    if (!voiceRec) return;                                              // 1800a0fa0
    Device::AudioChannel* const chan = &dev->_channels[slot];           // 1800a0faa
    if (!chan) return;                                                  // 1800a0fb1
    const SoundBuffer* const buf = chan->source;
    if (!buf) return;                                                   // 1800a0fc2

    // _wmaLength is the table's BYTE length; >> 2 is its ENTRY count.
    const int32_t segCount =
        static_cast<int32_t>(static_cast<uint64_t>(
            static_cast<int64_t>(buf->_wmaLength)) >> 2);
    const uint32_t* const segTable = buf->_wmaData;

    // curSeg = first cumulative entry STRICTLY above the requested byte
    // position; 0 when none is (retail stores the counter only on the JA exit).
    int32_t curSeg = 0;                                                 // [RBP+0x60]
    if (segCount > 0) {                                                 // 1800a0ff2
        const uint32_t bytePos = static_cast<uint32_t>(_bytePosition);
        for (int32_t i = 0; i < segCount; ++i) {                        // 1800a0ff7
            if (segTable[i] > bytePos) { curSeg = i; break; }           // JA 1800a1009
        }
    }

    // ---- IO::FileStream st;  st.Open(FileSystem::GetDataDirectory() + buf->path)
    // Retail inlines the ctor here (1800a100c..6a) and the dtor at the end
    // (1800a14b3..1654); the local's scope is the function's.
    IO::FileStream st;

    ::IO::FileSystem* const fsys = FileSystemSingleton();

    // The three temporaries are declared so that reverse-order destruction is
    // retail's release order — path (1800a10c6), joined (1800a10ef), dataDir
    // (1800a1118) — and the inner scope ends them where retail ends them.
    bool opened;
    {
        // IO::FileSystem::GetDataDirectory (0x180007bd0), called by name
        // (B3R); its by-value result is constructed in place.
        System::Filename dataDir = fsys->GetDataDirectory();           // 1800a107f

        const System::String joined =
            dataDir.AsString() + buf->GetStreamPath();                  // 1800a1090

        System::Filename path;
        if (char* const d = joined._data) {                             // 1800a1096
            path._data = d;
            ++RtSystem::StringRefCount(d);
        }
        System::FilenamePrivateAccess::AdjustDelimiters(path);

        opened = st.IO::FileStream::Open(path);                         // 1800a10bc
    }

    if (opened) {                                                       // 1800a1147
        ++g_opened;

        // The VoiceCallback's OnBufferEnd signals THIS handle. Published here
        // and nowhere else — audio_voice_callback.cpp §2 cites this store.
        voiceRec->bufferEvent = _bufferEvent;                           // 1800a114d

        // Phase D.4 SDL-4 A5: one decoder per track (this Run), opened with the
        // format and the seek table; the packets arrive per chunk. A pass that
        // starts at curSeg > 0 (a resumed track) is a fresh decoder pass, which is
        // what a fresh XAudio2 voice plays from there (handins/XWMA.md A5.7). The
        // decoder accepts every shipped streamed format; one it refused could not
        // be played by SDL at all, so the pass is skipped (the file is still
        // closed below).
        XwmaDecoder decoder;
        // Hardening (review F-096 / F-097): the ring slot holds 16 packets of
        // retail's fixed 0x8b6 bytes (XAudio2 read nPackets * 0x8b6 bytes, never
        // a stride); the decoder strides by the format's nBlockAlign. A track
        // whose nBlockAlign is not 0x8b6 (the only size retail's fixed stride plays)
        // read past the slot or decoded the wrong byte ranges; it is undecodable.
        const bool decodable = decoder.Open(buf->_format, nullptr, 0, segTable,
                                            static_cast<uint32_t>(segCount)) &&
                               decoder.BlockAlign() == static_cast<uint32_t>(kPacketBytes);
        if (!decodable) ++g_undecodable;
        const size_t pcmSlotSamples = decodable ? decoder.MaxSamplesFor(kPacketsPerSlot) : 0u;
        const uint32_t pcmChannels = decoder.Channels();

        Platform::LockStorage* const lock = &_lock;
        Platform::EventHandle handles[2];                               // [RBP-0x50]
        uint32_t buffersQueued = 0;   // XAUDIO2_VOICE_STATE::BuffersQueued, [RBP-0x78] block

        int32_t curSeg32   = curSeg;      // EBX,  mirrors [RBP+0x60]
        int64_t curSeg64   = curSeg;      // R12
        int     slotIndex  = 0;           // [RBP+0x68], int
        int64_t slotIndex64 = 0;          // [RBP+0x78], int64 (always equal)

        enum Exit { kExitStop, kExitDrain, kExitBail } how = kExitStop;

        while (decodable && pcmSlotSamples <= kPcmSlotSamples) {        // 1800a1170 for (;;)
            const int32_t byteOffset = curSeg32 * kPacketBytes;         // 1800a1172
            int32_t nPackets   = kPacketsPerSlot;                       // R15D
            int64_t nPackets64 = kPacketsPerSlot;                       // R13
            if (curSeg32 + kPacketsPerSlot > segCount) {                // 1800a1184
                nPackets   = segCount - curSeg32;
                nPackets64 = static_cast<int64_t>(segCount) - curSeg64;
            }
            const int32_t byteCount = nPackets * kPacketBytes;          // 1800a1197

            // Note 2 in the banner: the cursor store IS the seek for a packed
            // file, and it happens on both paths.
            st._file.cursor = byteOffset;                               // 1800a11a0
            if (!RtIO::FileInfo::IsPackedHandle(st._file.handle))       // 1800a11a4
                Platform::FileSeek32(st._file.winHandle, byteOffset);   // SetFilePointer(h, off, NULL, FILE_BEGIN)

            // `slotIndex * 0x8ba0` (1800a11c0): the Block of the ring.
            Block* const block = &_buffers._data[slotIndex];
            fsys->ReadFile(st._file, byteCount, block->packets);        // 1800a11e7, 0x1800ace00

            // Rebase this chunk's cumulative-byte entries onto the chunk, and
            // write them into the slot's first 0x40 bytes.
            const uint32_t base =
                (curSeg32 == 0) ? 0u : segTable[curSeg64 - 1];          // 1800a11f2/f9
            if (nPackets64 > 0) {                                       // 1800a1201
                for (int64_t k = 0; k < nPackets64; ++k) {              // 1800a1230
                    _buffers._data[slotIndex64].cumulativeBytes[k] = segTable[curSeg64 + k] - base;
                }
            }

            // Phase D.4 SDL-4 A5: decode this chunk now, on the worker. A call whose
            // first packet is not the decoder's next one starts a fresh pass: the
            // first chunk of a Run, and the loop wrap to packet 0 below. XAudio2
            // dropped the first 512 samples after the wrap on 2 of 3 measured sounds
            // (XWMA A5.8); three points are not a rule, so the wrap is a plain fresh
            // pass (a measured-on-Windows difference candidate, SDL-4 hand-in).
            int16_t* const pcm = PcmSlot(slotIndex);
            const size_t pcmSamples = decoder.DecodeChunk(
                static_cast<uint32_t>(curSeg32), static_cast<uint32_t>(nPackets),
                block->packets, sizeof(block->packets), pcm, pcmSlotSamples);

            // ---- wait until the voice has fewer than 3 buffers queued ------
            Platform::MutexLock(lock);                                 // 1800a124b
            Platform::AudioVoice voice = voiceRec->voice;
            if (!voice) { how = kExitBail; break; }                     // 1800a1258
            buffersQueued = Platform::AudioVoiceBuffersQueued(voice);   // +0xc8 GetState
            Platform::MutexUnlock(lock);                                 // 1800a126f

            bool bailed = false;
            while (buffersQueued >= kQueueHighWater) {                  // 1800a1279
                handles[0] = _bufferEvent;                              // 1800a1280
                handles[1] = _wakeEvent;                                // 1800a1288
                const uint32_t woke = Platform::EventWaitAny(2, handles, Platform::kInfinite);
                Platform::MutexLock(lock);                             // 1800a12ab
                if (woke != 0) { bailed = true; break; }                // 1800a12b3
                voice = voiceRec->voice;
                if (!voice) { bailed = true; break; }                   // 1800a12c0
                buffersQueued = Platform::AudioVoiceBuffersQueued(voice);   // +0xc8 GetState
                Platform::MutexUnlock(lock);                             // 1800a12d7
            }
            if (bailed) { how = kExitBail; break; }
            curSeg32 = curSeg;   // 1800a12e3 — restores EBX after the wait

            // ---- build and submit the chunk --------------------------------
            // Retail: a dword zero at +0x00, then +0x04..+0x2b zeroed as ONE
            // 0x28-byte memset (1800a12e6 / 1800a12f7): every field zero; then
            // AudioBytes (1800a12fc), pAudioData (1800a1302 / 1800a130f) and
            // Flags END_OF_STREAM on the last chunk of a non-looping track
            // (CMOVZ). PlayBegin / PlayLength / LoopBegin / LoopLength /
            // LoopCount stay 0, pContext null: AudioVoiceSubmit's zeroes.
            curSeg32 += nPackets;                                       // 1800a1313
            curSeg = curSeg32;                                          // 1800a1316
            const bool endOfStream = curSeg32 >= segCount &&            // 1800a131f
                                     buf->_loopForever == 0;            // 1800a1328

            Platform::MutexLock(lock);                                 // 1800a1343
            voice = voiceRec->voice;
            if (!voice) { how = kExitBail; break; }                     // 1800a1350
            // Phase D.4 SDL-4 A5: the chunk's PCM, not its packets and table
            // (retail: pAudioData = block->packets, AudioBytes = byteCount, and the
            // XAUDIO2_BUFFER_WMA {block->cumulativeBytes, nPackets}).
            Platform::AudioVoiceSubmit(voice, endOfStream,              // 1800a1361 +0xa8
                                       reinterpret_cast<const uint8_t*>(pcm),   // CAST-OK: byte_io
                                       static_cast<uint32_t>(pcmSamples * pcmChannels * 2u),
                                       0, 0);
            Platform::MutexUnlock(lock);                                 // 1800a136b
            ++g_chunks;

            ++slotIndex;                                                // 1800a1374
            ++slotIndex64;                                              // 1800a1379
            if (slotIndex >= kSlotCount) {                              // 1800a1380
                slotIndex   = 0;
                slotIndex64 = 0;
            }

            curSeg64 += nPackets64;                                     // 1800a138d
            if (curSeg64 >= static_cast<int64_t>(segCount)) {           // 1800a1397
                if (buf->_loopForever == 0) {          // 1800a13a0
                    how = kExitDrain; break;                            // -> 1800a13bd
                }
                curSeg32 = 0; curSeg = 0; curSeg64 = 0;                 // 1800a13a6..ac
            }
            // NOT RETAIL (review F-137): an atomic load; Play and StopPlatform store
            // the byte on the main thread while this loop runs (retail: plain MOV).
            if (__atomic_load_n(&_stopping, __ATOMIC_RELAXED) != 0) {           // 1800a13af
                how = kExitStop; break;                                 // -> 1800a13da
            }
        }

        if (how == kExitBail) {
            // 1800a1487 — every bail leaves the lock HELD; this is its release.
            ++g_abandoned;
            Platform::MutexUnlock(lock);
        } else if (how == kExitDrain) {
            // 1800a13bd — the track finished and does not loop: wait for the
            // voice to play out what is already queued, unless the track
            // changes underneath us.
            for (;;) {
                Platform::MutexLock(lock);                             // 1800a13c1
                Platform::AudioVoice voice = voiceRec->voice;
                if (!voice) { Platform::MutexUnlock(lock); break; }      // 1800a13ce
                buffersQueued = Platform::AudioVoiceBuffersQueued(voice);   // +0xc8 GetState
                Platform::MutexUnlock(lock);                             // 1800a140c
                if (buffersQueued == 0) break;                          // 1800a1412 JBE

                handles[0] = _bufferEvent;                              // 1800a1420
                handles[1] = _wakeEvent;                                // 1800a1428
                const uint32_t woke = Platform::EventWaitAny(2, handles, Platform::kInfinite);
                Platform::MutexLock(lock);                             // 1800a144b
                if (woke != 0) {                                        // 1800a1453
                    ++g_abandoned;
                    Platform::MutexUnlock(lock);
                    break;
                }
                voice = voiceRec->voice;
                if (!voice) { Platform::MutexUnlock(lock); break; }      // 1800a1460
                buffersQueued = Platform::AudioVoiceBuffersQueued(voice);   // +0xc8 GetState
                Platform::MutexUnlock(lock);                             // 1800a1477
                if (buffersQueued == 0) break;                          // 1800a147d JA
            }
        }
        // kExitStop (1800a13af -> 1800a13da) holds no lock and needs no release.

        if (st._file.handle != RtIO::FileInfo::kHandleNone)             // 1800a13da
            fsys->CloseFile(st._file);                                  // 0x1800accc0
    }
}   // ~FileStream: 1800a14b3..1654

}  // namespace Audio

// ENDGAME B.3.9 ENG W2: the `?Run@Stream@Audio@@UEAAXXZ` slot door and its
// registration are gone with the minted `??_7Stream@Audio@@6B@`: the live
// Stream wears the compiler table, whose slot 0 is Audio::Stream::Run above.
