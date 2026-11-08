// ============================================================================
// src/engine/audio/vtbatch7_soundbuffer_soundeffect.cpp
//   VTABLE BATCH 7 — Audio::SoundBuffer and Audio::SoundEffect
// ============================================================================
//
// Both Object::External subclasses. Retail addresses read with objdump against
// the shipped Runtime-x64.dll (not Ghidra's decompile --
// reference_ghidra_c_output_vs_disassembly.md).
//
// ---------------------------------------------------------------------------
// Audio::SoundBuffer — ??_7SoundBuffer@Audio@@6B@, 0x80 bytes
// ---------------------------------------------------------------------------
//   slot0 deleting dtor   0x18000bef0  src/engine/object/deleting_dtor.cpp
//   slot1 TypeName        0x18000b8b0  here
//   slot2 Type            0x18000b890  here
//   slot3 GetFileVersion  0x1800e0470  here (returns 0xa)
//   slot5 Serialize       0x1800e0480  here
//   slot6 Reconstruct     0x18000bb30  here
//   slot7 Destruct        0x18000bc70  here
//   slot8 Construct       0x18000bbd0  here
//
// Layout (ctor 0x18000ba90, inlined into both Reconstruct and Construct):
// three-level chain External -> CompiledExternal -> SoundBuffer (matches
// Video::VertexLayout's shape in vertexlayout.cpp).
//   +0x00 vtable = ??_7SoundBuffer@Audio@@6B@
//   +0x08/+0x10/+0x18  External fields, zeroed
//   +0x20 byte   _loopForever
//   +0x21 byte   _streaming
//   +0x24 float  _loopDurationSeconds
//   +0x28 String _str1   (WRITER-confirmed by Serialize -- ctor zeroes it as a
//                         plain qword, Serialize is what proves it is a String)
//   +0x30 String _str2
//   +0x38 void*  _audioData
//   +0x40 int32  _audioBytes
//   +0x48 void*  _segmentTable   (ascending byte-offset boundaries, uint32[])
//   +0x50 int32  _segmentBytes   (that table's BYTE length)
//   +0x54 uint8_t[0x2c] _format  (WAVEFORMATEX + trailing bytes; Serialize
//                         streams exactly the first 0x28 of these 0x2c as raw
//                         bytes -- kSbFormat's field breakdown in
//                         audio_play_platform.cpp names +0x54/+0x56/+0x58/
//                         +0x60/+0x62 within this same window)
// Total 0x80, matches the deleting dtor's measured elemSize.
//
// ---------------------------------------------------------------------------
// Audio::SoundEffect — ??_7SoundEffect@Audio@@6B@, 0x40 bytes
// ---------------------------------------------------------------------------
//   slot0 deleting dtor   0x180050820  src/engine/object/deleting_dtor.cpp
//   slot1 TypeName        0x180050340  here
//   slot2 Type            0x180050320  here
//   slot3 GetFileVersion  0x1800e0990  here (returns 3)
//   slot4 Compile         0x1800e09a0  here
//   slot5 Serialize       0x1800e0aa0  here
//   slot6 Reconstruct     0x180050560  here
//   slot7 Destruct        0x180050640  here
//   slot8 Construct       0x1800505d0  here
//
// Layout (ctor inlined into Reconstruct/Construct, two-level chain, no
// CompiledExternal step -- confirmed: only Base+External+SoundEffect stamps):
//   +0x00 vtable = ??_7SoundEffect@Audio@@6B@
//   +0x08/+0x10/+0x18  External fields, zeroed
//   +0x20 float  _f20        default 1.0f  (ctor stores +0x20/+0x24 together
//   +0x24 float  _f24        default 0.0f   as one qword immediate 0x3f800000)
//   +0x28 int64  _offsetTicks
//   +0x30 Array<T> _buffers  {data, size@+0x38, cap@+0x3c} -- Serialize hands
//                            SetSize the address of +0x30 directly as the
//                            Array<T>*, so size/cap are NOT a separate cache
// Total 0x40.
//
// `Compile` (0x1800e09a0) IS fully reimplemented as of VTABLE BATCH 7b -- this
// banner used to say "NOT fully reimplemented ... only the outer shape is
// faithful", which was true of batch 7 and is no longer true of this file. The
// whole body, including the two-level sample/variant walk, is transcribed and
// its evidence is set out above the function.
// `Serialize`'s two nested-array helpers (0x180053190 SetSize,
// 0x180053300 element loop) were NOT ported by batch 7; they are reached
// through ResolveAddr (port registry first, DLL fallback), same reasoning as
// the AnimationGroup SetSize calls in vtbatch7_animgroup_particlesystem.cpp.
//
// UPDATE (cutover closure batch D1, 2026-09-12): BOTH ARE NOW PORTED —
// 0x180053190 as `kSpec_SfxBuffer18` in src/engine/collection/array.cpp, and
// 0x180053300 at the bottom of THIS file. The ResolveAddr calls are unchanged
// because the registry is consulted first; they now land on our bodies. The
// element layouts the two ports settled are recorded with them:
//   _buffers element  {float @+0x00, Array<Sample> @+0x08}      stride 0x18
//   Sample   element  {ExternalPtr @+0x00, 3x float @+0x08..}   stride 0x18
//
// ENDGAME B.3 step 7, block B4: both classes are REAL C++ classes now
// (src/engine/audio/SoundBuffer.h, SoundEffect.h). The two anonymous-
// namespace structs that carried the layouts here MOVED into those headers
// under the retail names and the getter-attested field names (B6-8): the
// SoundBuffer's `str1`/`str2` are `_wave`/`_streamPath`, `audioData`/
// `audioBytes` are `_data`/`_length`, `segmentTable`/`segmentBytes` are
// `_wmaData`/`_wmaLength`; the SoundEffect's `f20`/`f24` are `_radius`/
// `_innerRadius`, `offsetTicks` is `_maxLength` and the `buffer*` triple is
// `_samples` (Array<Sample>). The slot bodies are the members below; every
// registered door is a one-line QUALIFIED delegation, because a live object
// wears the MINTED table whose slots resolve back to these doors.
//
// The by-address calls the move converted (RULE B7-11, hex literals checked
// against standalone/rt_export_addrs.inc):
//   0x1800c9930 `?Serialize@String@System@@` -> `System::String::Serialize`
//   0x1800c7ad0 `?MemFree@MemoryAllocator@System@@` -> `rtcoll::MemFree`, the
//               one allocator door PlatformSerialize's `rtcoll::MemAlloc`
//               already uses (Collection.h's allocator banner: a heap must not
//               be half-shimmed; same allocator on a normal run)
//   0x180053300 (unexported, this file's own body) -> a direct call
// 0x180053190 / 0x180052e60 (the two SoundEffect Array SetSize bodies,
// Collection code in array.cpp) and 0x180052a50 (the ExternalPtr<SoundBuffer>
// serialize helper) stay by-address calls: unexported, not this block's, and
// resolved per call with no cache.
//
// ENDGAME B.3.9 ENG (B39_RULES D1/D2): both classes are CONCRETE. Their real
// constructors (the headers) hold the factory rows' stores, their DeletingDtor
// the retired RT_DELDTOR rows, and every live object wears the COMPILER's
// table. The sixteen slot doors, `InlineCtorStores` and `CallSlot0` went with
// the minted `??_7SoundBuffer@Audio@@6B@` / `??_7SoundEffect@Audio@@6B@` rows
// and the FACTREST pair (rt_factories_audio.{cpp,inc}); the factory pairs are
// the classes' own statics below. ~SoundEffect's body moved here from the
// retired deleting_dtor_leaves.cpp door; ~SoundBuffer's is in
// batchc_audio_leaves.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>          // placement new of the two SoundBuffer Strings

#include "io/Serializer.h"
#include "io/Stream.h"
#include "object/External.h"
#include "collection/Collection.h"   // rtcoll::MemAlloc / MemFree
#include "system/String.h"
#include "audio/SoundBuffer.h"
#include "audio/SoundEffect.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "platform_audio.h"   // Platform::AudioWaveFormat (Phase D.4 A2 census)
#include "audio/xwma_decode.h"  // Audio::XwmaDecoder (Phase D.4 SDL-4 decode at load)
#include "audio/audio_pcm.h"    // Audio::RewriteFormatAsPcm
#include "audio/audio_cvtt.h"   // Audio::CvttSd2Si64 (review F-136)
#include "audio/load_decode_batch.h"  // block SNDPAR: the decodes of one load, in parallel

#include <atomic>
#include <cstdio>

extern "C" void RtAudio_SoundEffect_SerializeBuffers(IO::Serializer* ser, int32_t count,
                                                    Audio::SoundEffect::Sample* samples);

namespace {

// Retail's Serialize bodies build a stack-local `{stream, 0}` Serializer, ask
// the Encoding for a reader or writer (version 0) and delete it at the end:
// IO::Serializer's constructor and destructor, inlined.
bool IsReading(const IO::Stream* s) { return s->_direction == IO::kReading; }

// `?MemFree@MemoryAllocator@System@@` — retail tests for null OUTSIDE the call.
void MemFreeIfSet(void* p) {
    if (!p) return;
    rtcoll::MemFree(p);
}

}  // namespace

// ############################################################################
// Audio::SoundBuffer's member bodies
// ############################################################################
namespace Audio {

// TypeId 0x18000b8a0 / [2] 0x18000b890 / [1] 0x18000b8b0: DECLARE_TYPE's
// loads over the class's own record (audio_type_records.cpp; ENDGAME B.3.9
// TI-REST).
// [3] 0x1800e0470: `MOV EAX,0xa ; RET`.
int32_t SoundBuffer::GetFileVersion() const { return 0xa; }

// [8] 0x18000bbd0 -- the inlined constructor, i.e. placement construction.
// (Retail's null test sat in the retired door; a virtual call always has a
// `this`.) `::new`: the class declares its own placement new.
Object::External* SoundBuffer::Construct() {
    return ::new (this) SoundBuffer();
}
// [6] 0x18000bb30: slot0(this, 0) dispatched VIRTUALLY, then the same stores.
Object::External* SoundBuffer::Reconstruct() {
    this->~SoundBuffer();
    return ::new (this) SoundBuffer();
}
// [7] 0x18000bc70: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void SoundBuffer::Destruct() { this->~SoundBuffer(); }

// ?CreateNewObject@SoundBuffer@Audio@@SAPEAVBase@Object@@XZ  0x18000b9a0
// The FACTREST row this replaces: MemAlloc of 0x80 bytes, align 8 (the locked
// `?MemAlloc@MemoryAllocator@System@@` Object::detail::InternalMemAlloc
// reaches); retail's null branch; then the inlined constructor.
Object::Base* SoundBuffer::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SoundBuffer), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) SoundBuffer();
}
// ?CreateInPlaceObject@SoundBuffer@Audio@@SAPEAVBase@Object@@PEAX@Z  0x18000ba90
Object::Base* SoundBuffer::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) SoundBuffer();
}

// 0x1800e05c0 PlatformSerialize, the raw-buffer half of Serialize, is in
// soundbuffer_platform_serialize.cpp (moved out so the unit build links it without
// this file's slot bodies, which tests/unit/audio_abi_test.cpp replaces with markers).

// ---------------------------------------------------------------------------
// Phase D.4 A2 -- the format census (diag, NOT retail; SoundBuffer.h says why).
// A fixed table of distinct (WAVEFORMATEX, streamed) keys with a count each,
// filled on every Serialize READ. SoundBuffers load on the loader thread as well
// as the main thread, so an atomic_flag guards the table (a diagnostic's own
// lock; no retail path waits on it). Printed once, at exit.
// ---------------------------------------------------------------------------
namespace {

struct FormatCensusRow {
    uint16_t formatTag, channels;
    uint32_t samplesPerSec, avgBytesPerSec;
    uint16_t blockAlign, bitsPerSample, extraSize;
    bool     streamed;
    uint32_t count;
};
constexpr int kFormatCensusRows = 32;
FormatCensusRow g_formatCensus[kFormatCensusRows];
int g_formatCensusUsed = 0;
uint32_t g_formatCensusOverflow = 0;   // buffers whose format found no free row
std::atomic_flag g_formatCensusBusy = ATOMIC_FLAG_INIT;

void CountSoundBufferFormat(const SoundBuffer& buf) {
    const auto* f = reinterpret_cast<const Platform::AudioWaveFormat*>(buf._format);  // CAST-OK: byte_io
    const bool streamed = buf._streamed != 0;
    while (g_formatCensusBusy.test_and_set(std::memory_order_acquire)) {}
    int i = 0;
    for (; i < g_formatCensusUsed; ++i) {
        const FormatCensusRow& r = g_formatCensus[i];
        if (r.formatTag == f->formatTag && r.channels == f->channels &&
            r.samplesPerSec == f->samplesPerSec && r.avgBytesPerSec == f->avgBytesPerSec &&
            r.blockAlign == f->blockAlign && r.bitsPerSample == f->bitsPerSample &&
            r.extraSize == f->extraSize && r.streamed == streamed)
            break;
    }
    if (i < g_formatCensusUsed) {
        ++g_formatCensus[i].count;
    } else if (g_formatCensusUsed < kFormatCensusRows) {
        g_formatCensus[g_formatCensusUsed++] =
            FormatCensusRow{f->formatTag, f->channels, f->samplesPerSec, f->avgBytesPerSec,
                            f->blockAlign, f->bitsPerSample, f->extraSize, streamed, 1u};
    } else {
        ++g_formatCensusOverflow;
    }
    g_formatCensusBusy.clear(std::memory_order_release);
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase D.4 SDL-4 -- DECODE AT LOAD (D4_RULES §3 Q5; not retail, SoundBuffer.h's
// banner says why). Counters for the exit line below; atomics because buffers
// load on the loader thread and the main thread.
// ---------------------------------------------------------------------------
namespace {
std::atomic<uint32_t> g_decodedBuffers{0};
std::atomic<uint32_t> g_decodeRefused{0};        // the decoder refused the format: silent buffer
std::atomic<uint32_t> g_decodeShort{0};          // fewer samples than the table (invalid packets)
std::atomic<uint64_t> g_decodedXwmaBytes{0};     // packet bytes replaced
std::atomic<uint64_t> g_decodedPcmBytes{0};      // PCM bytes now held
#ifndef BANISHED_STRIP_LOGS
// The content of every decoded buffer, for the exit line (LOADPLAN AUDIODEC: the
// coordinator compares it across a decoder change). Per buffer an FNV-1a over its
// 64-bit words and its length; summed, so the two loading threads' order does not
// matter. Debug / og only: the perf and release exes strip the line, so they skip
// the hash too.
std::atomic<uint64_t> g_decodedPcmHash{0};
uint64_t PcmHash(const int16_t* pcm, size_t values) {
    uint64_t h = 0xcbf29ce484222325ull ^ static_cast<uint64_t>(values);
    size_t i = 0;
    for (; i + 4 <= values; i += 4) {
        uint64_t w;
        std::memcpy(&w, pcm + i, sizeof(w));
        h = (h ^ w) * 0x100000001b3ull;
    }
    for (; i < values; ++i) h = (h ^ static_cast<uint16_t>(pcm[i])) * 0x100000001b3ull;
    return h;
}
#endif
}  // namespace

SoundBuffer::ExtLoopSeam SoundBuffer::_extLoopSeam = nullptr;   // EXT SEAM (NOT RETAIL, docs/extensions/handins/SEAMLESS.md): ext.seamless_loops' loop hook, null by default (SoundBuffer.h)

// Block SNDPAR: the decode is split in two so that a load's buffers can decode in
// parallel (src/engine/audio/load_decode_batch.h). DecodeXwmaJob is the pure half: it
// reads the buffer's format, packets and seek table and writes only the job. FinishXwmaJob
// is everything the one-piece body did after its decoder call, in the same order: the
// counters, the loop seam, the packet free, the stores into `_data` / `_length` /
// `_decodedXwma` / `_format`. Inline (no Scope open) the two run back to back, which is
// the one-piece body; deferred, the batch runs the decodes on the pool and the finishes on
// the loading thread in queue order, so the [SeamlessLoops] lines keep their order too.
namespace {

struct XwmaJob {
    int16_t* pcm = nullptr;
    size_t   samples = 0;
    uint64_t total = 0;
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t frameLength = 0;
    bool     opened = false;
};

void DecodeXwmaJob(const SoundBuffer& buf, XwmaJob& job) {
    const uint32_t packetCount = static_cast<uint32_t>(
        static_cast<uint64_t>(static_cast<int64_t>(buf._wmaLength)) >> 2);

    // One decoder per call: buffers load on two threads (and, deferred, on the pool);
    // the decoder's shared tables are built under a real once-init (wmav2_decoder.h).
    XwmaDecoder dec;
    if (dec.Open(buf._format, buf._data, buf._length > 0 ? static_cast<size_t>(buf._length) : 0u,
                 buf._wmaData, packetCount)) {
        job.opened = true;
        // Sized by the table's total; the RETURNED count is the PCM's length: a
        // packet the decoder rejects yields nothing (Tornado.wav's last six,
        // handins/XWMA.md A4.4 and known_port_issues row 1276), so Failed() is
        // not fatal. TotalSamples is capped at what the packets can decode to
        // (review F-073: the table is untrusted).
        job.total = dec.TotalSamples();
        const uint64_t bytes = job.total * dec.Channels() * 2u;
        if (bytes > 0) {
            job.pcm = static_cast<int16_t*>(rtcoll::MemAlloc(bytes, 4));
            job.samples = dec.DecodePackets(0, dec.PacketCount(), job.pcm, static_cast<size_t>(job.total));
        }
        job.channels = dec.Channels();
        job.sampleRate = dec.SampleRate();
        job.frameLength = static_cast<uint16_t>(dec.FrameLength());
    }
}

void FinishXwmaJob(SoundBuffer& buf, const XwmaJob& job) {
    auto* const fmt = reinterpret_cast<Platform::AudioWaveFormat*>(buf._format);  // CAST-OK: byte_io
    const uint32_t packetBytes = fmt->blockAlign;
    int16_t* const pcm = job.pcm;
    size_t samples = job.samples;
    if (job.opened) {
        if (samples < job.total) g_decodeShort.fetch_add(1, std::memory_order_relaxed);
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/SEAMLESS.md): ext.seamless_loops -- a looping buffer's PCM may be rewritten in place
        // and shortened. Null unless the row was on at boot; `_loopForever` was read by Serialize
        // before the payload, and PlayPlatform loops a buffer by that flag alone.
        if (LoopSeamApplies(SoundBuffer::_extLoopSeam != nullptr, buf._loopForever, pcm, samples))
            samples = SoundBuffer::_extLoopSeam(pcm, static_cast<uint32_t>(samples), job.channels,
                                                job.sampleRate, buf._wave._data);
    } else {
        // Not mono / stereo WMA v2 or a short payload: no shipped sound. SDL
        // cannot play the packets, so the buffer becomes an empty PCM buffer.
        g_decodeRefused.fetch_add(1, std::memory_order_relaxed);
    }

    g_decodedXwmaBytes.fetch_add(static_cast<uint64_t>(buf._length > 0 ? buf._length : 0),
                                 std::memory_order_relaxed);
    MemFreeIfSet(buf._data);                      // the packets
    buf._data   = reinterpret_cast<uint8_t*>(pcm);   // CAST-OK: byte_io (S16 PCM held as the buffer's bytes)
    buf._length = static_cast<int32_t>(samples * 2u * fmt->channels);
    g_decodedPcmBytes.fetch_add(static_cast<uint64_t>(buf._length), std::memory_order_relaxed);
#ifndef BANISHED_STRIP_LOGS
    g_decodedPcmHash.fetch_add(PcmHash(pcm, samples * fmt->channels), std::memory_order_relaxed);
#endif
    g_decodedBuffers.fetch_add(1, std::memory_order_relaxed);

    buf._decodedXwma = SoundBuffer::DecodedXwma{static_cast<uint16_t>(packetBytes != 0 ? packetBytes : 1),
                                                job.frameLength};
    RewriteFormatAsPcm(*fmt);
}

// The batch's two callbacks (LoadDecode::JobFn): the item is the SoundBuffer, the result
// its XwmaJob, which the finish releases.
void DecodeXwmaJobFn(void* item, void* result) {
    DecodeXwmaJob(*static_cast<const SoundBuffer*>(item), *static_cast<XwmaJob*>(result));  // CAST-OK: user_data
}
void FinishXwmaJobFn(void* item, void* result) {
    XwmaJob* const job = static_cast<XwmaJob*>(result);   // CAST-OK: user_data
    FinishXwmaJob(*static_cast<SoundBuffer*>(item), *job);  // CAST-OK: user_data
    delete job;
}

}  // namespace

void SoundBuffer::DecodeAtLoad() {
    _decodedXwma = DecodedXwma{0, 0};
    if (_streamed) return;                        // the Stream worker decodes the music
    const auto* const fmt = reinterpret_cast<const Platform::AudioWaveFormat*>(_format);  // CAST-OK: byte_io
    if (fmt->formatTag != Platform::kAudioFormatXwma) return;

    // Block SNDPAR: under an External::Load, queue it for the load's batch (the packet
    // bytes are its cost: the decode time is about proportional to them).
    if (LoadDecode::BatchOpen()) {
        XwmaJob* const job = new XwmaJob();
        if (LoadDecode::Defer(this, job, static_cast<uint64_t>(_length > 0 ? _length : 0),
                              &DecodeXwmaJobFn, &FinishXwmaJobFn))
            return;
        delete job;
    }
    XwmaJob job;
    DecodeXwmaJob(*this, job);
    FinishXwmaJob(*this, job);
}

void SoundBuffer::EnsureDecodedAtLoad() const { LoadDecode::Await(this); }

void DumpSoundBufferFormatCensus() {
    // Phase D.4 SDL-4: the decode-at-load total (the memory figure in the hand-in).
#ifndef BANISHED_STRIP_LOGS
    printf("[SoundDecode] decoded=%u refused=%u short=%u xwmaBytes=%llu pcmBytes=%llu "
           "pcmHash=%016llx\n",
           g_decodedBuffers.load(), g_decodeRefused.load(), g_decodeShort.load(),
           static_cast<unsigned long long>(g_decodedXwmaBytes.load()),
           static_cast<unsigned long long>(g_decodedPcmBytes.load()),
           static_cast<unsigned long long>(g_decodedPcmHash.load()));
#endif
    // One line per distinct format; a run that loaded none says so once.
    if (g_formatCensusUsed == 0)
        printf("[SoundFormat] none (no SoundBuffer was loaded)\n");
    for (int i = 0; i < g_formatCensusUsed; ++i) {
        // maybe_unused: the perf build compiles printf away (log_strip.h).
        [[maybe_unused]] const FormatCensusRow& r = g_formatCensus[i];
        printf("[SoundFormat] tag=0x%04x channels=%u rate=%u avgBytesPerSec=%u blockAlign=%u "
               "bits=%u cbSize=%u streamed=%d count=%u overflow=%u\n",
               r.formatTag, r.channels, r.samplesPerSec, r.avgBytesPerSec, r.blockAlign,
               r.bitsPerSample, r.extraSize, r.streamed ? 1 : 0, r.count,
               g_formatCensusOverflow);
    }
    fflush(stdout);
}

// [5] 0x1800e0480.
bool SoundBuffer::Serialize(IO::Stream& streamv) {
    EnsureDecodedAtLoad();                // block SNDPAR (not retail): no decode of this buffer in flight
    IO::Stream* stream = &streamv;
    IO::Serializer ser(*stream, 0);

    ser._readerWriter->SerializeBool(_loopForever, nullptr);        // +0x78
    ser._readerWriter->SerializeBool(_streamed, nullptr);

    // `?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z` (0x1800c9930).
    _wave.Serialize(ser);
    _streamPath.Serialize(ser);

    ser._readerWriter->SerializeFloat(_durationSeconds, nullptr);   // +0x80

    PlatformSerialize(ser);

    if (IsReading(ser._stream)) {         // Phase D.4 A2 census (diag, not retail)
        CountSoundBufferFormat(*this);    // the format as stored (xWMA)
        DecodeAtLoad();                   // Phase D.4 SDL-4 (Q5): xWMA -> PCM, not retail
    }

    return true;
}   // ~Serializer: `if (rw) delete rw`

// (~SoundBuffer, 0x1800e03b0, is defined in batchc_audio_leaves.cpp next to
// the Device callback it opens with.)

// ############################################################################
// Audio::SoundEffect's member bodies
// ############################################################################

// TypeId 0x180050330 / [2] 0x180050320 / [1] 0x180050340: DECLARE_TYPE's
// loads over the class's own record (audio_type_records.cpp).
// [3] 0x1800e0990: `MOV EAX,3 ; RET`.
int32_t SoundEffect::GetFileVersion() const { return 3; }

// [8] 0x1800505d0 -- the inlined constructor (placement construction).
Object::External* SoundEffect::Construct() {
    return ::new (this) SoundEffect();
}
// [6] 0x180050560: slot0(this, 0) VIRTUALLY, then the same stores.
Object::External* SoundEffect::Reconstruct() {
    this->~SoundEffect();
    return ::new (this) SoundEffect();
}
// [7] 0x180050640.
void SoundEffect::Destruct() { this->~SoundEffect(); }

// ?CreateNewObject@SoundEffect@Audio@@SAPEAVBase@Object@@XZ  0x180050430
// MemAlloc of 0x40 bytes, align 8; retail's null branch; the inlined ctor.
Object::Base* SoundEffect::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SoundEffect), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) SoundEffect();
}
// ?CreateInPlaceObject@SoundEffect@Audio@@SAPEAVBase@Object@@PEAX@Z  0x1800504f0
Object::Base* SoundEffect::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) SoundEffect();
}

// ============================================================================
// 0x1800e09a0 — `?Compile@SoundEffect@Audio@@`, WHOLE, byte for byte.
//
// *** BATCH 7 LEFT THIS HALF-PORTED AND REGISTERED, WHICH IS WORSE THAN A TRAP.
// *** Its banner said "the per-buffer inner loop ... was read from the
// disassembly but not fully re-derived to certainty (the inner table's element
// shape is inferred)". Batch 7b re-read the whole body (Ghidra
// disassemble_function on retail 0x1800e09a0..0x1800e0a8b) and the inner
// table's shape is NOT inferred — it has an independent second witness in this
// tree, written months earlier by a READER:
// src/engine/audio/audio_scene_update.cpp:145-158 measured the same two levels
// off `?PlayEffect@Scene@Audio@@` (0x1800df7e0 / 0x1800dfc42 / 0x1800df9fe):
//
//     SoundEffect  +0x28 int64 duration, +0x30 sample table, +0x38 sample count
//     sample entry stride 0x18:  +0x00 float start SECONDS, +0x08 variant
//                                table, +0x10 variant count
//     variant entry stride 0x18: +0x00 ExternalPtr<SoundBuffer>, then floats
//
// This body (a WRITER) reaches exactly those offsets with exactly those
// strides, and it dereferences the variant's +0x00 as a SoundBuffer, reading
// +0x20 and +0x24 — which this same file's SoundBuffer struct above already
// pins as `loopForever` and `loopDurationSeconds` from a third direction (that
// class's own ctor and Serialize). Three independent witnesses; nothing here is
// inferred from a shape.
//
// THE BODY, instruction for instruction:
//
//   1800e09a6  XORPS XMM0,XMM0 / DIVSD XMM0,[_secondsPerTick] / CVTTSD2SI
//   1800e09bc  MOV [RCX+0x28],RAX            self->_durationTicks = (i64)(0.0/spt)
//   1800e09c0  MOV EAX,[RCX+0x38] ; TEST ; JLE 1800e0a80 -> return 1
//   1800e09d5  RDI = self->_samples ; EBP = count ; ESI = 1 ; R11B = 0
//   1800e09e0  R9 = RDI+0x10 (&sample->variantCount)
//   -- outer, back edge 1800e0a5e -> 1800e09e4, RDI/R9 both += 0x18 --
//     1800e09e6  if (*R9 <= 0) skip the inner loop entirely
//     1800e09eb  R10 = self->_durationTicks              (RELOADED per sample)
//     -- inner, RDX = k*0x18 --
//       1800e0a00  RAX = *(R9-8) = sample->_variants
//       1800e0a04  XMM1 = 0.0f
//       1800e0a07  R8 = *(SoundBuffer**)(RAX + RDX)
//       1800e0a0e  if (R8 == 0) leave XMM1 at 0.0f
//       1800e0a10  CMP [R8+0x20],0 / MOVSS XMM1,[R8+0x24] / ADDSS XMM1,[RDI]
//       1800e0a23  CMOVNZ R11D,ESI            anyLoopForever |= buf->loopForever
//       1800e0a2a  CVTSI2SS XMM0,R10 ; COMISS XMM1,XMM0 ; JBE skip
//       1800e0a34  R10 = (i64)((double)XMM1 / spt) ; MOV [RBX+0x28],R10
//   1800e0a6c  if (R11B) MOV qword [RBX+0x28],0
//   1800e0a6a/80 EAX = 1 on both exits
//
// TWO THINGS THAT LOOK LIKE TRANSCRIPTION ERRORS AND ARE NOT:
//
//  1. `COMISS XMM1, (float)R10` compares a value in SECONDS against a value in
//     TICKS. R10 starts at 0, so the first candidate always wins; after that
//     R10 holds a tick count (typically 10^4..10^7) and no later per-buffer
//     SECONDS value can exceed it, so in practice only the first non-zero
//     candidate is ever taken. That is retail's arithmetic, reproduced as
//     written. Fixing it would be a behaviour change, not a correction.
//  2. The running maximum is cached in R10 but RELOADED from self->+0x28 at the
//     top of every OUTER iteration (0x1800e09eb is inside the outer loop, past
//     the `JLE`), and only when that sample has variants. A sample with zero
//     variants therefore leaves the cache untouched — which is the same value,
//     so the reload is observationally a no-op and is written that way below.
//
// `anyLoopForever` clears the duration to 0 — the "this effect never ends"
// sentinel. It is set even for a buffer whose duration does NOT win the
// comparison, because the CMOVNZ is before the COMISS.
//
// Self-contained: no calls at all, and one data read (_secondsPerTick).
// ============================================================================
// (B.3.7: `self->_durationTicks` / `self->_samples` above are the members
// `_maxLength` / `_samples`; the SoundBuffer's +0x20/+0x24 are `_loopForever`
// and `_durationSeconds`.)
Object::External::CompileResult SoundEffect::Compile(const System::Filename&) {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    const double secondsPerTick = ::System::Timer::_secondsPerTick;
    _maxLength = static_cast<int64_t>(0.0 / secondsPerTick);          // 1800e09bc

    const int32_t sampleCount = _samples._size;                        // 1800e09c0
    if (sampleCount <= 0)
        return 1;                                                      // 1800e0a80

    const Sample* sample = _samples._data;                             // 1800e09d5
    bool anyLoopForever = false;                                        // R11B

    for (int32_t s = 0; s < sampleCount; ++s, ++sample) {
        const int32_t variantCount = sample->variants._size;
        if (variantCount <= 0) continue;                                // 1800e09e9

        int64_t running = _maxLength;                                   // 1800e09eb
        const Variant* const variants = sample->variants._data;
        const float startSeconds = sample->startSeconds;

        for (int32_t k = 0; k < variantCount; ++k) {
            const SoundBuffer* buf = variants[k].buffer.Get();          // 1800e0a07
            float end = 0.0f;                                            // XMM1
            if (buf) {                                                   // 1800e0a0e
                end = buf->_durationSeconds + startSeconds;              // 1800e0a15/1f
                if (buf->_loopForever != 0) anyLoopForever = true;       // 1800e0a23
            }
            // Retail's own mixed-unit compare; see note 1 in the banner.
            if (end > static_cast<float>(running)) {                     // 1800e0a2f
                // NOT RETAIL (review F-136, undefined behaviour): CVTTSD2SI's
                // INT64_MIN for an out-of-range end, without the C++ cast's UB.
                running = Audio::CvttSd2Si64(static_cast<double>(end) / secondsPerTick);
                _maxLength = running;                                    // 1800e0a44
            }
        }
    }

    if (anyLoopForever)                                                  // 1800e0a6f
        _maxLength = 0;                                                  // 1800e0a71

    return 1;  // External::CompileResult::Success — retail returns 1 on both exits
}

// [5] 0x1800e0aa0.
bool SoundEffect::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;
    IO::Serializer ser(*stream, 0);

    ser._readerWriter->SerializeFloat(_radius, nullptr);            // +0x80
    ser._readerWriter->SerializeFloat(_innerRadius, nullptr);       // +0x80

    int32_t n = _samples._size;
    ser._readerWriter->SerializeInt(n, nullptr);                    // +0xa0
    if (IsReading(stream)) {
        // 0x180053190 — kSpec_SfxBuffer18 (array.cpp), unexported Collection code.
        rtcoll::RtColl_ArraySetSize_180053190(&_samples, n);
    }
    // 0x180053300 — this file's own element walk, called directly.
    RtAudio_SoundEffect_SerializeBuffers(&ser, n, _samples._data);

    // GetVersion()==0 (writer path skips; reader path on an OLD save consumes
    // and discards one legacy field via a throwaway CreateReader/CreateWriter
    // pair). retail: `if (rw->GetVersion() >= 1) ScalarInt64(self+0x28) else if
    // (writing) {} else { consume-and-release a temp }`.
    const int32_t version = ser._readerWriter->GetVersion();        // +0x08
    if (version >= 1) {
        ser._readerWriter->SerializeInt64(_maxLength, nullptr);     // +0x90
    } else if (IsReading(stream)) {
        // retail (0x1800e0b88..0x1800e0bc3) calls vtable[0x20](this->encoding,
        // &ser, 0) to create ANOTHER throwaway ReaderWriter here, checks it for
        // non-null, and immediately releases it via vtable[0] -- the value is
        // never read either way. A pure allocate-then-free with no observable
        // effect; skipped rather than reproduced.
    }

    return true;
}   // ~Serializer: `if (rw) delete rw`

// ??1SoundEffect@Audio@@UEAA@XZ  0x1800e0880 -- retail's body, moved here from
// the retired deleting_dtor_leaves.cpp door (ENDGAME B.3.9 ENG): restamp (the
// compiler's), then 0x1800516c0 over `_samples` -- the element destroy of every
// Sample's variant table followed by the plain ~Array, which frees and ZEROES
// the header, so the compiler-emitted `_samples.~Array()` afterwards is a
// no-op -- then ~External (the compiler's chain, retail's tail call).
SoundEffect::~SoundEffect() {
    rtcoll::RtColl_ArrayDtorElem_1800516c0(&_samples);
}

}  // namespace Audio

// ============================================================================
// The `_buffers` element serialize walk   0x180053300   (batch D1)
// ============================================================================
// SoundEffect::Serialize above calls this directly (B.3.7). Element {float @+0x00,
// Collection::Array<Sample> @+0x08}, stride 0x18:
//
//   18005333e  kScalarFloat  e+0x00
//   180053344  n = e->samples._size   (e+0x10)
//   18005335a  kScalarInt(&n)                     — a LOCAL, not the field
//   180053360  if (reading) 0x180052e60(&e->samples, n)    SetSize
//   180053379  p = e->samples._data   (e+0x08)
//   180053390  per sample (stride 0x18):
//                 0x180052a50(p+0x00, ser)        ExternalPtr<SoundBuffer>
//                 kScalarFloat p+0x08 / p+0x0c / p+0x10
//
// `ser->readerWriter` is re-read before every virtual call, as retail does.
// 0x180052a50 is the ExternalPtr serialize helper of the sibling batch
// (object/external_ptr_serialize.cpp), called by name (B3R).
extern "C" void RtAudio_SoundEffect_SerializeBuffers(IO::Serializer* ser, int32_t count,
                                                    Audio::SoundEffect::Sample* e) {
    if (count <= 0) return;                                   // 180053300 TEST/JLE

    for (int32_t i = 0; i < count; ++i, ++e) {
        ser->_readerWriter->SerializeFloat(e->startSeconds, nullptr);

        int32_t n = e->variants._size;                         // 180053344
        ser->_readerWriter->SerializeInt(n, nullptr);          // 18005335a

        if (IsReading(ser->_stream))                           // 180053360
            rtcoll::RtColl_ArraySetSize_180052e60(&e->variants, n);   // 180053370

        Audio::SoundEffect::Variant* p = e->variants._data;    // 180053379
        for (int32_t k = 0; k < n; ++k, ++p) {                 // 180053390
            // 0x180052a50: ExternalPtr<SoundBuffer>::Serialize, the template's
            // member.
            p->buffer.Serialize(*ser);
            ser->_readerWriter->SerializeFloat(p->pitchLow, nullptr);
            ser->_readerWriter->SerializeFloat(p->pitchHigh, nullptr);
            ser->_readerWriter->SerializeFloat(p->volume, nullptr);
        }
    }
}
